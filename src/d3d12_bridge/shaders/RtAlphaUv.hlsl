// RtAlphaUv.hlsl: what the rays need of a mesh, made once (RtAlpha.cpp).
//   main:   the mesh's texture coordinates, three per triangle in index order, from the game's vertex
//           and index buffers into the pool (8 bytes each, float2), and each triangle's face normal
//           in object space (octahedral, two 16-bit snorms) into the normal pool.
//   mask:   the alpha of the game's texture into an R8 mask of at most 512 x 512, from the mip whose
//           size matches, filtered.
//   albedo: the game's colour texture into an RGBA8 copy of at most matSide, the same way (built with
//           -DALBEDO=1; the copy sits where the mask would).
// Original work, MIT.
cbuffer C : register(b0) { uint c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11; };
ByteAddressBuffer Vb : register(t0);
ByteAddressBuffer Ib : register(t1);
Texture2D<float4> Src : register(t2);
RWByteAddressBuffer Pool : register(u0);
#ifdef ALBEDO
RWTexture2D<unorm float4> Out : register(u1);
#else
RWTexture2D<unorm float> Mask : register(u1);
#endif
RWByteAddressBuffer Normals : register(u2);
SamplerState Linear : register(s0);

// A 32-bit word at a byte address that may sit 2 bytes off alignment (half formats, 16-bit indices).
uint word(ByteAddressBuffer b, uint a) {
    const uint lo = a & ~3u;
    if ((a & 3u) == 0u) { return b.Load(lo); }
    return (b.Load(lo) >> 16) | (b.Load(lo + 4u) << 16);
}

float3 position(uint v) {
    const uint a = c0 + v * c1 + c8;
    if (c9 == 0u) {
        const uint w0 = word(Vb, a), w1 = word(Vb, a + 4u);
        return float3(f16tof32(w0), f16tof32(w0 >> 16), f16tof32(w1));
    }
    return float3(asfloat(word(Vb, a)), asfloat(word(Vb, a + 4u)), asfloat(word(Vb, a + 8u)));
}

// Octahedral encoding of a unit vector into two 16-bit snorms.
uint packNormal(float3 n) {
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    float2 e = n.z >= 0.0f ? n.xy : (1.0f - abs(n.yx)) * float2(n.x >= 0.0f ? 1.0f : -1.0f, n.y >= 0.0f ? 1.0f : -1.0f);
    const int2 q = int2(round(clamp(e, -1.0f, 1.0f) * 32767.0f));
    return (uint(q.x) & 0xFFFFu) | (uint(q.y) << 16);
}

// c0 first vertex byte, c1 stride, c2 coordinate offset, c3 0 half / 1 float, c4 first index byte,
// c5 0 16-bit / 1 32-bit indices, c6 triangles, c7 first output coordinate, c8 position offset,
// c9 0 half / 1 float positions, c10 first face normal, c11 1: positions in this buffer.
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const uint t = id.x;
    if (t >= c6) { return; }
    uint vi[3];
    [unroll] for (uint k = 0u; k < 3u; k++) {
        const uint i = t * 3u + k;
        uint v;
        if (c5 != 0u) { v = Ib.Load(c4 + i * 4u); }
        else { const uint a = c4 + i * 2u; const uint w = Ib.Load(a & ~3u); v = (a & 2u) ? (w >> 16) : (w & 0xFFFFu); }
        vi[k] = v;
        const uint va = c0 + v * c1 + c2;
        float2 uv;
        if (c3 == 0u) { const uint w = word(Vb, va); uv = float2(f16tof32(w), f16tof32(w >> 16)); }
        else { uv = float2(asfloat(word(Vb, va)), asfloat(word(Vb, va + 4u))); }
        Pool.Store2((c7 + i) * 8u, asuint(uv));
    }
    uint packed = 0u;
    if (c11 != 0u) {
        const float3 p0 = position(vi[0]), p1 = position(vi[1]), p2 = position(vi[2]);
        const float3 n = cross(p1 - p0, p2 - p0);
        const float len = length(n);
        packed = len > 1.0e-12f ? packNormal(n / len) : 0u;
    }
    Normals.Store((c10 + t) * 4u, packed);
}

// c0, c1: the output's size.
[numthreads(8, 8, 1)]
void mask(uint3 id : SV_DispatchThreadID) {
    if (id.x >= c0 || id.y >= c1) { return; }
    uint w, h, m;
    Src.GetDimensions(0, w, h, m);
    const float lod = max(0.0f, log2(max(float(w) / float(c0), float(h) / float(c1))));
    const float4 s = Src.SampleLevel(Linear, (float2(id.xy) + 0.5f) / float2(c0, c1), lod);
#ifdef ALBEDO
    Out[id.xy] = s;
#else
    Mask[id.xy] = s.a;
#endif
}
