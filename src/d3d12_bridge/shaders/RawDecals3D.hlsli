// RawDecals3D.hlsli: True 3D Physical Relief Decal Enhancement Engine for RAW (SM 6.6 - SM 6.10).
// Wired into Lighting.hlsl to upgrade Skyrim's flat 2011 projected decals into physically shaded 3D relief surfaces:
//   1) Blood Pools & Arterial Splatters : wet clearcoat (F0=0.044, rough 0.07..0.14), raised surface-tension meniscus rim normal,
//                                         and Beer-Lambert arterial-to-venous depth absorption
//   2) Frost-Creep & Ice-Spell Decals   : static 3D dendritic crystal veins, 2-step view-tangent parallax relief, faceted IOR 1.31
//                                         crystal micro-normals (100% NON-PULSING — zero time modulation!)
//   3) Scorch / Burn Marks              : pitted carbon charcoal relief normals, ultra-matte soot roughness (0.92), crackled ash
//                                         crust fissures with cooling 1150K..1850K blackbody ember cracks when heat is nearby
//   4) Weapon / Impact Cracks & Scars   : 2-step view-tangent parallax cavity offset, fractured V-groove crack normals, deepened
//                                         cavity AO, and chipped-edge bevel catchlight
// 100% original MIT clean-room code; requires LightingCommon.hlsli and RawProceduralNoise.hlsli.
#ifndef RAW_DECALS_3D_HLSLI
#define RAW_DECALS_3D_HLSLI

#include "RawProceduralNoise.hlsli"

// ============================================================================
// Wave 41 Live 3D Dynamic Combat & Elemental Impact Decal System (SM 6.6 / 6.10)
// Pure-argument standalone evaluator (zero cbuffer dependency):
//   Kind 0: Physical Blood Splatter (wall drips + glossy arterial -> matte coagulated over 0..120s)
//   Kind 1: Stone Chip / Wood Splinter Impact (concave crater + pale beveled mineral/wood lip)
//   Kind 2: Fire Spell / Hearth Charred Scorch Crater (matte soot + cooling 1800K->700K Planck embers 0..18s)
//   Kind 3: Frost Spell Crystalline Ice Patch (feathered frost rime + IOR 1.31 glaze, sublimates over age)
//   Kind 4: Shock Spell Vitrified Lichtenberg Burn (dendritic burn + fading cyan-violet corona 0..4s)
// ============================================================================
struct RawImpactDecalResult {
    float3 albedo;
    float  albedoBlend;
    float  roughness;
    float  roughBlend;
    float3 normalPerturb;
    float3 emissive;
    float  f0Override;
    float  f0Blend;
    float  bloodMask;
};

RawImpactDecalResult evalDynamicImpactDecals3D(
    float3 worldPosAbs,
    float3 normalW,
    float4 decalPosR[16],
    float4 decalMeta[16])
{
    RawImpactDecalResult acc;
    acc.albedo        = 0.0f.xxx;
    acc.albedoBlend   = 0.0f;
    acc.roughness     = 0.5f;
    acc.roughBlend    = 0.0f;
    acc.normalPerturb = 0.0f.xxx;
    acc.emissive      = 0.0f.xxx;
    acc.f0Override    = 0.04f;
    acc.f0Blend       = 0.0f;
    acc.bloodMask     = 0.0f;

    const float3 nUnit = normalize(normalW);
    const float3 upRef = (abs(nUnit.z) < 0.92f) ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    const float3 tBase = normalize(cross(upRef, nUnit));
    const float3 bBase = cross(nUnit, tBase);

    float3 albSum = 0.0f.xxx;
    float  albW   = 0.0f;
    float  rghSum = 0.0f;
    float  rghW   = 0.0f;
    float  f0Sum  = 0.0f;
    float  f0W    = 0.0f;

    [loop] for (uint i = 0u; i < 16u; ++i) {
        const float rad = decalPosR[i].w;
        if (rad <= 0.5f) { continue; }
        const float3 rel = worldPosAbs - decalPosR[i].xyz;
        const float d2 = dot(rel, rel);
        const float maxR = rad * 1.48f;
        if (d2 >= maxR * maxR) { continue; }

        const float intensity = saturate(decalMeta[i].w);
        if (intensity <= 0.005f) { continue; }

        const uint  kind    = uint(decalMeta[i].x + 0.5f);
        const float ageSec  = max(decalMeta[i].y, 0.0f);
        const float seedAng = decalMeta[i].z;
        const float invRad  = 1.0f / max(rad, 1.0f);
        const float rNorm   = sqrt(d2) * invRad;

        // Tangent-plane coordinates rotated deterministically by per-decal seedAng:
        const float cs = cos(seedAng), sn = sin(seedAng);
        const float3 tRot = tBase * cs + bBase * sn;
        const float3 bRot = -tBase * sn + bBase * cs;
        const float2 uvT  = float2(dot(rel, tRot), dot(rel, bRot)) * invRad;
        const float3 relTang = rel - nUnit * dot(rel, nUnit);
        const float  relTangLen = length(relTang);
        const float3 radDirT = (relTangLen > 1.0e-3f) ? (relTang / relTangLen) : tRot;

        // 2-octave analytical 3D noise anchored to each decal's local frame (zero Voronoi hexagons!):
        const float3 seedOff = float3(seedAng * 7.31f + float(i) * 3.17f, float(i) * 11.43f, seedAng * 4.19f + 2.7f);
        const float4 n1 = rawNoise3Grad(rel * (2.6f * invRad) + seedOff);
        const float4 n2 = rawNoise3Grad(rel * (6.8f * invRad) + seedOff * 1.73f + float3(19.3f, 5.1f, 13.7f));
        const float3 n1Tang = n1.yzw - nUnit * dot(n1.yzw, nUnit);
        const float3 n2Tang = n2.yzw - nUnit * dot(n2.yzw, nUnit);

        if (kind == 0u) {
            // Kind 0: Physical Blood Splatter (organic multi-droplet + steep-wall -Z drip + arterial->coagulated aging)
            const float warpedR = rNorm + (n1.x - 0.5f) * 0.56f + (n2.x - 0.5f) * 0.24f;
            const float coreSplash = 1.0f - smoothstep(0.28f, 0.90f, warpedR);
            const float droplets = smoothstep(0.66f, 0.88f, n2.x) * (1.0f - smoothstep(0.55f, 1.38f, rNorm));
            // Gravity drip streaks running down -Z on steep walls/pillars:
            float drip = 0.0f;
            const float steepWall = 1.0f - smoothstep(0.45f, 0.78f, abs(nUnit.z));
            if (steepWall > 0.05f && rel.z < 0.0f && -rel.z < rad * 1.38f) {
                const float horizR = length(rel.xy) * invRad;
                const float streakN = rawNoise3Grad(float3(rel.xy * (6.0f * invRad), 0.35f) + seedOff).x;
                const float dripLen = (0.35f + 0.95f * streakN) * rad;
                drip = steepWall * (1.0f - smoothstep(0.18f, 0.58f, horizR))
                     * smoothstep(0.42f, 0.78f, streakN)
                     * (1.0f - smoothstep(dripLen * 0.65f, dripLen, -rel.z));
            }
            const float lifeFade = 1.0f - smoothstep(180.0f, 300.0f, ageSec);
            const float mask = saturate(max(max(coreSplash, droplets * 0.85f), drip * 0.88f)) * intensity * lifeFade;
            if (mask > 0.01f) {
                const float coag01 = smoothstep(4.0f, 120.0f, ageSec);
                const float3 freshCol = float3(0.24f, 0.012f, 0.009f);
                const float3 driedCol = float3(0.078f, 0.016f, 0.011f);
                const float3 bloodAlb = lerp(freshCol, driedCol, coag01);
                const float  bloodRgh = lerp(0.08f, 0.76f, coag01);
                const float  bloodF0  = lerp(0.035f, 0.024f, coag01);
                // Raised glossy liquid meniscus rim while fresh:
                const float meniscus = saturate(mask * (1.0f - mask) * 4.0f) * (1.0f - coag01 * 0.75f);
                albSum += bloodAlb * mask; albW += mask;
                rghSum += bloodRgh * mask; rghW += mask;
                f0Sum  += bloodF0  * mask; f0W  += mask;
                acc.normalPerturb += (-radDirT * (meniscus * 0.32f) + n2Tang * (0.06f * mask));
                acc.bloodMask = max(acc.bloodMask, mask);
            }
        } else if (kind == 1u) {
            // Kind 1: Stone Chip / Wood Splinter Impact (concave crater + pale exposed beveled lip)
            const float warpedR = rNorm + (n1.x - 0.5f) * 0.44f + (n2.x - 0.5f) * 0.18f;
            const float cavity = 1.0f - smoothstep(0.16f, 0.66f, warpedR);
            const float bevelLip = smoothstep(0.44f, 0.72f, warpedR) * (1.0f - smoothstep(0.72f, 1.04f, warpedR))
                                 * (0.52f + 0.48f * n2.x);
            const float lifeFade = 1.0f - smoothstep(240.0f, 360.0f, ageSec);
            const float mask = saturate(max(cavity, bevelLip * 0.85f)) * intensity * lifeFade;
            if (mask > 0.01f) {
                const float3 chipAlb = lerp(float3(0.105f, 0.100f, 0.095f), float3(0.45f, 0.42f, 0.38f), bevelLip);
                albSum += chipAlb * (mask * 0.78f); albW += mask * 0.78f;
                rghSum += 0.86f * mask;             rghW += mask;
                // Concave crater normal tilts inward inside pit and chips sharp across lip:
                acc.normalPerturb += (-radDirT * (cavity * 0.48f - bevelLip * 0.28f) + n2Tang * (0.24f * mask));
            }
        } else if (kind == 2u) {
            // Kind 2: Fire Spell / Hearth Charred Scorch Crater (matte soot + cooling 1800K->700K Planck embers 0..18s)
            const float warpedR = rNorm + (n1.x - 0.5f) * 0.48f + (n2.x - 0.5f) * 0.20f;
            const float scorch = 1.0f - smoothstep(0.22f, 1.06f, warpedR);
            const float lifeFade = 1.0f - smoothstep(180.0f, 320.0f, ageSec);
            const float mask = saturate(scorch) * intensity * lifeFade;
            if (mask > 0.01f) {
                const float3 sootAlb = float3(0.026f, 0.024f, 0.023f) * (0.75f + 0.50f * n2.x);
                albSum += sootAlb * (mask * 0.86f); albW += mask * 0.86f;
                rghSum += 0.94f * mask;             rghW += mask;
                acc.normalPerturb += (n1Tang * 0.14f + n2Tang * 0.18f) * mask;
                if (ageSec < 18.5f) {
                    const float cool01 = saturate(ageSec / 18.0f);
                    const float vein = pow(saturate(1.0f - abs(n2.x * 2.0f - 1.0f)), 4.5f)
                                     * (1.0f - smoothstep(0.12f, 0.82f, warpedR));
                    const float localTempK = lerp(1800.0f, 700.0f, saturate(cool01 + (1.0f - vein) * 0.32f));
                    float bbRad = 0.0f;
                    const float3 bbCol = rawPlanckBlackbodyRGB(localTempK, bbRad);
                    acc.emissive += bbCol * (bbRad * vein * (1.0f - cool01) * intensity * 1.45f);
                }
            }
        } else if (kind == 3u) {
            // Kind 3: Frost Spell Crystalline Ice Patch (feathered rime + IOR 1.31 specular glaze, sublimates over age)
            const float sublFade = 1.0f - smoothstep(18.0f, 95.0f, ageSec);
            if (sublFade > 0.01f) {
                const float warpedR = rNorm + (n1.x - 0.5f) * 0.52f;
                const float coreIce = 1.0f - smoothstep(0.20f, 0.92f, warpedR);
                const float crystalRidge = pow(saturate(1.0f - abs(n2.x * 2.0f - 1.0f)), 2.5f);
                const float rimeEdge = (1.0f - smoothstep(0.55f, 1.24f, warpedR)) * crystalRidge;
                const float mask = saturate(max(coreIce, rimeEdge * 0.85f)) * intensity * sublFade;
                if (mask > 0.01f) {
                    const float3 frostAlb = lerp(float3(0.54f, 0.75f, 0.92f), float3(0.84f, 0.93f, 0.99f), crystalRidge);
                    const float  frostRgh = lerp(0.28f, 0.09f, coreIce);
                    albSum += frostAlb * (mask * 0.82f); albW += mask * 0.82f;
                    rghSum += frostRgh * mask;           rghW += mask;
                    f0Sum  += 0.018f   * mask;           f0W  += mask; // Exact IOR 1.31 ice F0
                    acc.normalPerturb += (n1Tang * 0.12f + n2Tang * 0.22f) * mask;
                }
            }
        } else {
            // Kind 4: Shock Spell Vitrified Lichtenberg Burn (branching dendritic scars + fading cyan-violet corona 0..4s)
            const float ang = atan2(uvT.y, uvT.x);
            const float warpAng = ang + (n1.x - 0.5f) * 2.1f + (n2.x - 0.5f) * 0.85f;
            const float branchWave = abs(sin(warpAng * 4.5f + rNorm * 4.0f));
            const float branchCore = pow(saturate(1.0f - branchWave * 2.4f), 3.0f)
                                   * (1.0f - smoothstep(0.18f, 1.28f, rNorm));
            const float fineFork = pow(saturate(1.0f - abs(n2.x * 2.0f - 1.0f) * 2.6f), 2.5f)
                                 * (1.0f - smoothstep(0.10f, 1.05f, rNorm));
            const float burnHub  = 1.0f - smoothstep(0.10f, 0.42f, rNorm);
            const float lifeFade = 1.0f - smoothstep(140.0f, 260.0f, ageSec);
            const float dendrite = saturate(max(max(branchCore, fineFork * 0.72f), burnHub));
            const float mask = dendrite * intensity * lifeFade;
            if (mask > 0.01f) {
                const float3 vitAlb = float3(0.032f, 0.038f, 0.052f);
                albSum += vitAlb * (mask * 0.84f); albW += mask * 0.84f;
                rghSum += 0.24f  * mask;           rghW += mask;
                f0Sum  += 0.048f * mask;           f0W  += mask;
                acc.normalPerturb += (n2Tang * 0.24f) * mask;
                if (ageSec < 4.1f) {
                    const float coronaDecay = 1.0f - smoothstep(0.0f, 4.0f, ageSec);
                    const float flicker = 0.72f + 0.28f * rawNoise3Grad(rel * (4.0f * invRad) + float3(0.0f, 0.0f, ageSec * 14.0f)).x;
                    const float3 arcCol = lerp(float3(0.30f, 0.78f, 1.45f), float3(0.86f, 0.52f, 1.50f), n2.x);
                    acc.emissive += arcCol * (dendrite * coronaDecay * flicker * intensity * 3.0f);
                }
            }
        }
    }

    if (albW > 1.0e-4f) {
        acc.albedo      = saturate(albSum / albW);
        acc.albedoBlend = saturate(albW);
    }
    if (rghW > 1.0e-4f) {
        acc.roughness  = clamp(rghSum / rghW, 0.05f, 0.96f);
        acc.roughBlend = saturate(rghW);
    }
    if (f0W > 1.0e-4f) {
        acc.f0Override = clamp(f0Sum / f0W, 0.015f, 0.12f);
        acc.f0Blend    = saturate(f0W);
    }
    acc.emissive = min(acc.emissive, 8.0f.xxx);
    return acc;
}

#ifdef MAX_LIGHTS

struct RawDecalResult {
    float3 albedo;
    float3 normal;
    float  alpha;
    float  f0;
    float  mask;
    float3 emissive;
};

// Cooling blackbody ramp for recessed scorch ember cracks (1150K deep ember red -> 1850K glowing orange-gold).
float3 rawDecalEmberBlackbody(float t) {
    const float u = saturate(t);
    return lerp(float3(1.15f, 0.12f, 0.015f), float3(2.45f, 0.88f, 0.14f), u * u);
}

// Evaluates all 4 physical 3D relief decal archetypes in world + screen tangent space.
// Note: Call from uniform !match control flow so ddx/ddy derivatives execute across full 2x2 pixel quads.
RawDecalResult evalDecals3D(float3 albedoIn, float3 nIn, float3 vDir, float3 pRel, float z,
                            float alphaIn, float f0In, float maskIn,
                            uint base, float iceMask, float wetSnow) {
    RawDecalResult o;
    o.albedo   = albedoIn;
    o.normal   = nIn;
    o.alpha    = alphaIn;
    o.f0       = f0In;
    o.mask     = maskIn;
    o.emissive = 0.0f.xxx;

    const float3 lw = float3(0.2126f, 0.7152f, 0.0722f);
    const float aLum = dot(albedoIn, lw);
    const float aMax = max(albedoIn.r, max(albedoIn.g, albedoIn.b));
    const float aMin = min(albedoIn.r, min(albedoIn.g, albedoIn.b));
    const float aSat = (aMax - aMin) / max(aMax, 1.0e-4f);

    // Precompute uniform quad-safe screen derivatives BEFORE any per-pixel branch:
    const float bloodRaw = saturate((albedoIn.r - max(albedoIn.g, albedoIn.b) * 1.78f) * 6.2f)
                         * smoothstep(0.045f, 0.18f, albedoIn.r)
                         * (1.0f - smoothstep(0.38f, 0.56f, albedoIn.r));
    const float2 bloodGrad = float2(ddx(bloodRaw), ddy(bloodRaw));
    const float  lumaDx = ddx(aLum), lumaDy = ddy(aLum);
    const float  zGrad  = (abs(ddx(z)) + abs(ddy(z))) / max(z, 1.0f);
    const float3 dpX = ddx(pRel), dpY = ddy(pRel);
    const float  planeSafe = 1.0f - smoothstep(0.008f, 0.028f, zGrad);

    if (z >= 2400.0f || base == 2u || base == 5u || base == 10u || base == 11u || base == 12u) {
        return o;
    }

    const float distFade = smoothstep(2400.0f, 160.0f, z);
    const float3 wpAbs = pRel + camPosW.xyz;
    // Build orthonormal tangent basis for 3D meniscus / frost / crack relief:
    const float3 upRef = abs(nIn.z) < 0.92f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    const float3 tVec  = normalize(cross(upRef, nIn));
    const float3 bVec  = cross(nIn, tVec);
    const float  nv    = max(abs(dot(nIn, vDir)), 0.25f);
    const float3 vTan  = (-vDir + nIn * dot(vDir, nIn)) / nv;

    // -------------------------------------------------------------------------
    // 1) 3D Wet Blood Pools & Splatters (meniscus height rim + Beer-Lambert absorption + wet clearcoat)
    // -------------------------------------------------------------------------
    const float bloodMask = bloodRaw * distFade;
    if (bloodMask > 0.03f) {
        const float4 bN = rawNoise3Grad(wpAbs * 0.42f + float3(3.1f, 7.7f, 1.9f));
        // Meniscus rim peaks right at the blood edge transition (bloodRaw * (1 - bloodRaw) * 4):
        const float meniscusBand = saturate(bloodRaw * (1.0f - bloodRaw) * 4.2f) * planeSafe;
        // Convert screen-space mask slope into world tangent normal tilt so light glides over the curved liquid rim!
        const float3 worldSlope = normalize(dpX * bloodGrad.x + dpY * bloodGrad.y + nIn * 1.0e-4f);
        const float  slopeMag   = saturate(length(bloodGrad) * 6.5f);
        const float3 rimTilt    = (worldSlope - nIn * dot(worldSlope, nIn)) * (meniscusBand * slopeMag * 0.62f);
        // Subtle fluid micro-surface ripple inside pool center + raised dome curvature:
        const float3 microRipple = (tVec * (bN.y - 0.5f) + bVec * (bN.z - 0.5f)) * (0.045f * bloodMask);
        o.normal = normalize(o.normal - rimTilt + microRipple);

        // Beer-Lambert arterial-to-venous depth absorption: translucent ruby edge -> deep thick venous pool center:
        const float poolThick = saturate(bloodRaw * 1.25f + (bN.x - 0.5f) * 0.25f);
        const float3 beerBlood = exp(-poolThick * float3(0.32f, 2.65f, 3.10f));
        const float3 richBlood = o.albedo * lerp(float3(0.96f, 0.80f, 0.78f), beerBlood * float3(0.95f, 0.55f, 0.55f), poolThick * 0.68f);
        o.albedo = lerp(o.albedo, richBlood, bloodMask * 0.88f);
        // Glossy wet liquid blood clearcoat (0.075 in fresh pool center, 0.14 at drying edge; F0 = 0.044):
        const float bloodRough = lerp(0.14f, 0.075f, poolThick);
        o.alpha = lerp(o.alpha, bloodRough, bloodMask * 0.90f);
        o.f0    = lerp(o.f0, 0.044f, bloodMask * 0.85f);
        o.mask  = max(o.mask, bloodMask * 0.86f);
    }

    // -------------------------------------------------------------------------
    // 2) 3D Frost-Creep & Crystalline Ice-Decal Relief (100% STATIC, ZERO TIME PULSE!)
    //    Wave 89 Defect D fix: Strictly restrict to parallax ice (base==6) or interior ice caves (sunColour.w < 0.5)
    //    so exterior painted Nordic blue wooden barns (Whiterun) & cool-grey barrels (Riften) NEVER get frost splatter!
    // -------------------------------------------------------------------------
    const float frostAllowed = (base == 6u || sunColour.w < 0.5f) ? 1.0f : 0.0f;
    const float frostEdge = frostAllowed
                          * saturate((albedoIn.b - albedoIn.r * 1.09f) * 5.2f)
                          * smoothstep(0.18f, 0.45f, albedoIn.b)
                          * (1.0f - saturate(iceMask * 1.4f))
                          * (1.0f - saturate(wetSnow * 1.2f))
                          * smoothstep(1800.0f, 140.0f, z);
    if (frostEdge > 0.04f) {
        // 2-step view-tangent parallax into dendritic frost crust (100% world-locked, zero time modulation):
        const float3 pF1 = wpAbs + vTan * 1.25f;
        const float3 pF2 = wpAbs + vTan * 2.75f;
        const float4 f1 = rawNoise3Grad(pF1 * 0.28f + float3(5.3f, 11.1f, 2.7f));
        const float4 f2 = rawNoise3Grad(pF2 * 0.72f + float3(13.7f, 4.9f, 8.3f));
        // Sharp dendritic hexagonal crystal ridges:
        const float vein1 = pow(saturate(1.0f - abs(f1.x * 2.0f - 1.0f) * 2.4f), 2.0f);
        const float vein2 = pow(saturate(1.0f - abs(f2.x * 2.0f - 1.0f) * 2.8f), 2.5f);
        const float crystalRelief = saturate(vein1 * 0.65f + vein2 * 0.55f);
        const float3 fGrad = f1.yzw * 0.55f + f2.yzw * 0.45f;
        o.normal = normalize(o.normal + (fGrad - nIn * dot(fGrad, nIn)) * (0.42f * frostEdge));
        const float3 frostTint = lerp(float3(0.54f, 0.74f, 0.90f), float3(0.84f, 0.93f, 0.99f), crystalRelief);
        o.albedo = lerp(o.albedo, o.albedo * lerp(0.82f.xxx, frostTint * 1.28f, crystalRelief), frostEdge * 0.62f);
        o.alpha  = lerp(o.alpha, lerp(0.32f, 0.12f, crystalRelief), frostEdge * 0.80f);
        o.f0     = lerp(o.f0, 0.020f, frostEdge * 0.75f);
        o.mask   = max(o.mask, frostEdge * (0.35f + 0.45f * crystalRelief));
    }

    // -------------------------------------------------------------------------
    // 3) 3D Scorch / Burn Marks (charred pitted roughness + crackled ash + cooling ember cracks near heat)
    // -------------------------------------------------------------------------
    const float scorchMask = (1.0f - smoothstep(0.018f, 0.062f, aLum))
                           * (1.0f - smoothstep(0.14f, 0.30f, aSat))
                           * (1.0f - saturate(bloodMask * 4.0f))
                           * (1.0f - saturate(wetSnow * 2.5f))
                           * smoothstep(1600.0f, 120.0f, z);
    if (scorchMask > 0.04f) {
        const float4 c1 = rawNoise3Grad(wpAbs * 0.34f + float3(9.1f, 2.3f, 14.7f));
        const float4 c2 = rawNoise3Grad(wpAbs * 0.92f + float3(4.3f, 17.9f, 6.1f));
        // Crackled polygonal charcoal fissure network:
        const float crackV = 1.0f - smoothstep(0.015f, 0.135f, abs(c1.x * 2.0f - 1.0f));
        const float finePit = c2.x;
        const float3 charGrad = c1.yzw * 0.60f + c2.yzw * 0.40f;
        o.normal = normalize(o.normal + (charGrad - nIn * dot(charGrad, nIn)) * (0.38f * scorchMask));
        // Ultra-matte pitted soot charcoal on flat ash islands, dark recessed fissures:
        o.albedo *= lerp(1.0f, lerp(0.84f, 1.08f, finePit) * (1.0f - crackV * 0.28f), scorchMask * 0.65f);
        o.alpha  = lerp(o.alpha, 0.93f, scorchMask * 0.82f);
        o.f0     = lerp(o.f0, 0.022f, scorchMask * 0.60f);
        // Subtle cooling blackbody ember glow deep inside scorch fissures ONLY when an active warm fire/spell light is nearby!
        if (flags.y > 0u) {
            float nearHeat = 0.0f;
            const uint maxHL = min(flags.y, 12u);
            [loop] for (uint hi = 0u; hi < maxHL; ++hi) {
                const float3 hlc = lightColS[hi].rgb;
                if (hlc.r > hlc.b * 1.45f && hlc.r > 0.22f) {
                    const float3 hd = pRel - lightPosR[hi].xyz;
                    const float hRad = min(lightPosR[hi].w * 0.48f, 240.0f);
                    const float hd2 = dot(hd, hd);
                    if (hd2 < hRad * hRad) {
                        const float ha = 1.0f - sqrt(hd2) / max(hRad, 1.0f);
                        nearHeat = max(nearHeat, saturate(ha * ha * 1.6f));
                    }
                }
            }
            if (nearHeat > 0.12f) {
                const float emberMask = crackV * smoothstep(0.42f, 0.84f, c2.x) * scorchMask * saturate((nearHeat - 0.12f) * 1.85f);
                if (emberMask > 0.01f) {
                    o.emissive += rawDecalEmberBlackbody(saturate(nearHeat * 0.85f + c2.x * 0.35f)) * (emberMask * 1.15f);
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // 4) 3D Weapon / Impact Cracks & Stone Fracture Relief (parallax cavity AO + V-groove bevel normal)
    // -------------------------------------------------------------------------
    const float gradLumaMag = length(float2(lumaDx, lumaDy));
    const float crackSig = smoothstep(0.045f, 0.18f, gradLumaMag)
                         * (1.0f - smoothstep(0.12f, 0.34f, aLum))
                         * planeSafe
                         * (1.0f - saturate(bloodMask * 3.0f))
                         * (1.0f - saturate(iceMask * 2.0f))
                         * (1.0f - saturate(wetSnow * 2.0f))
                         * smoothstep(1200.0f, 100.0f, z);
    if (crackSig > 0.04f) {
        // 2-step view-tangent parallax cavity sampling into fractured stone/wood scar:
        const float3 pC1 = wpAbs + vTan * 1.10f;
        const float3 pC2 = wpAbs + vTan * 2.40f;
        const float4 k1 = rawNoise3Grad(pC1 * 0.55f + float3(19.3f, 7.1f, 3.7f));
        const float4 k2 = rawNoise3Grad(pC2 * 1.25f + float3(2.9f, 13.3f, 23.1f));
        const float cavDepth = saturate(1.0f - (k1.x * 0.60f + k2.x * 0.40f));
        // Sharp V-groove bevel normal oriented along local screen luminance gradient + micro-fracture shards:
        const float3 scarSlope = normalize(dpX * lumaDx + dpY * lumaDy + nIn * 1.0e-4f);
        const float3 vGroove   = (scarSlope - nIn * dot(scarSlope, nIn)) * (0.48f * crackSig);
        const float3 chipMicro = (k1.yzw * 0.55f + k2.yzw * 0.45f);
        o.normal = normalize(o.normal + vGroove + (chipMicro - nIn * dot(chipMicro, nIn)) * (0.24f * crackSig));
        // Deepened parallax cavity AO inside fissure + tiny chipped-edge bevel highlight on raised lip:
        const float cavAo = lerp(1.0f, lerp(0.70f, 1.06f, 1.0f - cavDepth), crackSig * 0.78f);
        o.albedo *= cavAo;
        o.alpha   = clamp(o.alpha + (cavDepth - 0.35f) * 0.16f * crackSig, 0.08f, 0.96f);
    }

    return o;
}

#endif // MAX_LIGHTS
#endif
