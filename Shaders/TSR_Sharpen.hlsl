// Copyright (c) 2026 Zain D. Harper. All rights reserved.
// RCAS (Robust Contrast-Adaptive Sharpening): 5-tap cross pattern
// that avoids ringing artifacts. Based on AMD FidelityFX CAS concepts.

cbuffer TSRSharpenCB : register(b0)
{
    float  Sharpness;              // [0..1] global sharpness control
    uint2  DisplayDims;            // display resolution
    float  pad0;
    float4 MaterialSharpenWeight;  // per-material sharpen: [default, arch, foliage, skin]
    float2 RcpDisplayDims;         // 1.0 / DisplayDims
    float2 pad1;
}

Texture2D<float4> UpscaledColor  : register(t0);
Texture2D<uint>   MaterialBuffer : register(t1);
SamplerState PointSampler        : register(s0);

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

float Luminance(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

float4 main(VSOut input) : SV_Target
{
    // Integer pixel coordinate
    int2 pos = int2(input.pos.xy);

    // 5-tap cross pattern: center + north + south + east + west
    float3 center = UpscaledColor.Load(int3(pos, 0)).rgb;
    float3 north  = UpscaledColor.Load(int3(pos + int2( 0, -1), 0)).rgb;
    float3 south  = UpscaledColor.Load(int3(pos + int2( 0,  1), 0)).rgb;
    float3 east   = UpscaledColor.Load(int3(pos + int2( 1,  0), 0)).rgb;
    float3 west   = UpscaledColor.Load(int3(pos + int2(-1,  0), 0)).rgb;

    // Per-channel min/max of the cross neighborhood
    float3 minRGB = min(min(north, south), min(east, west));
    float3 maxRGB = max(max(north, south), max(east, west));

    // Soft minimum and maximum (includes center)
    minRGB = min(minRGB, center);
    maxRGB = max(maxRGB, center);

    // Compute the RCAS sharpening amount
    // This is the reciprocal of the local contrast, clamped to avoid ringing
    float3 ampRGB = saturate(min(minRGB, 2.0 - maxRGB) / max(maxRGB, 1e-5));

    // Use the minimum channel amplitude to prevent color shifts
    float amp = min(min(ampRGB.r, ampRGB.g), ampRGB.b);

    // Scale by user sharpness control
    // Negative weight sharpens, clamped to [-0.5, 0] to avoid ringing
    float sharpWeight = -amp * Sharpness;
    sharpWeight = max(sharpWeight, -0.5); // Clamp to prevent ringing

    // Material-aware sharpness
    uint materialID = MaterialBuffer.Load(int3(pos, 0));
    float materialSharp = MaterialSharpenWeight.x; // default
    if (materialID == 1) materialSharp = MaterialSharpenWeight.y; // architecture
    else if (materialID == 2) materialSharp = MaterialSharpenWeight.z; // foliage
    else if (materialID == 3) materialSharp = MaterialSharpenWeight.w; // skin

    sharpWeight *= materialSharp;

    // Apply sharpening: weighted sum of neighbors vs center
    // result = (center + weight * (N + S + E + W)) / (1 + 4 * weight)
    float3 result = (center + sharpWeight * (north + south + east + west))
                  / (1.0 + 4.0 * sharpWeight);

    // Clamp to prevent negative values
    result = max(result, 0.0);

    return float4(result, 1.0);
}
