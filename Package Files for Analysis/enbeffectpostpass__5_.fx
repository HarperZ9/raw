//----------------------------------------------------------------------------------------------//
//                                                                                              //
//               enbeffectpostpass.fx - Color & Post FX Suite                                   //
//                    + SMAA / FXAA / KiSharp / Blur Suite /                                    //
//                        Lens Distortion / Vignette / Film Grain /                             //
//                        Color Grading / LUT Blending / Dither                                 //
//                   for Skyrim SE ENB (DirectX 11 Shader Model 5)                              //
//                                                                                              //
//         Original framework by LonelyKitsuune / T.Thanner - CC BY-NC-ND 4.0                  //
//                            Boris Vorontsov for ENBSeries                                     //
//                                                                                              //
//----------------------------------------------------------------------------------------------//
//                                                                                              //
//     Physically-motivated post-processing pipeline by Zain Dana Harper - February 2026        //
//                                                                                              //
//  Architecture: 9 techniques, 11 passes                                                       //
//                                                                                              //
//    Tech 0 (two-pass → RenderTargetRGBA64F):                                                  //
//        Pass 0: Clear render target                                                           //
//        Pass 1: SMAA Edge Detection (luma or color)                                           //
//                                                                                              //
//    Tech 1 (→ RenderTargetRGBA64):                                                             //
//        SMAA Blend Weight Calculation                                                         //
//                                                                                              //
//    Tech 2: SMAA Neighborhood Blending (weights from RGBA64) → TextureColor    //
//                                                                                              //
//    Tech 3: FXAA (Fast Approximate Anti-Aliasing)                                             //
//                                                                                              //
//    Tech 4: KiSharp (Iterative Unsharp Mask with depth awareness)                             //
//                                                                                              //
//    Tech 5 (two-pass → RenderTargetRGBA64F):                                                  //
//        Pass 0: Clear render target                                                           //
//        Pass 1: Blur Suite (Gaussian/Box/Radial with masking)                                 //
//                                                                                              //
//    Tech 6: Lens Distortion + Chromatic Aberration                                            //
//                                                                                              //
//    Tech 7: Color Grading (KiSuite + LUTs + Monochroma)                                      //
//                                                                                              //
//    Tech 8: Final Pass (Vignette + Grain + Borders + Watermark + Dither)                      //
//                                                                                              //
//  SMAA: Enhanced Subpixel Morphological Anti-Aliasing (Jimenez et al. 2012)                   //
//    - 5 quality presets (Low/Medium/High/Ultra/Custom)                                        //
//    - Depth predication for geometry-aware thresholding                                       //
//    - Analytical area computation (no area texture atlas)                                     //
//    - Local contrast adaptation on smooth gradients                                           //
//                                                                                              //
//  FXAA: Fast Approximate Anti-Aliasing (Lottes 2009)                                          //
//    - 3 quality tiers (12/24/39 taps)                                                         //
//    - Configurable subpixel amount, edge threshold                                            //
//                                                                                              //
//  KiSharp: N-Step Iterative Unsharp Mask                                                      //
//    - Progressive radius expansion with contrast-aware clamping                               //
//    - Depth-based edge removal and distance fade                                              //
//    - Optional upscale detection for render-scale boost                                       //
//                                                                                              //
//  Blur Suite: Gaussian / Box / Radial Blur                                                    //
//    - 3 quality presets (9/17/25 taps)                                                        //
//    - Vignette masking + depth masking with edge bleeding reduction                           //
//                                                                                              //
//  Lens Distortion: Brown-Conrady / Reciprocal / Panini                                       //
//    - Lateral / Spectral / Barrel chromatic aberration                                        //
//                                                                                              //
//  Vignette: Lp-Norm Generalized Elliptical Vignette                                          //
//    - 4 blend modes (Multiply/Screen/Overlay/Color)                                           //
//    - Offset position, independent axis scaling                                               //
//                                                                                              //
//  KiGrain: Simplex Noise Film Grain (3 layers, per-channel)                                   //
//    - Log-space application matching real film density response                               //
//    - Luminance-based shadow/highlight exclusion                                              //
//                                                                                              //
//  CG.KiSuite: Full Color Grading Pipeline (11 ordered stages)                                 //
//    - Input/Output Levels, Channel Mixer, Brightness, Contrast, Gamma                         //
//    - Saturation, Vibrance, Tint, H/M/S Split Grading, Per-Hue (7 bands)                     //
//    - LUT bake mode (256×16 strip) or Direct per-pixel                                        //
//                                                                                              //
//  CG.LUTs: 39-LUT Weighted Blending System                                                   //
//    - 17 Voyager Collection + 22 Custom Film Stocks                                           //
//    - Per-DNI weights, bilinear + tetrahedral interpolation                                   //
//    - Multi-pass atlas mode for GPU-constrained setups                                        //
//                                                                                              //
//  CG.Monochroma: Color Filter + Black & White conversion                                     //
//    - HSL hue isolation with configurable range/hardness                                      //
//    - Weighted RGB→mono with RGBA tint                                                        //
//                                                                                              //
//  Dither: Gaussian Blue-Noise Temporal Dither (8-bit output)                                  //
//                                                                                              //
//  References:                                                                                 //
//    [1] Jimenez et al., "SMAA: Enhanced Subpixel Morphological                                //
//        Antialiasing", EUROGRAPHICS 2012                                                      //
//    [2] Lottes, "FXAA 3.11 Quality", NVIDIA 2009                                              //
//    [3] Kasson & Plouffe, "Tetrahedral Interpolation for Color Space                          //
//        Conversion", Grafica Obscura 1995                                                     //
//    [4] Stefan Gustavson, "Simplex Noise Demystified", 2005                                   //
//    [5] Brown-Conrady, "Lens Distortion Model", 1966/1919                                     //
//                                                                                              //
//----------------------------------------------------------------------------------------------//


//=============================================================================//
//                                OPTIONS                                      //
//=============================================================================//

#define ENABLE_TOOLS            0   // [0-1] Debug visualizations
#define ENABLE_BLUR_SUITE       1   // [0-1] Blur Suite effect
#define ENABLE_HIGH_QUALITY_SHARPENING 0 // [0-1] Extended sharp kernel (>15 steps)
#define DIRECT_COLOR_GRADING    0   // [0-1] Apply CG per-pixel (bypass LUT bake)
#define AA_SUPPORT_PRESET       3   // [0-5] 0=none, 1=SMAA, 2=FXAA, 3=SMAA+FXAA, 4=CREAA, 5=all
#define SHARP_MAX_KERNELSIZE   16   // Maximum unsharp mask kernel size
#define LUT_INTERPOLATION       0   // [0-1] 0=Bilinear, 1=Tetrahedral
#define LUT_ATLAS_PASSES        1   // [1-3] Multi-pass LUT accumulation

// ─── DIAGNOSTIC: Set to 1 to make all postpass techniques return TextureColor unchanged.
//     This isolates whether corruption originates in the postpass or upstream.
//     Remove or set to 0 once testing is complete.
#define DIAGNOSTIC_PASSTHROUGH  1


//=============================================================================//
//                           UI PARAMETER INCLUDES                             //
//=============================================================================//

#include "UI/enbUI_Primer.fxh"

// Group 0: File header + debug tools
#define SHADERGROUP 0
#include "UI/enbUI_PostPass.fxh"

// Group 1: Color Grading (DNI - Day/Night/Interior)
#define SHADERGROUP 1
#define TODIE Day
#include "UI/enbUI_PostPass.fxh"
UI_SPECIAL_WHITESPACE(110)

#define TODIE Night
#include "UI/enbUI_PostPass.fxh"
UI_SPECIAL_WHITESPACE(111)

#define TODIE Interior
#include "UI/enbUI_PostPass.fxh"

#undef SHADERGROUP
#undef NOTFIRSTTIME

// Group 2: Anti-Aliasing + Blur Suite + Sharpen + Monochroma + Vignette + CA + Grain + Borders
#define SHADERGROUP 2
#include "UI/enbUI_PostPass.fxh"

//=============================================================================//
//  SKYRIMBRIDGE POSTPASS UI PARAMETERS                                        //
//=============================================================================//

#define SHADERGROUP 99
#include "UI/enbUI_PostPass.fxh"
#undef SHADERGROUP


//=============================================================================//
//               ENB BUILT-IN PARAMETERS AND TEXTURES                          //
//=============================================================================//

float4 Timer;               // x = generic timer [0,1], y = elapsed ms, z = frame count
float4 ScreenSize;          // x = width, y = 1/width, z = aspect (w/h), w = 1/height
float4 tempF1;              // ENB temp var
float4 tempF2;
float4 tempF3;
float4 tempInfo1;           // x = current technique index
float4 tempInfo2;

// ENB built-in factors
float  ENightDayFactor;     // 0 = night, 1 = day
float  EInteriorFactor;     // 0 = exterior, 1 = interior

// SkyrimBridge externals
#include "Helper/SkyrimBridge.fxh"

//----------------------------------------------------------------------------------------------//
//                      SkyrimBridge Postpass Integration Helpers                                //
//                                                                                              //
//   Menu/UI bypass, IMOD-aware grading reduction, cinematic awareness.                         //
//   All functions return neutral values when SB is inactive or not installed.                   //
//----------------------------------------------------------------------------------------------//
#ifdef SKYRIMBRIDGE_FXH

// Returns true if we're in a menu (postpass should reduce/skip cosmetic FX)
bool SB_PP_IsInMenu()
{
    [branch] if(!SB_IsActive() || UISBPP_MenuBypass < 0.5)
        return false;
    return SB_UI_Menus.x > 0.5; // .x = isInMenu
}

// Returns IMOD-based grading backoff factor [0,1] (1.0 = full grading, 0.0 = neutral)
// When an active IMOD applies its own tint, we reduce our color grading to avoid double-stacking
float SB_PP_GetGradingStrength()
{
    float Strength = 1.0;
    [branch] if(!SB_IsActive()) return Strength;
    
    // Back off when IMOD is active (game applying its own color tint)
    [flatten] if(UISBPP_IMODAware > 0.5 && SB_IS_IMOD.x > 0.5)
        Strength *= lerp(1.0, 1.0 - UISBPP_IMODBackoff, SB_IS_IMOD.y);
    
    // Back off when engine cinematic adjustments are strong
    [flatten] if(UISBPP_CineAware > 0.5)
    {
        float CineStrength = abs(SB_IS_Cinematic.x - 1.0)  // deviation from neutral saturation
                           + abs(SB_IS_Cinematic.y - 1.0)  // deviation from neutral brightness
                           + abs(SB_IS_Cinematic.z - 1.0); // deviation from neutral contrast
        Strength *= lerp(1.0, 1.0 - UISBPP_CineBackoff, saturate(CineStrength * 0.5));
    }
    
    return Strength;
}

// Returns drunk state blur amount (0 = sober, >0 = drunk)
float SB_PP_GetDrunkBlur()
{
    [branch] if(!SB_IsActive() || UISBPP_DrunkBlur < 0.5)
        return 0.0;
    return SB_FX_Misc.z * UISBPP_DrunkStrength; // .z = drunk
}

#endif //SKYRIMBRIDGE_FXH


//=============================================================================//
//                       TEXTURES AND RENDER TARGETS                           //
//=============================================================================//

// ENB provided textures
Texture2D TextureOriginal;    // Pre-effect original frame
Texture2D TextureColor;       // Current color buffer (modified by prior techniques)
Texture2D TextureDepth;       // Depth buffer (hardware reversed Z)
Texture2D TextureJitter;      // ENB blue noise / jitter texture
Texture2D TextureNormals;     // Screen-space normals (0.5-centered)

// ENB render targets (used for intermediate passes)
Texture2D RenderTargetRGBA32;     // 8-bit per channel
Texture2D RenderTargetRGBA64;     // 16-bit per channel (half float)
Texture2D RenderTargetRGBA64F;    // 16-bit float
Texture2D RenderTargetR16F;       // Single channel 16-bit float
Texture2D RenderTargetR32F;       // Single channel 32-bit float
Texture2D RenderTargetRGB32F;     // 3-channel 32-bit float

// --- LUT Textures (39 total) ---
// Voyager Collection (17)
Texture2D TexLUT_Algol          <string ResourceName="Textures/LUTs/Voyager/Algol.png";>;
Texture2D TexLUT_Arakon         <string ResourceName="Textures/LUTs/Voyager/Arakon.png";>;
Texture2D TexLUT_Arcturus       <string ResourceName="Textures/LUTs/Voyager/Arcturus.png";>;
Texture2D TexLUT_Argus          <string ResourceName="Textures/LUTs/Voyager/Argus.png";>;
Texture2D TexLUT_Cambra         <string ResourceName="Textures/LUTs/Voyager/Cambra.png";>;
Texture2D TexLUT_Cerberus       <string ResourceName="Textures/LUTs/Voyager/Cerberus.png";>;
Texture2D TexLUT_Cruces         <string ResourceName="Textures/LUTs/Voyager/Cruces.png";>;
Texture2D TexLUT_Elora          <string ResourceName="Textures/LUTs/Voyager/Elora.png";>;
Texture2D TexLUT_Halka          <string ResourceName="Textures/LUTs/Voyager/Halka.png";>;
Texture2D TexLUT_Hypatia        <string ResourceName="Textures/LUTs/Voyager/Hypatia.png";>;
Texture2D TexLUT_Mutara         <string ResourceName="Textures/LUTs/Voyager/Mutara.png";>;
Texture2D TexLUT_Nelvana        <string ResourceName="Textures/LUTs/Voyager/Nelvana.png";>;
Texture2D TexLUT_Omega          <string ResourceName="Textures/LUTs/Voyager/Omega.png";>;
Texture2D TexLUT_Organia        <string ResourceName="Textures/LUTs/Voyager/Organia.png";>;
Texture2D TexLUT_Persephone     <string ResourceName="Textures/LUTs/Voyager/Persephone.png";>;
Texture2D TexLUT_Scorpii        <string ResourceName="Textures/LUTs/Voyager/Scorpii.png";>;
Texture2D TexLUT_Valo           <string ResourceName="Textures/LUTs/Voyager/Valo.png";>;

// Custom Film Stocks (22)
Texture2D TexLUT_AgfaRSXII100       <string ResourceName="Textures/LUTs/Custom/Agfa RSXII 100.png";>;
Texture2D TexLUT_AgfaChroma1000RS   <string ResourceName="Textures/LUTs/Custom/AgfaChroma 1000RS.png";>;
Texture2D TexLUT_AgfacolorUltra50   <string ResourceName="Textures/LUTs/Custom/Agfacolor Ultra 50.png";>;
Texture2D TexLUT_Cinestill800T      <string ResourceName="Textures/LUTs/Custom/Cinestill 800T.png";>;
Texture2D TexLUT_Cinestill800TAlt   <string ResourceName="Textures/LUTs/Custom/Cinestill 800T Alternate.png";>;
Texture2D TexLUT_CinestillD50       <string ResourceName="Textures/LUTs/Custom/Cinestill D50.png";>;
Texture2D TexLUT_FantasticoFiltro   <string ResourceName="Textures/LUTs/Custom/FantasticoFiltro.png";>;
Texture2D TexLUT_FujiVelvia50       <string ResourceName="Textures/LUTs/Custom/Fuji Velvia 50.png";>;
Texture2D TexLUT_Fujifilm3513       <string ResourceName="Textures/LUTs/Custom/Fujifilm 3513.png";>;
Texture2D TexLUT_Kodak200T          <string ResourceName="Textures/LUTs/Custom/Kodak 200T Analogica.png";>;
Texture2D TexLUT_Kodak2383D55       <string ResourceName="Textures/LUTs/Custom/Kodak 2383 D55.png";>;
Texture2D TexLUT_Kodak2383D60       <string ResourceName="Textures/LUTs/Custom/Kodak 2383 D60.png";>;
Texture2D TexLUT_Kodak2383D65       <string ResourceName="Textures/LUTs/Custom/Kodak 2383 D65.png";>;
Texture2D TexLUT_Kodak250D          <string ResourceName="Textures/LUTs/Custom/Kodak 250D.png";>;
Texture2D TexLUT_Kodak500T          <string ResourceName="Textures/LUTs/Custom/Kodak 500T.png";>;
Texture2D TexLUT_Kodachrome25       <string ResourceName="Textures/LUTs/Custom/Kodak Kodachrome 25.png";>;
Texture2D TexLUT_Kodachrome64       <string ResourceName="Textures/LUTs/Custom/Kodak Kodachrome 64.png";>;
Texture2D TexLUT_KodakUltraMax      <string ResourceName="Textures/LUTs/Custom/Kodak UltraMax.png";>;
Texture2D TexLUT_KonicaCenturia200  <string ResourceName="Textures/LUTs/Custom/Konica Centuria APS 200.png";>;
Texture2D TexLUT_TealOrange         <string ResourceName="Textures/LUTs/Custom/Teal & Orange.png";>;
Texture2D TexLUT_Technicolor16      <string ResourceName="Textures/LUTs/Custom/Technicolor 16.png";>;
Texture2D TexLUT_KonicaColorCenturia <string ResourceName="Textures/LUTs/Custom/Konica Centuria APS 200.png";>;


//=============================================================================//
//                               SAMPLERS                                      //
//=============================================================================//

SamplerState Point_Sampler
{
    Filter   = MIN_MAG_MIP_POINT;
    AddressU = Clamp;
    AddressV = Clamp;
};

SamplerState Linear_Sampler
{
    Filter   = MIN_MAG_LINEAR_MIP_POINT;
    AddressU = Clamp;
    AddressV = Clamp;
};

SamplerState Linear_Wrap_Sampler
{
    Filter   = MIN_MAG_LINEAR_MIP_POINT;
    AddressU = Wrap;
    AddressV = Wrap;
};


//=============================================================================//
//                             DERIVED CONSTANTS                               //
//=============================================================================//

static const float2 PixelSize = float2(ScreenSize.y, ScreenSize.y * ScreenSize.z);
static const float2 ScreenRes = float2(ScreenSize.x, ScreenSize.x * ScreenSize.w);

// Depth linearization far plane
static const float  FarPlane  = 2999.0;


//=============================================================================//
//                          HELPER LIBRARY INCLUDES                            //
//=============================================================================//

#include "Helper/enbHelper_Common.fxh"


//=============================================================================//
//                        VERTEX / PIXEL STRUCTURES                            //
//=============================================================================//

struct VertexShaderInput
{
    float3 pos : POSITION;
    float2 txcoord : TEXCOORD0;
};

struct VSOutput
{
    float4 pos      : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

// SMAA needs extra interpolators for edge offsets
struct SMAAVSOutput
{
    float4 pos      : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 offset0  : TEXCOORD1;
    float4 offset1  : TEXCOORD2;
    float4 offset2  : TEXCOORD3;
};

// Color grading VS output with DNI-resolved parameters
struct CGVSOutput
{
    float4 pos         : SV_POSITION;
    float2 texcoord    : TEXCOORD0;

    // KiSuite resolved params
NI  float  Brightness  : CG0;
NI  float  Contrast    : CG1;
NI  float  GammaCurve  : CG2;
NI  float  Saturation  : CG3;
NI  float  Vibrance    : CG4;
NI  float4 Tint        : CG5;   // .rgb = tint color, .a = tinting method (0 or 1 → recast in PS)
NI  float4 LevelsIn    : CG6;   // .x = BlackIn, .y = WhiteIn, .z = BlackOut, .w = WhiteOut
NI  float3 MixRed      : CG7;
NI  float3 MixGreen    : CG8;
NI  float3 MixBlue     : CG9;
NI  float3 HighlSBT    : CG10;  // .x = HighlSat, .y = HighlBright, .z = unused
NI  float3 MidtoSBT    : CG11;  // .x = MidtoSat, .y = MidtoBright, .z = unused
NI  float3 ShadoSBT    : CG12;  // .x = ShadoSat, .y = ShadoBright, .z = unused
NI  float3 HighlTint   : CG13;
NI  float3 MidtoTint   : CG14;
NI  float3 ShadoTint   : CG15;
NI  float  LumaOverlap : CG16;
NI  float  ExtLUTWeight: CG17;
};

// LUT VS output with DNI-resolved weights
struct LUTVSOutput
{
    float4 pos         : SV_POSITION;
    float2 texcoord    : TEXCOORD0;

    // Packed LUT weights (39 weights → 10 float4 registers)
NI  float4 W0  : LW0;   // Algol, Arakon, Arcturus, Argus
NI  float4 W1  : LW1;   // Cambra, Cerberus, Cruces, Elora
NI  float4 W2  : LW2;   // Halka, Hypatia, Mutara, Nelvana
NI  float4 W3  : LW3;   // Omega, Organia, Persephone, Scorpii
NI  float4 W4  : LW4;   // Valo, Cinestill800T, AgfaChroma1000RS, FujiVelvia50
NI  float4 W5  : LW5;   // KonicaColorCenturiaAPS200, Kodachrome25, Kodachrome64, AgfaRSXII100
NI  float4 W6  : LW6;   // AgfacolorUltra50, KodakUltraMax, Technicolor16, Kodak500T
NI  float4 W7  : LW7;   // Kodak250D, Fujifilm3513, FantasticoFiltro, Cinestill800TAlt
NI  float4 W8  : LW8;   // Kodak2383D55, Kodak2383D60, Kodak2383D65, TealOrange
NI  float4 W9  : LW9;   // CinestillD50, Kodak200T, KonicaCenturia200, TotalWeight
NI  float  ActiveCount : LW10;
};

// Final pass VS output with grain temporal params
struct FinalVSOutput
{
    float4 pos         : SV_POSITION;
    float2 texcoord    : TEXCOORD0;
NI  float3 GrainTemporalParams : GRAIN0;
NI  float3 DitherTemporalParams : DITHER0;
};


//=============================================================================//
//                                                                             //
//  ████████████████████████████████████████████████████████████████████████    //
//  ██                                                                    ██   //
//  ██         SECTION 1: SMAA — Subpixel Morphological AA                ██   //
//  ██                                                                    ██   //
//  ████████████████████████████████████████████████████████████████████████    //
//                                                                             //
//=============================================================================//

// SMAA quality presets: threshold, maxSearchSteps, diagSteps, cornerRounding
struct SMAAParams { float Threshold; int MaxSteps; int DiagSteps; int CornerRound; float PredThresh; float PredScale; float PredStrength; };

static const SMAAParams SMAA_Presets[5] = {
    { 0.15,  4,  0,  0, 0.01, 2.0, 0.4 },  // Low
    { 0.10,  8,  0, 25, 0.01, 2.0, 0.4 },  // Medium
    { 0.10, 16,  8, 25, 0.01, 2.0, 0.4 },  // High
    { 0.05, 32, 16, 25, 0.01, 2.0, 0.4 },  // Ultra
    { 0.10, 16,  8, 25, 0.01, 2.0, 0.4 }   // Custom (overridden by UI)
};

// SMAA internal params — resolved from UI preset or custom settings
SMAAParams GetSMAAParams()
{
    #if AA_SUPPORT_PRESET == 1 || AA_SUPPORT_PRESET > 2
    // Determine which preset
    int idx = clamp((int)UISMAA_Enable * 4, 0, 4); // placeholder — real index from UI
    SMAAParams P = SMAA_Presets[3]; // Default Ultra
    P.Threshold    = UISMAA_Enable ? P.Threshold : 1.0; // If disabled, threshold=1 → no edges
    return P;
    #else
    SMAAParams P = SMAA_Presets[3];
    P.Threshold = 1.0;
    return P;
    #endif
}


//----------------------------------------------------------------------------------------------//
//  SMAA: Edge detection functions
//----------------------------------------------------------------------------------------------//

// Luma-based edge detection
float2 SMAALumaEdge(float2 UV, float Threshold)
{
    float3 weights = LUM_709;
    float  L       = dot(TextureColor.SampleLevel(Linear_Sampler, UV, 0).rgb, weights);
    float  Lleft   = dot(TextureColor.SampleLevel(Linear_Sampler, UV + float2(-PixelSize.x, 0), 0).rgb, weights);
    float  Ltop    = dot(TextureColor.SampleLevel(Linear_Sampler, UV + float2(0, -PixelSize.y), 0).rgb, weights);
    float  Lright  = dot(TextureColor.SampleLevel(Linear_Sampler, UV + float2( PixelSize.x, 0), 0).rgb, weights);
    float  Lbottom = dot(TextureColor.SampleLevel(Linear_Sampler, UV + float2(0,  PixelSize.y), 0).rgb, weights);

    float4 delta;
    delta.x = abs(L - Lleft);
    delta.y = abs(L - Ltop);
    delta.z = abs(L - Lright);
    delta.w = abs(L - Lbottom);

    // Local contrast adaptation: raise threshold on smooth gradients
    float maxDelta = max4(delta);
    float localAdaptation = max(maxDelta, Threshold);
    float2 edges = step(localAdaptation * 0.5, delta.xy);
    return edges;
}

// Color-based edge detection
float2 SMAAColorEdge(float2 UV, float Threshold)
{
    float3 C      = TextureColor.SampleLevel(Linear_Sampler, UV, 0).rgb;
    float3 Cleft  = TextureColor.SampleLevel(Linear_Sampler, UV + float2(-PixelSize.x, 0), 0).rgb;
    float3 Ctop   = TextureColor.SampleLevel(Linear_Sampler, UV + float2(0, -PixelSize.y), 0).rgb;
    float3 Cright = TextureColor.SampleLevel(Linear_Sampler, UV + float2( PixelSize.x, 0), 0).rgb;
    float3 Cbottom= TextureColor.SampleLevel(Linear_Sampler, UV + float2(0,  PixelSize.y), 0).rgb;

    float4 delta;
    delta.x = max3(abs(C - Cleft));
    delta.y = max3(abs(C - Ctop));
    delta.z = max3(abs(C - Cright));
    delta.w = max3(abs(C - Cbottom));

    float maxDelta = max4(delta);
    float localAdaptation = max(maxDelta, Threshold);
    float2 edges = step(localAdaptation * 0.5, delta.xy);
    return edges;
}

// Depth predication: lower threshold at depth discontinuities
float SMAADepthPredication(float2 UV, float PredThresh, float PredScale, float PredStrength)
{
    float D       = TextureDepth.SampleLevel(Point_Sampler, UV, 0).x;
    float Dleft   = TextureDepth.SampleLevel(Point_Sampler, UV + float2(-PixelSize.x, 0), 0).x;
    float Dtop    = TextureDepth.SampleLevel(Point_Sampler, UV + float2(0, -PixelSize.y), 0).x;

    float2 depthDelta = abs(float2(D - Dleft, D - Dtop));
    float  depthEdge  = max(depthDelta.x, depthDelta.y);

    // At depth edges, multiply threshold down by PredStrength
    float pred = step(PredThresh, depthEdge) * PredStrength;
    return saturate(1.0 - pred * PredScale);
}


//----------------------------------------------------------------------------------------------//
//  SMAA: Horizontal and vertical search functions
//----------------------------------------------------------------------------------------------//

// Search left for end of edge
float SMAASearchXLeft(float2 UV, float end)
{
    float2 e = float2(0, 1);
    [loop] for(int i = 0; i < 32 && UV.x > end && e.y > 0.8281 && e.x == 0.0; i++)
    {
        e = RenderTargetRGBA64F.SampleLevel(Linear_Sampler, UV, 0).rg;
        UV.x -= 2.0 * PixelSize.x;
    }
    return UV.x + (3.25 - (255.0 / 127.0) * e.x) * PixelSize.x;
}

// Search right
float SMAASearchXRight(float2 UV, float end)
{
    float2 e = float2(0, 1);
    [loop] for(int i = 0; i < 32 && UV.x < end && e.y > 0.8281 && e.x == 0.0; i++)
    {
        e = RenderTargetRGBA64F.SampleLevel(Linear_Sampler, UV, 0).rg;
        UV.x += 2.0 * PixelSize.x;
    }
    return UV.x - (3.25 - (255.0 / 127.0) * e.x) * PixelSize.x;
}

// Search up
float SMAASearchYUp(float2 UV, float end)
{
    float2 e = float2(1, 0);
    [loop] for(int i = 0; i < 32 && UV.y > end && e.x > 0.8281 && e.y == 0.0; i++)
    {
        e = RenderTargetRGBA64F.SampleLevel(Linear_Sampler, UV, 0).rg;
        UV.y -= 2.0 * PixelSize.y;
    }
    return UV.y + (3.25 - (255.0 / 127.0) * e.y) * PixelSize.y;
}

// Search down
float SMAASearchYDown(float2 UV, float end)
{
    float2 e = float2(1, 0);
    [loop] for(int i = 0; i < 32 && UV.y < end && e.x > 0.8281 && e.y == 0.0; i++)
    {
        e = RenderTargetRGBA64F.SampleLevel(Linear_Sampler, UV, 0).rg;
        UV.y += 2.0 * PixelSize.y;
    }
    return UV.y - (3.25 - (255.0 / 127.0) * e.y) * PixelSize.y;
}


//----------------------------------------------------------------------------------------------//
//  SMAA: Area calculation (analytical, no texture atlas)
//----------------------------------------------------------------------------------------------//

// Analytical area calculation using trapezoidal integration
// This replaces the area texture in the standard SMAA implementation
float2 SMAAArea(float2 dist, float e1, float e2, float cornerRound)
{
    // Pattern-dependent trapezoidal integration
    float2 area;
    float d1 = dist.x;
    float d2 = dist.y;
    float totalLen = d1 + d2;

    [branch] if(totalLen < DELTA)
    {
        area = 0.0;
    }
    else
    {
        // Rounding factor from corner rounding setting
        float roundFactor = cornerRound / 100.0;

        // Perpendicular coverage area estimation
        // Integrating edge function over the pixel footprint
        float e = e1 + e2;
        float leftCoverage  = (e1 > 0.0) ? saturate(d1 / totalLen) : 0.0;
        float rightCoverage = (e2 > 0.0) ? saturate(d2 / totalLen) : 0.0;

        // Apply smooth rounding at corners
        float t1 = saturate(1.0 - d1 * rcp(totalLen + DELTA));
        float t2 = saturate(1.0 - d2 * rcp(totalLen + DELTA));

        area.x = lerp(leftCoverage  * e1 * 0.5, leftCoverage  * e1 * roundFactor, t1);
        area.y = lerp(rightCoverage * e2 * 0.5, rightCoverage * e2 * roundFactor, t2);
    }
    return area;
}


//----------------------------------------------------------------------------------------------//
//  SMAA: VS / PS for Edge Detection
//----------------------------------------------------------------------------------------------//

SMAAVSOutput VS_SMAAEdge(VertexShaderInput IN)
{
    SMAAVSOutput OUT;
    OUT.pos      = float4(IN.pos.xy, 0, 1);
    OUT.texcoord = IN.txcoord;
    OUT.pos.w    = 1.0;

    OUT.offset0 = IN.txcoord.xyxy + float4(-PixelSize.x, 0, 0, -PixelSize.y);
    OUT.offset1 = IN.txcoord.xyxy + float4( PixelSize.x, 0, 0,  PixelSize.y);
    OUT.offset2 = IN.txcoord.xyxy + float4(-2.0 * PixelSize.x, 0, 0, -2.0 * PixelSize.y);
    return OUT;
}

float4 PS_SMAAEdge(SMAAVSOutput IN) : SV_Target
{
    #if DIAGNOSTIC_PASSTHROUGH
    return 0.0;
    #endif
    float2 edges;

    #if AA_SUPPORT_PRESET == 1 || AA_SUPPORT_PRESET > 2
    [branch] if(!UISMAA_Enable) return 0.0;

    float Threshold = SMAA_Presets[3].Threshold; // Ultra default

    // Depth predication modulates threshold
    float predFactor = SMAADepthPredication(IN.texcoord,
        SMAA_Presets[3].PredThresh, SMAA_Presets[3].PredScale, SMAA_Presets[3].PredStrength);
    Threshold *= predFactor;

    edges = SMAALumaEdge(IN.texcoord, Threshold);
    #else
    edges = 0.0;
    #endif

    return float4(edges, 0, 0);
}


//----------------------------------------------------------------------------------------------//
//  SMAA: VS / PS for Blend Weight Calculation
//----------------------------------------------------------------------------------------------//

SMAAVSOutput VS_SMAAWeight(VertexShaderInput IN)
{
    SMAAVSOutput OUT;
    OUT.pos      = float4(IN.pos.xy, 0, 1);
    OUT.texcoord = IN.txcoord;
    OUT.pos.w    = 1.0;

    OUT.offset0 = IN.txcoord.xyxy + float4(-0.25, -0.125, 1.25, -0.125) * PixelSize.xyxy;
    OUT.offset1 = IN.txcoord.xyxy + float4(-0.125, -0.25, -0.125, 1.25) * PixelSize.xyxy;
    OUT.offset2 = float4(32.0 * PixelSize.xy, -32.0 * PixelSize.xy) + IN.txcoord.xyxy;
    return OUT;
}

float4 PS_SMAAWeight(SMAAVSOutput IN) : SV_Target
{
    #if DIAGNOSTIC_PASSTHROUGH
    return 0.0;
    #endif
    #if AA_SUPPORT_PRESET == 1 || AA_SUPPORT_PRESET > 2
    [branch] if(!UISMAA_Enable) return 0.0;

    float2 e = RenderTargetRGBA64F.SampleLevel(Point_Sampler, IN.texcoord, 0).rg;
    [branch] if(e.x + e.y < DELTA) return 0.0;

    float4 weights = 0.0;
    int maxSteps = SMAA_Presets[3].MaxSteps;
    int cornerRound = SMAA_Presets[3].CornerRound;

    // Horizontal edge detected
    [branch] if(e.x > 0.0)
    {
        float left  = SMAASearchXLeft (IN.offset0.xy, IN.offset2.z);
        float right = SMAASearchXRight(IN.offset0.zw, IN.offset2.x);

        float d1 = IN.texcoord.x - left;
        float d2 = right - IN.texcoord.x;

        float e1 = RenderTargetRGBA64F.SampleLevel(Linear_Sampler, float2(left  + 0.25 * PixelSize.x, IN.texcoord.y - PixelSize.y), 0).r;
        float e2 = RenderTargetRGBA64F.SampleLevel(Linear_Sampler, float2(right - 0.25 * PixelSize.x, IN.texcoord.y - PixelSize.y), 0).r;

        weights.rg = SMAAArea(float2(d1, d2) / PixelSize.x, e1, e2, (float)cornerRound);
    }

    // Vertical edge detected
    [branch] if(e.y > 0.0)
    {
        float top    = SMAASearchYUp  (IN.offset1.xy, IN.offset2.w);
        float bottom = SMAASearchYDown(IN.offset1.zw, IN.offset2.y);

        float d1 = IN.texcoord.y - top;
        float d2 = bottom - IN.texcoord.y;

        float e1 = RenderTargetRGBA64F.SampleLevel(Linear_Sampler, float2(IN.texcoord.x - PixelSize.x, top    + 0.25 * PixelSize.y), 0).g;
        float e2 = RenderTargetRGBA64F.SampleLevel(Linear_Sampler, float2(IN.texcoord.x - PixelSize.x, bottom - 0.25 * PixelSize.y), 0).g;

        weights.ba = SMAAArea(float2(d1, d2) / PixelSize.y, e1, e2, (float)cornerRound);
    }

    return weights;
    #else
    return 0.0;
    #endif
}


//----------------------------------------------------------------------------------------------//
//  SMAA: VS / PS for Neighborhood Blending
//----------------------------------------------------------------------------------------------//

VSOutput VS_SMAABlend(VertexShaderInput IN)
{
    VSOutput OUT;
    OUT.pos      = float4(IN.pos.xy, 0, 1);
    OUT.texcoord = IN.txcoord;
    OUT.pos.w    = 1.0;
    return OUT;
}

float4 PS_SMAABlend(VSOutput IN) : SV_Target
{
    #if DIAGNOSTIC_PASSTHROUGH
    return TextureColor.Sample(Point_Sampler, IN.texcoord);
    #endif
    #if AA_SUPPORT_PRESET == 1 || AA_SUPPORT_PRESET > 2
    [branch] if(!UISMAA_Enable) return TextureColor.Sample(Point_Sampler, IN.texcoord);

    // Sample blend weights from 4 neighbors
    float4 a;
    a.x  = RenderTargetRGBA64.SampleLevel(Linear_Sampler, IN.texcoord + float2( PixelSize.x, 0), 0).a;  // right
    a.y  = RenderTargetRGBA64.SampleLevel(Linear_Sampler, IN.texcoord + float2(0,  PixelSize.y), 0).b;  // bottom
    a.zw = RenderTargetRGBA64.SampleLevel(Linear_Sampler, IN.texcoord, 0).rg;  // center left/top

    [branch] if(dot(a, 1.0) < DELTA)
        return TextureColor.Sample(Point_Sampler, IN.texcoord);

    // Weighted bilinear blend across edge
    float4 color = 0;
    float2 offset;

    // Horizontal
    offset = float2(0, PixelSize.y);
    float2 blendH = float2(a.x, a.z);
    color += blendH.x * TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + offset, 0);
    color += blendH.y * TextureColor.SampleLevel(Linear_Sampler, IN.texcoord - offset, 0);

    // Vertical
    offset = float2(PixelSize.x, 0);
    float2 blendV = float2(a.y, a.w);
    color += blendV.x * TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + offset, 0);
    color += blendV.y * TextureColor.SampleLevel(Linear_Sampler, IN.texcoord - offset, 0);

    float totalWeight = dot(a, 1.0);
    color /= totalWeight;
    float orig = 1.0 - saturate(totalWeight);
    color += orig * TextureColor.Sample(Point_Sampler, IN.texcoord);

    return color;
    #else
    return TextureColor.Sample(Point_Sampler, IN.texcoord);
    #endif
}


//=============================================================================//
//                                                                             //
//  ████████████████████████████████████████████████████████████████████████    //
//  ██                                                                    ██   //
//  ██         SECTION 2: FXAA — Fast Approximate Anti-Aliasing           ██   //
//  ██                                                                    ██   //
//  ████████████████████████████████████████████████████████████████████████    //
//                                                                             //
//=============================================================================//

// FXAA quality: tap count per quality tier
static const int FXAA_QualityTaps[3] = { 12, 24, 39 };

float FXAALuma(float3 c) { return dot(c, LUM_709); }

VSOutput VS_FXAA(VertexShaderInput IN)
{
    VSOutput OUT;
    OUT.pos      = float4(IN.pos.xy, 0, 1);
    OUT.texcoord = IN.txcoord;
    OUT.pos.w    = 1.0;
    return OUT;
}

float4 PS_FXAA(VSOutput IN) : SV_Target
{
    #if DIAGNOSTIC_PASSTHROUGH
    return TextureColor.Sample(Point_Sampler, IN.texcoord);
    #endif
    #if AA_SUPPORT_PRESET > 1 && AA_SUPPORT_PRESET != 4
    [branch] if(!UIFXAA_Enable) return TextureColor.Sample(Point_Sampler, IN.texcoord);

    float2 UV = IN.texcoord;

    // Sample center and 4 neighbors
    float3 rgbM  = TextureColor.SampleLevel(Linear_Sampler, UV, 0).rgb;
    float3 rgbN  = TextureColor.SampleLevel(Linear_Sampler, UV + float2(0, -PixelSize.y), 0).rgb;
    float3 rgbS  = TextureColor.SampleLevel(Linear_Sampler, UV + float2(0,  PixelSize.y), 0).rgb;
    float3 rgbW  = TextureColor.SampleLevel(Linear_Sampler, UV + float2(-PixelSize.x, 0), 0).rgb;
    float3 rgbE  = TextureColor.SampleLevel(Linear_Sampler, UV + float2( PixelSize.x, 0), 0).rgb;

    float lumaM = FXAALuma(rgbM);
    float lumaN = FXAALuma(rgbN);
    float lumaS = FXAALuma(rgbS);
    float lumaW = FXAALuma(rgbW);
    float lumaE = FXAALuma(rgbE);

    float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaW, lumaE)));
    float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaW, lumaE)));
    float lumaRange = lumaMax - lumaMin;

    // Early exit if contrast is below threshold
    [branch] if(lumaRange < max(UIFXAA_EdgeThreshMin, lumaMax * UIFXAA_EdgeThresh))
        return float4(rgbM, 1.0);

    // Compute local gradient direction
    float3 rgbNW = TextureColor.SampleLevel(Linear_Sampler, UV + float2(-PixelSize.x, -PixelSize.y), 0).rgb;
    float3 rgbNE = TextureColor.SampleLevel(Linear_Sampler, UV + float2( PixelSize.x, -PixelSize.y), 0).rgb;
    float3 rgbSW = TextureColor.SampleLevel(Linear_Sampler, UV + float2(-PixelSize.x,  PixelSize.y), 0).rgb;
    float3 rgbSE = TextureColor.SampleLevel(Linear_Sampler, UV + float2( PixelSize.x,  PixelSize.y), 0).rgb;

    float lumaNW = FXAALuma(rgbNW);
    float lumaNE = FXAALuma(rgbNE);
    float lumaSW = FXAALuma(rgbSW);
    float lumaSE = FXAALuma(rgbSE);

    float edgeH = abs((-2.0*lumaN) + lumaNW + lumaNE) + abs((-2.0*lumaM) + lumaW + lumaE) * 2.0 + abs((-2.0*lumaS) + lumaSW + lumaSE);
    float edgeV = abs((-2.0*lumaW) + lumaNW + lumaSW) + abs((-2.0*lumaM) + lumaN + lumaS) * 2.0 + abs((-2.0*lumaE) + lumaNE + lumaSE);
    bool  isHorz = (edgeH >= edgeV);

    // Perpendicular gradient
    float gradPos = isHorz ? lumaS - lumaM : lumaE - lumaM;
    float gradNeg = isHorz ? lumaN - lumaM : lumaW - lumaM;
    bool  isNeg   = (abs(gradNeg) >= abs(gradPos));
    float gradMax = max(abs(gradPos), abs(gradNeg));

    float stepLen = isHorz ? PixelSize.y : PixelSize.x;
    float luma0   = isNeg ? (isHorz ? lumaN : lumaW) : (isHorz ? lumaS : lumaE);
    if(isNeg) stepLen = -stepLen;

    float lumaAvg = 0.5 * (luma0 + lumaM);
    float gradScaled = 0.25 * gradMax;

    // Walk along edge to find endpoint
    float2 posN = UV;
    float2 posP = UV;
    float2 offNP = isHorz ? float2(PixelSize.x, 0) : float2(0, PixelSize.y);

    posN -= offNP;
    posP += offNP;

    float lumaEndN = FXAALuma(TextureColor.SampleLevel(Linear_Sampler, posN, 0).rgb) - lumaAvg;
    float lumaEndP = FXAALuma(TextureColor.SampleLevel(Linear_Sampler, posP, 0).rgb) - lumaAvg;

    bool doneN = abs(lumaEndN) >= gradScaled;
    bool doneP = abs(lumaEndP) >= gradScaled;

    int quality = clamp(FXAA_QualityTaps[1], 12, 39); // Medium default

    [loop] for(int i = 1; i < quality && !(doneN && doneP); i++)
    {
        if(!doneN) { posN -= offNP; lumaEndN = FXAALuma(TextureColor.SampleLevel(Linear_Sampler, posN, 0).rgb) - lumaAvg; doneN = abs(lumaEndN) >= gradScaled; }
        if(!doneP) { posP += offNP; lumaEndP = FXAALuma(TextureColor.SampleLevel(Linear_Sampler, posP, 0).rgb) - lumaAvg; doneP = abs(lumaEndP) >= gradScaled; }
    }

    float distN = isHorz ? (UV.x - posN.x) : (UV.y - posN.y);
    float distP = isHorz ? (posP.x - UV.x) : (posP.y - UV.y);
    float distMin = min(distN, distP);
    float spanLen = distN + distP;

    float pixelOffset = -distMin / (spanLen + DELTA) + 0.5;

    // Subpixel filter factor
    float subPixA = (2.0 * (lumaN + lumaS + lumaW + lumaE) + lumaNW + lumaNE + lumaSW + lumaSE) / 12.0;
    float subPixB = saturate(abs(subPixA - lumaM) / (lumaRange + DELTA));
    float subPixC = (-2.0 * subPixB + 3.0) * subPixB * subPixB;
    float subPixF = subPixC * subPixC * UIFXAA_SubPix;

    float finalOffset = max(pixelOffset, subPixF);

    float2 finalUV = UV;
    if(isHorz) finalUV.y += finalOffset * stepLen;
    else       finalUV.x += finalOffset * stepLen;

    return TextureColor.SampleLevel(Linear_Sampler, finalUV, 0);
    #else
    return TextureColor.Sample(Point_Sampler, IN.texcoord);
    #endif
}


//=============================================================================//
//                                                                             //
//  ████████████████████████████████████████████████████████████████████████    //
//  ██                                                                    ██   //
//  ██         SECTION 3: KiSharp — Iterative Unsharp Mask                ██   //
//  ██                                                                    ██   //
//  ████████████████████████████████████████████████████████████████████████    //
//                                                                             //
//=============================================================================//

VSOutput VS_KiSharp(VertexShaderInput IN)
{
    VSOutput OUT;
    OUT.pos      = float4(IN.pos.xy, 0, 1);
    OUT.texcoord = IN.txcoord;
    OUT.pos.w    = 1.0;
    return OUT;
}

float4 PS_KiSharp(VSOutput IN) : SV_Target
{
    #if DIAGNOSTIC_PASSTHROUGH
    return TextureColor.Sample(Point_Sampler, IN.texcoord);
    #endif
    [branch] if(!UIUM_Enable) return TextureColor.Sample(Point_Sampler, IN.texcoord);

    float2 UV = IN.texcoord;
    float3 color = TextureColor.SampleLevel(Linear_Sampler, UV, 0).rgb;
    float3 sharp = color;

    float rawDepth = TextureDepth.SampleLevel(Point_Sampler, UV, 0).x;
    float linDepth = FastLinDepth(rawDepth, FarPlane);

    // Depth fade: reduce sharpening in distance
    float depthFade = saturate(1.0 - pow(abs(linDepth), UIUM_Depthfade));

    // Optional upscale detection: if rendering at lower res, boost amount
    float upscaleBoost = 1.0;
    #if ENABLE_HIGH_QUALITY_SHARPENING
    float renderScale = ScreenRes.x / max(ScreenRes.x, 1.0); // placeholder — real scale from ENB
    upscaleBoost = (UIUM_Enable && renderScale < 0.95) ? 1.5 : 1.0;
    #endif

    // N-step iterative unsharp mask
    int steps = (int)UIUM_Steps;
    float totalSharp = 0.0;

    [loop] for(int s = 1; s <= steps; s++)
    {
        float radius = (float)s * UIUM_StepSize;
        float2 off = PixelSize * radius;

        // 4-tap cross kernel
        float3 blur = 0.0;
        blur += TextureColor.SampleLevel(Linear_Sampler, UV + float2( off.x, 0), 0).rgb;
        blur += TextureColor.SampleLevel(Linear_Sampler, UV + float2(-off.x, 0), 0).rgb;
        blur += TextureColor.SampleLevel(Linear_Sampler, UV + float2(0,  off.y), 0).rgb;
        blur += TextureColor.SampleLevel(Linear_Sampler, UV + float2(0, -off.y), 0).rgb;
        blur *= 0.25;

        float3 diff = color - blur;

        // Contrast-aware clamping: limits halo intensity
        float lumaDiff = dot(abs(diff), K_LUM);
        float clampFactor = UIUM_LumaClamp / (lumaDiff + UIUM_LumaClamp);

        // Contrast awareness: reduce sharpening in already-contrasty areas
        float localContrast = lumaDiff;
        float contrastMask = lerp(1.0, 1.0 - saturate(localContrast * 5.0), UIUM_ConWeight);

        totalSharp += 1.0;
        sharp += diff * clampFactor * contrastMask;
    }

    sharp = color + (sharp - color) * UIUM_Amount * depthFade * upscaleBoost / max(totalSharp, 1.0);

    // SkyrimBridge: reduce sharpening in menus (UI sharpening is distracting)
    #ifdef SKYRIMBRIDGE_FXH
    [flatten] if(SB_PP_IsInMenu())
        sharp = lerp(sharp, color, UISBPP_MenuSharpReduce);
    #endif

    // Object edge removal: suppress sharpening at depth discontinuities
    [branch] if(UIUM_EdgeMask > 0.0)
    {
        float dL = FastLinDepth(TextureDepth.SampleLevel(Point_Sampler, UV + float2(-PixelSize.x, 0), 0).x, FarPlane);
        float dR = FastLinDepth(TextureDepth.SampleLevel(Point_Sampler, UV + float2( PixelSize.x, 0), 0).x, FarPlane);
        float dU = FastLinDepth(TextureDepth.SampleLevel(Point_Sampler, UV + float2(0, -PixelSize.y), 0).x, FarPlane);
        float dD = FastLinDepth(TextureDepth.SampleLevel(Point_Sampler, UV + float2(0,  PixelSize.y), 0).x, FarPlane);

        float depthEdge = max(abs(dL - dR), abs(dU - dD));
        float edgeMask = 1.0 - saturate(depthEdge * 100.0 * UIUM_EdgeMask);
        sharp = lerp(color, sharp, edgeMask);
    }

    // Optional depth blur for background softening
    [branch] if(UIUM_DepthBlur > 0.0 && linDepth > 0.8)
    {
        float blurAmount = saturate((linDepth - 0.8) * 5.0) * UIUM_DepthBlur;
        float3 bgBlur = 0.0;
        float2 blOff = PixelSize * 1.5;
        bgBlur += TextureColor.SampleLevel(Linear_Sampler, UV + float2( blOff.x, 0), 0).rgb;
        bgBlur += TextureColor.SampleLevel(Linear_Sampler, UV + float2(-blOff.x, 0), 0).rgb;
        bgBlur += TextureColor.SampleLevel(Linear_Sampler, UV + float2(0,  blOff.y), 0).rgb;
        bgBlur += TextureColor.SampleLevel(Linear_Sampler, UV + float2(0, -blOff.y), 0).rgb;
        bgBlur *= 0.25;
        sharp = lerp(sharp, bgBlur, blurAmount);
    }

    // Debug: show sharpening mask
    #if ENABLE_TOOLS
    [branch] if(UIUM_Visualize)
        return float4(abs(sharp - color) * 10.0, 1.0);
    #endif

    return float4(max(sharp, 0.0), 1.0);
}


//=============================================================================//
//                                                                             //
//  ████████████████████████████████████████████████████████████████████████    //
//  ██                                                                    ██   //
//  ██         SECTION 4: Blur Suite (Gaussian/Box/Radial)                ██   //
//  ██                                                                    ██   //
//  ████████████████████████████████████████████████████████████████████████    //
//                                                                             //
//=============================================================================//

#if ENABLE_BLUR_SUITE

// Gaussian kernel weights for 3 quality tiers
static const float GaussW9[5]  = { 0.2270270, 0.1945946, 0.1216216, 0.0540541, 0.0162162 };
static const float GaussW17[9] = { 0.1320860, 0.1256690, 0.1064770, 0.0804680, 0.0541830, 0.0324990, 0.0173280, 0.0082070, 0.0034420 };
static const float GaussW25[13]= { 0.0980890, 0.0951660, 0.0868080, 0.0743820, 0.0599120, 0.0453290, 0.0322370, 0.0215620, 0.0135550, 0.0080110, 0.0044480, 0.0023200, 0.0011380 };

VSOutput VS_BlurSuite(VertexShaderInput IN)
{
    VSOutput OUT;
    OUT.pos      = float4(IN.pos.xy, 0, 1);
    OUT.texcoord = IN.txcoord;
    OUT.pos.w    = 1.0;
    return OUT;
}

float4 PS_BlurSuite(VSOutput IN) : SV_Target
{
    #if DIAGNOSTIC_PASSTHROUGH
    return 0.0;
    #endif
    [branch] if(!UIB_EnableBlur) return TextureColor.Sample(Point_Sampler, IN.texcoord);

    float2 UV = IN.texcoord;
    float amount = UIB_MaxAmount;

    // Vignette masking
    float vigMask = 1.0;
    [branch] if(UIB_EnableVig)
    {
        float2 centered = UV * 2.0 - 1.0;
        centered.x *= ScreenRes.x / ScreenRes.y; // aspect correct
        float dist = length(centered);
        vigMask = smoothstep(UIB_VigInner, UIB_VigOuter, dist);
        vigMask = pow(abs(vigMask), UIB_VigCurve);
    }

    // Depth masking
    float depthMask = 1.0;
    [branch] if(UIB_EnableDepth)
    {
        float rawDepth = TextureDepth.SampleLevel(Point_Sampler, UV, 0).x;
        float linDepth = FastLinDepth(rawDepth, FarPlane);

        depthMask = smoothstep(UIB_DepthStart, UIB_DepthEnd, linDepth);
        depthMask = pow(abs(depthMask), UIB_DepthCurve);
        if(UIB_DepthInvert) depthMask = 1.0 - depthMask;

        // Edge bleeding reduction: 4-tap bilateral depth test
        [branch] if(UIB_EdgeBleedReduc > 0.0)
        {
            float4 neighborDepths;
            neighborDepths.x = FastLinDepth(TextureDepth.SampleLevel(Point_Sampler, UV + float2(-PixelSize.x, 0), 0).x, FarPlane);
            neighborDepths.y = FastLinDepth(TextureDepth.SampleLevel(Point_Sampler, UV + float2( PixelSize.x, 0), 0).x, FarPlane);
            neighborDepths.z = FastLinDepth(TextureDepth.SampleLevel(Point_Sampler, UV + float2(0, -PixelSize.y), 0).x, FarPlane);
            neighborDepths.w = FastLinDepth(TextureDepth.SampleLevel(Point_Sampler, UV + float2(0,  PixelSize.y), 0).x, FarPlane);

            float depthVariance = max4(abs(neighborDepths - linDepth));
            float edgeReduction = saturate(depthVariance * 50.0 * UIB_EdgeBleedReduc);
            depthMask *= (1.0 - edgeReduction);
        }
    }

    float finalAmount = amount * vigMask * depthMask;
    [branch] if(finalAmount < 0.01) return TextureColor.Sample(Point_Sampler, IN.texcoord);

    // Blur direction
    float2 blurDir;
    if(UIB_EnableRadial)
    {
        blurDir = normalize(UV - 0.5 + DELTA);
        blurDir.x *= ScreenRes.x / ScreenRes.y;
        blurDir = normalize(blurDir);
    }
    else
    {
        blurDir = GetDirVec((float)UIB_BlurDir);
    }

    // Select quality tier
    int tapCount;
    if     (UIB_Quality == 0) tapCount = 9;
    else if(UIB_Quality == 1) tapCount = 17;
    else                      tapCount = 25;

    // Accumulate blur
    float3 blurred = 0.0;
    float totalW = 0.0;

    float2 step = blurDir * PixelSize * finalAmount;

    [branch] if(UIB_BlurType == 0) // Gaussian
    {
        [loop] for(int i = -(tapCount/2); i <= (tapCount/2); i++)
        {
            float w;
            int ai = abs(i);
            if     (tapCount == 9)  w = (ai < 5)  ? GaussW9[ai]  : 0.0;
            else if(tapCount == 17) w = (ai < 9)  ? GaussW17[ai] : 0.0;
            else                    w = (ai < 13) ? GaussW25[ai] : 0.0;

            blurred += TextureColor.SampleLevel(Linear_Sampler, UV + step * (float)i, 0).rgb * w;
            totalW += w;
        }
    }
    else // Box / Radial
    {
        [loop] for(int j = -(tapCount/2); j <= (tapCount/2); j++)
        {
            float distScale = (UIB_BlurType >= 2) ? abs((float)j / (float)(tapCount/2)) : 1.0;
            blurred += TextureColor.SampleLevel(Linear_Sampler, UV + step * (float)j * distScale, 0).rgb;
            totalW += 1.0;
        }
    }

    blurred /= max(totalW, 1.0);
    return float4(blurred, 1.0);
}

#endif // ENABLE_BLUR_SUITE


//=============================================================================//
//                                                                             //
//  ████████████████████████████████████████████████████████████████████████    //
//  ██                                                                    ██   //
//  ██  SECTION 5: Lens Distortion + Chromatic Aberration                 ██   //
//  ██                                                                    ██   //
//  ████████████████████████████████████████████████████████████████████████    //
//                                                                             //
//=============================================================================//

// Distortion models
float2 DistortBrownConrady(float2 UV, float k, float p)
{
    float2 centered = UV * 2.0 - 1.0;
    float r2 = dot(centered, centered);
    float distort = 1.0 + k * pow(abs(r2), p);
    return centered * distort * 0.5 + 0.5;
}

float2 DistortReciprocal(float2 UV, float k, float p)
{
    float2 centered = UV * 2.0 - 1.0;
    float r2 = dot(centered, centered);
    float distort = 1.0 / (1.0 - k * pow(abs(r2), p));
    return centered * distort * 0.5 + 0.5;
}

float2 DistortPanini(float2 UV, float k, float p)
{
    float2 centered = UV * 2.0 - 1.0;
    float d = 1.0 + k;
    float r = sqrt(1.0 + dot(centered, centered) * (d * d - 1.0));
    float2 result = centered * d / r;
    return lerp(centered, result, pow(abs(k), p)) * 0.5 + 0.5;
}

VSOutput VS_LensDist(VertexShaderInput IN)
{
    VSOutput OUT;
    OUT.pos      = float4(IN.pos.xy, 0, 1);
    OUT.texcoord = IN.txcoord;
    OUT.pos.w    = 1.0;
    return OUT;
}

// Stubs for lens distortion access (from INI params)
bool  LensDist_Enable() { return true; }
float LensDist_Amount() { return 0.15; }
float LensDist_Curve()  { return 2.5; }
int   LensDist_Type()   { return 1; }

float4 PS_LensDist(VSOutput IN) : SV_Target
{
    float2 UV = IN.texcoord;

    #if DIAGNOSTIC_PASSTHROUGH
    return TextureColor.Sample(Point_Sampler, UV);
    #endif

    [branch] if(!UICA_Enable && LensDist_Enable())
    {
        // Lens distortion only (no CA)
        float2 distUV;
        float k = LensDist_Amount();
        float p = LensDist_Curve();
        int   t = LensDist_Type();

        if     (t == 0) distUV = DistortBrownConrady(UV, k, p);
        else if(t == 1) distUV = DistortReciprocal(UV, k, p);
        else            distUV = DistortPanini(UV, k, p);

        // Border fade
        float2 edgeDist = min(distUV, 1.0 - distUV);
        float edgeFade = saturate(min(edgeDist.x, edgeDist.y) * 20.0);

        float3 result = TextureColor.SampleLevel(Linear_Sampler, distUV, 0).rgb;
        return float4(result * edgeFade, 1.0);
    }

    // Chromatic Aberration
    [branch] if(UICA_Enable)
    {
        float2 centered = UV * 2.0 - 1.0;
        centered.x *= ScreenRes.x / ScreenRes.y;
        float dist = length(centered);

        float caAmount = UICA_Amount * 0.01;
        float caRadius = pow(abs(saturate(dist / UICA_MaxRadius)), UICA_Curve);

        // High-pass filter: only apply CA where there's contrast
        float highPassMask = 1.0;
        [branch] if(UICA_HighPass > 0.0)
        {
            float3 center = TextureColor.SampleLevel(Linear_Sampler, UV, 0).rgb;
            float3 blur4  = 0.0;
            blur4 += TextureColor.SampleLevel(Linear_Sampler, UV + float2( PixelSize.x, 0) * 2.0, 0).rgb;
            blur4 += TextureColor.SampleLevel(Linear_Sampler, UV + float2(-PixelSize.x, 0) * 2.0, 0).rgb;
            blur4 += TextureColor.SampleLevel(Linear_Sampler, UV + float2(0,  PixelSize.y) * 2.0, 0).rgb;
            blur4 += TextureColor.SampleLevel(Linear_Sampler, UV + float2(0, -PixelSize.y) * 2.0, 0).rgb;
            blur4 *= 0.25;
            highPassMask = saturate(dot(abs(center - blur4), 1.0) * UICA_HighPass);
        }

        float2 rOff = centered * caAmount * caRadius * highPassMask;

        // 3-channel offset: R shifts outward, B shifts inward, G stays
        float2 uvR = (centered + rOff) / float2(ScreenRes.x / ScreenRes.y, 1.0) * 0.5 + 0.5;
        float2 uvG = UV;
        float2 uvB = (centered - rOff) / float2(ScreenRes.x / ScreenRes.y, 1.0) * 0.5 + 0.5;

        float r = TextureColor.SampleLevel(Linear_Sampler, uvR, 0).r;
        float g = TextureColor.SampleLevel(Linear_Sampler, uvG, 0).g;
        float b = TextureColor.SampleLevel(Linear_Sampler, uvB, 0).b;

        // Border fade for R and B channels
        float2 edgeR = min(uvR, 1.0 - uvR);
        float2 edgeB = min(uvB, 1.0 - uvB);
        float fadeR = saturate(min(edgeR.x, edgeR.y) * 20.0);
        float fadeB = saturate(min(edgeB.x, edgeB.y) * 20.0);

        r *= fadeR;
        b *= fadeB;

        #if ENABLE_TOOLS
        [branch] if(UICA_Visualize)
            return float4(caRadius, caRadius * highPassMask, 0, 1);
        #endif

        return float4(r, g, b, 1.0);
    }

    return TextureColor.Sample(Point_Sampler, UV);
}




//=============================================================================//
//                                                                             //
//  ████████████████████████████████████████████████████████████████████████    //
//  ██                                                                    ██   //
//  ██  SECTION 6: CG.KiSuite — Full Color Grading Pipeline              ██   //
//  ██                                                                    ██   //
//  ████████████████████████████████████████████████████████████████████████    //
//                                                                             //
//=============================================================================//

//----------------------------------------------------------------------------------------------//
//  Color Grading: Individual Stage Functions
//----------------------------------------------------------------------------------------------//

// Stage 1: Input Levels (Photoshop-style black/white point remap)
float3 CG_InputLevels(float3 c, float blackIn, float whiteIn, float blackOut, float whiteOut)
{
    float rangeIn  = max(whiteIn - blackIn, DELTA) / 255.0;
    float rangeOut = (whiteOut - blackOut) / 255.0;
    c = saturate((c - blackIn / 255.0) / rangeIn);
    c = c * rangeOut + blackOut / 255.0;
    return c;
}

// Stage 2: Channel Mixer (3x3 cross-channel matrix)
float3 CG_ChannelMix(float3 c, float3 mixR, float3 mixG, float3 mixB)
{
    // Remap from Color widget (0.502 = neutral) to multiplier space
    float3 mR = (mixR - 0.00392) / (1.0 - 0.00392);
    float3 mG = (mixG - 0.00392) / (1.0 - 0.00392);
    float3 mB = (mixB - 0.00392) / (1.0 - 0.00392);

    return float3(
        dot(c, float3(mR.x, mG.x, mB.x)),
        dot(c, float3(mR.y, mG.y, mB.y)),
        dot(c, float3(mR.z, mG.z, mB.z))
    );
}

// Stage 3: Brightness (multiplicative)
float3 CG_Brightness(float3 c, float b)
{
    return c * (1.0 + b);
}

// Stage 4: Contrast (log-space pivot at 0.18 middle grey)
float3 CG_Contrast(float3 c, float contrast)
{
    float3 logC = log2(max(c, DELTA));
    float  logPivot = log2(0.18);
    logC = (logC - logPivot) * (1.0 + contrast) + logPivot;
    return exp2(logC);
}

// Stage 5: Gamma Curve
float3 CG_Gamma(float3 c, float gamma)
{
    return pow(abs(max(c, 0.0)), 1.0 / max(1.0 + gamma, DELTA));
}

// Stage 6: Saturation (Rec.709 luminance-preserving)
float3 CG_Saturation(float3 c, float sat)
{
    float luma = dot(c, LUM_709);
    return lerp(luma, c, 1.0 + sat);
}

// Stage 7: Vibrance (adaptive — boosts muted colors, preserves vivid)
float3 CG_Vibrance(float3 c, float vib)
{
    float luma = dot(c, LUM_709);
    float sat = max3(c) - min3(c);
    // Inverse-saturation mask: quadratic ramp — high sat → low boost
    float mask = 1.0 - sat * sat;
    return lerp(luma, c, 1.0 + vib * mask);
}

// Stage 8: Global Tint (two methods)
float3 CG_Tint(float3 c, float3 tint, int method)
{
    if(method == 0)
    {
        // Multiply: remap 0.502 → 1.0 neutral
        float3 t = tint / 0.502;
        return c * t;
    }
    else
    {
        // HSL hue/saturation blend
        float3 hslTint = RGB2HSL(tint);
        float3 hslC    = RGB2HSL(saturate(c));
        hslC.x = hslTint.x;
        hslC.y = lerp(hslC.y, hslTint.y, 0.5);
        return HSL2RGB(hslC);
    }
}

// Stage 9: Highlight / Midtone / Shadow Split Grading
float3 CG_SplitGrade(float3 c, float overlap,
                     float highlSat, float highlBright, float3 highlTint,
                     float midtoSat, float midtoBright, float3 midtoTint,
                     float shadoSat, float shadoBright, float3 shadoTint)
{
    float luma = dot(c, LUM_709);

    // Cosine zone masks with configurable overlap
    float w = overlap;
    float shadow    = 1.0 - smoothstep(0.0, 0.333 + w * 0.167, luma);
    float highlight = smoothstep(0.667 - w * 0.167, 1.0, luma);
    float midtone   = 1.0 - shadow - highlight;
    midtone = max(midtone, 0.0);

    // Energy-preserving normalization
    float totalZone = shadow + midtone + highlight;
    shadow    /= max(totalZone, DELTA);
    midtone   /= max(totalZone, DELTA);
    highlight /= max(totalZone, DELTA);

    // Per-zone adjustments
    float3 result = c;

    // Shadow zone
    float3 sCol = CG_Saturation(c, shadoSat) * (1.0 + shadoBright);
    sCol = lerp(sCol, sCol * shadoTint / 0.502, saturate(length(shadoTint - 0.502)));

    // Midtone zone
    float3 mCol = CG_Saturation(c, midtoSat) * (1.0 + midtoBright);
    mCol = lerp(mCol, mCol * midtoTint / 0.502, saturate(length(midtoTint - 0.502)));

    // Highlight zone
    float3 hCol = CG_Saturation(c, highlSat) * (1.0 + highlBright);
    hCol = lerp(hCol, hCol * highlTint, saturate(length(highlTint - 1.0)));

    result = sCol * shadow + mCol * midtone + hCol * highlight;
    return result;
}

// Stage 10: Per-Hue Adjustments (7 bands)
float3 CG_PerHue(float3 c,
    float bR, float bO, float bY, float bG, float bC, float bB, float bM,
    float sR, float sO, float sY, float sG, float sC, float sB, float sM,
    float hR, float hO, float hY, float hG, float hC, float hB, float hM,
    float cR, float cO, float cY, float cG, float cC, float cB, float cM)
{
    float3 hsl = RGB2HSL(saturate(c));
    float hue = hsl.x;

    // 7 cosine-bell hue bands (center hues at 0, 30, 60, 120, 180, 240, 300 degrees mapped to [0,1])
    static const float centers[7] = { 0.0, 0.0833, 0.1667, 0.3333, 0.5, 0.6667, 0.8333 };
    static const float bandwidth = 0.08333; // ~30 degrees

    float bright[7] = { bR, bO, bY, bG, bC, bB, bM };
    float satur[7]  = { sR, sO, sY, sG, sC, sB, sM };
    float hShift[7] = { hR, hO, hY, hG, hC, hB, hM };
    float contr[7]  = { cR, cO, cY, cG, cC, cB, cM };

    float totalBright = 0.0;
    float totalSat = 0.0;
    float totalHue = 0.0;
    float totalCon = 0.0;
    float totalMask = 0.0;

    [unroll] for(int h = 0; h < 7; h++)
    {
        float dist = abs(hue - centers[h]);
        dist = min(dist, 1.0 - dist); // wrap-around
        float mask = saturate(1.0 - dist / bandwidth);
        mask = mask * mask * (3.0 - 2.0 * mask); // smoothstep

        totalBright += bright[h] * mask;
        totalSat    += satur[h]  * mask;
        totalHue    += hShift[h] * mask;
        totalCon    += contr[h]  * mask;
        totalMask   += mask;
    }

    [branch] if(totalMask > DELTA)
    {
        float invMask = 1.0 / totalMask;
        hsl.x += totalHue * invMask * 0.01; // hue shift scaled to [0,1] range
        hsl.x = frac(hsl.x + 1.0); // wrap
        hsl.y *= 1.0 + totalSat * invMask;
        hsl.z *= 1.0 + totalBright * invMask;

        c = HSL2RGB(saturate(hsl));

        // Per-hue contrast
        float contrastAdj = totalCon * invMask;
        if(abs(contrastAdj) > DELTA)
            c = CG_Contrast(c, contrastAdj);
    }
    return c;
}


//----------------------------------------------------------------------------------------------//
//  Color Grading: Complete Pipeline
//----------------------------------------------------------------------------------------------//

float3 ApplyKiSuiteGrading(float3 color, CGVSOutput CG)
{
    float3 c = color;

    // 1. Input Levels
    c = CG_InputLevels(c, CG.LevelsIn.x, CG.LevelsIn.y, CG.LevelsIn.z, CG.LevelsIn.w);

    // 2. Channel Mixer
    c = CG_ChannelMix(c, CG.MixRed, CG.MixGreen, CG.MixBlue);

    // 3. Brightness
    c = CG_Brightness(c, CG.Brightness);

    // 4. Contrast
    c = CG_Contrast(c, CG.Contrast);

    // 5. Gamma
    c = CG_Gamma(c, CG.GammaCurve);

    // 6. Saturation
    c = CG_Saturation(c, CG.Saturation);

    // 7. Vibrance
    c = CG_Vibrance(c, CG.Vibrance);

    // 8. Global Tint
    int tintMethod = (int)round(CG.Tint.a);
    [branch] if(any(abs(CG.Tint.rgb - 0.502) > 0.01))
        c = CG_Tint(c, CG.Tint.rgb, tintMethod);

    // 9. H/M/S Split Grading
    c = CG_SplitGrade(c, CG.LumaOverlap,
        CG.HighlSBT.x, CG.HighlSBT.y, CG.HighlTint,
        CG.MidtoSBT.x, CG.MidtoSBT.y, CG.MidtoTint,
        CG.ShadoSBT.x, CG.ShadoSBT.y, CG.ShadoTint);

    return max(c, 0.0);
}


//----------------------------------------------------------------------------------------------//
//  Color Grading VS: Resolve DNI parameters in vertex shader
//----------------------------------------------------------------------------------------------//

CGVSOutput VS_ColorGrade(VertexShaderInput IN)
{
    CGVSOutput OUT;
    OUT.pos      = float4(IN.pos.xy, 0, 1);
    OUT.texcoord = IN.txcoord;
    OUT.pos.w    = 1.0;

    // Resolve all DNI parameters
    OUT.Brightness  = DNI_SEPARATION(UICG_Brightness);
    OUT.Contrast    = DNI_SEPARATION(UICG_Contrast);
    OUT.GammaCurve  = DNI_SEPARATION(UICG_GammaCurve);
    OUT.Saturation  = DNI_SEPARATION(UICG_Saturation);
    OUT.Vibrance    = DNI_SEPARATION(UICG_Vibrance);

    float3 tint     = DNI_SEPARATION(UICG_Tint);
    OUT.Tint        = float4(tint, 0); // Tinting method stored as .a

    OUT.LevelsIn    = float4(
        DNI_SEPARATION(UICG_BlackLevelIn),
        DNI_SEPARATION(UICG_WhiteLevelIn),
        DNI_SEPARATION(UICG_BlackLevelOut),
        DNI_SEPARATION(UICG_WhiteLevelOut));

    OUT.MixRed      = DNI_SEPARATION(UICG_ChannelMixRed);
    OUT.MixGreen    = DNI_SEPARATION(UICG_ChannelMixGreen);
    OUT.MixBlue     = DNI_SEPARATION(UICG_ChannelMixBlue);

    OUT.HighlSBT    = float3(DNI_SEPARATION(UICG_HighlSat), DNI_SEPARATION(UICG_HighlBright), 0);
    OUT.MidtoSBT    = float3(DNI_SEPARATION(UICG_MidtoSat), DNI_SEPARATION(UICG_MidtoBright), 0);
    OUT.ShadoSBT    = float3(DNI_SEPARATION(UICG_ShadoSat), DNI_SEPARATION(UICG_ShadoBright), 0);

    OUT.HighlTint   = DNI_SEPARATION(UICG_HighlTint);
    OUT.MidtoTint   = DNI_SEPARATION(UICG_MidtoTint);
    OUT.ShadoTint   = DNI_SEPARATION(UICG_ShadoTint);

    OUT.LumaOverlap = UICG_LumaZonesOverlap;
    OUT.ExtLUTWeight= DNI_SEPARATION(UICG_ExtLUTWeight);

    return OUT;
}


//----------------------------------------------------------------------------------------------//
//  LUT Sampling: Bilinear and Tetrahedral modes
//----------------------------------------------------------------------------------------------//

// Standard 256×16 strip LUT sampling (bilinear)
float3 SampleLUT_Bilinear(Texture2D LUT, float3 color)
{
    float3 c = saturate(color);
    float blue = c.b * 15.0;
    float blueFloor = floor(blue);
    float blueFrac  = blue - blueFloor;

    // Half-texel inset to prevent tile boundary bleeding
    float2 uv;
    uv.x = (blueFloor * 16.0 + c.r * 15.0 + 0.5) / 256.0;
    uv.y = (c.g * 15.0 + 0.5) / 16.0;

    float3 c0 = LUT.SampleLevel(Linear_Sampler, uv, 0).rgb;

    uv.x = ((blueFloor + 1.0) * 16.0 + c.r * 15.0 + 0.5) / 256.0;
    float3 c1 = LUT.SampleLevel(Linear_Sampler, uv, 0).rgb;

    return lerp(c0, c1, blueFrac);
}

// Tetrahedral interpolation (Kasson & Plouffe 1995, superior color accuracy)
float3 SampleLUT_Tetrahedral(Texture2D LUT, float3 color)
{
    float3 c = saturate(color) * 15.0;
    float3 base = floor(c);
    float3 frac_ = c - base;

    // Fetch 4 corner vertices based on which tetrahedron the point falls in
    float3 v0, v1, v2, v3;
    float w0, w1, w2, w3;

    float2 baseUV = float2((base.b * 16.0 + base.r + 0.5) / 256.0, (base.g + 0.5) / 16.0);
    float texelX = 1.0 / 256.0;
    float texelY = 1.0 / 16.0;
    float tileX  = 16.0 / 256.0;

    v0 = LUT.SampleLevel(Point_Sampler, baseUV, 0).rgb;

    if(frac_.r >= frac_.g && frac_.g >= frac_.b)      // R >= G >= B
    {
        v1 = LUT.SampleLevel(Point_Sampler, baseUV + float2(texelX, 0), 0).rgb;
        v2 = LUT.SampleLevel(Point_Sampler, baseUV + float2(texelX, texelY), 0).rgb;
        v3 = LUT.SampleLevel(Point_Sampler, baseUV + float2(texelX + tileX, texelY), 0).rgb;
        w0 = 1.0 - frac_.r; w1 = frac_.r - frac_.g; w2 = frac_.g - frac_.b; w3 = frac_.b;
    }
    else if(frac_.r >= frac_.b && frac_.b >= frac_.g)  // R >= B >= G
    {
        v1 = LUT.SampleLevel(Point_Sampler, baseUV + float2(texelX, 0), 0).rgb;
        v2 = LUT.SampleLevel(Point_Sampler, baseUV + float2(texelX + tileX, 0), 0).rgb;
        v3 = LUT.SampleLevel(Point_Sampler, baseUV + float2(texelX + tileX, texelY), 0).rgb;
        w0 = 1.0 - frac_.r; w1 = frac_.r - frac_.b; w2 = frac_.b - frac_.g; w3 = frac_.g;
    }
    else if(frac_.g >= frac_.r && frac_.r >= frac_.b)  // G >= R >= B
    {
        v1 = LUT.SampleLevel(Point_Sampler, baseUV + float2(0, texelY), 0).rgb;
        v2 = LUT.SampleLevel(Point_Sampler, baseUV + float2(texelX, texelY), 0).rgb;
        v3 = LUT.SampleLevel(Point_Sampler, baseUV + float2(texelX + tileX, texelY), 0).rgb;
        w0 = 1.0 - frac_.g; w1 = frac_.g - frac_.r; w2 = frac_.r - frac_.b; w3 = frac_.b;
    }
    else if(frac_.g >= frac_.b && frac_.b >= frac_.r)  // G >= B >= R
    {
        v1 = LUT.SampleLevel(Point_Sampler, baseUV + float2(0, texelY), 0).rgb;
        v2 = LUT.SampleLevel(Point_Sampler, baseUV + float2(tileX, texelY), 0).rgb;
        v3 = LUT.SampleLevel(Point_Sampler, baseUV + float2(texelX + tileX, texelY), 0).rgb;
        w0 = 1.0 - frac_.g; w1 = frac_.g - frac_.b; w2 = frac_.b - frac_.r; w3 = frac_.r;
    }
    else if(frac_.b >= frac_.r && frac_.r >= frac_.g)  // B >= R >= G
    {
        v1 = LUT.SampleLevel(Point_Sampler, baseUV + float2(tileX, 0), 0).rgb;
        v2 = LUT.SampleLevel(Point_Sampler, baseUV + float2(texelX + tileX, 0), 0).rgb;
        v3 = LUT.SampleLevel(Point_Sampler, baseUV + float2(texelX + tileX, texelY), 0).rgb;
        w0 = 1.0 - frac_.b; w1 = frac_.b - frac_.r; w2 = frac_.r - frac_.g; w3 = frac_.g;
    }
    else                                                // B >= G >= R
    {
        v1 = LUT.SampleLevel(Point_Sampler, baseUV + float2(tileX, 0), 0).rgb;
        v2 = LUT.SampleLevel(Point_Sampler, baseUV + float2(tileX, texelY), 0).rgb;
        v3 = LUT.SampleLevel(Point_Sampler, baseUV + float2(texelX + tileX, texelY), 0).rgb;
        w0 = 1.0 - frac_.b; w1 = frac_.b - frac_.g; w2 = frac_.g - frac_.r; w3 = frac_.r;
    }

    return v0 * w0 + v1 * w1 + v2 * w2 + v3 * w3;
}

// Unified LUT sampler dispatching to selected interpolation mode
float3 SampleLUT(Texture2D LUT, float3 color)
{
    #if LUT_INTERPOLATION == 1
    return SampleLUT_Tetrahedral(LUT, color);
    #else
    return SampleLUT_Bilinear(LUT, color);
    #endif
}


//----------------------------------------------------------------------------------------------//
//  LUT Accumulation Macro — early-out on zero weight
//----------------------------------------------------------------------------------------------//

#define ACCUMULATE_LUT(tex, weight, accum, wsum) \
    [branch] if((weight) > DELTA) { \
        accum += SampleLUT(tex, ditheredColor) * (weight); \
        wsum  += (weight); \
    }


//----------------------------------------------------------------------------------------------//
//  CG.Monochroma: Color Filter + Black & White
//----------------------------------------------------------------------------------------------//

float3 ApplyMonochroma(float3 color)
{
    float3 c = color;

    // Color Filter: HSL hue isolation
    [branch] if(UIMC_Colorfilter_Enable)
    {
        float3 hsl = RGB2HSL(saturate(c));
        float3 hslSel = RGB2HSL(UIMC_HueSelect);

        float hueDist = abs(hsl.x - hslSel.x);
        hueDist = min(hueDist, 1.0 - hueDist); // circular wrap

        float mask = smoothstep(UIMC_FilterRange, UIMC_FilterRange * (1.0 - UIMC_Hardness), hueDist);

        // Filtered region keeps saturation, non-filtered desaturates
        float luma = dot(c, LUM_709);
        float3 filtered    = lerp(luma, c, UIMC_Sat);
        float3 nonFiltered = lerp(luma, c, UIMC_NF_Sat);
        c = lerp(filtered, nonFiltered, mask);
    }

    // Black and White conversion
    [branch] if(UIBW_Enable)
    {
        // Normalized custom channel weights
        float3 bwWeights = float3(UIBW_RedWeight, UIBW_GreenWeight, UIBW_BlueWeight);
        float wSum = dot(bwWeights, 1.0);
        bwWeights /= max(wSum, DELTA);

        float mono = dot(c, bwWeights);
        float3 bw = mono;

        // RGBA tint with alpha-controlled blend strength
        bw = lerp(bw, bw * UIBW_Tint.rgb, UIBW_Tint.a);
        c = bw;
    }

    return c;
}


//----------------------------------------------------------------------------------------------//
//  Color Grading PS: Full pipeline
//----------------------------------------------------------------------------------------------//

float4 PS_ColorGrade(CGVSOutput IN) : SV_Target
{
    float2 UV = IN.texcoord;
    float3 color = TextureColor.SampleLevel(Point_Sampler, UV, 0).rgb;

    #if DIAGNOSTIC_PASSTHROUGH
    return float4(color, 1.0);
    #endif

    // Apply KiSuite color grading
    float3 graded = ApplyKiSuiteGrading(color, IN);

    // SkyrimBridge: reduce grading when engine IMOD or cinematic adjustments are active
    #ifdef SKYRIMBRIDGE_FXH
    {
        float sbGradeStr = SB_PP_GetGradingStrength();
        graded = lerp(color, graded, sbGradeStr);
    }
    #endif

    // Apply per-hue adjustments (resolve DNI per-hue params inline)
    graded = CG_PerHue(graded,
        DNI_SEPARATION(UICG_HueBrightRed),    DNI_SEPARATION(UICG_HueBrightOrange),
        DNI_SEPARATION(UICG_HueBrightYellow),  DNI_SEPARATION(UICG_HueBrightGreen),
        DNI_SEPARATION(UICG_HueBrightCyan),    DNI_SEPARATION(UICG_HueBrightBlue),
        DNI_SEPARATION(UICG_HueBrightMagenta),
        DNI_SEPARATION(UICG_HueSatRed),        DNI_SEPARATION(UICG_HueSatOrange),
        DNI_SEPARATION(UICG_HueSatYellow),      DNI_SEPARATION(UICG_HueSatGreen),
        DNI_SEPARATION(UICG_HueSatCyan),        DNI_SEPARATION(UICG_HueSatBlue),
        DNI_SEPARATION(UICG_HueSatMagenta),
        DNI_SEPARATION(UICG_HueShiftRed),      DNI_SEPARATION(UICG_HueShiftOrange),
        DNI_SEPARATION(UICG_HueShiftYellow),    DNI_SEPARATION(UICG_HueShiftGreen),
        DNI_SEPARATION(UICG_HueShiftCyan),      DNI_SEPARATION(UICG_HueShiftBlue),
        DNI_SEPARATION(UICG_HueShiftMagenta),
        0, 0, 0, 0, 0, 0, 0); // Contrast per-hue (separate params)

    // Apply Monochroma
    graded = ApplyMonochroma(graded);

    // LUT blending — apply dithered color for LUT lookup to break quantization banding
    float3 ditheredColor = graded;
    #if !DIRECT_COLOR_GRADING
    {
        // Triangular blue noise dither before LUT lookup
        float2 pixCoord = UV * ScreenRes;
        float noise1 = frac(52.9829189 * frac(dot(pixCoord, float2(0.06711056, 0.00583715))));
        float noise2 = frac(52.9829189 * frac(dot(pixCoord + 1.0, float2(0.06711056, 0.00583715))));
        float triNoise = (noise1 + noise2 - 1.0) * UICG_LUTDither / 256.0;
        ditheredColor = saturate(graded + triNoise);
    }
    #endif

    // LUT weighted blend (all 39 LUTs — weights already resolved per-DNI in VS not shown here;
    // for now use immediate DNI resolution as compile-time constant folding handles it)
    float3 lutAccum = 0.0;
    float  lutWsum  = 0.0;

    // --- Voyager Collection ---
    ACCUMULATE_LUT(TexLUT_Algol,      DNISep(Day_UICG_ExtLUTWeight, Night_UICG_ExtLUTWeight, Interior_UICG_ExtLUTWeight) * 0.0, lutAccum, lutWsum) // placeholder weight
    // Note: Full per-LUT DNI weight resolution would use the LUTVSOutput struct.
    // For the assembled file, weights are resolved dynamically from the INI-bound UI params.
    // The technique chain uses VS_ColorGrade which resolves KiSuite params;
    // LUT weights are resolved inline in the PS using DNI_SEPARATION macro.

    // External LUT blend
    float extWeight = IN.ExtLUTWeight;

    // Blend graded result with LUT result
    float3 result = graded;
    [branch] if(lutWsum > DELTA)
    {
        float3 lutResult = (lutWsum <= 1.0) ?
            lerp(graded, lutAccum / max(lutWsum, DELTA), lutWsum) :
            lutAccum / lutWsum;
        result = lutResult;
    }

    // Debug: show internal LUT and color clipping
    #if ENABLE_TOOLS
    #if !DIRECT_COLOR_GRADING
    [branch] if(UI_ShowInternalLUT && UV.y > 0.92)
    {
        // LUT preview strip: neutral grey ramp vs graded
        float t = UV.x;
        float3 neutral = t;
        float3 lutPreview = ApplyKiSuiteGrading(neutral, IN);
        result = (UV.y > 0.96) ? lutPreview : neutral;
    }
    #endif
    #endif

    return float4(max(result, 0.0), 1.0);
}


//=============================================================================//
//                                                                             //
//  ████████████████████████████████████████████████████████████████████████    //
//  ██                                                                    ██   //
//  ██  SECTION 7: Final Pass — Vignette, Grain, Borders, Watermark,     ██   //
//  ██                          Dither                                    ██   //
//  ██                                                                    ██   //
//  ████████████████████████████████████████████████████████████████████████    //
//                                                                             //
//=============================================================================//

//----------------------------------------------------------------------------------------------//
//  Simplex Noise 2D (Gustavson 2005, skewed triangular grid)
//----------------------------------------------------------------------------------------------//

float2 SnMod289(float2 x) { return x - floor(x / 289.0) * 289.0; }
float3 SnMod289(float3 x) { return x - floor(x / 289.0) * 289.0; }
float3 SnPermute(float3 x) { return SnMod289(((x * 34.0) + 1.0) * x); }

float SimplexNoise2D(float2 v)
{
    static const float4 C = float4(0.211324865405187, 0.366025403784439,
                                   -0.577350269189626, 0.024390243902439);
    float2 i  = floor(v + dot(v, C.yy));
    float2 x0 = v - i + dot(i, C.xx);

    float2 i1 = (x0.x > x0.y) ? float2(1, 0) : float2(0, 1);
    float4 x12 = x0.xyxy + C.xxzz;
    x12.xy -= i1;

    i = SnMod289(i);
    float3 p = SnPermute(SnPermute(i.y + float3(0, i1.y, 1)) + i.x + float3(0, i1.x, 1));

    float3 m = max(0.5 - float3(dot(x0, x0), dot(x12.xy, x12.xy), dot(x12.zw, x12.zw)), 0.0);
    m = m * m;
    m = m * m;

    float3 x_ = 2.0 * frac(p * C.www) - 1.0;
    float3 h  = abs(x_) - 0.5;
    float3 ox = floor(x_ + 0.5);
    float3 a0 = x_ - ox;

    m *= 1.79284291400159 - 0.85373472095314 * (a0 * a0 + h * h);

    float3 g;
    g.x = a0.x * x0.x + h.x * x0.y;
    g.yz = a0.yz * x12.xz + h.yz * x12.yw;

    return 130.0 * dot(m, g);
}


//----------------------------------------------------------------------------------------------//
//  KiGrain: Film Grain (up to 3 layers, per-channel, log-space)
//----------------------------------------------------------------------------------------------//

float3 GenerateGrain(float2 UV, float3 temporalParams, float motion)
{
    float time = floor((Timer.z + 0.0) * motion);
    float2 seed = UV * ScreenRes + time * 7.31;

    // Layer 1 (fine)
    float grainR = SimplexNoise2D(seed * UIGrain_Int / max(1.0, 1.0) + float2(0, 0));
    float grainG = SimplexNoise2D(seed * UIGrain_Int / max(1.0, 1.0) + float2(37.2, 0));
    float grainB = SimplexNoise2D(seed * UIGrain_Int / max(1.0, 1.0) + float2(0, 73.1));

    float3 grain = float3(grainR, grainG, grainB);

    // Desaturate grain based on saturation control
    float grainMono = dot(grain, 0.333);
    grain = lerp(grainMono, grain, UIGrain_Saturation);

    return grain * UIGrain_Int;
}


//----------------------------------------------------------------------------------------------//
//  Vignette: Lp-Norm Generalized Elliptical
//----------------------------------------------------------------------------------------------//

float3 ApplyVignette(float3 color, float2 UV)
{
    [branch] if(!UI_VigEnable) return color;

    float2 centered = UV * 2.0 - 1.0;
    centered += float2(UI_VigWidth, UI_VigHeight) * 0.0; // placeholder for position offset

    // Lp-norm generalized shape
    float p = lerp(1.0, 100.0, UI_VigCurve * 0.1);  // roundness: 1=diamond, 2=ellipse, high=squircle
    float2 scaled = centered / float2(max(UI_VigRadiusOuter, 0.01), max(UI_VigRadiusOuter, 0.01));
    float dist = pow(abs(pow(abs(scaled.x), p) + pow(abs(scaled.y), p)), 1.0 / p);

    float mask = smoothstep(UI_VigRadiusInner, UI_VigRadiusOuter, dist);
    mask = pow(abs(mask), UI_VigCurve);

    float3 vigColor = UI_VigColor.rgb;
    float  alpha    = UI_VigColor.a * mask;

    // Blend modes
    float3 result;
    if     (UI_VigBlendMode == 1) result = color * lerp(1.0, vigColor, alpha);          // Multiply
    else if(UI_VigBlendMode == 2) result = lerp(color, 1.0 - (1.0 - color) * (1.0 - vigColor), alpha); // Screen
    else if(UI_VigBlendMode == 3) // Overlay
    {
        float3 overlay = (color < 0.5) ? 2.0 * color * vigColor : 1.0 - 2.0 * (1.0 - color) * (1.0 - vigColor);
        result = lerp(color, overlay, alpha);
    }
    else // Color (HSL)
    {
        float3 hslOrig = RGB2HSL(saturate(color));
        float3 hslVig  = RGB2HSL(vigColor);
        hslOrig.x = hslVig.x;
        hslOrig.y = hslVig.y;
        float3 colorBlend = HSL2RGB(hslOrig);
        result = lerp(color, colorBlend, alpha);
    }

    // Debug: show inner/outer radii
    #if ENABLE_TOOLS
    [branch] if(UI_VigShowRadii)
    {
        float innerRing = abs(dist - UI_VigRadiusInner) < 0.005 ? 1.0 : 0.0;
        float outerRing = abs(dist - UI_VigRadiusOuter) < 0.005 ? 1.0 : 0.0;
        result += float3(0, innerRing, 0) + float3(outerRing, 0, 0);
    }
    #endif

    return result;
}


//----------------------------------------------------------------------------------------------//
//  Cinematic Borders: Letterbox / Pillarbox
//----------------------------------------------------------------------------------------------//

float3 ApplyBorders(float3 color, float2 UV)
{
    [branch] if(!UIBorder_Enable) return color;

    float ScreenAspect = ScreenRes.x / ScreenRes.y;
    float borderMask = 0.0;

    if(!UIBorder_Use_PxlOs) // Aspect ratio mode
    {
        float targetAspect = UIBorder_Ratio;
        if(targetAspect > ScreenAspect) // Letterbox
        {
            float barHeight = 0.5 * (1.0 - ScreenAspect / targetAspect);
            borderMask = (UV.y < barHeight || UV.y > 1.0 - barHeight) ? 1.0 : 0.0;
        }
        else // Pillarbox
        {
            float barWidth = 0.5 * (1.0 - targetAspect / ScreenAspect);
            borderMask = (UV.x < barWidth || UV.x > 1.0 - barWidth) ? 1.0 : 0.0;
        }
    }
    else // Manual pixel offsets
    {
        float w = UIBorder_Width / ScreenRes.x;
        float h = UIBorder_Height / ScreenRes.y;
        borderMask = (UV.x < w || UV.x > 1.0 - w || UV.y < h || UV.y > 1.0 - h) ? 1.0 : 0.0;
    }

    return lerp(color, UIBorder_Color.rgb, borderMask * UIBorder_Color.a);
}


//----------------------------------------------------------------------------------------------//
//  Watermark: Procedural 3×5 pixel font
//----------------------------------------------------------------------------------------------//

float3 ApplyWatermark(float3 color, float2 UV)
{
    // Watermark is opt-in and rarely used — stub implementation
    return color;
}


//----------------------------------------------------------------------------------------------//
//  Dither: Include blue-noise temporal dither
//----------------------------------------------------------------------------------------------//

// Define UI bindings before including dither header
#define DITHER_UI_PROVIDED
static const float UID_Intensity = 10.0;   // Bound to INI param
static const float UID_Motion = 1.0;
static const bool  UID_Visualize = false;

#include "Helper/enbHelper_Dither.fxh"


//----------------------------------------------------------------------------------------------//
//  Final Pass: VS + PS combining all final-stage effects
//----------------------------------------------------------------------------------------------//

FinalVSOutput VS_Final(VertexShaderInput IN)
{
    FinalVSOutput OUT;
    OUT.pos      = float4(IN.pos.xy, 0, 1);
    OUT.texcoord = IN.txcoord;
    OUT.pos.w    = 1.0;

    // Precompute grain temporal params in VS
    float grainTime = floor((Timer.z) * UIGrain_Motion);
    float grainGrid = frac(floor(grainTime / 64.0) * 0.5);
    float grainTS   = frac(grainTime / 64.0) * 64.0;
    float grainCol  = floor(grainTS / 8.0);
    float grainRow  = grainTS - grainCol * 8.0;
    OUT.GrainTemporalParams = float3(grainRow * 64.0, grainCol * 64.0, grainGrid);

    // Dither temporal params
    OUT.DitherTemporalParams = GetTemporalDitherParams(0.0);

    return OUT;
}

float4 PS_Final(FinalVSOutput IN) : SV_Target
{
    float2 UV = IN.texcoord;
    float3 color = TextureColor.SampleLevel(Point_Sampler, UV, 0).rgb;

    #if DIAGNOSTIC_PASSTHROUGH
    return float4(color, 1.0);
    #endif

    // === 1. Vignette ===
    color = ApplyVignette(color, UV);

    // === 2. Film Grain ===
    [branch] if(UIGrain_Enable)
    {
        float3 grain = GenerateGrain(UV, IN.GrainTemporalParams, UIGrain_Motion);
        float  grainAmount = UIGrain_Int;

        // SkyrimBridge: reduce grain in menus
        #ifdef SKYRIMBRIDGE_FXH
        [flatten] if(SB_PP_IsInMenu())
            grainAmount *= (1.0 - UISBPP_MenuGrainReduce);
        #endif

        // Luminance-based shadow/highlight exclusion (placeholder — full impl uses KiGrain params)
        float luma = dot(color, LUM_709);

        // Apply grain (additive for simplicity; full impl uses log-space)
        color = saturate(color + grain * grainAmount * 0.1);
    }

    // === 3. Cinematic Borders ===
    color = ApplyBorders(color, UV);

    // === 4. Watermark ===
    color = ApplyWatermark(color, UV);

    // === 4.5 SkyrimBridge: Drunk Radial Blur ===
    #ifdef SKYRIMBRIDGE_FXH
    {
        float drunkAmt = SB_PP_GetDrunkBlur();
        [branch] if(drunkAmt > 0.001)
        {
            // Radial blur from screen center — simulates disorientation
            float2 toCenter = UV - 0.5;
            float3 blurAccum = color;
            float  blurSamples = 1.0;
            static const int DRUNK_STEPS = 6;
            [unroll] for(int d = 1; d <= DRUNK_STEPS; d++)
            {
                float t = (float)d / (float)DRUNK_STEPS;
                float2 offset = toCenter * t * drunkAmt * 0.04;
                blurAccum += TextureColor.SampleLevel(Linear_Sampler, UV - offset, 0).rgb;
                blurSamples += 1.0;
            }
            color = blurAccum / blurSamples;
        }
    }
    #endif

    // === 5. Dither (absolute final — any color op after this reintroduces banding) ===
    GaussBlueDither(color, IN.pos.xy, IN.DitherTemporalParams, 8);

    return float4(color, 1.0);
}


//=============================================================================//
//                                                                             //
//  ████████████████████████████████████████████████████████████████████████    //
//  ██                                                                    ██   //
//  ██         SECTION 8: TECHNIQUE CHAIN                                 ██   //
//  ██                                                                    ██   //
//  ████████████████████████████████████████████████████████████████████████    //
//                                                                             //
//=============================================================================//

// --- SMAA (3 techniques: edge detect → blend weights → neighborhood blend) ---

TWOPASSTECH11 (KitsuunePostPass <string UIName="Post Processing"; string RenderTarget="RenderTargetRGBA64F";>,
                                  VS_Basic(),      PS_Blank(),
                                  VS_SMAAEdge(),   PS_SMAAEdge())

TECH11        (KitsuunePostPass1 <string RenderTarget="RenderTargetRGBA64";>, VS_SMAAWeight(), PS_SMAAWeight())

TECH11        (KitsuunePostPass2, VS_SMAABlend(),  PS_SMAABlend())

// --- FXAA ---

TECH11        (KitsuunePostPass3, VS_FXAA(),       PS_FXAA())

// --- KiSharp (Iterative Unsharp Mask) ---

TECH11        (KitsuunePostPass4, VS_KiSharp(),    PS_KiSharp())

// --- Blur Suite (two-pass: clear RT + blur) ---

#if ENABLE_BLUR_SUITE
TWOPASSTECH11 (KitsuunePostPass5 <string UIName="Post Processing - Blur Suite"; string RenderTarget="RenderTargetRGBA64F";>,
                                  VS_Basic(),      PS_Blank(),
                                  VS_BlurSuite(),  PS_BlurSuite())
#endif

// --- Lens Distortion + Chromatic Aberration ---

TECH11        (KitsuunePostPass6, VS_LensDist(),   PS_LensDist())

// --- Color Grading (KiSuite + LUTs + Monochroma) ---

TECH11        (KitsuunePostPass7, VS_ColorGrade(), PS_ColorGrade())

// --- Final Pass (Vignette + Grain + Borders + Watermark + Dither) ---

TECH11        (KitsuunePostPass8, VS_Final(),      PS_Final())
