// RawAssetForge.cpp: Zero-External-Asset Seamless Procedural PBR Texture Synthesizer.
// Original work, MIT.
#include "RawAssetForge.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace raw::materials {
namespace {

constexpr float kTwoPi = 6.28318530717958647692f;

inline float Clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline Vec3  Lerp3(Vec3 a, Vec3 b, float t) {
    return {Lerp(a.x, b.x, t), Lerp(a.y, b.y, t), Lerp(a.z, b.z, t)};
}
inline float Smoothstep(float e0, float e1, float x) {
    const float t = Clamp01((x - e0) / std::max(e1 - e0, 1.0e-6f));
    return t * t * (3.0f - 2.0f * t);
}
inline uint8_t PackU8(float v) {
    return static_cast<uint8_t>(std::clamp(std::lround(Clamp01(v) * 255.0f), 0l, 255l));
}

// Deterministic periodic integer lattice hash in [0, 1).
float PeriodicHash(int ix, int iy, int periodX, int periodY, uint32_t seed) {
    const int wx = ((ix % periodX) + periodX) % periodX;
    const int wy = ((iy % periodY) + periodY) % periodY;
    uint32_t h = static_cast<uint32_t>(wx) * 374761393u
               ^ static_cast<uint32_t>(wy) * 668265263u
               ^ (seed * 2246822519u);
    h = (h ^ (h >> 13u)) * 1274126177u;
    h ^= (h >> 16u);
    return static_cast<float>(h & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

// Periodic 2D quintic gradient noise (Simplex-grade C2 smoothness, exact torus periodicity on [0, 1)^2).
// Returns value in [0, 1] and optional analytical gradient (dF/du, dF/dv).
float PeriodicGradNoise(float u, float v, int freqX, int freqY, uint32_t seed,
                        float* outDu = nullptr, float* outDv = nullptr) {
    const float x = u * static_cast<float>(freqX);
    const float y = v * static_cast<float>(freqY);
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(ix);
    const float fy = y - static_cast<float>(iy);

    const float ux = fx * fx * fx * (fx * (fx * 6.0f - 15.0f) + 10.0f);
    const float uy = fy * fy * fy * (fy * (fy * 6.0f - 15.0f) + 10.0f);
    const float dux = 30.0f * fx * fx * (fx * (fx - 2.0f) + 1.0f);
    const float duy = 30.0f * fy * fy * (fy * (fy - 2.0f) + 1.0f);

    auto gradCorner = [&](int cx, int cy, float dx, float dy, float& gx, float& gy) -> float {
        const float ang = PeriodicHash(cx, cy, freqX, freqY, seed) * kTwoPi;
        gx = std::cos(ang);
        gy = std::sin(ang);
        return gx * dx + gy * dy;
    };

    float gx00, gy00, gx10, gy10, gx01, gy01, gx11, gy11;
    const float n00 = gradCorner(ix,     iy,     fx,        fy,        gx00, gy00);
    const float n10 = gradCorner(ix + 1, iy,     fx - 1.0f, fy,        gx10, gy10);
    const float n01 = gradCorner(ix,     iy + 1, fx,        fy - 1.0f, gx01, gy01);
    const float n11 = gradCorner(ix + 1, iy + 1, fx - 1.0f, fy - 1.0f, gx11, gy11);

    const float nx0 = Lerp(n00, n10, ux);
    const float nx1 = Lerp(n01, n11, ux);
    const float val = Lerp(nx0, nx1, uy);

    if (outDu && outDv) {
        const float gxv = Lerp(Lerp(gx00, gx10, ux), Lerp(gx01, gx11, ux), uy);
        const float gyv = Lerp(Lerp(gy00, gy10, ux), Lerp(gy01, gy11, ux), uy);
        const float dnx = Lerp(n10 - n00, n11 - n01, uy) * dux;
        const float dny = (nx1 - nx0) * duy;
        *outDu = (gxv + dnx) * static_cast<float>(freqX);
        *outDv = (gyv + dny) * static_cast<float>(freqY);
    }
    return Clamp01(val * 0.7071f + 0.5f);
}

// Multi-octave periodic fBm in [0, 1].
float PeriodicFbm(float u, float v, int baseFreqX, int baseFreqY, uint32_t seed, int octaves) {
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    int fx = std::max(baseFreqX, 1);
    int fy = std::max(baseFreqY, 1);
    for (int i = 0; i < octaves; ++i) {
        sum += PeriodicGradNoise(u, v, fx, fy, seed + static_cast<uint32_t>(i) * 1619u) * amp;
        norm += amp;
        amp *= 0.5f;
        fx *= 2;
        fy *= 2;
    }
    return norm > 0.0f ? (sum / norm) : 0.5f;
}

// Divergence-free periodic 2D curl displacement (du, dv) from analytical gradient noise.
void PeriodicCurl2D(float u, float v, int freq, uint32_t seed, float& outDx, float& outDy) {
    float du = 0.0f, dv = 0.0f;
    PeriodicGradNoise(u, v, freq, freq, seed, &du, &dv);
    const float invF = 1.0f / static_cast<float>(std::max(freq, 1));
    outDx =  dv * invF;
    outDy = -du * invF;
}

struct WorleySample {
    float f1 = 1.0f;
    float edge = 1.0f;    // f2 - f1 (0 on Voronoi crack/facet border)
    float cellId = 0.0f;
    float cellTiltX = 0.0f;
    float cellTiltY = 0.0f;
};

// Exact periodic 2D Worley/Voronoi cellular kernel on [0, 1)^2.
WorleySample PeriodicWorley2D(float u, float v, int cellsX, int cellsY, uint32_t seed) {
    const float x = u * static_cast<float>(cellsX);
    const float y = v * static_cast<float>(cellsY);
    const int cx = static_cast<int>(std::floor(x));
    const int cy = static_cast<int>(std::floor(y));

    float d1 = 16.0f, d2 = 16.0f;
    int bestX = cx, bestY = cy;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int nx = cx + dx;
            const int ny = cy + dy;
            const float ox = 0.14f + 0.72f * PeriodicHash(nx, ny, cellsX, cellsY, seed);
            const float oy = 0.14f + 0.72f * PeriodicHash(nx, ny, cellsX, cellsY, seed + 7919u);
            const float rx = (static_cast<float>(nx) + ox) - x;
            const float ry = (static_cast<float>(ny) + oy) - y;
            const float d = rx * rx + ry * ry;
            if (d < d1) {
                d2 = d1;
                d1 = d;
                bestX = nx;
                bestY = ny;
            } else if (d < d2) {
                d2 = d;
            }
        }
    }
    WorleySample s{};
    s.f1 = std::sqrt(std::max(d1, 0.0f));
    s.edge = std::max(std::sqrt(std::max(d2, 0.0f)) - s.f1, 0.0f);
    s.cellId = PeriodicHash(bestX, bestY, cellsX, cellsY, seed + 131u);
    s.cellTiltX = PeriodicHash(bestX, bestY, cellsX, cellsY, seed + 911u) * 2.0f - 1.0f;
    s.cellTiltY = PeriodicHash(bestX, bestY, cellsX, cellsY, seed + 1777u) * 2.0f - 1.0f;
    return s;
}

}  // namespace

std::string_view ArchetypeName(ForgeArchetype archetype) {
    switch (archetype) {
        case ForgeArchetype::ArcaneMagic:  return "ArcaneMagic";
        case ForgeArchetype::FrostGlacier: return "FrostGlacier";
        case ForgeArchetype::MagmaHeat:    return "MagmaHeat";
        case ForgeArchetype::BrushedMetal: return "BrushedMetal";
        case ForgeArchetype::CutGlassGem:  return "CutGlassGem";
        case ForgeArchetype::BasaltStone:  return "BasaltStone";
        case ForgeArchetype::WoodBark:     return "WoodBark";
        case ForgeArchetype::AlpineSnow:   return "AlpineSnow";
        default:                           return "Unknown";
    }
}

MaterialId DefaultMaterialForArchetype(ForgeArchetype archetype) {
    switch (archetype) {
        case ForgeArchetype::ArcaneMagic:  return MaterialId::SoulGemResonance;
        case ForgeArchetype::FrostGlacier: return MaterialId::GlacialIce;
        case ForgeArchetype::MagmaHeat:    return MaterialId::MoltenMagma;
        case ForgeArchetype::BrushedMetal: return MaterialId::NordicSteel;
        case ForgeArchetype::CutGlassGem:  return MaterialId::SapphireCrystal;
        case ForgeArchetype::BasaltStone:  return MaterialId::ColumnarBasalt;
        case ForgeArchetype::WoodBark:     return MaterialId::NordicPineBark;
        case ForgeArchetype::AlpineSnow:   return MaterialId::FreshAlpineSnow;
        default:                           return MaterialId::ColumnarBasalt;
    }
}

bool SynthesizePbrSet(ForgeArchetype archetype, uint32_t width, uint32_t height, uint32_t seed,
                      ProceduralPbrSet* outSet, std::string* outErr) {
    auto fail = [&](const char* msg) -> bool {
        if (outErr) *outErr = msg;
        return false;
    };
    if (!outSet) return fail("outSet is null");
    if (static_cast<uint8_t>(archetype) >= static_cast<uint8_t>(ForgeArchetype::Count)) {
        return fail("invalid ForgeArchetype");
    }
    if (width < 16u || height < 16u || width > 2048u || height > 2048u) {
        return fail("dimensions must be in [16, 2048]");
    }

    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    const size_t byteCount = pixelCount * 4u;

    outSet->archetype = archetype;
    outSet->linkedMaterialId = DefaultMaterialForArchetype(archetype);
    outSet->width = width;
    outSet->height = height;
    outSet->seed = seed;
    outSet->albedoRgba8.assign(byteCount, 0u);
    outSet->normalRgba8.assign(byteCount, 0u);
    outSet->rmaohRgba8.assign(byteCount, 0u);
    outSet->emissiveRgba8.assign(byteCount, 0u);

    std::vector<float> heightField(pixelCount, 0.5f);
    std::vector<float> facetBiasX(pixelCount, 0.0f);
    std::vector<float> facetBiasY(pixelCount, 0.0f);

    float normalStrength = 2.4f;
    switch (archetype) {
        case ForgeArchetype::BrushedMetal: normalStrength = 1.4f; break;
        case ForgeArchetype::CutGlassGem:  normalStrength = 2.0f; break;
        case ForgeArchetype::AlpineSnow:   normalStrength = 1.8f; break;
        case ForgeArchetype::WoodBark:     normalStrength = 3.6f; break;
        case ForgeArchetype::BasaltStone:  normalStrength = 3.2f; break;
        default:                           normalStrength = 2.4f; break;
    }

    for (uint32_t y = 0; y < height; ++y) {
        const float v = static_cast<float>(y) / static_cast<float>(height);
        for (uint32_t x = 0; x < width; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(width);
            const size_t idx = static_cast<size_t>(y) * width + x;
            const size_t off = idx * 4u;

            Vec3  albedo{0.5f, 0.5f, 0.5f};
            Vec3  emissive{0.0f, 0.0f, 0.0f};
            float emMask = 0.0f;
            float rough = 0.5f;
            float metal = 0.0f;
            float ao = 1.0f;
            float h = 0.5f;

            switch (archetype) {
                case ForgeArchetype::ArcaneMagic: {
                    float cx = 0.0f, cy = 0.0f;
                    PeriodicCurl2D(u, v, 4, seed + 11u, cx, cy);
                    const float wu = u + cx * 0.09f;
                    const float wv = v + cy * 0.09f;
                    const auto w1 = PeriodicWorley2D(wu, wv, 6, 6, seed + 23u);
                    const auto w2 = PeriodicWorley2D(u, v, 12, 12, seed + 57u);
                    const float runeLine = 1.0f - Smoothstep(0.015f, 0.095f, w1.edge);
                    const float fineGrid = (1.0f - Smoothstep(0.01f, 0.06f, w2.edge)) * 0.5f;
                    const float plasma = PeriodicFbm(wu, wv, 4, 4, seed + 97u, 4);
                    const Vec3 irid = EvaluateThinFilmAiryRGB(0.45f + 0.45f * plasma, 360.0f + w1.cellId * 240.0f);
                    const Vec3 baseTint = Lerp3({0.14f, 0.22f, 0.52f}, {0.52f, 0.20f, 0.78f}, plasma);
                    albedo = {baseTint.x * irid.x, baseTint.y * irid.y, baseTint.z * irid.z};
                    emMask = Clamp01(runeLine * 0.90f + fineGrid * 0.45f + (1.0f - Smoothstep(0.05f, 0.30f, w1.f1)) * 0.30f);
                    emissive = {
                        Clamp01(Lerp(0.30f, irid.x, 0.55f) * emMask),
                        Clamp01(Lerp(0.55f, irid.y, 0.55f) * emMask),
                        Clamp01(Lerp(1.00f, irid.z, 0.45f) * emMask)
                    };
                    rough = Lerp(0.10f, 0.28f, 1.0f - runeLine);
                    metal = 0.0f;
                    h = Clamp01(0.45f + plasma * 0.35f + runeLine * 0.20f);
                    ao = Lerp(0.65f, 1.0f, h);
                    facetBiasX[idx] = w1.cellTiltX * 0.16f;
                    facetBiasY[idx] = w1.cellTiltY * 0.16f;
                    break;
                }
                case ForgeArchetype::FrostGlacier: {
                    const auto wCrack = PeriodicWorley2D(u, v, 5, 5, seed + 13u);
                    const auto wDeep  = PeriodicWorley2D(u + 0.13f, v + 0.07f, 9, 9, seed + 41u);
                    const float strata = PeriodicFbm(u, v, 3, 6, seed + 79u, 4);
                    const float crack1 = 1.0f - Smoothstep(0.015f, 0.13f, wCrack.edge);
                    const float crack2 = 1.0f - Smoothstep(0.02f, 0.15f, wDeep.edge);
                    const float frostVeil = Clamp01(crack1 * 0.65f + crack2 * 0.35f + Smoothstep(0.58f, 0.88f, strata) * 0.35f);
                    const Vec3 trans = EvaluateBeerLambert({0.46f, 0.145f, 0.052f}, 0.55f + (1.0f - strata) * 1.4f);
                    const Vec3 deepCore = {trans.x * 0.25f, trans.y * 0.46f, trans.z * 0.62f};
                    albedo = Lerp3(deepCore, {0.72f, 0.86f, 0.95f}, frostVeil * 0.78f);
                    rough = Lerp(0.08f, 0.38f, frostVeil);
                    metal = 0.0f;
                    h = Clamp01(0.55f + strata * 0.32f - crack1 * 0.28f);
                    ao = Lerp(0.58f, 1.0f, h);
                    facetBiasX[idx] = wCrack.cellTiltX * 0.22f;
                    facetBiasY[idx] = wCrack.cellTiltY * 0.22f;
                    break;
                }
                case ForgeArchetype::MagmaHeat: {
                    float cx = 0.0f, cy = 0.0f;
                    PeriodicCurl2D(u, v, 4, seed + 17u, cx, cy);
                    const auto wPlate = PeriodicWorley2D(u + cx * 0.08f, v + cy * 0.08f, 5, 5, seed + 29u);
                    const auto wFine  = PeriodicWorley2D(u + cx * 0.05f, v + cy * 0.05f, 11, 11, seed + 61u);
                    const float vesic = PeriodicFbm(u, v, 8, 8, seed + 103u, 4);
                    const float crackP = 1.0f - Smoothstep(0.015f, 0.17f, wPlate.edge);
                    const float crackS = (1.0f - Smoothstep(0.01f, 0.10f, wFine.edge)) * 0.55f;
                    const float heat = Clamp01(std::max(crackP, crackS));
                    const float tempK = Lerp(500.0f, 3100.0f, std::pow(heat, 0.75f));
                    float radScale = 0.0f;
                    const Vec3 bb = EvaluatePlanckBlackbodyRGB(tempK, &radScale);
                    const float normRad = Clamp01(radScale * 0.32f);
                    emMask = heat;
                    emissive = {Clamp01(bb.x * normRad), Clamp01(bb.y * normRad), Clamp01(bb.z * normRad)};
                    albedo = Lerp3({0.045f + vesic * 0.035f, 0.042f + vesic * 0.032f, 0.046f + vesic * 0.036f},
                                   {0.10f, 0.035f, 0.015f}, heat);
                    rough = Lerp(0.86f, 0.22f, heat);
                    metal = 0.0f;
                    h = Clamp01((0.68f + 0.26f * vesic) * (1.0f - heat * 0.72f));
                    ao = Lerp(0.45f, 1.0f, h);
                    break;
                }
                case ForgeArchetype::BrushedMetal: {
                    const float brush = PeriodicFbm(u, v, 4, 48, seed + 31u, 4);
                    const float hammer = PeriodicFbm(u, v, 6, 6, seed + 71u, 3);
                    const float patNoise = PeriodicFbm(u, v, 5, 5, seed + 113u, 4);
                    const float patina = Smoothstep(0.64f, 0.88f, patNoise) * 0.45f;
                    const Vec3 f0Steel = EvaluateConductorFresnelRGB(1.0f, {2.48f, 2.38f, 2.15f}, {3.42f, 3.30f, 3.10f});
                    const Vec3 steelCol = {
                        Clamp01(f0Steel.x * (0.88f + 0.22f * brush)),
                        Clamp01(f0Steel.y * (0.88f + 0.22f * brush)),
                        Clamp01(f0Steel.z * (0.88f + 0.22f * brush))
                    };
                    albedo = Lerp3(steelCol, {0.18f, 0.12f, 0.09f}, patina);
                    rough = Lerp(0.18f + brush * 0.16f, 0.78f, patina);
                    metal = Clamp01(1.0f - patina * 0.92f);
                    h = Clamp01(0.45f + brush * 0.22f + hammer * 0.28f);
                    ao = Lerp(0.70f, 1.0f, hammer);
                    break;
                }
                case ForgeArchetype::CutGlassGem: {
                    const auto wGem = PeriodicWorley2D(u, v, 6, 6, seed + 37u);
                    const float facetBevel = Smoothstep(0.015f, 0.11f, wGem.edge);
                    const float prism = PeriodicGradNoise(u, v, 6, 6, seed + 83u);
                    const Vec3 cauchy = EvaluateCauchyRGB(1.77f, 0.0112f);
                    const float dispBoost = Clamp01((cauchy.z - cauchy.x) * 18.0f);
                    const Vec3 gemBase = Lerp3({0.10f, 0.30f, 0.82f}, {0.26f, 0.56f, 0.96f}, wGem.cellId);
                    albedo = {
                        Clamp01(gemBase.x + dispBoost * prism * 0.18f),
                        Clamp01(gemBase.y + dispBoost * (1.0f - std::fabs(prism - 0.5f) * 2.0f) * 0.14f),
                        Clamp01(gemBase.z + dispBoost * (1.0f - prism) * 0.18f)
                    };
                    rough = Lerp(0.14f, 0.03f, facetBevel);
                    metal = 0.0f;
                    h = Clamp01(0.35f + (1.0f - wGem.f1) * 0.45f * facetBevel);
                    ao = Lerp(0.75f, 1.0f, facetBevel);
                    facetBiasX[idx] = wGem.cellTiltX * 0.42f * facetBevel;
                    facetBiasY[idx] = wGem.cellTiltY * 0.42f * facetBevel;
                    break;
                }
                case ForgeArchetype::BasaltStone: {
                    const auto wBlock = PeriodicWorley2D(u, v, 5, 5, seed + 43u);
                    const float pores = PeriodicFbm(u, v, 10, 10, seed + 89u, 4);
                    const float mortar = 1.0f - Smoothstep(0.015f, 0.09f, wBlock.edge);
                    const float lichen = Smoothstep(0.62f, 0.86f, PeriodicFbm(u, v, 4, 4, seed + 137u, 3)) * (0.4f + 0.6f * mortar);
                    const float stoneShade = 0.78f + 0.34f * wBlock.cellId;
                    const Vec3 basaltCol = {0.13f * stoneShade * (0.75f + 0.5f * pores),
                                            0.135f * stoneShade * (0.75f + 0.5f * pores),
                                            0.145f * stoneShade * (0.75f + 0.5f * pores)};
                    albedo = Lerp3(basaltCol, {0.14f, 0.22f, 0.10f}, lichen * 0.65f);
                    albedo = Lerp3(albedo, {0.06f, 0.06f, 0.065f}, mortar * 0.65f);
                    rough = Clamp01(Lerp(0.72f + pores * 0.20f, 0.92f, std::max(mortar, lichen)));
                    metal = 0.0f;
                    h = Clamp01((0.55f + 0.35f * pores) * (1.0f - mortar * 0.65f));
                    ao = Lerp(0.35f, 1.0f, h);
                    break;
                }
                case ForgeArchetype::WoodBark: {
                    const float warp = PeriodicFbm(u, v, 3, 2, seed + 47u, 3);
                    const float furrows = PeriodicFbm(u + warp * 0.06f, v, 14, 3, seed + 93u, 4);
                    const float fibers  = PeriodicFbm(u, v, 28, 6, seed + 149u, 3);
                    const float ridge = Smoothstep(0.24f, 0.78f, furrows);
                    albedo = Lerp3({0.075f, 0.048f, 0.032f}, {0.24f, 0.165f, 0.105f}, ridge * (0.72f + 0.28f * fibers));
                    rough = Lerp(0.92f, 0.72f, ridge);
                    metal = 0.0f;
                    h = Clamp01(0.15f + ridge * 0.72f + fibers * 0.13f);
                    ao = Lerp(0.32f, 1.0f, ridge);
                    break;
                }
                case ForgeArchetype::AlpineSnow:
                default: {
                    const float sastrugi = PeriodicFbm(u, v, 3, 7, seed + 53u, 4);
                    const float microGlint = PeriodicGradNoise(u, v, 24, 24, seed + 107u);
                    const float glintBit = Smoothstep(0.84f, 0.98f, microGlint);
                    albedo = Lerp3({0.58f, 0.70f, 0.84f}, {0.88f, 0.91f, 0.95f}, Smoothstep(0.20f, 0.80f, sastrugi));
                    albedo = Lerp3(albedo, {0.98f, 0.99f, 1.00f}, glintBit * 0.45f);
                    rough = Lerp(0.68f, 0.24f, glintBit);
                    metal = 0.0f;
                    h = Clamp01(0.25f + sastrugi * 0.65f + microGlint * 0.10f);
                    ao = Lerp(0.65f, 1.0f, sastrugi);
                    break;
                }
            }

            heightField[idx] = h;

            outSet->albedoRgba8[off + 0] = PackU8(albedo.x);
            outSet->albedoRgba8[off + 1] = PackU8(albedo.y);
            outSet->albedoRgba8[off + 2] = PackU8(albedo.z);
            outSet->albedoRgba8[off + 3] = 255u;

            outSet->rmaohRgba8[off + 0] = PackU8(rough);
            outSet->rmaohRgba8[off + 1] = PackU8(metal);
            outSet->rmaohRgba8[off + 2] = PackU8(ao);
            outSet->rmaohRgba8[off + 3] = PackU8(h);

            outSet->emissiveRgba8[off + 0] = PackU8(emissive.x);
            outSet->emissiveRgba8[off + 1] = PackU8(emissive.y);
            outSet->emissiveRgba8[off + 2] = PackU8(emissive.z);
            outSet->emissiveRgba8[off + 3] = PackU8(emMask);
        }
    }

    // Exact periodic Sobel tangent-space normal reconstruction from the heightfield + crystal facet bias:
    auto hAt = [&](int x, int y) -> float {
        const uint32_t wx = static_cast<uint32_t>(((x % static_cast<int>(width)) + static_cast<int>(width)) % static_cast<int>(width));
        const uint32_t wy = static_cast<uint32_t>(((y % static_cast<int>(height)) + static_cast<int>(height)) % static_cast<int>(height));
        return heightField[static_cast<size_t>(wy) * width + wx];
    };

    for (uint32_t y = 0; y < height; ++y) {
        const int iy = static_cast<int>(y);
        for (uint32_t x = 0; x < width; ++x) {
            const int ix = static_cast<int>(x);
            const size_t idx = static_cast<size_t>(y) * width + x;
            const size_t off = idx * 4u;

            const float tl = hAt(ix - 1, iy - 1), tc = hAt(ix, iy - 1), tr = hAt(ix + 1, iy - 1);
            const float ml = hAt(ix - 1, iy),                           mr = hAt(ix + 1, iy);
            const float bl = hAt(ix - 1, iy + 1), bc = hAt(ix, iy + 1), br = hAt(ix + 1, iy + 1);

            const float dhdx = (tr + 2.0f * mr + br) - (tl + 2.0f * ml + bl);
            const float dhdy = (bl + 2.0f * bc + br) - (tl + 2.0f * tc + tr);

            float nx = -dhdx * normalStrength + facetBiasX[idx];
            float ny = -dhdy * normalStrength + facetBiasY[idx];
            float nz = 1.0f;
            const float invLen = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
            nx *= invLen;
            ny *= invLen;
            nz *= invLen;

            outSet->normalRgba8[off + 0] = PackU8(nx * 0.5f + 0.5f);
            outSet->normalRgba8[off + 1] = PackU8(ny * 0.5f + 0.5f);
            outSet->normalRgba8[off + 2] = PackU8(nz * 0.5f + 0.5f);
            outSet->normalRgba8[off + 3] = 255u;
        }
    }

    return true;
}

bool WritePpmRgb(const std::string& path, uint32_t width, uint32_t height,
                 const std::vector<uint8_t>& rgba8, std::string* outErr) {
    if (width == 0 || height == 0 || rgba8.size() != static_cast<size_t>(width) * height * 4u) {
        if (outErr) *outErr = "invalid RGBA8 dimensions for PPM export";
        return false;
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        if (outErr) *outErr = "failed to open PPM file for writing: " + path;
        return false;
    }
    const std::string hdr = "P6\n" + std::to_string(width) + " " + std::to_string(height) + "\n255\n";
    f.write(hdr.data(), static_cast<std::streamsize>(hdr.size()));

    std::vector<uint8_t> rgb;
    rgb.resize(static_cast<size_t>(width) * height * 3u);
    for (size_t i = 0, j = 0; i < rgba8.size(); i += 4u, j += 3u) {
        rgb[j + 0] = rgba8[i + 0];
        rgb[j + 1] = rgba8[i + 1];
        rgb[j + 2] = rgba8[i + 2];
    }
    f.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    if (!f.good()) {
        if (outErr) *outErr = "I/O error writing PPM file: " + path;
        return false;
    }
    return true;
}

bool WriteRawRgba8(const std::string& path, const std::vector<uint8_t>& rgba8, std::string* outErr) {
    if (rgba8.empty()) {
        if (outErr) *outErr = "empty buffer for RAW export";
        return false;
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        if (outErr) *outErr = "failed to open RAW file for writing: " + path;
        return false;
    }
    f.write(reinterpret_cast<const char*>(rgba8.data()), static_cast<std::streamsize>(rgba8.size()));
    if (!f.good()) {
        if (outErr) *outErr = "I/O error writing RAW file: " + path;
        return false;
    }
    return true;
}

bool ExportPbrSetToDirectory(const ProceduralPbrSet& set, const std::string& dirPath,
                             const std::string& prefix, std::string* outErr) {
    std::error_code ec;
    std::filesystem::create_directories(dirPath, ec);
    if (ec) {
        if (outErr) *outErr = "failed to create export directory: " + dirPath;
        return false;
    }
    const std::filesystem::path base = std::filesystem::path(dirPath) / prefix;
    return WritePpmRgb((base.string() + "_albedo.ppm"),   set.width, set.height, set.albedoRgba8,   outErr) &&
           WritePpmRgb((base.string() + "_normal.ppm"),   set.width, set.height, set.normalRgba8,   outErr) &&
           WritePpmRgb((base.string() + "_rmaoh.ppm"),    set.width, set.height, set.rmaohRgba8,    outErr) &&
           WritePpmRgb((base.string() + "_emissive.ppm"), set.width, set.height, set.emissiveRgba8, outErr) &&
           WriteRawRgba8((base.string() + "_rmaoh.raw"),  set.rmaohRgba8, outErr);
}

}  // namespace raw::materials
