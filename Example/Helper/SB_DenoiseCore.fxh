//=============================================================================
//  SB_DenoiseCore.fxh — Spatial Denoising Library for ENB of the Elders
//
//  Provides edge-aware spatial filtering utilities for denoising screen-space
//  effects (AO, SSR, contact shadows, volumetric fog) without temporal history.
//
//  Contents:
//    1. Edge-stopping weight functions (depth, normal, luminance)
//    2. Joint bilateral filter (3×3 / 5×5, depth-guided)
//    3. À-trous wavelet filter (multi-level, B-spline kernel)
//    4. Variance estimation and variance-guided edge stopping
//    5. Bilateral upsample (for half-res effect compositing)
//    6. Sampling utilities (animated IGN, R2 quasirandom)
//
//  References:
//    Schied et al. 2017 — SVGF (Spatiotemporal Variance-Guided Filtering)
//    Dammertz et al. 2010 — Edge-Avoiding À-Trous Wavelet Transform
//    Tomasi & Manduchi 1998 — Bilateral Filtering
//    Roberts 2018 — R2 quasirandom sequence
//
//  Host requirements: TextureDepth, smpPoint/smpLinear, ScreenSize, Timer
//  Author: Zain Dana Harper — March 2026
//=============================================================================

#ifndef SB_DENOISE_CORE_FXH
#define SB_DENOISE_CORE_FXH


//=============================================================================
//  1. EDGE-STOPPING WEIGHT FUNCTIONS
//=============================================================================

// Depth edge-stopping: Gaussian weight based on depth difference.
// Prevents filter from blurring across depth discontinuities (silhouettes).
// sigma: controls tolerance (smaller = stricter edge preservation).
float DN_DepthWeight(float centerDepth, float sampleDepth, float sigma)
{
    float diff = abs(centerDepth - sampleDepth);
    return exp(-diff * diff / max(sigma * sigma, 1e-8));
}

// Normal edge-stopping: cosine similarity between surface normals.
// Prevents filter from blurring across normal discontinuities (creases).
// sigma: angular tolerance (1.0 = strict, 128.0 = permissive).
float DN_NormalWeight(float3 centerNormal, float3 sampleNormal, float sigma)
{
    float cosAngle = max(dot(centerNormal, sampleNormal), 0.0);
    return pow(cosAngle, sigma);
}

// Luminance edge-stopping: preserves color edges during spatial filtering.
// sigma: color tolerance (smaller = more edge-preserving).
float DN_LuminanceWeight(float3 centerColor, float3 sampleColor, float sigma)
{
    float3 diff = centerColor - sampleColor;
    float dist2 = dot(diff, diff);
    return exp(-dist2 / max(sigma * sigma, 1e-8));
}

// Combined edge-stopping weight: product of depth + luminance guides.
// Normal guide requires reconstructed normals (optional, pass weight=1.0 to skip).
float DN_CombinedWeight(float depthW, float normalW, float lumaW)
{
    return depthW * normalW * lumaW;
}


//=============================================================================
//  2. JOINT BILATERAL FILTER (depth-guided)
//=============================================================================

// 3×3 joint bilateral filter: edge-aware blur using depth as guide.
// Ideal for single-pass inline denoising of AO, contact shadows, etc.
// Returns filtered color. depthTex must be accessible at sampleUV.
float3 DN_JointBilateral3x3(
    Texture2D colorTex, Texture2D depthTex, SamplerState smp,
    float2 uv, float2 texelSize,
    float sigmaDepth, float sigmaLuma)
{
    float3 centerColor = colorTex.SampleLevel(smp, uv, 0).rgb;
    float centerDepth = depthTex.SampleLevel(smp, uv, 0).x;

    float3 accumColor = centerColor;
    float accumWeight = 1.0;

    // Spatial kernel weights for 3×3 (Gaussian sigma ≈ 0.85)
    static const float KERNEL_3x3[9] = {
        0.0625, 0.125, 0.0625,
        0.125,  0.0,   0.125,
        0.0625, 0.125, 0.0625
    };

    static const int2 OFFSETS_3x3[8] = {
        int2(-1,-1), int2(0,-1), int2(1,-1),
        int2(-1, 0),             int2(1, 0),
        int2(-1, 1), int2(0, 1), int2(1, 1)
    };

    [unroll]
    for (int i = 0; i < 8; i++)
    {
        float2 sampleUV = uv + float2(OFFSETS_3x3[i]) * texelSize;
        float3 sampleColor = colorTex.SampleLevel(smp, sampleUV, 0).rgb;
        float sampleDepth = depthTex.SampleLevel(smp, sampleUV, 0).x;

        float wDepth = DN_DepthWeight(centerDepth, sampleDepth, sigmaDepth);
        float wLuma = DN_LuminanceWeight(centerColor, sampleColor, sigmaLuma);
        // Spatial kernel: index 0-2 = top row, 3-4 = mid sides, 5-7 = bottom
        float wSpatial = KERNEL_3x3[i < 4 ? i : i + 1]; // skip center

        float w = wSpatial * wDepth * wLuma;
        accumColor += sampleColor * w;
        accumWeight += w;
    }

    return accumColor / max(accumWeight, 1e-6);
}

// 5×5 joint bilateral: wider kernel for stronger denoising.
// Uses cross pattern (13 taps) to reduce cost while maintaining quality.
float3 DN_JointBilateral5x5(
    Texture2D colorTex, Texture2D depthTex, SamplerState smp,
    float2 uv, float2 texelSize,
    float sigmaDepth, float sigmaLuma)
{
    float3 centerColor = colorTex.SampleLevel(smp, uv, 0).rgb;
    float centerDepth = depthTex.SampleLevel(smp, uv, 0).x;

    float3 accumColor = centerColor;
    float accumWeight = 1.0;

    // Diamond/cross pattern: 12 samples at distances 1 and 2
    static const int2 OFFSETS[12] = {
        // Ring 1 (distance 1)
        int2( 0,-1), int2(-1, 0), int2( 1, 0), int2( 0, 1),
        // Ring 1 diagonals
        int2(-1,-1), int2( 1,-1), int2(-1, 1), int2( 1, 1),
        // Ring 2 (distance 2, cross only)
        int2( 0,-2), int2(-2, 0), int2( 2, 0), int2( 0, 2)
    };

    static const float WEIGHTS[12] = {
        0.15, 0.15, 0.15, 0.15,    // ring 1 cross
        0.08, 0.08, 0.08, 0.08,    // ring 1 diag
        0.04, 0.04, 0.04, 0.04     // ring 2 cross
    };

    [unroll]
    for (int i = 0; i < 12; i++)
    {
        float2 sampleUV = uv + float2(OFFSETS[i]) * texelSize;
        float3 sampleColor = colorTex.SampleLevel(smp, sampleUV, 0).rgb;
        float sampleDepth = depthTex.SampleLevel(smp, sampleUV, 0).x;

        float wDepth = DN_DepthWeight(centerDepth, sampleDepth, sigmaDepth);
        float wLuma = DN_LuminanceWeight(centerColor, sampleColor, sigmaLuma);

        float w = WEIGHTS[i] * wDepth * wLuma;
        accumColor += sampleColor * w;
        accumWeight += w;
    }

    return accumColor / max(accumWeight, 1e-6);
}


//=============================================================================
//  3. À-TROUS WAVELET FILTER (multi-level edge-aware)
//=============================================================================

// Single level of à-trous wavelet transform.
// Applies 5×5 B-spline kernel at exponentially increasing dilation.
// Call with level = 0, 1, 2, ... for dilation = 1, 2, 4, ...
// Ping-pong between render targets for multi-level application.
//
// Ref: Dammertz et al. 2010 "Edge-Avoiding À-Trous Wavelet Transform"
//      Schied et al. 2017 "SVGF" — variance-guided σ extension
float3 DN_ATrousLevel(
    Texture2D colorTex, Texture2D depthTex, SamplerState smp,
    float2 uv, float2 texelSize,
    int level,
    float sigmaColor, float sigmaDepth)
{
    float3 centerColor = colorTex.SampleLevel(smp, uv, 0).rgb;
    float centerDepth = depthTex.SampleLevel(smp, uv, 0).x;

    // B-spline 5×5 kernel weights (1D: [1,4,6,4,1]/16, outer product)
    // Only sample the 5-tap cross + 4 diag for 9-tap approximation (not full 25)
    static const float H[3] = { 1.0 / 16.0, 4.0 / 16.0, 6.0 / 16.0 };

    int stepSize = 1 << level; // dilation: 1, 2, 4, 8, 16...

    float3 accumColor = centerColor * H[2] * H[2]; // center weight = 6/16 * 6/16
    float accumWeight = H[2] * H[2];

    // 5-tap cross pattern at current dilation
    static const int2 CROSS[4] = { int2(1,0), int2(-1,0), int2(0,1), int2(0,-1) };
    static const int2 DIAG[4]  = { int2(1,1), int2(-1,1), int2(1,-1), int2(-1,-1) };

    // Ring 1 (distance = stepSize): weight H[1]*H[2] = 4/16 * 6/16
    float w1 = H[1] * H[2];
    [unroll]
    for (int i = 0; i < 4; i++)
    {
        float2 sampleUV = uv + float2(CROSS[i] * stepSize) * texelSize;
        float3 sampleColor = colorTex.SampleLevel(smp, sampleUV, 0).rgb;
        float sampleDepth = depthTex.SampleLevel(smp, sampleUV, 0).x;

        float wEdge = DN_DepthWeight(centerDepth, sampleDepth, sigmaDepth)
                    * DN_LuminanceWeight(centerColor, sampleColor, sigmaColor);

        float w = w1 * wEdge;
        accumColor += sampleColor * w;
        accumWeight += w;
    }

    // Diagonal ring 1: weight H[1]*H[1] = 4/16 * 4/16
    float wD1 = H[1] * H[1];
    [unroll]
    for (int j = 0; j < 4; j++)
    {
        float2 sampleUV = uv + float2(DIAG[j] * stepSize) * texelSize;
        float3 sampleColor = colorTex.SampleLevel(smp, sampleUV, 0).rgb;
        float sampleDepth = depthTex.SampleLevel(smp, sampleUV, 0).x;

        float wEdge = DN_DepthWeight(centerDepth, sampleDepth, sigmaDepth)
                    * DN_LuminanceWeight(centerColor, sampleColor, sigmaColor);

        float w = wD1 * wEdge;
        accumColor += sampleColor * w;
        accumWeight += w;
    }

    // Ring 2 (distance = 2*stepSize): weight H[0]*H[2] = 1/16 * 6/16
    float w2 = H[0] * H[2];
    [unroll]
    for (int k = 0; k < 4; k++)
    {
        float2 sampleUV = uv + float2(CROSS[k] * stepSize * 2) * texelSize;
        float3 sampleColor = colorTex.SampleLevel(smp, sampleUV, 0).rgb;
        float sampleDepth = depthTex.SampleLevel(smp, sampleUV, 0).x;

        float wEdge = DN_DepthWeight(centerDepth, sampleDepth, sigmaDepth)
                    * DN_LuminanceWeight(centerColor, sampleColor, sigmaColor);

        float w = w2 * wEdge;
        accumColor += sampleColor * w;
        accumWeight += w;
    }

    return accumColor / max(accumWeight, 1e-6);
}


//=============================================================================
//  4. VARIANCE ESTIMATION & VARIANCE-GUIDED EDGE STOPPING
//=============================================================================

// Estimate local variance from 3×3 neighborhood.
// Returns variance of luminance — useful for SVGF-style adaptive σ.
float DN_EstimateVariance3x3(
    Texture2D colorTex, SamplerState smp,
    float2 uv, float2 texelSize)
{
    static const float3 LUM = float3(0.2126, 0.7152, 0.0722);

    float mean = 0.0;
    float meanSq = 0.0;

    [unroll]
    for (int y = -1; y <= 1; y++)
    {
        [unroll]
        for (int x = -1; x <= 1; x++)
        {
            float2 sampleUV = uv + float2(x, y) * texelSize;
            float luma = dot(colorTex.SampleLevel(smp, sampleUV, 0).rgb, LUM);
            mean += luma;
            meanSq += luma * luma;
        }
    }

    mean /= 9.0;
    meanSq /= 9.0;

    return max(meanSq - mean * mean, 0.0); // Var = E[X²] - E[X]²
}

// Variance-guided sigma scaling (SVGF).
// High variance = more noise → wider filter (larger σ).
// Low variance = confident signal → aggressive edge-stopping (smaller σ).
float DN_VarianceGuidedSigma(float variance, float sigmaMin, float sigmaMax, float threshold)
{
    float t = saturate(variance / max(threshold, 1e-6));
    return lerp(sigmaMin, sigmaMax, t);
}

// SVGF-lite: clamp history to neighborhood statistics (mean ± γ·σ).
// Simpler than k-DOP but effective for screen-space effect feedback.
float3 DN_VarianceClip(float3 history, float3 neighborMean, float neighborVariance, float gamma)
{
    float sigma = sqrt(max(neighborVariance, 1e-6));
    float3 clipMin = neighborMean - gamma * sigma;
    float3 clipMax = neighborMean + gamma * sigma;
    return clamp(history, clipMin, clipMax);
}


//=============================================================================
//  5. BILATERAL UPSAMPLE
//=============================================================================

// Bilateral upsample: reconstruct full-res from half-res effect using
// full-res depth as guide. Prevents ghosting at depth boundaries.
// Used for upsampling volumetric fog, half-res AO, etc.
//
// lowResTex: half-resolution effect texture
// depthTexFull: full-resolution depth
// texelSizeLow: texel size of low-res texture
// sigmaDepth: depth similarity threshold
float3 DN_BilateralUpsample(
    Texture2D lowResTex, Texture2D depthTexFull, SamplerState smpLin, SamplerState smpPt,
    float2 uv, float2 texelSizeLow, float sigmaDepth)
{
    float fullDepth = depthTexFull.SampleLevel(smpPt, uv, 0).x;

    float3 accumColor = float3(0, 0, 0);
    float accumWeight = 0.0;

    // Sample 4 nearest low-res texels (2×2 neighborhood)
    static const float2 OFFSETS[4] = {
        float2(-0.25, -0.25), float2( 0.25, -0.25),
        float2(-0.25,  0.25), float2( 0.25,  0.25)
    };

    [unroll]
    for (int i = 0; i < 4; i++)
    {
        float2 sampleUV = uv + OFFSETS[i] * texelSizeLow;
        float3 sampleColor = lowResTex.SampleLevel(smpLin, sampleUV, 0).rgb;
        float sampleDepth = depthTexFull.SampleLevel(smpPt, sampleUV, 0).x;

        float w = DN_DepthWeight(fullDepth, sampleDepth, sigmaDepth);
        accumColor += sampleColor * w;
        accumWeight += w;
    }

    if (accumWeight < 1e-6)
        return lowResTex.SampleLevel(smpLin, uv, 0).rgb; // fallback: bilinear

    return accumColor / accumWeight;
}


//=============================================================================
//  6. SAMPLING UTILITIES
//=============================================================================

// Animated IGN: per-frame temporal offset for stochastic sampling.
// Uses R2 golden ratio offset to decorrelate across frames.
float DN_AnimatedIGN(float2 screenPos, float frameTime)
{
    float frame = frac(frameTime * 0.017); // wrap at ~60 frames
    return frac(52.9829189 * frac(dot(screenPos + frame * 5.588238,
                                       float2(0.06711056, 0.00583715))));
}

// R2 quasirandom sequence: 2D low-discrepancy sampling.
// Better 2D coverage than Halton for jitter/sampling applications.
// Ref: Roberts 2018, "The Unreasonable Effectiveness of Quasirandom Sequences"
float2 DN_R2Sequence(int index)
{
    // Plastic constant and its square
    static const float PHI2    = 1.32471795724474602596;
    static const float PHI2_SQ = 1.75487766624669276005;

    return frac(float2((float)index / PHI2, (float)index / PHI2_SQ) + 0.5);
}

// Triangular-PDF noise: sum of two uniform → triangle distribution.
// Better than uniform for dithering (concentrates error near zero).
float DN_TriangularNoise(float2 screenPos, float offset)
{
    float n1 = frac(52.9829189 * frac(dot(screenPos, float2(0.06711056, 0.00583715))));
    float n2 = frac(52.9829189 * frac(dot(screenPos + offset, float2(0.06711056, 0.00583715))));
    return n1 + n2 - 1.0; // [-1, 1] triangular distribution
}


#endif // SB_DENOISE_CORE_FXH
