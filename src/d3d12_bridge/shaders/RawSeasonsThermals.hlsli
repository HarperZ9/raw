// RawSeasonsThermals.hlsli: GPU Seasonal Phenology, Surface Thermal Energy Balance,
// Snowpack Metamorphism, Fire/Hearth Melt Rings, Freeze-Thaw Ice Glaze & Water Ice (SM 6.6 / 6.10).
// Standalone clean-room MIT HLSL library (zero cbuffer coupling, 100% static world-locked frost/ice):
//   - Callable from Lighting.hlsl, RawTerrainWorld.hlsl, and RtWater.hlsl
//   - Consumes WeatherGpuUniforms::seasonThermal0/1 and phenology0/1 from RawWeatherSim
// Original work, MIT.
#ifndef RAW_SEASONS_THERMALS_HLSLI
#define RAW_SEASONS_THERMALS_HLSLI

#include "RawProceduralNoise.hlsli"

struct SurfaceThermalEval {
    float  snowCov01;       // Local snow coverage after slope/aspect solar melt + fire melt rings
    float  snowDepthM;      // Effective local snow thickness (meters)
    float3 snowAlbedo;      // Metamorphic aged snow albedo (0.88 fresh -> 0.70 firn -> 0.55 wet granular)
    float  snowRoughness;   // Fresh powder (0.88) -> dense firn (0.64) -> wet spring granular (0.28)
    float  iceGlaze01;      // Freeze-thaw black ice / surface ice glaze [0..1]
    float  frostRime01;     // Static crystalline hoarfrost / windward rime [0..1]
    float  fireMeltRing01;  // 1.0 inside active fire/hearth/forge thawed bare-earth core
    float  slushRim01;      // Glistening wet icy slush transition ring around fire melt perimeter
    float  wetBoost01;      // Snowmelt runoff + fire-thaw wetness boost [0..1]
    float  puddleMask01;    // Standing snowmelt/rain puddle mask after solar/wind evaporation
};

struct SeasonalFoliageEval {
    float3 albedoTint;       // Continuous chlorophyll -> carotenoid gold -> anthocyanin crimson shift
    float  alphaDiscard01;   // Deciduous autumn/winter leaf abscission mask (0 = full, 1 = bare branches)
    float  canopySnowCov01;  // Upward-facing bough snow accumulation coverage
    float  frostRim01;       // Sub-zero needle/leaf hoarfrost edge glint
};

// Wave 61: Strict finite sanitization & safe normalization helpers for seasonal thermal physics.
float3 safeNormThermal(float3 v, float3 fallback) {
    const float l2 = dot(v, v);
    return (isfinite(l2) && l2 > 1.0e-8f) ? (v * rsqrt(l2)) : fallback;
}

// Wave 61: Diurnal Thermal-Lag Hysteresis (afternoon heat peak ~15:00, pre-dawn frost minimum ~05:00).
// Infers thermal lag directly from astronomical sun vector (sd.x > 0 morning east, sd.x < 0 afternoon west)
// so callers without an explicit gameHour uniform remain 100% backward-compatible.
float evalDiurnalThermalLagFromSunC(float3 sunDirUnit) {
    const float sdZ = clamp(isfinite(sunDirUnit.z) ? sunDirUnit.z : 0.0f, -1.0f, 1.0f);
    const float sdX = clamp(isfinite(sunDirUnit.x) ? sunDirUnit.x : 0.0f, -1.0f, 1.0f);
    // Afternoon thermal accumulation peaks when sun descends into west (-X) after solar noon (+2.4 C):
    const float afternoonWarm = saturate(-sdX * 0.85f + 0.15f) * smoothstep(-0.08f, 0.42f, sdZ) * 2.4f;
    // Pre-dawn radiative cooling reaches frost minimum right before sunrise (+X, low/negative elevation, -2.6 C):
    const float predawnCold   = saturate(sdX * 0.85f + 0.20f) * (1.0f - smoothstep(-0.06f, 0.26f, sdZ)) * 2.6f;
    return clamp(afternoonWarm - predawnCold, -3.0f, 3.0f);
}

// Explicit 24-hour diurnal thermal-lag hysteresis helper (peak +2.5 C at 15:00, minimum -2.5 C at 03:00-05:00).
float evalDiurnalThermalLagFromHourC(float gameHour) {
    const float h = isfinite(gameHour) ? (frac(gameHour / 24.0f) * 24.0f) : 12.0f;
    // Cosine wave peaking at 15.0h (3 PM thermal lag behind solar noon):
    return clamp(cos((h - 15.0f) * (6.2831853f / 24.0f)) * 2.5f, -3.0f, 3.0f);
}

// Inverse-square windowed thermal flux helper for a single fire/hearth/forge/magma source.
// Returns normalized heat intensity in [0, 1.5] (1.0 at warm melt core, 0.0 outside fireRadiusU).
float evalFireThermalMeltMask(float3 posRel, float3 firePosRel, float fireRadiusU, float fireHeat01) {
    if (!isfinite(fireRadiusU) || fireRadiusU <= 1.0f || !isfinite(fireHeat01) || fireHeat01 <= 0.01f) { return 0.0f; }
    const float3 d = posRel - firePosRel;
    if (!all(isfinite(d))) { return 0.0f; }
    const float r2 = fireRadiusU * fireRadiusU;
    const float d2 = dot(d, d);
    if (d2 >= r2) { return 0.0f; }
    const float u = saturate(1.0f - sqrt(max(d2, 0.0f)) / fireRadiusU);
    return clamp(u * u * 1.65f * saturate(fireHeat01), 0.0f, 1.5f);
}

// Wave 61: Radiant campfire / hearth / volcanic lava local snow-melt helper with slush perimeter ring.
// Scales physical radiant reach by blackbody temperature (1050K campfire -> 2400K molten lava).
float evalRadiantMeltFlux(float3 posRel, float3 sourcePosRel, float radiusU, float sourceTempK, out float outSlushRim01) {
    outSlushRim01 = 0.0f;
    if (!isfinite(radiusU) || radiusU <= 1.0f || !isfinite(sourceTempK) || sourceTempK <= 310.0f) { return 0.0f; }
    const float3 d = posRel - sourcePosRel;
    if (!all(isfinite(d))) { return 0.0f; }
    const float d2 = dot(d, d);
    const float r2 = radiusU * radiusU;
    if (d2 >= r2) { return 0.0f; }
    const float dist = sqrt(max(d2, 0.0f));
    const float normD = saturate(dist / radiusU);
    // Smooth radiant window: inverse-square core with C2 cutoff at radiusU:
    const float win = (1.0f - normD * normD);
    const float tempScale = clamp((sourceTempK - 310.0f) / 1100.0f, 0.25f, 2.2f);
    const float invSq = 1.0f / (1.0f + normD * normD * 5.2f);
    const float flux = clamp(invSq * win * win * 1.85f * tempScale, 0.0f, 1.5f);
    // Wet slush transition collar rings the perimeter right outside the bare thawed heat core:
    outSlushRim01 = saturate(smoothstep(0.05f, 0.28f, flux) * (1.0f - smoothstep(0.48f, 0.92f, flux)));
    return flux;
}

// Evaluates per-pixel directional solar/aspect snow melt, fire/hearth melt rings, metamorphic snowpack
// albedo & roughness, freeze-thaw black ice & windward frost rime, and snowmelt puddle runoff/evaporation.
// Uniform packing (matching RawWeatherSim::WeatherGpuUniforms):
//   seasonThermal0 = float4(snowDepthM, snowAlbedo, snowCompaction01, surfaceTempC)
//   seasonThermal1 = float4(iceThicknessM, blackIce01, frostRime01, iceFracture01)
SurfaceThermalEval evalSeasonalSurfaceThermals(
    float3 worldPosAbs,
    float3 normalW,
    float3 sunDir,
    float2 windXY,
    float4 seasonThermal0,
    float4 seasonThermal1,
    float  fireHeat01,
    float  baseWet01,
    float  basePuddle01,
    float  puddleEvapRate)
{
    SurfaceThermalEval o;
    const float3 n = safeNormThermal(normalW, float3(0.0f, 0.0f, 1.0f));
    const float3 sd = safeNormThermal(sunDir, float3(0.0f, 0.0f, 1.0f));
    const float2 wSafe = all(isfinite(windXY)) ? windXY : float2(0.866f, 0.5f);
    const float windLen = length(wSafe);
    const float2 windDir = (windLen > 1.0e-3f) ? (wSafe / windLen) : float2(0.866f, 0.5f);

    const float baseSnowDepthM = isfinite(seasonThermal0.x) ? max(seasonThermal0.x, 0.0f) : 0.0f;
    const float agedAlbedoS    = isfinite(seasonThermal0.y) ? clamp(seasonThermal0.y, 0.48f, 0.92f) : 0.78f;
    const float compaction01   = isfinite(seasonThermal0.z) ? saturate(seasonThermal0.z) : 0.0f;
    const float rawTempC       = isfinite(seasonThermal0.w) ? clamp(seasonThermal0.w, -45.0f, 45.0f) : 0.0f;

    const float iceThickM  = isfinite(seasonThermal1.x) ? max(seasonThermal1.x, 0.0f) : 0.0f;
    const float blackIceIn = isfinite(seasonThermal1.y) ? saturate(seasonThermal1.y) : 0.0f;
    const float rimeIn     = isfinite(seasonThermal1.z) ? saturate(seasonThermal1.z) : 0.0f;
    const float fractureIn = isfinite(seasonThermal1.w) ? saturate(seasonThermal1.w) : 0.0f;

    // Organic world-locked 3D noise breakup (zero tiling, 100% static, zero time pulse!):
    const float nMacro = rawNoise3Grad(worldPosAbs * 0.0085f + float3(7.1f, 13.3f, 2.9f)).x;
    const float nMicro = rawNoise3Grad(worldPosAbs * 0.0650f + float3(23.7f, 4.1f, 17.9f)).x;
    const float driftNoise = nMacro * 0.68f + nMicro * 0.32f;

    // Wave 61: Diurnal Thermal-Lag Hysteresis + Elevation Lapse-Rate Snowline Micro-Undulation:
    // Afternoon thermal inertia accelerates solar snowmelt while pre-dawn radiative cooling deepens morning frost;
    // higher alpine elevations chill at environmental lapse rate (~-4.5 C per 5,000u) with organic gully undulation!
    const float diurnalLagC = evalDiurnalThermalLagFromSunC(sd);
    const float altExcessU  = max(worldPosAbs.z - 600.0f, 0.0f);
    const float lapseDropC  = min(altExcessU * (4.5f / 5000.0f), 14.0f);
    const float snowlineUndulC = (nMacro - 0.5f) * 1.50f + (nMicro - 0.5f) * 0.55f;
    const float surfTempC = clamp(rawTempC + diurnalLagC - lapseDropC + snowlineUndulC, -45.0f, 45.0f);

    // 1) Directional Aspect & Slope Solar Melt:
    // South-facing sunlit slopes (-Y in Skyrim's North=+Y world) melt first while shaded north shelves keep snow.
    const float southAspect = saturate(-n.y * 0.65f + 0.35f);
    const float sunExposure = saturate(dot(n, sd)) * smoothstep(0.0f, 0.22f, sd.z);
    const float warmMeltBias = smoothstep(-2.5f, 6.0f, surfTempC) * (0.35f + 0.65f * max(sunExposure, southAspect * 0.65f));
    const float slopeHold = smoothstep(0.32f, 0.78f, n.z);

    // 2) Active Fire / Hearth / Forge / Lava Heat-Radius Melt Ring:
    // Carves realistic bare damp ground/stone in the heat core surrounded by a glossy blue-grey slush perimeter!
    const float heatSafe = isfinite(fireHeat01) ? max(fireHeat01, 0.0f) : 0.0f;
    const float heatNoised = saturate(heatSafe * (0.86f + 0.28f * nMicro));
    const float fireCore = smoothstep(0.16f, 0.68f, heatNoised);
    const float slushRim = smoothstep(0.03f, 0.24f, heatNoised) * (1.0f - smoothstep(0.44f, 0.82f, heatNoised));

    // Effective local snow thickness & coverage (elevation chill subtly reinforces snowpack above snowline):
    const float lapseSnowBoost = smoothstep(0.5f, -5.5f, surfTempC) * smoothstep(1400.0f, 4800.0f, worldPosAbs.z) * 0.08f * step(0.005f, baseSnowDepthM);
    const float aspectDepth = max((baseSnowDepthM + lapseSnowBoost) * slopeHold * (0.65f + 0.70f * driftNoise) - warmMeltBias * 0.14f, 0.0f);
    const float localDepthM = aspectDepth * (1.0f - fireCore);
    const float snowCov = smoothstep(0.015f, 0.12f, localDepthM) * slopeHold;

    // Metamorphic snow albedo & roughness aging:
    // Fresh powder: high bright neutral-cool albedo (0.88) & fluffy matte roughness (0.88)
    // Dense firn:   compacted (~0.70) & firmer roughness (~0.62)
    // Wet spring:   granular melt crust (~0.55) & glistening damp roughness (~0.28)
    const float wetMeltMask = saturate(smoothstep(-0.5f, 3.5f, surfTempC) * 0.65f + slushRim * 0.85f);
    const float3 powderCol = float3(0.95f, 0.98f, 1.02f) * agedAlbedoS;
    const float3 slushCol  = float3(0.62f, 0.72f, 0.82f) * max(agedAlbedoS * 0.85f, 0.45f);
    const float3 snowAlb   = lerp(powderCol, slushCol, wetMeltMask * 0.65f) * lerp(0.92f, 1.05f, driftNoise);
    const float  snowRough = lerp(lerp(0.88f, 0.60f, compaction01), 0.28f, wetMeltMask);

    // 3) Freeze-Thaw Black Ice Glaze & Pre-Dawn Windward Hoarfrost Rime (100% static!):
    const float upFlat = smoothstep(0.55f, 0.90f, n.z);
    const float windward = saturate(dot(n.xy, -windDir)) * smoothstep(0.05f, 0.75f, n.z);
    const float iceCrackN = rawNoise3Grad(worldPosAbs * 0.042f + float3(11.3f, 19.7f, 5.1f)).x;
    const float thawBreak = 1.0f - fractureIn * smoothstep(0.45f, 0.85f, iceCrackN) * 0.75f;
    const float predawnRimeBoost = smoothstep(-0.5f, -3.5f, surfTempC) * saturate(-diurnalLagC * 0.35f) * 0.22f;
    const float iceGlaze = saturate(max(blackIceIn * upFlat, smoothstep(0.02f, 0.18f, iceThickM) * upFlat * 0.75f)
                                    * thawBreak * (1.0f - snowCov * 0.75f) * (1.0f - fireCore));
    const float frostRime = saturate(max(rimeIn, predawnRimeBoost) * (0.42f * upFlat + 0.68f * windward) * (0.55f + 0.45f * driftNoise)
                                     * (1.0f - fireCore) * (1.0f - slushRim * 0.6f));

    // 4) Snowmelt Runoff -> Wetness & Puddle Evaporation Balance:
    const float runoffWet = saturate(warmMeltBias * smoothstep(0.01f, 0.10f, baseSnowDepthM) * 0.55f
                                     + max(fireCore, slushRim) * smoothstep(0.005f, 0.06f, baseSnowDepthM) * 0.85f);
    const float evapFactor = saturate(1.0f - puddleEvapRate * (0.45f + 0.85f * sunExposure));
    const float puddleOut = saturate((basePuddle01 + runoffWet * 0.45f) * upFlat * evapFactor
                                     * (1.0f - snowCov * 0.85f) * (1.0f - iceGlaze * 0.90f));

    o.snowCov01      = saturate(snowCov);
    o.snowDepthM     = localDepthM;
    o.snowAlbedo     = saturate(snowAlb);
    o.snowRoughness  = clamp(snowRough, 0.18f, 0.94f);
    o.iceGlaze01     = iceGlaze;
    o.frostRime01    = frostRime;
    o.fireMeltRing01 = fireCore;
    o.slushRim01     = slushRim;
    o.wetBoost01     = saturate(max(baseWet01 * evapFactor, runoffWet) * (1.0f - iceGlaze * 0.7f));
    o.puddleMask01   = puddleOut;
    return o;
}

// Evaluates continuous botanical phenology on deciduous & evergreen foliage:
//   phenology0 = float4(budBreak01, chlorophyll01, autumnCarotenoid01, autumnAnthocyanin01)
//   phenology1 = float4(leafAbscission01, canopySnowLoadKgM2, inversionStrength01, puddleEvapRate)
SeasonalFoliageEval evalSeasonalFoliagePhenology(
    float3 worldPosAbs,
    float3 normalW,
    float3 baseAlbedo,
    float4 phenology0,
    float4 phenology1,
    float4 seasonThermal0,
    float4 seasonThermal1,
    bool   isEvergreen)
{
    SeasonalFoliageEval f;
    const float3 n = normalize(normalW);
    const float budBreak   = saturate(phenology0.x);
    const float chloro     = saturate(phenology0.y);
    const float carot      = saturate(phenology0.z);
    const float antho      = saturate(phenology0.w);
    const float abscission = saturate(phenology1.x);
    const float snowKgM2   = max(phenology1.y, 0.0f);
    const float surfTempC  = seasonThermal0.w;
    const float rime01     = saturate(seasonThermal1.z);

    // Per-tree / per-bough world-locked variation so an autumn forest shifts organically tree by tree:
    const float treeVar = rawNoise3Grad(worldPosAbs * 0.0028f + float3(19.3f, 7.1f, 3.7f)).x;
    const float leafVar = rawNoise3Grad(worldPosAbs * 0.0450f + float3(5.3f, 29.1f, 11.7f)).x;

    // Upward bough snow load coverage & sub-zero frost rime:
    const float boughUp = smoothstep(0.28f, 0.78f, n.z);
    f.canopySnowCov01 = saturate(smoothstep(0.8f, 16.0f, snowKgM2) * boughUp * (0.65f + 0.55f * leafVar));
    f.frostRim01      = saturate(rime01 * smoothstep(1.0f, -4.0f, surfTempC) * (0.45f + 0.55f * boughUp) * (1.0f - f.canopySnowCov01 * 0.5f));

    if (isEvergreen) {
        // Boreal conifers stay deep saturated evergreen year-round (subtle spring new-shoot brightness, deeper winter olive):
        const float3 springShoot = float3(0.96f, 1.08f, 0.88f);
        const float3 winterDeep  = float3(0.84f, 0.92f, 0.86f);
        const float winterW = smoothstep(4.0f, -6.0f, surfTempC);
        f.albedoTint     = lerp(lerp(1.0f.xxx, springShoot, budBreak * 0.30f), winterDeep, winterW * 0.45f);
        f.alphaDiscard01 = 0.0f;
        return f;
    }

    // Deciduous seasonal pigment chemistry (Chlorophyll -> Carotenoid Gold -> Anthocyanin Crimson):
    const float greenness = saturate((baseAlbedo.g - max(baseAlbedo.r, baseAlbedo.b) * 0.82f) * 3.8f);
    const float goldW   = saturate(carot * (0.65f + 0.70f * treeVar));
    const float redW    = saturate(antho * smoothstep(0.35f, 0.85f, treeVar * 0.7f + leafVar * 0.3f));
    const float springW = saturate(budBreak * (1.0f - goldW));

    float3 tint = lerp(float3(0.92f, 1.00f, 0.92f), float3(0.94f, 1.16f, 0.78f), springW * 0.55f);
    tint = lerp(tint, float3(1.36f, 0.92f, 0.30f), goldW * greenness);
    tint = lerp(tint, float3(1.28f, 0.34f, 0.18f), redW * greenness);
    // Late-autumn senescent russet-brown right before abscission:
    const float russetW = smoothstep(0.45f, 0.92f, abscission);
    tint = lerp(tint, float3(0.76f, 0.46f, 0.24f), russetW * 0.65f * (1.0f - chloro * 0.5f));

    f.albedoTint     = tint;
    f.alphaDiscard01 = saturate(smoothstep(0.18f, 0.95f, abscission + (leafVar - 0.5f) * 0.28f));
    return f;
}

// Evaluates crystalline shoreline/lake/river freeze-over ice crust mask for RtWater.hlsl:
// Shallow calm edges freeze first via freezing-degree-day iceThicknessM; spring fracture & fires open leads!
float evalWaterFreezeMask(
    float3 worldPosAbs,
    float  waterDepthU,
    float4 seasonThermal0,
    float4 seasonThermal1,
    float  fireHeat01)
{
    const float iceThickM  = max(seasonThermal1.x, 0.0f);
    const float fractureIn = saturate(seasonThermal1.w);
    const float surfTempC  = seasonThermal0.w;
    if (iceThickM <= 0.005f) { return 0.0f; }

    // Shallow water (<= 220u) freezes at thinner iceThicknessM; deep water freezes once ice exceeds ~0.18m:
    const float depthM = max(waterDepthU / 70.0f, 0.0f);
    const float shoreReach = smoothstep(0.02f, 0.32f, iceThickM / (0.35f + depthM * 0.38f));
    const float edgeN = rawNoise3Grad(worldPosAbs * 0.012f + float3(3.7f, 17.1f, 9.3f)).x;
    const float crackN = abs(rawNoise3Grad(worldPosAbs * 0.038f + float3(21.1f, 6.7f, 13.9f)).x * 2.0f - 1.0f);
    const float springLead = fractureIn * (1.0f - smoothstep(0.04f, 0.22f, crackN)) * smoothstep(-1.0f, 4.0f, surfTempC);
    const float fireOpen = smoothstep(0.12f, 0.55f, fireHeat01);
    return saturate((shoreReach * (0.72f + 0.56f * edgeN) - springLead * 0.85f) * (1.0f - fireOpen));
}

// ============================================================================
// Wave 43 Real 3D Volumetric Snowpack Boot-Print & NPC Trail Carving + Mud Impressions
// Drives deep shaded 3D footprints with raised displaced rim snowbanks for the player
// and all live NPCs in footTrail[0..31], plus glossy water-pooling mud prints!
// ============================================================================
struct FootTrailResult {
    float  cavityDepth01;
    float  rimHeight01;
    float3 normalPerturb;
    float  aoFactor;
    float  roughDelta;
};

FootTrailResult evalSnowAndMudFootTrail(
    float3 worldPosAbs,
    float3 normalW,
    float4 footTrail[32],
    float  snowMask,
    float  wetMask)
{
    FootTrailResult r;
    r.cavityDepth01 = 0.0f;
    r.rimHeight01   = 0.0f;
    r.normalPerturb = 0.0f.xxx;
    r.aoFactor      = 1.0f;
    r.roughDelta    = 0.0f;

    const bool inSnow = (snowMask > 0.15f);
    const bool inMud  = (!inSnow && wetMask > 0.25f);
    if (!inSnow && !inMud) { return r; }

    const float3 nUnit = normalize(normalW);
    const float nCrumb = rawNoise3Grad(worldPosAbs * 0.22f + float3(3.1f, 7.7f, 1.9f)).x;

    float maxDep = 0.0f;
    float maxRim = 0.0f;
    float2 slopeAccum = 0.0f.xx;
    float  slopeW     = 0.0f;

    [loop] for (uint fi = 0u; fi < 32u; ++fi) {
        const float4 ft = footTrail[fi];
        if (ft.w <= 0.01f) { continue; }
        const float3 fd = worldPosAbs - ft.xyz;
        if (abs(fd.z) >= 96.0f) { continue; }
        const float r2 = dot(fd.xy, fd.xy);
        if (r2 >= 676.0f) { continue; } // 26u cutoff covers r~13u cavity + r~14..22u displaced rim

        // Estimate directional boot stride axis from adjacent trail slot (or deterministic fallback heading):
        const float4 ftNext = footTrail[(fi + 1u) & 31u];
        float2 strideDir = float2(1.0f, 0.0f);
        if (ftNext.w > 0.01f) {
            const float2 seg = ftNext.xy - ft.xy;
            const float segL = length(seg);
            if (segL > 2.0f && segL < 180.0f) {
                strideDir = seg / segL;
            } else {
                const float ang = rawHash33(float3(ft.xy * 0.05f, float(fi))).x * 6.2831853f;
                strideDir = float2(cos(ang), sin(ang));
            }
        } else {
            const float ang = rawHash33(float3(ft.xy * 0.05f, float(fi))).x * 6.2831853f;
            strideDir = float2(cos(ang), sin(ang));
        }
        const float2 stridePerp = float2(-strideDir.y, strideDir.x);

        // Directional elliptical boot footprint (r ~ 13u average: ~15u length x ~11u width):
        const float along  = dot(fd.xy, strideDir);
        const float across = dot(fd.xy, stridePerp);
        const float2 eVec  = float2(along * (1.0f / 1.18f), across * (1.0f / 0.84f));
        const float eDist  = length(eVec);

        const float jitter = (nCrumb - 0.5f) * 2.2f;
        const float dep = (1.0f - smoothstep(4.5f, 13.5f + jitter, eDist)) * saturate(ft.w);
        const float rim = smoothstep(10.5f, 15.2f, eDist) * (1.0f - smoothstep(16.8f, 22.8f + jitter, eDist)) * saturate(ft.w);

        if (dep > 0.01f || rim > 0.01f) {
            const float rr = max(sqrt(r2), 0.5f);
            const float2 radXY = fd.xy / rr;
            // Inward-sloping cavity wall normal (points toward boot center: -radXY) on cavity edge,
            // outward-sloping displaced rim snowbank normal (+radXY) on outer rim flank:
            const float cavWall = smoothstep(2.5f, 11.5f, eDist) * (1.0f - smoothstep(11.5f, 14.5f, eDist)) * saturate(ft.w);
            const float rimOut  = smoothstep(14.5f, 20.5f, eDist) * (1.0f - smoothstep(19.5f, 23.0f, eDist)) * saturate(ft.w);
            const float wTap = max(dep, rim);
            slopeAccum += radXY * (-cavWall * 0.72f + rimOut * 0.44f);
            slopeW     += wTap;
            maxDep = max(maxDep, dep);
            maxRim = max(maxRim, rim);
        }
    }

    if (maxDep <= 0.005f && maxRim <= 0.005f) { return r; }

    // Overlapping footsteps suppress inner rims where a deeper trench carves through:
    const float effCav = saturate(maxDep);
    const float effRim = saturate(maxRim * (1.0f - effCav * 0.85f));
    const float2 avgSlope = (slopeW > 1.0e-4f) ? (slopeAccum / max(slopeW, 1.0f)) : 0.0f.xx;

    if (inSnow) {
        const float snowW = smoothstep(0.15f, 0.55f, snowMask);
        r.cavityDepth01 = effCav * snowW;
        r.rimHeight01   = effRim * snowW;
        const float3 rawPert = float3(avgSlope * (0.85f * snowW), 0.0f);
        r.normalPerturb = rawPert - nUnit * dot(rawPert, nUnit);
        r.aoFactor      = lerp(1.0f, 0.62f, r.cavityDepth01);
        r.roughDelta    = -0.18f * r.cavityDepth01 + 0.05f * r.rimHeight01;
    } else {
        const float mudW = smoothstep(0.25f, 0.65f, wetMask) * (1.0f - smoothstep(0.05f, 0.15f, snowMask));
        r.cavityDepth01 = effCav * mudW;
        r.rimHeight01   = effRim * mudW * 0.55f;
        const float3 rawPert = float3(avgSlope * (0.55f * mudW), 0.0f);
        r.normalPerturb = rawPert - nUnit * dot(rawPert, nUnit);
        r.aoFactor      = lerp(1.0f, 0.72f, r.cavityDepth01);
        r.roughDelta    = -0.22f * r.cavityDepth01;
    }
    return r;
}

#endif
