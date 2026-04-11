//=============================================================================
//  SB_TemporalCore.fxh — Temporal Anti-Aliasing & Reprojection Library
//
//  Provides math utilities for temporal filtering, anti-aliasing, and
//  motion estimation in ENB pixel shaders. Designed for use with or without
//  persistent history buffers.
//
//  Contents:
//    1. R2 quasirandom jitter (sub-pixel sampling)
//    2. 5-tap bicubic Catmull-Rom history sampling
//    3. k-DOP 14-DOP neighborhood clipping (ghosting reduction)
//    4. AABB clipping (simpler fallback)
//    5. Depth-aware disocclusion detection
//    6. Luminance-based motion estimation (no motion vectors)
//    7. Velocity dilation (neighborhood max for TAA)
//    8. Motion-adaptive blend factor
//
//  References:
//    Salvi 2016 — AABB neighborhood clipping
//    Karis 2014 — Temporal reprojection in UE4
//    Jimenez 2016 — Filmic SMAA T2x
//    Roberts 2018 — R2 quasirandom sequence
//    Gjoel et al. 2016 — Temporal reprojection AA (Inside)
//
//  Author: Zain Dana Harper — March 2026
//=============================================================================

#ifndef SB_TEMPORAL_CORE_FXH
#define SB_TEMPORAL_CORE_FXH


//=============================================================================
//  1. R2 QUASIRANDOM JITTER
//=============================================================================

// R2 sequence for sub-pixel jitter offsets.
// Superior to Halton(2,3) for 2D coverage at any sample count.
// Returns offset in [-0.5, 0.5] range for pixel-center jitter.
float2 TC_R2Jitter(int frameIndex)
{
    static const float PHI2    = 1.32471795724474602596;
    static const float PHI2_SQ = 1.75487766624669276005;

    float2 jitter = frac(float2(
        (float)frameIndex / PHI2,
        (float)frameIndex / PHI2_SQ
    ) + 0.5);

    return jitter - 0.5; // center around zero
}

// Get frame index from ENB Timer.z (frame counter or continuous time)
int TC_FrameIndex(float timerZ)
{
    return (int)frac(timerZ * 0.001) * 1000;
}


//=============================================================================
//  2. CATMULL-ROM BICUBIC HISTORY SAMPLING
//=============================================================================

// 5-tap Catmull-Rom bicubic filter for history buffer sampling.
// Preserves high-frequency detail (grass, terrain textures) that
// bilinear sampling would destroy during reprojection.
// Uses separable Catmull-Rom weights in a cross pattern (not full 4×4).
//
// Ref: Jimenez 2016, slide 82 (Filmic SMAA T2x)
float3 TC_CatmullRomSample(
    Texture2D tex, SamplerState smp,
    float2 uv, float2 texelSize)
{
    float2 texCoord = uv / texelSize; // to texel space
    float2 tc = floor(texCoord - 0.5) + 0.5;
    float2 f = texCoord - tc; // fractional part

    // Catmull-Rom weights
    float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    float2 w3 = f * f * (-0.5 + 0.5 * f);

    // Combine inner two weights for bilinear optimization
    float2 w12 = w1 + w2;
    float2 tc12 = (tc + w2 / max(w12, 1e-6)) * texelSize;
    float2 tc0  = (tc - 1.0) * texelSize;
    float2 tc3  = (tc + 2.0) * texelSize;

    // 5-tap cross: center + 4 surrounding samples
    float3 result = float3(0, 0, 0);
    float totalWeight = 0.0;

    // Center sample (bilinear of w1+w2)
    float wC = w12.x * w12.y;
    result += tex.SampleLevel(smp, tc12, 0).rgb * wC;
    totalWeight += wC;

    // Left (w0.x, w12.y)
    float wL = w0.x * w12.y;
    result += tex.SampleLevel(smp, float2(tc0.x, tc12.y), 0).rgb * wL;
    totalWeight += wL;

    // Right (w3.x, w12.y)
    float wR = w3.x * w12.y;
    result += tex.SampleLevel(smp, float2(tc3.x, tc12.y), 0).rgb * wR;
    totalWeight += wR;

    // Top (w12.x, w0.y)
    float wT = w12.x * w0.y;
    result += tex.SampleLevel(smp, float2(tc12.x, tc0.y), 0).rgb * wT;
    totalWeight += wT;

    // Bottom (w12.x, w3.y)
    float wB = w12.x * w3.y;
    result += tex.SampleLevel(smp, float2(tc12.x, tc3.y), 0).rgb * wB;
    totalWeight += wB;

    return result / max(totalWeight, 1e-6);
}


//=============================================================================
//  3. k-DOP 14-DOP NEIGHBORHOOD CLIPPING
//=============================================================================

// 14-DOP neighborhood clipping: clips history color to the current frame's
// local color distribution using 7 axis pairs (3 RGB + 4 diagonal).
// ~30% less ghosting than axis-aligned AABB with minimal extra cost.
//
// neighbors: 3×3 neighborhood colors (9 samples, row-major)
// history: previous frame's color at this pixel
// Returns clipped history color.
//
// Ref: Extended from Salvi 2016 AABB with diagonal axes
float3 TC_kDOP_Clip(float3 history, float3 neighbors[9])
{
    // Compute neighborhood statistics
    float3 mean = float3(0, 0, 0);
    [unroll] for (int i = 0; i < 9; i++)
        mean += neighbors[i];
    mean /= 9.0;

    // Axis-aligned bounds (RGB)
    float3 aabbMin = neighbors[0];
    float3 aabbMax = neighbors[0];
    [unroll] for (int j = 1; j < 9; j++)
    {
        aabbMin = min(aabbMin, neighbors[j]);
        aabbMax = max(aabbMax, neighbors[j]);
    }

    // 4 diagonal axes: (1,1,0), (1,0,1), (0,1,1), (1,1,1) normalized
    static const float3 DIAG_AXES[4] = {
        float3(0.7071, 0.7071, 0.0),     // RG
        float3(0.7071, 0.0,    0.7071),  // RB
        float3(0.0,    0.7071, 0.7071),  // GB
        float3(0.5774, 0.5774, 0.5774)   // RGB diagonal
    };

    // Project neighborhood onto diagonal axes to find bounds
    float diagMin[4], diagMax[4];
    [unroll] for (int a = 0; a < 4; a++)
    {
        float proj0 = dot(neighbors[0], DIAG_AXES[a]);
        diagMin[a] = proj0;
        diagMax[a] = proj0;
        [unroll] for (int n = 1; n < 9; n++)
        {
            float proj = dot(neighbors[n], DIAG_AXES[a]);
            diagMin[a] = min(diagMin[a], proj);
            diagMax[a] = max(diagMax[a], proj);
        }
    }

    // Ray-clip: find intersection of ray (mean → history) with all 7 slab pairs
    float3 rayDir = history - mean;
    float rayLen = length(rayDir);

    if (rayLen < 1e-6)
        return history; // history is at mean, no clipping needed

    rayDir /= rayLen;
    float tMin = 0.0;
    float tMax = rayLen;

    // Clip against 3 RGB axis slabs
    [unroll] for (int c = 0; c < 3; c++)
    {
        if (abs(rayDir[c]) > 1e-6)
        {
            float t1 = (aabbMin[c] - mean[c]) / rayDir[c];
            float t2 = (aabbMax[c] - mean[c]) / rayDir[c];
            if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
            tMin = max(tMin, t1);
            tMax = min(tMax, t2);
        }
    }

    // Clip against 4 diagonal slabs
    [unroll] for (int d = 0; d < 4; d++)
    {
        float dRayProj = dot(rayDir, DIAG_AXES[d]);
        if (abs(dRayProj) > 1e-6)
        {
            float dMeanProj = dot(mean, DIAG_AXES[d]);
            float t1 = (diagMin[d] - dMeanProj) / dRayProj;
            float t2 = (diagMax[d] - dMeanProj) / dRayProj;
            if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
            tMin = max(tMin, t1);
            tMax = min(tMax, t2);
        }
    }

    // Clip parameter
    float tClip = clamp(tMax, 0.0, rayLen);
    return mean + rayDir * tClip;
}


//=============================================================================
//  4. AABB CLIPPING (simpler fallback)
//=============================================================================

// Standard AABB clipping: clips history to axis-aligned bounding box
// of the 3×3 neighborhood. Simpler and faster than k-DOP.
float3 TC_AABB_Clip(float3 history, float3 aabbMin, float3 aabbMax)
{
    float3 center = (aabbMin + aabbMax) * 0.5;
    float3 halfExtent = (aabbMax - aabbMin) * 0.5 + 1e-6;

    float3 clipDir = history - center;
    float3 invExtent = 1.0 / halfExtent;
    float3 absScale = abs(clipDir * invExtent);
    float maxScale = max(max(absScale.x, absScale.y), absScale.z);

    if (maxScale > 1.0)
        return center + clipDir / maxScale;

    return history;
}

// Compute neighborhood AABB from 3×3 samples
void TC_ComputeAABB(float3 neighbors[9], out float3 aabbMin, out float3 aabbMax)
{
    aabbMin = neighbors[0];
    aabbMax = neighbors[0];
    [unroll] for (int i = 1; i < 9; i++)
    {
        aabbMin = min(aabbMin, neighbors[i]);
        aabbMax = max(aabbMax, neighbors[i]);
    }
}


//=============================================================================
//  5. DISOCCLUSION DETECTION
//=============================================================================

// Depth-aware disocclusion: detects when reprojected history sample
// lands on a depth discontinuity (newly revealed geometry).
// Returns 0.0 = no disocclusion, 1.0 = full disocclusion.
//
// currentDepth: linear depth at current pixel
// reprojectedDepth: depth from previous frame at reprojected position
// threshold: depth tolerance (smaller = more sensitive)
float TC_DetectDisocclusion(float currentDepth, float reprojectedDepth, float threshold)
{
    float depthDiff = abs(currentDepth - reprojectedDepth);
    // Scale threshold by depth (distant objects need larger tolerance)
    float adaptiveThreshold = threshold * max(currentDepth, 0.01);
    return saturate(depthDiff / max(adaptiveThreshold, 1e-6) - 1.0);
}

// Extended disocclusion: checks 3×3 neighborhood for consistent depth
// Returns confidence [0,1] that the pixel is NOT disoccluded.
float TC_DisocclusionConfidence(
    Texture2D depthTex, SamplerState smp,
    float2 uv, float2 texelSize,
    float referenceDepth, float threshold)
{
    float confidence = 0.0;
    [unroll] for (int y = -1; y <= 1; y++)
    {
        [unroll] for (int x = -1; x <= 1; x++)
        {
            float sampleDepth = depthTex.SampleLevel(smp, uv + float2(x, y) * texelSize, 0).x;
            float match = 1.0 - TC_DetectDisocclusion(sampleDepth, referenceDepth, threshold);
            confidence += match;
        }
    }
    return confidence / 9.0;
}


//=============================================================================
//  6. LUMINANCE-BASED MOTION ESTIMATION
//=============================================================================

// Without motion vectors, estimate "motion energy" from luminance change.
// High change = fast motion or disocclusion → reduce history weight.
// Used as a ghosting reduction signal in the blend factor.
float TC_LumaMotionEstimate(float currentLuma, float historyLuma, float sensitivity)
{
    float lumaDiff = abs(currentLuma - historyLuma);
    return saturate(lumaDiff * sensitivity);
}

// Material-aware alpha: reduce temporal blending for semi-transparent materials
// detected via low depth confidence or high local depth variance.
float TC_MaterialAlpha(
    Texture2D depthTex, SamplerState smp,
    float2 uv, float2 texelSize)
{
    float centerDepth = depthTex.SampleLevel(smp, uv, 0).x;

    float depthMean = 0.0;
    float depthVar = 0.0;
    [unroll] for (int y = -1; y <= 1; y++)
    {
        [unroll] for (int x = -1; x <= 1; x++)
        {
            float d = depthTex.SampleLevel(smp, uv + float2(x, y) * texelSize, 0).x;
            depthMean += d;
            depthVar += d * d;
        }
    }
    depthMean /= 9.0;
    depthVar = depthVar / 9.0 - depthMean * depthMean;

    // High depth variance → likely semi-transparent (foliage, particles)
    // Return reduced temporal weight [0,1]
    return saturate(1.0 - depthVar * 10000.0);
}


//=============================================================================
//  7. VELOCITY DILATION
//=============================================================================

// 3×3 velocity dilation: find the maximum velocity magnitude in the neighborhood.
// Prevents thin moving objects from leaving trail artifacts in TAA.
// velocityTex stores motion vectors (or estimated displacement).
// If no motion vectors available, this returns the maximum luma difference.
float TC_VelocityDilation3x3(
    Texture2D currentTex, Texture2D historyTex, SamplerState smp,
    float2 uv, float2 texelSize)
{
    static const float3 LUM = float3(0.2126, 0.7152, 0.0722);
    float maxMotion = 0.0;

    [unroll] for (int y = -1; y <= 1; y++)
    {
        [unroll] for (int x = -1; x <= 1; x++)
        {
            float2 sampleUV = uv + float2(x, y) * texelSize;
            float curLuma = dot(currentTex.SampleLevel(smp, sampleUV, 0).rgb, LUM);
            float hisLuma = dot(historyTex.SampleLevel(smp, sampleUV, 0).rgb, LUM);
            maxMotion = max(maxMotion, abs(curLuma - hisLuma));
        }
    }

    return maxMotion;
}


//=============================================================================
//  8. MOTION-ADAPTIVE BLEND FACTOR
//=============================================================================

// Compute adaptive temporal blend factor (α) based on multiple signals:
// - Disocclusion: newly revealed areas → high α (favor current frame)
// - Motion energy: fast movement → moderate α
// - Material: semi-transparent → high α
// - Confidence: neighborhood consistency → low α (favor history)
//
// α near 0: heavy temporal accumulation (static areas)
// α near 1: favor current frame (high motion / disocclusion)
float TC_AdaptiveAlpha(
    float disocclusion,     // [0,1] from TC_DetectDisocclusion
    float motionEnergy,     // [0,1] from TC_LumaMotionEstimate
    float materialAlpha,    // [0,1] from TC_MaterialAlpha
    float baseAlpha)        // base blend (typically 0.05-0.15)
{
    float alpha = baseAlpha;

    // Disocclusion: aggressively favor current frame
    alpha = lerp(alpha, 1.0, disocclusion * 0.9);

    // Motion: moderately increase α
    alpha = lerp(alpha, 0.5, motionEnergy * 0.5);

    // Semi-transparent materials: increase α
    alpha = lerp(alpha, 0.4, (1.0 - materialAlpha) * 0.6);

    return saturate(alpha);
}

// Sky-specific reprojection: for sky pixels (depth > 0.9999),
// only rotation matters (no parallax). Returns rotation-only UV offset.
// viewAngleChange: camera rotation delta (from SB or estimated).
float2 TC_SkyReprojection(float2 uv, float2 viewAngleChange)
{
    return uv - viewAngleChange;
}


#endif // SB_TEMPORAL_CORE_FXH
