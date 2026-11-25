// RawWeathering.hlsli: True 3D Procedural Surface Weathering, Edge Chipping, Masonry Mortar Erosion,
// Seasonal Creeping Moss/Lichen Colonies & Thermal Hearth/Forge Soot Accumulation (SM 6.6 / SM 6.10).
// Standalone clean-room MIT HLSL library (zero external textures, zero cbuffer dependency,
// zero uniform Voronoi hexagons, 100% static world-locked relief):
//   - Wired LIVE into Lighting.hlsl (and callable from RawTerrainWorld.hlsl)
//   - Inspired by procedural curvature-driven weathering, damp creeping moss & lived-in soot accumulation
// Original work, MIT.
#ifndef RAW_WEATHERING_HLSLI
#define RAW_WEATHERING_HLSLI

#include "RawProceduralNoise.hlsli"

struct RawWeatheringResult {
    float3 albedo;
    float3 normal;
    float  roughness;
    float  chipEdge01;       // Curvature-driven chipped stone/wood/iron bevel mask
    float  mortarCrack01;    // Concave mortar erosion & analytical micro-fracture mask
    float  mossCov01;        // North-facing / damp seasonal creeping moss & lichen
    float  sootMask01;       // Thermal soot & smoke stain above hearths/forges/sconces
    float  porosityWet01;    // Wave 58: multi-scale rainwater puddle porosity darkening mask
    float  hoarfrost01;      // Wave 58: sub-zero hoarfrost/rime coverage
    float  hoarfrostGlint01; // Wave 58: sub-zero crystal micro-sparkle mask
};

// Evaluates rising thermal smoke/soot deposition directly above an active hearth, forge, brazier, or sconce.
// Strictly activates above the flame (`posRel.z > lightPosRel.z + 2u`) within a gently expanding plume column,
// sculpted by vertical rising smoke wisps so chimney hoods and sconce stones look genuinely lived-in.
float evalThermalSootColumn(float3 posRel, float3 lightPosRel, float lightRadiusU, float warmIntensity01) {
    if (lightRadiusU <= 4.0f || warmIntensity01 <= 0.02f) { return 0.0f; }
    const float dz = posRel.z - lightPosRel.z;
    const float maxRise = min(lightRadiusU * 0.72f, 220.0f);
    if (dz <= 2.0f || dz >= maxRise) { return 0.0f; }
    const float hFrac = dz / maxRise;
    // Gently expanding rising thermal plume cone (tight near flame, widening smoothly up chimney/wall):
    const float plumeRad = lerp(22.0f, min(lightRadiusU * 0.28f, 92.0f), pow(hFrac, 0.75f));
    const float2 dXY = posRel.xy - lightPosRel.xy;
    const float rHoriz = length(dXY);
    if (rHoriz >= plumeRad) { return 0.0f; }
    const float radial = 1.0f - smoothstep(0.15f, 1.0f, rHoriz / plumeRad);
    const float vertEnv = smoothstep(0.0f, 0.14f, hFrac) * (1.0f - smoothstep(0.45f, 1.0f, hFrac));
    return saturate(radial * vertEnv * warmIntensity01);
}

// Evaluates unified 3D procedural surface weathering at `worldPosAbs`:
//   - curvConvex / curvConcave: quad-safe screen-space curvature computed from ddx/ddy of N & P
//   - phenology0: float4(budBreak01, chlorophyll01, autumnCarotenoid01, autumnAnthocyanin01)
//   - seasonThermal0: float4(snowDepthM, snowAlbedo, snowCompaction01, surfaceTempC)
//   - sootHeat01: accumulated rising soot exposure above nearby warm hearths/forges/sconces
RawWeatheringResult evalSurfaceWeathering3D(
    float3 worldPosAbs,
    float3 normalW,
    float3 baseAlbedo,
    float  baseRough,
    float  curvConvex,
    float  curvConcave,
    float  zDist,
    uint   matBase,
    float  wet01,
    float  snow01,
    float  iceMask01,
    float  waterProx01,
    float4 phenology0,
    float4 seasonThermal0,
    float  sootHeat01,
    bool   isExterior)
{
    RawWeatheringResult o;
    o.albedo           = all(isfinite(baseAlbedo)) ? max(baseAlbedo, 0.0f.xxx) : 0.0f.xxx;
    const float nLen2  = dot(normalW, normalW);
    const float3 nUnit = (isfinite(nLen2) && nLen2 > 1.0e-8f) ? (normalW * rsqrt(nLen2)) : float3(0.0f, 0.0f, 1.0f);
    o.normal           = nUnit;
    o.roughness        = isfinite(baseRough) ? clamp(baseRough, 0.04f, 0.98f) : 0.5f;
    o.chipEdge01       = 0.0f;
    o.mortarCrack01    = 0.0f;
    o.mossCov01        = 0.0f;
    o.sootMask01       = 0.0f;
    o.porosityWet01    = 0.0f;
    o.hoarfrost01      = 0.0f;
    o.hoarfrostGlint01 = 0.0f;

    const float distFade = smoothstep(3200.0f, 180.0f, max(zDist, 0.0f));
    if (distFade <= 0.005f) { return o; }

    const float nonSnowIce = (1.0f - saturate(snow01 * 1.45f)) * (1.0f - saturate(iceMask01 * 2.0f));
    const float surfTempC = isfinite(seasonThermal0.w) ? seasonThermal0.w : 0.0f;
    const bool hasTherm = (seasonThermal0.y > 0.05f || abs(surfTempC) > 0.01f);

    // Multi-octave analytical 3D gradient noise (ZERO uniform Voronoi polygons!):
    const float4 nMacro = rawNoise3Grad(worldPosAbs * 0.026f + float3(5.3f, 11.7f, 2.9f));
    const float4 nFine  = rawNoise3Grad(worldPosAbs * 0.115f + float3(19.1f, 3.7f, 14.3f));
    const float  chipNoise = saturate(nMacro.x * 0.55f + nFine.x * 0.45f);

    // -------------------------------------------------------------------------
    // 1) Curvature-Driven Edge Chipping & Masonry Mortar Erosion / Micro-Fractures
    // -------------------------------------------------------------------------
    const bool isMetal = (matBase == 2u);
    const float chipMask = saturate(curvConvex * smoothstep(0.28f, 0.78f, chipNoise)) * distFade * nonSnowIce;
    // Sparse organic hairline stone micro-fractures (domain-warped ridge noise gated by macro patch, NEVER honeycomb):
    const float fracRegion = smoothstep(0.56f, 0.84f, nMacro.x);
    const float fracRidge  = pow(saturate(1.0f - abs(nFine.x * 2.0f - 1.0f)), 6.0f) * fracRegion;
    const float mortarMask = saturate((curvConcave * (0.55f + 0.45f * nMacro.x) + fracRidge * (isMetal ? 0.18f : 0.48f)))
                           * distFade * nonSnowIce;

    // Subtle chipped bevel brightening + freshly exposed micro-roughness shift:
    const float3 chipTint = isMetal
        ? lerp(o.albedo * 1.18f + 0.05f.xxx, float3(0.72f, 0.74f, 0.78f), 0.25f)
        : (o.albedo * 1.16f + float3(0.025f, 0.024f, 0.022f));
    o.albedo = lerp(o.albedo, saturate(chipTint), chipMask * (isMetal ? 0.42f : 0.30f));
    // Darkened eroded mortar joints & hairline fractures:
    o.albedo *= (1.0f - mortarMask * (isMetal ? 0.16f : 0.28f));
    o.roughness = clamp(o.roughness - chipMask * (isMetal ? 0.18f : 0.06f) + mortarMask * 0.15f, 0.05f, 0.96f);

    // Analytical tangent-projected micro-normal perturbation along chipped bevels & eroded cracks:
    const float3 rawGrad = nFine.yzw * (0.12f * mortarMask + 0.08f * chipMask) + nMacro.yzw * (0.06f * mortarMask);
    const float3 tangGrad = rawGrad - nUnit * dot(rawGrad, nUnit);
    const float3 nPert = nUnit + tangGrad;
    const float nPert2 = dot(nPert, nPert);
    o.normal = (isfinite(nPert2) && nPert2 > 1.0e-8f) ? (nPert * rsqrt(nPert2)) : nUnit;
    o.chipEdge01    = chipMask;
    o.mortarCrack01 = mortarMask;

    // -------------------------------------------------------------------------
    // 2) Wave 58: Multi-Scale Rainwater Puddle Porosity Darkening (Porous Stone/Soil/Wood)
    // -------------------------------------------------------------------------
    const float liquidGate = hasTherm ? smoothstep(-2.0f, 1.5f, surfTempC) : 1.0f;
    const float wetDrive = saturate(wet01 * 0.95f + waterProx01 * 0.78f) * liquidGate * nonSnowIce;
    if (!isMetal && wetDrive > 0.02f) {
        // Multi-scale puddle basins (macro depression + fine stone pore absorption + concave mortar pooling):
        const float4 pBasin = rawNoise3Grad(worldPosAbs * 0.0085f + float3(41.3f, 17.9f, 3.1f));
        const float upPool = smoothstep(0.16f, 0.78f, nUnit.z);
        const float basinMask = smoothstep(0.32f, 0.76f, (1.0f - pBasin.x) * 0.60f + (1.0f - nMacro.x) * 0.25f + mortarMask * 0.45f);
        const float poreCapillary = saturate(0.45f + 0.55f * (1.0f - nFine.x));
        const float porosityWet = saturate(wetDrive * (0.42f * poreCapillary + 0.68f * basinMask * upPool)) * distFade;
        const float puddleFilm  = saturate(wetDrive * basinMask * upPool * smoothstep(0.48f, 0.88f, nUnit.z)) * distFade;
        if (porosityWet > 0.01f) {
            // Physical porous dielectric darkening: water displaces air in micro-pores -> quadratic internal-bounce deepening
            // (preserves rich mineral hue saturation rather than washing stone to muddy grey!).
            const float3 deepWetAlb = pow(max(o.albedo, 1.0e-4f.xxx), 1.46f) * 0.88f;
            o.albedo = lerp(o.albedo, deepWetAlb, porosityWet * 0.62f);
            o.roughness = clamp(lerp(o.roughness, max(o.roughness * 0.42f, 0.08f), max(porosityWet * 0.45f, puddleFilm * 0.82f)), 0.05f, 0.96f);
            // Gently level pooled puddle micro-normals toward horizontal water film:
            const float3 nPool = lerp(o.normal, float3(0.0f, 0.0f, 1.0f), puddleFilm * 0.42f);
            const float nPool2 = dot(nPool, nPool);
            o.normal = (isfinite(nPool2) && nPool2 > 1.0e-8f) ? (nPool * rsqrt(nPool2)) : o.normal;
            o.porosityWet01 = porosityWet;
        }
    }

    // -------------------------------------------------------------------------
    // 3) Seasonal North-Facing Slope Moss & Lichen Moisture Accumulation (Exterior Stone/Rock/Bark)
    // -------------------------------------------------------------------------
    if (isExterior && !isMetal && nonSnowIce > 0.05f) {
        // Suppressed in deep sub-zero blizzard zones unless thermal uniforms are uninitialized (0):
        const float tempGate = hasTherm ? smoothstep(-7.5f, 2.5f, surfTempC) : 1.0f;
        // Altitude transition: lush damp moss in valleys/foothills -> hardy pale-sage alpine lichen on mid-high crags:
        const float altLichen = smoothstep(2200.0f, 5400.0f, worldPosAbs.z);
        const float altCap    = 1.0f - smoothstep(5600.0f, 8200.0f, worldPosAbs.z);
        // North-facing (+Y) slope moisture retention + upward shelf pooling + damp mortar crevices & riverbanks:
        const float northFace = smoothstep(0.12f, 0.72f, nUnit.y);
        const float upShelf   = smoothstep(0.04f, 0.68f, nUnit.z);
        const float northMoisture = saturate(northFace * (0.55f + 0.45f * saturate(wet01 + waterProx01)));
        const float dampBoost = saturate(0.32f + wet01 * 0.56f + waterProx01 * 0.68f + mortarMask * 0.55f + northMoisture * 0.35f);
        const float4 mPatch   = rawNoise3Grad(worldPosAbs * 0.012f + float3(31.7f, 7.3f, 17.9f));
        const float4 mTuft    = rawNoise3Grad(worldPosAbs * 0.165f + float3(8.1f, 23.4f, 4.7f));
        const float colony    = smoothstep(0.44f, 0.80f, mPatch.x * 0.68f + mTuft.x * 0.32f);
        const float mossCov   = saturate(colony * max(northFace * 0.90f, upShelf * 0.55f) * upShelf * dampBoost * tempGate * altCap * nonSnowIce)
                              * smoothstep(2800.0f, 180.0f, max(zDist, 0.0f)) * 0.60f;
        if (mossCov > 0.015f) {
            // Subtle damp substrate moisture rim hugging the outer fringe of north-facing moss colonies:
            const float dampRim = smoothstep(0.015f, 0.18f, mossCov) * (1.0f - smoothstep(0.18f, 0.45f, mossCov));
            o.albedo *= (1.0f - dampRim * 0.14f);
            // React smoothly to live seasonal phenology + altitude lichen transition:
            const float budW    = saturate(phenology0.x);
            const float autumnW = saturate(phenology0.z * 0.75f + phenology0.w * 0.45f);
            const float winterW = hasTherm ? smoothstep(2.5f, -3.5f, surfTempC) : 0.0f;
            const float3 springMoss = lerp(float3(0.080f, 0.182f, 0.056f), float3(0.172f, 0.288f, 0.090f), saturate(mTuft.x + budW * 0.25f));
            const float3 autumnMoss = lerp(float3(0.145f, 0.140f, 0.058f), float3(0.220f, 0.185f, 0.075f), mTuft.x);
            const float3 winterMoss = lerp(float3(0.075f, 0.115f, 0.078f), float3(0.460f, 0.540f, 0.600f), smoothstep(0.55f, 0.90f, mTuft.x) * 0.55f);
            const float3 alpineLich = lerp(float3(0.220f, 0.265f, 0.215f), float3(0.360f, 0.405f, 0.340f), mTuft.x);
            float3 mossCol = lerp(springMoss, autumnMoss, autumnW * 0.78f);
            mossCol = lerp(mossCol, winterMoss, winterW * 0.72f);
            mossCol = lerp(mossCol, alpineLich, altLichen * 0.65f);

            o.albedo    = lerp(o.albedo, mossCol, mossCov);
            o.roughness = lerp(o.roughness, 0.89f, mossCov * 0.85f);
            const float3 mGrad = mTuft.yzw - nUnit * dot(mTuft.yzw, nUnit);
            const float3 nMoss = o.normal + mGrad * (0.26f * mossCov);
            const float nMoss2 = dot(nMoss, nMoss);
            o.normal    = (isfinite(nMoss2) && nMoss2 > 1.0e-8f) ? (nMoss * rsqrt(nMoss2)) : o.normal;
            o.mossCov01 = mossCov;
        }
    }

    // -------------------------------------------------------------------------
    // 4) Wave 58: Sub-Zero Hoarfrost & Rime Micro-Sparkle (Temperature + Altitude + Exposure)
    // -------------------------------------------------------------------------
    if (isExterior) {
        const float thermFreeze = hasTherm ? smoothstep(1.2f, -4.8f, surfTempC) : 0.0f;
        const float altFreeze   = smoothstep(2600.0f, 5200.0f, worldPosAbs.z) * 0.65f;
        const float snowFreeze  = saturate(snow01 * 0.95f + seasonThermal0.x * 1.25f);
        const float coldDrive   = saturate(max(thermFreeze, max(altFreeze, snowFreeze * 0.72f)));
        if (coldDrive > 0.03f) {
            const float frostNear = smoothstep(2200.0f, 120.0f, max(zDist, 0.0f));
            const float4 fFeather = rawNoise3Grad(worldPosAbs * 0.24f + float3(29.3f, 13.7f, 41.9f));
            // Hoarfrost nucleates along upward shelves, chipped stone edges, north/shaded faces, and damp rime margins:
            const float nucleate = saturate(smoothstep(0.08f, 0.74f, nUnit.z) * 0.65f + chipMask * 0.55f + smoothstep(0.15f, 0.75f, nUnit.y) * 0.25f);
            const float frostCov = saturate(coldDrive * nucleate * smoothstep(0.32f, 0.78f, fFeather.x * 0.65f + nFine.x * 0.35f) * (1.0f - iceMask01 * 0.85f))
                                 * frostNear * 0.52f;
            if (frostCov > 0.015f) {
                const float3 frostCol = float3(0.76f, 0.83f, 0.91f);
                o.albedo = lerp(o.albedo, frostCol, frostCov * 0.55f);
                o.roughness = lerp(o.roughness, 0.42f, frostCov * 0.45f);
                // World-locked high-frequency ice-needle crystal facet sparkle (aperiodic hash, zero Voronoi tiling):
                const float3 crystalCell = floor(worldPosAbs * 0.65f);
                const float3 hSpark = rawHash33(crystalCell + float3(17.1f, 3.7f, 29.3f));
                const float microSparkle = smoothstep(0.86f, 0.995f, hSpark.x) * smoothstep(950.0f, 90.0f, max(zDist, 0.0f));
                o.hoarfrost01      = frostCov;
                o.hoarfrostGlint01 = saturate(microSparkle * frostCov * 2.4f);
            }
        }
    }

    // -------------------------------------------------------------------------
    // 5) Thermal Soot & Smoke Stain Accumulation Above Active Hearths / Forges / Sconces
    // -------------------------------------------------------------------------
    if (sootHeat01 > 0.015f && nonSnowIce > 0.10f) {
        // Vertically stretched smoke-wisp pattern staining stone chimneys, arches & timber rafters:
        const float sootN = rawNoise3Grad(float3(worldPosAbs.xy * 0.068f, worldPosAbs.z * 0.022f) + float3(13.1f, 27.3f, 6.7f)).x;
        const float sootMask = saturate(sootHeat01 * (0.58f + 0.68f * sootN)) * distFade * nonSnowIce;
        const float3 sootCol = o.albedo * float3(0.32f, 0.30f, 0.29f);
        o.albedo     = lerp(o.albedo, sootCol, sootMask * 0.74f);
        o.roughness  = lerp(o.roughness, 0.94f, sootMask * 0.80f);
        o.sootMask01 = sootMask;
    }

    return o;
}

// ============================================================================
// Wave 45 100%-Analytical Micro-Relief Crevice Self-Shadowing (ZERO staircase bands!)
// Evaluates a smooth 2-tap analytical directional height derivative along the tangent-
// projected light direction using rawNoise3Grad so grazing sun & torchlight cast crisp,
// band-free micro-shadows inside cobblestone joints, ruined stonework, and bark fissures.
// Returns smooth horizon self-occlusion strictly in [0.48, 1.0].
// ============================================================================
float evalMicroReliefSelfShadow(
    float3 worldPosAbs,
    float3 N,
    float3 L,
    float  reliefStrength,
    float  detailScale)
{
    const float str = saturate(reliefStrength);
    if (str <= 1.0e-3f) { return 1.0f; }

    const float3 nUnit = (dot(N, N) > 1.0e-8f) ? normalize(N) : float3(0.0f, 0.0f, 1.0f);
    const float3 lUnit = (dot(L, L) > 1.0e-8f) ? normalize(L) : nUnit;
    const float nl = dot(nUnit, lUnit);
    if (nl <= 0.005f) { return 1.0f; }

    // Project light direction onto local surface tangent plane (zero horizon shadow when straight overhead):
    const float3 lTangRaw = lUnit - nUnit * nl;
    const float lTangLen2 = dot(lTangRaw, lTangRaw);
    if (lTangLen2 <= 1.0e-5f) { return 1.0f; }
    const float3 lTangDir = lTangRaw * rsqrt(lTangLen2);

    const float sc = clamp(detailScale, 0.02f, 2.0f);
    const float stepU = clamp(1.35f / sc, 1.5f, 10.0f);
    const float3 seedOff = float3(5.3f, 11.7f, 2.9f);

    // 2-tap analytical height + directional gradient along tangent-projected light vector (zero loops/steps!):
    const float4 g0 = rawNoise3Grad(worldPosAbs * sc + seedOff);
    const float4 g1 = rawNoise3Grad((worldPosAbs + lTangDir * stepU) * sc + seedOff);

    const float grazing = saturate(1.0f - nl);
    const float chordRise = max(g1.x - g0.x, 0.0f);
    const float localSlope = max(dot(g0.yzw, lTangDir), 0.0f);
    const float troughBias = saturate(0.58f - g0.x);

    const float horizonSlope = chordRise * 1.35f + localSlope * 0.45f;
    const float occDrive = saturate((horizonSlope * (0.65f + 1.35f * grazing) + troughBias * grazing * 0.42f) * str);
    return lerp(1.0f, 0.48f, smoothstep(0.04f, 0.78f, occDrive));
}

#endif
