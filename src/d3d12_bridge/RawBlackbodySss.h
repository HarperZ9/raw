#pragma once
// RawBlackbodySss.h: Wave 79 C++23 Mirror of RawBlackbodySss.hlsli
// Physically-Bounded Planck Blackbody Fire/Torch/Brazier Emission & Volumetric Smoke
// Self-Shadowing + Character Skin/Fur/Cloth Burley-Normalized Subsurface Scattering Guard.
// Clean-room MIT, zero external dependencies, MSVC /W4 /WX clean.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace raw::bridge::wave79
{
struct Float3 { float x, y, z; };

struct BlackbodyFireSmokeResult
{
    Float3 chromaticity;
    Float3 fireEmission;
    float  smokeTrans;
    Float3 smokeUnderScatter;
};

struct BurleySssBudget
{
    Float3 albedoEff;
    Float3 sssWrap;
    float  fresnelSpec;
    float  totalEnergyMax;
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

inline BlackbodyFireSmokeResult EvalBoundedBlackbodyFireSmoke(
    float tempK,
    float intensityScale,
    float smokeOpticalDepth,
    float sootAlbedo01) noexcept
{
    BlackbodyFireSmokeResult o{};
    o.chromaticity      = {0.0f, 0.0f, 0.0f};
    o.fireEmission      = {0.0f, 0.0f, 0.0f};
    o.smokeTrans        = 1.0f;
    o.smokeUnderScatter = {0.0f, 0.0f, 0.0f};

    const float tRaw    = San1(tempK, 0.0f);
    const float iScale  = (std::clamp)(San1(intensityScale, 1.0f), 0.0f, 8.0f);
    const float odSmoke = (std::clamp)((std::max)(San1(smokeOpticalDepth, 0.0f), 0.0f), 0.0f, 32.0f);
    const float sootAlb = (std::clamp)(San1(sootAlbedo01, 0.36f), 0.0f, 0.48f);

    o.smokeTrans = Sat(std::exp(-odSmoke));
    if (tRaw < 700.0f) {
        return o;
    }

    const float T = (std::clamp)(tRaw, 700.0f, 6500.0f);
    const float lam[3] = {615.0f, 540.0f, 465.0f};
    float ex[3]{};
    float w5[3]{};
    for (int i = 0; i < 3; ++i) {
        w5[i] = std::pow(550.0f / lam[i], 5.0f);
        ex[i] = 1.4387768e7f / (lam[i] * T);
    }
    const float exMin = (std::min)(ex[0], (std::min)(ex[1], ex[2]));
    float rad[3]{};
    for (int i = 0; i < 3; ++i) {
        const float num = w5[i] * std::exp(-(std::min)(ex[i] - exMin, 60.0f));
        const float den = (std::max)(1.0f - std::exp(-(std::min)(ex[i], 60.0f)), 1.0e-6f);
        rad[i] = num / den;
    }
    const float peak = (std::max)(rad[0], (std::max)(rad[1], rad[2]));
    const Float3 chroma = (peak > 1.0e-7f)
        ? Float3{Sat(rad[0] / peak), Sat(rad[1] / peak), Sat(rad[2] / peak)}
        : Float3{0.0f, 0.0f, 0.0f};
    o.chromaticity = chroma;

    const float visRamp     = Smooth(750.0f, 1200.0f, T);
    const float tNorm       = T * (1.0f / 2000.0f);
    const float rawLum      = visRamp * (std::min)(tNorm * tNorm * tNorm * tNorm, 12.0f) * iScale;
    const float shoulderLum = 3.6f * (rawLum / (3.6f + rawLum));
    const Float3 emit{chroma.x * shoulderLum, chroma.y * shoulderLum, chroma.z * shoulderLum};

    o.fireEmission = San3({emit.x * o.smokeTrans, emit.y * o.smokeTrans, emit.z * o.smokeTrans}, 4.0f);
    const float sc = sootAlb * (1.0f - o.smokeTrans);
    o.smokeUnderScatter = San3({emit.x * 1.00f * sc, emit.y * 0.72f * sc, emit.z * 0.42f * sc}, 2.0f);
    return o;
}

inline BurleySssBudget EvalBurleySkinClothSss(
    Float3 baseAlbedo,
    float  ndotl,
    float  ndotv,
    float  roughness,
    bool   isSkin) noexcept
{
    BurleySssBudget b{};
    const Float3 albSafe = San3(baseAlbedo, 0.90f);
    const float  nl      = (std::clamp)(San1(ndotl, 0.5f), -1.0f, 1.0f);
    const float  nv      = Sat(ndotv);
    const float  rSafe   = (std::clamp)(San1(roughness, isSkin ? 0.42f : 0.72f), 0.08f, 0.96f);

    const float f0        = isSkin ? 0.028f : 0.018f;
    const float fRaw      = f0 + (1.0f - f0) * std::pow(1.0f - nv, 5.0f);
    const float fSpec     = Sat(fRaw * (1.0f + (0.42f - 1.0f) * (rSafe * rSafe)));
    const float remEnergy = (std::max)(1.0f - fSpec, 0.0f);

    const float uTerm = std::fabs(nl) * (isSkin ? 3.2f : 2.6f);
    const float burleyProfile = 0.25f * std::exp(-(std::min)(uTerm, 24.0f))
                              + 0.75f * std::exp(-(std::min)(uTerm * (1.0f / 3.0f), 24.0f));
    const Float3 tint = isSkin ? Float3{0.86f, 0.28f, 0.14f} : Float3{0.72f, 0.64f, 0.56f};
    const float  sssK = (isSkin ? 0.20f : 0.12f) * burleyProfile * remEnergy;

    const Float3 rawDiff{albSafe.x * remEnergy, albSafe.y * remEnergy, albSafe.z * remEnergy};
    const Float3 rawSss{albSafe.x * tint.x * sssK, albSafe.y * tint.y * sssK, albSafe.z * tint.z * sssK};
    const float  maxComb = (std::max)(rawDiff.x + rawSss.x, (std::max)(rawDiff.y + rawSss.y, rawDiff.z + rawSss.z));
    const float  normScale = (maxComb > remEnergy && maxComb > 1.0e-6f) ? (remEnergy / maxComb) : 1.0f;

    b.albedoEff   = {rawDiff.x * normScale, rawDiff.y * normScale, rawDiff.z * normScale};
    b.sssWrap     = {rawSss.x  * normScale, rawSss.y  * normScale, rawSss.z  * normScale};
    b.fresnelSpec = fSpec;
    const float maxSum = (std::max)(b.albedoEff.x + b.sssWrap.x,
                         (std::max)(b.albedoEff.y + b.sssWrap.y, b.albedoEff.z + b.sssWrap.z));
    b.totalEnergyMax = maxSum + b.fresnelSpec;
    return b;
}

} // namespace raw::bridge::wave79
