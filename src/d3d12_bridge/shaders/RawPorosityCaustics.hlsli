// RawPorosityCaustics.hlsli: Wave 80 Milestone Porosity-Coupled Wet-Stone/Mud/Wood Albedo
// Darkening & Micro-Normal Water-Film Smoothing + Three-Band Cauchy Spectral Water-Bed Caustic
// Dispersion (n_R=1.331, n_G=1.333, n_B=1.339, white-furnace mean == 1.0).
// Self-contained clean-room MIT HLSL library (SM 6.6 / 6.9 / 6.10, 0 screen-UV dependence, 0 NaN/Inf).
// Reuses Wave 76 RawPrecipThermal.hlsli::rawPtLaplacianConcavity with zero duplicate code.
// Original work, MIT.
#ifndef RAW_POROSITY_CAUSTICS_HLSLI
#define RAW_POROSITY_CAUSTICS_HLSLI

#include "RawPrecipThermal.hlsli"

struct RawPorosityWetFilmResult {
    float3 albedoWet;        // Porous darkened wet albedo strictly <= baseAlbedo
    float3 normalFilm;       // Unit water-film smoothed normal (flattened toward macro normal in puddles)
    float  roughnessFilm;    // Tightened wet/puddle clearcoat roughness in [0.04, 0.95]
    float  fresnelFilm;      // Dielectric water film Fresnel (F0 = 0.02)
    float  totalEnergyMax;   // max_c(albedoWet) + fresnelFilm, algebraically <= 1.0!
};

struct RawSpectralCausticResult {
    float3 causticRGB;       // Three-band Cauchy-dispersed caustic multiplier (white-furnace spatial mean == 1.0)
    float  chromaticSpread;  // Max |R - B| dispersion delta across submerged bed
    float  focusEnvelope;    // Depth & cloud-transmittance caustic focusing weight in [0, 1]
};

float rawPcSan1(float v, float fb) {
    return isfinite(v) ? v : fb;
}

float3 rawPcSan3(float3 v, float capVal) {
    return all(isfinite(v)) ? clamp(v, 0.0f.xxx, capVal.xxx) : 0.0f.xxx;
}

// -----------------------------------------------------------------------------
// 1. Porosity-Coupled Wet-Stone / Mud / Wood Albedo Darkening & Film Smoothing
// -----------------------------------------------------------------------------
// Couples Wave 75/76 world-space heightfield concavity & active thermal melt
// wetness into physical porous internal-reflection darkening:
//   albedoWet = pow(albedo, 1 + k_porosity * wEff)
// and water-film micro-normal flattening + clearcoat roughness tightening
// (0.04..0.12 in pooled cavities). Strictly identity on living green pines
// (!isGreenPine) and algebraically energy-conserving (albedoWet + fresnel <= 1.0).
RawPorosityWetFilmResult rawEvalPorosityWetFilm(
    float3 wpAbs,
    float3 baseAlbedo,
    float3 microNormalW,
    float3 macroNormalW,
    float  baseRoughness,
    float  wet01,
    float  puddle01,
    float  porosity01,
    float  ndotv,
    bool   isGreenPine)
{
    RawPorosityWetFilmResult o;
    const float3 albSafe = rawPcSan3(baseAlbedo, 0.92f);
    const float  rBase   = clamp(rawPcSan1(baseRoughness, 0.65f), 0.04f, 0.95f);
    const float3 nMicRaw = float3(rawPcSan1(microNormalW.x, 0.0f), rawPcSan1(microNormalW.y, 0.0f), rawPcSan1(microNormalW.z, 1.0f));
    const float3 nMacRaw = float3(rawPcSan1(macroNormalW.x, 0.0f), rawPcSan1(macroNormalW.y, 0.0f), rawPcSan1(macroNormalW.z, 1.0f));
    const float  micL2   = dot(nMicRaw, nMicRaw);
    const float  macL2   = dot(nMacRaw, nMacRaw);
    const float3 nMic    = (micL2 > 1.0e-6f) ? (nMicRaw * rsqrt(micL2)) : float3(0.0f, 0.0f, 1.0f);
    const float3 nMac    = (macL2 > 1.0e-6f) ? (nMacRaw * rsqrt(macL2)) : float3(0.0f, 0.0f, 1.0f);
    const float  nv      = saturate(rawPcSan1(ndotv, 0.5f));

    if (isGreenPine) {
        o.albedoWet      = albSafe;
        o.normalFilm     = nMic;
        o.roughnessFilm  = rBase;
        o.fresnelFilm    = 0.02f;
        o.totalEnergyMax = min(max(albSafe.r, max(albSafe.g, albSafe.b)) + 0.02f, 1.0f);
        return o;
    }

    const float wIn  = saturate(rawPcSan1(wet01, 0.0f));
    const float pIn  = saturate(rawPcSan1(puddle01, 0.0f));
    const float por  = saturate(rawPcSan1(porosity01, 0.60f));
    const float conc = rawPtLaplacianConcavity(float2(rawPcSan1(wpAbs.x, 0.0f), rawPcSan1(wpAbs.y, 0.0f)));

    // Effective water-film saturation in surface pores (boosted in concave drainage hollows & puddles):
    const float wEff = saturate(max(wIn, pIn) * (0.76f + 0.24f * conc) + pIn * 0.30f);

    // Physical porous darkening (internal reflection inside water-filled pores):
    // Since albSafe in [0, 0.92] and expPor >= 1.0, pow(alb, expPor) <= alb STRICTLY!
    const float expPor = 1.0f + por * wEff * 0.85f;
    const float3 darkAlb = float3(
        (albSafe.r > 1.0e-5f) ? pow(albSafe.r, expPor) : 0.0f,
        (albSafe.g > 1.0e-5f) ? pow(albSafe.g, expPor) : 0.0f,
        (albSafe.b > 1.0e-5f) ? pow(albSafe.b, expPor) : 0.0f);

    // Water-film micro-normal smoothing & clearcoat roughness tightening (0.04..0.12 in wet cavities):
    const float smoothW = saturate(pIn * 0.85f + wEff * por * 0.42f);
    const float3 nBlend = lerp(nMic, nMac, smoothW);
    const float  nBL2   = dot(nBlend, nBlend);
    o.normalFilm = (nBL2 > 1.0e-6f) ? (nBlend * rsqrt(nBL2)) : nMac;

    const float rTarget = lerp(0.12f, 0.04f, pIn);
    o.roughnessFilm = clamp(lerp(rBase, min(rBase, rTarget), wEff), 0.04f, 0.95f);

    // Dielectric water Fresnel (F0 = 0.02), roughness-damped & strictly white-furnace bounded:
    const float fRaw = 0.02f + 0.98f * pow(1.0f - nv, 5.0f);
    o.fresnelFilm = saturate(fRaw * lerp(1.0f, 0.42f, o.roughnessFilm * o.roughnessFilm));
    const float remBudget = max(1.0f - o.fresnelFilm, 0.0f);
    o.albedoWet = min(darkAlb, remBudget.xxx);
    o.totalEnergyMax = max(o.albedoWet.r, max(o.albedoWet.g, o.albedoWet.b)) + o.fresnelFilm;
    return o;
}

// -----------------------------------------------------------------------------
// 2. Three-Band Spectral Water-Bed Caustic Dispersion (Cauchy IOR 1.331/1.333/1.339)
// -----------------------------------------------------------------------------
// Pure world-space zero-mean wave-focusing perturbation refracted through exact
// Cauchy water refractive indices (R=1.331, G=1.333, B=1.339). Spatial mean over
// any wavelength cycle is identically 1.0 (white-furnace flux conservation!),
// coupled to Wave 74 wind gusts and Wave 76 cloud transmittance (0 screen-UV drift).
float rawEvalZeroMeanCausticWave(float2 pSurfXY, float2 gustAdvXY, float tWrap) {
    const float2 q = pSurfXY * 0.055f - gustAdvXY;
    // Exact zero-mean sum of incommensurate harmonic wave-focusing modes (integral over 2pi == 0.0):
    const float w1 = sin(q.x * 1.00f + q.y * 0.57f - tWrap * 0.85f);
    const float w2 = cos(q.x * 0.63f - q.y * 1.19f + tWrap * 0.62f + 1.31f);
    const float w3 = sin(q.x * 1.47f - q.y * 0.93f - tWrap * 1.14f + 2.47f);
    const float w4 = cos(q.x * 0.88f + q.y * 1.53f + tWrap * 0.97f + 0.73f);
    return (w1 * 0.34f + w2 * 0.28f + w3 * 0.22f + w4 * 0.16f);
}

RawSpectralCausticResult rawEvalSpectralCausticDispersion(
    float3 wpBedAbs,
    float3 sunDirW,
    float  waterDepthU,
    float  gust01,
    float2 gustVelXY,
    float  cloudTrans,
    float  simTimeSec)
{
    RawSpectralCausticResult o;
    o.causticRGB      = 1.0f.xxx;
    o.chromaticSpread = 0.0f;
    o.focusEnvelope   = 0.0f;

    const float depthU = clamp(rawPcSan1(waterDepthU, 0.0f), 0.0f, 1200.0f);
    if (depthU <= 0.5f) {
        return o;
    }

    const float3 sRaw = float3(rawPcSan1(sunDirW.x, 0.3f), rawPcSan1(sunDirW.y, 0.2f), rawPcSan1(sunDirW.z, 0.9f));
    const float  sL2  = dot(sRaw, sRaw);
    const float3 sDir = (sL2 > 1.0e-6f) ? (sRaw * rsqrt(sL2)) : float3(0.0f, 0.0f, 1.0f);
    if (sDir.z <= 0.04f) {
        return o;
    }

    const float2 bedXY = float2(rawPcSan1(wpBedAbs.x, 0.0f), rawPcSan1(wpBedAbs.y, 0.0f));
    const float  g01   = saturate(rawPcSan1(gust01, 0.0f));
    const float2 gVel  = clamp(float2(rawPcSan1(gustVelXY.x, 0.0f), rawPcSan1(gustVelXY.y, 0.0f)), -2.0f.xx, 2.0f.xx);
    const float  ct    = saturate(rawPcSan1(cloudTrans, 1.0f));
    const float  tSafe = rawPcSan1(simTimeSec, 0.0f);
    const float  tWrap = fmod(abs(tSafe), 4096.0f) * (tSafe < 0.0f ? -1.0f : 1.0f);
    const float2 gAdv  = gVel * (tWrap * (0.28f + 0.42f * g01));

    // Depth-calibrated caustic focusing envelope (zero at 0 depth, peaks in clear shallows, fades smoothly deep):
    const float focusEnv = smoothstep(0.5f, 20.0f, depthU) * exp(-min(depthU / 380.0f, 20.0f)) * ct;
    o.focusEnvelope = focusEnv;

    // Exact Snell refraction across 3 Cauchy wavelengths: R=1.331 (650nm), G=1.333 (550nm), B=1.339 (450nm):
    static const float kInvIor[3] = { 1.0f / 1.331f, 1.0f / 1.333f, 1.0f / 1.339f };
    const float modAmp = (0.48f + 0.24f * g01) * focusEnv;
    float cBand[3];
    [unroll] for (int ci = 0; ci < 3; ++ci) {
        const float eta = kInvIor[ci];
        const float2 rXY = -sDir.xy * eta;
        const float  rZ  = -sqrt(max(1.0f - dot(rXY, rXY), 0.09f));
        // Amplified dispersion lever at optical bed scale so R/G/B fringes separate cleanly without neon blowout:
        const float2 surfXY = bedXY - rXY * (depthU / max(-rZ, 0.25f)) * (1.0f + float(ci - 1) * 0.35f);
        const float  zmWave = rawEvalZeroMeanCausticWave(surfXY, gAdv, tWrap);
        // Zero-mean perturbation around 1.0 preserves exact white-furnace spatial mean == 1.0!
        cBand[ci] = clamp(1.0f + zmWave * modAmp, 0.28f, 1.72f);
    }

    o.causticRGB = float3(cBand[0], cBand[1], cBand[2]);
    o.chromaticSpread = abs(cBand[0] - cBand[2]);
    return o;
}

#endif
