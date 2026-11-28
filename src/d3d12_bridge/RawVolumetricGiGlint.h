#pragma once
// RawVolumetricGiGlint.h: Wave 77 C++23 Mirror of RawVolumetricGiGlint.hlsli
// World-Space Multi-Bounce GI Injection into Froxel Volumetrics (Wave 75 L=8 Hash Cache),
// Gust-Advected Water Foam & Capillary Ripple Coupling (Wave 74 Coherent Gusts), and
// Energy-Conserving World-Space Micro-Flake Snow Glint (0.0 screen-UV drift, white-furnace <= 1.0).
// Clean-room MIT, zero external dependencies, MSVC /W4 /WX clean.
#include "RawRestirHashCache.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace raw::bridge::wave77
{
struct Float2 { float x, y; };
struct Float3 { float x, y, z; };

struct FroxelGiResult
{
    Float3   bounceInScat;
    uint32_t hashKeyL8;
    uint32_t lodLevel;
    float    depthGate;
};

struct GustWaterResult
{
    Float2 capillarySlope;
    Float2 foamAdvectDelta;
    float  foamAccum01;
    float  fresnelEff;
    float  roughnessEff;
};

struct SnowGlintResult
{
    float    glintSpec;
    float    totalReflectance;
    uint32_t worldCellHash;
    float    energyBudget;
};

inline float Sanitize1(float v, float fb) noexcept
{
    return std::isfinite(v) ? v : fb;
}

inline float Saturate(float v) noexcept
{
    return std::clamp(Sanitize1(v, 0.0f), 0.0f, 1.0f);
}

inline float Smoothstep(float e0, float e1, float x) noexcept
{
    const float denom = e1 - e0;
    if (std::fabs(denom) <= 1.0e-7f) { return x >= e1 ? 1.0f : 0.0f; }
    const float t = std::clamp((Sanitize1(x, e0) - e0) / denom, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline Float3 Sanitize3(Float3 v, float capVal) noexcept
{
    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) {
        return {0.0f, 0.0f, 0.0f};
    }
    return {
        std::clamp(v.x, 0.0f, capVal),
        std::clamp(v.y, 0.0f, capVal),
        std::clamp(v.z, 0.0f, capVal)
    };
}

inline float Dot3(Float3 a, Float3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float Dot2(Float2 a, Float2 b) noexcept { return a.x * b.x + a.y * b.y; }

inline uint32_t HashInt3(int32_t qx, int32_t qy, int32_t qz) noexcept
{
    uint32_t h = (static_cast<uint32_t>(qx) * 73856093u)
               ^ (static_cast<uint32_t>(qy) * 19349663u)
               ^ (static_cast<uint32_t>(qz) * 83492791u);
    h = (h ^ (h >> 16u)) * 2246822507u;
    h = (h ^ (h >> 13u)) * 3266489909u;
    return h ^ (h >> 16u);
}

inline float Unorm24(uint32_t h) noexcept
{
    return static_cast<float>(h & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

inline FroxelGiResult EvalFroxelWorldGiBounce(
    Float3 wpAbs,
    Float3 viewDir,
    float  rayDist,
    float  zMid,
    float  zSurf,
    Float3 cacheBounceRad,
    Float3 worldAmbBounce,
    float  scatterAlbedo,
    bool   isInterior,
    float  snowFlurry01) noexcept
{
    FroxelGiResult o{};
    const Float3 wpSafe{Sanitize1(wpAbs.x, 0.0f), Sanitize1(wpAbs.y, 0.0f), Sanitize1(wpAbs.z, 0.0f)};
    const Float3 vdRaw{Sanitize1(viewDir.x, 0.0f), Sanitize1(viewDir.y, 1.0f), Sanitize1(viewDir.z, 0.0f)};
    const float  vdLen2 = Dot3(vdRaw, vdRaw);
    const Float3 vdSafe = (vdLen2 > 1.0e-6f)
        ? Float3{vdRaw.x / std::sqrt(vdLen2), vdRaw.y / std::sqrt(vdLen2), vdRaw.z / std::sqrt(vdLen2)}
        : Float3{0.0f, 1.0f, 0.0f};

    const float distSafe = (std::max)(Sanitize1(rayDist, 64.0f), 1.0f);
    const float zMidSafe = (std::max)(Sanitize1(zMid, 64.0f), 1.0f);
    const float zSrfSafe = Sanitize1(zSurf, -1.0f);
    const float albSafe  = (std::clamp)(Sanitize1(scatterAlbedo, 0.65f), 0.0f, 0.85f);
    const float snow01   = Saturate(snowFlurry01);

    const float lodF = (std::max)(std::floor(std::log2((std::max)(distSafe * (1.0f / 64.0f), 1.0f))), 0.0f);
    const uint32_t lod = (std::min)(static_cast<uint32_t>(lodF), 7u);
    o.lodLevel = lod;
    o.hashKeyL8 = raw::bridge::HashCacheKeyMultiRes(
        raw::bridge::Vec3d{wpSafe.x, wpSafe.y, wpSafe.z},
        raw::bridge::Vec3d{vdSafe.x, vdSafe.y, vdSafe.z},
        lod, 16.0);

    float gate = 0.0f;
    if (zSrfSafe > 1.0f && zSrfSafe < 42000.0f) {
        const float cellSpan = 16.0f * static_cast<float>(1u << lod);
        const float radW = (std::clamp)(cellSpan * 2.5f, 48.0f, 420.0f);
        const float dz = std::fabs(zMidSafe - zSrfSafe);
        if (zMidSafe <= zSrfSafe + radW * 0.50f && dz <= radW * 2.50f) {
            const float q = dz / radW;
            gate = std::exp(-(std::min)(q * q, 24.0f));
        }
    }
    o.depthGate = gate;

    const Float3 cacheSafe = Sanitize3(cacheBounceRad, 12.0f);
    const Float3 ambSafe   = Sanitize3(worldAmbBounce, 6.0f);
    const float  snowBoost = 1.0f + snow01 * 0.25f;
    const float  gain      = isInterior ? 0.34f : 0.24f;
    const float  scale     = albSafe * gain * snowBoost;
    const float  maxCap    = albSafe * (isInterior ? 1.85f : 2.25f);

    auto capCh = [&](float cCh, float aCh) noexcept -> float {
        const float raw = (cCh * gate + aCh * 0.28f) * scale;
        const float eCap = (std::min)((cCh + aCh) * albSafe, maxCap);
        return (std::min)((std::clamp)(Sanitize1(raw, 0.0f), 0.0f, 4.0f), eCap);
    };
    o.bounceInScat = {
        capCh(cacheSafe.x, ambSafe.x),
        capCh(cacheSafe.y, ambSafe.y),
        capCh(cacheSafe.z, ambSafe.z)
    };
    return o;
}

inline GustWaterResult EvalGustWaterFoamRipple(
    Float3 wpAbs,
    Float2 gustVelXY,
    float  gust01,
    float  simTimeSec,
    float  baseFoam01,
    float  riverFlowSpeed,
    float  shoreMask01,
    float  fresnelWater,
    float  baseRoughness,
    float  distFade01) noexcept
{
    GustWaterResult o{};
    const Float2 pXY{Sanitize1(wpAbs.x, 0.0f), Sanitize1(wpAbs.y, 0.0f)};
    const Float2 gVel{
        std::clamp(Sanitize1(gustVelXY.x, 0.0f), -2.0f, 2.0f),
        std::clamp(Sanitize1(gustVelXY.y, 0.0f), -2.0f, 2.0f)
    };
    const float g01   = Saturate(gust01);
    const float tSafe = Sanitize1(simTimeSec, 0.0f);
    const float bFoam = Saturate(baseFoam01);
    const float rFlow = Saturate(riverFlowSpeed);
    const float shore = Saturate(shoreMask01);
    const float fW    = std::clamp(Sanitize1(fresnelWater, 0.02f), 0.0f, 1.0f);
    const float rBase = std::clamp(Sanitize1(baseRoughness, 0.04f), 0.02f, 0.65f);
    const float dFade = Saturate(distFade01);

    const float gLen = std::sqrt(gVel.x * gVel.x + gVel.y * gVel.y);
    const Float2 gDir = (gLen > 1.0e-4f) ? Float2{gVel.x / gLen, gVel.y / gLen} : Float2{0.8660254f, 0.5f};
    const Float2 gPerp{-gDir.y, gDir.x};

    const float tWrap = std::fmod(std::fabs(tSafe), 4096.0f) * (tSafe < 0.0f ? -1.0f : 1.0f);
    const float advDist = tWrap * (5.5f + 9.5f * g01);
    const Float2 pAdv{pXY.x - gDir.x * advDist, pXY.y - gDir.y * advDist};
    const float u1 = Dot2(pAdv, gDir) * 0.173f + Dot2(pAdv, gPerp) * 0.091f;
    const float u2 = Dot2(pAdv, gDir) * 0.119f - Dot2(pAdv, gPerp) * 0.237f + 1.93f;
    const float d1 = std::cos(u1) * 0.62f;
    const float d2 = std::cos(u2) * 0.38f;
    const float sx = (gDir.x * (d1 * 0.173f + d2 * 0.119f) + gPerp.x * (d1 * 0.091f - d2 * 0.237f)) * (0.48f * g01 * dFade);
    const float sy = (gDir.y * (d1 * 0.173f + d2 * 0.119f) + gPerp.y * (d1 * 0.091f - d2 * 0.237f)) * (0.48f * g01 * dFade);
    o.capillarySlope = {std::clamp(sx, -0.14f, 0.14f), std::clamp(sy, -0.14f, 0.14f)};

    const float advScale = g01 * (4.0f + 6.0f * rFlow);
    o.foamAdvectDelta = {gVel.x * advScale, gVel.y * advScale};
    const float gustBoost = Saturate((shore * 0.55f + rFlow * 0.45f) * g01 * 0.38f);
    o.foamAccum01 = Saturate(bFoam + gustBoost * (1.0f - bFoam * 0.65f));

    const float rIn = rBase + g01 * 0.06f;
    o.roughnessEff = (std::clamp)(rIn + (0.74f - rIn) * (o.foamAccum01 * 0.85f), 0.02f, 0.88f);
    const float fFoamCap = (std::min)(fW * 0.35f + 0.04f, 0.28f);
    const float fBlended = fW + (fFoamCap - fW) * (o.foamAccum01 * 0.82f);
    o.fresnelEff = Saturate((std::min)(fBlended, (std::max)(1.0f - 0.70f * o.foamAccum01, 0.0f)));
    return o;
}

inline float EvalSnowGlintGrid(int32_t qx, int32_t qy, int32_t qz, Float3 nSafe, Float3 hSafe, uint32_t& outHash) noexcept
{
    const uint32_t h0 = HashInt3(qx, qy, qz);
    const uint32_t h1 = HashInt3(qx + 19, qy + 47, qz + 83);
    const uint32_t h2 = HashInt3(qx - 31, qy + 61, qz + 13);
    outHash = h0;
    const float pProb = Unorm24(h0);
    const float gate  = Smoothstep(0.74f, 0.96f, pProb);
    if (gate <= 1.0e-5f) { return 0.0f; }

    Float3 mN{
        nSafe.x + (Unorm24(h1) - 0.5f) * 0.68f,
        nSafe.y + (Unorm24(h2) - 0.5f) * 0.68f,
        nSafe.z + (Unorm24(h0 ^ h1) - 0.5f) * 0.68f
    };
    float mLen2 = Dot3(mN, mN);
    if (mLen2 > 1.0e-6f) {
        const float inv = 1.0f / std::sqrt(mLen2);
        mN = {mN.x * inv, mN.y * inv, mN.z * inv};
    } else {
        mN = nSafe;
    }
    if (Dot3(mN, nSafe) < 0.25f) {
        Float3 blend{nSafe.x + mN.x * 0.5f, nSafe.y + mN.y * 0.5f, nSafe.z + mN.z * 0.5f};
        const float bLen = std::sqrt((std::max)(Dot3(blend, blend), 1.0e-6f));
        mN = {blend.x / bLen, blend.y / bLen, blend.z / bLen};
    }
    const float nh = Saturate(Dot3(mN, hSafe));
    const float diamond = std::pow(nh, 160.0f) * 1.45f;
    const float frost   = std::pow(nh,  44.0f) * 0.14f;
    return (diamond + frost) * gate;
}

inline SnowGlintResult EvalMicroFlakeSnowGlint(
    Float3 wpAbs,
    Float3 normalW,
    Float3 viewDirW,
    Float3 lightDirW,
    float  camDistU,
    float  snowCov01,
    float  macroDiffuseAlbedo) noexcept
{
    SnowGlintResult o{};
    const float alb = (std::clamp)(Sanitize1(macroDiffuseAlbedo, 0.72f), 0.0f, 0.92f);
    const float budget = (std::max)(1.0f - alb, 0.0f);
    o.glintSpec = 0.0f;
    o.totalReflectance = alb;
    o.energyBudget = budget;

    const Float3 wpSafe{Sanitize1(wpAbs.x, 0.0f), Sanitize1(wpAbs.y, 0.0f), Sanitize1(wpAbs.z, 0.0f)};
    const int32_t qFx = static_cast<int32_t>(std::floor(wpSafe.x * 0.50f));
    const int32_t qFy = static_cast<int32_t>(std::floor(wpSafe.y * 0.50f));
    const int32_t qFz = static_cast<int32_t>(std::floor(wpSafe.z * 0.50f));
    const int32_t qCx = static_cast<int32_t>(std::floor(wpSafe.x * 0.25f));
    const int32_t qCy = static_cast<int32_t>(std::floor(wpSafe.y * 0.25f));
    const int32_t qCz = static_cast<int32_t>(std::floor(wpSafe.z * 0.25f));
    o.worldCellHash = HashInt3(qFx, qFy, qFz);

    const float cov   = Saturate(snowCov01);
    const float distU = (std::max)(Sanitize1(camDistU, 0.0f), 0.0f);
    const Float3 nRaw{Sanitize1(normalW.x, 0.0f), Sanitize1(normalW.y, 0.0f), Sanitize1(normalW.z, 1.0f)};
    const Float3 vRaw{Sanitize1(viewDirW.x, 0.0f), Sanitize1(viewDirW.y, 0.0f), Sanitize1(viewDirW.z, 1.0f)};
    const Float3 lRaw{Sanitize1(lightDirW.x, 0.0f), Sanitize1(lightDirW.y, 0.0f), Sanitize1(lightDirW.z, 1.0f)};
    const float nL2 = Dot3(nRaw, nRaw), vL2 = Dot3(vRaw, vRaw), lL2 = Dot3(lRaw, lRaw);
    if (cov <= 1.0e-4f || nL2 <= 1.0e-6f || vL2 <= 1.0e-6f || lL2 <= 1.0e-6f) {
        return o;
    }

    const Float3 nSafe{nRaw.x / std::sqrt(nL2), nRaw.y / std::sqrt(nL2), nRaw.z / std::sqrt(nL2)};
    const Float3 vSafe{vRaw.x / std::sqrt(vL2), vRaw.y / std::sqrt(vL2), vRaw.z / std::sqrt(vL2)};
    const Float3 lSafe{lRaw.x / std::sqrt(lL2), lRaw.y / std::sqrt(lL2), lRaw.z / std::sqrt(lL2)};
    const float nl = Dot3(nSafe, lSafe);
    const float nv = Dot3(nSafe, vSafe);
    if (nl <= 1.0e-4f || nv <= 1.0e-4f) {
        return o;
    }

    const Float3 hRaw{vSafe.x + lSafe.x, vSafe.y + lSafe.y, vSafe.z + lSafe.z};
    const float  hL2 = Dot3(hRaw, hRaw);
    const Float3 hSafe = (hL2 > 1.0e-6f)
        ? Float3{hRaw.x / std::sqrt(hL2), hRaw.y / std::sqrt(hL2), hRaw.z / std::sqrt(hL2)}
        : nSafe;
    const float vh = Saturate(Dot3(vSafe, hSafe));
    const float fIce = 0.018f + 0.982f * std::pow(1.0f - vh, 5.0f);

    uint32_t hFineOut = 0u, hCoarseOut = 0u;
    const float gFine   = EvalSnowGlintGrid(qFx, qFy, qFz, nSafe, hSafe, hFineOut);
    const float gCoarse = EvalSnowGlintGrid(qCx, qCy, qCz, nSafe, hSafe, hCoarseOut);
    o.worldCellHash = hFineOut;

    const float distFade = Smoothstep(2000.0f, 120.0f, distU);
    const float coarseW  = Smoothstep(140.0f, 650.0f, distU);
    const float gBlend   = gFine + (gCoarse - gFine) * coarseW;
    const float rawGlint = (std::max)(gBlend * (0.35f + 0.65f * fIce) * cov * distFade * Saturate(nl), 0.0f);

    const float boundedGlint = budget * (rawGlint / (1.0f + rawGlint));
    o.glintSpec = boundedGlint;
    o.totalReflectance = alb + boundedGlint;
    return o;
}

} // namespace raw::bridge::wave77
