// SDSM depth histogram — 256-bin logarithmic depth distribution
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Reference: Lauritzen et al. 2011, "Sample Distribution Shadow Maps"
//
// Reads pre-linearized depth (t31, view-space Z) and bins each pixel
// into a 256-bin logarithmic histogram covering [NearZ .. MaxDistance].
// Uses groupshared memory for per-group local histogram, then atomically
// merges into the global histogram to reduce contention.
//
// Sky pixels (linearDepth <= 0) are skipped.
// Pixels beyond MaxDistance are clamped to the last bin.

cbuffer HistogramCB : register(b0)
{
    uint2  ScreenDims;
    float  NearZ;
    float  FarZ;
    float  MaxDistance;     // Max shadow distance for histogram range
    float3 pad0;
}

Texture2D<float> DepthTex : register(t0);
Texture2D<float> LinearDepth : register(t31); // pre-computed linearized depth
RWStructuredBuffer<uint> Histogram : register(u0);  // 256 bins

#define NUM_BINS      256
#define GROUP_SIZE_X  16
#define GROUP_SIZE_Y  16
#define GROUP_SIZE    (GROUP_SIZE_X * GROUP_SIZE_Y)

groupshared uint localHistogram[NUM_BINS];

[numthreads(GROUP_SIZE_X, GROUP_SIZE_Y, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint GI : SV_GroupIndex)
{
    // ── Initialize local histogram ───────────────────────────────────
    // Each thread clears one or more bins
    for (uint i = GI; i < NUM_BINS; i += GROUP_SIZE)
        localHistogram[i] = 0;

    GroupMemoryBarrierWithGroupSync();

    // ── Accumulate this pixel into local histogram ───────────────────
    if (all(DTid.xy < ScreenDims))
    {
        float linZ = LinearDepth.Load(int3(DTid.xy, 0));

        // Skip sky pixels and invalid depth
        if (linZ > 0.0)
        {
            // Clamp to [NearZ, MaxDistance] range
            float clampedNear = max(NearZ, 1.0);
            float z = clamp(linZ, clampedNear, MaxDistance);

            // Logarithmic binning: bin = (log2(z) - log2(near)) / (log2(max) - log2(near)) * NUM_BINS
            float logNear = log2(clampedNear);
            float logMax  = log2(MaxDistance);
            float logRange = logMax - logNear;

            uint bin;
            if (logRange > 0.0)
            {
                float t = (log2(z) - logNear) / logRange;
                bin = min(uint(t * (float)NUM_BINS), NUM_BINS - 1);
            }
            else
            {
                bin = 0;
            }

            InterlockedAdd(localHistogram[bin], 1);
        }
    }

    GroupMemoryBarrierWithGroupSync();

    // ── Merge local histogram into global buffer ─────────────────────
    for (uint j = GI; j < NUM_BINS; j += GROUP_SIZE)
    {
        if (localHistogram[j] > 0)
            InterlockedAdd(Histogram[j], localHistogram[j]);
    }
}
