// Copyright (c) 2026 Zain D. Harper. All rights reserved.
// Generate mip chain for cubemap face via 2x2 box filter downsample.

cbuffer MipGenCB : register(b0)
{
    uint srcMipLevel;
    uint dstMipSize;
    uint faceIndex;
    uint pad;
}

Texture2DArray<float4>      tSrcMip : register(t0);
RWTexture2DArray<float4>    uDstMip : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= dstMipSize || DTid.y >= dstMipSize)
        return;

    // Source coordinates: each dst texel maps to a 2x2 block in the source mip
    uint2 srcBase = DTid.xy * 2;

    // 2x2 box filter: average 4 source texels
    float4 s00 = tSrcMip.Load(int4(srcBase + uint2(0, 0), faceIndex, srcMipLevel));
    float4 s10 = tSrcMip.Load(int4(srcBase + uint2(1, 0), faceIndex, srcMipLevel));
    float4 s01 = tSrcMip.Load(int4(srcBase + uint2(0, 1), faceIndex, srcMipLevel));
    float4 s11 = tSrcMip.Load(int4(srcBase + uint2(1, 1), faceIndex, srcMipLevel));

    float4 avg = (s00 + s10 + s01 + s11) * 0.25;

    uDstMip[uint3(DTid.xy, faceIndex)] = avg;
}
