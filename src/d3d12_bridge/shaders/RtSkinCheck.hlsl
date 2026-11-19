// RtSkinCheck.hlsl: a skinned draw's triangles for its per-frame BLAS (RtSkin.cpp), after RtSkin.hlsl has
// placed the vertices. One thread a triangle: the game's indices are copied into RAW's own 32-bit index
// pool, and a triangle that skinning stretched is made degenerate there (all three corners the first one),
// so it can never be hit. Bones move the parts of a body rigidly, so an edge that grows past
// stretchRatio times its bind-pose length (and past stretchMin units) is a vertex the palette placed wrong:
// the sheet of triangles from an actor's lower body to the ground. Each draw's counts, and its first
// stretched triangle in full (indices, bind and skinned positions, blend indices and weights), go to a
// stats buffer the CPU reads three frames later. Original work, MIT.
cbuffer C : register(b0) {
    uint vStart, stride, posOffset, posFmt, bStart, bStride, idxOffset, wOffset;
    uint count, outFirst, iStart, ib32, triCount, idxFirst, job, filter;
    float stretchRatio, stretchMin, pad0, pad1;
};
ByteAddressBuffer Ib : register(t0);
ByteAddressBuffer Vb : register(t1);
ByteAddressBuffer Bv : register(t2);
RWByteAddressBuffer Pool : register(u0);
RWByteAddressBuffer Idx : register(u1);
RWByteAddressBuffer Stats : register(u2);   // per job kStatWords uints (RtSkin.h)

static const uint kStatWords = 40u;

uint word(ByteAddressBuffer b, uint a) {
    const uint lo = a & ~3u;
    if ((a & 3u) == 0u) { return b.Load(lo); }
    return (b.Load(lo) >> 16) | (b.Load(lo + 4u) << 16);
}

uint index(uint t, uint k) {
    if (ib32 != 0u) { return Ib.Load(iStart + (t * 3u + k) * 4u); }
    const uint a = iStart + (t * 3u + k) * 2u;
    return (Ib.Load(a & ~3u) >> ((a & 2u) * 8u)) & 0xFFFFu;
}

float3 bindPos(uint v) {
    const uint a = vStart + v * stride + posOffset;
    if (posFmt == 0u) {
        const uint w0 = word(Vb, a), w1 = word(Vb, a + 4u);
        return float3(f16tof32(w0), f16tof32(w0 >> 16), f16tof32(w1));
    }
    return float3(asfloat(word(Vb, a)), asfloat(word(Vb, a + 4u)), asfloat(word(Vb, a + 8u)));
}

float3 skinned(uint v) { return asfloat(Pool.Load3((outFirst + v) * 12u)); }

float maxEdge(float3 a, float3 b, float3 c) { return sqrt(max(dot(a - b, a - b), max(dot(b - c, b - c), dot(c - a, c - a)))); }

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const uint t = id.x;
    if (t >= triCount) { return; }
    uint i[3] = {index(t, 0u), index(t, 1u), index(t, 2u)};
    const uint base = job * kStatWords * 4u;
    const bool range = i[0] >= count || i[1] >= count || i[2] >= count;
    bool stretched = false;
    if (!range) {
        const float3 b0 = bindPos(i[0]), b1 = bindPos(i[1]), b2 = bindPos(i[2]);
        const float3 s0 = skinned(i[0]), s1 = skinned(i[1]), s2 = skinned(i[2]);
        const float eb = maxEdge(b0, b1, b2), es = maxEdge(s0, s1, s2);
        stretched = es > stretchMin && es > stretchRatio * eb;   // a NaN corner fails both and stays inactive
        if (stretched) {
            uint first;
            Stats.InterlockedCompareExchange(base + 3u * 4u, 0u, 1u, first);
            if (first == 0u) {
                Stats.Store(base + 4u * 4u, t);
                [unroll] for (uint k = 0u; k < 3u; k++) {
                    const uint o = base + (5u + k * 11u) * 4u;
                    const float3 bp = bindPos(i[k]), sp = skinned(i[k]);
                    const uint b = bStart + i[k] * bStride;
                    Stats.Store(o, i[k]);
                    Stats.Store3(o + 4u, asuint(bp));
                    Stats.Store3(o + 16u, asuint(sp));
                    Stats.Store(o + 28u, word(Bv, b + idxOffset));
                    Stats.Store2(o + 32u, uint2(word(Bv, b + wOffset), word(Bv, b + wOffset + 4u)));
                }
            }
        }
    }
    uint dummy;
    Stats.InterlockedAdd(base, 1u, dummy);
    if (range) { Stats.InterlockedAdd(base + 4u, 1u, dummy); }
    if (stretched) { Stats.InterlockedAdd(base + 8u, 1u, dummy); }
    const bool drop = range || (stretched && filter != 0u);
    const uint o = (idxFirst + t * 3u) * 4u;
    Idx.Store3(o, drop ? uint3(0u, 0u, 0u) : uint3(i[0], i[1], i[2]));
}
