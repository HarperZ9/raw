// RtOmmBake.hlsl: an opacity micromap for each triangle of an alpha-tested mesh (RtOmm.cpp).
// One thread per (triangle, 32-bit word): a word holds 16 micro-triangles, consecutive in the GPU's
// micromap order (level 4: 256 micro-triangles, 32 words a triangle). Each micro-triangle's three
// corners and centre are looked up in the mesh's alpha mask through its texture coordinates (RtAlpha's
// pool): all four at or above the cut make it opaque, all below transparent, a mix unknown (opaque or
// transparent by the centre), which the ray query then resolves with the mask as before. Two bits per
// micro-triangle; Cells (from OmmTable.h) gives the grid cell of each micromap index. Word 0 of each
// triangle also writes its micromap descriptor. Every word is written whole, so no clear is needed.
// Original work, MIT.
cbuffer C : register(b0) { uint triangles, uvBase, maskW, maskH, inputWord, descBase, refMilli, level; };
StructuredBuffer<uint> Cells : register(t0);       // micromap index -> grid cell, at `level`
RWByteAddressBuffer Coords : register(u0);         // RtAlpha's pool: float2 per triangle corner
RWTexture2D<unorm float> Mask : register(u1);
RWByteAddressBuffer Input : register(u2);          // micromap bits
RWByteAddressBuffer Descs : register(u3);          // D3D12_RAYTRACING_OPACITY_MICROMAP_DESC per triangle

float alphaAt(float2 uv) {
    const uint2 t = uint2(frac(uv) * float2(maskW, maskH)) % uint2(maskW, maskH);
    return Mask[t];
}

// D3D12_RAYTRACING_OPACITY_MICROMAP_STATE of grid cell `cell`: 0 transparent, 1 opaque, 2 unknown
// transparent, 3 unknown opaque.
uint cellState(uint cell, uint n, float2 uv0, float2 uv1, float2 uv2, float ref) {
    uint row = 0u, start = 0u;
    [loop] while (start + 2u * (n - row) - 1u <= cell) { start += 2u * (n - row) - 1u; row++; }
    const uint k = cell - start, c = k / 2u;
    float2 p0, p1, p2;   // barycentric corners (b.x, b.y)
    const float s = 1.0f / float(n);
    if ((k & 1u) == 0u) { p0 = float2(c, row) * s; p1 = float2(c + 1u, row) * s; p2 = float2(c, row + 1u) * s; }
    else { p0 = float2(c + 1u, row) * s; p1 = float2(c, row + 1u) * s; p2 = float2(c + 1u, row + 1u) * s; }
    const float2 pc = (p0 + p1 + p2) / 3.0f;
    const float2 pts[4] = {lerp(pc, p0, 0.9f), lerp(pc, p1, 0.9f), lerp(pc, p2, 0.9f), pc};
    uint above = 0u;
    bool centre = false;
    [unroll] for (uint i = 0u; i < 4u; i++) {
        const float2 b = pts[i];
        const bool on = alphaAt(uv0 * (1.0f - b.x - b.y) + uv1 * b.x + uv2 * b.y) >= ref;
        above += on ? 1u : 0u;
        centre = on;   // the last sample is the centre
    }
    return above == 4u ? 1u : above == 0u ? 0u : (centre ? 3u : 2u);
}

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const uint cells = 1u << (2u * level), n = 1u << level, words = cells / 16u;
    const uint tri = id.x / words, w = id.x % words;
    if (tri >= triangles) { return; }
    const uint a = (uvBase + tri * 3u) * 8u;
    const float2 uv0 = asfloat(Coords.Load2(a)), uv1 = asfloat(Coords.Load2(a + 8u)), uv2 = asfloat(Coords.Load2(a + 16u));
    const float ref = float(refMilli) / 1000.0f;
    uint bits = 0u;
    [loop] for (uint j = 0u; j < 16u; j++) { bits |= cellState(Cells[w * 16u + j], n, uv0, uv1, uv2, ref) << (2u * j); }
    const uint first = inputWord + tri * words;
    Input.Store((first + w) * 4u, bits);
    // ByteOffset; SubdivisionLevel (16 bits) | Format (16 bits, OC1_4_STATE = 2).
    if (w == 0u) { Descs.Store2((descBase + tri) * 8u, uint2(first * 4u, level | (2u << 16))); }
}
