// Copyright (c) 2026 Zain D. Harper. All rights reserved.
// Procedural caustic pattern via 2-layer Voronoi noise animated by time.

cbuffer CausticCB : register(b0)
{
    float4x4 InvViewProj;       // For world-space reconstruction
    float2   ScreenDims;        // Quarter-res output dimensions
    float2   FullScreenDims;    // Full-res screen dimensions
    float    Time;              // Accumulated time (seconds)
    float    NearZ;
    float    FarZ;
    float    WaterSurfaceZ;     // World-space Z of water surface
    float    CausticIntensity;  // Brightness multiplier
    float    MaxCausticDepth;   // World units below surface where caustics vanish
    float2   pad0;
}

Texture2D<float> DepthTex : register(t0);
RWTexture2D<float> CausticOutput : register(u0);

// Hash for Voronoi cell centers
float2 VoronoiHash(float2 p)
{
    p = float2(dot(p, float2(127.1, 311.7)),
               dot(p, float2(269.5, 183.3)));
    return frac(sin(p) * 43758.5453);
}

// Single-layer Voronoi distance field
float Voronoi(float2 uv)
{
    float2 ip = floor(uv);
    float2 fp = frac(uv);

    float minDist = 1.0;

    [unroll]
    for (int y = -1; y <= 1; y++)
    {
        [unroll]
        for (int x = -1; x <= 1; x++)
        {
            float2 neighbor = float2(x, y);
            float2 cellCenter = VoronoiHash(ip + neighbor);

            // Animate cell centers over time
            cellCenter = 0.5 + 0.5 * sin(Time * 0.8 + 6.2831 * cellCenter);

            float2 diff = neighbor + cellCenter - fp;
            float d = dot(diff, diff);
            minDist = min(minDist, d);
        }
    }

    return sqrt(minDist);
}

// Linearize reversed-Z depth
float LinearizeDepth(float d)
{
    return NearZ * FarZ / (NearZ + d * (FarZ - NearZ));
}

// Reconstruct world position from depth
float3 ReconstructWorldPos(float2 uv, float depth)
{
    float4 clip = float4(uv * 2.0 - 1.0, depth, 1.0);
    clip.y = -clip.y;
    float4 world = mul(InvViewProj, clip);
    return world.xyz / world.w;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= (uint)ScreenDims.x || DTid.y >= (uint)ScreenDims.y)
    {
        return;
    }

    // Map quarter-res to full-res UV
    float2 uv = (float2(DTid.xy) + 0.5) / ScreenDims;

    // Sample depth at corresponding full-res location
    uint2 fullCoord = min(uint2(uv * FullScreenDims), uint2(FullScreenDims) - 1);
    float depth = DepthTex.Load(int3(fullCoord, 0));

    // Reconstruct world position
    float3 worldPos = ReconstructWorldPos(uv, depth);

    // Compute depth below water surface
    float depthBelowSurface = WaterSurfaceZ - worldPos.z;

    // No caustics above water or beyond max depth
    if (depthBelowSurface <= 0.0 || depthBelowSurface > MaxCausticDepth)
    {
        CausticOutput[DTid.xy] = 0.0;
        return;
    }

    // Two-layer Voronoi at different scales for natural interference pattern
    float2 worldUV = worldPos.xy * 0.02; // world-space tiling scale

    float v1 = Voronoi(worldUV * 1.0 + float2(Time * 0.03, Time * 0.02));
    float v2 = Voronoi(worldUV * 2.7 + float2(-Time * 0.04, Time * 0.05));

    // Caustic pattern: bright where both layers have small distances (cell edges)
    // Invert and sharpen the Voronoi to get bright caustic lines
    float c1 = pow(saturate(1.0 - v1), 3.0);
    float c2 = pow(saturate(1.0 - v2), 3.0);

    // Combine layers: multiply for sharper pattern, add for broader glow
    float caustic = (c1 + c2) * 0.5 + c1 * c2 * 2.0;

    // Depth attenuation: caustics fade with depth below surface
    float depthFade = saturate(1.0 - depthBelowSurface / MaxCausticDepth);
    depthFade = depthFade * depthFade; // quadratic falloff

    // Surface proximity boost: brightest near the surface
    float surfaceBoost = saturate(1.0 - depthBelowSurface / (MaxCausticDepth * 0.1));

    float result = caustic * depthFade * CausticIntensity * (1.0 + surfaceBoost);
    CausticOutput[DTid.xy] = saturate(result);
}
