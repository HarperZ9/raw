// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// ProbeUpdate CS — No-op stub.  Fills every voxel with 1.0 (full sky).
// The screen-space pass (kSkylightScreenSpaceCS) does the real work.
// Probe-based skylighting can be added here later.

cbuffer ProbeUpdateCB : register(b0)
{
    float4x4 ViewProj;
    float3   GridCenter;
    float    ProbeRangeXY;
    float    ProbeRangeZ;
    uint     ProbeResXY;
    uint     ProbeResZ;
    uint     FrameIndex;
    uint     ScreenW;
    uint     ScreenH;
    float    pad0, pad1;
};

RWTexture3D<float4> ProbeGrid : register(u0);

[numthreads(4, 4, 4)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= ProbeResXY || DTid.y >= ProbeResXY || DTid.z >= ProbeResZ)
        return;

    // Full sky visibility in all SH2 bands (L0 = 1, L1 = 0)
    ProbeGrid[DTid] = float4(1.0, 0.0, 0.0, 0.0);
}
