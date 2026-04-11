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
    return NearZ * FarZ / (NearZ + d * (FarZ - NearZ));
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
