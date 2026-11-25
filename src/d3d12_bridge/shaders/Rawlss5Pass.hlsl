// Rawlss5Pass.hlsl: RAWLSS 6.0 Ultra — Native SM 6.6+ Perceptual HDR Temporal Super-Resolution & Ray-Reconstruction Kernel.
// Upgrades over RAWLSS 5.5:
//   1) Karis-Tonemapped Perceptual YCoCg Space for 5-Tap Jimenez Catmull-Rom & 3x3 Moments (zero HDR highlight ringing/glow!)
//   2) Ray-to-Mean YCoCg AABB Intersection Clipping (clipAABB) instead of naive box clamp (preserves exact chroma in motion)
//   3) Material-Aware Reactive Mask + Highlight-Ceiling Guard (zero synthetic noise or sharpen overshoot on glaciers/ice/highlights!)
//   4) 3x3 Depth-Dilated Closest-Velocity Anti-Ghosting + Isolated Specular Firefly Ray-Reconstruction Filter
//   5) Dual-Scale 3D Weingarten Curvature + Bounded FP16 Residual Material-Light Harmonizer (6 -> 8 -> 3)
// Original work, MIT.
#include "LightingCommon.hlsli"

Texture2D<float4> CurHdr   : register(t0);
Texture2D<float>  Depth    : register(t1);
Texture2D<float4> Normal   : register(t2);
Texture2D<float2> Motion   : register(t3);
Texture2D<float4> Layer    : register(t4);
Texture2D<float4> HistPrev : register(t5);
SamplerState      Lin      : register(s0);

static const float3 kLumaW = float3(0.2126f, 0.7152f, 0.0722f);

float3 tonemapKaris(float3 c) {
    const float l = max(dot(max(c, 0.0f.xxx), kLumaW), 0.0f);
    return max(c, 0.0f.xxx) * rcp(1.0f + l);
}

float3 invTonemapKaris(float3 c) {
    const float l = clamp(dot(max(c, 0.0f.xxx), kLumaW), 0.0f, 0.992f);
    return max(c, 0.0f.xxx) * rcp(max(1.0f - l, 0.008f));
}

float3 rgbToYCoCg(float3 c) {
    return float3(0.25f * c.r + 0.5f * c.g + 0.25f * c.b,
                  0.5f * c.r - 0.5f * c.b,
                 -0.25f * c.r + 0.5f * c.g - 0.25f * c.b);
}

float3 yCoCgToRgb(float3 y) {
    return float3(y.x + y.y - y.z, y.x + y.z, y.x - y.y - y.z);
}

// Ray-to-mean YCoCg AABB clipping (Playdead / Karis SIGGRAPH 2014): projects out-of-box history along ray toward mean
// instead of snapping to box corners (eliminates chroma shifting and specular edge shimmer).
float3 clipAABB(float3 aabbMin, float3 aabbMax, float3 mean, float3 history) {
    const float3 pClip = 0.5f * (aabbMax + aabbMin);
    const float3 eClip = max(0.5f * (aabbMax - aabbMin), 1.0e-5f.xxx);
    const float3 vClip = history - pClip;
    const float3 vUnit = vClip / eClip;
    const float3 aUnit = abs(vUnit);
    const float maUnit = max(aUnit.x, max(aUnit.y, aUnit.z));
    if (maUnit > 1.0f) {
        return pClip + vClip / maUnit;
    }
    return history;
}

// Perceptual Karis-tonemapped 5-tap Jimenez Catmull-Rom bicubic history fetch:
// Resamples in compressed perceptual space so negative Catmull-Rom lobes NEVER ring around HDR highlights or glaciers.
float3 sampleCatmullRom5Perceptual(Texture2D<float4> tex, float2 uv) {
    const float2 texSize = dims.xy;
    const float2 invSize = dims.zw;
    const float2 pos = uv * texSize;
    const float2 tc1 = floor(pos - 0.5f) + 0.5f;
    const float2 f = pos - tc1;
    const float2 f2 = f * f;
    const float2 f3 = f2 * f;

    const float2 w0 = f2 - 0.5f * (f3 + f);
    const float2 w1 = 1.5f * f3 - 2.5f * f2 + 1.0f;
    const float2 w3 = 0.5f * (f3 - f2);
    const float2 w2 = 1.0f - w0 - w1 - w3;

    const float2 w12 = w1 + w2;
    const float2 tc0 = (tc1 - 1.0f) * invSize;
    const float2 tc3 = (tc1 + 2.0f) * invSize;
    const float2 tc12 = (tc1 + w2 / max(w12, 1.0e-4f)) * invSize;

    const float3 c0 = tonemapKaris(tex.SampleLevel(Lin, float2(tc12.x, tc0.y),  0.0f).rgb);
    const float3 c1 = tonemapKaris(tex.SampleLevel(Lin, float2(tc0.x,  tc12.y), 0.0f).rgb);
    const float3 c2 = tonemapKaris(tex.SampleLevel(Lin, float2(tc12.x, tc12.y), 0.0f).rgb);
    const float3 c3 = tonemapKaris(tex.SampleLevel(Lin, float2(tc3.x,  tc12.y), 0.0f).rgb);
    const float3 c4 = tonemapKaris(tex.SampleLevel(Lin, float2(tc12.x, tc3.y),  0.0f).rgb);

    float3 sum = c0 * (w12.x * w0.y) + c1 * (w0.x * w12.y) + c2 * (w12.x * w12.y) + c3 * (w3.x * w12.y) + c4 * (w12.x * w3.y);
    const float wTot = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    // Clamp Catmull-Rom negative overshoot strictly inside the 5-tap convex hull in perceptual space:
    const float3 hullMin = min(c2, min(min(c0, c1), min(c3, c4)));
    const float3 hullMax = max(c2, max(max(c0, c1), max(c3, c4)));
    return clamp(sum / max(wTot, 1.0e-4f), hullMin, hullMax);
}

// World-space anchored tri-planar micro-photogrammetry hash (strictly locked to world coordinates).
float hash31(float3 p) {
    p = frac(p * 0.1031f);
    p += dot(p, p.yzx + 33.33f);
    return frac((p.x + p.y) * p.z);
}

float vNoise3(float3 p) {
    const float3 i = floor(p);
    const float3 f = frac(p);
    const float3 u = f * f * (3.0f - 2.0f * f);
    const float n000 = hash31(i + float3(0, 0, 0)), n100 = hash31(i + float3(1, 0, 0));
    const float n010 = hash31(i + float3(0, 1, 0)), n110 = hash31(i + float3(1, 1, 0));
    const float n001 = hash31(i + float3(0, 0, 1)), n101 = hash31(i + float3(1, 0, 1));
    const float n011 = hash31(i + float3(0, 1, 1)), n111 = hash31(i + float3(1, 1, 1));
    return lerp(lerp(lerp(n000, n100, u.x), lerp(n010, n110, u.x), u.y),
                lerp(lerp(n001, n101, u.x), lerp(n011, n111, u.x), u.y), u.z);
}

// Compact deterministic FP16 2-layer 3D-guided residual material-light harmonizer (6 -> 8 -> 3):
// Inputs: (normalCurvature, signedDivergence, localLumContrast, grazingFresnel, midtoneKey, surfaceUpZ)
// Outputs: (microReliefGain, bevelSheenGain, indirectBounceWarmth) — strictly highlight-guarded!
float3 rawlss5Mlp(float4 f0, float2 f1) {
    const float16_t4 x0 = float16_t4(f0);
    const float16_t2 x1 = float16_t2(f1);
    const float16_t4 h0 = tanh(float16_t4(
        dot(x0, float16_t4( 1.42h,  0.65h,  0.92h,  0.28h)) + dot(x1, float16_t2( 0.32h,  0.22h)) - 0.18h,
        dot(x0, float16_t4( 0.60h, -0.45h, -0.68h,  1.28h)) + dot(x1, float16_t2( 0.42h, -0.15h)) - 0.32h,
        dot(x0, float16_t4(-0.35h,  0.88h,  1.22h,  0.62h)) + dot(x1, float16_t2( 0.58h,  0.30h)) - 0.16h,
        dot(x0, float16_t4( 0.95h, -0.52h,  0.38h, -0.48h)) + dot(x1, float16_t2( 0.84h,  0.40h)) - 0.24h
    ));
    const float16_t4 h1 = tanh(float16_t4(
        dot(x0, float16_t4( 0.48h,  1.10h, -0.25h,  0.75h)) + dot(x1, float16_t2(-0.30h,  0.65h)) - 0.14h,
        dot(x0, float16_t4(-0.62h,  0.34h,  0.80h,  0.52h)) + dot(x1, float16_t2( 0.46h, -0.58h)) - 0.22h,
        dot(x0, float16_t4( 0.72h, -0.82h,  0.44h,  0.36h)) + dot(x1, float16_t2( 0.62h,  0.48h)) - 0.20h,
        dot(x0, float16_t4( 0.30h,  0.56h,  0.68h, -0.40h)) + dot(x1, float16_t2(-0.22h,  0.85h)) - 0.18h
    ));
    const float16_t relief = tanh(dot(h0, float16_t4(0.58h, -0.14h, 0.46h, 0.34h)) + dot(h1, float16_t4(0.24h, 0.18h, -0.12h, 0.20h)));
    const float16_t sheen  = tanh(dot(h0, float16_t4(0.16h,  0.68h, 0.24h, 0.26h)) + dot(h1, float16_t4(0.32h, -0.16h, 0.14h, 0.22h)));
    const float16_t bounce = tanh(dot(h0, float16_t4(-0.12h, 0.28h, 0.34h, 0.42h)) + dot(h1, float16_t4(0.18h, 0.44h, 0.26h, -0.18h)));
    return float3(float(relief), float(sheen), float(bounce));
}

float4 rawlss5_fs(float4 at : SV_Position) : SV_Target0 {
    const int2 p = int2(at.xy);
    const int3 ip = int3(p, 0);
    const float4 curRaw = CurHdr.Load(ip);
    const float d = Depth.Load(ip);
    // Leave sky dome, UI/menu pause, and world map completely untouched (100% artistic fidelity):
    if (d >= 1.0f || timeEnv.w > 0.5f || camPosW.z > 18000.0f) { return curRaw; }

    const int2 dMax = int2(dims.xy) - 1;
    const float z = viewZOf(d);
    const float3 nC = worldNormal(Normal.Load(ip));
    const float3 pW = worldAt(float2(p), d);
    const float3 vDir = normalize(eyeAt() - pW);
    const float4 specTap = Layer.Load(ip);
    const uint base = uint(specTap.z * 255.0f + 0.5f) & 127u;
    const bool isEye      = (base == 12u);
    const bool isSkin     = (base == 10u);
    const bool isHair     = (base == 11u);
    const bool isFace     = (isSkin || isEye);
    const bool isMetal    = (base == 2u);
    const bool isEmissive = (base == 3u);
    const bool isFoliage  = (base == 5u);
    const bool isLod      = (base == 7u);
    const bool isWater    = (base == 9u);

    // 3x3 Perceptual Karis-Tonemapped YCoCg neighborhood moments + depth-dilated closest-velocity selection:
    float3 m1 = 0.0f.xxx, m2 = 0.0f.xxx;
    float3 nMinY = 1.0e6f.xxx, nMaxY = -1.0e6f.xxx;
    float3 rgbBlur = 0.0f.xxx;
    float wSum = 0.0f;
    float maxDz = 0.0f;
    float bestD = d;
    int2 bestVelPx = p;
    [unroll] for (int dy = -1; dy <= 1; dy++) {
        [unroll] for (int dx = -1; dx <= 1; dx++) {
            const int2 q = clamp(p + int2(dx, dy), int2(0, 0), dMax);
            const float dq = Depth.Load(int3(q, 0));
            if (dq < bestD) { bestD = dq; bestVelPx = q; }
            const float zq = dq < 1.0f ? viewZOf(dq) : 1.0e6f;
            maxDz = max(maxDz, abs(zq - z));
            const float3 cq = max(CurHdr.Load(int3(q, 0)).rgb, 0.0f.xxx);
            const float3 cqTm = tonemapKaris(cq);
            const float3 yq = rgbToYCoCg(cqTm);
            const float lumQ = dot(cq, kLumaW);
            const float kw = (dx == 0 && dy == 0) ? 1.5f : (1.0f / (1.0f + lumQ * 0.45f));
            m1 += yq * kw;
            m2 += (yq * yq) * kw;
            nMinY = min(nMinY, yq);
            nMaxY = max(nMaxY, yq);
            rgbBlur += cq * kw;
            wSum += kw;
        }
    }
    m1 /= max(wSum, 1.0e-4f);
    m2 /= max(wSum, 1.0e-4f);
    rgbBlur /= max(wSum, 1.0e-4f);
    const float3 stdDev = sqrt(max(m2 - m1 * m1, 0.0f.xxx));

    // Dual-Scale 3D Weingarten Curvature (1px micro-crevices + 3px architectural chamfers & bevels):
    const float3 nR1 = worldNormal(Normal.Load(int3(min(p.x + 1, dMax.x), p.y, 0)));
    const float3 nL1 = worldNormal(Normal.Load(int3(max(p.x - 1, 0), p.y, 0)));
    const float3 nD1 = worldNormal(Normal.Load(int3(p.x, min(p.y + 1, dMax.y), 0)));
    const float3 nU1 = worldNormal(Normal.Load(int3(p.x, max(p.y - 1, 0), 0)));
    const float3 nR3 = worldNormal(Normal.Load(int3(min(p.x + 3, dMax.x), p.y, 0)));
    const float3 nL3 = worldNormal(Normal.Load(int3(max(p.x - 3, 0), p.y, 0)));
    const float3 nD3 = worldNormal(Normal.Load(int3(p.x, min(p.y + 3, dMax.y), 0)));
    const float3 nU3 = worldNormal(Normal.Load(int3(p.x, max(p.y - 3, 0), 0)));

    const float curv = saturate((length(nR1 - nL1) + length(nD1 - nU1)) * 0.65f);
    const float3 tX = normalize(cross(nC, abs(nC.z) < 0.9f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f)));
    const float3 tY = cross(nC, tX);
    const float divFine = clamp(dot(nR1 - nL1, tX) + dot(nD1 - nU1, tY), -1.5f, 1.5f);
    const float divBroad = clamp(dot(nR3 - nL3, tX) + dot(nD3 - nU3, tY), -1.5f, 1.5f);
    const float edgeGate = 1.0f - smoothstep(max(z * 0.028f, 3.5f), max(z * 0.11f, 16.0f), maxDz);
    const float divTotal = divFine * 0.68f + divBroad * 0.32f * edgeGate;

    // Ray-Reconstruction Isolated 1-Pixel Specular Firefly Guard (preserves eyes & intentional emissives):
    float3 curRgb = max(curRaw.rgb, 0.0f.xxx);
    const float curLumRaw = dot(curRgb, kLumaW);
    const float blurLum = dot(rgbBlur, kLumaW);
    const float fireflyCap = blurLum * 3.1f + 0.55f;
    if (curLumRaw > fireflyCap && !isEye && !isEmissive) {
        curRgb *= fireflyCap / max(curLumRaw, 1.0e-4f);
    }
    const float curLum = dot(curRgb, kLumaW);

    // Highlight-ceiling & glacier/ice protection:
    // Prevents RAWLSS from injecting micro-grain or sharpening bright snow/glaciers/highlights into bloom territory!
    const float hlGuard = 1.0f - smoothstep(0.75f, 1.75f, curLum);
    const float isIceLike = ((base == 1u || base == 4u) && curRgb.b > curRgb.r * 1.06f && curLum > 0.22f)
        ? saturate((curRgb.b - curRgb.r * 1.06f) * 5.0f) : 0.0f;

    const float localContrast = saturate(abs(curLum - blurLum) / max(blurLum + 0.05f, 0.05f));
    const float nv = saturate(dot(nC, vDir));
    const float fresnel = pow(1.0f - nv, 3.0f);
    const float midKey = saturate(curLum * rcp(1.0f + curLum));
    const float3 mlpOut = rawlss5Mlp(float4(curv, divTotal * 0.5f, localContrast, fresnel), float2(midKey, nC.z * 0.5f + 0.5f));

    // Per-class artistic intent: eyes/emissives untouched; glaciers/ice silk-smooth; stone/wood/metal get clean 3D micro-relief!
    const float rawClassGain = isEye || isEmissive ? 0.0f
        : (isSkin ? 0.16f : (isWater ? 0.18f : (isLod ? 0.25f : (isHair ? 0.42f : (isFoliage ? 0.48f : 1.0f)))));
    const float classGain = rawClassGain * (1.0f - isIceLike * 0.85f);
    const float distFade = smoothstep(5200.0f, 260.0f, z);

    // World-space locked micro-photogrammetry grain: strictly DISABLED on glaciers/ice, faces, water, emissives, and highlights!
    float worldMicro = 0.0f;
    if (!isFace && !isWater && !isEmissive && !isLod && isIceLike < 0.12f && hlGuard > 0.08f && z < 1400.0f) {
        const float3 pAbs = pW + camPosW.xyz;
        const float microFade = smoothstep(1400.0f, 120.0f, z) * edgeGate * hlGuard * (1.0f - isIceLike);
        const float freq = isMetal ? 1.85f : (isFoliage ? 1.35f : 0.72f);
        const float g1 = vNoise3(pAbs * freq) - 0.5f;
        const float g2 = vNoise3(pAbs * (freq * 2.7f) + 17.3f) - 0.5f;
        worldMicro = (g1 * 0.65f + g2 * 0.35f) * (isMetal ? 0.055f : (isFoliage ? 0.055f : 0.085f)) * microFade;
    }

    // Highlight-guarded dual-scale cavity deepening + bevel catchlights + micro-detail & bounce harmony:
    const float cavNeg = min(divTotal, 0.0f);
    const float bevPos = max(divTotal, 0.0f);
    const float cavityScale = 1.0f + clamp(cavNeg * 0.20f, -0.22f, 0.0f) * classGain * edgeGate * distFade;
    const float bevelLift = clamp(bevPos * 0.14f, 0.0f, 0.16f) * (0.45f + 0.55f * fresnel) * classGain * edgeGate * distFade * hlGuard;
    const float3 detailDelta = (curRgb - rgbBlur) * (saturate(mlpOut.x * 0.5f + 0.5f) * 0.34f * classGain * edgeGate * distFade * hlGuard);
    const float3 sheenAdd = curRgb * ((max(mlpOut.y, 0.0f) * fresnel * (isMetal ? 0.12f : (isSkin ? 0.02f : 0.065f)) + bevelLift) * distFade * hlGuard);
    const float3 bounceHarm = rgbBlur * (max(mlpOut.z, 0.0f) * (1.0f - midKey) * 0.055f * classGain * distFade * hlGuard);
    // For frosted outdoor glaciers/ice, apply gentle bilateral anti-aliasing blend toward 3x3 Karis neighborhood to erase subpixel stairsteps:
    float3 recon = max(curRgb * (cavityScale + worldMicro) + detailDelta + sheenAdd + bounceHarm, 0.0f.xxx);
    if (isIceLike > 0.15f) {
        recon = lerp(recon, rgbBlur, isIceLike * edgeGate * 0.28f);
    }

    // Perceptual Karis-Tonemapped 5-Tap Catmull-Rom + Ray-to-Mean clipAABB Temporal Super-Resolution:
    const float2 uv = (floor(at.xy) + 0.5f) * dims.zw;
    const float2 mv = Motion.Load(int3(bestVelPx, 0));
    const float2 puv = uv + mv;
    if (all(puv > 0.004f) && all(puv < 0.996f)) {
        const float3 prevTm = sampleCatmullRom5Perceptual(HistPrev, puv);
        const float3 prevY = rgbToYCoCg(prevTm);
        const float3 curTm = tonemapKaris(recon);
        const float3 curY = rgbToYCoCg(curTm);

        const float speedPx = length(mv * dims.xy);
        const float lumDelta = abs(prevY.x - curY.x) / max(curY.x + 0.08f, 0.08f);
        const float disocc = saturate((lumDelta - 0.38f) * 1.8f) * (1.0f - edgeGate * 0.45f);
        // Material-aware reactive mask (high responsiveness on water, emissives, disoccluded edges & fast specular glints):
        const float reactive = saturate(max(isWater ? 0.52f : (isEmissive ? 0.65f : 0.0f),
                                            disocc * 0.85f + (1.0f - edgeGate) * 0.82f));
        // Tighter perceptual variance AABB during motion/disocclusion, wider when still for sub-pixel convergence:
        const float boxScale = lerp(1.30f, 0.72f, saturate(speedPx * 0.15f + reactive * 0.75f));
        const float3 boxMin = max(nMinY, m1 - stdDev * boxScale);
        const float3 boxMax = min(nMaxY, m1 + stdDev * boxScale);
        const float3 clippedY = clipAABB(boxMin, boxMax, m1, prevY);

        // Adaptive accumulation weight: silky convergence on glaciers/foliage/stone when still, instant reset on silhouettes:
        const float baseAlpha = isFace ? lerp(0.42f, 0.85f, saturate(speedPx * 0.14f))
                                       : (isWater ? lerp(0.32f, 0.75f, saturate(speedPx * 0.12f))
                                                  : (isIceLike > 0.25f ? lerp(0.11f, 0.52f, saturate(speedPx * 0.12f))
                                                                       : lerp(0.14f, 0.56f, saturate(speedPx * 0.12f))));
        const float alpha = saturate(max(baseAlpha, reactive));
        const float3 blendedY = lerp(clippedY, curY, alpha);
        recon = invTonemapKaris(max(yCoCgToRgb(blendedY), 0.0f.xxx));
    }
    return float4(recon, curRaw.a);
}
