// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Pass 2: Hemisphere ray trace through voxel grid
// Reference: Crassin et al. 2011 — adapted for screen-space voxel radiance
//            caching with cosine-weighted hemisphere sampling.
//
// For each half-res pixel, reconstruct view-space position and geometric
// normal (from depth gradients), cast N cosine-weighted rays into the upper
// hemisphere (blue-noise jittered), march through the voxel grid accumulating
// radiance, and output half-res GI color.

cbuffer TraceCB : register(b0)
{
    float4x4 ProjMatrix;       // offset 0   (64 bytes)
    uint   HalfW;              // offset 64
    uint   HalfH;              // offset 68
    uint   ScreenW;            // offset 72
    uint   ScreenH;            // offset 76
    float  NearZ;              // offset 80
    float  FarZ;               // offset 84
    float  VoxelScale;         // offset 88
    int    RayCount;           // offset 92
    int    MaxSteps;           // offset 96
    float  GIIntensity;        // offset 100
    uint   FrameIndex;         // offset 104
    float  FPDepthThreshold;   // offset 108
};

// t0 = HiZ depth (reversed-Z, full-res)
// t1 = Backbuffer copy (albedo proxy, full-res)
// t2 = 128^3 luminance voxel grid (Y SH2)
// t3 = 64^3 chrominance voxel grid (CoCg)
// t30 = Blue noise (128x128, R2 quasi-random)
// t31 = Linear depth (R32_FLOAT, full-res)
Texture2D<float>    DepthTex   : register(t0);
Texture2D<float4>   AlbedoTex  : register(t1);
Texture3D<float4>   VoxelGrid  : register(t2);
Texture3D<float4>   VoxelCoCg  : register(t3);
Texture2D<float4>   BlueNoise  : register(t30);
Texture2D<float>    LinearDepth : register(t31);

RWTexture2D<float4> GIOutput   : register(u0);

SamplerState LinearSampler : register(s0);

static const float PI        = 3.14159265358979;
static const float TWO_PI    = 6.28318530717959;
static const uint  VOXEL_RES = 128;

#include "SharedRAW.hlsli"

// Reconstruct view-space position from half-res pixel coordinate
float3 ReconstructViewPos(uint2 halfPx, float linearZ)
{
    // Map half-res pixel to full-res UV
    float2 uv = (float2(halfPx) * 2.0 + 1.0) / float2(ScreenW, ScreenH);
    float2 ndc = float2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);
    return float3(ndc.x * linearZ / ProjMatrix[0][0],
                  ndc.y * linearZ / ProjMatrix[1][1],
                  linearZ);
}

// Reconstruct view-space position from UV + linear depth (helper)
float3 UVToView(float2 uv, float z)
{
    float2 ndc = float2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);
    return float3(ndc.x * z / ProjMatrix[0][0],
                  ndc.y * z / ProjMatrix[1][1], z);
}

// Reconstruct geometric normal from depth gradients (4-neighbor min-difference)
float3 ReconstructNormal(uint2 halfPx, float centerZ)
{
    int2 fullPx = int2(halfPx) * 2;
    float2 ts = 1.0 / float2(ScreenW, ScreenH);
    float2 uvC = (float2(fullPx) + 0.5) * ts;

    float zL = LinearizeDepth(DepthTex.Load(int3(max(fullPx.x - 2, 0),                  fullPx.y, 0)), NearZ, FarZ);
    float zR = LinearizeDepth(DepthTex.Load(int3(min(fullPx.x + 2, (int)ScreenW - 1),   fullPx.y, 0)), NearZ, FarZ);
    float zU = LinearizeDepth(DepthTex.Load(int3(fullPx.x, max(fullPx.y - 2, 0),                  0)), NearZ, FarZ);
    float zD = LinearizeDepth(DepthTex.Load(int3(fullPx.x, min(fullPx.y + 2, (int)ScreenH - 1),   0)), NearZ, FarZ);

    float3 pC = UVToView(uvC, centerZ);

    bool useR = abs(zR - centerZ) < abs(centerZ - zL);
    bool useU = abs(zU - centerZ) < abs(zD - centerZ);

    float3 ddx = useR
        ? UVToView(uvC + float2(ts.x * 2.0, 0), zR) - pC
        : pC - UVToView(uvC - float2(ts.x * 2.0, 0), zL);
    float3 ddy = useU
        ? UVToView(uvC + float2(0, -ts.y * 2.0), zU) - pC
        : pC - UVToView(uvC + float2(0, ts.y * 2.0), zD);

    return normalize(cross(ddy, ddx));
}

// Generate cosine-weighted hemisphere direction using blue noise
// Uses R2 quasi-random sequence for low-discrepancy sampling
float3 CosineHemisphereDir(float2 xi, float3 N)
{
    // Cosine-weighted hemisphere sampling (Malley's method)
    float r   = sqrt(xi.x);
    float phi = TWO_PI * xi.y;
    float x   = r * cos(phi);
    float y   = r * sin(phi);
    float z   = sqrt(max(0.0, 1.0 - xi.x));  // cos(theta)

    // Build tangent frame from normal
    float3 up = (abs(N.y) < 0.999) ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 T  = normalize(cross(up, N));
    float3 B  = cross(N, T);

    return normalize(T * x + B * y + N * z);
}

// March a ray through the voxel grid, accumulating radiance
// Returns accumulated color (RGB in YCoCg, reconstructed at end)
float3 MarchVoxelRay(float3 origin, float3 dir, float voxelSize)
{
    float3 accum   = 0.0;
    float  opacity = 0.0;

    // Step size in world units (one voxel diagonal)
    float stepSize = voxelSize * 1.732;  // sqrt(3) for diagonal

    float3 pos = origin + dir * stepSize * 2.0;  // Skip first 2 voxels (self-intersection guard)

    for (int step = 0; step < MaxSteps; ++step)
    {
        // Map world position to voxel UV [0, 1]
        float3 voxelUV = pos / VoxelScale * 0.5 + 0.5;

        // Exit if outside grid
        if (any(voxelUV < 0.0) || any(voxelUV > 1.0))
            break;

        // Sample luminance grid (trilinear filtered)
        float4 lumSample  = VoxelGrid.SampleLevel(LinearSampler, voxelUV, 0);
        // Sample chrominance grid (trilinear, lower res)
        float4 cocgSample = VoxelCoCg.SampleLevel(LinearSampler, voxelUV, 0);

        float voxelY       = lumSample.x;
        float voxelOpacity = lumSample.w;

        if (voxelOpacity > 0.01)
        {
            // Reconstruct YCoCg -> RGB
            float3 ycocg = float3(voxelY, cocgSample.x, cocgSample.y);
            float3 rgb   = float3(ycocg.x + ycocg.y - ycocg.z,
                                  ycocg.x + ycocg.z,
                                  ycocg.x - ycocg.y - ycocg.z);
            rgb = max(rgb, 0.0);

            // Front-to-back compositing
            float a = voxelOpacity * (1.0 - opacity);
            accum  += rgb * a;
            opacity += a;

            // Early out if nearly opaque
            if (opacity > 0.95)
                break;
        }

        // Distance-based falloff: increase step size with distance
        float dist = length(pos - origin);
        float adaptiveStep = stepSize * (1.0 + dist * 0.01);
        pos += dir * adaptiveStep;
    }

    return accum;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // Bounds check (half-res)
    if (DTid.x >= HalfW || DTid.y >= HalfH)
        return;

    // Read depth at corresponding full-res pixel (2x)
    int2 fullPx  = int2(DTid.xy) * 2;
    float rawDepth = DepthTex.Load(int3(fullPx, 0));

    // Sky check: reversed-Z sky near 0
    if (rawDepth < 0.0001)
    {
        GIOutput[DTid.xy] = float4(0, 0, 0, 0);
        return;
    }

    // Linearize depth
    float linearZ = LinearizeDepth(rawDepth, NearZ, FarZ);

    // First-person skip
    if (linearZ < FPDepthThreshold)
    {
        GIOutput[DTid.xy] = float4(0, 0, 0, 0);
        return;
    }

    // Reconstruct view-space position and geometric normal
    float3 viewPos = ReconstructViewPos(DTid.xy, linearZ);
    float3 normal  = ReconstructNormal(DTid.xy, linearZ);

    // Voxel size in world units
    float voxelSize = (VoxelScale * 2.0) / float(VOXEL_RES);

    // Accumulate GI from hemisphere rays
    float3 giAccum = 0.0;
    int    validRays = 0;

    for (int ray = 0; ray < RayCount; ++ray)
    {
        // Blue noise jitter: tile 128x128 noise, offset by ray index and frame
        uint2 noiseCoord = (DTid.xy + uint2(ray * 7, FrameIndex * 11)) % 128;
        float4 noise = BlueNoise.Load(int3(noiseCoord, 0));

        // R2 quasi-random sequence offset per ray
        float2 xi = frac(noise.xy + float(ray) * float2(0.7548776662, 0.5698402910));

        // Generate cosine-weighted hemisphere direction
        float3 rayDir = CosineHemisphereDir(xi, normal);

        // March through voxel grid
        float3 radiance = MarchVoxelRay(viewPos, rayDir, voxelSize);

        if (any(radiance > 0.0))
        {
            giAccum += radiance;
            validRays++;
        }
    }

    // Average over all rays (cosine weighting is baked into the sampling)
    float3 gi = (RayCount > 0) ? giAccum / float(RayCount) : 0.0;

    // Apply GI intensity ONCE (not in composite — single multiplication point)
    gi *= GIIntensity;

    // Soft Reinhard clamp to prevent blow-out
    float luma = dot(gi, float3(0.2126, 0.7152, 0.0722));
    if (luma > 0.001)
    {
        float compressed = luma / (1.0 + luma);
        gi *= compressed / luma;
    }

    // Convert back from linear to gamma for correct display blending
    gi = pow(max(gi, 0.0), 1.0 / 2.2);
    GIOutput[DTid.xy] = float4(gi, 1.0);
}
