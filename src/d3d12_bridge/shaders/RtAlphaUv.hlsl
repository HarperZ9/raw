// RtAlphaUv.hlsl: what the rays need of an alpha-tested mesh, made once (RtAlpha.cpp).
//   main: the mesh's texture coordinates, three per triangle in index order, from the game's vertex
//         and index buffers into the pool (8 bytes each, float2).
//   mask: the alpha of the game's texture into an R8 mask of at most 512 x 512, from the mip whose
//         size matches, filtered.
// Original work, MIT.
cbuffer C : register(b0) { uint c0, c1, c2, c3, c4, c5, c6, c7; };
ByteAddressBuffer Vb : register(t0);
ByteAddressBuffer Ib : register(t1);
Texture2D<float4> Src : register(t2);
RWByteAddressBuffer Pool : register(u0);
RWTexture2D<unorm float> Mask : register(u1);
SamplerState Linear : register(s0);

// A 32-bit word at a byte address that may sit 2 bytes off alignment (half formats, 16-bit indices).
uint word(ByteAddressBuffer b, uint a) {
    const uint lo = a & ~3u;
    if ((a & 3u) == 0u) { return b.Load(lo); }
    return (b.Load(lo) >> 16) | (b.Load(lo + 4u) << 16);
}

// c0 first vertex byte, c1 stride, c2 coordinate offset, c3 0 half / 1 float, c4 first index byte,
// c5 0 16-bit / 1 32-bit indices, c6 triangles, c7 first output coordinate.
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const uint t = id.x;
    if (t >= c6) { return; }
    [unroll] for (uint k = 0u; k < 3u; k++) {
        const uint i = t * 3u + k;
        uint v;
        if (c5 != 0u) { v = Ib.Load(c4 + i * 4u); }
        else { const uint a = c4 + i * 2u; const uint w = Ib.Load(a & ~3u); v = (a & 2u) ? (w >> 16) : (w & 0xFFFFu); }
        const uint va = c0 + v * c1 + c2;
        float2 uv;
        if (c3 == 0u) { const uint w = word(Vb, va); uv = float2(f16tof32(w), f16tof32(w >> 16)); }
        else { uv = float2(asfloat(word(Vb, va)), asfloat(word(Vb, va + 4u))); }
        Pool.Store2((c7 + i) * 8u, asuint(uv));
    }
}

// c0, c1: the mask's size.
[numthreads(8, 8, 1)]
void mask(uint3 id : SV_DispatchThreadID) {
    if (id.x >= c0 || id.y >= c1) { return; }
    uint w, h, m;
    Src.GetDimensions(0, w, h, m);
    const float lod = max(0.0f, log2(max(float(w) / float(c0), float(h) / float(c1))));
    Mask[id.xy] = Src.SampleLevel(Linear, (float2(id.xy) + 0.5f) / float2(c0, c1), lod).a;
}
