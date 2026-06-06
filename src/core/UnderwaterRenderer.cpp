//=============================================================================
//  UnderwaterRenderer.cpp — Compute-first underwater post-processing
//
//  Dispatch flow (per frame, PrePresent stage, priority 5):
//    1. Dispatch Caustics CS:   3-octave trochoidal waves → quarter-res caustic map
//    2. Dispatch God Rays CS:   Radial blur from sun → quarter-res volumetric light
//    3. Dispatch Wave Distortion CS: 4-octave Gerstner displacement → full-res UV offset
//    4. Execute Composite PS:   Beer-Lambert, photic zone, Tyndall, Snell's window,
//                               wet lens transition, caustic/godray overlay, depth fog
//
//  Only runs when player is submerged (PlayerData::Water.x == 1.0).
//=============================================================================

#include "UnderwaterRenderer.h"
#include "HiZPyramid.h"
#include "ShaderLoader.h"
#include "SRVInjector.h"
#include "SharedGPUResources.h"
#include "GPUResource.h"

namespace SB
{

// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Pass 1: Caustics CS (quarter-res)
//
//  Animated trochoidal wave pattern:
//    3 octaves at different scales and speeds produce a natural-looking
//    caustic pattern.  Result is depth-stratified: bright near the
//    surface, fading to zero at depth.
// ═══════════════════════════════════════════════════════════════════════════

static const char kCausticCS[] = R"HLSL(
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
// Procedural caustic pattern via 2-layer Voronoi noise animated by time.

cbuffer CausticCB : register(b0)
{
    float4x4 InvViewProj;       // For world-space reconstruction
    float2   ScreenDims;        // Quarter-res output dimensions
    float2   FullScreenDims;    // Full-res screen dimensions
    float    Time;              // Accumulated time (seconds)
    float    NearZ;
    float    FarZ;
    float    WaterSurfaceZ;     // World-space Z of water surface
    float    CausticIntensity;  // Brightness multiplier
    float    MaxCausticDepth;   // World units below surface where caustics vanish
    float2   pad0;
}

Texture2D<float> DepthTex : register(t0);
RWTexture2D<float> CausticOutput : register(u0);

// Hash for Voronoi cell centers
float2 VoronoiHash(float2 p)
{
    p = float2(dot(p, float2(127.1, 311.7)),
               dot(p, float2(269.5, 183.3)));
    return frac(sin(p) * 43758.5453);
}

// Single-layer Voronoi distance field
float Voronoi(float2 uv)
{
    float2 ip = floor(uv);
    float2 fp = frac(uv);

    float minDist = 1.0;

    [unroll]
    for (int y = -1; y <= 1; y++)
    {
        [unroll]
        for (int x = -1; x <= 1; x++)
        {
            float2 neighbor = float2(x, y);
            float2 cellCenter = VoronoiHash(ip + neighbor);

            // Animate cell centers over time
            cellCenter = 0.5 + 0.5 * sin(Time * 0.8 + 6.2831 * cellCenter);

            float2 diff = neighbor + cellCenter - fp;
            float d = dot(diff, diff);
            minDist = min(minDist, d);
        }
    }

    return sqrt(minDist);
}

// Linearize reversed-Z depth
float LinearizeDepth(float d)
{
    return NearZ * FarZ / (FarZ - d * (FarZ - NearZ));
}

// Reconstruct world position from depth
float3 ReconstructWorldPos(float2 uv, float depth)
{
    float4 clip = float4(uv * 2.0 - 1.0, depth, 1.0);
    clip.y = -clip.y;
    float4 world = mul(InvViewProj, clip);
    return world.xyz / world.w;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= (uint)ScreenDims.x || DTid.y >= (uint)ScreenDims.y)
    {
        return;
    }

    // Map quarter-res to full-res UV
    float2 uv = (float2(DTid.xy) + 0.5) / ScreenDims;

    // Sample depth at corresponding full-res location
    uint2 fullCoord = min(uint2(uv * FullScreenDims), uint2(FullScreenDims) - 1);
    float depth = DepthTex.Load(int3(fullCoord, 0));

    // Reconstruct world position
    float3 worldPos = ReconstructWorldPos(uv, depth);

    // Compute depth below water surface
    float depthBelowSurface = WaterSurfaceZ - worldPos.z;

    // No caustics above water or beyond max depth
    if (depthBelowSurface <= 0.0 || depthBelowSurface > MaxCausticDepth)
    {
        CausticOutput[DTid.xy] = 0.0;
        return;
    }

    // Two-layer Voronoi at different scales for natural interference pattern
    float2 worldUV = worldPos.xy * 0.02; // world-space tiling scale

    float v1 = Voronoi(worldUV * 1.0 + float2(Time * 0.03, Time * 0.02));
    float v2 = Voronoi(worldUV * 2.7 + float2(-Time * 0.04, Time * 0.05));

    // Caustic pattern: bright where both layers have small distances (cell edges)
    // Invert and sharpen the Voronoi to get bright caustic lines
    float c1 = pow(saturate(1.0 - v1), 3.0);
    float c2 = pow(saturate(1.0 - v2), 3.0);

    // Combine layers: multiply for sharper pattern, add for broader glow
    float caustic = (c1 + c2) * 0.5 + c1 * c2 * 2.0;

    // Depth attenuation: caustics fade with depth below surface
    float depthFade = saturate(1.0 - depthBelowSurface / MaxCausticDepth);
    depthFade = depthFade * depthFade; // quadratic falloff

    // Surface proximity boost: brightest near the surface
    float surfaceBoost = saturate(1.0 - depthBelowSurface / (MaxCausticDepth * 0.1));

    float result = caustic * depthFade * CausticIntensity * (1.0 + surfaceBoost);
    CausticOutput[DTid.xy] = saturate(result);
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Pass 2: God Rays CS (quarter-res)
//
//  Radial blur from the sun's screen-space position.  16 samples along
//  each ray, accumulating depth-tested luminance with exponential decay.
//  Produces soft volumetric light shafts filtering through the water.
// ═══════════════════════════════════════════════════════════════════════════

static const char kGodRayCS[] = R"HLSL(
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
    return NearZ * FarZ / (FarZ - d * (FarZ - NearZ));
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
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Pass 3: Wave Distortion CS (full-res)
//
//  4-octave Gerstner wave displacement produces natural interference
//  patterns in the UV offset map.  Each octave has a unique direction,
//  frequency, amplitude, and steepness.  The summed displacement is
//  written as a 2D UV offset (R16G16_FLOAT).
// ═══════════════════════════════════════════════════════════════════════════

static const char kWaveCS[] = R"HLSL(
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
    return NearZ * FarZ / (FarZ - d * (FarZ - NearZ));
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
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Pass 4: Composite PS (fullscreen)
//
//  Combines all underwater effects into the final image:
//    - UV distortion from wave offset map
//    - Beer-Lambert per-channel absorption
//    - Photic zone color grading
//    - Caustic overlay (additive)
//    - God ray blend (additive)
//    - Tyndall forward-scattering haze
//    - Snell's window (total internal reflection above critical angle)
//    - Wet lens transition (when near/at surface)
//    - Depth fog
// ═══════════════════════════════════════════════════════════════════════════

static const char* kCompositePS = R"HLSL(
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
// Underwater composite: Beer-Lambert absorption, caustics, god rays, wave distortion, depth fog.

cbuffer UnderwaterCompositeCB : register(b0)
{
    float4x4 InvViewProj;
    float4   AbsorptionCoeff;   // .xyz = per-channel absorption (R,G,B), .w = unused
    float4   FogColor;          // .xyz = underwater fog/tint color, .w = fogDensity
    float4   TyndallColor;      // .xyz = scatter color, .w = tyndallDensity
    float2   ScreenDims;
    float    NearZ;
    float    FarZ;
    float    Time;
    float    WaterSurfaceZ;
    float    SubmersionDepth;
    float    CausticIntensity;
    float    GodRayIntensity;
    float    WaveIntensity;
    float    WetLensTimer;       // Counts up from 0 after surfacing; >2.0 = fully dry
    float    pad0;
    float4   CameraPos;         // .xyz = world camera position
    float4   SunDir;            // .xyz = world-space sun direction (toward sun)
    float4   CameraForward;     // .xyz = camera forward direction
}

static const float PI = 3.14159265359;
static const float SNELL_CRITICAL_ANGLE = 0.8480;   // cos(48.6 degrees) — water IOR 1.333
static const float WATER_IOR = 1.333;

Texture2D<float4> SceneColor    : register(t0);  // Backbuffer copy
Texture2D<float>  DepthTex      : register(t1);  // Game depth (reversed-Z)
Texture2D<float>  CausticTex    : register(t2);  // Quarter-res caustic map
Texture2D<float>  GodRayTex     : register(t3);  // Quarter-res god ray buffer
Texture2D<float2> WaveOffsetTex : register(t4);  // Full-res UV offset
Texture2D<float>  LinearDepth   : register(t31); // pre-computed linearized depth
SamplerState PointSampler  : register(s0);
SamplerState LinearSampler : register(s1);

struct PSInput
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

// Linearize reversed-Z depth
float LinearizeDepth(float d)
{
    return NearZ * FarZ / (FarZ - d * (FarZ - NearZ));
}

// Reconstruct world position from UV + depth
float3 ReconstructWorldPos(float2 uv, float depth)
{
    float4 clip = float4(uv * 2.0 - 1.0, depth, 1.0);
    clip.y = -clip.y;
    float4 world = mul(InvViewProj, clip);
    return world.xyz / world.w;
}

float4 main(PSInput input) : SV_Target
{
    float2 uv = input.texcoord;

    // ── Wave distortion: apply UV offset ────────────────────────────
    float2 waveOffset = WaveOffsetTex.Sample(PointSampler, uv) * WaveIntensity;
    float2 distortedUV = saturate(uv + waveOffset);

    // ── Sample scene with distorted UVs ─────────────────────────────
    float3 color = SceneColor.Sample(PointSampler, distortedUV).rgb;

    // ── Depth and world position ────────────────────────────────────
    float depth = DepthTex.Sample(PointSampler, distortedUV);
    float linearZ = LinearizeDepth(depth);
    float3 worldPos = ReconstructWorldPos(distortedUV, depth);

    // Distance from camera to pixel in world space
    float viewDist = length(worldPos - CameraPos.xyz);

    // Depth below water surface
    float depthBelowSurface = max(WaterSurfaceZ - worldPos.z, 0.0);

    // ── Beer-Lambert color absorption ───────────────────────────────
    // exp(-depth * absorptionCoeff * colorChannel)
    // Red absorbed first (highest coeff), blue last (lowest)
    float3 absorption = exp(-viewDist * AbsorptionCoeff.xyz * float3(0.2, 0.6, 0.8));
    color *= absorption;

    // ── Caustic overlay (additive) ──────────────────────────────────
    float caustic = CausticTex.Sample(LinearSampler, distortedUV);
    // Caustics add light, modulated by sun color (assume white-ish for underwater)
    color += caustic * CausticIntensity * absorption * float3(0.8, 0.9, 1.0);

    // ── God ray blend (additive) ────────────────────────────────────
    float godRay = GodRayTex.Sample(LinearSampler, distortedUV);
    color += godRay * GodRayIntensity * float3(0.6, 0.8, 1.0);

    // ── Tyndall forward-scattering haze ─────────────────────────────
    // Light scattering through particulates in the water
    float3 viewDir = normalize(worldPos - CameraPos.xyz);
    float sunViewDot = max(dot(viewDir, SunDir.xyz), 0.0);
    float tyndallPhase = pow(sunViewDot, 4.0); // Forward-scatter lobe
    float tyndallFog = 1.0 - exp(-viewDist * TyndallColor.w * 0.001);
    color += TyndallColor.xyz * tyndallPhase * tyndallFog;

    // ── Depth fog: color shifts to deep blue with distance ──────────
    float fogFactor = 1.0 - exp(-viewDist * FogColor.w * 0.001);
    fogFactor = saturate(fogFactor);

    // Fog color shifts deeper blue with greater depth below surface
    float3 fogTint = FogColor.xyz;
    float deepFactor = saturate(depthBelowSurface / 500.0);
    fogTint = lerp(fogTint, float3(0.01, 0.02, 0.06), deepFactor);

    color = lerp(color, fogTint, fogFactor);

    // ── Snell's window (total internal reflection) ──────────────────
    // When looking upward past the critical angle, see total internal reflection
    float upDot = dot(CameraForward.xyz, float3(0, 0, 1));
    if (upDot > SNELL_CRITICAL_ANGLE)
    {
        // Outside Snell's window: strong reflection of underwater scene
        float reflectAmount = saturate((upDot - SNELL_CRITICAL_ANGLE) / (1.0 - SNELL_CRITICAL_ANGLE));
        reflectAmount = reflectAmount * reflectAmount;
        // Darken toward the reflection zone edges
        color = lerp(color, fogTint * 0.3, reflectAmount * 0.5);
    }

    // ── Wet lens transition ─────────────────────────────────────────
    // When just surfacing, apply water droplet / blur effect
    if (WetLensTimer < 2.0)
    {
        float wetAmount = saturate(1.0 - WetLensTimer * 0.5);
        // Darken edges (vignette) and add slight blue tint
        float2 centered = uv * 2.0 - 1.0;
        float vignette = 1.0 - dot(centered, centered) * 0.5;
        float wetVignette = lerp(1.0, vignette, wetAmount);
        color *= wetVignette;
        color = lerp(color, color * float3(0.85, 0.92, 1.0), wetAmount * 0.3);
    }

    return float4(max(color, 0.0), 1.0);
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  CB structures — must match HLSL cbuffers exactly
// ═══════════════════════════════════════════════════════════════════════════

struct alignas(16) CausticCBData
{
    float    invViewProj[16];    // 64 bytes — row-major 4x4
    float    screenDimsX;        // +64
    float    screenDimsY;        // +68
    float    fullScreenDimsX;    // +72
    float    fullScreenDimsY;    // +76
    float    time;               // +80
    float    nearZ;              // +84
    float    farZ;               // +88
    float    waterSurfaceZ;      // +92
    float    causticIntensity;   // +96
    float    maxCausticDepth;    // +100
    float    pad0[2];            // +104..112  → total 112 (7 * 16)
};
static_assert(sizeof(CausticCBData) == 112, "CausticCBData must be 112 bytes");

struct alignas(16) GodRayCBData
{
    float    screenDimsX;        // +0
    float    screenDimsY;        // +4
    float    fullScreenDimsX;    // +8
    float    fullScreenDimsY;    // +12
    float    sunScreenPosX;      // +16
    float    sunScreenPosY;      // +20
    float    intensity;          // +24
    float    decay;              // +28
    float    density;            // +32
    float    exposure;           // +36
    float    nearZ;              // +40
    float    farZ;               // +44
    int32_t  numSamples;         // +48
    float    waterSurfaceZ;      // +52
    float    pad0[2];            // +56..64 → total 64 (4 * 16)
};
static_assert(sizeof(GodRayCBData) == 64, "GodRayCBData must be 64 bytes");

struct alignas(16) WaveCBData
{
    float    screenDimsX;        // +0
    float    screenDimsY;        // +4
    float    time;               // +8
    float    waveIntensity;      // +12
    float    nearZ;              // +16
    float    farZ;               // +20
    float    submersionDepth;    // +24
    float    pad0;               // +28  → total 32 (2 * 16)
};
static_assert(sizeof(WaveCBData) == 32, "WaveCBData must be 32 bytes");

struct alignas(16) UnderwaterCompositeCBData
{
    float    invViewProj[16];    // 64 bytes — row-major 4x4
    float    absorptionR;        // +64
    float    absorptionG;        // +68
    float    absorptionB;        // +72
    float    absorptionPad;      // +76
    float    fogColorR;          // +80
    float    fogColorG;          // +84
    float    fogColorB;          // +88
    float    fogDensity;         // +92
    float    tyndallColorR;      // +96
    float    tyndallColorG;      // +100
    float    tyndallColorB;      // +104
    float    tyndallDensity;     // +108
    float    screenDimsX;        // +112
    float    screenDimsY;        // +116
    float    nearZ;              // +120
    float    farZ;               // +124
    float    time;               // +128
    float    waterSurfaceZ;      // +132
    float    submersionDepth;    // +136
    float    causticIntensity;   // +140
    float    godRayIntensity;    // +144
    float    waveIntensity;      // +148
    float    wetLensTimer;       // +152
    float    pad0;               // +156
    float    cameraPosX;         // +160
    float    cameraPosY;         // +164
    float    cameraPosZ;         // +168
    float    cameraPosPad;       // +172
    float    sunDirX;            // +176
    float    sunDirY;            // +180
    float    sunDirZ;            // +184
    float    sunDirPad;          // +188
    float    cameraFwdX;         // +192
    float    cameraFwdY;         // +196
    float    cameraFwdZ;         // +200
    float    cameraFwdPad;       // +204  → total 208 (13 * 16)
};
static_assert(sizeof(UnderwaterCompositeCBData) == 208, "UnderwaterCompositeCBData must be 208 bytes");


// ═══════════════════════════════════════════════════════════════════════════
//  SetUnderwater — called from game state tracking
// ═══════════════════════════════════════════════════════════════════════════

void UnderwaterRenderer::SetUnderwater(bool uw)
{
    bool wasUnder = m_underwater;
    m_underwater = uw;

    // Track transition for wet lens effect
    if (wasUnder && !uw) {
        // Just surfaced: start wet lens timer
        m_waterSurfaceTime = 0.0f;
    }

    m_wasUnderwater = wasUnder;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Initialize
// ═══════════════════════════════════════════════════════════════════════════

bool UnderwaterRenderer::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                                     IDXGISwapChain* sc)
{
    if (m_initialized) return true;
    if (!dev || !ctx || !sc) return false;

    auto& cm  = ComputeManager::Get();
    auto& rpm = RenderPassManager::Get();
    auto& pl  = RenderPipeline::Get();

    if (!cm.IsInitialized() || !rpm.IsInitialized() || !pl.IsInitialized()) {
        SKSE::log::error("UnderwaterRenderer: prerequisites not initialized");
        return false;
    }

    m_device  = dev;
    m_context = ctx;

    // Get backbuffer dimensions
    ID3D11Texture2D* backTex = nullptr;
    HRESULT hr = sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                reinterpret_cast<void**>(&backTex));
    if (FAILED(hr) || !backTex) {
        SKSE::log::error("UnderwaterRenderer: failed to get backbuffer (0x{:X})",
                         static_cast<uint32_t>(hr));
        return false;
    }

    D3D11_TEXTURE2D_DESC bbDesc;
    backTex->GetDesc(&bbDesc);
    backTex->Release();

    m_screenW = bbDesc.Width;
    m_screenH = bbDesc.Height;

    if (!CompileShaders()) return false;
    if (!CreateResources()) {
        ReleaseResources();
        return false;
    }

    // Register composite fullscreen pass
    m_compositePass = rpm.RegisterPass({
        .name     = "UnderwaterComposite",
        .psSource = kCompositePS,
    });

    if (!m_compositePass) {
        SKSE::log::error("UnderwaterRenderer: failed to compile composite PS");
        ReleaseResources();
        return false;
    }

    // Register pipeline pass: PrePresent, priority 5 (runs first)
    m_pipelineHandle = pl.AddPass({
        .name     = "UnderwaterRenderer",
        .stage    = PipelineStage::PreUI,
        .priority = 5,
        .enabled  = true,
        .execute  = [this](PassContext& ctx) { ExecutePass(ctx); },
    });

    m_initialized = true;
    m_frameIndex  = 0;

    SKSE::log::info("UnderwaterRenderer: initialized ({}x{}, qtr={}x{}, priority=5 PrePresent)",
                    m_screenW, m_screenH, m_screenW / 4, m_screenH / 4);
    SKSE::log::info("  Absorption: R={:.2f} G={:.2f} B={:.2f}, Fog={:.3f}, Tyndall={:.3f}",
                    m_absorptionR, m_absorptionG, m_absorptionB, m_fogDensity, m_tyndallDensity);
    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Compile all shaders
// ═══════════════════════════════════════════════════════════════════════════

bool UnderwaterRenderer::CompileShaders()
{
    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    auto CompileCS = [&](const char* name, const char* source,
                         ID3D11ComputeShader** outCS) -> bool
    {
        ID3DBlob* blob = ShaderLoader::Compile(name, source, "main", "cs_5_0", flags);
        if (!blob) {
            SKSE::log::error("UnderwaterRenderer: {} compile failed", name);
            return false;
        }

        HRESULT hr = m_device->CreateComputeShader(blob->GetBufferPointer(),
                                            blob->GetBufferSize(),
                                            nullptr, outCS);
        blob->Release();
        if (FAILED(hr)) {
            SKSE::log::error("UnderwaterRenderer: CreateComputeShader failed for {} (0x{:X})",
                             name, static_cast<uint32_t>(hr));
            return false;
        }
        return true;
    };

    if (!CompileCS("Underwater_Caustics",  kCausticCS, &m_causticCS)) return false;
    if (!CompileCS("Underwater_GodRays",   kGodRayCS,  &m_godRayCS))  return false;
    if (!CompileCS("Underwater_Waves",     kWaveCS,    &m_waveCS))    return false;

    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Create GPU resources
// ═══════════════════════════════════════════════════════════════════════════

bool UnderwaterRenderer::CreateResources()
{
    HRESULT hr;

    uint32_t qtrW = (m_screenW + 3) / 4;
    uint32_t qtrH = (m_screenH + 3) / 4;

    // ── Helper: create a 2D texture with SRV + UAV ──────────────────────
    auto CreateTexWithViews = [&](const char* name, uint32_t w, uint32_t h,
                                   DXGI_FORMAT format,
                                   ID3D11Texture2D** outTex,
                                   ID3D11ShaderResourceView** outSRV,
                                   ID3D11UnorderedAccessView** outUAV) -> bool
    {
        return CreateGPUTexture(m_device, w, h, format, outTex, outSRV, outUAV, name);
    };

    // Pass 1: Caustic map (quarter-res R8_UNORM)
    if (!CreateTexWithViews("caustic", qtrW, qtrH, DXGI_FORMAT_R8_UNORM,
                             &m_causticTex, &m_causticSRV, &m_causticUAV))
        return false;

    // Pass 2: God ray buffer (quarter-res R16_FLOAT)
    if (!CreateTexWithViews("godRay", qtrW, qtrH, DXGI_FORMAT_R16_FLOAT,
                             &m_godRayTex, &m_godRaySRV, &m_godRayUAV))
        return false;

    // Pass 3: Wave offset map (full-res R16G16_FLOAT)
    if (!CreateTexWithViews("waveOffset", m_screenW, m_screenH, DXGI_FORMAT_R16G16_FLOAT,
                             &m_waveTex, &m_waveSRV, &m_waveUAV))
        return false;

    // ── Constant buffers ────────────────────────────────────────────────
    // Constant buffers
    if (!CreateCB(m_device, sizeof(CausticCBData), &m_causticCB)) return false;
    if (!CreateCB(m_device, sizeof(GodRayCBData), &m_godRayCB)) return false;
    if (!CreateCB(m_device, sizeof(WaveCBData), &m_waveCB)) return false;
    // ── Backbuffer copy texture + SRV ───────────────────────────────────
    {
        D3D11_TEXTURE2D_DESC bbDesc = {};
        bbDesc.Width       = m_screenW;
        bbDesc.Height      = m_screenH;
        bbDesc.MipLevels   = 1;
        bbDesc.ArraySize   = 1;
        bbDesc.Format      = DXGI_FORMAT_R16G16B16A16_FLOAT;  // HDR-safe
        bbDesc.SampleDesc  = {1, 0};
        bbDesc.Usage       = D3D11_USAGE_DEFAULT;
        bbDesc.BindFlags   = D3D11_BIND_SHADER_RESOURCE;

        hr = m_device->CreateTexture2D(&bbDesc, nullptr, &m_bbCopyTex);
        if (FAILED(hr)) {
            SKSE::log::error("UnderwaterRenderer: failed to create BB copy texture");
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = bbDesc.Format;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels       = 1;
        srvDesc.Texture2D.MostDetailedMip = 0;

        hr = m_device->CreateShaderResourceView(m_bbCopyTex, &srvDesc, &m_bbCopySRV);
        if (FAILED(hr)) {
            SKSE::log::error("UnderwaterRenderer: failed to create BB copy SRV");
            return false;
        }
    }

    // ── Samplers ────────────────────────────────────────────────────────
    {
        D3D11_SAMPLER_DESC sd = {};
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD   = D3D11_FLOAT32_MAX;

        // s0: point (full-res exact reads)
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        m_device->CreateSamplerState(&sd, &m_pointSampler);

        // s1: bilinear (quarter-res upsample)
        sd.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        m_device->CreateSamplerState(&sd, &m_linearSampler);
    }

    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Per-frame execution (called by RenderPipeline at PrePresent, priority 5)
// ═══════════════════════════════════════════════════════════════════════════

void UnderwaterRenderer::ExecutePass(PassContext& ctx)
{
    if (!m_initialized || !m_enabled) return;

    // Only execute when player is underwater
    if (!m_underwater) {
        // Still tick the wet lens timer (counts up when NOT underwater)
        if (m_wasUnderwater || m_waterSurfaceTime < 3.0f) {
            m_waterSurfaceTime += ctx.deltaTime;
        }
        return;
    }

    auto& scene = SceneMatrices::Get();
    if (!scene.IsValid()) return;

    // Accumulate time for wave animation
    m_totalTime += ctx.deltaTime;

    // Reset wet lens timer while underwater
    m_waterSurfaceTime = 0.0f;

    auto& cm = ComputeManager::Get();

    const float nearZ = scene.NearClip();
    const float farZ  = scene.FarClip();

    uint32_t qtrW = (m_screenW + 3) / 4;
    uint32_t qtrH = (m_screenH + 3) / 4;

    // ── Acquire depth SRV ───────────────────────────────────────────────
    ID3D11ShaderResourceView* depthSRV = D3D11Hook::GetGameDepthSRV();
    if (!depthSRV) {
        auto& hiz = HiZPyramid::Get();
        if (hiz.IsInitialized() && hiz.GetSRV()) {
            depthSRV = hiz.GetSRV();
        }
    }
    if (!depthSRV) return;  // No depth buffer available

    // ── Acquire scene texture ─────────────────────────────────────────────
    ID3D11Texture2D* backTex = nullptr;
    bool ownBackTex = false;

    if (ctx.gameSceneRTV) {
        // Mid-frame: extract from game's active RTV
        ID3D11Resource* res = nullptr;
        ctx.gameSceneRTV->GetResource(&res);
        if (res) {
            res->QueryInterface(__uuidof(ID3D11Texture2D),
                                reinterpret_cast<void**>(&backTex));
            res->Release();
        }
        ownBackTex = (backTex != nullptr);
    }

    if (!backTex) {
        // Present-time fallback
        auto* sc = ctx.swapChain;
        if (!sc) sc = D3D11Hook::GetSwapChain();
        if (!sc) return;
        if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                  reinterpret_cast<void**>(&backTex))))
            return;
        ownBackTex = true;
    }

    // Check format compatibility for CopyResource
    {
        D3D11_TEXTURE2D_DESC sceneDesc, copyDesc;
        backTex->GetDesc(&sceneDesc);
        m_bbCopyTex->GetDesc(&copyDesc);
        if (sceneDesc.Format != copyDesc.Format ||
            sceneDesc.Width  != copyDesc.Width ||
            sceneDesc.Height != copyDesc.Height) {
            // Recreate copy texture with matching format
            m_bbCopyTex->Release(); m_bbCopyTex = nullptr;
            if (m_bbCopySRV) { m_bbCopySRV->Release(); m_bbCopySRV = nullptr; }
            D3D11_TEXTURE2D_DESC newDesc = sceneDesc;
            newDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            newDesc.Usage = D3D11_USAGE_DEFAULT;
            newDesc.CPUAccessFlags = 0;
            newDesc.MiscFlags = 0;
            m_device->CreateTexture2D(&newDesc, nullptr, &m_bbCopyTex);
            if (m_bbCopyTex) {
                D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.Format = newDesc.Format;
                srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Texture2D.MipLevels = 1;
                m_device->CreateShaderResourceView(m_bbCopyTex, &srvDesc, &m_bbCopySRV);
            }
            if (!m_bbCopyTex) {
                if (ownBackTex) backTex->Release();
                return;
            }
        }
    }

    ctx.context->CopyResource(m_bbCopyTex, backTex);

    // ── Compute sun screen-space position for god rays ──────────────────
    float sunScreenX = 0.5f;
    float sunScreenY = 0.3f;  // Default: above center
    {
        const float* sunDir = scene.SunDirection();
        const float* camPos = scene.CameraPos();
        // Project a point far along the sun direction
        float sunWorldX = camPos[0] + sunDir[0] * 10000.0f;
        float sunWorldY = camPos[1] + sunDir[1] * 10000.0f;
        float sunWorldZ = camPos[2] + sunDir[2] * 10000.0f;

        // World → clip via ViewProj
        const float* vp = scene.ViewProjMatrix();
        float cx = vp[0]*sunWorldX + vp[4]*sunWorldY + vp[8]*sunWorldZ  + vp[12];
        float cy = vp[1]*sunWorldX + vp[5]*sunWorldY + vp[9]*sunWorldZ  + vp[13];
        float cw = vp[3]*sunWorldX + vp[7]*sunWorldY + vp[11]*sunWorldZ + vp[15];

        if (cw > 0.001f) {
            float ndcX = cx / cw;
            float ndcY = cy / cw;
            sunScreenX = ndcX * 0.5f + 0.5f;
            sunScreenY = 1.0f - (ndcY * 0.5f + 0.5f);  // D3D UV flip
        }
    }

    cm.SaveCSState();

    // Bind pre-computed linearized depth at t31 for CS passes
    auto* linearDepthSRV = SharedGPUResources::Get().GetLinearDepthSRV();
    ctx.context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &linearDepthSRV);

    // ═════════════════════════════════════════════════════════════════════
    //  Pass 1: Caustics CS (quarter-res)
    // ═════════════════════════════════════════════════════════════════════
    {
        CausticCBData cb = {};
        std::memcpy(cb.invViewProj, scene.InvViewProjMatrix(), sizeof(float) * 16);
        cb.screenDimsX      = static_cast<float>(qtrW);
        cb.screenDimsY      = static_cast<float>(qtrH);
        cb.fullScreenDimsX  = static_cast<float>(m_screenW);
        cb.fullScreenDimsY  = static_cast<float>(m_screenH);
        cb.time             = m_totalTime;
        cb.nearZ            = nearZ;
        cb.farZ             = farZ;
        cb.waterSurfaceZ    = m_waterSurfaceZ;
        cb.causticIntensity = m_causticIntensity;
        cb.maxCausticDepth  = 200.0f;  // Caustics fade beyond 200 world units

        UploadCB(ctx.context, m_causticCB, &cb, sizeof(cb));

        ctx.context->CSSetShaderResources(0, 1, &depthSRV);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &m_causticUAV, nullptr);
        ctx.context->CSSetConstantBuffers(0, 1, &m_causticCB);
        ctx.context->CSSetShader(m_causticCS, nullptr, 0);

        UINT groupsX = (qtrW + 7) / 8;
        UINT groupsY = (qtrH + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        // Clear bindings
        ID3D11ShaderResourceView* nullSRV[1] = {};
        ctx.context->CSSetShaderResources(0, 1, nullSRV);
        ID3D11UnorderedAccessView* nullUAV[1] = {};
        ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
    }

    // ═════════════════════════════════════════════════════════════════════
    //  Pass 2: God Rays CS (quarter-res)
    // ═════════════════════════════════════════════════════════════════════
    {
        GodRayCBData cb = {};
        cb.screenDimsX     = static_cast<float>(qtrW);
        cb.screenDimsY     = static_cast<float>(qtrH);
        cb.fullScreenDimsX = static_cast<float>(m_screenW);
        cb.fullScreenDimsY = static_cast<float>(m_screenH);
        cb.sunScreenPosX   = sunScreenX;
        cb.sunScreenPosY   = sunScreenY;
        cb.intensity       = m_godRayIntensity;
        cb.decay           = 0.96f;
        cb.density         = 1.0f;
        cb.exposure        = 0.3f;
        cb.nearZ           = nearZ;
        cb.farZ            = farZ;
        cb.numSamples      = 16;
        cb.waterSurfaceZ   = m_waterSurfaceZ;

        UploadCB(ctx.context, m_godRayCB, &cb, sizeof(cb));

        // t0 = depth, t1 = scene (backbuffer copy)
        ID3D11ShaderResourceView* srvs[] = { depthSRV, m_bbCopySRV };
        ctx.context->CSSetShaderResources(0, 2, srvs);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &m_godRayUAV, nullptr);
        ctx.context->CSSetConstantBuffers(0, 1, &m_godRayCB);
        ctx.context->CSSetShader(m_godRayCS, nullptr, 0);

        UINT groupsX = (qtrW + 7) / 8;
        UINT groupsY = (qtrH + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        ID3D11ShaderResourceView* nullSRVs[2] = {};
        ctx.context->CSSetShaderResources(0, 2, nullSRVs);
        ID3D11UnorderedAccessView* nullUAV[1] = {};
        ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
    }

    // ═════════════════════════════════════════════════════════════════════
    //  Pass 3: Wave Distortion CS (full-res)
    // ═════════════════════════════════════════════════════════════════════
    {
        WaveCBData cb = {};
        cb.screenDimsX     = static_cast<float>(m_screenW);
        cb.screenDimsY     = static_cast<float>(m_screenH);
        cb.time            = m_totalTime;
        cb.waveIntensity   = m_waveIntensity;
        cb.nearZ           = nearZ;
        cb.farZ            = farZ;
        cb.submersionDepth = m_submersionDepth;

        UploadCB(ctx.context, m_waveCB, &cb, sizeof(cb));

        ctx.context->CSSetShaderResources(0, 1, &depthSRV);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &m_waveUAV, nullptr);
        ctx.context->CSSetConstantBuffers(0, 1, &m_waveCB);
        ctx.context->CSSetShader(m_waveCS, nullptr, 0);

        UINT groupsX = (m_screenW + 7) / 8;
        UINT groupsY = (m_screenH + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        ID3D11ShaderResourceView* nullSRV[1] = {};
        ctx.context->CSSetShaderResources(0, 1, nullSRV);
        ID3D11UnorderedAccessView* nullUAV[1] = {};
        ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
    }

    // Unbind linearized depth t31 from CS
    {
        ID3D11ShaderResourceView* nullSRV = nullptr;
        ctx.context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &nullSRV);
    }

    cm.RestoreCSState();

    // ═════════════════════════════════════════════════════════════════════
    //  Pass 4: Composite PS (fullscreen)
    // ═════════════════════════════════════════════════════════════════════

    // Build composite constant buffer
    UnderwaterCompositeCBData cb = {};
    std::memcpy(cb.invViewProj, scene.InvViewProjMatrix(), sizeof(float) * 16);

    cb.absorptionR      = m_absorptionR;
    cb.absorptionG      = m_absorptionG;
    cb.absorptionB      = m_absorptionB;
    cb.absorptionPad    = 0.0f;

    // Underwater fog: dark blue-green
    cb.fogColorR        = 0.02f;
    cb.fogColorG        = 0.06f;
    cb.fogColorB        = 0.10f;
    cb.fogDensity       = m_fogDensity;

    // Tyndall scattering: pale aqua
    cb.tyndallColorR    = 0.15f;
    cb.tyndallColorG    = 0.25f;
    cb.tyndallColorB    = 0.30f;
    cb.tyndallDensity   = m_tyndallDensity;

    cb.screenDimsX      = static_cast<float>(m_screenW);
    cb.screenDimsY      = static_cast<float>(m_screenH);
    cb.nearZ            = nearZ;
    cb.farZ             = farZ;
    cb.time             = m_totalTime;
    cb.waterSurfaceZ    = m_waterSurfaceZ;
    cb.submersionDepth  = m_submersionDepth;
    cb.causticIntensity = m_causticIntensity;
    cb.godRayIntensity  = m_godRayIntensity;
    cb.waveIntensity    = m_waveIntensity;
    cb.wetLensTimer     = m_waterSurfaceTime;

    cb.cameraPosX       = scene.CameraPosX();
    cb.cameraPosY       = scene.CameraPosY();
    cb.cameraPosZ       = scene.CameraPosZ();

    cb.sunDirX          = scene.SunDirection()[0];
    cb.sunDirY          = scene.SunDirection()[1];
    cb.sunDirZ          = scene.SunDirection()[2];

    // Camera forward = 3rd row of view matrix (row-major)
    const float* view = scene.ViewMatrix();
    cb.cameraFwdX       = view[8];
    cb.cameraFwdY       = view[9];
    cb.cameraFwdZ       = view[10];

    // Get output RTV: mid-frame uses game's RTV, Present creates from scene tex
    ID3D11RenderTargetView* backRTV = nullptr;
    bool ownBackRTV = false;
    if (ctx.gameSceneRTV) {
        backRTV = ctx.gameSceneRTV;
        backRTV->AddRef();
        ownBackRTV = true;
    } else {
        D3D11_TEXTURE2D_DESC texDesc;
        backTex->GetDesc(&texDesc);
        D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format        = texDesc.Format;
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        m_device->CreateRenderTargetView(backTex, &rtvDesc, &backRTV);
        ownBackRTV = (backRTV != nullptr);
    }
    if (ownBackTex) backTex->Release();

    if (!backRTV) return;

    // Build SRV array:
    //   t0 = scene color (backbuffer copy)
    //   t1 = depth
    //   t2 = caustic (quarter-res)
    //   t3 = god rays (quarter-res)
    //   t4 = wave offset (full-res)
    ID3D11ShaderResourceView* srvs[5] = {
        m_bbCopySRV,     // t0
        depthSRV,        // t1
        m_causticSRV,    // t2
        m_godRaySRV,     // t3
        m_waveSRV,       // t4
    };

    ID3D11SamplerState* samplers[2] = {
        m_pointSampler,   // s0
        m_linearSampler,  // s1
    };

    // Bind pre-computed linearized depth at t31 for composite PS
    ctx.context->PSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &linearDepthSRV);

    RenderPassManager::Get().Execute({
        .passID       = m_compositePass,
        .rtv          = backRTV,
        .srvs         = srvs,
        .srvCount     = 5,
        .samplers     = samplers,
        .samplerCount = 2,
        .cbData       = &cb,
        .cbSize       = sizeof(cb),
    });

    // Unbind linearized depth t31 from PS
    {
        ID3D11ShaderResourceView* nullSRV = nullptr;
        ctx.context->PSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &nullSRV);
    }

    if (ownBackRTV && backRTV) backRTV->Release();

    m_frameIndex++;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Shutdown
// ═══════════════════════════════════════════════════════════════════════════

void UnderwaterRenderer::Shutdown()
{
    if (!m_initialized) return;

    if (m_pipelineHandle) {
        RenderPipeline::Get().RemovePass(m_pipelineHandle);
        m_pipelineHandle = 0;
    }

    ReleaseResources();
    m_compositePass = 0;
    m_initialized   = false;

    SKSE::log::info("UnderwaterRenderer: shut down");
}


void UnderwaterRenderer::ReleaseResources()
{
    auto SafeRelease = [](auto*& ptr) {
        if (ptr) { ptr->Release(); ptr = nullptr; }
    };

    // Compute shaders
    SafeRelease(m_causticCS);
    SafeRelease(m_godRayCS);
    SafeRelease(m_waveCS);

    // Pass 1: Caustics
    SafeRelease(m_causticTex);
    SafeRelease(m_causticSRV);
    SafeRelease(m_causticUAV);
    SafeRelease(m_causticCB);

    // Pass 2: God Rays
    SafeRelease(m_godRayTex);
    SafeRelease(m_godRaySRV);
    SafeRelease(m_godRayUAV);
    SafeRelease(m_godRayCB);

    // Pass 3: Wave Distortion
    SafeRelease(m_waveTex);
    SafeRelease(m_waveSRV);
    SafeRelease(m_waveUAV);
    SafeRelease(m_waveCB);

    // Shared resources
    SafeRelease(m_bbCopyTex);
    SafeRelease(m_bbCopySRV);
    SafeRelease(m_pointSampler);
    SafeRelease(m_linearSampler);
}

} // namespace SB
