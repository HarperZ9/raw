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

float3 skyRgbToYCoCg(float3 c) {
    return float3(0.25f * c.r + 0.5f * c.g + 0.25f * c.b,
                  0.5f * c.r - 0.5f * c.b,
                 -0.25f * c.r + 0.5f * c.g - 0.25f * c.b);
}

float3 skyYCoCgToRgb(float3 y) {
    return max(float3(y.x + y.y - y.z, y.x + y.z, y.x - y.y - y.z), 0.0f.xxx);
}

// Ray-to-mean YCoCg variance clipping (Playdead/Karis/superframe): clips out-of-hull temporal GI history along
// the ray toward neighborhood mean instead of corner clamping (kills chroma ghost trails cleanly!).
float3 skyClipYCoCg(float3 meanY, float3 stdY, float gamma, float3 histY) {
    const float3 eClip = max(stdY * gamma, float3(0.025f, 0.020f, 0.020f));
    const float3 vClip = histY - meanY;
    const float3 aUnit = abs(vClip / eClip);
    const float ma = max(aUnit.x, max(aUnit.y, aUnit.z));
    return ma > 1.0f ? (meanY + vClip / ma) : histY;
}

float4 meanAround(int2 p, float z, float2 zSlope, int r, out float rest, out float4 stdDev, out float3 meanY, out float3 stdY) {
    float4 s = 0.0f, s2 = 0.0f;
    float3 sy1 = 0.0f.xxx, sy2 = 0.0f.xxx;
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
            // Smooth energy-conserving firefly shoulder (knee 0.95, ceiling 1.80): preserves rich sunlit grass/snow/hearth
            // indirect color bleed while stopping single-ray emissive fireflies from injecting runaway GI glow into history!
            const float tapMax = max(tap.r, max(tap.g, tap.b));
            if (tapMax > 0.95f) {
                const float softMax = 0.95f + (tapMax - 0.95f) / (1.0f + (tapMax - 0.95f) * 1.15f);
                tap.rgb *= min(softMax, 1.80f) / tapMax;
            }
            const float3 ty = skyRgbToYCoCg(tap.rgb);
            s += k * tap; s2 += k * (tap * tap);
            sy1 += k * ty; sy2 += k * (ty * ty);
            w += k;
            rs += k * rh * rv; rw += k * rh;
        }
    }
    if (rw > 1.0e-4f) { rest = rs / rw; }
    if (w > 1.0e-4f) {
        const float4 m1 = s / w;
        const float4 m2 = s2 / w;
        stdDev = sqrt(max(m2 - m1 * m1, 0.0f.xxxx));
        meanY = sy1 / w;
        stdY = sqrt(max(sy2 / w - meanY * meanY, 0.0f.xxx));
        return m1;
    }
    const float4 c0 = decodeRaw(Raw[p], rv, rh);
    stdDev = 0.1f.xxxx;
    meanY = skyRgbToYCoCg(c0.rgb);
    stdY = 0.08f.xxx;
    return c0;
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
        // Pure bilinear temporal reprojection ONLY into temporal history (BOut):
        // Removed recursive spatial BIn[q] feedback into BOut (eliminates IIR GI glow diffusion & normal-blind ghosting;
        // full Normal^64 + depth-plane + SVGF variance spatial filtering runs cleanly downstream in GiAtrousPass.hlsl!).
        if (hw > 0.05f) {
            past = hs / hw; n = hn / hw; pastB = hb / hw; pastM2 = hm2 / hw; pastR = hr / hw; have = true;
        }
    }
    float curR;
    float4 curStd;
    float3 meanY, stdY;
    const float4 cur = meanAround(p, z, zSlope, n < 6.0f ? 2 : 1, curR, curStd, meanY, stdY);
    // Superframe / Karis YCoCg ray-to-mean variance clipping (gamma 1.25 -> 0.85 under motion) + dual sky/GI disocclusion reset!
    const float curLum = dot(cur.rgb, float3(0.2126f, 0.7152f, 0.0722f));
    const float pastLum = dot(pastB, float3(0.2126f, 0.7152f, 0.0722f));
    const float tempVar = have ? max(pastM2 - pastLum * pastLum, 0.0f) : 0.0f;
    const float2 mvSpeed = (have && hasMotion != 0u) ? Motion.Load(int3(p, 0)) * dims.xy : float2(0.0f, 0.0f);
    const float pxVel = length(mvSpeed);
    const float gammaY = lerp(1.25f, 0.80f, saturate(pxVel * 0.18f));
    const float skyPad = max(curStd.a * lerp(1.05f, 1.30f, saturate(n / 10.0f)) * lerp(1.0f, 0.72f, saturate(pxVel * 0.18f)), 0.055f);
    // Dual sky + GI luminance/chromaticity disocclusion detector: immediately flushes stale shadows & stale GI color!
    const float skyDelta = have ? abs(cur.a - past) : 0.0f;
    const float giRelDelta = have ? (abs(curLum - pastLum) / max(max(curLum, pastLum), 0.06f)) : 0.0f;
    const float giChrDelta = have ? length(cur.rgb / max(curLum, 0.04f) - pastB / max(pastLum, 0.04f)) : 0.0f;
    if (have) {
        past = clamp(past, saturate(cur.a - skyPad), saturate(cur.a + skyPad));
        pastB = skyYCoCgToRgb(skyClipYCoCg(meanY, stdY, gammaY, skyRgbToYCoCg(pastB)));
    }
    const bool hardDisocc = (giRelDelta > 0.58f || giChrDelta > 0.70f || skyDelta > 0.32f);
    n = (have && !hardDisocc) ? min(n + 1.0f, min(float(maxFrames), 12.0f)) : 1.0f;
    const float trapSky = have ? rawTrappingSeparator(cur.a, past, curStd.a, pxVel, 0.5f) : 0.0f;
    const float trapGi  = have ? rawTrappingSeparator(curLum, pastLum, dot(curStd.rgb, float3(0.2126f, 0.7152f, 0.0722f)), pxVel, 0.5f) : 0.0f;
    const float giJumpBoost = max(smoothstep(0.20f, 0.58f, giRelDelta) * 0.72f, smoothstep(0.30f, 0.70f, giChrDelta) * 0.62f);
    const float disoccBoost = max(max(smoothstep(0.08f, 0.26f, skyDelta) * 0.48f, giJumpBoost), max(trapSky, trapGi) * 0.82f);
    const float alphaT = saturate(max(max(1.0f / n, lerp(0.12f, 0.60f, saturate(pxVel * 0.10f))), disoccBoost));
    const float vis = have ? lerp(past, cur.a, alphaT) : cur.a;
    const float3 b = have ? lerp(pastB, cur.rgb, alphaT) : cur.rgb;
    const float m2Out = have ? lerp(pastM2, curLum * curLum, alphaT) : (curLum * curLum);
    const float bLumOut = dot(b, float3(0.2126f, 0.7152f, 0.0722f));
    const float varOut = max(m2Out - bLumOut * bLumOut, 0.0f);
    // Boost initial variance on newly disoccluded pixels (n < 4) per Schied SVGF HPG 2017 so A-Trous widens cleanly.
    const float sigmaOut = clamp(sqrt(varOut) + (n < 4.0f ? (4.0f - n) * 0.14f : 0.0f), 0.0f, 1.5f);
    const float rest = have ? lerp(pastR, curR, alphaT) : curR;
    // Pack 16-level restVis + continuous temporal SVGF stdDev sigmaOut into RGBA16F Bounce.a (zero extra VRAM/textures).
    const float packedRestVar = floor(saturate(rest) * 15.0f + 0.5f) * 2.0f + sigmaOut;
    HistOut[p] = float4(vis, min(z, 65000.0f), n, rest);
    BOut[p] = float4(b, m2Out);
    Bounce[p] = float4(b, packedRestVar);
    Lights[p] = float4(L.xyz, vis);
}
