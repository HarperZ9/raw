// Physical thin-lens depth-of-field -- Pass 3: Far-field bokeh gather (disc blur)
// Reference: Potmesil & Chakravarty 1981, Jimenez 2014 (scatter-as-gather)
// Copyright (c) 2026 Zain D. Harper. All rights reserved.

cbuffer FarGatherCB : register(b0)
{
    uint2  HalfDims;        // Half-res output dimensions
    uint2  FullDims;        // Full-res source dimensions
    float  MaxBokehRadius;  // Max CoC in pixels (full-res)
    int    BladeCount;      // N-gon blade count (4-9)
    float  Roundness;       // 0=polygon, 1=circle
    float  CatEyeAmount;   // Cat-eye optical vignette (0-1)
    float  AnamorphicRatio; // Horizontal stretch (0=none, 1=full)
    float  SphericalAberr;  // Ring bokeh weight (0=uniform, 1=ring)
    int    SampleCount;     // Samples per pixel (48-128)
    float  BokehRotation;   // Rotation angle (radians)
}

static const float PI = 3.14159265359;
static const float GOLDEN_ANGLE = 2.39996322973;  // pi * (3 - sqrt(5))

Texture2D<float4> SceneColor : register(t0);  // Backbuffer copy (full-res)
Texture2D<float2> CoCMap     : register(t1);  // Full-res CoC (.x=signed, .y=abs)
Texture2D<float2> TileMap    : register(t2);  // Tile min/max CoC
SamplerState LinearSamp : register(s0);
RWTexture2D<float4> FarOutput : register(u0);  // Half-res RGBA16F

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (any(DTid.xy >= HalfDims))
        return;

    // Map half-res pixel to full-res center
    float2 fullCenter = float2(DTid.xy) * 2.0 + 1.0;

    // Read center pixel CoC (signed)
    float2 centerCoC = CoCMap.Load(int3((int2)fullCenter, 0));
    float  gatherCoC = centerCoC.x;  // signed: positive = far

    // Check tile for max far CoC -- early out if tile has no far blur
    uint2 tileIdx = (uint2)fullCenter / 16;
    float2 tileCoC = TileMap.Load(int3(tileIdx, 0));
    float  tileFarCoC = tileCoC.y;  // max positive CoC in tile

    // If this pixel and its tile have negligible far CoC, write sharp scene
    if (gatherCoC <= 0.5 && tileFarCoC <= 0.5)
    {
        float2 uv = (fullCenter + 0.5) / float2(FullDims);
        FarOutput[DTid.xy] = SceneColor.SampleLevel(LinearSamp, uv, 0);
        return;
    }

    // Gather radius in full-res pixels: use this pixel's far CoC
    float gatherRadius = max(gatherCoC, 0.0);
    gatherRadius = min(gatherRadius, MaxBokehRadius);

    // Scale to half-res for sampling offsets
    float halfRadius = gatherRadius * 0.5;

    // If radius is tiny, just sample center
    if (halfRadius < 0.5)
    {
        float2 uv = (fullCenter + 0.5) / float2(FullDims);
        FarOutput[DTid.xy] = SceneColor.SampleLevel(LinearSamp, uv, 0);
        return;
    }

    // Adaptive sample count based on CoC size
    int numSamples = SampleCount;

    // Disc gather using Fibonacci spiral (golden angle) for uniform coverage
    float3 colorSum = float3(0, 0, 0);
    float  weightSum = 0.0;

    float2 invFullDims = 1.0 / float2(FullDims);

    for (int i = 0; i < numSamples; i++)
    {
        // Fibonacci spiral: r = sqrt(i/N), theta = i * goldenAngle
        float t = (float(i) + 0.5) / float(numSamples);
        float r = sqrt(t);
        float theta = float(i) * GOLDEN_ANGLE + BokehRotation;

        float2 offset;
        offset.x = r * cos(theta);
        offset.y = r * sin(theta);

        // Apply anamorphic stretch (squeeze horizontally)
        offset.x *= 1.0 - AnamorphicRatio * 0.5;

        // Scale by gather radius and convert to UV space
        float2 sampleUV = (fullCenter + offset * gatherRadius + 0.5) * invFullDims;

        // Clamp to screen bounds
        sampleUV = clamp(sampleUV, invFullDims, 1.0 - invFullDims);

        // Read sample color and CoC
        float4 sampleColor = SceneColor.SampleLevel(LinearSamp, sampleUV, 0);
        float2 sampleCoC   = CoCMap.Load(int3(
            (int2)(sampleUV * float2(FullDims)), 0));

        // Disc membership test: a background sample should only contribute
        // if its own CoC is large enough to reach this pixel.
        // sampleCoC.x > 0 means the sample is in the far field.
        float sampleCoCFar = max(sampleCoC.x, 0.0);

        // Weight 1: disc membership -- sample's CoC must cover the distance
        //           from the sample to the gather center
        float sampleDist = r * gatherRadius;
        float membership = saturate(sampleCoCFar - sampleDist + 1.0);

        // Weight 2: Gaussian falloff from disc center (softer edges)
        float gaussian = exp(-2.0 * r * r);

        // Weight 3: spherical aberration (ring bokeh) -- boost edges of disc
        float ringWeight = lerp(1.0, r, SphericalAberr);

        float w = membership * gaussian * ringWeight;
        w = max(w, 1e-5);

        colorSum  += sampleColor.rgb * w;
        weightSum += w;
    }

    float3 result = (weightSum > 1e-5) ? (colorSum / weightSum) : float3(0, 0, 0);
    FarOutput[DTid.xy] = float4(result, 1.0);
}
