// Screen-space motion vector generation via depth reprojection
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Per pixel:
//   1. Load reversed-Z depth, reconstruct NDC position
//   2. Unproject to world space via InvViewProj
//   3. Reproject to previous frame via PrevViewProj
//   4. Motion vector = currentUV - previousUV
//
// Output: R16G16_FLOAT motion vectors in UV space.
// Sky pixels (reversed-Z depth < 0.0001) output zero motion.

cbuffer MotionVectorCB : register(b0)
{
    row_major float4x4 InvViewProj;
    row_major float4x4 PrevViewProj;
    uint2    Dimensions;
    float2   RcpDimensions;
}

Texture2D<float>     DepthBuffer   : register(t0);
RWTexture2D<float2>  MotionVectors : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    if (any(dtid.xy >= Dimensions))
        return;

    int2   pixelCoord = int2(dtid.xy);
    float2 uv = (float2(pixelCoord) + 0.5) * RcpDimensions;

    // Load reversed-Z depth (near=1, far=0)
    float depth = DepthBuffer.Load(int3(pixelCoord, 0));

    // Sky pixels: output zero motion (no meaningful reprojection)
    if (depth < 0.0001)
    {
        MotionVectors[dtid.xy] = float2(0.0, 0.0);
        return;
    }

    // Reconstruct clip-space position from UV + depth
    // UV -> NDC: x = uv.x * 2 - 1, y = (1 - uv.y) * 2 - 1
    float2 ndc = float2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);
    float4 clipPos = float4(ndc, depth, 1.0);

    // Unproject to world space
    float4 worldPos = mul(clipPos, InvViewProj);
    worldPos /= worldPos.w;

    // Reproject to previous frame
    float4 prevClip = mul(worldPos, PrevViewProj);
    float2 prevNDC  = prevClip.xy / prevClip.w;

    // NDC -> UV: x = ndc.x * 0.5 + 0.5, y = 0.5 - ndc.y * 0.5
    float2 prevUV = float2(prevNDC.x * 0.5 + 0.5, 0.5 - prevNDC.y * 0.5);

    // Motion vector: current UV minus previous UV
    // Positive X = object moved right, positive Y = object moved down
    float2 motion = uv - prevUV;

    MotionVectors[dtid.xy] = motion;
}
