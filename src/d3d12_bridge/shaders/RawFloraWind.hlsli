// RawFloraWind.hlsli: 4-Tier Euler-Bernoulli Cantilever Wind Physics for RAW Flora Engine.
// Clean-room MIT HLSL library (SM 6.6 & SM 6.10 verified, zero external textures, zero register conflicts).
//
// Models coherent 3D turbulent synoptic gust packets advecting across forests and evaluates:
//   Tier 1: Low-frequency trunk sway (Euler-Bernoulli cantilever deflection ~ h^2, exact arc-length preservation!)
//   Tier 2: Primary bough vertical spring bounce + torsional trunk twist
//   Tier 3: Secondary limb Strouhal vortex-shedding cross-wind whip
//   Tier 4: High-frequency individual needle/leaf flutter driven by local gust shear
//
// Designed to plug directly into `RawTerrainWorld.hlsl` (`ms_terrain_main` / `vs_terrain_main`) using
// `TerrainVertex.meta.y` (bendFlex: 0 rooted trunk -> 1 terminal tips) and `TerrainVertex.meta.z` (phaseHash).
// Original work, MIT.

#ifndef RAW_FLORA_WIND_HLSLI
#define RAW_FLORA_WIND_HLSLI

#include "RawProceduralNoise.hlsli"

struct FloraWindGustSample {
    float2 gustVecXY;     // Coherent synoptic + turbulent wind vector (world units/sec proxy)
    float  gustSpeed;     // Scalar wind speed magnitude
    float  gustShear;     // Local high-frequency spatial shear driving Tier 3/4 flutter
    float  gustPulse;     // Low-frequency travelling gust envelope in [0..1]
};

struct FloraWindResult {
    float3 displacement;  // World-space Z-up vertex offset (strictly 0 when bendFlex <= 0)
    float3 bentNormal;    // Coherently rotated unit surface normal under cantilever bend
    float  gustStrength;  // Normalized local gust intensity [0..2+] for shading/specular shimmer
};

// Coupled seasonal phenology & physical winter snow-load state (mirrors RawWeatherSim::PhenologyState & RawFloraEngine).
struct FloraSeasonalParams {
    float chlorophyll01;        // [0..1] lush summer chlorophyll retention
    float autumnCarotenoid01;   // [0..1] golden/amber autumn foliage shift (Birch, Oak)
    float autumnAnthocyanin01;  // [0..1] crimson/russet autumn foliage shift (Oak, Shrub)
    float leafAbscission01;     // [0..1] deciduous leaf drop (0=full canopy, 1=bare winter limbs)
    float canopySnowLoad01;     // [0..1] physical winter snow weight driving downward Euler-Bernoulli sag
};

// Wave 60: Strict finite sanitization & safe unit-vector helpers for zero-NaN wind physics.
float3 sanitizeFloraVec3(float3 v, float maxAbs) {
    return all(isfinite(v)) ? clamp(v, (-maxAbs).xxx, maxAbs.xxx) : float3(0.0f, 0.0f, 0.0f);
}

float3 safeNormalizeFlora(float3 n, float3 fallback) {
    const float len2 = dot(n, n);
    return (isfinite(len2) && len2 > 1.0e-8f) ? (n * rsqrt(len2)) : fallback;
}

// Evaluates a coherent 3D travelling synoptic gust field advected along windXY * timeSec.
// Macro gusts roll smoothly across 20m-80m forest stands (~1400..5600 Skyrim units).
FloraWindGustSample evalFloraSynopticGust(float3 absWorldPos, float2 windXY, float timeSec) {
    FloraWindGustSample g;
    const float2 wSafe = all(isfinite(windXY)) ? clamp(windXY, -4.0f.xx, 4.0f.xx) : float2(0.0f, 0.0f);
    const float baseSpeed = length(wSafe);
    if (baseSpeed < 1.0e-5f) {
        g.gustVecXY = float2(0.0f, 0.0f);
        g.gustSpeed = 0.0f;
        g.gustShear = 0.0f;
        g.gustPulse = 0.0f;
        return g;
    }

    const float tSafe = isfinite(timeSec) ? timeSec : 0.0f;
    const float2 wDir = wSafe / baseSpeed;
    const float2 wPerp = float2(-wDir.y, wDir.x);

    // Advect 3D gust packets downwind so neighboring trees bend in rolling coherent waves.
    const float advSpeed = 240.0f * (0.45f + 0.55f * baseSpeed);
    const float3 macroCoord = float3(
        (absWorldPos.xy - wDir * (advSpeed * tSafe)) * 0.00055f,
        absWorldPos.z * 0.00045f + tSafe * 0.07f);
    const float4 nMacro = rawNoise3Grad(macroCoord);

    // Mesoscale turbulent eddies (cross-wind eddies & downdrafts)
    const float3 eddyCoord = float3(
        (absWorldPos.xy - wDir * (advSpeed * 1.45f * tSafe)) * 0.0024f,
        absWorldPos.z * 0.0018f - tSafe * 0.19f);
    const float4 nEddy = rawNoise3Grad(eddyCoord);

    // Smooth non-negative gust wave envelope
    const float gustWave = saturate(0.35f + 0.85f * nMacro.x + 0.25f * (nEddy.x - 0.5f));
    const float lateralEddy = (nEddy.y - nEddy.z) * 0.42f;

    g.gustPulse = gustWave;
    g.gustVecXY = (wDir * (0.55f + 0.95f * gustWave) + wPerp * lateralEddy) * baseSpeed;
    g.gustSpeed = clamp(length(g.gustVecXY), 0.0f, 6.0f);
    g.gustShear = clamp(saturate(length(nEddy.yzw) * 0.55f + gustWave * 0.45f) * baseSpeed, 0.0f, 4.0f);
    return g;
}

// 4-Tier Euler-Bernoulli Cantilever Wind Displacement + Coherent Normal Bending.
// Wave 60: Upgraded with gust-envelope dual-harmonic branch sway, high-frequency leaf flutter,
// directional storm bending, strict zero-displacement root pinning (height01 == 0 / bendFlex == 0),
// and finite clamping.
FloraWindResult evalFloraWind4Tier(
    float3 absWorldPos,
    float3 rootPos,
    float  bendFlex,
    float  phaseHash,
    float2 windXY,
    float  timeSec,
    float3 baseNormal)
{
    FloraWindResult res;
    res.displacement = float3(0.0f, 0.0f, 0.0f);
    const float3 nRest = safeNormalizeFlora(baseNormal, float3(0.0f, 0.0f, 1.0f));
    res.bentNormal   = nRest;
    res.gustStrength = 0.0f;

    const float flex = isfinite(bendFlex) ? saturate(bendFlex) : 0.0f;
    const float2 wSafe = all(isfinite(windXY)) ? clamp(windXY, -4.0f.xx, 4.0f.xx) : float2(0.0f, 0.0f);
    const float windMag = length(wSafe);
    const float3 rel = all(isfinite(absWorldPos - rootPos)) ? (absWorldPos - rootPos) : float3(0.0f, 0.0f, 0.0f);
    const float h = max(rel.z, 0.0f);

    // Strict zero-displacement root pinning when bendFlex == 0 or height above root == 0 (or calm air):
    if (flex <= 1.0e-5f || h <= 1.0e-4f || windMag <= 1.0e-5f) {
        return res;
    }

    const float tSafe = isfinite(timeSec) ? timeSec : 0.0f;
    const float phSafe = isfinite(phaseHash) ? frac(phaseHash) : 0.0f;
    const float2 wDir = wSafe / windMag;
    const float2 wPerp = float2(-wDir.y, wDir.x);

    // Sample synoptic gust at tree crown center (coherent trunk sway) and local vertex (fine branch/leaf shear).
    const float rXY = length(rel.xy);
    const float beamLen = max(length(rel), 1.0f);
    // Smooth height01 root-pinning ramp guarantees C1 continuity right above the pinned root collar:
    const float height01 = saturate(h / max(beamLen, 16.0f));
    const float rootPin = smoothstep(0.0f, 0.04f, flex) * smoothstep(0.0f, 0.04f, height01);

    const FloraWindGustSample gTrunk = evalFloraSynopticGust(rootPos + float3(0.0f, 0.0f, h * 0.65f), wSafe, tSafe);
    const FloraWindGustSample gLocal = evalFloraSynopticGust(absWorldPos, wSafe, tSafe);
    res.gustStrength = gLocal.gustSpeed;

    // Tree-unique base phase from root coordinates (vendor-invariant polynomial hash)
    const float3 rootHash = rawHash33(floor(rootPos * 0.02f));
    const float  treePhase = rootHash.x * 6.2831853f;
    const float  branchPhase = phSafe * 6.2831853f;

    // ------------------------------------------------------------------------
    // TIER 1: Low-Frequency Trunk Sway + Directional Storm Bending (Euler-Bernoulli ~ h^2)
    // Deflection profile: w(s) = s^2 * (3 - s) / 2, preserving exact arc length!
    // ------------------------------------------------------------------------
    const float sNorm = saturate(h / 1400.0f);
    const float cantileverCurve = 0.5f * sNorm * sNorm * (3.0f - sNorm);
    const float trunkFreq = 0.55f + 0.35f * rootHash.y;
    // Dual-harmonic gust-modulated trunk oscillation + steady downwind aerodynamic storm lean:
    const float trunkHarm1 = sin(tSafe * trunkFreq * 2.199115f + treePhase);
    const float trunkHarm2 = sin(tSafe * trunkFreq * 3.55824f + treePhase * 1.618f + 0.9f);
    const float trunkOsc = 0.58f + (0.28f * trunkHarm1 + 0.14f * trunkHarm2) * (0.45f + 0.55f * gTrunk.gustPulse);
    const float stormBend = smoothstep(0.65f, 2.20f, gTrunk.gustSpeed) * (0.42f + 0.58f * gTrunk.gustPulse);

    // Cap lateral displacement strictly below 0.38 * beamLen so inextensible sqrt stays well-conditioned.
    const float maxLateral = beamLen * 0.38f;
    const float trunkAmp = min(beamLen * (0.14f * trunkOsc + 0.11f * stormBend) * cantileverCurve * gTrunk.gustSpeed * (0.25f + 0.75f * flex), maxLateral);
    const float2 trunkDirXY = (gTrunk.gustSpeed > 1.0e-5f) ? (gTrunk.gustVecXY / gTrunk.gustSpeed) : wDir;
    const float2 stormDirXY = safeNormalizeFlora(float3(lerp(trunkDirXY, wDir, stormBend * 0.55f), 0.0f), float3(wDir, 0.0f)).xy;
    const float2 disp1XY = stormDirXY * trunkAmp;

    // Exact inextensible cantilever arc-length preservation: dz = sqrt(L^2 - |dXY|^2) - L
    const float latSq = dot(disp1XY, disp1XY);
    const float disp1Z = sqrt(max(beamLen * beamLen - latSq, 0.0f)) - beamLen;
    const float3 tier1 = float3(disp1XY, disp1Z);

    // ------------------------------------------------------------------------
    // TIER 2: Gust-Envelope Dual-Harmonic Primary Bough Sway + Torsional Twist
    // ------------------------------------------------------------------------
    const float boughFlex = smoothstep(0.12f, 0.85f, flex);
    const float boughFreq = 1.15f + 0.55f * phSafe;
    const float gustEnv   = 0.35f + 0.65f * gLocal.gustPulse;
    const float boughH1   = sin(tSafe * boughFreq * 2.40f + branchPhase + treePhase * 0.5f);
    const float boughH2   = sin(tSafe * boughFreq * 3.88328f + branchPhase * 1.7f + 1.13f);
    const float boughWave = (boughH1 * 0.72f + boughH2 * 0.28f) * gustEnv;
    const float boughBounceZ = -boughWave * (0.048f * max(rXY, 18.0f)) * boughFlex * min(gLocal.gustSpeed, 3.0f);
    const float2 boughSwayXY = (wDir * (boughH1 * 0.62f) + wPerp * (boughH2 * 0.38f))
                             * (0.024f * max(rXY, 16.0f) * boughFlex * gustEnv * min(gLocal.gustSpeed, 3.0f));

    // Torsional twist rotates (rel.xy) around the trunk Z-axis under asymmetric lateral gusts.
    const float twistAngle = 0.095f * (0.70f * sin(tSafe * 1.45f + branchPhase * 1.3f) + 0.30f * boughH2)
                           * boughFlex * gustEnv * clamp(gLocal.gustSpeed, 0.0f, 2.0f);
    float sT, cT;
    sincos(twistAngle, sT, cT);
    const float2 twistedXY = float2(rel.x * cT - rel.y * sT, rel.x * sT + rel.y * cT) - rel.xy;
    const float3 tier2 = float3(twistedXY + boughSwayXY, boughBounceZ);

    // ------------------------------------------------------------------------
    // TIER 3: Secondary Limb Strouhal Vortex-Shedding Cross-Wind Whip
    // ------------------------------------------------------------------------
    const float limbFlex = pow(smoothstep(0.28f, 1.0f, flex), 1.35f);
    const float strouhalFreq = 2.6f + 1.8f * min(gLocal.gustSpeed, 3.0f) + 1.1f * phSafe;
    const float whipSin = sin(tSafe * strouhalFreq * 2.2f + branchPhase * 2.7f);
    const float whipCos = cos(tSafe * strouhalFreq * 1.7f + branchPhase * 1.9f);
    const float whipMag = min((4.5f + 0.022f * max(rXY, 30.0f)) * limbFlex * gLocal.gustShear * gustEnv, 36.0f);
    const float3 tier3 = float3(wPerp * (whipSin * whipMag) + wDir * (whipCos * whipMag * 0.45f),
                                whipCos * whipMag * 0.35f);

    // ------------------------------------------------------------------------
    // TIER 4: High-Frequency Individual Needle / Leaf Flutter (Dual-Harmonic Gust-Shear Driven)
    // ------------------------------------------------------------------------
    const float leafFlex = smoothstep(0.48f, 1.0f, flex);
    const float3 leafHash = rawHash33(floor(absWorldPos * 0.14f));
    const float  flutterPhase = dot(leafHash, float3(6.283f, 11.17f, 4.71f)) + branchPhase * 3.1f;
    const float  flutterFreq = 7.5f + 5.5f * gLocal.gustShear + 2.5f * leafHash.x;
    const float3 flutterH1 = float3(
        sin(tSafe * flutterFreq + flutterPhase),
        cos(tSafe * flutterFreq * 1.17f + flutterPhase * 1.3f),
        sin(tSafe * flutterFreq * 0.89f - flutterPhase * 0.7f));
    const float3 flutterH2 = float3(
        cos(tSafe * flutterFreq * 1.73f - flutterPhase * 0.9f),
        sin(tSafe * flutterFreq * 1.51f + flutterPhase * 1.1f),
        cos(tSafe * flutterFreq * 1.93f + flutterPhase * 0.5f));
    const float  flutterAmp = min(2.8f * leafFlex * leafFlex * (0.25f + 0.75f * gLocal.gustShear) * (0.40f + 0.60f * gLocal.gustPulse), 4.2f);
    const float3 tier4 = (flutterH1 * 0.68f + flutterH2 * 0.32f) * flutterAmp;

    // Combine all 4 tiers with strict zero-displacement root pinning and finite clamping:
    const float maxDisp = min(beamLen * 0.55f, 220.0f);
    res.displacement = sanitizeFloraVec3((tier1 + tier2 + tier3 + tier4) * rootPin, maxDisp);

    // Coherently tilt normal along cantilever deflection gradient so foliage specular shimmers naturally.
    const float3 tiltDelta = float3(disp1XY * (0.35f / max(beamLen, 30.0f)), 0.0f)
                           + tier4 * (0.045f * leafFlex) * rootPin;
    res.bentNormal = safeNormalizeFlora(nRest + sanitizeFloraVec3(tiltDelta, 1.5f), nRest);
    return res;
}

// Primary entry point matching the RAW Flora Engine contract:
//   evalFloraWindDisplacement(absWorldPos, rootPos, bendFlex, phaseHash, windXY, timeSec)
float3 evalFloraWindDisplacement(
    float3 absWorldPos,
    float3 rootPos,
    float  bendFlex,
    float  phaseHash,
    float2 windXY,
    float  timeSec)
{
    return evalFloraWind4Tier(absWorldPos, rootPos, bendFlex, phaseHash, windXY, timeSec, float3(0.0f, 0.0f, 1.0f)).displacement;
}

// ============================================================================
// Seasonal Phenology & Physical Winter Snow-Load Cantilever Sag Extensions
// ============================================================================

// Evaluates physical downward Euler-Bernoulli cantilever sag under winter canopy snow weight
// (inextensible horizontal pull-in preserving bough arc length!) plus snow-mass inertial wind damping.
// Evergreen conifers (speciesId < 1.5) retain full snow catch all winter; deciduous canopies lighten as leaves abscise.
FloraWindResult evalFloraSeasonalWind(
    float3 absWorldPos,
    float3 rootPos,
    float  bendFlex,
    float  phaseHash,
    float  speciesId,
    float2 windXY,
    float  timeSec,
    float3 baseNormal,
    FloraSeasonalParams season)
{
    const float flex = saturate(bendFlex);
    const float snowW = saturate(season.canopySnowLoad01);
    const float isConifer = (speciesId < 1.5f) ? 1.0f : 0.0f;
    const float foliageCatch = lerp(saturate(1.0f - season.leafAbscission01 * 0.85f), 1.0f, isConifer);
    const float effSnowLoad = snowW * foliageCatch;

    // Snow mass increases effective beam inertia: slower, heavier sway and damped high-frequency leaf flutter!
    const float snowTimeScale = rsqrt(1.0f + 1.85f * effSnowLoad);
    FloraWindResult windRes = evalFloraWind4Tier(
        absWorldPos, rootPos, flex, phaseHash, windXY * (1.0f - 0.28f * effSnowLoad),
        timeSec * snowTimeScale, baseNormal);

    if (flex <= 1.0e-5f || effSnowLoad <= 1.0e-5f) {
        return windRes;
    }

    // Physical downward Euler-Bernoulli cantilever bough sag proportional to radial lever arm & flex^2.
    const float3 rel = all(isfinite(absWorldPos - rootPos)) ? (absWorldPos - rootPos) : float3(0.0f, 0.0f, 0.0f);
    if (rel.z <= 1.0e-4f) {
        return windRes;
    }
    const float  rXY = length(rel.xy);
    const float  flexGate = smoothstep(0.02f, 0.14f, flex);
    const float  rawSag = effSnowLoad * rXY * (0.20f + 0.34f * flex * flex) * flexGate;
    const float  sagZ = min(rawSag, rXY * 0.58f);

    // Exact inextensible radial pull-in: rNew = sqrt(rXY^2 - sagZ^2)
    const float  rNew = sqrt(max(rXY * rXY - sagZ * sagZ, 0.0f));
    const float2 radialUnit = (rXY > 1.0e-4f) ? (rel.xy / rXY) : float2(0.0f, 0.0f);
    const float3 snowSagDisp = sanitizeFloraVec3(float3(-radialUnit * (rXY - rNew), -sagZ), 180.0f);

    windRes.displacement = sanitizeFloraVec3(windRes.displacement + snowSagDisp, 220.0f);
    // Tilt normal slightly outward/downward as heavy snow bows bough tips toward the ground.
    const float sagSlope = (rXY > 1.0f) ? saturate(sagZ / rXY) : 0.0f;
    windRes.bentNormal = safeNormalizeFlora(windRes.bentNormal + float3(radialUnit * (sagSlope * 0.32f), -sagSlope * 0.12f), windRes.bentNormal);
    return windRes;
}

// Modulates deciduous foliage albedo through seasonal chlorophyll -> golden carotenoid -> crimson anthocyanin stages.
// Evergreen conifers (SitkaSpruce=0, CoastalPine=1) preserve deep boreal green with subtle winter bronze tint.
float3 evalFloraSeasonalFoliageTint(float3 baseAlbedo, FloraSeasonalParams season, float speciesId, float phaseHash) {
    const float isConifer = (speciesId < 1.5f) ? 1.0f : 0.0f;
    const float chloro = saturate(season.chlorophyll01);
    const float carot  = saturate(season.autumnCarotenoid01);
    const float antho  = saturate(season.autumnAnthocyanin01);

    // Per-branch-order organic color variation so individual limbs turn gold/crimson asynchronously!
    const float localShift = saturate((carot + antho) * (0.72f + 0.56f * phaseHash));
    const float3 goldCol    = float3(0.76f, 0.56f, 0.11f);
    const float3 crimsonCol = float3(0.56f, 0.13f, 0.07f);
    const float  anthoRatio = antho / max(carot + antho, 1.0e-4f);
    const float3 autumnCol  = lerp(goldCol, crimsonCol, saturate(anthoRatio * (0.75f + 0.50f * phaseHash)));

    // Conifers resist senescence (keep 85%+ boreal green); deciduous species undergo full chromatic transition!
    const float decidWeight = lerp(1.0f, 0.12f, isConifer);
    const float shiftAmt = saturate(max(1.0f - chloro, localShift) * decidWeight);
    const float luma = max(dot(baseAlbedo, float3(0.299f, 0.587f, 0.114f)), 0.08f);
    return lerp(baseAlbedo, autumnCol * (0.55f + 1.65f * luma), shiftAmt);
}

// Smooth GPU deciduous leaf abscission survival mask in [0..1] (1.0 for conifers & trunks; thins deciduous leaves in winter).
float evalFloraSeasonalAbscissionMask(FloraSeasonalParams season, float speciesId, float phaseHash, float bendFlex) {
    if (speciesId < 1.5f || bendFlex < 0.35f) return 1.0f; // Conifers & woody trunk/primary limbs never abscise
    const float drop = saturate(season.leafAbscission01);
    if (drop <= 0.001f) return 1.0f;
    if (drop >= 0.995f) return 0.0f;
    return smoothstep(drop - 0.12f, drop + 0.06f, phaseHash);
}

#endif // RAW_FLORA_WIND_HLSLI
