//=============================================================================
//  ColorPipeline.cpp --- Full-spectrum color science pipeline
//
//  Single fullscreen pixel shader mega-pass with 12 independently togglable
//  stages.  Runs at PrePresent priority 50 (after BloomRenderer at 10,
//  before ToneMapManager at 100).
//
//  Pipeline:  Exposure -> Stevens -> Purkinje -> LocalTM -> Film/Contrast ->
//             Hunt -> ToneMap -> AgXPunchy -> Grade -> ExtGrade -> FinalOutput
//=============================================================================

#include "ColorPipeline.h"
#include "BloomRenderer.h"
#include "LuminanceHistogram.h"
#include "SharedGPUResources.h"
#include "D3D11Hook.h"

#include <SKSE/SKSE.h>
#include <cstring>
#include <cmath>
#include <cstdlib>

namespace SB
{

// =============================================================================
//  Embedded HLSL --- Color Pipeline Mega-Shader (Pixel Shader, SM5.0)
// =============================================================================

static const char kColorPipelinePS[] = R"HLSL(
// ---- Color Pipeline Mega-Shader ----
// 12 independently togglable stages, controlled by StageMask bitmask.

// ---- Stage bitmask constants (must match C++ enum) ----
static const uint STAGE_EXPOSURE  = 1u;
static const uint STAGE_STEVENS   = 2u;
static const uint STAGE_PURKINJE  = 4u;
static const uint STAGE_LOCALTM   = 8u;
static const uint STAGE_FILM      = 16u;
static const uint STAGE_CONTRAST  = 32u;
static const uint STAGE_HUNT      = 64u;
static const uint STAGE_TONEMAP   = 128u;
static const uint STAGE_AGXPUNCHY = 256u;
static const uint STAGE_GRADE     = 512u;
static const uint STAGE_EXTGRADE  = 1024u;
static const uint STAGE_DITHER    = 2048u;

cbuffer ColorPipelineCB : register(b0)
{
    // row 0
    float   CurrentEV;
    float   DeltaTime;
    uint    StageMask;
    int     ToneCurve;

    // row 1
    float   FilmToe;
    float   FilmShoulder;
    float   FilmGamma;
    float   FilmDensity;

    // row 2
    float   InterimageStrength;
    float   PurkinjeStrength;
    float   HuntStrength;
    float   LocalTMStrength;

    // row 3
    float   PrinterR;
    float   PrinterG;
    float   PrinterB;
    float   ColorTemp;

    // row 4
    float   ShadowTintA;
    float   ShadowTintB;
    float   ShadowTintStrength;
    float   HighlightTintA;

    // row 5
    float   HighlightTintB;
    float   HighlightTintStrength;
    float   CDLSlopeR;
    float   CDLSlopeG;

    // row 6
    float   CDLSlopeB;
    float   CDLSaturation;
    float   CDLOffsetR;
    float   CDLOffsetG;

    // row 7
    float   CDLOffsetB;
    float   CDLPower;
    float   BleachAmount;
    float   Saturation;

    // row 8
    float   DitherSeed;
    int     HDROutput;
    float   PaperWhiteNits;
    float   MaxNits;

    // row 9
    float   WhiteBalanceTemp;
    float   ExposureComp;
    float   PunchySaturation;
    float   StevensAdaptation;

    // row 10
    float   LiftR, LiftG, LiftB;
    float   GammaR;

    // row 11
    float   GammaG, GammaB;
    float   GainR, GainG;

    // row 12
    float   GainB;
    float   Vibrance;
    float   SCurveContrast;
    float   VanillaInfluence;

    // row 13
    float   FilmToeR, FilmToeG, FilmToeB;
    float   FilmShoulderR;

    // row 14
    float   FilmShoulderG, FilmShoulderB;
    float   FilmGammaR, FilmGammaG;

    // row 15
    float   FilmGammaB;
    float   FilmPrintToe;
    float   FilmPrintShoulder;
    float   FilmPrintGamma;
};

cbuffer VanillaParams : register(b7)
{
    // Row 0 — HDR
    float VP_EyeAdaptSpeed;
    float VP_BloomScale;
    float VP_BloomThreshold;
    float VP_SunlightScale;
    // Row 1 — Cinematic
    float VP_Saturation;
    float VP_Brightness;
    float VP_Contrast;
    float VP_TintAmount;
    // Row 2 — Tint + DOF
    float VP_TintR, VP_TintG, VP_TintB;
    float VP_DOFStrength;
    // Row 3 — DOF + IMOD
    float VP_DOFDistance, VP_DOFRange;
    float VP_IMODActive;
    float VP_IMODStrength;
};

Texture2D<float4> SceneColor : register(t0);  // Backbuffer copy
Texture2D<float4> BloomTex   : register(t1);  // BloomRenderer output

SamplerState LinearSampler : register(s0);
SamplerState PointSampler  : register(s1);


struct VSOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};


// =========================================================================
//  Color Space Utilities
// =========================================================================

float Luminance(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

// ---- Oklab (Bjorn Ottosson) ----

float3 LinearToOklab(float3 c)
{
    float l = 0.4122214708f * c.r + 0.5363325363f * c.g + 0.0514459929f * c.b;
    float m = 0.2119034982f * c.r + 0.6806995451f * c.g + 0.1073969566f * c.b;
    float s = 0.0883024619f * c.r + 0.2817188376f * c.g + 0.6299787005f * c.b;

    float l_ = pow(max(l, 0.0), 1.0 / 3.0);
    float m_ = pow(max(m, 0.0), 1.0 / 3.0);
    float s_ = pow(max(s, 0.0), 1.0 / 3.0);

    return float3(
        0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_,
        1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_,
        0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_
    );
}

float3 OklabToLinear(float3 lab)
{
    float l_ = lab.x + 0.3963377774f * lab.y + 0.2158037573f * lab.z;
    float m_ = lab.x - 0.1055613458f * lab.y - 0.0638541728f * lab.z;
    float s_ = lab.x - 0.0894841775f * lab.y - 1.2914855480f * lab.z;

    float l = l_ * l_ * l_;
    float m = m_ * m_ * m_;
    float s = s_ * s_ * s_;

    return float3(
        +4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
        -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
        -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s
    );
}


// =========================================================================
//  Stage 1: Exposure + White Balance (Tanner Helland)
// =========================================================================

float3 KelvinToRGB(float kelvin)
{
    // Tanner Helland approximation (1000K - 40000K)
    float temp = clamp(kelvin, 1000.0, 40000.0) / 100.0;
    float3 rgb;

    // Red
    if (temp <= 66.0)
        rgb.r = 1.0;
    else
        rgb.r = saturate(1.29293618 * pow(temp - 60.0, -0.1332047592));

    // Green
    if (temp <= 66.0)
        rgb.g = saturate(0.39008157876 * log(temp) - 0.63184144378);
    else
        rgb.g = saturate(1.12989086 * pow(temp - 60.0, -0.0755148492));

    // Blue
    if (temp >= 66.0)
        rgb.b = 1.0;
    else if (temp <= 19.0)
        rgb.b = 0.0;
    else
        rgb.b = saturate(0.54320678911 * log(temp - 10.0) - 1.19625408914);

    return rgb;
}

float3 ApplyExposureAndWB(float3 color, float ev, float wbTemp)
{
    // Apply auto-exposure
    float exposure = exp2(-ev);
    color *= exposure;

    // White balance: ratio of target temp to D65 (6500K)
    float3 wbTarget = KelvinToRGB(wbTemp);
    float3 wbD65    = KelvinToRGB(6500.0);
    float3 wbRatio  = wbD65 / max(wbTarget, 0.001);
    color *= wbRatio;

    return max(color, 0.0);
}


// =========================================================================
//  Stage 2: Stevens Effect
// =========================================================================

float3 ApplyStevens(float3 color, float adapted)
{
    // Stevens' power law: perceived contrast scales with adaptation luminance
    float exponent = pow(max(adapted, 0.0001), 0.4);
    float3 ratio = color / max(adapted, 0.0001);
    return adapted * pow(max(ratio, 0.0001), exponent);
}


// =========================================================================
//  Stage 3: Purkinje Shift (scotopic vision)
// =========================================================================

float3 ApplyPurkinje(float3 color, float strength)
{
    // Rod photoreceptor spectral sensitivity (scotopic V'(lambda) weights)
    float scotopic = dot(color, float3(0.062, 0.608, 0.330));

    // Desaturate toward scotopic (blue-shifted) perception
    return lerp(color, scotopic.xxx, strength);
}


// =========================================================================
//  Stage 4: Local Tone Mapping (bloom-driven)
// =========================================================================

float3 ApplyLocalTM(float3 color, float3 bloom, float strength)
{
    float localLum = max(Luminance(bloom), 0.001);
    float3 localContrast = color / localLum;

    float targetMidgrey = 0.18;
    return lerp(color, localContrast * targetMidgrey, strength);
}


// =========================================================================
//  Stage 5: FILM Pipeline (photochemical emulation)
// =========================================================================

float ApplyCharCurve(float x, float toe, float shoulder, float gamma)
{
    // Piecewise characteristic curve:
    //   toe region: cubic ease-in (shadows)
    //   linear mid: gamma power
    //   shoulder:   soft rolloff (highlights)
    float toeEnd = toe;
    float shoulderStart = shoulder;

    if (x < toeEnd)
    {
        // Cubic toe: x^3 / toeEnd^2  (smooth shadow rolloff)
        float t = x / max(toeEnd, 0.001);
        return toeEnd * t * t * t;
    }
    else if (x > shoulderStart)
    {
        // Soft shoulder: 1 - (1 - x)^2 style compression
        float t = (x - shoulderStart) / max(1.0 - shoulderStart, 0.001);
        float shoulderVal = shoulderStart + (1.0 - shoulderStart) * (1.0 - pow(max(1.0 - t, 0.0), 2.0));
        return shoulderVal;
    }
    else
    {
        return pow(max(x, 0.0), gamma);
    }
}

float3 ApplyFilm(float3 color,
                 float3 chToe, float3 chShoulder, float3 chGamma,
                 float printToe, float printShoulder, float printGamma,
                 float density, float interimage)
{
    // ---- Negative stock curves (per-channel) ----
    color.r = ApplyCharCurve(color.r, chToe.r, chShoulder.r, chGamma.r);
    color.g = ApplyCharCurve(color.g, chToe.g, chShoulder.g, chGamma.g);
    color.b = ApplyCharCurve(color.b, chToe.b, chShoulder.b, chGamma.b);

    // ---- Beer-Lambert density (subtractive color) ----
    color = exp(-density * max(1.0 - color, 0.0));

    // ---- Interimage effect (cross-channel developer inhibition) ----
    // Models how developing one dye layer inhibits adjacent layers
    float3 mid = color - 0.5;
    float3 inhibition = float3(
        0.0  * mid.r + 0.3 * mid.g + 0.1 * mid.b,   // R inhibited by G,B
        0.3  * mid.r + 0.0 * mid.g + 0.2 * mid.b,   // G inhibited by R,B
        0.15 * mid.r + 0.2 * mid.g + 0.0 * mid.b    // B inhibited by R,G
    );
    color -= inhibition * interimage;

    // ---- Print stock curve (combined) ----
    color.r = ApplyCharCurve(saturate(color.r), printToe, printShoulder, printGamma);
    color.g = ApplyCharCurve(saturate(color.g), printToe, printShoulder, printGamma);
    color.b = ApplyCharCurve(saturate(color.b), printToe, printShoulder, printGamma);

    return max(color, 0.0);
}


// =========================================================================
//  Stage 6: Log-domain Contrast (fallback when FILM disabled)
// =========================================================================

float3 ApplyLogContrast(float3 color, float contrastAmount)
{
    float3 logColor = log2(max(color, 1e-6));
    float logMid = log2(0.18);  // 18% grey in log space

    // Apply contrast around midpoint
    float contrast = 1.0 + contrastAmount;
    logColor = logMid + (logColor - logMid) * contrast;

    return exp2(logColor);
}


// =========================================================================
//  Stage 7: Hunt Effect
// =========================================================================

float3 ApplyHunt(float3 color, float strength)
{
    // Saturation increases with brightness (Hunt effect)
    float lum = Luminance(color);
    float satBoost = saturate(1.0 + strength * sqrt(max(lum, 0.0)));

    // Luminance-preserving saturation
    return max(lum + (color - lum) * satBoost, 0.0);
}


// =========================================================================
//  Stage 8: Tonemapping (8 operators)
// =========================================================================

// ---- 0: AgX (Troy Sobotka) ----

float3 AgXDefaultContrastApprox(float3 x)
{
    float3 x2 = x * x;
    float3 x4 = x2 * x2;
    return + 15.5     * x4 * x2
           - 40.14    * x4 * x
           + 31.96    * x4
           - 6.868    * x2 * x
           + 0.4298   * x2
           + 0.1191   * x
           - 0.00232;
}

float3 ToneMapAgX(float3 color)
{
    const float3x3 AgXInsetMatrix = float3x3(
        0.856627, 0.137318, 0.11189,
        0.0951212, 0.761241, 0.0767994,
        0.0482516, 0.101439, 0.811302
    );
    const float3x3 AgXOutsetMatrix = float3x3(
        1.1271,   -0.1413,  -0.14132,
        -0.11060,  1.15785, -0.11060,
        -0.016507,-0.016507, 1.25194
    );

    const float MinEV = -12.47393;
    const float MaxEV = 4.026069;

    color = mul(AgXInsetMatrix, color);
    color = max(color, 1e-10);
    color = log2(color);
    color = (color - MinEV) / (MaxEV - MinEV);
    color = saturate(color);
    color = AgXDefaultContrastApprox(color);
    color = mul(AgXOutsetMatrix, color);
    return saturate(color);
}

// ---- 1: ACES fitted (Stephen Hill) ----

float3 ToneMapACES(float3 color)
{
    const float3x3 ACESInputMat = float3x3(
        0.59719, 0.35458, 0.04823,
        0.07600, 0.90834, 0.01566,
        0.02840, 0.13383, 0.83777
    );
    const float3x3 ACESOutputMat = float3x3(
        1.60475, -0.53108, -0.07367,
       -0.10208,  1.10813, -0.00605,
       -0.00327, -0.07276,  1.07602
    );

    color = mul(ACESInputMat, color);
    float3 a = color * (color + 0.0245786) - 0.000090537;
    float3 b = color * (0.983729 * color + 0.4329510) + 0.238081;
    color = a / b;
    color = mul(ACESOutputMat, color);
    return saturate(color);
}

// ---- 2: Reinhard Extended ----

float3 ToneMapReinhard(float3 color)
{
    float whitePoint = 4.0;
    float3 num = color * (1.0 + color / (whitePoint * whitePoint));
    return num / (1.0 + color);
}

)HLSL"
// ---- Split to avoid MSVC C2026 (string literal > 16380 chars) ----
R"HLSL(
// ---- 3: Hejl-Burgess (Hejl 2010) ----

float3 ToneMapHejl(float3 color)
{
    float3 x = max(color - 0.004, 0.0);
    return (x * (6.2 * x + 0.5)) / (x * (6.2 * x + 1.7) + 0.06);
}

// ---- 4: Hable (Uncharted 2) ----

float3 HablePartial(float3 x)
{
    float A = 0.15;  // shoulder strength
    float B = 0.50;  // linear strength
    float C = 0.10;  // linear angle
    float D = 0.20;  // toe strength
    float E = 0.02;  // toe numerator
    float F = 0.30;  // toe denominator
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

float3 ToneMapHable(float3 color)
{
    float exposureBias = 2.0;
    float3 curr = HablePartial(color * exposureBias);
    float3 w = HablePartial(11.2);
    return curr / w;
}

// ---- 5: Lottes (AMD) ----

float3 ToneMapLottes(float3 color)
{
    float a = 1.6;   // contrast
    float d = 0.977; // shoulder
    float midIn = 0.18;
    float midOut = 0.267;

    float b = (-pow(midIn, a) + pow(midOut, a) * d) /
              ((pow(midOut, a * d) - pow(midIn, a)) * d);
    float c = (pow(midOut, a * d) * pow(midIn, a) - pow(midIn, a * d) * pow(midOut, a)) /
              ((pow(midOut, a * d) - pow(midIn, a)) * d);

    // Apply per channel
    color.r = pow(color.r, a) / (pow(color.r, a * d) * b + c);
    color.g = pow(color.g, a) / (pow(color.g, a * d) * b + c);
    color.b = pow(color.b, a) / (pow(color.b, a * d) * b + c);
    return saturate(color);
}

// ---- 6: Gran Turismo / Uchimura ----

float UchimuraSegment(float x, float P, float a, float m, float l, float c, float b)
{
    float l0 = ((P - m) * l) / a;
    float S0 = m + l0;
    float S1 = m + a * l0;
    float C2 = (a * P) / (P - S1);
    float CP = -C2 / P;

    float w0 = 1.0 - smoothstep(0.0, m, x);
    float w2 = step(m + l0, x);
    float w1 = 1.0 - w0 - w2;

    float T = m * pow(x / m, c) + b;
    float L = m + a * (x - m);
    float S = P - (P - S1) * exp(CP * (x - S0));

    return T * w0 + L * w1 + S * w2;
}

float3 ToneMapGranTurismo(float3 color)
{
    float P = 1.0;    // max brightness
    float a = 1.0;    // linear section contrast
    float m = 0.22;   // linear section start
    float l = 0.4;    // linear section length
    float c = 1.33;   // toe curvature
    float b = 0.0;    // black tightness

    color.r = UchimuraSegment(color.r, P, a, m, l, c, b);
    color.g = UchimuraSegment(color.g, P, a, m, l, c, b);
    color.b = UchimuraSegment(color.b, P, a, m, l, c, b);
    return saturate(color);
}

float3 ApplyToneMap(float3 color, int curve)
{
    if (curve == 0)      return ToneMapAgX(color);
    else if (curve == 1) return ToneMapACES(color);
    else if (curve == 2) return ToneMapReinhard(color);
    else if (curve == 3) return ToneMapHejl(color);
    else if (curve == 4) return ToneMapHable(color);
    else if (curve == 5) return ToneMapLottes(color);
    else if (curve == 6) return ToneMapGranTurismo(color);
    else                 return saturate(color);  // 7: None (linear clamp)
}


// =========================================================================
//  Stage 9: AgX Punchy Look
// =========================================================================

float3 ApplyAgXPunchy(float3 color, float punchySat)
{
    float luma = Luminance(color);
    return lerp(luma.xxx, color, 1.0 + punchySat);
}


// =========================================================================
//  Stage 10: GRADE Pipeline
// =========================================================================

float3 ApplyGrade(float3 color,
                  float3 printerLights, float gradeTemp,
                  float2 shadowTint, float shadowStr,
                  float2 highlightTint, float highlightStr,
                  float3 cdlSlope, float3 cdlOffset, float cdlPow,
                  float cdlSat, float bleach)
{
    float lum = Luminance(color);

    // ---- Highlight desaturation ----
    float highlightMask = smoothstep(0.5, 1.5, lum);
    color = lerp(color, lum.xxx, highlightMask * 0.3);

    // ---- Printer lights (exposure per channel, scale 1-50, 25 = unity) ----
    color *= printerLights / 25.0;

    // ---- Color temperature shift ----
    float3 tempShift = KelvinToRGB(gradeTemp);
    float3 tempD65   = KelvinToRGB(6500.0);
    color *= tempD65 / max(tempShift, 0.001);

    // ---- Split-toning in Oklab ----
    float3 oklab = LinearToOklab(max(color, 0.0));
    float L = saturate(oklab.x);

    // Shadows: tint low-luminance regions
    oklab.y += shadowTint.x * (1.0 - L) * shadowStr;
    oklab.z += shadowTint.y * (1.0 - L) * shadowStr;

    // Highlights: tint high-luminance regions
    oklab.y += highlightTint.x * L * highlightStr;
    oklab.z += highlightTint.y * L * highlightStr;

    color = max(OklabToLinear(oklab), 0.0);

    // ---- ASC-CDL: slope * color + offset, then pow ----
    color = cdlSlope * color + cdlOffset;
    color = pow(max(color, 0.0), cdlPow);

    // CDL saturation
    float cdlLum = Luminance(color);
    color = cdlLum + cdlSat * (color - cdlLum);

    // ---- Bleach bypass ----
    if (bleach > 0.0)
    {
        float bLum = Luminance(color);
        color = lerp(color, bLum.xxx, bleach);
    }

    return max(color, 0.0);
}


// =========================================================================
//  Stage 11: Extended Grading
// =========================================================================

float3 ApplyExtGrade(float3 color,
                     float3 lift, float3 gamma, float3 gain,
                     float vibrance, float sCurve)
{
    // ---- Lift / Gamma / Gain ----
    // gain * (color + lift * (1 - color)) ^ (1/gamma)
    color = gain * pow(max(color + lift * (1.0 - color), 0.0),
                       1.0 / max(gamma, 0.001));

    // ---- Vibrance (protects already-saturated colors) ----
    if (abs(vibrance) > 0.001)
    {
        float lum = Luminance(color);
        float maxC = max(max(color.r, color.g), color.b);
        float minC = min(min(color.r, color.g), color.b);
        float sat = (maxC > 0.001) ? (maxC - minC) / maxC : 0.0;

        // Lower saturation = more boost (protecting already-saturated)
        float vibranceMask = 1.0 - sat;
        float vibranceAmount = vibrance * vibranceMask;

        color = lum + (color - lum) * (1.0 + vibranceAmount);
    }

    // ---- S-curve contrast (cubic hermite in log domain) ----
    if (abs(sCurve) > 0.001)
    {
        float3 logC = log2(max(color, 1e-6));
        float logMid = log2(0.18);

        float3 t = saturate((logC - (logMid - 4.0)) / 8.0);  // normalize to 0-1
        // Cubic hermite: t^2 * (3 - 2t) centered at 0.5
        float3 s = t * t * (3.0 - 2.0 * t);
        float3 curved = lerp(t, s, sCurve);

        logC = (logMid - 4.0) + curved * 8.0;
        color = exp2(logC);
    }

    return max(color, 0.0);
}


// =========================================================================
//  Stage 12: Final Output
// =========================================================================

// PQ (Perceptual Quantizer, ST.2084) for HDR10
float3 LinearToPQ(float3 L)
{
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;

    float3 Lp = pow(saturate(L / 10000.0), m1);
    return pow((c1 + c2 * Lp) / (1.0 + c3 * Lp), m2);
}

// Simple hash for dithering
float TriangularNoise(float2 coord, float seed)
{
    float2 p = coord + float2(seed * 1.7, seed * 3.1);
    float n = frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453);
    float m = frac(sin(dot(p, float2(39.3467, 11.135))) * 24634.6345);
    return n + m - 1.0;  // triangular distribution [-1, 1]
}

float3 ApplyFinalOutput(float3 color, float satAmount, float2 screenPos,
                        float ditherSeed, int hdr, float paperWhite, float maxBright)
{
    // ---- Luminance-preserving saturation ----
    if (abs(satAmount - 1.0) > 0.001)
    {
        float lum = Luminance(color);
        color = lum + satAmount * (color - lum);
        color = max(color, 0.0);
    }

    // ── Vanilla ImageSpace adjustments (weather-aware) ───────────
    if (VanillaInfluence > 0.0)
    {
        float vLum = dot(color, float3(0.2126, 0.7152, 0.0722));
        float vSat = lerp(1.0, VP_Saturation, VanillaInfluence);
        color = lerp(vLum.xxx, color, vSat);
        color *= lerp(1.0, VP_Brightness, VanillaInfluence);
        color = lerp(0.5, color, lerp(1.0, VP_Contrast, VanillaInfluence));
        float3 vTint = float3(VP_TintR, VP_TintG, VP_TintB);
        color = lerp(color, color * vTint, VP_TintAmount * VanillaInfluence);
    }

    if (hdr)
    {
        // HDR path: scale to nits, compress, encode PQ
        float3 nits = color * paperWhite;

        float maxC = max(max(nits.r, nits.g), nits.b);
        if (maxC > maxBright)
        {
            float compress = maxBright / maxC;
            compress = lerp(compress, 1.0, 0.1);
            nits *= compress;
        }

        return LinearToPQ(nits);
    }
    else
    {
        // SDR path: sRGB gamma
        color = pow(max(color, 0.0), 1.0 / 2.2);

        // Triangular dither to prevent banding
        float dither = TriangularNoise(screenPos, ditherSeed);
        color += dither / 255.0;

        return saturate(color);
    }
}


// =========================================================================
//  Main Pixel Shader
// =========================================================================

float4 main(VSOut input) : SV_Target
{
    float2 uv = input.uv;
    float3 color = SceneColor.Sample(PointSampler, uv).rgb;

    // Skyrim's internal scene RT stores color in gamma ~1.6 space (not true linear,
    // not sRGB 2.2).  Confirmed via CS source (pow(color, 1.6) for gamma-to-linear).
    // Linearize before color science so all processing operates in true linear light.
    static const float SKYRIM_GAMMA = 1.6;
    color = pow(max(color, 0.0), SKYRIM_GAMMA);

    // ---- Stage 1: Exposure + White Balance ----
    if (StageMask & STAGE_EXPOSURE)
    {
        color = ApplyExposureAndWB(color, CurrentEV, WhiteBalanceTemp);
    }

    // ---- Stage 2: Stevens Effect ----
    if (StageMask & STAGE_STEVENS)
    {
        float adapted = exp2(-CurrentEV) * 0.18;  // adaptation luminance
        // Only engage in extreme lighting (very bright or very dark)
        float stevensGate = abs(CurrentEV) > 2.0 ? 1.0 : 0.0;
        if (stevensGate > 0.0)
        {
            color = ApplyStevens(color, max(adapted, 0.001) * StevensAdaptation);
        }
    }

    // ---- Stage 3: Purkinje Shift ----
    if (StageMask & STAGE_PURKINJE)
    {
        color = ApplyPurkinje(color, PurkinjeStrength);
    }

    // ---- Stage 4: Local Tone Mapping ----
    if (StageMask & STAGE_LOCALTM)
    {
        float3 bloom = BloomTex.Sample(LinearSampler, uv).rgb;
        color = ApplyLocalTM(color, bloom, LocalTMStrength);
    }

    // ---- Stage 5 or 6: Film OR Log Contrast ----
    if (StageMask & STAGE_FILM)
    {
        float3 chToe      = float3(FilmToeR, FilmToeG, FilmToeB);
        float3 chShoulder = float3(FilmShoulderR, FilmShoulderG, FilmShoulderB);
        float3 chGamma    = float3(FilmGammaR, FilmGammaG, FilmGammaB);

        color = ApplyFilm(color, chToe, chShoulder, chGamma,
                          FilmPrintToe, FilmPrintShoulder, FilmPrintGamma,
                          FilmDensity, InterimageStrength);
    }
    else if (StageMask & STAGE_CONTRAST)
    {
        // Log contrast as fallback (controlled by film gamma as contrast amount)
        color = ApplyLogContrast(color, FilmGamma);
    }

    // ---- Stage 7: Hunt Effect ----
    if (StageMask & STAGE_HUNT)
    {
        color = ApplyHunt(color, HuntStrength);
    }

    // ---- Stage 8: Tonemapping ----
    if (StageMask & STAGE_TONEMAP)
    {
        color = ApplyToneMap(color, ToneCurve);
    }

    // ---- Stage 9: AgX Punchy Look (only after AgX) ----
    if ((StageMask & STAGE_AGXPUNCHY) && ToneCurve == 0)
    {
        color = ApplyAgXPunchy(color, PunchySaturation);
    }

    // ---- Stage 10: GRADE Pipeline ----
    if (StageMask & STAGE_GRADE)
    {
        color = ApplyGrade(
            color,
            float3(PrinterR, PrinterG, PrinterB),
            ColorTemp,
            float2(ShadowTintA, ShadowTintB), ShadowTintStrength,
            float2(HighlightTintA, HighlightTintB), HighlightTintStrength,
            float3(CDLSlopeR, CDLSlopeG, CDLSlopeB),
            float3(CDLOffsetR, CDLOffsetG, CDLOffsetB),
            CDLPower, CDLSaturation, BleachAmount
        );
    }

    // ---- Stage 11: Extended Grading ----
    if (StageMask & STAGE_EXTGRADE)
    {
        color = ApplyExtGrade(
            color,
            float3(LiftR, LiftG, LiftB),
            float3(GammaR, GammaG, GammaB),
            float3(GainR, GainG, GainB),
            Vibrance, SCurveContrast
        );
    }

    // ---- Stage 12: Final Output (dither + encoding) ----
    // Always runs (but dithering is togglable)
    float ditherAmount = (StageMask & STAGE_DITHER) ? DitherSeed : 0.0;
    color = ApplyFinalOutput(color, Saturation, input.pos.xy,
                             ditherAmount, HDROutput,
                             PaperWhiteNits, MaxNits);

    return float4(color, 1.0);
}
)HLSL";


// =============================================================================
//  Initialize
// =============================================================================

bool ColorPipeline::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                                IDXGISwapChain* sc)
{
    if (m_initialized) return true;
    if (!dev || !ctx || !sc) return false;

    auto& rpm = RenderPassManager::Get();
    auto& pl  = RenderPipeline::Get();

    if (!rpm.IsInitialized() || !pl.IsInitialized()) {
        SKSE::log::error("ColorPipeline: prerequisites not initialized");
        return false;
    }

    m_device  = dev;
    m_context = ctx;

    // ---- Register fullscreen pixel shader pass ----
    m_mainPass = rpm.RegisterPass({
        .name     = "ColorPipeline",
        .psSource = kColorPipelinePS,
    });
    if (!m_mainPass) {
        SKSE::log::error("ColorPipeline: failed to register pixel shader pass");
        return false;
    }

    // ---- Create constant buffer ----
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth      = sizeof(ColorPipelineCBData);
        desc.Usage           = D3D11_USAGE_DYNAMIC;
        desc.BindFlags       = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags  = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(dev->CreateBuffer(&desc, nullptr, &m_mainCB))) {
            SKSE::log::error("ColorPipeline: failed to create constant buffer");
            return false;
        }
    }

    // ---- Create samplers ----
    {
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter   = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD   = D3D11_FLOAT32_MAX;
        if (FAILED(dev->CreateSamplerState(&sd, &m_linearSampler))) {
            SKSE::log::error("ColorPipeline: failed to create linear sampler");
            return false;
        }

        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        if (FAILED(dev->CreateSamplerState(&sd, &m_pointSampler))) {
            SKSE::log::error("ColorPipeline: failed to create point sampler");
            return false;
        }
    }

    // ---- Create initial scene color copy texture ----
    // Sized from the swapchain backbuffer at init time.  ExecutePass will
    // lazily recreate this if the game's scene RT has a different format/size.
    {
        ID3D11Texture2D* backTex = nullptr;
        if (SUCCEEDED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                     reinterpret_cast<void**>(&backTex))))
        {
            D3D11_TEXTURE2D_DESC bbDesc;
            backTex->GetDesc(&bbDesc);

            bbDesc.BindFlags      = D3D11_BIND_SHADER_RESOURCE;
            bbDesc.Usage          = D3D11_USAGE_DEFAULT;
            bbDesc.CPUAccessFlags = 0;

            if (FAILED(dev->CreateTexture2D(&bbDesc, nullptr, &m_backbufferCopy))) {
                backTex->Release();
                SKSE::log::error("ColorPipeline: failed to create backbuffer copy texture");
                return false;
            }

            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format                    = bbDesc.Format;
            srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels       = 1;
            srvDesc.Texture2D.MostDetailedMip = 0;

            if (FAILED(dev->CreateShaderResourceView(m_backbufferCopy, &srvDesc,
                                                      &m_backbufferCopySRV)))
            {
                backTex->Release();
                SKSE::log::error("ColorPipeline: failed to create backbuffer copy SRV");
                return false;
            }

            backTex->Release();
        } else {
            SKSE::log::error("ColorPipeline: failed to get backbuffer");
            return false;
        }
    }

    // ---- Register pipeline pass (PreUI, priority 50) ----
    m_pipelineHandle = pl.AddPass({
        .name     = "ColorPipeline",
        .stage    = PipelineStage::PreUI,
        .priority = 50,
        .enabled  = false,  // default disabled: verify baseline first
        .execute  = [this](PassContext& ctx) { ExecutePass(ctx); },
    });

    // Detect HDR from proxy
    m_hdrOutput = D3D11Hook::IsHDREnabled();

    m_initialized = true;
    SKSE::log::info("ColorPipeline: initialized (curve={}, stages=0x{:03X}, HDR={})",
                    static_cast<int>(m_toneCurve), m_stageMask, m_hdrOutput);
    return true;
}


// =============================================================================
//  Shutdown
// =============================================================================

void ColorPipeline::Shutdown()
{
    if (!m_initialized) return;

    if (m_pipelineHandle) {
        RenderPipeline::Get().RemovePass(m_pipelineHandle);
        m_pipelineHandle = 0;
    }

    auto SafeRelease = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };

    SafeRelease(m_mainCB);
    SafeRelease(m_linearSampler);
    SafeRelease(m_pointSampler);
    SafeRelease(m_backbufferCopy);
    SafeRelease(m_backbufferCopySRV);

    m_mainPass = 0;
    m_initialized = false;

    SKSE::log::info("ColorPipeline: shut down");
}


// =============================================================================
//  Per-frame execution (called by RenderPipeline at PreUI stage)
// =============================================================================

void ColorPipeline::ExecutePass(PassContext& ctx)
{
    if (!m_initialized || !m_enabled) return;

    auto& rpm  = RenderPassManager::Get();
    auto& hist = LuminanceHistogram::Get();

    // ---- Compute auto-exposure from histogram CPU readback ----
    if (hist.IsInitialized() && (m_stageMask & CPS_Exposure)) {
        auto& result = hist.GetResult();
        float avgLum = result.avgLuminance;
        float p50    = result.p50;

        // Geometric mean of average and median for robust metering
        float keyLum = std::sqrt((std::max)(avgLum, 0.001f) * (std::max)(p50, 0.001f));

        // Target EV: keyLum maps to 18% grey
        float targetEV = std::log2((std::max)(keyLum, 0.0001f) / 0.18f);
        targetEV -= m_exposureComp;
        targetEV = std::clamp(targetEV, -4.0f, 16.0f);

        // Temporal adaptation
        float dt    = (ctx.deltaTime > 0) ? ctx.deltaTime : 0.016f;
        float speed = std::clamp(2.0f * dt, 0.0f, 1.0f);
        m_currentEV = m_currentEV + (targetEV - m_currentEV) * speed;
        m_currentEV = std::clamp(m_currentEV, -20.0f, 20.0f);
    }

    // ---- Compute Purkinje strength from current EV ----
    // Ramps from 0 at EV>2 to full at EV<-2
    float purkinjeStr = 0.0f;
    if (m_stageMask & CPS_Purkinje) {
        purkinjeStr = std::clamp((2.0f - m_currentEV) / 4.0f, 0.0f, 1.0f);
    }

    // ---- Acquire scene texture + RTV ----------------------------------------
    // Mid-frame: the backbuffer does NOT contain the scene — the game renders
    // to an internal RT exposed via ctx.gameSceneRTV.  Extract the underlying
    // texture from that RTV first, falling back to the swapchain backbuffer
    // only when gameSceneRTV is null (PrePresent-time dispatch).
    ID3D11Texture2D*        sceneTex = nullptr;
    ID3D11RenderTargetView* sceneRTV = nullptr;
    bool ownSceneTex = false;  // true if we obtained sceneTex via QI/GetBuffer
    bool ownRTV      = false;  // true if we created the RTV and must release it

    if (ctx.gameSceneRTV) {
        // Mid-frame dispatch: game's active scene RT
        ID3D11Resource* res = nullptr;
        ctx.gameSceneRTV->GetResource(&res);
        if (res) {
            HRESULT hr = res->QueryInterface(__uuidof(ID3D11Texture2D),
                                              reinterpret_cast<void**>(&sceneTex));
            res->Release();
            if (FAILED(hr)) sceneTex = nullptr;
            ownSceneTex = (sceneTex != nullptr);
        }
        sceneRTV = ctx.gameSceneRTV;
        // Don't AddRef — D3D11StateBackup keeps it alive during dispatch
    } else {
        // PrePresent fallback: use swapchain backbuffer
        auto* sc = ctx.swapChain;
        if (!sc) sc = D3D11Hook::GetSwapChain();
        if (!sc) return;

        if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                  reinterpret_cast<void**>(&sceneTex))))
            return;
        ownSceneTex = true;

        // Create RTV from backbuffer
        D3D11_TEXTURE2D_DESC texDesc;
        sceneTex->GetDesc(&texDesc);
        D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format        = texDesc.Format;
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        m_device->CreateRenderTargetView(sceneTex, &rtvDesc, &sceneRTV);
        ownRTV = true;
    }

    if (!sceneTex || !sceneRTV) {
        if (ownSceneTex && sceneTex) sceneTex->Release();
        if (ownRTV && sceneRTV)      sceneRTV->Release();
        return;
    }

    // ---- Ensure copy texture matches scene RT format/size -----------------
    // The swapchain backbuffer is R8G8B8A8_UNORM but the game's internal
    // scene RT is often R16G16B16A16_FLOAT.  CopyResource requires identical
    // format+dimensions, so we lazily recreate the copy texture if needed.
    {
        D3D11_TEXTURE2D_DESC sceneDesc;
        sceneTex->GetDesc(&sceneDesc);

        D3D11_TEXTURE2D_DESC copyDesc;
        m_backbufferCopy->GetDesc(&copyDesc);

        if (sceneDesc.Format != copyDesc.Format ||
            sceneDesc.Width  != copyDesc.Width  ||
            sceneDesc.Height != copyDesc.Height)
        {
            SKSE::log::info("ColorPipeline: scene RT format/size changed — "
                "scene={}x{} fmt={}, copy={}x{} fmt={} — recreating",
                sceneDesc.Width, sceneDesc.Height, static_cast<int>(sceneDesc.Format),
                copyDesc.Width, copyDesc.Height, static_cast<int>(copyDesc.Format));

            if (m_backbufferCopySRV) { m_backbufferCopySRV->Release(); m_backbufferCopySRV = nullptr; }
            if (m_backbufferCopy)    { m_backbufferCopy->Release();    m_backbufferCopy = nullptr; }

            D3D11_TEXTURE2D_DESC newDesc = sceneDesc;
            newDesc.BindFlags      = D3D11_BIND_SHADER_RESOURCE;
            newDesc.Usage          = D3D11_USAGE_DEFAULT;
            newDesc.CPUAccessFlags = 0;
            newDesc.MiscFlags      = 0;

            HRESULT hr = m_device->CreateTexture2D(&newDesc, nullptr, &m_backbufferCopy);
            if (FAILED(hr)) {
                SKSE::log::error("ColorPipeline: failed to recreate copy texture");
                if (ownSceneTex) sceneTex->Release();
                if (ownRTV) sceneRTV->Release();
                return;
            }

            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format                    = newDesc.Format;
            srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels       = 1;
            srvDesc.Texture2D.MostDetailedMip = 0;

            hr = m_device->CreateShaderResourceView(m_backbufferCopy, &srvDesc,
                                                     &m_backbufferCopySRV);
            if (FAILED(hr)) {
                SKSE::log::error("ColorPipeline: failed to recreate copy SRV");
                if (ownSceneTex) sceneTex->Release();
                if (ownRTV) sceneRTV->Release();
                return;
            }

            SKSE::log::info("ColorPipeline: copy texture recreated as {}x{} fmt={}",
                newDesc.Width, newDesc.Height, static_cast<int>(newDesc.Format));
        }
    }

    ctx.context->CopyResource(m_backbufferCopy, sceneTex);
    if (ownSceneTex) sceneTex->Release();

    // ---- Fill constant buffer ----
    ColorPipelineCBData cb = {};

    cb.currentEV          = m_currentEV;
    cb.deltaTime          = (ctx.deltaTime > 0) ? ctx.deltaTime : 0.016f;
    cb.stageMask          = m_stageMask;
    cb.toneCurve          = static_cast<int32_t>(m_toneCurve);

    cb.filmToe            = m_filmToe;
    cb.filmShoulder       = m_filmShoulder;
    cb.filmGamma          = m_filmGamma;
    cb.filmDensity        = m_filmDensity;

    cb.interimageStrength = m_interimageStrength;
    cb.purkinjeStrength   = purkinjeStr;
    cb.huntStrength       = m_huntStrength;
    cb.localTMStrength    = m_localTMStrength;

    cb.printerR           = m_printerR;
    cb.printerG           = m_printerG;
    cb.printerB           = m_printerB;
    cb.colorTemp          = m_gradeColorTemp;

    cb.shadowTintA        = m_shadowTintA;
    cb.shadowTintB        = m_shadowTintB;
    cb.shadowTintStrength = m_shadowTintStrength;
    cb.highlightTintA     = m_highlightTintA;

    cb.highlightTintB         = m_highlightTintB;
    cb.highlightTintStrength  = m_highlightTintStrength;
    cb.cdlSlopeR              = m_cdlSlopeR;
    cb.cdlSlopeG              = m_cdlSlopeG;

    cb.cdlSlopeB          = m_cdlSlopeB;
    cb.cdlSaturation      = m_cdlSaturation;
    cb.cdlOffsetR         = m_cdlOffsetR;
    cb.cdlOffsetG         = m_cdlOffsetG;

    cb.cdlOffsetB         = m_cdlOffsetB;
    cb.cdlPower           = m_cdlPower;
    cb.bleachAmount       = m_bleachAmount;
    cb.saturation         = m_saturation;

    // Dither seed: cheap per-frame random
    cb.ditherSeed         = static_cast<float>(ctx.frameIndex) * 0.7548776662f;
    cb.hdrOutput          = m_hdrOutput ? 1 : 0;
    cb.paperWhiteNits     = m_paperWhiteNits;
    cb.maxNits            = m_maxNits;

    cb.whiteBalanceTemp   = m_whiteBalanceTemp;
    cb.exposureComp       = m_exposureComp;
    cb.punchySaturation   = m_punchySaturation;
    cb.stevensAdaptation  = 1.0f;

    // Extended grading
    cb.liftR  = m_liftR;  cb.liftG  = m_liftG;  cb.liftB  = m_liftB;
    cb.gammaR = m_gammaR;
    cb.gammaG = m_gammaG;  cb.gammaB = m_gammaB;
    cb.gainR  = m_gainR;   cb.gainG  = m_gainG;
    cb.gainB  = m_gainB;
    cb.vibrance       = m_vibrance;
    cb.sCurveContrast = m_sCurveContrast;
    cb.vanillaInfluence = m_vanillaInfluence;

    // Film per-channel presets (Kodak 500T defaults)
    cb.filmToeR = m_filmToe;       cb.filmToeG = m_filmToe * 0.95f;  cb.filmToeB = m_filmToe * 1.05f;
    cb.filmShoulderR = m_filmShoulder;
    cb.filmShoulderG = m_filmShoulder * 0.98f;
    cb.filmShoulderB = m_filmShoulder * 1.02f;
    cb.filmGammaR = m_filmGamma;
    cb.filmGammaG = m_filmGamma * 1.02f;
    cb.filmGammaB = m_filmGamma * 0.98f;
    cb.filmPrintToe      = 0.05f;   // Kodak 2383 print stock
    cb.filmPrintShoulder = 0.95f;
    cb.filmPrintGamma    = 0.85f;

    // ---- Upload CB ----
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(ctx.context->Map(m_mainCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        std::memcpy(mapped.pData, &cb, sizeof(cb));
        ctx.context->Unmap(m_mainCB, 0);
    }

    // ---- Build SRV array: t0 = scene color copy, t1 = bloom ----
    ID3D11ShaderResourceView* srvs[2] = { m_backbufferCopySRV, nullptr };

    auto& bloom = BloomRenderer::Get();
    if (bloom.IsInitialized() && bloom.IsEnabled()) {
        srvs[1] = bloom.GetBloomSRV();
    }

    uint32_t srvCount = srvs[1] ? 2u : 1u;

    // ---- Build sampler array: s0 = linear, s1 = point ----
    ID3D11SamplerState* samplers[2] = { m_linearSampler, m_pointSampler };

    // ---- Bind vanilla ImageSpace params CB at PS b7 ----
    auto* vanillaCB = SharedGPUResources::Get().GetVanillaParamsCB();
    ctx.context->PSSetConstantBuffers(SharedGPUResources::kVanillaParamsCBSlot, 1, &vanillaCB);

    // ---- Execute fullscreen pass ----
    rpm.Execute({
        .passID       = m_mainPass,
        .rtv          = sceneRTV,
        .srvs         = srvs,
        .srvCount     = srvCount,
        .samplers     = samplers,
        .samplerCount = 2,
        .cbData       = &cb,
        .cbSize       = sizeof(cb),
    });

    // ---- Unbind vanilla params CB ----
    {
        ID3D11Buffer* nullCB = nullptr;
        ctx.context->PSSetConstantBuffers(SharedGPUResources::kVanillaParamsCBSlot, 1, &nullCB);
    }

    if (ownRTV) sceneRTV->Release();
}

} // namespace SB
