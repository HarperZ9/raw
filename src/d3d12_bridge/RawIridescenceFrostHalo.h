#pragma once
// RawIridescenceFrostHalo.h: Wave 81 C++23 Mirror of RawIridescenceFrostHalo.hlsli
// Physical Airy Thin-Film Interference (650/532/450 nm), Anisotropic Glaze-Ice & Frost-Crystal
// Phase Transition (-4 C .. +2 C), and Atmospheric Rain-Mist Bow (42 deg) & 22-Degree Ice Halo.
// Clean-room MIT, zero external dependencies, MSVC /W4 /WX clean.
#include "RawPrecipThermal.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace raw::bridge::wave81
{
inline constexpr float kPi = 3.14159265f;

struct Float2 { float x, y; };
struct Float3 { float x, y, z; };

struct ThinFilmResult
{
    Float3 reflectanceRGB;
    float  filmThicknessNm;
    float  iridescenceWeight;
    float  maxReflectance;
};

struct FrostGlazeResult
{
    Float3 albedoOut;
    Float3 normalOut;
    float  roughnessOut;
    float  anisoRatio;
    float  glazeMask;
    float  frostMask;
    float  fresnelOut;
    float  totalEnergyMax;
};

struct AtmoHaloResult
{
    Float3 haloRGB;
    float  iceHalo22Weight;
    float  rainBow42Weight;
    float  chromaticSpread;
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

inline ThinFilmResult EvalThinFilmIridescence(
    Float3 wpAbs,
    float  cosTheta,
    Float3 baseF0,
    float  filmThicknessBaseNm,
    float  filmMask01,
    float  filmIor,
    float  baseIor,
    bool   isGreenPine) noexcept
{
    ThinFilmResult o{};
    const Float3 f0Safe = San3(baseF0, 1.0f);
    const float  mask   = Sat(filmMask01);
    const float  dBase  = San1(filmThicknessBaseNm, 0.0f);

    if (isGreenPine || mask <= 0.005f || dBase < 1.0f) {
        o.reflectanceRGB    = f0Safe;
        o.filmThicknessNm   = 0.0f;
        o.iridescenceWeight = 0.0f;
        o.maxReflectance    = (std::max)(f0Safe.x, (std::max)(f0Safe.y, f0Safe.z));
        return o;
    }

    const float c    = Sat(cosTheta);
    const float nF   = (std::clamp)(San1(filmIor, 1.333f), 1.05f, 2.40f);
    const float nB   = (std::clamp)(San1(baseIor, 1.52f), 1.05f, 3.00f);
    const float conc = static_cast<float>(raw::bridge::EvalPtLaplacianConcavity(
        San1(wpAbs.x, 0.0f), San1(wpAbs.y, 0.0f)));
    const float dNm  = (std::clamp)(dBase * (0.72f + 0.56f * conc), 80.0f, 900.0f);

    const float sin2I = (std::max)(1.0f - c * c, 0.0f);
    const float cosT  = std::sqrt((std::max)(1.0f - sin2I / (nF * nF), 0.04f));
    const float opd   = 2.0f * nF * dNm * cosT;

    const float r12n = (1.0f - nF) / (1.0f + nF);
    const float r23n = (nF - nB) / (nF + nB);
    const float R12  = Sat(r12n * r12n + (1.0f - r12n * r12n) * std::pow(1.0f - c, 5.0f));
    const float R23  = (std::clamp)(r23n * r23n, 0.0f, 0.995f);

    const float phaseFlip = (r12n * r23n < 0.0f) ? kPi : 0.0f;
    const float lambdas[3] = {650.0f, 532.0f, 450.0f};
    const float twoRootR   = 2.0f * std::sqrt((std::max)(R12 * R23, 0.0f));
    float rAiry[3]{};
    for (int i = 0; i < 3; ++i) {
        const float delta = (2.0f * kPi * opd) / lambdas[i] + phaseFlip;
        const float cosD  = std::cos(delta);
        const float num   = (std::max)((R12 + R23) + twoRootR * cosD, 0.0f);
        const float den   = (std::max)((1.0f + R12 * R23) + twoRootR * cosD, 1.0e-5f);
        rAiry[i] = Sat(num / den);
    }

    o.reflectanceRGB = {
        Sat(f0Safe.x + (rAiry[0] - f0Safe.x) * mask),
        Sat(f0Safe.y + (rAiry[1] - f0Safe.y) * mask),
        Sat(f0Safe.z + (rAiry[2] - f0Safe.z) * mask)
    };
    o.filmThicknessNm   = dNm;
    o.iridescenceWeight = mask;
    o.maxReflectance    = (std::max)(o.reflectanceRGB.x, (std::max)(o.reflectanceRGB.y, o.reflectanceRGB.z));
    return o;
}

inline FrostGlazeResult EvalFrostGlazeCoupling(
    Float3 wpAbs,
    Float3 macroNormalW,
    Float3 baseAlbedo,
    float  baseRoughness,
    float  wet01,
    float  snow01,
    float  tempC,
    Float2 windDirXY,
    float  windSpeed01,
    float  fireHeat01,
    float  ndotv,
    bool   isGreenPine) noexcept
{
    FrostGlazeResult o{};
    const Float3 albSafe = San3(baseAlbedo, 0.92f);
    const float  rBase   = (std::clamp)(San1(baseRoughness, 0.65f), 0.05f, 0.95f);
    const Float3 nRaw{San1(macroNormalW.x, 0.0f), San1(macroNormalW.y, 0.0f), San1(macroNormalW.z, 1.0f)};
    const float  nL2     = nRaw.x * nRaw.x + nRaw.y * nRaw.y + nRaw.z * nRaw.z;
    const Float3 nMac    = (nL2 > 1.0e-6f)
        ? Float3{nRaw.x / std::sqrt(nL2), nRaw.y / std::sqrt(nL2), nRaw.z / std::sqrt(nL2)}
        : Float3{0.0f, 0.0f, 1.0f};
    const float nv = Sat(ndotv);

    if (isGreenPine) {
        o.albedoOut      = albSafe;
        o.normalOut      = nMac;
        o.roughnessOut   = rBase;
        o.anisoRatio     = 0.0f;
        o.glazeMask      = 0.0f;
        o.frostMask      = 0.0f;
        o.fresnelOut     = 0.02f;
        o.totalEnergyMax = (std::min)((std::max)(albSafe.x, (std::max)(albSafe.y, albSafe.z)) + 0.02f, 1.0f);
        return o;
    }

    const float wIn    = Sat(wet01);
    const float sIn    = Sat(snow01);
    const float heat   = Sat(fireHeat01);
    const float windSp = Sat(windSpeed01);
    const float tEff   = (std::clamp)(San1(tempC, 4.0f), -45.0f, 45.0f) + heat * 10.0f;

    const float freeze01 = 1.0f - Smooth(-4.0f, 2.0f, tEff);
    if (freeze01 <= 0.002f) {
        o.normalOut    = nMac;
        o.roughnessOut = rBase;
        o.anisoRatio   = 0.0f;
        o.glazeMask    = 0.0f;
        o.frostMask    = 0.0f;
        const float fWarm = Sat((0.02f + 0.98f * std::pow(1.0f - nv, 5.0f)) * (1.0f + (0.42f - 1.0f) * (rBase * rBase)));
        o.fresnelOut   = fWarm;
        const float rem = (std::max)(1.0f - fWarm, 0.0f);
        o.albedoOut    = {(std::min)(albSafe.x, rem), (std::min)(albSafe.y, rem), (std::min)(albSafe.z, rem)};
        o.totalEnergyMax = (std::max)(o.albedoOut.x, (std::max)(o.albedoOut.y, o.albedoOut.z)) + fWarm;
        return o;
    }

    const Float2 wpXY{San1(wpAbs.x, 0.0f), San1(wpAbs.y, 0.0f)};
    const float  conc = static_cast<float>(raw::bridge::EvalPtLaplacianConcavity(wpXY.x, wpXY.y));
    const float  upW  = Smooth(0.20f, 0.75f, nMac.z);

    const float glazeM = Sat(wIn * freeze01 * (0.72f + 0.28f * conc) * (0.35f + 0.65f * upW));
    const float frostM = Sat((1.0f - glazeM * 0.72f) * freeze01 * (std::max)(sIn, windSp * 0.58f)
                             * Smooth(0.5f, -3.5f, tEff) * (0.45f + 0.55f * (1.0f - conc)) * upW);

    const float gScale = 1.0f + (0.80f - 1.0f) * glazeM;
    const Float3 glazeAlb{albSafe.x * gScale, albSafe.y * gScale, albSafe.z * gScale};
    const float fW = frostM * 0.55f;
    const Float3 frostAlb{
        glazeAlb.x + (0.78f - glazeAlb.x) * fW,
        glazeAlb.y + (0.82f - glazeAlb.y) * fW,
        glazeAlb.z + (0.86f - glazeAlb.z) * fW
    };

    const float rGoal  = (std::min)(rBase, 0.14f + (0.06f - 0.14f) * conc);
    const float rGlaze = rBase + (rGoal - rBase) * glazeM;
    const float rFrost = (std::max)(rGlaze, 0.48f);
    o.roughnessOut = (std::clamp)(rGlaze + (rFrost - rGlaze) * (frostM * 0.65f), 0.05f, 0.95f);

    const Float2 wDirRaw{San1(windDirXY.x, 1.0f), San1(windDirXY.y, 0.0f)};
    const float  wDL2 = wDirRaw.x * wDirRaw.x + wDirRaw.y * wDirRaw.y;
    const Float2 wDir = (wDL2 > 1.0e-6f)
        ? Float2{wDirRaw.x / std::sqrt(wDL2), wDirRaw.y / std::sqrt(wDL2)}
        : Float2{1.0f, 0.0f};
    const Float2 wPerp{-wDir.y, wDir.x};
    const float anisoPh = (wpXY.x * wPerp.x + wpXY.y * wPerp.y) * 0.22f
                        + (wpXY.x * wDir.x  + wpXY.y * wDir.y)  * 0.045f;
    const float anisoG  = std::sin(anisoPh) * 0.55f + std::cos(anisoPh * 1.73f + 0.9f) * 0.45f;
    const float pertAmp = glazeM * (0.045f + 0.055f * windSp);
    const Float3 nPert{nMac.x + wPerp.x * (anisoG * pertAmp), nMac.y + wPerp.y * (anisoG * pertAmp), nMac.z};
    const float nPL2 = nPert.x * nPert.x + nPert.y * nPert.y + nPert.z * nPert.z;
    o.normalOut = (nPL2 > 1.0e-6f)
        ? Float3{nPert.x / std::sqrt(nPL2), nPert.y / std::sqrt(nPL2), nPert.z / std::sqrt(nPL2)}
        : nMac;

    o.anisoRatio = Sat(glazeM * (0.25f + 0.40f * windSp)) * 0.65f;
    o.glazeMask  = glazeM;
    o.frostMask  = frostM;

    const float fIceRaw = 0.018f + 0.982f * std::pow(1.0f - nv, 5.0f);
    o.fresnelOut = Sat(fIceRaw * (1.0f + (0.42f - 1.0f) * (o.roughnessOut * o.roughnessOut)));
    const float remBudget = (std::max)(1.0f - o.fresnelOut, 0.0f);
    o.albedoOut = {
        (std::min)(frostAlb.x, remBudget),
        (std::min)(frostAlb.y, remBudget),
        (std::min)(frostAlb.z, remBudget)
    };
    o.totalEnergyMax = (std::max)(o.albedoOut.x, (std::max)(o.albedoOut.y, o.albedoOut.z)) + o.fresnelOut;
    return o;
}

inline AtmoHaloResult EvalAtmoSpectralHalo(
    Float3 viewDirW,
    Float3 sunDirW,
    float  rain01,
    float  snow01,
    float  cloudTrans,
    float  altMeters,
    float  tempC) noexcept
{
    AtmoHaloResult o{};
    o.haloRGB         = {0.0f, 0.0f, 0.0f};
    o.iceHalo22Weight = 0.0f;
    o.rainBow42Weight = 0.0f;
    o.chromaticSpread = 0.0f;

    const Float3 vRaw{San1(viewDirW.x, 0.0f), San1(viewDirW.y, 1.0f), San1(viewDirW.z, 0.2f)};
    const Float3 sRaw{San1(sunDirW.x, 0.0f),  San1(sunDirW.y, 0.8f),  San1(sunDirW.z, 0.6f)};
    const float  vL2  = vRaw.x * vRaw.x + vRaw.y * vRaw.y + vRaw.z * vRaw.z;
    const float  sL2  = sRaw.x * sRaw.x + sRaw.y * sRaw.y + sRaw.z * sRaw.z;
    const Float3 vDir = (vL2 > 1.0e-6f)
        ? Float3{vRaw.x / std::sqrt(vL2), vRaw.y / std::sqrt(vL2), vRaw.z / std::sqrt(vL2)}
        : Float3{0.0f, 1.0f, 0.0f};
    const Float3 sDir = (sL2 > 1.0e-6f)
        ? Float3{sRaw.x / std::sqrt(sL2), sRaw.y / std::sqrt(sL2), sRaw.z / std::sqrt(sL2)}
        : Float3{0.0f, 0.0f, 1.0f};

    const float rain  = Sat(rain01);
    const float snow  = Sat(snow01);
    const float ct    = Sat(cloudTrans);
    const float altM  = (std::clamp)(San1(altMeters, 0.0f), 0.0f, 12000.0f);
    const float tSurf = (std::clamp)(San1(tempC, 8.0f), -50.0f, 50.0f);
    const float tAlt  = tSurf - 0.0065f * altM;
    const float skyUp = Smooth(-0.02f, 0.12f, vDir.z);
    const float sunUp = Smooth(0.02f, 0.14f, sDir.z);

    if (skyUp <= 0.001f || sunUp <= 0.001f || ct <= 0.02f) {
        return o;
    }

    const float cosS = (std::clamp)(vDir.x * sDir.x + vDir.y * sDir.y + vDir.z * sDir.z, -1.0f, 1.0f);

    const float coldIce = Sat(Smooth(1.0f, -8.0f, tAlt) * (std::max)(snow, Smooth(900.0f, 3200.0f, altM) * 0.72f));
    Float3 iceAdd{0.0f, 0.0f, 0.0f};
    if (coldIce > 0.01f) {
        const float thetaS     = std::acos(cosS);
        const float minDevGate = Smooth(0.352f, 0.374f, thetaS);
        const float dR = (thetaS - 0.3760f) / 0.014f;
        const float dG = (thetaS - 0.3812f) / 0.016f;
        const float dB = (thetaS - 0.3892f) / 0.019f;
        const Float3 iceSpec{
            std::exp(-(std::min)(dR * dR, 30.0f)) * minDevGate,
            std::exp(-(std::min)(dG * dG, 30.0f)) * minDevGate,
            std::exp(-(std::min)(dB * dB, 30.0f)) * minDevGate
        };
        const float iceW = Sat(coldIce * sunUp * skyUp * ct);
        o.iceHalo22Weight = iceW * (std::max)(iceSpec.x, (std::max)(iceSpec.y, iceSpec.z));
        iceAdd = {iceSpec.x * (0.14f * iceW), iceSpec.y * (0.14f * iceW), iceSpec.z * (0.14f * iceW)};
    }

    const float rainGate = Smooth(0.05f, 0.35f, rain) * Smooth(-1.5f, 1.5f, tSurf) * Smooth(0.06f, 0.45f, ct);
    Float3 bowAdd{0.0f, 0.0f, 0.0f};
    if (rainGate > 0.005f) {
        const float thetaAnti = std::acos(-cosS);
        const float bR = (thetaAnti - 0.7383f) / 0.016f;
        const float bG = (thetaAnti - 0.7226f) / 0.016f;
        const float bB = (thetaAnti - 0.7086f) / 0.017f;
        const Float3 bowSpec{
            std::exp(-(std::min)(bR * bR, 30.0f)),
            std::exp(-(std::min)(bG * bG, 30.0f)),
            std::exp(-(std::min)(bB * bB, 30.0f))
        };
        const float bowW = Sat(rainGate * sunUp * skyUp);
        o.rainBow42Weight = bowW * (std::max)(bowSpec.x, (std::max)(bowSpec.y, bowSpec.z));
        bowAdd = {bowSpec.x * (0.18f * bowW), bowSpec.y * (0.18f * bowW), bowSpec.z * (0.18f * bowW)};
    }

    o.haloRGB = {
        (std::clamp)(iceAdd.x + bowAdd.x, 0.0f, 0.35f),
        (std::clamp)(iceAdd.y + bowAdd.y, 0.0f, 0.35f),
        (std::clamp)(iceAdd.z + bowAdd.z, 0.0f, 0.35f)
    };
    const float hiC = (std::max)(o.haloRGB.x, (std::max)(o.haloRGB.y, o.haloRGB.z));
    const float loC = (std::min)(o.haloRGB.x, (std::min)(o.haloRGB.y, o.haloRGB.z));
    o.chromaticSpread = (std::max)(hiC - loC, 0.0f);
    return o;
}

} // namespace raw::bridge::wave81
