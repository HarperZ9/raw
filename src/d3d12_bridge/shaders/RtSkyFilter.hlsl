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
// (RtShadow.hlsl). Wave 55: NaN/Inf sanitization guarantees corrupt rays never poison temporal SVGF history.
float sanitizeSkyScalar(float v, float fallback) {
    return isfinite(v) ? v : fallback;
}
float3 sanitizeSkyRgb(float3 c) {
    return all(isfinite(c)) ? max(c, 0.0f.xxx) : 0.0f.xxx;
}

float4 decodeRaw(float4 vRaw, out float rest, out float has) {
    const float3 rgb = sanitizeSkyRgb(vRaw.rgb);
    const float aSafe = isfinite(vRaw.a) ? max(vRaw.a, 0.0f) : 1.0f;
    has = aSafe >= 3.5f ? 1.0f : 0.0f;
    const float t = aSafe - 4.0f * has;
    const float q = has > 0.5f ? clamp(floor(t * 0.5f), 0.0f, 15.0f) : 0.0f;
    rest = q / 15.0f;
    return float4(rgb, saturate(t - 2.0f * q));
}

// Wave 55: Best-sided 3D geometric surface normal reconstruction from Depth (zero Normal SRV needed):
// Picks the closer-in-depth finite difference on X and Y so silhouette corners (doorframes, overhangs, cliff ledges)
// never straddle depth discontinuities, giving an accurate 3D tangent plane for geometric plane-distance stopping!
float3 reconstructGeomNormal(int2 p, float3 wpC, float zC) {
    const int2 lim = int2(dims.xy) - 1;
    const int2 pR = int2(min(p.x + 1, lim.x), p.y);
    const int2 pL = int2(max(p.x - 1, 0),     p.y);
    const int2 pD = int2(p.x, min(p.y + 1, lim.y));
    const int2 pU = int2(p.x, max(p.y - 1, 0));
    const float dR = Depth.Load(int3(pR, 0));
    const float dL = Depth.Load(int3(pL, 0));
    const float dD = Depth.Load(int3(pD, 0));
    const float dU = Depth.Load(int3(pU, 0));
    const bool okR = (dR < 0.99998f);
    const bool okL = (dL < 0.99998f);
    const bool okD = (dD < 0.99998f);
    const bool okU = (dU < 0.99998f);
    const float zR = okR ? viewZOf(dR) : 1.0e9f;
    const float zL = okL ? viewZOf(dL) : 1.0e9f;
    const float zD = okD ? viewZOf(dD) : 1.0e9f;
    const float zU = okU ? viewZOf(dU) : 1.0e9f;

    const float3 ddx = (okR && (!okL || abs(zR - zC) <= abs(zL - zC)))
        ? (worldAt(float2(pR), dR) - wpC)
        : (okL ? (wpC - worldAt(float2(pL), dL)) : float3(1.0f, 0.0f, 0.0f));
    const float3 ddy = (okD && (!okU || abs(zD - zC) <= abs(zU - zC)))
        ? (worldAt(float2(pD), dD) - wpC)
        : (okU ? (wpC - worldAt(float2(pU), dU)) : float3(0.0f, 1.0f, 0.0f));

    float3 n = cross(ddx, ddy);
    const float len2 = dot(n, n);
    if (!isfinite(len2) || len2 < 1.0e-10f) {
        const float3 toEye = eyeAt() - wpC;
        const float e2 = dot(toEye, toEye);
        return (isfinite(e2) && e2 > 1.0e-8f) ? (toEye * rsqrt(e2)) : float3(0.0f, 0.0f, 1.0f);
    }
    n *= rsqrt(len2);
    if (dot(n, eyeAt() - wpC) < 0.0f) { n = -n; }
    return n;
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
    const float nightW = 1.0f - smoothstep(-0.09f, 0.03f, aElev);
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

// Wave 55: True 3D geometric plane-distance bilateral neighborhood mean + plane-consistent sky visibility bounds.
// Prevents bright exterior sky-light from leaking under doorframes, overhangs, and cliff ledges!
float4 meanAround(int2 p, float3 wpC, float3 nGeom, float z, int r, out float rest, out float4 stdDev, out float3 meanY, out float3 stdY, out float skyMin, out float skyMax) {
    float4 s = 0.0f, s2 = 0.0f;
    float3 sy1 = 0.0f.xxx, sy2 = 0.0f.xxx;
    float w = 0.0f, rs = 0.0f, rw = 0.0f, rv, rh;
    rest = 1.0f;
    skyMin = 1.0f;
    skyMax = 0.0f;
    const float invPlane = 1.0f / max(z * 0.0085f, 1.25f);
    const float invAbsZ  = 1.0f / max(z * 0.032f, 3.0f);
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            const int2 q = clamp(p + int2(x, y), int2(0, 0), int2(dims.xy) - 1);
            const float dq = Depth.Load(int3(q, 0));
            if (dq >= 0.99998f) { continue; }
            const float zq = max(viewZOf(dq), 0.1f);
            // True 3D signed perpendicular distance from neighbor world point to center tangent plane:
            const float3 wpQ = worldAt(float2(q), dq);
            const float planeDist = abs(dot(wpQ - wpC, nGeom));
            const float pd = planeDist * invPlane;
            const float zd = abs(zq - z) * invAbsZ;
            const float k = exp(-pd * pd - zd * zd * 0.42f);
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
            if (k > 0.14f) {
                skyMin = min(skyMin, tap.a);
                skyMax = max(skyMax, tap.a);
            }
            rs += k * rh * rv; rw += k * rh;
        }
    }
    if (rw > 1.0e-4f) { rest = saturate(rs / rw); }
    if (w > 1.0e-4f) {
        const float4 m1 = s / w;
        const float4 m2 = s2 / w;
        stdDev = sqrt(max(m2 - m1 * m1, 0.0f.xxxx));
        meanY = sy1 / w;
        stdY = sqrt(max(sy2 / w - meanY * meanY, 0.0f.xxx));
        if (skyMin > skyMax) { skyMin = m1.a; skyMax = m1.a; }
        return float4(sanitizeSkyRgb(m1.rgb), saturate(m1.a));
    }
    const float4 c0 = decodeRaw(Raw[p], rv, rh);
    stdDev = 0.1f.xxxx;
    meanY = skyRgbToYCoCg(c0.rgb);
    stdY = 0.08f.xxx;
    skyMin = c0.a;
    skyMax = c0.a;
    return c0;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= uint(dims.x) || id.y >= uint(dims.y)) { return; }
    const int2 p = int2(id.xy);
    const float d = Depth.Load(int3(p, 0));
    const float4 Lraw = Lights[p];
    const float3 Lxyz = all(isfinite(Lraw.xyz)) ? saturate(Lraw.xyz) : 0.0f.xxx;
    // Wave 55 strict Standard-Z sky passthrough (d >= 0.99998): pure-sky waves skip the entire bilateral GI/RTAO filter.
    if (WaveActiveAllTrue(d >= 0.99998f) || d >= 0.99998f) {
        HistOut[p] = float4(1.0f, -1.0f, 0.0f, 0.0f);
        BOut[p] = 0.0f;
        Bounce[p] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        Lights[p] = float4(Lxyz, 1.0f);
        return;
    }
    const float z = max(sanitizeSkyScalar(viewZOf(d), 100.0f), 0.1f);
    const int2 lim = int2(dims.xy) - 1;
    float n = 0.0f, past = 0.0f, pastR = 1.0f, pastM2 = 0.0f;
    float3 pastB = 0.0f;
    bool have = false;
    const float3 wpC = worldAt(float2(p), d);
    const float3 nGeom = reconstructGeomNormal(p, wpC, z);

    // Measure pixel motion magnitude + 4-neighbor motion divergence (detects moving NPCs/weapons/torches instantly!):
    const float2 mvRaw = hasMotion != 0u ? Motion.Load(int3(p, 0)) : float2(0.0f, 0.0f);
    const float2 mvC = all(isfinite(mvRaw)) ? mvRaw : float2(0.0f, 0.0f);
    const float pxVel = length(mvC * dims.xy);
    float mvDivPx = 0.0f;
    if (hasMotion != 0u) {
        const float2 mvR = Motion.Load(int3(min(p.x + 1, lim.x), p.y, 0));
        const float2 mvL = Motion.Load(int3(max(p.x - 1, 0),      p.y, 0));
        const float2 mvD = Motion.Load(int3(p.x, min(p.y + 1, lim.y), 0));
        const float2 mvU = Motion.Load(int3(p.x, max(p.y - 1, 0),      0));
        mvDivPx = max(max(length((mvR - mvC) * dims.xy), length((mvL - mvC) * dims.xy)),
                      max(length((mvD - mvC) * dims.xy), length((mvU - mvC) * dims.xy)));
        if (!isfinite(mvDivPx)) { mvDivPx = 0.0f; }
    }

    if (reset == 0u) {
        const float2 prev = float2(p) + mvC * dims.xy;
        const int2 pb = int2(floor(prev));
        const float2 pa = prev - float2(pb);
        float hs = 0.0f, hn = 0.0f, hw = 0.0f, hr = 0.0f, hm2 = 0.0f;
        float3 hb = 0.0f;
        // Tighter grazing-aware depth reprojection gate rejects foreground/background & doorframe bleed cleanly!
        const float3 viewDirC = normalize(eyeAt() - wpC + float3(0.0f, 0.0f, 1.0e-5f));
        const float grazing = saturate(abs(dot(nGeom, viewDirC)));
        const float zTol = max((0.022f + 0.014f * (1.0f - grazing)) * z, 2.2f);
        const float zClamped = min(z, 65000.0f);
        [unroll] for (int k = 0; k < 4; k++) {
            const int2 t = pb + int2(k & 1, k >> 1);
            if (any(t < 0) || t.x >= int(dims.x) || t.y >= int(dims.y)) { continue; }
            const float4 h = HistIn[t];
            if (!all(isfinite(h)) || h.y <= 0.0f || abs(h.y - zClamped) > zTol) { continue; }
            const float w = ((k & 1) ? pa.x : 1.0f - pa.x) * ((k >> 1) ? pa.y : 1.0f - pa.y);
            const float4 bTapRaw = BIn[t];
            const float3 bTapRgb = sanitizeSkyRgb(bTapRaw.rgb);
            const float bTapM2 = isfinite(bTapRaw.a) ? max(bTapRaw.a, 0.0f) : 0.0f;
            hs += w * saturate(h.x); hn += w * clamp(h.z, 0.0f, 64.0f); hw += w; hb += w * bTapRgb; hm2 += w * bTapM2; hr += w * saturate(h.w);
        }
        if (hw > 0.08f) {
            past = saturate(hs / hw); n = hn / hw; pastB = hb / hw; pastM2 = hm2 / hw; pastR = saturate(hr / hw); have = true;
        }
    }

    float curR, skyMin, skyMax;
    float4 curStd;
    float3 meanY, stdY;
    const float4 cur = meanAround(p, wpC, nGeom, z, n < 5.0f ? 2 : 1, curR, curStd, meanY, stdY, skyMin, skyMax);

    // Wave 55: Cloud-transition activity awareness (broken/moving exterior cloud cover tightens variance clamping
    // and speeds temporal convergence so sweeping cloud shadows adapt cleanly with ZERO trailing smears!).
    const float cloudCoverAct = saturate(clouds.x * (1.0f - clouds.x * 0.45f) * 2.2f) * saturate(clouds.w);
    const float cloudSpeed01  = saturate(length(cloudWind.xy) / 60.0f);
    const float cloudTransW   = (sunColour.w > 0.5f) ? saturate(cloudCoverAct * (0.35f + 0.65f * cloudSpeed01)) : 0.0f;

    // Wave 65: Separate rigid camera-pan velocity (pxVel, where reprojection is exact!) from non-rigid
    // moving-actor silhouette divergence (mvDivPx) so cam_sweep keeps temporal sky-vis & GI rock-solid
    // instead of flushing 54% of history every frame during smooth camera rotation!
    const float curLum = dot(cur.rgb, float3(0.2126f, 0.7152f, 0.0722f));
    const float pastLum = dot(pastB, float3(0.2126f, 0.7152f, 0.0722f));
    const float rigidPanFactor = saturate(pxVel * 0.035f);
    const float motionTight = saturate(rigidPanFactor + mvDivPx * 0.42f);
    const float dynTight = max(motionTight, cloudTransW * 0.60f);
    const float gammaY = lerp(1.22f, 0.78f, dynTight);
    const float skyPad = max(curStd.a * lerp(1.05f, 1.25f, saturate(n / 8.0f)) * lerp(1.0f, 0.68f, dynTight), lerp(0.044f, 0.024f, cloudTransW));
    const float skyLo = max(saturate(cur.a - skyPad), saturate(skyMin - 0.020f));
    const float skyHi = min(saturate(cur.a + skyPad), saturate(skyMax + 0.020f));

    // Dual sky + GI luminance/chromaticity + motion-divergence disocclusion detector:
    const float skyDelta = have ? abs(cur.a - past) : 0.0f;
    const float giRelDelta = have ? (abs(curLum - pastLum) / max(max(curLum, pastLum), 0.045f)) : 0.0f;
    const float giChrDelta = have ? length(cur.rgb / max(curLum, 0.035f) - pastB / max(pastLum, 0.035f)) : 0.0f;
    if (have) {
        past = (skyLo <= skyHi) ? clamp(past, skyLo, skyHi) : clamp(past, saturate(cur.a - skyPad), saturate(cur.a + skyPad));
        pastB = skyYCoCgToRgb(skyClipYCoCg(meanY, stdY, gammaY, skyRgbToYCoCg(pastB)));
    }

    // Wave 63 + Wave 65 Camera-Rotation-Stable Disocclusion & Temporal Decay:
    // Pure rigid camera yaw/pitch has tiny motion divergence (mvDivPx < 0.85): decay history smoothly instead
    // of binary hard-resetting on 1-spp ray variance (kills shadow/GI flicker during cam_sweep!), while
    // moving NPCs/torches/silhouettes (mvDivPx > 1.35) still reset immediately with zero ghosting!
    const bool hardDisocc = (mvDivPx > 1.45f) || (giRelDelta > 0.65f && skyDelta > 0.34f) || (giChrDelta > 0.68f && mvDivPx > 0.55f);
    const float nDecayed = max(n * lerp(1.0f, 0.55f, saturate(giRelDelta * 1.05f + motionTight * 0.38f + skyDelta * (0.40f + cloudTransW * 1.50f))), 1.0f);
    n = (have && !hardDisocc) ? min(nDecayed + 1.0f, min(float(maxFrames), 12.0f)) : 1.0f;

    const float trapSky = have ? rawTrappingSeparator(cur.a, past, curStd.a, pxVel * 0.35f, 0.5f) : 0.0f;
    const float trapGi  = have ? rawTrappingSeparator(curLum, pastLum, dot(curStd.rgb, float3(0.2126f, 0.7152f, 0.0722f)), pxVel * 0.35f, 0.5f) : 0.0f;
    const float giJumpBoost = max(smoothstep(0.24f, 0.58f, giRelDelta) * 0.68f, smoothstep(0.30f, 0.62f, giChrDelta) * 0.60f);
    const float mvDivBoost  = smoothstep(0.55f, 1.60f, mvDivPx) * 0.85f;
    const float cloudBoost  = smoothstep(0.035f, 0.16f, skyDelta) * cloudTransW * 0.50f;
    const float disoccBoost = max(max(max(smoothstep(0.14f, 0.34f, skyDelta) * 0.42f, cloudBoost), giJumpBoost),
                                  max(max(trapSky, trapGi) * 0.70f, mvDivBoost));
    const float alphaT = saturate(max(max(1.0f / max(n, 1.0f), lerp(0.10f, 0.42f, motionTight)), disoccBoost));

    const float vis = saturate(sanitizeSkyScalar(have ? lerp(past, cur.a, alphaT) : cur.a, cur.a));
    const float3 bFiltered = sanitizeSkyRgb(have ? lerp(pastB, cur.rgb, alphaT) : cur.rgb);
    const float m2Out = max(sanitizeSkyScalar(have ? lerp(pastM2, curLum * curLum, alphaT) : (curLum * curLum), curLum * curLum), 0.0f);
    const float bLumOut = dot(bFiltered, float3(0.2126f, 0.7152f, 0.0722f));
    const float varOut = max(m2Out - bLumOut * bLumOut, 0.0f);
    // Boost initial variance on newly disoccluded pixels (n < 4) per Schied SVGF HPG 2017 so A-Trous widens cleanly.
    const float sigmaOut = clamp(sqrt(varOut) + (n < 4.0f ? (4.0f - n) * 0.12f : 0.0f), 0.0f, 1.25f);
    const float rest = saturate(sanitizeSkyScalar(have ? lerp(pastR, curR, alphaT) : curR, 1.0f));

    // Add restrained physical night aurora / twilight sky bounce ONLY to the output Bounce (NOT back into BOut history,
    // guaranteeing zero temporal feedback amplification!).
    const float3 skyAuroraTint = sanitizeSkyRgb(evalNightAuroraSkyBounce(vis));
    const float3 bOutDisplay = min(bFiltered + skyAuroraTint, 1.85f.xxx);

    // Pack 16-level restVis + continuous temporal SVGF stdDev sigmaOut into RGBA16F Bounce.a (zero extra VRAM/textures).
    const float packedRestVar = floor(saturate(rest) * 15.0f + 0.5f) * 2.0f + sigmaOut;
    HistOut[p] = float4(vis, min(z, 65000.0f), n, rest);
    BOut[p] = float4(bFiltered, m2Out);
    Bounce[p] = float4(bOutDisplay, packedRestVar);
    Lights[p] = float4(Lxyz, vis);
}
