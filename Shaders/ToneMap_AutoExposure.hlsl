// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// GPU autoexposure from luminance histogram.
// Reference: standard real-time HDR exposure (geometric mean + percentile metering).
//
// Reads a 256-bin log-luminance histogram (normalized, sum ~ 1.0) and computes
// a temporally-smoothed exposure multiplier.  The histogram covers the range
// [2^MinEV .. 2^MaxEV] mapped linearly across bins 0..255.

cbuffer AutoExpCB : register(b0)
{
    float PrevEV;
    float DeltaTime;
    float AdaptSpeed;
    float ExposureComp;
    float MinEV;
    float MaxEV;
    float pad0, pad1;
}

StructuredBuffer<float> HistogramStats : register(t0);
RWStructuredBuffer<float> ExposureOut : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // ── Accumulate weighted log-luminance from the 256-bin histogram ──
    // Each bin i represents a luminance range centered at:
    //   EV_i = MinEV + (i + 0.5) / 256.0 * (MaxEV - MinEV)
    //   lum_i = 2^EV_i
    //
    // We trim the bottom 10% and top 2% of the distribution to reject
    // outliers (deep shadows, specular highlights / sun disk).

    float evRange = MaxEV - MinEV;
    float totalWeight = 0.0;
    float weightedLogSum = 0.0;

    // First pass: compute total weight for percentile thresholds
    float cumulative = 0.0;
    float totalMass = 0.0;
    for (uint i = 0; i < 256; i++)
        totalMass += HistogramStats[i];

    // Guard against empty histogram (no pixels rendered yet)
    if (totalMass < 1e-6)
    {
        ExposureOut[0] = PrevEV;
        return;
    }

    float lowThreshold  = totalMass * 0.10;  // Skip bottom 10%
    float highThreshold = totalMass * 0.98;  // Skip top 2%

    // Second pass: geometric mean over the accepted range
    cumulative = 0.0;
    for (uint j = 0; j < 256; j++)
    {
        float binWeight = HistogramStats[j];
        float prevCumulative = cumulative;
        cumulative += binWeight;

        // Trim: only include bins within [10%, 98%] of the CDF
        if (cumulative < lowThreshold || prevCumulative > highThreshold)
            continue;

        // Clamp the contribution to only the portion within the accepted range
        float lo = max(prevCumulative, lowThreshold);
        float hi = min(cumulative, highThreshold);
        float accepted = hi - lo;

        // EV at the center of this bin
        float binEV = MinEV + ((float)j + 0.5) / 256.0 * evRange;

        weightedLogSum += binEV * accepted;
        totalWeight += accepted;
    }

    // Geometric mean luminance via weighted average of log2(luminance) = EV
    float avgEV = (totalWeight > 1e-6) ? (weightedLogSum / totalWeight) : 0.0;
    float avgLuminance = exp2(avgEV);

    // ── Target exposure: map avgLuminance to 18% grey (standard key value) ──
    // exposure = 0.18 / avgLuminance  =>  targetEV = log2(avgLuminance / 0.18)
    float targetEV = log2(max(avgLuminance, 1e-6) / 0.18);

    // Apply user exposure compensation
    targetEV -= ExposureComp;

    // Clamp to configured range
    targetEV = clamp(targetEV, MinEV, MaxEV);

    // ── Temporal smoothing: exponential moving average ──
    float alpha = saturate(AdaptSpeed * DeltaTime);
    float smoothedEV = lerp(PrevEV, targetEV, alpha);
    smoothedEV = clamp(smoothedEV, -20.0, 20.0);

    ExposureOut[0] = smoothedEV;
}
