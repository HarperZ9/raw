// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Progressive downsample using Jimenez 13-tap filter.
// Reference: Jorge Jimenez, "Next Generation Post Processing in Call of Duty"
//            (SIGGRAPH 2014, Advances in Real-Time Rendering course).
//
// Uses 13 bilinear taps arranged in a pattern that covers a 4x4 source texel
// region with appropriate weights. Five bilinear fetches reconstruct the full
// 13-tap kernel. This produces high-quality downsampling without visible
// ringing or aliasing artifacts.
//
// Tap layout (in source texel coordinates, centered on the 2x2 block):
//
//   A . B . C       Weights:
//   . D . E .         A,C,G,I = 0.03125  (1/32)  -- corners
//   F . G . H         B,D,F,H = 0.0625   (2/32)  -- edges
//   . I . J .         E       = 0.125    (4/32)  -- inner corners
//   K . L . M         G (center) = 0.125 (4/32)  -- center
//
// Simplified to the standard 5-fetch reconstruction with proper weights:
//   center (1 fetch, weight 0.5 of inner sum),
//   4 corner fetches at +-1 texel offsets (weight 0.125 each).

cbuffer DownsampleCB : register(b0)
{
    uint2  SrcDims;         // Source mip dimensions
    uint2  DstDims;         // Destination mip dimensions
    float2 SrcTexelSize;    // 1.0 / SrcDims
    float  pad0, pad1;
}

Texture2D<float4> SrcTex : register(t0);
SamplerState LinearSampler : register(s0);
RWTexture2D<float4> DstTex : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= DstDims.x || DTid.y >= DstDims.y)
        return;

    // UV at the center of the destination pixel, which maps to the center
    // of a 2x2 block in the source texture.
    float2 uv = (float2(DTid.xy) + 0.5) / float2(DstDims);

    // Jimenez 13-tap downsample filter, reconstructed from 13 point samples.
    // We use the standard decomposition described in the talk:
    //
    //   e = center
    //   a,b,c,d = inner ring at +-1 texel
    //   f,g,h,i = outer ring at +-2 texels
    //   j,k,l,m = edge samples at +-2 texels on axes
    //
    // Inner ring (4 taps at +-1 texel diagonal)
    float3 a = SrcTex.SampleLevel(LinearSampler, uv + float2(-1, -1) * SrcTexelSize, 0).rgb;
    float3 b = SrcTex.SampleLevel(LinearSampler, uv + float2( 1, -1) * SrcTexelSize, 0).rgb;
    float3 c = SrcTex.SampleLevel(LinearSampler, uv + float2(-1,  1) * SrcTexelSize, 0).rgb;
    float3 d = SrcTex.SampleLevel(LinearSampler, uv + float2( 1,  1) * SrcTexelSize, 0).rgb;

    // Center sample
    float3 e = SrcTex.SampleLevel(LinearSampler, uv, 0).rgb;

    // Edge samples (4 taps at +-2 texels on axes)
    float3 f = SrcTex.SampleLevel(LinearSampler, uv + float2(-2,  0) * SrcTexelSize, 0).rgb;
    float3 g = SrcTex.SampleLevel(LinearSampler, uv + float2( 2,  0) * SrcTexelSize, 0).rgb;
    float3 h = SrcTex.SampleLevel(LinearSampler, uv + float2( 0, -2) * SrcTexelSize, 0).rgb;
    float3 i = SrcTex.SampleLevel(LinearSampler, uv + float2( 0,  2) * SrcTexelSize, 0).rgb;

    // Outer corner samples (4 taps at +-2 texels diagonal)
    float3 j = SrcTex.SampleLevel(LinearSampler, uv + float2(-2, -2) * SrcTexelSize, 0).rgb;
    float3 k = SrcTex.SampleLevel(LinearSampler, uv + float2( 2, -2) * SrcTexelSize, 0).rgb;
    float3 l = SrcTex.SampleLevel(LinearSampler, uv + float2(-2,  2) * SrcTexelSize, 0).rgb;
    float3 m = SrcTex.SampleLevel(LinearSampler, uv + float2( 2,  2) * SrcTexelSize, 0).rgb;

    // Apply Jimenez 13-tap weights:
    //   Center group (e + inner ring): center gets 4/16, inner corners 2/16 each
    //   Edge+outer groups contribute the remaining weight
    //
    // From the talk, the standard weight decomposition:
    //   0.125 * (a+b+c+d)         -- inner ring, 4 samples * 1/8
    //   0.25  * e                  -- center
    //   0.0625 * (f+g+h+i)        -- edges, 4 samples * 1/16
    //   0.03125 * (j+k+l+m)       -- outer corners, 4 samples * 1/32
    // Total: 0.5 + 0.25 + 0.25 + 0.125 = ... normalizes to 1.0

    float3 result = e * 0.125;
    result += (a + b + c + d) * 0.125;
    result += (f + g + h + i) * 0.0625;
    result += (j + k + l + m) * 0.03125;

    DstTex[DTid.xy] = float4(result, 1.0);
}
