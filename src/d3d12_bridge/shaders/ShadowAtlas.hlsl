// ShadowAtlas.hlsl: D3D11 compute pass that copies the game's sun cascades (a 2D array of
// 16-bit depth, one layer a cascade) into the shared R32_FLOAT atlas the lighting pass reads:
// cascade k at x offset k * side. The D3D12 side has no 16-bit format in the engine's RHI, and
// D3D11 opens only render-target capable shared textures. Original work, MIT.

Texture2DArray<float> gCascades : register(t0);
RWTexture2D<float>    gAtlas    : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint w, h;
    gAtlas.GetDimensions(w, h);
    if (id.x >= w || id.y >= h) return;
    const uint side = h;
    gAtlas[id.xy] = gCascades.Load(int4(id.x % side, id.y, id.x / side, 0));
}
