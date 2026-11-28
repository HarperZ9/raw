// RawShadowPcssSim.hlsl: SM 6.6 / 6.10 GPU Compute Variable-Penumbra PCSS Contact-Hardening Shadow Solver.
// Implements Fernando (2005) 3-stage Percentage-Closer Soft Shadows with Interleaved Gradient Noise (IGN)
// rotated Vogel-disk sampling and depth-reconstructed normal-offset slope bias.
// Original clean-room implementation, MIT License.
//
// Inputs:
//   t0: SceneDepth (Standard-Z: 0 = near, 1 = far; >= 0.99998 treated as sky -> visibility = 1.0)
//   t1: ShadowMap  (Directional ortho cascade depth: 0 = near caster, 1 = far)
// Output:
//   u0: ShadowOut  (float2: x = visibility [0..1], y = normalized penumbra radius [0..1])

Texture2D<float>       gSceneDepth : register(t0);
Texture2D<float>       gShadowMap  : register(t1);
RWTexture2D<float2>    gShadowOut  : register(u0);

SamplerState           gPointClamp : register(s0);
SamplerComparisonState gShadowCmp  : register(s1);

cbuffer ShadowPcssCB : register(b0)
{
    row_major float4x4 gInvViewProj;    // NDC (x,y,depth,1) -> worldPos
    row_major float4x4 gLightViewProj;  // worldPos -> ortho light clip (xy in [-1,1], z in [0,1])
    float4             gSunDirAndBias;  // xyz = unit sun direction (toward sun), w = baseDepthBias
    float4             gBiasAndJitter;  // x = slopeDepthBias, y = normalOffsetWorld, z = lightAngularSize, w = frameJitter
    float4             gFilterParams;   // x = blockerSearchTexels, y = minFilterTexels, z = maxFilterTexels, w = shadowInvSize
    uint4              gDims;           // x = outWidth, y = outHeight, z = shadowMapSize, w = flags
};

static const float kPi          = 3.14159265359f;
static const float kTwoPi       = 6.28318530718f;
static const float kGoldenAngle = 2.39996322973f;

float InterleavedGradientNoise(float2 px, float frameJitter)
{
    const float2 p = px + float2(frameJitter * 5.588238f, frameJitter * 9.042254f);
    return frac(52.9829189f * frac(dot(p, float2(0.06711056f, 0.00583715f))));
}

float2 VogelDiskSample(uint sampleIdx, uint sampleCount, float phi)
{
    const float r     = sqrt(((float)sampleIdx + 0.5f) / (float)sampleCount);
    const float theta = (float)sampleIdx * kGoldenAngle + phi;
    float s, c;
    sincos(theta, s, c);
    return float2(c, s) * r;
}

float3 ReconstructWorldPos(uint2 px, float depth, float2 invDims)
{
    const float2 uv  = (float2(px) + 0.5f) * invDims;
    const float  ndcX = uv.x * 2.0f - 1.0f;
    const float  ndcY = 1.0f - uv.y * 2.0f;
    const float4 wp4  = mul(float4(ndcX, ndcY, depth, 1.0f), gInvViewProj);
    const float  invW = (abs(wp4.w) > 1.0e-7f) ? (1.0f / wp4.w) : 1.0f;
    return wp4.xyz * invW;
}

float3 ReconstructSurfaceNormal(uint2 px, float depthC, float3 posC, uint2 dims, float2 invDims)
{
    const uint2 pxR = uint2(min(px.x + 1u, dims.x - 1u), px.y);
    const uint2 pxL = uint2((px.x > 0u) ? (px.x - 1u) : 0u, px.y);
    const uint2 pxD = uint2(px.x, min(px.y + 1u, dims.y - 1u));
    const uint2 pxU = uint2(px.x, (px.y > 0u) ? (px.y - 1u) : 0u);

    const float dR = gSceneDepth.Load(int3(pxR, 0));
    const float dL = gSceneDepth.Load(int3(pxL, 0));
    const float dD = gSceneDepth.Load(int3(pxD, 0));
    const float dU = gSceneDepth.Load(int3(pxU, 0));

    float3 dPdx;
    if (abs(dR - depthC) <= abs(dL - depthC) && dR < 0.99998f)
        dPdx = ReconstructWorldPos(pxR, dR, invDims) - posC;
    else if (dL < 0.99998f)
        dPdx = posC - ReconstructWorldPos(pxL, dL, invDims);
    else
        dPdx = float3(1.0f, 0.0f, 0.0f);

    float3 dPdy;
    if (abs(dD - depthC) <= abs(dU - depthC) && dD < 0.99998f)
        dPdy = ReconstructWorldPos(pxD, dD, invDims) - posC;
    else if (dU < 0.99998f)
        dPdy = posC - ReconstructWorldPos(pxU, dU, invDims);
    else
        dPdy = float3(0.0f, 1.0f, 0.0f);

    float3 n = cross(dPdy, dPdx);
    const float lenSq = dot(n, n);
    if (lenSq < 1.0e-12f)
        return normalize(gSunDirAndBias.xyz);
    n *= rsqrt(lenSq);
    if (dot(n, gSunDirAndBias.xyz) < 0.0f)
        n = -n;
    return n;
}

[numthreads(8, 8, 1)]
void cs_shadow_pcss(uint3 dtid : SV_DispatchThreadID)
{
    const uint2 dims = gDims.xy;
    if (dtid.x >= dims.x || dtid.y >= dims.y)
        return;

    const float sceneDepth = gSceneDepth.Load(int3(dtid.xy, 0));
    // Sky pixels (>= 0.99998 standard-Z) are always fully sunlit
    if (sceneDepth >= 0.99998f || sceneDepth <= 0.0f || !isfinite(sceneDepth))
    {
        gShadowOut[dtid.xy] = float2(1.0f, 0.0f);
        return;
    }

    const float2 invDims  = 1.0f / max(float2(dims), 1.0f.xx);
    const float3 worldPos = ReconstructWorldPos(dtid.xy, sceneDepth, invDims);
    const float3 sunDir   = normalize(gSunDirAndBias.xyz);
    const float3 surfN    = ReconstructSurfaceNormal(dtid.xy, sceneDepth, worldPos, dims, invDims);
    const float  nDotL    = saturate(dot(surfN, sunDir));

    // Normal-offset projection eliminates self-shadow acne on grazing slopes
    const float  normOffset = gBiasAndJitter.y * (1.0f - nDotL);
    const float3 samplePosW = worldPos + surfN * normOffset;
    const float4 lightClip  = mul(float4(samplePosW, 1.0f), gLightViewProj);
    const float  invLw      = (abs(lightClip.w) > 1.0e-7f) ? (1.0f / lightClip.w) : 1.0f;
    const float3 lightNdc   = lightClip.xyz * invLw;

    const float2 shadowUv   = float2(lightNdc.x * 0.5f + 0.5f, 0.5f - lightNdc.y * 0.5f);
    const float  receiverZ  = lightNdc.z;

    if (shadowUv.x <= 0.0f || shadowUv.x >= 1.0f ||
        shadowUv.y <= 0.0f || shadowUv.y >= 1.0f ||
        receiverZ  <= 0.0f || receiverZ  >= 1.0f)
    {
        gShadowOut[dtid.xy] = float2(1.0f, 0.0f);
        return;
    }

    const float tanSlope       = sqrt(max(0.0f, 1.0f - nDotL * nDotL)) / max(nDotL, 0.15f);
    const float depthBias      = gSunDirAndBias.w + gBiasAndJitter.x * min(tanSlope, 4.0f);
    const float biasedReceiver = saturate(receiverZ - depthBias);

    const float invShadowRes   = gFilterParams.w;
    const float searchRadiusUv = max(gFilterParams.x, 1.0f) * invShadowRes;
    const float minFilterTex   = max(gFilterParams.y, 0.25f);
    const float maxFilterTex   = max(gFilterParams.z, minFilterTex + 0.5f);

    const float ignAngle = InterleavedGradientNoise(float2(dtid.xy), gBiasAndJitter.w) * kTwoPi;

    // ------------------------------------------------------------------------
    // Stage 1: Fernando (2005) 16-Tap Vogel-Disk Blocker Search
    // ------------------------------------------------------------------------
    float blockerDepthSum = 0.0f;
    uint  blockerCount    = 0u;

    [unroll]
    for (uint i = 0u; i < 16u; ++i)
    {
        const float2 offset = VogelDiskSample(i, 16u, ignAngle) * searchRadiusUv;
        const float  smZ    = gShadowMap.SampleLevel(gPointClamp, shadowUv + offset, 0.0f);
        if (smZ < biasedReceiver)
        {
            blockerDepthSum += smZ;
            ++blockerCount;
        }
    }

    // Early-out when zero blockers are found in the search disk (open sunlit surface)
    if (blockerCount == 0u)
    {
        gShadowOut[dtid.xy] = float2(1.0f, 0.0f);
        return;
    }

    // ------------------------------------------------------------------------
    // Stage 2: Physical Contact-Hardening Penumbra Estimation
    // ------------------------------------------------------------------------
    const float avgBlockerZ    = blockerDepthSum / (float)blockerCount;
    const float blockerGap     = max(receiverZ - avgBlockerZ, 0.0f);
    const float lightAngSize   = max(gBiasAndJitter.z, 0.01f);
    const float rawPenumbraTex = (blockerGap * lightAngSize) / max(avgBlockerZ, 1.0e-4f);
    const float penumbraTexels = clamp(rawPenumbraTex, minFilterTex, maxFilterTex);
    const float normPenumbra   = saturate(penumbraTexels / maxFilterTex);
    const float filterRadiusUv = penumbraTexels * invShadowRes;

    // ------------------------------------------------------------------------
    // Stage 3: 32-Tap IGN-Rotated Vogel-Disk Hardware-Bilinear PCF Filter
    // ------------------------------------------------------------------------
    float visSum = 0.0f;
    const float pcfAngle = ignAngle + 1.1357f;

    [unroll]
    for (uint j = 0u; j < 32u; ++j)
    {
        const float2 offset = VogelDiskSample(j, 32u, pcfAngle) * filterRadiusUv;
        visSum += gShadowMap.SampleCmpLevelZero(gShadowCmp, shadowUv + offset, biasedReceiver);
    }

    const float visibility = saturate(visSum * (1.0f / 32.0f));
    gShadowOut[dtid.xy] = float2(visibility, normPenumbra);
}
