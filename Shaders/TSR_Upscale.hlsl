// Copyright (c) 2026 Zain D. Harper. All rights reserved.
// Temporal super-resolution upscaler: jittered rendering + temporal accumulation.
// Reprojects history with motion vectors, applies AABB variance clipping,
// blends with bicubic Catmull-Rom history sampling, anti-ghosting via luminance rejection.

cbuffer TSRUpscaleCB : register(b0)
{
    uint2  RenderDims;         // render resolution (lower)
    uint2  DisplayDims;        // display resolution (output)
    float2 JitterOffset;       // current sub-pixel jitter (NDC)
    uint   FrameIndex;         // monotonic frame counter
    float  pad0;
    float4 MaterialHistoryWeight;  // per-material history blend: [default, arch, foliage, skin]
    float2 MotionScale;        // motion vector UV scale
    float2 RcpRenderDims;      // 1.0 / RenderDims
    float2 RcpDisplayDims;     // 1.0 / DisplayDims
    float  RenderToDisplayX;   // DisplayDims.x / RenderDims.x
    float  RenderToDisplayY;   // DisplayDims.y / RenderDims.y
}

Texture2D<float4>   CurrentFrame   : register(t0);  // render-res current frame
Texture2D<float4>   HistoryBuffer  : register(t1);  // display-res previous output
Texture2D<float2>   MotionVectors  : register(t2);  // screen-space motion (UV delta)
Texture2D<float>    DepthBuffer    : register(t3);  // scene depth (0..1)
Texture2D<uint>     MaterialBuffer : register(t4);  // material classification (R8_UINT, t25)
RWTexture2D<float4> OutputColor    : register(u0);  // display-res upscaled result
SamplerState LinearClamp : register(s0);
SamplerState PointClamp  : register(s1);

float Luminance(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

// Catmull-Rom bicubic filter weight
float CatmullRomWeight(float x)
{
    float ax = abs(x);
    if (ax < 1.0)
        return (1.5 * ax - 2.5) * ax * ax + 1.0;
    else if (ax < 2.0)
        return ((-0.5 * ax + 2.5) * ax - 4.0) * ax + 2.0;
    return 0.0;
}

// Bicubic Catmull-Rom sample from history buffer
float4 SampleHistoryCatmullRom(float2 uv)
{
    float2 texelPos = uv * float2(DisplayDims) - 0.5;
    float2 texelFloor = floor(texelPos);
    float2 f = texelPos - texelFloor;

    float4 result = 0.0;
    float  totalWeight = 0.0;

    [unroll]
    for (int y = -1; y <= 2; y++)
    {
        [unroll]
        for (int x = -1; x <= 2; x++)
        {
            int2 coord = int2(texelFloor) + int2(x, y);
            coord = clamp(coord, int2(0, 0), int2(DisplayDims) - 1);

            float wx = CatmullRomWeight(float(x) - f.x);
            float wy = CatmullRomWeight(float(y) - f.y);
            float w = wx * wy;

            result += HistoryBuffer.Load(int3(coord, 0)) * w;
            totalWeight += w;
        }
    }

    return result / max(totalWeight, 1e-6);
}

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= DisplayDims.x || dtid.y >= DisplayDims.y)
        return;

    // Display-res UV
    float2 displayUV = (float2(dtid.xy) + 0.5) * RcpDisplayDims;

    // ── Sample current frame at render resolution ───────────────────
    // Map display UV to render-res UV (accounting for jitter)
    float2 renderUV = displayUV - JitterOffset * RcpRenderDims;
    float4 currentColor = CurrentFrame.SampleLevel(LinearClamp, renderUV, 0);

    // ── Reproject: where was this pixel in the previous frame? ──────
    float2 motionVec = MotionVectors.SampleLevel(LinearClamp, displayUV, 0);
    float2 historyUV = displayUV - motionVec * MotionScale;

    // ── Sample history with bicubic Catmull-Rom ─────────────────────
    float4 historyColor;
    bool historyValid = true;

    if (any(historyUV < 0.0) || any(historyUV > 1.0))
    {
        // Out of bounds: no valid history
        historyValid = false;
        historyColor = currentColor;
    }
    else
    {
        historyColor = SampleHistoryCatmullRom(historyUV);
    }

    // ── Neighborhood clipping (AABB variance clip) ──────────────────
    // Gather a 3x3 neighborhood from the current frame to compute color AABB
    float3 neighborMin = float3(1e10, 1e10, 1e10);
    float3 neighborMax = float3(-1e10, -1e10, -1e10);
    float3 neighborMean = 0.0;
    float3 neighborM2 = 0.0;

    [unroll]
    for (int dy = -1; dy <= 1; dy++)
    {
        [unroll]
        for (int dx = -1; dx <= 1; dx++)
        {
            float2 sampleUV = displayUV + float2(dx, dy) * RcpRenderDims;
            float3 s = CurrentFrame.SampleLevel(PointClamp, sampleUV, 0).rgb;

            neighborMin = min(neighborMin, s);
            neighborMax = max(neighborMax, s);
            neighborMean += s;
            neighborM2 += s * s;
        }
    }

    neighborMean /= 9.0;
    neighborM2 /= 9.0;
    float3 variance = max(neighborM2 - neighborMean * neighborMean, 0.0);
    float3 sigma = sqrt(variance);

    // Variance-based AABB clip: tighter than min/max, reduces ghosting
    float3 clipMin = neighborMean - sigma * 1.25;
    float3 clipMax = neighborMean + sigma * 1.25;

    // Clip history to the AABB
    float3 clippedHistory = clamp(historyColor.rgb, clipMin, clipMax);

    // ── Anti-ghosting: luminance-based rejection ────────────────────
    float lumCurrent = Luminance(currentColor.rgb);
    float lumHistory = Luminance(clippedHistory);
    float lumDiff = abs(lumCurrent - lumHistory);

    // Large luminance difference indicates ghosting — reduce history weight
    float ghostRejection = saturate(lumDiff * 4.0);

    // ── Material-aware history weight ───────────────────────────────
    float historyWeight = MaterialHistoryWeight.x; // Default blend
    uint materialID = MaterialBuffer.Load(int3(
        int2(renderUV * float2(RenderDims)), 0));

    // Select per-material weight (0=default, 1=architecture, 2=foliage, 3=skin)
    if (materialID == 1) historyWeight = MaterialHistoryWeight.y;
    else if (materialID == 2) historyWeight = MaterialHistoryWeight.z;
    else if (materialID == 3) historyWeight = MaterialHistoryWeight.w;

    // Reduce history contribution when ghosting is detected
    historyWeight = lerp(historyWeight, 0.0, ghostRejection);

    // First frame or invalid history: use current frame directly
    if (!historyValid || FrameIndex == 0)
        historyWeight = 0.0;

    // ── Blend current + clipped history ─────────────────────────────
    float3 result = lerp(currentColor.rgb, clippedHistory, historyWeight);

    OutputColor[dtid.xy] = float4(result, 1.0);
}
