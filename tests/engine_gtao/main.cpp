// engine_gtao_check: the Playground engine's GTAO behind the bridge, against
// the engine's own CPU reference, on the same inputs the game provides.
//
// Inputs are a D24S8 depth buffer, the game's RGBA8 normal target and an RGBA8
// frame: either a synthetic scene (default) or the buffers of a captured
// gameplay frame (--depth, --normal, --color, --proj). They go through the
// real path: D3D11 textures, the bridge's depth and normal capture, the
// engine pass on D3D12, and back.
//
// Checks (exit code 0 only when all pass):
//   1. what crossed the bridge, read on D3D12, equals the D3D11 inputs
//   2. the engine's visibility matches raw::post::gtao on the same G-buffer
//      at the engine's own bound (1e-3 a pixel on 99.9 % of drawn pixels)
//   3. CONTROL: the same comparison against a reference with another radius fails
//   4. mode 0 passes the frame through unchanged
//   5. GPU timestamps from the RHI read back
//
// Original work, MIT.
#include "../bridge_roundtrip/fixtures.h"
#include "raw/renderer/post.hpp"

#include <chrono>
#include <cstdio>
#include <string>

using namespace raw::bridge;
using fx::ComPtr;

static int g_failed = 0;
static void Check(bool ok, const char* what)
{
    printf("%s %s\n", ok ? "[PASS]" : "[FAIL]", what);
    if (!ok) ++g_failed;
}
static void Log(const char* fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    vprintf(fmt, a);
    va_end(a);
    printf("\n");
}

struct Inputs
{
    UINT w = 0, h = 0;
    std::vector<uint32_t> depth, normal, colour;   // D24S8 words, RGBA8 texels, RGBA8 texels
    float A = 1.00004f, B = 15.00064f, sx = 1.19175f, sy = 2.11867f;   // the game's projection terms
};

// The .rawtex files the proxy's FrameLedger writes (tools/frame_buffers/rawtex.py).
static bool ReadRawTex(const std::string& path, UINT bytesPerPixel, UINT* w, UINT* h, std::vector<uint32_t>* out)
{
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return false;
    char magic[8];
    uint32_t hdr[10];
    bool ok = fread(magic, 1, 8, f) == 8 && memcmp(magic, "RAWTEX1", 8) == 0 && fread(hdr, 4, 10, f) == 10 &&
              hdr[7] == hdr[0] * bytesPerPixel && bytesPerPixel == 4;
    if (ok) {
        *w = hdr[0]; *h = hdr[1];
        out->resize(size_t(hdr[0]) * hdr[1]);
        ok = fread(out->data(), 4, out->size(), f) == out->size();
    }
    fclose(f);
    return ok;
}

// The game's normal write, as its lighting shaders do it: n.xy / sqrt(8 - 8 n.z) + 0.5.
static uint32_t EncodeNormal(float x, float y, float z)
{
    const float p = std::max(std::sqrt(8.0f - 8.0f * z), 0.001f);
    const auto q = [](float v) { return uint32_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    return q(x / p + 0.5f) | (q(y / p + 0.5f) << 8);
}

// A floor, a back wall and a ball in the game's view space (x right, y up, z forward).
// wallZ > 0 gives one wall facing the camera at that distance and nothing else: with a
// small distance the GTAO radius covers thousands of pixels, the costly case for the pass.
static Inputs Synthetic(UINT w, UINT h, float wallZ = 0)
{
    Inputs in;
    in.w = w; in.h = h;
    in.sy = in.sx * float(w) / float(h);
    in.depth.assign(size_t(w) * h, 0xFFFFFF);
    in.normal.assign(size_t(w) * h, 127u | (127u << 8));
    in.colour.resize(size_t(w) * h);
    const float cx = 0, cy = -20, cz = 220, r = 60;
    for (UINT y = 0; y < h; ++y)
        for (UINT x = 0; x < w; ++x) {
            const size_t i = size_t(y) * w + x;
            in.colour[i] = 0xFF000000u | (160u + ((x / 16 + y / 16) & 1) * 60u) * 0x010101u;
            const float dx = (((x + 0.5f) / w) * 2 - 1) / in.sx, dy = (1 - ((y + 0.5f) / h) * 2) / in.sy;   // ray (dx, dy, 1)
            float best = 1e30f, nx = 0, ny = 0, nz = 0;
            if (wallZ <= 0 && dy < -1e-4f) { const float t = -80.0f / dy; if (t < best) { best = t; nx = 0; ny = 1; nz = 0; } }   // floor y = -80
            { const float t = wallZ > 0 ? wallZ : 500.0f; if (t < best) { best = t; nx = 0; ny = 0; nz = -1; } }                  // wall
            const float a = dx * dx + dy * dy + 1, b = -2 * (dx * cx + dy * cy + cz), c = cx * cx + cy * cy + cz * cz - r * r;
            const float disc = b * b - 4 * a * c;
            if (disc > 0 && wallZ <= 0) {
                const float t = (-b - std::sqrt(disc)) / (2 * a);
                if (t > 0 && t < best) { best = t; nx = (dx * t - cx) / r; ny = (dy * t - cy) / r; nz = (t - cz) / r; }
            }
            const double d = double(in.A) - double(in.B) / double(best);
            in.depth[i] = uint32_t(std::lround(std::clamp(d, 0.0, 1.0) * 16777215.0));
            in.normal[i] = EncodeNormal(nx, ny, nz);
        }
    return in;
}

// GamePack.hlsl on the CPU, in float as the shader computes it.
static raw::post::ViewGBuffer Pack(const Inputs& in)
{
    raw::post::ViewGBuffer v;
    v.w = int(in.w); v.h = int(in.h);
    v.tanHalf = 1.0f / in.sy; v.aspect = in.sy / in.sx; v.nearZ = in.B / in.A;
    const size_t n = size_t(in.w) * in.h;
    v.pos.assign(n * 3, 0.0f); v.nrm.assign(n * 3, 0.0f); v.depth.assign(n, -1.0f);
    const float tanHalf = 1.0f / in.sy, aspect = in.sy / in.sx;
    for (UINT y = 0; y < in.h; ++y)
        for (UINT x = 0; x < in.w; ++x) {
            const size_t i = size_t(y) * in.w + x;
            const float d = float(in.depth[i] & 0xFFFFFF) / 16777215.0f;
            v.nrm[i * 3 + 2] = 1.0f;
            if (d >= 1.0f || in.A - d <= 0.0f) continue;
            const float z = in.B / (in.A - d);
            const float sx = ((float(x) + 0.5f) / float(in.w)) * 2.0f - 1.0f, sy = 1.0f - ((float(y) + 0.5f) / float(in.h)) * 2.0f;
            v.pos[i * 3] = sx * z * tanHalf * aspect; v.pos[i * 3 + 1] = sy * z * tanHalf; v.pos[i * 3 + 2] = -z;
            v.depth[i] = z;
            const uint32_t t = in.normal[i];
            const float ex = float(t & 255u) / 255.0f - 0.5f, ey = float((t >> 8) & 255u) / 255.0f - 0.5f;
            const float s = std::min(ex * ex + ey * ey, 0.25f), k = 4.0f * std::sqrt(1.0f - 4.0f * s);
            float nx = ex * k, ny = ey * k, nz = 8.0f * s - 1.0f;
            const float len = std::max(std::sqrt(nx * nx + ny * ny + nz * nz), 1.0e-6f);
            v.nrm[i * 3] = nx / len; v.nrm[i * 3 + 1] = ny / len; v.nrm[i * 3 + 2] = -nz / len;
        }
    return v;
}

struct Parity { long pixels = 0, outside = 0; double worst = 0; };
static Parity Compare(const std::vector<float>& gpu, const std::vector<double>& ref, const raw::post::ViewGBuffer& v)
{
    Parity p;
    for (size_t i = 0; i < ref.size(); ++i) {
        if (v.depth[i] < 0.0f) continue;
        const double e = std::fabs(double(gpu[i]) - ref[i]);
        ++p.pixels; p.worst = std::max(p.worst, e); p.outside += e > 1e-3;
    }
    return p;
}

// The capture set the proxy writes in game (BridgeHookCapture.cpp): three uint32
// (width, height, row bytes), then tight rows.
template <class T>
static bool ReadRows(const std::string& path, UINT* w, UINT* h, std::vector<T>* out)
{
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return false;
    uint32_t hdr[3];
    bool ok = fread(hdr, 4, 3, f) == 3 && hdr[2] == hdr[0] * 4 && sizeof(T) == 4;
    if (ok) {
        *w = hdr[0]; *h = hdr[1];
        out->resize(size_t(hdr[0]) * hdr[1]);
        ok = fread(out->data(), 4, out->size(), f) == out->size();
    }
    fclose(f);
    return ok;
}

// The visibility the pass computed in the game, against the CPU reference on
// the depth and normals that crossed the bridge in that same frame.
static int InGame(const std::string& dir, Inputs in, const EngineTuning& tune, const std::string& outPath)
{
    std::vector<float> depth, vis;
    UINT w2 = 0, h2 = 0;
    const bool ok = ReadRows(dir + "/d3d12_depth.bin", &in.w, &in.h, &depth) &&
                    ReadRows(dir + "/d3d12_normal.bin", &w2, &h2, &in.normal) && w2 == in.w && h2 == in.h &&
                    ReadRows(dir + "/engine_visibility.bin", &w2, &h2, &vis) && w2 == in.w && h2 == in.h;
    if (!ok) { printf("cannot read the in-game capture set in %s\n", dir.c_str()); return 2; }
    in.depth.resize(depth.size());
    for (size_t i = 0; i < depth.size(); ++i) in.depth[i] = uint32_t(std::lround(double(depth[i]) * 16777215.0));
    printf("inputs: in-game capture set, %ux%u, projection A=%.6f B=%.5f sx=%.5f sy=%.5f, radius %.1f, slices %u, steps %u\n",
           in.w, in.h, in.A, in.B, in.sx, in.sy, tune.radius, tune.slices, tune.steps);
    const raw::post::ViewGBuffer v = Pack(in);
    raw::post::GtaoParams gp;
    gp.radius = tune.radius;
    if (tune.slices) gp.slices = int(tune.slices);
    if (tune.steps) gp.steps = int(tune.steps);
    const Parity p = Compare(vis, raw::post::gtao(v, gp), v);
    const double frac = p.pixels ? double(p.outside) / double(p.pixels) : 1.0;
    printf("       gtao computed in game: %ld drawn pixels, %ld outside 1e-3 (%.5f %%), worst %.3e\n", p.pixels, p.outside, frac * 100.0, p.worst);
    Check(p.pixels > 0 && frac <= 0.001, "the visibility computed in the game matches raw::post::gtao within 1e-3 on 99.9 % of drawn pixels");
    raw::post::GtaoParams wrong = gp;
    wrong.radius = gp.radius * 1.25;
    const Parity pw = Compare(vis, raw::post::gtao(v, wrong), v);
    printf("       control: against a reference with radius x1.25, %ld of %ld outside\n", pw.outside, pw.pixels);
    Check(pw.pixels > 0 && double(pw.outside) / double(pw.pixels) > 0.001, "CONTROL: a reference with another radius is reported as a mismatch");
    if (!outPath.empty()) {
        FILE* f = nullptr;
        if (fopen_s(&f, outPath.c_str(), "w") == 0 && f) {
            fprintf(f, "{\"inputs\":\"in-game capture set\",\"width\":%u,\"height\":%u,\"radius\":%.3f,\"slices\":%d,\"steps\":%d,\n"
                       " \"gtao\":{\"pixels\":%ld,\"outside_1e-3\":%ld,\"worst\":%.4e,\"bound_fraction\":0.001},\n"
                       " \"control_wrong_radius\":{\"pixels\":%ld,\"outside_1e-3\":%ld},\"failed_checks\":%d}\n",
                    in.w, in.h, gp.radius, gp.slices, gp.steps, p.pixels, p.outside, p.worst, pw.pixels, pw.outside, g_failed);
            fclose(f);
        }
    }
    printf("%s: %d check(s) failed\n", g_failed ? "FAILED" : "ALL CHECKS PASSED", g_failed);
    return g_failed ? 1 : 0;
}

static void WriteFile(const std::string& path, const void* data, size_t bytes)
{
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "wb") == 0 && f) { fwrite(data, 1, bytes, f); fclose(f); }
}

int main(int argc, char** argv)
{
    bool warp = false;
    UINT size = 256;
    int frames = 12;
    std::string depthPath, normalPath, colourPath, outPath, dump, ingame;
    Inputs in;
    EngineTuning tune;
    tune.radius = 35.0f;   // game units: half a metre
    bool haveProj = false;
    UINT cropX = 0, cropY = 0, cropW = 0, cropH = 0;
    float wallZ = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--warp") warp = true;
        else if (a == "--size") size = UINT(atoi(next().c_str()));
        else if (a == "--frames") frames = atoi(next().c_str());
        else if (a == "--wall") wallZ = float(atof(next().c_str()));
        else if (a == "--ingame") ingame = next();
        else if (a == "--depth") depthPath = next();
        else if (a == "--normal") normalPath = next();
        else if (a == "--color") colourPath = next();
        else if (a == "--out") outPath = next();
        else if (a == "--dump") dump = next();
        else if (a == "--radius") tune.radius = float(atof(next().c_str()));
        else if (a == "--slices") tune.slices = uint32_t(atoi(next().c_str()));
        else if (a == "--steps") tune.steps = uint32_t(atoi(next().c_str()));
        else if (a == "--crop") { cropX = UINT(atoi(next().c_str())); cropY = UINT(atoi(next().c_str())); cropW = UINT(atoi(next().c_str())); cropH = UINT(atoi(next().c_str())); }
        else if (a == "--proj") {
            in.A = float(atof(next().c_str())); in.B = float(atof(next().c_str()));
            in.sx = float(atof(next().c_str())); in.sy = float(atof(next().c_str()));
            haveProj = true;
        }
    }
    if (!ingame.empty()) {
        if (!haveProj) { printf("--ingame needs --proj A B sx sy\n"); return 2; }
        return InGame(ingame, in, tune, outPath);
    }
    const bool captured = !depthPath.empty();
    if (captured) {
        UINT w2 = 0, h2 = 0;
        const bool ok = haveProj && ReadRawTex(depthPath, 4, &in.w, &in.h, &in.depth) &&
                        ReadRawTex(normalPath, 4, &w2, &h2, &in.normal) && w2 == in.w && h2 == in.h &&
                        ReadRawTex(colourPath, 4, &w2, &h2, &in.colour) && w2 == in.w && h2 == in.h;
        if (!ok) { printf("cannot read the captured buffers (need --depth --normal --color --proj A B sx sy)\n"); return 2; }
        if (cropW && cropH && cropX + cropW <= in.w && cropY + cropH <= in.h) {
            // A window of the captured buffers, treated as its own image: the same
            // content at smaller pixel coordinates.
            Inputs c = in;
            c.w = cropW; c.h = cropH;
            c.sy = c.sx * float(cropW) / float(cropH);
            using Plane = std::vector<uint32_t> Inputs::*;
            for (Plane plane : {&Inputs::depth, &Inputs::normal, &Inputs::colour}) {
                auto& dst = c.*plane; const auto& src = in.*plane;
                dst.resize(size_t(cropW) * cropH);
                for (UINT y = 0; y < cropH; ++y)
                    memcpy(&dst[size_t(y) * cropW], &src[size_t(y + cropY) * in.w + cropX], size_t(cropW) * 4);
            }
            in = c;
        }
    } else {
        const Inputs s = Synthetic(size, size * 9 / 16, wallZ);
        in = s;
    }
    printf("inputs: %s, %ux%u, projection A=%.6f B=%.5f sx=%.5f sy=%.5f, radius %.1f\n",
           captured ? "captured gameplay frame" : "synthetic scene", in.w, in.h, in.A, in.B, in.sx, in.sy, tune.radius);

    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    if (FAILED(D3D11CreateDevice(nullptr, warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &fl, 1,
                                 D3D11_SDK_VERSION, &dev, nullptr, &ctx))) {
        printf("no D3D11 device\n");
        return 2;
    }
    auto colour = fx::MakeTex(dev.Get(), in.w, in.h, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, in.colour.data(), in.w * 4);
    auto dst = fx::MakeTex(dev.Get(), in.w, in.h, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET, nullptr, 0);
    auto depth = fx::MakeTex(dev.Get(), in.w, in.h, DXGI_FORMAT_R24G8_TYPELESS, D3D11_BIND_SHADER_RESOURCE, in.depth.data(), in.w * 4);
    auto normal = fx::MakeTex(dev.Get(), in.w, in.h, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, in.normal.data(), in.w * 4);
    auto depthSrv = fx::MakeSrv(dev.Get(), depth.Get(), DXGI_FORMAT_R24_UNORM_X8_TYPELESS);
    Check(colour && dst && depth && normal && depthSrv, "D3D11 input textures created");

    D3D12Bridge b;
    BridgeConfig cfg;
    cfg.width = in.w; cfg.height = in.h;
    cfg.debugLayer = !captured;
    cfg.normalFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    cfg.enginePass = true;
    cfg.cpuWaitTimeout = 120000;
    Check(b.Initialize(dev.Get(), ctx.Get(), cfg, Log), "bridge initialised with the engine pass on the bridge's D3D12 device");
    if (g_failed) return 1;
    b.TuneEngine(tune);
    CameraConstants cam;
    cam.valid = true;
    cam.proj[0] = in.sx; cam.proj[5] = in.sy; cam.proj[10] = in.A; cam.proj[11] = -in.B; cam.proj[14] = 1.0f;
    cam.tanHalfFovY = 1.0f / in.sy; cam.aspect = in.sy / in.sx; cam.nearZ = in.B / in.A; cam.farZ = in.B / (in.A - 1.0f);
    b.SetCamera(cam);

    PassConstants k;
    k.mode = 1.0f;
    const auto frame = [&]() {
        return b.CaptureDepth(depthSrv.Get()) && b.CaptureNormal(normal.Get()) && b.Execute(colour.Get(), dst.Get(), k);
    };
    const auto first0 = std::chrono::steady_clock::now();
    Check(frame(), "one frame: depth and normals captured, engine pass recorded, D3D11 signalled, pass submitted");
    {
        BridgeTimings t;
        const bool got = b.ReadTimings(0, true, &t);
        printf("       first frame: %.0f ms of wall time until it retired; GPU pass %.1f ms\n",
               std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - first0).count(), got ? t.passMs : -1.0);
    }

    // 1. What crossed the bridge, as D3D12 sees it.
    std::vector<uint8_t> bytes;
    UINT row = 0;
    bool same = b.ReadShared(Shared::Normal, &bytes, &row) && row == in.w * 4 &&
                memcmp(bytes.data(), in.normal.data(), bytes.size()) == 0;
    Check(same, "shared normals read on D3D12 equal the D3D11 normal target, byte for byte");
    long depthOff = 0;
    if (b.ReadShared(Shared::Depth, &bytes, &row) && row == in.w * 4) {
        const float* f = reinterpret_cast<const float*>(bytes.data());
        for (size_t i = 0; i < in.depth.size(); ++i)
            depthOff += std::fabs(double(f[i]) - double(in.depth[i] & 0xFFFFFF) / 16777215.0) > 6.0e-8;   // one float step at 1.0
    } else depthOff = -1;
    printf("       shared depth: %ld texels off by more than one float step\n", depthOff);
    Check(depthOff == 0, "shared depth read on D3D12 equals the 24-bit depth buffer");
    same = b.ReadShared(Shared::ColorIn, &bytes, &row) && memcmp(bytes.data(), in.colour.data(), bytes.size()) == 0;
    Check(same, "shared frame read on D3D12 equals the D3D11 frame");

    // 2 and 3. The engine's visibility against its CPU reference.
    std::vector<float> vis;
    Check(b.ReadEngineVisibility(&vis) && vis.size() == in.depth.size(), "visibility read back from the engine pass");
    if (g_failed) return 1;
    const raw::post::ViewGBuffer v = Pack(in);
    raw::post::GtaoParams gp;
    gp.radius = tune.radius;
    if (tune.slices) gp.slices = int(tune.slices);
    if (tune.steps) gp.steps = int(tune.steps);
    const auto t0 = std::chrono::steady_clock::now();
    const std::vector<double> ref = raw::post::gtao(v, gp);
    const double refSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const Parity p = Compare(vis, ref, v);
    const double frac = p.pixels ? double(p.outside) / double(p.pixels) : 1.0;
    printf("       gtao: %ld drawn pixels, %ld outside 1e-3 (%.5f %%), worst %.3e; CPU reference took %.1f s\n",
           p.pixels, p.outside, frac * 100.0, p.worst, refSeconds);
    Check(p.pixels > 0 && frac <= 0.001, "engine GTAO on the bridge matches raw::post::gtao within 1e-3 on 99.9 % of drawn pixels");
    raw::post::GtaoParams wrong = gp;
    wrong.radius = gp.radius * 1.25;
    const Parity pw = Compare(vis, raw::post::gtao(v, wrong), v);
    printf("       control: against a reference with radius x1.25, %ld of %ld outside (%.2f %%)\n", pw.outside, pw.pixels,
           pw.pixels ? 100.0 * double(pw.outside) / double(pw.pixels) : 0.0);
    Check(pw.pixels > 0 && double(pw.outside) / double(pw.pixels) > 0.001, "CONTROL: a reference with another radius is reported as a mismatch");

    std::vector<uint32_t> composited = fx::ReadBack(dev.Get(), ctx.Get(), dst.Get(), in.w, in.h);
    if (!dump.empty()) {
        WriteFile(dump + "_visibility.f32", vis.data(), vis.size() * 4);
        std::vector<float> r32(ref.begin(), ref.end());
        WriteFile(dump + "_reference.f32", r32.data(), r32.size() * 4);
        WriteFile(dump + "_composite.rgba", composited.data(), composited.size() * 4);
    }

    // 4. Pass-through.
    k.mode = 0.0f;
    frame();
    const fx::Diff id = fx::Compare(fx::ReadBack(dev.Get(), ctx.Get(), dst.Get(), in.w, in.h), in.colour, 0);
    Check(id.mismatches == 0, "mode 0 passes the frame through unchanged");

    // 5. Cost by GPU timestamp, from the RHI.
    k.mode = 1.0f;
    std::vector<double> total, pack, gtao, comp, cost;
    for (int f = 0; f < frames; ++f) {
        frame();
        BridgeTimings t;
        if (b.ReadTimings(b.FramesExecuted() - 1, true, &t) && f >= 2) {
            total.push_back(t.passMs); pack.push_back(t.pass.stageMs[0]); gtao.push_back(t.pass.stageMs[1]);
            comp.push_back(t.pass.stageMs[2]); cost.push_back(t.bridgeCostMs);
        }
    }
    const auto med = [](std::vector<double> x) { if (x.empty()) return 0.0; std::sort(x.begin(), x.end()); return x[x.size() / 2]; };
    printf("       %ux%u: engine pass median %.3f ms (pack %.3f, gtao %.3f, composite %.3f), bridge cost %.3f ms, n=%zu\n",
           in.w, in.h, med(total), med(pack), med(gtao), med(comp), med(cost), total.size());
    Check(!total.empty() && med(gtao) > 0.0, "GPU timestamps read back through the RHI");

    if (!outPath.empty()) {
        FILE* f = nullptr;
        if (fopen_s(&f, outPath.c_str(), "w") == 0 && f) {
            fprintf(f, "{\"inputs\":\"%s\",\"width\":%u,\"height\":%u,\"adapter\":\"%s\",\"radius\":%.3f,\"slices\":%d,\"steps\":%d,\n"
                       " \"gtao\":{\"pixels\":%ld,\"outside_1e-3\":%ld,\"worst\":%.4e,\"bound_fraction\":0.001,\"cpu_reference_s\":%.1f},\n"
                       " \"control_wrong_radius\":{\"pixels\":%ld,\"outside_1e-3\":%ld},\n"
                       " \"shared_depth_off\":%ld,\n"
                       " \"gpu_ms\":{\"pass\":%.4f,\"pack\":%.4f,\"gtao\":%.4f,\"composite\":%.4f,\"bridge_cost\":%.4f,\"frames\":%zu},\n"
                       " \"failed_checks\":%d}\n",
                    captured ? "captured" : "synthetic", in.w, in.h, warp ? "WARP" : "hardware", gp.radius, gp.slices, gp.steps,
                    p.pixels, p.outside, p.worst, refSeconds, pw.pixels, pw.outside, depthOff,
                    med(total), med(pack), med(gtao), med(comp), med(cost), total.size(), g_failed);
            fclose(f);
        }
    }
    printf("%s: %d check(s) failed\n", g_failed ? "FAILED" : "ALL CHECKS PASSED", g_failed);
    return g_failed ? 1 : 0;
}
