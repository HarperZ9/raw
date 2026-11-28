// RawSplitTemporalAerial.hlsli: Wave 78 Specular-Diffuse Split Temporal Disocclusion Guard,
// World-Space Sub-Pixel Foliage Anti-Shimmer & Coverage Preservation, and Energy-Conserving
// Analytical Aerial-Perspective & Height-Fog Multi-Scatter Coupling.
// Self-contained clean-room MIT HLSL library (SM 6.6 / 6.9 / 6.10, 0 screen-UV dependence, 0 NaN/Inf).
// Reuses Wave 76 RawPrecipThermal.hlsli::rawEvalCrepuscularPhase with zero duplicate code.
// Original work, MIT.
#ifndef RAW_SPLIT_TEMPORAL_AERIAL_HLSLI
#define RAW_SPLIT_TEMPORAL_AERIAL_HLSLI

#include "RawPrecipThermal.hlsli"

struct RawSplitTemporalGuard {
    float specParallaxFactor; // Roughness-monotone virtual reflection parallax weight in [0, 1]
    float gammaDiff;          // Smooth diffuse GI variance clip gamma
    float gammaSpec;          // Tighter roughness-adaptive specular variance clip gamma (<= gammaDiff)
    float gammaEff;           // Combined variance clip gamma
    float alphaDiff;          // Smooth diffuse GI temporal blend alpha (0 crosstalk from spec parallax)
    float alphaSpec;          // Responsive specular temporal blend alpha (>= alphaDiff)
    float alphaEff;           // Combined temporal blend alpha
};

struct RawFoliageAntiShimmerResult {
    float coverageScale;       // Distance-adaptive canopy silhouette solidity preservation [1.0, 1.18]
    float translucencyScale;   // Pine-needle backlit translucency preservation [0.85, 1.12]
    float canopyFloorScale;    // Shadow floor guard (>= 0.44, never crushes canopy or darkens bark)
    float totalEnergyRatio;    // White-furnace reflectance + transmission bound (strictly <= 1.0)
};

struct RawAerialHeightFogResult {
    float  transmittance;      // Analytical Beer-Lambert height-fog transmittance strictly in [0, 1]
    float3 inScatter;          // Energy-conserving aerial in-scatter bounded by horizon sky/sun radiance
    float  horizonBlend;       // Smooth horizon sky lock factor in [0, 1] (0 horizon seam pops)
    float  opticalDepth;       // Clamped non-negative analytical optical depth
};

float rawStaSan1(float v, float fb) {
    return isfinite(v) ? v : fb;
}

float3 rawStaSan3(float3 v, float capVal) {
    return all(isfinite(v)) ? clamp(v, 0.0f.xxx, capVal.xxx) : 0.0f.xxx;
}

// -----------------------------------------------------------------------------
// 1. Specular-Diffuse Split Temporal Disocclusion Guard
// -----------------------------------------------------------------------------
// Separates high-frequency specular reprojection clamping (roughness-adaptive
// virtual reflection parallax + tighter variance clip) from smooth diffuse GI
// history so wet stone, snow glint, and water reflections stay sharp under
// rapid camera sweep without dragging dark trails or destabilizing diffuse GI.
RawSplitTemporalGuard rawEvalSplitTemporalGuard(
    float roughness,
    float specLobe01,
    float speedPx,
    float mvDivPx,
    float disocc01,
    float baseGammaDiff,
    float baseAlphaDiff)
{
    RawSplitTemporalGuard o;
    const float rSafe   = clamp(rawStaSan1(roughness, 0.55f), 0.02f, 1.0f);
    const float sLobe   = saturate(rawStaSan1(specLobe01, 0.0f));
    const float spd     = max(rawStaSan1(speedPx, 0.0f), 0.0f);
    const float mvDiv   = max(rawStaSan1(mvDivPx, 0.0f), 0.0f);
    const float disocc  = saturate(rawStaSan1(disocc01, 0.0f));
    const float gDiffIn = clamp(rawStaSan1(baseGammaDiff, 1.15f), 0.45f, 1.65f);
    const float aDiffIn = clamp(rawStaSan1(baseAlphaDiff, 0.25f), 0.05f, 0.98f);

    // Roughness-monotone virtual reflection parallax factor: mirror (r=0.02) -> ~0.96, matte (r=1.0) -> 0.0.
    const float oneMinusR = 1.0f - rSafe;
    const float parallax  = oneMinusR * oneMinusR;
    o.specParallaxFactor  = parallax;

    // Diffuse GI branch: strictly independent of specular parallax (zero specular->diffuse crosstalk!).
    const float diffMotion = saturate(spd * 0.10f + mvDiv * 0.25f);
    o.gammaDiff = clamp(gDiffIn * lerp(1.0f, 0.82f, diffMotion * 0.5f), 0.50f, 1.65f);
    o.alphaDiff = clamp(max(aDiffIn, disocc * 0.92f), 0.05f, 0.98f);

    // Specular reflection branch: tightens variance clip & boosts reactivity monotonically as roughness drops & speed grows.
    const float specMotion = saturate(spd * 0.18f + mvDiv * 0.45f);
    const float specTighten = saturate(parallax * (0.28f + 0.52f * specMotion));
    o.gammaSpec = clamp(min(o.gammaDiff, o.gammaDiff * (1.0f - specTighten * 0.42f)), 0.38f, o.gammaDiff);
    const float specReactAlpha = saturate(o.alphaDiff + parallax * specMotion * 0.42f);
    o.alphaSpec = clamp(max(o.alphaDiff, specReactAlpha), o.alphaDiff, 0.98f);

    // Combined effective clip & blend weighted strictly by detected specular lobe share:
    o.gammaEff = lerp(o.gammaDiff, o.gammaSpec, sLobe);
    o.alphaEff = lerp(o.alphaDiff, o.alphaSpec, sLobe);
    return o;
}

// -----------------------------------------------------------------------------
// 2. World-Space Sub-Pixel Foliage Anti-Shimmer & Coverage Preservation
// -----------------------------------------------------------------------------
// Preserves canopy silhouette solidity and pine-needle translucency across
// moving camera sweeps using world-space distance-adaptive mip/coverage bias.
// Strictly identity on wood bark (isWoodBark == true) so trunks never darken.
RawFoliageAntiShimmerResult rawEvalFoliageCoverageAntiShimmer(
    float3 wpAbs,
    float3 normalW,
    float3 viewDirW,
    float  camDistU,
    float  maxAlbedoCh,
    float  baseTransWeight,
    float  skyVis,
    bool   isWoodBark)
{
    RawFoliageAntiShimmerResult o;
    const float albSafe   = clamp(rawStaSan1(maxAlbedoCh, 0.35f), 0.0f, 0.92f);
    const float transSafe = clamp(rawStaSan1(baseTransWeight, 0.12f), 0.0f, 0.50f);
    const float svSafe    = saturate(rawStaSan1(skyVis, 0.8f));

    if (isWoodBark) {
        o.coverageScale     = 1.0f;
        o.translucencyScale = 1.0f;
        o.canopyFloorScale  = 1.0f;
        o.totalEnergyRatio  = min(albSafe, 1.0f);
        return o;
    }

    const float3 wpSafe = float3(rawStaSan1(wpAbs.x, 0.0f), rawStaSan1(wpAbs.y, 0.0f), rawStaSan1(wpAbs.z, 0.0f));
    const float3 nRaw   = float3(rawStaSan1(normalW.x, 0.0f), rawStaSan1(normalW.y, 0.0f), rawStaSan1(normalW.z, 1.0f));
    const float3 vRaw   = float3(rawStaSan1(viewDirW.x, 0.0f), rawStaSan1(viewDirW.y, 0.0f), rawStaSan1(viewDirW.z, 1.0f));
    const float  nL2    = dot(nRaw, nRaw);
    const float  vL2    = dot(vRaw, vRaw);
    const float3 nSafe  = (nL2 > 1.0e-6f) ? (nRaw * rsqrt(nL2)) : float3(0.0f, 0.0f, 1.0f);
    const float3 vSafe  = (vL2 > 1.0e-6f) ? (vRaw * rsqrt(vL2)) : float3(0.0f, 0.0f, 1.0f);

    const float distU = max(rawStaSan1(camDistU, 0.0f), 0.0f);
    const float distMipBias = smoothstep(180.0f, 2400.0f, distU);
    const float nv = abs(dot(nSafe, vSafe));
    const float grazeW = 1.0f - smoothstep(0.10f, 0.65f, nv);

    // Pure world-space coherent canopy density modulation (zero screen-UV dependence):
    const float3 wCell = wpSafe * 0.045f;
    const float wCoh = 0.50f + 0.50f * (sin(wCell.x + cos(wCell.y)) * cos(wCell.z * 0.7f));

    // Distance-adaptive coverage solidity lift (compensates alpha-test sub-pixel thinning without highlight blowout):
    const float rawCovScale = 1.0f + distMipBias * (0.08f + 0.06f * grazeW + 0.03f * wCoh);
    const float maxSafeScale = (albSafe > 1.0e-4f) ? min(1.18f, 0.94f / albSafe) : 1.18f;
    o.coverageScale = clamp(min(rawCovScale, maxSafeScale), 1.0f, 1.18f);

    // Energy-conserving pine-needle backlit translucency preservation:
    const float effAlb = min(albSafe * o.coverageScale, 0.94f);
    const float remBudget = max(1.0f - effAlb, 0.0f);
    const float rawTransScale = lerp(1.0f, 1.10f, distMipBias * (0.45f + 0.55f * svSafe));
    const float cappedTrans = min(transSafe * rawTransScale, remBudget);
    o.translucencyScale = (transSafe > 1.0e-5f) ? clamp(cappedTrans / transSafe, 0.85f, 1.12f) : 1.0f;
    o.canopyFloorScale  = clamp(lerp(0.46f, 1.0f, saturate(svSafe * 0.68f + distMipBias * 0.16f + 0.16f)), 0.44f, 1.0f);
    o.totalEnergyRatio  = min(effAlb + min(transSafe * o.translucencyScale, remBudget), 1.0f);
    return o;
}

// -----------------------------------------------------------------------------
// 3. Energy-Conserving Analytical Aerial-Perspective & Height-Fog Coupling
// -----------------------------------------------------------------------------
// Exact analytical integral of altitude-exponential height fog coupled with
// Wave 76 volumetric cloud transmittance & two-order HG forward scattering.
// Guarantees T in [0, 1], finite non-negative inScatter <= omega * sky/sun,
// and exact sky-luminance match at the horizon (0 horizon seam pops).
RawAerialHeightFogResult rawEvalAnalyticalAerialHeightFog(
    float  camZ,
    float  rayDirZ,
    float  rayDist,
    float  refFloorZ,
    float  baseSigma,
    float  scaleHeightU,
    float  cloudTrans,
    float  cosSun,
    float3 skyHorizonCol,
    float3 sunCol,
    float  nightW)
{
    RawAerialHeightFogResult o;
    const float cz     = rawStaSan1(camZ, 0.0f);
    const float dzDir  = clamp(rawStaSan1(rayDirZ, 0.0f), -1.0f, 1.0f);
    const float dist   = clamp(rawStaSan1(rayDist, 0.0f), 0.0f, 300000.0f);
    const float zRef   = rawStaSan1(refFloorZ, 0.0f);
    const float sig0   = clamp(rawStaSan1(baseSigma, 0.000012f), 0.0f, 0.01f);
    const float H      = clamp(rawStaSan1(scaleHeightU, 2400.0f), 120.0f, 40000.0f);
    const float ct     = saturate(rawStaSan1(cloudTrans, 1.0f));
    const float mu     = clamp(rawStaSan1(cosSun, 0.0f), -1.0f, 1.0f);
    const float nW     = saturate(rawStaSan1(nightW, 0.0f));
    const float3 skyC  = rawStaSan3(skyHorizonCol, 8.0f);
    const float3 sunC  = rawStaSan3(sunCol, 12.0f);

    // Exact analytical optical-depth integral of sigma_0 * exp(-(z(t) - zRef)/H) over t in [0, dist]:
    //   tau = sigma_0 * exp(-h0 / H) * dist * ((1 - exp(-u)) / u),  u = dzDir * dist / H
    // Uses exact Taylor expansion 1 - u/2 + u^2/6 when |u| < 1e-3 (zero 0/0 NaNs on flat horizon rays!).
    const float h0Norm = clamp((cz - zRef) / H, -2.0f, 16.0f);
    const float startDens = sig0 * exp(-h0Norm);
    const float u = clamp((dzDir * dist) / H, -16.0f, 16.0f);
    float heightInt = 1.0f;
    if (abs(u) < 1.0e-3f) {
        heightInt = 1.0f - 0.5f * u + (1.0f / 6.0f) * u * u;
    } else {
        heightInt = (1.0f - exp(-u)) / u;
    }
    const float od = clamp(startDens * dist * max(heightInt, 0.0f), 0.0f, 32.0f);
    const float T  = saturate(exp(-od));

    // Smooth far-horizon lock so distant mountain silhouettes converge identically to skyHorizonCol (0 seam pop!):
    const float hBlend = smoothstep(55000.0f, 175000.0f, dist);
    const float tEff = T * (1.0f - hBlend);

    // Energy-conserving two-order crepuscular phase (reuses Wave 76 rawEvalCrepuscularPhase, omega <= 0.96):
    const float phW76 = rawEvalCrepuscularPhase(mu, od, ct);
    const float sunShare = saturate(phW76 * 0.22f * ct * (1.0f - nW) * (1.0f - hBlend));
    const float cloudSkyMod = lerp(lerp(0.64f, 1.0f, ct), 1.0f, max(hBlend, nW));
    const float3 scatterBase = skyC * cloudSkyMod;
    // Convex blend bounded strictly by max(skyC, sunC) and single-scattering albedo <= 0.96:
    const float3 aerialTarget = lerp(scatterBase, min(sunC, skyC * 1.45f + 0.05f.xxx), sunShare * 0.35f);
    const float3 maxAllowed = max(skyC, sunC) * 0.96f;
    const float3 clampedTarget = lerp(min(aerialTarget, maxAllowed), skyC, hBlend);

    o.opticalDepth = od;
    o.transmittance = saturate(tEff);
    o.horizonBlend  = hBlend;
    o.inScatter     = rawStaSan3(clampedTarget * (1.0f - o.transmittance), 8.0f);
    return o;
}

#endif
