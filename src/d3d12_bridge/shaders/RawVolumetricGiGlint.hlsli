// RawVolumetricGiGlint.hlsli: Wave 77 World-Space Multi-Bounce GI Injection into Froxel Volumetrics,
// Gust-Advected Water Foam & Capillary Ripple Coupling, and Energy-Conserving Micro-Flake Snow Glint.
// Self-contained clean-room MIT HLSL library (SM 6.6 / 6.9 / 6.10, 0 screen-UV dependence, 0 NaN/Inf).
// Reuses Wave 75 L=8 world-space hash cascade (RawRestirHashCache.hlsli) with zero duplicate code.
// Original work, MIT.
#ifndef RAW_VOLUMETRIC_GI_GLINT_HLSLI
#define RAW_VOLUMETRIC_GI_GLINT_HLSLI

#include "RawRestirHashCache.hlsli"

struct RawFroxelGiResult {
    float3 bounceInScat;    // Finite, albedo-bounded world-space multi-bounce GI radiance
    uint   hashKeyL8;       // Wave 75 L=8 world-space hash cell key for this froxel
    uint   lodLevel;        // Selected L=8 cascade LOD in [0, 7]
    float  depthGate;       // 3D world-space proximity weight [0, 1] (0 when far from surface -> 0 screen bleed)
};

struct RawGustWaterResult {
    float2 capillarySlope;  // Gust-advected capillary normal slope perturbation (finite, bounded)
    float2 foamAdvectDelta; // Coherent downwind foam advection offset (world units)
    float  foamAccum01;     // Coupled shoreline/rapids/crest foam accumulation [0, 1]
    float  fresnelEff;      // Energy-conserving Fresnel after foam micro-bubble modulation [0, 1]
    float  roughnessEff;    // Coupled capillary + foam surface roughness [0.02, 0.88]
};

struct RawSnowGlintResult {
    float glintSpec;        // Strictly energy-conserving micro-flake specular reflectance [0, energyBudget)
    float totalReflectance; // macroDiffuseAlbedo + glintSpec, algebraically <= 1.0 (white-furnace safe)
    uint  worldCellHash;    // Pure world-space 3D micro-flake cell hash (0.0 screen-UV drift)
    float energyBudget;     // Available hemispherical specular energy budget (1.0 - macroDiffuseAlbedo)
};

float rawVggSan1(float v, float fb) {
    return isfinite(v) ? v : fb;
}

float3 rawVggSan3(float3 v, float capVal) {
    return all(isfinite(v)) ? clamp(v, 0.0f.xxx, capVal.xxx) : 0.0f.xxx;
}

uint rawVggHashInt3(int3 q) {
    uint h = (uint(q.x) * 73856093u) ^ (uint(q.y) * 19349663u) ^ (uint(q.z) * 83492791u);
    h = (h ^ (h >> 16u)) * 2246822507u;
    h = (h ^ (h >> 13u)) * 3266489909u;
    return h ^ (h >> 16u);
}

float rawVggUnorm(uint h) {
    return float(h & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

// -----------------------------------------------------------------------------
// 1. World-Space Multi-Bounce GI Injection into Froxel Volumetrics
// -----------------------------------------------------------------------------
// Samples Wave 75's L=8 world-space hash cascade cell + depth-gated multi-bounce
// radiance with strict finite clamping and albedo-bounded bounce energy so
// torchlight, fire, and sunlit bounce illuminate indoor/outdoor fog and snow
// flurries in 3D world space without screen-space glow bleed.
RawFroxelGiResult rawEvalFroxelWorldGiBounce(
    float3 wpAbs,
    float3 viewDir,
    float  rayDist,
    float  zMid,
    float  zSurf,
    float3 cacheBounceRad,
    float3 worldAmbBounce,
    float  scatterAlbedo,
    bool   isInterior,
    float  snowFlurry01)
{
    RawFroxelGiResult o;
    const float3 wpSafe = float3(rawVggSan1(wpAbs.x, 0.0f), rawVggSan1(wpAbs.y, 0.0f), rawVggSan1(wpAbs.z, 0.0f));
    const float3 vdRaw  = float3(rawVggSan1(viewDir.x, 0.0f), rawVggSan1(viewDir.y, 1.0f), rawVggSan1(viewDir.z, 0.0f));
    const float  vdLen2 = dot(vdRaw, vdRaw);
    const float3 vdSafe = (vdLen2 > 1.0e-6f) ? (vdRaw * rsqrt(vdLen2)) : float3(0.0f, 1.0f, 0.0f);

    const float distSafe = max(rawVggSan1(rayDist, 64.0f), 1.0f);
    const float zMidSafe = max(rawVggSan1(zMid, 64.0f), 1.0f);
    const float zSrfSafe = rawVggSan1(zSurf, -1.0f);
    const float albSafe  = clamp(rawVggSan1(scatterAlbedo, 0.65f), 0.0f, 0.85f);
    const float snow01   = saturate(rawVggSan1(snowFlurry01, 0.0f));

    // Wave 75 L=8 world-space cascade LOD and deterministic hash cell key:
    const uint lod = min(uint(max(floor(log2(max(distSafe * (1.0f / 64.0f), 1.0f))), 0.0f)), 7u);
    o.lodLevel  = lod;
    o.hashKeyL8 = rawHashCacheKeyMultiRes(wpSafe, vdSafe, lod, 16.0f);

    // Strict 3D world-space proximity gate (eliminates screen-space glow bleed across depth jumps):
    // A distant glowing wall or sunlit surface at zSurf never bleeds into foreground air at zMid,
    // and froxels occluded behind solid walls (zMid > zSurf + halfCell) receive 0 screen-hit bounce.
    float gate = 0.0f;
    if (zSrfSafe > 1.0f && zSrfSafe < 42000.0f) {
        const float cellSpan = 16.0f * float(1u << lod);
        const float radW = clamp(cellSpan * 2.5f, 48.0f, 420.0f);
        const float dz = abs(zMidSafe - zSrfSafe);
        if (zMidSafe <= zSrfSafe + radW * 0.50f && dz <= radW * 2.50f) {
            const float q = dz / radW;
            gate = exp(-min(q * q, 24.0f));
        }
    }
    o.depthGate = gate;

    // Strict finite radiance clamping & albedo-bounded bounce conservation:
    const float3 cacheSafe = rawVggSan3(cacheBounceRad, 12.0f);
    const float3 ambSafe   = rawVggSan3(worldAmbBounce, 6.0f);
    const float  snowBoost = 1.0f + snow01 * 0.25f;
    const float  gain      = isInterior ? 0.34f : 0.24f;

    const float3 rawCombined = cacheSafe * gate + ambSafe * 0.28f;
    const float3 scaled = rawCombined * (albSafe * gain * snowBoost);
    const float3 energyCap = min((cacheSafe + ambSafe) * albSafe, (albSafe * (isInterior ? 1.85f : 2.25f)).xxx);
    o.bounceInScat = min(rawVggSan3(scaled, 4.0f), energyCap);
    return o;
}

// -----------------------------------------------------------------------------
// 2. Gust-Advected Water Foam & Capillary Ripple Coupling
// -----------------------------------------------------------------------------
// Couples Wave 74's coherent gust vector directly into water capillary normal
// advection, shoreline/rapids foam accumulation, and energy-conserving Fresnel
// + roughness modulation (white-furnace safe: diffuseFoam + fresnelEff <= 1.0).
RawGustWaterResult rawEvalGustWaterFoamRipple(
    float3 wpAbs,
    float2 gustVelXY,
    float  gust01,
    float  simTimeSec,
    float  baseFoam01,
    float  riverFlowSpeed,
    float  shoreMask01,
    float  fresnelWater,
    float  baseRoughness,
    float  distFade01)
{
    RawGustWaterResult o;
    const float2 pXY    = float2(rawVggSan1(wpAbs.x, 0.0f), rawVggSan1(wpAbs.y, 0.0f));
    const float2 gVel   = clamp(float2(rawVggSan1(gustVelXY.x, 0.0f), rawVggSan1(gustVelXY.y, 0.0f)), -2.0f.xx, 2.0f.xx);
    const float  g01    = saturate(rawVggSan1(gust01, 0.0f));
    const float  tSafe  = rawVggSan1(simTimeSec, 0.0f);
    const float  bFoam  = saturate(rawVggSan1(baseFoam01, 0.0f));
    const float  rFlow  = saturate(rawVggSan1(riverFlowSpeed, 0.0f));
    const float  shore  = saturate(rawVggSan1(shoreMask01, 0.0f));
    const float  fW     = clamp(rawVggSan1(fresnelWater, 0.02f), 0.0f, 1.0f);
    const float  rBase  = clamp(rawVggSan1(baseRoughness, 0.04f), 0.02f, 0.65f);
    const float  dFade  = saturate(rawVggSan1(distFade01, 1.0f));

    const float  gLen = length(gVel);
    const float2 gDir = (gLen > 1.0e-4f) ? (gVel / gLen) : float2(0.8660254f, 0.5f);
    const float2 gPerp = float2(-gDir.y, gDir.x);

    // Downwind-advected incommensurate dual-band capillary ripple gradient (0 periodic stripes):
    const float tWrap = fmod(abs(tSafe), 4096.0f) * (tSafe < 0.0f ? -1.0f : 1.0f);
    const float2 pAdv = pXY - gDir * (tWrap * (5.5f + 9.5f * g01));
    const float  u1 = dot(pAdv, gDir) * 0.173f + dot(pAdv, gPerp) * 0.091f;
    const float  u2 = dot(pAdv, gDir) * 0.119f - dot(pAdv, gPerp) * 0.237f + 1.93f;
    const float  d1 = cos(u1) * 0.62f;
    const float  d2 = cos(u2) * 0.38f;
    const float2 rawSlope = (gDir * (d1 * 0.173f + d2 * 0.119f) + gPerp * (d1 * 0.091f - d2 * 0.237f))
                          * (0.48f * g01 * dFade);
    o.capillarySlope = clamp(rawSlope, -0.14f.xx, 0.14f.xx);

    // Coherent gust downwind foam advection & shoreline/rapids accumulation:
    o.foamAdvectDelta = gVel * (g01 * (4.0f + 6.0f * rFlow));
    const float gustBoost = saturate((shore * 0.55f + rFlow * 0.45f) * g01 * 0.38f);
    o.foamAccum01 = saturate(bFoam + gustBoost * (1.0f - bFoam * 0.65f));

    // Energy-conserving Fresnel + roughness modulation under foam micro-bubbles:
    // Foam roughens specular reflection and caps Fresnel so diffuseFoam (<=0.70*foam) + fresnelEff <= 1.0.
    o.roughnessEff = clamp(lerp(rBase + g01 * 0.06f, 0.74f, o.foamAccum01 * 0.85f), 0.02f, 0.88f);
    const float fFoamCap = min(fW * 0.35f + 0.04f, 0.28f);
    const float fBlended = lerp(fW, fFoamCap, o.foamAccum01 * 0.82f);
    o.fresnelEff = saturate(min(fBlended, max(1.0f - 0.70f * o.foamAccum01, 0.0f)));
    return o;
}

// -----------------------------------------------------------------------------
// 3. Micro-Flake Snow Glint Energy Conservation (World-Space Anchored)
// -----------------------------------------------------------------------------
// Evaluates stochastic IOR 1.31 ice-crystal micro-facet sparkles strictly on
// fixed dyadic 3D world-space grids (2u & 4u cells -> 0.0 screen-UV drift on
// camera pan!) with algebraic white-furnace upper bound <= 1.0 and 0 NaN/Inf.
float rawEvalSnowGlintGrid(int3 qCell, float3 nSafe, float3 hSafe, out uint outHash) {
    const uint h0 = rawVggHashInt3(qCell);
    const uint h1 = rawVggHashInt3(qCell + int3(19, 47, 83));
    const uint h2 = rawVggHashInt3(qCell + int3(-31, 61, 13));
    outHash = h0;
    const float pProb = rawVggUnorm(h0);
    const float gate  = smoothstep(0.74f, 0.96f, pProb);
    if (gate <= 1.0e-5f) {
        return 0.0f;
    }
    const float3 jitter = float3(rawVggUnorm(h1), rawVggUnorm(h2), rawVggUnorm(h0 ^ h1)) - 0.5f.xxx;
    float3 mN = nSafe + jitter * 0.68f;
    const float mLen2 = dot(mN, mN);
    mN = (mLen2 > 1.0e-6f) ? (mN * rsqrt(mLen2)) : nSafe;
    if (dot(mN, nSafe) < 0.25f) {
        mN = normalize(nSafe + mN * 0.5f);
    }
    const float nh = saturate(dot(mN, hSafe));
    const float diamond = pow(nh, 160.0f) * 1.45f;
    const float frost   = pow(nh,  44.0f) * 0.14f;
    return (diamond + frost) * gate;
}

RawSnowGlintResult rawEvalMicroFlakeSnowGlint(
    float3 wpAbs,
    float3 normalW,
    float3 viewDirW,
    float3 lightDirW,
    float  camDistU,
    float  snowCov01,
    float  macroDiffuseAlbedo)
{
    RawSnowGlintResult o;
    const float alb = clamp(rawVggSan1(macroDiffuseAlbedo, 0.72f), 0.0f, 0.92f);
    const float budget = max(1.0f - alb, 0.0f);
    o.glintSpec = 0.0f;
    o.totalReflectance = alb;
    o.energyBudget = budget;

    const float3 wpSafe = float3(rawVggSan1(wpAbs.x, 0.0f), rawVggSan1(wpAbs.y, 0.0f), rawVggSan1(wpAbs.z, 0.0f));
    const int3 qFine   = int3(floor(wpSafe * 0.50f));
    const int3 qCoarse = int3(floor(wpSafe * 0.25f));
    o.worldCellHash = rawVggHashInt3(qFine);

    const float cov = saturate(rawVggSan1(snowCov01, 0.0f));
    const float distU = max(rawVggSan1(camDistU, 0.0f), 0.0f);
    const float3 nRaw = float3(rawVggSan1(normalW.x, 0.0f), rawVggSan1(normalW.y, 0.0f), rawVggSan1(normalW.z, 1.0f));
    const float3 vRaw = float3(rawVggSan1(viewDirW.x, 0.0f), rawVggSan1(viewDirW.y, 0.0f), rawVggSan1(viewDirW.z, 1.0f));
    const float3 lRaw = float3(rawVggSan1(lightDirW.x, 0.0f), rawVggSan1(lightDirW.y, 0.0f), rawVggSan1(lightDirW.z, 1.0f));
    const float nL2 = dot(nRaw, nRaw), vL2 = dot(vRaw, vRaw), lL2 = dot(lRaw, lRaw);
    if (cov <= 1.0e-4f || nL2 <= 1.0e-6f || vL2 <= 1.0e-6f || lL2 <= 1.0e-6f) {
        return o;
    }

    const float3 nSafe = nRaw * rsqrt(nL2);
    const float3 vSafe = vRaw * rsqrt(vL2);
    const float3 lSafe = lRaw * rsqrt(lL2);
    const float nl = dot(nSafe, lSafe);
    const float nv = dot(nSafe, vSafe);
    if (nl <= 1.0e-4f || nv <= 1.0e-4f) {
        return o;
    }

    const float3 hRaw = vSafe + lSafe;
    const float  hL2  = dot(hRaw, hRaw);
    const float3 hSafe = (hL2 > 1.0e-6f) ? (hRaw * rsqrt(hL2)) : nSafe;
    const float  vh = saturate(dot(vSafe, hSafe));
    // IOR 1.31 ice Fresnel (F0 = 0.018):
    const float  fIce = 0.018f + 0.982f * pow(1.0f - vh, 5.0f);

    uint hFineOut = 0u, hCoarseOut = 0u;
    const float gFine   = rawEvalSnowGlintGrid(qFine,   nSafe, hSafe, hFineOut);
    const float gCoarse = rawEvalSnowGlintGrid(qCoarse, nSafe, hSafe, hCoarseOut);
    o.worldCellHash = hFineOut;

    const float distFade = smoothstep(2000.0f, 120.0f, distU);
    const float coarseW  = smoothstep(140.0f, 650.0f, distU);
    const float rawGlint = max(lerp(gFine, gCoarse, coarseW) * (0.35f + 0.65f * fIce) * cov * distFade * saturate(nl), 0.0f);

    // Strictly energy-conserving rational saturation inside [0, budget):
    // Guarantees alb + glintSpec < 1.0 for ALL inputs (white-furnace bounded <= 1.0)!
    const float boundedGlint = budget * (rawGlint / (1.0f + rawGlint));
    o.glintSpec = boundedGlint;
    o.totalReflectance = alb + boundedGlint;
    return o;
}

#endif
