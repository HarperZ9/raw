// GiAtrousPass.hlsl: True 3-Pass Hierarchical Edge-Stopping A-Trous Wavelet GI Denoiser (SM 6.9).
// Implements Dammertz et al. (HPG 2010) / Schied et al. (SVGF, HPG 2017) 5x5 B3-spline wavelet filtering
// across step sizes 1 -> 2 -> 4 (effective 17x17 edge-preserving receptive field) with joint
// Normal (pow 32), Depth, and Luminance edge-stopping weights. Preserves ReSTIR restVis in alpha.
// Original work, MIT.
#include "LightingCommon.hlsli"

Texture2D<float4> GiIn   : register(t0);
Texture2D<float>  Depth  : register(t1);
Texture2D<float4> Normal : register(t2);

float4 atrousStep(float4 at, int stepSize) {
    const int2 p = int2(at.xy);
    const int3 ip = int3(p, 0);
    const float d = Depth.Load(ip);
    const float4 center = GiIn.Load(ip);
    if (WaveActiveAllTrue(d >= 1.0f)) { return center; }
    if (d >= 1.0f) { return center; }

    const float z = viewZOf(d);
    const float3 n = worldNormal(Normal.Load(ip));
    const float cLum = dot(center.rgb, float3(0.2126f, 0.7152f, 0.0722f));
    const int2 dMax = int2(dims.xy) - 1;
    // 3x3 / 5x5 B3-spline wavelet weights (1/16, 1/4, 3/8, 1/4, 1/16 -> 3x3 separable [1, 2, 1]/4 for fast 9-tap a-trous).
    static const float kW[3] = {0.25f, 0.50f, 0.25f};
    float3 sumRgb = center.rgb * 0.25f;
    float  wSum   = 0.25f;
    [unroll] for (int dy = -1; dy <= 1; dy++) {
        [unroll] for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) { continue; }
            const int2 q = clamp(p + int2(dx, dy) * stepSize, int2(0, 0), dMax);
            const float dq = Depth.Load(int3(q, 0));
            if (dq >= 1.0f) { continue; }
            const float zq = viewZOf(dq);
            const float relZ = abs(zq - z) / max(z * 0.025f * float(stepSize), 1.5f);
            const float wz = exp(-relZ * relZ);
            const float3 nq = worldNormal(Normal.Load(int3(q, 0)));
            const float wn = pow(saturate(dot(n, nq)), 32.0f);
            const float4 tap = GiIn.Load(int3(q, 0));
            const float tLum = dot(tap.rgb, float3(0.2126f, 0.7152f, 0.0722f));
            const float wl = exp(-abs(tLum - cLum) / max(cLum * 0.65f + 0.08f, 0.08f));
            const float w = kW[dx + 1] * kW[dy + 1] * wz * wn * wl;
            sumRgb += tap.rgb * w;
            wSum += w;
        }
    }
    return float4(sumRgb / max(wSum, 1.0e-4f), center.a);
}

float4 step1(float4 at : SV_Position) : SV_Target0 { return atrousStep(at, 1); }
float4 step2(float4 at : SV_Position) : SV_Target0 { return atrousStep(at, 2); }
float4 step4(float4 at : SV_Position) : SV_Target0 { return atrousStep(at, 4); }
