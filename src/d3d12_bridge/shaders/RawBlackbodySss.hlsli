// RawBlackbodySss.hlsli: Wave 79 Physically-Bounded Planck Blackbody Fire/Torch/Brazier Emission
// & Volumetric Smoke Self-Shadowing + Character Skin/Fur/Cloth Burley-Normalized SSS Guard.
// Self-contained clean-room MIT HLSL library (SM 6.6 / 6.9 / 6.10, 0 NaN/Inf, white-furnace <= 1.0).
// Original work, MIT.
#ifndef RAW_BLACKBODY_SSS_HLSLI
#define RAW_BLACKBODY_SSS_HLSLI

struct RawBlackbodyFireSmokeResult {
    float3 chromaticity;     // Peak-normalized Rec.709/Rec.2020-safe Planck chromaticity in [0, 1]
    float3 fireEmission;     // Hue-preserving soft-shouldered HDR emission (zero salmon/magenta clip!)
    float  smokeTrans;       // Beer-Lambert smoke self-shadowing transmittance strictly in [0, 1]
    float3 smokeUnderScatter;// Albedo-bounded warm soot underside scatter (<= 0.48 * fireEmission * (1 - T))
};

struct RawBurleySssBudget {
    float3 albedoEff;        // Energy-conserving diffuse base reflectance
    float3 sssWrap;          // Burley-normalized hemoglobin/cloth subsurface wrap term
    float  fresnelSpec;      // Schlick sebum / cloth-fuzz Fresnel reflectance
    float  totalEnergyMax;   // max_c(albedoEff + sssWrap) + fresnelSpec, algebraically <= 1.0!
};

float rawBbSan1(float v, float fb) {
    return isfinite(v) ? v : fb;
}

float3 rawBbSan3(float3 v, float capVal) {
    return all(isfinite(v)) ? clamp(v, 0.0f.xxx, capVal.xxx) : 0.0f.xxx;
}

// -----------------------------------------------------------------------------
// 1. Physically-Bounded Planck Blackbody Fire/Torch/Brazier Emission & Smoke
// -----------------------------------------------------------------------------
// Evaluates exact Planck spectral radiance at Rec.709/Rec.2020-safe bands
// (615 nm, 540 nm, 465 nm) for 1400 K..3200 K flames/braziers/torches, applies
// a chroma-preserving HDR soft shoulder (scaling RGB uniformly so hue NEVER
// clips to salmon or magenta!), and attenuates via exact Beer-Lambert smoke
// self-shadowing T_smoke = exp(-max(smokeOpticalDepth, 0)).
RawBlackbodyFireSmokeResult rawEvalBoundedBlackbodyFireSmoke(
    float tempK,
    float intensityScale,
    float smokeOpticalDepth,
    float sootAlbedo01)
{
    RawBlackbodyFireSmokeResult o;
    o.chromaticity      = 0.0f.xxx;
    o.fireEmission      = 0.0f.xxx;
    o.smokeTrans        = 1.0f;
    o.smokeUnderScatter = 0.0f.xxx;

    const float tRaw    = rawBbSan1(tempK, 0.0f);
    const float iScale  = clamp(rawBbSan1(intensityScale, 1.0f), 0.0f, 8.0f);
    const float odSmoke = clamp(max(rawBbSan1(smokeOpticalDepth, 0.0f), 0.0f), 0.0f, 32.0f);
    const float sootAlb = clamp(rawBbSan1(sootAlbedo01, 0.36f), 0.0f, 0.48f);

    o.smokeTrans = saturate(exp(-odSmoke));
    if (tRaw < 700.0f) {
        return o;
    }

    const float T = clamp(tRaw, 700.0f, 6500.0f);
    // Exact Planck law at RGB centroids (615, 540, 465 nm), stabilized by subtracting min exponent:
    const float3 lamNm = float3(615.0f, 540.0f, 465.0f);
    const float3 w5    = pow(550.0f / lamNm, 5.0f);
    const float3 ex    = 1.4387768e7f / (lamNm * T);
    const float  exMin = min(ex.r, min(ex.g, ex.b));
    const float3 rad   = (w5 * exp(-min(ex - exMin, 60.0f.xxx)))
                       / max(1.0f - exp(-min(ex, 60.0f.xxx)), 1.0e-6f.xxx);
    const float  peak  = max(rad.r, max(rad.g, rad.b));
    const float3 chroma = (peak > 1.0e-7f) ? saturate(rad / peak) : 0.0f.xxx;
    o.chromaticity = chroma;

    // Smooth Stefan-Boltzmann visible intensity ramp (normalized at 2000 K) + hue-preserving HDR shoulder:
    const float visRamp = smoothstep(750.0f, 1200.0f, T);
    const float tNorm   = T * (1.0f / 2000.0f);
    const float rawLum  = visRamp * min(tNorm * tNorm * tNorm * tNorm, 12.0f) * iScale;
    // Uniform scalar shoulder compresses peak into [0, 3.6) while keeping exact R:G:B chromaticity ratio!
    const float shoulderLum = 3.6f * (rawLum / (3.6f + rawLum));
    const float3 emit = chroma * shoulderLum;

    o.fireEmission = rawBbSan3(emit * o.smokeTrans, 4.0f);
    // Warm soot underside scattering strictly bounded by sootAlbedo (<= 0.48) * (1 - T_smoke) * emit:
    const float3 sootTint = float3(1.00f, 0.72f, 0.42f);
    o.smokeUnderScatter = rawBbSan3(emit * sootTint * (sootAlb * (1.0f - o.smokeTrans)), 2.0f);
    return o;
}

// -----------------------------------------------------------------------------
// 2. Character Skin & Fur/Cloth Burley-Normalized Subsurface Scattering Guard
// -----------------------------------------------------------------------------
// Evaluates Christensen-Burley normalized diffusion profile wrap + Schlick
// sebum/cloth Fresnel with strict algebraic white-furnace energy conservation:
//   max_c(albedoEff + sssWrap) + fresnelSpec <= 1.0 for ALL inputs!
// Protects guards & NPCs in exteriors and interiors from waxy blowouts or
// dark shadow banding.
RawBurleySssBudget rawEvalBurleySkinClothSss(
    float3 baseAlbedo,
    float  ndotl,
    float  ndotv,
    float  roughness,
    bool   isSkin)
{
    RawBurleySssBudget b;
    const float3 albSafe = rawBbSan3(baseAlbedo, 0.90f);
    const float  nl      = clamp(rawBbSan1(ndotl, 0.5f), -1.0f, 1.0f);
    const float  nv      = saturate(rawBbSan1(ndotv, 0.5f));
    const float  rSafe   = clamp(rawBbSan1(roughness, isSkin ? 0.42f : 0.72f), 0.08f, 0.96f);

    // Schlick Fresnel (sebum F0=0.028 for skin, rough woven cloth/fur F0=0.018):
    const float f0 = isSkin ? 0.028f : 0.018f;
    const float fRaw = f0 + (1.0f - f0) * pow(1.0f - nv, 5.0f);
    // Roughness attenuates grazing mirror Fresnel cleanly on matte cloth/skin:
    const float fSpec = saturate(fRaw * lerp(1.0f, 0.42f, rSafe * rSafe));
    const float remEnergy = max(1.0f - fSpec, 0.0f);

    // Christensen-Burley normalized radial diffusion terminator profile across shadow boundary:
    //   R_burley(u) = 0.25 * exp(-u) + 0.75 * exp(-u / 3), normalized to peak <= 1.0 at terminator (nl = 0).
    const float uTerm = abs(nl) * (isSkin ? 3.2f : 2.6f);
    const float burleyProfile = 0.25f * exp(-min(uTerm, 24.0f)) + 0.75f * exp(-min(uTerm * (1.0f / 3.0f), 24.0f));
    const float3 scatterTint  = isSkin ? float3(0.86f, 0.28f, 0.14f) : float3(0.72f, 0.64f, 0.56f);
    const float  sssStrength  = isSkin ? 0.20f : 0.12f;

    // Partition remaining hemispherical budget strictly between diffuse albedo & Burley SSS wrap:
    const float3 rawDiff = albSafe * remEnergy;
    const float3 rawSss  = albSafe * scatterTint * (burleyProfile * sssStrength * remEnergy);
    const float3 combined = rawDiff + rawSss;
    const float  maxComb  = max(combined.r, max(combined.g, combined.b));
    const float  normScale = (maxComb > remEnergy && maxComb > 1.0e-6f) ? (remEnergy / maxComb) : 1.0f;

    b.albedoEff      = rawDiff * normScale;
    b.sssWrap        = rawSss  * normScale;
    b.fresnelSpec    = fSpec;
    const float3 sum = b.albedoEff + b.sssWrap;
    b.totalEnergyMax = max(sum.r, max(sum.g, sum.b)) + b.fresnelSpec;
    return b;
}

#endif
