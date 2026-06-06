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

#include "SharedRAW.hlsli"

// RGB -> YCoCg (luminance + chroma) — uses SharedRAW's RGBToYCoCg but with
// different capitalization for backward compat
float3 RGBtoYCoCg(float3 rgb) { return RGBToYCoCg(rgb); }

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

    // Read scene color (albedo proxy) and linearize from Skyrim's gamma space.
    // Without this, gamma-encoded greens from foliage produce oversaturated
    // green bounce light because GI math assumes linear radiance.
    float4 color = SceneColor.Load(int3(DTid.xy, 0));
    float3 rgb   = pow(max(color.rgb, 0.0), 2.2);

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
