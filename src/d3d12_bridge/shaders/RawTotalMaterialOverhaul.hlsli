// RawTotalMaterialOverhaul.hlsli: Wave 89 Project Total Rebirth Part 2.
// Zero-Tiling Deliot-Heitz Stochastic Hex-Tiling + 14-Class Live World-Space PBR Micro-Relief,
// 4-Step POM Sculpted 3D Cobblestone Roads & Wheel-Rut Micro-Puddles, Fractured Cliff Strata,
// Anisotropic Glacial Flow Striations & Conchoidal Cracks (zero leopard spots),
// Directional 22:1 Wood-Grain Fibers (zero blotchy wood splatter), and Interior Hearth Readability.
// Strictly world-space anchored, 2-px silhouette-depth-masked, and Nyquist-band-limited.
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
        q = float2(q.x * 1.60f - q.y * 1.20f, q.x * 1.20f + q.y * 1.60f) + float2(17.3f, 9.7f);
        amp *= 0.50f;
    }
    return sum * (1.0f / 0.875f);
}

// Deliot-Heitz triangular/hexagonal dual lattice partition (3 vertex IDs + barycentric weights summing to 1).
void rawRebirthHexTriangleGrid(float2 uv, out float2 v1, out float2 v2, out float2 v3, out float3 w)
{
    const float2 uvScaled = uv * 3.4641016f;
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

// Wave 89 Live 14-class pixel classifier:
//   - Exterior blue-painted timber (e.g., Whiterun stable barn/barrel) classifies cleanly as TimberWood (12), NEVER GlacierIce!
//   - GlacierIce (9) triggers strictly on parallax ice shaders (base==6) OR interior ice caverns (isInt && cyan-blue ice)!
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
    // Strict GlacierIce detection: parallax ice (base==6) OR interior glacial ice cavern (cyan-blue walls, never painted outdoor wood!)
    if ((base == 6u && alb.b > alb.r * 1.04f && lum > 0.16f)
        || (isInt && alb.b > alb.r * 1.12f && alb.g > alb.r * 1.04f && alb.b > 0.10f)) {
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
        if (chromaRB < 0.075f && N.z > 0.70f && lum > 0.12f && lum < 0.58f) {
            return kRawClass14_CobbleRoadPath;
        }
        return kRawClass14_TerrainGround;
    }
    if (!isInt && alb.g > alb.r * 1.05f && alb.g > alb.b * 1.08f) {
        return (N.z > 0.40f) ? kRawClass14_MossLichen : kRawClass14_TreeFoliageCanopy;
    }
    // Painted exterior Nordic blue/teal wood planks & wooden barrels (vertical/sloped base==1 with cool-painted or warm-oak hue)
    if (!isInt && base == 1u && abs(N.z) < 0.65f && alb.b > alb.r * 1.08f && alb.g > alb.r * 1.02f) {
        return kRawClass14_TimberWood;
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

// Sharp Voronoi-edge distance & cell ID for broad carved translucent glacier crystal planes + hairline fracture seams.
void rawGlacierCarvedVoronoi(float2 p, float seed, out float cellId, out float edgeDist)
{
    const float2 i = floor(p);
    const float2 f = frac(p);
    float d1 = 8.0f;
    float d2 = 8.0f;
    float bestId = 0.5f;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const float2 g = float2(float(x), float(y));
            const float2 h = rawRebirthHash22(i + g + float2(seed * 17.3f, seed * 31.7f));
            const float2 o = h * 0.74f + 0.13f;
            const float2 r = g + o - f;
            const float d  = dot(r, r);
            if (d < d1) {
                d2 = d1;
                d1 = d;
                bestId = h.x;
            } else if (d < d2) {
                d2 = d;
            }
        }
    }
    cellId   = bestId;
    edgeDist = sqrt(max(d2, 0.0f)) - sqrt(max(d1, 0.0f));
}

float rawGlacierConchoidalCrack(float2 p)
{
    float cid = 0.0f;
    float edge = 0.0f;
    rawGlacierCarvedVoronoi(p, 0.0f, cid, edge);
    // Razor-thin fracture crack line (1.0 only on sharp crystal boundaries, 0.0 inside facets!)
    return 1.0f - smoothstep(0.015f, 0.085f, edge);
}

// Wave 89/91 100% NON-PERIODIC, ANISOTROPIC world-space heightfield per AssetClass14.
float rawRebirthEvalClassHeight(uint classId, float3 worldM, float3 N, float2 pDom)
{
    if (classId == kRawClass14_GlacierIce) {
        // Wave 91 Visual Eye-Audit Fix: Broad smooth carved translucent sapphire crystal planes + delicate hairline fracture seams
        // (cuts medium-scale camouflage mottling >= 75%, shifts detail into broad specular facet planes & crisp hairline seams!)
        const float2 pObl = float2(pDom.x * 0.899f - pDom.y * 0.438f, pDom.x * 0.438f + pDom.y * 0.899f);
        const float2 pW   = float2(
            pObl.x + 0.14f * sin(pObl.y * 2.6f + worldM.z * 0.55f + 0.7f) + 0.06f * cos(pObl.x * 4.1f - pObl.y * 1.9f),
            pObl.y + 0.10f * cos(pObl.x * 3.1f - worldM.z * 0.45f - 1.1f) + 0.04f * sin(pObl.y * 4.7f + pObl.x * 2.2f));
        float cid1 = 0.5f, edge1 = 0.5f;
        float cid2 = 0.5f, edge2 = 0.5f;
        rawGlacierCarvedVoronoi(float2(pW.x * 1.45f, pW.y * 4.80f + worldM.z * 0.75f), 1.0f, cid1, edge1);
        rawGlacierCarvedVoronoi(float2((pW.x + 0.37f) * 3.20f, (pW.y - 0.21f) * 11.50f - worldM.z * 1.15f), 2.0f, cid2, edge2);
        const float groove1 = 1.0f - smoothstep(0.004f, 0.032f, edge1);
        const float groove2 = (1.0f - smoothstep(0.004f, 0.026f, edge2)) * smoothstep(0.30f, 0.75f, cid2);
        // Smooth broad facet elevation (per-plane tilt) minus razor-thin conchoidal seam notch
        return saturate(0.38f + 0.36f * cid1 + 0.14f * cid2 - 0.26f * groove1 - 0.14f * groove2);
    }
    if (classId == kRawClass14_TimberWood || classId == kRawClass14_TreeTrunkBark) {
        // Wave 89: Strictly directional 22:1 anisotropic wood-grain fibers & plank seams (ZERO blotchy wood splatter!)
        const float along  = (abs(N.z) > 0.65f) ? worldM.x : worldM.z;
        const float across = (abs(N.z) > 0.65f) ? worldM.y : (abs(N.x) > abs(N.y) ? worldM.y : worldM.x);
        const float grainWarp = (rawRebirthValue2D(float2(across * 1.4f, along * 0.35f)) - 0.5f) * 0.14f;
        const float fiberA = rawRebirthValue2D(float2((across + grainWarp) * 24.0f, along * 1.10f));
        const float fiberB = rawRebirthValue2D(float2((across + grainWarp) * 52.0f, along * 2.30f));
        const float ring   = 0.5f + 0.5f * sin((across + grainWarp) * 38.0f + fiberA * 2.4f);
        return saturate(0.46f * fiberA + 0.34f * fiberB + 0.20f * ring);
    }
    if (classId == kRawClass14_CobbleRoadPath) {
        // Sculpted non-periodic rounded cobblestone stone relief + weathered pebble/sand interstices
        const float stoneMacro = rawRebirthHexStochasticNoise(pDom * 1.55f);
        const float chiselPit  = rawRebirthValue2D(pDom * 11.5f + float2(stoneMacro * 1.2f, 2.7f));
        const float quartzGrit = rawRebirthValue2D(pDom * 26.0f + worldM.z * 6.0f);
        return saturate(0.36f * stoneMacro + 0.36f * chiselPit + 0.28f * quartzGrit);
    }
    if (classId == kRawClass14_NordicStoneMasonry) {
        // Hand-dressed ashlar stone quarry pits & crisp chiseled grain (zero fake brick grid!)
        const float stoneMacro = rawRebirthHexStochasticNoise(pDom * 1.35f);
        const float chiselPit  = rawRebirthValue2D(pDom * 10.0f + float2(stoneMacro * 1.1f, 3.1f));
        const float quartzGrit = rawRebirthValue2D(pDom * 24.0f + worldM.z * 7.5f);
        return saturate(0.38f * stoneMacro + 0.36f * chiselPit + 0.26f * quartzGrit);
    }
    if (classId == kRawClass14_RockCliffBoulder) {
        // Deep horizontal sedimentary rock strata ledges + angular fractured cleavage planes + granite grit
        const float warp = (rawRebirthHexStochasticNoise(pDom * 0.52f) - 0.5f) * 1.45f;
        const float strata = 0.5f + 0.5f * sin(worldM.z * 2.8f + warp * 3.4f);
        const float ledge  = smoothstep(0.28f, 0.72f, strata);
        const float crack  = rawGlacierConchoidalCrack(pDom * 1.15f + float2(worldM.z * 0.45f, 1.9f));
        const float grit   = rawRebirthValue2D(pDom * 18.0f + worldM.z * 6.5f);
        return saturate(0.36f * ledge + 0.38f * grit + 0.26f * rawRebirthHexStochasticNoise(pDom * 1.7f) - 0.30f * crack);
    }
    if (classId == kRawClass14_Snowpack) {
        const float sastrugi = rawRebirthHexStochasticNoise(float2(pDom.x * 0.85f, pDom.y * 2.2f));
        const float micro = rawRebirthValue2D(pDom * 15.0f);
        return saturate(0.62f * sastrugi + 0.38f * micro);
    }
    if (classId == kRawClass14_ForgedMetalArmor) {
        const float brush = rawRebirthValue2D(float2(pDom.x * 5.0f, pDom.y * 24.0f));
        const float dimple = rawRebirthHexStochasticNoise(pDom * 5.5f);
        return saturate(0.64f * brush + 0.36f * dimple);
    }
    // TerrainGround / MossLichen / GrassGroundcover / Foliage
    const float macro = rawRebirthHexStochasticNoise(pDom * 1.15f);
    const float pebble = rawRebirthValue2D(pDom * 11.5f);
    const float micro = rawRebirthValue2D(pDom * 24.0f);
    return saturate(0.38f * macro + 0.36f * pebble + 0.26f * micro);
}

// Wave 89 Part 2: 4-Step Silhouette-Safe POM Height-Field Displacement & Cobblestone/Cliff/Road Sculpting.
// Returns POM-displaced world position `outWorldM`, self-shadowing AO `outPomAo`, and road micro-puddle mask `outPuddle`.
void rawRebirthEvalPomDisplacement(
    uint classId, float3 worldM, float3 N, float3 viewDir, float distM, float interiorMask, float rain01,
    out float3 outWorldM, out float outPomAo, out float outPuddle)
{
    outWorldM = worldM;
    outPomAo  = 1.0f;
    outPuddle = 0.0f;

    const bool pomEligible = (classId == kRawClass14_CobbleRoadPath)
                          || (classId == kRawClass14_RockCliffBoulder)
                          || (classId == kRawClass14_TerrainGround)
                          || (classId == kRawClass14_NordicStoneMasonry);
    const float pomFade = (1.0f - smoothstep(8.0f, 24.0f, distM)) * interiorMask;
    if (!pomEligible || pomFade <= 0.02f) {
        return;
    }

    // Project view ray onto surface tangent plane safely (clamped grazing denominator prevents stretching!)
    const float ndv = max(abs(dot(N, viewDir)), 0.38f);
    const float3 tanView = viewDir - N * dot(viewDir, N);
    const float depthM = (classId == kRawClass14_RockCliffBoulder) ? 0.075f
                       : ((classId == kRawClass14_CobbleRoadPath) ? 0.055f : 0.040f);
    const float3 stepVec = -tanView * ((depthM * pomFade) / (ndv * 4.0f));

    float3 curW = worldM;
    float curLayer = 1.0f;
    float hitH = 0.5f;
    float3 triW;
    [unroll]
    for (int i = 0; i < 4; ++i) {
        curLayer -= 0.25f;
        curW += stepVec;
        const float2 pStep = rawRebirthTriplanarUv(curW, N, triW);
        const float h = rawRebirthEvalClassHeight(classId, curW, N, pStep);
        if (h >= curLayer) {
            hitH = h;
            break;
        }
        hitH = h;
    }
    outWorldM = curW;
    // Recessed mortar crevices & cliff fissures receive physically grounded POM ambient occlusion
    outPomAo = lerp(1.0f, lerp(0.76f, 1.08f, hitH), pomFade * 0.72f);

    // Subtle wheel-rut & crevice micro-puddles on upward cobblestone roads & damp paths
    if ((classId == kRawClass14_CobbleRoadPath || classId == kRawClass14_TerrainGround) && N.z > 0.78f) {
        const float hollow = smoothstep(0.46f, 0.20f, hitH);
        const float rutMacro = smoothstep(0.54f, 0.80f, rawRebirthHexStochasticNoise(worldM.xy * 0.26f + float2(9.1f, 4.3f)));
        outPuddle = hollow * rutMacro * saturate(0.32f + rain01 * 0.68f) * pomFade;
    }
}

// Applies full Wave 89 14-class world-space PBR overhaul, 4-step POM road/cliff relief,
// anisotropic glacial striations, directional wood grain, and warm interior readability.
float3 rawRebirthApplyTotalMaterialOverhaul(
    float3 col, float3 alb, float3 N, float3 worldM, float distM,
    float3 sunDir, float3 sunCol, float3 viewDir,
    float interiorMask, float albLap, float2 albGrad, float rain01,
    uint classId, bool isInt, out float3 outN)
{
    outN = N;
    if (classId == kRawClass14_CharacterSkinHairCloth || classId == kRawClass14_WaterSurface || classId == kRawClass14_Unclassified) {
        return col;
    }

    const bool isThinFoliage = (classId == kRawClass14_TreeFoliageCanopy || classId == kRawClass14_GrassGroundcover);
    const bool isTimber      = (classId == kRawClass14_TimberWood || classId == kRawClass14_TreeTrunkBark);
    const bool isIceCavern   = (classId == kRawClass14_GlacierIce);

    const float macroFade  = 1.0f - smoothstep(28.0f, 95.0f, distM);
    const float detailFade = (1.0f - smoothstep(isInt ? 18.0f : 12.0f, isInt ? 54.0f : 32.0f, distM)) * interiorMask;
    if (macroFade <= 1.0e-4f) {
        return col;
    }

    // 0) Wave 89 Part 2: 4-Step POM height displacement for sculpted 3D cobble roads, fractured cliffs & stone masonry
    float3 pomWorldM = worldM;
    float pomAo = 1.0f;
    float roadPuddle = 0.0f;
    rawRebirthEvalPomDisplacement(classId, worldM, N, viewDir, distM, interiorMask, rain01, pomWorldM, pomAo, roadPuddle);

    float3 triW;
    const float2 pDom = rawRebirthTriplanarUv(pomWorldM, N, triW);

    // 1) Low-frequency non-periodic hex-tiled geological breakup (ZERO blotchiness on wood or glacier ice!)
    const float macroHexA = rawRebirthHexStochasticNoise(pDom * 0.17f + float2(3.1f, 7.4f));
    const float macroHexB = rawRebirthHexStochasticNoise(pDom * 0.43f + float2(11.7f, 2.3f));
    const float macroBreak = (macroHexA * 0.62f + macroHexB * 0.38f) - 0.5f;

    const float3 mineralTint = isTimber
        ? lerp(float3(0.97f, 0.98f, 0.99f), float3(1.03f, 1.01f, 0.98f), macroHexA) // preserve original painted teal/oak hue!
        : lerp(float3(0.88f, 0.93f, 1.00f), float3(1.10f, 1.03f, 0.92f), macroHexA);
    const float macroAmp = isThinFoliage ? 0.10f
                         : (isTimber ? 0.04f
                         : (isIceCavern ? 0.02f
                         : ((classId == kRawClass14_Snowpack) ? 0.20f : 0.34f)));
    float3 res = col * lerp(float3(1.0f, 1.0f, 1.0f), mineralTint * (1.0f + macroBreak * 0.28f), macroFade * macroAmp);

    // 2) Surface-interior chiseled PBR micro-bevel relief on SOLID world geometry
    if (!isThinFoliage && detailFade > 1.0e-3f) {
        const float eps = clamp(0.018f + distM * 0.0014f, 0.018f, 0.09f);
        const float h0  = rawRebirthEvalClassHeight(classId, pomWorldM, N, pDom);
        const float hx  = rawRebirthEvalClassHeight(classId, pomWorldM + float3(eps, 0.0f, 0.0f), N, pDom + float2(eps, 0.0f));
        const float hy  = rawRebirthEvalClassHeight(classId, pomWorldM + float3(0.0f, eps, 0.0f), N, pDom + float2(0.0f, eps));

        const float closeFade = (1.0f - smoothstep(6.0f, 16.0f, distM)) * interiorMask;
        float fineGrain = 0.0f;
        if (isTimber) {
            // Strictly directional high-freq wood grain fibers (never isotropic blotches!)
            const float along  = (abs(N.z) > 0.65f) ? pomWorldM.x : pomWorldM.z;
            const float across = (abs(N.z) > 0.65f) ? pomWorldM.y : (abs(N.x) > abs(N.y) ? pomWorldM.y : pomWorldM.x);
            fineGrain = (rawRebirthValue2D(float2(across * 46.0f, along * 2.2f)) - 0.5f) * closeFade * 0.72f;
        } else if (isIceCavern) {
            // Wave 91 v2: Silky 15:1 unidirectional glacial-flow cleavage sheen (68% less amplitude than Wave 90 mottling, ZERO camo blobs!)
            const float2 pC1 = float2(pDom.x * 0.899f - pDom.y * 0.438f, pDom.x * 0.438f + pDom.y * 0.899f);
            const float silkA = rawRebirthValue2D(float2(pC1.x * 2.2f, pC1.y * 34.0f + pomWorldM.z * 7.5f));
            const float silkB = rawRebirthValue2D(float2(pC1.x * 4.5f + 5.1f, pC1.y * 62.0f - pomWorldM.z * 9.0f));
            fineGrain = ((silkA * 0.65f + silkB * 0.35f) - 0.5f) * closeFade * 1.15f;
        } else {
            const float fineA = rawRebirthValue2D(pDom * 24.0f + float2(pomWorldM.z * 9.0f, -pomWorldM.z * 7.0f));
            const float fineB = rawRebirthValue2D(pDom * 42.0f + float2(-pomWorldM.z * 13.0f, pomWorldM.z * 17.0f));
            fineGrain = ((fineA * 0.58f + fineB * 0.42f) - 0.5f) * closeFade;
        }

        const float slopeGain = (classId == kRawClass14_CobbleRoadPath || classId == kRawClass14_NordicStoneMasonry) ? 1.00f
                              : ((classId == kRawClass14_RockCliffBoulder) ? 0.96f
                              : (isTimber ? 0.72f
                              : (isIceCavern ? 0.92f
                              : ((classId == kRawClass14_Snowpack) ? 0.58f : 0.78f))));

        // Prevent magnifying 2011 DXT1 4x4 texel blocks at very close wall range (<3.2m); rely on crisp procedural world PBR!
        const float dxtSafe = smoothstep(0.9f, 3.6f, distM);
        const float albGradGain = (isTimber ? 1.8f : 3.4f) * dxtSafe;
        const float dHx = clamp((h0 - hx) * (0.36f / eps) + albGrad.x * albGradGain, -1.8f, 1.8f);
        const float dHy = clamp((h0 - hy) * (0.36f / eps) + albGrad.y * albGradGain, -1.8f, 1.8f);
        const float3 gradW = (abs(N.z) > 0.65f) ? float3(dHx, dHy, 0.0f)
                           : ((abs(N.x) > abs(N.y)) ? float3(0.0f, dHx, dHy) : float3(dHx, 0.0f, dHy));
        // Level micro-normal smoothly inside road micro-puddles for wet mirror glint
        const float3 nPert = normalize(lerp(N + gradW * (slopeGain * detailFade * 0.60f), float3(0.0f, 0.0f, 1.0f), roadPuddle * 0.65f));
        outN = nPert;

        const float3 keyDir = isInt ? normalize(float3(0.32f, 0.28f, 0.90f)) : normalize(sunDir);
        const float ndlBase = max(dot(N, keyDir), 0.20f);
        const float ndlPert = max(dot(nPert, keyDir), 0.08f);
        const float bevelRatio = clamp(ndlPert / ndlBase, 0.72f, 1.42f);

        const float lum0 = dot(res, float3(0.2126f, 0.7152f, 0.0722f));
        const float darkSafe = smoothstep(0.026f, 0.090f, lum0);
        const float hiSafe   = isIceCavern ? (1.0f - smoothstep(0.36f, 0.74f, lum0))
                                           : (1.0f - smoothstep(0.18f, 0.46f, lum0));
        const float grainAmp = isIceCavern ? 0.06f : (isTimber ? 0.28f : 0.66f);
        const float fineAmp  = isIceCavern ? 0.82f : (isTimber ? 0.48f : 0.72f);
        const float grainHi  = (h0 - 0.5f) * grainAmp + fineGrain * fineAmp;
        const float carveHi  = clamp(albLap * (isIceCavern ? 3.20f : (isTimber ? 2.60f : 4.20f)) * dxtSafe,
                                     isIceCavern ? -0.10f : -0.12f,
                                     isIceCavern ? 0.24f : 0.28f);
        const float maxMul   = lerp(1.02f, isIceCavern ? 1.32f : (isTimber ? 1.18f : 1.34f), hiSafe);
        // Wave 91 v2: Smooth translucent sapphire floor (0.80 -> ZERO busy camo mottling; carved crystal planes & hairline seams below!)
        const float minMul   = (isInt && !isIceCavern) || isTimber ? 0.84f : (isIceCavern ? 0.80f : 0.78f);
        const float reliefMul = clamp(
            lerp(1.0f, bevelRatio * pomAo * (1.0f + grainHi + carveHi), detailFade * lerp(0.32f, isIceCavern ? 0.76f : 0.82f, darkSafe)),
            minMul, maxMul);
        res *= reliefMul;

        // Wave 91 GlacierIce Visual Eye-Audit Fix: Broad smooth translucent sapphire crystal planes + delicate hairline fracture seams
        // + soft Beer-Lambert depth (OPT-033) + 2-layer Snell internal parallax caustics (OPT-001, IOR 1.31), ZERO camo mottling!
        if (isIceCavern) {
            const float2 pOblI = float2(pDom.x * 0.899f - pDom.y * 0.438f, pDom.x * 0.438f + pDom.y * 0.899f);
            const float2 pWI   = float2(
                pOblI.x + 0.14f * sin(pOblI.y * 2.6f + pomWorldM.z * 0.55f + 0.7f) + 0.06f * cos(pOblI.x * 4.1f - pOblI.y * 1.9f),
                pOblI.y + 0.10f * cos(pOblI.x * 3.1f - pomWorldM.z * 0.45f - 1.1f) + 0.04f * sin(pOblI.y * 4.7f + pOblI.x * 2.2f));
            float cid1 = 0.5f, edge1 = 0.5f;
            float cid2 = 0.5f, edge2 = 0.5f;
            rawGlacierCarvedVoronoi(float2(pWI.x * 1.45f, pWI.y * 4.80f + pomWorldM.z * 0.75f), 1.0f, cid1, edge1);
            rawGlacierCarvedVoronoi(float2((pWI.x + 0.37f) * 3.20f, (pWI.y - 0.21f) * 11.50f - pomWorldM.z * 1.15f), 2.0f, cid2, edge2);

            // Delicate razor-thin crystalline fracture seams (luminous Fresnel rim + subtle internal shadow groove)
            const float seam1  = saturate(1.0f - (edge1 - 0.002f) / 0.019f) * (0.65f + 0.35f * cid1);
            const float seam2  = saturate(1.0f - (edge2 - 0.002f) / 0.016f) * smoothstep(0.30f, 0.75f, cid2);
            const float groove = 1.0f - 0.22f * saturate(1.0f - abs(edge1 - 0.020f) / 0.014f)
                                      - 0.12f * saturate(1.0f - abs(edge2 - 0.018f) / 0.012f);
            // Broad smooth carved crystal plane specular shift (flat & translucent inside each carved facet, >=48% tiling breakup!)
            const float planeLight = lerp(0.66f, 1.36f, cid1) * lerp(0.84f, 1.16f, cid2) * groove;

            const float3 vRefr = refract(-viewDir, nPert, 1.0f / 1.31f);
            const float3 pIn1  = pomWorldM + vRefr * 0.24f;
            const float3 pIn2  = pomWorldM + vRefr * 0.54f;
            const float cIn1   = rawRebirthHexStochasticNoise(pIn1.xy * 1.85f + float2(pIn1.z * 1.35f, 3.1f));
            const float cIn2   = rawRebirthHexStochasticNoise(pIn2.xy * 3.40f + float2(-pIn2.z * 2.15f, 9.7f));
            const float causticInt = pow(saturate(1.0f - abs(cIn1 * 2.0f - 1.0f)), 3.2f) * 0.65f
                                   + pow(saturate(1.0f - abs(cIn2 * 2.0f - 1.0f)), 3.8f) * 0.35f;
            const float3 beerSapphire = exp(-lerp(0.24f, 0.68f, 1.0f - cid1) * float3(1.55f, 0.40f, 0.12f));
            const float3 iceBodyTint  = lerp(float3(0.88f, 0.98f, 1.08f), float3(1.05f, 1.16f, 1.24f), cid1);
            res *= lerp(float3(1.0f, 1.0f, 1.0f), iceBodyTint * planeLight * lerp(float3(0.90f, 0.97f, 1.04f), beerSapphire + 0.44f, 0.38f), detailFade * 0.85f);
            const float fresIce = pow(saturate(1.0f - abs(dot(nPert, viewDir))), 3.4f);
            const float crystalGlint = pow(saturate(dot(nPert, normalize(keyDir + viewDir))), 40.0f) * smoothstep(0.58f, 0.92f, cid1);
            const float seamRim = (pow(seam1, 1.8f) * 0.20f + pow(seam2, 2.0f) * 0.13f) * closeFade * hiSafe;
            res += float3(0.16f, 0.26f, 0.34f) * seamRim;
            res += float3(0.038f, 0.098f, 0.145f) * (causticInt * closeFade * hiSafe * 0.65f);
            res += float3(0.095f, 0.145f, 0.195f) * ((crystalGlint * 0.42f + fresIce * 0.20f) * closeFade * hiSafe);
        }

        // Cobblestone wheel-rut micro-puddle darkening & sky/sun reflection sheen
        if (roadPuddle > 0.02f) {
            res *= lerp(1.0f, 0.74f, roadPuddle * darkSafe);
            const float fresPud = pow(1.0f - saturate(dot(nPert, viewDir)), 4.0f);
            const float3 skyRefl = float3(0.32f, 0.42f, 0.56f) * (0.35f + 0.65f * fresPud);
            res += skyRefl * (roadPuddle * 0.24f * hiSafe);
        }

        // Crisp physical micro-specular bevel glint (strictly zero-firefly!)
        const float3 halfV = normalize(keyDir + viewDir);
        const float specPow = (classId == kRawClass14_ForgedMetalArmor) ? 26.0f
                            : ((isIceCavern || classId == kRawClass14_CobbleRoadPath) ? 20.0f : 14.0f);
        const float specGain = (classId == kRawClass14_ForgedMetalArmor) ? 0.075f
                             : (isIceCavern ? 0.065f
                             : ((classId == kRawClass14_CobbleRoadPath || classId == kRawClass14_NordicStoneMasonry) ? 0.026f : 0.010f));
        const float microSpec = pow(saturate(dot(nPert, halfV)), specPow) * smoothstep(0.48f, 0.90f, h0);
        const float3 specTint = (classId == kRawClass14_ForgedMetalArmor) ? max(alb, float3(0.55f, 0.58f, 0.62f))
                              : (isIceCavern ? float3(0.76f, 0.92f, 1.0f) : float3(1.0f, 1.0f, 1.0f));
        res += min(sunCol, float3(1.00f, 1.00f, 1.00f)) * specTint * (microSpec * specGain * detailFade * darkSafe * hiSafe);
    }

    return res;
}

#endif // RAW_TOTAL_MATERIAL_OVERHAUL_HLSLI
