Texture2D<float> SrcDepth : register(t0);
RWTexture2D<float> DstMip : register(u0);

cbuffer HiZParams : register(b0)
{
    uint2 DstDimensions;
    uint2 Padding;
};

[numthreads(8, 8, 1)]
void CSCopy(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= DstDimensions.x || DTid.y >= DstDimensions.y)
        return;
    // Convert standard depth (near=0, far=1) to reversed-Z (near=1, far=0)
    DstMip[DTid.xy] = 1.0 - SrcDepth[DTid.xy];
}
