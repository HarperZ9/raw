// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Progressive upsample using 9-tap tent (3x3 bilinear) filter.
// Reference: Jorge Jimenez, "Next Generation Post Processing in Call of Duty"
//            (SIGGRAPH 2014, Advances in Real-Time Rendering course).
//
// The tent filter produces a smooth, artifact-free upsample. The result is
// additively blended with the corresponding downsample mip to accumulate
// bloom across all mip levels. Per-mip spectral tinting approximates
// Rayleigh scattering (warm near-field, cool far-field).
//
// 3x3 tent kernel weights (sum = 1.0):
//   1/16  2/16  1/16
//   2/16  4/16  2/16
//   1/16  2/16  1/16

cbuffer UpsampleCB : register(b0)
{
    uint2  SrcDims;         // Source (smaller mip) dimensions
    uint2  DstDims;         // Destination (larger mip) dimensions
    float2 SrcTexelSize;    // 1.0 / SrcDims
    float  MipLevel;        // Current mip level (for spectral tinting)
    float  BlendWeight;     // Blend factor for additive mix (0-1)
    float3 SpectralTint;    // Per-mip color tint (Rayleigh approximation)
    float  pad0;
}

Texture2D<float4> SrcTex : register(t0);   // Smaller mip (to upsample)
Texture2D<float4> DstBlend : register(t1); // Same-size downsample mip (to blend with)
SamplerState LinearSampler : register(s0);
RWTexture2D<float4> DstTex : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= DstDims.x || DTid.y >= DstDims.y)
        return;

    // UV in the source (smaller) texture corresponding to this output pixel
    float2 uv = (float2(DTid.xy) + 0.5) / float2(DstDims);

    // 9-tap tent filter (3x3 bilinear kernel)
    // Sample at 9 positions around the center with appropriate tent weights
    float3 result = 0.0;

    // Corners: weight 1/16 each
    result += SrcTex.SampleLevel(LinearSampler, uv + float2(-1, -1) * SrcTexelSize, 0).rgb * (1.0 / 16.0);
    result += SrcTex.SampleLevel(LinearSampler, uv + float2( 1, -1) * SrcTexelSize, 0).rgb * (1.0 / 16.0);
    result += SrcTex.SampleLevel(LinearSampler, uv + float2(-1,  1) * SrcTexelSize, 0).rgb * (1.0 / 16.0);
    result += SrcTex.SampleLevel(LinearSampler, uv + float2( 1,  1) * SrcTexelSize, 0).rgb * (1.0 / 16.0);

    // Edges: weight 2/16 each
    result += SrcTex.SampleLevel(LinearSampler, uv + float2( 0, -1) * SrcTexelSize, 0).rgb * (2.0 / 16.0);
    result += SrcTex.SampleLevel(LinearSampler, uv + float2(-1,  0) * SrcTexelSize, 0).rgb * (2.0 / 16.0);
    result += SrcTex.SampleLevel(LinearSampler, uv + float2( 1,  0) * SrcTexelSize, 0).rgb * (2.0 / 16.0);
    result += SrcTex.SampleLevel(LinearSampler, uv + float2( 0,  1) * SrcTexelSize, 0).rgb * (2.0 / 16.0);

    // Center: weight 4/16
    result += SrcTex.SampleLevel(LinearSampler, uv, 0).rgb * (4.0 / 16.0);

    // Apply per-mip spectral tint (Rayleigh scattering approximation)
    result *= SpectralTint;

    // Read the corresponding downsample mip at this resolution for additive blend
    float2 dstUV = (float2(DTid.xy) + 0.5) / float2(DstDims);
    float3 blendColor = DstBlend.SampleLevel(LinearSampler, dstUV, 0).rgb;

    // Additive blend: upsampled lower mip + current downsample mip
    float3 final_ = blendColor + result * BlendWeight;

    DstTex[DTid.xy] = float4(final_, 1.0);
}
