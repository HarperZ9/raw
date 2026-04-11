// Copyright (c) 2026 Zain D. Harper. All rights reserved.
// Screen-space wave distortion: sinusoidal UV displacement based on time + depth.

cbuffer WaveCB : register(b0)
{
    float2 ScreenDims;
    float  Time;
    float  WaveIntensity;     // Global amplitude scale
    float  NearZ;
    float  FarZ;
    float  SubmersionDepth;   // Depth below surface (waves diminish with depth)
    float  pad0;
}

Texture2D<float> DepthTex : register(t0);
Texture2D<float> LinearDepth : register(t31); // pre-computed linearized depth
RWTexture2D<float2> WaveOutput : register(u0);

static const float PI = 3.14159265359;

// Linearize reversed-Z depth
float LinearizeDepthVal(float d)
{
    return NearZ * FarZ / (NearZ + d * (FarZ - NearZ));
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= (uint)ScreenDims.x || DTid.y >= (uint)ScreenDims.y)
    {
        WaveOutput[DTid.xy] = float2(0.0, 0.0);
        return;
    }

    float2 uv = (float2(DTid.xy) + 0.5) / ScreenDims;

    // Sample depth to modulate wave amplitude (closer objects get more distortion)
    float depth = DepthTex.Load(int3(DTid.xy, 0));
    float linearZ = LinearizeDepthVal(depth);

    // Depth-based attenuation: less distortion for distant pixels
    float depthAtten = saturate(1.0 - linearZ / (FarZ * 0.3));

    // Submersion depth attenuation: waves diminish the deeper you go
    float submersionAtten = saturate(1.0 - SubmersionDepth / 500.0);
    submersionAtten = submersionAtten * submersionAtten;

    // 4-octave sinusoidal wave displacement
    // Each octave has a unique direction, frequency, and speed
    float2 offset = float2(0.0, 0.0);

    // Octave 1: large slow horizontal waves
    offset.x += sin(uv.y * 8.0 * PI + Time * 1.2) * 0.003;
    offset.y += cos(uv.x * 6.0 * PI + Time * 0.9) * 0.002;

    // Octave 2: medium diagonal waves
    offset.x += sin((uv.x + uv.y) * 14.0 * PI + Time * 1.8) * 0.0015;
    offset.y += cos((uv.x - uv.y) * 12.0 * PI + Time * 1.5) * 0.0015;

    // Octave 3: small fast vertical ripples
    offset.x += sin(uv.y * 24.0 * PI + Time * 3.0) * 0.0008;
    offset.y += cos(uv.x * 20.0 * PI + Time * 2.7) * 0.001;

    // Octave 4: tiny high-frequency detail
    offset.x += sin((uv.x * 2.0 + uv.y) * 40.0 * PI + Time * 4.5) * 0.0004;
    offset.y += cos((uv.y * 2.0 - uv.x) * 35.0 * PI + Time * 4.0) * 0.0004;

    // Apply all attenuation factors
    offset *= WaveIntensity * depthAtten * submersionAtten;

    // Reduce distortion near screen edges to avoid pulling in out-of-bounds texels
    float2 edgeDist = min(uv, 1.0 - uv);
    float edgeFade = saturate(min(edgeDist.x, edgeDist.y) * 20.0);
    offset *= edgeFade;

    WaveOutput[DTid.xy] = offset;
}
