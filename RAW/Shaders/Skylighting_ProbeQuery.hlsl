// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// ProbeQuery CS — No-op stub.  Writes 1.0 (full sky visibility) for every pixel.
// The screen-space pass (kSkylightScreenSpaceCS) does the real work.
// Probe-grid lookup can be added here later.

cbuffer SkylightCB : register(b0)
{
    float4x4 InvViewProj;
    uint2    ScreenDims;
    float    NearZ;
    float    FarZ;
    float3   GridCenter;
    float    ProbeRangeXY;
    float    ProbeRangeZ;
    float    Intensity;
    uint     FrameIndex;
    float    pad0;
};

Texture2D<float>  DepthTex  : register(t0);

RWTexture2D<float> SkyOutput : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= ScreenDims.x || DTid.y >= ScreenDims.y)
        return;

    SkyOutput[int2(DTid.xy)] = 1.0;
}
