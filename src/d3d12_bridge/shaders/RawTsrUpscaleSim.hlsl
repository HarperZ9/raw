// RawTsrUpscaleSim.hlsl: Clean-Room SM 6.6 / 6.10 GPU Compute Temporal Super-Resolution (RAW-TSR)
// & RCAS Contrast-Adaptive Sharpening Upscaler.
//
// Features:
//   1. cs_tsr_resolve ([numthreads(8, 8, 1)]):
//      - Halton (2,3) sub-pixel un-jittering with distance-weighted 3x3 Lanczos-Gaussian reconstruction
//      - Closest-depth (3x3 Standard-Z) dilated velocity lookup for crisp moving silhouettes
//      - 5-tap Catmull-Rom bicubic history reprojection (eliminates bilinear temporal blur)
//      - Karis-luma compressed YCoCg variance-clipping AABB anti-ghosting (gamma = 1.15)
//      - Off-screen / fast-motion / depth disocclusion rejection
//      - Reactive-mask pass-through (1.0 -> immediate current-frame spark/fire preservation)
//   2. cs_tsr_rcas_sharpen ([numthreads(8, 8, 1)]):
//      - Clean-room 5-tap Contrast-Adaptive Sharpening in Reinhard perceptual space with strict
//        cross-neighborhood non-ringing clamp & NaN/Inf sanitization.
//
// Original clean-room implementation, MIT License.

cbuffer TsrUpscaleCB : register(b0)
{
    uint4  gDims;      // x = inW, y = inH, z = outW, w = outH
    float4 gInvDims;   // x = 1/inW, y = 1/inH, z = 1/outW, w = 1/outH
    float4 gJitterZ;   // x = jitterPx.x, y = jitterPx.y, z = nearZ, w = farZ
    float4 gTsrParams; // x = historyWeight, y = varianceGamma, z = rcasSharpness, w = flags (bit0: reset, bit1: hasVel, bit2: hasReactive)
};

Texture2D<float4>   InputColor   : register(t0);
Texture2D<float>    InputDepth   : register(t1);
Texture2D<float2>   VelocityTex  : register(t2);
Texture2D<float>    ReactiveMask : register(t3);
Texture2D<float4>   HistoryTex   : register(t4); // Previous history in Pass 1; ResolvedOut in Pass 2

RWTexture2D<float4> OutputTex    : register(u0); // ResolvedOut in Pass 1; SharpenedOut in Pass 2

SamplerState gLinearClamp : register(s0);

static const float kSkyDepthThreshold = 0.99998f;

float3 Sanitize3(float3 v)
{
    return float3(
        isfinite(v.x) ? clamp(v.x, 0.0f, 65000.0f) : 0.0f,
        isfinite(v.y) ? clamp(v.y, 0.0f, 65000.0f) : 0.0f,
        isfinite(v.z) ? clamp(v.z, 0.0f, 65000.0f) : 0.0f);
}

float Luma709(float3 c)
{
    return dot(c, float3(0.2126f, 0.7152f, 0.0722f));
}

// Karis reversible tonemap compression so HDR spikes do not distort temporal variance or blends
float3 ToneCompress(float3 c)
{
    return c / (1.0f + max(Luma709(c), 0.0f));
}

float3 ToneExpand(float3 c)
{
    return c / max(1.0f - max(Luma709(c), 0.0f), 1.0e-4f);
}

float3 RgbToYCoCg(float3 rgb)
{
    const float y  =  0.25f * rgb.r + 0.50f * rgb.g + 0.25f * rgb.b;
    const float co =  0.50f * rgb.r                 - 0.50f * rgb.b;
    const float cg = -0.25f * rgb.r + 0.50f * rgb.g - 0.25f * rgb.b;
    return float3(y, co, cg);
}

float3 YCoCgToRgb(float3 ycocg)
{
    const float y  = ycocg.x;
    const float co = ycocg.y;
    const float cg = ycocg.z;
    return max(float3(y + co - cg, y + cg, y - co - cg), 0.0f);
}

// Ray-box clip from history point toward current-frame mean inside [boxMin, boxMax]
float3 ClipToAabb(float3 boxMin, float3 boxMax, float3 center, float3 hist)
{
    const float3 pClip = 0.5f * (boxMax + boxMin);
    const float3 eClip = 0.5f * (boxMax - boxMin) + 1.0e-5f;
    const float3 vClip = hist - pClip;
    const float3 vUnit = vClip / eClip;
    const float3 aUnit = abs(vUnit);
    const float  maUnit = max(aUnit.x, max(aUnit.y, aUnit.z));
    if (maUnit > 1.0f) {
        return pClip + vClip / maUnit;
    }
    return hist;
}

// 5-Tap Catmull-Rom bicubic filter on HistoryTex (outW x outH)
float4 SampleHistoryCatmullRom5(float2 uv, float2 texSize, float2 invTexSize)
{
    const float2 samplePos = uv * texSize;
    const float2 tc1       = floor(samplePos - 0.5f) + 0.5f;
    const float2 f         = samplePos - tc1;
    const float2 f2        = f * f;
    const float2 f3        = f2 * f;

    const float2 w0  = f2 - 0.5f * (f3 + f);
    const float2 w1  = 1.5f * f3 - 2.5f * f2 + 1.0f;
    const float2 w3  = 0.5f * (f3 - f2);
    const float2 w2  = 1.0f - w0 - w1 - w3;

    const float2 w12      = w1 + w2;
    const float2 offset12 = w2 / max(w12, float2(1.0e-5f, 1.0e-5f));

    const float2 tc0  = (tc1 - 1.0f) * invTexSize;
    const float2 tc3  = (tc1 + 2.0f) * invTexSize;
    const float2 tc12 = (tc1 + offset12) * invTexSize;

    const float4 s0 = HistoryTex.SampleLevel(gLinearClamp, clamp(float2(tc12.x, tc0.y),  invTexSize * 0.5f, 1.0f - invTexSize * 0.5f), 0.0f);
    const float4 s1 = HistoryTex.SampleLevel(gLinearClamp, clamp(float2(tc0.x,  tc12.y), invTexSize * 0.5f, 1.0f - invTexSize * 0.5f), 0.0f);
    const float4 s2 = HistoryTex.SampleLevel(gLinearClamp, clamp(float2(tc12.x, tc12.y), invTexSize * 0.5f, 1.0f - invTexSize * 0.5f), 0.0f);
    const float4 s3 = HistoryTex.SampleLevel(gLinearClamp, clamp(float2(tc3.x,  tc12.y), invTexSize * 0.5f, 1.0f - invTexSize * 0.5f), 0.0f);
    const float4 s4 = HistoryTex.SampleLevel(gLinearClamp, clamp(float2(tc12.x, tc3.y),  invTexSize * 0.5f, 1.0f - invTexSize * 0.5f), 0.0f);

    const float cw0 = w12.x * w0.y;
    const float cw1 = w0.x  * w12.y;
    const float cw2 = w12.x * w12.y;
    const float cw3 = w3.x  * w12.y;
    const float cw4 = w12.x * w3.y;
    const float wSum = cw0 + cw1 + cw2 + cw3 + cw4;

    const float4 res = (s0 * cw0 + s1 * cw1 + s2 * cw2 + s3 * cw3 + s4 * cw4) / max(wSum, 1.0e-5f);
    return float4(Sanitize3(res.rgb), saturate(isfinite(res.a) ? res.a : 0.0f));
}

// ============================================================================
// Pass 1: Temporal Super-Resolution Resolve (Halton Un-Jitter + Catmull-Rom + YCoCg Variance Clip)
// ============================================================================
[numthreads(8, 8, 1)]
void cs_tsr_resolve(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= gDims.z || dtid.y >= gDims.w) return;

    const uint flags       = (uint)gTsrParams.w;
    const bool resetHist   = ((flags & 1u) != 0u);
    const bool hasVel      = ((flags & 2u) != 0u);
    const bool hasReactive = ((flags & 4u) != 0u);

    const float2 inDims  = float2(gDims.xy);
    const float2 outDims = float2(gDims.zw);
    const float2 outUv   = (float2(dtid.xy) + 0.5f) * gInvDims.zw;

    // Map high-res un-jittered coordinate into jittered low-res input pixel space:
    // If low-res texel (lx, ly) sampled the scene at (lx + 0.5 + jitterPx) / inDims,
    // then un-jittered scene position (outUv * inDims) corresponds to input pos (outUv * inDims - jitterPx).
    const float2 unjitLowPos = outUv * inDims - gJitterZ.xy;
    const int2   centerLowPx = clamp(int2(floor(unjitLowPos)), int2(0, 0), int2(gDims.xy) - 1);

    // 3x3 low-res neighborhood: sub-pixel un-jittered reconstruction, YCoCg variance moments,
    // closest-depth velocity dilation (Standard-Z: smallest Z is closest), and max reactive mask.
    float3 curAccumRgb = float3(0.0f, 0.0f, 0.0f);
    float  curAccumW   = 0.0f;
    float  minDistSq   = 1.0e6f;

    float3 m1 = float3(0.0f, 0.0f, 0.0f);
    float3 m2 = float3(0.0f, 0.0f, 0.0f);
    float3 boxHardMin = float3( 1.0e6f,  1.0e6f,  1.0e6f);
    float3 boxHardMax = float3(-1.0e6f, -1.0e6f, -1.0e6f);

    float  closestZ     = 2.0f;
    int2   closestLowPx = centerLowPx;
    float  maxReactive  = 0.0f;
    float  zMin3x3      = 2.0f;
    float  zMax3x3      = 0.0f;

    [unroll]
    for (int dy = -1; dy <= 1; ++dy) {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx) {
            const int2 tapPx = clamp(centerLowPx + int2(dx, dy), int2(0, 0), int2(gDims.xy) - 1);
            const float3 rgb = Sanitize3(InputColor.Load(int3(tapPx, 0)).rgb);
            const float3 ycc = RgbToYCoCg(ToneCompress(rgb));

            m1 += ycc;
            m2 += ycc * ycc;
            boxHardMin = min(boxHardMin, ycc);
            boxHardMax = max(boxHardMax, ycc);

            const float2 tapCenter = float2(tapPx) + 0.5f;
            const float2 d         = tapCenter - unjitLowPos;
            const float  d2        = dot(d, d);
            minDistSq = min(minDistSq, d2);

            // Sharp sub-pixel Gaussian reconstruction kernel in Karis-compressed space
            const float w = exp(-5.0f * d2);
            curAccumRgb += ToneCompress(rgb) * w;
            curAccumW   += w;

            const float rawZ = InputDepth.Load(int3(tapPx, 0));
            const float z    = isfinite(rawZ) ? clamp(rawZ, 0.0f, 1.0f) : 1.0f;
            zMin3x3 = min(zMin3x3, z);
            zMax3x3 = max(zMax3x3, z);
            if (z < closestZ) {
                closestZ     = z;
                closestLowPx = tapPx;
            }

            if (hasReactive && abs(dx) <= 1 && abs(dy) <= 1) {
                const float rVal = ReactiveMask.Load(int3(tapPx, 0));
                if (isfinite(rVal) && d2 <= 1.25f) {
                    maxReactive = max(maxReactive, saturate(rVal));
                }
            }
        }
    }

    const float3 curCompRgb = curAccumRgb / max(curAccumW, 1.0e-5f);
    const float3 curYCoCg   = RgbToYCoCg(curCompRgb);
    const float3 curHdrRgb  = ToneExpand(curCompRgb);

    // Closest-depth dilated screen-space velocity (prevUv = outUv - vel)
    float2 velUv = float2(0.0f, 0.0f);
    if (hasVel) {
        const float2 rawV = VelocityTex.Load(int3(closestLowPx, 0));
        if (all(isfinite(rawV))) {
            velUv = rawV;
        }
    }
    const float2 prevUv    = outUv - velUv;
    const bool   offscreen = (prevUv.x < 0.0f) || (prevUv.x > 1.0f) || (prevUv.y < 0.0f) || (prevUv.y > 1.0f);

    if (resetHist || offscreen) {
        OutputTex[dtid.xy] = float4(Sanitize3(curHdrRgb), 0.15f);
        return;
    }

    // Wave 72: Disocclusion & Specular MV Guard (falls back off 5-tap negative-lobe Catmull-Rom to clean bilinear
    // across high depth-edge silhouettes or reactive sparks/speculars so zero dark outline halos bleed on motion).
    const float velOutPx     = length(velUv * outDims);
    const float depthEdge    = saturate((zMax3x3 - zMin3x3) * 40.0f);
    const bool  useBilinTsr  = (depthEdge > 0.35f) || (maxReactive > 0.50f);
    const float4 histRawTap  = HistoryTex.SampleLevel(gLinearClamp, clamp(prevUv, gInvDims.zw * 0.5f, 1.0f - gInvDims.zw * 0.5f), 0.0f);
    const float4 histSample  = useBilinTsr
        ? float4(Sanitize3(histRawTap.rgb), saturate(isfinite(histRawTap.a) ? histRawTap.a : 0.0f))
        : SampleHistoryCatmullRom5(prevUv, outDims, gInvDims.zw);
    const float3 histYCoCg   = RgbToYCoCg(ToneCompress(histSample.rgb));

    // YCoCg variance-clipping anti-ghosting box (gamma = 1.15 default)
    m1 *= (1.0f / 9.0f);
    m2 *= (1.0f / 9.0f);
    const float3 sigma = sqrt(max(m2 - m1 * m1, float3(0.0f, 0.0f, 0.0f)));
    const float meanLumaHdr  = Luma709(ToneExpand(YCoCgToRgb(m1)));
    const float specSpike    = saturate((Luma709(curHdrRgb) - meanLumaHdr * 1.25f) * 1.65f) * saturate(velOutPx * 0.25f);
    maxReactive              = max(maxReactive, specSpike * 0.65f);

    // Tighten variance box under fast motion/depth edges for zero disocclusion ghosting;
    // preserve thin sub-pixel lines inside [boxHardMin, boxHardMax] when completely static.
    const float staticFactor  = (1.0f - saturate(velOutPx * 2.0f)) * (1.0f - depthEdge);
    const float motionScale   = lerp(lerp(1.0f, 0.72f, saturate(velOutPx / 12.0f)), 1.50f, staticFactor);
    const float gamma         = max(gTsrParams.y, 0.25f) * motionScale;
    const float3 boxMin       = max(m1 - gamma * sigma, boxHardMin);
    const float3 boxMax       = min(m1 + gamma * sigma, boxHardMax);

    const float3 clippedHistYcc = ClipToAabb(boxMin, boxMax, m1, histYCoCg);

    // Measure how far raw history deviated from the variance box (disocclusion / ghost detector)
    const float clipDist = length(histYCoCg - clippedHistYcc);
    const float sigmaL   = max(sigma.x, 0.02f);
    const float disocc   = saturate((clipDist / (sigmaL * 2.5f)) - 0.15f);

    // Sub-pixel hit-gated TSR accumulation:
    // Frames whose Halton jitter lands directly on this output pixel (minDistSq ~ 0) inject high-res
    // detail strongly; frames whose jitter lands far away preserve crisp accumulated history instead
    // of overwriting it with a blurry low-res neighborhood average!
    const float subpixelHit = exp(-12.0f * minDistSq);
    const float baseAlpha   = 1.0f - clamp(gTsrParams.x, 0.0f, 0.96f);
    const float histConf    = saturate(isfinite(histSample.a) ? histSample.a : 0.15f);

    // Faster initial lock-in while confidence is low; converges cleanly once all Halton phases hit
    const float hitAlpha     = saturate(subpixelHit / max(histConf * 3.0f + subpixelHit, 0.35f));
    float       curAlpha     = lerp(baseAlpha * 0.15f, max(baseAlpha * 2.0f, hitAlpha * 0.55f), subpixelHit);

    // On motion/disocclusion or reactive mask (fire/sparks), switch immediately to responsive blend
    const float motionReject = disocc * saturate(velOutPx * 0.25f + depthEdge);
    curAlpha = max(curAlpha, motionReject * 0.85f);
    curAlpha = lerp(curAlpha, 1.0f, maxReactive);

    const float  histWeight = saturate(1.0f - curAlpha);
    const float3 blendedYcc = lerp(curYCoCg, clippedHistYcc, histWeight);
    const float3 resolved   = ToneExpand(YCoCgToRgb(blendedYcc));
    const float  newConf    = saturate(histConf * (1.0f - 0.5f * motionReject) * (1.0f - maxReactive) + subpixelHit * 0.25f);

    OutputTex[dtid.xy] = float4(Sanitize3(resolved), newConf);
}

// ============================================================================
// Pass 2: Clean-Room RCAS Contrast-Adaptive Sharpening (Zero-Ringing Cross Clamp)
// ============================================================================
[numthreads(8, 8, 1)]
void cs_tsr_rcas_sharpen(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= gDims.z || dtid.y >= gDims.w) return;

    const int2 maxC = int2(gDims.zw) - 1;
    const int2 p    = int2(dtid.xy);

    const float4 center4 = HistoryTex.Load(int3(p, 0));
    const float3 eHdr    = Sanitize3(center4.rgb);
    const float  sharp   = saturate(gTsrParams.z);

    if (sharp <= 1.0e-4f) {
        OutputTex[dtid.xy] = float4(eHdr, saturate(isfinite(center4.a) ? center4.a : 1.0f));
        return;
    }

    const float3 bHdr = Sanitize3(HistoryTex.Load(int3(clamp(p + int2( 0, -1), int2(0, 0), maxC), 0)).rgb);
    const float3 dHdr = Sanitize3(HistoryTex.Load(int3(clamp(p + int2(-1,  0), int2(0, 0), maxC), 0)).rgb);
    const float3 fHdr = Sanitize3(HistoryTex.Load(int3(clamp(p + int2( 1,  0), int2(0, 0), maxC), 0)).rgb);
    const float3 hHdr = Sanitize3(HistoryTex.Load(int3(clamp(p + int2( 0,  1), int2(0, 0), maxC), 0)).rgb);

    // Work in Karis-compressed perceptual [0..1) domain so HDR brights sharpen cleanly with zero ringing
    const float3 e = ToneCompress(eHdr);
    const float3 b = ToneCompress(bHdr);
    const float3 d = ToneCompress(dHdr);
    const float3 f = ToneCompress(fHdr);
    const float3 h = ToneCompress(hHdr);

    const float lE = Luma709(e);
    const float lB = Luma709(b);
    const float lD = Luma709(d);
    const float lF = Luma709(f);
    const float lH = Luma709(h);

    const float mnL = min(lE, min(min(lB, lD), min(lF, lH)));
    const float mxL = max(lE, max(max(lB, lD), max(lF, lH)));

    // Analytic RCAS negative lobe bound keeping sharpened result strictly inside local dynamic range
    const float hitMin = -mnL / max(4.0f * mxL, 1.0e-5f);
    const float hitMax = (1.0f - mxL) / min(4.0f * mnL - 4.0f, -1.0e-5f);
    const float lobe   = clamp(max(hitMin, hitMax), -0.1875f, 0.0f) * sharp;

    float3 sharpComp = (e + lobe * (b + d + f + h)) / max(1.0f + 4.0f * lobe, 1.0e-4f);

    // Strict non-ringing guard: never overshoot beyond the 5-tap cross color envelope
    const float3 mnC = min(e, min(min(b, d), min(f, h)));
    const float3 mxC = max(e, max(max(b, d), max(f, h)));
    sharpComp = clamp(sharpComp, mnC, mxC);

    const float3 sharpHdr = ToneExpand(sharpComp);
    OutputTex[dtid.xy] = float4(Sanitize3(sharpHdr), saturate(isfinite(center4.a) ? center4.a : 1.0f));
}
