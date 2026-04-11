// Copyright (c) 2026 Zain D. Harper. All rights reserved.
// Underwater god rays: radial blur from sun screen position with depth attenuation.

cbuffer GodRayCB : register(b0)
{
    float2 ScreenDims;        // Quarter-res output dims
    float2 FullScreenDims;    // Full-res dims
    float2 SunScreenPos;      // Sun position in UV space [0,1]
    float  Intensity;         // Ray brightness
    float  Decay;             // Exponential decay per step (0.95 = slow fade)
    float  Density;           // Sample density multiplier
    float  Exposure;          // Final exposure multiplier
    float  NearZ;
    float  FarZ;
    int    NumSamples;        // Samples along ray (default 16)
    float  WaterSurfaceZ;     // For depth testing
    float2 pad0;
}

Texture2D<float>  DepthTex  : register(t0);
Texture2D<float4> SceneTex  : register(t1);   // Backbuffer copy (bright pixels)
Texture2D<float>  LinearDepth : register(t31); // pre-computed linearized depth
RWTexture2D<float> GodRayOutput : register(u0);

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
        GodRayOutput[DTid.xy] = 0.0;
        return;
    }

    // Current pixel UV in quarter-res space
    float2 uv = (float2(DTid.xy) + 0.5) / ScreenDims;

    // Direction from this pixel toward the sun's screen position
    float2 deltaUV = (SunScreenPos - uv);
    float  rayLength = length(deltaUV);

    // Normalize and scale by density / number of samples
    deltaUV *= Density / float(max(NumSamples, 1));

    // Radial blur accumulation
    float2 sampleUV = uv;
    float  illumination = 0.0;
    float  decayFactor = 1.0;

    for (int i = 0; i < NumSamples; i++)
    {
        // Sample scene luminance at this point along the ray
        // Map to full-res coordinates for scene texture lookup
        uint2 fullCoord = min(uint2(sampleUV * FullScreenDims), uint2(FullScreenDims) - 1);

        float4 sceneColor = SceneTex.Load(int3(fullCoord, 0));
        float luminance = dot(sceneColor.rgb, float3(0.2126, 0.7152, 0.0722));

        // Depth-based attenuation: only accumulate scatter for pixels
        // that are NOT occluded by geometry close to camera
        float sampleDepth = DepthTex.Load(int3(fullCoord, 0));
        float linearZ = LinearizeDepthVal(sampleDepth);

        // Sky or very far pixels contribute more (light shafts come through water surface)
        float depthWeight = saturate(linearZ / (FarZ * 0.5));

        illumination += luminance * decayFactor * depthWeight;

        // Exponential decay along the ray
        decayFactor *= Decay;

        // Step along the ray toward the sun
        sampleUV += deltaUV;

        // Clamp UV to valid range
        sampleUV = saturate(sampleUV);
    }

    // Apply exposure and intensity
    float result = illumination * Exposure * Intensity / float(max(NumSamples, 1));

    // Distance-from-sun falloff: rays are strongest near the sun
    float sunDist = saturate(1.0 - rayLength * 1.5);
    result *= sunDist;

    GodRayOutput[DTid.xy] = saturate(result);
}
