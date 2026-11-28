// CloudNoiseLut.hlsl: Precomputed 64x64x64 (512x512 8x8 slice atlas) High-Contrast Multi-Octave
// Periodic 3D Perlin-Worley + Curl-Billow Volumetric Weather Field (SM 6.6 / 6.10).
// Runs ONCE on GPU init (0.00 ms per frame after frame 0) using exact 27-neighbor [-1, 0, +1]^3
// periodic wrapping so 3D cloud sampling has ZERO tile seams and razor-crisp convective structure:
//   R = High-contrast curl-warped Perlin-Worley base convective tower field (4-oct Perlin + 4-oct Worley)
//   G = Curl-billowed periodic 3D Worley macro cauliflower domes (freq = 4, F1 + crisp ridge contrast)
//   B = Periodic 3D Worley medium cauliflower erosion turrets (freq = 8 + harmonic 12)
//   A = High-frequency wispy curl-modulated Worley tufts (freq = 16 + micro 32)
// Original work, MIT.
#include "LightingCommon.hlsli"

float3 cnHash33(float3 p) {
    p = frac(p * float3(0.1031f, 0.1030f, 0.0973f));
    p += dot(p, p.yxz + 33.33f);
    return frac((p.xxy + p.yxx) * p.zyx);
}

// Exact periodic 3D gradient Perlin noise wrapping modulo `freq` in X, Y, Z with analytical gradient!
// Returns float4(value in [0, 1], dV/du, dV/dv, dV/dw) for divergence-free periodic 3D curl synthesis.
float4 cnPerlin3DPeriodicGrad(float3 uvw, float freq) {
    const float3 p  = frac(uvw) * freq;
    const float3 i0 = floor(p);
    const float3 f  = frac(p);
    const float3 u  = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
    const float3 du = 30.0f * f * f * (f * (f - 2.0f) + 1.0f);

    float val = 0.0f;
    float3 grad = 0.0f.xxx;

    [unroll] for (int dz = 0; dz <= 1; dz++) {
        const float wz  = dz == 0 ? (1.0f - u.z) : u.z;
        const float dwz = dz == 0 ? -du.z : du.z;
        [unroll] for (int dy = 0; dy <= 1; dy++) {
            const float wy  = dy == 0 ? (1.0f - u.y) : u.y;
            const float dwy = dy == 0 ? -du.y : du.y;
            [unroll] for (int dx = 0; dx <= 1; dx++) {
                const float wx  = dx == 0 ? (1.0f - u.x) : u.x;
                const float dwx = dx == 0 ? -du.x : du.x;

                const float3 g = float3(float(dx), float(dy), float(dz));
                const float3 cell = fmod(i0 + g + freq, freq);
                const float3 gv = normalize(cnHash33(cell) * 2.0f - 1.0f + float3(1.0e-4f, -1.0e-4f, 1.0e-4f));
                const float3 d = f - g;
                const float proj = dot(gv, d);

                val += proj * wx * wy * wz;
                grad.x += (gv.x * wx + proj * dwx) * wy * wz;
                grad.y += (gv.y * wy + proj * dwy) * wx * wz;
                grad.z += (gv.z * wz + proj * dwz) * wx * wy;
            }
        }
    }
    return float4(saturate(val * 0.88f + 0.5f), grad * (0.88f * freq));
}

float cnPerlin3DPeriodic(float3 uvw, float freq) {
    return cnPerlin3DPeriodicGrad(uvw, freq).x;
}

// Divergence-free periodic 3D curl vector field (100% tileable on [0, 1)^3) for convective billowing & wisps:
float3 cnCurl3DPeriodic(float3 uvw, float freq) {
    const float3 gX = cnPerlin3DPeriodicGrad(uvw, freq).yzw;
    const float3 gY = cnPerlin3DPeriodicGrad(uvw + float3(0.37f, 0.19f, 0.61f), freq).yzw;
    const float3 gZ = cnPerlin3DPeriodicGrad(uvw + float3(0.73f, 0.53f, 0.29f), freq).yzw;
    return float3(gZ.y - gY.z, gX.z - gZ.x, gY.x - gX.y) / max(freq, 1.0f);
}

// Exact 27-neighbor [-1, 0, +1]^3 periodic 3D Worley cellular field:
// Returns float2(1 - F1 cauliflower dome, saturate(F2 - F1) crisp cellular cleft).
float2 cnWorley3DPeriodicF1F2(float3 uvw, float freq) {
    const float3 p = frac(uvw) * freq;
    const float3 i = floor(p);
    const float3 f = frac(p);
    float d1 = 2.0f;
    float d2 = 2.0f;
    [unroll] for (int dz = -1; dz <= 1; dz++) {
        [unroll] for (int dy = -1; dy <= 1; dy++) {
            [unroll] for (int dx = -1; dx <= 1; dx++) {
                const float3 off = float3(float(dx), float(dy), float(dz));
                const float3 cell = fmod(i + off + freq, freq);
                const float3 pt = off + cnHash33(cell) - f;
                const float dist2 = dot(pt, pt);
                if (dist2 < d1) {
                    d2 = d1;
                    d1 = dist2;
                } else if (dist2 < d2) {
                    d2 = dist2;
                }
            }
        }
    }
    const float f1 = sqrt(max(d1, 0.0f));
    const float f2 = sqrt(max(d2, 0.0f));
    return float2(saturate(1.0f - f1), saturate((f2 - f1) * 1.45f));
}

float cnRemap01(float v, float lo, float hi) {
    return saturate((v - lo) / max(hi - lo, 1.0e-5f));
}

float4 bake(float4 at : SV_Position) : SV_Target0 {
    const uint2 px = uint2(at.xy);
    const uint slice = (px.y / 64u) * 8u + (px.x / 64u);
    const float3 uvw = float3(float(px.x & 63u) + 0.5f, float(px.y & 63u) + 0.5f, float(slice) + 0.5f) / 64.0f;

    // Periodic divergence-free 3D curl-billow advection (creates swirling convective thermal eddies & wisps):
    const float3 curlLo = cnCurl3DPeriodic(uvw, 4.0f);
    const float3 curlHi = cnCurl3DPeriodic(uvw, 8.0f);
    const float3 uvwBillow = frac(uvw + curlLo * 0.032f);
    const float3 uvwWisp   = frac(uvw + curlLo * 0.024f + curlHi * 0.016f);

    // 4-octave curl-warped periodic Perlin FBM (macro convective airmass + rising thermal structure):
    const float p4  = cnPerlin3DPeriodic(uvwBillow, 4.0f);
    const float p8  = cnPerlin3DPeriodic(uvwBillow, 8.0f);
    const float p16 = cnPerlin3DPeriodic(uvwWisp, 16.0f);
    const float p32 = cnPerlin3DPeriodic(uvwWisp, 32.0f);
    const float perlinFbm = p4 * 0.52f + p8 * 0.27f + p16 * 0.14f + p32 * 0.07f;

    // Multi-octave periodic 3D Worley cauliflower towers + crisp convective clefts:
    const float2 w4  = cnWorley3DPeriodicF1F2(uvwBillow, 4.0f);
    const float2 w8  = cnWorley3DPeriodicF1F2(uvwBillow, 8.0f);
    const float2 w12 = cnWorley3DPeriodicF1F2(uvwWisp + float3(0.17f, 0.43f, 0.29f), 12.0f);
    const float2 w16 = cnWorley3DPeriodicF1F2(uvwWisp, 16.0f);
    const float2 w32 = cnWorley3DPeriodicF1F2(uvwWisp, 32.0f);

    // Sculpted cauliflower channels (sharp rounded tops, clean deep crevices instead of blurry low-frequency mush):
    const float gMacro  = saturate(pow(w4.x, 0.88f) * (0.78f + 0.26f * w4.y));
    const float bMedium = saturate(pow(w8.x * 0.76f + w12.x * 0.24f, 0.92f) * (0.80f + 0.24f * w8.y));
    // High-frequency wispy curl channel (tufted cumulus edges & sheared cloud wisps):
    const float curlMag = saturate(length(curlHi) * 0.85f);
    const float aFine   = saturate(w16.x * 0.62f + w32.x * 0.26f + (1.0f - abs(p16 * 2.0f - 1.0f)) * 0.12f * (0.5f + curlMag));

    // Schneider & Vos (SIGGRAPH 2015 / Nubis Evolved) Perlin-Worley convective base tower field
    // with high-contrast billowing silhouette shaping:
    const float worleyFbm = gMacro * 0.58f + bMedium * 0.28f + aFine * 0.14f;
    const float pwRaw = cnRemap01(perlinFbm, -(1.0f - worleyFbm) * 0.70f, 1.0f);
    // Gentle convective S-curve preserves full [0..1] range while sharpening tower boundaries cleanly:
    const float perlinWorley = saturate(lerp(pwRaw, smoothstep(0.06f, 0.94f, pwRaw), 0.35f));

    return float4(perlinWorley, gMacro, bMedium, aFine);
}
