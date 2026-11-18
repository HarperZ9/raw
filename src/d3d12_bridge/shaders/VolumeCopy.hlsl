// D3D11 compute copy of a 3D texture into a shared 2D atlas: the depth slices
// side by side, row after row. Slice z sits at tile (z % columns, z / columns)
// with columns = atlas width / volume width. Original work, MIT.
Texture3D<float4>   gSrc : register(t0);
RWTexture2D<float4> gDst : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint w, h, d, aw, ah;
    gSrc.GetDimensions(w, h, d);
    gDst.GetDimensions(aw, ah);
    if (id.x >= aw || id.y >= ah) return;
    const uint z = (id.y / h) * (aw / w) + id.x / w;
    gDst[id.xy] = z < d ? gSrc.Load(int4(id.x % w, id.y % h, z, 0)) : float4(0, 0, 0, 0);
}
