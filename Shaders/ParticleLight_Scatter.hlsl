// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Pass 2: Light Scatter.
// Reads quarter-res emissive detection buffer and performs a radial
// blur / light scatter kernel.  Each texel gathers contributions from
// surrounding emissive sources, weighted by distance falloff and
// depth proximity (to prevent scatter across depth discontinuities).

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

Texture2D<float4> tEmissive : register(t0);
Texture2D<float>  tDepth    : register(t1);
RWTexture2D<float4> uScatter : register(u0);
SamplerState sLinear : register(s0);

// Linearize reversed-Z depth.
float LinearizeDepth(float z)
{
    return nearZ * farZ / (nearZ + z * (farZ - nearZ));
}

// Number of scatter samples along each ring.
static const uint SCATTER_SAMPLES = 16;
static const float PI = 3.14159265;

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= quarterWidth || DTid.y >= quarterHeight)
        return;

    // Map quarter-res texel to approximate full-res location for depth read.
    uint2 fullResCoord = DTid.xy * 4 + uint2(2, 2);
    fullResCoord = min(fullResCoord, uint2(fullWidth - 1, fullHeight - 1));

    float centerDepth = tDepth.Load(int3(fullResCoord, 0));

    // Sky check.
    if (centerDepth < 0.0001)
    {
        uScatter[DTid.xy] = float4(0, 0, 0, 0);
        return;
    }

    float centerLinearZ = LinearizeDepth(centerDepth);

    // Read this pixel's own emissive contribution.
    float4 selfEmissive = tEmissive.Load(int3(DTid.xy, 0));

    // Gather scattered light from surrounding emissive sources.
    float3 scatterAccum = float3(0, 0, 0);
    float  totalWeight  = 0.0;

    // Scatter radius in quarter-res pixels.
    float radiusPx = scatterRadius;

    for (uint ring = 1; ring <= 3; ++ring)
    {
        float r = radiusPx * (float(ring) / 3.0);

        for (uint s = 0; s < SCATTER_SAMPLES; ++s)
        {
            float angle = (float(s) / float(SCATTER_SAMPLES)) * 2.0 * PI;
            float2 offset = float2(cos(angle), sin(angle)) * r;

            float2 sampleUV = (float2(DTid.xy) + 0.5 + offset) /
                               float2(quarterWidth, quarterHeight);

            // Bounds check.
            if (sampleUV.x < 0.0 || sampleUV.x > 1.0 ||
                sampleUV.y < 0.0 || sampleUV.y > 1.0)
                continue;

            float4 emissiveSample = tEmissive.SampleLevel(sLinear, sampleUV, 0);

            // Depth proximity weighting: read depth at sample location.
            uint2 sampleFullRes = uint2(sampleUV * float2(fullWidth, fullHeight));
            sampleFullRes = min(sampleFullRes, uint2(fullWidth - 1, fullHeight - 1));
            float sampleDepth = tDepth.Load(int3(sampleFullRes, 0));
            float sampleLinearZ = LinearizeDepth(max(sampleDepth, 0.0001));

            float depthDiff = abs(centerLinearZ - sampleLinearZ);
            float depthWeight = exp(-depthDiff * depthDiff /
                                     max(depthTolerance * depthTolerance, 0.01));

            // Distance falloff.
            float dist = length(offset);
            float distWeight = pow(saturate(1.0 - dist / radiusPx), falloffExponent);

            float w = distWeight * depthWeight;
            scatterAccum += emissiveSample.rgb * w;
            totalWeight += w;
        }
    }

    if (totalWeight > 0.0)
        scatterAccum /= totalWeight;

    // Combine self-emission with scattered light.
    float3 result = selfEmissive.rgb + scatterAccum * intensity;
    uScatter[DTid.xy] = float4(result, 1.0);
}
