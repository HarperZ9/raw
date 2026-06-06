// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Pass 3: Temporal + spatial denoise (half-res, bilateral)
//
// 1. Bilateral spatial filter (5x5) with depth-based edge stopping to
//    smooth the noisy per-pixel GI from the trace pass while preserving
//    geometric edges.
// 2. Temporal blend with ping-pong history buffer (alpha=0.2).
//    FrameIndex < 3: bypass history entirely to avoid black ghosting
//    from uninitialized buffers.

cbuffer DenoiseCB : register(b0)
{
    uint   HalfW;             // offset 0
    uint   HalfH;             // offset 4
    uint   ScreenW;           // offset 8
    uint   ScreenH;           // offset 12
    float  NearZ;             // offset 16
    float  FarZ;              // offset 20
    float  TemporalAlpha;     // offset 24  (blend weight, 0.2 default)
    float  DepthThreshold;    // offset 28  (edge-stopping threshold)
    uint   FrameIndex;        // offset 32
    float3 pad0;              // offset 36-44  (pad to 48 bytes)
};

// t0 = Raw GI from trace pass (half-res)
// t1 = Previous denoised GI history (half-res, ping-pong read)
// t2 = HiZ depth (reversed-Z, full-res)
// t31 = Linear depth (R32_FLOAT, full-res)
Texture2D<float4>   GIInput       : register(t0);
Texture2D<float4>   HistoryTex    : register(t1);
Texture2D<float>    DepthTex      : register(t2);
Texture2D<float2>   MotionVectors : register(t3); // Per-pixel UV delta from MotionVectorGen
Texture2D<float>    LinearDepth   : register(t31);

RWTexture2D<float4> GIOutput      : register(u0);

SamplerState LinearSampler : register(s0);

#include "SharedRAW.hlsli"

// Bilateral weight from depth difference
float DepthWeight(float centerZ, float sampleZ, float threshold)
{
    float diff = abs(centerZ - sampleZ) / max(centerZ, 0.001);
    return exp(-diff * diff / (2.0 * threshold * threshold));
}

// 5x5 bilateral spatial filter with depth edge stopping
float4 BilateralFilter(uint2 px, float centerZ)
{
    float4 accumColor  = 0.0;
    float  accumWeight = 0.0;

    static const int KERNEL_RADIUS = 2;  // 5x5

    for (int dy = -KERNEL_RADIUS; dy <= KERNEL_RADIUS; ++dy)
    {
        for (int dx = -KERNEL_RADIUS; dx <= KERNEL_RADIUS; ++dx)
        {
            int2 samplePx = int2(px) + int2(dx, dy);

            // Clamp to texture bounds
            samplePx = clamp(samplePx, int2(0, 0), int2(HalfW - 1, HalfH - 1));

            // Read sample GI
            float4 sampleGI = GIInput.Load(int3(samplePx, 0));

            // Read depth at corresponding full-res pixel
            int2 fullPx = samplePx * 2;
            float sampleRawZ = DepthTex.Load(int3(fullPx, 0));
            float sampleZ    = LinearizeDepth(max(sampleRawZ, 0.0001), NearZ, FarZ);

            // Spatial Gaussian weight
            float spatialDist = float(dx * dx + dy * dy);
            float spatialW    = exp(-spatialDist / 4.5);  // sigma ~ 1.5 pixels

            // Depth edge-stopping weight
            float depthW = DepthWeight(centerZ, sampleZ, DepthThreshold);

            float w = spatialW * depthW;
            accumColor  += sampleGI * w;
            accumWeight += w;
        }
    }

    return (accumWeight > 0.001) ? accumColor / accumWeight : GIInput.Load(int3(px, 0));
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // Bounds check (half-res)
    if (DTid.x >= HalfW || DTid.y >= HalfH)
        return;

    // Read depth for center pixel
    int2  fullPx   = int2(DTid.xy) * 2;
    float rawDepth = DepthTex.Load(int3(fullPx, 0));

    // Sky check: reversed-Z sky near 0 — pass through zero
    if (rawDepth < 0.0001)
    {
        GIOutput[DTid.xy] = float4(0, 0, 0, 0);
        return;
    }

    float centerZ = LinearizeDepth(rawDepth, NearZ, FarZ);

    // ── Spatial bilateral filter ─────────────────────────────────────────
    float4 spatialFiltered = BilateralFilter(DTid.xy, centerZ);

    // ── Temporal accumulation with motion vector reprojection ────────────
    if (FrameIndex < 3)
    {
        GIOutput[DTid.xy] = spatialFiltered;
        return;
    }

    // Reproject via motion vectors (UV-space, resolution-independent)
    float2 uv = (float2(DTid.xy) + 0.5) / float2(HalfW, HalfH);
    float2 motion = MotionVectors.SampleLevel(LinearSampler, uv, 0);
    float2 prevUV = uv - motion;

    float4 history;
    bool valid = all(prevUV >= 0.0) && all(prevUV <= 1.0);

    if (valid)
        history = HistoryTex.SampleLevel(LinearSampler, prevUV, 0);
    else
        history = spatialFiltered;

    // 3x3 neighborhood clamping (SVGF-style anti-ghosting)
    float4 minVal = spatialFiltered, maxVal = spatialFiltered;
    [unroll] for (int ny = -1; ny <= 1; ny++)
        [unroll] for (int nx = -1; nx <= 1; nx++)
        {
            float4 nv = GIInput.Load(int3(clamp(int2(DTid.xy) + int2(nx, ny), 0, int2(HalfW - 1, HalfH - 1)), 0));
            minVal = min(minVal, nv);
            maxVal = max(maxVal, nv);
        }
    if (valid)
        history = clamp(history, minVal, maxVal);

    // Adaptive alpha: large difference = faster convergence
    float histLuma    = dot(history.rgb, float3(0.2126, 0.7152, 0.0722));
    float currentLuma = dot(spatialFiltered.rgb, float3(0.2126, 0.7152, 0.0722));
    float lumaDiff    = abs(histLuma - currentLuma) / max(max(histLuma, currentLuma), 0.001);
    float alpha = valid ? TemporalAlpha : 1.0;
    if (lumaDiff > 0.3)
        alpha = saturate(alpha + lumaDiff * 0.5);

    float4 result = lerp(history, spatialFiltered, alpha);
    result = max(result, 0.0);

    // NaN/Inf guard: prevent corruption propagating through ping-pong
    GIOutput[DTid.xy] = any(isnan(result)) || any(isinf(result)) ? spatialFiltered : result;
}
