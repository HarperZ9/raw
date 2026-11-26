// Rawlss5Pass.hlsl: RAWLSS 7.0 Split-Manifold Metal — SM 6.6-6.10 Wave-Quad & 16-Wide Long-Vector Ray Reconstructor.
// Wave 40 Zero-Ghosting & Anti-Halo Temporal Reconstruction:
//   1) Velocity-adaptive 3x3 Karis-weighted YCoCg variance clipping (gamma 1.25 static -> 0.85 fast motion/divergence)
//   2) Full depth + normal + motion-magnitude + 3x3 motion-vector divergence disocclusion reset (zero ghostly trails
//      on moving NPCs, swinging weapons, and foreground silhouettes; Karis anti-flicker preserved on foliage & snow!)
//   3) Split-Manifold Albedo Demodulation + 12->16->4 SM 6.9 Long-Vector Neural Ray Reconstructor
//   4) Restrained highlight-guarded cavity/detail harmony (zero dark-edge GI glow bleed)
// Original work, MIT.
#include "LightingCommon.hlsli"
#include "RawTensorMath.hlsli"
#include "RawWorldDynamics.hlsli"

Texture2D<float4> CurHdr    : register(t0);
Texture2D<float>  Depth     : register(t1);
Texture2D<float4> Normal    : register(t2);
Texture2D<float2> Motion    : register(t3);
Texture2D<float4> Layer     : register(t4);
Texture2D<float4> HistPrev  : register(t5);
Texture2D<float4> AlbedoTex : register(t6);
Texture2D<float4> GiBounce  : register(t7);
SamplerState      Lin       : register(s0);

static const float3 kLumaW = float3(0.2126f, 0.7152f, 0.0722f);

// Finite NaN/Inf sanitizer for HDR RGB taps (guarantees zero poison propagation across temporal history):
float3 sanitizeFiniteRgb(float3 c) {
    return (any(isnan(c)) || any(isinf(c))) ? 0.0f.xxx : clamp(c, 0.0f.xxx, 64.0f.xxx);
}

float3 tonemapKaris(float3 c) {
    const float3 s = sanitizeFiniteRgb(c);
    const float l = max(dot(s, kLumaW), 0.0f);
    return s * rcp(1.0f + l);
}

float3 invTonemapKaris(float3 c) {
    const float3 s = sanitizeFiniteRgb(c);
    const float l = clamp(dot(s, kLumaW), 0.0f, 0.992f);
    return s * rcp(max(1.0f - l, 0.008f));
}

float3 rgbToYCoCg(float3 c) {
    return float3(0.25f * c.r + 0.5f * c.g + 0.25f * c.b,
                  0.5f * c.r - 0.5f * c.b,
                 -0.25f * c.r + 0.5f * c.g - 0.25f * c.b);
}

float3 yCoCgToRgb(float3 y) {
    return float3(y.x + y.y - y.z, y.x + y.z, y.x - y.y - y.z);
}

// Ray-to-mean YCoCg AABB + Statistical Ellipsoid Hybrid Clipping (Playdead / Karis + Wave 52 Variance Ellipsoid):
// Tighter than AABB along diagonal luma/chroma corners, eliminating ghosting halos on moving silhouettes & embers!
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

float3 clipYCoCgEllipse(float3 nMinY, float3 nMaxY, float3 mean, float3 stdDev, float gammaClip, float3 history) {
    const float3 rad = max(stdDev * max(gammaClip, 0.25f), 1.0e-4f.xxx);
    const float3 boxMin = max(nMinY, mean - rad);
    const float3 boxMax = min(nMaxY, mean + rad);
    float3 h = clipAABB(boxMin, boxMax, mean, history);
    // Project onto exact L2 statistical ellipsoid around neighborhood mean:
    const float3 dNorm = (h - mean) / rad;
    const float dLen2 = dot(dNorm, dNorm);
    if (dLen2 > 1.0f) {
        h = mean + (h - mean) * rsqrt(dLen2);
    }
    return clamp(h, nMinY, nMaxY);
}

// Perceptual Karis-tonemapped 5-tap Jimenez Catmull-Rom sharpening bicubic history fetch:
// Resamples in compressed perceptual space with convex-hull clamp so negative Catmull-Rom lobes sharpen static
// detail cleanly without ever ringing around HDR highlights, water glints, or glaciers.
float3 sampleCatmullRom5Perceptual(Texture2D<float4> tex, float2 uv, float sharpBias) {
    const float2 texSize = max(dims.xy, float2(1.0f, 1.0f));
    const float2 invSize = 1.0f / texSize;
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
    const float2 tc0 = clamp((tc1 - 1.0f) * invSize, 0.0005f.xx, 0.9995f.xx);
    const float2 tc3 = clamp((tc1 + 2.0f) * invSize, 0.0005f.xx, 0.9995f.xx);
    const float2 tc12 = clamp((tc1 + w2 / max(w12, 1.0e-4f)) * invSize, 0.0005f.xx, 0.9995f.xx);

    const float3 c0 = tonemapKaris(tex.SampleLevel(Lin, float2(tc12.x, tc0.y),  0.0f).rgb);
    const float3 c1 = tonemapKaris(tex.SampleLevel(Lin, float2(tc0.x,  tc12.y), 0.0f).rgb);
    const float3 c2 = tonemapKaris(tex.SampleLevel(Lin, float2(tc12.x, tc12.y), 0.0f).rgb);
    const float3 c3 = tonemapKaris(tex.SampleLevel(Lin, float2(tc3.x,  tc12.y), 0.0f).rgb);
    const float3 c4 = tonemapKaris(tex.SampleLevel(Lin, float2(tc12.x, tc3.y),  0.0f).rgb);

    const float3 sum = c0 * (w12.x * w0.y) + c1 * (w0.x * w12.y) + c2 * (w12.x * w12.y) + c3 * (w3.x * w12.y) + c4 * (w12.x * w3.y);
    const float wTot = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    const float3 cr = sum / max(wTot, 1.0e-4f);
    // Subtle center-weighted unsharp lift on static detail, strictly clamped inside the 5-tap convex hull:
    const float3 crossMean = 0.25f * (c0 + c1 + c3 + c4);
    const float3 crSharp = cr + (c2 - crossMean) * clamp(sharpBias, 0.0f, 0.24f);
    const float3 hullMin = min(c2, min(min(c0, c1), min(c3, c4)));
    const float3 hullMax = max(c2, max(max(c0, c1), max(c3, c4)));
    return clamp(crSharp, hullMin, hullMax);
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

// Delegates neural material-light harmonizer to RawTensorMath.hlsli (packed FP16 / SM 6.9 Long-Vector fast tanh):
float3 rawlss5Mlp(float4 f0, float2 f1) {
    return rawEvalMaterialMlp6x8x3(f0, f1);
}

float4 rawlss5_fs(float4 at : SV_Position) : SV_Target0 {
    const int2 p = int2(at.xy);
    const int3 ip = int3(p, 0);
    const float4 curRaw = CurHdr.Load(ip);
    const float d = Depth.Load(ip);
    const float safeAlpha0 = isfinite(curRaw.a) ? saturate(curRaw.a) : 1.0f;
    // Wave 65 Pure Debug Layer Contract:
    // 1) Check flags.w == 35u FIRST (even when paused timeEnv.w > 0.5f or on sky d >= 0.99998f!):
    //    outputs exact decodable signed GPU Motion Vectors + 3x3 divergence directly from Skyrim Motion texture.
    //    Verified Skyrim Motion sign convention: prevUv = curUv + mv (matches RtSkyFilter, RtShadow, RtReflFilter).
    if (flags.w == 35u) {
        if (d >= 0.99998f) { return float4(0.5f, 0.5f, 0.0f, 1.0f); }
        const int2 dMax35 = max(int2(dims.xy) - 1, int2(0, 0));
        const float2 mvC35Raw = Motion.Load(ip);
        const float2 mvC35 = (any(isnan(mvC35Raw)) || any(isinf(mvC35Raw))) ? 0.0f.xx : mvC35Raw;
        float mvDiv35 = 0.0f;
        [unroll] for (int dy35 = -1; dy35 <= 1; dy35++) {
            [unroll] for (int dx35 = -1; dx35 <= 1; dx35++) {
                if (dx35 == 0 && dy35 == 0) continue;
                const int2 q35 = clamp(p + int2(dx35, dy35), int2(0, 0), dMax35);
                const float2 mvQ35Raw = Motion.Load(int3(q35, 0));
                const float2 mvQ35 = (any(isnan(mvQ35Raw)) || any(isinf(mvQ35Raw))) ? mvC35 : mvQ35Raw;
                mvDiv35 = max(mvDiv35, length((mvQ35 - mvC35) * dims.xy));
            }
        }
        return float4(
            saturate(mvC35.x * 16.0f + 0.5f),
            saturate(mvC35.y * 16.0f + 0.5f),
            saturate(mvDiv35 * 0.25f),
            1.0f
        );
    }
    // 2) For ALL other Debug Views (1..34, 36): bit-exact passthrough (ZERO TAA smearing, cavity or detail edits!).
    if (flags.w != 0u) { return float4(sanitizeFiniteRgb(curRaw.rgb), safeAlpha0); }
    // Leave sky dome (d >= 0.99998) and paused UI/MapMenu untouched; NEVER bypass on walkable alpine mountains!
    if (d >= 0.99998f || timeEnv.w > 0.5f) { return float4(sanitizeFiniteRgb(curRaw.rgb), safeAlpha0); }

    const int2 dMax = max(int2(dims.xy) - 1, int2(0, 0));
    const float z = viewZOf(d);
    const float3 nC = worldNormal(Normal.Load(ip));
    const float3 pW = worldAt(float2(p), d);
    const float3 toEyeC = eyeAt() - pW;
    const float toEyeLen2 = dot(toEyeC, toEyeC);
    const float3 vDir = (toEyeLen2 > 1.0e-8f) ? (toEyeC * rsqrt(toEyeLen2)) : float3(0.0f, 0.0f, 1.0f);
    const float4 specTap = Layer.Load(ip);
    const uint base = uint(specTap.z * 255.0f + 0.5f) & 127u;
    const bool isEye          = (base == 12u);
    const bool isSkin         = (base == 10u);
    const bool isHair         = (base == 11u);
    const bool isSkinnedActor = (base == 14u);
    const bool isFace         = (isSkin || isEye);
    const bool isActor        = (isSkin || isHair || isEye || isSkinnedActor);
    const bool isMetal        = (base == 2u);
    const bool isEmissive     = (base == 3u);
    const bool isFoliage      = (base == 5u);
    const bool isLod          = (base == 7u);
    const bool isWater        = (base == 9u);

    const float2 mvCenterRaw = Motion.Load(ip);
    const float2 mvCenter = (any(isnan(mvCenterRaw)) || any(isinf(mvCenterRaw))) ? 0.0f.xx : mvCenterRaw;

    // 3x3 Perceptual Karis-Tonemapped YCoCg neighborhood moments + coplanar same-depth bounds + luminance/motion-divergence scan:
    float3 m1 = 0.0f.xxx, m2 = 0.0f.xxx;
    float3 nMinY = 1.0e6f.xxx, nMaxY = -1.0e6f.xxx;
    float3 copMinY = 1.0e6f.xxx, copMaxY = -1.0e6f.xxx, copSumY = 0.0f.xxx;
    float copW = 0.0f;
    float3 rgbBlur = 0.0f.xxx;
    float wSum = 0.0f;
    float lumM1 = 0.0f, lumM2 = 0.0f, lumMax3 = 0.0f;
    float maxDz = 0.0f;
    float minDotN = 1.0f;
    float mvDivPx = 0.0f;
    float bestD = d;
    int2 bestVelPx = p;
    const float zCopTol = max(z * 0.035f, 3.0f);
    [unroll] for (int dy = -1; dy <= 1; dy++) {
        [unroll] for (int dx = -1; dx <= 1; dx++) {
            const int2 q = clamp(p + int2(dx, dy), int2(0, 0), dMax);
            const int3 iq = int3(q, 0);
            const float dq = Depth.Load(iq);
            if (dq < bestD) { bestD = dq; bestVelPx = q; }
            const float zq = dq < 0.99998f ? viewZOf(dq) : 1.0e6f;
            const float dzq = abs(zq - z);
            maxDz = max(maxDz, dzq);
            if (dx != 0 || dy != 0) {
                const float3 nq = worldNormal(Normal.Load(iq));
                minDotN = min(minDotN, dot(nC, nq));
                const float2 mvqRaw = Motion.Load(iq);
                const float2 mvq = (any(isnan(mvqRaw)) || any(isinf(mvqRaw))) ? mvCenter : mvqRaw;
                mvDivPx = max(mvDivPx, length((mvq - mvCenter) * dims.xy));
            }
            const float3 cq = sanitizeFiniteRgb(CurHdr.Load(iq).rgb);
            const float3 cqTm = tonemapKaris(cq);
            const float3 yq = rgbToYCoCg(cqTm);
            const float lumQ = dot(cq, kLumaW);
            lumM1 += lumQ;
            lumM2 += lumQ * lumQ;
            lumMax3 = max(lumMax3, lumQ);
            // Karis luminance anti-flicker weighting preserves crisp fine foliage & snow sparkles!
            const float kw = (dx == 0 && dy == 0) ? 1.55f : (1.0f / (1.0f + lumQ * 0.55f));
            m1 += yq * kw;
            m2 += (yq * yq) * kw;
            nMinY = min(nMinY, yq);
            nMaxY = max(nMaxY, yq);
            if (dzq <= zCopTol) {
                copMinY = min(copMinY, yq);
                copMaxY = max(copMaxY, yq);
                copSumY += yq * kw;
                copW += kw;
            }
            rgbBlur += cq * kw;
            wSum += kw;
        }
    }
    m1 /= max(wSum, 1.0e-4f);
    m2 /= max(wSum, 1.0e-4f);
    rgbBlur /= max(wSum, 1.0e-4f);
    const float3 copMeanY = (copW > 1.0e-4f) ? (copSumY / copW) : m1;
    if (copW <= 1.0e-4f) { copMinY = m1; copMaxY = m1; }
    const float3 stdDev = sqrt(max(m2 - m1 * m1, 0.0f.xxx));
    const float lumMean3 = lumM1 * (1.0f / 9.0f);
    const float lumStd3 = sqrt(max(lumM2 * (1.0f / 9.0f) - lumMean3 * lumMean3, 0.0f));

    // Metal-Level Dual-Scale 3D Weingarten Curvature:
    // Fine 1px differential runs via hardware QuadReadAcrossX/Y register shuffles (ZERO extra Normal L1 loads!),
    // paired with 3px architectural chamfer/bevel taps:
    const float3 tX = normalize(cross(nC, abs(nC.z) < 0.9f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f)));
    const float3 tY = cross(nC, tX);
    float curv = 0.0f, divFine = 0.0f;
    rawQuadSurfaceDiff(nC, tX, tY, p, curv, divFine);
    const float3 nR3 = worldNormal(Normal.Load(int3(min(p.x + 3, dMax.x), p.y, 0)));
    const float3 nL3 = worldNormal(Normal.Load(int3(max(p.x - 3, 0), p.y, 0)));
    const float3 nD3 = worldNormal(Normal.Load(int3(p.x, min(p.y + 3, dMax.y), 0)));
    const float3 nU3 = worldNormal(Normal.Load(int3(p.x, max(p.y - 3, 0), 0)));
    const float divBroad = clamp(dot(nR3 - nL3, tX) + dot(nD3 - nU3, tY), -1.5f, 1.5f);
    const float edgeGate = 1.0f - smoothstep(max(z * 0.022f, 2.5f), max(z * 0.095f, 12.0f), maxDz);
    const float divTotal = divFine * 0.68f + divBroad * 0.32f * edgeGate;

    // Hypergraph 4-Party Quad-Cut Firefly vs Geometric Glint Separator:
    //   - Radiance splits (1 vs 3) while normal & depth are uniform (4 vs 0) -> isolated RT firefly -> clamp cleanly!
    //   - Radiance AND normal/depth BOTH split (1 vs 3) -> true geometric bevel glint or snow diamond -> preserve 100%!
    float3 curRgb = sanitizeFiniteRgb(curRaw.rgb);
    const float curLumRaw = dot(curRgb, kLumaW);
    const float qLumX = QuadReadAcrossX(curLumRaw);
    const float qLumY = QuadReadAcrossY(curLumRaw);
    const float qLumD = QuadReadAcrossDiagonal(curLumRaw);
    const float3 qNX = QuadReadAcrossX(nC);
    const float3 qNY = QuadReadAcrossY(nC);
    const float qZX = QuadReadAcrossX(z);
    const float qZY = QuadReadAcrossY(z);
    const float quadOtherMax = max(qLumX, max(qLumY, qLumD));
    const float quadGeoSplit = saturate((1.0f - min(dot(nC, qNX), dot(nC, qNY))) * 8.0f + max(abs(z - qZX), abs(z - qZY)) / max(z * 0.03f, 2.0f));
    const bool isHyperFirefly = (curLumRaw > max(quadOtherMax * 2.85f, 0.45f)) && (quadGeoSplit < 0.22f);
    const float blurLum = dot(rgbBlur, kLumaW);
    const float fireflyCap = isHyperFirefly ? (quadOtherMax * 1.65f + 0.18f) : (blurLum * 3.1f + 0.55f);
    if (curLumRaw > fireflyCap && !isEye && !isEmissive) {
        curRgb *= fireflyCap / max(curLumRaw, 1.0e-4f);
    }
    const float curLum = dot(curRgb, kLumaW);

    // Highlight-ceiling & glacier/ice protection:
    const float hlGuard = 1.0f - smoothstep(0.72f, 1.65f, curLum);
    const float isIceLike = ((base == 1u || base == 2u || base == 4u) && curRgb.b > curRgb.r * 1.05f && curLum > 0.18f)
        ? saturate((curRgb.b - curRgb.r * 1.05f) * 5.0f) : 0.0f;

    const float localContrast = saturate(abs(curLum - blurLum) / max(blurLum + 0.05f, 0.05f));
    const float nv = saturate(dot(nC, vDir));
    const float fresnel = pow(1.0f - nv, 3.0f);
    const float midKey = saturate(curLum * rcp(1.0f + curLum));

    // Split-Manifold Albedo & World-GI Decomposition (zero-fetch 2x2 quad stats):
    const float3 albedoRaw = max(AlbedoTex.Load(ip).rgb, 0.0f.xxx);
    const float albedoPeak = max(albedoRaw.r, max(albedoRaw.g, albedoRaw.b));
    const bool useDemod = (!isEye && !isEmissive && !isWater && !isLod && albedoPeak > 0.045f);
    const float3 albedoBase = max(albedoRaw, 0.06f.xxx);
    const float3 irradCur = useDemod ? (curRgb / albedoBase) : curRgb;
    float3 quadIrradMean = irradCur, quadAlbMean = albedoRaw;
    float quadIrradStd = 0.0f, quadAlbStd = 0.0f;
    rawQuadVec3Stats(irradCur, quadIrradMean, quadIrradStd);
    rawQuadVec3Stats(albedoRaw, quadAlbMean, quadAlbStd);
    const float3 giTap = max(GiBounce.Load(ip).rgb, 0.0f.xxx);
    const float giLum = dot(giTap, kLumaW);

    const float2 mvBestRaw = Motion.Load(int3(bestVelPx, 0));
    const float2 mvBest = (any(isnan(mvBestRaw)) || any(isinf(mvBestRaw))) ? mvCenter : mvBestRaw;
    // Wave 63 Anti-Halo Silhouette Velocity Guard:
    // If bestVelPx belongs to a closer foreground object across a depth silhouette, do NOT smear
    // the foreground actor's velocity vector onto the background pixel (that pulled dark foreground
    // actor body pixels into HistPrev and painted a dark cel-shading outline around moving NPCs!).
    const float bestZ = bestD < 0.99998f ? viewZOf(bestD) : z;
    const bool fgSilhouetteNeighbor = (bestD < d) && ((z - bestZ) > max(z * 0.025f, 2.5f));
    const float2 mv = fgSilhouetteNeighbor ? mvCenter : mvBest;
    const float speedPx = max(length(mv * dims.xy), length(mvCenter * dims.xy));

    // RAWLSS 7.0 12->16->4 Split-Manifold Neural Ray Reconstructor (native 16-wide SM 6.9 Long Vectors on DXIL 1.9!)
    float4 qf0, qf1;
    rawQuantizeE8(
        float4(curv, divTotal * 0.5f, localContrast, fresnel),
        float4(midKey, nC.z * 0.5f + 0.5f, saturate(quadIrradStd * 0.65f), saturate(giLum * 0.8f)),
        16.0f, qf0, qf1
    );
    const float4 mlpOut = rawEvalSplitManifoldNet12x16x4(
        lerp(float4(curv, divTotal * 0.5f, localContrast, fresnel), qf0, 0.35f),
        lerp(float4(midKey, nC.z * 0.5f + 0.5f, saturate(quadIrradStd * 0.65f), saturate(giLum * 0.8f)), qf1, 0.35f),
        float4(saturate(specTap.x), saturate(specTap.y), saturate(speedPx * 0.12f), 1.0f - edgeGate)
    );

    const float rawClassGain = isEye || isEmissive ? 0.0f
        : (isSkin ? 0.14f : (isSkinnedActor ? 0.24f : (isWater ? 0.18f : (isLod ? 0.25f : (isHair ? 0.32f : (isFoliage ? 0.30f : 1.0f))))));
    const float classGain = rawClassGain * (1.0f - isIceLike * 0.22f);
    const float distFade = 1.0f - smoothstep(260.0f, 5200.0f, z);
    // Wave 64: Protect dark tree bark and shadowed stone seams from extra cavity/micro darkening into #050505!
    const float darkShadowGuard = smoothstep(0.025f, 0.14f, curLum);

    // World-space locked micro-photogrammetry grain: strictly DISABLED on actors, faces, foliage, water, emissives, and blown highlights!
    float worldMicro = 0.0f;
    if (!isActor && !isFoliage && !isWater && !isEmissive && !isLod && hlGuard > 0.08f && z < 1400.0f) {
        const float3 pAbs = pW + camPosW.xyz;
        const float microFade = (1.0f - smoothstep(120.0f, 1400.0f, z)) * edgeGate * hlGuard * (1.0f - isIceLike * 0.45f);
        const float freq = isMetal ? 1.85f : 0.72f;
        const float g1 = vNoise3(pAbs * freq) - 0.5f;
        const float g2 = vNoise3(pAbs * (freq * 2.7f) + 17.3f) - 0.5f;
        const float rawMicro = (g1 * 0.65f + g2 * 0.35f) * (isMetal ? 0.055f : 0.085f) * microFade;
        worldMicro = (rawMicro < 0.0f) ? (rawMicro * darkShadowGuard) : rawMicro;
    }

    // Highlight-guarded dual-scale cavity deepening + bevel catchlights (Wave 63 + Wave 64: strictly zero cavity
    // darkening on ALL actors/creatures AND foliage pine needles, guarded off silhouette edges & dark bark/stone seams!):
    const float cavNeg = min(divTotal, 0.0f);
    const float bevPos = max(divTotal, 0.0f);
    const float cavSilGuard = (isActor || isFoliage) ? 0.0f : (smoothstep(0.84f, 0.99f, edgeGate) * darkShadowGuard);
    const float cavityScale = 1.0f + clamp(cavNeg * 0.16f, -0.16f, 0.0f) * classGain * edgeGate * cavSilGuard * distFade;
    const float bevelLift = clamp(bevPos * 0.13f, 0.0f, 0.14f) * (0.45f + 0.55f * fresnel) * classGain * edgeGate * distFade * hlGuard;
    // Clamp negative unsharp-mask undershoot along high-contrast pine needle & stone silhouette rims (zero dark halo ringing!):
    const float3 rawDetailDelta = (curRgb - rgbBlur) * (saturate(mlpOut.x * 0.5f + 0.5f) * 0.32f * classGain * edgeGate * distFade * hlGuard);
    const float negRingCap = (isFoliage ? 0.04f : 0.10f) * smoothstep(0.70f, 0.98f, edgeGate) * darkShadowGuard;
    const float3 detailDelta = max(rawDetailDelta, -curRgb * negRingCap);
    const float3 sheenAdd = curRgb * ((max(mlpOut.y, 0.0f) * fresnel * (isMetal ? 0.10f : (isSkin ? 0.02f : 0.055f)) + bevelLift) * distFade * hlGuard);
    const float3 bounceHarm = rgbBlur * (max(mlpOut.z, 0.0f) * (1.0f - midKey) * 0.020f * classGain * edgeGate * edgeGate * distFade * hlGuard);
    float3 recon = max(curRgb * (cavityScale + worldMicro) + detailDelta + sheenAdd + bounceHarm, 0.0f.xxx);

    // Wave 52 + Wave 63 Razor-Crisp Reactive-Mask YCoCg Ellipsoid Temporal Reconstruction:
    // Detects intense fire/ember spikes, specular water glints, falling snow/rain streaks & fast-moving foreground
    // silhouettes (3x3 luminance variance + motion-vector divergence) so dynamic FX & moving actors NEVER ghost!
    const float2 uv = (floor(at.xy) + 0.5f) * dims.zw;
    const float2 mvSafe = (any(isnan(mv)) || any(isinf(mv))) ? 0.0f.xx : mv;
    // Wave 74: World-space coherent gust field + foliage phase-locked temporal MV compensation.
    // Aligns untracked wind-sway foliage history along the analytical gust phase derivative (clamped <= 1.25 px)
    // and drives gust-front reactivity so pine needles stay sharp under wind gusts & camera sweeps.
    float4 foliageGustW = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float2 gustMvComp = 0.0f.xx;
    if (isFoliage && z < 2800.0f) {
        foliageGustW = rawEvalCoherentWindGust4(pW + camPosW.xyz, cloudWind, weather, camPosW.w);
        const float gustFadeF = smoothstep(2800.0f, 180.0f, z) * (1.0f - smoothstep(4.0f, 10.0f, speedPx));
        gustMvComp = clamp(foliageGustW.xy * (dims.zw * 0.65f * gustFadeF), -dims.zw * 1.25f, dims.zw * 1.25f);
        if (any(isnan(gustMvComp)) || any(isinf(gustMvComp))) { gustMvComp = 0.0f.xx; }
    }
    const float2 puv = uv + mvSafe + gustMvComp;
    if (all(puv > 0.004f) && all(puv < 0.996f)) {
        // High-contrast reactive mask (0 on calm static scenery -> 1 on dynamic embers, water glints, precip & sword swings):
        const float depthDisocc  = smoothstep(max(z * 0.022f, 2.2f), max(z * 0.085f, 10.0f), maxDz);
        const float normalDisocc = smoothstep(0.18f, 0.52f, 1.0f - minDotN) * saturate(speedPx * 0.45f + mvDivPx * 0.85f);
        const float mvDivDisocc  = smoothstep(0.38f, 1.45f, mvDivPx);
        const float speedRamp    = smoothstep(0.65f, 6.20f, speedPx);
        const float relLumVar    = lumStd3 / max(lumMean3 + 0.08f, 0.08f);
        const float fireEmberReact = (curRgb.r > curRgb.b * 1.65f && curLum > 0.72f)
            ? smoothstep(0.72f, 2.20f, curLum) * 0.94f : 0.0f;
        const float waterGlintReact = isWater
            ? saturate(0.64f + smoothstep(0.18f, 0.80f, localContrast) * 0.34f) : 0.0f;
        const float precipReact = ((weather.z > 0.06f || weather.w > 0.06f) && curLum > blurLum * 1.24f && curLum > 0.18f)
            ? saturate((curLum - blurLum) * 1.85f) * 0.88f : 0.0f;
        const float fgSwingReact = smoothstep(0.45f, 3.20f, speedPx) * (1.0f - smoothstep(45.0f, 260.0f, z)) * 0.96f;
        const float silRimReact = fgSilhouetteNeighbor ? saturate(0.55f + mvDivDisocc * 0.45f + speedRamp * 0.35f) : 0.0f;
        const float varMotionReact = smoothstep(0.20f, 0.72f, relLumVar) * saturate(speedPx * 0.42f + mvDivPx * 0.78f);
        const float spikeContrastReact = smoothstep(0.65f, 2.20f, (lumMax3 - lumMean3) / max(lumMean3 + 0.15f, 0.15f))
            * saturate(mvDivDisocc + speedRamp * 0.65f);
        // Wave 71 + Wave 74: Foliage Micro-Motion & Phase-Locked Gust-Front Reactivity (untracked vertex wind sway).
        // Prevents temporal needle smearing under wind gusts & camera sweeps while preserving Deja 10-step anti-drift.
        const float windMagF = saturate(length(cloudWind.xy) * 0.018f + weather.w * 0.30f + weather.z * 0.25f);
        const float gustFrontF = isFoliage ? saturate(abs(foliageGustW.w) * foliageGustW.z * 0.85f) : 0.0f;
        const float foliageMicroReact = isFoliage
            ? saturate(smoothstep(0.14f, 0.58f, relLumVar + quadIrradStd * 0.35f) * (0.28f + 0.42f * windMagF + 0.22f * gustFrontF + 0.30f * speedRamp)
                       + gustFrontF * 0.14f * smoothstep(0.08f, 0.35f, relLumVar)
                       + (fgSilhouetteNeighbor ? 0.45f : 0.0f))
            : 0.0f;
        // Wave 72: Specular Motion-Vector Guard (parallax reflection highlights on metal/water/wet stone move at
        // reflection velocity rather than surface MV velocity; boosts reactivity & disables bicubic ringing on motion).
        const float specPeakW = saturate((curLum - blurLum * 1.18f) * 2.2f) * saturate(fresnel * 1.45f + (isMetal || isWater ? 0.50f : specTap.x * 0.65f));
        const float specMvReact = (!isActor && !isFoliage) ? saturate(specPeakW * smoothstep(0.12f, 0.65f, speedRamp) * 0.82f) : 0.0f;
        const float reactiveMask = saturate(max(
            max(max(fireEmberReact, waterGlintReact), max(precipReact, max(fgSwingReact, silRimReact))),
            max(max(varMotionReact, max(spikeContrastReact, max(foliageMicroReact, specMvReact))), max(mvDivDisocc * 0.98f, isEmissive ? 0.90f : 0.0f))));

        // Wave 71 + Wave 72: Disocclusion-safe history fetch (falls back from 5-tap bicubic Catmull-Rom to clean
        // bilinear across foreground silhouettes, high motion divergence, specular parallax, and alpha-tested foliage
        // so negative bicubic lobes never bleed dark outline halos across newly disoccluded edges!).
        const float histSharp = (1.0f - max(speedRamp, reactiveMask)) * 0.14f * edgeGate;
        const bool useBilinearHist = fgSilhouetteNeighbor || (depthDisocc > 0.28f) || (mvDivDisocc > 0.28f) || isFoliage || (specMvReact > 0.35f);
        const float3 prevTm = useBilinearHist
            ? tonemapKaris(HistPrev.SampleLevel(Lin, puv, 0.0f).rgb)
            : sampleCatmullRom5Perceptual(HistPrev, puv, histSharp);
        const float3 prevY = rgbToYCoCg(prevTm);
        const float3 curTm = tonemapKaris(recon);
        const float3 curY = rgbToYCoCg(curTm);

        const float lumDelta  = abs(prevY.x - curY.x) / max(curY.x + 0.05f, 0.05f);
        const float chrDelta  = length(prevY.yz - curY.yz) / max(curY.x + 0.07f, 0.07f);
        const float trapSep   = rawTrappingSeparator(curY.x, prevY.x, stdDev.x, speedPx, edgeGate);
        const float colDisocc = max(saturate((lumDelta - 0.18f) * 2.6f), saturate((chrDelta - 0.22f) * 2.4f));
        const float geoDisocc = max(mvDivDisocc, max(depthDisocc * 0.92f, normalDisocc * 0.85f));
        const float disocc    = saturate(max(max(colDisocc, trapSep), geoDisocc));
        // Wave 72: Trichrome-Drift Adaptive Detail Guard (suppresses chroma ringing when temporal chroma drifts faster than luma; excludes actors):
        const float triChromaDrift = (!isActor) ? saturate((chrDelta - (lumDelta * 1.25f + 0.08f)) * 3.2f) : 0.0f;
        const float3 curYStable = lerp(curY, rgbToYCoCg(tonemapKaris(max(curRgb * cavityScale, 0.0f.xxx))), triChromaDrift * 0.45f * saturate(speedRamp + mvDivDisocc));

        // Tightened YCoCg Ellipsoid + Variance Clipping (Wave 66 anti-outline silhouette guard):
        // On high motion-divergence silhouettes (mvDivPx > 1.5), clamp neighborhood variance box cleanly toward
        // coplanar local mean rather than darkest cross-depth outlier so moving silhouettes NEVER form dark rims!
        const float tightDrive = saturate(max(max(speedRamp, mvDivDisocc * 0.95f), reactiveMask));
        const float gammaClip  = lerp(1.28f, 0.62f, tightDrive);
        const float silDivW    = smoothstep(1.20f, 2.50f, mvDivPx);
        const float3 silMeanY  = lerp(m1, copMeanY, silDivW);
        const float3 silMinY   = lerp(nMinY, max(copMinY, copMeanY - stdDev * 0.45f), silDivW);
        const float3 silMaxY   = lerp(nMaxY, min(copMaxY, copMeanY + stdDev * 0.45f), silDivW);
        const float3 clippedY  = clipYCoCgEllipse(min(silMinY, curY), max(silMaxY, curY), silMeanY, stdDev, gammaClip, prevY);

        // Material-aware & reactive history flush (zero dark outline/trails on moving actors, creatures, dogs & weapons!):
        const float actorReact = isActor ? (0.52f + speedRamp * 0.44f) : 0.0f;
        const float reactiveFinal = saturate(max(reactiveMask, max(actorReact, disocc * 0.96f)));
        const float baseAlpha = isFace ? lerp(0.54f, 0.96f, speedRamp)
                                       : (isSkinnedActor ? lerp(0.50f, 0.96f, speedRamp)
                                                         : (isWater ? lerp(0.52f, 0.96f, speedRamp)
                                                                    : (isFoliage ? lerp(0.32f, 0.90f, speedRamp)
                                                                                 : (isIceLike > 0.25f ? lerp(0.24f, 0.84f, speedRamp)
                                                                                                      : lerp(0.26f, 0.88f, speedRamp)))));
        const float alpha = saturate(max(baseAlpha, reactiveFinal));
        const float3 blendedY = lerp(clippedY, curYStable, alpha);
        // Wave 73: Deja-inspired null-space 10-step chromatic/luminance anti-drift guard.
        // Clamps blended YCoCg chroma within the [cur, clipped] saturation envelope (zero bloom-bleed)
        // and projects out Jensen-inequality Karis darkening along the linear-energy null-space while
        // staying strictly bounded inside [min, max] luminance (firefly-safe & dark-bark-safe).
        const float maxChromaEnv = max(length(curYStable.yz), length(clippedY.yz));
        const float blendChromaMag = length(blendedY.yz);
        const float chromaKeep = (blendChromaMag > maxChromaEnv && blendChromaMag > 1.0e-5f) ? (maxChromaEnv / blendChromaMag) : 1.0f;
        const float3 blendedYConserv = float3(blendedY.x, blendedY.yz * chromaKeep);
        recon = sanitizeFiniteRgb(invTonemapKaris(max(yCoCgToRgb(blendedYConserv), 0.0f.xxx)));
        const float3 clippedLinRgb = sanitizeFiniteRgb(invTonemapKaris(max(yCoCgToRgb(clippedY), 0.0f.xxx)));
        const float3 curStabLinRgb = sanitizeFiniteRgb(invTonemapKaris(max(yCoCgToRgb(curYStable), 0.0f.xxx)));
        const float clippedLinL = max(dot(clippedLinRgb, float3(0.2126f, 0.7152f, 0.0722f)), 0.0f);
        const float curStabLinL = max(dot(curStabLinRgb, float3(0.2126f, 0.7152f, 0.0722f)), 0.0f);
        const float reconLinL = max(dot(recon, float3(0.2126f, 0.7152f, 0.0722f)), 0.0f);
        const float maxPairLinL = max(clippedLinL, curStabLinL);
        const float minPairLinL = min(clippedLinL, curStabLinL);
        const float antiDriftGate = smoothstep(0.015f, 0.06f, reconLinL)
                                  * (1.0f - smoothstep(4.0f, 8.0f, maxPairLinL))
                                  * (1.0f - saturate(max(disocc, reactiveFinal) * 0.85f));
        if (antiDriftGate > 1.0e-3f && reconLinL > 1.0e-4f) {
            const float targetLinL = clamp(lerp(clippedLinL, curStabLinL, alpha), minPairLinL, maxPairLinL);
            const float rawDriftCorr = clamp(targetLinL / reconLinL, 0.90f, 1.25f);
            recon = sanitizeFiniteRgb(recon * lerp(1.0f, rawDriftCorr, antiDriftGate));
        }

        // Zero-Lag Disocclusion Split-Manifold Prior + Native-Resolution Albedo Sharpness Preservation:
        // Suppresses priorRgb on moving silhouette divergence & high reactiveMask so dynamic FX stay razor-sharp!
        if (useDemod && edgeGate > 0.30f && mvDivDisocc < 0.35f && reactiveMask < 0.40f) {
            const float priorW = saturate(colDisocc * mlpOut.w * 0.26f * (1.0f - isIceLike * 0.5f) * (1.0f - mvDivDisocc * 2.5f));
            if (priorW > 0.01f) {
                const float3 priorRgb = max(quadIrradMean * albedoBase, giTap * albedoBase * 0.60f);
                recon = lerp(recon, priorRgb, priorW);
            }
            const float panBlurGuard = saturate(speedPx * 0.14f) * (1.0f - alpha) * classGain * edgeGate * hlGuard * (1.0f - isIceLike);
            if (panBlurGuard > 0.01f && quadAlbStd > 0.012f) {
                const float3 albRatio = clamp(albedoBase / max(quadAlbMean, 0.06f.xxx), 0.86f.xxx, 1.16f.xxx);
                recon = lerp(recon, recon * albRatio, panBlurGuard * 0.42f);
            }
        }
    }
    const float safeAlpha = isfinite(curRaw.a) ? saturate(curRaw.a) : 1.0f;
    return float4(sanitizeFiniteRgb(recon), safeAlpha);
}
