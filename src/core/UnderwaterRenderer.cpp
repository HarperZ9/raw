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
//  Replaces ENB enbunderwater.fx entirely.
//=============================================================================

#include "UnderwaterRenderer.h"
#include "HiZPyramid.h"
#include "SRVInjector.h"
#include "SharedGPUResources.h"

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
// Caustics CS — 3-octave trochoidal wave pattern, quarter-resolution.
//
// Trochoidal (trochoid of a circle) caustics are the bright network of
// light lines you see on the bottom of a swimming pool.  We approximate
// them by summing sine waves at multiple scales, then sharpening.
// Output fades with linear depth to zero below the photic zone.

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
};

Texture2D<float> DepthTex : register(t0);

RWTexture2D<float> CausticOutput : register(u0);

// ── Reconstruct world position from depth ───────────────────────────────
float3 WorldFromDepth(float2 uv, float rawDepth)
{
    float4 clipPos = float4(uv * 2.0 - 1.0, rawDepth, 1.0);
    clipPos.y = -clipPos.y;  // D3D UV convention
    float4 worldPos = mul(clipPos, InvViewProj);
    return worldPos.xyz / worldPos.w;
}

// ── Trochoidal caustic function ─────────────────────────────────────────
// Each octave is a directional sine wave.  The caustic pattern emerges
// from the interference of multiple waves with different frequencies.
float TrochoidalOctave(float2 worldXZ, float time, float2 dir, float scale, float speed)
{
    float phase = dot(worldXZ * scale, dir) + time * speed;
    // Sharp caustic lines via abs(sin) raised to a power
    float wave = abs(sin(phase));
    wave = pow(wave, 0.5);  // sharpen
    return wave;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= (uint)ScreenDims.x || DTid.y >= (uint)ScreenDims.y)
        return;

    int2 coord = int2(DTid.xy);
    float2 uv = (float2(coord) + 0.5) / ScreenDims;

    // Sample depth at full-res coordinate (nearest)
    int2 fullCoord = int2(uv * FullScreenDims);
    fullCoord = clamp(fullCoord, 0, int2(FullScreenDims) - 1);
    float rawDepth = DepthTex.Load(int3(fullCoord, 0));

    // Skip sky
    if (rawDepth < 0.0001)
    {
        CausticOutput[coord] = 0.0;
        return;
    }

    float3 worldPos = WorldFromDepth(uv, rawDepth);

    // Depth below water surface (positive = deeper)
    float depthBelowSurface = WaterSurfaceZ - worldPos.z;

    // No caustics above water or beyond photic zone
    if (depthBelowSurface < 0.0 || depthBelowSurface > MaxCausticDepth)
    {
        CausticOutput[coord] = 0.0;
        return;
    }

    float2 xz = worldPos.xy;

    // ── 3-octave trochoidal caustics ────────────────────────────────────
    // Each octave: different direction, scale, speed, and amplitude
    float c = 0.0;

    // Octave 1: large, slow swell
    c += TrochoidalOctave(xz, Time, float2(0.7, 0.7), 0.08, 0.6) * 0.5;

    // Octave 2: medium ripple
    c += TrochoidalOctave(xz, Time, float2(-0.5, 0.85), 0.15, 1.1) * 0.3;

    // Octave 3: fine detail
    c += TrochoidalOctave(xz, Time, float2(0.9, -0.4), 0.3, 1.8) * 0.2;

    // Interference creates bright caustic lines where waves constructively overlap
    c = saturate(c);

    // Sharpen the final pattern (caustics are high-contrast)
    c = smoothstep(0.3, 0.8, c);

    // Depth-stratified visibility: strong near surface, fades linearly
    float depthFade = 1.0 - saturate(depthBelowSurface / MaxCausticDepth);
    depthFade = depthFade * depthFade;  // quadratic falloff

    CausticOutput[coord] = c * CausticIntensity * depthFade;
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
// God Rays CS — Underwater volumetric light shafts, quarter-resolution.
//
// For each pixel, march a ray toward the sun's screen-space position.
// At each step, test if the sample is unoccluded (depth near sky/water
// surface).  Accumulate luminance with exponential falloff.
// The result is an additive light shaft buffer.

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
};

Texture2D<float>  DepthTex  : register(t0);
Texture2D<float4> SceneTex  : register(t1);   // Backbuffer copy (bright pixels)
Texture2D<float>  LinearDepth : register(t31); // pre-computed linearized depth

RWTexture2D<float> GodRayOutput : register(u0);

float Luminance(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= (uint)ScreenDims.x || DTid.y >= (uint)ScreenDims.y)
        return;

    int2 coord = int2(DTid.xy);
    float2 uv = (float2(coord) + 0.5) / ScreenDims;

    // Direction from this pixel toward the sun
    float2 delta = SunScreenPos - uv;
    float rayLen = length(delta);

    // Clamp ray length to prevent marching across entire screen
    float maxRayLen = 0.5;
    float stepScale = min(rayLen, maxRayLen) / float(NumSamples);
    float2 stepDir = normalize(delta) * stepScale * Density;

    float illumination = 0.0;
    float decayFactor = 1.0;

    float2 sampleUV = uv;

    for (int i = 0; i < NumSamples; i++)
    {
        sampleUV += stepDir;

        // Bounds check
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0)
            break;

        int2 sampleCoord = int2(sampleUV * FullScreenDims);
        sampleCoord = clamp(sampleCoord, 0, int2(FullScreenDims) - 1);

        float rawDepth = DepthTex.Load(int3(sampleCoord, 0));

        // Accumulate if the sample is near sky (reversed-Z: sky ~ 0) or
        // is shallow water (not blocked by geometry).  Light passes through
        // water near the surface.
        float linearZ = LinearDepth.Load(int3(sampleCoord, 0));
        float depthWeight = saturate(1.0 - rawDepth * 5.0);  // sky-like = more light

        // Also sample scene luminance as source brightness
        float3 sceneCol = SceneTex.Load(int3(sampleCoord, 0)).rgb;
        float luma = Luminance(max(sceneCol, 0.0));

        // Combine: bright unoccluded areas contribute to god rays
        float contribution = max(depthWeight, luma * 0.3);

        illumination += contribution * decayFactor;
        decayFactor *= Decay;
    }

    illumination = saturate(illumination * Exposure / float(NumSamples));

    GodRayOutput[coord] = illumination * Intensity;
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
// Wave Distortion CS — 4-octave Gerstner wave UV displacement, full-resolution.
//
// Gerstner waves simulate realistic ocean surface motion.  Each wave is
// parameterized by direction, frequency, amplitude, and steepness (Q).
// The displacement creates the characteristic rolling, tilting motion
// that makes the underwater view feel alive.

cbuffer WaveCB : register(b0)
{
    float2 ScreenDims;
    float  Time;
    float  WaveIntensity;     // Global amplitude scale
    float  NearZ;
    float  FarZ;
    float  SubmersionDepth;   // Depth below surface (waves diminish with depth)
    float  pad0;
};

Texture2D<float> DepthTex : register(t0);
Texture2D<float> LinearDepth : register(t31); // pre-computed linearized depth

RWTexture2D<float2> WaveOutput : register(u0);

// ── Gerstner wave displacement ──────────────────────────────────────────
// Returns the XY UV offset from a single Gerstner wave.
//
//   Q = steepness (0..1), A = amplitude, w = frequency, phi = speed
//   dir = normalized wave direction
//
// The classical Gerstner formula displaces a point (x,y) as:
//   x' = x - Q * A * Dx * sin(dot(D, P) * w + t * phi)
//   y' = y - Q * A * Dy * sin(dot(D, P) * w + t * phi)
//
// For screen-space UV distortion we just compute the sin/cos offset.
float2 GerstnerWave(float2 uv, float time, float2 dir, float freq, float amp, float steepness)
{
    float phase = dot(uv, dir) * freq + time;
    float s = sin(phase);
    float c = cos(phase);

    float2 offset;
    offset.x = steepness * amp * dir.x * c;
    offset.y = steepness * amp * dir.y * c;
    return offset;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= (uint)ScreenDims.x || DTid.y >= (uint)ScreenDims.y)
        return;

    int2 coord = int2(DTid.xy);
    float2 uv = (float2(coord) + 0.5) / ScreenDims;

    // Check depth — reduce distortion for very distant geometry
    float linearZ = LinearDepth.Load(int3(coord, 0));
    float depthAtten = saturate(1.0 - linearZ / 5000.0);  // fade at long range

    // ── 4-octave Gerstner waves ─────────────────────────────────────────
    // Each octave: (direction, frequency, amplitude, steepness)
    // Directions chosen for natural multi-directional interference.
    float2 totalOffset = float2(0.0, 0.0);

    // Octave 1: broad swell
    totalOffset += GerstnerWave(uv, Time * 0.8,
        normalize(float2(1.0, 0.3)), 8.0, 0.008, 0.7);

    // Octave 2: cross-wave
    totalOffset += GerstnerWave(uv, Time * 1.1,
        normalize(float2(-0.5, 1.0)), 12.0, 0.005, 0.6);

    // Octave 3: chop
    totalOffset += GerstnerWave(uv, Time * 1.5,
        normalize(float2(0.7, -0.7)), 20.0, 0.003, 0.5);

    // Octave 4: fine ripple
    totalOffset += GerstnerWave(uv, Time * 2.2,
        normalize(float2(-0.3, -0.9)), 35.0, 0.0015, 0.4);

    // Depth attenuation: waves diminish the deeper you are
    float depthDamp = saturate(1.0 - SubmersionDepth / 500.0);
    depthDamp = depthDamp * depthDamp;

    totalOffset *= WaveIntensity * depthAtten * depthDamp;

    WaveOutput[coord] = totalOffset;
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

Texture2D<float4> SceneColor    : register(t0);  // Backbuffer copy
Texture2D<float>  DepthTex      : register(t1);  // Game depth
Texture2D<float>  CausticTex    : register(t2);  // Quarter-res caustic map
Texture2D<float>  GodRayTex     : register(t3);  // Quarter-res god ray buffer
Texture2D<float2> WaveOffsetTex : register(t4);  // Full-res UV offset
Texture2D<float>  LinearDepth   : register(t31); // pre-computed linearized depth

SamplerState PointSampler  : register(s0);
SamplerState LinearSampler : register(s1);

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
};

struct PSInput
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

// ── Constants ───────────────────────────────────────────────────────────
static const float PI = 3.14159265359;
static const float SNELL_CRITICAL_ANGLE = 0.8480;   // cos(48.6 degrees) — water IOR 1.333
static const float WATER_IOR = 1.333;

// ── Utility ─────────────────────────────────────────────────────────────
float3 WorldFromDepth(float2 uv, float rawDepth)
{
    float4 clipPos = float4(uv * 2.0 - 1.0, rawDepth, 1.0);
    clipPos.y = -clipPos.y;
    float4 worldPos = mul(clipPos, InvViewProj);
    return worldPos.xyz / worldPos.w;
}

float Luminance(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

// ── Hash for wet lens droplets ──────────────────────────────────────────
float Hash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float2 Hash22(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return frac(float2((p3.x + p3.y) * p3.z, (p3.x + p3.z) * p3.y));
}

// ── Wet lens droplet refraction ─────────────────────────────────────────
// Simulates water droplets clinging to the "camera lens" when near the
// surface.  Each droplet is a circular region with a refractive offset.
float2 WetLensOffset(float2 uv, float timer)
{
    // Timer: 0 = just surfaced (full droplets), 2.0+ = fully evaporated
    float dryness = saturate(timer / 2.0);
    if (dryness >= 1.0)
        return float2(0.0, 0.0);

    float2 totalOffset = float2(0.0, 0.0);
    float wetness = 1.0 - dryness;

    // Grid of potential droplet positions
    float dropletScale = 8.0;  // grid cells across screen
    float2 gridUV = uv * dropletScale;
    int2 gridCell = int2(floor(gridUV));

    // Check 3x3 neighborhood for overlapping droplets
    for (int dy = -1; dy <= 1; dy++)
    {
        for (int dx = -1; dx <= 1; dx++)
        {
            int2 cell = gridCell + int2(dx, dy);
            float2 cellHash = Hash22(float2(cell));

            // Random droplet center within cell
            float2 dropCenter = (float2(cell) + cellHash) / dropletScale;

            // Random droplet radius (varies by wetness)
            float dropRadius = (0.02 + cellHash.x * 0.04) * wetness;

            // Distance from pixel to droplet center
            float2 diff = uv - dropCenter;
            float dist = length(diff);

            if (dist < dropRadius)
            {
                // Surface tension refraction: radial distortion
                float t = dist / dropRadius;
                float strength = (1.0 - t * t) * 0.015 * wetness;

                // Gravity: droplets elongate downward over time
                float gravity = dryness * 0.01;
                diff.y -= gravity;

                totalOffset += normalize(diff + 0.0001) * strength;
            }
        }
    }

    return totalOffset;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Main composite
// ═══════════════════════════════════════════════════════════════════════════

float4 main(PSInput input) : SV_Target
{
    float2 uv = input.texcoord;

    // ── Wave distortion (UV offset) ────────────────────────────────────
    float2 waveOffset = WaveOffsetTex.Sample(PointSampler, uv);
    float2 distortedUV = uv + waveOffset * WaveIntensity;

    // ── Wet lens transition (near-surface droplet effect) ──────────────
    float2 wetOffset = WetLensOffset(uv, WetLensTimer);
    distortedUV += wetOffset;

    // Clamp to valid UV range
    distortedUV = saturate(distortedUV);

    // ── Sample scene with distorted UVs ────────────────────────────────
    float3 color = SceneColor.Sample(LinearSampler, distortedUV).rgb;

    // Sanitize
    if (any(isnan(color)) || any(isinf(color)))
        color = float3(0.0, 0.0, 0.0);

    // ── Sample depth for this pixel ────────────────────────────────────
    float rawDepth = DepthTex.Sample(PointSampler, uv).r;
    float linearDepth = LinearDepth.Load(int3(int2(input.position.xy), 0));
    float3 worldPos = WorldFromDepth(uv, rawDepth);

    // Distance below water surface for this pixel
    float pixelWaterDepth = max(WaterSurfaceZ - worldPos.z, 0.0);


    // ── Beer-Lambert absorption ────────────────────────────────────────
    // Light intensity falls off exponentially per channel.
    // Red is absorbed first (ocean = blue-green), then green, blue last.
    //   I(λ) = I₀ · exp(-α(λ) · d)
    float3 absorption = exp(-AbsorptionCoeff.xyz * pixelWaterDepth);
    color *= absorption;


    // ── Photic zone grading ────────────────────────────────────────────
    // Near surface: warm, bright (sunlight-dominated)
    // At depth: cold blue-green, desaturated
    float depthRatio = saturate(pixelWaterDepth / 300.0);  // 0 = surface, 1 = deep

    // Warm → cold shift
    float3 warmTint  = float3(1.0, 0.95, 0.85);   // sunlit surface
    float3 coldTint  = float3(0.3, 0.55, 0.7);     // twilight zone
    float3 zoneTint  = lerp(warmTint, coldTint, depthRatio);
    color *= zoneTint;

    // Desaturation at depth
    float luma = Luminance(color);
    float desatAmount = depthRatio * 0.6;
    color = lerp(color, float3(luma, luma, luma), desatAmount);


    // ── Caustic overlay (additive) ─────────────────────────────────────
    {
        float caustic = CausticTex.Sample(LinearSampler, uv).r;
        // Caustics illuminate the scene additively, tinted by water color
        float3 causticColor = float3(0.7, 0.9, 1.0) * caustic * CausticIntensity;
        // Only apply caustics to geometry (not sky)
        causticColor *= (rawDepth > 0.0001) ? 1.0 : 0.0;
        color += causticColor;
    }


    // ── God ray blend (additive volumetric light) ──────────────────────
    {
        float godRay = GodRayTex.Sample(LinearSampler, uv).r;
        // Tint god rays warm (sunlight filtering through water)
        float3 rayColor = float3(0.9, 0.85, 0.6) * godRay * GodRayIntensity;
        color += rayColor;
    }


    // ── Tyndall scattering ─────────────────────────────────────────────
    // Forward-scatter haze: particles in water scatter light toward the
    // viewer.  Increases with distance and depth.
    {
        float scatterDist = linearDepth * TyndallColor.w;
        float scatter = 1.0 - exp(-scatterDist);
        scatter *= saturate(pixelWaterDepth / 50.0);  // stronger with depth
        color = lerp(color, TyndallColor.xyz, scatter);
    }


    // ── Depth fog ──────────────────────────────────────────────────────
    // Exponential distance fog with configurable density and color.
    {
        float fog = 1.0 - exp(-FogColor.w * linearDepth);
        color = lerp(color, FogColor.xyz, fog);
    }


    // ── Snell's Window ─────────────────────────────────────────────────
    // When looking upward from underwater, total internal reflection occurs
    // beyond the critical angle (~48.6 degrees from vertical for water).
    // Inside the cone you see the sky; outside you see a mirror reflection
    // of the underwater scene.
    {
        // View direction in world space
        float3 camFwd = normalize(CameraForward.xyz);

        // Angle between camera forward and straight up
        float cosAngle = dot(camFwd, float3(0.0, 0.0, 1.0));

        // UV distance from screen center (for vignette-style mask)
        float2 centerDist = uv - float2(0.5, 0.5);
        float radialDist = length(centerDist) * 2.0;  // 0 = center, 1 = edge

        // The Snell's window is a circle on the water surface above the viewer.
        // Angular radius = arcsin(1/n) ≈ 48.6° for water.
        // In screen space, the edge of the window depends on view angle.
        float windowRadius = 0.7;  // Approximate screen-space radius of Snell's circle

        // When looking up, apply total internal reflection outside the window
        float lookUpFactor = saturate(-cosAngle);  // 1 when looking straight up

        if (lookUpFactor > 0.1)
        {
            float edgeDist = radialDist / windowRadius;

            // Outside the critical angle: total internal reflection
            if (edgeDist > 1.0)
            {
                // Reflection: darken and tint with water color (simulates reflected
                // underwater scene).  In reality this would be a mirror of the scene
                // below, but we approximate with a color tint.
                float reflStrength = saturate((edgeDist - 1.0) * 3.0) * lookUpFactor;
                float3 reflColor = color * float3(0.2, 0.4, 0.5);  // underwater reflection tint
                color = lerp(color, reflColor, reflStrength);
            }

            // Fresnel falloff at the boundary (smooth transition)
            float fresnelEdge = saturate(1.0 - abs(edgeDist - 1.0) * 5.0) * lookUpFactor;
            float3 fresnelColor = float3(0.6, 0.8, 0.9);  // bright rim at boundary
            color = lerp(color, fresnelColor, fresnelEdge * 0.3);
        }
    }


    // Final safety clamp
    color = max(color, 0.0);

    return float4(color, 1.0);
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
        ID3DBlob* blob = nullptr;
        ID3DBlob* err  = nullptr;
        HRESULT hr = D3DCompile(source, strlen(source), name,
                                nullptr, nullptr, "main", "cs_5_0",
                                flags, 0, &blob, &err);
        if (FAILED(hr)) {
            if (err) {
                SKSE::log::error("UnderwaterRenderer: {} compile failed: {}", name,
                                 static_cast<const char*>(err->GetBufferPointer()));
                err->Release();
            }
            return false;
        }
        if (err) err->Release();

        hr = m_device->CreateComputeShader(blob->GetBufferPointer(),
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

    if (!CompileCS("UnderwaterCaustics",  kCausticCS, &m_causticCS)) return false;
    if (!CompileCS("UnderwaterGodRays",   kGodRayCS,  &m_godRayCS))  return false;
    if (!CompileCS("UnderwaterWaves",     kWaveCS,    &m_waveCS))    return false;

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
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width      = w;
        desc.Height     = h;
        desc.MipLevels  = 1;
        desc.ArraySize  = 1;
        desc.Format     = format;
        desc.SampleDesc = {1, 0};
        desc.Usage      = D3D11_USAGE_DEFAULT;
        desc.BindFlags  = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

        hr = m_device->CreateTexture2D(&desc, nullptr, outTex);
        if (FAILED(hr)) {
            SKSE::log::error("UnderwaterRenderer: failed to create {} texture (0x{:X})",
                             name, static_cast<uint32_t>(hr));
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = format;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels       = 1;
        hr = m_device->CreateShaderResourceView(*outTex, &srvDesc, outSRV);
        if (FAILED(hr)) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format             = format;
        uavDesc.ViewDimension      = D3D11_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;
        hr = m_device->CreateUnorderedAccessView(*outTex, &uavDesc, outUAV);
        if (FAILED(hr)) return false;

        return true;
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
    {
        D3D11_BUFFER_DESC desc = {};
        desc.Usage          = D3D11_USAGE_DYNAMIC;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;

        desc.ByteWidth = sizeof(CausticCBData);
        hr = m_device->CreateBuffer(&desc, nullptr, &m_causticCB);
        if (FAILED(hr)) return false;

        desc.ByteWidth = sizeof(GodRayCBData);
        hr = m_device->CreateBuffer(&desc, nullptr, &m_godRayCB);
        if (FAILED(hr)) return false;

        desc.ByteWidth = sizeof(WaveCBData);
        hr = m_device->CreateBuffer(&desc, nullptr, &m_waveCB);
        if (FAILED(hr)) return false;
    }

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

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx.context->Map(m_causticCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            ctx.context->Unmap(m_causticCB, 0);
        }

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

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx.context->Map(m_godRayCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            ctx.context->Unmap(m_godRayCB, 0);
        }

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

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx.context->Map(m_waveCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            ctx.context->Unmap(m_waveCB, 0);
        }

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
