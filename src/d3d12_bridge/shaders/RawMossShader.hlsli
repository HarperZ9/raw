// RawMossShader.hlsli: Wave 88 Project Total Rebirth Part 1.
// Live Multi-Layer 3D Fuzzy Velvet Moss & Alpine Lichen Shader (SM 6.6 / 6.9 / 6.10).
// Renders ecological, world-space-locked 3-shell micro-frond velvet moss cushions with:
//   - Ecological placement: upward/sloped rocks, boulders, road edges, Nordic masonry, fallen timber
//   - Moisture wicking boost near rivers/canals (`waterPlaneZ_m`) and north-facing aspect bias
//   - 3-shell parallax velvet cushion micro-frond depth + damp contact-rim darkening
//   - Charlie-style grazing-angle microfiber sheen + warm sunlit wrap diffuse
//   - Strict exclusion of ALL actors/creatures, sky, water, interiors, and snow-buried zones.
// Clean-room MIT.

#ifndef RAW_MOSS_SHADER_HLSLI
#define RAW_MOSS_SHADER_HLSLI

#include "RawTotalMaterialOverhaul.hlsli"

float rawMossEvalFrondShell(float2 worldXY, float shellFrac)
{
    // Multi-scale velvet micro-tuft clusters (world-locked, irrational octave rotation)
    const float tuftA = rawRebirthValue2D(worldXY * 14.5f + float2(shellFrac * 1.7f, shellFrac * 2.3f));
    const float tuftB = rawRebirthValue2D(worldXY * 27.0f + float2(9.3f, 4.1f));
    const float frond = tuftA * 0.62f + tuftB * 0.38f;
    // Upper shells are sparser fuzzy tips; base shell is dense velvety cushion
    const float thresh = lerp(0.28f, 0.62f, shellFrac);
    return smoothstep(thresh, thresh + 0.28f, frond);
}

// Evaluates live 3D fuzzy velvet moss + alpine lichen and blends over HDR surface `col`.
// Returns true if moss was applied (for compositor modified flag).
bool rawRebirthApply3DMoss(
    inout float3 col, float3 alb, float3 N, float3 worldM, float distM,
    float3 rayDir, float3 sunDir, float3 sunCol, float sunIntensity,
    float waterPlaneZ_m, float snowCoverage01, float rain01,
    float interiorMask, uint base, uint classId, bool isInt, bool isActor)
{
    if (isInt || isActor || distM > 52.0f || interiorMask <= 0.05f) {
        return false;
    }
    // Strictly eligible world geometry: landscape terrain/roads/banks (base==4) or upward rock/boulder crowns (N.z > 0.66).
    // Never apply moss to vertical/sloped creature/horse bodies rendered under generic Lit (base==1)!
    const bool isLandscape = (base == 4u);
    const bool isUpRock    = (N.z > 0.66f) && (classId == kRawClass14_RockCliffBoulder || classId == kRawClass14_CobbleRoadPath || classId == kRawClass14_MossLichen);
    if (!isLandscape && !isUpRock) {
        return false;
    }
    const bool eligible = (classId == kRawClass14_RockCliffBoulder)
                       || (classId == kRawClass14_CobbleRoadPath)
                       || (classId == kRawClass14_MossLichen)
                       || (classId == kRawClass14_TerrainGround);
    if (!eligible) {
        return false;
    }

    // Snow suppression: heavy snowpack buries velvet moss cleanly
    const float snowSuppress = 1.0f - smoothstep(0.18f, 0.55f, snowCoverage01);
    if (snowSuppress <= 0.02f) {
        return false;
    }

    // Ecological upward slope + north-aspect + riparian moisture wicking
    const float distFade = 1.0f - smoothstep(22.0f, 52.0f, distM);
    const float upSlope  = smoothstep(0.26f, 0.76f, N.z);
    const float northBias = 0.72f + 0.28f * saturate(-N.y * 0.5f + 0.5f);
    const float heightAboveWaterM = worldM.z - waterPlaneZ_m;
    const float riparianBoost = (heightAboveWaterM > -0.15f && heightAboveWaterM < 6.5f)
                              ? (1.0f - smoothstep(0.4f, 6.5f, max(heightAboveWaterM, 0.0f))) : 0.0f;

    // Clumped organic world-space hex-tiled moss colony growth field (never flat green paint!)
    const float colonyMacro = rawRebirthHexStochasticNoise(worldM.xy * 0.34f + worldM.z * 0.12f);
    const float colonyMed   = rawRebirthHexStochasticNoise(worldM.xy * 1.15f + float2(7.3f, 13.1f));
    const float colonyField = colonyMacro * 0.64f + colonyMed * 0.36f
                            + riparianBoost * 0.20f + rain01 * 0.08f
                            + ((classId == kRawClass14_MossLichen) ? 0.18f : 0.0f)
                            - ((classId == kRawClass14_CobbleRoadPath) ? 0.08f : 0.0f);

    // Non-periodic organic crevice preference on roads & masonry (zero square grid!)
    float creviceBias = 1.0f;
    if (classId == kRawClass14_CobbleRoadPath) {
        const float orgPatch = rawRebirthHexStochasticNoise(worldM.xy * 1.65f + float2(4.2f, 9.1f));
        creviceBias = smoothstep(0.32f, 0.72f, orgPatch);
    }

    // Damp contact-rim darkening right around moss patch borders (silhouette-safe!)
    const float dampHalo = smoothstep(0.40f, 0.53f, colonyField) * (1.0f - smoothstep(0.53f, 0.64f, colonyField))
                         * upSlope * snowSuppress * distFade * interiorMask;
    if (dampHalo > 0.01f) {
        col *= lerp(1.0f, 0.82f, dampHalo * 0.55f);
    }

    const float patchMask = smoothstep(0.51f, 0.72f, colonyField) * upSlope * northBias * creviceBias * snowSuppress * distFade * interiorMask;
    if (patchMask <= 0.015f) {
        return dampHalo > 0.01f;
    }

    // 3-shell world-space parallax velvet moss cushion depth (0.012m -> 0.036m)
    const float3 V = -rayDir;
    const float3 L = normalize(sunDir);
    const float invRayZ = 1.0f / max(abs(rayDir.z), 0.38f);
    const float ndlWrap = saturate((dot(N, L) + 0.45f) / 1.45f);
    const float ndv = saturate(dot(N, V));
    // Grazing-angle microfiber velvet sheen (Charlie-inspired rim glow on fuzzy moss tips)
    const float velvetSheen = pow(1.0f - ndv, 3.1f) * (0.35f + 0.65f * saturate(dot(N, L) * 0.5f + 0.5f));
    const float backLight   = pow(saturate(dot(rayDir, L)), 2.4f) * max(sunIntensity, 0.20f);

    float3 mossAcc = float3(0.0f, 0.0f, 0.0f);
    float  mossAlpha = 0.0f;
    const float hueShift = rawRebirthValue2D(worldM.xy * 0.85f + 5.1f);
    // Natural boreal moss palette: deep damp forest-olive root -> lush velvety emerald-chartreuse tips
    const float3 baseMoss = lerp(float3(0.055f, 0.115f, 0.038f), float3(0.080f, 0.140f, 0.045f), hueShift);
    const float3 tipMoss  = lerp(float3(0.145f, 0.265f, 0.068f), float3(0.245f, 0.335f, 0.095f), hueShift);

    [unroll]
    for (int s = 2; s >= 0; --s) {
        const float hFrac = (float(s) + 0.5f) * (1.0f / 3.0f);
        const float hM    = lerp(0.012f, 0.038f, hFrac);
        const float2 pShell = worldM.xy - rayDir.xy * (hM * invRayZ);
        const float frond = rawMossEvalFrondShell(pShell, hFrac);
        const float a = frond * lerp(0.68f, 0.44f, hFrac) * (1.0f - mossAlpha);

        float3 frondCol = lerp(baseMoss, tipMoss, hFrac) * lerp(0.62f, 1.18f, hFrac) * (0.38f + 0.62f * ndlWrap);
        frondCol *= max(min(sunCol, float3(1.35f, 1.35f, 1.35f)), float3(0.30f, 0.35f, 0.28f));
        // Golden-emerald fuzzy microfiber grazing rim & soft sunlit translucency
        frondCol += float3(0.18f, 0.28f, 0.07f) * (velvetSheen * 0.55f + backLight * 0.32f) * hFrac;

        mossAcc += frondCol * a;
        mossAlpha += a;
    }

    const float blendW = saturate(mossAlpha * patchMask * 0.82f);
    // Keep underlying ambient illumination consistent so mossy rocks in deep shade never glow artificially
    const float baseLum = dot(col, float3(0.2126f, 0.7152f, 0.0722f));
    const float shadeMatch = clamp(0.42f + baseLum * 1.45f, 0.22f, 1.35f);
    const float3 finalMoss = (mossAcc / max(mossAlpha, 1.0e-3f)) * shadeMatch;
    col = lerp(col, finalMoss, blendW);
    return true;
}

#endif // RAW_MOSS_SHADER_HLSLI
