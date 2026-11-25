// RawAutoExposureSim.hlsl: Real-Time SM 6.6 / 6.10 GPU Compute 256-Bin Center-Weighted
// Log-Luminance Histogram & Percentile-Trimmed Asymmetric Human Eye-Adaptation Solver.
//
// Entry points:
//   1. cs_exposure_histogram ([numthreads(16, 16, 1)]):
//      Maps finite positive Rec.709 HDR luminance into 256 log2 bins [minLogLum .. maxLogLum]
//      using groupshared uint s_hist[256] (zero global atomic contention), smooth center-weighted
//      spatial weighting (1..4x), and NaN/Inf filtering, merging into RWByteAddressBuffer HistogramBuf.
//   2. cs_exposure_adapt ([numthreads(256, 1, 1)]):
//      Parallel Hillis-Steele prefix sum over all 256 bins trimming bottom lowPercentile (15%)
//      and top (1 - highPercentile) (5%) outliers/fireflies, computes EV100 & middle-gray exposure,
//      applies asymmetric human scotopic/photopic eye-adaptation EMA (tauBright=0.35s, tauDark=1.40s),
//      writes ExposureStateBuf[0], and clears HistogramBuf to 0 for the next frame.
//
// Original clean-room implementation, MIT License.

struct ExposureState
{
    float adaptedLum;
    float adaptedExposure;
    float targetLum;
    float targetEv100;
    uint  totalWeight;
    uint  validPixels;
    uint  trimmedWeight;
    uint  frameCount;
};

cbuffer AutoExposureCB : register(b0)
{
    uint4  gDimsAndFlags; // x = width, y = height, z = flags (bit0: resetAdaptation), w = reserved
    float4 gLogRange;     // x = minLogLum, y = maxLogLum, z = invLogLumRange, w = keyValue
    float4 gAdaptParams;  // x = dtSec, y = tauBrightSec, z = tauDarkSec, w = minExposure
    float4 gTrimAndClamp; // x = maxExposure, y = lowPercentile, z = highPercentile, w = minLumClamp
};

Texture2D<float4>                 HdrColorTex      : register(t0);
RWByteAddressBuffer               HistogramBuf     : register(u0);
RWStructuredBuffer<ExposureState> ExposureStateBuf : register(u1);

groupshared uint  s_hist[256];
groupshared uint  s_prefix[256];
groupshared float s_wSum[256];
groupshared float s_wLogSum[256];

[numthreads(16, 16, 1)]
void cs_exposure_histogram(
    uint3 dtid       : SV_DispatchThreadID,
    uint  groupIndex : SV_GroupIndex)
{
    s_hist[groupIndex] = 0u;
    GroupMemoryBarrierWithGroupSync();

    const uint width  = gDimsAndFlags.x;
    const uint height = gDimsAndFlags.y;

    if (dtid.x < width && dtid.y < height) {
        const float3 rgb = HdrColorTex.Load(int3(dtid.xy, 0)).rgb;

        // Ignore NaN / Inf pixels safely so corrupted HDR texels never poison exposure.
        if (all(isfinite(rgb))) {
            const float3 posRgb = max(rgb, 0.0f);
            const float  lum    = dot(posRgb, float3(0.2126f, 0.7152f, 0.0722f));

            // Smooth center-weighted spatial importance (1..4 integer weight).
            const float2 uv = (float2(dtid.xy) + 0.5f) / float2(max(width, 1u), max(height, 1u));
            const float2 d  = (uv - 0.5f) * 2.0f;
            const float  r2 = dot(d, d);
            const uint   w  = clamp((uint)round(1.0f + 3.0f * exp(-2.2f * r2)), 1u, 4u);

            const float minLogLum      = gLogRange.x;
            const float invLogLumRange = gLogRange.z;
            const float minLumThresh   = exp2(minLogLum);

            uint bin = 0u;
            if (lum >= minLumThresh) {
                const float norm = saturate((log2(lum) - minLogLum) * invLogLumRange);
                bin = min(255u, (uint)(norm * 254.999f) + 1u);
            }

            InterlockedAdd(s_hist[bin], w);
        }
    }

    GroupMemoryBarrierWithGroupSync();

    const uint localCount = s_hist[groupIndex];
    if (localCount > 0u) {
        HistogramBuf.InterlockedAdd(groupIndex * 4u, localCount);
    }
}

[numthreads(256, 1, 1)]
void cs_exposure_adapt(uint groupIndex : SV_GroupIndex)
{
    const uint idx = groupIndex;

    // Load bin count and immediately zero HistogramBuf for the next frame.
    const uint binW = HistogramBuf.Load(idx * 4u);
    HistogramBuf.Store(idx * 4u, 0u);

    s_prefix[idx] = binW;
    GroupMemoryBarrierWithGroupSync();

    // Parallel inclusive prefix sum (Hillis-Steele over 256 bins).
    [unroll]
    for (uint stride = 1u; stride < 256u; stride <<= 1u) {
        const uint val = (idx >= stride) ? s_prefix[idx - stride] : 0u;
        GroupMemoryBarrierWithGroupSync();
        s_prefix[idx] += val;
        GroupMemoryBarrierWithGroupSync();
    }

    const uint totalW = s_prefix[255u];
    const uint excl   = (idx > 0u) ? s_prefix[idx - 1u] : 0u;
    const uint incl   = s_prefix[idx];

    const float minLogLum      = gLogRange.x;
    const float maxLogLum      = gLogRange.y;
    const float logLumRange    = max(maxLogLum - minLogLum, 1.0e-4f);
    const float lowPercentile  = saturate(gTrimAndClamp.y);
    const float highPercentile = max(saturate(gTrimAndClamp.z), lowPercentile + 1.0e-4f);

    const float lowCut  = (float)totalW * lowPercentile;
    const float highCut = (float)totalW * highPercentile;
    const float lo      = max((float)excl, lowCut);
    const float hi      = min((float)incl, highCut);
    const float contrib = max(0.0f, hi - lo);

    const float binLog = (idx == 0u)
        ? minLogLum
        : (minLogLum + (((float)(idx - 1u) + 0.5f) / 255.0f) * logLumRange);

    s_wSum[idx]    = contrib;
    s_wLogSum[idx] = contrib * binLog;
    GroupMemoryBarrierWithGroupSync();

    // Parallel tree reduction over 256 bins.
    [unroll]
    for (uint s = 128u; s > 0u; s >>= 1u) {
        if (idx < s) {
            s_wSum[idx]    += s_wSum[idx + s];
            s_wLogSum[idx] += s_wLogSum[idx + s];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (idx == 0u) {
        const float keyValue    = max(gLogRange.w, 1.0e-4f);
        const float dtSec       = max(gAdaptParams.x, 0.0f);
        const float tauBright   = max(gAdaptParams.y, 1.0e-4f);
        const float tauDark     = max(gAdaptParams.z, 1.0e-4f);
        const float minExposure = max(gAdaptParams.w, 1.0e-5f);
        const float maxExposure = max(gTrimAndClamp.x, minExposure);
        const float minLumClamp = max(gTrimAndClamp.w, 1.0e-6f);

        const float trimmedW = s_wSum[0];
        float targetLog = (trimmedW > 1.0e-4f) ? (s_wLogSum[0] / trimmedW) : minLogLum;
        targetLog = clamp(targetLog, minLogLum, maxLogLum);
        const float targetLum = max(exp2(targetLog), minLumClamp);

        // Standard photographic EV100 = log2(avgLum * 100 / 12.5).
        const float targetEv100 = log2(max(targetLum, 1.0e-6f) * (100.0f / 12.5f));

        const ExposureState prev = ExposureStateBuf[0];
        const bool doReset = ((gDimsAndFlags.z & 1u) != 0u) ||
                             (prev.frameCount == 0u) ||
                             !isfinite(prev.adaptedLum) ||
                             (prev.adaptedLum <= 0.0f);

        float adaptedLum = targetLum;
        if (!doReset) {
            const float prevLog = clamp(log2(max(prev.adaptedLum, minLumClamp)), minLogLum, maxLogLum);
            // Asymmetric human scotopic/photopic eye adaptation:
            // faster pupil contraction into bright light (tauBright), slower dark dilation (tauDark).
            const float tau    = (targetLog >= prevLog) ? tauBright : tauDark;
            const float alpha  = saturate(1.0f - exp(-dtSec / tau));
            const float newLog = lerp(prevLog, targetLog, alpha);
            adaptedLum = max(exp2(newLog), minLumClamp);
        }

        const float adaptedExposure = clamp(keyValue / max(adaptedLum, minLumClamp), minExposure, maxExposure);

        ExposureState next;
        next.adaptedLum      = adaptedLum;
        next.adaptedExposure = adaptedExposure;
        next.targetLum       = targetLum;
        next.targetEv100     = targetEv100;
        next.totalWeight     = totalW;
        next.validPixels     = gDimsAndFlags.x * gDimsAndFlags.y;
        next.trimmedWeight   = (uint)round(trimmedW);
        next.frameCount      = doReset ? 1u : (prev.frameCount + 1u);
        ExposureStateBuf[0]  = next;
    }
}
