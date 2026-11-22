// GiAtrousPass.hlsl: True 3-Pass 5x5 B3-Spline Plane-Distance Edge-Stopping A-Trous Wavelet GI Denoiser (SM 6.9).
// Implements Dammertz et al. (HPG 2010) / Schied et al. (SVGF, HPG 2017) full 25-tap B3-spline wavelet kernel
// (1/16, 1/4, 3/8, 1/4, 1/16) across step sizes 1 -> 2 -> 4 (effective 29x29 edge-preserving receptive field)
// with world-space plane-distance depth stopping (abs(dot(n, pq - pp)) — exact on roads, stairs, and grazing walls),
// Normal^64 stopping, and local-variance-adaptive Luminance stopping. Preserves ReSTIR restVis in alpha.
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
    if (WaveActiveAllTrue(d >= 1.0f) || d >= 1.0f) { return center; }

    const float z = viewZOf(d);
    const float3 wp = worldAt(float2(p), d);
    const float3 n = worldNormal(Normal.Load(ip));
    const float cLum = dot(center.rgb, float3(0.2126f, 0.7152f, 0.0722f));
    const int2 dMax = int2(dims.xy) - 1;

    // Estimate local 3x3 luminance standard deviation for SVGF-style variance-guided luminance edge-stopping.
    float lSum = cLum, lSq = cLum * cLum, lCnt = 1.0f;
    [unroll] for (int vy = -1; vy <= 1; vy++) {
        [unroll] for (int vx = -1; vx <= 1; vx++) {
            if (vx == 0 && vy == 0) { continue; }
            const int2 vq = clamp(p + int2(vx, vy) * stepSize, int2(0, 0), dMax);
            const float vdq = Depth.Load(int3(vq, 0));
            if (vdq >= 1.0f) { continue; }
            const float vl = dot(GiIn.Load(int3(vq, 0)).rgb, float3(0.2126f, 0.7152f, 0.0722f));
            lSum += vl; lSq += vl * vl; lCnt += 1.0f;
        }
    }
    const float lMean = lSum / lCnt;
    const float lSigma = sqrt(max(lSq / lCnt - lMean * lMean, 0.0f));
    const float invSigmaL = 1.0f / max(2.2f * lSigma + 0.045f, 0.045f);
    const float planeScale = 1.0f / max(z * 0.0085f * float(stepSize), 1.8f);

    // True 5x5 B3-spline wavelet weights: [1/16, 1/4, 3/8, 1/4, 1/16].
    static const float kB3[5] = { 0.0625f, 0.25f, 0.375f, 0.25f, 0.0625f };
    float3 sumRgb = center.rgb * (0.375f * 0.375f);
    float  wSum   = 0.375f * 0.375f;
    [unroll] for (int dy = -2; dy <= 2; dy++) {
        const float wy = kB3[dy + 2];
        [unroll] for (int dx = -2; dx <= 2; dx++) {
            if (dx == 0 && dy == 0) { continue; }
            const int2 q = clamp(p + int2(dx, dy) * stepSize, int2(0, 0), dMax);
            const float dq = Depth.Load(int3(q, 0));
            if (dq >= 1.0f) { continue; }
            // World-space plane-distance stopping: exact across grazing roads, cobblestones, stairs & walls.
            const float3 wq = worldAt(float2(q), dq);
            const float planeDist = abs(dot(n, wq - wp)) * planeScale;
            const float wz = exp(-planeDist * planeDist);
            const float3 nq = worldNormal(Normal.Load(int3(q, 0)));
            const float wn = pow(saturate(dot(n, nq)), 64.0f);
            const float4 tap = GiIn.Load(int3(q, 0));
            const float tLum = dot(tap.rgb, float3(0.2126f, 0.7152f, 0.0722f));
            const float wl = exp(-abs(tLum - cLum) * invSigmaL);
            const float w = kB3[dx + 2] * wy * wz * wn * wl;
            sumRgb += tap.rgb * w;
            wSum += w;
        }
    }
    return float4(sumRgb / max(wSum, 1.0e-5f), center.a);
}

float4 step1(float4 at : SV_Position) : SV_Target0 { return atrousStep(at, 1); }
float4 step2(float4 at : SV_Position) : SV_Target0 { return atrousStep(at, 2); }
float4 step4(float4 at : SV_Position) : SV_Target0 { return atrousStep(at, 4); }
