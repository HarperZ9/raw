// Copyright (c) 2026 Zain D. Harper. All rights reserved.
// Screen-space volumetric light shafts — Crytek cascaded radial blur approach.
// Half-res per-pixel ray march with Henyey-Greenstein phase + Beer-Lambert extinction.

cbuffer VolLightCB : register(b0)
{
    float3   sunDir;           float  intensity;
    float3   sunColor;         float  scatterDensity;
    float3   cameraPos;        float  anisotropy;
    float4x4 invViewProj;
    float4x4 viewProj;
    float    nearZ;
    float    farZ;
    uint     screenWidth;      // full-res
    uint     screenHeight;     // full-res
    uint     halfWidth;        // half-res
    uint     halfHeight;       // half-res
    int      numSteps;
    float    maxDistance;
    uint     frameIndex;
    float    pad0;
}

static const float PI = 3.14159265359;

Texture2D<float>    tDepth    : register(t0);  // full-res depth buffer (reversed-Z)
Texture2D<float>    LinearDepth : register(t31); // pre-computed linearized depth
RWTexture2D<float4> uScatter  : register(u0);  // half-res scatter output

// Interleaved gradient noise — Jorge Jimenez, "Next Generation Post Processing in Call of Duty: AW"
float InterleavedGradientNoise(float2 screenPos)
{
    float3 magic = float3(0.06711056, 0.00583715, 52.9829189);
    return frac(magic.z * frac(dot(screenPos, magic.xy)));
}

// Henyey-Greenstein phase function
// g: asymmetry parameter (-1..1), cosTheta: cos(angle between view and light)
float HenyeyGreenstein(float g, float cosTheta)
{
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / (4.0 * PI * pow(max(denom, 1e-5), 1.5));
}

// Reconstruct world position from depth buffer value and UV
float3 ReconstructWorldPos(float2 uv, float depth)
{
    // UV to NDC: [0,1] -> [-1,1], flip Y for D3D
    float4 clipPos = float4(uv * 2.0 - 1.0, depth, 1.0);
    clipPos.y = -clipPos.y;

    float4 worldPos = mul(invViewProj, clipPos);
    return worldPos.xyz / worldPos.w;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= halfWidth || DTid.y >= halfHeight)
        return;

    // Map half-res pixel to full-res UV
    float2 uv = (float2(DTid.xy) + 0.5) / float2(halfWidth, halfHeight);

    // Sample full-res depth at corresponding location
    uint2 fullCoord = uint2(DTid.xy * 2);
    fullCoord = min(fullCoord, uint2(screenWidth - 1, screenHeight - 1));
    float sceneDepth = tDepth.Load(int3(fullCoord, 0));

    // Reconstruct world position of this pixel
    float3 worldPos = ReconstructWorldPos(uv, sceneDepth);

    // Ray direction: camera to world position
    float3 rayDir = worldPos - cameraPos;
    float  rayLength = length(rayDir);
    rayDir = rayDir / max(rayLength, 1e-6);

    // Clamp ray length to max distance
    rayLength = min(rayLength, maxDistance);

    // Compute phase function: angle between view ray and sun direction
    float cosTheta = dot(rayDir, sunDir);
    float phase = HenyeyGreenstein(anisotropy, cosTheta);

    // Step size along the ray
    float stepSize = rayLength / float(numSteps);

    // Temporal jitter: offset ray start by IGN-based dither to break banding
    float jitter = InterleavedGradientNoise(float2(DTid.xy) + float(frameIndex % 64) * float2(5.588238, 3.138137));

    // Ray march accumulation
    float3 scatterAccum = 0.0;
    float  transmittance = 1.0;
    float  extinction = scatterDensity * 0.01; // extinction coefficient

    for (int i = 0; i < numSteps; i++)
    {
        // Current sample position along the ray
        float t = (float(i) + jitter) * stepSize;
        float3 samplePos = cameraPos + rayDir * t;

        // Project sample world position to screen to check occlusion
        {
            float4 clipSample = mul(viewProj, float4(samplePos, 1.0));
            float2 sampleUV2 = (clipSample.xy / clipSample.w) * 0.5 + 0.5;
            sampleUV2.y = 1.0 - sampleUV2.y; // D3D Y-flip

            // Skip samples projecting outside screen
            if (any(sampleUV2 < 0.0) || any(sampleUV2 > 1.0))
                continue;

            // Sample scene depth at projected position
            uint2 sCoord = min(uint2(sampleUV2 * float2(screenWidth, screenHeight)),
                              uint2(screenWidth - 1, screenHeight - 1));
            float sceneZ = tDepth.Load(int3(sCoord, 0));
            float sampleClipZ = clipSample.z / clipSample.w;

            // Reversed-Z: higher value = closer. If scene is closer than sample, sample is occluded.
            if (sceneZ > sampleClipZ + 0.0005)
                continue;
        }

        // Beer-Lambert extinction: transmittance decreases exponentially
        float stepTransmittance = exp(-extinction * stepSize);

        // Accumulate in-scattered light: phase * sunColor * intensity
        // The scatter at this step is weighted by current transmittance
        float3 stepScatter = phase * sunColor * intensity * transmittance * stepSize * extinction;
        scatterAccum += stepScatter;

        // Update transmittance
        transmittance *= stepTransmittance;

        // Early exit if transmittance is negligible
        if (transmittance < 0.01)
            break;
    }

    uScatter[DTid.xy] = float4(scatterAccum, transmittance);
}
