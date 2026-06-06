// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Bright pixel extraction with Karis anti-firefly weighting.
// Reference: Brian Karis, "Tone Mapping" (UE4 Course Notes, SIGGRAPH 2013).
//
// Downsample source to half-res. For each 2x2 block, compute a weighted
// average where weight = 1/(1+luminance) to prevent single bright pixels
// from dominating the bloom. Apply soft-knee threshold with quadratic
// interpolation in the knee region.

cbuffer ExtractCB : register(b0)
{
    uint2  SrcDims;         // Source (backbuffer) dimensions
    uint2  DstDims;         // Destination (half-res) dimensions
    float  Threshold;       // Brightness threshold
    float  Knee;            // Soft-knee width (0 = hard cutoff)
    float  pad0, pad1;
}

Texture2D<float4> SrcTex : register(t0);
SamplerState LinearSampler : register(s0);
RWTexture2D<float4> DstTex : register(u0);

#include "SharedRAW.hlsli"

// Karis weight: suppress fireflies by weighting inversely with brightness
float KarisWeight(float3 c)
{
    return 1.0 / (1.0 + Luminance(c));
}

// Reversible Reinhard (inverse + forward) — preserves HDR highlight color ratios
// Reference: Cinematic DOF v1.2.1, Frans Bouma
float3 InverseReinhard(float3 c)
{
    return c / max(1.0 - Luminance(c), 1e-6);
}

float3 ForwardReinhard(float3 c)
{
    return c / (1.0 + Luminance(c));
}

// Soft-knee threshold: quadratic blend in the knee region
// Returns a multiplier in [0,1] applied to the color after extraction.
float3 SoftThreshold(float3 c)
{
    float brightness = Luminance(c);

    // Below threshold-knee: fully rejected
    // Above threshold+knee: fully accepted
    // In between: quadratic interpolation
    float lower = Threshold - Knee;
    float upper = Threshold + Knee;

    // Soft curve in the knee region
    float soft = clamp((brightness - lower) / (upper - lower + 1e-6), 0.0, 1.0);
    soft = soft * soft; // quadratic ease-in

    // Hard threshold contribution
    float hard = step(Threshold, brightness);

    // Blend: use soft curve in knee region, hard above
    float contribution = (Knee > 0.0) ? soft : hard;

    return c * contribution;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= DstDims.x || DTid.y >= DstDims.y)
        return;

    // Each output pixel corresponds to a 2x2 block in the source.
    // Use bilinear sampling at the center of each source texel in the block,
    // then apply Karis weighting across the 4 samples.
    float2 srcTexelSize = 1.0 / float2(SrcDims);

    // Center of the 2x2 block in source UV space
    float2 baseUV = (float2(DTid.xy) * 2.0 + 1.0) * srcTexelSize;

    // Sample the 4 source texels in the 2x2 block
    float2 offsets[4] = {
        float2(-0.5, -0.5),
        float2( 0.5, -0.5),
        float2(-0.5,  0.5),
        float2( 0.5,  0.5)
    };

    float3 weightedSum = 0.0;
    float  totalWeight = 0.0;

    [unroll]
    for (int i = 0; i < 4; i++)
    {
        float2 uv = baseUV + offsets[i] * srcTexelSize;
        float3 sample_ = SrcTex.SampleLevel(LinearSampler, uv, 0).rgb;

        // Inverse Reinhard: recover HDR magnitude for correct bloom color ratios
        sample_ = InverseReinhard(max(sample_, 0.0));

        // Apply Karis anti-firefly weight
        float w = KarisWeight(sample_);
        weightedSum += sample_ * w;
        totalWeight += w;
    }

    float3 avg = weightedSum / (totalWeight + 1e-6);

    // Apply soft-knee threshold to extract only bright pixels
    float3 bright = SoftThreshold(avg);

    // Forward Reinhard: compress back to [0,1) range for bloom convolution
    bright = ForwardReinhard(bright);

    DstTex[DTid.xy] = float4(bright, 1.0);
}
