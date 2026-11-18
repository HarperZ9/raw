// D3D11 compute copy of the stencil byte of a D24S8 buffer into a shared
// R8_UINT texture. The stencil plane reads as the green channel of an
// X24_TYPELESS_G8_UINT view. Original work, MIT.
Texture2D<uint2>  gSrc : register(t0);
RWTexture2D<uint> gDst : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint w, h;
    gDst.GetDimensions(w, h);
    if (id.x < w && id.y < h)
        gDst[id.xy] = gSrc.Load(int3(id.xy, 0)).g;
}
