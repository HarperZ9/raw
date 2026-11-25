// RtReflFilter.hlsl: Wave 53 Roughness-Adaptive Specular Lobe Bilateral Reflection Denoiser + Firefly Clamping.
// Transforms raw 1-2 spp Heitz 2018 GGX VNDF hardware RayQuery reflections (m_rtReflect) into temporally
// rock-solid, firefly-free reflections: mirror-smooth surfaces (roughness < 0.08) preserve razor-crisp 1px
// specular reflections while rougher iron/stone/wet wood (0.15..0.85) gather smoothly across a wider lobe
// with 3x3 luminance firefly clamping, exact geometric plane-distance stopping, and normal-power stopping.
// Original work, MIT.
#include "LightingCommon.hlsli"

Texture2D<float4> RawRefl  : register(t0);
Texture2D<float>  Depth    : register(t1);
Texture2D<float2> Motion   : register(t2);
Texture2D<float4> HistPrev : register(t3);
SamplerState      Lin      : register(s0);

static const float3 kReflLuma = float3(0.2126f, 0.7152f, 0.0722f);

float4 sanitizeReflTap(float4 t) {
    if (any(isnan(t)) || any(isinf(t))) { return 0.0f.xxxx; }
    return float4(clamp(t.rgb, 0.0f.xxx, 8.0f.xxx), saturate(t.a));
}

// Reconstruct world-space geometric normal directly from Depth (zero extra SRVs; exact plane-distance stopping!).
float3 reflDepthNormal(int2 p, float dC, float3 wpC, int2 dMax) {
    const int2 pR = min(p + int2(1, 0), dMax);
    const int2 pL = max(p - int2(1, 0), int2(0, 0));
    const int2 pD = min(p + int2(0, 1), dMax);
    const int2 pU = max(p - int2(0, 1), int2(0, 0));
    const float dR = Depth.Load(int3(pR, 0));
    const float dL = Depth.Load(int3(pL, 0));
    const float dD = Depth.Load(int3(pD, 0));
    const float dU = Depth.Load(int3(pU, 0));
    const float3 dx = (abs(dR - dC) < abs(dC - dL) && dR < 0.99998f)
                    ? (worldAt(float2(pR), dR) - wpC)
                    : ((dL < 0.99998f) ? (wpC - worldAt(float2(pL), dL)) : float3(1.0f, 0.0f, 0.0f));
    const float3 dy = (abs(dD - dC) < abs(dC - dU) && dD < 0.99998f)
                    ? (worldAt(float2(pD), dD) - wpC)
                    : ((dU < 0.99998f) ? (wpC - worldAt(float2(pU), dU)) : float3(0.0f, 1.0f, 0.0f));
    const float3 nRaw = cross(dx, dy);
    const float len2 = dot(nRaw, nRaw);
    return (len2 > 1.0e-10f) ? (nRaw * rsqrt(len2)) : float3(0.0f, 0.0f, 1.0f);
}

float4 fs(float4 at : SV_Position) : SV_Target0 {
    const int2 p = int2(at.xy);
    const int3 ip = int3(p, 0);
    const float d = Depth.Load(ip);
    const float4 raw = sanitizeReflTap(RawRefl.Load(ip));
    // Standard-Z sky passthrough (d >= 0.99998) + wave-coherent early exit on matte/sky waves:
    if (WaveActiveAllTrue(d >= 0.99998f || raw.a < 0.005f)) { return 0.0f.xxxx; }
    if (d >= 0.99998f || raw.a < 0.005f) { return 0.0f.xxxx; }

    const int2 dMax = max(int2(dims.xy) - 1, int2(0, 0));
    const float z = max(viewZOf(d), 0.25f);
    const float3 wpC = worldAt(float2(p), d);
    const float3 nGeo = reflDepthNormal(p, d, wpC, dMax);

    // 1) 3x3 Neighborhood Luminance Firefly Clamp & Specular Lobe Roughness Estimator:
    //    Suppresses single-ray RT bright fireflies BEFORE spatial gather and estimates effective lobe roughness
    //    so mirror-smooth puddles/ice (<0.08) keep crisp 1px reflections while rougher iron/stone (0.15..0.85) widens cleanly!
    float lumSum3 = 0.0f, lumSq3 = 0.0f, lumPeak3 = 0.0f, lumSecond3 = 0.0f;
    float cnt3 = 0.0f;
    float alphaDiff3 = 0.0f;
    [unroll] for (int ny = -1; ny <= 1; ++ny) {
        [unroll] for (int nx = -1; nx <= 1; ++nx) {
            const int2 nq = clamp(p + int2(nx, ny), int2(0, 0), dMax);
            const float dnq = Depth.Load(int3(nq, 0));
            if (dnq >= 0.99998f) { continue; }
            const float4 tq = sanitizeReflTap(RawRefl.Load(int3(nq, 0)));
            alphaDiff3 = max(alphaDiff3, abs(tq.a - raw.a));
            if (tq.a < 0.005f) { continue; }
            const float lq = dot(tq.rgb, kReflLuma);
            lumSum3 += lq;
            lumSq3  += lq * lq;
            cnt3    += 1.0f;
            if (lq >= lumPeak3) { lumSecond3 = lumPeak3; lumPeak3 = lq; }
            else if (lq > lumSecond3) { lumSecond3 = lq; }
        }
    }
    const float lumMean3 = (cnt3 > 0.5f) ? (lumSum3 / cnt3) : dot(raw.rgb, kReflLuma);
    const float lumStd3  = (cnt3 > 1.5f) ? sqrt(max(lumSq3 / cnt3 - lumMean3 * lumMean3, 0.0f)) : 0.0f;
    const float fireflyCeil = max(lumSecond3 * 2.15f + 0.20f, lumMean3 * 2.50f + 0.25f);

    // Roughness proxy in [0.04..0.85]: flat puddles/water/mirror ice have low local variance -> tight 1px footprint;
    // rough brushed iron/stone/wet wood show wider VNDF ray variance -> smooth wider specular lobe footprint!
    const float relRaySpread = saturate(lumStd3 / max(lumMean3 + 0.08f, 0.08f));
    const float mirrorPuddleHint = (sunColour.w > 0.5f && weather.y > 0.25f && abs(nGeo.z) > 0.94f) ? 1.0f : 0.0f;
    const float estRough = clamp(lerp(relRaySpread * 0.88f + 0.04f, 0.04f, mirrorPuddleHint * 0.85f), 0.04f, 0.85f);
    const float lobeT = smoothstep(0.08f, 0.72f, estRough); // 0 = mirror-crisp 1px, 1 = wide rough specular lobe
    const float gaussK = lerp(2.65f, 0.20f, lobeT);
    const float stepMult = lerp(1.0f, 1.50f, lobeT);
    const float invZ = 1.0f / max(z, 0.25f);
    const float planeTol = lerp(0.0045f, 0.0115f, lobeT);

    float16_t4 s1 = float16_t4(0.0h, 0.0h, 0.0h, 0.0h), s2 = float16_t4(0.0h, 0.0h, 0.0h, 0.0h);
    float16_t wSum = float16_t(0.0h);

    // 2) Roughness-Adaptive 5x5 Specular Bilateral Gather with Firefly Clamp, Plane-Distance & Normal-Power Edge Stopping:
    [unroll] for (int dy = -2; dy <= 2; dy++) {
        [unroll] for (int dx = -2; dx <= 2; dx++) {
            const float2 fOff = float2(float(dx), float(dy)) * stepMult;
            const int2 q = clamp(p + int2(round(fOff)), int2(0, 0), dMax);
            const float dq = Depth.Load(int3(q, 0));
            if (dq >= 0.99998f) { continue; }
            float4 tap = sanitizeReflTap(RawRefl.Load(int3(q, 0)));
            if (tap.a < 0.005f) { continue; }

            // Apply 3x3 neighborhood luminance firefly clamp before spatial gather:
            const float tapLumRaw = dot(tap.rgb, kReflLuma);
            if (tapLumRaw > fireflyCeil) {
                tap.rgb *= fireflyCeil / max(tapLumRaw, 1.0e-4f);
            }
            const float pk = max(tap.r, max(tap.g, tap.b));
            if (pk > 2.4f) { tap.rgb *= 2.4f / pk; }

            // Exact geometric plane-distance depth stopping (zero light-leak across oblique floors/walls!):
            const float3 wpQ = worldAt(float2(q), dq);
            const float planeDist = abs(dot(wpQ - wpC, nGeo)) * invZ;
            const float wPlane = exp(-pow(planeDist / planeTol, 2.0f));
            if (wPlane < 1.0e-4f) { continue; }

            // Exact normal-power stopping on geometric creases (prevents bleed across 90-deg corners & silhouettes):
            float wNorm = 1.0f;
            if (dx != 0 || dy != 0) {
                const float3 nQ = reflDepthNormal(q, dq, wpQ, dMax);
                const float nd = saturate(dot(nGeo, nQ));
                if (nd < 0.65f) { continue; }
                wNorm = pow(nd, lerp(48.0f, 18.0f, lobeT));
            }

            // Roughness/hit-confidence similarity + roughness-scaled Gaussian footprint + Karis stability:
            const float wRough = exp(-abs(tap.a - raw.a) * 8.0f);
            const float r2 = float(dx * dx + dy * dy);
            const float gauss = exp(-r2 * gaussK);
            const float tapLum = dot(tap.rgb, kReflLuma);
            const float karis = 1.0f / (1.0f + tapLum * 0.45f);
            const float16_t w = float16_t(wPlane * wNorm * wRough * gauss * karis);
            const float16_t4 t16 = float16_t4(tap);
            s1 += t16 * w;
            s2 += (t16 * t16) * w;
            wSum += w;
        }
    }
    if (float(wSum) < 1.0e-4f) { return float4(raw.rgb, raw.a); }
    const float invWSum = 1.0f / max(float(wSum), 1.0e-5f);
    const float4 m1 = float4(s1) * invWSum;
    const float4 stdDev = sqrt(max(float4(s2) * invWSum - m1 * m1, 0.0f.xxxx));
    float4 cur = m1;

    // 3) Motion-vector temporal reprojection + roughness-adaptive SVGF variance clamp:
    const float2 uv = (floor(at.xy) + 0.5f) * dims.zw;
    const float2 mvRaw = Motion.Load(ip);
    const float2 mv = (any(isnan(mvRaw)) || any(isinf(mvRaw))) ? 0.0f.xx : mvRaw;
    const float2 puv = uv + mv;
    if (all(puv > 0.005f) && all(puv < 0.995f)) {
        const float4 prev = sanitizeReflTap(HistPrev.SampleLevel(Lin, puv, 0.0f));
        if (prev.a > 0.01f) {
            const float clampSig = lerp(0.95f, 1.25f, lobeT);
            const float4 lo = max(m1 - stdDev * clampSig, 0.0f.xxxx);
            const float4 hi = m1 + stdDev * clampSig;
            const float4 clamped = clamp(prev, lo, hi);
            const float speed = length(mv * dims.xy);
            const float curLum = dot(cur.rgb, kReflLuma);
            const float prevLum = dot(clamped.rgb, kReflLuma);
            const float stdLum = dot(stdDev.rgb, kReflLuma);
            const float edgeStab = 1.0f - saturate(alphaDiff3 * 2.5f);
            const float trapSep = rawTrappingSeparator(curLum, prevLum, stdLum, speed, edgeStab);
            const float lumJump = saturate(abs(curLum - prevLum) / max(curLum + prevLum + 0.08f, 0.08f));
            const float minAlpha = lerp(0.14f, 0.06f, lobeT); // crisp mirror response vs silky rough convergence
            const float alpha = saturate(max(lerp(minAlpha, 0.30f, saturate(speed * 0.06f + lumJump * 0.30f)), trapSep * 0.85f));
            cur = lerp(clamped, cur, alpha);
        }
    }
    return sanitizeReflTap(cur);
}
