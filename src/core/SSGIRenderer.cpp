//=============================================================================
//  SSGIRenderer.cpp — Screen-space global illumination via voxel cone tracing
//
//  Dispatch flow (per frame, PostGeometry stage):
//    1. Copy backbuffer for albedo read
//    2. Clear + dispatch Voxelize CS: depth+albedo → 128^3 voxel grid
//    3. Dispatch TraceGI CS: hemisphere rays → half-res raw GI
//    4. Dispatch Denoise CS: bilateral temporal filter (ping-pong)
//    5. Execute composite PS: bilateral upsample + additive blend
//=============================================================================

#include "SSGIRenderer.h"
#include "HiZPyramid.h"
#include "SRVInjector.h"
#include "SharedGPUResources.h"
#include "ShaderLoader.h"
#include "D3D11Hook.h"
#include "MotionVectorGen.h"
#include "SceneData.h"

#include <SKSE/SKSE.h>
#include <d3dcompiler.h>
#include <cstring>
#include <cmath>
#include "GPUResource.h"

namespace SB
{

// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Pass 1: Voxelize CS
// ═══════════════════════════════════════════════════════════════════════════

static const char kVoxelizeCS[] = R"HLSL(
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Pass 1: Screen-space voxelization
// Reference: Crassin et al. 2011 "Interactive Indirect Illumination Using
//            Voxel Cone Tracing" — concept adapted for screen-space.
//
// For each screen pixel, reconstruct view-space position from depth, map into
// a camera-centered 128^3 voxel grid, and write scene color (albedo proxy).
//
// Race-condition strategy: last-write-wins on R16G16B16A16_FLOAT UAV.
// D3D11 SM5.0 does not support atomics on float16x4 typed UAVs.  The grid is
// cleared every frame and temporal denoising in Pass 3 smooths the per-voxel
// noise that arises from write ordering.  DeviceMemoryBarrier after the store
// ensures the write is visible to subsequent dispatches.

cbuffer VoxelizeCB : register(b0)
{
    float4x4 ProjMatrix;  // Projection matrix (row-major)
    uint2  ScreenDims;    // Full-res width, height
    float  NearZ;         // Camera near clip
    float  FarZ;          // Camera far clip
    float  VoxelScale;    // World-space half-extent of the voxel grid
    float3 pad0;
};

// t0 = HiZ depth (reversed-Z: near=1, far=0)
// t1 = Backbuffer copy (albedo proxy)
Texture2D<float>    DepthTex   : register(t0);
Texture2D<float4>   SceneColor : register(t1);

// u0 = 128^3 luminance voxel grid (R16G16B16A16_FLOAT)
// u1 = 64^3  chrominance voxel grid (R16G16B16A16_FLOAT)
RWTexture3D<float4> VoxelGrid  : register(u0);
RWTexture3D<float4> VoxelCoCg  : register(u1);

// Linearize reversed-Z depth to view-space Z
// reversed-Z: z_rev = 1 at near, 0 at far
float LinearizeDepth(float z_rev, float N, float F)
{
    return N * F / (N + z_rev * (F - N));
}

// RGB -> YCoCg (luminance + chroma)
float3 RGBtoYCoCg(float3 rgb)
{
    return float3( 0.25 * rgb.r + 0.5 * rgb.g + 0.25 * rgb.b,
                   0.5  * rgb.r                - 0.5  * rgb.b,
                  -0.25 * rgb.r + 0.5 * rgb.g - 0.25 * rgb.b);
}

static const uint VOXEL_RES     = 128;
static const uint COCG_VOXEL_RES = 64;

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // Bounds check
    if (DTid.x >= ScreenDims.x || DTid.y >= ScreenDims.y)
        return;

    // Read reversed-Z depth from HiZ
    float rawDepth = DepthTex.Load(int3(DTid.xy, 0));

    // Sky check: reversed-Z sky pixels are near 0
    if (rawDepth < 0.0001)
        return;

    // Linearize to view-space Z (game units)
    float linearZ = LinearizeDepth(rawDepth, NearZ, FarZ);

    // First-person skip: geometry too close to camera (arms, weapons)
    if (linearZ < 16.0)
        return;

    // Reconstruct view-space position from pixel coordinate + depth
    // UV from pixel center
    float2 uv = (float2(DTid.xy) + 0.5) / float2(ScreenDims);

    // NDC: [-1, 1] range
    float2 ndc = float2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);

    // View-space position (camera at origin, +Z forward)
    float3 viewPos = float3(ndc.x * linearZ / ProjMatrix[0][0],
                            ndc.y * linearZ / ProjMatrix[1][1],
                            linearZ);

    // Map view-space position to voxel grid coordinates
    // Grid is camera-centered, spanning [-VoxelScale, +VoxelScale] in each axis
    float3 gridPos = viewPos / VoxelScale;  // Normalize to [-1, 1]
    float3 voxelUV = gridPos * 0.5 + 0.5;  // Remap to [0, 1]

    // Reject pixels outside the voxel grid
    if (any(voxelUV < 0.0) || any(voxelUV > 1.0))
        return;

    // Voxel coordinates for the 128^3 luminance grid
    uint3 voxelCoord = uint3(voxelUV * float(VOXEL_RES));
    voxelCoord = min(voxelCoord, uint3(VOXEL_RES - 1, VOXEL_RES - 1, VOXEL_RES - 1));

    // Read scene color (albedo proxy)
    float4 color = SceneColor.Load(int3(DTid.xy, 0));
    float3 rgb   = max(color.rgb, 0.0);

    // Convert to YCoCg
    float3 ycocg = RGBtoYCoCg(rgb);

    // Write luminance (Y) and opacity into the 128^3 grid
    // Last-write-wins: temporal denoising in Pass 3 smooths the noise
    VoxelGrid[voxelCoord] = float4(ycocg.x, 0.0, 0.0, 1.0);

    // Write chrominance (Co, Cg) into the 64^3 subsampled grid
    uint3 cocgCoord = uint3(voxelUV * float(COCG_VOXEL_RES));
    cocgCoord = min(cocgCoord, uint3(COCG_VOXEL_RES - 1, COCG_VOXEL_RES - 1, COCG_VOXEL_RES - 1));
    VoxelCoCg[cocgCoord] = float4(ycocg.y, ycocg.z, 1.0, 0.0);

    // Ensure the writes are visible to subsequent dispatches
    DeviceMemoryBarrier();
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Pass 2: TraceGI CS (half-res)
// ═══════════════════════════════════════════════════════════════════════════

static const char kTraceGICS[] = R"HLSL(
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

// Linearize reversed-Z depth
float LinearizeDepth(float z_rev, float N, float F)
{
    return N * F / (N + z_rev * (F - N));
}

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

    GIOutput[DTid.xy] = float4(gi, 1.0);
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Pass 3: Denoise CS (half-res, bilateral temporal)
// ═══════════════════════════════════════════════════════════════════════════

static const char kDenoiseCS[] = R"HLSL(
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Pass 3: Temporal + spatial denoise (half-res, bilateral)
//
// 1. Bilateral spatial filter (5x5) with depth-based edge stopping to
//    smooth the noisy per-pixel GI from the trace pass while preserving
//    geometric edges.
// 2. Temporal blend with ping-pong history buffer (alpha=0.2).
//    FrameIndex < 3: bypass history entirely to avoid black ghosting
//    from uninitialized buffers.

cbuffer DenoiseCB : register(b0)
{
    uint   HalfW;             // offset 0
    uint   HalfH;             // offset 4
    uint   ScreenW;           // offset 8
    uint   ScreenH;           // offset 12
    float  NearZ;             // offset 16
    float  FarZ;              // offset 20
    float  TemporalAlpha;     // offset 24  (blend weight, 0.2 default)
    float  DepthThreshold;    // offset 28  (edge-stopping threshold)
    uint   FrameIndex;        // offset 32
    float3 pad0;              // offset 36-44  (pad to 48 bytes)
};

// t0 = Raw GI from trace pass (half-res)
// t1 = Previous denoised GI history (half-res, ping-pong read)
// t2 = HiZ depth (reversed-Z, full-res)
// t31 = Linear depth (R32_FLOAT, full-res)
Texture2D<float4>   GIInput    : register(t0);
Texture2D<float4>   HistoryTex : register(t1);
Texture2D<float>    DepthTex   : register(t2);
Texture2D<float>    LinearDepth : register(t31);

RWTexture2D<float4> GIOutput   : register(u0);

// Linearize reversed-Z depth
float LinearizeDepth(float z_rev, float N, float F)
{
    return N * F / (N + z_rev * (F - N));
}

// Bilateral weight from depth difference
float DepthWeight(float centerZ, float sampleZ, float threshold)
{
    float diff = abs(centerZ - sampleZ) / max(centerZ, 0.001);
    return exp(-diff * diff / (2.0 * threshold * threshold));
}

// 5x5 bilateral spatial filter with depth edge stopping
float4 BilateralFilter(uint2 px, float centerZ)
{
    float4 accumColor  = 0.0;
    float  accumWeight = 0.0;

    static const int KERNEL_RADIUS = 2;  // 5x5

    for (int dy = -KERNEL_RADIUS; dy <= KERNEL_RADIUS; ++dy)
    {
        for (int dx = -KERNEL_RADIUS; dx <= KERNEL_RADIUS; ++dx)
        {
            int2 samplePx = int2(px) + int2(dx, dy);

            // Clamp to texture bounds
            samplePx = clamp(samplePx, int2(0, 0), int2(HalfW - 1, HalfH - 1));

            // Read sample GI
            float4 sampleGI = GIInput.Load(int3(samplePx, 0));

            // Read depth at corresponding full-res pixel
            int2 fullPx = samplePx * 2;
            float sampleRawZ = DepthTex.Load(int3(fullPx, 0));
            float sampleZ    = LinearizeDepth(max(sampleRawZ, 0.0001), NearZ, FarZ);

            // Spatial Gaussian weight
            float spatialDist = float(dx * dx + dy * dy);
            float spatialW    = exp(-spatialDist / 4.5);  // sigma ~ 1.5 pixels

            // Depth edge-stopping weight
            float depthW = DepthWeight(centerZ, sampleZ, DepthThreshold);

            float w = spatialW * depthW;
            accumColor  += sampleGI * w;
            accumWeight += w;
        }
    }

    return (accumWeight > 0.001) ? accumColor / accumWeight : GIInput.Load(int3(px, 0));
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // Bounds check (half-res)
    if (DTid.x >= HalfW || DTid.y >= HalfH)
        return;

    // Read depth for center pixel
    int2  fullPx   = int2(DTid.xy) * 2;
    float rawDepth = DepthTex.Load(int3(fullPx, 0));

    // Sky check: reversed-Z sky near 0 — pass through zero
    if (rawDepth < 0.0001)
    {
        GIOutput[DTid.xy] = float4(0, 0, 0, 0);
        return;
    }

    float centerZ = LinearizeDepth(rawDepth, NearZ, FarZ);

    // ── Spatial bilateral filter ─────────────────────────────────────────
    float4 spatialFiltered = BilateralFilter(DTid.xy, centerZ);

    // ── Temporal accumulation ────────────────────────────────────────────
    // MUST bypass history for first 3 frames (prevents black ghosting
    // from uninitialized ping-pong buffers)
    if (FrameIndex < 3)
    {
        GIOutput[DTid.xy] = spatialFiltered;
        return;
    }

    // Read history (previous frame's denoised output)
    float4 history = HistoryTex.Load(int3(DTid.xy, 0));

    // Simple temporal blend (no motion vectors — accept ghosting tradeoff)
    // TemporalAlpha = 0.2 means 20% new frame, 80% history
    float alpha = TemporalAlpha;

    // Reject history if luminance difference is too large (disocclusion)
    float histLuma    = dot(history.rgb, float3(0.2126, 0.7152, 0.0722));
    float currentLuma = dot(spatialFiltered.rgb, float3(0.2126, 0.7152, 0.0722));
    float lumaDiff    = abs(histLuma - currentLuma) / max(max(histLuma, currentLuma), 0.001);
    if (lumaDiff > 0.5)
        alpha = saturate(alpha + lumaDiff * 0.5);  // Increase new frame weight on disocclusion

    float4 result = lerp(history, spatialFiltered, alpha);

    // Clamp to prevent energy accumulation over many frames
    result = max(result, 0.0);

    GIOutput[DTid.xy] = result;
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Pass 4: Upsample + Composite PS (fullscreen)
// ═══════════════════════════════════════════════════════════════════════════

static const char kCompositePS[] = R"HLSL(
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Pass 4: Bilateral upsample from half-res + additive blend
//
// Reads the denoised half-res GI buffer and upsamples to full resolution
// using a bilateral filter guided by full-res depth.  The pixel shader
// outputs upsampled GI color with alpha=1 for the additive blend state
// configured on the C++ side (SrcBlend=ONE, DestBlend=ONE).

cbuffer CompositeCB : register(b0)
{
    uint   ScreenW;        // offset 0   Full-res width
    uint   ScreenH;        // offset 4   Full-res height
    uint   HalfW;          // offset 8   Half-res width
    uint   HalfH;          // offset 12  Half-res height
    float  NearZ;          // offset 16
    float  FarZ;           // offset 20
    float  GIIntensity;    // offset 24  (NOT applied here — already in trace)
    float  pad0;           // offset 28
};

// t0 = Denoised half-res GI (from ping-pong history write)
// t1 = Full-res depth (HiZ, reversed-Z)
// t31 = Linear depth (R32_FLOAT, full-res)
Texture2D<float4> GITex       : register(t0);
Texture2D<float>  DepthTex    : register(t1);
Texture2D<float>  LinearDepth : register(t31);

SamplerState LinearSampler : register(s0);
SamplerState PointSampler  : register(s1);

struct PSInput
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

// Linearize reversed-Z depth
float LinearizeDepth(float z_rev, float N, float F)
{
    return N * F / (N + z_rev * (F - N));
}

float4 main(PSInput input) : SV_Target
{
    // Full-res pixel coordinate
    int2 fullPx = int2(input.position.xy);

    // Read full-res depth
    float rawDepth = DepthTex.Load(int3(fullPx, 0));

    // Sky check: reversed-Z sky near 0 — output zero (no GI contribution)
    if (rawDepth < 0.0001)
        return float4(0, 0, 0, 1);

    float centerZ = LinearizeDepth(rawDepth, NearZ, FarZ);

    // First-person skip
    if (centerZ < 16.0)
        return float4(0, 0, 0, 1);

    // ── Bilateral upsample from half-res ─────────────────────────────────
    // Sample a 2x2 neighborhood in the half-res GI buffer, weight by
    // depth similarity to the full-res center pixel.
    float2 halfUV = (float2(fullPx) + 0.5) / float2(ScreenW, ScreenH);
    float2 halfTexelSize = 1.0 / float2(HalfW, HalfH);

    // Half-res pixel coordinate (fractional)
    float2 halfPxF = halfUV * float2(HalfW, HalfH) - 0.5;
    int2   halfPx0 = int2(floor(halfPxF));
    float2 frac2   = halfPxF - float2(halfPx0);

    float4 accumGI     = 0.0;
    float  accumWeight = 0.0;

    // 2x2 bilinear tap with depth-guided weighting
    for (int dy = 0; dy <= 1; ++dy)
    {
        for (int dx = 0; dx <= 1; ++dx)
        {
            int2 samplePx = halfPx0 + int2(dx, dy);
            samplePx = clamp(samplePx, int2(0, 0), int2(HalfW - 1, HalfH - 1));

            // Read half-res GI
            float4 sampleGI = GITex.Load(int3(samplePx, 0));

            // Read depth at corresponding full-res pixel
            int2 sampleFullPx = samplePx * 2;
            sampleFullPx = clamp(sampleFullPx, int2(0, 0), int2(ScreenW - 1, ScreenH - 1));
            float sampleRawZ = DepthTex.Load(int3(sampleFullPx, 0));
            float sampleZ    = LinearizeDepth(max(sampleRawZ, 0.0001), NearZ, FarZ);

            // Bilinear weight
            float bx = (dx == 0) ? (1.0 - frac2.x) : frac2.x;
            float by = (dy == 0) ? (1.0 - frac2.y) : frac2.y;
            float bilinearW = bx * by;

            // Depth similarity weight (edge stopping)
            float depthDiff = abs(sampleZ - centerZ) / max(centerZ, 0.001);
            float depthW    = exp(-depthDiff * depthDiff * 50.0);

            float w = bilinearW * depthW;
            accumGI     += sampleGI * w;
            accumWeight += w;
        }
    }

    float3 gi = (accumWeight > 0.001) ? accumGI.rgb / accumWeight : 0.0;

    // GI intensity already applied in trace pass — do NOT multiply again here.
    // Output with alpha=1 for additive blend state (SrcBlend=ONE, DestBlend=ONE).
    return float4(max(gi, 0.0), 1.0);
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  CB structures — must match HLSL cbuffers exactly
// ═══════════════════════════════════════════════════════════════════════════

struct alignas(16) VoxelizeCBData
{
    float    projMatrix[16]; // 64 bytes — projection matrix for view-space reconstruction
    uint32_t screenW;
    uint32_t screenH;
    float    nearZ;
    float    farZ;
    float    voxelRange;
    float    pad[3];
};
static_assert(sizeof(VoxelizeCBData) == 96, "VoxelizeCB must be 96 bytes");

struct alignas(16) TraceGICBData
{
    float    projMatrix[16]; // 64 bytes — projection matrix for view-space reconstruction
    uint32_t halfW;
    uint32_t halfH;
    uint32_t screenW;
    uint32_t screenH;
    float    nearZ;
    float    farZ;
    float    voxelRange;
    int32_t  rayCount;
    int32_t  maxSteps;
    float    giIntensity;
    uint32_t frameIndex;
    float    fpDepthThreshold;  // first-person depth mask (view-space units)
};
static_assert(sizeof(TraceGICBData) == 112, "TraceGICB must be 112 bytes");

struct alignas(16) DenoiseCBData
{
    uint32_t halfW;
    uint32_t halfH;
    uint32_t screenW;
    uint32_t screenH;
    float    nearZ;
    float    farZ;
    float    temporalAlpha;
    float    depthThreshold;
    uint32_t frameIndex;
    float    pad[3];
};
static_assert(sizeof(DenoiseCBData) == 48, "DenoiseCB must be 48 bytes");

struct alignas(16) CompositeCBData
{
    uint32_t screenW;
    uint32_t screenH;
    uint32_t halfW;
    uint32_t halfH;
    float    nearZ;
    float    farZ;
    float    giIntensity;
    float    pad0;
};
static_assert(sizeof(CompositeCBData) == 32, "CompositeCB must be 32 bytes");


// ═══════════════════════════════════════════════════════════════════════════
//  Initialize
// ═══════════════════════════════════════════════════════════════════════════

bool SSGIRenderer::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc)
{
    if (m_initialized) return true;
    if (!dev || !ctx || !sc) return false;

    auto& cm  = ComputeManager::Get();
    auto& rpm = RenderPassManager::Get();
    auto& pl  = RenderPipeline::Get();

    if (!cm.IsInitialized() || !rpm.IsInitialized() || !pl.IsInitialized()) {
        SKSE::log::error("SSGIRenderer: prerequisites not initialized");
        return false;
    }

    m_device  = dev;
    m_context = ctx;

    // Get backbuffer dimensions
    ID3D11Texture2D* backTex = nullptr;
    HRESULT hr = sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                reinterpret_cast<void**>(&backTex));
    if (FAILED(hr) || !backTex) {
        SKSE::log::error("SSGIRenderer: failed to get backbuffer (0x{:X})",
                         static_cast<uint32_t>(hr));
        return false;
    }

    D3D11_TEXTURE2D_DESC bbDesc;
    backTex->GetDesc(&bbDesc);
    backTex->Release();

    m_screenW = bbDesc.Width;
    m_screenH = bbDesc.Height;
    m_halfW   = (m_screenW + 1) / 2;
    m_halfH   = (m_screenH + 1) / 2;

    if (!CompileShaders()) return false;
    if (!CreateResources()) {
        ReleaseResources();
        return false;
    }

    // Register as PostGeometry pipeline pass
    m_pipelineHandle = pl.AddPass({
        .name     = "SSGI",
        .stage    = PipelineStage::PostGeometry,
        .priority = 20,  // After HiZ (priority 10), before other effects
        .execute  = [this](PassContext& ctx) { ExecutePass(ctx); },
    });

    m_initialized = true;
    m_firstFrame  = true;
    m_frameIndex  = 0;

    SKSE::log::info("SSGIRenderer: initialized ({}x{}, half={}x{}, Y-SH2={}^3, CoCg={}^3, rays={}, steps={}, t{})",
                    m_screenW, m_screenH, m_halfW, m_halfH, kVoxelRes, kCoCgVoxelRes,
                    m_rayCount, m_maxSteps, kSRVSlot);
    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Compile all shaders
// ═══════════════════════════════════════════════════════════════════════════

bool SSGIRenderer::CompileShaders()
{
    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    auto CompileCS = [&](const char* name, const char* source,
                         ID3D11ComputeShader** outCS) -> bool
    {
        ID3DBlob* blob = ShaderLoader::Compile(name, source, "main", "cs_5_0", flags);
        if (!blob) {
            SKSE::log::error("SSGIRenderer: {} compile failed", name);
            return false;
        }

        HRESULT hr = m_device->CreateComputeShader(blob->GetBufferPointer(),
                                            blob->GetBufferSize(),
                                            nullptr, outCS);
        blob->Release();
        if (FAILED(hr)) {
            SKSE::log::error("SSGIRenderer: CreateComputeShader failed for {} (0x{:X})",
                             name, static_cast<uint32_t>(hr));
            return false;
        }
        return true;
    };

    if (!CompileCS("SSGI_Voxelize", kVoxelizeCS, &m_voxelizeCS)) return false;
    if (!CompileCS("SSGI_Trace",    kTraceGICS,  &m_traceCS))    return false;
    if (!CompileCS("SSGI_Denoise",  kDenoiseCS,  &m_denoiseCS))  return false;

    // Compile composite PS via RenderPassManager
    m_compositePass = RenderPassManager::Get().RegisterPass({
        .name     = "SSGIComposite",
        .psSource = kCompositePS,
    });
    if (!m_compositePass) {
        SKSE::log::error("SSGIRenderer: failed to register SSGIComposite pass");
        return false;
    }

    return true;
}

bool SSGIRenderer::RecompileShaders()
{
    SafeRelease(m_voxelizeCS);
    SafeRelease(m_traceCS);
    SafeRelease(m_denoiseCS);
    return CompileShaders();
}


// ═══════════════════════════════════════════════════════════════════════════
//  Create GPU resources
// ═══════════════════════════════════════════════════════════════════════════

bool SSGIRenderer::CreateResources()
{
    HRESULT hr;

    // ── 3D voxel grid: 128^3 R16G16B16A16_FLOAT ────────────────────────
    {
        D3D11_TEXTURE3D_DESC desc = {};
        desc.Width     = kVoxelRes;
        desc.Height    = kVoxelRes;
        desc.Depth     = kVoxelRes;
        desc.MipLevels = 1;
        desc.Format    = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.Usage     = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

        hr = m_device->CreateTexture3D(&desc, nullptr, &m_voxelGrid);
        if (FAILED(hr)) {
            SKSE::log::error("SSGIRenderer: failed to create voxel grid (0x{:X})",
                             static_cast<uint32_t>(hr));
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = desc.Format;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE3D;
        srvDesc.Texture3D.MostDetailedMip = 0;
        srvDesc.Texture3D.MipLevels       = 1;
        hr = m_device->CreateShaderResourceView(m_voxelGrid, &srvDesc, &m_voxelGridSRV);
        if (FAILED(hr)) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format               = desc.Format;
        uavDesc.ViewDimension        = D3D11_UAV_DIMENSION_TEXTURE3D;
        uavDesc.Texture3D.MipSlice   = 0;
        uavDesc.Texture3D.FirstWSlice = 0;
        uavDesc.Texture3D.WSize       = kVoxelRes;
        hr = m_device->CreateUnorderedAccessView(m_voxelGrid, &uavDesc, &m_voxelGridUAV);
        if (FAILED(hr)) return false;

        // ── Second 3D grid: CoCg chrominance + opacity ──────────────────
        // Half resolution of Y-SH2 grid (64^3 vs 128^3).  Chroma needs less
        // spatial detail — trilinear sampling in the trace smooths it fine.
        desc.Width  = kCoCgVoxelRes;
        desc.Height = kCoCgVoxelRes;
        desc.Depth  = kCoCgVoxelRes;
        hr = m_device->CreateTexture3D(&desc, nullptr, &m_voxelGridCoCg);
        if (FAILED(hr)) {
            SKSE::log::error("SSGIRenderer: failed to create CoCg voxel grid (0x{:X})",
                             static_cast<uint32_t>(hr));
            return false;
        }
        hr = m_device->CreateShaderResourceView(m_voxelGridCoCg, &srvDesc, &m_voxelGridCoCgSRV);
        if (FAILED(hr)) return false;
        uavDesc.Texture3D.WSize = kCoCgVoxelRes;  // 64 (not 128)
        hr = m_device->CreateUnorderedAccessView(m_voxelGridCoCg, &uavDesc, &m_voxelGridCoCgUAV);
        if (FAILED(hr)) return false;
    }

    // Backbuffer copy for albedo (SRV only)
    if (!CreateGPUTexture(m_device, m_screenW, m_screenH, DXGI_FORMAT_R8G8B8A8_UNORM,
                          &m_backbufferCopy, &m_backbufferCopySRV, nullptr, "backbufferCopy"))
        return false;
    // Half-res raw GI buffer
    if (!CreateGPUTexture(m_device, m_halfW, m_halfH, DXGI_FORMAT_R16G16B16A16_FLOAT, &m_giRaw, &m_giRawSRV, &m_giRawUAV, "giRaw"))
        return false;
    // Ping-pong GI history (half-res)
    for (int i = 0; i < 2; ++i) {
        char name[32]; snprintf(name, sizeof(name), "giHistory%d", i);
        if (!CreateGPUTexture(m_device, m_halfW, m_halfH, DXGI_FORMAT_R16G16B16A16_FLOAT,
                              &m_giHistory[i], &m_giHistorySRV[i], &m_giHistoryUAV[i], name))
            return false;
    }
    // ── Constant buffers ────────────────────────────────────────────────
    // Constant buffers
    if (!CreateCB(m_device, sizeof(VoxelizeCBData), &m_voxelizeCB)) return false;
    if (!CreateCB(m_device, sizeof(TraceGICBData), &m_traceCB)) return false;
    if (!CreateCB(m_device, sizeof(DenoiseCBData), &m_denoiseCB)) return false;
    if (!CreateCB(m_device, sizeof(CompositeCBData), &m_compositeCB)) return false;
    // ── Additive blend state ────────────────────────────────────────────
    {
        D3D11_BLEND_DESC desc = {};
        desc.RenderTarget[0].BlendEnable    = TRUE;
        desc.RenderTarget[0].SrcBlend       = D3D11_BLEND_ONE;
        desc.RenderTarget[0].DestBlend      = D3D11_BLEND_ONE;
        desc.RenderTarget[0].BlendOp        = D3D11_BLEND_OP_ADD;
        desc.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_ONE;
        desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        desc.RenderTarget[0].BlendOpAlpha   = D3D11_BLEND_OP_ADD;
        desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

        hr = m_device->CreateBlendState(&desc, &m_additiveBlend);
        if (FAILED(hr)) return false;
    }

    // ── Samplers ────────────────────────────────────────────────────────
    {
        D3D11_SAMPLER_DESC desc = {};
        desc.Filter   = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.MaxAnisotropy  = 1;
        desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        desc.MaxLOD         = D3D11_FLOAT32_MAX;
        hr = m_device->CreateSamplerState(&desc, &m_linearSampler);
        if (FAILED(hr)) return false;

        desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        hr = m_device->CreateSamplerState(&desc, &m_pointSampler);
        if (FAILED(hr)) return false;
    }

    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Acquire depth SRV from currently-bound DSV
// ═══════════════════════════════════════════════════════════════════════════

bool SSGIRenderer::AcquireDepthSRV()
{
    if (m_depthSRV) {
        m_depthSRV->Release();
        m_depthSRV = nullptr;
    }

    ID3D11DepthStencilView* dsv = nullptr;
    m_context->OMGetRenderTargets(0, nullptr, &dsv);
    if (!dsv) return false;

    ID3D11Resource* res = nullptr;
    dsv->GetResource(&res);
    dsv->Release();
    if (!res) return false;

    ID3D11Texture2D* depthTex = nullptr;
    HRESULT hr = res->QueryInterface(__uuidof(ID3D11Texture2D),
                                     reinterpret_cast<void**>(&depthTex));
    res->Release();
    if (FAILED(hr) || !depthTex) return false;

    D3D11_TEXTURE2D_DESC desc;
    depthTex->GetDesc(&desc);

    if (!(desc.BindFlags & D3D11_BIND_SHADER_RESOURCE)) {
        depthTex->Release();
        return false;
    }

    DXGI_FORMAT srvFormat;
    switch (desc.Format) {
        case DXGI_FORMAT_R32_TYPELESS:      srvFormat = DXGI_FORMAT_R32_FLOAT;              break;
        case DXGI_FORMAT_R24G8_TYPELESS:    srvFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;  break;
        case DXGI_FORMAT_R32G8X24_TYPELESS: srvFormat = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS; break;
        case DXGI_FORMAT_R16_TYPELESS:      srvFormat = DXGI_FORMAT_R16_UNORM;              break;
        case DXGI_FORMAT_R32_FLOAT:         srvFormat = DXGI_FORMAT_R32_FLOAT;              break;
        case DXGI_FORMAT_R16_UNORM:         srvFormat = DXGI_FORMAT_R16_UNORM;              break;
        default:
            depthTex->Release();
            return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                    = srvFormat;
    srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels       = 1;

    hr = m_device->CreateShaderResourceView(depthTex, &srvDesc, &m_depthSRV);
    depthTex->Release();

    return SUCCEEDED(hr) && m_depthSRV != nullptr;
}


// ═══════════════════════════════════════════════════════════════════════════
//  GetGISRV — returns the most recently denoised GI buffer SRV
// ═══════════════════════════════════════════════════════════════════════════

ID3D11ShaderResourceView* SSGIRenderer::GetGISRV() const
{
    if (!m_initialized) return nullptr;
    // Return temporally accumulated output (falls back to raw if temporal not ready)
    auto* temporal = m_giHistorySRV[1 - m_pingPongIdx];
    return temporal ? temporal : m_giRawSRV;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Per-frame execution (called by RenderPipeline at PostGeometry stage)
// ═══════════════════════════════════════════════════════════════════════════

void SSGIRenderer::ExecutePass(PassContext& ctx)
{
    if (!m_initialized || !m_enabled) return;

    // Gate: scene matrices must be valid (camera data verified non-garbage)
    if (!SceneMatrices::Get().IsValid()) return;

    auto& cm = ComputeManager::Get();

    // ── Acquire depth ───────────────────────────────────────────────────
    // Prefer proxy-captured depth (always available during Present hook)
    ID3D11ShaderResourceView* depthSRV = D3D11Hook::GetGameDepthSRV();
    if (!depthSRV) {
        if (!AcquireDepthSRV()) return;
        depthSRV = m_depthSRV;
    }

    // HiZ is now built at PostGeometry:1 (before this pass),
    // so it's fresh during both mid-frame and Present-time dispatch.
    {
        auto& hiz = HiZPyramid::Get();
        if (hiz.IsInitialized() && hiz.GetSRV())
            depthSRV = hiz.GetSRV();
    }

    // ── Copy scene color for albedo ─────────────────────────────────────
    // During mid-frame dispatch, the backbuffer doesn't contain the scene —
    // the game renders to an internal RT exposed via ctx.gameSceneRTV.
    // Extract the texture from that RTV first; fall back to swapchain only
    // when gameSceneRTV is null (Present-time dispatch).
    {
        ID3D11Texture2D* sceneTex = nullptr;

        if (ctx.gameSceneRTV) {
            ID3D11Resource* res = nullptr;
            ctx.gameSceneRTV->GetResource(&res);
            if (res) {
                res->QueryInterface(__uuidof(ID3D11Texture2D),
                                    reinterpret_cast<void**>(&sceneTex));
                res->Release();
            }
        }

        if (!sceneTex) {
            // Fallback: swapchain backbuffer (Present-time path)
            auto* sc = ctx.swapChain ? ctx.swapChain : D3D11Hook::GetSwapChain();
            if (sc) {
                sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                              reinterpret_cast<void**>(&sceneTex));
            }
        }

        if (!sceneTex) {
            if (m_depthSRV) { m_depthSRV->Release(); m_depthSRV = nullptr; }
            return;
        }

        // Ensure copy texture format matches source (game scene RT is HDR float,
        // but m_backbufferCopy was created as R8G8B8A8_UNORM at init time).
        D3D11_TEXTURE2D_DESC srcDesc;
        sceneTex->GetDesc(&srcDesc);
        D3D11_TEXTURE2D_DESC copyDesc;
        m_backbufferCopy->GetDesc(&copyDesc);
        if (srcDesc.Format != copyDesc.Format ||
            srcDesc.Width  != copyDesc.Width  ||
            srcDesc.Height != copyDesc.Height)
        {
            m_backbufferCopySRV->Release(); m_backbufferCopySRV = nullptr;
            m_backbufferCopy->Release();    m_backbufferCopy    = nullptr;

            D3D11_TEXTURE2D_DESC newDesc = srcDesc;
            newDesc.BindFlags      = D3D11_BIND_SHADER_RESOURCE;
            newDesc.Usage          = D3D11_USAGE_DEFAULT;
            newDesc.CPUAccessFlags = 0;
            newDesc.MiscFlags      = 0;
            newDesc.MipLevels      = 1;
            newDesc.ArraySize      = 1;
            newDesc.SampleDesc     = {1, 0};
            m_device->CreateTexture2D(&newDesc, nullptr, &m_backbufferCopy);

            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format                    = newDesc.Format;
            srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.MipLevels       = 1;
            m_device->CreateShaderResourceView(m_backbufferCopy, &srvDesc,
                                                &m_backbufferCopySRV);

            SKSE::log::info("SSGI: backbuffer copy recreated — {}x{} fmt={}",
                srcDesc.Width, srcDesc.Height, static_cast<uint32_t>(srcDesc.Format));
        }

        ctx.context->CopyResource(m_backbufferCopy, sceneTex);
        sceneTex->Release();
    }

    // Camera near/far from SceneData (reconstructed from CameraTracker)
    auto& scene = SceneMatrices::Get();
    const float nearZ = scene.NearClip();
    const float farZ  = scene.FarClip();

    // ── First frame: clear history buffers ──────────────────────────────
    if (m_firstFrame) {
        float clearColor[4] = {0, 0, 0, 0};
        ctx.context->ClearUnorderedAccessViewFloat(m_giHistoryUAV[0], clearColor);
        ctx.context->ClearUnorderedAccessViewFloat(m_giHistoryUAV[1], clearColor);
        m_firstFrame = false;
        m_pingPongIdx = 0;
    }

    cm.SaveCSState();

    // ── Bind pre-computed linear depth at t31 for all CS passes ─────────
    auto* linearDepthSRV = SharedGPUResources::Get().GetLinearDepthSRV();
    ctx.context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &linearDepthSRV);

    // ═════════════════════════════════════════════════════════════════════
    //  Pass 1: Voxelize — depth + albedo → 128^3 voxel grid
    // ═════════════════════════════════════════════════════════════════════
    {
        // Clear both voxel grids (Y-SH2 + CoCg)
        float clearVal[4] = {0, 0, 0, 0};
        ctx.context->ClearUnorderedAccessViewFloat(m_voxelGridUAV, clearVal);
        ctx.context->ClearUnorderedAccessViewFloat(m_voxelGridCoCgUAV, clearVal);

        // Update CB
        VoxelizeCBData cb;
        std::memcpy(cb.projMatrix, scene.ProjMatrix(), sizeof(float) * 16);
        cb.screenW    = m_screenW;
        cb.screenH    = m_screenH;
        cb.nearZ      = nearZ;
        cb.farZ       = farZ;
        cb.voxelRange = m_voxelRange;
        cb.pad[0] = cb.pad[1] = cb.pad[2] = 0;

        UploadCB(ctx.context, m_voxelizeCB, &cb, sizeof(cb));

        ID3D11ShaderResourceView* srvs[] = { depthSRV, m_backbufferCopySRV };
        ctx.context->CSSetShaderResources(0, 2, srvs);

        // u0 = Y-SH2 grid, u1 = CoCg grid
        ID3D11UnorderedAccessView* uavs[] = { m_voxelGridUAV, m_voxelGridCoCgUAV };
        ctx.context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);

        ctx.context->CSSetConstantBuffers(0, 1, &m_voxelizeCB);

        ctx.context->CSSetShader(m_voxelizeCS, nullptr, 0);

        UINT groupsX = (m_screenW + 7) / 8;
        UINT groupsY = (m_screenH + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        // Clear bindings
        ID3D11ShaderResourceView* nullSRVs[2] = {};
        ctx.context->CSSetShaderResources(0, 2, nullSRVs);
        ID3D11UnorderedAccessView* nullUAVs[2] = {};
        ctx.context->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
    }

    // ═════════════════════════════════════════════════════════════════════
    //  Pass 2: TraceGI — hemisphere rays through voxel grid → half-res GI
    // ═════════════════════════════════════════════════════════════════════
    {
        TraceGICBData cb;
        std::memcpy(cb.projMatrix, scene.ProjMatrix(), sizeof(float) * 16);
        cb.halfW       = m_halfW;
        cb.halfH       = m_halfH;
        cb.screenW     = m_screenW;
        cb.screenH     = m_screenH;
        cb.nearZ       = nearZ;
        cb.farZ        = farZ;
        cb.voxelRange  = m_voxelRange;
        cb.rayCount    = m_rayCount;
        cb.maxSteps    = m_maxSteps;
        cb.giIntensity = m_giIntensity;
        cb.frameIndex  = m_frameIndex;
        cb.fpDepthThreshold = 16.0f;  // ~16 view-space units

        UploadCB(ctx.context, m_traceCB, &cb, sizeof(cb));

        // t0=depth, t1=albedo, t2=Y-SH2 voxel grid, t3=CoCg voxel grid, t30=blue noise
        ID3D11ShaderResourceView* srvs[] = { depthSRV, m_backbufferCopySRV, m_voxelGridSRV, m_voxelGridCoCgSRV };
        ctx.context->CSSetShaderResources(0, 4, srvs);
        auto* bnSRV = SharedGPUResources::Get().GetBlueNoiseSRV();
        if (bnSRV) ctx.context->CSSetShaderResources(SharedGPUResources::kBlueNoiseSlot, 1, &bnSRV);

        ID3D11SamplerState* samplers[] = { m_linearSampler };
        ctx.context->CSSetSamplers(0, 1, samplers);

        ID3D11UnorderedAccessView* uavs[] = { m_giRawUAV };
        ctx.context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

        ctx.context->CSSetConstantBuffers(0, 1, &m_traceCB);

        ctx.context->CSSetShader(m_traceCS, nullptr, 0);

        UINT groupsX = (m_halfW + 7) / 8;
        UINT groupsY = (m_halfH + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        ID3D11ShaderResourceView* nullSRVs[4] = {};
        ctx.context->CSSetShaderResources(0, 4, nullSRVs);
        { ID3D11ShaderResourceView* n = nullptr; ctx.context->CSSetShaderResources(SharedGPUResources::kBlueNoiseSlot, 1, &n); }
        ID3D11UnorderedAccessView* nullUAVs[1] = {};
        ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
    }

    // ═════════════════════════════════════════════════════════════════════
    //  Pass 3: Denoise — bilateral temporal filter (ping-pong)
    // ═════════════════════════════════════════════════════════════════════
    {
        int readIdx  = 1 - m_pingPongIdx;
        int writeIdx = m_pingPongIdx;

        DenoiseCBData cb;
        cb.halfW          = m_halfW;
        cb.halfH          = m_halfH;
        cb.screenW        = m_screenW;
        cb.screenH        = m_screenH;
        cb.nearZ          = nearZ;
        cb.farZ           = farZ;
        cb.temporalAlpha  = 0.2f;
        cb.depthThreshold = 0.05f;
        cb.frameIndex     = m_frameIndex;

        UploadCB(ctx.context, m_denoiseCB, &cb, sizeof(cb));

        // t0 = raw GI, t1 = history, t2 = depth, t3 = motion vectors
        auto* motionSRV = MotionVectorGen::Get().GetMotionSRV();
        ID3D11ShaderResourceView* srvs[] = { m_giRawSRV, m_giHistorySRV[readIdx], depthSRV, motionSRV };
        ctx.context->CSSetShaderResources(0, 4, srvs);
        // Bind linear sampler at s0 for bilinear history + motion vector sampling
        ctx.context->CSSetSamplers(0, 1, &m_linearSampler);

        // u0 = output denoised GI
        ID3D11UnorderedAccessView* uavs[] = { m_giHistoryUAV[writeIdx] };
        ctx.context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

        ctx.context->CSSetConstantBuffers(0, 1, &m_denoiseCB);

        ctx.context->CSSetShader(m_denoiseCS, nullptr, 0);

        UINT groupsX = (m_halfW + 7) / 8;
        UINT groupsY = (m_halfH + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        ID3D11ShaderResourceView* nullSRVs[4] = {};
        ctx.context->CSSetShaderResources(0, 4, nullSRVs);
        ID3D11UnorderedAccessView* nullUAVs[1] = {};
        ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
    }

    // ── Unbind linear depth from CS t31 ─────────────────────────────────
    {
        ID3D11ShaderResourceView* nullSRV = nullptr;
        ctx.context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &nullSRV);
    }

    cm.RestoreCSState();

    // ═════════════════════════════════════════════════════════════════════
    //  Pass 4: Upsample + Composite (fullscreen PS, additive blend)
    // ═════════════════════════════════════════════════════════════════════
    // During mid-frame dispatch, composite to the game's scene RT (not the
    // swapchain backbuffer, which doesn't hold the scene yet). Fall back to
    // swapchain only at Present-time.
    {
        ID3D11RenderTargetView* compositeRTV = nullptr;
        bool ownRTV = false;  // true when we created the RTV and must release it

        if (ctx.gameSceneRTV) {
            // Mid-frame: composite directly into the game's scene RT
            compositeRTV = ctx.gameSceneRTV;
            compositeRTV->AddRef();
            ownRTV = true;
        } else {
            // Present-time fallback: create an RTV from the swapchain backbuffer
            auto* sc = ctx.swapChain ? ctx.swapChain : D3D11Hook::GetSwapChain();
            if (sc) {
                ID3D11Texture2D* backTex = nullptr;
                if (SUCCEEDED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                             reinterpret_cast<void**>(&backTex)))) {
                    D3D11_TEXTURE2D_DESC bbDesc;
                    backTex->GetDesc(&bbDesc);

                    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
                    rtvDesc.Format        = bbDesc.Format;
                    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
                    m_device->CreateRenderTargetView(backTex, &rtvDesc, &compositeRTV);
                    backTex->Release();
                    ownRTV = true;
                }
            }
        }

        if (compositeRTV) {
            // Update composite CB
            CompositeCBData cb;
            cb.screenW     = m_screenW;
            cb.screenH     = m_screenH;
            cb.halfW       = m_halfW;
            cb.halfH       = m_halfH;
            cb.nearZ       = nearZ;
            cb.farZ        = farZ;
            cb.giIntensity = m_giIntensity;
            cb.pad0        = 0;

            // Denoised GI SRV is from the write side of this frame's ping-pong
            int writeIdx = m_pingPongIdx;
            ID3D11ShaderResourceView* compositeSRVs[] = {
                m_giHistorySRV[writeIdx],  // t0: denoised half-res GI
                depthSRV                    // t1: full-res depth
            };
            ID3D11SamplerState* compositeSamplers[] = {
                m_linearSampler,  // s0
                m_pointSampler    // s1
            };

            // Bind linear depth at PS t31 for composite pixel shader
            auto* psLinearDepthSRV = SharedGPUResources::Get().GetLinearDepthSRV();
            ctx.context->PSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &psLinearDepthSRV);

            RenderPassManager::Get().Execute({
                .passID       = m_compositePass,
                .rtv          = compositeRTV,
                .srvs         = compositeSRVs,
                .srvCount     = 2,
                .samplers     = compositeSamplers,
                .samplerCount = 2,
                .cbData       = &cb,
                .cbSize       = sizeof(cb),
                .blendState   = m_additiveBlend,
            });

            // Unbind linear depth from PS t31
            {
                ID3D11ShaderResourceView* nullSRV = nullptr;
                ctx.context->PSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &nullSRV);
            }

            if (ownRTV)
                compositeRTV->Release();
        }
    }

    // ── Register GI SRV for injection at t26 ────────────────────────────
    {
        int writeIdx = m_pingPongIdx;
        auto& inj = SRVInjector::Get();
        if (inj.IsInitialized() && m_giHistorySRV[writeIdx]) {
            inj.RegisterSRV(kSRVSlot, m_giHistorySRV[writeIdx]);
        }
    }

    // ── Swap ping-pong index ────────────────────────────────────────────
    m_pingPongIdx = 1 - m_pingPongIdx;

    // ── Cleanup ─────────────────────────────────────────────────────────
    if (m_depthSRV) {
        m_depthSRV->Release();
        m_depthSRV = nullptr;
    }

    ++m_frameIndex;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Shutdown + ReleaseResources
// ═══════════════════════════════════════════════════════════════════════════

void SSGIRenderer::ReleaseResources()
{
    auto SafeRelease = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };

    // Voxelize (Y-SH2 + CoCg grids)
    SafeRelease(m_voxelGridUAV);
    SafeRelease(m_voxelGridSRV);
    SafeRelease(m_voxelGrid);
    SafeRelease(m_voxelGridCoCgUAV);
    SafeRelease(m_voxelGridCoCgSRV);
    SafeRelease(m_voxelGridCoCg);
    SafeRelease(m_voxelizeCB);
    SafeRelease(m_backbufferCopySRV);
    SafeRelease(m_backbufferCopy);

    // TraceGI
    SafeRelease(m_giRawUAV);
    SafeRelease(m_giRawSRV);
    SafeRelease(m_giRaw);
    SafeRelease(m_traceCB);

    // Denoise
    for (int i = 0; i < 2; ++i) {
        SafeRelease(m_giHistoryUAV[i]);
        SafeRelease(m_giHistorySRV[i]);
        SafeRelease(m_giHistory[i]);
    }
    SafeRelease(m_denoiseCB);

    // Composite
    SafeRelease(m_compositeCB);
    SafeRelease(m_additiveBlend);
    SafeRelease(m_linearSampler);
    SafeRelease(m_pointSampler);

    // Depth
    SafeRelease(m_depthSRV);

    // Compute shaders
    SafeRelease(m_voxelizeCS);
    SafeRelease(m_traceCS);
    SafeRelease(m_denoiseCS);
}

void SSGIRenderer::Shutdown()
{
    ReleaseResources();
    m_initialized = false;
    m_screenW = m_screenH = m_halfW = m_halfH = 0;
}

} // namespace SB
