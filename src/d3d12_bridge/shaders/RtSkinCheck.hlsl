// RtSkinCheck.hlsl: a skinned draw's triangles for its per-frame BLAS (RtSkin.cpp), after RtSkin.hlsl has
// placed the vertices. One thread a triangle: the game's indices are copied into RAW's own 32-bit index
// pool, and a triangle that skinning stretched is made degenerate there (all three corners the first one),
// so it can never be hit. Bones move the parts of a body rigidly, so an edge that grows past
// stretchRatio times its bind-pose length (and past stretchMin units) is a vertex the palette placed wrong:
// the sheet of triangles from an actor's lower body to the ground. Each draw's counts, and its first
// stretched triangle in full (indices, bind and skinned positions, blend indices and weights), go to a
// stats buffer the CPU reads three frames later.
// The game's own depth buffer is the second check: a skinned corner that lands on screen in front of the
// surface the game drew there (by more than a margin) is a corner the game did not draw there. A triangle
// with two such corners is counted as floating and, with the filter, made degenerate too; its first
// floating corner is kept in full. Original work, MIT.
cbuffer C : register(b0) {
    uint vStart, stride, posOffset, posFmt, bStart, bStride, idxOffset, wOffset;
    uint count, outFirst, iStart, ib32, triCount, idxFirst, job, filter;
    float stretchRatio, stretchMin, projA, projB;
    float4 w2c[4];      // camera-relative world to clip (the lighting uniform's worldToClip)
    float2 screen; float2 pad2;
};
ByteAddressBuffer Ib : register(t0);
ByteAddressBuffer Vb : register(t1);
ByteAddressBuffer Bv : register(t2);
RWByteAddressBuffer Pool : register(u0);
RWByteAddressBuffer Idx : register(u1);
RWByteAddressBuffer Stats : register(u2);   // per job kStatWords uints (RtSkin.h)
Texture2D<float> Depth : register(t3);      // the game's depth (R32 copy)

static const uint kStatWords = 48u;

// How far in front of the game's surface a skinned point lands (game units; <= 0 behind it or off screen).
float inFront(float3 s) {
    const float4 c = float4(dot(w2c[0], float4(s, 1.0f)), dot(w2c[1], float4(s, 1.0f)), dot(w2c[2], float4(s, 1.0f)), dot(w2c[3], float4(s, 1.0f)));
    if (c.w < 20.0f || any(isnan(s))) { return 0.0f; }
    const float2 ndc = c.xy / c.w;
    if (any(abs(ndc) > 0.995f)) { return 0.0f; }
    const int2 px = int2((ndc.x * 0.5f + 0.5f) * screen.x, (0.5f - ndc.y * 0.5f) * screen.y);
    const float d = Depth.Load(int3(px, 0));
    const float zScene = projB / max(projA - d, 1.0e-7f);
    return zScene - c.w - (8.0f + 0.02f * c.w);
}

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
    bool floating = false;
    if (!range && screen.x > 0.0f) {
        const float f0 = inFront(skinned(i[0])), f1 = inFront(skinned(i[1])), f2 = inFront(skinned(i[2]));
        floating = uint(f0 > 0.0f) + uint(f1 > 0.0f) + uint(f2 > 0.0f) >= 2u;
        if (floating) {
            uint first;
            Stats.InterlockedCompareExchange(base + 39u * 4u, 0u, 1u, first);
            if (first == 0u) {
                const uint k = f0 > 0.0f ? 0u : f1 > 0.0f ? 1u : 2u;
                const uint b = bStart + i[k] * bStride;
                Stats.Store(base + 40u * 4u, i[k]);
                Stats.Store3(base + 41u * 4u, asuint(skinned(i[k])));
                Stats.Store(base + 44u * 4u, word(Bv, b + idxOffset));
                Stats.Store2(base + 45u * 4u, uint2(word(Bv, b + wOffset), word(Bv, b + wOffset + 4u)));
                Stats.Store(base + 47u * 4u, asuint(max(f0, max(f1, f2))));
            }
        }
    }
    uint dummy;
    if (floating) { Stats.InterlockedAdd(base + 38u * 4u, 1u, dummy); }
    Stats.InterlockedAdd(base, 1u, dummy);
    if (range) { Stats.InterlockedAdd(base + 4u, 1u, dummy); }
    if (stretched) { Stats.InterlockedAdd(base + 8u, 1u, dummy); }
    const bool drop = range || ((stretched || floating) && filter != 0u);
    const uint o = (idxFirst + t * 3u) * 4u;
    Idx.Store3(o, drop ? uint3(0u, 0u, 0u) : uint3(i[0], i[1], i[2]));
}
