//----------------------------------------------------------------------------------------------//
//                                                                                              //
//                     enbeffect.fx — HDR Post-Processing Compositor                            //
//                  Bloom Mixing / Tonemapping / Color Grading / Dithering                      //
//                   for Skyrim SE ENB (DirectX 11 Shader Model 5)                              //
//                                                                                              //
//         Original framework by LonelyKitsuune / T.Thanner - CC BY-NC-ND 4.0                  //
//                            Boris Vorontsov for ENBSeries                                     //
//                                                                                              //
//----------------------------------------------------------------------------------------------//
//                                                                                              //
//     Physically-motivated rendering pipeline by Zain Dana Harper - February 2026              //
//                                                                                              //
//  Architecture: 1 technique, 1 pass                                                          //
//                                                                                              //
//  Pipeline (order controlled by Color Pipeline selector):                                     //
//                                                                                              //
//    Standard (Pipeline 0):                                                                    //
//        1. Bloom mixing / blending                                                            //
//        2. Tonemapping                                                                        //
//        3. HDR color grading (exposure, contrast, sat, gamma, tint, balance)                  //
//        4. AGIS — Automatic Game Image-Space (respects game's own IMODs)                      //
//        5. Film emulation (gamut transform, log encoding)                                     //
//        6. Output dithering                                                                   //
//                                                                                              //
//    Pre-Grade (Pipeline 1):                                                                   //
//        1. Bloom mixing / blending                                                            //
//        2. HDR color grading (in linear HDR, before tonemap clamp)                            //
//        3. Tonemapping                                                                        //
//        4. AGIS                                                                               //
//        5. Film emulation                                                                     //
//        6. Output dithering                                                                   //
//                                                                                              //
//  Bloom Mixing:                                                                               //
//    Per-TOD intensity/contrast/saturation/color-balance on the bloom buffer                   //
//    before additive composite. Contrast uses Lottes-style midpoint pivot.                     //
//    Optional linear blend mode (screen blend) for softer integration.                         //
//                                                                                              //
//  Tonemappers (7 selectable, per-TOD parameterized):                                          //
//    0. Linear (bypass)                                                                        //
//    1. Reinhard extended (white point)                                                        //
//    2. Hejl-Burgess filmic (white point)                                                      //
//    3. Uncharted 2 / Hable filmic (6-param curve)                                             //
//    4. VDR — Variable Dynamic Range (middle grey, toe contrast, shoulder)                     //
//    5. ACES — Academy Color Encoding System (two fitting methods)                             //
//    6. Kitsuune — custom S-curve (shoulder + toe)                                             //
//                                                                                              //
//  Tonemapping Methods:                                                                        //
//    0. Per-channel (preserves hue under heavy compression)                                    //
//    1. Luminance-based (compress luminance, reapply chroma — can shift hues)                  //
//                                                                                              //
//  CG.HDR — Color Grading:                                                                     //
//    Per 7-TOD: Exposure (EV), Contrast (pivot at middle grey), Saturation,                    //
//    3-channel Color Balance, Gamma, Gamma Correction, RGBA Tint.                              //
//    All operations in linear light (before or after tonemap per pipeline).                    //
//                                                                                              //
//  CG.AGIS — Automatic Game Image-Space:                                                       //
//    When enabled, maps the game's own imagespace modifiers (saturation,                       //
//    contrast, brightness, tint from SkyrimBridge IS_Cinematic/IS_CineTint)                    //
//    into bounded adjustments. Prevents preset from fighting game effects                      //
//    (vampire lord, damage flash, skooma, Dragonborn DLC effects).                             //
//    Per-DNI min/max clamping prevents extreme game-authored adjustments.                       //
//                                                                                              //
//  Film Emulation:                                                                             //
//    Input/output gamut transforms (sRGB, Rec.709, DCI-P3, ACEScg).                            //
//    Log color space encoding (ACEScg, LogC, S-Log3, DaVinci Intermediate).                    //
//    Enables LUT compatibility with professional color grading workflows.                      //
//                                                                                              //
//  Dithering:                                                                                  //
//    Gaussian blue-noise temporal dither to break banding at output bit depth.                 //
//    Motion-aware temporal cycling to prevent static patterns on still frames.                  //
//                                                                                              //
//  SkyrimBridge Integration:                                                                   //
//    - Lightning flash → momentary exposure compensation                                       //
//    - Rain/snow → atmospheric desaturation                                                    //
//    - Game IS_Cinematic → AGIS pipeline                                                       //
//    - Night Eye → exposure adaptation override                                                //
//                                                                                              //
//  Debug:                                                                                      //
//    Bloom isolation overlay, dither pattern visualization, tonemapper                         //
//    curve plot, color grading before/after split-screen.                                       //
//    All guarded behind ENABLE_TOOLS compile flag.                                             //
//                                                                                              //
//  References:                                                                                 //
//    [1] Hable, "Filmic Tonemapping Operators", GDC 2010                                       //
//    [2] Narkowicz, "ACES Filmic Tone Mapping Curve", 2015                                     //
//    [3] Hill, "sRGB Approximation for ACES Output Transform", 2016                            //
//    [4] Lottes, "Advanced Techniques and Optimization of HDR Color Pipelines",                //
//        GDC 2016                                                                              //
//    [5] Hejl & Burgess, "Filmic Tonemapping for Real-Time Rendering",                         //
//        SIGGRAPH 2010                                                                         //
//    [6] Reinhard et al., "Photographic Tone Reproduction for Digital Images",                  //
//        SIGGRAPH 2002                                                                         //
//    [7] Zink, "Variable Dynamic Range", 2016                                                  //
//                                                                                              //
//----------------------------------------------------------------------------------------------//


//=============================================================================//
//                                OPTIONS                                      //
//=============================================================================//

#define ENABLE_TOOLS 0   // [0-1] Debug visualizations


//=============================================================================//
//                           UI PARAMETER INCLUDES                             //
//=============================================================================//

#include "UI/enbUI_Primer.fxh"
UI_FileHeaderLong(" Silent Horizons ENB", " HDR Post-Processing")


//=============================================================================//
//                                                                             //
//  UI PARAMETERS — ALL INLINE                                                 //
//                                                                             //
//  Organized by INI section to match enbeffect_fx.ini exactly.                //
//  Every float parameter uses UIWidget="spinner" per project convention.      //
//                                                                             //
//=============================================================================//


// ─────────────────── Developer Tools ────────────────────────────────────────
int _spc0 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrDEV < string UIName = "======== DEVELOPER TOOLS ========"; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool  UIDebugTools < string UIName = ":: Enable Debug Tools"; > = false;


// ─────────────────── Globals ────────────────────────────────────────────────
int _spc1 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrGLOB < string UIName = "========= GLOBALS ========="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

int UIColorPipeline < string UIName = ":: Color Pipeline"; string UIWidget = "spinner"; int UIMin = 0; int UIMax = 1; > = 0;
// 0 = Standard (bloom → tonemap → CG)
// 1 = Pre-Grade (bloom → CG → tonemap)


// ─────────────────── Bloom Mixing ───────────────────────────────────────────
int _spc2 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrBLM < string UIName = "========= BLOOM MIXING ========="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

int  UIBloomMix_WeatherSep   < string UIName = ":: Weather Separation"; string UIWidget = "spinner"; int UIMin = 0; int UIMax = 1; > = 0;
bool UIBloomMix_Enable       < string UIName = ":: Enable Mixing"; > = true;
bool UIBloomMix_ExtendTOD    < string UIName = ":: Extend Time Separation"; > = false;

// --- Dawn ---
float  Dawn_BloomIntensity   < string UIName = "|- Dawn - Intensity"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01; > = 1.0;
float  Dawn_BloomContrast    < string UIName = "|- Dawn - Contrast"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 0.7;
float  Dawn_BloomContrastMid < string UIName = "|- Dawn - Contrast Middle Grey"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 10.0; float UIStep = 0.1; > = 2.5;
float  Dawn_BloomSaturation  < string UIName = "|- Dawn - Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01; > = 1.5;
float3 Dawn_BloomBalance     < string UIName = "|- Dawn - Color Balance"; string UIWidget = "color"; > = {1.0, 1.0, 1.0};
UI_SPECIAL_WHITESPACE(2)

// --- Sunrise ---
float  Sunrise_BloomIntensity   < string UIName = "|- Sunrise - Intensity"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01; > = 1.0;
float  Sunrise_BloomContrast    < string UIName = "|- Sunrise - Contrast"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 0.7;
float  Sunrise_BloomContrastMid < string UIName = "|- Sunrise - Contrast Middle Grey"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 10.0; float UIStep = 0.1; > = 2.5;
float  Sunrise_BloomSaturation  < string UIName = "|- Sunrise - Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01; > = 1.5;
float3 Sunrise_BloomBalance     < string UIName = "|- Sunrise - Color Balance"; string UIWidget = "color"; > = {1.0, 1.0, 1.0};
UI_SPECIAL_WHITESPACE(3)

// --- Day ---
float  Day_BloomIntensity   < string UIName = "|- Day - Intensity"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01; > = 1.0;
float  Day_BloomContrast    < string UIName = "|- Day - Contrast"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 0.7;
float  Day_BloomContrastMid < string UIName = "|- Day - Contrast Middle Grey"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 10.0; float UIStep = 0.1; > = 2.5;
float  Day_BloomSaturation  < string UIName = "|- Day - Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01; > = 1.5;
float3 Day_BloomBalance     < string UIName = "|- Day - Color Balance"; string UIWidget = "color"; > = {1.0, 1.0, 1.0};
UI_SPECIAL_WHITESPACE(4)

// --- Sunset ---
float  Sunset_BloomIntensity   < string UIName = "|- Sunset - Intensity"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01; > = 1.0;
float  Sunset_BloomContrast    < string UIName = "|- Sunset - Contrast"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 0.7;
float  Sunset_BloomContrastMid < string UIName = "|- Sunset - Contrast Middle Grey"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 10.0; float UIStep = 0.1; > = 2.5;
float  Sunset_BloomSaturation  < string UIName = "|- Sunset - Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01; > = 1.5;
float3 Sunset_BloomBalance     < string UIName = "|- Sunset - Color Balance"; string UIWidget = "color"; > = {1.0, 1.0, 1.0};
UI_SPECIAL_WHITESPACE(5)

// --- Dusk ---
float  Dusk_BloomIntensity   < string UIName = "|- Dusk - Intensity"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01; > = 1.0;
float  Dusk_BloomContrast    < string UIName = "|- Dusk - Contrast"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 0.7;
float  Dusk_BloomContrastMid < string UIName = "|- Dusk - Contrast Middle Grey"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 10.0; float UIStep = 0.1; > = 2.5;
float  Dusk_BloomSaturation  < string UIName = "|- Dusk - Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01; > = 1.5;
float3 Dusk_BloomBalance     < string UIName = "|- Dusk - Color Balance"; string UIWidget = "color"; > = {1.0, 1.0, 1.0};
UI_SPECIAL_WHITESPACE(6)

// --- Night ---
float  Night_BloomIntensity   < string UIName = "|- Night - Intensity"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01; > = 1.0;
float  Night_BloomContrast    < string UIName = "|- Night - Contrast"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 0.7;
float  Night_BloomContrastMid < string UIName = "|- Night - Contrast Middle Grey"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 10.0; float UIStep = 0.1; > = 2.5;
float  Night_BloomSaturation  < string UIName = "|- Night - Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01; > = 1.5;
float3 Night_BloomBalance     < string UIName = "|- Night - Color Balance"; string UIWidget = "color"; > = {1.0, 1.0, 1.0};
UI_SPECIAL_WHITESPACE(7)

// --- Interior ---
float  Interior_BloomIntensity   < string UIName = "|- Interior - Intensity"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01; > = 1.0;
float  Interior_BloomContrast    < string UIName = "|- Interior - Contrast"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 0.75;
float  Interior_BloomContrastMid < string UIName = "|- Interior - Contrast Middle Grey"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 10.0; float UIStep = 0.1; > = 2.5;
float  Interior_BloomSaturation  < string UIName = "|- Interior - Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01; > = 1.5;
float3 Interior_BloomBalance     < string UIName = "|- Interior - Color Balance"; string UIWidget = "color"; > = {1.0, 1.0, 1.0};


// ─────────────────── Bloom Blending (Linear) ────────────────────────────────
int _spc8 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrBLB < string UIName = "========= BLOOM BLENDING ========="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

int  UIBloomBlend_WeatherSep < string UIName = ":: Weather Separation"; string UIWidget = "spinner"; int UIMin = 0; int UIMax = 1; > = 0;
bool UIBloomBlend_Enable     < string UIName = ":: Enable Linear Blending"; > = false;
bool UIBloomBlend_ExtendTOD  < string UIName = ":: Extend Time Separation"; > = false;

float Day_BlendAmount     < string UIName = "|- Day - Linear Blend Amount"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.65;
float Day_BlendWeight     < string UIName = "|- Day - Linear Blend Weight"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.16;
UI_SPECIAL_WHITESPACE(9)
float Night_BlendAmount   < string UIName = "|- Night - Linear Blend Amount"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.65;
float Night_BlendWeight   < string UIName = "|- Night - Linear Blend Weight"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.16;
UI_SPECIAL_WHITESPACE(10)
float Interior_BlendAmount < string UIName = "|- Interior - Linear Blend Amount"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.65;
float Interior_BlendWeight < string UIName = "|- Interior - Linear Blend Weight"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.16;


// ─────────────────── Tonemapping ────────────────────────────────────────────
int _spc11 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrTM < string UIName = "========= TONEMAPPING ========="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

int UITonemap_WeatherSep  < string UIName = ":: Weather Separation"; string UIWidget = "spinner"; int UIMin = 0; int UIMax = 1; > = 0;
int UITonemapper          < string UIName = ":: Tonemapper"; string UIWidget = "spinner"; int UIMin = 0; int UIMax = 6; > = 6;
// 0=Linear, 1=Reinhard, 2=Hejl, 3=UC2, 4=VDR, 5=ACES, 6=Kitsuune
int UITonemapMethod       < string UIName = ":: Tonemapping Method"; string UIWidget = "spinner"; int UIMin = 0; int UIMax = 1; > = 0;
// 0=Per-channel, 1=Luminance-based
int UITonemapACESMethod   < string UIName = ":: Tonemapping Method (ACES)"; string UIWidget = "spinner"; int UIMin = 0; int UIMax = 1; > = 0;
// 0=Stephen Hill sRGB fit, 1=Narkowicz approximation

// --- Kitsuune per-TOD ---
int _spcKIT < string UIName = "   --- Kitsuune ---"; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
float Dawn_KitShoulder      < string UIName = "|- Dawn - Kitsuune - Shoulder"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 5.0; float UIStep = 0.01; > = 1.66;
float Dawn_KitToe            < string UIName = "|- Dawn - Kitsuune - Toe"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 0.42;
float Sunrise_KitShoulder   < string UIName = "|- Sunrise - Kitsuune - Shoulder"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 5.0; float UIStep = 0.01; > = 1.66;
float Sunrise_KitToe         < string UIName = "|- Sunrise - Kitsuune - Toe"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 0.42;
float Day_KitShoulder       < string UIName = "|- Day - Kitsuune - Shoulder"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 5.0; float UIStep = 0.01; > = 1.66;
float Day_KitToe             < string UIName = "|- Day - Kitsuune - Toe"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 0.42;
float Sunset_KitShoulder    < string UIName = "|- Sunset - Kitsuune - Shoulder"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 5.0; float UIStep = 0.01; > = 1.66;
float Sunset_KitToe          < string UIName = "|- Sunset - Kitsuune - Toe"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 0.42;
float Dusk_KitShoulder      < string UIName = "|- Dusk - Kitsuune - Shoulder"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 5.0; float UIStep = 0.01; > = 1.66;
float Dusk_KitToe            < string UIName = "|- Dusk - Kitsuune - Toe"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 0.42;
float Night_KitShoulder     < string UIName = "|- Night - Kitsuune - Shoulder"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 5.0; float UIStep = 0.01; > = 1.66;
float Night_KitToe           < string UIName = "|- Night - Kitsuune - Toe"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 0.42;
float Interior_KitShoulder  < string UIName = "|- Interior - Kitsuune - Shoulder"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 5.0; float UIStep = 0.01; > = 1.658;
float Interior_KitToe        < string UIName = "|- Interior - Kitsuune - Toe"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 0.42;

// --- Reinhard per-TOD ---
int _spcRH < string UIName = "   --- Reinhard ---"; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
float Dawn_ReinhardWP      < string UIName = "|- Dawn - Reinhard - White Point"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 20.0; float UIStep = 0.01; > = 1.0;
float Sunrise_ReinhardWP   < string UIName = "|- Sunrise - Reinhard - White Point"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 20.0; float UIStep = 0.01; > = 1.0;
float Day_ReinhardWP       < string UIName = "|- Day - Reinhard - White Point"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 20.0; float UIStep = 0.01; > = 1.85;
float Sunset_ReinhardWP    < string UIName = "|- Sunset - Reinhard - White Point"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 20.0; float UIStep = 0.01; > = 1.0;
float Dusk_ReinhardWP      < string UIName = "|- Dusk - Reinhard - White Point"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 20.0; float UIStep = 0.01; > = 1.0;
float Night_ReinhardWP     < string UIName = "|- Night - Reinhard - White Point"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 20.0; float UIStep = 0.01; > = 1.0;
float Interior_ReinhardWP  < string UIName = "|- Interior - Reinhard - White Point"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 20.0; float UIStep = 0.01; > = 1.0;

// --- Hejl per-TOD ---
int _spcHJ < string UIName = "   --- Hejl ---"; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
float Dawn_HejlWP      < string UIName = "|- Dawn - Hejl - White Point"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 20.0; float UIStep = 0.01; > = 1.0;
float Sunrise_HejlWP   < string UIName = "|- Sunrise - Hejl - White Point"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 20.0; float UIStep = 0.01; > = 1.0;
float Day_HejlWP       < string UIName = "|- Day - Hejl - White Point"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 20.0; float UIStep = 0.01; > = 1.0;
float Sunset_HejlWP    < string UIName = "|- Sunset - Hejl - White Point"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 20.0; float UIStep = 0.01; > = 1.0;
float Dusk_HejlWP      < string UIName = "|- Dusk - Hejl - White Point"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 20.0; float UIStep = 0.01; > = 1.0;
float Night_HejlWP     < string UIName = "|- Night - Hejl - White Point"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 20.0; float UIStep = 0.01; > = 1.0;
float Interior_HejlWP  < string UIName = "|- Interior - Hejl - White Point"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 20.0; float UIStep = 0.01; > = 1.0;

// --- UC2 (Uncharted 2 / Hable) per-TOD ---
int _spcUC < string UIName = "   --- Uncharted 2 ---"; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

// Dawn
float Dawn_UC2_ShoulderStr < string UIName = "|- Dawn - UC2 - Shoulder Strength"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01; > = 0.7;
float Dawn_UC2_LinStr      < string UIName = "|- Dawn - UC2 - Linear Strength"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 0.4;
float Dawn_UC2_LinAngle    < string UIName = "|- Dawn - UC2 - Linear Angle"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.25;
float Dawn_UC2_ToeStr      < string UIName = "|- Dawn - UC2 - Toe Strength"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01; > = 2.5;
float Dawn_UC2_ToeNum      < string UIName = "|- Dawn - UC2 - Toe Numerator"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.001; > = 0.03;
float Dawn_UC2_ToeDen      < string UIName = "|- Dawn - UC2 - Toe Denominator"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.001; > = 0.17;
float Dawn_UC2_LinWhite    < string UIName = "|- Dawn - UC2 - Linear White"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 50.0; float UIStep = 0.1; > = 15.0;
UI_SPECIAL_WHITESPACE(12)
// Sunrise
float Sunrise_UC2_ShoulderStr < string UIName = "|- Sunrise - UC2 - Shoulder Strength"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01; > = 0.7;
float Sunrise_UC2_LinStr      < string UIName = "|- Sunrise - UC2 - Linear Strength"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 0.4;
float Sunrise_UC2_LinAngle    < string UIName = "|- Sunrise - UC2 - Linear Angle"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.25;
float Sunrise_UC2_ToeStr      < string UIName = "|- Sunrise - UC2 - Toe Strength"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01; > = 2.5;
float Sunrise_UC2_ToeNum      < string UIName = "|- Sunrise - UC2 - Toe Numerator"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.001; > = 0.03;
float Sunrise_UC2_ToeDen      < string UIName = "|- Sunrise - UC2 - Toe Denominator"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.001; > = 0.17;
float Sunrise_UC2_LinWhite    < string UIName = "|- Sunrise - UC2 - Linear White"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 50.0; float UIStep = 0.1; > = 15.0;
UI_SPECIAL_WHITESPACE(13)
// Day
float Day_UC2_ShoulderStr < string UIName = "|- Day - UC2 - Shoulder Strength"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01; > = 0.7;
float Day_UC2_LinStr      < string UIName = "|- Day - UC2 - Linear Strength"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 0.4;
float Day_UC2_LinAngle    < string UIName = "|- Day - UC2 - Linear Angle"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.25;
float Day_UC2_ToeStr      < string UIName = "|- Day - UC2 - Toe Strength"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01; > = 2.5;
float Day_UC2_ToeNum      < string UIName = "|- Day - UC2 - Toe Numerator"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.001; > = 0.03;
float Day_UC2_ToeDen      < string UIName = "|- Day - UC2 - Toe Denominator"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.001; > = 0.17;
float Day_UC2_LinWhite    < string UIName = "|- Day - UC2 - Linear White"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 50.0; float UIStep = 0.1; > = 15.0;
UI_SPECIAL_WHITESPACE(14)
// Sunset
float Sunset_UC2_ShoulderStr < string UIName = "|- Sunset - UC2 - Shoulder Strength"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01; > = 0.7;
float Sunset_UC2_LinStr      < string UIName = "|- Sunset - UC2 - Linear Strength"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 0.4;
float Sunset_UC2_LinAngle    < string UIName = "|- Sunset - UC2 - Linear Angle"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.25;
float Sunset_UC2_ToeStr      < string UIName = "|- Sunset - UC2 - Toe Strength"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01; > = 2.5;
float Sunset_UC2_ToeNum      < string UIName = "|- Sunset - UC2 - Toe Numerator"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.001; > = 0.03;
float Sunset_UC2_ToeDen      < string UIName = "|- Sunset - UC2 - Toe Denominator"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.001; > = 0.17;
float Sunset_UC2_LinWhite    < string UIName = "|- Sunset - UC2 - Linear White"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 50.0; float UIStep = 0.1; > = 15.0;
UI_SPECIAL_WHITESPACE(15)
// Dusk
float Dusk_UC2_ShoulderStr < string UIName = "|- Dusk - UC2 - Shoulder Strength"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01; > = 0.7;
float Dusk_UC2_LinStr      < string UIName = "|- Dusk - UC2 - Linear Strength"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 0.4;
float Dusk_UC2_LinAngle    < string UIName = "|- Dusk - UC2 - Linear Angle"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.25;
float Dusk_UC2_ToeStr      < string UIName = "|- Dusk - UC2 - Toe Strength"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01; > = 2.5;
float Dusk_UC2_ToeNum      < string UIName = "|- Dusk - UC2 - Toe Numerator"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.001; > = 0.03;
float Dusk_UC2_ToeDen      < string UIName = "|- Dusk - UC2 - Toe Denominator"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.001; > = 0.17;
float Dusk_UC2_LinWhite    < string UIName = "|- Dusk - UC2 - Linear White"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 50.0; float UIStep = 0.1; > = 15.0;
UI_SPECIAL_WHITESPACE(16)
// Night
float Night_UC2_ShoulderStr < string UIName = "|- Night - UC2 - Shoulder Strength"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01; > = 0.7;
float Night_UC2_LinStr      < string UIName = "|- Night - UC2 - Linear Strength"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 0.4;
float Night_UC2_LinAngle    < string UIName = "|- Night - UC2 - Linear Angle"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.25;
float Night_UC2_ToeStr      < string UIName = "|- Night - UC2 - Toe Strength"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01; > = 2.5;
float Night_UC2_ToeNum      < string UIName = "|- Night - UC2 - Toe Numerator"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.001; > = 0.03;
float Night_UC2_ToeDen      < string UIName = "|- Night - UC2 - Toe Denominator"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.001; > = 0.17;
float Night_UC2_LinWhite    < string UIName = "|- Night - UC2 - Linear White"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 50.0; float UIStep = 0.1; > = 15.0;
UI_SPECIAL_WHITESPACE(17)
// Interior
float Interior_UC2_ShoulderStr < string UIName = "|- Interior - UC2 - Shoulder Strength"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01; > = 0.7;
float Interior_UC2_LinStr      < string UIName = "|- Interior - UC2 - Linear Strength"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 0.4;
float Interior_UC2_LinAngle    < string UIName = "|- Interior - UC2 - Linear Angle"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.25;
float Interior_UC2_ToeStr      < string UIName = "|- Interior - UC2 - Toe Strength"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01; > = 2.5;
float Interior_UC2_ToeNum      < string UIName = "|- Interior - UC2 - Toe Numerator"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.001; > = 0.03;
float Interior_UC2_ToeDen      < string UIName = "|- Interior - UC2 - Toe Denominator"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.001; > = 0.17;
float Interior_UC2_LinWhite    < string UIName = "|- Interior - UC2 - Linear White"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 50.0; float UIStep = 0.1; > = 15.0;

// --- VDR (Variable Dynamic Range) per-TOD ---
int _spcVDR < string UIName = "   --- VDR ---"; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
float Dawn_VDR_MidGrey    < string UIName = "|- Dawn - VDR - Middle Grey In"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.5;
float Dawn_VDR_Toe        < string UIName = "|- Dawn - VDR - Contrast (Toe)"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 5.0; float UIStep = 0.01; > = 1.0;
float Dawn_VDR_Shoulder   < string UIName = "|- Dawn - VDR - Shoulder"; string UIWidget = "spinner"; float UIMin = 0.01; float UIMax = 50.0; float UIStep = 0.01; > = 1.0;
float Sunrise_VDR_MidGrey < string UIName = "|- Sunrise - VDR - Middle Grey In"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.5;
float Sunrise_VDR_Toe     < string UIName = "|- Sunrise - VDR - Contrast (Toe)"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 5.0; float UIStep = 0.01; > = 1.0;
float Sunrise_VDR_Shoulder < string UIName = "|- Sunrise - VDR - Shoulder"; string UIWidget = "spinner"; float UIMin = 0.01; float UIMax = 50.0; float UIStep = 0.01; > = 1.0;
float Day_VDR_MidGrey     < string UIName = "|- Day - VDR - Middle Grey In"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.45;
float Day_VDR_Toe         < string UIName = "|- Day - VDR - Contrast (Toe)"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 5.0; float UIStep = 0.01; > = 1.89;
float Day_VDR_Shoulder    < string UIName = "|- Day - VDR - Shoulder"; string UIWidget = "spinner"; float UIMin = 0.01; float UIMax = 50.0; float UIStep = 0.01; > = 0.18;
float Sunset_VDR_MidGrey  < string UIName = "|- Sunset - VDR - Middle Grey In"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.5;
float Sunset_VDR_Toe      < string UIName = "|- Sunset - VDR - Contrast (Toe)"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 5.0; float UIStep = 0.01; > = 1.0;
float Sunset_VDR_Shoulder < string UIName = "|- Sunset - VDR - Shoulder"; string UIWidget = "spinner"; float UIMin = 0.01; float UIMax = 50.0; float UIStep = 0.01; > = 1.0;
float Dusk_VDR_MidGrey    < string UIName = "|- Dusk - VDR - Middle Grey In"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.5;
float Dusk_VDR_Toe        < string UIName = "|- Dusk - VDR - Contrast (Toe)"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 5.0; float UIStep = 0.01; > = 1.0;
float Dusk_VDR_Shoulder   < string UIName = "|- Dusk - VDR - Shoulder"; string UIWidget = "spinner"; float UIMin = 0.01; float UIMax = 50.0; float UIStep = 0.01; > = 28.12;
float Night_VDR_MidGrey   < string UIName = "|- Night - VDR - Middle Grey In"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.5;
float Night_VDR_Toe       < string UIName = "|- Night - VDR - Contrast (Toe)"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 5.0; float UIStep = 0.01; > = 1.0;
float Night_VDR_Shoulder  < string UIName = "|- Night - VDR - Shoulder"; string UIWidget = "spinner"; float UIMin = 0.01; float UIMax = 50.0; float UIStep = 0.01; > = 1.0;
float Interior_VDR_MidGrey < string UIName = "|- Interior - VDR - Middle Grey In"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.5;
float Interior_VDR_Toe     < string UIName = "|- Interior - VDR - Contrast (Toe)"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 5.0; float UIStep = 0.01; > = 1.0;
float Interior_VDR_Shoulder < string UIName = "|- Interior - VDR - Shoulder"; string UIWidget = "spinner"; float UIMin = 0.01; float UIMax = 50.0; float UIStep = 0.01; > = 1.0;


// ─────────────────── CG.HDR — Color Grading ────────────────────────────────
int _spc18 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrCG < string UIName = "========= COLOR GRADING (HDR) ========="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

int UICG_WeatherSep < string UIName = ":: Weather Separation"; string UIWidget = "spinner"; int UIMin = 0; int UIMax = 1; > = 0;

// Dawn
float  Dawn_CGExposure     < string UIName = "|- Dawn - Exposure (in EVs)"; string UIWidget = "spinner"; float UIMin = -5.0; float UIMax = 5.0; float UIStep = 0.01; > = 0.0;
float  Dawn_CGContrast     < string UIName = "|- Dawn - Contrast"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 3.0; float UIStep = 0.01; > = 1.0;
float  Dawn_CGContrastMid  < string UIName = "|- Dawn - Contrast Middle Grey"; string UIWidget = "spinner"; float UIMin = 0.01; float UIMax = 10.0; float UIStep = 0.01; > = 1.4;
float  Dawn_CGSaturation   < string UIName = "|- Dawn - Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01; > = 1.0;
float3 Dawn_CGBalance      < string UIName = "|- Dawn - Color Balance"; string UIWidget = "color"; > = {1.0, 1.0, 1.0};
float  Dawn_CGGamma        < string UIName = "|- Dawn - Gamma"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 5.0; float UIStep = 0.01; > = 1.0;
float  Dawn_CGGammaCorr    < string UIName = "|- Dawn - Gamma Correction"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 5.0; float UIStep = 0.01; > = 1.0;
float4 Dawn_CGTint         < string UIName = "|- Dawn - Tint"; string UIWidget = "color"; > = {1.0, 1.0, 1.0, 0.0};
UI_SPECIAL_WHITESPACE(19)
// Sunrise
float  Sunrise_CGExposure     < string UIName = "|- Sunrise - Exposure (in EVs)"; string UIWidget = "spinner"; float UIMin = -5.0; float UIMax = 5.0; float UIStep = 0.01; > = 0.0;
float  Sunrise_CGContrast     < string UIName = "|- Sunrise - Contrast"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 3.0; float UIStep = 0.01; > = 1.0;
float  Sunrise_CGContrastMid  < string UIName = "|- Sunrise - Contrast Middle Grey"; string UIWidget = "spinner"; float UIMin = 0.01; float UIMax = 10.0; float UIStep = 0.01; > = 1.4;
float  Sunrise_CGSaturation   < string UIName = "|- Sunrise - Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01; > = 1.0;
float3 Sunrise_CGBalance      < string UIName = "|- Sunrise - Color Balance"; string UIWidget = "color"; > = {1.0, 1.0, 1.0};
float  Sunrise_CGGamma        < string UIName = "|- Sunrise - Gamma"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 5.0; float UIStep = 0.01; > = 1.0;
float  Sunrise_CGGammaCorr    < string UIName = "|- Sunrise - Gamma Correction"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 5.0; float UIStep = 0.01; > = 1.0;
float4 Sunrise_CGTint         < string UIName = "|- Sunrise - Tint"; string UIWidget = "color"; > = {1.0, 1.0, 1.0, 0.0};
UI_SPECIAL_WHITESPACE(20)
// Day
float  Day_CGExposure     < string UIName = "|- Day - Exposure (in EVs)"; string UIWidget = "spinner"; float UIMin = -5.0; float UIMax = 5.0; float UIStep = 0.01; > = 0.0;
float  Day_CGContrast     < string UIName = "|- Day - Contrast"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 3.0; float UIStep = 0.01; > = 0.999998;
float  Day_CGContrastMid  < string UIName = "|- Day - Contrast Middle Grey"; string UIWidget = "spinner"; float UIMin = 0.01; float UIMax = 10.0; float UIStep = 0.01; > = 1.0;
float  Day_CGSaturation   < string UIName = "|- Day - Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01; > = 1.05;
float3 Day_CGBalance      < string UIName = "|- Day - Color Balance"; string UIWidget = "color"; > = {1.0, 1.0, 1.0};
float  Day_CGGamma        < string UIName = "|- Day - Gamma"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 5.0; float UIStep = 0.01; > = 1.0;
float  Day_CGGammaCorr    < string UIName = "|- Day - Gamma Correction"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 5.0; float UIStep = 0.01; > = 1.0;
float4 Day_CGTint         < string UIName = "|- Day - Tint"; string UIWidget = "color"; > = {1.0, 1.0, 1.0, 0.0};
UI_SPECIAL_WHITESPACE(21)
// Sunset
float  Sunset_CGExposure     < string UIName = "|- Sunset - Exposure (in EVs)"; string UIWidget = "spinner"; float UIMin = -5.0; float UIMax = 5.0; float UIStep = 0.01; > = 0.0;
float  Sunset_CGContrast     < string UIName = "|- Sunset - Contrast"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 3.0; float UIStep = 0.01; > = 1.0;
float  Sunset_CGContrastMid  < string UIName = "|- Sunset - Contrast Middle Grey"; string UIWidget = "spinner"; float UIMin = 0.01; float UIMax = 10.0; float UIStep = 0.01; > = 1.4;
float  Sunset_CGSaturation   < string UIName = "|- Sunset - Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01; > = 1.0;
float3 Sunset_CGBalance      < string UIName = "|- Sunset - Color Balance"; string UIWidget = "color"; > = {1.0, 1.0, 1.0};
float  Sunset_CGGamma        < string UIName = "|- Sunset - Gamma"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 5.0; float UIStep = 0.01; > = 1.0;
float  Sunset_CGGammaCorr    < string UIName = "|- Sunset - Gamma Correction"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 5.0; float UIStep = 0.01; > = 1.0;
float4 Sunset_CGTint         < string UIName = "|- Sunset - Tint"; string UIWidget = "color"; > = {1.0, 1.0, 1.0, 0.0};
UI_SPECIAL_WHITESPACE(22)
// Dusk
float  Dusk_CGExposure     < string UIName = "|- Dusk - Exposure (in EVs)"; string UIWidget = "spinner"; float UIMin = -5.0; float UIMax = 5.0; float UIStep = 0.01; > = 0.0;
float  Dusk_CGContrast     < string UIName = "|- Dusk - Contrast"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 3.0; float UIStep = 0.01; > = 1.0;
float  Dusk_CGContrastMid  < string UIName = "|- Dusk - Contrast Middle Grey"; string UIWidget = "spinner"; float UIMin = 0.01; float UIMax = 10.0; float UIStep = 0.01; > = 1.4;
float  Dusk_CGSaturation   < string UIName = "|- Dusk - Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01; > = 1.08;
float3 Dusk_CGBalance      < string UIName = "|- Dusk - Color Balance"; string UIWidget = "color"; > = {1.0, 1.0, 1.0};
float  Dusk_CGGamma        < string UIName = "|- Dusk - Gamma"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 5.0; float UIStep = 0.01; > = 1.0;
float  Dusk_CGGammaCorr    < string UIName = "|- Dusk - Gamma Correction"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 5.0; float UIStep = 0.01; > = 1.0;
float4 Dusk_CGTint         < string UIName = "|- Dusk - Tint"; string UIWidget = "color"; > = {1.0, 1.0, 1.0, 0.0};
UI_SPECIAL_WHITESPACE(23)
// Night
float  Night_CGExposure     < string UIName = "|- Night - Exposure (in EVs)"; string UIWidget = "spinner"; float UIMin = -5.0; float UIMax = 5.0; float UIStep = 0.01; > = 0.0;
float  Night_CGContrast     < string UIName = "|- Night - Contrast"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 3.0; float UIStep = 0.01; > = 1.0;
float  Night_CGContrastMid  < string UIName = "|- Night - Contrast Middle Grey"; string UIWidget = "spinner"; float UIMin = 0.01; float UIMax = 10.0; float UIStep = 0.01; > = 1.4;
float  Night_CGSaturation   < string UIName = "|- Night - Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01; > = 1.0;
float3 Night_CGBalance      < string UIName = "|- Night - Color Balance"; string UIWidget = "color"; > = {1.0, 1.0, 1.0};
float  Night_CGGamma        < string UIName = "|- Night - Gamma"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 5.0; float UIStep = 0.01; > = 1.0;
float  Night_CGGammaCorr    < string UIName = "|- Night - Gamma Correction"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 5.0; float UIStep = 0.01; > = 1.0;
float4 Night_CGTint         < string UIName = "|- Night - Tint"; string UIWidget = "color"; > = {1.0, 1.0, 1.0, 0.0};
UI_SPECIAL_WHITESPACE(24)
// Interior
float  Interior_CGExposure     < string UIName = "|- Interior - Exposure (in EVs)"; string UIWidget = "spinner"; float UIMin = -5.0; float UIMax = 5.0; float UIStep = 0.01; > = 0.0;
float  Interior_CGContrast     < string UIName = "|- Interior - Contrast"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 3.0; float UIStep = 0.01; > = 1.0;
float  Interior_CGContrastMid  < string UIName = "|- Interior - Contrast Middle Grey"; string UIWidget = "spinner"; float UIMin = 0.01; float UIMax = 10.0; float UIStep = 0.01; > = 1.4;
float  Interior_CGSaturation   < string UIName = "|- Interior - Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01; > = 1.0;
float3 Interior_CGBalance      < string UIName = "|- Interior - Color Balance"; string UIWidget = "color"; > = {1.0, 1.0, 1.0};
float  Interior_CGGamma        < string UIName = "|- Interior - Gamma"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 5.0; float UIStep = 0.01; > = 1.0;
float  Interior_CGGammaCorr    < string UIName = "|- Interior - Gamma Correction"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 5.0; float UIStep = 0.01; > = 1.0;
float4 Interior_CGTint         < string UIName = "|- Interior - Tint"; string UIWidget = "color"; > = {1.0, 1.0, 1.0, 0.0};


// ─────────────────── CG.AGIS — Automatic Game Image-Space ──────────────────
int _spc25 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrAGIS < string UIName = "========= AGIS (Game Image-Space) ========="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool UIAGIS_Enable       < string UIName = "|- Enable"; > = false;
bool UIAGIS_AlwaysDeltas < string UIName = "|- Always use deltas"; > = false;
// Day
float UIAGIS_Day_MinSat    < string UIName = "|- Day - Min Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 0.0;
float UIAGIS_Day_MaxSat    < string UIName = "|- Day - Max Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 1.0;
float UIAGIS_Day_MinCon    < string UIName = "|- Day - Min Contrast"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 0.0;
float UIAGIS_Day_MaxCon    < string UIName = "|- Day - Max Contrast"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 1.0;
float UIAGIS_Day_MinBri    < string UIName = "|- Day - Min Brightness"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 0.0;
float UIAGIS_Day_MaxBri    < string UIName = "|- Day - Max Brightness"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 1.0;
float UIAGIS_Day_MaxTint   < string UIName = "|- Day - Max Tint"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.0;
float UIAGIS_Day_MaxFade   < string UIName = "|- Day - Max Fade"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 1.0;
UI_SPECIAL_WHITESPACE(26)
// Night
float UIAGIS_Night_MinSat  < string UIName = "|- Night - Min Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 0.0;
float UIAGIS_Night_MaxSat  < string UIName = "|- Night - Max Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 1.0;
float UIAGIS_Night_MinCon  < string UIName = "|- Night - Min Contrast"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 0.0;
float UIAGIS_Night_MaxCon  < string UIName = "|- Night - Max Contrast"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 1.0;
float UIAGIS_Night_MinBri  < string UIName = "|- Night - Min Brightness"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 0.0;
float UIAGIS_Night_MaxBri  < string UIName = "|- Night - Max Brightness"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 1.0;
float UIAGIS_Night_MaxTint < string UIName = "|- Night - Max Tint"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.0;
float UIAGIS_Night_MaxFade < string UIName = "|- Night - Max Fade"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 1.0;
UI_SPECIAL_WHITESPACE(27)
// Interior
float UIAGIS_Int_MinSat    < string UIName = "|- Interior - Min Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 0.0;
float UIAGIS_Int_MaxSat    < string UIName = "|- Interior - Max Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 1.0;
float UIAGIS_Int_MinCon    < string UIName = "|- Interior - Min Contrast"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 0.0;
float UIAGIS_Int_MaxCon    < string UIName = "|- Interior - Max Contrast"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 1.0;
float UIAGIS_Int_MinBri    < string UIName = "|- Interior - Min Brightness"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 0.0;
float UIAGIS_Int_MaxBri    < string UIName = "|- Interior - Max Brightness"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 1.0;
float UIAGIS_Int_MaxTint   < string UIName = "|- Interior - Max Tint"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.0;
float UIAGIS_Int_MaxFade   < string UIName = "|- Interior - Max Fade"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 1.0;


// ─────────────────── Dithering ──────────────────────────────────────────────
int _spc28 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrDITH < string UIName = "========= DITHER ========="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

#define DITHER_UI_PROVIDED
bool  UID_Enable    < string UIName = "|- Enable"; > = true;
float UID_Intensity < string UIName = "|- Intensity"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 30.0; float UIStep = 0.1; > = 10.0;
float UID_Motion    < string UIName = "|- Motion"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 0.33;
bool  UID_Visualize < string UIName = "|- Visualize"; > = false;


// ─────────────────── Film Emulation ─────────────────────────────────────────
int _spc29 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrFE < string UIName = "========= FILM EMULATION ========="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

int UIFilmEmu_LogSpace     < string UIName = ":: Logarithmic color space"; string UIWidget = "spinner"; int UIMin = 0; int UIMax = 4; > = 0;
// 0=Bypass, 1=ACEScg, 2=LogC (Alexa), 3=S-Log3 (Sony), 4=DaVinci Intermediate
int UIFilmEmu_InputGamut   < string UIName = ":: Input Color Gamut"; string UIWidget = "spinner"; int UIMin = 0; int UIMax = 3; > = 0;
// 0=sRGB/Rec.709, 1=Rec.709, 2=DCI-P3, 3=ACEScg
int UIFilmEmu_OutputGamut  < string UIName = ":: Output Color Gamut"; string UIWidget = "spinner"; int UIMin = 0; int UIMax = 2; > = 0;
// 0=sRGB/Rec.709, 1=Rec.709, 2=DCI-P3


// ─────────────────── Debug Visualization ────────────────────────────────────
int _spc30 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrDBG < string UIName = "========= DEBUG VISUALIZATION ========="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool UIBloomVis < string UIName = "|- Visualize"; > = false;


//=============================================================================//
//                       ENB EXTERNAL PARAMETERS                               //
//=============================================================================//

float4 Timer;
float4 ScreenSize;
float  AdaptiveQuality;
float4 TimeOfDay1;       // x=dawn, y=sunrise, z=day, w=sunset [0..1]
float4 TimeOfDay2;       // x=dusk, y=night [0..1]
float  ENightDayFactor;   // 0=night, 1=day
float  EInteriorFactor;   // 0=exterior, 1=interior
float  FieldOfView;
float4 Weather;
float4 SunDirection;
float4 SunColor;

float4 tempF1;
float4 tempF2;
float4 tempF3;
float4 tempInfo1;
float4 tempInfo2;


//=============================================================================//
//                    SkyrimBridge External Data Parameters                     //
//                                                                             //
//  INLINE only — enbeffect.fx has ~268 UI params; including the full          //
//  SkyrimBridge.fxh (102 params) overflows ENB's constant buffer.             //
//  Only the params actually referenced by this shader are declared.           //
//=============================================================================//
#define SKYRIMBRIDGE_FXH 1   // Prevent accidental full-header inclusion

// Core: SB_IsActive() sentinel
float4 SB_Render_Frame;        // .x = frameCount, .y = deltaTime

// Reactive effects (ApplySkyrimBridgeEffects)
float4 SB_Lightning;           // .y = isFlashing, .z = flashIntensity
float4 SB_Precipitation;       // .y = intensity
float4 SB_FX_Vision;           // .x = nightEye strength

// AGIS (disabled by default — kept for future use)
float4 SB_IS_Cinematic;        // .x = sat, .y = bri, .z = con, .w = tintAlpha
float4 SB_IS_CineTint;         // .rgb = tint color

// Inline helpers
bool SB_IsActive() { return SB_Render_Frame.x > 0.0; }


//=============================================================================//
//                           GAME TEXTURES                                     //
//=============================================================================//

Texture2D TextureOriginal;      // R16G16B16A16 original game output
Texture2D TextureColor;         // Previous technique output (post-prepass/bloom)
Texture2D TextureBloom;         // Bloom result (multiple mip levels)
Texture2D TextureDepth;         // R32F depth
Texture2D TextureAdaptation;    // Eye adaptation value
Texture2D TextureLens;          // Lens dirt / flare texture

SamplerState Point_Sampler
{
    Filter   = MIN_MAG_MIP_POINT;
    AddressU = Clamp;
    AddressV = Clamp;
};

SamplerState Linear_Sampler
{
    Filter   = MIN_MAG_MIP_LINEAR;
    AddressU = Clamp;
    AddressV = Clamp;
};


//=============================================================================//
//                           HELPER INCLUDES                                   //
//=============================================================================//

#include "Helper/enbHelper_Common.fxh"
// PixelSize and ScreenRes provided by enbHelper_Common.fxh

#include "Helper/enbHelper_Dither.fxh"

#if ENABLE_TOOLS
    #include "Helper/enbHelper_Debug.fxh"
#endif


//=============================================================================//
//                                                                             //
//  TIME-OF-DAY SEPARATION — Extended 7-way interpolation                      //
//                                                                             //
//  TimeOfDay1.xyzw = dawn, sunrise, day, sunset weights [0..1]                //
//  TimeOfDay2.xy   = dusk, night weights [0..1]                               //
//  EInteriorFactor = 0 for exterior, 1 for interior (overrides all)           //
//                                                                             //
//  Extended 7-way: weighted sum across all 7 periods with interior override.  //
//  DNI 3-way: standard Day/Night/Interior from ENightDayFactor.               //
//                                                                             //
//=============================================================================//

float ExtSep7(float Dawn, float Sunrise, float Day, float Sunset,
              float Dusk, float Night, float Interior)
{
    float ext = Dawn    * TimeOfDay1.x
              + Sunrise * TimeOfDay1.y
              + Day     * TimeOfDay1.z
              + Sunset  * TimeOfDay1.w
              + Dusk    * TimeOfDay2.x
              + Night   * TimeOfDay2.y;
    return lerp(ext, Interior, EInteriorFactor);
}

float3 ExtSep7(float3 Dawn, float3 Sunrise, float3 Day, float3 Sunset,
               float3 Dusk, float3 Night, float3 Interior)
{
    float3 ext = Dawn    * TimeOfDay1.x
               + Sunrise * TimeOfDay1.y
               + Day     * TimeOfDay1.z
               + Sunset  * TimeOfDay1.w
               + Dusk    * TimeOfDay2.x
               + Night   * TimeOfDay2.y;
    return lerp(ext, Interior, EInteriorFactor);
}

float4 ExtSep7(float4 Dawn, float4 Sunrise, float4 Day, float4 Sunset,
               float4 Dusk, float4 Night, float4 Interior)
{
    float4 ext = Dawn    * TimeOfDay1.x
               + Sunrise * TimeOfDay1.y
               + Day     * TimeOfDay1.z
               + Sunset  * TimeOfDay1.w
               + Dusk    * TimeOfDay2.x
               + Night   * TimeOfDay2.y;
    return lerp(ext, Interior, EInteriorFactor);
}


//=============================================================================//
//                                                                             //
//  TONEMAPPING OPERATORS                                                      //
//                                                                             //
//  All operators map HDR [0, ∞) → LDR [0, 1].                                //
//  Scalar versions for luminance-based tonemapping.                           //
//  Float3 versions for per-channel tonemapping.                               //
//                                                                             //
//=============================================================================//

// --- 1. Reinhard Extended ---
// Classic with adjustable white point: brighter than WP maps to 1.0
float3 TM_Reinhard(float3 x, float WP)
{
    float WP2 = WP * WP;
    return x * (1.0 + x / WP2) / (1.0 + x);
}

float TM_Reinhard(float x, float WP)
{
    float WP2 = WP * WP;
    return x * (1.0 + x / WP2) / (1.0 + x);
}


// --- 2. Hejl-Burgess Filmic ---
// Single-pass filmic approximation with built-in gamma and toe
float3 TM_Hejl(float3 x, float WP)
{
    x = max(0.0, x - 0.004);
    float3 t = (x * (6.2 * x + 0.5)) / (x * (6.2 * x + 1.7) + 0.06);
    // Apply white point scaling
    float3 wp = max(0.0, WP - 0.004);
    float3 tw = (wp * (6.2 * wp + 0.5)) / (wp * (6.2 * wp + 1.7) + 0.06);
    return t / max(tw, DELTA);
}


// --- 3. Uncharted 2 / Hable Filmic ---
// 6-parameter curve: Shoulder, Linear, LinAngle, Toe, ToeNum, ToeDen, LinWhite
float3 UC2_Partial(float3 x, float A, float B, float C, float D, float E, float F)
{
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

float3 TM_UC2(float3 x, float A, float B, float C, float D, float E, float F, float W)
{
    float3 num = UC2_Partial(x, A, B, C, D, E, F);
    float3 den = UC2_Partial(W, A, B, C, D, E, F);
    return num / max(den, DELTA);
}


// --- 4. VDR — Variable Dynamic Range ---
// Middle grey input remapping with adjustable toe contrast and shoulder roll-off
float3 TM_VDR(float3 x, float MidGrey, float Toe, float Shoulder)
{
    // Remap input so MidGrey maps to 0.5
    float3 mid = max(MidGrey, DELTA);
    x = x / mid * 0.5;
    
    // Toe: power curve for shadow contrast
    float3 toed = pow(abs(x), Toe);
    
    // Shoulder: soft clamp
    return toed / (toed + pow(abs(Shoulder), Toe));
}


// --- 5. ACES — Academy Color Encoding System ---
// Stephen Hill's sRGB fit (more accurate, includes output transform)
float3 TM_ACES_Hill(float3 x)
{
    // sRGB fitted matrices from Stephen Hill
    static const float3x3 ACESin = float3x3(
        0.59719, 0.35458, 0.04823,
        0.07600, 0.90834, 0.01566,
        0.02840, 0.13383, 0.83777
    );
    static const float3x3 ACESout = float3x3(
         1.60475, -0.53108, -0.07367,
        -0.10208,  1.10813, -0.00605,
        -0.00327, -0.07276,  1.07602
    );
    
    float3 v = mul(ACESin, x);
    float3 a = v * (v + 0.0245786) - 0.000090537;
    float3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return saturate(mul(ACESout, a / b));
}

// Narkowicz approximation (simpler, cheaper)
float3 TM_ACES_Narkowicz(float3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}


// --- 6. Kitsuune Custom ---
// Parameterized S-curve with independent shoulder and toe control.
// Shoulder controls highlight compression (higher = brighter highlights).
// Toe controls shadow lift/crush (higher = lighter shadows).
float3 TM_Kitsuune(float3 x, float Shoulder, float Toe)
{
    // Toe: controls shadow region curvature
    float3 toeCurve = pow(abs(x), max(Toe, 0.01));
    
    // Shoulder: soft roll-off at highlights
    // Uses a modified rational function for smooth S-curve
    float3 shouldered = toeCurve * Shoulder / (toeCurve * (Shoulder - 1.0) + 1.0);
    
    return saturate(shouldered);
}


// --- Master Tonemapper Dispatch ---
float3 ApplyTonemap(float3 color)
{
    // Interpolate per-TOD parameters for current time
    float kitShoulder = ExtSep7(Dawn_KitShoulder, Sunrise_KitShoulder, Day_KitShoulder,
                                Sunset_KitShoulder, Dusk_KitShoulder, Night_KitShoulder,
                                Interior_KitShoulder);
    float kitToe      = ExtSep7(Dawn_KitToe, Sunrise_KitToe, Day_KitToe,
                                Sunset_KitToe, Dusk_KitToe, Night_KitToe, Interior_KitToe);
    
    float reinhardWP = ExtSep7(Dawn_ReinhardWP, Sunrise_ReinhardWP, Day_ReinhardWP,
                               Sunset_ReinhardWP, Dusk_ReinhardWP, Night_ReinhardWP,
                               Interior_ReinhardWP);
    
    float hejlWP = ExtSep7(Dawn_HejlWP, Sunrise_HejlWP, Day_HejlWP,
                            Sunset_HejlWP, Dusk_HejlWP, Night_HejlWP, Interior_HejlWP);
    
    // UC2 parameters
    float uc2A = ExtSep7(Dawn_UC2_ShoulderStr, Sunrise_UC2_ShoulderStr, Day_UC2_ShoulderStr,
                         Sunset_UC2_ShoulderStr, Dusk_UC2_ShoulderStr, Night_UC2_ShoulderStr,
                         Interior_UC2_ShoulderStr);
    float uc2B = ExtSep7(Dawn_UC2_LinStr, Sunrise_UC2_LinStr, Day_UC2_LinStr,
                         Sunset_UC2_LinStr, Dusk_UC2_LinStr, Night_UC2_LinStr,
                         Interior_UC2_LinStr);
    float uc2C = ExtSep7(Dawn_UC2_LinAngle, Sunrise_UC2_LinAngle, Day_UC2_LinAngle,
                         Sunset_UC2_LinAngle, Dusk_UC2_LinAngle, Night_UC2_LinAngle,
                         Interior_UC2_LinAngle);
    float uc2D = ExtSep7(Dawn_UC2_ToeStr, Sunrise_UC2_ToeStr, Day_UC2_ToeStr,
                         Sunset_UC2_ToeStr, Dusk_UC2_ToeStr, Night_UC2_ToeStr,
                         Interior_UC2_ToeStr);
    float uc2E = ExtSep7(Dawn_UC2_ToeNum, Sunrise_UC2_ToeNum, Day_UC2_ToeNum,
                         Sunset_UC2_ToeNum, Dusk_UC2_ToeNum, Night_UC2_ToeNum,
                         Interior_UC2_ToeNum);
    float uc2F = ExtSep7(Dawn_UC2_ToeDen, Sunrise_UC2_ToeDen, Day_UC2_ToeDen,
                         Sunset_UC2_ToeDen, Dusk_UC2_ToeDen, Night_UC2_ToeDen,
                         Interior_UC2_ToeDen);
    float uc2W = ExtSep7(Dawn_UC2_LinWhite, Sunrise_UC2_LinWhite, Day_UC2_LinWhite,
                         Sunset_UC2_LinWhite, Dusk_UC2_LinWhite, Night_UC2_LinWhite,
                         Interior_UC2_LinWhite);
    
    // VDR parameters
    float vdrMid = ExtSep7(Dawn_VDR_MidGrey, Sunrise_VDR_MidGrey, Day_VDR_MidGrey,
                           Sunset_VDR_MidGrey, Dusk_VDR_MidGrey, Night_VDR_MidGrey,
                           Interior_VDR_MidGrey);
    float vdrToe = ExtSep7(Dawn_VDR_Toe, Sunrise_VDR_Toe, Day_VDR_Toe,
                           Sunset_VDR_Toe, Dusk_VDR_Toe, Night_VDR_Toe, Interior_VDR_Toe);
    float vdrSh  = ExtSep7(Dawn_VDR_Shoulder, Sunrise_VDR_Shoulder, Day_VDR_Shoulder,
                           Sunset_VDR_Shoulder, Dusk_VDR_Shoulder, Night_VDR_Shoulder,
                           Interior_VDR_Shoulder);
    
    // Apply selected tonemapper
    float3 mapped;
    
    // Luminance-based vs per-channel
    if (UITonemapMethod == 1 && UITonemapper > 0)
    {
        // Luminance-based: tonemap luminance, reapply chroma
        float  luma = dot(color, K_LUM);
        float  mappedLuma;
        
        [branch] switch (UITonemapper)
        {
            case 1: mappedLuma = TM_Reinhard(luma, reinhardWP); break;
            case 2: mappedLuma = dot(TM_Hejl(luma, hejlWP), K_LUM); break;
            case 3: mappedLuma = dot(TM_UC2(luma, uc2A, uc2B, uc2C, uc2D, uc2E, uc2F, uc2W), K_LUM); break;
            case 4: mappedLuma = dot(TM_VDR(luma, vdrMid, vdrToe, vdrSh), K_LUM); break;
            case 5: mappedLuma = (UITonemapACESMethod == 0)
                        ? dot(TM_ACES_Hill(luma), K_LUM)
                        : dot(TM_ACES_Narkowicz(luma), K_LUM); break;
            case 6: mappedLuma = dot(TM_Kitsuune(luma, kitShoulder, kitToe), K_LUM); break;
            default: mappedLuma = luma; break;
        }
        
        // Reconstruct color preserving chrominance
        mapped = color * (mappedLuma / max(luma, DELTA));
        mapped = max(mapped, 0.0);
    }
    else
    {
        // Per-channel tonemapping
        [branch] switch (UITonemapper)
        {
            case 0:  mapped = color; break;
            case 1:  mapped = TM_Reinhard(color, reinhardWP); break;
            case 2:  mapped = TM_Hejl(color, hejlWP); break;
            case 3:  mapped = TM_UC2(color, uc2A, uc2B, uc2C, uc2D, uc2E, uc2F, uc2W); break;
            case 4:  mapped = TM_VDR(color, vdrMid, vdrToe, vdrSh); break;
            case 5:  mapped = (UITonemapACESMethod == 0)
                        ? TM_ACES_Hill(color) : TM_ACES_Narkowicz(color); break;
            case 6:  mapped = TM_Kitsuune(color, kitShoulder, kitToe); break;
            default: mapped = color; break;
        }
    }
    
    return mapped;
}


//=============================================================================//
//                                                                             //
//  COLOR GRADING                                                              //
//                                                                             //
//  Operations applied in linear light (before or after tonemap):              //
//    1. Exposure (EV shift)                                                   //
//    2. Contrast (Lottes-style pivot at configurable middle grey)              //
//    3. Saturation (luminance-preserving)                                     //
//    4. Color balance (per-channel multiplier)                                //
//    5. Gamma + gamma correction                                             //
//    6. Tint (additive color overlay with alpha blend)                        //
//                                                                             //
//=============================================================================//

float3 ApplyColorGrading(float3 color)
{
    // Interpolate all CG parameters for current TOD
    float  exposure    = ExtSep7(Dawn_CGExposure, Sunrise_CGExposure, Day_CGExposure,
                                 Sunset_CGExposure, Dusk_CGExposure, Night_CGExposure,
                                 Interior_CGExposure);
    float  contrast    = ExtSep7(Dawn_CGContrast, Sunrise_CGContrast, Day_CGContrast,
                                 Sunset_CGContrast, Dusk_CGContrast, Night_CGContrast,
                                 Interior_CGContrast);
    float  contrastMid = ExtSep7(Dawn_CGContrastMid, Sunrise_CGContrastMid, Day_CGContrastMid,
                                 Sunset_CGContrastMid, Dusk_CGContrastMid, Night_CGContrastMid,
                                 Interior_CGContrastMid);
    float  saturation  = ExtSep7(Dawn_CGSaturation, Sunrise_CGSaturation, Day_CGSaturation,
                                 Sunset_CGSaturation, Dusk_CGSaturation, Night_CGSaturation,
                                 Interior_CGSaturation);
    float3 balance     = ExtSep7(Dawn_CGBalance, Sunrise_CGBalance, Day_CGBalance,
                                 Sunset_CGBalance, Dusk_CGBalance, Night_CGBalance,
                                 Interior_CGBalance);
    float  gamma       = ExtSep7(Dawn_CGGamma, Sunrise_CGGamma, Day_CGGamma,
                                 Sunset_CGGamma, Dusk_CGGamma, Night_CGGamma,
                                 Interior_CGGamma);
    float  gammaCorr   = ExtSep7(Dawn_CGGammaCorr, Sunrise_CGGammaCorr, Day_CGGammaCorr,
                                 Sunset_CGGammaCorr, Dusk_CGGammaCorr, Night_CGGammaCorr,
                                 Interior_CGGammaCorr);
    float4 tint        = ExtSep7(Dawn_CGTint, Sunrise_CGTint, Day_CGTint,
                                 Sunset_CGTint, Dusk_CGTint, Night_CGTint,
                                 Interior_CGTint);
    
    // ── Weather Parameter Modulation ──────────────────────────────────
    // SkyrimBridge v3: per-weather color grading adjustments.
    // Multiplies/offsets the TOD-interpolated values so weather adjustments
    // stack naturally on top of time-of-day settings.
    [branch] if (SB_IsActive())
    {
        // Weather Parameters: per-weather adjustments (Phase 2 — stubbed)
        // saturation *= SB_GetWP(SB_WP_Saturation, 1.0);
        // contrast *= SB_GetWP(SB_WP_Contrast, 1.0);
        // float colorTemp = SB_GetWP(SB_WP_ColorTemp, 0.0);
        // balance *= float3(1.0 + colorTemp * 0.1, 1.0, 1.0 - colorTemp * 0.1);
    }

    // 1. Exposure (EV shift: multiply by 2^EV)
    color *= exp2(exposure);
    
    // 2. Contrast (Lottes pivot method — contrast around middle grey)
    //    log(color) - log(mid) scaled by contrast, then exp back
    //    This preserves relative brightness relationships better than
    //    the naive (color - 0.5) * contrast + 0.5 approach
    float  mid = max(contrastMid * 0.18, DELTA);  // Map middle grey param to 18% grey
    float3 logColor = log2(max(color, DELTA));
    float  logMid   = log2(mid);
    logColor = (logColor - logMid) * contrast + logMid;
    color = exp2(logColor);
    color = max(color, 0.0);
    
    // 3. Saturation (luminance-preserving)
    float luma = dot(color, K_LUM);
    color = lerp(luma, color, saturation);
    color = max(color, 0.0);
    
    // 4. Color balance (per-channel multiplier)
    color *= balance;
    
    // 5. Gamma
    //    Apply gamma curve: color^(1/gamma)
    //    GammaCorrection provides additional display gamma compensation
    float effectiveGamma = gamma * gammaCorr;
    color = pow(abs(color), rcp(max(effectiveGamma, 0.01)));
    
    // 6. Tint (additive overlay blended by alpha)
    //    tint.rgb = color, tint.a = blend strength
    color = lerp(color, color * tint.rgb, tint.a);
    
    return max(color, 0.0);
}


//=============================================================================//
//                                                                             //
//  AGIS — AUTOMATIC GAME IMAGE-SPACE                                         //
//                                                                             //
//  Reads the game's own imagespace modifiers (saturation, contrast,           //
//  brightness, tint) from SkyrimBridge and applies them as bounded            //
//  adjustments. Prevents the ENB preset from overriding game-authored         //
//  visual effects (vampire lord, hit flash, skooma, DB DLC, etc.)             //
//                                                                             //
//  "Always use deltas" mode applies the game's adjustments as relative        //
//  changes rather than absolute values, so the preset's own grading is        //
//  preserved while the game's intent is respected.                            //
//                                                                             //
//=============================================================================//

float3 ApplyAGIS(float3 color)
{
    if (!UIAGIS_Enable) return color;
    
    // Read game's imagespace state from SkyrimBridge
    float gameSat  = SB_IS_Cinematic.x;   // Game's saturation (1.0 = normal)
    float gameBri  = SB_IS_Cinematic.y;   // Game's brightness (1.0 = normal)
    float gameCon  = SB_IS_Cinematic.z;   // Game's contrast (1.0 = normal)
    float tintAlph = SB_IS_Cinematic.w;   // Game's tint alpha
    float3 gameTint = SB_IS_CineTint.rgb; // Game's tint color
    
    // Interpolate AGIS bounds per DNI
    float minSat  = DNISep(UIAGIS_Day_MinSat,  UIAGIS_Night_MinSat,  UIAGIS_Int_MinSat);
    float maxSat  = DNISep(UIAGIS_Day_MaxSat,  UIAGIS_Night_MaxSat,  UIAGIS_Int_MaxSat);
    float minCon  = DNISep(UIAGIS_Day_MinCon,  UIAGIS_Night_MinCon,  UIAGIS_Int_MinCon);
    float maxCon  = DNISep(UIAGIS_Day_MaxCon,  UIAGIS_Night_MaxCon,  UIAGIS_Int_MaxCon);
    float minBri  = DNISep(UIAGIS_Day_MinBri,  UIAGIS_Night_MinBri,  UIAGIS_Int_MinBri);
    float maxBri  = DNISep(UIAGIS_Day_MaxBri,  UIAGIS_Night_MaxBri,  UIAGIS_Int_MaxBri);
    float maxTint = DNISep(UIAGIS_Day_MaxTint, UIAGIS_Night_MaxTint, UIAGIS_Int_MaxTint);
    float maxFade = DNISep(UIAGIS_Day_MaxFade, UIAGIS_Night_MaxFade, UIAGIS_Int_MaxFade);
    
    // Clamp game values to user-defined bounds
    float sat  = clamp(gameSat, minSat, maxSat);
    float con  = clamp(gameCon, minCon, maxCon);
    float bri  = clamp(gameBri, minBri, maxBri);
    float tintStr = min(tintAlph, maxTint);
    
    if (UIAGIS_AlwaysDeltas)
    {
        // Delta mode: apply game adjustments as relative changes
        // (sat - 1.0) gives the game's deviation from neutral
        float luma = dot(color, K_LUM);
        color = lerp(luma, color, max(1.0 + (sat - 1.0), 0.0));
        
        // Contrast as deviation from 1.0
        float conDelta = con - 1.0;
        float3 logC = log2(max(color, DELTA));
        float logMid = log2(0.18);
        logC = (logC - logMid) * (1.0 + conDelta) + logMid;
        color = exp2(logC);
        
        // Brightness as multiplicative delta
        color *= bri;
    }
    else
    {
        // Absolute mode: apply game values directly
        float luma = dot(color, K_LUM);
        color = lerp(luma, color, sat);
        
        // Contrast around middle grey
        float3 logC = log2(max(color, DELTA));
        float logMid = log2(0.18);
        logC = (logC - logMid) * con + logMid;
        color = exp2(logC);
        
        color *= bri;
    }
    
    // Apply game tint (clamped to maxTint strength)
    color = lerp(color, color * gameTint, tintStr);
    
    // Apply fade (darkening toward black)
    color *= maxFade;
    
    return max(color, 0.0);
}


//=============================================================================//
//                                                                             //
//  FILM EMULATION — Gamut Transform & Log Encoding                            //
//                                                                             //
//  Converts between color gamuts (sRGB ↔ DCI-P3 ↔ ACEScg) and applies        //
//  log encoding curves used in professional color grading workflows.          //
//  Enables compatibility with external LUT pipelines (DaVinci Resolve, etc.)  //
//                                                                             //
//=============================================================================//

// --- Gamut Transform Matrices ---

// sRGB/Rec.709 → ACEScg (AP1)
static const float3x3 MAT_709_to_AP1 = float3x3(
    0.6131, 0.3395, 0.0474,
    0.0701, 0.9164, 0.0135,
    0.0206, 0.1096, 0.8698
);

// ACEScg → sRGB/Rec.709
static const float3x3 MAT_AP1_to_709 = float3x3(
     1.7051, -0.6218, -0.0833,
    -0.1302,  1.1408, -0.0106,
    -0.0240, -0.1290,  1.1530
);

// sRGB → DCI-P3
static const float3x3 MAT_709_to_P3 = float3x3(
    0.8225, 0.1774, 0.0001,
    0.0332, 0.9669, 0.0000,
    0.0171, 0.0724, 0.9106
);

// DCI-P3 → sRGB
static const float3x3 MAT_P3_to_709 = float3x3(
     1.2249, -0.2250,  0.0001,
    -0.0420,  1.0421, -0.0001,
    -0.0197, -0.0786,  1.0984
);

// ACEScg → DCI-P3
static const float3x3 MAT_AP1_to_P3 = float3x3(
     1.3793, -0.3094, -0.0699,
    -0.0694,  1.0822, -0.0128,
    -0.0022, -0.0465,  1.0487
);

// DCI-P3 → ACEScg
static const float3x3 MAT_P3_to_AP1 = float3x3(
    0.7378, 0.2163, 0.0459,
    0.0475, 0.9382, 0.0143,
    0.0012, 0.0416, 0.9572
);


// --- Log Encoding Curves ---

// ACES linear → ACEScg log
float3 LogEncode_ACEScg(float3 x)
{
    // ACEScg uses a simple log2-based encoding
    return (log2(max(x, DELTA)) + 9.72) / 17.52;
}

// Alexa LogC (EI 800)
float3 LogEncode_LogC(float3 x)
{
    // ARRI LogC curve (simplified EI 800)
    const float cut   = 0.010591;
    const float a     = 5.555556;
    const float b     = 0.052272;
    const float c     = 0.247190;
    const float d     = 0.385537;
    const float e     = 5.367655;
    const float f     = 0.092809;
    
    float3 result;
    result = (x > cut) ? c * log10(max(a * x + b, DELTA)) + d
                       : e * x + f;
    return result;
}

// Sony S-Log3
float3 LogEncode_SLog3(float3 x)
{
    const float t = 0.01125000;
    float3 result;
    result = (x >= t) ? (420.0 + log10(max((x + 0.01) / (0.18 + 0.01), DELTA)) * 261.5) / 1023.0
                      : (x * (171.2102946929 - 95.0) / 0.01125000 + 95.0) / 1023.0;
    return result;
}

// DaVinci Intermediate
float3 LogEncode_DaVinci(float3 x)
{
    // DaVinci Intermediate log curve
    const float A = 0.0075;
    const float B = 7.0;
    const float C = 0.07329248;
    const float M = 10.44426855;
    const float cutLin = 0.00262409;
    const float cutLog = 0.02740668;
    
    float3 result;
    result = (x > cutLin) ? (log2(x + A) + B) * C
                          : x * M;
    return saturate(result);
}


// --- Apply Film Emulation Pipeline ---
float3 ApplyFilmEmulation(float3 color)
{
    // 1. Input gamut transform
    [branch] switch (UIFilmEmu_InputGamut)
    {
        case 2:  color = mul(MAT_P3_to_709, color); break;      // DCI-P3 input → sRGB working
        case 3:  color = mul(MAT_AP1_to_709, color); break;     // ACEScg input → sRGB working
        default: break;                                          // sRGB/Rec.709 passthrough
    }
    
    // 2. Log encoding (if enabled)
    [branch] if (UIFilmEmu_LogSpace > 0)
    {
        switch (UIFilmEmu_LogSpace)
        {
            case 1: color = LogEncode_ACEScg(color); break;
            case 2: color = LogEncode_LogC(color); break;
            case 3: color = LogEncode_SLog3(color); break;
            case 4: color = LogEncode_DaVinci(color); break;
        }
    }
    
    // 3. Output gamut transform
    [branch] switch (UIFilmEmu_OutputGamut)
    {
        case 2:  color = mul(MAT_709_to_P3, color); break;      // sRGB → DCI-P3 output
        default: break;                                          // sRGB/Rec.709 output
    }
    
    return color;
}


//=============================================================================//
//                                                                             //
//  BLOOM MIXING & BLENDING                                                    //
//                                                                             //
//  Two modes for integrating bloom into the scene:                            //
//                                                                             //
//  Additive Mix (default):                                                    //
//    Bloom is processed (contrast/saturation/balance), then added to scene.   //
//    Contrast uses Lottes pivot at configurable middle grey.                   //
//                                                                             //
//  Linear Blend (alternative):                                                //
//    Screen-blend mode: luminance-based mixing that prevents blow-out.        //
//    Produces softer, more photographic bloom integration.                    //
//                                                                             //
//=============================================================================//

float3 ProcessBloom(float3 bloom)
{
    // Interpolate bloom mixing parameters for current TOD
    float  intensity   = ExtSep7(Dawn_BloomIntensity, Sunrise_BloomIntensity, Day_BloomIntensity,
                                 Sunset_BloomIntensity, Dusk_BloomIntensity, Night_BloomIntensity,
                                 Interior_BloomIntensity);
    float  bContrast   = ExtSep7(Dawn_BloomContrast, Sunrise_BloomContrast, Day_BloomContrast,
                                 Sunset_BloomContrast, Dusk_BloomContrast, Night_BloomContrast,
                                 Interior_BloomContrast);
    float  bContMid    = ExtSep7(Dawn_BloomContrastMid, Sunrise_BloomContrastMid, Day_BloomContrastMid,
                                 Sunset_BloomContrastMid, Dusk_BloomContrastMid, Night_BloomContrastMid,
                                 Interior_BloomContrastMid);
    float  bSaturation = ExtSep7(Dawn_BloomSaturation, Sunrise_BloomSaturation, Day_BloomSaturation,
                                 Sunset_BloomSaturation, Dusk_BloomSaturation, Night_BloomSaturation,
                                 Interior_BloomSaturation);
    float3 bBalance    = ExtSep7(Dawn_BloomBalance, Sunrise_BloomBalance, Day_BloomBalance,
                                 Sunset_BloomBalance, Dusk_BloomBalance, Night_BloomBalance,
                                 Interior_BloomBalance);
    
    // Contrast (pivot at middle grey)
    float mid = max(bContMid * 0.18, DELTA);
    float3 logBloom = log2(max(bloom, DELTA));
    float  logMid   = log2(mid);
    logBloom = (logBloom - logMid) * bContrast + logMid;
    bloom = max(exp2(logBloom), 0.0);
    
    // Saturation
    float bloomLuma = dot(bloom, K_LUM);
    bloom = lerp(bloomLuma, bloom, bSaturation);
    bloom = max(bloom, 0.0);
    
    // Color balance & intensity
    bloom *= bBalance * intensity;
    
    return bloom;
}

float3 MixBloom(float3 scene, float3 bloom)
{
    if (!UIBloomMix_Enable) return scene;
    
    float3 processedBloom = ProcessBloom(bloom);
    
    // Additive mixing
    float3 result = scene + processedBloom;
    
    // Optional linear blend mode (screen blend)
    [branch] if (UIBloomBlend_Enable)
    {
        float blendAmt = DNISep(Day_BlendAmount, Night_BlendAmount, Interior_BlendAmount);
        float blendWt  = DNISep(Day_BlendWeight, Night_BlendWeight, Interior_BlendWeight);
        
        // Screen blend: 1 - (1-a)(1-b)
        float3 screenBlend = 1.0 - (1.0 - scene) * (1.0 - processedBloom * blendAmt);
        
        // Blend between additive and screen modes
        result = lerp(result, screenBlend, blendWt);
    }
    
    return max(result, 0.0);
}


//=============================================================================//
//                                                                             //
//  SKYRIMBRIDGE REACTIVE EFFECTS                                              //
//                                                                             //
//  Weather-reactive adjustments applied before the main pipeline:             //
//    - Lightning flash → momentary exposure boost                              //
//    - Rain/snow → atmospheric desaturation                                   //
//    - Night Eye → exposure compensation                                      //
//                                                                             //
//=============================================================================//

float3 ApplySkyrimBridgeEffects(float3 color)
{
    [branch] if (!SB_IsActive()) return color;
    
    // Lightning flash: momentary exposure spike (saturate for safety)
    float flashIntensity = saturate(SB_Lightning.z) * saturate(SB_Lightning.y);
    if (flashIntensity > 0.01)
    {
        float flashEV = flashIntensity * 0.5;  // Up to +0.5 EV during flash
        color *= exp2(flashEV);
    }

    // Precipitation: atmospheric desaturation (saturate for safety)
    float precipIntensity = saturate(SB_Precipitation.y);
    if (precipIntensity > 0.01)
    {
        float desat = precipIntensity * 0.1;  // Up to 10% desaturation in heavy rain
        float luma = dot(color, K_LUM);
        color = lerp(color, luma, desat);
    }

    // Night Eye: exposure adaptation boost (saturate for safety)
    float nightEye = saturate(SB_FX_Vision.x);
    if (nightEye > 0.01)
    {
        color *= exp2(nightEye * 0.8);  // Brighten the scene
        // Shift toward blue/green for night vision appearance
        color.g *= 1.0 + nightEye * 0.15;
        color.b *= 1.0 + nightEye * 0.1;
    }
    
    return color;
}


//=============================================================================//
//                                                                             //
//  DEBUG OVERLAYS (guarded behind ENABLE_TOOLS)                               //
//                                                                             //
//=============================================================================//

#if ENABLE_TOOLS
float3 ApplyDebugOverlays(float3 color, float2 UV, float3 bloom)
{
    if (!UIDebugTools) return color;
    
    // Bloom visualization: show isolated bloom buffer
    if (UIBloomVis)
    {
        return bloom;
    }
    
    // Tonemapper curve plot (bottom-right quadrant)
    if (UV.x > 0.75 && UV.y > 0.75)
    {
        float2 plotUV = (UV - float2(0.75, 0.75)) * 4.0;
        float  inputVal = plotUV.x * 4.0;  // Input range [0, 4]
        float  plotY = 1.0 - plotUV.y;     // Flip Y for plot
        
        // Evaluate current tonemapper at this input
        float3 testColor = inputVal;
        float3 mapped = ApplyTonemap(testColor);
        float  mapLuma = dot(mapped, K_LUM);
        
        // Draw curve line
        float dist = abs(plotY - mapLuma);
        float line = smoothstep(0.015, 0.005, dist);
        
        // Draw grid lines
        float gridH = smoothstep(0.003, 0.001, min(frac(plotUV.x * 4.0), frac(plotUV.y * 4.0)));
        
        color = lerp(color * 0.2 + gridH * 0.05, float3(1.0, 0.5, 0.1), line);
    }
    
    return color;
}
#endif


//=============================================================================//
//                       VERTEX & PIXEL SHADERS                                //
//=============================================================================//

void VS_Effect(inout float4 pos : SV_POSITION, inout float2 txcoord : TEXCOORD0)
{
    pos.w = 1.0;
}


float4 PS_Effect(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float2 UV = txcoord.xy;
    
    // Sample scene and bloom
    float3 color = TextureColor.Sample(Point_Sampler, UV).rgb;
    float3 bloom = TextureBloom.Sample(Linear_Sampler, UV).rgb;

    // Preserve original alpha
    float alpha = TextureColor.Sample(Point_Sampler, UV).a;
    
    
    //=== SkyrimBridge reactive effects (pre-pipeline) ===//
    color = ApplySkyrimBridgeEffects(color);
    
    
    //=== Color Pipeline ===//
    [branch] if (UIColorPipeline == 0)
    {
        // Pipeline 0: Standard
        // 1. Bloom mixing
        color = MixBloom(color, bloom);
        
        // 2. Tonemapping
        color = ApplyTonemap(color);
        
        // 3. Color grading
        color = ApplyColorGrading(color);
    }
    else
    {
        // Pipeline 1: Pre-Grade
        // 1. Bloom mixing
        color = MixBloom(color, bloom);
        
        // 2. Color grading (in HDR, before tonemap)
        color = ApplyColorGrading(color);
        
        // 3. Tonemapping
        color = ApplyTonemap(color);
    }
    
    
    //=== AGIS (Automatic Game Image-Space) ===//
    color = ApplyAGIS(color);
    
    
    //=== Film Emulation ===//
    [branch] if (UIFilmEmu_LogSpace > 0 || UIFilmEmu_InputGamut > 0 || UIFilmEmu_OutputGamut > 0)
    {
        color = ApplyFilmEmulation(color);
    }
    
    
    //=== Dithering ===//
    [branch] if (UID_Enable)
    {
        GaussBlueDither(color, pos.xy, 8);
    }
    
    
    //=== Debug overlays ===//
    #if ENABLE_TOOLS
        color = ApplyDebugOverlays(color, UV, bloom);
    #endif
    
    
    return float4(max(color, 0.0), alpha);
}


//=============================================================================//
//                              TECHNIQUE                                      //
//=============================================================================//

technique11 Draw <string UIName = "Silent Horizons";>
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Effect()));
        SetPixelShader (CompileShader(ps_5_0, PS_Effect()));
    }
}
