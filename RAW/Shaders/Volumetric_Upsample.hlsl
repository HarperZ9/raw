// Copyright (c) 2026 Zain D. Harper. All rights reserved.
// Bilateral depth-aware upsample from half-res to full-res volumetric scatter.

cbuffer UpsampleCB : register(b0)
{
    uint  fullWidth;
    uint  fullHeight;
    uint  halfWidth;
    uint  halfHeight;
    float nearZ;
    float farZ;
    float depthThreshold;
    float pad0;
}

Texture2D<float4>   tHalfRes  : register(t0);  // half-res scatter
Texture2D<float>    tDepth    : register(t1);  // full-res depth (reversed-Z)
Texture2D<float>    LinearDepth : register(t31); // pre-computed linearized depth
RWTexture2D<float4> uOutput   : register(u0);  // full-res output
SamplerState sLinear : register(s0);

// Linearize reversed-Z depth to view-space distance
float LinearizeDepth(float d)
{
    // reversed-Z: near=1, far=0
    // linearZ = nearZ * farZ / (nearZ + d * (farZ - nearZ))
    return nearZ * farZ / (nearZ + d * (farZ - nearZ));
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= fullWidth || DTid.y >= fullHeight)
        return;

    // Full-res pixel center depth
    float fullDepth = tDepth.Load(int3(DTid.xy, 0));
    float fullLinZ  = LinearizeDepth(fullDepth);

    // Corresponding half-res coordinate (with 0.5 offset for center)
    float2 halfUV = (float2(DTid.xy) + 0.5) / float2(fullWidth, fullHeight);
    float2 halfCoordF = halfUV * float2(halfWidth, halfHeight) - 0.5;

    // 2x2 bilinear tap coordinates in half-res
    int2 baseCoord = int2(floor(halfCoordF));
    float2 frac2 = halfCoordF - float2(baseCoord);

    // Bilateral weights: combine bilinear interpolation weight with depth similarity
    float4 result = 0.0;
    float  totalWeight = 0.0;

    [unroll]
    for (int dy = 0; dy <= 1; dy++)
    {
        [unroll]
        for (int dx = 0; dx <= 1; dx++)
        {
            int2 tapCoord = baseCoord + int2(dx, dy);
            tapCoord = clamp(tapCoord, int2(0, 0), int2(halfWidth - 1, halfHeight - 1));

            // Bilinear weight
            float bx = (dx == 0) ? (1.0 - frac2.x) : frac2.x;
            float by = (dy == 0) ? (1.0 - frac2.y) : frac2.y;
            float bilinearW = bx * by;

            // Sample half-res scatter and the depth at the corresponding full-res location
            float4 halfSample = tHalfRes.Load(int3(tapCoord, 0));

            // Depth at this half-res tap (sample full-res depth at 2x location)
            uint2 fullTapCoord = min(uint2(tapCoord) * 2, uint2(fullWidth - 1, fullHeight - 1));
            float tapDepth = tDepth.Load(int3(fullTapCoord, 0));
            float tapLinZ  = LinearizeDepth(tapDepth);

            // Bilateral depth weight: Gaussian falloff based on depth difference
            float depthDiff = abs(fullLinZ - tapLinZ) / max(fullLinZ * depthThreshold, 1e-5);
            float depthW = exp(-depthDiff * depthDiff);

            float w = bilinearW * depthW;
            result     += halfSample * w;
            totalWeight += w;
        }
    }

    // Normalize and write output
    if (totalWeight > 1e-6)
        result /= totalWeight;
    else
        result = tHalfRes.Load(int3(max(baseCoord, int2(0,0)), 0));

    uOutput[DTid.xy] = result;
}
