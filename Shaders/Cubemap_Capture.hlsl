// Copyright (c) 2026 Zain D. Harper. All rights reserved.
// Capture current scene into one face of a cubemap via reprojection from the backbuffer.

cbuffer CubemapCaptureCB : register(b0)
{
    float4x4 faceViewMatrix;   // View matrix for this cube face
    float4x4 faceProjection;   // 90-degree FOV projection
    float4x4 cameraViewProj;   // Current camera VP (for reprojection)
    float4x4 cameraInvViewProj;
    uint   faceIndex;
    uint   faceResolution;
    float  blendFactor;
    float  gameTime;
    uint   screenWidth;
    uint   screenHeight;
    uint2  pad;
}

Texture2D<float4>   tBackbuffer : register(t0);
Texture2D<float>    tDepth      : register(t1);
SamplerState        sLinear     : register(s0);
RWTexture2DArray<float4> uCubemap : register(u0);

static const float PI = 3.14159265359;

// Get cubemap face direction vectors
// Returns (right, up, forward) for the given face
void GetFaceVectors(uint face, out float3 forward, out float3 right, out float3 up)
{
    // Standard cubemap face directions (LH)
    switch (face)
    {
        case 0: forward = float3( 1, 0, 0); right = float3(0, 0, 1); up = float3(0, 1, 0); break; // +X
        case 1: forward = float3(-1, 0, 0); right = float3(0, 0,-1); up = float3(0, 1, 0); break; // -X
        case 2: forward = float3( 0, 1, 0); right = float3(1, 0, 0); up = float3(0, 0,-1); break; // +Y
        case 3: forward = float3( 0,-1, 0); right = float3(1, 0, 0); up = float3(0, 0, 1); break; // -Y
        case 4: forward = float3( 0, 0, 1); right = float3(1, 0, 0); up = float3(0, 1, 0); break; // +Z
        default:forward = float3( 0, 0,-1); right = float3(-1,0, 0); up = float3(0, 1, 0); break; // -Z
    }
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= faceResolution || DTid.y >= faceResolution)
        return;

    // Map cubemap texel to a direction vector
    float2 uv = (float2(DTid.xy) + 0.5) / float(faceResolution);
    // UV to [-1, 1]
    float2 ndc = uv * 2.0 - 1.0;

    float3 forward, right, up;
    GetFaceVectors(faceIndex, forward, right, up);

    // Direction from the cube center through this texel (90-degree FOV)
    float3 dir = normalize(forward + ndc.x * right - ndc.y * up);

    // Project this direction into current camera's screen space
    // Treat as a far-away point: worldPos = dir * large_distance
    float4 clipPos = mul(cameraViewProj, float4(dir * 100000.0, 1.0));

    // Check if visible in current camera frustum
    if (clipPos.w <= 0.0)
    {
        // Behind camera — blend toward existing content (don't overwrite)
        return;
    }

    float2 screenNDC = clipPos.xy / clipPos.w;
    float2 screenUV = float2(screenNDC.x * 0.5 + 0.5, -screenNDC.y * 0.5 + 0.5);

    // Bounds check: only sample if within the current viewport
    if (screenUV.x < 0.0 || screenUV.x > 1.0 || screenUV.y < 0.0 || screenUV.y > 1.0)
        return;

    // Sample backbuffer at the projected location
    float4 sceneColor = tBackbuffer.SampleLevel(sLinear, screenUV, 0);

    // Temporal blend: smoothly update cubemap face
    float4 existing = uCubemap[uint3(DTid.xy, faceIndex)];
    float4 result = lerp(existing, sceneColor, blendFactor);

    uCubemap[uint3(DTid.xy, faceIndex)] = result;
}
