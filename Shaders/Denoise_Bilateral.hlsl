// Joint bilateral filter — single-channel float (depth edge-stopping)
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// 5x5 bilateral Gaussian with depth-based edge stopping.
// Uses pre-linearized depth from t31 (SharedGPUResources) for
// the current frame. Gaussian spatial weights are hard-coded
// for a 5x5 kernel (sigma_s ~ 1.5). The depth weight uses an
// exponential falloff: w_d = exp(-|z_c - z_n| / (DepthSigma * z_c)).

cbuffer BilateralCB : register(b0)
{
    uint2  ScreenDims;
    float  NearZ;
    float  FarZ;
    float  DepthSigma;      // Depth edge-stopping sensitivity (0.01-0.1)
    float  NormalSigma;     // Normal edge-stopping sensitivity (0.1-1.0)
    int    KernelRadius;    // 1=3x3, 2=5x5, 3=7x7
    float  pad0;
}

Texture2D<float>   NoisyInput  : register(t0);
Texture2D<float>   DepthTex    : register(t1);
Texture2D<float>   LinearDepth : register(t31);
RWTexture2D<float> Output      : register(u0);

// Spatial Gaussian weight for offset (dx, dy) with sigma ~ 1.5
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

    float centerValue = NoisyInput.Load(int3(center, 0));
    float centerDepth = LinearDepth.Load(int3(center, 0));

    // Sky pixels: pass through unfiltered
    if (centerDepth <= 0.0)
    {
        Output[DTid.xy] = centerValue;
        return;
    }

    float weightSum = 0.0;
    float valueSum  = 0.0;

    int radius = clamp(KernelRadius, 1, 3);

    [loop]
    for (int dy = -radius; dy <= radius; dy++)
    {
        [loop]
        for (int dx = -radius; dx <= radius; dx++)
        {
            int2 samplePos = center + int2(dx, dy);

            // Clamp to screen bounds
            samplePos = clamp(samplePos, int2(0, 0), int2(ScreenDims) - int2(1, 1));

            float sampleValue = NoisyInput.Load(int3(samplePos, 0));
            float sampleDepth = LinearDepth.Load(int3(samplePos, 0));

            // Spatial weight (Gaussian)
            float wSpatial = GaussianWeight(dx, dy);

            // Depth edge-stopping weight
            float depthDiff = abs(centerDepth - sampleDepth) / max(centerDepth * DepthSigma, 1e-6);
            float wDepth = exp(-depthDiff);

            float w = wSpatial * wDepth;
            valueSum  += sampleValue * w;
            weightSum += w;
        }
    }

    Output[DTid.xy] = (weightSum > 1e-6) ? (valueSum / weightSum) : centerValue;
}
