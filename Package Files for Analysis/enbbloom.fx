//----------------------------------------------------------------------------------------------//
//                                                                                              //
//               enbbloom.fx - Physically-Based Multi-Pass Progressive Bloom                    //
//                   for Skyrim SE ENB (DirectX 11 Shader Model 5)                              //
//                                                                                              //
//         Original framework by LonelyKitsuune / T.Thanner - CC BY-NC-ND 4.0                  //
//                            Boris Vorontsov for ENBSeries                                     //
//                                                                                              //
//----------------------------------------------------------------------------------------------//
//                                                                                              //
//     Physically-motivated bloom pipeline by Zain Dana Harper - February 2026                  //
//                                                                                              //
//  Architecture: 1 technique, called once per bloom pass by ENB                                //
//                                                                                              //
//    Pass 0 (Full → Half): Luminance threshold with soft knee + chromatic                      //
//           pre-filter.  Depth mask splits scene into Near / Far / Sky                         //
//           layers with independent threshold & intensity.                                     //
//                                                                                              //
//    Pass 1-6 (Progressive Downsample/Blur): Dual-filter Kawase blur at                        //
//           each mip level.  Per-pass RGBA balance weights control the                         //
//           frequency contribution of each octave.                                             //
//                                                                                              //
//    Final Composite (in enbeffect.fx): Passes are summed with per-TOD                         //
//           balance weights → bloom texture fed to the main color pass.                        //
//                                                                                              //
//  Threshold Model: Soft-Knee (Karis 2014)                                                    //
//    - Smooth transition around threshold via quadratic knee curve                             //
//    - Prevents hard clipping artifacts and bloom "pop"                                        //
//    - Feathering parameter controls transition width                                          //
//                                                                                              //
//  Blur Model: Dual Kawase (Marius Bjorge, ARM 2015)                                          //
//    - Bilinear tap offsets for 4× sample efficiency vs Gaussian                               //
//    - Progressive downsample/upsample for wide radius at low cost                            //
//    - ENB handles the mip chain; we modify each level                                        //
//                                                                                              //
//  Depth Separation:                                                                           //
//    - Near layer: foreground objects (characters, items) → tighter bloom                      //
//    - Far layer: distant landscape → wider, softer bloom                                      //
//    - Sky layer: sky dome / emissives → controlled separately                                 //
//    - Smooth depth curve interpolation between layers                                        //
//                                                                                              //
//  SkyrimBridge Integration:                                                                   //
//    - Lightning flash → momentary bloom spike                                                //
//    - Rain/snow → bloom dampening (atmospheric scatter absorbs glow)                         //
//    - Fog density → bloom softening (scattering broadens PSF)                                //
//    - Nearby lights → point light bloom contribution                                          //
//    - Game IS_HDR.bloomScale → respect engine's bloom intent                                  //
//                                                                                              //
//  References:                                                                                //
//    [1] Karis, "Real Shading in Unreal Engine 4", SIGGRAPH 2013                              //
//    [2] Bjorge, "Bandwidth-Efficient Rendering", GDC/ARM 2015                                //
//    [3] Jimenez, "Next Generation Post Processing in CoD:AW", SIGGRAPH 2014                  //
//    [4] Pettineo, "Tone Mapping", MJP blog, 2012                                            //
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
UI_FileHeader("  SILENT HORIZONS — BLOOM")


//=============================================================================//
//                           UI PARAMETERS (INLINE)                            //
//=============================================================================//

// ═══════════════════════ DEVELOPER TOOLS ═══════════════════════

bool  UIDebug_Enable < string UIName = "Debug | Enable Overlay"; > = false;
bool  UIDebug_ShowThreshold < string UIName = "Debug | Show Threshold Mask"; > = false;
bool  UIDebug_ShowDepthLayers < string UIName = "Debug | Show Depth Layers"; > = false;
bool  UIDebug_ShowPassIndex < string UIName = "Debug | Show Pass Index"; > = false;

UI_SPECIAL_WHITESPACE(1)


// ═══════════════════════ GLOBALS ═══════════════════════════════

int   UIGlobal_ColorPipeline < string UIName = "Globals | Color Pipeline";
        string UIWidget = "spinner"; int UIMin = 0; int UIMax = 1; > = 0;

int   UIGlobal_WeatherSep < string UIName = "Bloom | Weather Separation";
        string UIWidget = "spinner"; int UIMin = 0; int UIMax = 1; > = 0;
        // 0 = DNI (3-way: Day/Night/Interior)
        // 1 = Extended (7-way: Dawn/Sunrise/Day/Sunset/Dusk/Night/Interior)

bool  UIGlobal_DepthTweaking < string UIName = "Bloom | Depth Tweaking"; > = false;
        // Enable separate Near/Far/Sky bloom layers

bool  UIGlobal_BorderCorrect < string UIName = "Bloom | Border Correction"; > = true;
        // Fix darkened edges from blur kernel clipping

int   UIGlobal_PassCount < string UIName = "Bloom | Pass Count";
        string UIWidget = "spinner"; int UIMin = 1; int UIMax = 7; > = 4;

UI_SPECIAL_WHITESPACE(2)


// ═══════════════════════ PER-TOD BLOOM CONTROLS ═══════════════
// Dawn / Sunrise / Day / Sunset / Dusk / Night / Interior
// Each period: Intensity, Threshold, Feathering, Saturation, Scale

// --- Dawn ---
int _spcDawn < string UIName = "--- Dawn ---"; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
float UIDawn_Intensity  < string UIName = "Dawn | Intensity";   string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 20.0;  float UIStep = 0.1;  > = 3.0;
float UIDawn_Threshold  < string UIName = "Dawn | Threshold";   string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 20.0;  float UIStep = 0.1;  > = 3.0;
float UIDawn_Feathering < string UIName = "Dawn | Feathering";  string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 5.0;   float UIStep = 0.01; > = 1.4;
float UIDawn_Saturation < string UIName = "Dawn | Saturation";  string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 3.0;   float UIStep = 0.01; > = 1.0;
float UIDawn_Scale      < string UIName = "Dawn | Scale";       string UIWidget = "spinner"; float UIMin = 0.5;  float UIMax = 8.0;   float UIStep = 0.1;  > = 1.5;

// --- Sunrise ---
int _spcSunrise < string UIName = "--- Sunrise ---"; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
float UISunrise_Intensity  < string UIName = "Sunrise | Intensity";   string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 20.0;  float UIStep = 0.1;  > = 1.5;
float UISunrise_Threshold  < string UIName = "Sunrise | Threshold";   string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 20.0;  float UIStep = 0.1;  > = 1.5;
float UISunrise_Feathering < string UIName = "Sunrise | Feathering";  string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 5.0;   float UIStep = 0.01; > = 0.7;
float UISunrise_Saturation < string UIName = "Sunrise | Saturation";  string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 3.0;   float UIStep = 0.01; > = 1.0;
float UISunrise_Scale      < string UIName = "Sunrise | Scale";       string UIWidget = "spinner"; float UIMin = 0.5;  float UIMax = 8.0;   float UIStep = 0.1;  > = 1.5;

// --- Day ---
int _spcDay < string UIName = "--- Day ---"; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
float UIDay_Intensity  < string UIName = "Day | Intensity";   string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 20.0;  float UIStep = 0.1;  > = 1.5;
float UIDay_Threshold  < string UIName = "Day | Threshold";   string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 20.0;  float UIStep = 0.1;  > = 1.5;
float UIDay_Feathering < string UIName = "Day | Feathering";  string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 5.0;   float UIStep = 0.01; > = 0.7;
float UIDay_Saturation < string UIName = "Day | Saturation";  string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 3.0;   float UIStep = 0.01; > = 1.0;
float UIDay_Scale      < string UIName = "Day | Scale";       string UIWidget = "spinner"; float UIMin = 0.5;  float UIMax = 8.0;   float UIStep = 0.1;  > = 1.5;

// --- Sunset ---
int _spcSunset < string UIName = "--- Sunset ---"; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
float UISunset_Intensity  < string UIName = "Sunset | Intensity";   string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 20.0;  float UIStep = 0.1;  > = 1.5;
float UISunset_Threshold  < string UIName = "Sunset | Threshold";   string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 20.0;  float UIStep = 0.1;  > = 1.5;
float UISunset_Feathering < string UIName = "Sunset | Feathering";  string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 5.0;   float UIStep = 0.01; > = 0.7;
float UISunset_Saturation < string UIName = "Sunset | Saturation";  string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 3.0;   float UIStep = 0.01; > = 1.0;
float UISunset_Scale      < string UIName = "Sunset | Scale";       string UIWidget = "spinner"; float UIMin = 0.5;  float UIMax = 8.0;   float UIStep = 0.1;  > = 1.5;

// --- Dusk ---
int _spcDusk < string UIName = "--- Dusk ---"; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
float UIDusk_Intensity  < string UIName = "Dusk | Intensity";   string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 20.0;  float UIStep = 0.1;  > = 3.0;
float UIDusk_Threshold  < string UIName = "Dusk | Threshold";   string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 30.0;  float UIStep = 0.1;  > = 3.0;
float UIDusk_Feathering < string UIName = "Dusk | Feathering";  string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 5.0;   float UIStep = 0.01; > = 1.4;
float UIDusk_Saturation < string UIName = "Dusk | Saturation";  string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 3.0;   float UIStep = 0.01; > = 1.0;
float UIDusk_Scale      < string UIName = "Dusk | Scale";       string UIWidget = "spinner"; float UIMin = 0.5;  float UIMax = 8.0;   float UIStep = 0.1;  > = 1.5;

// --- Night ---
int _spcNight < string UIName = "--- Night ---"; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
float UINight_Intensity  < string UIName = "Night | Intensity";   string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 100.0; float UIStep = 0.5;  > = 3.0;
float UINight_Threshold  < string UIName = "Night | Threshold";   string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 30.0;  float UIStep = 0.1;  > = 3.0;
float UINight_Feathering < string UIName = "Night | Feathering";  string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 5.0;   float UIStep = 0.01; > = 2.0;
float UINight_Saturation < string UIName = "Night | Saturation";  string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 3.0;   float UIStep = 0.01; > = 1.0;
float UINight_Scale      < string UIName = "Night | Scale";       string UIWidget = "spinner"; float UIMin = 0.5;  float UIMax = 8.0;   float UIStep = 0.1;  > = 1.5;

// --- Interior ---
int _spcInterior < string UIName = "--- Interior ---"; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
float UIInterior_Intensity  < string UIName = "Interior | Intensity";   string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 100.0; float UIStep = 0.5;  > = 3.0;
float UIInterior_Threshold  < string UIName = "Interior | Threshold";   string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 30.0;  float UIStep = 0.1;  > = 2.6;
float UIInterior_Feathering < string UIName = "Interior | Feathering";  string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 5.0;   float UIStep = 0.01; > = 2.0;
float UIInterior_Saturation < string UIName = "Interior | Saturation";  string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 3.0;   float UIStep = 0.01; > = 1.0;
float UIInterior_Scale      < string UIName = "Interior | Scale";       string UIWidget = "spinner"; float UIMin = 0.5;  float UIMax = 8.0;   float UIStep = 0.1;  > = 1.5;

UI_SPECIAL_WHITESPACE(3)


// ═══════════════════════ PASS BALANCING ════════════════════════
// Per-pass RGBA contribution weights per TOD.
// Each float4: (R, G, B, A) — controls per-channel bloom contribution
// of that frequency octave. Higher pass = wider/softer bloom.

int _spcPassBal < string UIName = "=== Pass Balancing ==="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

// Dawn
float4 UIDawn_Pass1 < string UIName = "Dawn | Pass 1 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UIDawn_Pass2 < string UIName = "Dawn | Pass 2 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UIDawn_Pass3 < string UIName = "Dawn | Pass 3 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UIDawn_Pass4 < string UIName = "Dawn | Pass 4 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UIDawn_Pass5 < string UIName = "Dawn | Pass 5 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UIDawn_Pass6 < string UIName = "Dawn | Pass 6 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UIDawn_Pass7 < string UIName = "Dawn | Pass 7 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};

// Sunrise
float4 UISunrise_Pass1 < string UIName = "Sunrise | Pass 1 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UISunrise_Pass2 < string UIName = "Sunrise | Pass 2 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UISunrise_Pass3 < string UIName = "Sunrise | Pass 3 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UISunrise_Pass4 < string UIName = "Sunrise | Pass 4 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UISunrise_Pass5 < string UIName = "Sunrise | Pass 5 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UISunrise_Pass6 < string UIName = "Sunrise | Pass 6 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UISunrise_Pass7 < string UIName = "Sunrise | Pass 7 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};

// Day
float4 UIDay_Pass1 < string UIName = "Day | Pass 1 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UIDay_Pass2 < string UIName = "Day | Pass 2 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UIDay_Pass3 < string UIName = "Day | Pass 3 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UIDay_Pass4 < string UIName = "Day | Pass 4 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UIDay_Pass5 < string UIName = "Day | Pass 5 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UIDay_Pass6 < string UIName = "Day | Pass 6 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UIDay_Pass7 < string UIName = "Day | Pass 7 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};

// Sunset
float4 UISunset_Pass1 < string UIName = "Sunset | Pass 1 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UISunset_Pass2 < string UIName = "Sunset | Pass 2 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UISunset_Pass3 < string UIName = "Sunset | Pass 3 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UISunset_Pass4 < string UIName = "Sunset | Pass 4 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UISunset_Pass5 < string UIName = "Sunset | Pass 5 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UISunset_Pass6 < string UIName = "Sunset | Pass 6 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UISunset_Pass7 < string UIName = "Sunset | Pass 7 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};

// Dusk
float4 UIDusk_Pass1 < string UIName = "Dusk | Pass 1 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UIDusk_Pass2 < string UIName = "Dusk | Pass 2 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UIDusk_Pass3 < string UIName = "Dusk | Pass 3 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UIDusk_Pass4 < string UIName = "Dusk | Pass 4 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UIDusk_Pass5 < string UIName = "Dusk | Pass 5 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UIDusk_Pass6 < string UIName = "Dusk | Pass 6 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UIDusk_Pass7 < string UIName = "Dusk | Pass 7 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};

// Night
float4 UINight_Pass1 < string UIName = "Night | Pass 1 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UINight_Pass2 < string UIName = "Night | Pass 2 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UINight_Pass3 < string UIName = "Night | Pass 3 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UINight_Pass4 < string UIName = "Night | Pass 4 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UINight_Pass5 < string UIName = "Night | Pass 5 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UINight_Pass6 < string UIName = "Night | Pass 6 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UINight_Pass7 < string UIName = "Night | Pass 7 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};

// Interior
float4 UIInterior_Pass1 < string UIName = "Interior | Pass 1 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UIInterior_Pass2 < string UIName = "Interior | Pass 2 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UIInterior_Pass3 < string UIName = "Interior | Pass 3 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UIInterior_Pass4 < string UIName = "Interior | Pass 4 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UIInterior_Pass5 < string UIName = "Interior | Pass 5 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UIInterior_Pass6 < string UIName = "Interior | Pass 6 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};
float4 UIInterior_Pass7 < string UIName = "Interior | Pass 7 Balance"; > = {0.00392, 0.00392, 0.00392, 0.00392};

UI_SPECIAL_WHITESPACE(4)


// ═══════════════════════ DEPTH LAYER CONTROLS ═════════════════
// Only active when Depth Tweaking is enabled.
// Near = foreground, Far = background, Sky = sky dome / distant emissives

int _spcDepth < string UIName = "=== Depth Layers (req. Depth Tweaking) ==="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

float UIDepth_NearDist < string UIName = "Depth | Near Distance";  string UIWidget = "spinner"; float UIMin = 0.001; float UIMax = 1.0; float UIStep = 0.01; > = 0.1;
float UIDepth_FarDist  < string UIName = "Depth | Far Distance";   string UIWidget = "spinner"; float UIMin = 0.01;  float UIMax = 1.0; float UIStep = 0.01; > = 1.0;
float UIDepth_Curve    < string UIName = "Depth | Blend Curve";    string UIWidget = "spinner"; float UIMin = 0.1;   float UIMax = 4.0; float UIStep = 0.1;  > = 1.0;

// Per-TOD depth layer overrides (Near/Far/Sky)
// Dawn Near
float UIDawn_NearInt   < string UIName = "Dawn Near | Intensity";   string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 100.0; float UIStep = 0.1;  > = 6.0;
float UIDawn_NearThr   < string UIName = "Dawn Near | Threshold";   string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 30.0;  float UIStep = 0.1;  > = 5.0;
float UIDawn_NearFea   < string UIName = "Dawn Near | Feathering";  string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 5.0;   float UIStep = 0.01; > = 0.65;
float UIDawn_NearSat   < string UIName = "Dawn Near | Saturation";  string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 3.0;   float UIStep = 0.01; > = 1.0;
float UIDawn_NearScl   < string UIName = "Dawn Near | Scale";       string UIWidget = "spinner"; float UIMin = 0.5;  float UIMax = 8.0;   float UIStep = 0.1;  > = 1.6;
// Dawn Far
float UIDawn_FarInt    < string UIName = "Dawn Far | Intensity";    string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 100.0; float UIStep = 0.1;  > = 0.0;
float UIDawn_FarThr    < string UIName = "Dawn Far | Threshold";    string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 30.0;  float UIStep = 0.1;  > = 2.2;
float UIDawn_FarFea    < string UIName = "Dawn Far | Feathering";   string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 5.0;   float UIStep = 0.01; > = 0.0;
float UIDawn_FarSat    < string UIName = "Dawn Far | Saturation";   string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 3.0;   float UIStep = 0.01; > = 2.33;
float UIDawn_FarScl    < string UIName = "Dawn Far | Scale";        string UIWidget = "spinner"; float UIMin = 0.5;  float UIMax = 8.0;   float UIStep = 0.1;  > = 3.77;
// Dawn Sky
float UIDawn_SkyInt    < string UIName = "Dawn Sky | Intensity";    string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 100.0; float UIStep = 0.1;  > = 0.0;
float UIDawn_SkyThr    < string UIName = "Dawn Sky | Threshold";    string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 30.0;  float UIStep = 0.1;  > = 6.45;
float UIDawn_SkyFea    < string UIName = "Dawn Sky | Feathering";   string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 5.0;   float UIStep = 0.01; > = 2.35;
float UIDawn_SkySat    < string UIName = "Dawn Sky | Saturation";   string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 3.0;   float UIStep = 0.01; > = 1.4;
float UIDawn_SkyScl    < string UIName = "Dawn Sky | Scale";        string UIWidget = "spinner"; float UIMin = 0.5;  float UIMax = 8.0;   float UIStep = 0.1;  > = 1.75;

// Sunrise Near/Far/Sky
float UISunrise_NearInt < string UIName = "Sunrise Near | Intensity"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 100.0; float UIStep = 0.1; > = 6.0;
float UISunrise_NearThr < string UIName = "Sunrise Near | Threshold"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 30.0;  float UIStep = 0.1; > = 5.0;
float UISunrise_NearFea < string UIName = "Sunrise Near | Feathering"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0;  float UIStep = 0.01; > = 0.65;
float UISunrise_NearSat < string UIName = "Sunrise Near | Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 3.0;  float UIStep = 0.01; > = 1.0;
float UISunrise_NearScl < string UIName = "Sunrise Near | Scale";     string UIWidget = "spinner"; float UIMin = 0.5; float UIMax = 8.0;   float UIStep = 0.1; > = 1.6;
float UISunrise_FarInt  < string UIName = "Sunrise Far | Intensity";  string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 100.0; float UIStep = 0.1; > = 0.0;
float UISunrise_FarThr  < string UIName = "Sunrise Far | Threshold";  string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 30.0;  float UIStep = 0.1; > = 2.2;
float UISunrise_FarFea  < string UIName = "Sunrise Far | Feathering"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0;   float UIStep = 0.01; > = 0.0;
float UISunrise_FarSat  < string UIName = "Sunrise Far | Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 3.0;   float UIStep = 0.01; > = 1.0;
float UISunrise_FarScl  < string UIName = "Sunrise Far | Scale";      string UIWidget = "spinner"; float UIMin = 0.5; float UIMax = 8.0;   float UIStep = 0.1; > = 3.77;
float UISunrise_SkyInt  < string UIName = "Sunrise Sky | Intensity";  string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 100.0; float UIStep = 0.1; > = 0.0;
float UISunrise_SkyThr  < string UIName = "Sunrise Sky | Threshold";  string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 30.0;  float UIStep = 0.1; > = 6.45;
float UISunrise_SkyFea  < string UIName = "Sunrise Sky | Feathering"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0;   float UIStep = 0.01; > = 2.35;
float UISunrise_SkySat  < string UIName = "Sunrise Sky | Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 3.0;   float UIStep = 0.01; > = 1.0;
float UISunrise_SkyScl  < string UIName = "Sunrise Sky | Scale";      string UIWidget = "spinner"; float UIMin = 0.5; float UIMax = 8.0;   float UIStep = 0.1; > = 1.75;

// Day Near/Far/Sky
float UIDay_NearInt < string UIName = "Day Near | Intensity"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 100.0; float UIStep = 0.1; > = 6.0;
float UIDay_NearThr < string UIName = "Day Near | Threshold"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 30.0;  float UIStep = 0.1; > = 5.0;
float UIDay_NearFea < string UIName = "Day Near | Feathering"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0;  float UIStep = 0.01; > = 0.65;
float UIDay_NearSat < string UIName = "Day Near | Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 3.0;  float UIStep = 0.01; > = 1.0;
float UIDay_NearScl < string UIName = "Day Near | Scale";     string UIWidget = "spinner"; float UIMin = 0.5; float UIMax = 8.0;   float UIStep = 0.1; > = 1.6;
float UIDay_FarInt  < string UIName = "Day Far | Intensity";  string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 100.0; float UIStep = 0.1; > = 0.0;
float UIDay_FarThr  < string UIName = "Day Far | Threshold";  string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 30.0;  float UIStep = 0.1; > = 2.2;
float UIDay_FarFea  < string UIName = "Day Far | Feathering"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0;   float UIStep = 0.01; > = 0.0;
float UIDay_FarSat  < string UIName = "Day Far | Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 3.0;   float UIStep = 0.01; > = 1.0;
float UIDay_FarScl  < string UIName = "Day Far | Scale";      string UIWidget = "spinner"; float UIMin = 0.5; float UIMax = 8.0;   float UIStep = 0.1; > = 3.77;
float UIDay_SkyInt  < string UIName = "Day Sky | Intensity";  string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 100.0; float UIStep = 0.1; > = 13.16;
float UIDay_SkyThr  < string UIName = "Day Sky | Threshold";  string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 30.0;  float UIStep = 0.1; > = 1.62;
float UIDay_SkyFea  < string UIName = "Day Sky | Feathering"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0;   float UIStep = 0.01; > = 2.35;
float UIDay_SkySat  < string UIName = "Day Sky | Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 3.0;   float UIStep = 0.01; > = 1.0;
float UIDay_SkyScl  < string UIName = "Day Sky | Scale";      string UIWidget = "spinner"; float UIMin = 0.5; float UIMax = 8.0;   float UIStep = 0.1; > = 1.75;

// Sunset Near/Far/Sky
float UISunset_NearInt < string UIName = "Sunset Near | Intensity"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 100.0; float UIStep = 0.1; > = 6.0;
float UISunset_NearThr < string UIName = "Sunset Near | Threshold"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 30.0;  float UIStep = 0.1; > = 5.0;
float UISunset_NearFea < string UIName = "Sunset Near | Feathering"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0;  float UIStep = 0.01; > = 0.65;
float UISunset_NearSat < string UIName = "Sunset Near | Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 3.0;  float UIStep = 0.01; > = 1.0;
float UISunset_NearScl < string UIName = "Sunset Near | Scale";     string UIWidget = "spinner"; float UIMin = 0.5; float UIMax = 8.0;   float UIStep = 0.1; > = 1.6;
float UISunset_FarInt  < string UIName = "Sunset Far | Intensity";  string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 100.0; float UIStep = 0.1; > = 0.0;
float UISunset_FarThr  < string UIName = "Sunset Far | Threshold";  string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 30.0;  float UIStep = 0.1; > = 2.2;
float UISunset_FarFea  < string UIName = "Sunset Far | Feathering"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0;   float UIStep = 0.01; > = 0.0;
float UISunset_FarSat  < string UIName = "Sunset Far | Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 3.0;   float UIStep = 0.01; > = 1.0;
float UISunset_FarScl  < string UIName = "Sunset Far | Scale";      string UIWidget = "spinner"; float UIMin = 0.5; float UIMax = 8.0;   float UIStep = 0.1; > = 3.77;
float UISunset_SkyInt  < string UIName = "Sunset Sky | Intensity";  string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 100.0; float UIStep = 0.1; > = 9.72;
float UISunset_SkyThr  < string UIName = "Sunset Sky | Threshold";  string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 30.0;  float UIStep = 0.1; > = 6.4;
float UISunset_SkyFea  < string UIName = "Sunset Sky | Feathering"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0;   float UIStep = 0.01; > = 2.35;
float UISunset_SkySat  < string UIName = "Sunset Sky | Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 3.0;   float UIStep = 0.01; > = 1.0;
float UISunset_SkyScl  < string UIName = "Sunset Sky | Scale";      string UIWidget = "spinner"; float UIMin = 0.5; float UIMax = 8.0;   float UIStep = 0.1; > = 1.75;

// Dusk Near/Far/Sky
float UIDusk_NearInt < string UIName = "Dusk Near | Intensity"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 100.0; float UIStep = 0.1; > = 6.0;
float UIDusk_NearThr < string UIName = "Dusk Near | Threshold"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 30.0;  float UIStep = 0.1; > = 5.0;
float UIDusk_NearFea < string UIName = "Dusk Near | Feathering"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0;  float UIStep = 0.01; > = 0.65;
float UIDusk_NearSat < string UIName = "Dusk Near | Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 3.0;  float UIStep = 0.01; > = 1.0;
float UIDusk_NearScl < string UIName = "Dusk Near | Scale";     string UIWidget = "spinner"; float UIMin = 0.5; float UIMax = 8.0;   float UIStep = 0.1; > = 1.6;
float UIDusk_FarInt  < string UIName = "Dusk Far | Intensity";  string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 100.0; float UIStep = 0.1; > = 0.0;
float UIDusk_FarThr  < string UIName = "Dusk Far | Threshold";  string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 30.0;  float UIStep = 0.1; > = 30.0;
float UIDusk_FarFea  < string UIName = "Dusk Far | Feathering"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0;   float UIStep = 0.01; > = 0.0;
float UIDusk_FarSat  < string UIName = "Dusk Far | Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 3.0;   float UIStep = 0.01; > = 0.7;
float UIDusk_FarScl  < string UIName = "Dusk Far | Scale";      string UIWidget = "spinner"; float UIMin = 0.5; float UIMax = 8.0;   float UIStep = 0.1; > = 3.77;
float UIDusk_SkyInt  < string UIName = "Dusk Sky | Intensity";  string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 100.0; float UIStep = 0.1; > = 0.0;
float UIDusk_SkyThr  < string UIName = "Dusk Sky | Threshold";  string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 30.0;  float UIStep = 0.1; > = 30.0;
float UIDusk_SkyFea  < string UIName = "Dusk Sky | Feathering"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0;   float UIStep = 0.01; > = 2.35;
float UIDusk_SkySat  < string UIName = "Dusk Sky | Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 3.0;   float UIStep = 0.01; > = 0.7;
float UIDusk_SkyScl  < string UIName = "Dusk Sky | Scale";      string UIWidget = "spinner"; float UIMin = 0.5; float UIMax = 8.0;   float UIStep = 0.1; > = 1.75;

// Night Near/Far/Sky
float UINight_NearInt < string UIName = "Night Near | Intensity"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 100.0; float UIStep = 0.5; > = 50.0;
float UINight_NearThr < string UIName = "Night Near | Threshold"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 30.0;  float UIStep = 0.1; > = 1.0;
float UINight_NearFea < string UIName = "Night Near | Feathering"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0;  float UIStep = 0.01; > = 0.65;
float UINight_NearSat < string UIName = "Night Near | Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 3.0;  float UIStep = 0.01; > = 1.0;
float UINight_NearScl < string UIName = "Night Near | Scale";     string UIWidget = "spinner"; float UIMin = 0.5; float UIMax = 8.0;   float UIStep = 0.1; > = 1.5;
float UINight_FarInt  < string UIName = "Night Far | Intensity";  string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 100.0; float UIStep = 0.1; > = 0.0;
float UINight_FarThr  < string UIName = "Night Far | Threshold";  string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 30.0;  float UIStep = 0.1; > = 2.2;
float UINight_FarFea  < string UIName = "Night Far | Feathering"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0;   float UIStep = 0.01; > = 0.0;
float UINight_FarSat  < string UIName = "Night Far | Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 3.0;   float UIStep = 0.01; > = 0.7;
float UINight_FarScl  < string UIName = "Night Far | Scale";      string UIWidget = "spinner"; float UIMin = 0.5; float UIMax = 8.0;   float UIStep = 0.1; > = 3.77;
float UINight_SkyInt  < string UIName = "Night Sky | Intensity";  string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 100.0; float UIStep = 0.1; > = 0.0;
float UINight_SkyThr  < string UIName = "Night Sky | Threshold";  string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 30.0;  float UIStep = 0.1; > = 5.3;
float UINight_SkyFea  < string UIName = "Night Sky | Feathering"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0;   float UIStep = 0.01; > = 2.35;
float UINight_SkySat  < string UIName = "Night Sky | Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 3.0;   float UIStep = 0.01; > = 0.7;
float UINight_SkyScl  < string UIName = "Night Sky | Scale";      string UIWidget = "spinner"; float UIMin = 0.5; float UIMax = 8.0;   float UIStep = 0.1; > = 1.5;

// Interior Near/Far/Sky
float UIInterior_NearInt < string UIName = "Interior Near | Intensity"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 100.0; float UIStep = 0.5; > = 50.0;
float UIInterior_NearThr < string UIName = "Interior Near | Threshold"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 30.0;  float UIStep = 0.1; > = 1.0;
float UIInterior_NearFea < string UIName = "Interior Near | Feathering"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0;  float UIStep = 0.01; > = 0.65;
float UIInterior_NearSat < string UIName = "Interior Near | Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 3.0;  float UIStep = 0.01; > = 1.0;
float UIInterior_NearScl < string UIName = "Interior Near | Scale";     string UIWidget = "spinner"; float UIMin = 0.5; float UIMax = 8.0;   float UIStep = 0.1; > = 1.5;
float UIInterior_FarInt  < string UIName = "Interior Far | Intensity";  string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 100.0; float UIStep = 0.1; > = 0.34;
float UIInterior_FarThr  < string UIName = "Interior Far | Threshold";  string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 30.0;  float UIStep = 0.1; > = 2.2;
float UIInterior_FarFea  < string UIName = "Interior Far | Feathering"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0;   float UIStep = 0.01; > = 0.0;
float UIInterior_FarSat  < string UIName = "Interior Far | Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 3.0;   float UIStep = 0.01; > = 2.33;
float UIInterior_FarScl  < string UIName = "Interior Far | Scale";      string UIWidget = "spinner"; float UIMin = 0.5; float UIMax = 8.0;   float UIStep = 0.1; > = 3.77;
float UIInterior_SkyInt  < string UIName = "Interior Sky | Intensity";  string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 100.0; float UIStep = 0.1; > = 0.55;
float UIInterior_SkyThr  < string UIName = "Interior Sky | Threshold";  string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 30.0;  float UIStep = 0.1; > = 2.6;
float UIInterior_SkyFea  < string UIName = "Interior Sky | Feathering"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 5.0;   float UIStep = 0.01; > = 0.0;
float UIInterior_SkySat  < string UIName = "Interior Sky | Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 3.0;   float UIStep = 0.01; > = 2.33;
float UIInterior_SkyScl  < string UIName = "Interior Sky | Scale";      string UIWidget = "spinner"; float UIMin = 0.5; float UIMax = 8.0;   float UIStep = 0.1; > = 6.1;

UI_SPECIAL_WHITESPACE(5)


// ═══════════════════════ SKYRIMBRIDGE BLOOM ════════════════════

int _spcSB < string UIName = "=== SkyrimBridge Modulation ==="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

float UISB_LightningSpike    < string UIName = "SB | Lightning Bloom Spike";  string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 10.0; float UIStep = 0.1;  > = 3.0;
float UISB_RainDampening     < string UIName = "SB | Rain Bloom Dampening";   string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0;  float UIStep = 0.01; > = 0.15;
float UISB_SnowDampening     < string UIName = "SB | Snow Bloom Dampening";   string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0;  float UIStep = 0.01; > = 0.10;
float UISB_FogSoftening      < string UIName = "SB | Fog Bloom Softening";    string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0;  float UIStep = 0.01; > = 0.5;
float UISB_NearbyLightBoost  < string UIName = "SB | Nearby Light Bloom";     string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 3.0;  float UIStep = 0.01; > = 0.5;
float UISB_GameBloomRespect  < string UIName = "SB | Game Bloom Scale Mix";   string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0;  float UIStep = 0.01; > = 0.3;


//=============================================================================//
//                       ENB EXTERNAL PARAMETERS                               //
//=============================================================================//

float4 Timer;
float4 ScreenSize;
float  AdaptiveQuality;
float4 Weather;
float  ENightDayFactor;   // 0=night, 1=day
float  EInteriorFactor;   // 0=exterior, 1=interior
float  FieldOfView;
float4 TimeOfDay1;        // x=dawn, y=sunrise, z=day, w=sunset [0..1]
float4 TimeOfDay2;        // x=dusk, y=night [0..1]

float4 tempF1;
float4 tempF2;
float4 tempF3;
float4 tempInfo1;
float4 tempInfo2;


//=============================================================================//
//                    SkyrimBridge External Data Parameters                     //
//=============================================================================//
#define SB_WEATHER_PARAMS
#include "Helper/SkyrimBridge.fxh"


//=============================================================================//
//                           GAME TEXTURES                                     //
//=============================================================================//

Texture2D TextureOriginal;   // R16G16B16A16 HDR scene
Texture2D TextureColor;      // Current bloom pass input
Texture2D TextureDepth;      // R32F depth buffer
Texture2D TextureAdaptation; // Eye adaptation value (1×1 or small)

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

SamplerState Mirror_Sampler
{
    Filter   = MIN_MAG_MIP_LINEAR;
    AddressU = Mirror;
    AddressV = Mirror;
};


//=============================================================================//
//                              HELPERS                                        //
//=============================================================================//

#include "Helper/enbHelper_Common.fxh"
static const float2 PixelSize = _HLP_PixelSize;
static const float2 ScreenRes = _HLP_ScreenRes;

#if ENABLE_TOOLS
    #include "Helper/enbHelper_Debug.fxh"
#endif


//=============================================================================//
//                    TIME-OF-DAY INTERPOLATION                                //
//=============================================================================//
//
//  7-way separation: uses TimeOfDay1 (dawn/sunrise/day/sunset) and
//  TimeOfDay2 (dusk/night) plus EInteriorFactor. The weights are
//  normalized [0..1] factors for each period. We blend all 7 values.
//
//  3-way (DNI) separation: uses ENightDayFactor + EInteriorFactor
//  as the simpler Day/Night/Interior blend.

// 7-way interpolation for float values
float TOD7(float Dawn, float Sunrise, float Day, float Sunset,
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

// 7-way interpolation for float4 values
float4 TOD7_4(float4 Dawn, float4 Sunrise, float4 Day, float4 Sunset,
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

// 3-way DNI interpolation
float DNI(float Day, float Night, float Interior)
{
    return lerp(lerp(Night, Day, ENightDayFactor), Interior, EInteriorFactor);
}


//=============================================================================//
//                   BLOOM PARAMETER RESOLUTION                                //
//=============================================================================//
//
//  Resolves per-TOD bloom parameters to current frame values based on
//  whether extended (7-way) or DNI (3-way) separation is selected.

struct BloomParams
{
    float Intensity;
    float Threshold;
    float Feathering;
    float Saturation;
    float Scale;
};

BloomParams GetBloomParams()
{
    BloomParams p;

    if (UIGlobal_WeatherSep == 1)
    {
        // Extended 7-way
        p.Intensity  = TOD7(UIDawn_Intensity, UISunrise_Intensity, UIDay_Intensity,
                            UISunset_Intensity, UIDusk_Intensity, UINight_Intensity,
                            UIInterior_Intensity);
        p.Threshold  = TOD7(UIDawn_Threshold, UISunrise_Threshold, UIDay_Threshold,
                            UISunset_Threshold, UIDusk_Threshold, UINight_Threshold,
                            UIInterior_Threshold);
        p.Feathering = TOD7(UIDawn_Feathering, UISunrise_Feathering, UIDay_Feathering,
                            UISunset_Feathering, UIDusk_Feathering, UINight_Feathering,
                            UIInterior_Feathering);
        p.Saturation = TOD7(UIDawn_Saturation, UISunrise_Saturation, UIDay_Saturation,
                            UISunset_Saturation, UIDusk_Saturation, UINight_Saturation,
                            UIInterior_Saturation);
        p.Scale      = TOD7(UIDawn_Scale, UISunrise_Scale, UIDay_Scale,
                            UISunset_Scale, UIDusk_Scale, UINight_Scale,
                            UIInterior_Scale);
    }
    else
    {
        // DNI 3-way (Day = Day params, Night = Night params)
        p.Intensity  = DNI(UIDay_Intensity,  UINight_Intensity,  UIInterior_Intensity);
        p.Threshold  = DNI(UIDay_Threshold,  UINight_Threshold,  UIInterior_Threshold);
        p.Feathering = DNI(UIDay_Feathering, UINight_Feathering, UIInterior_Feathering);
        p.Saturation = DNI(UIDay_Saturation, UINight_Saturation, UIInterior_Saturation);
        p.Scale      = DNI(UIDay_Scale,      UINight_Scale,      UIInterior_Scale);
    }

    return p;
}

// Depth-layer bloom params (Near/Far/Sky for current TOD)
struct DepthBloomParams
{
    float Intensity;
    float Threshold;
    float Feathering;
    float Saturation;
    float Scale;
};

DepthBloomParams GetNearBloomParams()
{
    DepthBloomParams p;
    p.Intensity  = TOD7(UIDawn_NearInt, UISunrise_NearInt, UIDay_NearInt, UISunset_NearInt,
                        UIDusk_NearInt, UINight_NearInt, UIInterior_NearInt);
    p.Threshold  = TOD7(UIDawn_NearThr, UISunrise_NearThr, UIDay_NearThr, UISunset_NearThr,
                        UIDusk_NearThr, UINight_NearThr, UIInterior_NearThr);
    p.Feathering = TOD7(UIDawn_NearFea, UISunrise_NearFea, UIDay_NearFea, UISunset_NearFea,
                        UIDusk_NearFea, UINight_NearFea, UIInterior_NearFea);
    p.Saturation = TOD7(UIDawn_NearSat, UISunrise_NearSat, UIDay_NearSat, UISunset_NearSat,
                        UIDusk_NearSat, UINight_NearSat, UIInterior_NearSat);
    p.Scale      = TOD7(UIDawn_NearScl, UISunrise_NearScl, UIDay_NearScl, UISunset_NearScl,
                        UIDusk_NearScl, UINight_NearScl, UIInterior_NearScl);
    return p;
}

DepthBloomParams GetFarBloomParams()
{
    DepthBloomParams p;
    p.Intensity  = TOD7(UIDawn_FarInt, UISunrise_FarInt, UIDay_FarInt, UISunset_FarInt,
                        UIDusk_FarInt, UINight_FarInt, UIInterior_FarInt);
    p.Threshold  = TOD7(UIDawn_FarThr, UISunrise_FarThr, UIDay_FarThr, UISunset_FarThr,
                        UIDusk_FarThr, UINight_FarThr, UIInterior_FarThr);
    p.Feathering = TOD7(UIDawn_FarFea, UISunrise_FarFea, UIDay_FarFea, UISunset_FarFea,
                        UIDusk_FarFea, UINight_FarFea, UIInterior_FarFea);
    p.Saturation = TOD7(UIDawn_FarSat, UISunrise_FarSat, UIDay_FarSat, UISunset_FarSat,
                        UIDusk_FarSat, UINight_FarSat, UIInterior_FarSat);
    p.Scale      = TOD7(UIDawn_FarScl, UISunrise_FarScl, UIDay_FarScl, UISunset_FarScl,
                        UIDusk_FarScl, UINight_FarScl, UIInterior_FarScl);
    return p;
}

DepthBloomParams GetSkyBloomParams()
{
    DepthBloomParams p;
    p.Intensity  = TOD7(UIDawn_SkyInt, UISunrise_SkyInt, UIDay_SkyInt, UISunset_SkyInt,
                        UIDusk_SkyInt, UINight_SkyInt, UIInterior_SkyInt);
    p.Threshold  = TOD7(UIDawn_SkyThr, UISunrise_SkyThr, UIDay_SkyThr, UISunset_SkyThr,
                        UIDusk_SkyThr, UINight_SkyThr, UIInterior_SkyThr);
    p.Feathering = TOD7(UIDawn_SkyFea, UISunrise_SkyFea, UIDay_SkyFea, UISunset_SkyFea,
                        UIDusk_SkyFea, UINight_SkyFea, UIInterior_SkyFea);
    p.Saturation = TOD7(UIDawn_SkySat, UISunrise_SkySat, UIDay_SkySat, UISunset_SkySat,
                        UIDusk_SkySat, UINight_SkySat, UIInterior_SkySat);
    p.Scale      = TOD7(UIDawn_SkyScl, UISunrise_SkyScl, UIDay_SkyScl, UISunset_SkyScl,
                        UIDusk_SkyScl, UINight_SkyScl, UIInterior_SkyScl);
    return p;
}


// Get per-pass balance weight for current TOD
float4 GetPassBalance(int passIdx)
{
    // Array of pass balance arrays, indexed by pass [0-6]
    // We switch on pass index since HLSL doesn't allow true 2D indexing of UI vars
    float4 dawn[7]     = { UIDawn_Pass1, UIDawn_Pass2, UIDawn_Pass3, UIDawn_Pass4,
                           UIDawn_Pass5, UIDawn_Pass6, UIDawn_Pass7 };
    float4 sunrise[7]  = { UISunrise_Pass1, UISunrise_Pass2, UISunrise_Pass3, UISunrise_Pass4,
                           UISunrise_Pass5, UISunrise_Pass6, UISunrise_Pass7 };
    float4 day[7]      = { UIDay_Pass1, UIDay_Pass2, UIDay_Pass3, UIDay_Pass4,
                           UIDay_Pass5, UIDay_Pass6, UIDay_Pass7 };
    float4 sunset[7]   = { UISunset_Pass1, UISunset_Pass2, UISunset_Pass3, UISunset_Pass4,
                           UISunset_Pass5, UISunset_Pass6, UISunset_Pass7 };
    float4 dusk[7]     = { UIDusk_Pass1, UIDusk_Pass2, UIDusk_Pass3, UIDusk_Pass4,
                           UIDusk_Pass5, UIDusk_Pass6, UIDusk_Pass7 };
    float4 night[7]    = { UINight_Pass1, UINight_Pass2, UINight_Pass3, UINight_Pass4,
                           UINight_Pass5, UINight_Pass6, UINight_Pass7 };
    float4 interior[7] = { UIInterior_Pass1, UIInterior_Pass2, UIInterior_Pass3, UIInterior_Pass4,
                           UIInterior_Pass5, UIInterior_Pass6, UIInterior_Pass7 };

    int idx = clamp(passIdx, 0, 6);

    return TOD7_4(dawn[idx], sunrise[idx], day[idx], sunset[idx],
                  dusk[idx], night[idx], interior[idx]);
}


//=============================================================================//
//                   SOFT-KNEE THRESHOLD (KARIS 2014)                          //
//=============================================================================//
//
//  Physically-motivated bloom threshold with smooth knee transition.
//  Prevents hard clipping artifacts that cause bloom "popping".
//
//  knee = feathering parameter (width of transition zone)
//  lum  = pixel luminance
//  thr  = threshold luminance
//
//  Returns [0..1] bloom contribution factor.

float SoftKneeThreshold(float lum, float thr, float knee)
{
    float soft = lum - thr + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + DELTA);
    float contribution = max(soft, lum - thr) / max(lum, DELTA);
    return max(contribution, 0.0);
}

// Apply threshold to color, return thresholded bloom source
float3 ApplyThreshold(float3 color, float threshold, float feathering, float saturation)
{
    float lum = dot(color, K_LUM);
    float factor = SoftKneeThreshold(lum, threshold, max(feathering, 0.01));

    float3 bloomed = color * factor;

    // Saturation adjustment on bloom source
    if (abs(saturation - 1.0) > 0.001)
    {
        float bloomLum = dot(bloomed, K_LUM);
        bloomed = lerp(bloomLum, bloomed, saturation);
    }

    return max(bloomed, 0.0);
}


//=============================================================================//
//                   DEPTH LAYER MASKS                                         //
//=============================================================================//

// Compute depth layer weights: near, far, sky
// Returns float3(nearWeight, farWeight, skyWeight)
float3 GetDepthWeights(float2 uv)
{
    float rawDepth = TextureDepth.SampleLevel(Point_Sampler, uv, 0).x;
    float linDepth = FastLinDepth(rawDepth, 2999.0);

    // Sky detection: depth ≈ 1.0 (far clip)
    float skyMask = smoothstep(0.995, 1.0, rawDepth);

    // Near/Far masks based on linearized depth
    float nearEnd = UIDepth_NearDist;
    float farStart = UIDepth_FarDist;
    float curve = UIDepth_Curve;

    float depthNorm = saturate(linDepth);
    depthNorm = pow(abs(depthNorm), curve);

    float nearMask = 1.0 - smoothstep(0.0, nearEnd, depthNorm);
    float farMask  = smoothstep(nearEnd, farStart, depthNorm);

    // Remove sky from near/far
    nearMask *= (1.0 - skyMask);
    farMask  *= (1.0 - skyMask);

    // Mid zone (between near and far) gets interpolated
    float midMask = 1.0 - nearMask - farMask - skyMask;
    midMask = max(midMask, 0.0);

    // Distribute mid to near/far proportionally
    nearMask += midMask * 0.5;
    farMask  += midMask * 0.5;

    return float3(nearMask, farMask, skyMask);
}


//=============================================================================//
//                   SKYRIMBRIDGE MODULATION                                   //
//=============================================================================//

// Compute SB-driven bloom intensity multiplier
float GetSBBloomModulation()
{
    float mod = 1.0;

    [branch] if (SB_IsActive())
    {
        // Lightning flash → momentary bloom spike
        // SB_Lightning.y = isFlashing, .z = flashIntensity
        float lightningBoost = SB_Lightning.y * SB_Lightning.z * UISB_LightningSpike;
        mod += lightningBoost;

        // Rain dampening: reduce bloom in rainy weather
        // SB_Precipitation.x: 0=none, 1=rain, 2=snow; .y = intensity [0,1]
        float isRain = (SB_Precipitation.x > 0.5 && SB_Precipitation.x < 1.5) ? 1.0 : 0.0;
        float isSnow = (SB_Precipitation.x > 1.5) ? 1.0 : 0.0;
        mod *= 1.0 - isRain * SB_Precipitation.y * UISB_RainDampening;
        mod *= 1.0 - isSnow * SB_Precipitation.y * UISB_SnowDampening;

        // Respect game's bloom scale intent
        float gameScale = SB_IS_HDR.y; // game's bloomScale
        mod = lerp(mod, mod * gameScale, UISB_GameBloomRespect);

        // Weather Parameter: per-weather bloom intensity from WeatherParams.ini
        float wpBloomInt = SB_GetWP(SB_WP_BloomInt, 1.0);
        mod *= wpBloomInt;
    }

    return max(mod, 0.0);
}

// Compute SB-driven bloom scale (radius) modifier
float GetSBScaleModulation()
{
    float mod = 1.0;

    [branch] if (SB_IsActive())
    {
        // Fog density → bloom softening (scattering broadens PSF)
        float fogDensity = SB_Fog_Density.x;
        mod += fogDensity * UISB_FogSoftening;

        // Weather Parameter: per-weather bloom radius from WeatherParams.ini
        float wpBloomRad = SB_GetWP(SB_WP_BloomRad, 1.0);
        mod *= wpBloomRad;
    }

    return max(mod, 0.5);
}


//=============================================================================//
//                   DUAL KAWASE BLUR KERNEL                                   //
//=============================================================================//
//
//  Bandwidth-efficient blur using bilinear tap offsets.
//  4 corner taps at ±0.5px offset → equivalent to 9-tap box with half the
//  texture fetches.  Progressive application at each mip yields wide radius.
//
//  Reference: Bjorge, "Bandwidth-Efficient Rendering", ARM 2015

float3 KawaseBlurDown(float2 uv, float2 texelSize, float scale)
{
    float2 halfTex = texelSize * 0.5 * scale;

    float3 sum = TextureColor.SampleLevel(Linear_Sampler, uv, 0).rgb * 4.0;
    sum += TextureColor.SampleLevel(Linear_Sampler, uv + float2(-halfTex.x, -halfTex.y), 0).rgb;
    sum += TextureColor.SampleLevel(Linear_Sampler, uv + float2( halfTex.x, -halfTex.y), 0).rgb;
    sum += TextureColor.SampleLevel(Linear_Sampler, uv + float2(-halfTex.x,  halfTex.y), 0).rgb;
    sum += TextureColor.SampleLevel(Linear_Sampler, uv + float2( halfTex.x,  halfTex.y), 0).rgb;

    return sum * 0.125; // 1/8 normalization
}

float3 KawaseBlurUp(float2 uv, float2 texelSize, float scale)
{
    float2 off = texelSize * scale;
    float2 halfOff = off * 0.5;

    // 8 samples in cross+corner pattern for smoother upscale
    float3 sum = 0.0;
    sum += TextureColor.SampleLevel(Linear_Sampler, uv + float2(-off.x,  0.0),    0).rgb * 2.0;
    sum += TextureColor.SampleLevel(Linear_Sampler, uv + float2( off.x,  0.0),    0).rgb * 2.0;
    sum += TextureColor.SampleLevel(Linear_Sampler, uv + float2( 0.0,   -off.y),  0).rgb * 2.0;
    sum += TextureColor.SampleLevel(Linear_Sampler, uv + float2( 0.0,    off.y),  0).rgb * 2.0;
    sum += TextureColor.SampleLevel(Linear_Sampler, uv + float2(-halfOff.x, -halfOff.y), 0).rgb;
    sum += TextureColor.SampleLevel(Linear_Sampler, uv + float2( halfOff.x, -halfOff.y), 0).rgb;
    sum += TextureColor.SampleLevel(Linear_Sampler, uv + float2(-halfOff.x,  halfOff.y), 0).rgb;
    sum += TextureColor.SampleLevel(Linear_Sampler, uv + float2( halfOff.x,  halfOff.y), 0).rgb;

    return sum / 12.0; // 1/12 normalization (4*2 + 4*1 = 12)
}


//=============================================================================//
//               BORDER CORRECTION (EDGE DARKENING FIX)                        //
//=============================================================================//
//
//  At screen edges, blur kernels have fewer valid samples (some fall
//  off-screen).  This causes progressive darkening near borders.
//  We compensate by boosting edge pixels proportionally to the
//  percentage of kernel area that lies outside the viewport.

float BorderCorrectionFactor(float2 uv, float kernelRadius)
{
    if (!UIGlobal_BorderCorrect) return 1.0;

    // Distance from each edge in UV space
    float2 distFromEdge = min(uv, 1.0 - uv);

    // Fraction of kernel that falls within bounds
    float2 coverage = saturate(distFromEdge / max(kernelRadius, DELTA));

    // Geometric mean of X and Y coverage
    float totalCoverage = coverage.x * coverage.y;

    // Inverse = correction factor (brighter at edges to compensate)
    return rcp(max(totalCoverage, 0.25)); // Clamp to 4× max correction
}


//=============================================================================//
//                         VERTEX SHADER                                       //
//=============================================================================//

void VS_Bloom(inout float4 pos : SV_POSITION, inout float2 txcoord : TEXCOORD0)
{
    pos.w = 1.0;
}


//=============================================================================//
//                    MAIN PIXEL SHADER — BLOOM PASS                           //
//=============================================================================//
//
//  ENB calls this shader once per bloom pass. The current pass index is
//  provided via tempInfo1.x. The shader must:
//
//    Pass 0: Apply luminance threshold to the scene → bloom source
//    Pass 1+: Progressive blur at decreasing resolutions
//
//  Each pass output feeds the next pass input via TextureColor.

float4 PS_Bloom(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target
{
    // Current pass index (0-based, provided by ENB)
    int passIndex = (int)tempInfo1.x;

    // Early out if this pass exceeds user's configured count
    if (passIndex >= UIGlobal_PassCount)
        return TextureColor.SampleLevel(Linear_Sampler, uv, 0);

    // Resolve current texel size (shrinks each pass as mip decreases)
    // ENB provides the current render target size implicitly; we approximate
    // based on progressive halving from full resolution.
    float mipScale = pow(2.0, (float)passIndex);
    float2 texelSize = PixelSize * mipScale;

    // Get per-pass balance weight
    float4 passBalance = GetPassBalance(passIndex);

    // SkyrimBridge modulation
    float sbIntensityMod = GetSBBloomModulation();
    float sbScaleMod     = GetSBScaleModulation();


    //=== PASS 0: THRESHOLD + PREFILTER ===//

    [branch] if (passIndex == 0)
    {
        float3 color = TextureOriginal.SampleLevel(Linear_Sampler, uv, 0).rgb;

        float3 bloomResult;

        [branch] if (UIGlobal_DepthTweaking)
        {
            // Depth-separated bloom: compute per-layer thresholded bloom
            float3 depthW = GetDepthWeights(uv);

            DepthBloomParams nearP = GetNearBloomParams();
            DepthBloomParams farP  = GetFarBloomParams();
            DepthBloomParams skyP  = GetSkyBloomParams();

            float3 nearBloom = ApplyThreshold(color, nearP.Threshold, nearP.Feathering, nearP.Saturation)
                             * nearP.Intensity * nearP.Scale;
            float3 farBloom  = ApplyThreshold(color, farP.Threshold, farP.Feathering, farP.Saturation)
                             * farP.Intensity * farP.Scale;
            float3 skyBloom  = ApplyThreshold(color, skyP.Threshold, skyP.Feathering, skyP.Saturation)
                             * skyP.Intensity * skyP.Scale;

            bloomResult = nearBloom * depthW.x + farBloom * depthW.y + skyBloom * depthW.z;
        }
        else
        {
            // Simple unified bloom threshold
            BloomParams bp = GetBloomParams();
            bloomResult = ApplyThreshold(color, bp.Threshold, bp.Feathering, bp.Saturation)
                        * bp.Intensity * bp.Scale;
        }

        // Apply SkyrimBridge intensity modulation
        bloomResult *= sbIntensityMod;

        // Apply pass balance weight (RGB channels)
        bloomResult *= passBalance.rgb * 255.0; // Balance values are stored as 1/255

        // Nearby light bloom contribution
        [branch] if (SB_IsActive() && UISB_NearbyLightBoost > 0.001)
        {
            float nearbyFlux = SB_Light_Summary.z; // total luminous flux
            bloomResult *= 1.0 + nearbyFlux * UISB_NearbyLightBoost * 0.01;
        }

        return float4(max(bloomResult, 0.0), 1.0);
    }


    //=== PASS 1+: PROGRESSIVE BLUR ===//

    BloomParams bp = GetBloomParams();
    float blurScale = bp.Scale * sbScaleMod;

    // Alternate between down and up sampling for dual-filter approach
    float3 blurred;
    if (passIndex % 2 == 1)
        blurred = KawaseBlurDown(uv, texelSize, blurScale);
    else
        blurred = KawaseBlurUp(uv, texelSize, blurScale);

    // Apply per-pass balance weight
    blurred *= passBalance.rgb * 255.0;

    // Border correction
    float borderFix = BorderCorrectionFactor(uv, texelSize.x * blurScale * 2.0);
    blurred *= borderFix;

    // SkyrimBridge intensity modulation on blur passes too
    blurred *= sbIntensityMod;


    //=== DEBUG OVERLAY ===//

#if ENABLE_TOOLS
    [branch] if (UIDebug_Enable)
    {
        float3 debug = blurred;

        // Show threshold mask (pass 0 only applies to first pass, but we
        // can show it overlaid on any pass)
        if (UIDebug_ShowThreshold && passIndex == 0)
        {
            float lum = dot(TextureOriginal.SampleLevel(Linear_Sampler, uv, 0).rgb, K_LUM);
            float mask = SoftKneeThreshold(lum, bp.Threshold, max(bp.Feathering, 0.01));
            debug = float3(mask, mask * 0.5, 0.0);
        }

        // Show depth layers as color overlay
        if (UIDebug_ShowDepthLayers && UIGlobal_DepthTweaking)
        {
            float3 dw = GetDepthWeights(uv);
            debug = float3(dw.x, dw.y, dw.z); // R=near, G=far, B=sky
        }

        // Show pass index as color bar at top of screen
        if (UIDebug_ShowPassIndex && uv.y < 0.02)
        {
            float passNorm = (float)passIndex / max((float)UIGlobal_PassCount - 1.0, 1.0);
            debug = float3(passNorm, 1.0 - passNorm, 0.3);
        }

        return float4(debug, 1.0);
    }
#endif

    return float4(max(blurred, 0.0), 1.0);
}


//=============================================================================//
//                              TECHNIQUE                                      //
//=============================================================================//

technique11 Draw
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Bloom()));
        SetPixelShader(CompileShader(ps_5_0, PS_Bloom()));
    }
}
