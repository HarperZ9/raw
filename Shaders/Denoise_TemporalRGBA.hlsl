// Temporal accumulation — RGBA float4
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Same reprojection + disocclusion + neighborhood-clamping logic as
// the single-channel variant, but operates on R16G16B16A16_FLOAT data.
// Neighborhood clamping is per-channel (AABB clamp in RGBA space).
// Previous-frame depth is linearized inline (no pre-computed buffer).

cbuffer TemporalCB : register(b0)
{
    uint2    ScreenDims;
    float    NearZ;
    float    FarZ;
    float4x4 PrevViewProj;
    float4x4 InvViewProj;
    float    BlendAlpha;
    float    DepthRejectThreshold;
    float    pad0;
    float    pad1;
}

SamplerState PointSampler  : register(s0);
SamplerState LinearSampler : register(s1);
Texture2D<float4> CurrentInput : register(t0);
Texture2D<float4> HistoryInput : register(t1);
Texture2D<float>  DepthTex     : register(t2);
Texture2D<float>  PrevDepthTex : register(t3);
Texture2D<float>  LinearDepth  : register(t31); // Pre-computed linearized depth (current frame)
RWTexture2D<float4> Output     : register(u0);

float LinearizeDepth(float z)
{
    return (NearZ * FarZ) / (NearZ + z * (FarZ - NearZ));
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (any(DTid.xy >= ScreenDims))
        return;

    int2   pixelCoord = int2(DTid.xy);
    float2 screenSize = float2(ScreenDims);
    float2 uv = (float2(pixelCoord) + 0.5) / screenSize;

    float4 currentValue = CurrentInput.Load(int3(pixelCoord, 0));
    float  currentLinZ  = LinearDepth.Load(int3(pixelCoord, 0));

    // Sky pixels: pass through
    if (currentLinZ <= 0.0)
    {
        Output[DTid.xy] = currentValue;
        return;
    }

    // ── Reproject to previous frame ──────────────────────────────────
    float rawDepth = DepthTex.Load(int3(pixelCoord, 0));
    float2 ndc = float2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);
    float4 clipPos = float4(ndc, rawDepth, 1.0);

    float4 worldPos = mul(clipPos, InvViewProj);
    worldPos /= worldPos.w;

    float4 prevClip = mul(worldPos, PrevViewProj);
    float2 prevNDC  = prevClip.xy / prevClip.w;
    float2 prevUV   = float2(prevNDC.x * 0.5 + 0.5, 0.5 - prevNDC.y * 0.5);

    // ── Disocclusion detection ───────────────────────────────────────
    bool valid = true;

    if (any(prevUV < 0.0) || any(prevUV > 1.0))
        valid = false;

    float4 historyValue = currentValue;

    if (valid)
    {
        int2 prevPixel = clamp(int2(prevUV * screenSize),
                               int2(0, 0), int2(ScreenDims) - int2(1, 1));
        float prevRawZ = PrevDepthTex.Load(int3(prevPixel, 0));
        float prevLinZ = LinearizeDepth(prevRawZ);

        float depthRatio = abs(currentLinZ - prevLinZ) / max(currentLinZ, 1e-6);
        if (depthRatio > DepthRejectThreshold)
            valid = false;
    }

    if (valid)
    {
        historyValue = HistoryInput.SampleLevel(LinearSampler, prevUV, 0);

        // ── Neighborhood clamping (3x3, per-channel AABB) ────────────
        float4 minVal = currentValue;
        float4 maxVal = currentValue;

        [unroll]
        for (int dy = -1; dy <= 1; dy++)
        {
            [unroll]
            for (int dx = -1; dx <= 1; dx++)
            {
                int2 np = clamp(pixelCoord + int2(dx, dy),
                                int2(0, 0), int2(ScreenDims) - int2(1, 1));
                float4 nv = CurrentInput.Load(int3(np, 0));
                minVal = min(minVal, nv);
                maxVal = max(maxVal, nv);
            }
        }

        historyValue = clamp(historyValue, minVal, maxVal);

        Output[DTid.xy] = lerp(historyValue, currentValue, BlendAlpha);
    }
    else
    {
        Output[DTid.xy] = currentValue;
    }
}
