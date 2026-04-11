//----------------------------------------------------------------------------------------------//
//                    ENB of the Elders - Screen-Space Prepass Suite                            //
//----------------------------------------------------------------------------------------------//
//                                                                                              //
//  Single-technique prepass pipeline (1 technique, all effects inline).                        //
//                                                                                              //
//  EotE_PrePass — All effects computed in a single pixel shader:                               //
//    1. VB-SSGI  — Visibility Bitmask Screen-Space Global Illumination.                        //
//                  12 slice directions x 6 steps x 2 sides = 144 depth samples.                //
//                  Engine TextureNormal (with depth-derived fallback),                          //
//                  cosine-weighted run decomposition (Gilcher/iMMERSE v2),                     //
//                  hemisphere-aware asymmetric falloff, bent normal directional GI,             //
//                  physically-based disc-occluder falloff, power-1.5 step distribution,         //
//                  adaptive MIP GI, 2D R2 temporal jitter.                                     //
//    2. Skylighting — Cone-aperture sky evaluation, multi-sample integration,                  //
//                    bent-normal self-shadowing, horizon fade.                                 //
//    3. Multi-Bounce AO — Jimenez albedo-tinted AO (colored bounce light in crevices).         //
//    4. Volumetric Fog — World-space ray march with analytical height fog,                     //
//                        Beer-Lambert extinction, Cornette-Shanks phase function,              //
//                        Frostbite energy-conserving integration, atmospheric perspective.     //
//    5. Atmospheric Haze — Depth-based aerial perspective, Beer-Lambert extinction,           //
//                          sun forward scatter (inscattering glow), desaturation,              //
//                          contrast reduction at distance, sky protection.                     //
//    6. God Rays — Merged with vol fog march (shared VOL_STEPS loop),                         //
//                  sky mask + HDR bright pixel detection, HG phase + silver lining,            //
//                  fog transmittance inherently attenuates shafts.                             //
//                  ViewProjectionMatrix sun projection (world→screen), SB fallback.           //
//    7. Water Surface Mask — GBuffer/depth normal mismatch detection,                         //
//                           reduces AO/GI/fog/haze on water pixels.                           //
//                                                                                              //
//  References:                                                                                 //
//    - Therrien, Levesque, Gilet (CGF 2023) — Visibility Bitmask SSGI                         //
//    - Jimenez (SIGGRAPH 2016) — Multi-bounce AO polynomial                                    //
//    - Hillaire 2016 / Frostbite 2015 — Energy-conserving volumetric integration               //
//    - Mitchell (GPU Gems 3, 2007) — Volumetric Light Scattering as Post-Process               //
//    - Bavoil & Sainz — Depth-derived normal reconstruction                                    //
//                                                                                              //
//  Adapted for ENB of the Elders by Zain Dana Harper - March 2026                              //
//                                                                                              //
//----------------------------------------------------------------------------------------------//


//=== COMPILE-TIME OPTIONS ===//

// Quality tier overrides (from enbglobals.fxh: QUALITY_TIER 0/1/2)
#define VB_NUM_DIRS    QT_AO_DIRS    // Slice directions (x4 frame rotation = effective dirs)
#define VB_NUM_STEPS   QT_AO_STEPS   // Steps per direction per side
#define VB_SECTORS     32             // Bitmask bits (uint32) — always 32
#define VOL_STEPS      QT_VOL_STEPS  // Ray march steps


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
float4  SunDirection;       // .xyz = world-space direction toward the sun, .w = behind camera (>0.5)
float4x4 ViewProjectionMatrix;  // World → Clip (ENB native, row-major)
float4  tempF1;
float4  tempF2;
float4  tempF3;
float4  tempInfo1;
float4  tempInfo2;


//=== GLOBALS ===//

#include "enbglobals.fxh"


//=== SKYRIMBRIDGE ===//

#include "Helper/SkyrimBridge_CB.fxh"
#include "Helper/SB_DenoiseCore.fxh"
#include "Helper/SB_TemporalCore.fxh"


//=== TEXTURES ===//

Texture2D   TextureColor;
Texture2D   TextureOriginal;
Texture2D   TextureDepth;
Texture2D   TextureNormal;      // xyz = screen-space normals [0,1], engine-derived (includes normal maps)
Texture2D   TextureMask;        // alpha < 1.0 = skin/subsurface material (Kitsuune convention)



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


//=== CONSTANTS ===//

static const float2 PixelSize = float2(ScreenSize.y, ScreenSize.y * ScreenSize.z);
static const float  PI = 3.14159265;
static const float  HALF_PI = 1.57079632;
static const float  GOLDEN_ANGLE = 2.39996323;
static const float  DELTA = 1e-6;

// Rec.709 luminance weights
static const float3 LUM_709 = float3(0.2126, 0.7152, 0.0722);

// Sector angle width: PI / VB_SECTORS
static const float VB_SECTOR_SCALE = (float)VB_SECTORS / PI;


//=== UI PARAMETERS ===//

// =============================================================================
//  VB-SSGI (Visibility Bitmask Screen-Space Global Illumination)
// =============================================================================

bool ui_GI_Enable
<
    string UIName = "GI | Enable";
> = {true};

float ui_GI_RadiusDay
<
    string UIName = "GI | Day - Radius";
    string UIWidget = "Spinner";
    float UIMin = 0.1;
    float UIMax = 5.0;
    float UIStep = 0.01;
> = {1.5};

float ui_GI_RadiusNight
<
    string UIName = "GI | Night - Radius";
    string UIWidget = "Spinner";
    float UIMin = 0.1;
    float UIMax = 5.0;
    float UIStep = 0.01;
> = {1.2};

float ui_GI_RadiusInterior
<
    string UIName = "GI | Interior - Radius";
    string UIWidget = "Spinner";
    float UIMin = 0.1;
    float UIMax = 5.0;
    float UIStep = 0.01;
> = {1.0};

float ui_GI_IntensityDay
<
    string UIName = "GI | Day - AO Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 3.0;
    float UIStep = 0.005;
> = {1.0};

float ui_GI_IntensityNight
<
    string UIName = "GI | Night - AO Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 3.0;
    float UIStep = 0.005;
> = {0.8};

float ui_GI_IntensityInterior
<
    string UIName = "GI | Interior - AO Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 3.0;
    float UIStep = 0.005;
> = {1.2};

float ui_GI_Power
<
    string UIName = "GI | AO Power Curve";
    string UIWidget = "Spinner";
    float UIMin = 0.1;
    float UIMax = 6.0;
    float UIStep = 0.005;
> = {1.5};

float ui_GI_Thickness
<
    string UIName = "GI | Surface Thickness";
    string UIWidget = "Spinner";
    float UIMin = 0.01;
    float UIMax = 0.5;
    float UIStep = 0.005;
> = {0.15};

float ui_GI_BounceIntensity
<
    string UIName = "GI | Bounce Light";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {0.5};

float ui_GI_BounceSaturation
<
    string UIName = "GI | Bounce Saturation";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {0.7};

bool ui_GI_MultiBounce
<
    string UIName = "GI | Multi-Bounce Enable";
> = {true};

float ui_GI_SpecOccStrength
<
    string UIName = "GI | Specular Occlusion Strength";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.5};

float ui_GI_SpecOccRoughness
<
    string UIName = "GI | Specular Occlusion Roughness";
    string UIWidget = "Spinner";
    float UIMin = 0.01;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.15};

float ui_GI_FadeStart
<
    string UIName = "GI | Fade Start";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.005;
> = {0.3};

float ui_GI_FadeEnd
<
    string UIName = "GI | Fade End";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.005;
> = {0.8};

float ui_GI_Denoise
<
    string UIName = "GI | Denoise Strength";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.5};

// =============================================================================
//  Water Surface Mask (reduces prepass effects on water pixels)
// =============================================================================

bool ui_Water_Enable
<
    string UIName = "WATER | Enable Water Mask";
> = {true};

float ui_Water_Reduction
<
    string UIName = "WATER | Effect Reduction";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.85};

float ui_Water_Threshold
<
    string UIName = "WATER | Height Tolerance (world units)";
    string UIWidget = "Spinner";
    float UIMin = 1.0;
    float UIMax = 500.0;
    float UIStep = 5.0;
> = {50.0};

float ui_Water_Debug
<
    string UIName = "WATER | Debug (0=off 1=mask 2=gbuf 3=depth 4=heightZ)";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 4.0;
    float UIStep = 1.0;
    int UIHidden = 1;
> = {0.0};

// =============================================================================
//  Skylighting (bent normal → hemispherical sky evaluation)
//  Technique ref: Frostbite SH skylight, UE4 bent normal ambient, COD:IW sky eval
// =============================================================================

bool ui_Sky_Enable
<
    string UIName = "SKY | Enable Skylighting";
> = {true};

float ui_Sky_Intensity
<
    string UIName = "SKY | Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {0.3};

float ui_Sky_GroundBounce
<
    string UIName = "SKY | Ground Bounce";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.3};

float ui_Sky_SunInfluence
<
    string UIName = "SKY | Sun Influence";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {0.5};

float ui_Sky_MultiscatterMin
<
    string UIName = "SKY | Multiscatter Minimum";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 0.5;
    float UIStep = 0.005;
> = {0.08};

float ui_Sky_SelfShadow
<
    string UIName = "SKY | Self-Shadow Strength";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {0.8};

float ui_Sky_HorizonFade
<
    string UIName = "SKY | Horizon Fade";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 3.0;
    float UIStep = 0.05;
> = {1.5};

float ui_Sky_ConeSharpness
<
    string UIName = "SKY | Cone Sharpness";
    string UIWidget = "Spinner";
    float UIMin = 0.5;
    float UIMax = 4.0;
    float UIStep = 0.1;
> = {2.0};

// =============================================================================
//  Volumetric Fog (world-space ray march)
// =============================================================================

bool ui_Vol_Enable
<
    string UIName = "VOL | Enable Volumetric Fog";
> = {true};

float ui_Vol_DensityDay
<
    string UIName = "VOL | Day - Density";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 0.005;
    float UIStep = 0.00005;
> = {0.0003};

float ui_Vol_DensityNight
<
    string UIName = "VOL | Night - Density";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 0.005;
    float UIStep = 0.00005;
> = {0.0005};

float ui_Vol_DensityInterior
<
    string UIName = "VOL | Interior - Density";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 0.005;
    float UIStep = 0.00005;
> = {0.0001};

float ui_Vol_MaxDistance
<
    string UIName = "VOL | Max Distance";
    string UIWidget = "Spinner";
    float UIMin = 500.0;
    float UIMax = 5000.0;
    float UIStep = 50.0;
> = {3000.0};

float ui_Vol_HeightFalloff
<
    string UIName = "VOL | Height Falloff";
    string UIWidget = "Spinner";
    float UIMin = 0.001;
    float UIMax = 0.05;
    float UIStep = 0.0005;
> = {0.008};

float ui_Vol_BaseHeight
<
    string UIName = "VOL | Base Height";
    string UIWidget = "Spinner";
    float UIMin = -500.0;
    float UIMax = 500.0;
    float UIStep = 5.0;
> = {0.0};

float ui_Vol_Extinction
<
    string UIName = "VOL | Extinction";
    string UIWidget = "Spinner";
    float UIMin = 0.1;
    float UIMax = 4.0;
    float UIStep = 0.01;
> = {1.0};

float ui_Vol_ScatterAlbedo
<
    string UIName = "VOL | Scatter Albedo";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.9};

float ui_Vol_Anisotropy
<
    string UIName = "VOL | Sun Anisotropy (g)";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 0.9;
    float UIStep = 0.01;
> = {0.5};

float ui_Vol_SunIntensity
<
    string UIName = "VOL | Sun Scatter Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {0.5};

float ui_Vol_AmbientStrength
<
    string UIName = "VOL | Ambient Light";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.3};

float ui_Vol_SkyProtect
<
    string UIName = "VOL | Sky Protection";
    string UIWidget = "Spinner";
    float UIMin = 0.8;
    float UIMax = 1.0;
    float UIStep = 0.005;
> = {0.95};

bool ui_Vol_UseSBFog
<
    string UIName = "VOL | Use SB Fog Color";
> = {true};

float3 ui_Vol_Color
<
    string UIName = "VOL | Fog Color Tint";
    string UIWidget = "Color";
> = {0.7, 0.75, 0.85};

float ui_Vol_Desaturation
<
    string UIName = "VOL | Desaturation";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.005;
> = {0.3};

float ui_Vol_ContrastLoss
<
    string UIName = "VOL | Contrast Reduction";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.005;
> = {0.25};

// -- Procedural Noise --

bool ui_Vol_NoiseEnable
<
    string UIName = "VOL | Noise Enable";
> = {true};

float ui_Vol_NoiseScale
<
    string UIName = "VOL | Noise Scale";
    string UIWidget = "Spinner";
    float UIMin = 0.0005;
    float UIMax = 0.01;
    float UIStep = 0.0005;
> = {0.002};

float ui_Vol_NoiseStrength
<
    string UIName = "VOL | Noise Strength";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.4};

float ui_Vol_NoiseOctaves
<
    string UIName = "VOL | Noise Octaves (1-3)";
    string UIWidget = "Spinner";
    float UIMin = 1.0;
    float UIMax = 3.0;
    float UIStep = 1.0;
> = {2.0};

float ui_Vol_NoiseWindSpeed
<
    string UIName = "VOL | Noise Wind Speed";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 100.0;
    float UIStep = 1.0;
> = {20.0};

float ui_Vol_NoiseHeightFade
<
    string UIName = "VOL | Noise Height Fade";
    string UIWidget = "Spinner";
    float UIMin = 0.001;
    float UIMax = 0.05;
    float UIStep = 0.001;
> = {0.01};

// -- Sun Occlusion / God Rays --

bool ui_Vol_OcclusionEnable
<
    string UIName = "VOL | Sun Occlusion Enable";
> = {true};

float ui_Vol_OcclusionSpread
<
    string UIName = "VOL | Occlusion Spread";
    string UIWidget = "Spinner";
    float UIMin = 0.01;
    float UIMax = 0.2;
    float UIStep = 0.005;
> = {0.08};

float ui_Vol_OcclusionIntensity
<
    string UIName = "VOL | Occlusion Strength";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.7};

float ui_Vol_ExpSteps
<
    string UIName = "VOL | Exponential Step Distribution";
    string UIWidget = "Spinner";
    float UIMin = 1.0;
    float UIMax = 3.0;
    float UIStep = 0.1;
> = {1.0};

// =============================================================================
//  Aerial Perspective — Rayleigh/Mie Atmospheric Scattering
//
//  Physically-motivated two-channel model:
//    Rayleigh: λ⁻⁴ selective scatter → blue distance haze, blue mountains
//    Mie:      forward-scatter → golden sun halos, milky horizon glow
//    Height:   exponential density falloff with view-ray altitude
//    Multi-scatter: approximate energy from secondary/tertiary bounces
//
//  Ref: Bruneton 2008, Hillaire 2020, Preetham 1999
// =============================================================================

bool ui_Haze_Enable
<
    string UIName = "HAZE | Enable Aerial Perspective";
> = {true};

float ui_Haze_Intensity
<
    string UIName = "HAZE | Day - Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {0.7};

float ui_Haze_IntensityNight
<
    string UIName = "HAZE | Night - Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.12};

float ui_Haze_IntensityInterior
<
    string UIName = "HAZE | Interior - Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.05};

float ui_Haze_StartDist
<
    string UIName = "HAZE | Start Distance";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 0.5;
    float UIStep = 0.005;
> = {0.03};

float ui_Haze_RayleighDensity
<
    string UIName = "HAZE | Rayleigh Density (blue scatter)";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 4.0;
    float UIStep = 0.05;
> = {1.5};

float ui_Haze_MieDensity
<
    string UIName = "HAZE | Mie Density (sun glow)";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 4.0;
    float UIStep = 0.05;
> = {0.8};

float ui_Haze_MieAnisotropy
<
    string UIName = "HAZE | Mie Anisotropy (0=uniform 0.99=narrow)";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 0.99;
    float UIStep = 0.01;
> = {0.76};

float ui_Haze_HeightScale
<
    string UIName = "HAZE | Height Scale (altitude falloff)";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 3.0;
    float UIStep = 0.05;
> = {0.8};

float3 ui_Haze_RayleighTint
<
    string UIName = "HAZE | Rayleigh Tint (scatter color)";
    string UIWidget = "Color";
> = {0.35, 0.55, 0.95};

float3 ui_Haze_ColorTint
<
    string UIName = "HAZE | Mie/Ambient Tint";
    string UIWidget = "Color";
> = {0.75, 0.78, 0.85};

bool ui_Haze_UseSBFogColor
<
    string UIName = "HAZE | Use SkyrimBridge Fog Color";
> = {false};

float ui_Haze_Inscattering
<
    string UIName = "HAZE | Inscatter Strength";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 3.0;
    float UIStep = 0.01;
> = {1.2};

float ui_Haze_MultiScatter
<
    string UIName = "HAZE | Multi-Scatter (indirect bounce)";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.3};

float ui_Haze_SkyProtection
<
    string UIName = "HAZE | Sky Protection";
    string UIWidget = "Spinner";
    float UIMin = 0.5;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.9};

float ui_Haze_ContrastReduction
<
    string UIName = "HAZE | Contrast Reduction";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.4};

float ui_Haze_Desaturation
<
    string UIName = "HAZE | Desaturation";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.2};


// =============================================================================
//  God Rays (screen-space radial blur shaft extraction)
// =============================================================================

bool ui_GR_Enable
<
    string UIName = "RAYS | Enable God Rays";
> = {true};

float ui_GR_Intensity
<
    string UIName = "RAYS | Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 3.0;
    float UIStep = 0.01;
> = {0.5};

float ui_GR_Density
<
    string UIName = "RAYS | Density (march spread)";
    string UIWidget = "Spinner";
    float UIMin = 0.3;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {0.85};

float ui_GR_Decay
<
    string UIName = "RAYS | Exponential Decay";
    string UIWidget = "Spinner";
    float UIMin = 0.90;
    float UIMax = 1.0;
    float UIStep = 0.001;
> = {0.970};

float ui_GR_Exposure
<
    string UIName = "RAYS | Exposure";
    string UIWidget = "Spinner";
    float UIMin = 0.01;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.25};

float ui_GR_SkyThreshold
<
    string UIName = "RAYS | Sky Threshold";
    string UIWidget = "Spinner";
    float UIMin = 0.5;
    float UIMax = 1.0;
    float UIStep = 0.005;
> = {0.92};

float ui_GR_BrightThreshold
<
    string UIName = "RAYS | HDR Bright Threshold";
    string UIWidget = "Spinner";
    float UIMin = 0.5;
    float UIMax = 5.0;
    float UIStep = 0.1;
> = {1.5};

float ui_GR_PhaseG
<
    string UIName = "RAYS | Phase Anisotropy";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 0.95;
    float UIStep = 0.01;
> = {0.76};

float ui_GR_BackscatterBlend
<
    string UIName = "RAYS | Silver Lining";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 0.5;
    float UIStep = 0.01;
> = {0.12};

float3 ui_GR_Tint
<
    string UIName = "RAYS | Sun Tint (fallback)";
    string UIWidget = "Color";
> = {1.0, 0.95, 0.85};


//=== HELPER FUNCTIONS ===//

float GetLinearDepth(float2 uv)
{
    float d = TextureDepth.SampleLevel(smpPoint, uv, 0).x;
    return d / (d * (-2999.0) + 3000.0);
}

float GetWorldZ(float linDepth)
{
    return linDepth * 3000.0;
}

float2 Hash2D(float2 coord)
{
    float n1 = frac(52.9829189 * frac(dot(coord, float2(0.06711056, 0.00583715))));
    float n2 = frac(n1 * 3571.0 + dot(coord, float2(0.00483759, 0.04673812)));
    return float2(n1, n2);
}

float TemporalIGN(float2 screenPos, float frameTime)
{
    float frame = frac(frameTime * 0.017);
    return frac(52.9829189 * frac(dot(screenPos + frame * 5.588238, float2(0.06711056, 0.00583715))));
}

// View-space position from screen UV + linear depth (no SB dependency)
float3 ViewPosFromUV(float2 uv, float linearDepth)
{
    float tanHFov = tan(FieldOfView * 0.5 * PI / 180.0);
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    return float3(ndc.x * ScreenSize.z * tanHFov * linearDepth,
                  ndc.y * tanHFov * linearDepth,
                  linearDepth);
}

// Depth-derived normal (Bavoil-Sainz smallest-derivative method)
float3 NormalFromDepth(float2 uv)
{
    float2 texel = float2(ScreenSize.y, ScreenSize.y * ScreenSize.z);

    float cZ = GetLinearDepth(uv);
    float rZ = GetLinearDepth(uv + float2(texel.x, 0));
    float lZ = GetLinearDepth(uv - float2(texel.x, 0));
    float dZ = GetLinearDepth(uv + float2(0, texel.y));
    float uZ = GetLinearDepth(uv - float2(0, texel.y));

    float3 cP = ViewPosFromUV(uv, cZ);

    float3 ddxR = ViewPosFromUV(uv + float2(texel.x, 0), rZ) - cP;
    float3 ddxL = cP - ViewPosFromUV(uv - float2(texel.x, 0), lZ);
    float3 dx = (abs(ddxR.z) < abs(ddxL.z)) ? ddxR : ddxL;

    float3 ddyD = ViewPosFromUV(uv + float2(0, texel.y), dZ) - cP;
    float3 ddyU = cP - ViewPosFromUV(uv - float2(0, texel.y), uZ);
    float3 dy = (abs(ddyD.z) < abs(ddyU.z)) ? ddyD : ddyU;

    return normalize(cross(dy, dx));
}

// Water surface detection — height-based via SkyrimBridge.
//
// Screen-space detection is impossible: ENB provides valid GBuffer normals for
// water pixels, leaving no distinguishing signal. Instead, when SB is active,
// we reconstruct the pixel's world-space Z and compare against the cell's water
// surface height (SB_Fog_Height.x = cell->GetExteriorWaterHeight()).
//
// Without SB, returns 0 (no detection possible).
float DetectWater(float2 uv, float depth)
{
    if (!ui_Water_Enable) return 0.0;

    // SB path: height-based detection using cell water surface Z
    if (SB_IsActive())
    {
        // Skip in interiors
        if (SB_Interior_Flags.x > 0.5) return 0.0;

        float waterZ = SB_Fog_Height.x;

        // GetExteriorWaterHeight returns -2^31 for cells without water
        if (waterZ < -1e6) return 0.0;

        // Skip if camera is underwater
        if (SB_Player_Water.x > 0.5) return 0.0;

        // Skip sky pixels
        if (depth > 0.99) return 0.0;

        // Reconstruct pixel world-space Z from depth + SB camera matrices
        float3 camRight   = float3(SB_View_Row0.x, SB_View_Row1.x, SB_View_Row2.x);
        float3 camUp      = float3(SB_View_Row0.y, SB_View_Row1.y, SB_View_Row2.y);
        float3 camForward = float3(SB_View_Row0.z, SB_View_Row1.z, SB_View_Row2.z);

        float fwdLen = length(camForward);
        if (fwdLen < 0.5) return 0.0;

        camRight   = normalize(camRight);
        camUp      = normalize(camUp);
        camForward /= fwdLen;

        float2 ndc = uv * 2.0 - 1.0;
        ndc.y = -ndc.y;
        float3 viewRay = float3(ndc.x / SB_Proj_Row0.x,
                                ndc.y / SB_Proj_Row1.y,
                                1.0);

        float3 rayDir = normalize(camRight * viewRay.x
                                 + camUp   * viewRay.y
                                 + camForward * viewRay.z);

        float cosAngle = dot(rayDir, camForward);
        float rayLen = GetWorldZ(depth) / max(cosAngle, 0.001);
        float pixelZ = SB_Camera_WorldPos.z + rayDir.z * rayLen;

        // Water mask: 1.0 when pixel Z matches water surface, fading to 0
        // within ui_Water_Threshold world units of the surface
        float heightDiff = abs(pixelZ - waterZ);
        return smoothstep(ui_Water_Threshold, 0.0, heightDiff);
    }

    // Fallback without SB: heuristic using depth-derived normals
    // Water surfaces are flat (normal ≈ up) with low depth variance
    if (depth > 0.99) return 0.0; // skip sky

    float3 normal = NormalFromDepth(uv);

    // Upward-facing surface check (normal.y close to 1.0 in our depth-space)
    float flatness = saturate(normal.y);

    // Only very flat surfaces qualify
    if (flatness < 0.95) return 0.0;

    // Check depth variance in a small neighborhood — water has uniform depth
    float centerZ = GetWorldZ(depth);
    float variance = 0.0;
    [unroll] for (int sy = -1; sy <= 1; sy += 2)
    [unroll] for (int sx = -1; sx <= 1; sx += 2)
    {
        float neighborZ = GetWorldZ(GetLinearDepth(uv + float2(sx, sy) * PixelSize * 3.0));
        float diff = abs(neighborZ - centerZ);
        variance += diff;
    }
    variance *= 0.25;

    // Low variance + flat normal = likely water
    float uniformity = 1.0 - saturate(variance / max(centerZ * 0.002, 0.1));
    return smoothstep(0.95, 1.0, flatness) * smoothstep(0.5, 0.9, uniformity);
}

// Flag sectors in a 32-bit bitmask between two elevation angles
uint FlagSectors(float frontAngle, float backAngle)
{
    int first = clamp(int(floor((frontAngle + HALF_PI) * VB_SECTOR_SCALE)), 0, VB_SECTORS - 1);
    int last  = clamp(int(floor((backAngle  + HALF_PI) * VB_SECTOR_SCALE)), 0, VB_SECTORS - 1);

    if (last < first) return 0u;

    uint high = (last < 31) ? ((1u << (uint(last) + 1u)) - 1u) : 0xFFFFFFFF;
    uint low  = (1u << uint(first)) - 1u;
    return high & ~low;
}

// Cosine-weighted AO via run decomposition (Gilcher / iMMERSE v2)
// Integrates cos(θ - θ_n) over contiguous visible sector runs.
// 56% of bitmasks have ≤2 runs, 87% ≤3 — handle up to 4, fallback for rest.
float CosineWeightedAO(uint visibleMask, float normalAngle)
{
    if (visibleMask == 0u) return 0.0;
    if (visibleMask == 0xFFFFFFFF)
    {
        // Full visibility: integral of cos(θ - θ_n) over [-π/2, π/2] = 2.0
        return 1.0;
    }

    float totalContrib = 0.0;
    float sectorWidth = PI / (float)VB_SECTORS;
    uint remaining = visibleMask;

    [loop]
    for (int r = 0; r < 4; r++)
    {
        if (remaining == 0u) break;

        int runStart = firstbitlow(remaining);
        // Find run length: invert shifted mask, find first set = end of run
        uint shifted = remaining >> (uint)runStart;
        int runLen = firstbitlow(~shifted);
        if (runLen <= 0) runLen = 32 - runStart; // all remaining bits are set

        // Cosine integral: sin(b) - sin(a) where a,b are run boundaries
        // mapped from sector indices to angles relative to normal
        float a = (float)runStart * sectorWidth - HALF_PI - normalAngle;
        float b = (float)(runStart + runLen) * sectorWidth - HALF_PI - normalAngle;
        totalContrib += sin(b) - sin(a);

        // Clear processed run bits
        uint runMask = ((uint)runLen < 32u)
            ? (((1u << (uint)runLen) - 1u) << (uint)runStart)
            : (0xFFFFFFFF << (uint)runStart);
        remaining &= ~runMask;
    }

    // Fallback for remaining runs (rare, <13% of cases)
    if (remaining != 0u)
    {
        float avgCos = 0.6366; // 2/π — average |cos| over hemisphere
        totalContrib += (float)countbits(remaining) * sectorWidth * avgCos;
    }

    // Normalize: max integral of cos over full hemisphere [-π/2, π/2] = 2.0
    return saturate(totalContrib * 0.5);
}

// Multi-bounce AO (Jimenez 2016)
float3 MultiBounceAO(float ao, float3 albedo)
{
    float3 a = 2.0404 * albedo - 0.3324;
    float3 b = -4.7951 * albedo + 0.6417;
    float3 c = 2.7552 * albedo + 0.6903;
    return max(ao, ((ao * a + b) * ao + c) * ao);
}

// Hemispherical sky gradient evaluation (production skylighting)
// Schlick phase function (fast HG approximation, no pow())
// Ref: Schlick 1993 — visually equivalent to HG, avoids pow(1.5)
float SchlickPhase(float cosTheta, float g)
{
    float k = 1.55 * g - 0.55 * g * g * g;
    float d = 1.0 - k * cosTheta;
    return (1.0 - k * k) / (4.0 * PI * d * d);
}

// Dual-lobe phase: forward scatter (sun halos) + backward scatter (silver lining)
// Ref: Frostbite 2015, Hillaire 2020
float DualLobePhase(float cosTheta, float gForward, float gBackward, float blend)
{
    return lerp(SchlickPhase(cosTheta, gForward),
                SchlickPhase(cosTheta, -gBackward),
                blend);
}

// Cornette-Shanks phase function
float CornetteShanks(float cosTheta, float g)
{
    float g2 = g * g;
    float num   = 3.0 * (1.0 - g2) * (1.0 + cosTheta * cosTheta);
    float denom = 8.0 * PI * (2.0 + g2) * pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5);
    return num / max(denom, DELTA);
}

//
// Evaluates a physically-motivated sky dome at the given direction.
// Two-layer model: Rayleigh (blue) + Mie (white/warm) scattering.
//
//   - Upper hemisphere: gradient from warm horizon to deep blue zenith
//   - Lower hemisphere: terrain-reflected bounce light
//   - Sun-facing: Rayleigh (1+cos²θ) + Mie (HG forward scatter)
//   - Weather: cloud cover flattens gradient, increases Mie contribution
//   - 4 time-of-day presets blended via TimeOfDay1 weights
//
// Ref: Bruneton 2008, Hillaire 2020, Hosek-Wilkie 2012 (simplified)
float3 EvaluateSkyGradient(float3 dir, float2 sunDir2D, float sunBehind)
{
    float upFacing = dir.y;

    // ---- Zenith colors (deep blue Rayleigh-dominant) ----
    float3 zenithSunrise = float3(0.18, 0.25, 0.55);
    float3 zenithDay     = float3(0.15, 0.32, 0.78);
    float3 zenithSunset  = float3(0.16, 0.18, 0.48);
    float3 zenithNight   = float3(0.02, 0.03, 0.08);

    float3 skyZenith = zenithSunrise * TimeOfDay1.x
                     + zenithDay     * TimeOfDay1.y
                     + zenithSunset  * TimeOfDay1.z
                     + zenithNight   * TimeOfDay1.w;

    // ---- Horizon colors (warm Mie-dominant, longer atmosphere path) ----
    float3 horizonSunrise = float3(0.65, 0.42, 0.25);
    float3 horizonDay     = float3(0.52, 0.52, 0.50);
    float3 horizonSunset  = float3(0.62, 0.35, 0.18);
    float3 horizonNight   = float3(0.035, 0.038, 0.05);

    float3 skyHorizon = horizonSunrise * TimeOfDay1.x
                      + horizonDay     * TimeOfDay1.y
                      + horizonSunset  * TimeOfDay1.z
                      + horizonNight   * TimeOfDay1.w;

    // ---- Ground bounce (terrain albedo × downwelling) ----
    float3 groundSunrise = float3(0.12, 0.09, 0.05);
    float3 groundDay     = float3(0.12, 0.14, 0.08);
    float3 groundSunset  = float3(0.10, 0.07, 0.04);
    float3 groundNight   = float3(0.008, 0.008, 0.01);

    float3 skyGround = groundSunrise * TimeOfDay1.x
                     + groundDay     * TimeOfDay1.y
                     + groundSunset  * TimeOfDay1.z
                     + groundNight   * TimeOfDay1.w;

    // ---- Weather flattening: clouds reduce zenith-horizon contrast ----
    float cloudFactor = Weather.x; // 0=clear, 1=overcast
    skyZenith  = lerp(skyZenith,  skyHorizon * 0.85, cloudFactor * 0.6);
    skyHorizon = lerp(skyHorizon, skyZenith  * 1.1,  cloudFactor * 0.3);

    // ---- Gradient evaluation ----
    float3 skyLight;
    if (upFacing >= 0.0)
    {
        float t = upFacing;

        // Rayleigh-inspired horizon band: optical depth ∝ 1/sin(elevation)
        // Creates the characteristic bright-then-dark band near horizon
        float airmass = 1.0 - exp(-4.0 * t);  // thicker at horizon
        float horizonBand = exp(-8.0 * t * t); // narrow bright band at horizon

        // Two-stage gradient: horizon band → zenith transition
        float3 horizonBright = skyHorizon * (0.6 + 0.4 * airmass + 0.3 * horizonBand);
        skyLight = lerp(horizonBright, skyZenith, saturate(t * t * 1.2));

        // Subtle Rayleigh blue boost in upper sky (λ⁻⁴ wavelength dependence)
        float blueBoost = saturate(t - 0.3) * 0.15;
        skyLight.b += blueBoost * ENightDayFactor;
    }
    else
    {
        float t = saturate(-upFacing);
        // Softer ground transition with terrain color
        skyLight = lerp(skyHorizon * 0.55, skyGround, smoothstep(0.0, 0.6, t));
    }

    // ---- Sun-facing scattering (Rayleigh + Mie) ----
    float dayAmount = ENightDayFactor;
    if (dayAmount > 0.05 && sunBehind < 0.5)
    {
        float sunDirLen = length(sunDir2D);
        if (sunDirLen > 0.01)
        {
            float2 normSunDir = sunDir2D / sunDirLen;
            float cosTheta = dot(dir.xz, normSunDir);

            // Rayleigh phase: (3/16π)(1 + cos²θ)
            float rayleighPhase = (3.0 / (16.0 * PI)) * (1.0 + cosTheta * cosTheta);

            // Mie phase: HG with g=0.76 (creates bright sun halo)
            float g = 0.76;
            float g2 = g * g;
            float mieD = 1.0 + g2 - 2.0 * g * cosTheta;
            float miePhase = (1.0 - g2) / max(mieD * sqrt(mieD), 0.001) * 0.25;

            // Golden hours: thicker atmosphere path → stronger Mie + orange shift
            float goldenHour = TimeOfDay1.x + TimeOfDay1.z;
            float mieWeight = lerp(0.15, 0.65, goldenHour);
            float scatterLobe = lerp(rayleighPhase, miePhase, mieWeight);

            // Sun tint with realistic atmospheric reddening
            float3 sunTint = float3(1.0, 0.93, 0.80) * TimeOfDay1.y
                           + float3(1.0, 0.60, 0.28) * TimeOfDay1.x
                           + float3(1.0, 0.52, 0.22) * TimeOfDay1.z;

            // Elevation mask: scatter only toward sky-facing directions
            float sunMask = saturate(upFacing + 0.35);

            // Circumsolar brightening: extra Mie intensity near the sun disc
            float circumsolar = pow(max(cosTheta, 0.0), 32.0) * goldenHour * 0.3;

            skyLight += sunTint * (scatterLobe + circumsolar) * sunMask
                      * dayAmount * ui_Sky_SunInfluence;

            // Rayleigh blue enhancement opposite to sun (deeper blue away from sun)
            float antiSolar = saturate(-cosTheta * 0.3) * (1.0 - goldenHour);
            skyLight += float3(0.02, 0.04, 0.08) * antiSolar * dayAmount;
        }
    }

    return max(skyLight, 0.0);
}

// Exponential height fog density
float HeightFogDensity(float worldHeight, float baseHeight, float baseDensity, float falloff)
{
    float h = worldHeight - baseHeight;
    return baseDensity * exp(-max(h, 0.0) * falloff);
}

// 3D lattice hash
float Hash3D(float3 p)
{
    p = frac(p * float3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yzx + 33.33);
    return frac((p.x + p.y) * p.z);
}

// 3D value noise
float ValueNoise3D(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    float3 u = f * f * (3.0 - 2.0 * f);

    float c000 = Hash3D(i);
    float c100 = Hash3D(i + float3(1, 0, 0));
    float c010 = Hash3D(i + float3(0, 1, 0));
    float c110 = Hash3D(i + float3(1, 1, 0));
    float c001 = Hash3D(i + float3(0, 0, 1));
    float c101 = Hash3D(i + float3(1, 0, 1));
    float c011 = Hash3D(i + float3(0, 1, 1));
    float c111 = Hash3D(i + float3(1, 1, 1));

    return lerp(
        lerp(lerp(c000, c100, u.x), lerp(c010, c110, u.x), u.y),
        lerp(lerp(c001, c101, u.x), lerp(c011, c111, u.x), u.y),
        u.z);
}

// Fog FBM with curl-like distortion (manually unrolled, FXC safe)
// 3 octaves available, with rotated domains to reduce axis-aligned artifacts
float FogNoiseFBM(float3 worldPos, float time, float2 windDir,
                  float windSpeed, float scale, float octaves)
{
    float3 p = worldPos * scale;
    p.xy += windDir * windSpeed * time;

    float sum = 0.5 * ValueNoise3D(p);

    if (octaves > 1.5)
    {
        // Rotate domain to break axis alignment (cheap curl approximation)
        p = p * 2.0 + float3(0.37, -0.13, 0.59);
        sum += 0.25 * ValueNoise3D(p);
    }

    if (octaves > 2.5)
    {
        p = p * 2.0 + float3(-0.71, 0.23, -0.41);
        sum += 0.125 * ValueNoise3D(p);
    }

    return sum;
}

// Screen-space sun occlusion (unrolled 6 samples, FXC safe)
float ScreenSpaceSunOcclusion(float2 uv, float depth, float2 sunDir2D,
                              float spread)
{
    float2 stepUV = sunDir2D * spread * 0.166667;
    float lit = 0.0;
    float validSamples = 0.0;

    [unroll]
    for (int i = 1; i <= 6; i++)
    {
        float2 sampleUV = uv + stepUV * (float)i;

        if (any(sampleUV < 0.001) || any(sampleUV > 0.999))
            continue;

        validSamples += 1.0;
        float sampleDepth = GetLinearDepth(sampleUV);

        float depthDiff = depth - sampleDepth;
        float occluded = saturate(depthDiff * 50.0);

        lit += 1.0 - occluded;
    }

    return (validSamples > 0.5) ? lit / validSamples : 1.0;
}

// Sun screen UV projection (SB-enhanced + ViewProjectionMatrix fallback)
// Returns: .xy = sun UV in [0,1], .z = validity flag (1.0 = valid sun position)
float3 GetSunScreenUV()
{
    // Tier 1: SB path — precise game-engine projected sun position
    if (SB_IsActive() && SB_Sun_NDC.z > 0.5)
        return float3(SB_SunScreenUV(), 1.0);

    // Tier 2: ViewProjectionMatrix — project world-space sun direction to screen
    // SunDirection.xyz is world-space direction TOWARD the sun.
    // w=0 projects a direction (point at infinity) — no CameraPosition needed,
    // and the view matrix translation is naturally zeroed out.
    // Output .w = view-space Z of sun direction (positive = in front of camera).
    float sunLen = length(SunDirection.xyz);
    if (sunLen > 0.001 && SunDirection.w < 0.5)
    {
        float3 sunDir = SunDirection.xyz / sunLen;

        float4 sunClip = mul(float4(sunDir, 0.0), ViewProjectionMatrix);

        // sunClip.w > 0 = sun is in front of camera
        if (sunClip.w > 0.0001)
        {
            float2 sunUV = (sunClip.xy / sunClip.w) * float2(0.5, -0.5) + 0.5;

            // Allow off-screen sun (rays still scatter from edges)
            if (sunUV.x > -0.5 && sunUV.x < 1.5 && sunUV.y > -0.5 && sunUV.y < 1.5)
                return float3(sunUV, 1.0);
        }
    }

    // Tier 3: screen center fallback (daytime exterior only)
    if (ENightDayFactor > 0.1 && EInteriorFactor < 0.5)
        return float3(0.5, 0.4, 1.0);

    return float3(0.5, 0.5, 0.0);  // invalid — no rays
}

// Normalized sun tint color (SB-enhanced + TOD-blended fallback)
// Always returns a unit-peak color suitable for tinting effects
float3 GetSunTint()
{
    if (SB_IsActive())
    {
        float3 c = SB_Atmos_Sunlight.rgb;
        float peak = max(max(c.r, c.g), c.b);
        if (peak > 0.01) return c / peak;
    }
    float3 tod = float3(1.0, 0.68, 0.32) * TimeOfDay1.x
               + float3(1.0, 0.96, 0.88) * TimeOfDay1.y
               + float3(1.0, 0.60, 0.28) * TimeOfDay1.z
               + float3(0.35, 0.45, 0.65) * TimeOfDay1.w;
    float peak = max(max(tod.r, tod.g), tod.b);
    return (peak > 0.01) ? tod / peak : float3(1.0, 0.95, 0.85);
}


//=== VERTEX SHADER ===//

void VS_Basic(inout float4 pos : SV_POSITION, inout float2 txcoord : TEXCOORD0)
{
    pos.w = 1.0;
}


//=== PIXEL SHADERS ===//

// --- Main PrePass ---

float4 PS_PrePass(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float3 color = TextureColor.SampleLevel(smpPoint, txcoord, 0).rgb;
    float  depth = GetLinearDepth(txcoord);

    // Sky early-out
    if (depth > 0.99)
        return float4(color, 1.0);

    float3 albedo = color;

    // Resolve view-space normal early (shared by water mask + GI)
    float3 viewNormal = TextureNormal.SampleLevel(smpPoint, txcoord, 0).xyz * 2.0 - 1.0;
    bool hasGBufferNormal = dot(viewNormal, viewNormal) > 0.01;
    if (!hasGBufferNormal)
        viewNormal = NormalFromDepth(txcoord);
    else
        viewNormal = normalize(viewNormal);

    // Water surface mask (reduces prepass effects on detected water pixels)
    float waterMask = DetectWater(txcoord, depth);
    float waterScale = 1.0 - waterMask * ui_Water_Reduction;

    float hazeAmount = 0.0;


    // =================================================================
    //  1. VB-SSGI — Visibility Bitmask AO + Indirect Light
    // =================================================================

    [branch] if (ui_GI_Enable)
    {
        // Distance fade
        float fadeFactor = 1.0 - saturate((depth - ui_GI_FadeStart) / max(ui_GI_FadeEnd - ui_GI_FadeStart, 0.001));

        if (fadeFactor > 0.01)
        {
            // Reconstruct view-space position and normal
            float3 viewPos = ViewPosFromUV(txcoord, depth);

            // DNI radius interpolation
            float worldRadius = lerp(lerp(ui_GI_RadiusNight, ui_GI_RadiusDay, ENightDayFactor),
                                     ui_GI_RadiusInterior, EInteriorFactor);

            // World radius -> UV radius (FOV-aware projection)
            float tanHFov_gi = tan(FieldOfView * 0.5 * PI / 180.0);
            float worldZ = depth * 3000.0;
            float uvRadius = worldRadius / (2.0 * worldZ * tanHFov_gi * ScreenSize.z);
            uvRadius = min(uvRadius, 256.0 * ScreenSize.y); // max 256 pixels
            float pixelRadius = uvRadius * ScreenSize.x;

            if (pixelRadius >= 1.0) // at least 1 pixel
            {
                // 2D R2 temporal jitter (Gilcher improved coefficients for >1M usable indices)
                // .x = direction rotation, .y = step offset — decorrelated across frames
                float2 r2Jitter = frac(float2(0.2451223338, 0.4301597090) * Timer.z + 0.5);
                float frameJitter = r2Jitter.x * (PI / (float)VB_NUM_DIRS);

                float thickness = ui_GI_Thickness;

                float totalAO = 0.0;
                float3 totalGI = float3(0.0, 0.0, 0.0);
                float3 totalBentNormal = float3(0.0, 0.0, 0.0);
                float totalWeight = 0.0;

                [loop]
                for (int dir = 0; dir < VB_NUM_DIRS; dir++)
                {
                    float angle = ((float)dir + 0.5) * (PI / (float)VB_NUM_DIRS) + frameJitter;
                    float2 sliceDir;
                    sincos(angle, sliceDir.y, sliceDir.x);

                    // Project normal into slice plane for proper cosine weighting
                    float2 projN = float2(dot(viewNormal.xy, sliceDir), viewNormal.z);
                    float projNLen = length(projN);
                    float normalAngle = atan2(projN.y, projN.x);
                    float normalWeight = max(projNLen, 0.05);

                    uint occlusionBitmask = 0u;
                    float3 sliceBounce = float3(0.0, 0.0, 0.0);

                    float2 noise = Hash2D(pos.xy + float2(dir * 7.3, 0.0));
                    float stepJitter = frac(noise.x + r2Jitter.y); // spatial + temporal decorrelation

                    // ---- March positive direction ----
                    [loop]
                    for (int stepP = 1; stepP <= VB_NUM_STEPS; stepP++)
                    {
                        // Power-1.5 step distribution: concentrates samples near pixel
                        float linearT = ((float)stepP - 0.5 + stepJitter) / (float)VB_NUM_STEPS;
                        float t = linearT * sqrt(linearT); // pow(linearT, 1.5)
                        float2 sampleUV = txcoord + sliceDir * t * uvRadius * float2(1.0, ScreenSize.z);

                        if (any(sampleUV < 0.001) || any(sampleUV > 0.999))
                            continue;

                        float sampleDepth = GetLinearDepth(sampleUV);
                        float3 samplePos = ViewPosFromUV(sampleUV, sampleDepth);

                        float3 diff = samplePos - viewPos;
                        float dist = length(diff);

                        if (dist < 0.0001) continue;

                        // Physically-based falloff: 1/(1+(d/0.556R)^1.6)
                        // Matches solid-angle formula for disc occluder (Phase 2 §5.2)
                        float worldDist = dist * 3000.0;
                        float normDist = worldDist / worldRadius;
                        float falloff = rcp(1.0 + pow(normDist * 1.799, 1.6));

                        // Hemisphere-aware asymmetry: samples behind the normal fade faster
                        float cosToNormal = dot(normalize(diff.xy), viewNormal.xy);
                        falloff *= lerp(1.0, 0.7, saturate(-cosToNormal));

                        if (falloff < 0.01) continue;

                        // Elevation angle + finite thickness
                        float elevation = asin(clamp(diff.z / dist, -1.0, 1.0));
                        float angThick = atan2(thickness, worldDist);

                        float frontAngle = elevation;
                        float backAngle = min(elevation + angThick, HALF_PI);

                        uint sectorMask = FlagSectors(frontAngle, backAngle);

                        // Accumulate GI from newly-occluded sectors only
                        uint newlyOccluded = sectorMask & ~occlusionBitmask;
                        float newWeight = (float)countbits(newlyOccluded) / (float)VB_SECTORS;

                        if (newWeight > 0.001)
                        {
                            // Adaptive MIP: scale with screen-space step distance
                            float mipLevel = clamp(log2(max(t * pixelRadius * 0.5, 1.0)), 0.0, 4.0);
                            float3 sampleColor = TextureColor.SampleLevel(smpLinear, sampleUV, mipLevel).rgb;
                            sliceBounce += sampleColor * newWeight * falloff;
                        }

                        occlusionBitmask |= sectorMask;
                    }

                    // ---- March negative direction ----
                    [loop]
                    for (int stepN = 1; stepN <= VB_NUM_STEPS; stepN++)
                    {
                        float linearT = ((float)stepN - 0.5 + stepJitter) / (float)VB_NUM_STEPS;
                        float t = linearT * sqrt(linearT);
                        float2 sampleUV = txcoord - sliceDir * t * uvRadius * float2(1.0, ScreenSize.z);

                        if (any(sampleUV < 0.001) || any(sampleUV > 0.999))
                            continue;

                        float sampleDepth = GetLinearDepth(sampleUV);
                        float3 samplePos = ViewPosFromUV(sampleUV, sampleDepth);

                        float3 diff = samplePos - viewPos;
                        float dist = length(diff);

                        if (dist < 0.0001) continue;

                        float worldDist = dist * 3000.0;
                        float normDist = worldDist / worldRadius;
                        float falloff = rcp(1.0 + pow(normDist * 1.799, 1.6));

                        // Hemisphere-aware asymmetry: samples behind the normal fade faster
                        float cosToNormal = dot(normalize(diff.xy), viewNormal.xy);
                        falloff *= lerp(1.0, 0.7, saturate(-cosToNormal));

                        if (falloff < 0.01) continue;

                        // Negative direction: mirror elevation into opposite bitmask half
                        float elevation = asin(clamp(diff.z / dist, -1.0, 1.0));
                        float negElevation = -elevation;
                        float angThick = atan2(thickness, worldDist);

                        float frontAngle = max(negElevation - angThick, -HALF_PI);
                        float backAngle = negElevation;

                        uint sectorMask = FlagSectors(frontAngle, backAngle);

                        uint newlyOccluded = sectorMask & ~occlusionBitmask;
                        float newWeight = (float)countbits(newlyOccluded) / (float)VB_SECTORS;

                        if (newWeight > 0.001)
                        {
                            float mipLevel = clamp(log2(max(t * pixelRadius * 0.5, 1.0)), 0.0, 4.0);
                            float3 sampleColor = TextureColor.SampleLevel(smpLinear, sampleUV, mipLevel).rgb;
                            sliceBounce += sampleColor * newWeight * falloff;
                        }

                        occlusionBitmask |= sectorMask;
                    }

                    // Normal-oriented hemisphere masking:
                    // Only count sectors within the surface normal's hemisphere
                    float hemiStart = max(normalAngle - HALF_PI, -HALF_PI);
                    float hemiEnd   = min(normalAngle + HALF_PI,  HALF_PI);
                    uint hemisphereMask = FlagSectors(hemiStart, hemiEnd);

                    uint visibleInHemi = (~occlusionBitmask) & hemisphereMask;

                    // Cosine-weighted AO: integrate cos(θ - θ_n) over visible angular runs
                    // This is radiometrically correct — zenith sectors contribute more than horizon
                    float sliceAO = CosineWeightedAO(visibleInHemi, normalAngle);

                    totalAO += sliceAO * normalWeight;
                    totalGI += sliceBounce * normalWeight;

                    // Bent normal: average unoccluded direction in the slice plane
                    // Compute weighted center of visible sectors
                    float bentAngle = 0.0;
                    uint visBits = visibleInHemi;
                    if (visBits != 0u)
                    {
                        float sectorW = PI / (float)VB_SECTORS;
                        float sumAngle = 0.0;
                        float sumW = 0.0;
                        uint scan = visBits;
                        // Fast scan: use run starts/ends to compute weighted center
                        [loop]
                        for (int br = 0; br < 4; br++)
                        {
                            if (scan == 0u) break;
                            int rs = firstbitlow(scan);
                            uint sh = scan >> (uint)rs;
                            int rl = firstbitlow(~sh);
                            if (rl <= 0) rl = 32 - rs;
                            float mid = ((float)rs + (float)rl * 0.5) * sectorW - HALF_PI;
                            float w = (float)rl;
                            sumAngle += mid * w;
                            sumW += w;
                            uint rm = ((uint)rl < 32u) ? (((1u << (uint)rl) - 1u) << (uint)rs) : (0xFFFFFFFF << (uint)rs);
                            scan &= ~rm;
                        }
                        if (scan != 0u)
                        {
                            float midFallback = 0.0; // hemisphere center
                            float wf = (float)countbits(scan);
                            sumAngle += midFallback * wf;
                            sumW += wf;
                        }
                        bentAngle = (sumW > 0.0) ? (sumAngle / sumW) : 0.0;
                    }
                    float3 bentDir = float3(sliceDir * cos(bentAngle), sin(bentAngle));
                    totalBentNormal += bentDir * normalWeight;

                    totalWeight += normalWeight;
                }

                // Normalize
                float aoRaw = (totalWeight > 0.001) ? (totalAO / totalWeight) : 1.0;
                float3 gi = (totalWeight > 0.001) ? (totalGI / totalWeight) : float3(0.0, 0.0, 0.0);
                float3 bentNormal = (totalWeight > 0.001) ? normalize(totalBentNormal) : viewNormal;

                // Screen-edge fade
                float2 edgeDist = min(txcoord, 1.0 - txcoord);
                float edgeFade = saturate(min(edgeDist.x, edgeDist.y) / max(uvRadius * 0.5, 0.03));
                aoRaw = lerp(1.0, aoRaw, edgeFade);
                gi *= edgeFade;

                // DNI intensity interpolation
                float intensity = lerp(lerp(ui_GI_IntensityNight, ui_GI_IntensityDay, ENightDayFactor),
                                       ui_GI_IntensityInterior, EInteriorFactor);

                // Apply AO: power curve + intensity + fade
                float ao = pow(saturate(1.0 - aoRaw), ui_GI_Power) * intensity * fadeFactor;
                ao = 1.0 - saturate(ao);

                // Denoise: depth-variance-aware bilateral softening.
                // Uses local depth variance to detect noisy regions vs true edges.
                // High depth variance = geometry edge (preserve). Low variance + AO
                // noise = sampling artifact (smooth aggressively).
                // At denoise=1, full bilateral softening; at denoise=0, raw AO.
                if (ui_GI_Denoise > 0.01)
                {
                    // Estimate local depth variance for edge detection
                    float depthVar = DN_EstimateVariance3x3(TextureDepth, smpPoint,
                                                             txcoord, float2(ScreenSize.y, ScreenSize.y * ScreenSize.z));
                    // Adaptive sigma: low depth variance = aggressive smooth, high = preserve
                    float adaptiveSigma = DN_VarianceGuidedSigma(depthVar, 0.3, 0.02, 0.0001);
                    float aoSmooth = smoothstep(0.0, 1.0, ao);
                    ao = lerp(ao, aoSmooth, ui_GI_Denoise * adaptiveSigma * 5.0);
                    // Raise the minimum AO (floor): prevents harsh black crevices
                    ao = max(ao, ui_GI_Denoise * 0.15);
                }

                // Water mask: reduce AO darkening on water surfaces
                ao = lerp(ao, 1.0, waterMask * ui_Water_Reduction);

                // Multi-bounce AO
                if (ui_GI_MultiBounce)
                {
                    float3 alb = saturate(albedo / max(dot(albedo, LUM_709), 0.01));
                    alb = max(alb, 0.04);
                    float3 aoMB = MultiBounceAO(ao, alb);
                    color *= aoMB;
                }
                else
                {
                    color *= ao;
                }

                // Specular occlusion: Lagarde 2014
                // Surfaces at glancing angles with high AO still receive specular
                // reflections (which travel tighter cones than diffuse). This lifts
                // the darkening for highlights, preventing over-occluded metals/water.
                if (ui_GI_SpecOccStrength > 0.001)
                {
                    float3 viewDir = normalize(-viewPos);
                    float NdotV = saturate(dot(viewNormal, viewDir));
                    // SO = saturate(pow(NdotV + ao, exp2(-16*r - 1)) - 1 + ao)
                    float soExponent = exp2(-16.0 * ui_GI_SpecOccRoughness - 1.0);
                    float so = saturate(pow(NdotV + ao, soExponent) - 1.0 + ao);
                    // Lift: where SO > AO, specular sees more than diffuse
                    float specLift = max(so - ao, 0.0) * ui_GI_SpecOccStrength;
                    // Apply proportional brightness lift for specular highlights
                    color += color * specLift;
                }

                // Add indirect bounce light with bent normal directional modulation
                if (ui_GI_BounceIntensity > 0.001)
                {
                    float giLuma = dot(gi, LUM_709);
                    gi = lerp(giLuma, gi, ui_GI_BounceSaturation);

                    // Bent normal modulation: GI is stronger when the average
                    // unoccluded direction faces the light source (ENB-native SunDirection)
                    float2 sunDir2D = SunDirection.xy;
                    float sunLen = length(sunDir2D);
                    if (sunLen > 0.01 && SunDirection.w < 0.5)
                    {
                        float bentDotLight = saturate(dot(bentNormal.xy, sunDir2D / sunLen) * 0.5 + 0.5);
                        gi *= lerp(0.7, 1.3, bentDotLight);
                    }

                    color += gi * ui_GI_BounceIntensity * fadeFactor * waterScale;
                }

                // Skylighting: 5-sample cone-aperture sky evaluation
                //
                // 1. AO → cone half-angle (aoRaw=1 → 90° full hemisphere)
                // 2. Bent normal = center of visible sky cone
                // 3. 5 hemisphere samples: bent normal, zenith-biased,
                //    horizon-biased, left-lateral, right-lateral
                // 4. Self-shadowing via bent-vs-surface normal deviation
                // 5. Ground bounce for downward-facing surfaces
                //
                // Ref: Frostbite 2015 SH skylight, UE4 bent normal ambient
                if (ui_Sky_Enable && ui_Sky_Intensity > 0.001)
                {
                    // --- Cone aperture from AO ---
                    float coneAngle = acos(1.0 - aoRaw) * (2.0 / PI);
                    float coneWeight = pow(saturate(coneAngle), ui_Sky_ConeSharpness);

                    // --- Primary: bent normal direction ---
                    float3 skyColor = EvaluateSkyGradient(bentNormal,
                        SunDirection.xy, SunDirection.w);

                    // --- Cone integration: 4 offset samples ---
                    // Wider cone → more off-axis influence → richer color

                    // Zenith-biased: sky directly above
                    float3 coneUp = normalize(float3(bentNormal.x * 0.25,
                        lerp(bentNormal.y, 1.0, 0.75), bentNormal.z * 0.25));
                    float3 skyZenith = EvaluateSkyGradient(coneUp,
                        SunDirection.xy, SunDirection.w);

                    // Horizon-biased: atmospheric band at eye level
                    float3 coneHoriz = normalize(float3(bentNormal.x,
                        lerp(bentNormal.y, 0.05, 0.65), bentNormal.z));
                    float3 skyHoriz = EvaluateSkyGradient(coneHoriz,
                        SunDirection.xy, SunDirection.w);

                    // Lateral samples: left/right of bent normal for
                    // azimuthal variation (captures sun-facing vs shadow-facing)
                    float3 bentPerp = normalize(float3(-bentNormal.z, 0.0, bentNormal.x));
                    float3 coneLeft = normalize(bentNormal + bentPerp * 0.5);
                    float3 coneRight = normalize(bentNormal - bentPerp * 0.5);
                    float3 skyLeft = EvaluateSkyGradient(coneLeft,
                        SunDirection.xy, SunDirection.w);
                    float3 skyRight = EvaluateSkyGradient(coneRight,
                        SunDirection.xy, SunDirection.w);

                    // Weighted blend: wider cone = more off-axis influence
                    float coneBlend = saturate(coneAngle * 1.5);
                    float3 offAxis = skyZenith * 0.30 + skyHoriz * 0.25
                                   + skyLeft * 0.15 + skyRight * 0.15
                                   + skyColor * 0.15;
                    skyColor = lerp(skyColor, offAxis, coneBlend);

                    // --- Ground bounce for downward-facing surfaces ---
                    if (ui_Sky_GroundBounce > 0.001)
                    {
                        float3 groundEval = EvaluateSkyGradient(float3(0.0, -1.0, 0.0),
                            SunDirection.xy, SunDirection.w);
                        float downFacing = saturate(-bentNormal.y);
                        skyColor += groundEval * downFacing * ui_Sky_GroundBounce;
                    }

                    // --- Self-shadowing ---
                    // When the bent normal points low or downward, nearby geometry
                    // is blocking sky access (overhangs, doorways, tree canopy).
                    // Measure how much the bent normal's elevation deviates from
                    // the surface normal's — large deviation = heavy occlusion.
                    float bentElevation = bentNormal.y; // [-1, 1], >0 = sky-facing
                    float normalElevation = viewNormal.y;

                    // Self-shadow: how far bent normal has been pushed away from
                    // the surface normal toward occluders. Large difference =
                    // strong directional occlusion (geometry shadowing from above).
                    float bentDeviation = saturate(normalElevation - bentElevation);
                    float selfShadow = 1.0 - bentDeviation * ui_Sky_SelfShadow;
                    selfShadow = max(selfShadow, 0.05); // never fully black

                    // --- Horizon fade ---
                    // Surfaces whose visible sky direction is near the horizon
                    // receive less sky contribution (long atmospheric path,
                    // terrain occlusion at low elevation angles).
                    float horizonTerm = saturate(bentElevation * ui_Sky_HorizonFade + 0.3);

                    // --- Combine visibility ---
                    float skyVisibility = coneWeight * selfShadow * horizonTerm;

                    // Multiscatter minimum: even fully occluded surfaces receive
                    // some scattered light (interreflection approximation)
                    skyVisibility = max(skyVisibility, ui_Sky_MultiscatterMin);

                    // Interior suppression: reduce skylighting indoors
                    float interiorDamp = 1.0 - EInteriorFactor * 0.7;

                    color += skyColor * skyVisibility * ui_Sky_Intensity
                           * fadeFactor * interiorDamp * waterScale;
                }
            }
        }
    }


    // =================================================================
    //  2. Volumetric Fog + God Rays — Merged forward march
    //     Ref: MegaShader Merging Analysis §1 (VoluFog + GodRays)
    //     Both accumulate along the same march: fog via density integration,
    //     god rays via sky-visibility sampling along the pixel→sun line.
    //     Interaction is inherent: fog transmittance attenuates god rays,
    //     god ray light scatters through fog.
    // =================================================================

    float godRayIntensity = 0.0;

    // Determine god ray sun UV before the fog block (used even if fog is off)
    float3 grSunInfo = float3(0.5, 0.5, 0.0);
    bool grEnabled = ui_GR_Enable && EInteriorFactor < 0.5;
    if (grEnabled)
        grSunInfo = GetSunScreenUV();

    [branch] if (ui_Vol_Enable || (grEnabled && grSunInfo.z > 0.5))
    {
        float baseDensity = ui_Vol_Enable
            ? lerp(lerp(ui_Vol_DensityNight, ui_Vol_DensityDay, ENightDayFactor),
                   ui_Vol_DensityInterior, EInteriorFactor)
            : 0.0;

        float skyMask = 1.0 - smoothstep(ui_Vol_SkyProtect - 0.02, ui_Vol_SkyProtect, depth);
        bool doFog = ui_Vol_Enable && baseDensity > 1e-7 && skyMask > 0.01;
        bool doGodRays = grEnabled && grSunInfo.z > 0.5;

        if (doFog || doGodRays)
        {
            // --- View ray setup (shared by fog + god rays) ---
            bool useWorldSpace = false;
            float3 camPos = float3(0.0, 0.0, 0.0);
            float3 rayDir = float3(0.0, 0.0, 1.0);
            float  rayLen = GetWorldZ(depth);

            if (SB_IsActive())
            {
                float3 camRight   = float3(SB_View_Row0.x, SB_View_Row1.x, SB_View_Row2.x);
                float3 camUp      = float3(SB_View_Row0.y, SB_View_Row1.y, SB_View_Row2.y);
                float3 camForward = float3(SB_View_Row0.z, SB_View_Row1.z, SB_View_Row2.z);

                float fwdLen = length(camForward);
                if (fwdLen > 0.5)
                {
                    camRight   = normalize(camRight);
                    camUp      = normalize(camUp);
                    camForward = camForward / fwdLen;

                    float2 ndc = txcoord * 2.0 - 1.0;
                    ndc.y = -ndc.y;
                    float3 viewRay = float3(ndc.x / SB_Proj_Row0.x,
                                            ndc.y / SB_Proj_Row1.y,
                                            1.0);

                    rayDir = normalize(camRight * viewRay.x
                                     + camUp   * viewRay.y
                                     + camForward * viewRay.z);
                    camPos = SB_Camera_WorldPos.xyz;

                    float cosAngle = dot(rayDir, camForward);
                    rayLen = GetWorldZ(depth) / max(cosAngle, 0.001);

                    useWorldSpace = true;
                }
            }

            if (!useWorldSpace)
            {
                float fovRad  = FieldOfView * PI / 180.0;
                float tanHFov = tan(fovRad * 0.5);

                float2 ndc = txcoord * 2.0 - 1.0;
                ndc.y = -ndc.y;

                rayDir = normalize(float3(ndc.x * tanHFov * ScreenSize.z,
                                          ndc.y * tanHFov, 1.0));
                rayLen = GetWorldZ(depth) / max(rayDir.z, 0.001);

                // Without SB world position, camera height is unknown.
                // Offset above fog base so horizontal views aren't at uniform
                // density (which creates the classic "flat wall" fog look).
                // This makes fog denser in the lower screen and thinner above.
                camPos.y = 120.0;
            }

            rayLen = min(rayLen, ui_Vol_MaxDistance);

            // --- Fog setup (only if fog is active) ---
            float3 fogColorNear = float3(0.5, 0.5, 0.5);
            float3 fogColorFar  = float3(0.5, 0.5, 0.5);
            float3 sunDirWorld  = float3(0.0, 0.0, 1.0);
            float3 sunColor     = float3(1.0, 0.95, 0.85);
            float  phase        = 1.0 / (4.0 * PI);
            float  baseHeight   = ui_Vol_BaseHeight;
            float  heightFalloff = 0.0;
            float  noiseHeightFade = ui_Vol_NoiseHeightFade;
            float2 windDir2D    = float2(1.0, 0.0);
            float  sunOcclusion = 1.0;

            if (doFog)
            {
                // Fog color
                if (ui_Vol_UseSBFog && SB_IsActive())
                {
                    fogColorNear = SB_Fog_NearColor.rgb;
                    fogColorFar  = SB_Fog_FarColor.rgb;
                }
                else
                {
                    fogColorNear = ui_Vol_Color;
                    float3 horizonShift = float3(0.55, 0.52, 0.50) * TimeOfDay1.x
                                        + float3(0.50, 0.52, 0.56) * TimeOfDay1.y
                                        + float3(0.52, 0.42, 0.38) * TimeOfDay1.z
                                        + float3(0.12, 0.14, 0.18) * TimeOfDay1.w;
                    fogColorFar = lerp(ui_Vol_Color, horizonShift, 0.4);
                }
                fogColorNear = fogColorNear / (fogColorNear + 1.0);
                fogColorFar  = fogColorFar  / (fogColorFar  + 1.0);

                // Sun direction (world space)
                if (SB_IsActive() && length(SB_Sun_Direction.xyz) > 0.01)
                    sunDirWorld = normalize(SB_Sun_Direction.xyz);
                else
                {
                    float sunZ = (SunDirection.w > 0.5) ? -0.5 : 0.5;
                    sunDirWorld = normalize(float3(SunDirection.xy, sunZ));
                }

                // Phase function
                float cosTheta = dot(rayDir, sunDirWorld);
                static const float isotropicPhase = 1.0 / (4.0 * PI);
                float forwardPhase = CornetteShanks(cosTheta, ui_Vol_Anisotropy);
                float backwardPhase = SchlickPhase(cosTheta, -0.4);
                float dualPhase = lerp(forwardPhase, backwardPhase, 0.15);
                phase = lerp(isotropicPhase, dualPhase, ui_Vol_SunIntensity);

                // Sun color
                if (SB_IsActive())
                {
                    sunColor = SB_Atmos_Sunlight.rgb;
                    float sunPeak = max(max(sunColor.r, sunColor.g), sunColor.b);
                    if (sunPeak > 0.01) sunColor /= sunPeak;
                    sunColor = max(sunColor, 0.05);
                }
                else
                {
                    sunColor = float3(1.0, 0.68, 0.32) * TimeOfDay1.x
                             + float3(1.0, 0.96, 0.88) * TimeOfDay1.y
                             + float3(1.0, 0.60, 0.28) * TimeOfDay1.z
                             + float3(0.35, 0.45, 0.65) * TimeOfDay1.w;
                    float sPeak = max(max(sunColor.r, sunColor.g), sunColor.b);
                    if (sPeak > 0.01) sunColor /= sPeak;
                    sunColor = max(sunColor, 0.05);
                }

                // Height fog
                if (SB_IsActive() && abs(SB_Fog_Height.x) > 0.01)
                    baseHeight = SB_Fog_Height.x;

                bool isInterior = (SB_IsActive())
                    ? (SB_Interior_Flags.x > 0.5)
                    : (EInteriorFactor > 0.5);
                heightFalloff = isInterior ? 0.0 : ui_Vol_HeightFalloff;

                // SB world coordinates are ~40x larger than ENB depth units
                // (Skyrim far clip ~131072 vs ENB depth scale 3000).
                // Without this, height fog density drops to zero above ground
                // level because exp(-5000 * 0.008) ≈ 0.
                if (useWorldSpace)
                {
                    heightFalloff   *= 0.025;
                    noiseHeightFade *= 0.025;
                }

                // Wind
                if (SB_IsActive())
                {
                    float windAngle = SB_Wind.y;
                    sincos(windAngle, windDir2D.y, windDir2D.x);
                }
                else
                {
                    float windAngle = Timer.x * 0.004;
                    sincos(windAngle, windDir2D.y, windDir2D.x);
                }

                // Sun occlusion
                if (ui_Vol_OcclusionEnable)
                {
                    float2 sunScreenDir = float2(0.0, -1.0);
                    bool sunValid = false;

                    if (SB_IsActive() && SB_Sun_NDC.z > 0.5)
                    {
                        float2 sunUV = SB_SunScreenUV();
                        float2 toSun = sunUV - txcoord;
                        float toSunLen = length(toSun);
                        if (toSunLen > 0.001)
                        {
                            sunScreenDir = toSun / toSunLen;
                            sunValid = true;
                        }
                    }
                    else if (SunDirection.w < 0.5)
                    {
                        float sdLen = length(SunDirection.xy);
                        if (sdLen > 0.01)
                        {
                            sunScreenDir = SunDirection.xy / sdLen;
                            sunValid = true;
                        }
                    }

                    if (sunValid)
                    {
                        sunOcclusion = ScreenSpaceSunOcclusion(
                            txcoord, depth, sunScreenDir,
                            ui_Vol_OcclusionSpread
                        );
                        sunOcclusion = lerp(1.0, sunOcclusion, ui_Vol_OcclusionIntensity);
                    }
                }
            }

            // --- God ray setup ---
            float2 grSunUV    = grSunInfo.xy;
            float2 grDeltaUV  = float2(0.0, 0.0);
            float  grCosTheta = 0.0;

            if (doGodRays)
            {
                grDeltaUV  = grSunUV - txcoord;
                grCosTheta = 1.0 / (1.0 + length(grDeltaUV) * 2.5);
            }

            // --- Merged march loop ---
            float jitter = TemporalIGN(pos.xy, Timer.z) - 0.5;
            float expPow = ui_Vol_ExpSteps;

            float3 inscattered   = float3(0.0, 0.0, 0.0);
            float  transmittance = 1.0;
            float  grAccum       = 0.0;
            float  grWeight      = 1.0 / (float)VOL_STEPS;

            [loop]
            for (int s = 0; s < VOL_STEPS; s++)
            {
                // Exponential step distribution: pow > 1 clusters steps near camera
                // 1.0 = uniform (default), 1.6 = recommended, 2.0+ = aggressive near-bias
                float tLinear = ((float)s + 0.5 + jitter) / (float)VOL_STEPS;
                float tFrac   = pow(saturate(tLinear), expPow);
                float stepSize = (pow(saturate(tLinear + 0.5 / (float)VOL_STEPS), expPow)
                                - pow(saturate(tLinear - 0.5 / (float)VOL_STEPS), expPow)) * rayLen;
                float t     = tFrac * rayLen;

                // --- God ray sky sampling (along pixel→sun line) ---
                if (doGodRays)
                {
                    float2 grSampleUV = txcoord + grDeltaUV * tFrac * ui_GR_Density;

                    if (all(grSampleUV > 0.0) && all(grSampleUV < 1.0))
                    {
                        float grDepth = GetLinearDepth(grSampleUV);
                        float grSky   = smoothstep(ui_GR_SkyThreshold - 0.05,
                                                   ui_GR_SkyThreshold, grDepth);

                        // Also check bright pixels (sun disc, clouds)
                        float3 grCol = TextureOriginal.SampleLevel(smpLinear,
                                                                    grSampleUV, 0).rgb;
                        float grBright = saturate(
                            (dot(grCol, LUM_709) - ui_GR_BrightThreshold) * 0.3);

                        float grLight = max(grSky, grBright);

                        // Exponential decay along march
                        float grDecay = pow(ui_GR_Decay, (float)s);

                        // God rays attenuated by fog transmittance (inherent interaction)
                        grAccum += grLight * grDecay * grWeight * transmittance;
                    }
                }

                // --- Volumetric fog density ---
                if (doFog)
                {
                    float3 samplePos = camPos + rayDir * t;
                    float sampleHeight = useWorldSpace ? samplePos.z : samplePos.y;

                    float density;
                    if (heightFalloff > 0.0001)
                        density = HeightFogDensity(sampleHeight, baseHeight, baseDensity, heightFalloff);
                    else
                        density = baseDensity;

                    // Procedural noise
                    if (ui_Vol_NoiseEnable && ui_Vol_NoiseStrength > 0.001)
                    {
                        float noiseHeightMask = exp(-max(sampleHeight - baseHeight, 0.0)
                                                    * noiseHeightFade);

                        if (noiseHeightMask > 0.01)
                        {
                            float effectiveScale = ui_Vol_NoiseScale;
                            if (!useWorldSpace)
                                effectiveScale *= 10.0;

                            float noise = FogNoiseFBM(samplePos, Timer.x, windDir2D,
                                                      ui_Vol_NoiseWindSpeed,
                                                      effectiveScale,
                                                      ui_Vol_NoiseOctaves);
                            noise = noise * 2.0 - 0.5;

                            density += baseDensity * noise * ui_Vol_NoiseStrength * noiseHeightMask;
                            density = max(density, 0.0);
                        }
                    }

                    if (density > 1e-8)
                    {
                        float opticalDepth = density * ui_Vol_Extinction * stepSize;
                        float stepT = exp(-opticalDepth);

                        float rayProgress = t / max(rayLen, DELTA);
                        float3 fogColor = lerp(fogColorNear, fogColorFar, saturate(rayProgress));

                        // Sun-aligned color warmth: fog facing the sun picks up warm tint
                        float sunAlignWarmth = saturate(dot(rayDir, sunDirWorld) * 0.5 + 0.3);
                        float3 warmFog = fogColor * lerp(float3(1.0, 1.0, 1.0),
                            sunColor * 1.2, sunAlignWarmth * 0.3 * ENightDayFactor);

                        float ambientOccl = lerp(1.0, sunOcclusion, 0.5);
                        float3 lighting = warmFog * (ui_Vol_AmbientStrength * ambientOccl
                            + sunColor * phase * sunOcclusion);

                        // Multi-scatter approximation: boost ambient at high density
                        float multiScatBoost = 1.0 + (1.0 - stepT) * 0.3;
                        lighting *= multiScatBoost;

                        float3 stepInscatter = lighting * ui_Vol_ScatterAlbedo * (1.0 - stepT);

                        inscattered += transmittance * stepInscatter;
                        transmittance *= stepT;

                        if (transmittance < 0.003)
                            break;
                    }
                }
            }

            // --- Apply volumetric fog to scene ---
            if (doFog)
            {
                float fogAmount = (1.0 - transmittance) * skyMask * waterScale;

                color = color * lerp(1.0, transmittance, skyMask * waterScale)
                      + inscattered * skyMask * waterScale;

                if (ui_Vol_ContrastLoss > 0.001 && fogAmount > 0.001)
                {
                    float3 avgFogColor = lerp(fogColorNear, fogColorFar, 0.5);
                    float fogLuma = dot(avgFogColor, LUM_709);
                    float contrastFade = fogAmount * ui_Vol_ContrastLoss;
                    color = lerp(color, color + avgFogColor * fogLuma * contrastFade, contrastFade * 0.5);
                }

                if (ui_Vol_Desaturation > 0.001 && fogAmount > 0.001)
                {
                    float monoLuma = dot(color, LUM_709);
                    color = lerp(color, monoLuma, fogAmount * ui_Vol_Desaturation);
                }

                color = max(color, 0.0);
            }

            // --- Apply god rays to scene ---
            if (doGodRays && grAccum > 0.001)
            {
                // HG phase (angular falloff from sun)
                float grG  = ui_GR_PhaseG;
                float grG2 = grG * grG;
                float hgD  = 1.0 + grG2 - 2.0 * grG * grCosTheta;
                float hgP  = (1.0 - grG2) / max(hgD * sqrt(hgD), 0.001);

                // Silver lining (backscatter lobe)
                float grGB  = 0.4;
                float grG2B = grGB * grGB;
                float hgDB  = 1.0 + grG2B - 2.0 * grGB * grCosTheta;
                float bkP   = (1.0 - grG2B) / max(hgDB * sqrt(hgDB), 0.001);
                float grPhase = lerp(hgP, bkP, ui_GR_BackscatterBlend) * 0.25;

                godRayIntensity = saturate(grAccum * ui_GR_Exposure * grPhase)
                                * ui_GR_Intensity;

                // Shaft color with spectral Rayleigh extinction
                // Real crepuscular rays traverse atmosphere: blue scatters away first,
                // leaving warm-tinted shafts. Longer path (low sun) = more reddening.
                float3 shaftColor = ui_GR_Tint;
                float3 todSun = GetSunTint();
                shaftColor = lerp(ui_GR_Tint, ui_GR_Tint * todSun, 0.5);

                // Spectral extinction: Rayleigh coefficients scaled by sun elevation
                // Low sun = long path = strong blue attenuation (warm shafts)
                float grSinElev = max(abs(1.0 - grSunUV.y * 2.0), 0.05);
                float grAirMass = 1.0 / (grSinElev + 0.50572
                    * pow(max(degrees(asin(grSinElev)) + 6.07995, 0.1), -1.6364));
                float3 grExtinction = exp(-float3(0.03, 0.07, 0.18) * grAirMass);
                shaftColor *= grExtinction;

                // Additive composite
                color += shaftColor * godRayIntensity;
            }

            // Lightning flash burst (SB-only)
            if (SB_IsActive() && SB_Lightning.z > 0.01)
            {
                float flashBoost = SB_Lightning.z * 0.15;
                color += float3(0.7, 0.75, 0.9) * flashBoost
                       * saturate(1.0 - depth * 2.0);
            }
        }
    }


    // =================================================================
    //  3. Aerial Perspective — Rayleigh/Mie Atmospheric Scattering
    //
    //  Two-channel model: blue Rayleigh scatter + warm Mie forward scatter
    //  creates realistic "blue mountains, golden horizon" aerial perspective.
    //  Height-dependent density via view-ray altitude reconstruction.
    //
    //  Ref: Bruneton 2008, Hillaire 2020, Frostbite 2015
    // =================================================================

    [branch] if (ui_Haze_Enable && depth > ui_Haze_StartDist && depth < 0.95)
    {
        float hazeIntensity = lerp(
            lerp(ui_Haze_IntensityNight, ui_Haze_Intensity, ENightDayFactor),
            ui_Haze_IntensityInterior, EInteriorFactor);

        if (hazeIntensity > 0.001)
        {
            // Normalized distance from start
            float hazeDist = (depth - ui_Haze_StartDist)
                           / max(1.0 - ui_Haze_StartDist, 0.01);

            // Sky protection
            float skyProtect = smoothstep(ui_Haze_SkyProtection, 1.0, depth);
            float skyMask = 1.0 - skyProtect;

            // ---- View ray reconstruction ----
            float tanHFov_h = tan(FieldOfView * 0.5 * PI / 180.0);
            float2 hNDC = txcoord * 2.0 - 1.0;
            hNDC.y = -hNDC.y;
            float3 viewRay = normalize(float3(
                hNDC.x * tanHFov_h * ScreenSize.z,
                hNDC.y * tanHFov_h, 1.0));

            // Height-dependent density: exponential falloff with altitude
            // Positive viewRay.y = looking up = thinner atmosphere
            // Negative viewRay.y = looking down = denser atmosphere
            float heightFactor = exp(-max(viewRay.y, 0.0) * ui_Haze_HeightScale * 3.0);
            // Below horizon: extra density but capped
            float belowHorizon = saturate(-viewRay.y * 0.5);
            heightFactor = lerp(heightFactor, 1.0 + belowHorizon * 0.5,
                                belowHorizon);

            // ---- Rayleigh optical depth (wavelength-dependent: λ⁻⁴) ----
            // Blue scatters most, red scatters least → blue distance haze
            float3 rayleighCoeff = float3(0.58, 1.35, 3.31) * 0.01
                                 * ui_Haze_RayleighDensity;
            float3 rayleighOptDepth = rayleighCoeff * hazeDist * hazeIntensity
                                    * heightFactor * 4.0;

            // ---- Mie optical depth (wavelength-independent) ----
            float mieCoeff = 0.021 * ui_Haze_MieDensity;
            float mieOptDepth = mieCoeff * hazeDist * hazeIntensity
                              * heightFactor * 4.0;

            // ---- Beer-Lambert extinction (per-channel for Rayleigh) ----
            float3 totalOptDepth = rayleighOptDepth + mieOptDepth;
            float3 transmittance3 = exp(-totalOptDepth);
            float  transmittanceScalar = dot(transmittance3, 0.333);

            hazeAmount = saturate((1.0 - transmittanceScalar) * skyMask);

            if (hazeAmount > 0.001)
            {
                // ---- Sun direction ----
                float3 sunDir;
                if (SB_IsActive() && dot(SB_Sun_Direction.xyz,
                                         SB_Sun_Direction.xyz) > 0.001)
                    sunDir = normalize(SB_Sun_Direction.xyz);
                else
                    sunDir = normalize(float3(SunDirection.xy,
                                              max(abs(SunDirection.z), 0.1)));

                float cosTheta = dot(viewRay, sunDir);

                // ---- Rayleigh phase: (3/16π)(1 + cos²θ) ----
                float rayleighPhase = (3.0 / (16.0 * PI))
                                    * (1.0 + cosTheta * cosTheta);

                // ---- Mie phase: Henyey-Greenstein ----
                float g = ui_Haze_MieAnisotropy;
                float g2 = g * g;
                float mieD = 1.0 + g2 - 2.0 * g * cosTheta;
                float miePhase = (1.0 - g2) / max(mieD * sqrt(mieD), 0.001) * 0.25;

                // ---- Sun & ambient light ----
                float3 sunColor;
                if (SB_IsActive())
                {
                    sunColor = SB_Atmos_Sunlight.rgb;
                    float sPeak = max(max(sunColor.r, sunColor.g), sunColor.b);
                    if (sPeak > 0.01) sunColor /= sPeak;
                    sunColor = max(sunColor, 0.05);
                }
                else
                {
                    sunColor = float3(1.0, 0.65, 0.30) * TimeOfDay1.x
                             + float3(1.0, 0.96, 0.88) * TimeOfDay1.y
                             + float3(1.0, 0.55, 0.25) * TimeOfDay1.z
                             + float3(0.30, 0.40, 0.60) * TimeOfDay1.w;
                    float sPeak = max(max(sunColor.r, sunColor.g), sunColor.b);
                    if (sPeak > 0.01) sunColor /= sPeak;
                }

                // Ambient sky color for multi-scatter
                float3 ambientSky = EvaluateSkyGradient(float3(0.0, 0.5, 0.0),
                                                         SunDirection.xy, SunDirection.w);

                // Haze base color (SB or user tint)
                float3 hazeColor = ui_Haze_ColorTint;
                if (ui_Haze_UseSBFogColor && SB_IsActive())
                {
                    float3 sbFog = SB_Fog_FarColor.rgb;
                    if (dot(sbFog, sbFog) > 0.001)
                        hazeColor = lerp(hazeColor, sbFog, 0.6);
                }

                // ---- Inscattered light (analytical integration) ----
                // For each channel: inscatter = β × P(θ) × L_sun × (1 - T) / σ_total
                float3 oneMinusT = 1.0 - transmittance3;
                float3 invTotalOpt = 1.0 / max(totalOptDepth, 0.001);

                // Rayleigh inscatter: tinted by Rayleigh coefficients (blue)
                float3 rayleighInscatter = rayleighCoeff * rayleighPhase
                    * ui_Haze_RayleighTint * sunColor * ENightDayFactor
                    * oneMinusT * invTotalOpt;

                // Mie inscatter: warm sun glow (wavelength-independent)
                float3 mieInscatter = mieCoeff * miePhase
                    * hazeColor * sunColor * ENightDayFactor
                    * oneMinusT * invTotalOpt;

                // Ambient inscatter: isotropic sky contribution
                float3 ambientInscatter = (rayleighCoeff * 0.25 + mieCoeff * 0.5)
                    * ambientSky * oneMinusT * invTotalOpt;

                // Multi-scatter: approximate higher-order bounces
                // Increases ambient contribution in thick atmosphere
                float multiScatterBoost = 1.0 + ui_Haze_MultiScatter
                    * (1.0 - transmittanceScalar) * 2.0;

                float3 totalInscatter = (rayleighInscatter + mieInscatter
                    + ambientInscatter * multiScatterBoost)
                    * ui_Haze_Inscattering;

                // ---- Apply extinction + inscattering ----
                float sceneLuma = dot(color, LUM_709);

                // Desaturation at distance
                float3 desatColor = lerp(float3(sceneLuma, sceneLuma, sceneLuma),
                                         color,
                                         1.0 - ui_Haze_Desaturation * hazeAmount);

                // Contrast reduction at distance
                float3 midGray = float3(sceneLuma, sceneLuma, sceneLuma);
                desatColor = lerp(desatColor,
                                  lerp(midGray, desatColor,
                                       1.0 - ui_Haze_ContrastReduction),
                                  hazeAmount);

                // Per-channel extinction + inscatter (Rayleigh removes more red)
                float3 hazeResult = desatColor * transmittance3
                                  + totalInscatter;

                // Apply haze (reduced on water surfaces)
                float finalHaze = hazeAmount * waterScale;
                color = lerp(color, hazeResult, finalHaze);
            }
        }
    }


    // =================================================================
    //  Water Mask Debug Visualization
    // =================================================================

    if (ui_Water_Debug > 0.5)
    {
        if (ui_Water_Debug < 1.5)
        {
            // Mode 1: Water mask — orange = detected as water
            return float4(waterMask, waterMask * 0.6, 0.0, 1.0);
        }
        else if (ui_Water_Debug < 2.5)
        {
            // Mode 2: Raw GBuffer normals (undecoded [0,1])
            float3 gn = TextureNormal.SampleLevel(smpPoint, txcoord, 0).xyz;
            return float4(gn, 1.0);
        }
        else if (ui_Water_Debug < 3.5)
        {
            // Mode 3: Depth-derived normals (encoded [0,1])
            float3 dn = NormalFromDepth(txcoord) * 0.5 + 0.5;
            return float4(dn, 1.0);
        }
        else
        {
            // Mode 4: Diagnostic — R = SB active, G = waterZ mapped, B = depth
            // Red only   = SB active but waterZ is sentinel/invalid
            // Yellow-ish = SB active, valid waterZ, near surface
            // Purple     = SB active, valid waterZ, deep (far from surface)
            // Black      = SB not active
            float sbActive = SB_IsActive() ? 1.0 : 0.0;
            // Map waterZ to [0,1]: -20000 → 0, +20000 → 1
            float waterZMapped = saturate((SB_Fog_Height.x + 20000.0) / 40000.0);
            return float4(sbActive, waterZMapped, depth, 1.0);
        }
    }


    // =================================================================
    //  SkyrimBridge KeepAlive
    // =================================================================

    if (Timer.x < -99999.0)
    {
        color.r += SB_Sun_Direction.x + SB_Fog_FarColor.x + SB_Fog_NearColor.x;
        color.r += SB_Fog_Density.x + SB_Render_Frame.x + SB_Atmos_Ambient.x;
        color.r += SB_Camera_Info.x + SB_Interior_Flags.x;
        color.r += SB_Fog_Height.x + SB_Camera_WorldPos.x + SB_Atmos_Sunlight.x;
        color.r += SB_View_Row0.x + SB_View_Row1.x + SB_View_Row2.x + SB_View_Row3.x;
        color.r += SB_Proj_Row0.x + SB_Proj_Row1.x;
        color.r += SB_Wind.x + SB_Sun_NDC.x;
        color.r += SB_Player_Water.x;
        color.r += SB_UI_Menus.x;
    }


    return float4(color, 1.0);
}


//=== SSS ADDON ===//

#include "Addons/PrePass_SkinSSS.fxh"


//=== STYLIZATION ADDON ===//

#include "Addons/PrePass_StylizationSuite.fxh"


//=== SNOW COVER ADDON ===//

#include "Addons/PrePass_SnowCover.fxh"


//=== PARTICLE FIELD ADDON ===//

#include "Addons/PrePass_ParticleField.fxh"


//=== INCANDESCENCE ADDON ===//

#include "Addons/PrePass_Incandescence.fxh"


//=== PHOTO STUDIO ADDON ===//

#include "Addons/PrePass_PhotoStudio.fxh"


//=== SSR ADDON ===//

#include "Addons/PrePass_SSR.fxh"


//=== TECHNIQUES ===//
//
// Single-pass pipeline: all effects (VB-SSGI, skylighting, volumetric fog,
// atmospheric haze, god rays, compositing) are computed in PS_PrePass.
// SSS runs as two sub-technique passes after the main prepass.

technique11 EotE_PrePass <string UIName = "EotE: Pre-Pass";>
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_PrePass()));
    }
}

// SSS Pass 1: Horizontal Burley-profile scatter blur
technique11 EotE_PrePass1
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_SSS_Horizontal()));
    }
}

// SSS Pass 2: Vertical scatter blur + skin tone controls
technique11 EotE_PrePass2
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_SSS_Vertical()));
    }
}

// Stylization Pass: NPR effects (Kuwahara, watercolor, ink wash, cross-hatch, posterize)
technique11 EotE_PrePass3
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_Stylize()));
    }
}

// Snow Cover Pass: Screen-space snow accumulation on upward-facing surfaces
#if SNOW_LOADED
technique11 EotE_PrePass4
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_SnowCover()));
    }
}
#endif

// Particle Field Pass: Screen-space atmospheric particles (motes, fireflies, embers, dust, snow)
#if PARTICLES_LOADED
technique11 EotE_PrePass5
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_ParticleField()));
    }
}
#endif

// Incandescence Pass: Thermal glow and heat shimmer for fire/lava/forge/torch sources
#if INCANDESCENCE_LOADED
technique11 EotE_PrePass6
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_Incandescence()));
    }
}
#endif

// Photo Studio Pass: Composition guides, focus peaking, zebra stripes, histogram
#if PHOTO_LOADED
technique11 EotE_PrePass7
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_PhotoStudio()));
    }
}
#endif

// SSR Pass: Screen-space reflections (view-space march + binary refinement)
#if SSR_LOADED
technique11 EotE_PrePass8
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_SSR()));
    }
}
#endif
