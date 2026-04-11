// Physical thin-lens depth-of-field -- Pass 1: Depth-based autofocus
// Reference: Standard thin-lens optics (textbook), Potmesil & Chakravarty 1981
// Copyright (c) 2026 Zain D. Harper. All rights reserved.

cbuffer AutofocusCB : register(b0)
{
    float4x4 ProjMatrix;       // Projection matrix (row-major)
    float2   ScreenDims;       // Full-res pixel dimensions
    float    NearZ;
    float    FarZ;
    float    FocusSpeed;       // Temporal smoothing speed
    float    DeltaTime;        // Frame delta time
    float    ManualFocusDist;  // >0 means override autofocus
    float    PrevFocusDist;    // Previous frame result
}

    static const int GRID = 10;

Texture2D<float> DepthTex : register(t0);
Texture2D<float> LinearDepth : register(t31); // pre-computed linearized depth
RWStructuredBuffer<float> FocusOutput : register(u0);  // 1-element: focus distance

// Linearize a reversed-Z depth value to view-space distance.
// LinearDepth (t31) already stores linear depth, but if we sample raw depth
// from DepthTex (t0) we need this conversion.
float DepthToViewZ(float d)
{
    // Reversed-Z: d=1 at near, d=0 at far
    // viewZ = NearZ * FarZ / (FarZ - d * (FarZ - NearZ))
    float denom = FarZ - d * (FarZ - NearZ);
    return (denom > 1e-6) ? (NearZ * FarZ / denom) : FarZ;
}

[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // If manual focus is set, use it with temporal smoothing
    if (ManualFocusDist > 0.0)
    {
        float smoothed = lerp(PrevFocusDist, ManualFocusDist,
                              saturate(FocusSpeed * DeltaTime));
        FocusOutput[0] = smoothed;
        return;
    }

    // Sample a GRID x GRID weighted grid of depth values around screen center.
    // Center-weighted: closer to center = higher weight.
    float2 center = ScreenDims * 0.5;
    float  gridSpan = min(ScreenDims.x, ScreenDims.y) * 0.15; // 15% of smaller dim

    float depths[GRID * GRID];
    float weights[GRID * GRID];
    int count = 0;

    [unroll]
    for (int y = 0; y < GRID; y++)
    {
        [unroll]
        for (int x = 0; x < GRID; x++)
        {
            // Map grid cell to screen position centered on screen
            float2 offset = float2(x - (GRID - 1) * 0.5, y - (GRID - 1) * 0.5)
                          / ((GRID - 1) * 0.5);   // [-1, 1]
            float2 samplePos = center + offset * gridSpan;

            // Clamp to valid screen range
            samplePos = clamp(samplePos, float2(0, 0), ScreenDims - float2(1, 1));

            // Read linear depth from pre-computed buffer
            float viewZ = LinearDepth.Load(int3((int2)samplePos, 0));

            // Reject sky pixels (extremely large depth)
            if (viewZ >= FarZ * 0.99)
            {
                depths[count]  = 0.0;
                weights[count] = 0.0;
            }
            else
            {
                depths[count]  = viewZ;
                // Gaussian center-weighting
                float dist2 = dot(offset, offset);
                weights[count] = exp(-2.0 * dist2);
            }
            count++;
        }
    }

    // Simple insertion sort on depths for median finding
    [loop]
    for (int i = 1; i < GRID * GRID; i++)
    {
        float dKey = depths[i];
        float wKey = weights[i];
        int j = i - 1;
        [loop]
        while (j >= 0 && depths[j] > dKey)
        {
            depths[j + 1]  = depths[j];
            weights[j + 1] = weights[j];
            j--;
        }
        depths[j + 1]  = dKey;
        weights[j + 1] = wKey;
    }

    // Find median of non-zero depth samples
    int validStart = 0;
    int validEnd   = GRID * GRID - 1;
    [loop]
    while (validStart < GRID * GRID && depths[validStart] <= 0.0)
        validStart++;
    [loop]
    while (validEnd > validStart && weights[validEnd] <= 0.0)
        validEnd--;

    if (validStart > validEnd)
    {
        // All sky or invalid -- keep previous focus
        FocusOutput[0] = PrevFocusDist;
        return;
    }

    int medianIdx = (validStart + validEnd) / 2;
    float medianDepth = depths[medianIdx];

    // Compute variance around median and reject outliers > 2 sigma
    float sumW  = 0.0;
    float sumD  = 0.0;
    float sumD2 = 0.0;

    [loop]
    for (int k = validStart; k <= validEnd; k++)
    {
        sumW  += weights[k];
        sumD  += depths[k] * weights[k];
        sumD2 += depths[k] * depths[k] * weights[k];
    }

    float meanD    = (sumW > 1e-6) ? (sumD / sumW) : medianDepth;
    float variance = (sumW > 1e-6) ? (sumD2 / sumW - meanD * meanD) : 0.0;
    float sigma    = sqrt(max(variance, 0.0));
    float threshold = max(sigma * 2.0, medianDepth * 0.3); // at least 30% of median

    // Weighted average of inliers
    float finalSum  = 0.0;
    float finalWSum = 0.0;

    [loop]
    for (int m = validStart; m <= validEnd; m++)
    {
        float deviation = abs(depths[m] - medianDepth);
        if (deviation <= threshold)
        {
            finalSum  += depths[m] * weights[m];
            finalWSum += weights[m];
        }
    }

    float newFocus = (finalWSum > 1e-6) ? (finalSum / finalWSum) : medianDepth;

    // Clamp to valid range
    newFocus = clamp(newFocus, NearZ * 2.0, FarZ * 0.95);

    // Temporal smoothing: lerp from previous focus distance
    float smoothAlpha = saturate(FocusSpeed * DeltaTime);
    float result = lerp(PrevFocusDist, newFocus, smoothAlpha);

    // Guard against degenerate values
    result = max(result, NearZ * 2.0);

    FocusOutput[0] = result;
}
