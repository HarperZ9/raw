#pragma once
// RawPorosityCaustics.h: Wave 80 Milestone C++23 Mirror of RawPorosityCaustics.hlsli
// Porosity-Coupled Wet-Stone/Mud/Wood Albedo Darkening & Micro-Normal Water-Film Smoothing
// + Three-Band Cauchy Spectral Water-Bed Caustic Dispersion (n_R=1.331, n_G=1.333, n_B=1.339).
// Clean-room MIT, zero external dependencies, MSVC /W4 /WX clean.
#include "RawPrecipThermal.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace raw::bridge::wave80
{
struct Float2 { float x, y; };
struct Float3 { float x, y, z; };

struct PorosityWetFilmResult
{
    Float3 albedoWet;
    Float3 normalFilm;
    float  roughnessFilm;
    float  fresnelFilm;
    float  totalEnergyMax;
};

struct SpectralCausticResult
{
    Float3 causticRGB;
    float  chromaticSpread;
    float  focusEnvelope;
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

inline PorosityWetFilmResult EvalPorosityWetFilm(
    Float3 wpAbs,
    Float3 baseAlbedo,
    Float3 microNormalW,
    Float3 macroNormalW,
    float  baseRoughness,
    float  wet01,
    float  puddle01,
    float  porosity01,
    float  ndotv,
    bool   isGreenPine) noexcept
{
    PorosityWetFilmResult o{};
    const Float3 albSafe = San3(baseAlbedo, 0.92f);
    const float  rBase   = (std::clamp)(San1(baseRoughness, 0.65f), 0.04f, 0.95f);
    const Float3 nMicRaw{San1(microNormalW.x, 0.0f), San1(microNormalW.y, 0.0f), San1(microNormalW.z, 1.0f)};
    const Float3 nMacRaw{San1(macroNormalW.x, 0.0f), San1(macroNormalW.y, 0.0f), San1(macroNormalW.z, 1.0f)};
    const float  micL2   = nMicRaw.x * nMicRaw.x + nMicRaw.y * nMicRaw.y + nMicRaw.z * nMicRaw.z;
    const float  macL2   = nMacRaw.x * nMacRaw.x + nMacRaw.y * nMacRaw.y + nMacRaw.z * nMacRaw.z;
    const Float3 nMic    = (micL2 > 1.0e-6f)
        ? Float3{nMicRaw.x / std::sqrt(micL2), nMicRaw.y / std::sqrt(micL2), nMicRaw.z / std::sqrt(micL2)}
        : Float3{0.0f, 0.0f, 1.0f};
    const Float3 nMac    = (macL2 > 1.0e-6f)
        ? Float3{nMacRaw.x / std::sqrt(macL2), nMacRaw.y / std::sqrt(macL2), nMacRaw.z / std::sqrt(macL2)}
        : Float3{0.0f, 0.0f, 1.0f};
    const float nv = Sat(ndotv);

    if (isGreenPine) {
        o.albedoWet      = albSafe;
        o.normalFilm     = nMic;
        o.roughnessFilm  = rBase;
        o.fresnelFilm    = 0.02f;
        o.totalEnergyMax = (std::min)((std::max)(albSafe.x, (std::max)(albSafe.y, albSafe.z)) + 0.02f, 1.0f);
        return o;
    }

    const float wIn  = Sat(wet01);
    const float pIn  = Sat(puddle01);
    const float por  = Sat(porosity01);
    const float conc = static_cast<float>(raw::bridge::EvalPtLaplacianConcavity(
        San1(wpAbs.x, 0.0f), San1(wpAbs.y, 0.0f)));

    const float wEff   = Sat((std::max)(wIn, pIn) * (0.76f + 0.24f * conc) + pIn * 0.30f);
    const float expPor = 1.0f + por * wEff * 0.85f;
    const Float3 darkAlb{
        (albSafe.x > 1.0e-5f) ? std::pow(albSafe.x, expPor) : 0.0f,
        (albSafe.y > 1.0e-5f) ? std::pow(albSafe.y, expPor) : 0.0f,
        (albSafe.z > 1.0e-5f) ? std::pow(albSafe.z, expPor) : 0.0f
    };

    const float smoothW = Sat(pIn * 0.85f + wEff * por * 0.42f);
    const Float3 nBlend{
        nMic.x + (nMac.x - nMic.x) * smoothW,
        nMic.y + (nMac.y - nMic.y) * smoothW,
        nMic.z + (nMac.z - nMic.z) * smoothW
    };
    const float nBL2 = nBlend.x * nBlend.x + nBlend.y * nBlend.y + nBlend.z * nBlend.z;
    o.normalFilm = (nBL2 > 1.0e-6f)
        ? Float3{nBlend.x / std::sqrt(nBL2), nBlend.y / std::sqrt(nBL2), nBlend.z / std::sqrt(nBL2)}
        : nMac;

    const float rTarget = 0.12f + (0.04f - 0.12f) * pIn;
    const float rGoal   = (std::min)(rBase, rTarget);
    o.roughnessFilm = (std::clamp)(rBase + (rGoal - rBase) * wEff, 0.04f, 0.95f);

    const float fRaw = 0.02f + 0.98f * std::pow(1.0f - nv, 5.0f);
    o.fresnelFilm = Sat(fRaw * (1.0f + (0.42f - 1.0f) * (o.roughnessFilm * o.roughnessFilm)));
    const float remBudget = (std::max)(1.0f - o.fresnelFilm, 0.0f);
    o.albedoWet = {
        (std::min)(darkAlb.x, remBudget),
        (std::min)(darkAlb.y, remBudget),
        (std::min)(darkAlb.z, remBudget)
    };
    o.totalEnergyMax = (std::max)(o.albedoWet.x, (std::max)(o.albedoWet.y, o.albedoWet.z)) + o.fresnelFilm;
    return o;
}

inline float EvalZeroMeanCausticWave(Float2 pSurfXY, Float2 gustAdvXY, float tWrap) noexcept
{
    const float qx = pSurfXY.x * 0.055f - gustAdvXY.x;
    const float qy = pSurfXY.y * 0.055f - gustAdvXY.y;
    const float w1 = std::sin(qx * 1.00f + qy * 0.57f - tWrap * 0.85f);
    const float w2 = std::cos(qx * 0.63f - qy * 1.19f + tWrap * 0.62f + 1.31f);
    const float w3 = std::sin(qx * 1.47f - qy * 0.93f - tWrap * 1.14f + 2.47f);
    const float w4 = std::cos(qx * 0.88f + qy * 1.53f + tWrap * 0.97f + 0.73f);
    return w1 * 0.34f + w2 * 0.28f + w3 * 0.22f + w4 * 0.16f;
}

inline SpectralCausticResult EvalSpectralCausticDispersion(
    Float3 wpBedAbs,
    Float3 sunDirW,
    float  waterDepthU,
    float  gust01,
    Float2 gustVelXY,
    float  cloudTrans,
    float  simTimeSec) noexcept
{
    SpectralCausticResult o{};
    o.causticRGB      = {1.0f, 1.0f, 1.0f};
    o.chromaticSpread = 0.0f;
    o.focusEnvelope   = 0.0f;

    const float depthU = (std::clamp)(San1(waterDepthU, 0.0f), 0.0f, 1200.0f);
    if (depthU <= 0.5f) { return o; }

    const Float3 sRaw{San1(sunDirW.x, 0.3f), San1(sunDirW.y, 0.2f), San1(sunDirW.z, 0.9f)};
    const float  sL2 = sRaw.x * sRaw.x + sRaw.y * sRaw.y + sRaw.z * sRaw.z;
    const Float3 sDir = (sL2 > 1.0e-6f)
        ? Float3{sRaw.x / std::sqrt(sL2), sRaw.y / std::sqrt(sL2), sRaw.z / std::sqrt(sL2)}
        : Float3{0.0f, 0.0f, 1.0f};
    if (sDir.z <= 0.04f) { return o; }

    const Float2 bedXY{San1(wpBedAbs.x, 0.0f), San1(wpBedAbs.y, 0.0f)};
    const float  g01   = Sat(gust01);
    const Float2 gVel{
        (std::clamp)(San1(gustVelXY.x, 0.0f), -2.0f, 2.0f),
        (std::clamp)(San1(gustVelXY.y, 0.0f), -2.0f, 2.0f)
    };
    const float  ct    = Sat(cloudTrans);
    const float  tSafe = San1(simTimeSec, 0.0f);
    const float  tWrap = std::fmod(std::fabs(tSafe), 4096.0f) * (tSafe < 0.0f ? -1.0f : 1.0f);
    const Float2 gAdv{gVel.x * (tWrap * (0.28f + 0.42f * g01)), gVel.y * (tWrap * (0.28f + 0.42f * g01))};

    const float focusEnv = Smooth(0.5f, 20.0f, depthU) * std::exp(-(std::min)(depthU / 380.0f, 20.0f)) * ct;
    o.focusEnvelope = focusEnv;

    const float kInvIor[3] = {1.0f / 1.331f, 1.0f / 1.333f, 1.0f / 1.339f};
    const float modAmp = (0.48f + 0.24f * g01) * focusEnv;
    float cBand[3]{};
    for (int ci = 0; ci < 3; ++ci) {
        const float eta = kInvIor[ci];
        const float rx  = -sDir.x * eta;
        const float ry  = -sDir.y * eta;
        const float rz  = -std::sqrt((std::max)(1.0f - (rx * rx + ry * ry), 0.09f));
        const float lev = (depthU / (std::max)(-rz, 0.25f)) * (1.0f + static_cast<float>(ci - 1) * 0.35f);
        const Float2 surfXY{bedXY.x - rx * lev, bedXY.y - ry * lev};
        const float zmWave = EvalZeroMeanCausticWave(surfXY, gAdv, tWrap);
        cBand[ci] = (std::clamp)(1.0f + zmWave * modAmp, 0.28f, 1.72f);
    }

    o.causticRGB = {cBand[0], cBand[1], cBand[2]};
    o.chromaticSpread = std::fabs(cBand[0] - cBand[2]);
    return o;
}

} // namespace raw::bridge::wave80
