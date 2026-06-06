Texture2D<float> SrcMip : register(t0);
RWTexture2D<float> DstMip : register(u0);

cbuffer HiZParams : register(b0)
{
    uint2 DstDimensions;
    uint2 Padding;
};

[numthreads(8, 8, 1)]
void CSDownsample(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= DstDimensions.x || DTid.y >= DstDimensions.y)
        return;

    uint2 srcCoord = DTid.xy * 2;

    float d00 = SrcMip[srcCoord + uint2(0, 0)];
    float d10 = SrcMip[srcCoord + uint2(1, 0)];
    float d01 = SrcMip[srcCoord + uint2(0, 1)];
    float d11 = SrcMip[srcCoord + uint2(1, 1)];

    // Skyrim uses reversed-Z: near=1.0, far=0.0
    // MAX = closest surface (conservative occlusion)
    DstMip[DTid.xy] = max(max(d00, d10), max(d01, d11));
}
