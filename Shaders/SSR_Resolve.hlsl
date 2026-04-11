// Resolve — sample scene color at ray hit UV, apply fades.
// Copyright (c) 2026 Zain D. Harper. All rights reserved.

cbuffer ResolveCB : register(b0)
{
    uint2  HalfDims;
    uint2  ScreenDims;
    float  Intensity;
    float3 pad0;
};

Texture2D<float4> HitBuffer    : register(t0);  // UV + viewZ + confidence
Texture2D<float4> SceneColor   : register(t1);  // Backbuffer copy
SamplerState      LinearSamp   : register(s0);

RWTexture2D<float4> ReflOutput : register(u0);  // Resolved reflection color

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= HalfDims.x || DTid.y >= HalfDims.y)
        return;

    int2 coord = int2(DTid.xy);
    float4 hit = HitBuffer.Load(int3(coord, 0));

    float2 hitUV     = hit.xy;
    float  confidence = hit.w;

    if (confidence < 0.001)
    {
        ReflOutput[coord] = float4(0, 0, 0, 0);
        return;
    }

    // Sample scene color at hit position (use linear filtering for smooth result)
    float3 reflColor = SceneColor.SampleLevel(LinearSamp, hitUV, 0).rgb;

    // Simple Fresnel approximation baked into confidence
    // (Full Fresnel would need the view angle, which we don't store in hit buffer)

    ReflOutput[coord] = float4(reflColor * Intensity, confidence);
}
