#pragma once
// RawSplitTemporalAerial.h: Wave 78 C++23 Mirror of RawSplitTemporalAerial.hlsli
// Specular-Diffuse Split Temporal Disocclusion Guard, World-Space Sub-Pixel Foliage
// Anti-Shimmer & Coverage Preservation, and Energy-Conserving Analytical Aerial-Perspective
// & Height-Fog Multi-Scatter Coupling.
// Clean-room MIT, zero external dependencies, MSVC /W4 /WX clean.
#include "RawPrecipThermal.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace raw::bridge::wave78
{
struct Float3 { float x, y, z; };

struct SplitTemporalGuard
{
    float specParallaxFactor;
    float gammaDiff;
    float gammaSpec;
    float gammaEff;
    float alphaDiff;
    float alphaSpec;
    float alphaEff;
};

struct FoliageAntiShimmerResult
{
    float coverageScale;
    float translucencyScale;
    float canopyFloorScale;
    float totalEnergyRatio;
};

struct AerialHeightFogResult
{
    float  transmittance;
    Float3 inScatter;
    float  horizonBlend;
    float  opticalDepth;
};

inline float San1(float v, float fb) noexcept
{
    return std::isfinite(v) ? v : fb;
}

inline float Sat(float v) noexcept
{
    return (std::clamp)(San1(v, 0.0f), 0.0f, 1.0f);
}

inline float Smooth(float e0, float e1, float x) noexcept
{
    const float d = e1 - e0;
    if (std::fabs(d) <= 1.0e-7f) { return x >= e1 ? 1.0f : 0.0f; }
    const float t = (std::clamp)((San1(x, e0) - e0) / d, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline Float3 San3(Float3 v, float capVal) noexcept
{
    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) {
        return {0.0f, 0.0f, 0.0f};
    }
    return {
        (std::clamp)(v.x, 0.0f, capVal),
        (std::clamp)(v.y, 0.0f, capVal),
        (std::clamp)(v.z, 0.0f, capVal)
    };
}

inline SplitTemporalGuard EvalSplitTemporalGuard(
    float roughness,
    float specLobe01,
    float speedPx,
    float mvDivPx,
    float disocc01,
    float baseGammaDiff,
    float baseAlphaDiff) noexcept
{
    SplitTemporalGuard o{};
    const float rSafe   = (std::clamp)(San1(roughness, 0.55f), 0.02f, 1.0f);
    const float sLobe   = Sat(specLobe01);
    const float spd     = (std::max)(San1(speedPx, 0.0f), 0.0f);
    const float mvDiv   = (std::max)(San1(mvDivPx, 0.0f), 0.0f);
    const float disocc  = Sat(disocc01);
    const float gDiffIn = (std::clamp)(San1(baseGammaDiff, 1.15f), 0.45f, 1.65f);
    const float aDiffIn = (std::clamp)(San1(baseAlphaDiff, 0.25f), 0.05f, 0.98f);

    const float oneMinusR = 1.0f - rSafe;
    const float parallax  = oneMinusR * oneMinusR;
    o.specParallaxFactor  = parallax;

    const float diffMotion = Sat(spd * 0.10f + mvDiv * 0.25f);
    o.gammaDiff = (std::clamp)(gDiffIn * (1.0f + (0.82f - 1.0f) * (diffMotion * 0.5f)), 0.50f, 1.65f);
    o.alphaDiff = (std::clamp)((std::max)(aDiffIn, disocc * 0.92f), 0.05f, 0.98f);

    const float specMotion  = Sat(spd * 0.18f + mvDiv * 0.45f);
    const float specTighten = Sat(parallax * (0.28f + 0.52f * specMotion));
    o.gammaSpec = (std::clamp)((std::min)(o.gammaDiff, o.gammaDiff * (1.0f - specTighten * 0.42f)), 0.38f, o.gammaDiff);
    const float specReactAlpha = Sat(o.alphaDiff + parallax * specMotion * 0.42f);
    o.alphaSpec = (std::clamp)((std::max)(o.alphaDiff, specReactAlpha), o.alphaDiff, 0.98f);

    o.gammaEff = o.gammaDiff + (o.gammaSpec - o.gammaDiff) * sLobe;
    o.alphaEff = o.alphaDiff + (o.alphaSpec - o.alphaDiff) * sLobe;
    return o;
}

inline FoliageAntiShimmerResult EvalFoliageCoverageAntiShimmer(
    Float3 wpAbs,
    Float3 normalW,
    Float3 viewDirW,
    float  camDistU,
    float  maxAlbedoCh,
    float  baseTransWeight,
    float  skyVis,
    bool   isWoodBark) noexcept
{
    FoliageAntiShimmerResult o{};
    const float albSafe   = (std::clamp)(San1(maxAlbedoCh, 0.35f), 0.0f, 0.92f);
    const float transSafe = (std::clamp)(San1(baseTransWeight, 0.12f), 0.0f, 0.50f);
    const float svSafe    = Sat(skyVis);

    if (isWoodBark) {
        o.coverageScale     = 1.0f;
        o.translucencyScale = 1.0f;
        o.canopyFloorScale  = 1.0f;
        o.totalEnergyRatio  = (std::min)(albSafe, 1.0f);
        return o;
    }

    const Float3 wpSafe{San1(wpAbs.x, 0.0f), San1(wpAbs.y, 0.0f), San1(wpAbs.z, 0.0f)};
    const Float3 nRaw{San1(normalW.x, 0.0f), San1(normalW.y, 0.0f), San1(normalW.z, 1.0f)};
    const Float3 vRaw{San1(viewDirW.x, 0.0f), San1(viewDirW.y, 0.0f), San1(viewDirW.z, 1.0f)};
    const float  nL2 = nRaw.x * nRaw.x + nRaw.y * nRaw.y + nRaw.z * nRaw.z;
    const float  vL2 = vRaw.x * vRaw.x + vRaw.y * vRaw.y + vRaw.z * vRaw.z;
    const Float3 nSafe = (nL2 > 1.0e-6f)
        ? Float3{nRaw.x / std::sqrt(nL2), nRaw.y / std::sqrt(nL2), nRaw.z / std::sqrt(nL2)}
        : Float3{0.0f, 0.0f, 1.0f};
    const Float3 vSafe = (vL2 > 1.0e-6f)
        ? Float3{vRaw.x / std::sqrt(vL2), vRaw.y / std::sqrt(vL2), vRaw.z / std::sqrt(vL2)}
        : Float3{0.0f, 0.0f, 1.0f};

    const float distU = (std::max)(San1(camDistU, 0.0f), 0.0f);
    const float distMipBias = Smooth(180.0f, 2400.0f, distU);
    const float nv = std::fabs(nSafe.x * vSafe.x + nSafe.y * vSafe.y + nSafe.z * vSafe.z);
    const float grazeW = 1.0f - Smooth(0.10f, 0.65f, nv);

    const float wCoh = 0.50f + 0.50f * (std::sin(wpSafe.x * 0.045f + std::cos(wpSafe.y * 0.045f)) * std::cos(wpSafe.z * 0.045f * 0.7f));
    const float rawCovScale = 1.0f + distMipBias * (0.08f + 0.06f * grazeW + 0.03f * wCoh);
    const float maxSafeScale = (albSafe > 1.0e-4f) ? (std::min)(1.18f, 0.94f / albSafe) : 1.18f;
    o.coverageScale = (std::clamp)((std::min)(rawCovScale, maxSafeScale), 1.0f, 1.18f);

    const float effAlb = (std::min)(albSafe * o.coverageScale, 0.94f);
    const float remBudget = (std::max)(1.0f - effAlb, 0.0f);
    const float rawTransScale = 1.0f + 0.10f * (distMipBias * (0.45f + 0.55f * svSafe));
    const float cappedTrans = (std::min)(transSafe * rawTransScale, remBudget);
    o.translucencyScale = (transSafe > 1.0e-5f) ? (std::clamp)(cappedTrans / transSafe, 0.85f, 1.12f) : 1.0f;
    const float occT = Sat(svSafe * 0.68f + distMipBias * 0.16f + 0.16f);
    o.canopyFloorScale = (std::clamp)(0.46f + (1.0f - 0.46f) * occT, 0.44f, 1.0f);
    o.totalEnergyRatio = (std::min)(effAlb + (std::min)(transSafe * o.translucencyScale, remBudget), 1.0f);
    return o;
}

inline AerialHeightFogResult EvalAnalyticalAerialHeightFog(
    float  camZ,
    float  rayDirZ,
    float  rayDist,
    float  refFloorZ,
    float  baseSigma,
    float  scaleHeightU,
    float  cloudTrans,
    float  cosSun,
    Float3 skyHorizonCol,
    Float3 sunCol,
    float  nightW) noexcept
{
    AerialHeightFogResult o{};
    const float  cz    = San1(camZ, 0.0f);
    const float  dzDir = (std::clamp)(San1(rayDirZ, 0.0f), -1.0f, 1.0f);
    const float  dist  = (std::clamp)(San1(rayDist, 0.0f), 0.0f, 300000.0f);
    const float  zRef  = San1(refFloorZ, 0.0f);
    const float  sig0  = (std::clamp)(San1(baseSigma, 0.000012f), 0.0f, 0.01f);
    const float  H     = (std::clamp)(San1(scaleHeightU, 2400.0f), 120.0f, 40000.0f);
    const float  ct    = Sat(cloudTrans);
    const float  mu    = (std::clamp)(San1(cosSun, 0.0f), -1.0f, 1.0f);
    const float  nW    = Sat(nightW);
    const Float3 skyC  = San3(skyHorizonCol, 8.0f);
    const Float3 sunC  = San3(sunCol, 12.0f);

    const float h0Norm = (std::clamp)((cz - zRef) / H, -2.0f, 16.0f);
    const float startDens = sig0 * std::exp(-h0Norm);
    const float u = (std::clamp)((dzDir * dist) / H, -16.0f, 16.0f);
    float heightInt = 1.0f;
    if (std::fabs(u) < 1.0e-3f) {
        heightInt = 1.0f - 0.5f * u + (1.0f / 6.0f) * u * u;
    } else {
        heightInt = (1.0f - std::exp(-u)) / u;
    }
    const float od = (std::clamp)(startDens * dist * (std::max)(heightInt, 0.0f), 0.0f, 32.0f);
    const float T  = Sat(std::exp(-od));

    const float hBlend = Smooth(55000.0f, 175000.0f, dist);
    const float tEff = T * (1.0f - hBlend);

    const float phW76 = static_cast<float>(raw::bridge::EvalCrepuscularPhase(mu, od, ct));
    const float sunShare = Sat(phW76 * 0.22f * ct * (1.0f - nW) * (1.0f - hBlend));
    const float cloudModBase = 0.64f + (1.0f - 0.64f) * ct;
    const float cloudSkyMod = cloudModBase + (1.0f - cloudModBase) * (std::max)(hBlend, nW);

    auto calcCh = [&](float skyCh, float sunCh) noexcept -> float {
        const float scatBase = skyCh * cloudSkyMod;
        const float sunCapped = (std::min)(sunCh, skyCh * 1.45f + 0.05f);
        const float aerialTgt = scatBase + (sunCapped - scatBase) * (sunShare * 0.35f);
        const float maxAllow = (std::max)(skyCh, sunCh) * 0.96f;
        const float clTgt = (std::min)(aerialTgt, maxAllow) + (skyCh - (std::min)(aerialTgt, maxAllow)) * hBlend;
        return (std::clamp)(San1(clTgt * (1.0f - tEff), 0.0f), 0.0f, 8.0f);
    };

    o.opticalDepth  = od;
    o.transmittance = Sat(tEff);
    o.horizonBlend  = hBlend;
    o.inScatter     = {calcCh(skyC.x, sunC.x), calcCh(skyC.y, sunC.y), calcCh(skyC.z, sunC.z)};
    return o;
}

} // namespace raw::bridge::wave78
