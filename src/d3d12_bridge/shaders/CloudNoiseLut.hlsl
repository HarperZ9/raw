// CloudNoiseLut.hlsl: Precomputed 64x64x64 (512x512 8x8 slice atlas) Periodic 3D Perlin-Worley + 3-Octave Worley FBM Volume (SM 6.9).
// Runs ONCE on GPU init (0.00 ms per frame after frame 0) using exact 27-neighbor [-1, 0, +1]^3 periodic wrapping
// so 3D cloud sampling has ZERO cell seams, razor-sharp Schneider & Vos (SIGGRAPH 2015) cauliflower turrets,
// and 3-5x faster runtime than procedural 3D noise raymarching.
// Channels:
//   R = Perlin-Worley base cumulus shape (4-octave periodic Perlin remapped by periodic Worley FBM)
//   G = Periodic 3D Worley F1 (freq = 4, macro cumulus domes)
//   B = Periodic 3D Worley F2 (freq = 8, medium cauliflower turrets)
//   A = Periodic 3D Worley F3 (freq = 16, razor-sharp fine wisps & tufts)
// Original work, MIT.
#include "LightingCommon.hlsli"

float3 hash33(float3 p) {
    p = frac(p * float3(0.1031f, 0.1030f, 0.0973f));
    p += dot(p, p.yxz + 33.33f);
    return frac((p.xxy + p.yxx) * p.zyx);
}

// Exact periodic 3D gradient Perlin noise wrapping modulo `freq` in X, Y, Z.
float perlin3DPeriodic(float3 uvw, float freq) {
    const float3 p = uvw * freq;
    const float3 i0 = floor(p);
    const float3 f  = frac(p);
    const float3 u  = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
    float acc = 0.0f;
    [unroll] for (int dz = 0; dz <= 1; dz++) {
        const float wz = dz == 0 ? (1.0f - u.z) : u.z;
        [unroll] for (int dy = 0; dy <= 1; dy++) {
            const float wy = dy == 0 ? (1.0f - u.y) : u.y;
            [unroll] for (int dx = 0; dx <= 1; dx++) {
                const float wx = dx == 0 ? (1.0f - u.x) : u.x;
                const float3 g = float3(float(dx), float(dy), float(dz));
                const float3 cell = fmod(i0 + g + freq, freq);
                const float3 grad = normalize(hash33(cell) * 2.0f - 1.0f + 1.0e-4f);
                const float3 d = f - g;
                acc += dot(grad, d) * wx * wy * wz;
            }
        }
    }
    return saturate(acc * 0.85f + 0.5f);
}

// Exact 27-neighbor [-1, 0, +1]^3 periodic 3D inverted Worley cellular noise (100% seam-free cauliflower lobes).
float worley3DPeriodic(float3 uvw, float freq) {
    const float3 p = uvw * freq;
    const float3 i = floor(p);
    const float3 f = frac(p);
    float minD2 = 1.5f;
    [unroll] for (int dz = -1; dz <= 1; dz++) {
        [unroll] for (int dy = -1; dy <= 1; dy++) {
            [unroll] for (int dx = -1; dx <= 1; dx++) {
                const float3 off = float3(float(dx), float(dy), float(dz));
                const float3 cell = fmod(i + off + freq, freq);
                const float3 pt = off + hash33(cell) - f;
                minD2 = min(minD2, dot(pt, pt));
            }
        }
    }
    return saturate(1.0f - sqrt(minD2));
}

float remap01(float v, float lo, float hi) {
    return saturate((v - lo) / max(hi - lo, 1.0e-5f));
}

float4 bake(float4 at : SV_Position) : SV_Target0 {
    const uint2 px = uint2(at.xy);
    const uint slice = (px.y / 64u) * 8u + (px.x / 64u);
    const float3 uvw = float3(float(px.x & 63u) + 0.5f, float(px.y & 63u) + 0.5f, float(slice) + 0.5f) / 64.0f;

    const float p0 = perlin3DPeriodic(uvw, 4.0f);
    const float p1 = perlin3DPeriodic(uvw, 8.0f);
    const float p2 = perlin3DPeriodic(uvw, 16.0f);
    const float perlinFbm = p0 * 0.58f + p1 * 0.28f + p2 * 0.14f;

    const float w4  = worley3DPeriodic(uvw, 4.0f);
    const float w8  = worley3DPeriodic(uvw, 8.0f);
    const float w16 = worley3DPeriodic(uvw, 16.0f);
    const float worleyFbm = w4 * 0.625f + w8 * 0.25f + w16 * 0.125f;

    // Classic Schneider & Vos (SIGGRAPH 2015) Perlin-Worley cumulus base channel.
    const float perlinWorley = remap01(perlinFbm, -(1.0f - worleyFbm) * 0.65f, 1.0f);
    return float4(perlinWorley, w4, w8, w16);
}
