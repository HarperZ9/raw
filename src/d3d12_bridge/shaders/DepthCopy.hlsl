// DepthCopy.hlsl: D3D11 compute pass that copies the game's depth (any
// readable depth format, e.g. R24_UNORM_X8) into the shared R32_FLOAT
// texture the D3D12 side reads. D3D11 cannot open a D3D12 depth-stencil
// texture, so depth crosses as a float colour texture. Original work, MIT.

Texture2D<float>   gSrc : register(t0);
RWTexture2D<float> gDst : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint w, h;
    gDst.GetDimensions(w, h);
    if (id.x < w && id.y < h)
        gDst[id.xy] = gSrc.Load(int3(id.xy, 0));
}
