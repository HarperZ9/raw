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
#include "gtao_precision.h"

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
#include "inputs.h"
#include "ingame.h"

static void Log(const char* fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    vprintf(fmt, a);
    va_end(a);
    printf("\n");
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
