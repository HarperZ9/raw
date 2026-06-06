// Temporal accumulation — exponential blend with rejection.
// Copyright (c) 2026 Zain D. Harper. All rights reserved.

cbuffer TemporalCB : register(b0)
{
    uint2  ScreenDims;
    float  NearZ;
    float  FarZ;
    float  TemporalAlpha;
    float  DepthRejectThreshold;
    uint   FrameIndex;
    float  pad0;
};

Texture2D<float>  AOCurrent     : register(t0);
Texture2D<float>  AOHistory     : register(t1);
Texture2D<float>  DepthTex      : register(t2);
Texture2D<float2> MotionVectors : register(t3); // Per-pixel UV delta from MotionVectorGen
RWTexture2D<float> AOOutput     : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= ScreenDims.x || DTid.y >= ScreenDims.y) return;

    int2   coord   = int2(DTid.xy);
    float2 uv      = (float2(coord) + 0.5) / float2(ScreenDims);
    float  current = AOCurrent.Load(int3(coord, 0));

    // Skip history for first frames
    if (FrameIndex < 3) { AOOutput[coord] = current; return; }

    // Reproject via motion vectors
    float2 motion = MotionVectors.Load(int3(coord, 0));
    float2 prevUV = uv - motion;

    // Off-screen rejection
    if (any(prevUV < 0.0) || any(prevUV > 1.0))
    {
        AOOutput[coord] = current;
        return;
    }

    int2 prevCoord = clamp(int2(prevUV * float2(ScreenDims)), 0, int2(ScreenDims) - 1);
    float history = AOHistory.Load(int3(prevCoord, 0));

    // 3x3 neighborhood clamping to suppress ghosting
    float minVal = current, maxVal = current;
    [unroll] for (int dy = -1; dy <= 1; dy++)
        [unroll] for (int dx = -1; dx <= 1; dx++)
        {
            float nv = AOCurrent.Load(int3(clamp(coord + int2(dx, dy), 0, int2(ScreenDims) - 1), 0));
            minVal = min(minVal, nv);
            maxVal = max(maxVal, nv);
        }
    history = clamp(history, minVal, maxVal);

    // Adaptive rejection: large diff = faster convergence
    float diff   = abs(current - history);
    float reject = smoothstep(0.05, 0.3, diff);
    float alpha  = lerp(TemporalAlpha, 0.5, reject);

    float result = lerp(history, current, alpha);
    AOOutput[coord] = (isnan(result) || isinf(result)) ? current : result;
}
