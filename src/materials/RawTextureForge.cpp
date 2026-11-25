// RawTextureForge.cpp: Zero-External-Asset 4D Torus-Mapped Seamless Procedural PBR Texture Forge (`raw::materials`).
// Original work, MIT.

#include "RawTextureForge.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace raw::materials {
namespace {

constexpr float kTwoPi = 6.28318530717958647692f;

inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float Smoothstep(float e0, float e1, float x) {
    const float t = Clamp01((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

inline uint8_t FloatToU8(float v) {
    const float c = Clamp01(v) * 255.0f + 0.5f;
    return static_cast<uint8_t>(c > 255.0f ? 255.0f : (c < 0.0f ? 0.0f : c));
}

inline float SrgbToLinear(float c) {
    return (c <= 0.04045f) ? (c / 12.92f) : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

inline float LinearToSrgb(float c) {
    c = Clamp01(c);
    return (c <= 0.0031308f) ? (12.92f * c) : (1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f);
}

inline bool IsPowerOfTwo(uint32_t v) {
    return v != 0u && (v & (v - 1u)) == 0u;
}

// Fast deterministic integer hash for 4D lattice and periodic 2D Worley grids.
inline uint32_t HashU32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

inline float Hash4To01(int32_t x, int32_t y, int32_t z, int32_t w, uint32_t seed) {
    uint32_t h = seed ^ (uint32_t(x) * 374761393u) ^ (uint32_t(y) * 668265263u) ^
                 (uint32_t(z) * 2147483647u) ^ (uint32_t(w) * 1274126177u);
    return float(HashU32(h) & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

inline float Quintic(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// 4D Quintic Value Noise: smooth C2 noise in R^4.
float ValueNoise4D(float x, float y, float z, float w, uint32_t seed) {
    const int32_t ix = static_cast<int32_t>(std::floor(x));
    const int32_t iy = static_cast<int32_t>(std::floor(y));
    const int32_t iz = static_cast<int32_t>(std::floor(z));
    const int32_t iw = static_cast<int32_t>(std::floor(w));
    const float fx = Quintic(x - float(ix));
    const float fy = Quintic(y - float(iy));
    const float fz = Quintic(z - float(iz));
    const float fw = Quintic(w - float(iw));

    float cW[2]{};
    for (int dw = 0; dw < 2; ++dw) {
        float cZ[2]{};
        for (int dz = 0; dz < 2; ++dz) {
            float cY[2]{};
            for (int dy = 0; dy < 2; ++dy) {
                const float v0 = Hash4To01(ix,     iy + dy, iz + dz, iw + dw, seed);
                const float v1 = Hash4To01(ix + 1, iy + dy, iz + dz, iw + dw, seed);
                cY[dy] = Lerp(v0, v1, fx);
            }
            cZ[dz] = Lerp(cY[0], cY[1], fy);
        }
        cW[dw] = Lerp(cZ[0], cZ[1], fz);
    }
    return Lerp(cW[0], cW[1], fw);
}

// 4D Torus-Mapped Seamless Noise: strictly periodic in u in [0,1) and v in [0,1)!
// `freqU` and `freqV` control independent periodic frequencies along U and V (anisotropic support!).
float TorusNoise2D(float u, float v, float freqU, float freqV, uint32_t seed) {
    const float au = u * kTwoPi;
    const float av = v * kTwoPi;
    const float ru = freqU / kTwoPi;
    const float rv = freqV / kTwoPi;
    return ValueNoise4D(std::cos(au) * ru, std::sin(au) * ru,
                        std::cos(av) * rv, std::sin(av) * rv, seed);
}

float TorusFbm2D(float u, float v, float freqU, float freqV, uint32_t seed, int octaves = 4) {
    float sum = 0.0f;
    float amp = 0.5f;
    float norm = 0.0f;
    float fu = freqU;
    float fv = freqV;
    for (int o = 0; o < octaves; ++o) {
        sum += amp * TorusNoise2D(u, v, fu, fv, seed + uint32_t(o) * 1013u);
        norm += amp;
        fu *= 2.0f;
        fv *= 2.0f;
        amp *= 0.5f;
    }
    return norm > 0.0f ? (sum / norm) : 0.5f;
}

float TorusRidged2D(float u, float v, float freqU, float freqV, uint32_t seed, int octaves = 4) {
    float sum = 0.0f;
    float amp = 0.5f;
    float norm = 0.0f;
    float fu = freqU;
    float fv = freqV;
    for (int o = 0; o < octaves; ++o) {
        const float n = TorusNoise2D(u, v, fu, fv, seed + uint32_t(o) * 1619u);
        const float r = 1.0f - std::fabs(n * 2.0f - 1.0f);
        sum += amp * r * r;
        norm += amp;
        fu *= 2.0f;
        fv *= 2.0f;
        amp *= 0.5f;
    }
    return norm > 0.0f ? (sum / norm) : 0.0f;
}

struct PeriodicWorleySample {
    float    f1       = 1.0f;
    float    edgeDist = 1.0f; // F2 - F1 (0 on cell border!)
    float    cellRand = 0.5f; // Deterministic per-cell [0..1]
};

inline int32_t WrapMod(int32_t a, int32_t m) {
    const int32_t r = a % m;
    return r < 0 ? (r + m) : r;
}

// Strictly periodic 2D Worley cellular noise on an integer (cellsX x cellsY) toroidal grid!
PeriodicWorleySample PeriodicWorley2D(float u, float v, int32_t cellsX, int32_t cellsY,
                                      float jitter01, uint32_t seed, bool hexStagger = false) {
    const float px = u * float(cellsX);
    const float py = v * float(cellsY);
    const int32_t ix = static_cast<int32_t>(std::floor(px));
    const int32_t iy = static_cast<int32_t>(std::floor(py));

    float d1 = 1.0e9f;
    float d2 = 1.0e9f;
    float bestRand = 0.5f;

    for (int32_t dy = -1; dy <= 1; ++dy) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            const int32_t gx = ix + dx;
            const int32_t gy = iy + dy;
            const int32_t wx = WrapMod(gx, cellsX);
            const int32_t wy = WrapMod(gy, cellsY);
            const uint32_t h1 = HashU32(seed ^ (uint32_t(wx) * 374761393u) ^ (uint32_t(wy) * 668265263u));
            const uint32_t h2 = HashU32(h1 ^ 0x9E3779B9u);
            const float jx = (float(h1 & 0xFFFFu) * (1.0f / 65535.0f) - 0.5f) * jitter01;
            const float jy = (float(h2 & 0xFFFFu) * (1.0f / 65535.0f) - 0.5f) * jitter01;
            const float staggerX = (hexStagger && ((wy & 1) != 0)) ? 0.5f : 0.0f;

            const float fx = float(gx) + 0.5f + staggerX + jx;
            const float fy = float(gy) + 0.5f + jy;
            const float rx = fx - px;
            const float ry = fy - py;
            const float d = std::sqrt(rx * rx + ry * ry);
            if (d < d1) {
                d2 = d1;
                d1 = d;
                bestRand = float((h1 >> 16) & 0xFFFFu) * (1.0f / 65535.0f);
            } else if (d < d2) {
                d2 = d;
            }
        }
    }
    return {d1, std::max(0.0f, d2 - d1), bestRand};
}

struct PbrPixel {
    float albedo[3] = {0.5f, 0.5f, 0.5f}; // sRGB [0..1]
    float alpha     = 1.0f;
    float height    = 0.5f;               // Continuous height [0..1] for Scharr normal map
    float roughness = 0.6f;
    float ao        = 1.0f;
    float extra     = 0.0f;               // Emissive (magma) or Metallic (steel)
};

PbrPixel EvalProceduralPixel(ProceduralTextureKind kind, float u, float v, uint32_t seed) {
    PbrPixel px{};
    switch (kind) {
        case ProceduralTextureKind::NordicGraniteStrata: {
            const float warp = (TorusFbm2D(u, v, 3.0f, 3.0f, seed + 11u, 3) - 0.5f) * 0.22f;
            const float strata = 0.5f + 0.5f * std::sin((v + warp) * kTwoPi * 5.0f);
            const float macro = TorusFbm2D(u, v, 4.0f, 4.0f, seed + 23u, 4);
            const float speck = TorusNoise2D(u, v, 24.0f, 24.0f, seed + 37u);
            const float crack = TorusRidged2D(u, v, 5.0f, 5.0f, seed + 53u, 3);
            const float fissure = Smoothstep(0.68f, 0.92f, crack);
            const float h = Clamp01(0.45f * strata + 0.40f * macro + 0.15f * speck - 0.35f * fissure);
            const float tone = Lerp(0.24f, 0.58f, 0.55f * strata + 0.45f * macro) * (0.86f + 0.26f * speck);
            px.albedo[0] = Clamp01(tone * 0.98f * (1.0f - 0.48f * fissure));
            px.albedo[1] = Clamp01(tone * 1.00f * (1.0f - 0.48f * fissure));
            px.albedo[2] = Clamp01(tone * 1.04f * (1.0f - 0.44f * fissure));
            px.height    = h;
            px.roughness = Lerp(0.64f, 0.88f, 0.5f * (1.0f - speck) + 0.5f * fissure);
            px.ao        = Clamp01(0.55f + 0.45f * h - 0.30f * fissure);
            break;
        }
        case ProceduralTextureKind::GlacialStratifiedIce: {
            const float warp = (TorusFbm2D(u, v, 3.0f, 2.0f, seed + 17u, 3) - 0.5f) * 0.25f;
            const float band = 0.5f + 0.5f * std::sin((v * 4.0f + warp) * kTwoPi);
            const float bubble = TorusNoise2D(u, v, 18.0f, 8.0f, seed + 41u);
            const float frost = Smoothstep(0.60f, 0.90f, TorusFbm2D(u, v, 6.0f, 6.0f, seed + 67u, 4));
            const float h = Clamp01(0.50f + 0.22f * band + 0.18f * frost + 0.10f * bubble);
            px.albedo[0] = Clamp01(Lerp(0.30f, 0.84f, 0.45f * band + 0.55f * frost));
            px.albedo[1] = Clamp01(Lerp(0.64f, 0.92f, 0.45f * band + 0.55f * frost));
            px.albedo[2] = Clamp01(Lerp(0.80f, 0.97f, 0.35f * band + 0.65f * frost));
            px.height    = h;
            px.roughness = Lerp(0.10f, 0.36f, frost * 0.75f + bubble * 0.25f);
            px.ao        = Clamp01(0.75f + 0.25f * h);
            break;
        }
        case ProceduralTextureKind::ForgedBrushedSteel: {
            // Anisotropic horizontal brush streaks (high freqV, low freqU!) + subtle forged hammer dimples
            const float streak = TorusFbm2D(u, v, 3.0f, 28.0f, seed + 13u, 3);
            const float fine   = TorusNoise2D(u, v, 6.0f, 48.0f, seed + 29u);
            const auto  dimple = PeriodicWorley2D(u, v, 6, 6, 0.72f, seed + 71u, false);
            const float dent   = Smoothstep(0.45f, 0.05f, dimple.f1) * 0.24f;
            const float h      = Clamp01(0.60f + 0.14f * (streak - 0.5f) + 0.06f * (fine - 0.5f) - dent);
            const float metal  = Lerp(0.56f, 0.78f, 0.65f * streak + 0.35f * fine) * (1.0f - 0.15f * dent);
            px.albedo[0] = Clamp01(metal * 0.96f);
            px.albedo[1] = Clamp01(metal * 0.98f);
            px.albedo[2] = Clamp01(metal * 1.03f);
            px.height    = h;
            px.roughness = Lerp(0.16f, 0.38f, (1.0f - streak) * 0.65f + dent * 1.4f);
            px.ao        = Clamp01(0.72f + 0.28f * h);
            px.extra     = 0.96f; // High metallic mask
            break;
        }
        case ProceduralTextureKind::BorealPineBark: {
            // Deep longitudinal bark furrows along V with organic warped crevices & subtle north-moss flecks
            const float warpU = (TorusFbm2D(u, v, 4.0f, 2.0f, seed + 19u, 3) - 0.5f) * 0.10f;
            const float uW = u + warpU - std::floor(u + warpU);
            const float ridge = TorusRidged2D(uW, v, 10.0f, 2.5f, seed + 31u, 4);
            const float plate = TorusFbm2D(uW, v, 6.0f, 3.0f, seed + 47u, 3);
            const float mossMask = Smoothstep(0.64f, 0.84f, TorusFbm2D(u, v, 5.0f, 5.0f, seed + 83u, 3)) *
                                   Smoothstep(0.55f, 0.25f, ridge);
            const float h = Clamp01(0.68f * ridge + 0.32f * plate);
            const float rBark = Lerp(0.14f, 0.38f, h);
            const float gBark = Lerp(0.09f, 0.25f, h);
            const float bBark = Lerp(0.07f, 0.18f, h);
            px.albedo[0] = Clamp01(Lerp(rBark, 0.18f, mossMask));
            px.albedo[1] = Clamp01(Lerp(gBark, 0.34f, mossMask));
            px.albedo[2] = Clamp01(Lerp(bBark, 0.12f, mossMask));
            px.height    = h;
            px.roughness = Lerp(0.72f, 0.94f, 1.0f - 0.4f * h);
            px.ao        = Clamp01(0.32f + 0.68f * h);
            break;
        }
        case ProceduralTextureKind::HexagonalBasalt: {
            // Exact periodic staggered hexagonal column grid with crisp beveled joints!
            const float hu = u + 0.17f - std::floor(u + 0.17f);
            const float hv = v + 0.23f - std::floor(v + 0.23f);
            const auto hex = PeriodicWorley2D(hu, hv, 6, 6, 0.06f, seed + 59u, true);
            const float bevel = Smoothstep(0.02f, 0.14f, hex.edgeDist);
            const float pore  = TorusFbm2D(u, v, 14.0f, 14.0f, seed + 73u, 3);
            const float h     = Clamp01(0.18f + 0.64f * bevel + 0.12f * hex.cellRand + 0.08f * pore);
            const float base  = Lerp(0.11f, 0.28f, 0.55f * bevel + 0.30f * hex.cellRand + 0.15f * pore);
            px.albedo[0] = Clamp01(base * 0.95f);
            px.albedo[1] = Clamp01(base * 0.98f);
            px.albedo[2] = Clamp01(base * 1.06f);
            px.height    = h;
            px.roughness = Lerp(0.34f, 0.58f, 1.0f - 0.5f * bevel + 0.3f * pore);
            px.ao        = Clamp01(0.28f + 0.72f * bevel);
            break;
        }
        case ProceduralTextureKind::VolcanicMagmaCrust: {
            const float wU = u + (TorusFbm2D(u, v, 4.0f, 4.0f, seed + 101u, 2) - 0.5f) * 0.08f;
            const float wV = v + (TorusFbm2D(u, v, 4.0f, 4.0f, seed + 131u, 2) - 0.5f) * 0.08f;
            const auto plate = PeriodicWorley2D(wU - std::floor(wU), wV - std::floor(wV), 5, 5, 0.72f, seed + 149u, false);
            const float crackGlow = 1.0f - Smoothstep(0.02f, 0.22f, plate.edgeDist);
            const float crustGrain = TorusFbm2D(u, v, 12.0f, 12.0f, seed + 163u, 3);
            const float h = Clamp01((1.0f - crackGlow) * (0.55f + 0.40f * crustGrain) + 0.08f * crackGlow);
            const float dark = Lerp(0.08f, 0.19f, crustGrain);
            px.albedo[0] = Clamp01(Lerp(dark * 1.05f, 1.00f, crackGlow));
            px.albedo[1] = Clamp01(Lerp(dark * 0.98f, 0.42f + 0.45f * crackGlow, crackGlow));
            px.albedo[2] = Clamp01(Lerp(dark * 0.95f, 0.08f + 0.25f * crackGlow * crackGlow, crackGlow));
            px.height    = h;
            px.roughness = Lerp(0.84f, 0.24f, crackGlow);
            px.ao        = Clamp01(0.45f + 0.55f * (1.0f - crackGlow));
            px.extra     = crackGlow; // Emissive magma intensity
            break;
        }
        case ProceduralTextureKind::AlpineMossMeadow: {
            const float macro  = TorusFbm2D(u, v, 4.0f, 4.0f, seed + 173u, 4);
            const float fibers = TorusNoise2D(u, v, 20.0f, 20.0f, seed + 191u);
            const float clover = Smoothstep(0.62f, 0.88f, TorusFbm2D(u, v, 8.0f, 8.0f, seed + 211u, 3));
            const float h      = Clamp01(0.45f * macro + 0.35f * fibers + 0.20f * clover);
            px.albedo[0] = Clamp01(Lerp(0.14f, 0.34f, 0.5f * macro + 0.5f * clover));
            px.albedo[1] = Clamp01(Lerp(0.26f, 0.50f, 0.5f * macro + 0.3f * fibers + 0.2f * clover));
            px.albedo[2] = Clamp01(Lerp(0.10f, 0.20f, 0.6f * macro + 0.4f * fibers));
            px.height    = h;
            px.roughness = Lerp(0.76f, 0.94f, 1.0f - 0.5f * clover);
            px.ao        = Clamp01(0.48f + 0.52f * h);
            break;
        }
        case ProceduralTextureKind::BraidedRiverPebbles:
        default: {
            const auto peb = PeriodicWorley2D(u, v, 8, 8, 0.82f, seed + 223u, false);
            const float dome = Smoothstep(0.01f, 0.22f, peb.edgeDist);
            const float grit = TorusFbm2D(u, v, 16.0f, 16.0f, seed + 239u, 3);
            const float h    = Clamp01(0.12f + 0.78f * std::sqrt(dome) + 0.10f * grit);
            const float stoneVal = Lerp(0.22f, 0.58f, peb.cellRand) * (0.85f + 0.25f * grit);
            const float sandVal  = Lerp(0.18f, 0.28f, grit);
            px.albedo[0] = Clamp01(Lerp(sandVal * 1.05f, stoneVal * (0.94f + 0.10f * peb.cellRand), dome));
            px.albedo[1] = Clamp01(Lerp(sandVal * 0.98f, stoneVal * 0.96f, dome));
            px.albedo[2] = Clamp01(Lerp(sandVal * 0.88f, stoneVal * (1.02f - 0.08f * peb.cellRand), dome));
            px.height    = h;
            px.roughness = Lerp(0.58f, 0.20f, dome * 0.85f); // Smooth wet river stones, matte sand gaps
            px.ao        = Clamp01(0.32f + 0.68f * dome);
            break;
        }
    }
    return px;
}

}  // namespace

const char* GetProceduralTextureKindName(ProceduralTextureKind kind) {
    switch (kind) {
        case ProceduralTextureKind::NordicGraniteStrata:  return "NordicGraniteStrata";
        case ProceduralTextureKind::GlacialStratifiedIce: return "GlacialStratifiedIce";
        case ProceduralTextureKind::ForgedBrushedSteel:   return "ForgedBrushedSteel";
        case ProceduralTextureKind::BorealPineBark:       return "BorealPineBark";
        case ProceduralTextureKind::HexagonalBasalt:      return "HexagonalBasalt";
        case ProceduralTextureKind::VolcanicMagmaCrust:   return "VolcanicMagmaCrust";
        case ProceduralTextureKind::AlpineMossMeadow:     return "AlpineMossMeadow";
        case ProceduralTextureKind::BraidedRiverPebbles:  return "BraidedRiverPebbles";
        default:                                          return "UnknownProceduralTexture";
    }
}

TextureMipChain BuildMipChainFromRgba8(uint32_t width, uint32_t height, const std::vector<uint8_t>& mip0Rgba8,
                                       bool isSrgb, bool isNormalMap) {
    TextureMipChain chain{};
    chain.isSrgb      = isSrgb;
    chain.isNormalMap = isNormalMap;
    if (width == 0u || height == 0u || mip0Rgba8.size() < size_t(width) * height * 4u) return chain;

    chain.mips.push_back({width, height, mip0Rgba8});

    while (chain.mips.back().width > 1u || chain.mips.back().height > 1u) {
        const TextureMipLevel& prev = chain.mips.back();
        const uint32_t nw = std::max(1u, prev.width >> 1u);
        const uint32_t nh = std::max(1u, prev.height >> 1u);
        TextureMipLevel next{};
        next.width  = nw;
        next.height = nh;
        next.rgba8.resize(size_t(nw) * nh * 4u, 255u);

        for (uint32_t y = 0u; y < nh; ++y) {
            const uint32_t y0 = std::min(prev.height - 1u, y * 2u);
            const uint32_t y1 = std::min(prev.height - 1u, y * 2u + 1u);
            for (uint32_t x = 0u; x < nw; ++x) {
                const uint32_t x0 = std::min(prev.width - 1u, x * 2u);
                const uint32_t x1 = std::min(prev.width - 1u, x * 2u + 1u);

                const uint8_t* p00 = prev.rgba8.data() + (size_t(y0) * prev.width + x0) * 4u;
                const uint8_t* p10 = prev.rgba8.data() + (size_t(y0) * prev.width + x1) * 4u;
                const uint8_t* p01 = prev.rgba8.data() + (size_t(y1) * prev.width + x0) * 4u;
                const uint8_t* p11 = prev.rgba8.data() + (size_t(y1) * prev.width + x1) * 4u;
                uint8_t* dst = next.rgba8.data() + (size_t(y) * nw + x) * 4u;

                if (isNormalMap) {
                    const uint8_t* taps[4] = {p00, p10, p01, p11};
                    float nx = 0.0f, ny = 0.0f, nz = 0.0f;
                    for (const uint8_t* t : taps) {
                        nx += (float(t[0]) * (1.0f / 255.0f)) * 2.0f - 1.0f;
                        ny += (float(t[1]) * (1.0f / 255.0f)) * 2.0f - 1.0f;
                        nz += (float(t[2]) * (1.0f / 255.0f)) * 2.0f - 1.0f;
                    }
                    const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
                    if (len > 1.0e-5f) { nx /= len; ny /= len; nz /= len; }
                    else               { nx = 0.0f; ny = 0.0f; nz = 1.0f; }
                    dst[0] = FloatToU8(nx * 0.5f + 0.5f);
                    dst[1] = FloatToU8(ny * 0.5f + 0.5f);
                    dst[2] = FloatToU8(nz * 0.5f + 0.5f);
                    dst[3] = 255u;
                } else if (isSrgb) {
                    for (int ch = 0; ch < 3; ++ch) {
                        const float avgLin = 0.25f * (
                            SrgbToLinear(float(p00[ch]) / 255.0f) + SrgbToLinear(float(p10[ch]) / 255.0f) +
                            SrgbToLinear(float(p01[ch]) / 255.0f) + SrgbToLinear(float(p11[ch]) / 255.0f));
                        dst[ch] = FloatToU8(LinearToSrgb(avgLin));
                    }
                    dst[3] = static_cast<uint8_t>((uint32_t(p00[3]) + p10[3] + p01[3] + p11[3] + 2u) >> 2u);
                } else {
                    for (int ch = 0; ch < 4; ++ch) {
                        dst[ch] = static_cast<uint8_t>((uint32_t(p00[ch]) + p10[ch] + p01[ch] + p11[ch] + 2u) >> 2u);
                    }
                }
            }
        }
        chain.mips.push_back(std::move(next));
    }
    return chain;
}

bool GenerateProceduralPbrTextureSet(ProceduralTextureKind kind, uint32_t resolution, uint32_t seed,
                                     ProceduralPbrTextureSet* outSet) {
    if (!outSet) return false;
    if (static_cast<uint32_t>(kind) >= static_cast<uint32_t>(ProceduralTextureKind::Count)) return false;
    if (!IsPowerOfTwo(resolution) || resolution < 16u || resolution > 2048u) return false;

    const size_t pixelCount = size_t(resolution) * resolution;
    std::vector<uint8_t> albedo0(pixelCount * 4u, 255u);
    std::vector<uint8_t> normal0(pixelCount * 4u, 255u);
    std::vector<uint8_t> ormh0(pixelCount * 4u, 255u);
    std::vector<float>   heights(pixelCount, 0.5f);

    const float invRes = 1.0f / float(resolution);
    const uint32_t matSeed = seed ^ (static_cast<uint32_t>(kind) * 0x9E3779B9u + 0x517CC1B7u);

    for (uint32_t y = 0u; y < resolution; ++y) {
        const float v = float(y) * invRes;
        for (uint32_t x = 0u; x < resolution; ++x) {
            const float u = float(x) * invRes;
            const PbrPixel px = EvalProceduralPixel(kind, u, v, matSeed);
            const size_t idx = size_t(y) * resolution + x;
            heights[idx] = px.height;

            albedo0[idx * 4u + 0u] = FloatToU8(px.albedo[0]);
            albedo0[idx * 4u + 1u] = FloatToU8(px.albedo[1]);
            albedo0[idx * 4u + 2u] = FloatToU8(px.albedo[2]);
            albedo0[idx * 4u + 3u] = FloatToU8(px.alpha);

            ormh0[idx * 4u + 0u] = FloatToU8(px.roughness);
            ormh0[idx * 4u + 1u] = FloatToU8(px.ao);
            ormh0[idx * 4u + 2u] = FloatToU8(px.height);
            ormh0[idx * 4u + 3u] = FloatToU8(px.extra);
        }
    }

    // Compute exact wrapped Scharr 3x3 tangent-space normals from the seamless continuous heightfield!
    const float normalStrength = (kind == ProceduralTextureKind::GlacialStratifiedIce) ? 1.8f
                               : (kind == ProceduralTextureKind::ForgedBrushedSteel)  ? 1.4f
                               : 3.4f;
    const uint32_t mask = resolution - 1u;
    for (uint32_t y = 0u; y < resolution; ++y) {
        const uint32_t ym = (y - 1u) & mask;
        const uint32_t yp = (y + 1u) & mask;
        for (uint32_t x = 0u; x < resolution; ++x) {
            const uint32_t xm = (x - 1u) & mask;
            const uint32_t xp = (x + 1u) & mask;

            const float h00 = heights[size_t(ym) * resolution + xm];
            const float h10 = heights[size_t(ym) * resolution + x];
            const float h20 = heights[size_t(ym) * resolution + xp];
            const float h01 = heights[size_t(y)  * resolution + xm];
            const float h21 = heights[size_t(y)  * resolution + xp];
            const float h02 = heights[size_t(yp) * resolution + xm];
            const float h12 = heights[size_t(yp) * resolution + x];
            const float h22 = heights[size_t(yp) * resolution + xp];

            // Scharr operator (superior rotational symmetry vs Sobel)
            const float dhdx = (3.0f * (h20 - h00) + 10.0f * (h21 - h01) + 3.0f * (h22 - h02)) * (1.0f / 32.0f);
            const float dhdy = (3.0f * (h02 - h00) + 10.0f * (h12 - h10) + 3.0f * (h22 - h20)) * (1.0f / 32.0f);

            float nx = -dhdx * normalStrength;
            float ny = -dhdy * normalStrength;
            float nz = 1.0f;
            const float invL = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
            nx *= invL; ny *= invL; nz *= invL;

            const size_t idx = size_t(y) * resolution + x;
            normal0[idx * 4u + 0u] = FloatToU8(nx * 0.5f + 0.5f);
            normal0[idx * 4u + 1u] = FloatToU8(ny * 0.5f + 0.5f);
            normal0[idx * 4u + 2u] = FloatToU8(nz * 0.5f + 0.5f);
            normal0[idx * 4u + 3u] = 255u;
        }
    }

    outSet->kind       = kind;
    outSet->name       = GetProceduralTextureKindName(kind);
    outSet->resolution = resolution;
    outSet->seed       = seed;
    outSet->albedo     = BuildMipChainFromRgba8(resolution, resolution, albedo0, true, false);
    outSet->normal     = BuildMipChainFromRgba8(resolution, resolution, normal0, false, true);
    outSet->ormh       = BuildMipChainFromRgba8(resolution, resolution, ormh0, false, false);
    return !outSet->albedo.mips.empty() && !outSet->normal.mips.empty() && !outSet->ormh.mips.empty();
}

std::vector<uint8_t> SerializeDdsRgba8Bytes(const TextureMipChain& chain) {
    if (chain.mips.empty() || chain.Width() == 0u || chain.Height() == 0u) return {};

    size_t totalPixelsBytes = 0u;
    for (const auto& m : chain.mips) totalPixelsBytes += m.rgba8.size();

    std::vector<uint8_t> out(148u, 0u);
    out.reserve(148u + totalPixelsBytes);

    auto put32 = [&](size_t off, uint32_t v) { std::memcpy(out.data() + off, &v, 4u); };
    put32(0u, 0x20534444u);                         // "DDS "
    put32(4u, 124u);                                // DDS_HEADER::dwSize
    put32(8u, 0x0002100Fu);                         // CAPS | HEIGHT | WIDTH | PITCH | PIXELFORMAT | MIPMAPCOUNT
    put32(12u, chain.Height());
    put32(16u, chain.Width());
    put32(20u, chain.Width() * 4u);                 // pitch
    put32(28u, chain.MipCount());                   // dwMipMapCount
    put32(76u, 32u);                                // DDS_PIXELFORMAT::dwSize
    put32(80u, 0x4u);                               // DDPF_FOURCC
    put32(84u, 0x30315844u);                        // "DX10"
    put32(108u, 0x00401008u);                       // DDSCAPS_COMPLEX | MIPMAP | TEXTURE

    // DDS_HEADER_DXT10 (20 bytes at offset 128)
    put32(128u, chain.isSrgb ? 29u : 28u);          // DXGI_FORMAT_R8G8B8A8_UNORM_SRGB (29) / UNORM (28)
    put32(132u, 3u);                                // D3D10_RESOURCE_DIMENSION_TEXTURE2D
    put32(136u, 0u);                                // miscFlag
    put32(140u, 1u);                                // arraySize
    put32(144u, 0u);                                // miscFlags2

    for (const auto& m : chain.mips) {
        out.insert(out.end(), m.rgba8.begin(), m.rgba8.end());
    }
    return out;
}

bool WriteDdsRgba8(const std::string& path, const TextureMipChain& chain) {
    if (path.empty()) return false;
    const auto bytes = SerializeDdsRgba8Bytes(chain);
    if (bytes.empty()) return false;
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return f.good();
}

bool WriteTextureMip0ToBmp(const std::string& path, const TextureMipChain& chain) {
    if (path.empty() || chain.mips.empty()) return false;
    const TextureMipLevel& m = chain.mips[0];
    if (m.width == 0u || m.height == 0u || m.rgba8.size() < size_t(m.width) * m.height * 4u) return false;

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    const uint32_t rowBytes = (m.width * 3u + 3u) & ~3u;
    const uint32_t imgBytes = rowBytes * m.height;
    const uint32_t fileBytes = 54u + imgBytes;
    uint8_t hdr[54]{};
    hdr[0] = 'B'; hdr[1] = 'M';
    std::memcpy(hdr + 2, &fileBytes, 4u);
    const uint32_t pixOff = 54u;
    std::memcpy(hdr + 10, &pixOff, 4u);
    const uint32_t dibSize = 40u;
    std::memcpy(hdr + 14, &dibSize, 4u);
    const int32_t w = static_cast<int32_t>(m.width);
    const int32_t h = -static_cast<int32_t>(m.height);
    std::memcpy(hdr + 18, &w, 4u);
    std::memcpy(hdr + 22, &h, 4u);
    const uint16_t planes = 1u, bpp = 24u;
    std::memcpy(hdr + 26, &planes, 2u);
    std::memcpy(hdr + 28, &bpp, 2u);
    std::memcpy(hdr + 34, &imgBytes, 4u);
    f.write(reinterpret_cast<const char*>(hdr), 54);

    std::vector<uint8_t> row(rowBytes, 0u);
    for (uint32_t y = 0u; y < m.height; ++y) {
        const uint8_t* srcRow = m.rgba8.data() + size_t(y) * m.width * 4u;
        for (uint32_t x = 0u; x < m.width; ++x) {
            row[x * 3u + 0u] = srcRow[x * 4u + 2u];
            row[x * 3u + 1u] = srcRow[x * 4u + 1u];
            row[x * 3u + 2u] = srcRow[x * 4u + 0u];
        }
        f.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(rowBytes));
    }
    return f.good();
}

TileabilityMetrics MeasureSeamlessTileability(const TextureMipLevel& mip0) {
    TileabilityMetrics m{};
    if (mip0.width < 2u || mip0.height < 2u || mip0.rgba8.size() < size_t(mip0.width) * mip0.height * 4u) {
        return m;
    }
    double sumX = 0.0, sumY = 0.0;
    for (uint32_t y = 0u; y < mip0.height; ++y) {
        const uint8_t* pL = mip0.rgba8.data() + (size_t(y) * mip0.width + 0u) * 4u;
        const uint8_t* pR = mip0.rgba8.data() + (size_t(y) * mip0.width + (mip0.width - 1u)) * 4u;
        const float d = (std::fabs(float(pL[0]) - float(pR[0])) +
                         std::fabs(float(pL[1]) - float(pR[1])) +
                         std::fabs(float(pL[2]) - float(pR[2]))) * (1.0f / 3.0f);
        m.maxEdgeDiffX = std::max(m.maxEdgeDiffX, d);
        sumX += d;
    }
    for (uint32_t x = 0u; x < mip0.width; ++x) {
        const uint8_t* pT = mip0.rgba8.data() + (size_t(0u) * mip0.width + x) * 4u;
        const uint8_t* pB = mip0.rgba8.data() + (size_t(mip0.height - 1u) * mip0.width + x) * 4u;
        const float d = (std::fabs(float(pT[0]) - float(pB[0])) +
                         std::fabs(float(pT[1]) - float(pB[1])) +
                         std::fabs(float(pT[2]) - float(pB[2]))) * (1.0f / 3.0f);
        m.maxEdgeDiffY = std::max(m.maxEdgeDiffY, d);
        sumY += d;
    }
    m.meanEdgeDiffX = float(sumX / double(mip0.height));
    m.meanEdgeDiffY = float(sumY / double(mip0.width));
    return m;
}

}  // namespace raw::materials
