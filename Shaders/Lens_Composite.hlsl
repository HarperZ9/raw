// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Pass 5: Final Lens Composite — Distortion + Chromatic Aberration + Vignette
//
// Combines all lens effect layers (ghosts, starburst, anamorphic streak) with
// the original scene, applying physical lens distortion and sensor simulation:
//
//   1. Brown-Conrady barrel/pincushion distortion:
//      r2 = dot(uv - 0.5, uv - 0.5) * 4
//      distortedUV = (uv - 0.5) * (1 + k1*r2 + k2*r2^2) + 0.5
//      Reference: Brown, "Decentering Distortion of Lenses" (1966);
//                 Conrady, "Applied Optics and Optical Design" (1929).
//
//   2. Chromatic aberration via per-channel radial UV offset:
//      Red and blue channels are offset outward/inward from center.
//      Reference: Cauchy dispersion equation n(lambda) = A + B/lambda^2
//
//   3. cos^4(theta) natural vignette (illumination falloff):
//      Reference: Any optics textbook — image irradiance falls as cos^4
//      of the chief ray angle from the optical axis.
//
//   4. Sensor clip: per-channel saturation at different thresholds,
//      modeling CMOS photosite well capacity differences.

cbuffer CompositeCB : register(b0)
{
    float2 ScreenDims;
    float  DistortionK1;
    float  DistortionK2;
    float  CAStrength;
    float  VignetteStrength;
    float  GhostBlend;       // Ghost overall blend factor
    float  StarburstBlend;   // Starburst+veil blend factor
    float  FlareBlend;       // Anamorphic flare blend factor
    float  DirtIntensity;    // Lens dirt strength
    float  SensorClipR;      // Per-channel clip (R)
    float  SensorClipG;      // Per-channel clip (G, clips first)
    float  SensorClipB;      // Per-channel clip (B)
    float  CauchyB;          // Cauchy dispersion coefficient B
    float  Time;             // For procedural dirt animation
    float  pad0;
}

Texture2D<float4> SceneTex     : register(t0); // Backbuffer copy (original scene)
Texture2D<float4> GhostTex     : register(t1); // Quarter-res ghost accumulation
Texture2D<float4> StarburstTex : register(t2); // Quarter-res starburst+veil
Texture2D<float4> FlareTex     : register(t3); // Half-res anamorphic flare
SamplerState      LinSamp      : register(s0);

// VS output struct -- must match the RenderPassManager fullscreen VS
struct VSOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

// Brown-Conrady radial distortion model.
// Maps an undistorted UV to a distorted UV based on radial distance from center.
//   r2 = squared radial distance from center (normalized so corners ~ 1)
//   distortion = 1 + k1 * r2 + k2 * r2^2
float2 ApplyDistortion(float2 uv, float k1, float k2)
{
    float2 centered = uv - 0.5;
    float r2 = dot(centered, centered) * 4.0;  // *4 normalizes so corner r2 ~ 1
    float distortion = 1.0 + k1 * r2 + k2 * r2 * r2;
    return centered * distortion + 0.5;
}

float4 main(VSOut input) : SV_Target
{
    float2 uv = input.uv;

    // ── 1. Brown-Conrady barrel/pincushion distortion ────────────────
    float2 distortedUV = ApplyDistortion(uv, DistortionK1, DistortionK2);

    // ── 2. Chromatic aberration (per-channel radial UV offset) ───────
    // Red refracts less (offset outward), blue refracts more (offset inward).
    // The offset direction is radially away from screen center.
    float2 caOffset = (distortedUV - 0.5) * CAStrength;
    float2 uvR = distortedUV + caOffset;
    float2 uvG = distortedUV;
    float2 uvB = distortedUV - caOffset;

    // Sample scene with per-channel distortion
    float sceneR = SceneTex.SampleLevel(LinSamp, uvR, 0).r;
    float sceneG = SceneTex.SampleLevel(LinSamp, uvG, 0).g;
    float sceneB = SceneTex.SampleLevel(LinSamp, uvB, 0).b;
    float3 scene = float3(sceneR, sceneG, sceneB);

    // ── 3. cos^4(theta) optical vignette ─────────────────────────────
    // The cos^4 law describes natural illumination falloff: image irradiance
    // at angle theta from the optical axis is proportional to cos^4(theta).
    // We approximate theta from the normalized radial distance.
    float2 vigUV = distortedUV - 0.5;
    float r2 = dot(vigUV, vigUV);
    float vignette = pow(saturate(1.0 - r2 * VignetteStrength), 2.0);

    // ── 4. Sample lens effect layers ─────────────────────────────────
    float3 ghosts   = GhostTex.SampleLevel(LinSamp, uv, 0).rgb;
    float3 star     = StarburstTex.SampleLevel(LinSamp, uv, 0).rgb;
    float3 flare    = FlareTex.SampleLevel(LinSamp, uv, 0).rgb;

    // Combine lens effects with per-layer blend factors
    float3 lensEffects = ghosts * GhostBlend
                       + star   * StarburstBlend
                       + flare  * FlareBlend;

    // ── 5. Final composite ───────────────────────────────────────────
    // Scene with distortion and vignette + additive lens effects
    float3 result = scene * vignette + lensEffects;

    // ── 6. Sensor clip (per-channel saturation) ──────────────────────
    // CMOS sensors have per-channel well capacity limits. Green typically
    // clips first due to Bayer filter design (2x green photosites).
    result.r = min(result.r, SensorClipR);
    result.g = min(result.g, SensorClipG);
    result.b = min(result.b, SensorClipB);

    return float4(result, 1.0);
}
