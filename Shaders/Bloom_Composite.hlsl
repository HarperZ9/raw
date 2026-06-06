// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Final bloom composite pixel shader.
// Reads all bloom mip levels, applies per-mip weighting, optional chromatic
// shift, optional anamorphic streak blend, and outputs the combined bloom
// texture scaled by overall intensity. The result is written to the bloom
// output RT, which is later composited with the scene by the pipeline.

cbuffer CompositeCB : register(b0)
{
    float4 MipWeights;      // Per-mip blend weights (x=half, y=quarter, z=eighth, w=sixteenth)
    float  Intensity;       // Overall bloom intensity
    float  ColorTemp;       // Color temperature in Kelvin (6500 = neutral)
    float  ChromaticShift;  // R/B channel shift per mip (0.0-0.02)
    float  AnamorphicBlend; // Anamorphic streak blend factor (0 = off)
    float4 Padding;
}

Texture2D<float4> Mip0 : register(t0);     // Half-res upsample
Texture2D<float4> Mip1 : register(t1);     // Quarter-res upsample
Texture2D<float4> Mip2 : register(t2);     // Eighth-res upsample
Texture2D<float4> Mip3 : register(t3);     // Sixteenth-res upsample
Texture2D<float4> AnamorphicTex : register(t4); // Anamorphic streak (half-res)
SamplerState LinearSampler : register(s0);

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

float4 main(VSOut input) : SV_Target
{
    float2 uv = input.uv;

    // Sample each mip level with optional chromatic shift.
    // ChromaticShift offsets the R and B channels outward from center
    // at increasing amounts for higher (smaller) mip levels, simulating
    // lens chromatic aberration in the bloom.
    float3 bloom = 0.0;

    // Mip 0 (half-res) -- minimal chromatic shift
    {
        float shift = ChromaticShift * 1.0;
        float2 dir = uv - 0.5;
        float r = Mip0.SampleLevel(LinearSampler, uv + dir * shift, 0).r;
        float g = Mip0.SampleLevel(LinearSampler, uv, 0).g;
        float b = Mip0.SampleLevel(LinearSampler, uv - dir * shift, 0).b;
        bloom += float3(r, g, b) * MipWeights.x;
    }

    // Mip 1 (quarter-res)
    {
        float shift = ChromaticShift * 2.0;
        float2 dir = uv - 0.5;
        float r = Mip1.SampleLevel(LinearSampler, uv + dir * shift, 0).r;
        float g = Mip1.SampleLevel(LinearSampler, uv, 0).g;
        float b = Mip1.SampleLevel(LinearSampler, uv - dir * shift, 0).b;
        bloom += float3(r, g, b) * MipWeights.y;
    }

    // Mip 2 (eighth-res)
    {
        float shift = ChromaticShift * 3.0;
        float2 dir = uv - 0.5;
        float r = Mip2.SampleLevel(LinearSampler, uv + dir * shift, 0).r;
        float g = Mip2.SampleLevel(LinearSampler, uv, 0).g;
        float b = Mip2.SampleLevel(LinearSampler, uv - dir * shift, 0).b;
        bloom += float3(r, g, b) * MipWeights.z;
    }

    // Mip 3 (sixteenth-res)
    {
        float shift = ChromaticShift * 4.0;
        float2 dir = uv - 0.5;
        float r = Mip3.SampleLevel(LinearSampler, uv + dir * shift, 0).r;
        float g = Mip3.SampleLevel(LinearSampler, uv, 0).g;
        float b = Mip3.SampleLevel(LinearSampler, uv - dir * shift, 0).b;
        bloom += float3(r, g, b) * MipWeights.w;
    }

    // Optional anamorphic streak blend
    if (AnamorphicBlend > 0.0)
    {
        float3 streak = AnamorphicTex.SampleLevel(LinearSampler, uv, 0).rgb;
        bloom += streak * AnamorphicBlend;
    }

    // Apply overall bloom intensity
    bloom = max(bloom * Intensity, 0.0);

    // Inverse Reinhard: recover HDR magnitude from the forward Reinhard applied in Extract.
    // This preserves highlight color ratios through the bloom convolution chain.
    float bloomLuma = dot(bloom, float3(0.2126, 0.7152, 0.0722));
    if (bloomLuma > 0.001)
        bloom = bloom / max(1.0 - bloomLuma, 0.05); // Floor at 0.05 prevents extreme values

    // Safety clamp: prevent bloom from exceeding reasonable HDR range
    bloom = min(bloom, 64.0);

    return float4(bloom, 1.0);
}
