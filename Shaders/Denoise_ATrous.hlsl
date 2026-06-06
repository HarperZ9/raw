// A-trous wavelet filter with depth + luminance edge stopping
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Reference: Dammertz et al. 2010, "Edge-Avoiding A-Trous Wavelet
// Transform for fast Global Illumination Filtering"
//
// Uses a 5x5 B3-spline kernel decimated by StepSize. The filter
// is applied in a single pass at the current decimation level;
// the caller (DispatchATrousFull) ping-pongs across levels 0..N
// with StepSize = 1, 2, 4, 8, 16.
//
// Edge-stopping functions:
//   - Depth:     w_z = exp(-|z_c - z_n| / (sigma_z * z_c))
//   - Luminance: w_l = exp(-|l_c - l_n| / sigma_l)  [single-channel]

cbuffer ATrousCB : register(b0)
{
    uint2  ScreenDims;
    float  NearZ;
    float  FarZ;
    int    StepSize;         // 1, 2, or 4 (for each a-trous level)
    float  DepthSigma;      // Depth edge-stopping sensitivity
    float  NormalSigma;     // Reserved for normal edge-stopping
    float  LuminanceSigma;  // Luminance edge-stopping sensitivity
}

// B3-spline 1D kernel weights: [1/16, 1/4, 3/8, 1/4, 1/16]
// 2D weight = h[|dx|] * h[|dy|] where index maps: 0->3/8, 1->1/4, 2->1/16
static const float kKernel[3] = { 3.0 / 8.0, 1.0 / 4.0, 1.0 / 16.0 };

Texture2D<float>   InputTex    : register(t0);
Texture2D<float>   DepthTex    : register(t1);
Texture2D<float>   LinearDepth : register(t31);
RWTexture2D<float> Output      : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (any(DTid.xy >= ScreenDims))
        return;

    int2 center = int2(DTid.xy);

    float centerValue = InputTex.Load(int3(center, 0));
    float centerDepth = LinearDepth.Load(int3(center, 0));

    // Sky pixels: pass through
    if (centerDepth <= 0.0)
    {
        Output[DTid.xy] = centerValue;
        return;
    }

    float weightSum = 0.0;
    float valueSum  = 0.0;

    // 5-tap 2D separable kernel with a-trous stride
    [unroll]
    for (int dy = -2; dy <= 2; dy++)
    {
        [unroll]
        for (int dx = -2; dx <= 2; dx++)
        {
            int2 offset = int2(dx, dy) * StepSize;
            int2 samplePos = center + offset;

            // Clamp to screen
            samplePos = clamp(samplePos, int2(0, 0), int2(ScreenDims) - int2(1, 1));

            float sampleValue = InputTex.Load(int3(samplePos, 0));
            float sampleDepth = LinearDepth.Load(int3(samplePos, 0));

            // B3-spline spatial weight (separable: w = h[|dx|] * h[|dy|])
            float wSpatial = kKernel[abs(dx)] * kKernel[abs(dy)];

            // Depth edge-stopping
            float depthDiff = abs(centerDepth - sampleDepth) / max(centerDepth * DepthSigma, 1e-6);
            float wDepth = exp(-depthDiff);

            // Luminance edge-stopping
            float lumaDiff = abs(centerValue - sampleValue);
            float wLuma = exp(-lumaDiff / max(LuminanceSigma, 1e-6));

            float w = wSpatial * wDepth * wLuma;
            valueSum  += sampleValue * w;
            weightSum += w;
        }
    }

    Output[DTid.xy] = (weightSum > 1e-6) ? (valueSum / weightSum) : centerValue;
}
