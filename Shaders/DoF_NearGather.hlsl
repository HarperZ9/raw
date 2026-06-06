// Physical thin-lens depth-of-field -- Pass 4: Near-field bokeh gather
// Reference: Potmesil & Chakravarty 1981, Nilsson 2012 (near-field dilation)
// Copyright (c) 2026 Zain D. Harper. All rights reserved.

cbuffer NearGatherCB : register(b0)
{
    uint2  HalfDims;
    uint2  FullDims;
    float  MaxBokehRadius;
    int    BladeCount;
    float  Roundness;
    float  CatEyeAmount;
    float  AnamorphicRatio;
    float  SphericalAberr;
    int    SampleCount;
    float  BokehRotation;
}

static const float PI = 3.14159265359;
static const float GOLDEN_ANGLE = 2.39996322973;

Texture2D<float4> SceneColor : register(t0);
Texture2D<float2> CoCMap     : register(t1);
Texture2D<float2> TileMap    : register(t2);
SamplerState LinearSamp : register(s0);
RWTexture2D<float4> NearOutput : register(u0);  // Half-res RGBA16F (rgb=color, a=near weight)

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (any(DTid.xy >= HalfDims))
    {
        return;
    }

    // Map half-res pixel to full-res center
    float2 fullCenter = float2(DTid.xy) * 2.0 + 1.0;

    // Read center pixel CoC
    float2 centerCoC = CoCMap.Load(int3((int2)fullCenter, 0));
    float  centerNearCoC = -min(centerCoC.x, 0.0);  // Flip sign: near CoC becomes positive

    // Read tile min CoC for near-field dilation (Nilsson 2012).
    // The tile stores the most-negative CoC; we use its absolute value as the
    // dilated gather radius so near-field objects bleed into focused regions.
    uint2 tileIdx = (uint2)fullCenter / 16;
    float2 tileCoC = TileMap.Load(int3(tileIdx, 0));
    float  tileNearCoC = -min(tileCoC.x, 0.0);  // Most negative -> positive magnitude

    // Use dilated tile CoC for gather radius (prevents sharp near-field edges)
    float gatherRadius = max(tileNearCoC, centerNearCoC);
    gatherRadius = min(gatherRadius, MaxBokehRadius);

    // Early out: no near-field blur in this tile
    if (gatherRadius < 0.5)
    {
        NearOutput[DTid.xy] = float4(0, 0, 0, 0);
        return;
    }

    int numSamples = SampleCount;

    // Disc gather using Fibonacci spiral
    float3 colorSum = float3(0, 0, 0);
    float  weightSum = 0.0;
    float  nearAlphaSum = 0.0;

    float2 invFullDims = 1.0 / float2(FullDims);

    for (int i = 0; i < numSamples; i++)
    {
        // Fibonacci spiral point
        float t = (float(i) + 0.5) / float(numSamples);
        float r = sqrt(t);
        float theta = float(i) * GOLDEN_ANGLE + BokehRotation;

        float2 offset;
        offset.x = r * cos(theta);
        offset.y = r * sin(theta);

        // Anamorphic stretch
        offset.x *= 1.0 - AnamorphicRatio * 0.5;

        // Scale by gather radius and convert to UV
        float2 sampleUV = (fullCenter + offset * gatherRadius + 0.5) * invFullDims;
        sampleUV = clamp(sampleUV, invFullDims, 1.0 - invFullDims);

        // Read sample color and CoC
        float4 sampleColor = SceneColor.SampleLevel(LinearSamp, sampleUV, 0);
        float2 sampleCoC   = CoCMap.Load(int3(
            (int2)(sampleUV * float2(FullDims)), 0));

        // Near-field: negative CoC means closer than focus plane
        float sampleNearCoC = -min(sampleCoC.x, 0.0);  // positive magnitude

        // Disc membership: sample must be in near field to contribute.
        // Near-field objects scatter into focused areas, so we accept samples
        // that have significant near CoC.
        float membership = saturate(sampleNearCoC * 2.0);

        // Gaussian falloff from disc center
        float gaussian = exp(-2.0 * r * r);

        // Spherical aberration (ring bokeh)
        float ringWeight = lerp(1.0, r, SphericalAberr);

        float w = membership * gaussian * ringWeight;
        w = max(w, 1e-5);

        colorSum    += sampleColor.rgb * w;
        weightSum   += w;
        nearAlphaSum += membership;
    }

    float3 result = (weightSum > 1e-5) ? (colorSum / weightSum) : float3(0, 0, 0);

    // Near-field alpha: fraction of samples that were in the near field,
    // weighted by the gather radius relative to max.  This controls how
    // strongly the near field overlaps the final composite.
    float nearAlpha = (nearAlphaSum / float(numSamples))
                    * saturate(gatherRadius / max(MaxBokehRadius, 1.0));

    NearOutput[DTid.xy] = float4(result, saturate(nearAlpha));
}
