// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Pass 3: Composite.
// Upsamples the quarter-res scattered light result to full resolution
// using bilinear filtering and additively blends it onto the backbuffer.
// Depth-aware rejection prevents light bleeding across depth edges.

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

Texture2D<float4> tScatter : register(t0);
Texture2D<float>  tDepth   : register(t1);
SamplerState sLinear : register(s0);
RWTexture2D<float4> uBackbuffer : register(u0);

// Linearize reversed-Z depth.
float LinearizeDepth(float z)
{
    return nearZ * farZ / (nearZ + z * (farZ - nearZ));
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= fullWidth || DTid.y >= fullHeight)
        return;

    // Depth — HiZ reversed-Z.
    float rawDepth = tDepth.Load(int3(DTid.xy, 0));

    // Sky check (reversed-Z: sky ~ 0).
    if (rawDepth < 0.0001)
        return;

    // First-person geometry skip.
    float linearZ = LinearizeDepth(rawDepth);
    if (linearZ < 16.0)
        return;

    // Upsample: map full-res pixel to quarter-res UV.
    float2 quarterUV = (float2(DTid.xy) + 0.5) / float2(fullWidth, fullHeight);

    // Bilinear sample from quarter-res scatter result.
    float4 scatterSample = tScatter.SampleLevel(sLinear, quarterUV, 0);

    // Skip negligible contributions early.
    float scatterLuma = dot(scatterSample.rgb, float3(0.299, 0.587, 0.114));
    if (scatterLuma < 0.001)
        return;

    // Depth-aware edge rejection: compare full-res depth against the
    // quarter-res neighbourhood to prevent light bleeding at edges.
    // Sample 4 nearest quarter-res depth values.
    float2 quarterPixel = quarterUV * float2(quarterWidth, quarterHeight) - 0.5;
    int2 baseQP = int2(floor(quarterPixel));

    float depthReject = 0.0;
    float totalW = 0.0;
    for (int dy = 0; dy <= 1; ++dy)
    {
        for (int dx = 0; dx <= 1; ++dx)
        {
            int2 qCoord = baseQP + int2(dx, dy);
            qCoord = clamp(qCoord, int2(0, 0),
                           int2(quarterWidth - 1, quarterHeight - 1));

            // Map back to full-res for depth comparison.
            uint2 fullCoord = uint2(qCoord) * 4 + uint2(2, 2);
            fullCoord = min(fullCoord, uint2(fullWidth - 1, fullHeight - 1));
            float sampleDepth = tDepth.Load(int3(fullCoord, 0));
            float sampleLinearZ = LinearizeDepth(max(sampleDepth, 0.0001));

            float depthDiff = abs(linearZ - sampleLinearZ);
            float w = exp(-depthDiff * depthDiff /
                           max(depthTolerance * depthTolerance, 0.01));
            depthReject += w;
            totalW += 1.0;
        }
    }
    depthReject = (totalW > 0.0) ? (depthReject / totalW) : 1.0;

    // Additive blend onto backbuffer, modulated by depth edge rejection.
    float4 sceneColor = uBackbuffer[DTid.xy];
    float3 addLight = scatterSample.rgb * intensity * depthReject;

    uBackbuffer[DTid.xy] = float4(sceneColor.rgb + addLight, sceneColor.a);
}
