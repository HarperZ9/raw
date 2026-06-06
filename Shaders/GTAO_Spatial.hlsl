// Spatial denoise — 5x5 bilateral filter with depth edge stopping.
// Copyright (c) 2026 Zain D. Harper. All rights reserved.

cbuffer SpatialCB : register(b0)
{
    uint2  ScreenDims;
    float  NearZ;
    float  FarZ;
    float  DepthThreshold;
    float3 pad0;
};

Texture2D<float>  AOInput     : register(t0);
Texture2D<float>  DepthTex    : register(t1);
Texture2D<float>  LinearDepth : register(t31);
RWTexture2D<float> AOOutput   : register(u0);

static const float kW[3] = { 0.375, 0.25, 0.0625 };

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= ScreenDims.x || DTid.y >= ScreenDims.y) return;

    int2  coord = int2(DTid.xy);
    float cZ    = LinearDepth.Load(int3(coord, 0));
    float cAO   = AOInput.Load(int3(coord, 0));
    float total = 0.0, wSum = 0.0;

    [unroll] for (int dy = -2; dy <= 2; dy++)
    [unroll] for (int dx = -2; dx <= 2; dx++)
    {
        int2  sc = clamp(coord + int2(dx, dy), 0, int2(ScreenDims) - 1);
        float sZ = LinearDepth.Load(int3(sc, 0));
        float sA = AOInput.Load(int3(sc, 0));

        float spatW  = kW[abs(dx)] * kW[abs(dy)];
        float depthW = exp(-abs(sZ - cZ) / max(cZ * DepthThreshold, 0.01));
        float aoW    = exp(-abs(sA - cAO) * 10.0);
        float w      = spatW * depthW * aoW;

        total += sA * w;
        wSum  += w;
    }

    AOOutput[coord] = total / max(wSum, 0.0001);
}
