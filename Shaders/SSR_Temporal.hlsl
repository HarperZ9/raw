// Temporal accumulation for SSR — exponential blend with confidence-aware
// history rejection.

cbuffer TemporalCB : register(b0)
{
    uint2  HalfDims;
    float  TemporalAlpha;       // Blend weight for new frame (0.05-0.2)
    uint   FrameIndex;
};

Texture2D<float4> ReflCurrent   : register(t0);  // Resolved reflection (this frame)
Texture2D<float4> ReflHistory   : register(t1);  // Previous frame's accumulated
Texture2D<float2> MotionVectors : register(t2);  // Per-pixel UV delta from MotionVectorGen

RWTexture2D<float4> ReflOutput  : register(u0);  // Output accumulated reflection

SamplerState LinearSampler : register(s0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= HalfDims.x || DTid.y >= HalfDims.y)
        return;

    int2   coord = int2(DTid.xy);
    float2 uv    = (float2(coord) + 0.5) / float2(HalfDims);
    float4 current = ReflCurrent.Load(int3(coord, 0));

    // First few frames: no valid history yet
    if (FrameIndex < 3) { ReflOutput[coord] = current; return; }

    // Reproject via motion vectors (UV-space, resolution-independent)
    float2 motion = MotionVectors.SampleLevel(LinearSampler, uv, 0);
    float2 prevUV = uv - motion;

    float4 history;
    bool valid = all(prevUV >= 0.0) && all(prevUV <= 1.0);

    if (valid)
        history = ReflHistory.SampleLevel(LinearSampler, prevUV, 0);
    else
        history = float4(0, 0, 0, 0);

    // 3x3 neighborhood clamping (RGB only, preserve confidence in .w)
    float4 minVal = current, maxVal = current;
    [unroll] for (int dy = -1; dy <= 1; dy++)
        [unroll] for (int dx = -1; dx <= 1; dx++)
        {
            float4 nv = ReflCurrent.Load(int3(clamp(coord + int2(dx, dy), 0, int2(HalfDims) - 1), 0));
            minVal = min(minVal, nv);
            maxVal = max(maxVal, nv);
        }
    if (valid)
        history = clamp(history, minVal, maxVal);

    // Confidence-aware blending
    float alpha = TemporalAlpha;
    if (current.w < 0.001)
        alpha = valid ? 0.02 : 1.0; // No hit: fade slowly if valid history, else reset
    else if (!valid || history.w < 0.001)
        alpha = 1.0; // No valid history: trust current

    float4 result = lerp(history, current, alpha);
    ReflOutput[coord] = any(isnan(result)) || any(isinf(result)) ? current : result;
}
