// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Anamorphic lens streak: 1D horizontal Gaussian blur.
// Creates the characteristic horizontal streak seen in anamorphic lenses.
// Uses a 7-tap Gaussian kernel scaled by StreakLength to control width.

cbuffer AnamorphicCB : register(b0)
{
    uint2  Dims;            // Half-res dimensions
    float  Intensity;       // Streak intensity (0-2)
    float  StreakLength;    // Base streak length in texels
    float2 TexelSize;       // 1.0 / Dims
    float  pad0, pad1;
}

Texture2D<float4> SrcTex : register(t0);   // Half-res bloom (upsample level 0)
SamplerState LinearSampler : register(s0);
RWTexture2D<float4> DstTex : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= Dims.x || DTid.y >= Dims.y)
        return;

    float2 uv = (float2(DTid.xy) + 0.5) / float2(Dims);

    // 7-tap Gaussian weights (sigma ~ 1.5, normalized)
    // Kernel: { 0.0205, 0.0855, 0.232, 0.324, 0.232, 0.0855, 0.0205 }
    static const float weights[4] = { 0.324, 0.232, 0.0855, 0.0205 };

    // Horizontal offset per tap, scaled by streak length
    float step = TexelSize.x * StreakLength;

    // Center tap
    float3 result = SrcTex.SampleLevel(LinearSampler, uv, 0).rgb * weights[0];

    // Symmetric taps 1-3
    [unroll]
    for (int i = 1; i < 4; i++)
    {
        float offset = float(i) * step;
        float3 left  = SrcTex.SampleLevel(LinearSampler, uv + float2(-offset, 0), 0).rgb;
        float3 right = SrcTex.SampleLevel(LinearSampler, uv + float2( offset, 0), 0).rgb;
        result += (left + right) * weights[i];
    }

    // Scale by intensity
    result *= Intensity;

    DstTex[DTid.xy] = float4(result, 1.0);
}
