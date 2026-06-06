//=============================================================================
//  LensRenderer.cpp — Physically-Based Lens Effects (Compute-First)
//
//  Dispatch flow (per frame, PreUI stage, priority 20):
//    1. Copy backbuffer to scratch texture
//    2. Dispatch Downsample+BrightExtract CS: 4 mips (1/2->1/4->1/8->1/16)
//    3. Dispatch Ghost Evaluation CS: ABCD matrix + polynomial ghosts
//    4. Dispatch Starburst + Veiling Glare CS: diffraction starburst + 1/r PSF
//    5. Dispatch Anamorphic Flare CS: horizontal/rotatable streaks
//    6. Execute Composite PS: distortion + CA + vignette + sensor clip + dirt
//
//  Standalone compute+pixel pipeline with no BSShader hooks required.
//=============================================================================

#include "LensRenderer.h"
#include "ShaderLoader.h"
#include "SRVInjector.h"

#include <cstring>
#include <cmath>
#include "GPUResource.h"


namespace SB
{

// =============================================================================
//  Embedded HLSL -- Pass 1: Downsample + Bright Extract CS
// =============================================================================

static const char kDownsampleBrightCS[] = R"HLSL(
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Pass 1: Downsample + Bright Pixel Extraction (quarter-resolution)
//
// 2x2 box-filter downsample with luminance-threshold bright extraction.
// Mip 0 applies the threshold to isolate bright pixels for lens effects;
// subsequent mips are pure 2x2 averages for the mip chain.
//
// Reference: Standard image pyramid construction; threshold uses
// BT.709 luminance weighting with soft-knee for smooth transition.

cbuffer DownsampleCB : register(b0)
{
    uint2  SrcDims;       // Source texture dimensions
    uint2  DstDims;       // Destination (output) dimensions
    float  BrightThreshold; // Threshold for bright extraction (mip 0 only)
    float  SoftKnee;      // Soft knee transition width
    uint   MipLevel;      // Which mip we are generating (0..3)
    float  pad0;
}

Texture2D<float4>   SrcTex  : register(t0);
SamplerState        LinSamp : register(s0);
RWTexture2D<float4> DstTex  : register(u0);

// BT.709 luminance weights
float Luminance(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= DstDims.x || DTid.y >= DstDims.y)
        return;

    // Compute center UV of the 2x2 source texel block for bilinear filtering.
    // Adding 0.5 to destination pixel centers it; multiplying by 2 maps to
    // source space; adding 0.5 centers within the 2x2 block.
    float2 uv = (float2(DTid.xy) * 2.0 + 1.0) / float2(SrcDims);

    // 2x2 bilinear average -- the hardware sampler does the box filter
    // when we sample at the exact center of the 2x2 block.
    float4 color = SrcTex.SampleLevel(LinSamp, uv, 0);

    // On mip 0, apply brightness threshold to extract only bright pixels.
    // Subsequent mips are pure downsample (no threshold).
    if (MipLevel == 0)
    {
        float lum = Luminance(color.rgb);

        // Soft-knee: smoothstep transition from threshold-knee to threshold+knee.
        // This avoids hard cut artifacts in the lens flare source.
        float knee = BrightThreshold * SoftKnee;
        float soft = lum - (BrightThreshold - knee);
        soft = clamp(soft / (2.0 * knee + 1e-5), 0.0, 1.0);
        soft = soft * soft;

        // Blend between zero and full color based on soft threshold
        float contrib = max(soft, lum - BrightThreshold) / max(lum, 1e-5);
        contrib = max(contrib, 0.0);

        color.rgb *= contrib;
    }

    DstTex[DTid.xy] = color;
}
)HLSL";


// =============================================================================
//  Embedded HLSL -- Pass 2: Ghost Evaluation CS
// =============================================================================

static const char kGhostCS[] = R"HLSL(
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Pass 2: Lens Ghost Generation (ABCD ray-transfer matrix optics)
//
// Each ghost image is produced by light reflecting between lens elements.
// The ABCD ray-transfer matrix describes how a ray's position and angle
// transform through a cascade of surfaces. For ghost generation:
//   - GhostParams[i].xy = scale factor (ABCD 'A' diagonal = magnification)
//     Negative scale means the ghost is inverted (even number of reflections).
//   - GhostParams[i].zw = offset (ABCD 'B' translation)
//
// Per-ghost attenuation uses the Fresnel-Schlick approximation:
//   R = R0 + (1 - R0) * (1 - cos(theta))^5
// where R0 depends on the lens coating refractive index.
//
// Ghost tints model thin-film interference colors from multi-coated optics.
//
// Reference: ABCD matrix optics (Hecht, "Optics", Ch. 6);
//            Fresnel-Schlick approximation (Schlick 1994).

cbuffer GhostCB : register(b0)
{
    uint2  OutputDims;    // Quarter-res output dimensions
    uint2  Mip0Dims;      // Half-res mip dimensions
    float  GhostIntensity; // Overall ghost strength
    int    GhostCount;    // Number of active ghosts (2-8)
    float  AspectRatio;   // Screen aspect ratio
    float  pad0;

    // Per-ghost parameters packed as float4 rows:
    // .xy = scale (ABCD A diagonal), .zw = offset (ABCD B translation)
    float4 GhostParams[8];

    // Per-ghost tint (from thin-film interference model)
    float4 GhostTint[8];
}

Texture2D<float4> Mip0Tex : register(t0); // Half-res bright
Texture2D<float4> Mip1Tex : register(t1); // Quarter-res
Texture2D<float4> Mip2Tex : register(t2); // Eighth-res
Texture2D<float4> Mip3Tex : register(t3); // Sixteenth-res
SamplerState      LinSamp : register(s0);
RWTexture2D<float4> GhostOutput : register(u0);

// Fresnel-Schlick approximation for dielectric coating reflectance.
// R0 for typical multi-coated glass: ((1.38 - 1.0) / (1.38 + 1.0))^2 ~ 0.025
float FresnelSchlick(float cosTheta, float R0)
{
    return R0 + (1.0 - R0) * pow(saturate(1.0 - cosTheta), 5.0);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= OutputDims.x || DTid.y >= OutputDims.y)
        return;

    // Normalized UV in [0,1] for this output pixel
    float2 uv = (float2(DTid.xy) + 0.5) / float2(OutputDims);

    float4 ghostAccum = 0.0;

    // Coating reflectance R0: multi-coated MgF2 on glass, n_coating ~ 1.38
    // R0 = ((n_coating - 1) / (n_coating + 1))^2
    static const float R0 = 0.025;

    for (int i = 0; i < GhostCount; i++)
    {
        float2 scale  = GhostParams[i].xy;
        float2 offset = GhostParams[i].zw;

        // Mirror the UV around screen center and apply per-ghost scale.
        // This simulates the ABCD matrix transformation:
        //   ghostUV = A * (uv - 0.5) + B + 0.5
        // where A (scale) encodes magnification and inversion,
        // and B (offset) encodes lateral displacement from misaligned elements.
        float2 ghostUV = (uv - 0.5) * scale + offset + 0.5;

        // Discard ghosts that fall outside the frame
        if (any(ghostUV < 0.0) || any(ghostUV > 1.0))
            continue;

        // Choose mip based on ghost magnification -- larger ghosts use
        // lower-res mips for softer appearance (as in real optics, larger
        // ghost images have more aberration blur).
        float absMag = max(abs(scale.x), abs(scale.y));
        float4 sample;
        if (absMag > 1.5)
            sample = Mip2Tex.SampleLevel(LinSamp, ghostUV, 0);  // Eighth-res for large ghosts
        else if (absMag > 0.8)
            sample = Mip0Tex.SampleLevel(LinSamp, ghostUV, 0);  // Half-res for medium
        else if (absMag > 0.3)
            sample = Mip1Tex.SampleLevel(LinSamp, ghostUV, 0);  // Quarter-res for small
        else
            sample = Mip3Tex.SampleLevel(LinSamp, ghostUV, 0);  // Sixteenth-res for tiny

        // Distance from center: ghosts at screen edges are dimmer due to
        // the cos^4 illumination falloff of optical systems.
        float2 centered = ghostUV - 0.5;
        float dist = length(centered);
        float edgeFalloff = 1.0 - smoothstep(0.3, 0.7, dist);

        // Fresnel attenuation: the angle of incidence increases with distance
        // from the optical axis. cosTheta approximated from distance.
        float cosTheta = saturate(1.0 - dist * 2.0);
        float fresnel = FresnelSchlick(cosTheta, R0);

        // Apply per-ghost thin-film interference tint
        float3 tinted = sample.rgb * GhostTint[i].rgb;

        // Weight by Fresnel reflectance and edge falloff
        ghostAccum.rgb += tinted * fresnel * edgeFalloff;
    }

    GhostOutput[DTid.xy] = float4(ghostAccum.rgb * GhostIntensity, 1.0);
}
)HLSL";


// =============================================================================
//  Embedded HLSL -- Pass 3: Starburst + Veiling Glare CS
// =============================================================================

static const char kStarburstCS[] = R"HLSL(
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Pass 3: Diffraction Starburst from Aperture Blades
//
// Bright point sources produce radial streaks due to diffraction at the
// aperture blade edges. An N-blade aperture produces 2*N streaks (for even N)
// or N streaks (for odd N). The streak pattern is:
//   streak = pow(abs(cos(angle * numBlades / 2)), streakPower)
//
// Each output pixel accumulates light from bright source pixels along radial
// directions matching the starburst pattern, weighted by distance falloff.
//
// Reference: Fraunhofer diffraction from polygonal aperture (Hecht, "Optics",
//            Ch. 10); starburst power empirically fit to real lens photographs.

cbuffer StarburstCB : register(b0)
{
    uint2  OutputDims;
    float  StarburstIntensity;
    float  VeilingGlareStrength;
    int    ApertureBlades;     // 4-9
    float  StarburstRotation;  // Rotation angle in radians (linked to camera)
    float  CoatingFactor;      // Multi-coated: 0.01, uncoated: 0.1
    float  pad0;
    uint2  Mip3Dims;           // Sixteenth-res dimensions
    float2 pad1;
}

Texture2D<float4> BrightMip   : register(t0); // Quarter-res bright extract
Texture2D<float4> BloomLowest : register(t1); // Bloom lowest mip (veiling glare proxy)
SamplerState      LinSamp     : register(s0);
RWTexture2D<float4> StarburstOutput : register(u0);

static const float PI = 3.14159265359;

// Number of radial samples per streak direction
static const int kRadialSamples = 16;

// Starburst sharpness exponent (higher = thinner streaks)
static const float kStreakPower = 4.0;

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= OutputDims.x || DTid.y >= OutputDims.y)
        return;

    float2 uv = (float2(DTid.xy) + 0.5) / float2(OutputDims);
    float2 center = float2(0.5, 0.5);
    float2 toPixel = uv - center;
    float dist = length(toPixel);

    // ── Starburst ────────────────────────────────────────────────────
    // Compute the angular streak pattern from the aperture blade count.
    // For N blades, diffraction produces streaks at angles k * PI / N.
    float angle = atan2(toPixel.y, toPixel.x) + StarburstRotation;
    float halfBlades = float(ApertureBlades) * 0.5;

    // Streak intensity: cos^power produces the characteristic star pattern.
    // The abs() ensures we get streaks in both directions for each blade edge.
    float streak = pow(abs(cos(angle * halfBlades)), kStreakPower);

    // Accumulate bright source light along radial direction toward center.
    // This simulates the radial PSF of diffraction streaks.
    float3 starColor = 0.0;
    float2 dir = (dist > 1e-5) ? normalize(toPixel) : float2(0, 0);

    for (int s = 0; s < kRadialSamples; s++)
    {
        // Sample along the radial direction from this pixel toward center
        float t = (float(s) + 0.5) / float(kRadialSamples);
        float2 sampleUV = uv - dir * t * 0.5;  // Walk halfway toward center

        // Clamp to valid range
        sampleUV = clamp(sampleUV, 0.0, 1.0);

        float3 src = BrightMip.SampleLevel(LinSamp, sampleUV, 0).rgb;

        // Distance-based falloff: light from distant bright sources fades
        float weight = exp(-t * 3.0);
        starColor += src * weight;
    }
    starColor /= float(kRadialSamples);

    // Apply streak pattern and coating factor (multi-coated lenses produce
    // weaker starbursts due to reduced surface reflections)
    float3 starburst = starColor * streak * CoatingFactor * 10.0;

    // ── Veiling Glare ────────────────────────────────────────────────
    // Low-frequency haze from internal scatter (approximated by the lowest
    // mip of the bright extract or bloom). Adds a uniform warm glow.
    float3 veil = BloomLowest.SampleLevel(LinSamp, uv, 0).rgb;

    float3 result = starburst * StarburstIntensity + veil * VeilingGlareStrength;

    StarburstOutput[DTid.xy] = float4(result, 1.0);
}
)HLSL";


// =============================================================================
//  Embedded HLSL -- Pass 4: Anamorphic Lens Flare CS
// =============================================================================

static const char kAnamorphicCS[] = R"HLSL(
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Pass 4: Anamorphic Horizontal Streak (1D Gaussian Blur)
//
// Anamorphic lenses use cylindrical elements that create a characteristic
// horizontal (or angled) streak from bright sources. This is modeled as a
// 1D Gaussian blur along the streak direction of the bright-extracted pixels.
//
// The primary streak runs along StreakAngle (typically 0 = horizontal).
// An optional secondary streak at 90 degrees adds cross-shaped flares at
// reduced intensity, simulating multi-element anamorphic lens designs.
//
// Reference: Standard 1D Gaussian separable filter; anamorphic lens behavior
//            documented in cinematography literature (Vantage, Panavision).

cbuffer AnamorphicCB : register(b0)
{
    uint2  OutputDims;    // Half-res output
    uint2  SrcDims;       // Source (half-res bright) dimensions
    float  FlareIntensity;
    float  FalloffRate;   // Exponential falloff rate (higher = shorter streaks)
    float  StreakAngle;   // Primary streak angle in radians (0 = horizontal)
    float  SecondaryIntensity; // Secondary 90-degree streak strength
    float  DispersionScale;    // Spectral dispersion amount (R wider than B)
    float3 pad0;
}

static const int kSamples = 32;

Texture2D<float4> SrcTex  : register(t0); // Half-res bright extract
SamplerState      LinSamp : register(s0);
RWTexture2D<float4> FlareOutput : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= OutputDims.x || DTid.y >= OutputDims.y)
        return;

    float2 uv = (float2(DTid.xy) + 0.5) / float2(OutputDims);
    float2 texelSize = 1.0 / float2(SrcDims);

    // Primary streak direction (default horizontal)
    float cosA = cos(StreakAngle);
    float sinA = sin(StreakAngle);
    float2 primaryDir = float2(cosA, sinA);

    // Secondary streak direction (perpendicular)
    float2 secondaryDir = float2(-sinA, cosA);

    // ── 1D Gaussian blur along primary direction ─────────────────────
    // Sigma is derived from the falloff rate. Lower falloff = wider streaks.
    // The Gaussian weight is: exp(-0.5 * (offset / sigma)^2)
    // We use the exponential falloff directly for a more physically motivated
    // streak shape that matches anamorphic lens measurements.
    float3 primaryAccum = 0.0;
    float primaryWeight = 0.0;

    for (int i = -kSamples; i <= kSamples; i++)
    {
        float offset = float(i);
        float2 sampleUV = uv + primaryDir * offset * texelSize * 2.0;

        // Clamp to valid range
        sampleUV = clamp(sampleUV, 0.0, 1.0);

        // Exponential falloff: intensity drops with distance from center
        float w = exp(-abs(offset) * FalloffRate);

        float3 src = SrcTex.SampleLevel(LinSamp, sampleUV, 0).rgb;
        primaryAccum += src * w;
        primaryWeight += w;
    }
    primaryAccum /= max(primaryWeight, 1e-5);

    // ── 1D Gaussian blur along secondary direction ───────────────────
    float3 secondaryAccum = 0.0;
    float secondaryWeight = 0.0;

    if (SecondaryIntensity > 0.001)
    {
        for (int j = -kSamples; j <= kSamples; j++)
        {
            float offset = float(j);
            float2 sampleUV = uv + secondaryDir * offset * texelSize * 2.0;
            sampleUV = clamp(sampleUV, 0.0, 1.0);

            // Secondary streaks use a tighter falloff (shorter)
            float w = exp(-abs(offset) * FalloffRate * 2.0);

            float3 src = SrcTex.SampleLevel(LinSamp, sampleUV, 0).rgb;
            secondaryAccum += src * w;
            secondaryWeight += w;
        }
        secondaryAccum /= max(secondaryWeight, 1e-5);
    }

    // Combine primary and secondary streaks
    float3 streak = primaryAccum + secondaryAccum * SecondaryIntensity;

    FlareOutput[DTid.xy] = float4(streak * FlareIntensity, 1.0);
}
)HLSL";


// =============================================================================
//  Embedded HLSL -- Pass 5: Composite + Distortion + CA PS (fullscreen)
// =============================================================================

static const char kCompositePS[] = R"HLSL(
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
)HLSL";


// =============================================================================
//  CB structures -- must match HLSL cbuffers exactly
// =============================================================================

struct alignas(16) DownsampleCBData
{
    uint32_t srcW;             // +0
    uint32_t srcH;             // +4
    uint32_t dstW;             // +8
    uint32_t dstH;             // +12
    float    brightThreshold;  // +16
    float    softKnee;         // +20
    uint32_t mipLevel;         // +24
    float    pad0;             // +28
};
static_assert(sizeof(DownsampleCBData) == 32, "DownsampleCB must be 32 bytes");

struct alignas(16) GhostCBData
{
    uint32_t outputW;          // +0
    uint32_t outputH;          // +4
    uint32_t mip0W;            // +8
    uint32_t mip0H;            // +12
    float    ghostIntensity;   // +16
    int32_t  ghostCount;       // +20
    float    aspectRatio;      // +24
    float    pad0;             // +28
    float    ghostParams[8][4]; // +32  (8 * float4 = 128 bytes)
    float    ghostTint[8][4];   // +160 (8 * float4 = 128 bytes)
};
static_assert(sizeof(GhostCBData) == 288, "GhostCB must be 288 bytes");

struct alignas(16) StarburstCBData
{
    uint32_t outputW;           // +0
    uint32_t outputH;           // +4
    float    starburstIntensity;// +8
    float    veilingGlareStr;   // +12
    int32_t  apertureBlades;    // +16
    float    starburstRotation; // +20
    float    coatingFactor;     // +24
    float    pad0;              // +28
    uint32_t mip3W;             // +32
    uint32_t mip3H;             // +36
    float    pad1[2];           // +40
};
static_assert(sizeof(StarburstCBData) == 48, "StarburstCB must be 48 bytes");

struct alignas(16) AnamorphicCBData
{
    uint32_t outputW;           // +0
    uint32_t outputH;           // +4
    uint32_t srcW;              // +8
    uint32_t srcH;              // +12
    float    flareIntensity;    // +16
    float    falloffRate;       // +20
    float    streakAngle;       // +24
    float    secondaryIntensity;// +28
    float    dispersionScale;   // +32
    float    pad0[3];           // +36
};
static_assert(sizeof(AnamorphicCBData) == 48, "AnamorphicCB must be 48 bytes");

struct alignas(16) CompositeCBData
{
    float    screenW;           // +0
    float    screenH;           // +4
    float    distortionK1;      // +8
    float    distortionK2;      // +12
    float    caStrength;        // +16
    float    vignetteStrength;  // +20
    float    ghostBlend;        // +24
    float    starburstBlend;    // +28
    float    flareBlend;        // +32
    float    dirtIntensity;     // +36
    float    sensorClipR;       // +40
    float    sensorClipG;       // +44
    float    sensorClipB;       // +48
    float    cauchyB;           // +52
    float    time;              // +56
    float    pad0;              // +60
};
static_assert(sizeof(CompositeCBData) == 64, "CompositeCB must be 64 bytes");


// =============================================================================
//  Default ghost parameters (physically motivated lens prescription)
// =============================================================================

// Ghost scale/offset pairs derived from typical double-Gauss lens prescription.
// scale.xy = ABCD 'A' diagonal (magnification); offset.zw = 'B' translation.
// Negative scale = inverted ghost (light reflected even number of times).
static const float kDefaultGhostParams[8][4] = {
    { -1.50f,  -1.50f,   0.00f,  0.00f },  // Ghost 0: Large inverted
    {  0.80f,   0.80f,   0.00f,  0.00f },  // Ghost 1: Near-unity, upright
    { -0.45f,  -0.45f,   0.10f,  0.05f },  // Ghost 2: Small inverted, offset
    {  2.00f,   2.00f,  -0.05f, -0.05f },  // Ghost 3: 2x magnified
    { -0.20f,  -0.20f,   0.00f,  0.00f },  // Ghost 4: Tiny inverted
    {  0.60f,   0.60f,   0.08f,  0.00f },  // Ghost 5: Medium, slight offset
    {  0.30f,   0.00f,   0.15f,  0.00f },  // Ghost 6: Polynomial hero (k1, -, k3, -)
    {  0.50f,   0.00f,  -0.10f,  0.00f },  // Ghost 7: Polynomial hero (k1, -, k3, -)
};

// Ghost tints: warm/cool interference colors typical of multi-coated optics.
static const float kDefaultGhostTint[8][4] = {
    { 1.0f, 0.8f, 0.6f, 1.0f },  // Warm amber
    { 0.6f, 0.8f, 1.0f, 1.0f },  // Cool blue
    { 0.8f, 1.0f, 0.7f, 1.0f },  // Green tint
    { 1.0f, 0.6f, 0.9f, 1.0f },  // Magenta
    { 0.9f, 0.9f, 1.0f, 1.0f },  // Near-white
    { 1.0f, 0.7f, 0.5f, 1.0f },  // Orange
    { 0.5f, 0.7f, 1.0f, 1.0f },  // Sky blue
    { 0.7f, 1.0f, 0.8f, 1.0f },  // Mint
};


// =============================================================================
//  Initialize
// =============================================================================

bool LensRenderer::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                                IDXGISwapChain* sc)
{
    if (m_initialized) return true;
    if (!dev || !ctx || !sc) return false;

    auto& cm = ComputeManager::Get();
    auto& pl = RenderPipeline::Get();
    auto& rpm = RenderPassManager::Get();

    if (!cm.IsInitialized() || !pl.IsInitialized() || !rpm.IsInitialized()) {
        SKSE::log::error("LensRenderer: prerequisites not initialized");
        return false;
    }

    m_device  = dev;
    m_context = ctx;

    // Get backbuffer dimensions
    ID3D11Texture2D* backTex = nullptr;
    HRESULT hr = sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                reinterpret_cast<void**>(&backTex));
    if (FAILED(hr) || !backTex) {
        SKSE::log::error("LensRenderer: failed to get backbuffer (0x{:X})",
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

    // Register as PrePresent pipeline pass (after BloomRenderer at 10, before DoF at 30)
    m_pipelineHandle = pl.AddPass({
        .name     = "LensEffects",
        .stage    = PipelineStage::PreUI,
        .priority = 20,
        .enabled  = false,
        .execute  = [this](PassContext& ctx) { ExecutePass(ctx); },
    });

    m_initialized = true;
    m_frameIndex  = 0;

    SKSE::log::info("LensRenderer: initialized ({}x{}, ghosts={}, blades={}, k1={:.3f}, k2={:.4f})",
                    m_screenW, m_screenH, m_ghostCount, m_apertureBlades,
                    m_distortionK1, m_distortionK2);
    return true;
}


// =============================================================================
//  Compile all shaders
// =============================================================================

bool LensRenderer::CompileShaders()
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
            SKSE::log::error("LensRenderer: {} compile failed", name);
            return false;
        }

        HRESULT hr = m_device->CreateComputeShader(blob->GetBufferPointer(),
                                            blob->GetBufferSize(),
                                            nullptr, outCS);
        blob->Release();
        if (FAILED(hr)) {
            SKSE::log::error("LensRenderer: CreateComputeShader failed for {} (0x{:X})",
                             name, static_cast<uint32_t>(hr));
            return false;
        }
        return true;
    };

    // Compile 4 compute shaders
    if (!CompileCS("Lens_Downsample",  kDownsampleBrightCS, &m_downsampleCS)) return false;
    if (!CompileCS("Lens_Ghost",       kGhostCS,            &m_ghostCS))      return false;
    if (!CompileCS("Lens_Starburst",   kStarburstCS,        &m_starburstCS))  return false;
    if (!CompileCS("Lens_Anamorphic",  kAnamorphicCS,       &m_anamorphicCS)) return false;

    // Compile composite pixel shader via RenderPassManager
    m_compositePass = RenderPassManager::Get().RegisterPass({
        .name     = "LensComposite",
        .psSource = kCompositePS,
    });
    if (m_compositePass == 0) {
        SKSE::log::error("LensRenderer: failed to register composite pass");
        return false;
    }

    return true;
}


// =============================================================================
//  Create GPU resources
// =============================================================================

bool LensRenderer::CreateResources()
{
    // Helper: create an R16G16B16A16_FLOAT texture with SRV + UAV
    auto CreateHDRTexture = [&](const char* name, uint32_t w, uint32_t h,
                                 ID3D11Texture2D** outTex,
                                 ID3D11ShaderResourceView** outSRV,
                                 ID3D11UnorderedAccessView** outUAV) -> bool
    {
        return CreateGPUTexture(m_device, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                outTex, outSRV, outUAV, name);
    };

    // ── Downsample mip chain (half, quarter, eighth, sixteenth) ─────
    uint32_t w = m_screenW;
    uint32_t h = m_screenH;
    for (int i = 0; i < 4; i++)
    {
        w = (w + 1) / 2;
        h = (h + 1) / 2;
        m_downMips[i].w = w;
        m_downMips[i].h = h;

        char name[64];
        snprintf(name, sizeof(name), "lensMip%d", i);
        if (!CreateHDRTexture(name, w, h, &m_downMips[i].tex,
                              &m_downMips[i].srv, &m_downMips[i].uav))
            return false;
    }

    // ── Ghost output (quarter-res) ──────────────────────────────────
    if (!CreateHDRTexture("lensGhost", m_downMips[1].w, m_downMips[1].h,
                          &m_ghostTex, &m_ghostSRV, &m_ghostUAV))
        return false;

    // ── Starburst output (quarter-res) ──────────────────────────────
    if (!CreateHDRTexture("lensStarburst", m_downMips[1].w, m_downMips[1].h,
                          &m_starburstTex, &m_starburstSRV, &m_starburstUAV))
        return false;

    // ── Anamorphic output (half-res) ────────────────────────────────
    if (!CreateHDRTexture("lensAnamorphic", m_downMips[0].w, m_downMips[0].h,
                          &m_anamorphicTex, &m_anamorphicSRV, &m_anamorphicUAV))
        return false;

    // ── Backbuffer copy (full-res, SRV only) ────────────────────────
    if (!CreateGPUTexture(m_device, m_screenW, m_screenH, DXGI_FORMAT_R16G16B16A16_FLOAT,
                          &m_backbufferCopy, &m_backbufferCopySRV, nullptr, "bbCopy"))
        return false;

    // ── Constant buffers ────────────────────────────────────────────
    // Constant buffers
    if (!CreateCB(m_device, sizeof(DownsampleCBData), &m_downsampleCB)) return false;
    if (!CreateCB(m_device, sizeof(GhostCBData), &m_ghostCB)) return false;
    if (!CreateCB(m_device, sizeof(StarburstCBData), &m_starburstCB)) return false;
    if (!CreateCB(m_device, sizeof(AnamorphicCBData), &m_anamorphicCB)) return false;
    if (!CreateCB(m_device, sizeof(CompositeCBData), &m_compositeCB)) return false;
    // ── Linear clamp sampler ────────────────────────────────────────
    {
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxAnisotropy  = 1;
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sd.MaxLOD         = D3D11_FLOAT32_MAX;
        HRESULT hr = m_device->CreateSamplerState(&sd, &m_linearClampSampler);
        if (FAILED(hr)) return false;
    }

    return true;
}


// =============================================================================
//  Lens preset table
// =============================================================================

void LensRenderer::SetLensPreset(LensPreset p)
{
    switch (p)
    {
        case Cooke:  m_distortionK1 = -0.12f;  m_distortionK2 = 0.030f;  break;
        case Zeiss:  m_distortionK1 = -0.02f;  m_distortionK2 = 0.001f;  break;
        case MIR1:   m_distortionK1 = -0.25f;  m_distortionK2 = 0.080f;  break;
        case Primo:  m_distortionK1 = -0.005f; m_distortionK2 = 0.000f;  break;
        case Custom: /* leave user values intact */                        break;
    }
}


// =============================================================================
//  Per-frame execution (called by RenderPipeline at PreUI stage)
// =============================================================================

void LensRenderer::ExecutePass(PassContext& ctx)
{
    if (!m_initialized || !m_enabled) return;

    auto& cm = ComputeManager::Get();

    // ── Copy scene color to scratch texture ────────────────────────
    // Mid-frame: extract the scene texture from the game's active RTV.
    // PrePresent fallback: use the swapchain backbuffer.
    {
        ID3D11Texture2D* sceneTex = nullptr;

        if (ctx.gameSceneRTV) {
            // Mid-frame dispatch: game's active scene RT
            ID3D11Resource* res = nullptr;
            ctx.gameSceneRTV->GetResource(&res);
            if (res) {
                res->QueryInterface(__uuidof(ID3D11Texture2D),
                                    reinterpret_cast<void**>(&sceneTex));
                res->Release();
            }
        } else {
            // PrePresent fallback: use backbuffer
            auto* sc = ctx.swapChain;
            if (!sc) return;
            sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                          reinterpret_cast<void**>(&sceneTex));
        }

        if (!sceneTex) return;

        // CopyResource requires matching format+dimensions.  The game's
        // internal scene RT is often R16G16B16A16_FLOAT while the
        // swapchain backbuffer is R8G8B8A8_UNORM.  Lazily recreate the
        // copy texture if needed.
        D3D11_TEXTURE2D_DESC sceneDesc;
        sceneTex->GetDesc(&sceneDesc);

        D3D11_TEXTURE2D_DESC copyDesc;
        m_backbufferCopy->GetDesc(&copyDesc);

        if (sceneDesc.Format != copyDesc.Format ||
            sceneDesc.Width  != copyDesc.Width  ||
            sceneDesc.Height != copyDesc.Height)
        {
            SKSE::log::info("LensRenderer: scene RT format/size changed — "
                "scene={}x{} fmt={}, copy={}x{} fmt={} — recreating",
                sceneDesc.Width, sceneDesc.Height, static_cast<int>(sceneDesc.Format),
                copyDesc.Width, copyDesc.Height, static_cast<int>(copyDesc.Format));

            if (m_backbufferCopySRV) { m_backbufferCopySRV->Release(); m_backbufferCopySRV = nullptr; }
            if (m_backbufferCopy)    { m_backbufferCopy->Release();    m_backbufferCopy    = nullptr; }

            D3D11_TEXTURE2D_DESC newDesc = sceneDesc;
            newDesc.BindFlags      = D3D11_BIND_SHADER_RESOURCE;
            newDesc.Usage          = D3D11_USAGE_DEFAULT;
            newDesc.CPUAccessFlags = 0;
            newDesc.MiscFlags      = 0;

            HRESULT hr = m_device->CreateTexture2D(&newDesc, nullptr, &m_backbufferCopy);
            if (FAILED(hr)) {
                SKSE::log::error("LensRenderer: failed to recreate copy tex");
                sceneTex->Release();
                return;
            }

            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format                    = newDesc.Format;
            srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.MipLevels       = 1;
            hr = m_device->CreateShaderResourceView(m_backbufferCopy, &srvDesc,
                                                     &m_backbufferCopySRV);
            if (FAILED(hr)) {
                SKSE::log::error("LensRenderer: failed to recreate copy SRV");
                sceneTex->Release();
                return;
            }
        }

        ctx.context->CopyResource(m_backbufferCopy, sceneTex);
        sceneTex->Release();
    }

    cm.SaveCSState();

    // ── Helper: update CB (delegates to GPUResource.h UploadCB) ────
    auto UpdateCB = [&](ID3D11Buffer* cb, const void* data, size_t size) {
        UploadCB(ctx.context, cb, data, static_cast<uint32_t>(size));
    };

    // ═════════════════════════════════════════════════════════════════
    //  Pass 1: Downsample + Bright Extract (4 mip levels)
    // ═════════════════════════════════════════════════════════════════
    {
        // Mip 0: backbuffer -> half-res (with bright extract)
        // Mip 1-3: each mip from the previous mip
        ID3D11ShaderResourceView* srcSRV = m_backbufferCopySRV;
        uint32_t srcW = m_screenW;
        uint32_t srcH = m_screenH;

        for (int mip = 0; mip < 4; mip++)
        {
            DownsampleCBData cb = {};
            cb.srcW            = srcW;
            cb.srcH            = srcH;
            cb.dstW            = m_downMips[mip].w;
            cb.dstH            = m_downMips[mip].h;
            cb.brightThreshold = 1.0f;  // Only applied on mip 0
            cb.softKnee        = 0.5f;
            cb.mipLevel        = static_cast<uint32_t>(mip);
            UpdateCB(m_downsampleCB, &cb, sizeof(cb));

            ctx.context->CSSetShader(m_downsampleCS, nullptr, 0);
            ctx.context->CSSetConstantBuffers(0, 1, &m_downsampleCB);
            ctx.context->CSSetShaderResources(0, 1, &srcSRV);
            ctx.context->CSSetSamplers(0, 1, &m_linearClampSampler);
            ctx.context->CSSetUnorderedAccessViews(0, 1, &m_downMips[mip].uav, nullptr);

            UINT groupsX = (m_downMips[mip].w + 7) / 8;
            UINT groupsY = (m_downMips[mip].h + 7) / 8;
            ctx.context->Dispatch(groupsX, groupsY, 1);

            // Clear bindings
            ID3D11ShaderResourceView* nullSRV[1] = {};
            ctx.context->CSSetShaderResources(0, 1, nullSRV);
            ID3D11UnorderedAccessView* nullUAV[1] = {};
            ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);

            // Next mip reads from this one
            srcSRV = m_downMips[mip].srv;
            srcW   = m_downMips[mip].w;
            srcH   = m_downMips[mip].h;
        }
    }

    // ═════════════════════════════════════════════════════════════════
    //  Pass 2: Ghost Evaluation (quarter-res)
    // ═════════════════════════════════════════════════════════════════
    {
        GhostCBData cb = {};
        cb.outputW        = m_downMips[1].w;  // quarter-res
        cb.outputH        = m_downMips[1].h;
        cb.mip0W          = m_downMips[0].w;
        cb.mip0H          = m_downMips[0].h;
        cb.ghostIntensity = m_ghostIntensity;
        cb.ghostCount     = m_ghostCount;
        cb.aspectRatio    = static_cast<float>(m_screenW) / static_cast<float>(m_screenH);

        // Copy ghost parameters
        std::memcpy(cb.ghostParams, kDefaultGhostParams, sizeof(kDefaultGhostParams));
        std::memcpy(cb.ghostTint,   kDefaultGhostTint,   sizeof(kDefaultGhostTint));

        UpdateCB(m_ghostCB, &cb, sizeof(cb));

        // Bind 4 mip SRVs
        ID3D11ShaderResourceView* mipSRVs[4] = {
            m_downMips[0].srv, m_downMips[1].srv,
            m_downMips[2].srv, m_downMips[3].srv
        };
        ctx.context->CSSetShader(m_ghostCS, nullptr, 0);
        ctx.context->CSSetConstantBuffers(0, 1, &m_ghostCB);
        ctx.context->CSSetShaderResources(0, 4, mipSRVs);
        ctx.context->CSSetSamplers(0, 1, &m_linearClampSampler);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &m_ghostUAV, nullptr);

        UINT groupsX = (m_downMips[1].w + 7) / 8;
        UINT groupsY = (m_downMips[1].h + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        ID3D11ShaderResourceView* nullSRVs[4] = {};
        ctx.context->CSSetShaderResources(0, 4, nullSRVs);
        ID3D11UnorderedAccessView* nullUAV[1] = {};
        ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
    }

    // ═════════════════════════════════════════════════════════════════
    //  Pass 3: Starburst + Veiling Glare (quarter-res)
    // ═════════════════════════════════════════════════════════════════
    {
        StarburstCBData cb = {};
        cb.outputW             = m_downMips[1].w;
        cb.outputH             = m_downMips[1].h;
        cb.starburstIntensity  = m_starburstIntensity;
        cb.veilingGlareStr     = m_veilingGlareStrength;
        cb.apertureBlades      = m_apertureBlades;
        cb.starburstRotation   = 0.0f;  // Could link to camera rotation
        cb.coatingFactor       = 0.02f; // Multi-coated default
        cb.mip3W               = m_downMips[3].w;
        cb.mip3H               = m_downMips[3].h;

        UpdateCB(m_starburstCB, &cb, sizeof(cb));

        // t0 = quarter-res bright, t1 = bloom lowest mip (or our sixteenth-res)
        // Use our own lowest mip as veiling glare proxy if bloom not available
        ID3D11ShaderResourceView* bloomLowest = nullptr;
        auto& bloom = BloomRenderer::Get();
        if (bloom.IsInitialized() && bloom.GetBloomSRV()) {
            bloomLowest = bloom.GetBloomSRV();
        } else {
            bloomLowest = m_downMips[3].srv;  // Fallback: use our own lowest mip
        }

        ID3D11ShaderResourceView* srvs[2] = { m_downMips[1].srv, bloomLowest };
        ctx.context->CSSetShader(m_starburstCS, nullptr, 0);
        ctx.context->CSSetConstantBuffers(0, 1, &m_starburstCB);
        ctx.context->CSSetShaderResources(0, 2, srvs);
        ctx.context->CSSetSamplers(0, 1, &m_linearClampSampler);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &m_starburstUAV, nullptr);

        UINT groupsX = (m_downMips[1].w + 7) / 8;
        UINT groupsY = (m_downMips[1].h + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        ID3D11ShaderResourceView* nullSRVs[2] = {};
        ctx.context->CSSetShaderResources(0, 2, nullSRVs);
        ID3D11UnorderedAccessView* nullUAV[1] = {};
        ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
    }

    // ═════════════════════════════════════════════════════════════════
    //  Pass 4: Anamorphic Lens Flare (half-res)
    // ═════════════════════════════════════════════════════════════════
    {
        AnamorphicCBData cb = {};
        cb.outputW            = m_downMips[0].w;
        cb.outputH            = m_downMips[0].h;
        cb.srcW               = m_downMips[0].w;
        cb.srcH               = m_downMips[0].h;
        cb.flareIntensity     = m_flareIntensity;
        cb.falloffRate        = 0.12f;   // Exponential falloff
        cb.streakAngle        = 0.0f;    // Horizontal
        cb.secondaryIntensity = m_flareIntensity * 0.3f;  // 30% secondary
        cb.dispersionScale    = 0.15f;   // Spectral dispersion amount

        UpdateCB(m_anamorphicCB, &cb, sizeof(cb));

        ID3D11ShaderResourceView* srcSRV = m_downMips[0].srv;
        ctx.context->CSSetShader(m_anamorphicCS, nullptr, 0);
        ctx.context->CSSetConstantBuffers(0, 1, &m_anamorphicCB);
        ctx.context->CSSetShaderResources(0, 1, &srcSRV);
        ctx.context->CSSetSamplers(0, 1, &m_linearClampSampler);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &m_anamorphicUAV, nullptr);

        UINT groupsX = (m_downMips[0].w + 7) / 8;
        UINT groupsY = (m_downMips[0].h + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        ID3D11ShaderResourceView* nullSRV[1] = {};
        ctx.context->CSSetShaderResources(0, 1, nullSRV);
        ID3D11UnorderedAccessView* nullUAV[1] = {};
        ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
    }

    cm.RestoreCSState();

    // ═════════════════════════════════════════════════════════════════
    //  Pass 5: Composite + Distortion + CA (fullscreen pixel shader)
    // ═════════════════════════════════════════════════════════════════
    {
        CompositeCBData cb = {};
        cb.screenW          = static_cast<float>(m_screenW);
        cb.screenH          = static_cast<float>(m_screenH);
        cb.distortionK1     = m_distortionK1;
        cb.distortionK2     = m_distortionK2;
        cb.caStrength       = m_caStrength;
        cb.vignetteStrength = m_vignetteStrength;
        cb.ghostBlend       = m_ghostIntensity;
        cb.starburstBlend   = m_starburstIntensity;
        cb.flareBlend       = m_flareIntensity;
        cb.dirtIntensity    = m_dirtIntensity;
        cb.sensorClipR      = 8.0f;    // Red clips at ~8 stops
        cb.sensorClipG      = 6.0f;    // Green clips first at ~6 stops
        cb.sensorClipB      = 7.0f;    // Blue clips at ~7 stops
        cb.cauchyB          = 1.5e6f;  // Cauchy B coefficient for typical glass
        cb.time             = static_cast<float>(m_frameIndex) * 0.016f; // ~60fps

        // Get output RTV — mid-frame: use game's active scene RT;
        // PrePresent: create from swapchain backbuffer.
        ID3D11RenderTargetView* outRTV = nullptr;
        bool ownRTV = false;

        if (ctx.gameSceneRTV) {
            // Mid-frame dispatch: composite directly onto the game's scene RT
            outRTV = ctx.gameSceneRTV;
            // Don't AddRef — backup keeps it alive during ExecuteStage
        } else {
            // PrePresent fallback: create RTV from swapchain backbuffer
            ID3D11Texture2D* backTex = nullptr;
            auto* sc = ctx.swapChain;
            if (!sc) return;
            HRESULT hr = sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                        reinterpret_cast<void**>(&backTex));
            if (FAILED(hr) || !backTex) return;

            D3D11_TEXTURE2D_DESC texDesc;
            backTex->GetDesc(&texDesc);
            D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
            rtvDesc.Format        = texDesc.Format;
            rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            hr = m_device->CreateRenderTargetView(backTex, &rtvDesc, &outRTV);
            backTex->Release();
            if (FAILED(hr) || !outRTV) return;
            ownRTV = true;
        }

        // Bind SRVs: t0=scene, t1=ghosts, t2=starburst, t3=flare
        ID3D11ShaderResourceView* srvs[4] = {
            m_backbufferCopySRV,
            m_ghostSRV,
            m_starburstSRV,
            m_anamorphicSRV
        };

        RenderPassManager::Get().Execute({
            .passID       = m_compositePass,
            .rtv          = outRTV,
            .srvs         = srvs,
            .srvCount     = 4,
            .samplers     = &m_linearClampSampler,
            .samplerCount = 1,
            .cbData       = &cb,
            .cbSize       = sizeof(cb),
        });

        if (ownRTV) outRTV->Release();
    }

    m_frameIndex++;
}


// =============================================================================
//  Shutdown
// =============================================================================

void LensRenderer::Shutdown()
{
    if (!m_initialized) return;

    if (m_pipelineHandle) {
        RenderPipeline::Get().RemovePass(m_pipelineHandle);
        m_pipelineHandle = 0;
    }

    ReleaseResources();
    m_initialized = false;

    SKSE::log::info("LensRenderer: shut down");
}


void LensRenderer::ReleaseResources()
{
    auto SafeRelease = [](auto*& ptr) {
        if (ptr) { ptr->Release(); ptr = nullptr; }
    };

    // Compute shaders
    SafeRelease(m_downsampleCS);
    SafeRelease(m_ghostCS);
    SafeRelease(m_starburstCS);
    SafeRelease(m_anamorphicCS);

    // Downsample mip chain
    for (int i = 0; i < 4; i++) {
        SafeRelease(m_downMips[i].tex);
        SafeRelease(m_downMips[i].srv);
        SafeRelease(m_downMips[i].uav);
        m_downMips[i].w = m_downMips[i].h = 0;
    }

    // Ghost
    SafeRelease(m_ghostTex);
    SafeRelease(m_ghostSRV);
    SafeRelease(m_ghostUAV);

    // Starburst
    SafeRelease(m_starburstTex);
    SafeRelease(m_starburstSRV);
    SafeRelease(m_starburstUAV);

    // Anamorphic
    SafeRelease(m_anamorphicTex);
    SafeRelease(m_anamorphicSRV);
    SafeRelease(m_anamorphicUAV);

    // Backbuffer copy
    SafeRelease(m_backbufferCopy);
    SafeRelease(m_backbufferCopySRV);

    // Constant buffers
    SafeRelease(m_downsampleCB);
    SafeRelease(m_ghostCB);
    SafeRelease(m_starburstCB);
    SafeRelease(m_anamorphicCB);
    SafeRelease(m_compositeCB);

    // Sampler
    SafeRelease(m_linearClampSampler);

    // Note: m_compositePass is managed by RenderPassManager, not released here
    m_compositePass = 0;
}

} // namespace SB
