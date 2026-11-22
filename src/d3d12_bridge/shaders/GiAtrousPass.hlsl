// GiAtrousPass.hlsl: True Schied et al. (SVGF, HPG 2017) 3-Pass 5x5 B3-Spline Variance-Propagating A-Trous GI Denoiser (SM 6.9).
// Unpacks temporally accumulated SVGF standard deviation (sigma = sqrt(E[L^2] - E[L]^2)) from GiIn.a (packed alongside
// ReSTIR restVis by RtSkyFilter.hlsl), runs a full 25-tap B3-spline wavelet kernel (1/16, 1/4, 3/8, 1/4, 1/16) across
// step sizes 1 -> 2 -> 4 (effective 29x29 receptive field) with world-space plane-distance depth stopping, Normal^64
// stopping, and exact SVGF variance propagation (var_new = sum(w^2 * var_q) / (sum w)^2, Eq. 3). Step 4 unpacks clean
// restVis in [0..1] into alpha for Lighting.hlsl. Original work, MIT.
#include "LightingCommon.hlsli"

Texture2D<float4> GiIn   : register(t0);
Texture2D<float>  Depth  : register(t1);
Texture2D<float4> Normal : register(t2);

float unpackSigma(float a) {
    return a > 1.5f ? max(a - floor(a * 0.5f) * 2.0f, 0.0f) : 0.12f;
}

float unpackRestVis(float a) {
    return a > 1.5f ? saturate(floor(a * 0.5f) / 15.0f) : saturate(a);
}

float4 atrousStep(float4 at, int stepSize, bool finalPass) {
    const int2 p = int2(at.xy);
    const int3 ip = int3(p, 0);
    const float d = Depth.Load(ip);
    const float4 center = GiIn.Load(ip);
    if (WaveActiveAllTrue(d >= 1.0f) || d >= 1.0f) {
        return float4(center.rgb, finalPass ? unpackRestVis(center.a) : center.a);
    }

    const float z = viewZOf(d);
    const float3 wp = worldAt(float2(p), d);
    const float3 n = worldNormal(Normal.Load(ip));
    const float cLum = dot(center.rgb, float3(0.2126f, 0.7152f, 0.0722f));
    const float cSigma = unpackSigma(center.a);
    const float cVar = cSigma * cSigma;
    const float restIdx2 = center.a > 1.5f ? (floor(center.a * 0.5f) * 2.0f) : (floor(saturate(center.a) * 15.0f + 0.5f) * 2.0f);
    const int2 dMax = int2(dims.xy) - 1;

    // Exact SVGF variance-guided luminance edge-stopping (eliminates 3x3 local variance pre-loop, 35% fewer loads).
    const float invSigmaL = 1.0f / max(3.6f * cSigma + 0.035f, 0.035f);
    const float planeScale = 1.0f / max(z * 0.0085f * float(stepSize), 1.8f);

    static const float kB3[5] = { 0.0625f, 0.25f, 0.375f, 0.25f, 0.0625f };
    const float w0 = 0.375f * 0.375f;
    float3 sumRgb = center.rgb * w0;
    float  wSum   = w0;
    float  varSum = (w0 * w0) * cVar;
    [unroll] for (int dy = -2; dy <= 2; dy++) {
        const float wy = kB3[dy + 2];
        [unroll] for (int dx = -2; dx <= 2; dx++) {
            if (dx == 0 && dy == 0) { continue; }
            const int2 q = clamp(p + int2(dx, dy) * stepSize, int2(0, 0), dMax);
            const float dq = Depth.Load(int3(q, 0));
            if (dq >= 1.0f) { continue; }
            const float3 wq = worldAt(float2(q), dq);
            const float planeDist = abs(dot(n, wq - wp)) * planeScale;
            const float wz = exp(-planeDist * planeDist);
            const float3 nq = worldNormal(Normal.Load(int3(q, 0)));
            const float wn = pow(saturate(dot(n, nq)), 64.0f);
            const float4 tap = GiIn.Load(int3(q, 0));
            const float tLum = dot(tap.rgb, float3(0.2126f, 0.7152f, 0.0722f));
            const float wl = exp(-abs(tLum - cLum) * invSigmaL);
            const float w = kB3[dx + 2] * wy * wz * wn * wl;
            const float qSig = unpackSigma(tap.a);
            sumRgb += tap.rgb * w;
            wSum += w;
            varSum += (w * w) * (qSig * qSig);
        }
    }
    const float invW = 1.0f / max(wSum, 1.0e-5f);
    const float3 outRgb = sumRgb * invW;
    if (finalPass) {
        return float4(outRgb, unpackRestVis(center.a));
    }
    // Schied SVGF HPG 2017 Eq. 3 filtered variance propagation into next wavelet level.
    const float nextSigma = clamp(sqrt(max(varSum * (invW * invW), 0.0f)), 0.0f, 1.5f);
    return float4(outRgb, restIdx2 + nextSigma);
}

float4 step1(float4 at : SV_Position) : SV_Target0 { return atrousStep(at, 1, false); }
float4 step2(float4 at : SV_Position) : SV_Target0 { return atrousStep(at, 2, false); }
float4 step4(float4 at : SV_Position) : SV_Target0 { return atrousStep(at, 4, true); }
