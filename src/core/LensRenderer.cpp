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
//  Replaces ENB's enblens.fx entirely.  Runs as a standalone compute+pixel
//  pipeline with no BSShader hooks required.
//=============================================================================

#include "LensRenderer.h"
#include "SRVInjector.h"

#include <cstring>
#include <cmath>


namespace SB
{

// =============================================================================
//  Embedded HLSL -- Pass 1: Downsample + Bright Extract CS
// =============================================================================

static const char kDownsampleBrightCS[] = R"HLSL(
// Downsample + Bright Extract CS
// Performs a 4-tap circular box blur during downsample with UV offset.
// Each dispatch writes one mip level from the previous level (or backbuffer).

cbuffer DownsampleCB : register(b0)
{
    uint2  SrcDims;       // Source texture dimensions
    uint2  DstDims;       // Destination (output) dimensions
    float  BrightThreshold; // Threshold for bright extraction (mip 0 only)
    float  SoftKnee;      // Soft knee transition width
    uint   MipLevel;      // Which mip we are generating (0..3)
    float  pad0;
};

Texture2D<float4>   SrcTex  : register(t0);
SamplerState        LinSamp : register(s0);
RWTexture2D<float4> DstTex  : register(u0);

// Soft threshold with knee (Karis-style)
float3 SoftThreshold(float3 color, float threshold, float knee)
{
    float brightness = max(color.r, max(color.g, color.b));
    float soft = brightness - threshold + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 0.00001);
    float contrib = max(soft, brightness - threshold) / max(brightness, 0.00001);
    return color * max(contrib, 0.0);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= DstDims.x || DTid.y >= DstDims.y)
        return;

    float2 texelSize = 1.0 / float2(SrcDims);
    float2 uv = (float2(DTid.xy) + 0.5) / float2(DstDims);

    // 4-tap circular box blur: offset samples in a ring pattern
    // Uses bilinear filtering for an effective 16-sample coverage
    float2 offsets[4] = {
        float2(-0.5, -0.5),
        float2( 0.5, -0.5),
        float2(-0.5,  0.5),
        float2( 0.5,  0.5)
    };

    float4 color = 0.0;
    for (int i = 0; i < 4; i++)
    {
        float2 sampleUV = uv + offsets[i] * texelSize;
        color += SrcTex.SampleLevel(LinSamp, sampleUV, 0);
    }
    color *= 0.25;

    // Apply bright extraction on the first mip only
    if (MipLevel == 0)
    {
        color.rgb = SoftThreshold(color.rgb, BrightThreshold, SoftKnee);
    }

    // Clamp to prevent NaN/Inf propagation
    color = clamp(color, 0.0, 65504.0);

    DstTex[DTid.xy] = color;
}
)HLSL";


// =============================================================================
//  Embedded HLSL -- Pass 2: Ghost Evaluation CS
// =============================================================================

static const char kGhostCS[] = R"HLSL(
// Ghost Evaluation CS — ABCD matrix ghosts + polynomial hero ghosts
//
// For each ghost:
//   - ABCD ghosts (6): UV' = A * UV + B (affine transform from lens prescription)
//   - Polynomial hero ghosts (2): degree-3 rotationally symmetric polynomial distortion
//   - Each ghost samples from a downsample mip, scales by Fresnel coefficient,
//     and tints by thin-film interference color (MgF2 single-layer coating).
//
// Accumulates all 8 ghosts additively into the output.

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
};

Texture2D<float4> Mip0Tex : register(t0); // Half-res bright
Texture2D<float4> Mip1Tex : register(t1); // Quarter-res
Texture2D<float4> Mip2Tex : register(t2); // Eighth-res
Texture2D<float4> Mip3Tex : register(t3); // Sixteenth-res
SamplerState      LinSamp : register(s0);

RWTexture2D<float4> GhostOutput : register(u0);

// Thin-film interference: MgF2 single-layer coating
// reflectance = 4*R1*R2*sin^2(delta/2) / (1 - R1*R2)^2
// where delta = 2*pi*n*d*cos(theta)/lambda
float3 ThinFilmColor(float cosTheta, float coatingThickness)
{
    // MgF2 refractive index
    const float n_coating = 1.38;
    // Air-coating and coating-glass interface reflectances (normal incidence approx)
    const float R1 = 0.025; // (1.38-1.0)^2/(1.38+1.0)^2
    const float R2 = 0.015; // (1.52-1.38)^2/(1.52+1.38)^2

    float3 color;
    // Evaluate at 3 wavelengths (RGB: 630nm, 532nm, 465nm)
    float wavelengths[3] = { 0.630, 0.532, 0.465 };

    [unroll]
    for (int i = 0; i < 3; i++)
    {
        float delta = 2.0 * 3.14159265 * n_coating * coatingThickness * cosTheta / wavelengths[i];
        float sinHalf = sin(delta * 0.5);
        float reflectance = 4.0 * R1 * R2 * sinHalf * sinHalf / ((1.0 - R1 * R2) * (1.0 - R1 * R2));
        // Ghost brightness is proportional to reflectance
        color[i] = reflectance;
    }
    return color;
}

// Fresnel reflectance (Schlick approximation)
float FresnelSchlick(float cosTheta, float F0)
{
    float t = 1.0 - cosTheta;
    float t2 = t * t;
    return F0 + (1.0 - F0) * t2 * t2 * t;
}

float4 SampleMip(int mipIndex, float2 uv)
{
    // Select mip based on ghost index (larger ghosts sample coarser mips)
    if (mipIndex == 0) return Mip0Tex.SampleLevel(LinSamp, uv, 0);
    if (mipIndex == 1) return Mip1Tex.SampleLevel(LinSamp, uv, 0);
    if (mipIndex == 2) return Mip2Tex.SampleLevel(LinSamp, uv, 0);
    return Mip3Tex.SampleLevel(LinSamp, uv, 0);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= OutputDims.x || DTid.y >= OutputDims.y)
        return;

    float2 uv = (float2(DTid.xy) + 0.5) / float2(OutputDims);
    // Center UV for ghost transforms (origin at image center)
    float2 uvCentered = uv * 2.0 - 1.0;

    float4 totalGhost = 0.0;

    for (int g = 0; g < GhostCount; g++)
    {
        float2 scale  = GhostParams[g].xy;
        float2 offset = GhostParams[g].zw;

        float2 ghostUV;

        if (g < 6)
        {
            // ABCD Matrix ghost: UV' = A * UV + B
            // A is diagonal scale, B is translation offset
            ghostUV = uvCentered * scale + offset;
        }
        else
        {
            // Polynomial hero ghost: degree-3 rotationally symmetric distortion
            float r = length(uvCentered);
            float r2 = r * r;
            float r3 = r2 * r;
            // Polynomial coefficients packed in scale.x (k1) and offset.x (k3)
            float distortion = 1.0 + scale.x * r2 + offset.x * r3;
            ghostUV = uvCentered * distortion;
        }

        // Map back to [0,1]
        float2 sampleUV = ghostUV * 0.5 + 0.5;

        // Discard if outside texture
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 ||
            sampleUV.y < 0.0 || sampleUV.y > 1.0)
            continue;

        // Select mip level based on ghost scale (larger = coarser mip)
        float ghostSize = max(abs(scale.x), abs(scale.y));
        int mipIdx = (ghostSize > 2.0) ? 3 : (ghostSize > 1.0) ? 2 : (ghostSize > 0.5) ? 1 : 0;

        float4 sample = SampleMip(mipIdx, sampleUV);

        // Fresnel coefficient: angle from center determines reflectance
        float cosTheta = saturate(1.0 - length(uvCentered) * 0.7);
        float fresnel = FresnelSchlick(cosTheta, 0.04);

        // Thin-film interference tint (coating thickness varies per ghost)
        float coatingThickness = 0.1 + float(g) * 0.025; // 100-300nm range
        float3 thinFilm = ThinFilmColor(cosTheta, coatingThickness);

        // Apply ghost tint from CB (artist override) * thin-film * Fresnel
        float3 tint = GhostTint[g].rgb * thinFilm * fresnel;

        totalGhost.rgb += sample.rgb * tint * GhostIntensity;
    }

    totalGhost.a = 1.0;
    totalGhost = clamp(totalGhost, 0.0, 65504.0);

    GhostOutput[DTid.xy] = totalGhost;
}
)HLSL";


// =============================================================================
//  Embedded HLSL -- Pass 3: Starburst + Veiling Glare CS
// =============================================================================

static const char kStarburstCS[] = R"HLSL(
// Starburst + Veiling Glare CS
//
// Diffraction starburst: For N aperture blades, cast N or 2N rays from bright
// pixel centers.  sinc^2 approximation along each ray.  Spectral color per
// ray: map blade angle to rainbow via wavelength->RGB.
//
// Veiling glare: Use bloom lowest mip as proxy for 1/r^2 PSF.
//   veilGlare = bloomMip16.Sample(uv) * coatingFactor

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
};

Texture2D<float4> BrightMip   : register(t0); // Quarter-res bright extract
Texture2D<float4> BloomLowest : register(t1); // Bloom lowest mip (veiling glare proxy)
SamplerState      LinSamp     : register(s0);

RWTexture2D<float4> StarburstOutput : register(u0);

// Approximate sinc^2(x) = (sin(pi*x)/(pi*x))^2
float Sinc2(float x)
{
    if (abs(x) < 0.001) return 1.0;
    float pix = 3.14159265 * x;
    float s = sin(pix) / pix;
    return s * s;
}

// Spectral wavelength to approximate RGB
// Maps wavelength in [380, 780] nm to RGB
float3 WavelengthToRGB(float wavelength)
{
    float3 rgb = 0.0;

    if (wavelength >= 380.0 && wavelength < 440.0)
    {
        rgb.r = -(wavelength - 440.0) / (440.0 - 380.0);
        rgb.b = 1.0;
    }
    else if (wavelength < 490.0)
    {
        rgb.g = (wavelength - 440.0) / (490.0 - 440.0);
        rgb.b = 1.0;
    }
    else if (wavelength < 510.0)
    {
        rgb.g = 1.0;
        rgb.b = -(wavelength - 510.0) / (510.0 - 490.0);
    }
    else if (wavelength < 580.0)
    {
        rgb.r = (wavelength - 510.0) / (580.0 - 510.0);
        rgb.g = 1.0;
    }
    else if (wavelength < 645.0)
    {
        rgb.r = 1.0;
        rgb.g = -(wavelength - 645.0) / (645.0 - 580.0);
    }
    else if (wavelength <= 780.0)
    {
        rgb.r = 1.0;
    }

    // Intensity falloff at spectrum edges
    float factor;
    if (wavelength >= 380.0 && wavelength < 420.0)
        factor = 0.3 + 0.7 * (wavelength - 380.0) / (420.0 - 380.0);
    else if (wavelength >= 700.0 && wavelength <= 780.0)
        factor = 0.3 + 0.7 * (780.0 - wavelength) / (780.0 - 700.0);
    else
        factor = 1.0;

    return rgb * factor;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= OutputDims.x || DTid.y >= OutputDims.y)
        return;

    float2 uv = (float2(DTid.xy) + 0.5) / float2(OutputDims);
    float2 uvCentered = uv * 2.0 - 1.0;

    float4 result = 0.0;

    // ── Diffraction Starburst ───────────────────────────────────────
    // Sample the bright mip at this position to determine if we are near a bright source
    float4 brightSample = BrightMip.SampleLevel(LinSamp, uv, 0);
    float brightness = max(brightSample.r, max(brightSample.g, brightSample.b));

    if (brightness > 0.01 && StarburstIntensity > 0.0)
    {
        float3 starburst = 0.0;
        int numRays = ApertureBlades * 2; // 2 rays per blade (both directions)
        float angleStep = 3.14159265 / float(ApertureBlades);

        for (int ray = 0; ray < numRays; ray++)
        {
            float angle = float(ray) * angleStep + StarburstRotation;
            float2 rayDir = float2(cos(angle), sin(angle));

            // Project pixel position onto this ray
            float proj = dot(uvCentered, rayDir);

            // sinc^2 envelope along the ray (diffraction pattern)
            float diffraction = Sinc2(proj * 8.0); // scale controls spike width

            // Spectral color: map ray angle to wavelength (420-680nm rainbow)
            float wavelength = 420.0 + frac(float(ray) / float(numRays)) * 260.0;
            float3 spectralColor = WavelengthToRGB(wavelength);

            // Per-blade brightness variation (manufacturing imperfection)
            float bladeVariation = 0.8 + 0.2 * sin(float(ray) * 2.37 + 0.5);

            starburst += diffraction * spectralColor * bladeVariation;
        }

        starburst /= float(numRays);
        result.rgb += starburst * brightness * StarburstIntensity;
    }

    // ── Veiling Glare ───────────────────────────────────────────────
    // Use bloom lowest mip as proxy for wide-angle scattering (1/r^2 PSF)
    float4 bloomSample = BloomLowest.SampleLevel(LinSamp, uv, 0);
    result.rgb += bloomSample.rgb * VeilingGlareStrength * CoatingFactor;

    result.a = 1.0;
    result = clamp(result, 0.0, 65504.0);

    StarburstOutput[DTid.xy] = result;
}
)HLSL";


// =============================================================================
//  Embedded HLSL -- Pass 4: Anamorphic Lens Flare CS
// =============================================================================

static const char kAnamorphicCS[] = R"HLSL(
// Anamorphic Lens Flare CS
//
// Horizontal streak from bright spots with exponential falloff.
// Spectral dispersion: R channel extends wider than B.
// Rotatable angle (not just horizontal).
// Secondary streak at 90 degrees with reduced intensity.

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
};

Texture2D<float4> SrcTex  : register(t0); // Half-res bright extract
SamplerState      LinSamp : register(s0);

RWTexture2D<float4> FlareOutput : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= OutputDims.x || DTid.y >= OutputDims.y)
        return;

    float2 uv = (float2(DTid.xy) + 0.5) / float2(OutputDims);
    float2 texelSize = 1.0 / float2(OutputDims);

    float4 result = 0.0;

    // Primary streak direction
    float2 primaryDir = float2(cos(StreakAngle), sin(StreakAngle));
    // Secondary streak at 90 degrees
    float2 secondaryDir = float2(-primaryDir.y, primaryDir.x);

    // Number of samples along the streak
    static const int kSamples = 32;
    float stepSize = 2.0; // texels per step

    // ── Primary streak with spectral dispersion ─────────────────────
    float3 primaryStreak = 0.0;

    for (int i = -kSamples; i <= kSamples; i++)
    {
        if (i == 0) continue; // skip center

        float dist = abs(float(i));
        float weight = exp(-dist * FalloffRate);

        // Per-channel dispersion: R extends further, B is shorter
        float3 channelScale = float3(
            1.0 + DispersionScale,      // Red extends wider
            1.0,                         // Green baseline
            1.0 - DispersionScale * 0.5  // Blue shorter
        );

        float3 sample_rgb;
        [unroll]
        for (int ch = 0; ch < 3; ch++)
        {
            float2 offset = primaryDir * float(i) * stepSize * channelScale[ch] * texelSize;
            float2 sampleUV = uv + offset;
            sampleUV = clamp(sampleUV, 0.0, 1.0);
            sample_rgb[ch] = SrcTex.SampleLevel(LinSamp, sampleUV, 0)[ch];
        }

        primaryStreak += sample_rgb * weight;
    }
    primaryStreak /= float(kSamples);

    // ── Secondary streak (90 degrees, reduced intensity) ────────────
    float3 secondaryStreak = 0.0;

    for (int j = -kSamples; j <= kSamples; j++)
    {
        if (j == 0) continue;

        float dist = abs(float(j));
        float weight = exp(-dist * FalloffRate * 1.5); // faster falloff

        float2 offset = secondaryDir * float(j) * stepSize * texelSize;
        float2 sampleUV = uv + offset;
        sampleUV = clamp(sampleUV, 0.0, 1.0);

        float4 s = SrcTex.SampleLevel(LinSamp, sampleUV, 0);
        secondaryStreak += s.rgb * weight;
    }
    secondaryStreak /= float(kSamples);

    result.rgb = primaryStreak * FlareIntensity + secondaryStreak * SecondaryIntensity;
    result.a = 1.0;
    result = clamp(result, 0.0, 65504.0);

    FlareOutput[DTid.xy] = result;
}
)HLSL";


// =============================================================================
//  Embedded HLSL -- Pass 5: Composite + Distortion + CA PS (fullscreen)
// =============================================================================

static const char kCompositePS[] = R"HLSL(
// Composite PS — Combines all lens effects onto the scene.
//
// - Brown-Conrady barrel/pincushion distortion
// - 6-band spectral chromatic aberration (Cauchy dispersion)
// - cos^4(theta) optical vignette
// - Sensor per-channel soft saturation (green clips first -> pink highlights)
// - Lens dirt overlay (procedural multi-octave noise modulated by flare brightness)
// - Additive blend of ghosts + starburst + anamorphic flare + veiling glare

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
};

Texture2D<float4> SceneTex     : register(t0); // Backbuffer copy (original scene)
Texture2D<float4> GhostTex     : register(t1); // Quarter-res ghost accumulation
Texture2D<float4> StarburstTex : register(t2); // Quarter-res starburst+veil
Texture2D<float4> FlareTex     : register(t3); // Half-res anamorphic flare
SamplerState      LinSamp      : register(s0);

struct VSOutput
{
    float4 Position : SV_Position;
    float2 TexCoord : TEXCOORD0;
};

// ── Brown-Conrady distortion ────────────────────────────────────────
float2 ApplyDistortion(float2 uv, float k1, float k2)
{
    float2 centered = uv - 0.5;
    float r2 = dot(centered, centered);
    float distortion = 1.0 + k1 * r2 + k2 * r2 * r2;
    return centered * distortion + 0.5;
}

// ── 6-band spectral CA via Cauchy dispersion ────────────────────────
// Sample scene at 6 wavelengths with per-wavelength radial offset.
// Reconstruct RGB via CIE XYZ observer weights -> sRGB.

// CIE 1931 2-degree observer approximation for 6 wavelengths
// Wavelengths: 420, 460, 520, 570, 620, 680 nm
static const float3 kCIE_XYZ[6] = {
    float3(0.0681, 0.0021, 0.3230), // 420nm
    float3(0.0218, 0.0600, 0.3100), // 460nm
    float3(0.0633, 0.7100, 0.0782), // 520nm
    float3(0.3281, 0.8620, 0.0112), // 570nm
    float3(0.6133, 0.3810, 0.0002), // 620nm
    float3(0.0720, 0.0410, 0.0000), // 680nm
};

static const float kWavelengths[6] = { 420.0, 460.0, 520.0, 570.0, 620.0, 680.0 };

// XYZ to sRGB (D65)
float3 XYZtoRGB(float3 xyz)
{
    float3 rgb;
    rgb.r =  3.2406 * xyz.x - 1.5372 * xyz.y - 0.4986 * xyz.z;
    rgb.g = -0.9689 * xyz.x + 1.8758 * xyz.y + 0.0415 * xyz.z;
    rgb.b =  0.0557 * xyz.x - 0.2040 * xyz.y + 1.0570 * xyz.z;
    return rgb;
}

float3 SpectralCA(float2 uv, float caBase, float cauchyB)
{
    float2 centered = uv - 0.5;
    float r2 = dot(centered, centered);

    float3 xyz = 0.0;
    float totalWeight = 0.0;

    [unroll]
    for (int i = 0; i < 6; i++)
    {
        float lambda = kWavelengths[i];
        // Cauchy dispersion: offset = baseCA * (1.0 + B / lambda^2) * r^2
        float dispersion = caBase * (1.0 + cauchyB / (lambda * lambda)) * r2;
        float2 sampleUV = centered * (1.0 + dispersion) + 0.5;
        sampleUV = clamp(sampleUV, 0.0, 1.0);

        float4 s = SceneTex.SampleLevel(LinSamp, sampleUV, 0);
        float luminance = dot(s.rgb, float3(0.2126, 0.7152, 0.0722));

        xyz += kCIE_XYZ[i] * luminance;
        totalWeight += dot(kCIE_XYZ[i], 1.0);
    }

    xyz /= max(totalWeight, 0.0001) * 0.5; // Normalize
    return XYZtoRGB(xyz);
}

// ── cos^4(theta) optical vignette ───────────────────────────────────
float OpticalVignette(float2 uv, float strength)
{
    float2 centered = uv - 0.5;
    // cos(theta) approximation: 1 / sqrt(1 + r^2 * tan^2(halfFOV))
    // Simplified: use r^2 directly for cos^4 falloff
    float r2 = dot(centered, centered) * 4.0; // scale to max ~1 at corners
    float cosTheta = 1.0 / sqrt(1.0 + r2);
    float cos4 = cosTheta * cosTheta * cosTheta * cosTheta;
    return lerp(1.0, cos4, strength);
}

// ── Sensor soft clipping (per-channel, green clips first) ──────────
float3 SensorClip(float3 color, float clipR, float clipG, float clipB)
{
    // Soft saturation: smoothstep ramp near clip point
    color.r = color.r / (1.0 + max(color.r - clipR, 0.0) * 0.5);
    color.g = color.g / (1.0 + max(color.g - clipG, 0.0) * 0.5);
    color.b = color.b / (1.0 + max(color.b - clipB, 0.0) * 0.5);
    return color;
}

// ── Procedural lens dirt (multi-octave simplex noise) ───────────────
// Simple procedural pattern modulated by lens effect brightness.
float2 Hash22(float2 p)
{
    p = float2(dot(p, float2(127.1, 311.7)),
               dot(p, float2(269.5, 183.3)));
    return frac(sin(p) * 43758.5453);
}

float Voronoi(float2 uv, float scale)
{
    float2 p = uv * scale;
    float2 ip = floor(p);
    float2 fp = frac(p);

    float minDist = 1.0;
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            float2 neighbor = float2(x, y);
            float2 cellPos = Hash22(ip + neighbor);
            float dist = length(neighbor + cellPos - fp);
            minDist = min(minDist, dist);
        }
    }
    return minDist;
}

float LensDirt(float2 uv)
{
    // Multi-octave Voronoi for dirt-like pattern
    float dirt = 0.0;
    dirt += Voronoi(uv, 8.0)  * 0.5;
    dirt += Voronoi(uv, 16.0) * 0.3;
    dirt += Voronoi(uv, 32.0) * 0.2;

    // Invert and shape: 1.0 = clean, 0.0 = dirty
    dirt = 1.0 - saturate(dirt * 1.5);
    dirt = dirt * dirt; // sharpen

    return dirt;
}

float4 main(VSOutput input) : SV_Target
{
    float2 uv = input.TexCoord;

    // ── Apply barrel/pincushion distortion ──────────────────────────
    float2 distortedUV = ApplyDistortion(uv, DistortionK1, DistortionK2);

    // ── Scene sampling with spectral CA ─────────────────────────────
    float3 scene;
    if (CAStrength > 0.0001)
    {
        // Blend between distorted spectral CA and simple distorted sample
        float3 spectralScene = SpectralCA(distortedUV, CAStrength, CauchyB);
        float4 directScene = SceneTex.SampleLevel(LinSamp, distortedUV, 0);
        // Use spectral result weighted by radial distance (CA is radial)
        float2 centered = distortedUV - 0.5;
        float r2 = dot(centered, centered) * 4.0;
        float caBlend = saturate(r2 * 2.0); // Spectral CA stronger at edges
        scene = lerp(directScene.rgb, spectralScene, caBlend);
    }
    else
    {
        scene = SceneTex.SampleLevel(LinSamp, distortedUV, 0).rgb;
    }

    // ── Sample lens effect layers ───────────────────────────────────
    float3 ghosts    = GhostTex.SampleLevel(LinSamp, uv, 0).rgb;
    float3 starburst = StarburstTex.SampleLevel(LinSamp, uv, 0).rgb;
    float3 flare     = FlareTex.SampleLevel(LinSamp, uv, 0).rgb;

    // ── Lens dirt (modulated by lens effect brightness) ─────────────
    float dirtMask = LensDirt(uv);
    float effectBrightness = dot(ghosts + starburst + flare, float3(0.2126, 0.7152, 0.0722));
    float dirtContrib = dirtMask * effectBrightness * DirtIntensity;

    // ── Composite all layers ────────────────────────────────────────
    float3 final_color = scene;
    final_color += ghosts    * GhostBlend;
    final_color += starburst * StarburstBlend;
    final_color += flare     * FlareBlend;
    final_color += dirtContrib; // Dirt is additive, only visible in backlit conditions

    // ── cos^4 optical vignette ──────────────────────────────────────
    float vignette = OpticalVignette(uv, VignetteStrength);
    final_color *= vignette;

    // ── Sensor clipping (green clips first -> characteristic pink) ──
    final_color = SensorClip(final_color, SensorClipR, SensorClipG, SensorClipB);

    return float4(final_color, 1.0);
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
        ID3DBlob* blob = nullptr;
        ID3DBlob* err  = nullptr;
        HRESULT hr = D3DCompile(source, strlen(source), name,
                                nullptr, nullptr, "main", "cs_5_0",
                                flags, 0, &blob, &err);
        if (FAILED(hr)) {
            if (err) {
                SKSE::log::error("LensRenderer: {} compile failed: {}", name,
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
            SKSE::log::error("LensRenderer: CreateComputeShader failed for {} (0x{:X})",
                             name, static_cast<uint32_t>(hr));
            return false;
        }
        return true;
    };

    // Compile 4 compute shaders
    if (!CompileCS("LensDownsample",  kDownsampleBrightCS, &m_downsampleCS)) return false;
    if (!CompileCS("LensGhost",       kGhostCS,            &m_ghostCS))      return false;
    if (!CompileCS("LensStarburst",   kStarburstCS,        &m_starburstCS))  return false;
    if (!CompileCS("LensAnamorphic",  kAnamorphicCS,       &m_anamorphicCS)) return false;

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
    HRESULT hr;

    // Helper: create an R16G16B16A16_FLOAT texture with SRV + UAV
    auto CreateHDRTexture = [&](const char* name, uint32_t w, uint32_t h,
                                 ID3D11Texture2D** outTex,
                                 ID3D11ShaderResourceView** outSRV,
                                 ID3D11UnorderedAccessView** outUAV) -> bool
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width      = w;
        desc.Height     = h;
        desc.MipLevels  = 1;
        desc.ArraySize  = 1;
        desc.Format     = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc = {1, 0};
        desc.Usage      = D3D11_USAGE_DEFAULT;
        desc.BindFlags  = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

        hr = m_device->CreateTexture2D(&desc, nullptr, outTex);
        if (FAILED(hr)) {
            SKSE::log::error("LensRenderer: failed to create {} texture (0x{:X})",
                             name, static_cast<uint32_t>(hr));
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels       = 1;
        hr = m_device->CreateShaderResourceView(*outTex, &srvDesc, outSRV);
        if (FAILED(hr)) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format             = DXGI_FORMAT_R16G16B16A16_FLOAT;
        uavDesc.ViewDimension      = D3D11_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;
        hr = m_device->CreateUnorderedAccessView(*outTex, &uavDesc, outUAV);
        if (FAILED(hr)) return false;

        return true;
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

    // ── Backbuffer copy (full-res, same format as backbuffer) ───────
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width      = m_screenW;
        desc.Height     = m_screenH;
        desc.MipLevels  = 1;
        desc.ArraySize  = 1;
        desc.Format     = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc = {1, 0};
        desc.Usage      = D3D11_USAGE_DEFAULT;
        desc.BindFlags  = D3D11_BIND_SHADER_RESOURCE;

        hr = m_device->CreateTexture2D(&desc, nullptr, &m_backbufferCopy);
        if (FAILED(hr)) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels       = 1;
        hr = m_device->CreateShaderResourceView(m_backbufferCopy, &srvDesc, &m_backbufferCopySRV);
        if (FAILED(hr)) return false;
    }

    // ── Constant buffers ────────────────────────────────────────────
    {
        D3D11_BUFFER_DESC desc = {};
        desc.Usage          = D3D11_USAGE_DYNAMIC;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;

        desc.ByteWidth = sizeof(DownsampleCBData);
        hr = m_device->CreateBuffer(&desc, nullptr, &m_downsampleCB);
        if (FAILED(hr)) return false;

        desc.ByteWidth = sizeof(GhostCBData);
        hr = m_device->CreateBuffer(&desc, nullptr, &m_ghostCB);
        if (FAILED(hr)) return false;

        desc.ByteWidth = sizeof(StarburstCBData);
        hr = m_device->CreateBuffer(&desc, nullptr, &m_starburstCB);
        if (FAILED(hr)) return false;

        desc.ByteWidth = sizeof(AnamorphicCBData);
        hr = m_device->CreateBuffer(&desc, nullptr, &m_anamorphicCB);
        if (FAILED(hr)) return false;

        desc.ByteWidth = sizeof(CompositeCBData);
        hr = m_device->CreateBuffer(&desc, nullptr, &m_compositeCB);
        if (FAILED(hr)) return false;
    }

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
        hr = m_device->CreateSamplerState(&sd, &m_linearClampSampler);
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

    // ── Helper: update CB ───────────────────────────────────────────
    auto UpdateCB = [&](ID3D11Buffer* cb, const void* data, size_t size) {
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx.context->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, data, size);
            ctx.context->Unmap(cb, 0);
        }
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
