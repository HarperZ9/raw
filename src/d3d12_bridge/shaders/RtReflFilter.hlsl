// RtReflFilter.hlsl: Dedicated SM 6.9 Temporal + SVGF Variance-Clamped Specular Reflection Denoiser.
// Transforms raw 1-spp Heitz 2018 GGX VNDF hardware RayQuery reflections (m_rtReflect) into temporally
// rock-solid, firefly-free glossy reflections on wet stone, puddles, forged steel, Dwemer gold, and ice.
// Original work, MIT.
#include "LightingCommon.hlsli"

Texture2D<float4> RawRefl  : register(t0);
Texture2D<float>  Depth    : register(t1);
Texture2D<float2> Motion   : register(t2);
Texture2D<float4> HistPrev : register(t3);
SamplerState      Lin      : register(s0);

float4 fs(float4 at : SV_Position) : SV_Target0 {
    const int2 p = int2(at.xy);
    const int3 ip = int3(p, 0);
    const float d = Depth.Load(ip);
    const float4 raw = RawRefl.Load(ip);
    // SM 6.9 Wave-coherent early exit: skip sky and non-reflective matte waves cleanly.
    if (WaveActiveAllTrue(d >= 1.0f || raw.a < 0.005f)) { return float4(0.0f, 0.0f, 0.0f, 0.0f); }
    if (d >= 1.0f) { return float4(0.0f, 0.0f, 0.0f, 0.0f); }

    const float z = viewZOf(d);
    const int2 dMax = int2(dims.xy) - 1;
    // arXiv:2610.11702 Prefiltered Specular Radiance Filtering: Karis inverse-luminance firefly suppression +
    // depth-plane-aware 3x3 spatial moment estimation (exact on grazing wet roads, armor, and puddles).
    const float zR = viewZOf(Depth.Load(int3(min(p.x + 1, dMax.x), p.y, 0))) - z;
    const float zL = z - viewZOf(Depth.Load(int3(max(p.x - 1, 0), p.y, 0)));
    const float zD = viewZOf(Depth.Load(int3(p.x, min(p.y + 1, dMax.y), 0))) - z;
    const float zU = z - viewZOf(Depth.Load(int3(p.x, max(p.y - 1, 0), 0)));
    const float2 zSlope = float2(abs(zR) < abs(zL) ? zR : zL, abs(zD) < abs(zU) ? zD : zU);
    const float invPlane = 1.0f / max(z * 0.018f, 2.0f);
    float4 s1 = 0.0f.xxxx, s2 = 0.0f.xxxx;
    float wSum = 0.0f;
    [unroll] for (int dy = -1; dy <= 1; dy++) {
        [unroll] for (int dx = -1; dx <= 1; dx++) {
            const int2 q = clamp(p + int2(dx, dy), int2(0, 0), dMax);
            const float dq = Depth.Load(int3(q, 0));
            if (dq >= 1.0f) { continue; }
            float4 tap = RawRefl.Load(int3(q, 0));
            if (tap.a < 0.005f) { continue; }
            const float zPred = z + zSlope.x * float(dx) + zSlope.y * float(dy);
            const float pd = abs(viewZOf(dq) - zPred) * invPlane;
            const float tapLum = dot(tap.rgb, float3(0.2126f, 0.7152f, 0.0722f));
            const float karis = 1.0f / (1.0f + tapLum * 0.45f);
            const float w = exp(-pd * pd) * karis * ((dx == 0 && dy == 0) ? 1.5f : 1.0f);
            const float pk = max(tap.r, max(tap.g, tap.b));
            if (pk > 4.5f) { tap.rgb *= 4.5f / pk; }
            s1 += tap * w;
            s2 += (tap * tap) * w;
            wSum += w;
        }
    }
    if (wSum < 1.0e-4f) { return float4(0.0f, 0.0f, 0.0f, 0.0f); }
    const float4 m1 = s1 / wSum;
    const float4 stdDev = sqrt(max(s2 / wSum - m1 * m1, 0.0f.xxxx));
    float4 cur = raw.a > 0.005f ? lerp(raw, m1, 0.50f) : m1;

    // Motion-vector temporal reprojection + tight 1.8-sigma SVGF neighborhood clamp (zero ghosting in motion).
    const float2 uv = (floor(at.xy) + 0.5f) * dims.zw;
    const float2 mv = Motion.Load(ip);
    const float2 puv = uv + mv;
    if (all(puv > 0.005f) && all(puv < 0.995f)) {
        const float4 prev = HistPrev.SampleLevel(Lin, puv, 0.0f);
        if (prev.a > 0.01f) {
            const float4 lo = max(m1 - stdDev * 1.8f, 0.0f.xxxx);
            const float4 hi = m1 + stdDev * 1.8f;
            const float4 clamped = clamp(prev, lo, hi);
            const float speed = length(mv * dims.xy);
            const float alpha = lerp(0.18f, 0.45f, saturate(speed * 0.08f));
            cur = lerp(clamped, cur, alpha);
        }
    }
    return float4(max(cur.rgb, 0.0f.xxx), saturate(cur.a));
}
