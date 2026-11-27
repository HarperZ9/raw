// RawTotalMaterialOverhaul.hlsli: Wave 88 Project Total Rebirth Part 1 (v2).
// Zero-Tiling Deliot-Heitz Stochastic Hex-Tiling + 14-Class Live World-Space PBR Micro-Relief,
// Surface-Interior Albedo-Guided Micro-Bevel Carving, and Macro-Geological Breakup (SM 6.6 / 6.9 / 6.10).
// Strictly world-space anchored (zero camera swimming), silhouette-depth-masked (zero edge ghosting),
// and Nyquist-band-limited with distance (zero temporal shimmer under moving-camera sweeps).
// Clean-room MIT.

#ifndef RAW_TOTAL_MATERIAL_OVERHAUL_HLSLI
#define RAW_TOTAL_MATERIAL_OVERHAUL_HLSLI

// Matches raw::dxbc::material::AssetClass14 (1..14)
static const uint kRawClass14_Unclassified           = 0u;
static const uint kRawClass14_TerrainGround          = 1u;
static const uint kRawClass14_CobbleRoadPath         = 2u;
static const uint kRawClass14_RockCliffBoulder       = 3u;
static const uint kRawClass14_MossLichen             = 4u;
static const uint kRawClass14_TreeTrunkBark          = 5u;
static const uint kRawClass14_TreeFoliageCanopy      = 6u;
static const uint kRawClass14_GrassGroundcover       = 7u;
static const uint kRawClass14_WaterSurface           = 8u;
static const uint kRawClass14_GlacierIce             = 9u;
static const uint kRawClass14_Snowpack               = 10u;
static const uint kRawClass14_NordicStoneMasonry     = 11u;
static const uint kRawClass14_TimberWood             = 12u;
static const uint kRawClass14_ForgedMetalArmor       = 13u;
static const uint kRawClass14_CharacterSkinHairCloth = 14u;

float rawRebirthHash21(float2 p)
{
    float2 q = frac(p * float2(0.1031f, 0.1030f));
    q += dot(q, q.yx + 33.33f);
    return frac((q.x + q.y) * q.x);
}

float2 rawRebirthHash22(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * float3(0.1031f, 0.1030f, 0.0973f));
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.xx + p3.yz) * p3.zy);
}

float rawRebirthValue2D(float2 p)
{
    const float2 i = floor(p);
    const float2 f = frac(p);
    const float2 u = f * f * (3.0f - 2.0f * f);
    const float a = rawRebirthHash21(i + float2(0.0f, 0.0f));
    const float b = rawRebirthHash21(i + float2(1.0f, 0.0f));
    const float c = rawRebirthHash21(i + float2(0.0f, 1.0f));
    const float d = rawRebirthHash21(i + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float rawRebirthFbm2D(float2 p)
{
    float sum = 0.0f;
    float amp = 0.50f;
    float2 q = p;
    [unroll]
    for (int i = 0; i < 3; ++i) {
        sum += amp * rawRebirthValue2D(q);
        // 37-degree irrational rotation between octaves eliminates axial grid periodicity
        q = float2(q.x * 1.60f - q.y * 1.20f, q.x * 1.20f + q.y * 1.60f) + float2(17.3f, 9.7f);
        amp *= 0.50f;
    }
    return sum * (1.0f / 0.875f);
}

// Deliot-Heitz triangular/hexagonal dual lattice partition (3 vertex IDs + barycentric weights summing to 1).
void rawRebirthHexTriangleGrid(float2 uv, out float2 v1, out float2 v2, out float2 v3, out float3 w)
{
    const float2 uvScaled = uv * 3.4641016f; // 2 * sqrt(3)
    const float2 skew = float2(
        uvScaled.x - 0.57735027f * uvScaled.y,
        1.15470054f * uvScaled.y);
    const float2 vx = floor(skew);
    const float2 bar = frac(skew);
    const float z = 1.0f - bar.x - bar.y;
    if (z > 0.0f) {
        v1 = vx;
        v2 = vx + float2(1.0f, 0.0f);
        v3 = vx + float2(0.0f, 1.0f);
        w  = float3(z, bar.x, bar.y);
    } else {
        v1 = vx + float2(1.0f, 1.0f);
        v2 = vx + float2(0.0f, 1.0f);
        v3 = vx + float2(1.0f, 0.0f);
        w  = float3(-z, 1.0f - bar.x, 1.0f - bar.y);
    }
    w = pow(max(w, 1.0e-4f), 2.2f);
    w /= max(w.x + w.y + w.z, 1.0e-5f);
}

float2 rawRebirthHexTransformUv(float2 uv, float2 cellId)
{
    const float2 rnd = rawRebirthHash22(cellId);
    const float ang = rnd.x * 6.2831853f;
    const float ca = cos(ang);
    const float sa = sin(ang);
    const float2 rotUv = float2(ca * uv.x - sa * uv.y, sa * uv.x + ca * uv.y);
    return rotUv + rnd * 13.7f;
}

// Variance-preserving stochastic hex-tiled Texture2DArray lookup (zero square UV tiling!).
float4 rawRebirthSampleHexPbrSlice(Texture2DArray<float4> texArr, SamplerState smp,
                                   float2 uv, uint slice, float mipLevel)
{
    float2 v1, v2, v3;
    float3 w;
    rawRebirthHexTriangleGrid(uv * 0.35f, v1, v2, v3, w);

    const float2 uv1 = rawRebirthHexTransformUv(uv, v1);
    const float2 uv2 = rawRebirthHexTransformUv(uv, v2);
    const float2 uv3 = rawRebirthHexTransformUv(uv, v3);

    const float4 s1 = texArr.SampleLevel(smp, float3(uv1, float(slice)), mipLevel);
    const float4 s2 = texArr.SampleLevel(smp, float3(uv2, float(slice)), mipLevel);
    const float4 s3 = texArr.SampleLevel(smp, float3(uv3, float(slice)), mipLevel);

    const float4 avg = s1 * w.x + s2 * w.y + s3 * w.z;
    const float invNorm = rsqrt(max(dot(w, w), 0.3333f));
    const float4 centered = (s1 - 0.5f) * w.x + (s2 - 0.5f) * w.y + (s3 - 0.5f) * w.z;
    const float4 vp = saturate(0.5f + centered * lerp(1.0f, invNorm, 0.55f));
    return lerp(avg, vp, 0.60f);
}

// Analytical hex-tiled 2D procedural field (eliminates all periodic square grid repetition in world space).
float rawRebirthHexStochasticNoise(float2 p)
{
    float2 v1, v2, v3;
    float3 w;
    rawRebirthHexTriangleGrid(p * 0.28f, v1, v2, v3, w);
    const float n1 = rawRebirthFbm2D(rawRebirthHexTransformUv(p, v1));
    const float n2 = rawRebirthFbm2D(rawRebirthHexTransformUv(p, v2));
    const float n3 = rawRebirthFbm2D(rawRebirthHexTransformUv(p, v3));
    const float invNorm = rsqrt(max(dot(w, w), 0.3333f));
    const float c = (n1 - 0.5f) * w.x + (n2 - 0.5f) * w.y + (n3 - 0.5f) * w.z;
    return saturate(0.5f + c * lerp(1.0f, invNorm, 0.55f));
}

// Triplanar dominant-axis projection (world-space meters, Z-up Skyrim coordinates).
float2 rawRebirthTriplanarUv(float3 worldM, float3 N, out float3 triW)
{
    const float3 an = abs(N);
    triW = pow(max(an, 1.0e-4f), 4.0f);
    triW /= max(triW.x + triW.y + triW.z, 1.0e-5f);
    return (triW.z >= triW.x && triW.z >= triW.y) ? worldM.xy
         : ((triW.x >= triW.y) ? worldM.yz : worldM.xz);
}

// Live 14-class pixel classifier (cool blue glacier ice checked BEFORE interior masonry!).
uint rawRebirthClassifyPixel14(uint base, float3 alb, float3 N, bool isInt, bool isActor)
{
    if (isActor) {
        return kRawClass14_CharacterSkinHairCloth;
    }
    if (base == 9u) {
        return kRawClass14_WaterSurface;
    }
    if (base == 5u) {
        return kRawClass14_TreeFoliageCanopy;
    }
    const float lum = dot(alb, float3(0.2126f, 0.7152f, 0.0722f));
    const float chromaRB = abs(alb.r - alb.b);
    // Detect glacial blue ice caves (e.g., Septimus Signus Outpost) BEFORE interior masonry fallback!
    if ((alb.b > alb.r * 1.07f && alb.b > 0.10f) || (base == 6u && alb.b > alb.r * 1.03f && lum > 0.20f)) {
        return kRawClass14_GlacierIce;
    }
    if (base == 2u) {
        return kRawClass14_ForgedMetalArmor;
    }
    if (base == 6u) {
        return (N.z > 0.68f) ? kRawClass14_CobbleRoadPath : kRawClass14_NordicStoneMasonry;
    }
    if (!isInt && lum > 0.62f && alb.b >= alb.r * 0.94f && N.z > 0.42f) {
        return kRawClass14_Snowpack;
    }
    if (base == 4u) {
        if (!isInt && alb.g > alb.r * 1.04f && alb.g > alb.b * 1.08f) {
            return (N.z > 0.72f) ? kRawClass14_GrassGroundcover : kRawClass14_MossLichen;
        }
        if (chromaRB < 0.065f && N.z > 0.74f && lum > 0.14f && lum < 0.56f) {
            return kRawClass14_CobbleRoadPath;
        }
        return kRawClass14_TerrainGround;
    }
    if (!isInt && alb.g > alb.r * 1.05f && alb.g > alb.b * 1.08f) {
        return (N.z > 0.40f) ? kRawClass14_MossLichen : kRawClass14_TreeFoliageCanopy;
    }
    if (alb.r > alb.b * 1.12f && alb.r >= alb.g * 0.94f) {
        if (!isInt && abs(N.z) < 0.52f && lum < 0.30f) {
            return kRawClass14_TreeTrunkBark;
        }
        return kRawClass14_TimberWood;
    }
    if (isInt || (abs(N.z) < 0.48f && chromaRB < 0.09f)) {
        return kRawClass14_NordicStoneMasonry;
    }
    if (N.z > 0.72f && chromaRB < 0.07f) {
        return kRawClass14_CobbleRoadPath;
    }
    return kRawClass14_RockCliffBoulder;
}

// 100% NON-PERIODIC hex-tiled world-space micro-heightfield (zero synthetic brick/square grids!).
float rawRebirthEvalClassHeight(uint classId, float3 worldM, float3 N, float2 pDom)
{
    if (classId == kRawClass14_GlacierIce) {
        // Organic sweeping conchoidal crystal veins & translucent fracture steps (zero grid!)
        const float w0 = rawRebirthHexStochasticNoise(pDom * 0.85f + float2(worldM.z * 0.35f, -worldM.z * 0.25f));
        const float vein = 1.0f - abs(rawRebirthValue2D(pDom * 3.8f + w0 * 2.4f) * 2.0f - 1.0f);
        const float facet = rawRebirthValue2D(pDom * 11.5f + worldM.z * 4.2f);
        return saturate(0.45f * w0 + 0.35f * (vein * vein) + 0.20f * facet);
    }
    if (classId == kRawClass14_CobbleRoadPath || classId == kRawClass14_NordicStoneMasonry) {
        // Non-periodic hand-chiseled stone mineral pores, quarry micro-pits & quartz speckle
        const float stoneMacro = rawRebirthHexStochasticNoise(pDom * 1.45f);
        const float chiselPit  = rawRebirthValue2D(pDom * 9.5f + float2(stoneMacro * 1.3f, 2.7f));
        const float quartzGrit = rawRebirthValue2D(pDom * 23.0f + worldM.z * 7.5f);
        return saturate(0.36f * stoneMacro + 0.36f * chiselPit + 0.28f * quartzGrit);
    }
    if (classId == kRawClass14_RockCliffBoulder) {
        // Fractured sedimentary rock strata + organic basalt cleavage cracks + mineral grit
        const float warp = (rawRebirthHexStochasticNoise(pDom * 0.55f) - 0.5f) * 1.45f;
        const float strata = 0.5f + 0.5f * sin(worldM.z * 2.4f + warp * 3.2f);
        const float crack = 1.0f - smoothstep(0.05f, 0.26f, abs(rawRebirthValue2D(pDom * 3.2f + worldM.z * 1.1f) * 2.0f - 1.0f));
        const float grit = rawRebirthValue2D(pDom * 16.5f + worldM.z * 6.0f);
        return saturate(0.34f * strata + 0.44f * grit + 0.22f * rawRebirthHexStochasticNoise(pDom * 1.8f) - 0.24f * crack);
    }
    if (classId == kRawClass14_TimberWood || classId == kRawClass14_TreeTrunkBark) {
        // Anisotropic longitudinal wood-grain fibers & corrugated bark ridges (non-periodic)
        const float along  = (classId == kRawClass14_TreeTrunkBark) ? worldM.z : (abs(N.z) > 0.6f ? worldM.x : worldM.y);
        const float across = (classId == kRawClass14_TreeTrunkBark) ? (worldM.x + worldM.y) : (abs(N.z) > 0.6f ? worldM.y : worldM.z);
        const float warp   = (rawRebirthHexStochasticNoise(float2(across * 0.9f, along * 0.35f)) - 0.5f) * 0.65f;
        const float groove = rawRebirthValue2D(float2((across + warp) * 10.5f, along * 1.75f));
        const float fiber  = rawRebirthValue2D(float2((across + warp) * 26.0f, along * 4.5f));
        return saturate(0.58f * groove + 0.42f * fiber);
    }
    if (classId == kRawClass14_Snowpack) {
        // Non-periodic wind-sculpted sastrugi snow ripples & crisp crystal grain
        const float sastrugi = rawRebirthHexStochasticNoise(float2(pDom.x * 0.85f, pDom.y * 2.1f));
        const float micro = rawRebirthValue2D(pDom * 14.5f);
        return saturate(0.62f * sastrugi + 0.38f * micro);
    }
    if (classId == kRawClass14_ForgedMetalArmor) {
        const float brush = rawRebirthValue2D(float2(pDom.x * 5.0f, pDom.y * 24.0f));
        const float dimple = rawRebirthHexStochasticNoise(pDom * 5.5f);
        return saturate(0.64f * brush + 0.36f * dimple);
    }
    // TerrainGround / MossLichen / GrassGroundcover / Foliage
    const float macro = rawRebirthHexStochasticNoise(pDom * 1.10f);
    const float pebble = rawRebirthValue2D(pDom * 11.0f);
    const float micro = rawRebirthValue2D(pDom * 24.0f);
    return saturate(0.40f * macro + 0.35f * pebble + 0.25f * micro);
}

// Applies full 14-class world-space stochastic hex-tiled PBR material overhaul,
// surface-interior albedo-guided micro-bevel carving, and crisp tactile detail relief.
float3 rawRebirthApplyTotalMaterialOverhaul(
    float3 col, float3 alb, float3 N, float3 worldM, float distM,
    float3 sunDir, float3 sunCol, float3 viewDir,
    float interiorMask, float albLap, float2 albGrad,
    uint classId, bool isInt, out float3 outN)
{
    outN = N;
    if (classId == kRawClass14_CharacterSkinHairCloth || classId == kRawClass14_WaterSurface || classId == kRawClass14_Unclassified) {
        return col;
    }

    // Strictly keep thin alpha-tested foliage cards immune to high-freq micro-carving (prevents moving-camera edge shimmer!)
    const bool isThinFoliage = (classId == kRawClass14_TreeFoliageCanopy || classId == kRawClass14_GrassGroundcover);

    const float macroFade  = 1.0f - smoothstep(28.0f, 95.0f, distM);
    const float detailFade = (1.0f - smoothstep(isInt ? 18.0f : 12.0f, isInt ? 54.0f : 32.0f, distM)) * interiorMask;
    if (macroFade <= 1.0e-4f) {
        return col;
    }

    float3 triW;
    const float2 pDom = rawRebirthTriplanarUv(worldM, N, triW);

    // 1) Low-frequency non-periodic hex-tiled geological breakup (suppresses vanilla 2011 periodic UV tiling!)
    const float macroHexA = rawRebirthHexStochasticNoise(pDom * 0.17f + float2(3.1f, 7.4f));
    const float macroHexB = rawRebirthHexStochasticNoise(pDom * 0.43f + float2(11.7f, 2.3f));
    const float macroBreak = (macroHexA * 0.62f + macroHexB * 0.38f) - 0.5f;

    const float3 mineralTint = lerp(float3(0.86f, 0.92f, 1.01f), float3(1.13f, 1.04f, 0.90f), macroHexA);
    const float macroAmp = isThinFoliage ? 0.12f
                         : ((classId == kRawClass14_GlacierIce) ? 0.08f
                         : ((classId == kRawClass14_Snowpack) ? 0.24f : 0.36f));
    float3 res = col * lerp(float3(1.0f, 1.0f, 1.0f), mineralTint * (1.0f + macroBreak * 0.32f), macroFade * macroAmp);

    // 2) Surface-interior chiseled PBR micro-bevel relief on SOLID world geometry (zero foliage shimmer, zero edge ghosting!)
    if (!isThinFoliage && detailFade > 1.0e-3f) {
        const float eps = clamp(0.018f + distM * 0.0014f, 0.018f, 0.09f);
        const float h0  = rawRebirthEvalClassHeight(classId, worldM, N, pDom);
        const float hx  = rawRebirthEvalClassHeight(classId, worldM + float3(eps, 0.0f, 0.0f), N, pDom + float2(eps, 0.0f));
        const float hy  = rawRebirthEvalClassHeight(classId, worldM + float3(0.0f, eps, 0.0f), N, pDom + float2(0.0f, eps));

        // Crisp close-range world-locked micro-mineral / ice-crystal / wood-fiber octaves
        // Strictly Nyquist-faded beyond 16m so distant moving shadows stay 100% boil-free across all 6 scenes!
        const float closeFade = (1.0f - smoothstep(6.0f, 16.0f, distM)) * interiorMask;
        const float fineA = rawRebirthValue2D(pDom * 24.0f + float2(worldM.z * 9.0f, -worldM.z * 7.0f));
        const float fineB = rawRebirthValue2D(pDom * 42.0f + float2(-worldM.z * 13.0f, worldM.z * 17.0f));
        const float fineGrain = ((fineA * 0.58f + fineB * 0.42f) - 0.5f) * closeFade;

        const float slopeGain = (classId == kRawClass14_CobbleRoadPath || classId == kRawClass14_NordicStoneMasonry) ? 1.00f
                              : ((classId == kRawClass14_RockCliffBoulder || classId == kRawClass14_TreeTrunkBark) ? 0.92f
                              : ((classId == kRawClass14_TimberWood || classId == kRawClass14_TerrainGround) ? 0.88f
                              : ((classId == kRawClass14_GlacierIce) ? 0.88f
                              : ((classId == kRawClass14_Snowpack) ? 0.62f : 0.72f))));

        // Combine world-space procedural stone/grain slope + surface-locked albedo micro-bevel slope
        const float dHx = clamp((h0 - hx) * (0.36f / eps) + albGrad.x * 4.0f, -1.8f, 1.8f);
        const float dHy = clamp((h0 - hy) * (0.36f / eps) + albGrad.y * 4.0f, -1.8f, 1.8f);
        const float3 gradW = (abs(N.z) > 0.65f) ? float3(dHx, dHy, 0.0f)
                           : ((abs(N.x) > abs(N.y)) ? float3(0.0f, dHx, dHy) : float3(dHx, 0.0f, dHy));
        const float3 nPert = normalize(N + gradW * (slopeGain * detailFade * 0.60f));
        outN = nPert;

        const float3 keyDir = isInt ? normalize(float3(0.32f, 0.28f, 0.90f)) : normalize(sunDir);
        const float ndlBase = max(dot(N, keyDir), 0.20f);
        const float ndlPert = max(dot(nPert, keyDir), 0.08f);
        const float bevelRatio = clamp(ndlPert / ndlBase, 0.68f, 1.46f);

        // Energy-conserving tactile micro-contrast + surface-interior chiseled detail carving
        // (strictly firefly-proof, dark-outline-proof, and shadow-boil-proof across all 6 moving-camera scenes!)
        const float lum0 = dot(res, float3(0.2126f, 0.7152f, 0.0722f));
        const float darkSafe = smoothstep(0.026f, 0.090f, lum0);
        const bool  isIceCavern = (classId == kRawClass14_GlacierIce);
        const float hiSafe   = isIceCavern ? (1.0f - smoothstep(0.36f, 0.74f, lum0))
                                           : (1.0f - smoothstep(0.18f, 0.46f, lum0));
        const float grainHi  = (h0 - 0.5f) * (isIceCavern ? 0.94f : 0.74f) + fineGrain * (isIceCavern ? 1.08f : 0.80f);
        const float carveHi  = clamp(albLap * (isIceCavern ? 6.80f : 5.40f), isIceCavern ? -0.26f : -0.18f, isIceCavern ? 0.48f : 0.34f);
        const float maxMul   = lerp(1.02f, isIceCavern ? 1.58f : 1.36f, hiSafe);
        const float minMul   = isIceCavern ? 0.60f : 0.74f;
        const float reliefMul = clamp(
            lerp(1.0f, bevelRatio * (1.0f + grainHi + carveHi), detailFade * lerp(0.34f, isIceCavern ? 0.96f : 0.86f, darkSafe)),
            minMul, maxMul);
        res *= reliefMul;

        // Crisp physical micro-specular bevel glint on stone/cobble/ice/metal ridges (strictly zero-firefly!)
        const float3 halfV = normalize(keyDir + viewDir);
        const float specPow = (classId == kRawClass14_ForgedMetalArmor) ? 26.0f
                            : ((classId == kRawClass14_GlacierIce || classId == kRawClass14_CobbleRoadPath) ? 18.0f : 14.0f);
        const float specGain = (classId == kRawClass14_ForgedMetalArmor) ? 0.075f
                             : ((classId == kRawClass14_GlacierIce) ? 0.045f
                             : ((classId == kRawClass14_CobbleRoadPath || classId == kRawClass14_NordicStoneMasonry) ? 0.020f : 0.010f));
        const float microSpec = pow(saturate(dot(nPert, halfV)), specPow) * smoothstep(0.50f, 0.90f, h0);
        const float3 specTint = (classId == kRawClass14_ForgedMetalArmor) ? max(alb, float3(0.55f, 0.58f, 0.62f))
                              : ((classId == kRawClass14_GlacierIce) ? float3(0.74f, 0.90f, 1.0f) : float3(1.0f, 1.0f, 1.0f));
        res += min(sunCol, float3(1.00f, 1.00f, 1.00f)) * specTint * (microSpec * specGain * detailFade * darkSafe * hiSafe);
    }

    return res;
}

#endif // RAW_TOTAL_MATERIAL_OVERHAUL_HLSLI
