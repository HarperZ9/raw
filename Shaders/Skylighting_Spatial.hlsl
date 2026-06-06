// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Spatial Denoise CS — 5x5 bilateral filter on single-channel sky visibility.
//
// Edge-stopping weight is based on the relative depth difference between
// the center pixel and each neighbour.  Large depth jumps (object
// silhouettes) are preserved; smooth surfaces are blurred.
//
// This is the same pattern used by GTAO's spatial denoise.

cbuffer SpatialCB : register(b0)
{
    uint2  ScreenDims;
    float  NearZ;
    float  FarZ;
    float  DepthThreshold;    // Relative depth tolerance (0.05 = 5%)
    float3 pad0;
};

Texture2D<float>  SkyInput    : register(t0);
Texture2D<float>  DepthTex    : register(t1);
Texture2D<float>  LinearDepth : register(t31);

RWTexture2D<float> SkyOutput  : register(u0);

// 5x5 separable Gaussian kernel (sigma ~ 2.0, wider for better noise suppression).
// Wider sigma smooths out blue-noise jitter patterns from the screen-space trace.
static const float kGauss[3] = { 0.30, 0.28, 0.14 };

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= ScreenDims.x || DTid.y >= ScreenDims.y)
        return;

    int2  coord       = int2(DTid.xy);
    float centerDepth = LinearDepth.Load(int3(coord, 0));
    float centerSky   = SkyInput.Load(int3(coord, 0));

    // Depth edge-stopping sensitivity.  A larger centerDepth tolerates
    // a proportionally larger absolute depth difference.
    float depthSigma = max(centerDepth * DepthThreshold, 0.1);

    float totalSky    = 0.0;
    float totalWeight = 0.0;

    [unroll]
    for (int dy = -2; dy <= 2; dy++)
    {
        [unroll]
        for (int dx = -2; dx <= 2; dx++)
        {
            int2 sc = clamp(coord + int2(dx, dy), 0, int2(ScreenDims) - 1);

            float sDepth = LinearDepth.Load(int3(sc, 0));
            float sSky   = SkyInput.Load(int3(sc, 0));

            // Spatial weight: 2D Gaussian
            float spatialW = kGauss[abs(dx)] * kGauss[abs(dy)];

            // Depth edge-stopping: exponential fall-off on relative depth difference
            float depthDiff = abs(sDepth - centerDepth);
            float depthW    = exp(-depthDiff * depthDiff / (2.0 * depthSigma * depthSigma));

            float w = spatialW * depthW;
            totalSky    += sSky * w;
            totalWeight += w;
        }
    }

    SkyOutput[coord] = totalSky / max(totalWeight, 1e-5);
}
