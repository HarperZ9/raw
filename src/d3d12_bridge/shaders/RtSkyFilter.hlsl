// RtSkyFilter.hlsl: one sky ray a pixel, made usable (RtSky.cpp). Per pixel: a depth-aware mean of
// the 3 x 3 raw rays around it, blended into the history found along the game's motion vector
// (bilinear, each tap only at the pixel's own view depth), up to 32 frames. While the history is
// short the mean widens to 5 x 5. Out: the light-index output's alpha (the lighting pass's sky
// visibility), the filtered bounce (RtSky.h) and the next histories. Original work, MIT.
#define ATLAS_SLOT t9
#include "LightingCommon.hlsli"

Texture2D<float>  Depth  : register(t0);
Texture2D<float2> Motion : register(t2);
RWTexture2D<float4>      Raw     : register(u0);   // bounce rgb, sky a
RWTexture2D<float4>      HistIn  : register(u1);   // sky, view depth, frames
RWTexture2D<float4>      HistOut : register(u2);
RWTexture2D<unorm float4> Lights : register(u3);
RWTexture2D<float4>      BIn     : register(u4);   // bounce rgb
RWTexture2D<float4>      BOut    : register(u5);
RWTexture2D<float4>      Bounce  : register(u6);   // the lighting pass's input
cbuffer F : register(b1) { uint hasMotion, reset, maxFrames, fPad; };

// Raw alpha: sky (0 or 1), plus 4 + 2 q where the untraced lights' share was sampled, q its value in fifteenths
// (RtShadow.hlsl).
float4 decodeRaw(float4 v, out float rest, out float has) {
    has = v.a >= 3.5f ? 1.0f : 0.0f;
    const float t = v.a - 4.0f * has;
    const float q = has > 0.5f ? floor(t * 0.5f) : 0.0f;
    rest = q / 15.0f;
    return float4(v.rgb, t - 2.0f * q);
}

// Fast screen-space depth-plane slope (mathematically equivalent to world plane-distance, 0 matrix multiplies).
float2 depthPlaneSlope(int2 p, float z) {
    const int2 lim = int2(dims.xy) - 1;
    const float zr = viewZOf(Depth.Load(int3(min(p.x + 1, lim.x), p.y, 0))) - z;
    const float zl = z - viewZOf(Depth.Load(int3(max(p.x - 1, 0), p.y, 0)));
    const float zd = viewZOf(Depth.Load(int3(p.x, min(p.y + 1, lim.y), 0))) - z;
    const float zu = z - viewZOf(Depth.Load(int3(p.x, max(p.y - 1, 0), 0)));
    return float2(abs(zr) < abs(zl) ? zr : zl, abs(zd) < abs(zu) ? zd : zu);
}

float4 meanAround(int2 p, float z, float2 zSlope, int r, out float rest, out float4 stdDev) {
    float4 s = 0.0f, s2 = 0.0f;
    float w = 0.0f, rs = 0.0f, rw = 0.0f, rv, rh;
    rest = 1.0f;
    const float invPlane = 1.0f / max(z * 0.015f, 1.8f);
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            const int2 q = clamp(p + int2(x, y), int2(0, 0), int2(dims.xy) - 1);
            const float dq = Depth.Load(int3(q, 0));
            if (dq >= 1.0f) { continue; }
            const float zPred = z + zSlope.x * float(x) + zSlope.y * float(y);
            const float pd = abs(viewZOf(dq) - zPred) * invPlane;
            const float k = exp(-pd * pd);
            float4 tap = decodeRaw(Raw[q], rv, rh);
            // Preserve rich sunlit grass/snow/hearth indirect bounce energy while rejecting extreme fireflies (>3.5 HDR).
            const float tapMax = max(tap.r, max(tap.g, tap.b));
            if (tapMax > 3.50f) { tap.rgb *= 3.50f / tapMax; }
            s += k * tap; s2 += k * (tap * tap); w += k;
            rs += k * rh * rv; rw += k * rh;
        }
    }
    if (rw > 1.0e-4f) { rest = rs / rw; }
    if (w > 1.0e-4f) {
        const float4 m1 = s / w;
        const float4 m2 = s2 / w;
        stdDev = sqrt(max(m2 - m1 * m1, 0.0f.xxxx));
        return m1;
    }
    stdDev = 0.1f.xxxx;
    return decodeRaw(Raw[p], rv, rh);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= uint(dims.x) || id.y >= uint(dims.y)) { return; }
    const int2 p = int2(id.xy);
    const float d = Depth.Load(int3(p, 0));
    const float4 L = Lights[p];
    // SM6.9 Wave-coherent sky early-exit: pure-sky waves skip the entire 5x5 bilateral A-trous GI/RTAO filter.
    if (WaveActiveAllTrue(d >= 1.0f) || d >= 1.0f) {
        HistOut[p] = float4(1.0f, -1.0f, 0.0f, 0.0f);
        BOut[p] = 0.0f;
        Bounce[p] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        Lights[p] = float4(L.xyz, 1.0f);
        return;
    }
    const float z = viewZOf(d);
    float n = 0.0f, past = 0.0f, pastR = 1.0f, pastM2 = 0.0f;
    float3 pastB = 0.0f;
    bool have = false;
    const float2 zSlope = depthPlaneSlope(p, z);
    if (reset == 0u) {
        const float2 mv = hasMotion != 0u ? Motion.Load(int3(p, 0)) : float2(0.0f, 0.0f);
        const float2 prev = float2(p) + mv * dims.xy;
        const int2 pb = int2(floor(prev));
        const float2 pa = prev - float2(pb);
        float hs = 0.0f, hn = 0.0f, hw = 0.0f, hr = 0.0f, hm2 = 0.0f;
        float3 hb = 0.0f;
        [unroll] for (int k = 0; k < 4; k++) {
            const int2 t = pb + int2(k & 1, k >> 1);
            if (any(t < 0) || t.x >= int(dims.x) || t.y >= int(dims.y)) { continue; }
            const float4 h = HistIn[t];
            if (h.y <= 0.0f || abs(h.y - min(z, 65000.0f)) > 0.04f * z) { continue; }
            const float w = ((k & 1) ? pa.x : 1.0f - pa.x) * ((k >> 1) ? pa.y : 1.0f - pa.y);
            const float4 bTap = BIn[t];
            hs += w * h.x; hn += w * h.z; hw += w; hb += w * bTap.rgb; hm2 += w * bTap.a; hr += w * h.w;
        }
        // Spatiotemporal ReSTIR-style 4-neighbor indirect GI reservoir reuse (radius 5..14px, rotated per-frame):
        // shares converged indirect bounce samples across depth-plane-coherent neighbors (48+ effective spp).
        if (hw > 0.05f) {
            past = hs / hw; n = hn / hw; pastB = hb / hw; pastM2 = hm2 / hw; pastR = hr / hw; have = true;
            const float ang0 = ign(float2(p) + float2(proj.z * 7.3f, proj.z * 13.1f)) * 6.2831853f;
            float spW = min(n, 16.0f);
            float3 spB = pastB * spW;
            float spS = past * spW;
            float spM2 = pastM2 * spW;
            const float invPlaneS = 1.0f / max(z * 0.022f, 2.5f);
            [unroll] for (int si = 0; si < 4; si++) {
                const float a = ang0 + 1.5707963f * float(si);
                const float rad = 5.0f + 3.0f * float(si);
                const int2 off = int2(round(float2(cos(a), sin(a)) * rad));
                const int2 q = clamp(pb + off, int2(0, 0), int2(dims.xy) - 1);
                const float4 hq = HistIn[q];
                if (hq.y <= 0.0f) { continue; }
                const float zPred = z + zSlope.x * float(off.x) + zSlope.y * float(off.y);
                const float pd = abs(hq.y - min(zPred, 65000.0f)) * invPlaneS;
                if (pd > 2.2f) { continue; }
                const float wq = exp(-pd * pd) * min(hq.z, 8.0f) * 0.18f;
                const float4 bq = BIn[q];
                spB += bq.rgb * wq;
                spS += hq.x * wq;
                spM2 += bq.a * wq;
                spW += wq;
            }
            pastB = spB / spW;
            past = spS / spW;
            pastM2 = spM2 / spW;
        }
    }
    float curR;
    float4 curStd;
    const float4 cur = meanAround(p, z, zSlope, n < 10.0f ? 2 : 1, curR, curStd);
    // True SVGF temporal variance (Var = max(0, E[L^2] - E[L]^2)) modulates history clamp cleanly:
    // high temporal variance widens clamp responsiveness on dynamic lighting; converged surfaces stay rock-solid.
    const float curLum = dot(cur.rgb, float3(0.2126f, 0.7152f, 0.0722f));
    const float pastLum = dot(pastB, float3(0.2126f, 0.7152f, 0.0722f));
    const float tempVar = have ? max(pastM2 - pastLum * pastLum, 0.0f) : 0.0f;
    const float sigScale = lerp(1.50f, 2.40f, saturate(n / 12.0f)) * (1.0f + saturate(sqrt(tempVar) * 1.5f) * 0.25f);
    const float4 pad = max(curStd * sigScale, float4(0.06f, 0.06f, 0.06f, 0.10f));
    if (have) {
        past = clamp(past, saturate(cur.a - pad.a), saturate(cur.a + pad.a));
        pastB = clamp(pastB, max(cur.rgb - pad.rgb, 0.0f.xxx), cur.rgb + pad.rgb);
    }
    n = have ? min(n + 1.0f, float(maxFrames)) : 1.0f;
    const float alphaT = 1.0f / n;
    const float vis = have ? lerp(past, cur.a, alphaT) : cur.a;
    const float3 b = have ? lerp(pastB, cur.rgb, alphaT) : cur.rgb;
    const float m2Out = have ? lerp(pastM2, curLum * curLum, alphaT) : (curLum * curLum);
    const float rest = have ? lerp(pastR, curR, alphaT) : curR;
    HistOut[p] = float4(vis, min(z, 65000.0f), n, rest);
    BOut[p] = float4(b, m2Out);
    Bounce[p] = float4(b, rest);   // alpha: the untraced lights' visibility (Lighting.hlsl, FLAG_REST)
    Lights[p] = float4(L.xyz, vis);
}
