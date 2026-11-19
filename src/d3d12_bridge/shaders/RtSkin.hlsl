// RtSkin.hlsl: a skinned draw's positions for its per-frame BLAS (RtSkin.cpp). One thread a vertex:
// the position from its stream, blended by up to four bones of the draw's palette (three float4 rows
// each, the row offset being the blend index's unorm value times 765, as the game's own skinned vertex
// shaders index it), written as float3 into this frame's position pool. Original work, MIT.
cbuffer C : register(b0) { uint vStart, stride, posOffset, posFmt, bStart, bStride, idxOffset, wOffset, count, outFirst, boneFirst, pad; float3 camPos; float pad2; };
ByteAddressBuffer Vb : register(t0);
ByteAddressBuffer Bv : register(t1);
StructuredBuffer<float4> Bones : register(t2);
RWByteAddressBuffer Out : register(u0);

uint word(ByteAddressBuffer b, uint a) {
    const uint lo = a & ~3u;
    if ((a & 3u) == 0u) { return b.Load(lo); }
    return (b.Load(lo) >> 16) | (b.Load(lo + 4u) << 16);
}

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const uint v = id.x;
    if (v >= count) { return; }
    const uint a = vStart + v * stride + posOffset;
    float4 p;
    if (posFmt == 0u) {
        const uint w0 = word(Vb, a), w1 = word(Vb, a + 4u);
        p = float4(f16tof32(w0), f16tof32(w0 >> 16), f16tof32(w1), 1.0f);
    } else {
        p = float4(asfloat(word(Vb, a)), asfloat(word(Vb, a + 4u)), asfloat(word(Vb, a + 8u)), 1.0f);
    }
    const uint b = bStart + v * bStride;
    const uint iw = word(Bv, b + idxOffset);
    const uint w0 = word(Bv, b + wOffset), w1 = word(Bv, b + wOffset + 4u);
    const float4 wt = float4(f16tof32(w0), f16tof32(w0 >> 16), f16tof32(w1), f16tof32(w1 >> 16));
    float3 s = 0.0f;
    bool bad = dot(wt, 1.0f.xxxx) < 0.5f;
    [unroll] for (uint k = 0u; k < 4u; k++) {
        const uint row = min(uint(float((iw >> (8u * k)) & 0xFFu) / 255.0f * 765.01f), 237u);
        const float4 r0 = Bones[boneFirst + row], r1 = Bones[boneFirst + row + 1u], r2 = Bones[boneFirst + row + 2u];
        // A weighted bone the palette leaves empty would pull the vertex to the world's origin: the actors'
        // first run drew long spikes from the body to the ground that way.
        bad = bad || (wt[k] > 0.01f && dot(r0.xyz, r0.xyz) + dot(r1.xyz, r1.xyz) + dot(r2.xyz, r2.xyz) < 1.0e-6f);
        s += wt[k] * float3(dot(r0, p), dot(r1, p), dot(r2, p));
    }
    // A NaN position makes its triangles inactive in the BLAS (DXR), so a vertex that cannot be placed
    // takes its triangles out instead of stretching them across the scene.
    const float3 o = bad ? asfloat(0x7FC00000u).xxx : s - camPos;
    Out.Store3((outFirst + v) * 12u, asuint(o));   // the bones are in world space; the scene is camera-relative
}
