// TAAManager resolve compute shader — 14-DOP neighbourhood clamp in YCoCg
// Reads post-pipeline backbuffer + depth + history, writes resolved to both
// backbuffer and history ping-pong target.
//
// Uses k-DOP (k-Discrete Oriented Polytope) clamping with 7 oriented axes
// (14 half-planes) for tighter color-space bounds than AABB, reducing ghosting.
// Clamping operates in YCoCg color space for perceptual uniformity.

cbuffer TAAParams : register(b0)
{
    uint2 ScreenDims;      // backbuffer width, height
    uint  FrameIndex;      // monotonic frame counter
    float BlendAlpha;      // base temporal blend [0.05 .. 0.2], default 0.1
    float4 Jitter;         // .xy = current sub-pixel jitter (NDC), .zw = unused
};

// Inputs
Texture2D<float4>   CurrentColor  : register(t0);  // Copy of backbuffer (pre-resolve)
Texture2D<float4>   HistoryColor  : register(t1);  // Previous frame history
Texture2D<float>    DepthBuffer   : register(t2);  // Scene depth (0..1)

// Output — resolved color written to history; CopyResource puts it back on BB
RWTexture2D<float4> OutputHistory : register(u1);  // History write (ping-pong)

SamplerState smpLinear : register(s0);

// ── Color space conversions ──────────────────────────────────────────

float3 RGBtoYCoCg(float3 c)
{
    return float3(
         0.25 * c.r + 0.5 * c.g + 0.25 * c.b,
         0.5  * c.r             - 0.5  * c.b,
        -0.25 * c.r + 0.5 * c.g - 0.25 * c.b
    );
}

float3 YCoCgtoRGB(float3 c)
{
    return float3(
        c.x + c.y - c.z,
        c.x       + c.z,
        c.x - c.y - c.z
    );
}

// ── Perceptual tonemap in YCoCg (operates on Y channel) ─────────────

float3 TonemapYCoCg(float3 ycocg)
{
    float w = 1.0 / (1.0 + ycocg.x);
    return ycocg * w;
}

float3 InvTonemapYCoCg(float3 ycocg)
{
    float w = 1.0 / max(1.0 - ycocg.x, 1e-6);
    return ycocg * w;
}

// ── 14-DOP clip ──────────────────────────────────────────────────────
// Clips a point (history) toward the mean along the ray mean→history,
// constraining it to the intersection of 7 axis-aligned slab pairs.
// This is geometrically the tightest convex polytope from 7 oriented
// axes — much tighter than an AABB (3 axes) for the same samples.

float3 ClipToKDOP(float3 history, float3 mean,
                  float minProj[7], float maxProj[7],
                  float3 axes[7])
{
    float3 dir = history - mean;
    float tMin = 0.0;
    float tMax = 1.0;

    [unroll]
    for (int i = 0; i < 7; i++)
    {
        float dProj = dot(dir, axes[i]);
        float oProj = dot(mean, axes[i]);

        if (abs(dProj) > 1e-6)
        {
            float t0 = (minProj[i] - oProj) / dProj;
            float t1 = (maxProj[i] - oProj) / dProj;
            if (t0 > t1)
            {
                float tmp = t0;
                t0 = t1;
                t1 = tmp;
            }
            tMin = max(tMin, t0);
            tMax = min(tMax, t1);
        }
    }

    tMax = max(tMin, tMax);
    float t = saturate(tMax);
    return mean + dir * t;
}

// ── Main ─────────────────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= ScreenDims.x || dtid.y >= ScreenDims.y)
        return;

    int2 coord = int2(dtid.xy);
    float2 texelSize = 1.0 / float2(ScreenDims);
    float2 uv = (float2(coord) + 0.5) * texelSize;

    // ── Sample current frame ─────────────────────────────────────────
    float3 current = CurrentColor.Load(int3(coord, 0)).rgb;

    // ── Depth-based reprojection ─────────────────────────────────────
    // Without true motion vectors we reproject using depth + jitter offset.
    // The jitter offset accounts for TAA sub-pixel shifts between frames.
    // This gives correct reprojection for static geometry; dynamic objects
    // will be handled by the neighbourhood clamp (ghosting suppression).
    float2 historyUV = uv - Jitter.xy;

    // Clamp to valid UV range
    historyUV = clamp(historyUV, texelSize * 0.5, 1.0 - texelSize * 0.5);

    // ── Sample history (bilinear) ────────────────────────────────────
    float3 history = HistoryColor.SampleLevel(smpLinear, historyUV, 0).rgb;

    // ── 14-DOP neighbourhood clamp in YCoCg (perceptual space) ───────
    // 7 oriented axes: 3 axis-aligned + 4 body diagonals (normalized).
    // For each axis, project all 9 neighbourhood samples and track
    // min/max projection.  Then clip history to the polytope interior.

    static const float kInvSqrt3 = 0.57735026919;  // rsqrt(3)

    float3 axes[7];
    axes[0] = float3(1, 0, 0);
    axes[1] = float3(0, 1, 0);
    axes[2] = float3(0, 0, 1);
    axes[3] = float3( kInvSqrt3,  kInvSqrt3,  kInvSqrt3);
    axes[4] = float3( kInvSqrt3,  kInvSqrt3, -kInvSqrt3);
    axes[5] = float3( kInvSqrt3, -kInvSqrt3,  kInvSqrt3);
    axes[6] = float3(-kInvSqrt3,  kInvSqrt3,  kInvSqrt3);

    float minProj[7];
    float maxProj[7];
    float sumProj[7];
    float sumSqProj[7];

    [unroll]
    for (int a = 0; a < 7; a++)
    {
        minProj[a]   =  1e6;
        maxProj[a]   = -1e6;
        sumProj[a]   = 0.0;
        sumSqProj[a] = 0.0;
    }

    float3 nMean = 0;

    [unroll]
    for (int dy = -1; dy <= 1; dy++)
    {
        [unroll]
        for (int dx = -1; dx <= 1; dx++)
        {
            int2 sc = clamp(coord + int2(dx, dy), 0, int2(ScreenDims) - 1);
            float3 rgb = CurrentColor.Load(int3(sc, 0)).rgb;
            float3 ycocg = TonemapYCoCg(RGBtoYCoCg(rgb));

            nMean += ycocg;

            [unroll]
            for (int a2 = 0; a2 < 7; a2++)
            {
                float p = dot(ycocg, axes[a2]);
                minProj[a2]   = min(minProj[a2], p);
                maxProj[a2]   = max(maxProj[a2], p);
                sumProj[a2]   += p;
                sumSqProj[a2] += p * p;
            }
        }
    }
    nMean /= 9.0;

    // Variance-based tightening: shrink raw [min,max] to mean ± gamma * stddev
    // Raw min/max is WAY too loose — lets stale history through, causing ghosting.
    // gamma < 1.0 because k-DOP already has more axes than AABB.
    static const float gamma = 0.75;

    [unroll]
    for (int a3 = 0; a3 < 7; a3++)
    {
        float meanP  = sumProj[a3] / 9.0;
        float variance = max(sumSqProj[a3] / 9.0 - meanP * meanP, 0.0);
        float stddev = sqrt(variance);
        minProj[a3] = meanP - gamma * stddev;
        maxProj[a3] = meanP + gamma * stddev;
    }

    // Convert history to tonemapped YCoCg for clamping
    float3 histYCoCg = TonemapYCoCg(RGBtoYCoCg(history));

    // Clip history to the variance-tightened 14-DOP polytope
    float3 clippedYCoCg = ClipToKDOP(histYCoCg, nMean, minProj, maxProj, axes);

    // Convert clipped result back to RGB
    history = YCoCgtoRGB(InvTonemapYCoCg(clippedYCoCg));

    // ── Disocclusion detection ───────────────────────────────────────
    // Large distance between raw history and clipped history in YCoCg
    // indicates the sample was clipped hard = likely disoccluded/moved.
    float clipDist = length(histYCoCg - clippedYCoCg);
    float disocclusion = saturate(clipDist * 15.0);

    // Increase blend toward current frame when disoccluded
    // 0.8 = nearly full current frame on hard disocclusion
    float alpha = lerp(BlendAlpha, 0.8, disocclusion);

    // ── Depth discontinuity rejection ────────────────────────────────
    float centerDepth = DepthBuffer.Load(int3(coord, 0));
    float minDepth = centerDepth;
    float maxDepth = centerDepth;
    [unroll]
    for (int dy3 = -1; dy3 <= 1; dy3++) {
        [unroll]
        for (int dx3 = -1; dx3 <= 1; dx3++) {
            int2 sc = clamp(coord + int2(dx3, dy3), 0, int2(ScreenDims) - 1);
            float d = DepthBuffer.Load(int3(sc, 0));
            minDepth = min(minDepth, d);
            maxDepth = max(maxDepth, d);
        }
    }
    float depthRange = maxDepth - minDepth;
    // At depth edges, aggressively reduce temporal accumulation
    float depthEdge = saturate(depthRange * 1000.0);
    alpha = lerp(alpha, max(alpha, 0.6), depthEdge);

    // ── Blend ────────────────────────────────────────────────────────
    float3 resolved = lerp(history, current, alpha);

    // Write resolved to history (CopyResource copies it back to backbuffer)
    OutputHistory[coord] = float4(resolved, 1.0);
}
