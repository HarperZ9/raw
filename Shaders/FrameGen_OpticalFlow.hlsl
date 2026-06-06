// Copyright (c) 2026 Zain D. Harper. All rights reserved.
// Quarter-res block matching optical flow via SAD (Sum of Absolute Differences).
// For each block in the current frame, search a neighborhood in the previous frame
// for the best match. Output: per-block motion vectors in pixel units.

cbuffer FrameGenParams : register(b0)
{
    uint2  ScreenDims;       // Full-res backbuffer dimensions
    uint2  FlowDims;         // Flow map dimensions (quarter or half res)
    float  FlowScale;        // Pixels-to-UV conversion factor (1/divisor)
    float  BlendWeight;      // Synthesis blend weight [0..1]
    uint   FrameIndex;       // Monotonic frame counter
    uint   QualityMode;      // 1 = Low (no refinement), 2 = High (hierarchical)
}

static const int kBlockRadius = 2;   // 5x5 block (2*2+1 = 5)
static const int kSearchRadius = 4;  // Search ±4 pixels in previous frame

Texture2D<float4> PrevFrame : register(t0);   // History[read] — frame N-1
Texture2D<float4> CurrFrame : register(t1);   // History[write] — frame N
RWTexture2D<float2> FlowOut : register(u0);   // R16G16_FLOAT motion vectors (pixels)

// Convert RGB to luminance for SAD comparison
float Luminance(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= FlowDims.x || dtid.y >= FlowDims.y)
    {
        return;
    }

    // Center of this block in full-res coordinates
    // Each flow texel corresponds to a block in full-res space
    float scale = 1.0 / FlowScale; // e.g., 4.0 for quarter-res
    int2 blockCenter = int2(float2(dtid.xy) * scale + scale * 0.5);

    // Clamp to screen bounds
    blockCenter = clamp(blockCenter, int2(kBlockRadius, kBlockRadius),
                        int2(ScreenDims) - int2(kBlockRadius + 1, kBlockRadius + 1));

    // Search for the best matching block in the previous frame
    float bestSAD = 1e30;
    int2 bestOffset = int2(0, 0);

    // Determine search radius based on quality mode
    int searchRadius = kSearchRadius;
    if (QualityMode >= 2)
        searchRadius = kSearchRadius * 2; // Wider search for High quality

    for (int sy = -searchRadius; sy <= searchRadius; sy++)
    {
        for (int sx = -searchRadius; sx <= searchRadius; sx++)
        {
            int2 offset = int2(sx, sy);
            float sad = 0.0;

            // Compare 5x5 block (kBlockRadius=2 -> 5x5)
            [unroll]
            for (int by = -kBlockRadius; by <= kBlockRadius; by++)
            {
                [unroll]
                for (int bx = -kBlockRadius; bx <= kBlockRadius; bx++)
                {
                    int2 currPos = blockCenter + int2(bx, by);
                    int2 prevPos = currPos + offset;

                    // Clamp to valid texture coordinates
                    currPos = clamp(currPos, int2(0, 0), int2(ScreenDims) - 1);
                    prevPos = clamp(prevPos, int2(0, 0), int2(ScreenDims) - 1);

                    float currLum = Luminance(CurrFrame.Load(int3(currPos, 0)).rgb);
                    float prevLum = Luminance(PrevFrame.Load(int3(prevPos, 0)).rgb);

                    sad += abs(currLum - prevLum);
                }
            }

            if (sad < bestSAD)
            {
                bestSAD = sad;
                bestOffset = offset;
            }
        }
    }

    // Output motion vector in pixel units (from current to previous)
    FlowOut[dtid.xy] = float2(bestOffset);
}
