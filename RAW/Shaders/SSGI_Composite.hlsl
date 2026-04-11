// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Pass 4: Bilateral upsample from half-res + additive blend
//
// Reads the denoised half-res GI buffer and upsamples to full resolution
// using a bilateral filter guided by full-res depth.  The pixel shader
// outputs upsampled GI color with alpha=1 for the additive blend state
// configured on the C++ side (SrcBlend=ONE, DestBlend=ONE).

cbuffer CompositeCB : register(b0)
{
    uint   ScreenW;        // offset 0   Full-res width
    uint   ScreenH;        // offset 4   Full-res height
    uint   HalfW;          // offset 8   Half-res width
    uint   HalfH;          // offset 12  Half-res height
    float  NearZ;          // offset 16
    float  FarZ;           // offset 20
    float  GIIntensity;    // offset 24  (NOT applied here — already in trace)
    float  pad0;           // offset 28
};

// t0 = Denoised half-res GI (from ping-pong history write)
// t1 = Full-res depth (HiZ, reversed-Z)
// t31 = Linear depth (R32_FLOAT, full-res)
Texture2D<float4> GITex       : register(t0);
Texture2D<float>  DepthTex    : register(t1);
Texture2D<float>  LinearDepth : register(t31);

SamplerState LinearSampler : register(s0);
SamplerState PointSampler  : register(s1);

struct PSInput
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

#include "SharedRAW.hlsli"

float4 main(PSInput input) : SV_Target
{
    // Full-res pixel coordinate
    int2 fullPx = int2(input.position.xy);

    // Read full-res depth
    float rawDepth = DepthTex.Load(int3(fullPx, 0));

    // Sky check: reversed-Z sky near 0 — output zero (no GI contribution)
    if (rawDepth < 0.0001)
        return float4(0, 0, 0, 1);

    float centerZ = LinearizeDepth(rawDepth, NearZ, FarZ);

    // First-person skip
    if (centerZ < 16.0)
        return float4(0, 0, 0, 1);

    // ── Bilateral upsample from half-res ─────────────────────────────────
    // Sample a 2x2 neighborhood in the half-res GI buffer, weight by
    // depth similarity to the full-res center pixel.
    float2 halfUV = (float2(fullPx) + 0.5) / float2(ScreenW, ScreenH);
    float2 halfTexelSize = 1.0 / float2(HalfW, HalfH);

    // Half-res pixel coordinate (fractional)
    float2 halfPxF = halfUV * float2(HalfW, HalfH) - 0.5;
    int2   halfPx0 = int2(floor(halfPxF));
    float2 frac2   = halfPxF - float2(halfPx0);

    float4 accumGI     = 0.0;
    float  accumWeight = 0.0;

    // 2x2 bilinear tap with depth-guided weighting
    for (int dy = 0; dy <= 1; ++dy)
    {
        for (int dx = 0; dx <= 1; ++dx)
        {
            int2 samplePx = halfPx0 + int2(dx, dy);
            samplePx = clamp(samplePx, int2(0, 0), int2(HalfW - 1, HalfH - 1));

            // Read half-res GI
            float4 sampleGI = GITex.Load(int3(samplePx, 0));

            // Read depth at corresponding full-res pixel
            int2 sampleFullPx = samplePx * 2;
            sampleFullPx = clamp(sampleFullPx, int2(0, 0), int2(ScreenW - 1, ScreenH - 1));
            float sampleRawZ = DepthTex.Load(int3(sampleFullPx, 0));
            float sampleZ    = LinearizeDepth(max(sampleRawZ, 0.0001), NearZ, FarZ);

            // Bilinear weight
            float bx = (dx == 0) ? (1.0 - frac2.x) : frac2.x;
            float by = (dy == 0) ? (1.0 - frac2.y) : frac2.y;
            float bilinearW = bx * by;

            // Depth similarity weight (edge stopping)
            float depthDiff = abs(sampleZ - centerZ) / max(centerZ, 0.001);
            float depthW    = exp(-depthDiff * depthDiff * 50.0);

            float w = bilinearW * depthW;
            accumGI     += sampleGI * w;
            accumWeight += w;
        }
    }

    float3 gi = (accumWeight > 0.001) ? accumGI.rgb / accumWeight : 0.0;

    // GI intensity already applied in trace pass — do NOT multiply again here.
    // Output with alpha=1 for additive blend state (SrcBlend=ONE, DestBlend=ONE).
    return float4(max(gi, 0.0), 1.0);
}
