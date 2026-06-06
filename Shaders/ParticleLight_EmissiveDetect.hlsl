// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Pass 1: Emissive Detection.
// Reads full-resolution backbuffer copy, identifies bright emissive
// pixels (luminance above threshold), and downsamples them into
// a quarter-resolution emissive buffer using a 4x4 block max filter.

cbuffer ParticleLightCB : register(b0)
{
    float  luminanceThreshold;
    float  intensity;
    float  scatterRadius;
    float  falloffExponent;
    float  depthTolerance;
    float  nearZ;
    float  farZ;
    uint   fullWidth;
    uint   fullHeight;
    uint   quarterWidth;
    uint   quarterHeight;
    uint   frameIndex;
    float4x4 invViewProj;
    float3 cameraPos;
    float  pad;
}

Texture2D<float4> tBackbuffer : register(t0);
Texture2D<uint>   tMaterialID : register(t1);
RWTexture2D<float4> uEmissive : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= quarterWidth || DTid.y >= quarterHeight)
        return;

    // Map quarter-res pixel to full-res 4x4 block origin.
    uint2 baseCoord = DTid.xy * 4;

    // Max-luminance filter across the 4x4 block: pick the brightest
    // emissive pixel to preserve small point lights.
    float  maxLuma = 0.0;
    float3 maxColor = float3(0, 0, 0);

    for (uint dy = 0; dy < 4; ++dy)
    {
        for (uint dx = 0; dx < 4; ++dx)
        {
            uint2 sampleCoord = baseCoord + uint2(dx, dy);
            if (sampleCoord.x >= fullWidth || sampleCoord.y >= fullHeight)
                continue;

            float4 color = tBackbuffer.Load(int3(sampleCoord, 0));
            float luma = dot(color.rgb, float3(0.299, 0.587, 0.114));

            if (luma > maxLuma)
            {
                maxLuma = luma;
                maxColor = color.rgb;
            }
        }
    }

    // Threshold: only keep pixels above the emissive luminance cutoff.
    float emissive = saturate((maxLuma - luminanceThreshold) /
                               max(1.0 - luminanceThreshold, 0.001));

    // Soft-knee: smooth the threshold transition.
    emissive = emissive * emissive;

    float3 result = maxColor * emissive;
    uEmissive[DTid.xy] = float4(result, emissive);
}
