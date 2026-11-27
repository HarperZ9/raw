#pragma once
// RawTotalRebirth.h: Wave 88 Project Total Rebirth Part 1 C++23 Analytical Reference & Verification Mirror.
// Mirrors RawTotalMaterialOverhaul.hlsli (Deliot-Heitz stochastic hex-tiling, 14-class material relief),
// RawMossShader.hlsli (3D fuzzy velvet moss ecological/parallax BRDF), and RawSubdiv.hlsl
// (>= 4.0x curved Vlachos PN-Triangles + class-specific 3D world-space displacement).
// Clean-room MIT.

#include "../core/dxbc/GBufferPatch.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace raw::rebirth
{

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

inline Vec3 operator+(Vec3 a, Vec3 b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) noexcept { return {a.x * s, a.y * s, a.z * s}; }
inline float Dot3(Vec3 a, Vec3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float Len3(Vec3 a) noexcept { return std::sqrt(Dot3(a, a)); }
inline Vec3 Normalize3(Vec3 a) noexcept {
    const float l = Len3(a);
    return (l > 1.0e-6f) ? Vec3{a.x / l, a.y / l, a.z / l} : Vec3{0.0f, 0.0f, 1.0f};
}
inline float Clamp01(float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline float Lerp(float a, float b, float t) noexcept { return a + (b - a) * t; }
inline float Smoothstep(float e0, float e1, float x) noexcept {
    const float t = Clamp01((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}
inline float Frac(float v) noexcept { return v - std::floor(v); }

inline float Hash21(float x, float y) noexcept {
    float qx = Frac(x * 0.1031f);
    float qy = Frac(y * 0.1030f);
    const float d = qx * (qy + 33.33f) + qy * (qx + 33.33f);
    qx += d;
    qy += d;
    return Frac((qx + qy) * qx);
}

inline Vec2 Hash22(Vec2 p) noexcept {
    float x = Frac(p.x * 0.1031f);
    float y = Frac(p.y * 0.1030f);
    float z = Frac(p.x * 0.0973f);
    const float d = x * (y + 33.33f) + y * (z + 33.33f) + z * (x + 33.33f);
    x += d; y += d; z += d;
    return {Frac((x + y) * z), Frac((x + z) * y)};
}

inline float ValueNoise2D(float x, float y) noexcept {
    const float ix = std::floor(x);
    const float iy = std::floor(y);
    const float fx = x - ix;
    const float fy = y - iy;
    const float ux = fx * fx * (3.0f - 2.0f * fx);
    const float uy = fy * fy * (3.0f - 2.0f * fy);
    const float a = Hash21(ix, iy);
    const float b = Hash21(ix + 1.0f, iy);
    const float c = Hash21(ix, iy + 1.0f);
    const float d = Hash21(ix + 1.0f, iy + 1.0f);
    return Lerp(Lerp(a, b, ux), Lerp(c, d, ux), uy);
}

inline float Fbm2D(float x, float y) noexcept {
    float sum = 0.0f;
    float amp = 0.50f;
    float qx = x, qy = y;
    for (int i = 0; i < 3; ++i) {
        sum += amp * ValueNoise2D(qx, qy);
        const float nx = qx * 1.60f - qy * 1.20f + 17.3f;
        const float ny = qx * 1.20f + qy * 1.60f + 9.7f;
        qx = nx; qy = ny;
        amp *= 0.50f;
    }
    return sum * (1.0f / 0.875f);
}

struct HexGridSample {
    Vec2  v1{};
    Vec2  v2{};
    Vec2  v3{};
    float w1 = 0.0f;
    float w2 = 0.0f;
    float w3 = 0.0f;
};

inline HexGridSample ComputeHexTriangleGrid(Vec2 uv) noexcept {
    const float sx = uv.x * 3.4641016f;
    const float sy = uv.y * 3.4641016f;
    const float kx = sx - 0.57735027f * sy;
    const float ky = 1.15470054f * sy;
    const float vx = std::floor(kx);
    const float vy = std::floor(ky);
    const float bx = kx - vx;
    const float by = ky - vy;
    const float bz = 1.0f - bx - by;

    HexGridSample out{};
    if (bz > 0.0f) {
        out.v1 = {vx, vy};
        out.v2 = {vx + 1.0f, vy};
        out.v3 = {vx, vy + 1.0f};
        out.w1 = bz; out.w2 = bx; out.w3 = by;
    } else {
        out.v1 = {vx + 1.0f, vy + 1.0f};
        out.v2 = {vx, vy + 1.0f};
        out.v3 = {vx + 1.0f, vy};
        out.w1 = -bz; out.w2 = 1.0f - bx; out.w3 = 1.0f - by;
    }
    out.w1 = std::pow(std::max(out.w1, 1.0e-4f), 2.2f);
    out.w2 = std::pow(std::max(out.w2, 1.0e-4f), 2.2f);
    out.w3 = std::pow(std::max(out.w3, 1.0e-4f), 2.2f);
    const float invSum = 1.0f / std::max(out.w1 + out.w2 + out.w3, 1.0e-5f);
    out.w1 *= invSum; out.w2 *= invSum; out.w3 *= invSum;
    return out;
}

inline Vec2 HexTransformUv(Vec2 uv, Vec2 cell) noexcept {
    const Vec2 rnd = Hash22(cell);
    const float ang = rnd.x * 6.2831853f;
    const float ca = std::cos(ang);
    const float sa = std::sin(ang);
    return {ca * uv.x - sa * uv.y + rnd.x * 13.7f,
            sa * uv.x + ca * uv.y + rnd.y * 13.7f};
}

inline float EvalHexStochasticNoise(Vec2 p) noexcept {
    const HexGridSample h = ComputeHexTriangleGrid({p.x * 0.28f, p.y * 0.28f});
    const Vec2 u1 = HexTransformUv(p, h.v1);
    const Vec2 u2 = HexTransformUv(p, h.v2);
    const Vec2 u3 = HexTransformUv(p, h.v3);
    const float n1 = Fbm2D(u1.x, u1.y);
    const float n2 = Fbm2D(u2.x, u2.y);
    const float n3 = Fbm2D(u3.x, u3.y);
    const float invNorm = 1.0f / std::sqrt(std::max(h.w1 * h.w1 + h.w2 * h.w2 + h.w3 * h.w3, 0.3333f));
    const float c = (n1 - 0.5f) * h.w1 + (n2 - 0.5f) * h.w2 + (n3 - 0.5f) * h.w3;
    return Clamp01(0.5f + c * Lerp(1.0f, invNorm, 0.55f));
}

// 3D Fuzzy Velvet Moss analytical mirror (RawMossShader.hlsli)
struct MossEvalInput {
    Vec3  worldM{};
    Vec3  normal{0.0f, 0.0f, 1.0f};
    Vec3  viewDir{0.0f, -0.707f, 0.707f};
    Vec3  sunDir{0.4f, 0.3f, 0.86f};
    float distM = 4.0f;
    float waterPlaneZ_m = 0.0f;
    float snowCoverage01 = 0.0f;
    float rain01 = 0.0f;
    raw::dxbc::material::AssetClass14 assetClass = raw::dxbc::material::AssetClass14::RockCliffBoulder;
    bool  isInterior = false;
    bool  isActor = false;
    float albedoR = 0.35f;
    float albedoG = 0.35f;
    float albedoB = 0.35f;
};

struct MossEvalResult {
    float coverage01   = 0.0f;
    float velvetSheen  = 0.0f;
    float dampRim01    = 0.0f;
    Vec3  mossColor{};
};

// Wave 89 Defect B guard: distinguishes genuinely living green foliage from frosted/dead winter brambles.
constexpr bool IsLivingGreenFoliage(float r, float g, float b) noexcept {
    return (g > r * 1.04f) && (g > b * 1.08f) && (g > 0.024f);
}

inline MossEvalResult Evaluate3DMoss(const MossEvalInput& in) noexcept {
    MossEvalResult out{};
    if (in.isInterior || in.isActor || in.distM > 52.0f) return out;
    // Wave 89 Defect A + D hard hue exclusion: warm fur/wood and painted Nordic blue wood NEVER receive moss.
    const bool isWarmFurOrWood = (in.albedoR > in.albedoB * 1.16f && in.albedoR >= in.albedoG * 1.02f);
    const bool isPaintedBlue   = (in.albedoB > in.albedoR * 1.12f && in.albedoG > in.albedoR * 1.02f);
    if (isWarmFurOrWood || isPaintedBlue) return out;
    using AC = raw::dxbc::material::AssetClass14;
    const bool eligible = (in.assetClass == AC::RockCliffBoulder || in.assetClass == AC::CobbleRoadPath ||
                           in.assetClass == AC::NordicStoneMasonry || in.assetClass == AC::MossLichen ||
                           in.assetClass == AC::TimberWood || in.assetClass == AC::TreeTrunkBark ||
                           in.assetClass == AC::TerrainGround);
    if (!eligible) return out;

    const float snowSuppress = 1.0f - Smoothstep(0.18f, 0.55f, in.snowCoverage01);
    if (snowSuppress <= 0.02f) return out;

    const float distFade = 1.0f - Smoothstep(22.0f, 52.0f, in.distM);
    const float upSlope  = Smoothstep(0.26f, 0.76f, in.normal.z);
    const float northBias = 0.72f + 0.28f * Clamp01(-in.normal.y * 0.5f + 0.5f);
    const float hWater = in.worldM.z - in.waterPlaneZ_m;
    const float riparian = (hWater > -0.15f && hWater < 6.5f)
                         ? (1.0f - Smoothstep(0.4f, 6.5f, std::max(hWater, 0.0f))) : 0.0f;

    const float cMacro = EvalHexStochasticNoise({in.worldM.x * 0.34f + in.worldM.z * 0.12f,
                                                 in.worldM.y * 0.34f + in.worldM.z * 0.12f});
    const float cMed   = EvalHexStochasticNoise({in.worldM.x * 1.15f + 7.3f, in.worldM.y * 1.15f + 13.1f});
    const float field  = cMacro * 0.64f + cMed * 0.36f + riparian * 0.20f + in.rain01 * 0.08f
                       + (in.assetClass == AC::MossLichen ? 0.18f : 0.0f);

    out.dampRim01 = Smoothstep(0.40f, 0.53f, field) * (1.0f - Smoothstep(0.53f, 0.64f, field))
                  * upSlope * snowSuppress * distFade;
    out.coverage01 = Smoothstep(0.51f, 0.72f, field) * upSlope * northBias * snowSuppress * distFade;
    const float ndv = Clamp01(Dot3(in.normal, Normalize3(in.viewDir)));
    const float ndl = Clamp01(Dot3(in.normal, Normalize3(in.sunDir)) * 0.5f + 0.5f);
    out.velvetSheen = std::pow(1.0f - ndv, 3.1f) * (0.35f + 0.65f * ndl) * out.coverage01;
    out.mossColor = {
        Lerp(0.065f, 0.22f, ndl) + 0.18f * out.velvetSheen,
        Lerp(0.125f, 0.32f, ndl) + 0.28f * out.velvetSheen,
        Lerp(0.040f, 0.09f, ndl) + 0.07f * out.velvetSheen,
    };
    return out;
}

// Wave 89 Part 2: Sharp Voronoi-edge conchoidal glacier fracture crack (mirrors RawTotalMaterialOverhaul.hlsli).
inline float EvalGlacierConchoidalCrack(Vec2 p) noexcept {
    const float ix = std::floor(p.x);
    const float iy = std::floor(p.y);
    const float fx = p.x - ix;
    const float fy = p.y - iy;
    float d1 = 8.0f;
    float d2 = 8.0f;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            const Vec2 o = Hash22({ix + float(x), iy + float(y)});
            const float rx = float(x) + (o.x * 0.78f + 0.11f) - fx;
            const float ry = float(y) + (o.y * 0.78f + 0.11f) - fy;
            const float d  = rx * rx + ry * ry;
            if (d < d1) { d2 = d1; d1 = d; }
            else if (d < d2) { d2 = d; }
        }
    }
    const float edge = std::sqrt(std::max(d2, 0.0f)) - std::sqrt(std::max(d1, 0.0f));
    return 1.0f - Smoothstep(0.015f, 0.085f, edge);
}

// Wave 92 Visual Eye-Audit Fix #1: ~2.2x enlarged Voronoi glacier crystal planes + softened interior seam contrast.
inline float EvalGlacierAnisotropicHeight(Vec3 worldM, Vec2 pDom) noexcept {
    const Vec2 pObl{pDom.x * 0.899f - pDom.y * 0.438f, pDom.x * 0.438f + pDom.y * 0.899f};
    const Vec2 pW{
        pObl.x + 0.16f * std::sin(pObl.y * 1.45f + worldM.z * 0.32f + 0.7f) + 0.06f * std::cos(pObl.x * 2.15f - pObl.y * 1.15f),
        pObl.y + 0.12f * std::cos(pObl.x * 1.65f - worldM.z * 0.28f - 1.1f) + 0.04f * std::sin(pObl.y * 2.45f + pObl.x * 1.25f)
    };
    const float edge1 = EvalGlacierConchoidalCrack({pW.x * 0.66f, pW.y * 2.18f + worldM.z * 0.34f});
    const float edge2 = EvalGlacierConchoidalCrack({(pW.x + 0.37f) * 1.45f, (pW.y - 0.21f) * 5.20f - worldM.z * 0.52f});
    const float plane = EvalHexStochasticNoise({pW.x * 0.66f, pW.y * 2.18f + worldM.z * 0.34f});
    return Clamp01(0.40f + 0.46f * plane - 0.14f * edge1 - 0.06f * edge2);
}

// Wave 90 Part 3: Henyey-Greenstein (OPT-032) backlit subsurface foliage translucency analytical mirror.
inline float EvalHenyeyGreensteinLeafTranslucency(Vec3 viewDir, Vec3 sunDir, float g = 0.62f) noexcept {
    const float cosBack = Clamp01(Dot3(viewDir * -1.0f, Normalize3(sunDir)));
    const float den = std::max(1.0f + g * g - 2.0f * g * cosBack, 0.08f);
    return Clamp01(((1.0f - g * g) / (den * std::sqrt(den))) * 0.22f);
}

// Wave 89 Part 2: Strictly directional 22:1 anisotropic wood-grain fibers (zero blotchy wood splatter).
inline float EvalTimberAnisotropicGrain(Vec3 worldM, Vec3 N) noexcept {
    const float along  = (std::fabs(N.z) > 0.65f) ? worldM.x : worldM.z;
    const float across = (std::fabs(N.z) > 0.65f) ? worldM.y : (std::fabs(N.x) > std::fabs(N.y) ? worldM.y : worldM.x);
    const float grainWarp = (ValueNoise2D(across * 1.4f, along * 0.35f) - 0.5f) * 0.14f;
    const float fiberA = ValueNoise2D((across + grainWarp) * 24.0f, along * 1.10f);
    const float fiberB = ValueNoise2D((across + grainWarp) * 52.0f, along * 2.30f);
    const float ring   = 0.5f + 0.5f * std::sin((across + grainWarp) * 38.0f + fiberA * 2.4f);
    return Clamp01(0.46f * fiberA + 0.34f * fiberB + 0.20f * ring);
}

// Wave 89 Part 2: 4-Step Silhouette-Safe POM Height-Field Displacement & Road Wheel-Rut Puddle mirror.
struct PomEvalResult {
    Vec3  displacedWorldM{};
    float pomAo        = 1.0f;
    float roadPuddle01 = 0.0f;
    float hitHeight01  = 0.5f;
};

inline PomEvalResult EvalPomDisplacement4Step(raw::dxbc::material::AssetClass14 cls,
                                              Vec3 worldM, Vec3 N, Vec3 viewDir,
                                              float distM, float interiorMask, float rain01) noexcept {
    PomEvalResult out{worldM, 1.0f, 0.0f, 0.5f};
    using AC = raw::dxbc::material::AssetClass14;
    const bool eligible = (cls == AC::CobbleRoadPath || cls == AC::RockCliffBoulder ||
                           cls == AC::TerrainGround || cls == AC::NordicStoneMasonry);
    const float pomFade = (1.0f - Smoothstep(8.0f, 24.0f, distM)) * interiorMask;
    if (!eligible || pomFade <= 0.02f) return out;

    const float ndv = std::max(std::fabs(Dot3(N, viewDir)), 0.38f);
    const Vec3 tanView = viewDir - N * Dot3(viewDir, N);
    const float depthM = (cls == AC::RockCliffBoulder) ? 0.075f
                       : ((cls == AC::CobbleRoadPath) ? 0.055f : 0.040f);
    const Vec3 stepVec = tanView * (-(depthM * pomFade) / (ndv * 4.0f));

    Vec3 curW = worldM;
    float curLayer = 1.0f;
    float hitH = 0.5f;
    for (int i = 0; i < 4; ++i) {
        curLayer -= 0.25f;
        curW = curW + stepVec;
        const Vec2 pStep = (std::fabs(N.z) >= std::fabs(N.x) && std::fabs(N.z) >= std::fabs(N.y))
            ? Vec2{curW.x, curW.y} : ((std::fabs(N.x) >= std::fabs(N.y)) ? Vec2{curW.y, curW.z} : Vec2{curW.x, curW.z});
        float h = 0.5f;
        if (cls == AC::RockCliffBoulder) {
            const float warp = (EvalHexStochasticNoise({pStep.x * 0.52f, pStep.y * 0.52f}) - 0.5f) * 1.45f;
            const float ledge = Smoothstep(0.28f, 0.72f, 0.5f + 0.5f * std::sin(curW.z * 2.8f + warp * 3.4f));
            const float crack = EvalGlacierConchoidalCrack({pStep.x * 1.15f + curW.z * 0.45f, pStep.y * 1.15f + 1.9f});
            const float grit  = ValueNoise2D(pStep.x * 18.0f + curW.z * 6.5f, pStep.y * 18.0f + curW.z * 6.5f);
            h = Clamp01(0.36f * ledge + 0.38f * grit + 0.26f * EvalHexStochasticNoise({pStep.x * 1.7f, pStep.y * 1.7f}) - 0.30f * crack);
        } else {
            const float stoneMacro = EvalHexStochasticNoise({pStep.x * 1.55f, pStep.y * 1.55f});
            const float chiselPit  = ValueNoise2D(pStep.x * 11.5f + stoneMacro * 1.2f, pStep.y * 11.5f + 2.7f);
            const float quartzGrit = ValueNoise2D(pStep.x * 26.0f + curW.z * 6.0f, pStep.y * 26.0f + curW.z * 6.0f);
            h = Clamp01(0.36f * stoneMacro + 0.36f * chiselPit + 0.28f * quartzGrit);
        }
        hitH = h;
        if (h >= curLayer) break;
    }
    out.displacedWorldM = curW;
    out.hitHeight01 = hitH;
    out.pomAo = Lerp(1.0f, Lerp(0.76f, 1.08f, hitH), pomFade * 0.72f);
    if ((cls == AC::CobbleRoadPath || cls == AC::TerrainGround) && N.z > 0.78f) {
        const float hollow = Smoothstep(0.46f, 0.20f, hitH);
        const float rutMacro = Smoothstep(0.54f, 0.80f, EvalHexStochasticNoise({worldM.x * 0.26f + 9.1f, worldM.y * 0.26f + 4.3f}));
        out.roadPuddle01 = hollow * rutMacro * Clamp01(0.32f + rain01 * 0.68f) * pomFade;
    }
    return out;
}

// Curved Vlachos PN-Triangle 1->4 (and cascaded 1->16) CPU mirror matching RawSubdiv.hlsl bit-for-bit structure.
struct SubdivVert {
    Vec3 pos{};
    Vec3 normal{0.0f, 0.0f, 1.0f};
    Vec2 uv{};
};

inline float EvalClassDisplacement3D(Vec3 worldPos, Vec3 n, raw::dxbc::material::AssetClass14 cls, float amp) noexcept {
    if (amp <= 1.0e-5f) return 0.0f;
    using AC = raw::dxbc::material::AssetClass14;
    if (cls == AC::CharacterSkinHairCloth || cls == AC::WaterSurface) return 0.0f;
    const float baseN = ValueNoise2D(worldPos.x * 0.07f + worldPos.z * 0.03f, worldPos.y * 0.07f - worldPos.z * 0.03f);
    if (cls == AC::RockCliffBoulder) {
        const float strata = std::sin(worldPos.z * 0.16f + baseN * 2.2f);
        return (strata * 0.55f + (baseN - 0.5f) * 1.15f) * amp;
    }
    if (cls == AC::CobbleRoadPath) {
        const float gx = Frac(worldPos.x * 0.045f);
        const float gy = Frac(worldPos.y * 0.045f);
        const float edge = std::min(std::min(gx, 1.0f - gx), std::min(gy, 1.0f - gy));
        return (Smoothstep(0.04f, 0.22f, edge) - 0.35f) * amp * 1.10f;
    }
    if (cls == AC::NordicStoneMasonry) {
        const float gx = Frac((worldPos.x + worldPos.y) * 0.025f);
        const float gy = Frac(worldPos.z * 0.040f);
        const float bevel = Smoothstep(0.03f, 0.14f, std::min(std::min(gx, 1.0f - gx), std::min(gy, 1.0f - gy)));
        return (bevel - 0.40f + (baseN - 0.5f) * 0.35f) * amp;
    }
    if (cls == AC::TreeTrunkBark) {
        const float ridge = std::fabs(ValueNoise2D((worldPos.x + worldPos.y) * 0.22f, worldPos.z * 0.035f) * 2.0f - 1.0f);
        return (0.5f - ridge) * amp * 1.25f;
    }
    if (cls == AC::GlacierIce) {
        // Wave 89: 6:1 anisotropic glacial flow ridges + conchoidal stepped ledges (zero isotropic blobs)
        const float stria = ValueNoise2D(worldPos.x * 0.025f + worldPos.y * 0.015f, worldPos.z * 0.16f);
        const float facet = std::fabs(std::sin((worldPos.x * 0.03f - worldPos.y * 0.02f + worldPos.z * 0.12f) + stria * 1.4f));
        return (facet * 0.65f + stria * 0.35f - 0.5f) * amp * 1.15f;
    }
    (void)n;
    return (baseN - 0.5f) * amp;
}

inline Vec3 PnEdge(Vec3 p, Vec3 n, Vec3 q, float creaseW) noexcept {
    const Vec3 lin = (p * 2.0f + q) * (1.0f / 3.0f);
    const Vec3 crv = (p * 2.0f + q - n * Dot3(q - p, n)) * (1.0f / 3.0f);
    return {Lerp(lin.x, crv.x, creaseW), Lerp(lin.y, crv.y, creaseW), Lerp(lin.z, crv.z, creaseW)};
}

struct SubdivMeshData {
    std::vector<SubdivVert> vertices;
    std::vector<uint32_t>   indices;
};

// Runs 1..N passes of curved PN-Triangle subdivision (1 pass = 4x tris, 2 passes = 16x tris).
// Crack-free: midpoint displacement is purely deterministic from world position + symmetric edge normal.
inline SubdivMeshData SubdividePnTriangles(const SubdivMeshData& src, uint32_t passes,
                                           float curvatureGain, float displaceAmp,
                                           raw::dxbc::material::AssetClass14 assetClass,
                                           Vec3 camPosW = {}) {
    SubdivMeshData cur = src;
    for (uint32_t pass = 0u; pass < passes; ++pass) {
        const uint32_t triCount = static_cast<uint32_t>(cur.indices.size() / 3u);
        SubdivMeshData next{};
        next.vertices.resize(size_t(triCount) * 6u);
        next.indices.resize(size_t(triCount) * 12u);

        constexpr float kBary[6][3] = {
            {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
            {0.5f, 0.0f, 0.5f}, {0.5f, 0.5f, 0.0f}, {0.0f, 0.5f, 0.5f},
        };
        constexpr uint32_t kSubIdx[4][3] = {
            {0u, 3u, 5u}, {3u, 1u, 4u}, {5u, 4u, 2u}, {3u, 4u, 5u},
        };
        const float passDisp = displaceAmp * (pass == 0u ? 1.0f : 0.45f);

        for (uint32_t t = 0u; t < triCount; ++t) {
            const SubdivVert& v0 = cur.vertices[cur.indices[t * 3u + 0u]];
            const SubdivVert& v1 = cur.vertices[cur.indices[t * 3u + 1u]];
            const SubdivVert& v2 = cur.vertices[cur.indices[t * 3u + 2u]];

            const Vec3 n0 = Normalize3(v0.normal);
            const Vec3 n1 = Normalize3(v1.normal);
            const Vec3 n2 = Normalize3(v2.normal);
            const float c01 = Smoothstep(0.25f, 0.72f, Dot3(n0, n1)) * curvatureGain;
            const float c12 = Smoothstep(0.25f, 0.72f, Dot3(n1, n2)) * curvatureGain;
            const float c20 = Smoothstep(0.25f, 0.72f, Dot3(n2, n0)) * curvatureGain;

            const Vec3 b300 = v0.pos, b030 = v1.pos, b003 = v2.pos;
            const Vec3 b210 = PnEdge(b300, n0, b030, c01);
            const Vec3 b120 = PnEdge(b030, n1, b300, c01);
            const Vec3 b021 = PnEdge(b030, n1, b003, c12);
            const Vec3 b012 = PnEdge(b003, n2, b030, c12);
            const Vec3 b102 = PnEdge(b003, n2, b300, c20);
            const Vec3 b201 = PnEdge(b300, n0, b003, c20);
            const Vec3 E = (b210 + b120 + b021 + b012 + b102 + b201) * (1.0f / 6.0f);
            const Vec3 V = (b300 + b030 + b003) * (1.0f / 3.0f);
            const Vec3 b111 = E + (E - V) * 0.5f;

            const uint32_t vBase = t * 6u;
            for (uint32_t k = 0u; k < 6u; ++k) {
                const float u = kBary[k][0], v = kBary[k][1], w = kBary[k][2];
                Vec3 p = b300 * (w * w * w) + b030 * (u * u * u) + b003 * (v * v * v)
                       + (b210 * (w * w * u) + b120 * (w * u * u) + b201 * (w * w * v)
                        + b021 * (u * u * v) + b102 * (w * v * v) + b012 * (u * v * v)) * 3.0f
                       + b111 * (6.0f * u * v * w);
                const Vec3 n = Normalize3(n0 * w + n1 * u + n2 * v);
                if (k >= 3u && passDisp > 1.0e-4f) {
                    const float d = EvalClassDisplacement3D(p + camPosW, n, assetClass, passDisp);
                    p = p + n * d;
                }
                next.vertices[vBase + k] = {
                    p, n, {v0.uv.x * w + v1.uv.x * u + v2.uv.x * v,
                           v0.uv.y * w + v1.uv.y * u + v2.uv.y * v}
                };
            }
            const uint32_t iBase = t * 12u;
            for (uint32_t s = 0u; s < 4u; ++s) {
                next.indices[iBase + s * 3u + 0u] = vBase + kSubIdx[s][0];
                next.indices[iBase + s * 3u + 1u] = vBase + kSubIdx[s][1];
                next.indices[iBase + s * 3u + 2u] = vBase + kSubIdx[s][2];
            }
        }
        cur = std::move(next);
    }
    return cur;
}

} // namespace raw::rebirth
