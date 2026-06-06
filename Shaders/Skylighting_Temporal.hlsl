// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Temporal Accumulation CS — Single-channel ping-pong history blend.
//
// Blends the current spatially-denoised sky visibility with the
// previous frame's accumulated result.  Without motion vectors we
// cannot reproject, so we rely on adaptive alpha to fight ghosting:
// when current and history diverge significantly the blend weight
// ramps toward 0.5 (faster response, less stability).
//
// FrameIndex < 3 bypass: the first few frames have no valid history,
// so we write current directly to avoid black ghosting on startup or
// after a load screen.

cbuffer TemporalCB : register(b0)
{
    uint2  ScreenDims;
    float  NearZ;
    float  FarZ;
    float  TemporalAlpha;          // Base blend factor (0.15-0.25)
    float  DepthRejectThreshold;   // Relative depth delta for rejection
    uint   FrameIndex;
    float  pad0;
};

Texture2D<float>  SkyCurrent     : register(t0);   // This frame (spatial output)
Texture2D<float>  SkyHistory     : register(t1);   // Previous accumulated frame
Texture2D<float>  DepthTex       : register(t2);   // HiZ depth (reversed-Z)
Texture2D<float2> MotionVectors  : register(t3);   // Per-pixel UV delta from MotionVectorGen
Texture2D<float>  LinearDepth    : register(t31);  // View-space Z

RWTexture2D<float> SkyOutput     : register(u0);   // Next accumulated frame

SamplerState LinearSampler : register(s0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= ScreenDims.x || DTid.y >= ScreenDims.y)
        return;

    int2   coord   = int2(DTid.xy);
    float2 uv      = (float2(coord) + 0.5) / float2(ScreenDims);
    float  current = SkyCurrent.Load(int3(coord, 0));

    // No valid history yet — write current directly
    if (FrameIndex < 3) { SkyOutput[coord] = current; return; }

    // Reproject via motion vectors
    float2 motion = MotionVectors.Load(int3(coord, 0));
    float2 prevUV = uv - motion;

    // Off-screen rejection
    if (any(prevUV < 0.0) || any(prevUV > 1.0))
    {
        SkyOutput[coord] = current;
        return;
    }

    float history = SkyHistory.SampleLevel(LinearSampler, prevUV, 0);

    // 3x3 neighborhood clamping
    float minVal = current, maxVal = current;
    [unroll] for (int dy = -1; dy <= 1; dy++)
        [unroll] for (int dx = -1; dx <= 1; dx++)
        {
            float nv = SkyCurrent.Load(int3(clamp(coord + int2(dx, dy), 0, int2(ScreenDims) - 1), 0));
            minVal = min(minVal, nv);
            maxVal = max(maxVal, nv);
        }
    // Widen clamp range slightly to allow more temporal smoothing
    float margin = (maxVal - minVal) * 0.25 + 0.02;
    history = clamp(history, minVal - margin, maxVal + margin);

    // Softer adaptive rejection — allow more history to reduce scan-line noise
    float diff   = abs(current - history);
    float reject = smoothstep(0.10, 0.40, diff);
    float alpha  = lerp(TemporalAlpha * 0.7, 0.4, reject);

    float result = lerp(history, current, alpha);
    SkyOutput[coord] = (isnan(result) || isinf(result)) ? current : result;
}
