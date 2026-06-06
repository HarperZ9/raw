// Temporal accumulation — single-channel float
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Reprojects current pixel to previous frame via InvViewProj + PrevViewProj.
// On successful reprojection, blends current value with history using
// BlendAlpha. On disocclusion (depth mismatch or off-screen), resets to
// current-frame value. Uses neighborhood clamping (3x3 min/max) to
// suppress ghosting from stale history.
//
// Previous-frame depth uses raw reversed-Z (near=1, far=0) and is
// linearized inline since no pre-computed buffer exists for it.

cbuffer TemporalCB : register(b0)
{
    uint2    ScreenDims;
    float    NearZ;
    float    FarZ;
    float4x4 PrevViewProj;   // Previous frame's view-projection matrix
    float4x4 InvViewProj;    // Current frame's inverse view-projection
    float    BlendAlpha;      // Base blend factor (e.g. 0.05 for AO, 0.1 for SSR)
    float    DepthRejectThreshold;  // Relative depth difference for disocclusion
    float    pad0;
    float    pad1;
}

SamplerState PointSampler  : register(s0);
SamplerState LinearSampler : register(s1);
Texture2D<float> CurrentInput : register(t0);  // Current frame noisy input
Texture2D<float> HistoryInput : register(t1);  // Previous frame output
Texture2D<float> DepthTex     : register(t2);  // Current depth (reversed-Z)
Texture2D<float> PrevDepthTex : register(t3);  // Previous frame depth (reversed-Z)
Texture2D<float> LinearDepth  : register(t31); // Pre-computed linearized depth (current frame)
RWTexture2D<float> Output     : register(u0);

// Linearize reversed-Z depth: viewZ = N*F / (N + z*(F-N))
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

    float currentValue = CurrentInput.Load(int3(pixelCoord, 0));
    float currentLinZ  = LinearDepth.Load(int3(pixelCoord, 0));

    // Sky pixels: just output current value, no accumulation
    if (currentLinZ <= 0.0)
    {
        Output[DTid.xy] = currentValue;
        return;
    }

    // ── Reproject to previous frame ──────────────────────────────────
    // Reconstruct clip-space position from UV + reversed-Z depth
    float rawDepth = DepthTex.Load(int3(pixelCoord, 0));
    float2 ndc = float2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);
    float4 clipPos = float4(ndc, rawDepth, 1.0);

    // Unproject to world space
    float4 worldPos = mul(clipPos, InvViewProj);
    worldPos /= worldPos.w;

    // Project to previous frame clip space
    float4 prevClip = mul(worldPos, PrevViewProj);
    float2 prevNDC  = prevClip.xy / prevClip.w;
    float2 prevUV   = float2(prevNDC.x * 0.5 + 0.5, 0.5 - prevNDC.y * 0.5);

    // ── Disocclusion detection ───────────────────────────────────────
    bool valid = true;

    // Off-screen rejection
    if (any(prevUV < 0.0) || any(prevUV > 1.0))
        valid = false;

    float historyValue = currentValue;  // fallback

    if (valid)
    {
        // Sample previous-frame depth at reprojected location
        int2 prevPixel = int2(prevUV * screenSize);
        prevPixel = clamp(prevPixel, int2(0, 0), int2(ScreenDims) - int2(1, 1));
        float prevRawZ = PrevDepthTex.Load(int3(prevPixel, 0));
        float prevLinZ = LinearizeDepth(prevRawZ);

        // Depth-based disocclusion: relative difference check
        float depthRatio = abs(currentLinZ - prevLinZ) / max(currentLinZ, 1e-6);
        if (depthRatio > DepthRejectThreshold)
            valid = false;
    }

    if (valid)
    {
        // Bilinear sample history at reprojected UV
        historyValue = HistoryInput.SampleLevel(LinearSampler, prevUV, 0);

        // ── Neighborhood clamping (3x3) ──────────────────────────────
        // Prevents ghosting by clamping history to the local value range
        float minVal = currentValue;
        float maxVal = currentValue;

        [unroll]
        for (int dy = -1; dy <= 1; dy++)
        {
            [unroll]
            for (int dx = -1; dx <= 1; dx++)
            {
                int2 np = clamp(pixelCoord + int2(dx, dy),
                                int2(0, 0), int2(ScreenDims) - int2(1, 1));
                float nv = CurrentInput.Load(int3(np, 0));
                minVal = min(minVal, nv);
                maxVal = max(maxVal, nv);
            }
        }

        historyValue = clamp(historyValue, minVal, maxVal);

        // Blend: small alpha = slow accumulation (more temporal stability)
        Output[DTid.xy] = lerp(historyValue, currentValue, BlendAlpha);
    }
    else
    {
        // Disoccluded: reset to current frame
        Output[DTid.xy] = currentValue;
    }
}
