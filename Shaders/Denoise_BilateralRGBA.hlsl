// Joint bilateral filter — RGBA float4 (depth edge-stopping)
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Same algorithm as the single-channel variant but operates on
// R16G16B16A16_FLOAT data. Depth edge stopping prevents color
// bleeding across geometric discontinuities.

cbuffer BilateralCB : register(b0)
{
    uint2  ScreenDims;
    float  NearZ;
    float  FarZ;
    float  DepthSigma;
    float  NormalSigma;
    int    KernelRadius;
    float  pad0;
}

Texture2D<float4>   NoisyInput  : register(t0);
Texture2D<float>    DepthTex    : register(t1);
Texture2D<float>    LinearDepth : register(t31);
RWTexture2D<float4> Output      : register(u0);

float GaussianWeight(int dx, int dy)
{
    static const float kSigmaS = 1.5;
    static const float kInvTwoSigma2 = 1.0 / (2.0 * kSigmaS * kSigmaS);
    return exp(-(float)(dx * dx + dy * dy) * kInvTwoSigma2);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (any(DTid.xy >= ScreenDims))
        return;

    int2 center = int2(DTid.xy);

    float4 centerValue = NoisyInput.Load(int3(center, 0));
    float  centerDepth = LinearDepth.Load(int3(center, 0));

    // Sky pixels: pass through unfiltered
    if (centerDepth <= 0.0)
    {
        Output[DTid.xy] = centerValue;
        return;
    }

    float  weightSum = 0.0;
    float4 valueSum  = 0.0;

    int radius = clamp(KernelRadius, 1, 3);

    [loop]
    for (int dy = -radius; dy <= radius; dy++)
    {
        [loop]
        for (int dx = -radius; dx <= radius; dx++)
        {
            int2 samplePos = clamp(center + int2(dx, dy),
                                   int2(0, 0), int2(ScreenDims) - int2(1, 1));

            float4 sampleValue = NoisyInput.Load(int3(samplePos, 0));
            float  sampleDepth = LinearDepth.Load(int3(samplePos, 0));

            float wSpatial = GaussianWeight(dx, dy);

            float depthDiff = abs(centerDepth - sampleDepth) / max(centerDepth * DepthSigma, 1e-6);
            float wDepth = exp(-depthDiff);

            float w = wSpatial * wDepth;
            valueSum  += sampleValue * w;
            weightSum += w;
        }
    }

    Output[DTid.xy] = (weightSum > 1e-6) ? (valueSum / weightSum) : centerValue;
}
