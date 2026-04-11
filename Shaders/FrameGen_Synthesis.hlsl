// Copyright (c) 2026 Zain D. Harper. All rights reserved.
// Synthesize an intermediate frame (N+0.5) by warping previous frame using optical flow.
// Handles disocclusion (holes) by blending with the current frame.

cbuffer FrameGenParams : register(b0)
{
    uint2  ScreenDims;
    uint2  FlowDims;
    float  FlowScale;
    float  BlendWeight;
    uint   FrameIndex;
    uint   QualityMode;
}

Texture2D<float4>  CurrFrame   : register(t0);   // Frame N (current)
Texture2D<float4>  PrevFrame   : register(t1);   // Frame N-1 (history)
Texture2D<float2>  FlowMap     : register(t2);   // Motion vectors (pixels, quarter-res)
RWTexture2D<float4> SynthOut   : register(u0);   // Synthesized frame N+0.5
SamplerState smpLinear : register(s0);
SamplerState smpPoint  : register(s1);

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= ScreenDims.x || dtid.y >= ScreenDims.y)
        return;

    float2 pixelUV = (float2(dtid.xy) + 0.5) / float2(ScreenDims);

    // Sample flow map at this pixel's corresponding flow-res coordinate
    float2 flowUV = pixelUV; // flow map covers the same UV range
    float2 flow = FlowMap.SampleLevel(smpLinear, flowUV, 0);

    // Flow is in pixel units at full-res. For the intermediate frame (N+0.5),
    // we warp by half the motion.
    float2 halfFlow = flow * 0.5;

    // Warp: where was this pixel in the previous frame?
    // Previous frame location = current pixel + halfFlow (motion from curr->prev)
    float2 prevUV = pixelUV + halfFlow / float2(ScreenDims);

    // Where will this pixel be in the current frame relative to the midpoint?
    float2 currUV = pixelUV - halfFlow / float2(ScreenDims);

    // Sample both warped frames
    float4 warpedPrev = PrevFrame.SampleLevel(smpLinear, prevUV, 0);
    float4 warpedCurr = CurrFrame.SampleLevel(smpLinear, currUV, 0);

    // ── Disocclusion detection ──────────────────────────────────────
    // Check if the warped UV is out of bounds (indicates disocclusion)
    bool prevOOB = any(prevUV < 0.0) || any(prevUV > 1.0);
    bool currOOB = any(currUV < 0.0) || any(currUV > 1.0);

    // Check flow consistency: sample flow at neighboring pixels
    // and detect divergence (indicates object boundaries / disocclusion)
    float2 flowRight = FlowMap.SampleLevel(smpLinear,
        pixelUV + float2(1.0 / FlowDims.x, 0), 0);
    float2 flowDown  = FlowMap.SampleLevel(smpLinear,
        pixelUV + float2(0, 1.0 / FlowDims.y), 0);

    float divergence = abs(flowRight.x - flow.x) + abs(flowDown.y - flow.y);
    float disocclusionMask = saturate(divergence * 0.1);

    // ── Blend synthesized frame ─────────────────────────────────────
    float4 synthesized;

    if (prevOOB && currOOB)
    {
        // Both warps are out of bounds: fall back to current frame
        synthesized = CurrFrame.Load(int3(dtid.xy, 0));
    }
    else if (prevOOB)
    {
        synthesized = warpedCurr;
    }
    else if (currOOB)
    {
        synthesized = warpedPrev;
    }
    else
    {
        // Normal case: blend the two warped samples
        // Weight toward current frame in disoccluded regions
        float prevWeight = (1.0 - disocclusionMask) * BlendWeight;
        float currWeight = 1.0 - prevWeight;
        synthesized = warpedPrev * prevWeight + warpedCurr * currWeight;
    }

    // In heavily disoccluded regions, blend more with the raw current frame
    float4 rawCurr = CurrFrame.Load(int3(dtid.xy, 0));
    synthesized = lerp(synthesized, rawCurr, disocclusionMask);

    SynthOut[dtid.xy] = synthesized;
}
