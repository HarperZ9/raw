// RtSkyFilter.hlsl: Spatiotemporal SVGF GI & Sky-Visibility Filter (SM 6.6-6.10).
// Per pixel: depth-plane-aware 3x3/5x5 mean with conservative energy-conserving firefly shoulder,
// bilinear temporal reprojection with tight depth + motion-divergence disocclusion rejection,
// velocity-adaptive ray-to-mean YCoCg variance clipping (fast reactive response so moving torches & NPCs
// never leave lingering bright ghosts!), and subtle physical aurora/twilight sky-bounce chromaticity on
// exterior sky-exposed snow & stone. Original work, MIT.
#define ATLAS_SLOT t9
#include "LightingCommon.hlsli"
#include "RawCosmosSky.hlsli"

Texture2D<float>  Depth  : register(t0);
Texture2D<float2> Motion : register(t2);
RWTexture2D<float4>      Raw     : register(u0);   // bounce rgb, sky a
RWTexture2D<float4>      HistIn  : register(u1);   // sky, view depth, frames, restVis
RWTexture2D<float4>      HistOut : register(u2);
RWTexture2D<unorm float4> Lights : register(u3);
RWTexture2D<float4>      BIn     : register(u4);   // bounce rgb, lum^2
RWTexture2D<float4>      BOut    : register(u5);
RWTexture2D<float4>      Bounce  : register(u6);   // lighting pass input (rgb, packed restVis + sigma)
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

// Ray-to-mean YCoCg variance clipping (Playdead/Karis): clips out-of-hull temporal GI history along
// the ray toward neighborhood mean instead of corner clamping (kills chroma ghost trails cleanly!).
float3 skyClipYCoCg(float3 meanY, float3 stdY, float gamma, float3 histY) {
    const float3 eClip = max(stdY * gamma, float3(0.018f, 0.015f, 0.015f));
    const float3 vClip = histY - meanY;
    const float3 aUnit = abs(vClip / eClip);
    const float ma = max(aUnit.x, max(aUnit.y, aUnit.z));
    return ma > 1.0f ? (meanY + vClip / ma) : histY;
}

// Restrained exterior nocturnal aurora & Chappuis-ozone twilight hemisphere sky-bounce chromaticity:
// Gives snow and pale stone subtle cool emerald-indigo aurora bounce on clear northern nights and rose-indigo
// twilight sky bounce at dusk/dawn — strictly bounded (<= 0.055 linear) so surfaces NEVER glow artificially!
float3 evalNightAuroraSkyBounce(float skyVis) {
    if (sunColour.w <= 0.5f || skyVis <= 0.04f) { return 0.0f.xxx; }
    const float aElev = astroSunElev();
    if (aElev >= 0.14f) { return 0.0f.xxx; }

    const float clearSky = pow(1.0f - saturate(clouds.x * 1.02f), 1.45f);
    const float simTime = physWorld[0].x > 0.001f ? physWorld[0].x : camPosW.w;
    float3 skyTint = 0.0f.xxx;

    // 1) Clear-night aurora + galactic hemisphere irradiance (modulated smoothly by camPosW.y northern latitude):
    const float nightW = smoothstep(0.03f, -0.09f, aElev);
    if (nightW > 0.01f && clearSky > 0.02f) {
        // Sample representative northern upper-hemisphere aurora curtain direction at zero-texture cost:
        const float3 aurDirN = normalize(float3(0.08f, 0.52f, 0.85f));
        const float3 aurRad = rawCosmosVolumetricAurora(aurDirN, simTime, camPosW.y, nightW, clouds.x);
        const float latW = lerp(0.45f, 1.00f, smoothstep(-30000.0f, 90000.0f, camPosW.y));
        const float3 indigoFloor = float3(0.004f, 0.008f, 0.019f) * (nightW * clearSky);
        // Conservative linear irradiance injection (max ~0.048): crisp emerald/indigo tint on snow without self-glow!
        skyTint += min(aurRad * (0.24f * latW) + indigoFloor, float3(0.032f, 0.052f, 0.046f));
    }

    // 2) Chappuis-ozone twilight hemisphere sky tint (cool indigo-rose dusk/dawn fill on sky-exposed stone & snow):
    const float twilightW = smoothstep(-0.12f, -0.01f, aElev) * (1.0f - smoothstep(0.04f, 0.14f, aElev));
    if (twilightW > 0.01f) {
        skyTint += float3(0.022f, 0.016f, 0.034f) * (twilightW * (0.45f + 0.55f * clearSky));
    }

    return skyTint * saturate(skyVis);
}

float4 meanAround(int2 p, float z, float2 zSlope, int r, out float rest, out float4 stdDev, out float3 meanY, out float3 stdY) {
    float4 s = 0.0f, s2 = 0.0f;
    float3 sy1 = 0.0f.xxx, sy2 = 0.0f.xxx;
    float w = 0.0f, rs = 0.0f, rw = 0.0f, rv, rh;
    rest = 1.0f;
    const float invPlane = 1.0f / max(z * 0.012f, 1.4f);
    const float invAbsZ  = 1.0f / max(z * 0.030f, 3.0f);
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            const int2 q = clamp(p + int2(x, y), int2(0, 0), int2(dims.xy) - 1);
            const float dq = Depth.Load(int3(q, 0));
            if (dq >= 1.0f) { continue; }
            const float zq = viewZOf(dq);
            const float zPred = z + zSlope.x * float(x) + zSlope.y * float(y);
            const float pd = abs(zq - zPred) * invPlane;
            const float zd = abs(zq - z) * invAbsZ;
            const float k = exp(-pd * pd - zd * zd * 0.45f);
            float4 tap = decodeRaw(Raw[q], rv, rh);
            // Conservative energy-conserving firefly shoulder (knee 0.90, ceiling 1.72 <= 2.2x):
            // Preserves warm hearth/torch bounce while stopping hot emissive rays from blooming into history!
            const float tapMax = max(tap.r, max(tap.g, tap.b));
            if (tapMax > 0.90f) {
                const float softMax = 0.90f + (tapMax - 0.90f) / (1.0f + (tapMax - 0.90f) * 1.30f);
                tap.rgb *= min(softMax, 1.72f) / tapMax;
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
    // Wave-coherent sky early-exit: pure-sky waves skip the entire bilateral GI/RTAO filter.
    if (WaveActiveAllTrue(d >= 1.0f) || d >= 1.0f) {
        HistOut[p] = float4(1.0f, -1.0f, 0.0f, 0.0f);
        BOut[p] = 0.0f;
        Bounce[p] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        Lights[p] = float4(L.xyz, 1.0f);
        return;
    }
    const float z = viewZOf(d);
    const int2 lim = int2(dims.xy) - 1;
    float n = 0.0f, past = 0.0f, pastR = 1.0f, pastM2 = 0.0f;
    float3 pastB = 0.0f;
    bool have = false;
    const float2 zSlope = depthPlaneSlope(p, z);

    // Measure pixel motion magnitude + 4-neighbor motion divergence (detects moving NPCs/weapons/torches instantly!):
    const float2 mvC = hasMotion != 0u ? Motion.Load(int3(p, 0)) : float2(0.0f, 0.0f);
    const float pxVel = length(mvC * dims.xy);
    float mvDivPx = 0.0f;
    if (hasMotion != 0u) {
        const float2 mvR = Motion.Load(int3(min(p.x + 1, lim.x), p.y, 0));
        const float2 mvL = Motion.Load(int3(max(p.x - 1, 0),      p.y, 0));
        const float2 mvD = Motion.Load(int3(p.x, min(p.y + 1, lim.y), 0));
        const float2 mvU = Motion.Load(int3(p.x, max(p.y - 1, 0),      0));
        mvDivPx = max(max(length((mvR - mvC) * dims.xy), length((mvL - mvC) * dims.xy)),
                      max(length((mvD - mvC) * dims.xy), length((mvU - mvC) * dims.xy)));
    }

    if (reset == 0u) {
        const float2 prev = float2(p) + mvC * dims.xy;
        const int2 pb = int2(floor(prev));
        const float2 pa = prev - float2(pb);
        float hs = 0.0f, hn = 0.0f, hw = 0.0f, hr = 0.0f, hm2 = 0.0f;
        float3 hb = 0.0f;
        // Tighter depth reprojection gate (2.8% of viewZ) rejects foreground/background bleed cleanly!
        const float zTol = max(0.028f * z, 2.5f);
        [unroll] for (int k = 0; k < 4; k++) {
            const int2 t = pb + int2(k & 1, k >> 1);
            if (any(t < 0) || t.x >= int(dims.x) || t.y >= int(dims.y)) { continue; }
            const float4 h = HistIn[t];
            if (h.y <= 0.0f || abs(h.y - min(z, 65000.0f)) > zTol) { continue; }
            const float w = ((k & 1) ? pa.x : 1.0f - pa.x) * ((k >> 1) ? pa.y : 1.0f - pa.y);
            const float4 bTap = BIn[t];
            hs += w * h.x; hn += w * h.z; hw += w; hb += w * bTap.rgb; hm2 += w * bTap.a; hr += w * h.w;
        }
        if (hw > 0.08f) {
            past = hs / hw; n = hn / hw; pastB = hb / hw; pastM2 = hm2 / hw; pastR = hr / hw; have = true;
        }
    }

    float curR;
    float4 curStd;
    float3 meanY, stdY;
    const float4 cur = meanAround(p, z, zSlope, n < 5.0f ? 2 : 1, curR, curStd, meanY, stdY);

    // Fast reactive temporal clipping & light-change tracking (stops moving torches & NPCs from leaving bright ghosts!):
    const float curLum = dot(cur.rgb, float3(0.2126f, 0.7152f, 0.0722f));
    const float pastLum = dot(pastB, float3(0.2126f, 0.7152f, 0.0722f));
    const float motionTight = saturate(pxVel * 0.22f + mvDivPx * 0.35f);
    const float gammaY = lerp(1.18f, 0.75f, motionTight);
    const float skyPad = max(curStd.a * lerp(1.00f, 1.22f, saturate(n / 8.0f)) * lerp(1.0f, 0.68f, motionTight), 0.048f);

    // Dual sky + GI luminance/chromaticity + motion-divergence disocclusion detector:
    const float skyDelta = have ? abs(cur.a - past) : 0.0f;
    const float giRelDelta = have ? (abs(curLum - pastLum) / max(max(curLum, pastLum), 0.045f)) : 0.0f;
    const float giChrDelta = have ? length(cur.rgb / max(curLum, 0.035f) - pastB / max(pastLum, 0.035f)) : 0.0f;
    if (have) {
        past = clamp(past, saturate(cur.a - skyPad), saturate(cur.a + skyPad));
        pastB = skyYCoCgToRgb(skyClipYCoCg(meanY, stdY, gammaY, skyRgbToYCoCg(pastB)));
    }

    // Immediate reset on hard disocclusion, moving torch light swings, or silhouette velocity divergence:
    const bool hardDisocc = (giRelDelta > 0.38f || giChrDelta > 0.48f || skyDelta > 0.24f || mvDivPx > 1.35f);
    // Smoothly decay effective history length when lighting or camera changes so moving torches converge in 1-2 frames!
    const float nDecayed = n * lerp(1.0f, 0.45f, saturate(giRelDelta * 1.4f + motionTight * 0.65f));
    n = (have && !hardDisocc) ? min(nDecayed + 1.0f, min(float(maxFrames), 10.0f)) : 1.0f;

    const float trapSky = have ? rawTrappingSeparator(cur.a, past, curStd.a, pxVel, 0.5f) : 0.0f;
    const float trapGi  = have ? rawTrappingSeparator(curLum, pastLum, dot(curStd.rgb, float3(0.2126f, 0.7152f, 0.0722f)), pxVel, 0.5f) : 0.0f;
    const float giJumpBoost = max(smoothstep(0.14f, 0.42f, giRelDelta) * 0.82f, smoothstep(0.22f, 0.52f, giChrDelta) * 0.72f);
    const float mvDivBoost  = smoothstep(0.55f, 1.60f, mvDivPx) * 0.85f;
    const float disoccBoost = max(max(smoothstep(0.06f, 0.22f, skyDelta) * 0.55f, giJumpBoost),
                                  max(max(trapSky, trapGi) * 0.86f, mvDivBoost));
    const float alphaT = saturate(max(max(1.0f / n, lerp(0.14f, 0.72f, motionTight)), disoccBoost));

    const float vis = have ? lerp(past, cur.a, alphaT) : cur.a;
    const float3 bFiltered = have ? lerp(pastB, cur.rgb, alphaT) : cur.rgb;
    const float m2Out = have ? lerp(pastM2, curLum * curLum, alphaT) : (curLum * curLum);
    const float bLumOut = dot(bFiltered, float3(0.2126f, 0.7152f, 0.0722f));
    const float varOut = max(m2Out - bLumOut * bLumOut, 0.0f);
    // Boost initial variance on newly disoccluded pixels (n < 4) per Schied SVGF HPG 2017 so A-Trous widens cleanly.
    const float sigmaOut = clamp(sqrt(varOut) + (n < 4.0f ? (4.0f - n) * 0.12f : 0.0f), 0.0f, 1.25f);
    const float rest = have ? lerp(pastR, curR, alphaT) : curR;

    // Add restrained physical night aurora / twilight sky bounce ONLY to the output Bounce (NOT back into BOut history,
    // guaranteeing zero temporal feedback amplification!).
    const float3 skyAuroraTint = evalNightAuroraSkyBounce(vis);
    const float3 bOutDisplay = min(bFiltered + skyAuroraTint, 1.85f.xxx);

    // Pack 16-level restVis + continuous temporal SVGF stdDev sigmaOut into RGBA16F Bounce.a (zero extra VRAM/textures).
    const float packedRestVar = floor(saturate(rest) * 15.0f + 0.5f) * 2.0f + sigmaOut;
    HistOut[p] = float4(vis, min(z, 65000.0f), n, rest);
    BOut[p] = float4(bFiltered, m2Out);
    Bounce[p] = float4(bOutDisplay, packedRestVar);
    Lights[p] = float4(L.xyz, vis);
}
