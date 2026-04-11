//----------------------------------------------------------------------------------------------//
//                     ENB of the Elders - Post-Processing Pipeline                             //
//----------------------------------------------------------------------------------------------//
//                                                                                              //
//  10-pass cinematic post-processing pipeline:                                                 //
//                                                                                              //
//    Pass 0: Lens Diffusion      — Pro-Mist / Glimmerglass filter                              //
//    Pass 1: Film Halation       — Wavelength-dependent scatter through film base               //
//    Pass 2: Light Leaks         — FBM-based film burns with hash intermittency                 //
//    Pass 3: Chromatic Aberration — Radial lateral CA                                           //
//    Pass 4: Anamorphic Lens     — Horizontal blur, CA, field curvature, streaks                //
//    Pass 5: Gate Weave          — Film transport jitter with motion blur                       //
//    Pass 6: Optical Vignette    — cos^4(theta) + mechanical + cat-eye + wavelength             //
//    Pass 7: Letterbox           — Aspect ratio masking with projected-black grain              //
//    Pass 8: Film Damage         — Scratches, dust, hair, splices, chemical fading              //
//    Pass 9: Final Polish        — CAS sharpening + enhanced film grain + dither                //
//                                                                                              //
//  Effects 1-9 adapted from CinematicFX Suite v2.1                                             //
//  Chromatic aberration and enhanced chromatic grain are new additions.                         //
//  All effects default to disabled for backward compatibility.                                  //
//                                                                                              //
//  Zain Dana Harper - March 2026                                                               //
//                                                                                              //
//----------------------------------------------------------------------------------------------//


//=== ENB EXTERNAL VARIABLES ===//

float4  Timer;
float4  ScreenSize;
float   AdaptiveQuality;
float4  Weather;
float4  TimeOfDay1;
float4  TimeOfDay2;
float   ENightDayFactor;
float   EInteriorFactor;
float   FieldOfView;
float4  tempF1;
float4  tempF2;
float4  tempF3;
float4  tempInfo1;
float4  tempInfo2;


//=== TEXTURES ===//

Texture2D   TextureColor;          // Current pass input (output of previous technique)
Texture2D   TextureOriginal;       // Original scene (full-res, unmodified)
Texture2D   TextureDepth;          // Scene depth buffer
Texture2D   TextureDownsampled;    // 1024x1024 HDR scene (from bloom pipeline)
Texture2D   RenderTarget128;       // 128px bloom mip (from bloom pipeline)
Texture2D   RenderTargetRGBA64F;  // HDR float intermediate (used by SMAA edge storage)


//=== SAMPLERS ===//

SamplerState smpPoint
{
    Filter = MIN_MAG_MIP_POINT;
    AddressU = Clamp;
    AddressV = Clamp;
};

SamplerState smpLinear
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = Clamp;
    AddressV = Clamp;
};


//=== GLOBALS ===//

#include "enbglobals.fxh"
#include "Helper/SB_LensCore.fxh"
#include "Helper/SB_DenoiseCore.fxh"


//=== CONSTANTS ===//

static const float2 PixelSize = float2(ScreenSize.y, ScreenSize.y * ScreenSize.z);
static const float3 K_LUM    = float3(0.25, 0.60, 0.15);   // CinematicFX perceptual

#ifndef PI
#define PI      3.14159265358979
#endif
#ifndef TWO_PI
#define TWO_PI  6.28318530717959
#endif


//=== NOISE UTILITIES ===//

float CFX_Hash(float2 P)
{
    float3 P3 = frac(float3(P.xyx) * float3(443.8975, 397.2973, 491.1871));
    P3 += dot(P3, P3.yzx + 19.19);
    return frac((P3.x + P3.y) * P3.z);
}

float CFX_ValueNoise(float2 P)
{
    float2 I = floor(P);
    float2 F = frac(P);
    float2 U = F * F * F * (F * (F * 6.0 - 15.0) + 10.0);

    float A = CFX_Hash(I + float2(0, 0));
    float B = CFX_Hash(I + float2(1, 0));
    float C = CFX_Hash(I + float2(0, 1));
    float D = CFX_Hash(I + float2(1, 1));

    return lerp(lerp(A, B, U.x), lerp(C, D, U.x), U.y);
}

float CFX_FBM(float2 P, int Octaves, float Lacunarity, float Gain)
{
    float Value  = 0.0;
    float Amp    = 0.5;
    float2 Coord = P;

    [unroll] for(int i = 0; i < 4; i++)
    {
        if(i >= Octaves) break;
        Value += Amp * CFX_ValueNoise(Coord);
        Coord *= Lacunarity;
        Amp   *= Gain;
    }
    return Value;
}


//=== VERTEX SHADER STRUCTS ===//

struct VSInput
{
    float3 pos      : POSITION;
    float2 txcoord  : TEXCOORD0;
};

// Simple fullscreen pass — Diffusion, Halation, CA, Letterbox
struct CineFXVSOutput
{
    float4 pos      : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

// Light leaks — animation phases + scene average
struct LeakVSOutput
{
    float4 pos                     : SV_POSITION;
    float2 texcoord                : TEXCOORD0;
    nointerpolation float  Phase   : LEAK0;
    nointerpolation float  Phase2  : LEAK1;
    nointerpolation float  Phase3  : LEAK2;
    nointerpolation float  SceneAvg: LEAK3;
};

// Gate weave — jitter + velocity for motion blur + exposure
struct WeaveVSOutput
{
    float4 pos                       : SV_POSITION;
    float2 texcoord                  : TEXCOORD0;
    nointerpolation float2 Jitter    : WEAVE0;
    nointerpolation float  RotAngle  : WEAVE1;
    nointerpolation float  ZoomOff   : WEAVE2;
    nointerpolation float2 Velocity  : WEAVE3;
    nointerpolation float  ExpMul    : WEAVE4;
};

// Anamorphic — breathing scale
struct AnamVSOutput
{
    float4 pos                          : SV_POSITION;
    float2 texcoord                     : TEXCOORD0;
    nointerpolation float  BreathScale  : ANAM0;
};

// Optical vignette — precomputed parameters
struct VignetteVSOutput
{
    float4 pos                        : SV_POSITION;
    float2 texcoord                   : TEXCOORD0;
    nointerpolation float  InnerR     : VIGP0;
    nointerpolation float  FalloffR   : VIGP1;
};

// Film damage — precomputed timing + splice state
struct DamageVSOutput
{
    float4 pos                          : SV_POSITION;
    float2 texcoord                     : TEXCOORD0;
    nointerpolation float  FrameID      : DMGP0;
    nointerpolation float  FlickerMul   : DMGP1;
    nointerpolation float  ScratchX1    : DMGP2;
    nointerpolation float  ScratchX2    : DMGP3;
    nointerpolation float  HairPhase    : DMGP4;
    nointerpolation float  SpliceFlash  : DMGP5;
};


//=============================================================================//
//                         UI PARAMETERS                                       //
//=============================================================================//


// --- Enable Toggles ---

bool UIDIFF_Enable
<
    string UIName = "DIFFUSION | Enable Lens Diffusion";
> = {false};

bool UIFHALO_Enable
<
    string UIName = "HALATION | Enable Film Halation";
> = {false};

// Theme-aware enable helpers for postpass effects
#define PP_DIFF_ON  TB(UIDIFF_Enable, GetTheme().diffusionEnable)
#define PP_HALO_ON  TB(UIFHALO_Enable, GetTheme().halationEnable)
#define PP_VIG_ON   TB(UIVIG_Enable, GetTheme().vignetteEnable)

bool UILEAK_Enable
<
    string UIName = "LEAKS | Enable Light Leaks";
> = {false};

bool ui_CAEnable
<
    string UIName = "CA | Enable Chromatic Aberration";
> = {false};

bool UIANAM_Enable
<
    string UIName = "ANAM | Enable Anamorphic Lens";
> = {false};

bool UIWEAVE_Enable
<
    string UIName = "WEAVE | Enable Gate Weave";
> = {false};

bool UIVIG_Enable
<
    string UIName = "VIGNETTE | Enable Optical Vignette";
> = {false};

bool UILBOX_Enable
<
    string UIName = "LETTERBOX | Enable Letterbox";
> = {false};

bool UIDMG_Enable
<
    string UIName = "DAMAGE | Enable Film Damage";
> = {false};


// --- Lens Diffusion ---

float UIDIFF_Strength
<
    string UIName = "DIFFUSION | Strength";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.25};

float UIDIFF_HighlightBias
<
    string UIName = "DIFFUSION | Highlight Bias";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.1;
> = {1.5};

float UIDIFF_HighlightRetain
<
    string UIName = "DIFFUSION | Highlight Retention";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.50};

float UIDIFF_BlackLift
<
    string UIName = "DIFFUSION | Black Lift";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 0.1; float UIStep = 0.001;
> = {0.02};

float UIDIFF_Desaturate
<
    string UIName = "DIFFUSION | Desaturation";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.30};

float UIDIFF_LocalRadius
<
    string UIName = "DIFFUSION | Local Blur Radius (px)";
    string UIWidget = "Spinner";
    float UIMin = 0.5; float UIMax = 10.0; float UIStep = 0.1;
> = {2.0};

float UIDIFF_LocalWeight
<
    string UIName = "DIFFUSION | Local vs Wide Mix";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.35};

float3 UIDIFF_Tint
<
    string UIName = "DIFFUSION | Tint";
    string UIWidget = "Color";
> = {1.0, 0.98, 0.95};


// --- Film Halation ---

float UIFHALO_Strength
<
    string UIName = "HALATION | Strength";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.20};

float UIFHALO_Threshold
<
    string UIName = "HALATION | Brightness Threshold";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.60};

float UIFHALO_Knee
<
    string UIName = "HALATION | Threshold Knee";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 0.5; float UIStep = 0.01;
> = {0.25};

float UIFHALO_SpreadMix
<
    string UIName = "HALATION | Wide vs Narrow Spread";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.65};

float UIFHALO_WaveSpread
<
    string UIName = "HALATION | Wavelength Spread (R > B)";
    string UIWidget = "Spinner";
    float UIMin = 0.5; float UIMax = 3.0; float UIStep = 0.01;
> = {1.50};

float UIFHALO_Desaturate
<
    string UIName = "HALATION | Desaturation";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.40};

float3 UIFHALO_Tint
<
    string UIName = "HALATION | Color (film base)";
    string UIWidget = "Color";
> = {1.0, 0.75, 0.50};

bool UIFHALO_UseBloomRT
<
    string UIName = "HALATION | Use Bloom RT (faster, wider)";
> = {false};


// --- Light Leaks ---

float UILEAK_Intensity
<
    string UIName = "LEAKS | Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.15};

float UILEAK_Speed
<
    string UIName = "LEAKS | Animation Speed";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 3.0; float UIStep = 0.01;
> = {0.40};

float UILEAK_Coverage
<
    string UIName = "LEAKS | Coverage";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.45};

float UILEAK_EdgeBias
<
    string UIName = "LEAKS | Edge Bias";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.60};

float UILEAK_Softness
<
    string UIName = "LEAKS | Softness";
    string UIWidget = "Spinner";
    float UIMin = 0.5; float UIMax = 10.0; float UIStep = 0.1;
> = {2.0};

float UILEAK_SceneAdapt
<
    string UIName = "LEAKS | Scene Brightness Adapt";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.30};

float3 UILEAK_Color1
<
    string UIName = "LEAKS | Warm Color A";
    string UIWidget = "Color";
> = {1.0, 0.65, 0.20};

float3 UILEAK_Color2
<
    string UIName = "LEAKS | Warm Color B";
    string UIWidget = "Color";
> = {0.95, 0.35, 0.55};

int UILEAK_BlendMode
<
    string UIName = "LEAKS | Blend (0=Screen 1=Add 2=Softlight)";
    string UIWidget = "Spinner";
    int UIMin = 0; int UIMax = 2; int UIStep = 1;
> = {0};


// --- Chromatic Aberration ---

float ui_CAIntensity
<
    string UIName = "CA | Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 3.0; float UIStep = 0.01;
> = {0.50};

float ui_CAFalloff
<
    string UIName = "CA | Radial Falloff Power";
    string UIWidget = "Spinner";
    float UIMin = 0.5; float UIMax = 4.0; float UIStep = 0.1;
> = {2.0};

float ui_CABalance
<
    string UIName = "CA | R/B Balance (-1=R only, 1=B only)";
    string UIWidget = "Spinner";
    float UIMin = -1.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.0};

float ui_CACenterX
<
    string UIName = "CA | Center Offset X";
    string UIWidget = "Spinner";
    float UIMin = -0.5; float UIMax = 0.5; float UIStep = 0.01;
> = {0.0};

float ui_CACenterY
<
    string UIName = "CA | Center Offset Y";
    string UIWidget = "Spinner";
    float UIMin = -0.5; float UIMax = 0.5; float UIStep = 0.01;
> = {0.0};

bool ui_CASpectral
<
    string UIName = "CA | 6-Band Spectral Mode";
> = {false};


// --- Anamorphic Lens ---

float UIANAM_HBlur
<
    string UIName = "ANAM | Horizontal Blur";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01;
> = {1.5};

float UIANAM_FieldCurve
<
    string UIName = "ANAM | Field Curvature";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.20};

float UIANAM_ChromaH
<
    string UIName = "ANAM | Horizontal CA";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 3.0; float UIStep = 0.01;
> = {0.50};

float UIANAM_Mumps
<
    string UIName = "ANAM | Edge Stretch";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.15};

float UIANAM_Breathe
<
    string UIName = "ANAM | Breathing Amount";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.001;
> = {0.15};

float UIANAM_BreatheSpeed
<
    string UIName = "ANAM | Breathing Speed";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 3.0; float UIStep = 0.01;
> = {0.50};

float UIANAM_Streak
<
    string UIName = "ANAM | Vertical Streak";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01;
> = {0.20};


// --- Gate Weave ---

float UIWEAVE_AmplitudeX
<
    string UIName = "WEAVE | Horizontal Amplitude (px)";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01;
> = {0.40};

float UIWEAVE_AmplitudeY
<
    string UIName = "WEAVE | Vertical Amplitude (px)";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01;
> = {0.60};

float UIWEAVE_Speed
<
    string UIName = "WEAVE | Speed";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01;
> = {1.0};

float UIWEAVE_Rotation
<
    string UIName = "WEAVE | Rotational Jitter (deg)";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.001;
> = {0.05};

float UIWEAVE_Breathe
<
    string UIName = "WEAVE | Breathing";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 0.1; float UIStep = 0.0001;
> = {0.001};

float UIWEAVE_MotionBlur
<
    string UIName = "WEAVE | Motion Blur";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.30};

float UIWEAVE_ExpJitter
<
    string UIName = "WEAVE | Exposure Jitter";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 0.2; float UIStep = 0.001;
> = {0.02};


// --- Optical Vignette ---

float UIVIG_Strength
<
    string UIName = "VIGNETTE | Strength";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.50};

float UIVIG_Softness
<
    string UIName = "VIGNETTE | Plateau (center flat zone)";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.55};

float UIVIG_Power
<
    string UIName = "VIGNETTE | Falloff Steepness";
    string UIWidget = "Spinner";
    float UIMin = 0.5; float UIMax = 8.0; float UIStep = 0.1;
> = {3.5};

float UIVIG_Roundness
<
    string UIName = "VIGNETTE | Roundness";
    string UIWidget = "Spinner";
    float UIMin = 0.3; float UIMax = 1.5; float UIStep = 0.01;
> = {0.85};

float UIVIG_CatEye
<
    string UIName = "VIGNETTE | Cat-Eye Shape";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.30};

float UIVIG_ChromaShift
<
    string UIName = "VIGNETTE | Edge Color Shift";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.10};

float3 UIVIG_ChromaTint
<
    string UIName = "VIGNETTE | Edge Color Tint";
    string UIWidget = "Color";
> = {1.0, 0.92, 0.85};


// --- Letterbox ---

int UILBOX_Ratio
<
    string UIName = "LETTERBOX | Ratio (0=2.39 1=2.00 2=1.85 3=1.66 4=4:3 5=Custom)";
    string UIWidget = "Spinner";
    int UIMin = 0; int UIMax = 5; int UIStep = 1;
> = {0};

float UILBOX_CustomRatio
<
    string UIName = "LETTERBOX | Custom Ratio";
    string UIWidget = "Spinner";
    float UIMin = 1.0; float UIMax = 3.0; float UIStep = 0.01;
> = {2.39};

float UILBOX_Opacity
<
    string UIName = "LETTERBOX | Bar Opacity";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {1.0};

float UILBOX_EdgeSoftness
<
    string UIName = "LETTERBOX | Edge Softness";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 0.1; float UIStep = 0.001;
> = {0.005};

float3 UILBOX_BarColor
<
    string UIName = "LETTERBOX | Bar Color";
    string UIWidget = "Color";
> = {0.0, 0.0, 0.0};


// --- Film Damage ---

float UIDMG_ScratchInt
<
    string UIName = "DAMAGE | Scratch Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.20};

float UIDMG_ScratchWidth
<
    string UIName = "DAMAGE | Scratch Width";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.1;
> = {1.5};

float UIDMG_ScratchDensity
<
    string UIName = "DAMAGE | Scratch Density";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.30};

float UIDMG_DustInt
<
    string UIName = "DAMAGE | Dust Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.15};

float UIDMG_DustDensity
<
    string UIName = "DAMAGE | Dust Count";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.40};

float UIDMG_HairInt
<
    string UIName = "DAMAGE | Gate Hair Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.15};

float UIDMG_FlickerInt
<
    string UIName = "DAMAGE | Exposure Flicker";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 0.5; float UIStep = 0.01;
> = {0.05};

float UIDMG_SpliceInt
<
    string UIName = "DAMAGE | Splice Marks";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.10};

float UIDMG_ColorFade
<
    string UIName = "DAMAGE | Color Fading (vinegar syndrome)";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.0};

float3 UIDMG_VintageTint
<
    string UIName = "DAMAGE | Aging Tint";
    string UIWidget = "Color";
> = {1.0, 0.94, 0.82};


// --- Sharpening ---

bool ui_SharpenEnable
<
    string UIName = "SHARP | Enable CAS";
> = {true};

float ui_SharpenIntensity
<
    string UIName = "SHARP | Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01;
> = {0.5};


// --- Film Grain ---

bool ui_GrainEnable
<
    string UIName = "GRAIN | Enable Film Grain";
> = {true};

float ui_GrainIntensity
<
    string UIName = "GRAIN | Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 0.5; float UIStep = 0.001;
> = {0.12};

float ui_GrainSize
<
    string UIName = "GRAIN | Size";
    string UIWidget = "Spinner";
    float UIMin = 1.0; float UIMax = 4.0; float UIStep = 0.1;
> = {1.5};

bool ui_GrainChromatic
<
    string UIName = "GRAIN | Chromatic (per-channel)";
> = {false};

float ui_GrainBlueBoost
<
    string UIName = "GRAIN | Blue Layer Coarseness";
    string UIWidget = "Spinner";
    float UIMin = 1.0; float UIMax = 2.0; float UIStep = 0.01;
> = {1.4};

float ui_GrainMidtoneWeight
<
    string UIName = "GRAIN | Midtone Emphasis";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.5};

float ui_GrainSatResponse
<
    string UIName = "GRAIN | Saturation Response";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.3};

float ui_GrainDensityDep
<
    string UIName = "GRAIN | Density Dependence (shadows = more grain)";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.0};


//=============================================================================//
//                         VERTEX SHADERS                                      //
//=============================================================================//

CineFXVSOutput VS_CineFX(VSInput IN)
{
    CineFXVSOutput OUT;
    OUT.pos      = float4(IN.pos.xyz, 1.0);
    OUT.texcoord = IN.txcoord.xy;
    return OUT;
}


//=============================================================================//
//                                                                             //
//                    Pass 0: LENS DIFFUSION  (Pro-Mist)                       //
//                                                                             //
//  16-tap local Gaussian (1sigma + 2sigma) + TextureDownsampled far-field.    //
//  Screen blend composite preserving highlights.                              //
//  Smooth toe-curve black lift.                                               //
//                                                                             //
//=============================================================================//

float4 PS_Diffusion(CineFXVSOutput IN) : SV_Target
{
    float3 Sharp = TextureColor.SampleLevel(smpLinear, IN.texcoord, 0).rgb;

    [branch] if(!PP_DIFF_ON)
        return float4(Sharp, 1.0);

    float  SharpLuma = dot(Sharp, K_LUM);

    // Local Gaussian blur: 16-tap (1sigma ring + 2sigma ring)
    float  Rad1  = UIDIFF_LocalRadius * PixelSize.x;
    float  VRad1 = Rad1 * ScreenSize.z;
    float  Rad2  = Rad1 * 2.0;
    float  VRad2 = VRad1 * 2.0;

    float3 LocalBlur = Sharp;
    float  LocalW = 1.0;

    // Ring 1 cardinal: distance = 1sigma, weight = exp(-0.5) = 0.6065
    static const float W1C = 0.6065;
    LocalBlur += (TextureColor.SampleLevel(smpLinear, IN.texcoord + float2( Rad1, 0),     0).rgb
                + TextureColor.SampleLevel(smpLinear, IN.texcoord + float2(-Rad1, 0),     0).rgb
                + TextureColor.SampleLevel(smpLinear, IN.texcoord + float2(0,     VRad1), 0).rgb
                + TextureColor.SampleLevel(smpLinear, IN.texcoord + float2(0,    -VRad1), 0).rgb) * W1C;
    // Ring 1 diagonal: distance = sqrt(2)*sigma, weight = exp(-1.0) = 0.3679
    static const float W1D = 0.3679;
    float D1  = Rad1 * 0.7071;
    float DV1 = VRad1 * 0.7071;
    LocalBlur += (TextureColor.SampleLevel(smpLinear, IN.texcoord + float2( D1,  DV1), 0).rgb
                + TextureColor.SampleLevel(smpLinear, IN.texcoord + float2(-D1,  DV1), 0).rgb
                + TextureColor.SampleLevel(smpLinear, IN.texcoord + float2( D1, -DV1), 0).rgb
                + TextureColor.SampleLevel(smpLinear, IN.texcoord + float2(-D1, -DV1), 0).rgb) * W1D;
    LocalW += 4.0 * W1C + 4.0 * W1D;

    // Ring 2 cardinal: distance = 2sigma, weight = exp(-2.0) = 0.1353
    static const float W2C = 0.1353;
    LocalBlur += (TextureColor.SampleLevel(smpLinear, IN.texcoord + float2( Rad2, 0),     0).rgb
                + TextureColor.SampleLevel(smpLinear, IN.texcoord + float2(-Rad2, 0),     0).rgb
                + TextureColor.SampleLevel(smpLinear, IN.texcoord + float2(0,     VRad2), 0).rgb
                + TextureColor.SampleLevel(smpLinear, IN.texcoord + float2(0,    -VRad2), 0).rgb) * W2C;
    // Ring 2 diagonal: distance = 2*sqrt(2)*sigma, weight = exp(-4.0) = 0.0183
    static const float W2D = 0.0183;
    float D2  = Rad2 * 0.7071;
    float DV2 = VRad2 * 0.7071;
    LocalBlur += (TextureColor.SampleLevel(smpLinear, IN.texcoord + float2( D2,  DV2), 0).rgb
                + TextureColor.SampleLevel(smpLinear, IN.texcoord + float2(-D2,  DV2), 0).rgb
                + TextureColor.SampleLevel(smpLinear, IN.texcoord + float2( D2, -DV2), 0).rgb
                + TextureColor.SampleLevel(smpLinear, IN.texcoord + float2(-D2, -DV2), 0).rgb) * W2D;
    LocalW += 4.0 * W2C + 4.0 * W2D;

    LocalBlur /= LocalW;

    // Wide blur from TextureDownsampled
    float3 WideBlur = TextureDownsampled.SampleLevel(smpLinear, IN.texcoord, 0).rgb;
    WideBlur = WideBlur / (1.0 + WideBlur * 0.5);

    // Blend local and wide
    float3 Diffused = lerp(WideBlur, LocalBlur, UIDIFF_LocalWeight);

    // Luminance weighting
    float  DiffLuma = dot(Diffused, K_LUM);
    float  Weight = pow(saturate(DiffLuma), UIDIFF_HighlightBias);

    // Desaturation of scatter
    Diffused = lerp(Diffused, DiffLuma, UIDIFF_Desaturate);

    // Tint
    Diffused *= UIDIFF_Tint;

    // Screen blend composite (preserves highlights, avoids blowout)
    float3 DiffContrib = Diffused * TF(UIDIFF_Strength, GetTheme().diffusionStr) * Weight;
    float3 Screened = 1.0 - (1.0 - Sharp) * (1.0 - DiffContrib);

    // Highlight retention
    float  RetainMask = saturate(SharpLuma * 3.0) * UIDIFF_HighlightRetain;
    float3 Result = lerp(Screened, Sharp, RetainMask);

    // Black lift: smooth toe curve
    float  ToeMask = 1.0 / (1.0 + SharpLuma * 8.0);
    Result += UIDIFF_BlackLift * ToeMask;

    return float4(max(Result, 0.0), 1.0);
}


//=============================================================================//
//                                                                             //
//                    Pass 1: FILM HALATION                                    //
//                                                                             //
//  Per-channel wavelength-dependent scatter through film base.                //
//  Red scatters ~1.5x wider than blue (anti-halation backing absorption).     //
//  12-tap near-field + TextureDownsampled wide-field.                         //
//                                                                             //
//=============================================================================//

float4 PS_FilmHalation(CineFXVSOutput IN) : SV_Target
{
    float3 Color = TextureColor.SampleLevel(smpLinear, IN.texcoord, 0).rgb;

    [branch] if(!PP_HALO_ON)
        return float4(Color, 1.0);

    // Threshold on source luminance
    float SourceLuma = dot(Color, K_LUM);
    float KS = max(UIFHALO_Threshold - UIFHALO_Knee, 0.0);
    float KE = UIFHALO_Threshold + UIFHALO_Knee;
    float ThreshMask = smoothstep(KS, KE, SourceLuma);

    [branch] if(ThreshMask < 0.001)
        return float4(Color, 1.0);

    // Per-channel wavelength-dependent scatter
    float WS = UIFHALO_WaveSpread;
    float3 ChScale = float3(WS, 1.0, 1.0 / max(WS, 0.5));

    float3 Scatter;

    // Bloom-RT mode: single read from pre-blurred 128px mip (5 taps vs 15)
    [branch] if (UIFHALO_UseBloomRT)
    {
        float2 CenteredUV = IN.texcoord - 0.5;
        float  RTSpread = 0.02;
        Scatter.r = RenderTarget128.SampleLevel(smpLinear, 0.5 + CenteredUV * (1.0 + RTSpread * ChScale.r), 0).r;
        Scatter.g = RenderTarget128.SampleLevel(smpLinear, IN.texcoord, 0).g;
        Scatter.b = RenderTarget128.SampleLevel(smpLinear, 0.5 + CenteredUV * (1.0 - RTSpread * (ChScale.b - 1.0)), 0).b;

        // Bloom RT is already thresholded; re-apply halation threshold for consistency
        float scatterLuma = dot(Scatter, K_LUM);
        Scatter *= smoothstep(max(UIFHALO_Threshold - UIFHALO_Knee, 0.0),
                              UIFHALO_Threshold + UIFHALO_Knee, scatterLuma);
    }
    else
    {
        // Near-field: sample at 3 wavelength-dependent radii (12 reads total)
        float  BaseR = 3.5 * PixelSize.x;
        float3 Near = 0.0;

        // Red channel: widest scatter
        float RR  = BaseR * ChScale.r;
        float RRV = RR * ScreenSize.z;
        Near.r = (TextureColor.SampleLevel(smpLinear, IN.texcoord + float2( RR, 0), 0).r
                + TextureColor.SampleLevel(smpLinear, IN.texcoord + float2(-RR, 0), 0).r
                + TextureColor.SampleLevel(smpLinear, IN.texcoord + float2(0,  RRV), 0).r
                + TextureColor.SampleLevel(smpLinear, IN.texcoord + float2(0, -RRV), 0).r) * 0.25;

        // Green channel: medium scatter
        float RG  = BaseR * ChScale.g;
        float RGV = RG * ScreenSize.z;
        Near.g = (TextureColor.SampleLevel(smpLinear, IN.texcoord + float2( RG, 0), 0).g
                + TextureColor.SampleLevel(smpLinear, IN.texcoord + float2(-RG, 0), 0).g
                + TextureColor.SampleLevel(smpLinear, IN.texcoord + float2(0,  RGV), 0).g
                + TextureColor.SampleLevel(smpLinear, IN.texcoord + float2(0, -RGV), 0).g) * 0.25;

        // Blue channel: narrowest scatter
        float RB  = BaseR * ChScale.b;
        float RBV = RB * ScreenSize.z;
        Near.b = (TextureColor.SampleLevel(smpLinear, IN.texcoord + float2( RB, 0), 0).b
                + TextureColor.SampleLevel(smpLinear, IN.texcoord + float2(-RB, 0), 0).b
                + TextureColor.SampleLevel(smpLinear, IN.texcoord + float2(0,  RBV), 0).b
                + TextureColor.SampleLevel(smpLinear, IN.texcoord + float2(0, -RBV), 0).b) * 0.25;

        // Wide-field: TextureDownsampled with center-relative UV scale
        float  WideScale = 0.015;
        float2 CenteredUV = IN.texcoord - 0.5;
        float3 Wide;
        Wide.r = TextureDownsampled.SampleLevel(smpLinear, 0.5 + CenteredUV * (1.0 + WideScale * ChScale.r), 0).r;
        Wide.g = TextureDownsampled.SampleLevel(smpLinear, IN.texcoord, 0).g;
        Wide.b = TextureDownsampled.SampleLevel(smpLinear, 0.5 + CenteredUV * (1.0 - WideScale * (ChScale.b - 1.0)), 0).b;

        // Blend near and wide
        Scatter = lerp(Near, Wide, UIFHALO_SpreadMix);
    }

    // Apply threshold mask
    Scatter *= ThreshMask;

    // Anti-halation spectral tint
    Scatter *= UIFHALO_Tint;

    // Partial desaturation
    float HaloGray = dot(Scatter, K_LUM);
    Scatter = lerp(Scatter, HaloGray, UIFHALO_Desaturate);

    // Additive composite
    Color += Scatter * TF(UIFHALO_Strength, GetTheme().halationStr);

    return float4(max(Color, 0.0), 1.0);
}


//=============================================================================//
//                                                                             //
//                    Pass 2: LIGHT LEAKS / FILM BURNS                         //
//                                                                             //
//  Three FBM noise layers with irrational drift ratios.                       //
//  Hash-based sporadic intermittency.  Scene brightness adaptation.           //
//                                                                             //
//=============================================================================//

LeakVSOutput VS_LightLeaks(VSInput IN)
{
    LeakVSOutput OUT;
    OUT.pos      = float4(IN.pos.xyz, 1.0);
    OUT.texcoord = IN.txcoord.xy;

    float T = Timer.x * 16777.216 * UILEAK_Speed;
    OUT.Phase  = T;
    OUT.Phase2 = T * 0.6180339887;
    OUT.Phase3 = T * 0.4142135623;

    // Estimate scene average brightness for adaptation
    float3 AvgA = TextureDownsampled.SampleLevel(smpLinear, float2(0.3, 0.3), 0).rgb;
    float3 AvgB = TextureDownsampled.SampleLevel(smpLinear, float2(0.7, 0.7), 0).rgb;
    float3 AvgC = TextureDownsampled.SampleLevel(smpLinear, float2(0.5, 0.5), 0).rgb;
    OUT.SceneAvg = dot((AvgA + AvgB + AvgC) / 3.0, K_LUM);

    return OUT;
}

float4 PS_LightLeaks(LeakVSOutput IN) : SV_Target
{
    float3 Color = TextureColor.SampleLevel(smpLinear, IN.texcoord, 0).rgb;

    [branch] if(!UILEAK_Enable)
        return float4(Color, 1.0);

    float2 UV = IN.texcoord;
    float  Softness = max(UILEAK_Softness, 0.5);

    // Three FBM noise layers for organic shapes
    float2 NUV1 = UV * Softness + float2(IN.Phase * 0.08, IN.Phase * 0.05);
    float  L1 = CFX_FBM(NUV1, 3, 2.37, 0.55);

    float2 NUV2 = UV * Softness * 1.7 + float2(-IN.Phase3 * 0.12, IN.Phase3 * 0.07);
    float  L2 = CFX_FBM(NUV2, 3, 2.13, 0.50);

    float2 NUV3 = UV * Softness * 0.6 + float2(IN.Phase2 * 0.05, -IN.Phase * 0.03);
    float  L3 = CFX_FBM(NUV3, 2, 1.87, 0.60);

    float  Leak = L1 * L2 * (0.5 + L3);

    // Coverage threshold
    float  CovThresh = 1.0 - UILEAK_Coverage;
    Leak = smoothstep(CovThresh, CovThresh + 0.25, Leak);

    // Edge bias
    float2 CenteredUV = UV * 2.0 - 1.0;
    float  EdgeDist = max(abs(CenteredUV.x), abs(CenteredUV.y));
    float  EdgeMask = smoothstep(0.2, 1.0, EdgeDist);
    Leak *= 1.0 - UILEAK_EdgeBias * (1.0 - EdgeMask);

    // Hash-based sporadic intermittency
    float  Period = floor(IN.Phase * 0.15);
    float  Burst = CFX_Hash(float2(Period, Period * 1.73));
    float  BurstMask = smoothstep(0.35, 0.50, Burst);
    float  BurstPhase = frac(IN.Phase * 0.15);
    float  FadeEnv = smoothstep(0.0, 0.2, BurstPhase) * smoothstep(1.0, 0.7, BurstPhase);
    Leak *= BurstMask * FadeEnv;

    // Color modulation
    float  ColorPhase = CFX_ValueNoise(UV * 1.3 + float2(IN.Phase2 * 0.15, 0));
    float3 LeakColor = lerp(UILEAK_Color1, UILEAK_Color2, ColorPhase);

    // Scene brightness adaptation
    float  SceneBoost = lerp(1.0, 1.0 / max(IN.SceneAvg * 2.0, 0.2), UILEAK_SceneAdapt);
    SceneBoost = min(SceneBoost, 3.0);

    float3 LeakRGB = LeakColor * Leak * UILEAK_Intensity * SceneBoost;

    // Blend
    float3 Result;
    [branch] if(UILEAK_BlendMode == 0)
    {
        Result = 1.0 - (1.0 - Color) * (1.0 - LeakRGB);
    }
    else if(UILEAK_BlendMode == 1)
    {
        Result = Color + LeakRGB;
    }
    else
    {
        float3 A = Color;
        float3 B = LeakRGB;
        float3 Lo = 2.0 * A * B + A * A * (1.0 - 2.0 * B);
        float3 Hi = 2.0 * A * (1.0 - B) + sqrt(max(A, 0.001)) * (2.0 * B - 1.0);
        Result = (B < 0.5) ? Lo : Hi;
    }

    return float4(max(Result, 0.0), 1.0);
}


//=============================================================================//
//                                                                             //
//                    Pass 3: CHROMATIC ABERRATION                              //
//                                                                             //
//  Radial lateral CA: R/B offset proportional to distance^2 from center.      //
//  Green stays centered (reference wavelength).                               //
//                                                                             //
//=============================================================================//

float4 PS_ChromaticAberration(CineFXVSOutput IN) : SV_Target
{
    float3 Color = TextureColor.SampleLevel(smpPoint, IN.texcoord, 0).rgb;

    if(ui_CAEnable)
    {
        float2 center = float2(0.5 + ui_CACenterX, 0.5 + ui_CACenterY);

        if (ui_CASpectral)
        {
            // 6-band spectral CA (Wyman-Sloan-Shirley CIE, Cauchy dispersion)
            // fovScale: postpass doesn't have FieldOfView, use 1.0
            Color = EvalSpectralCA(TextureColor, smpLinear,
                                   IN.texcoord, center,
                                   ui_CAIntensity * 0.5, 1.0);
        }
        else
        {
            // Classic 3-channel radial CA
            float2 delta  = IN.texcoord - center;
            float  r2     = dot(delta, delta);

            float caAmount = ui_CAIntensity * pow(r2, ui_CAFalloff * 0.5) * 0.01;

            float rScale = 1.0 - caAmount * (1.0 - ui_CABalance * 0.5);
            float bScale = 1.0 + caAmount * (1.0 + ui_CABalance * 0.5);

            float2 uvR = center + delta * rScale;
            float2 uvB = center + delta * bScale;

            Color.r = TextureColor.SampleLevel(smpLinear, uvR, 0).r;
            Color.b = TextureColor.SampleLevel(smpLinear, uvB, 0).b;
        }
    }

    return float4(Color, 1.0);
}


//=============================================================================//
//                                                                             //
//                    Pass 4: ANAMORPHIC LENS  v2.1                            //
//                                                                             //
//  Bilinear-optimized 13-effective-tap horizontal blur (7 reads).             //
//  Per-channel CA before blur.  Field curvature.  Breathing.                  //
//  Vertical streak highlight bloom.                                           //
//                                                                             //
//=============================================================================//

AnamVSOutput VS_Anamorphic(VSInput IN)
{
    AnamVSOutput OUT;
    OUT.pos      = float4(IN.pos.xyz, 1.0);
    OUT.texcoord = IN.txcoord.xy;

    float T = Timer.x * 16777.216 * UIANAM_BreatheSpeed;
    float Breath = sin(T * 0.617) * 0.50
                 + sin(T * 1.059) * 0.30
                 + sin(T * 1.732) * 0.20;
    OUT.BreathScale = 1.0 + Breath * UIANAM_Breathe * 0.01;

    return OUT;
}

float4 PS_Anamorphic(AnamVSOutput IN) : SV_Target
{
    float3 Color = TextureColor.SampleLevel(smpLinear, IN.texcoord, 0).rgb;

    [branch] if(!UIANAM_Enable)
        return float4(Color, 1.0);

    float2 UV = IN.texcoord;

    // Focus breathing (horizontal-only scale)
    // NOTE: No [branch] on inner conditionals — d3dcompiler_46 falsely reports
    // X4014 for SampleLevel inside nested [branch] blocks.
    if(UIANAM_Breathe > 0.001)
    {
        UV.x = (UV.x - 0.5) * IN.BreathScale + 0.5;
    }

    // Edge stretch (mumps)
    if(UIANAM_Mumps > 0.001)
    {
        float2 C = UV * 2.0 - 1.0;
        float HStretch = (C.x * C.x + C.y * C.y * 0.3) * UIANAM_Mumps * 0.12;
        UV.x += C.x * HStretch;
    }

    // Horizontal chromatic aberration — applied BEFORE blur
    float2 UV_R = UV;
    float2 UV_G = UV;
    float2 UV_B = UV;

    bool hasChromaH = (UIANAM_ChromaH > 0.01);
    if(hasChromaH)
    {
        float2 C = UV * 2.0 - 1.0;
        float  Fringe = length(C) * UIANAM_ChromaH * PixelSize.x * 3.0;
        UV_R.x = UV.x - Fringe;
        UV_B.x = UV.x + Fringe;
    }

    // Bilinear-optimized horizontal blur (13 effective taps from 7 reads)
    bool hasBlur = (UIANAM_HBlur > 0.01);
    if(hasBlur)
    {
        // Field curvature — worse aberrations off-axis
        float2 C = UV * 2.0 - 1.0;
        float  FieldMul = 1.0 + dot(C, C) * UIANAM_FieldCurve * 2.0;
        float  Sigma = UIANAM_HBlur * FieldMul;

        // Gaussian weights at integer pixel distances
        float S2 = 2.0 * Sigma * Sigma;
        float W0 = 1.0;
        float W1 = exp(-1.0  / S2);
        float W2 = exp(-4.0  / S2);
        float W3 = exp(-9.0  / S2);
        float W4 = exp(-16.0 / S2);
        float W5 = exp(-25.0 / S2);
        float W6 = exp(-36.0 / S2);

        // Bilinear pairs
        float BWA = W1 + W2;
        float BWB = W3 + W4;
        float BWC = W5 + W6;
        float BOA = (1.0 * W1 + 2.0 * W2) / max(BWA, 0.0001);
        float BOB = (3.0 * W3 + 4.0 * W4) / max(BWB, 0.0001);
        float BOC = (5.0 * W5 + 6.0 * W6) / max(BWC, 0.0001);

        float WTotal = W0 + 2.0 * (BWA + BWB + BWC);
        float InvWT  = 1.0 / WTotal;

        float  StepX = PixelSize.x;

        // Green channel (reference wavelength) — always computed when blur active
        float3 BlurG;
        BlurG  = TextureColor.SampleLevel(smpLinear, UV_G, 0).rgb * W0;
        BlurG += TextureColor.SampleLevel(smpLinear, UV_G + float2( BOA * StepX, 0), 0).rgb * BWA;
        BlurG += TextureColor.SampleLevel(smpLinear, UV_G + float2(-BOA * StepX, 0), 0).rgb * BWA;
        BlurG += TextureColor.SampleLevel(smpLinear, UV_G + float2( BOB * StepX, 0), 0).rgb * BWB;
        BlurG += TextureColor.SampleLevel(smpLinear, UV_G + float2(-BOB * StepX, 0), 0).rgb * BWB;
        BlurG += TextureColor.SampleLevel(smpLinear, UV_G + float2( BOC * StepX, 0), 0).rgb * BWC;
        BlurG += TextureColor.SampleLevel(smpLinear, UV_G + float2(-BOC * StepX, 0), 0).rgb * BWC;

        // Red channel (shifted left) — always compute, cost is texture reads
        float3 BlurR;
        BlurR  = TextureColor.SampleLevel(smpLinear, UV_R, 0).rgb * W0;
        BlurR += TextureColor.SampleLevel(smpLinear, UV_R + float2( BOA * StepX, 0), 0).rgb * BWA;
        BlurR += TextureColor.SampleLevel(smpLinear, UV_R + float2(-BOA * StepX, 0), 0).rgb * BWA;
        BlurR += TextureColor.SampleLevel(smpLinear, UV_R + float2( BOB * StepX, 0), 0).rgb * BWB;
        BlurR += TextureColor.SampleLevel(smpLinear, UV_R + float2(-BOB * StepX, 0), 0).rgb * BWB;
        BlurR += TextureColor.SampleLevel(smpLinear, UV_R + float2( BOC * StepX, 0), 0).rgb * BWC;
        BlurR += TextureColor.SampleLevel(smpLinear, UV_R + float2(-BOC * StepX, 0), 0).rgb * BWC;

        // Blue channel (shifted right) — always compute
        float3 BlurB;
        BlurB  = TextureColor.SampleLevel(smpLinear, UV_B, 0).rgb * W0;
        BlurB += TextureColor.SampleLevel(smpLinear, UV_B + float2( BOA * StepX, 0), 0).rgb * BWA;
        BlurB += TextureColor.SampleLevel(smpLinear, UV_B + float2(-BOA * StepX, 0), 0).rgb * BWA;
        BlurB += TextureColor.SampleLevel(smpLinear, UV_B + float2( BOB * StepX, 0), 0).rgb * BWB;
        BlurB += TextureColor.SampleLevel(smpLinear, UV_B + float2(-BOB * StepX, 0), 0).rgb * BWB;
        BlurB += TextureColor.SampleLevel(smpLinear, UV_B + float2( BOC * StepX, 0), 0).rgb * BWC;
        BlurB += TextureColor.SampleLevel(smpLinear, UV_B + float2(-BOC * StepX, 0), 0).rgb * BWC;

        // Select per-channel or green-only based on chroma enabled
        Color = hasChromaH
            ? float3(BlurR.r, BlurG.g, BlurB.b) * InvWT
            : BlurG * InvWT;
    }
    else if(hasChromaH)
    {
        // CA without blur: simple per-channel fetch
        Color.r = TextureColor.SampleLevel(smpLinear, UV_R, 0).r;
        Color.g = TextureColor.SampleLevel(smpLinear, UV_G, 0).g;
        Color.b = TextureColor.SampleLevel(smpLinear, UV_B, 0).b;
    }

    // Vertical streak highlight bloom
    if(UIANAM_Streak > 0.01)
    {
        float  Luma = dot(Color, float3(0.2126, 0.7152, 0.0722));
        float  StreakThresh = saturate((Luma - 0.7) * 3.0);

        if(StreakThresh > 0.01)
        {
            float  StreakLen = UIANAM_Streak * PixelSize.y * 25.0;

            float3 Streak = Color * 0.30;
            Streak += TextureColor.SampleLevel(smpLinear, UV + float2(0,  StreakLen * 0.5), 0).rgb * 0.22;
            Streak += TextureColor.SampleLevel(smpLinear, UV + float2(0, -StreakLen * 0.5), 0).rgb * 0.22;
            Streak += TextureColor.SampleLevel(smpLinear, UV + float2(0,  StreakLen),       0).rgb * 0.13;
            Streak += TextureColor.SampleLevel(smpLinear, UV + float2(0, -StreakLen),       0).rgb * 0.13;

            float3 StreakContrib = Streak * StreakThresh * UIANAM_Streak;
            Color = 1.0 - (1.0 - Color) * (1.0 - StreakContrib);
        }
    }

    return float4(max(Color, 0.0), 1.0);
}


//=============================================================================//
//                                                                             //
//                    Pass 5: GATE WEAVE / FILM JITTER                         //
//                                                                             //
//  Frame-quantized weave (24fps timing).  4 incommensurate sinusoids.         //
//  Analytical velocity for directional motion blur.                           //
//  Rotation, breathing, per-frame exposure jitter.                            //
//                                                                             //
//=============================================================================//

WeaveVSOutput VS_GateWeave(VSInput IN)
{
    WeaveVSOutput OUT;
    OUT.pos      = float4(IN.pos.xyz, 1.0);
    OUT.texcoord = IN.txcoord.xy;

    // Frame-quantized time (snaps to 24fps intervals)
    float  RawT = Timer.x * 16777.216 * UIWEAVE_Speed;
    float  FrameRate = 24.0;
    float  FrameID = floor(RawT * FrameRate);
    float  T = FrameID / FrameRate;

    // Translational weave (4 incommensurate sinusoids)
    float JX = sin(T * 1.000) * 0.40
             + sin(T * 1.618) * 0.30
             + sin(T * 2.236) * 0.20
             + sin(T * 3.317) * 0.10;

    float JY = sin(T * 0.877) * 0.40
             + sin(T * 1.414) * 0.30
             + sin(T * 2.094) * 0.20
             + sin(T * 2.718) * 0.10;

    OUT.Jitter.x = JX * UIWEAVE_AmplitudeX * PixelSize.x;
    OUT.Jitter.y = JY * UIWEAVE_AmplitudeY * PixelSize.y;

    // Analytical velocity (derivatives of sinusoid sums)
    float VX = cos(T * 1.000) * 1.000 * 0.40
             + cos(T * 1.618) * 1.618 * 0.30
             + cos(T * 2.236) * 2.236 * 0.20
             + cos(T * 3.317) * 3.317 * 0.10;

    float VY = cos(T * 0.877) * 0.877 * 0.40
             + cos(T * 1.414) * 1.414 * 0.30
             + cos(T * 2.094) * 2.094 * 0.20
             + cos(T * 2.718) * 2.718 * 0.10;

    OUT.Velocity.x = VX * UIWEAVE_AmplitudeX * PixelSize.x / max(FrameRate, 1.0);
    OUT.Velocity.y = VY * UIWEAVE_AmplitudeY * PixelSize.y / max(FrameRate, 1.0);

    // Rotational jitter
    float RotDeg = sin(T * 0.731) * 0.5 + sin(T * 1.137) * 0.3 + sin(T * 2.053) * 0.2;
    OUT.RotAngle = RotDeg * UIWEAVE_Rotation * 0.01745329;

    // Breathing
    float Breath = sin(T * 0.317) * 0.5 + sin(T * 0.519) * 0.3 + sin(T * 0.883) * 0.2;
    OUT.ZoomOff = Breath * UIWEAVE_Breathe;

    // Per-frame exposure jitter
    float ExpHash = frac(sin(FrameID * 127.1) * 43758.5453);
    OUT.ExpMul = 1.0 + (ExpHash * 2.0 - 1.0) * UIWEAVE_ExpJitter;

    return OUT;
}

float4 PS_GateWeave(WeaveVSOutput IN) : SV_Target
{
    float3 Color = TextureColor.SampleLevel(smpLinear, IN.texcoord, 0).rgb;

    [branch] if(!UIWEAVE_Enable)
        return float4(Color, 1.0);

    float2 UV = IN.texcoord + IN.Jitter;

    // Rotational jitter
    [branch] if(UIWEAVE_Rotation > 0.001)
    {
        float2 Offset = UV - 0.5;
        float  CosA = cos(IN.RotAngle);
        float  SinA = sin(IN.RotAngle);
        UV = 0.5 + float2(
            Offset.x * CosA - Offset.y * SinA,
            Offset.x * SinA + Offset.y * CosA
        );
    }

    // Breathing
    [branch] if(UIWEAVE_Breathe > 0.0001)
    {
        UV = (UV - 0.5) * (1.0 + IN.ZoomOff) + 0.5;
    }

    // Directional motion blur along weave velocity
    [branch] if(UIWEAVE_MotionBlur > 0.01)
    {
        float2 Vel = IN.Velocity * UIWEAVE_MotionBlur * 15.0;

        Color = TextureColor.SampleLevel(smpLinear, UV, 0).rgb * 0.30;
        Color += TextureColor.SampleLevel(smpLinear, UV + Vel * 0.50, 0).rgb * 0.20;
        Color += TextureColor.SampleLevel(smpLinear, UV - Vel * 0.50, 0).rgb * 0.20;
        Color += TextureColor.SampleLevel(smpLinear, UV + Vel * 1.00, 0).rgb * 0.15;
        Color += TextureColor.SampleLevel(smpLinear, UV - Vel * 1.00, 0).rgb * 0.15;
    }
    else
    {
        Color = TextureColor.SampleLevel(smpLinear, UV, 0).rgb;
    }

    // Exposure jitter
    Color *= IN.ExpMul;

    return float4(max(Color, 0.0), 1.0);
}


//=============================================================================//
//                                                                             //
//                    Pass 6: OPTICAL VIGNETTE  v2.1                           //
//                                                                             //
//  Combined cos^4(arctan(r)) natural + mechanical plateau-steep falloff.      //
//  Cat-eye aperture shape at edges.  Per-channel wavelength-dependent         //
//  vignetting (blue falls off faster than red).                               //
//                                                                             //
//=============================================================================//

VignetteVSOutput VS_OptVignette(VSInput IN)
{
    VignetteVSOutput OUT;
    OUT.pos      = float4(IN.pos.xyz, 1.0);
    OUT.texcoord = IN.txcoord.xy;

    float Plateau = UIVIG_Softness;
    OUT.InnerR   = Plateau * Plateau;
    OUT.FalloffR = 1.0 / max(1.0 - OUT.InnerR, 0.001);

    return OUT;
}

float4 PS_OptVignette(VignetteVSOutput IN) : SV_Target
{
    float3 Color = TextureColor.SampleLevel(smpLinear, IN.texcoord, 0).rgb;

    [branch] if(!PP_VIG_ON)
        return float4(Color, 1.0);

    float2 C = IN.texcoord * 2.0 - 1.0;

    // Aspect-correct
    C.x *= ScreenSize.z * UIVIG_Roundness;

    // Cat-eye aperture shape at edges
    float R2_raw = dot(C, C);
    float R2 = R2_raw;

    [branch] if(UIVIG_CatEye > 0.01)
    {
        float2 Dir = normalize(C + 0.0001);
        float2 Tangent = float2(-Dir.y, Dir.x);
        float  TangentDist = abs(dot(C, Tangent));
        float  RadialDist  = abs(dot(C, Dir));
        float  CatEyeFactor = 1.0 + UIVIG_CatEye * R2_raw * 0.8;
        R2 = RadialDist * RadialDist + TangentDist * TangentDist * CatEyeFactor;
    }

    // Combined natural + mechanical vignette
    float  MaxR2  = ScreenSize.z * ScreenSize.z * UIVIG_Roundness * UIVIG_Roundness + 1.0;
    float  NormR2 = R2 / MaxR2;

    // Mechanical: flat plateau, then steep power-law falloff
    float  T = saturate((NormR2 - IN.InnerR) * IN.FalloffR);
    float  MechVig = 1.0 - pow(T, UIVIG_Power);

    // Natural: cos^4(arctan(r)) = 1/(1 + r^2)^2
    float  CosR = NormR2 * 0.5;
    float  NatFactor = 1.0 + CosR;
    float  NatVig = 1.0 / (NatFactor * NatFactor);

    // Combined mask
    float  VigMask = MechVig * NatVig;
    float vigStr = TF(UIVIG_Strength, GetTheme().vignetteStr);
    VigMask = lerp(1.0, VigMask, vigStr);

    // Per-channel wavelength-dependent vignetting
    [branch] if(UIVIG_ChromaShift > 0.01)
    {
        float3 ChPow = 1.0 + (1.0 - UIVIG_ChromaTint) * UIVIG_ChromaShift * 0.5;
        float3 VigRGB;
        VigRGB.r = lerp(1.0, pow(abs(VigMask), ChPow.r), vigStr);
        VigRGB.g = lerp(1.0, pow(abs(VigMask), ChPow.g), vigStr);
        VigRGB.b = lerp(1.0, pow(abs(VigMask), ChPow.b), vigStr);
        Color *= VigRGB;
    }
    else
    {
        Color *= VigMask;
    }

    return float4(max(Color, 0.0), 1.0);
}


//=============================================================================//
//                                                                             //
//                    Pass 7: CINEMATIC LETTERBOX                              //
//                                                                             //
//  Aspect ratio masking with projected-black grain in bars.                   //
//                                                                             //
//=============================================================================//

float4 PS_Letterbox(CineFXVSOutput IN) : SV_Target
{
    float3 Color = TextureColor.SampleLevel(smpLinear, IN.texcoord, 0).rgb;

    [branch] if(!UILBOX_Enable)
        return float4(Color, 1.0);

    // Target aspect ratio
    float TargetAspect;
    [branch] if(UILBOX_Ratio == 0)      TargetAspect = 2.39;
    else if(UILBOX_Ratio == 1) TargetAspect = 2.00;
    else if(UILBOX_Ratio == 2) TargetAspect = 1.85;
    else if(UILBOX_Ratio == 3) TargetAspect = 1.66;
    else if(UILBOX_Ratio == 4) TargetAspect = 1.333;
    else                       TargetAspect = UILBOX_CustomRatio;

    float ScreenAspect = ScreenSize.z;
    float BarMask = 0.0;
    float2 UV = IN.texcoord;
    float  Softness = max(UILBOX_EdgeSoftness, 0.0001);

    [branch] if(TargetAspect > ScreenAspect + 0.01)
    {
        float BarFrac = (1.0 - ScreenAspect / TargetAspect) * 0.5;
        float TopBar = smoothstep(BarFrac, BarFrac - Softness, UV.y);
        float BotBar = smoothstep(1.0 - BarFrac, 1.0 - BarFrac + Softness, UV.y);
        BarMask = max(TopBar, BotBar);
    }
    else if(TargetAspect < ScreenAspect - 0.01)
    {
        float BarFrac = (1.0 - TargetAspect / ScreenAspect) * 0.5;
        float LeftBar  = smoothstep(BarFrac, BarFrac - Softness, UV.x);
        float RightBar = smoothstep(1.0 - BarFrac, 1.0 - BarFrac + Softness, UV.x);
        BarMask = max(LeftBar, RightBar);
    }

    // Projected-black grain in bars
    float3 BarFill = UILBOX_BarColor;
    if(BarMask > 0.01)
    {
        float BarGrain = CFX_Hash(IN.pos.xy + Timer.z * 5.588) * 0.015;
        BarFill += BarGrain;
    }

    Color = lerp(Color, BarFill, BarMask * UILBOX_Opacity);

    return float4(Color, 1.0);
}


//=============================================================================//
//                                                                             //
//                    Pass 8: FILM DAMAGE  v2.1                                //
//                                                                             //
//  Accumulated damage from repeated projection: scratches, dust, gate hair,   //
//  sprocket burns, splice marks, exposure flicker, chemical fading.           //
//                                                                             //
//=============================================================================//

DamageVSOutput VS_FilmDamage(VSInput IN)
{
    DamageVSOutput OUT;
    OUT.pos      = float4(IN.pos.xyz, 1.0);
    OUT.texcoord = IN.txcoord.xy;

    float T = Timer.x * 16777.216;
    float FrameRate = 24.0;
    OUT.FrameID = floor(T * FrameRate);

    // Exposure flicker (3 incommensurate sinusoids)
    float FlickPhase = T * 3.0;
    float Flicker = sin(FlickPhase * 6.2831853) * 0.4
                  + sin(FlickPhase * 1.618 * 6.2831853) * 0.3
                  + sin(FlickPhase * 2.414 * 6.2831853) * 0.15;
    OUT.FlickerMul = 1.0 + Flicker * UIDMG_FlickerInt * 0.5;

    // Scratch positions (persist for 12-20 frames)
    float ScratchGroup = floor(OUT.FrameID / 16.0);
    OUT.ScratchX1 = frac(sin(ScratchGroup * 127.1 + 7.3) * 43758.5453);
    OUT.ScratchX2 = frac(sin(ScratchGroup * 269.5 + 13.7) * 43758.5453);

    // Gate hair phase
    OUT.HairPhase = T * 0.15;

    // Splice marks (every ~2000 frames at 24fps = ~83 seconds)
    float  ReelPeriod  = 2000.0;
    float  FrameInReel = fmod(OUT.FrameID, ReelPeriod);
    float  SpliceFrame = step(FrameInReel, 2.0);
    float  SpliceDecay = 1.0 - FrameInReel / 3.0;
    OUT.SpliceFlash = SpliceFrame * max(SpliceDecay, 0.0) * UIDMG_SpliceInt;

    return OUT;
}

float4 PS_FilmDamage(DamageVSOutput IN) : SV_Target
{
    float3 Color = TextureColor.SampleLevel(smpLinear, IN.texcoord, 0).rgb;

    [branch] if(!UIDMG_Enable)
        return float4(Color, 1.0);

    float2 UV = IN.texcoord;

    // --- VERTICAL SCRATCHES ---
    [branch] if(UIDMG_ScratchInt > 0.01)
    {
        // Scratch 1 — primary (wider)
        float ScrX1 = IN.ScratchX1;
        ScrX1 += sin(UV.y * 12.0 + IN.FrameID * 0.01) * 0.003;

        float  WidthBase = PixelSize.x * (1.0 + UIDMG_ScratchWidth);
        float  WidthMod1 = 1.0 + sin(UV.y * 25.0 + IN.ScratchX1 * 50.0) * 0.4
                               + sin(UV.y * 63.0 + IN.ScratchX1 * 127.0) * 0.2;
        float  ScrW1 = WidthBase * max(WidthMod1, 0.3);

        float  D1 = abs(UV.x - ScrX1);
        float  Scratch1 = smoothstep(ScrW1, ScrW1 * 0.25, D1);

        float  FadeIn  = smoothstep(0.0, 0.08, UV.y);
        float  FadeOut = smoothstep(1.0, 0.92, UV.y);
        Scratch1 *= FadeIn * FadeOut;

        float  Vis1 = step(0.75 - UIDMG_ScratchDensity * 0.5,
                          frac(sin(IN.ScratchX1 * 31.37) * 43758.5453));

        // Scratch 2 — secondary (thinner)
        float ScrX2 = IN.ScratchX2;
        ScrX2 += sin(UV.y * 8.0 + IN.FrameID * 0.02) * 0.002;
        float  WidthMod2 = 1.0 + sin(UV.y * 31.0 + IN.ScratchX2 * 73.0) * 0.35;
        float  ScrW2 = WidthBase * 0.5 * max(WidthMod2, 0.3);
        float  D2 = abs(UV.x - ScrX2);
        float  Scratch2 = smoothstep(ScrW2, ScrW2 * 0.2, D2) * FadeIn * FadeOut;
        float  Vis2 = step(0.85 - UIDMG_ScratchDensity * 0.3,
                          frac(sin(IN.ScratchX2 * 73.91) * 43758.5453));

        // Bright/dark selection per scratch
        float BD1 = step(0.4, frac(ScrX1 * 173.7));
        float BD2 = step(0.4, frac(ScrX2 * 217.3));

        float  ScrVal1 = Scratch1 * Vis1 * UIDMG_ScratchInt * lerp(-0.35, 0.55, BD1);
        float  ScrVal2 = Scratch2 * Vis2 * UIDMG_ScratchInt * 0.5 * lerp(-0.25, 0.45, BD2);
        Color = max(Color + ScrVal1 + ScrVal2, 0.0);
    }

    // --- DUST / DIRT ---
    [branch] if(UIDMG_DustInt > 0.01)
    {
        float  DustThresh = 1.0 - UIDMG_DustDensity;

        [unroll] for(int i = 0; i < 8; i++)
        {
            float  LifeSeed = frac(sin(float(i) * 43.37 + 17.1) * 43758.5453);
            float  Lifetime = floor(LifeSeed * 3.0) + 1.0;
            float  GroupID  = floor(IN.FrameID / Lifetime);

            float  Seed = float(i) * 127.1 + GroupID * 7.13;
            float2 DustPos;
            DustPos.x = frac(sin(Seed * 12.9898) * 43758.5453);
            DustPos.y = frac(sin(Seed * 78.233)  * 43758.5453);

            // Some particles are elongated fibers
            float  FiberSeed = frac(Seed * 0.4271);
            float2 Diff = UV - DustPos;
            Diff.x *= ScreenSize.z;

            float  FiberAngle = FiberSeed * 6.2831853;
            float2 FiberDir = float2(cos(FiberAngle), sin(FiberAngle));
            float  AlongFiber = abs(dot(Diff, FiberDir));
            float  AcrossFiber = length(Diff - FiberDir * dot(Diff, FiberDir));
            float  Elongation = (FiberSeed > 0.7) ? 3.0 : 1.0;

            float  SizeBase = (1.0 + LifeSeed * 2.0) * PixelSize.x;
            float  Dist = (FiberSeed > 0.7)
                        ? max(AlongFiber / Elongation, AcrossFiber)
                        : length(Diff);

            float  Speck = smoothstep(SizeBase, SizeBase * 0.25, Dist);

            float  ParticleVis = step(DustThresh, frac(sin(float(i) * 91.17 + GroupID * 3.31) * 43758.5453));
            float  BrightDark = step(0.7, frac(Seed * 91.17));

            Color += Speck * ParticleVis * UIDMG_DustInt * lerp(-0.5, 0.8, BrightDark);
        }
    }

    // --- GATE HAIR ---
    [branch] if(UIDMG_HairInt > 0.01)
    {
        float HairGroup = floor(IN.HairPhase * 0.3);
        float HairVis = step(0.90, frac(sin(HairGroup * 173.7) * 43758.5453));

        if(HairVis > 0.5)
        {
            float  SideSelect = step(0.5, frac(sin(HairGroup * 47.13) * 43758.5453));
            float  HairBaseX = lerp(
                0.08 - sin(IN.HairPhase * 0.7) * 0.05,
                0.92 + sin(IN.HairPhase * 0.7) * 0.05,
                SideSelect);

            float FiberX = HairBaseX
                         + sin(UV.y * 6.0 + IN.HairPhase) * 0.025
                         + sin(UV.y * 14.0 + IN.HairPhase * 0.4) * 0.008
                         + cos(UV.y * 3.0 + IN.HairPhase * 1.3) * 0.012
                         + sin(UV.y * 23.0 + IN.HairPhase * 0.7) * 0.004;

            float HairDist = abs(UV.x - FiberX);

            float ThickVar = 1.0 + sin(UV.y * 40.0 + IN.HairPhase * 2.0) * 0.3
                                 + sin(UV.y * 97.0 + HairGroup) * 0.15;
            float HairWidth = PixelSize.x * 1.5 * max(ThickVar, 0.4);
            float HairMask = smoothstep(HairWidth, HairWidth * 0.15, HairDist);

            float VExtent = smoothstep(0.10, 0.22, UV.y) * smoothstep(0.90, 0.75, UV.y);
            Color -= HairMask * VExtent * UIDMG_HairInt * 0.6;
        }
    }

    // --- SPROCKET HOLE BURNS ---
    [branch] if(UIDMG_ScratchInt > 0.05)
    {
        float  SprTop = smoothstep(0.005, 0.015, UV.y) * smoothstep(0.045, 0.035, UV.y);
        float  SprBot = smoothstep(0.955, 0.965, UV.y) * smoothstep(0.995, 0.985, UV.y);
        float  SprocketMask = max(SprTop, SprBot);

        float  SprocketNoise = frac(sin(UV.x * 800.0 + IN.FrameID * 0.003) * 43758.5453);
        float  SprocketStreak = smoothstep(0.3, 0.7, SprocketNoise);

        Color += SprocketMask * SprocketStreak * UIDMG_ScratchInt * 0.15;
    }

    // --- SPLICE MARKS ---
    [branch] if(IN.SpliceFlash > 0.01)
    {
        Color = lerp(Color, 1.5, IN.SpliceFlash * 0.8);

        float  SpliceShift = IN.SpliceFlash * 0.03;
        float2 SplicedUV = UV + float2(0, SpliceShift);
        float3 SpliceSample = TextureColor.SampleLevel(smpLinear, SplicedUV, 0).rgb;
        Color = lerp(Color, SpliceSample + 0.3, IN.SpliceFlash * 0.5);

        float SpliceLine = smoothstep(0.002, 0.0, abs(UV.y - 0.5 + IN.SpliceFlash * 0.05));
        Color += SpliceLine * IN.SpliceFlash;
    }

    // --- EXPOSURE FLICKER ---
    Color *= IN.FlickerMul;

    // --- CHEMICAL COLOR FADING ---
    [branch] if(UIDMG_ColorFade > 0.01)
    {
        float  FadeLuma = dot(Color, float3(0.2126, 0.7152, 0.0722));
        float3 FadedColor = lerp(Color, FadeLuma, UIDMG_ColorFade * 0.6);
        FadedColor *= UIDMG_VintageTint;
        Color = lerp(Color, FadedColor, UIDMG_ColorFade);
    }

    return float4(max(Color, 0.0), 1.0);
}


//=============================================================================//
//                                                                             //
//                    Pass 9: FINAL POLISH                                     //
//                                                                             //
//  CAS Sharpening + Enhanced Film Grain + Dither                              //
//                                                                             //
//=============================================================================//

float4 PS_PostPass(CineFXVSOutput IN) : SV_Target
{
    float3 color = TextureColor.SampleLevel(smpPoint, IN.texcoord, 0).rgb;

    // --- CAS Sharpening ---
    [branch] if (ui_SharpenEnable)
    {
        float3 n = TextureColor.SampleLevel(smpPoint, IN.texcoord + float2(0, -PixelSize.y), 0).rgb;
        float3 s = TextureColor.SampleLevel(smpPoint, IN.texcoord + float2(0,  PixelSize.y), 0).rgb;
        float3 e = TextureColor.SampleLevel(smpPoint, IN.texcoord + float2( PixelSize.x, 0), 0).rgb;
        float3 w = TextureColor.SampleLevel(smpPoint, IN.texcoord + float2(-PixelSize.x, 0), 0).rgb;

        float lumaC = dot(color, float3(0.2126, 0.7152, 0.0722));
        float lumaN = dot(n, float3(0.2126, 0.7152, 0.0722));
        float lumaS = dot(s, float3(0.2126, 0.7152, 0.0722));
        float lumaE = dot(e, float3(0.2126, 0.7152, 0.0722));
        float lumaW = dot(w, float3(0.2126, 0.7152, 0.0722));

        float lumaMin = min(lumaC, min(min(lumaN, lumaS), min(lumaE, lumaW)));
        float lumaMax = max(lumaC, max(max(lumaN, lumaS), max(lumaE, lumaW)));

        float contrast = lumaMax - lumaMin;
        float sharpAmount = saturate(contrast / max(lumaMax, 0.04)) * TF(ui_SharpenIntensity, GetTheme().sharpenStr);

        float3 blur = (n + s + e + w) * 0.25;
        color = saturate(color + (color - blur) * sharpAmount);
    }

    // --- Enhanced Film Grain ---
    [branch] if (ui_GrainEnable)
    {
        float2 grainCoord = IN.pos.xy / ui_GrainSize;
        float luma = dot(color, float3(0.2126, 0.7152, 0.0722));

        // Luminance-zone masking: grain strongest in midtones (zone V)
        float midtoneMask = exp(-8.0 * (luma - 0.4) * (luma - 0.4));
        float adaptiveScale = lerp(lerp(1.0, 0.3, saturate(luma)), midtoneMask,
                                   ui_GrainMidtoneWeight);

        // Density dependence: silver halide underdeveloped in shadows → more visible grain
        // pow(1-luma, 1.5) gives natural photographic density curve
        if (ui_GrainDensityDep > 0.001)
            adaptiveScale *= lerp(1.0, pow(max(1.0 - luma, 0.0), 1.5) + 0.5, ui_GrainDensityDep);

        // Saturation response: more grain in saturated areas (dye cloud effect)
        float maxC = max(max(color.r, color.g), color.b);
        float minC = min(min(color.r, color.g), color.b);
        float sat = maxC - minC;
        float satBoost = 1.0 + sat * ui_GrainSatResponse;

        float scale = TF(ui_GrainIntensity, GetTheme().grainIntensity) * adaptiveScale * satBoost;

        // No [branch] — d3dcompiler_46 falsely flags X4014 for nested [branch]
        if (ui_GrainChromatic)
        {
            // Per-channel independent grain (three emulsion layers)
            float grainR = frac(52.9829189 * frac(dot(grainCoord,
                           float2(0.06711056, 0.00583715)) + Timer.x * 4.7)) - 0.5;
            float grainG = frac(52.9829189 * frac(dot(grainCoord + 37.0,
                           float2(0.06711056, 0.00583715)) + Timer.x * 7.3)) - 0.5;
            // Blue layer: coarser grain (larger crystal clusters)
            float2 blueCoord = IN.pos.xy / (ui_GrainSize * ui_GrainBlueBoost);
            float grainB = frac(52.9829189 * frac(dot(blueCoord + 91.0,
                           float2(0.06711056, 0.00583715)) + Timer.x * 11.1)) - 0.5;

            color += float3(grainR, grainG, grainB) * scale;
        }
        else
        {
            // Monochrome grain (backward compatible)
            float seed = dot(grainCoord, float2(12.9898, 78.233)) + Timer.x * 43758.5453;
            float grain = frac(sin(seed) * 43758.5453) - 0.5;
            color += grain * scale;
        }
    }

    // --- Final dither (anti-banding) ---
    // Triangular-PDF blue noise: sum of two independent IGN samples shifted to [-1,1]
    // Triangular distribution concentrates error near zero vs uniform, reducing visible
    // banding on R10G10B10A2 (10-bit = 1024 levels). Ref: Gjoel 2016, Lottes 2016.
    float3 n1 = float3(
        frac(52.9829189 * frac(dot(IN.pos.xy, float2(0.06711056, 0.00583715)))),
        frac(52.9829189 * frac(dot(IN.pos.xy + 72.0, float2(0.06711056, 0.00583715)))),
        frac(52.9829189 * frac(dot(IN.pos.xy + 144.0, float2(0.06711056, 0.00583715))))
    );
    float3 n2 = float3(
        frac(52.9829189 * frac(dot(IN.pos.xy + 333.0, float2(0.06711056, 0.00583715)))),
        frac(52.9829189 * frac(dot(IN.pos.xy + 407.0, float2(0.06711056, 0.00583715)))),
        frac(52.9829189 * frac(dot(IN.pos.xy + 481.0, float2(0.06711056, 0.00583715))))
    );
    color += (n1 + n2 - 1.0) / 1023.0;

    return float4(saturate(color), 1.0);
}


//=============================================================================//
//                       POST-PASS ADDONS                                      //
//=============================================================================//

#include "Addons/Effect_BlurSuite.fxh"
#include "Addons/Effect_AASuite.fxh"
#include "Addons/Effect_Sharpening.fxh"
#include "Addons/Effect_CRTShader.fxh"


//=============================================================================//
//                         TECHNIQUES                                          //
//                                                                             //
//  Unified pipeline: cinematic FX + blur + sharpening in one technique chain, //
//  AA as separate base (needs RenderTarget annotation for SMAA),              //
//  CRT/VHS as final standalone.                                               //
//  Each sub-technique reads TextureColor (output of previous).                //
//  Disabled effects pass through unchanged via [branch] guards.               //
//                                                                             //
//  Minimized base techniques (3 instead of 5) to prevent ENB INI param        //
//  duplication — each base technique with UIName creates a panel that shows    //
//  ALL global params.                                                         //
//=============================================================================//

// Pass 0: Lens Diffusion
technique11 EotE_PostPass <string UIName = "EotE: Post-Pass";>
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_CineFX()));
        SetPixelShader(CompileShader(ps_5_0, PS_Diffusion()));
    }
}

// Pass 1: Film Halation
technique11 EotE_PostPass1
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_CineFX()));
        SetPixelShader(CompileShader(ps_5_0, PS_FilmHalation()));
    }
}

// Pass 2: Light Leaks
technique11 EotE_PostPass2
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_LightLeaks()));
        SetPixelShader(CompileShader(ps_5_0, PS_LightLeaks()));
    }
}

// Pass 3: Chromatic Aberration
technique11 EotE_PostPass3
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_CineFX()));
        SetPixelShader(CompileShader(ps_5_0, PS_ChromaticAberration()));
    }
}

// Pass 4: Anamorphic Lens
technique11 EotE_PostPass4
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Anamorphic()));
        SetPixelShader(CompileShader(ps_5_0, PS_Anamorphic()));
    }
}

// Pass 5: Gate Weave
technique11 EotE_PostPass5
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_GateWeave()));
        SetPixelShader(CompileShader(ps_5_0, PS_GateWeave()));
    }
}

// Pass 6: Optical Vignette
technique11 EotE_PostPass6
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_OptVignette()));
        SetPixelShader(CompileShader(ps_5_0, PS_OptVignette()));
    }
}

// Pass 7: Letterbox
technique11 EotE_PostPass7
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_CineFX()));
        SetPixelShader(CompileShader(ps_5_0, PS_Letterbox()));
    }
}

// Pass 8: Film Damage
technique11 EotE_PostPass8
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_FilmDamage()));
        SetPixelShader(CompileShader(ps_5_0, PS_FilmDamage()));
    }
}

// Pass 9: Final Polish (CAS + Film Grain + Dither)
technique11 EotE_PostPass9
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_CineFX()));
        SetPixelShader(CompileShader(ps_5_0, PS_PostPass()));
    }
}

// Pass 10: Blur Suite Horizontal (separable modes) or full radial
technique11 EotE_PostPass10
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_CineFX()));
        SetPixelShader(CompileShader(ps_5_0, PS_BlurH()));
    }
}

// Pass 11: Blur Suite Vertical (passthrough for radial mode)
technique11 EotE_PostPass11
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_CineFX()));
        SetPixelShader(CompileShader(ps_5_0, PS_BlurV()));
    }
}

// Pass 12: Advanced Sharpening (RCAS or KiSharp)
technique11 EotE_PostPass12
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_CineFX()));
        SetPixelShader(CompileShader(ps_5_0, PS_Sharpen()));
    }
}


//=============================================================================//
//                    AA (separate base — needs RenderTarget)                   //
//=============================================================================//

// AA Suite: Edge detection (SMAA) → writes to RenderTargetRGBA64F
technique11 EotE_AA <string UIName = "EotE: AA Suite";
    string RenderTarget = "RenderTargetRGBA64F";>
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_AAEdge()));
        SetPixelShader(CompileShader(ps_5_0, PS_AAEdge()));
    }
}

// AA Suite: SMAA weight+blend or FXAA → writes to TextureColor
technique11 EotE_AA1
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_CineFX()));
        SetPixelShader(CompileShader(ps_5_0, PS_AABlend()));
    }
}


//=============================================================================//
//                    CRT/VHS Display (standalone)                             //
//=============================================================================//

// CRT/VHS Display Simulation
technique11 EotE_Display <string UIName = "EotE: CRT/VHS Display";>
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_CineFX()));
        SetPixelShader(CompileShader(ps_5_0, PS_CRTDisplay()));
    }
}
