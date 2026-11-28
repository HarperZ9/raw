// RawIridescenceFrostHalo.hlsli: Wave 81 Physical Airy Thin-Film Frost/Oil/Water Iridescence
// (650/532/450 nm), Anisotropic Glaze-Ice & Frost-Crystal Phase Transition (-4 C .. +2 C),
// and Atmospheric Rain-Mist Bow (42 deg) & 22-Degree Ice-Crystal Halo Spectral Scattering.
// Self-contained clean-room MIT HLSL library (SM 6.6 / 6.9 / 6.10, 0 screen-UV dependence, 0 NaN/Inf).
// Reuses Wave 76 RawPrecipThermal.hlsli::rawPtLaplacianConcavity with zero duplicate code.
// Original work, MIT.
#ifndef RAW_IRIDESCENCE_FROST_HALO_HLSLI
#define RAW_IRIDESCENCE_FROST_HALO_HLSLI

#include "RawPrecipThermal.hlsli"

static const float RAW_W81_PI = 3.14159265f;

struct RawThinFilmResult {
    float3 reflectanceRGB;     // 3-wavelength Airy reflectance (650/532/450 nm), strictly in [0, 1]
    float  filmThicknessNm;    // Effective world-modulated film thickness in nm (80..900 active, 0 inactive)
    float  iridescenceWeight;  // Active thin-film blend weight in [0, 1]
    float  maxReflectance;     // max(r, g, b), algebraically <= 1.0 (white-furnace safe)
};

struct RawFrostGlazeResult {
    float3 albedoOut;          // Energy-conserving albedo after glaze/frost phase transition
    float3 normalOut;          // Unit world-space normal with static wind-aligned anisotropic glaze facets
    float  roughnessOut;       // Coupled roughness in [0.05, 0.95]
    float  anisoRatio;         // Glaze anisotropy ratio in [0.0, 0.65]
    float  glazeMask;          // Specular black-ice / glaze fraction in [0, 1]
    float  frostMask;          // Crystalline hoarfrost rime fraction in [0, 1]
    float  fresnelOut;         // Dielectric ice Fresnel (IOR 1.31, F0 = 0.018), roughness-damped
    float  totalEnergyMax;     // max(albedoOut) + fresnelOut, algebraically <= 1.0
};

struct RawAtmoHaloResult {
    float3 haloRGB;            // Bounded additive spectral sky scattering in [0, 0.35]
    float  iceHalo22Weight;    // 22-deg hexagonal ice-crystal halo weight in [0, 1]
    float  rainBow42Weight;    // 42-deg primary rain-mist bow weight in [0, 1]
    float  chromaticSpread;    // max(haloRGB) - min(haloRGB)
};

float rawIfhSan1(float v, float fb) {
    return isfinite(v) ? v : fb;
}

float3 rawIfhSan3(float3 v, float capVal) {
    return all(isfinite(v)) ? clamp(v, 0.0f.xxx, capVal.xxx) : 0.0f.xxx;
}

// -----------------------------------------------------------------------------
// 1. Physical Airy Thin-Film Interference (650 / 532 / 450 nm, d = 80..900 nm)
// -----------------------------------------------------------------------------
// Computes exact multi-bounce Airy thin-film reflectance coupled to world-space
// concavity (Wave 76/80) and wet/frost film thickness. Algebraically bounded in
// [0, 1] for all wavelengths & viewing angles, and exact identity on dry/matte
// surfaces or living green pines (isGreenPine == true).
RawThinFilmResult rawEvalThinFilmIridescence(
    float3 wpAbs,
    float  cosTheta,
    float3 baseF0,
    float  filmThicknessBaseNm,
    float  filmMask01,
    float  filmIor,
    float  baseIor,
    bool   isGreenPine)
{
    RawThinFilmResult o;
    const float3 f0Safe = rawIfhSan3(baseF0, 1.0f);
    const float  mask   = saturate(rawIfhSan1(filmMask01, 0.0f));
    const float  dBase  = rawIfhSan1(filmThicknessBaseNm, 0.0f);

    if (isGreenPine || mask <= 0.005f || dBase < 1.0f) {
        o.reflectanceRGB    = f0Safe;
        o.filmThicknessNm   = 0.0f;
        o.iridescenceWeight = 0.0f;
        o.maxReflectance    = max(f0Safe.r, max(f0Safe.g, f0Safe.b));
        return o;
    }

    const float c    = saturate(rawIfhSan1(cosTheta, 0.5f));
    const float nF   = clamp(rawIfhSan1(filmIor, 1.333f), 1.05f, 2.40f);
    const float nB   = clamp(rawIfhSan1(baseIor, 1.52f), 1.05f, 3.00f);
    const float conc = rawPtLaplacianConcavity(float2(rawIfhSan1(wpAbs.x, 0.0f), rawIfhSan1(wpAbs.y, 0.0f)));
    const float dNm  = clamp(dBase * (0.72f + 0.56f * conc), 80.0f, 900.0f);

    const float sin2I = max(1.0f - c * c, 0.0f);
    const float cosT  = sqrt(max(1.0f - sin2I / (nF * nF), 0.04f));
    const float opd   = 2.0f * nF * dNm * cosT;

    // Interface reflectances with grazing Schlick envelope; strictly R12 in [0, 1], R23 in [0, 1):
    const float r12n = (1.0f - nF) / (1.0f + nF);
    const float r23n = (nF - nB) / (nF + nB);
    const float R12  = saturate(r12n * r12n + (1.0f - r12n * r12n) * pow(1.0f - c, 5.0f));
    const float R23  = clamp(r23n * r23n, 0.0f, 0.995f);

    // Half-wave phase shift when internal reflection is against a denser medium:
    const float phaseFlip = (r12n * r23n < 0.0f) ? RAW_W81_PI : 0.0f;
    const float3 deltaRGB = (2.0f * RAW_W81_PI * opd) / float3(650.0f, 532.0f, 450.0f) + phaseFlip;
    const float3 cosD     = cos(deltaRGB);

    // Exact Airy reflectance: den - num = (1 - R12)*(1 - R23) >= 0 -> strictly in [0, 1]!
    const float  twoRootR = 2.0f * sqrt(max(R12 * R23, 0.0f));
    const float3 airyNum  = max((R12 + R23).xxx + twoRootR * cosD, 0.0f.xxx);
    const float3 airyDen  = max((1.0f + R12 * R23).xxx + twoRootR * cosD, 1.0e-5f.xxx);
    const float3 rAiry    = saturate(airyNum / airyDen);

    o.reflectanceRGB    = saturate(lerp(f0Safe, rAiry, mask));
    o.filmThicknessNm   = dNm;
    o.iridescenceWeight = mask;
    o.maxReflectance    = max(o.reflectanceRGB.r, max(o.reflectanceRGB.g, o.reflectanceRGB.b));
    return o;
}

// -----------------------------------------------------------------------------
// 2. Anisotropic Glaze-Ice & Frost-Crystal Phase Transition (-4 C .. +2 C)
// -----------------------------------------------------------------------------
// Continuously transitions wet surfaces into specular glaze-ice (hollows) and
// crystalline hoarfrost rime (exposed cold ridges) across -4 C .. +2 C, thawed
// locally by fire/torch heat. Zero time modulation -> zero temporal shimmer,
// zero blue/cyan neon clipping (B - R <= 0.12), exact identity on isGreenPine.
RawFrostGlazeResult rawEvalFrostGlazeCoupling(
    float3 wpAbs,
    float3 macroNormalW,
    float3 baseAlbedo,
    float  baseRoughness,
    float  wet01,
    float  snow01,
    float  tempC,
    float2 windDirXY,
    float  windSpeed01,
    float  fireHeat01,
    float  ndotv,
    bool   isGreenPine)
{
    RawFrostGlazeResult o;
    const float3 albSafe = rawIfhSan3(baseAlbedo, 0.92f);
    const float  rBase   = clamp(rawIfhSan1(baseRoughness, 0.65f), 0.05f, 0.95f);
    const float3 nRaw    = float3(rawIfhSan1(macroNormalW.x, 0.0f), rawIfhSan1(macroNormalW.y, 0.0f), rawIfhSan1(macroNormalW.z, 1.0f));
    const float  nL2     = dot(nRaw, nRaw);
    const float3 nMac    = (nL2 > 1.0e-6f) ? (nRaw * rsqrt(nL2)) : float3(0.0f, 0.0f, 1.0f);
    const float  nv      = saturate(rawIfhSan1(ndotv, 0.5f));

    if (isGreenPine) {
        o.albedoOut      = albSafe;
        o.normalOut      = nMac;
        o.roughnessOut   = rBase;
        o.anisoRatio     = 0.0f;
        o.glazeMask      = 0.0f;
        o.frostMask      = 0.0f;
        o.fresnelOut     = 0.02f;
        o.totalEnergyMax = min(max(albSafe.r, max(albSafe.g, albSafe.b)) + 0.02f, 1.0f);
        return o;
    }

    const float wIn    = saturate(rawIfhSan1(wet01, 0.0f));
    const float sIn    = saturate(rawIfhSan1(snow01, 0.0f));
    const float heat   = saturate(rawIfhSan1(fireHeat01, 0.0f));
    const float windSp = saturate(rawIfhSan1(windSpeed01, 0.0f));
    const float tEff   = clamp(rawIfhSan1(tempC, 4.0f), -45.0f, 45.0f) + heat * 10.0f;

    // Smooth freezing phase transition across -4 C .. +2 C (0.0 above +2 C, 1.0 at/below -4 C):
    const float freeze01 = 1.0f - smoothstep(-4.0f, 2.0f, tEff);
    if (freeze01 <= 0.002f) {
        o.albedoOut      = albSafe;
        o.normalOut      = nMac;
        o.roughnessOut   = rBase;
        o.anisoRatio     = 0.0f;
        o.glazeMask      = 0.0f;
        o.frostMask      = 0.0f;
        const float fWarm = saturate((0.02f + 0.98f * pow(1.0f - nv, 5.0f)) * lerp(1.0f, 0.42f, rBase * rBase));
        o.fresnelOut     = fWarm;
        o.albedoOut      = min(albSafe, max(1.0f - fWarm, 0.0f).xxx);
        o.totalEnergyMax = max(o.albedoOut.r, max(o.albedoOut.g, o.albedoOut.b)) + fWarm;
        return o;
    }

    const float2 wpXY = float2(rawIfhSan1(wpAbs.x, 0.0f), rawIfhSan1(wpAbs.y, 0.0f));
    const float  conc = rawPtLaplacianConcavity(wpXY);
    const float  upW  = smoothstep(0.20f, 0.75f, nMac.z);

    // Liquid wetness freezes into specular black-ice glaze in hollows; exposed cold ridges grow crystalline hoarfrost:
    const float glazeM = saturate(wIn * freeze01 * (0.72f + 0.28f * conc) * (0.35f + 0.65f * upW));
    const float frostM = saturate((1.0f - glazeM * 0.72f) * freeze01 * max(sIn, windSp * 0.58f)
                                  * smoothstep(0.5f, -3.5f, tEff) * (0.45f + 0.55f * (1.0f - conc)) * upW);

    // Neutral crystalline frost tint (0.78, 0.82, 0.86 -> B - R = 0.08, strictly zero neon blue/cyan clipping!):
    const float3 glazeAlb = albSafe * lerp(1.0f, 0.80f, glazeM);
    const float3 frostAlb = lerp(glazeAlb, float3(0.78f, 0.82f, 0.86f), frostM * 0.55f);

    // Roughness tightening on glaze-ice [0.06..0.14] vs micro-crystalline hoarfrost [0.48]:
    const float rGlaze = lerp(rBase, min(rBase, lerp(0.14f, 0.06f, conc)), glazeM);
    o.roughnessOut = clamp(lerp(rGlaze, max(rGlaze, 0.48f), frostM * 0.65f), 0.05f, 0.95f);

    // Static world-space wind-aligned anisotropic glaze micro-normal (ZERO time term -> zero shimmer!):
    const float2 wDirRaw = float2(rawIfhSan1(windDirXY.x, 1.0f), rawIfhSan1(windDirXY.y, 0.0f));
    const float  wDL2    = dot(wDirRaw, wDirRaw);
    const float2 wDir    = (wDL2 > 1.0e-6f) ? (wDirRaw * rsqrt(wDL2)) : float2(1.0f, 0.0f);
    const float2 wPerp   = float2(-wDir.y, wDir.x);
    const float  anisoPh = dot(wpXY, wPerp) * 0.22f + dot(wpXY, wDir) * 0.045f;
    const float  anisoG  = sin(anisoPh) * 0.55f + cos(anisoPh * 1.73f + 0.9f) * 0.45f;
    const float  pertAmp = glazeM * (0.045f + 0.055f * windSp);
    const float3 nPert   = nMac + float3(wPerp * (anisoG * pertAmp), 0.0f);
    const float  nPL2    = dot(nPert, nPert);
    o.normalOut = (nPL2 > 1.0e-6f) ? (nPert * rsqrt(nPL2)) : nMac;

    o.anisoRatio = saturate(glazeM * (0.25f + 0.40f * windSp)) * 0.65f;
    o.glazeMask  = glazeM;
    o.frostMask  = frostM;

    // Dielectric ice Fresnel (IOR 1.31 -> F0 = 0.018), roughness-damped & white-furnace bounded:
    const float fIceRaw = 0.018f + 0.982f * pow(1.0f - nv, 5.0f);
    o.fresnelOut = saturate(fIceRaw * lerp(1.0f, 0.42f, o.roughnessOut * o.roughnessOut));
    const float remBudget = max(1.0f - o.fresnelOut, 0.0f);
    o.albedoOut = min(frostAlb, remBudget.xxx);
    o.totalEnergyMax = max(o.albedoOut.r, max(o.albedoOut.g, o.albedoOut.b)) + o.fresnelOut;
    return o;
}

// -----------------------------------------------------------------------------
// 3. Atmospheric Rain-Mist Bow (42 deg) & 22-Degree Ice-Crystal Halo Scattering
// -----------------------------------------------------------------------------
// Pure world-space angular scattering (cosTheta = dot(V, S), 0 screen-UV drift):
//   - 22-deg hexagonal ice-crystal halo (n_R=1.307, n_G=1.311, n_B=1.317)
//   - 42-deg primary Descartes rain-mist bow around anti-solar axis (-S)
// Strictly gated on real weather/temperature/cloud transmittance (0 dry phantoms).
RawAtmoHaloResult rawEvalAtmoSpectralHalo(
    float3 viewDirW,
    float3 sunDirW,
    float  rain01,
    float  snow01,
    float  cloudTrans,
    float  altMeters,
    float  tempC)
{
    RawAtmoHaloResult o;
    o.haloRGB         = 0.0f.xxx;
    o.iceHalo22Weight = 0.0f;
    o.rainBow42Weight = 0.0f;
    o.chromaticSpread = 0.0f;

    const float3 vRaw = float3(rawIfhSan1(viewDirW.x, 0.0f), rawIfhSan1(viewDirW.y, 1.0f), rawIfhSan1(viewDirW.z, 0.2f));
    const float3 sRaw = float3(rawIfhSan1(sunDirW.x, 0.0f),  rawIfhSan1(sunDirW.y, 0.8f),  rawIfhSan1(sunDirW.z, 0.6f));
    const float  vL2  = dot(vRaw, vRaw);
    const float  sL2  = dot(sRaw, sRaw);
    const float3 vDir = (vL2 > 1.0e-6f) ? (vRaw * rsqrt(vL2)) : float3(0.0f, 1.0f, 0.0f);
    const float3 sDir = (sL2 > 1.0e-6f) ? (sRaw * rsqrt(sL2)) : float3(0.0f, 0.0f, 1.0f);

    const float rain   = saturate(rawIfhSan1(rain01, 0.0f));
    const float snow   = saturate(rawIfhSan1(snow01, 0.0f));
    const float ct     = saturate(rawIfhSan1(cloudTrans, 1.0f));
    const float altM   = clamp(rawIfhSan1(altMeters, 0.0f), 0.0f, 12000.0f);
    const float tSurf  = clamp(rawIfhSan1(tempC, 8.0f), -50.0f, 50.0f);
    const float tAlt   = tSurf - 0.0065f * altM;
    const float skyUp  = smoothstep(-0.02f, 0.12f, vDir.z);
    const float sunUp  = smoothstep(0.02f, 0.14f, sDir.z);

    if (skyUp <= 0.001f || sunUp <= 0.001f || ct <= 0.02f) {
        return o;
    }

    const float cosS = clamp(dot(vDir, sDir), -1.0f, 1.0f);

    // A) 22-Degree Hexagonal Ice-Crystal Halo (Snell minimum deviation: R=21.54 deg, G=21.84 deg, B=22.30 deg):
    const float coldIce = saturate(smoothstep(1.0f, -8.0f, tAlt) * max(snow, smoothstep(900.0f, 3200.0f, altM) * 0.72f));
    float3 iceAdd = 0.0f.xxx;
    if (coldIce > 0.01f) {
        const float thetaS = acos(cosS);
        // Forbidden dark interior inside ~21.0 deg (0.3665 rad):
        const float minDevGate = smoothstep(0.352f, 0.374f, thetaS);
        const float dR = (thetaS - 0.3760f) / 0.014f;
        const float dG = (thetaS - 0.3812f) / 0.016f;
        const float dB = (thetaS - 0.3892f) / 0.019f;
        const float3 iceSpec = float3(exp(-min(dR * dR, 30.0f)), exp(-min(dG * dG, 30.0f)), exp(-min(dB * dB, 30.0f))) * minDevGate;
        const float iceW = saturate(coldIce * sunUp * skyUp * ct);
        o.iceHalo22Weight = iceW * max(iceSpec.r, max(iceSpec.g, iceSpec.b));
        iceAdd = iceSpec * (0.14f * iceW);
    }

    // B) 42-Degree Primary Descartes Rain-Mist Bow (anti-solar axis -S: R=42.3 deg, G=41.4 deg, B=40.6 deg):
    // Strictly zero on dry days (rain <= 0.05) or deep freezing air (tSurf <= -1.5 C)!
    const float rainGate = smoothstep(0.05f, 0.35f, rain) * smoothstep(-1.5f, 1.5f, tSurf) * smoothstep(0.06f, 0.45f, ct);
    float3 bowAdd = 0.0f.xxx;
    if (rainGate > 0.005f) {
        const float thetaAnti = acos(-cosS);
        const float bR = (thetaAnti - 0.7383f) / 0.016f;
        const float bG = (thetaAnti - 0.7226f) / 0.016f;
        const float bB = (thetaAnti - 0.7086f) / 0.017f;
        const float3 bowSpec = float3(exp(-min(bR * bR, 30.0f)), exp(-min(bG * bG, 30.0f)), exp(-min(bB * bB, 30.0f)));
        const float bowW = saturate(rainGate * sunUp * skyUp);
        o.rainBow42Weight = bowW * max(bowSpec.r, max(bowSpec.g, bowSpec.b));
        bowAdd = bowSpec * (0.18f * bowW);
    }

    o.haloRGB = clamp(iceAdd + bowAdd, 0.0f.xxx, 0.35f.xxx);
    const float hiC = max(o.haloRGB.r, max(o.haloRGB.g, o.haloRGB.b));
    const float loC = min(o.haloRGB.r, min(o.haloRGB.g, o.haloRGB.b));
    o.chromaticSpread = max(hiC - loC, 0.0f);
    return o;
}

#endif
