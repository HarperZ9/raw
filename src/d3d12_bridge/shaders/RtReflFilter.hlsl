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
    // 3x3 depth-aware spatial moment estimation (m1, m2) with firefly clamping (> 3.5).
    float4 s1 = 0.0f.xxxx, s2 = 0.0f.xxxx;
    float wSum = 0.0f;
    [unroll] for (int dy = -1; dy <= 1; dy++) {
        [unroll] for (int dx = -1; dx <= 1; dx++) {
            const int2 q = clamp(p + int2(dx, dy), int2(0, 0), dMax);
            const float dq = Depth.Load(int3(q, 0));
            if (dq >= 1.0f) { continue; }
            float4 tap = RawRefl.Load(int3(q, 0));
            if (tap.a < 0.005f) { continue; }
            const float relZ = (viewZOf(dq) - z) / max(z, 1.0f);
            const float w = exp2(-relZ * relZ * 3200.0f) * ((dx == 0 && dy == 0) ? 1.5f : 1.0f);
            const float pk = max(tap.r, max(tap.g, tap.b));
            if (pk > 3.5f) { tap.rgb *= 3.5f / pk; }
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
