#pragma once
// engine_gtao_check inputs: a synthetic scene or captured buffers, the CPU form of
// GamePack.hlsl, and the comparison. Included by main.cpp. Original work, MIT.

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
