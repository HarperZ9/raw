//----------------------------------------------------------------------------------------------//
//                                                                                              //
//          enbunderwater.fx — Advanced Physically-Based Underwater Post-Processing              //
//                                                                                              //
//          Original framework by LonelyKitsuune / T.Thanner (Silent Horizons ENB)              //
//          Substantially expanded with PBR absorption, caustics, volumetric                     //
//          light shafts, particulate scattering, and enhanced wave optics.                      //
//                                                                                              //
//          Original portions: Copyright (c) 2019-2020 LonelyKitsuune — CC BY-NC-ND 4.0        //
//          Enhancements: Zain Dana Harper — MIT License                                        //
//                                                                                              //
//----------------------------------------------------------------------------------------------//


//================================================================================
// COMPILE-TIME OPTIONS
//================================================================================

// Applies a small blur to the depth buffer before use to reduce aliased edges
#define ENABLE_DEPTH_PRE_BLUR                   1       //[0-1]

// Use bicubic filtering for lens distortion upscaling (heavier, better quality)
#define USE_BICUBIC_LENS_DISTORTION_UPSCALING   0       //[0-1]

// Caustic rendering method:
//   0 = Dual-layer noise (fast, good quality)
//   1 = Voronoi cell (expensive, sharper/more realistic)
#define CAUSTIC_METHOD                          0       //[0-1]

// Enable screen-space god rays from approximate sun direction
#define ENABLE_GOD_RAYS                         1       //[0-1]

// Enable floating particle / sediment overlay
#define ENABLE_PARTICLES                        1       //[0-1]

// Enable procedural bubble overlay
#define ENABLE_BUBBLES                          1       //[0-1]

// Enable Snell's window / total internal reflection
#define ENABLE_SNELLS_WINDOW                    1       //[0-1]

// Enable bioluminescence particles
#define ENABLE_BIOLUMINESCENCE                  1       //[0-1]

// Enable wet lens effect (near-surface water droplets)
#define ENABLE_WET_LENS                         1       //[0-1]

// Enable Tyndall / directional particulate scattering
#define ENABLE_TYNDALL                          1       //[0-1]

// Out-of-bounds fill for heavy lens distortion:
//   0 = Clamp at borders
//   1 = Passthrough undistorted
//   2 = Fill with water tint (recommended)
#define FILLING_METHOD                          2       //[0-2]


//================================================================================
// INCLUDES & UI HEADER
//================================================================================

#include "UI/enbUI_Primer.fxh"

UI_FileHeaderLong(">>>  Advanced Underwater Shader for SSE  <<<",
                  ">>>     LonelyKitsuune + Z.D. Harper      <<<")


//================================================================================
// UI PARAMETERS — WAVE DISTORTION
//================================================================================

UI_WHITESPACE(1)
UI_ELEMENT(UIW_Header, "      >>>>>>WAVE DISTORTION<<<<<<")
UI_WHITESPACE(2)
float UIW_DepthCurve     < string UIGroup = "WaveDistortion"; string UIName="|- WD - Depth Curve";                   float UIMin= 0.0; float UIMax=20.0; > = { 0.4};
float UIW_Amount         < string UIGroup = "WaveDistortion"; string UIName="|- WD - Distortion Amount";             float UIMin= 0.0; float UIMax= 3.0; > = { 0.5};
float UIW_Dispersion     < string UIGroup = "WaveDistortion"; string UIName="|- WD - Dispersion Amount";             float UIMin=-1.0; float UIMax= 1.0; > = { 0.1};
float UIW_AnimSpeed      < string UIGroup = "WaveDistortion"; string UIName="|- WD - Animation Speed";               float UIMin= 0.0; float UIMax= 4.0; > = { 0.7};
float UIW_Octaves        < string UIGroup = "WaveDistortion"; string UIName="|- WD - Noise Octaves (1-3)";           float UIMin= 1.0; float UIMax= 3.0; > = { 2.0};
float UIW_Lacunarity     < string UIGroup = "WaveDistortion"; string UIName="|- WD - Octave Frequency Mult";         float UIMin= 1.5; float UIMax= 3.0; > = { 2.0};
float UIW_Persistence    < string UIGroup = "WaveDistortion"; string UIName="|- WD - Octave Amplitude Decay";        float UIMin= 0.2; float UIMax= 0.8; > = { 0.5};
float UIW_CurrentStrength< string UIGroup = "WaveDistortion"; string UIName="|- WD - Current Strength";              float UIMin= 0.0; float UIMax= 2.0; > = { 0.3};
float UIW_CurrentAngle   < string UIGroup = "WaveDistortion"; string UIName="|- WD - Current Direction (degrees)";   float UIMin= 0.0; float UIMax=360.0;> = {45.0};
float UIW_CurrentTurb    < string UIGroup = "WaveDistortion"; string UIName="|- WD - Current Turbulence";            float UIMin= 0.0; float UIMax= 1.0; > = { 0.4};


//================================================================================
// UI PARAMETERS — DEPTH BLUR
//================================================================================

UI_WHITESPACE(3)
UI_WHITESPACE(4)
UI_ELEMENT(UIB_Header, "            >>>>>>DEPTH BLUR<<<<<<")
UI_WHITESPACE(5)
float UIB_DepthCurve     < string UIGroup = "DepthBlur"; string UIName="|- Blur - Depth Curve";                 float UIMin= 0.0; float UIMax=20.0; > = { 0.4};
float UIB_MaxAmount      < string UIGroup = "DepthBlur"; string UIName="|- Blur - Max Amount";                  float UIMin= 0.0; float UIMax=20.0; > = { 6.0};
float UIB_EdgeBleedReduc < string UIGroup = "DepthBlur"; string UIName="|- Blur - Edge Bleeding Reduction";     float UIMin= 0.0; float UIMax= 1.0; > = { 0.7};
int   UIB_Quality        < string UIGroup = "DepthBlur"; string UIName="|- Blur - Quality"; string UIWidget="Quality"; int UIMin=0; int UIMax=2; > = { 1};
float UIB_AnimSpeed      < string UIGroup = "DepthBlur"; string UIName="|- Blur - Animation Speed";             float UIMin= 0.0; float UIMax= 4.0; > = { 0.7};
float UIB_AnimWeight     < string UIGroup = "DepthBlur"; string UIName="|- Blur - Animation Weight";            float UIMin= 0.0; float UIMax= 1.0; > = { 0.8};


//================================================================================
// UI PARAMETERS — BEER-LAMBERT VOLUMETRIC ABSORPTION
//================================================================================

UI_WHITESPACE(6)
UI_WHITESPACE(7)
UI_ELEMENT(UIA_Header, "   >>>>>>VOLUMETRIC ABSORPTION<<<<<<")
UI_WHITESPACE(8)
float UIA_Enable         < string UIGroup = "Absorption"; string UIName="|- Absorption - Enable";                float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UIA_Density        < string UIGroup = "Absorption"; string UIName="|- Absorption - Density";               float UIMin= 0.0; float UIMax= 5.0; > = { 1.0};
float UIA_AbsorbR        < string UIGroup = "Absorption"; string UIName="|- Absorption - Red Coefficient";       float UIMin= 0.0; float UIMax= 3.0; > = { 0.45};
float UIA_AbsorbG        < string UIGroup = "Absorption"; string UIName="|- Absorption - Green Coefficient";     float UIMin= 0.0; float UIMax= 3.0; > = { 0.07};
float UIA_AbsorbB        < string UIGroup = "Absorption"; string UIName="|- Absorption - Blue Coefficient";      float UIMin= 0.0; float UIMax= 3.0; > = { 0.03};
float UIA_FogR           < string UIGroup = "Absorption"; string UIName="|- Absorption - Fog Color R";           float UIMin= 0.0; float UIMax= 1.0; > = { 0.01};
float UIA_FogG           < string UIGroup = "Absorption"; string UIName="|- Absorption - Fog Color G";           float UIMin= 0.0; float UIMax= 1.0; > = { 0.06};
float UIA_FogB           < string UIGroup = "Absorption"; string UIName="|- Absorption - Fog Color B";           float UIMin= 0.0; float UIMax= 1.0; > = { 0.10};
float UIA_DepthScale     < string UIGroup = "Absorption"; string UIName="|- Absorption - Depth Scale";           float UIMin= 0.1; float UIMax=10.0; > = { 1.0};
float UIA_ScatterAmount  < string UIGroup = "Absorption"; string UIName="|- Absorption - In-Scatter Amount";     float UIMin= 0.0; float UIMax= 2.0; > = { 0.3};
float UIA_ScatterAniso   < string UIGroup = "Absorption"; string UIName="|- Absorption - Scatter Anisotropy";    float UIMin=-0.9; float UIMax= 0.9; > = { 0.6};


//================================================================================
// UI PARAMETERS — DEPTH COLOR GRADING (Photic Zones)
//================================================================================

UI_WHITESPACE(24)
UI_ELEMENT(UIDCG_Header, "   >>>>>>DEPTH COLOR GRADING<<<<<<")
UI_WHITESPACE(25)
float UIDCG_Enable       < string UIGroup = "DepthColorGrading"; string UIName="|- DepthGrade - Enable";                float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UIDCG_PhDepth      < string UIGroup = "DepthColorGrading"; string UIName="|- DepthGrade - Photic Zone Depth";     float UIMin= 0.1; float UIMax= 1.0; > = { 0.15};
float UIDCG_TwDepth      < string UIGroup = "DepthColorGrading"; string UIName="|- DepthGrade - Twilight Zone Depth";   float UIMin= 0.2; float UIMax= 1.0; > = { 0.50};
float UIDCG_SurfWarmth   < string UIGroup = "DepthColorGrading"; string UIName="|- DepthGrade - Surface Warmth";        float UIMin= 0.0; float UIMax= 1.0; > = { 0.3};
float UIDCG_DeepCold     < string UIGroup = "DepthColorGrading"; string UIName="|- DepthGrade - Deep Cold Shift";       float UIMin= 0.0; float UIMax= 1.0; > = { 0.5};
float UIDCG_DeepDesat    < string UIGroup = "DepthColorGrading"; string UIName="|- DepthGrade - Deep Desaturation";     float UIMin= 0.0; float UIMax= 1.0; > = { 0.6};
float UIDCG_Contrast     < string UIGroup = "DepthColorGrading"; string UIName="|- DepthGrade - Depth Contrast Loss";   float UIMin= 0.0; float UIMax= 1.0; > = { 0.4};


//================================================================================
// UI PARAMETERS — CAUSTICS
//================================================================================

UI_WHITESPACE(9)
UI_WHITESPACE(10)
UI_ELEMENT(UIC_Header, "          >>>>>>CAUSTICS<<<<<<")
UI_WHITESPACE(11)
float UIC_Enable         < string UIGroup = "Caustics"; string UIName="|- Caustics - Enable";                  float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UIC_Intensity      < string UIGroup = "Caustics"; string UIName="|- Caustics - Intensity";               float UIMin= 0.0; float UIMax= 5.0; > = { 1.2};
float UIC_Scale          < string UIGroup = "Caustics"; string UIName="|- Caustics - Pattern Scale";           float UIMin= 0.5; float UIMax=20.0; > = { 5.0};
float UIC_Speed          < string UIGroup = "Caustics"; string UIName="|- Caustics - Animation Speed";         float UIMin= 0.0; float UIMax= 4.0; > = { 0.8};
float UIC_Sharpness      < string UIGroup = "Caustics"; string UIName="|- Caustics - Sharpness";              float UIMin= 0.5; float UIMax= 8.0; > = { 3.0};
float UIC_DepthFade      < string UIGroup = "Caustics"; string UIName="|- Caustics - Depth Fade";             float UIMin= 0.0; float UIMax=10.0; > = { 2.0};
float UIC_SurfaceFade    < string UIGroup = "Caustics"; string UIName="|- Caustics - Surface Proximity Fade";  float UIMin= 0.0; float UIMax= 5.0; > = { 0.5};
float UIC_Chromatic      < string UIGroup = "Caustics"; string UIName="|- Caustics - Chromatic Split";         float UIMin= 0.0; float UIMax= 1.0; > = { 0.3};


//================================================================================
// UI PARAMETERS — GOD RAYS
//================================================================================

UI_WHITESPACE(12)
UI_WHITESPACE(13)
UI_ELEMENT(UIR_Header, "          >>>>>>GOD RAYS<<<<<<")
UI_WHITESPACE(14)
float UIR_Enable         < string UIGroup = "GodRays"; string UIName="|- Rays - Enable";                      float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UIR_Intensity      < string UIGroup = "GodRays"; string UIName="|- Rays - Intensity";                   float UIMin= 0.0; float UIMax= 3.0; > = { 0.4};
float UIR_Decay          < string UIGroup = "GodRays"; string UIName="|- Rays - Decay";                       float UIMin= 0.8; float UIMax= 1.0; > = { 0.96};
float UIR_Density        < string UIGroup = "GodRays"; string UIName="|- Rays - Density";                     float UIMin= 0.1; float UIMax= 2.0; > = { 0.8};
float UIR_SunPosX        < string UIGroup = "GodRays"; string UIName="|- Rays - Sun Screen X (0=left)";       float UIMin= 0.0; float UIMax= 1.0; > = { 0.5};
float UIR_SunPosY        < string UIGroup = "GodRays"; string UIName="|- Rays - Sun Screen Y (0=top)";        float UIMin=-0.5; float UIMax= 1.0; > = { 0.0};
int   UIR_Samples        < string UIGroup = "GodRays"; string UIName="|- Rays - Sample Count (perf)";         int   UIMin=   8; int   UIMax=  24; > = {  24};
float UIR_AbsorbTint     < string UIGroup = "GodRays"; string UIName="|- Rays - Absorption Tinting";          float UIMin= 0.0; float UIMax= 1.0; > = { 0.5};


//================================================================================
// UI PARAMETERS — PARTICLES / SEDIMENT
//================================================================================

UI_WHITESPACE(15)
UI_WHITESPACE(16)
UI_ELEMENT(UIP_Header, "       >>>>>>PARTICLES / SEDIMENT<<<<<<")
UI_WHITESPACE(17)
float UIP_Enable         < string UIGroup = "Particles"; string UIName="|- Particles - Enable";                 float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UIP_Density        < string UIGroup = "Particles"; string UIName="|- Particles - Density";                float UIMin= 0.0; float UIMax= 3.0; > = { 0.6};
float UIP_Size           < string UIGroup = "Particles"; string UIName="|- Particles - Size";                   float UIMin= 0.5; float UIMax= 5.0; > = { 1.5};
float UIP_Brightness     < string UIGroup = "Particles"; string UIName="|- Particles - Brightness";             float UIMin= 0.0; float UIMax= 2.0; > = { 0.8};
float UIP_AnimSpeed      < string UIGroup = "Particles"; string UIName="|- Particles - Drift Speed";            float UIMin= 0.0; float UIMax= 4.0; > = { 0.5};
float UIP_DepthFade      < string UIGroup = "Particles"; string UIName="|- Particles - Depth Fade";             float UIMin= 0.0; float UIMax= 5.0; > = { 1.5};


//================================================================================
// UI PARAMETERS — BUBBLES
//================================================================================

UI_WHITESPACE(26)
UI_ELEMENT(UIBB_Header, "          >>>>>>BUBBLES<<<<<<")
UI_WHITESPACE(27)
float UIBB_Enable        < string UIGroup = "Bubbles"; string UIName="|- Bubbles - Enable";                   float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UIBB_Density       < string UIGroup = "Bubbles"; string UIName="|- Bubbles - Density";                  float UIMin= 0.0; float UIMax= 3.0; > = { 0.8};
float UIBB_MinSize       < string UIGroup = "Bubbles"; string UIName="|- Bubbles - Min Size";                 float UIMin= 0.5; float UIMax= 5.0; > = { 1.0};
float UIBB_MaxSize       < string UIGroup = "Bubbles"; string UIName="|- Bubbles - Max Size";                 float UIMin= 1.0; float UIMax=10.0; > = { 4.0};
float UIBB_RiseSpeed     < string UIGroup = "Bubbles"; string UIName="|- Bubbles - Rise Speed";               float UIMin= 0.0; float UIMax= 4.0; > = { 1.2};
float UIBB_Wobble        < string UIGroup = "Bubbles"; string UIName="|- Bubbles - Wobble Amount";            float UIMin= 0.0; float UIMax= 2.0; > = { 0.6};
float UIBB_Refraction    < string UIGroup = "Bubbles"; string UIName="|- Bubbles - Refraction Strength";      float UIMin= 0.0; float UIMax= 1.0; > = { 0.3};
float UIBB_Highlight     < string UIGroup = "Bubbles"; string UIName="|- Bubbles - Specular Highlight";       float UIMin= 0.0; float UIMax= 2.0; > = { 1.0};
float UIBB_Opacity       < string UIGroup = "Bubbles"; string UIName="|- Bubbles - Opacity";                  float UIMin= 0.0; float UIMax= 1.0; > = { 0.5};


//================================================================================
// UI PARAMETERS — SNELL'S WINDOW
//================================================================================

UI_WHITESPACE(28)
UI_ELEMENT(UISW_Header, "       >>>>>>SNELL'S WINDOW<<<<<<")
UI_WHITESPACE(29)
float UISW_Enable        < string UIGroup = "SnellsWindow"; string UIName="|- Snell - Enable";                     float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UISW_TIRDarkness   < string UIGroup = "SnellsWindow"; string UIName="|- Snell - TIR Mirror Darkness";        float UIMin= 0.0; float UIMax= 1.0; > = { 0.7};
float UISW_CritAngle     < string UIGroup = "SnellsWindow"; string UIName="|- Snell - Critical Angle Adjust";      float UIMin= 0.5; float UIMax= 1.5; > = { 1.0};
float UISW_Fresnel       < string UIGroup = "SnellsWindow"; string UIName="|- Snell - Fresnel Edge Glow";          float UIMin= 0.0; float UIMax= 2.0; > = { 0.8};
float UISW_RippleStr     < string UIGroup = "SnellsWindow"; string UIName="|- Snell - Edge Ripple Distortion";     float UIMin= 0.0; float UIMax= 1.0; > = { 0.5};
float UISW_ChromaShift   < string UIGroup = "SnellsWindow"; string UIName="|- Snell - Chromatic Edge Shift";       float UIMin= 0.0; float UIMax= 1.0; > = { 0.4};


//================================================================================
// UI PARAMETERS — BIOLUMINESCENCE
//================================================================================

UI_WHITESPACE(30)
UI_ELEMENT(UIBIO_Header, "     >>>>>>BIOLUMINESCENCE<<<<<<")
UI_WHITESPACE(31)
float UIBIO_Enable       < string UIGroup = "Bioluminescence"; string UIName="|- Bio - Enable";                       float UIMin= 0.0; float UIMax= 1.0; > = { 0.0};
float UIBIO_Density      < string UIGroup = "Bioluminescence"; string UIName="|- Bio - Density";                      float UIMin= 0.0; float UIMax= 3.0; > = { 0.5};
float UIBIO_Brightness   < string UIGroup = "Bioluminescence"; string UIName="|- Bio - Brightness";                   float UIMin= 0.0; float UIMax= 3.0; > = { 1.0};
float UIBIO_PulseSpeed   < string UIGroup = "Bioluminescence"; string UIName="|- Bio - Pulse Speed";                  float UIMin= 0.0; float UIMax= 4.0; > = { 0.8};
float UIBIO_ColorR       < string UIGroup = "Bioluminescence"; string UIName="|- Bio - Color R";                      float UIMin= 0.0; float UIMax= 1.0; > = { 0.1};
float UIBIO_ColorG       < string UIGroup = "Bioluminescence"; string UIName="|- Bio - Color G";                      float UIMin= 0.0; float UIMax= 1.0; > = { 0.8};
float UIBIO_ColorB       < string UIGroup = "Bioluminescence"; string UIName="|- Bio - Color B";                      float UIMin= 0.0; float UIMax= 1.0; > = { 0.6};
float UIBIO_DepthBias    < string UIGroup = "Bioluminescence"; string UIName="|- Bio - Depth Bias (deeper=more)";     float UIMin= 0.0; float UIMax= 5.0; > = { 2.0};
float UIBIO_Reactive     < string UIGroup = "Bioluminescence"; string UIName="|- Bio - Motion Reactivity";            float UIMin= 0.0; float UIMax= 1.0; > = { 0.5};


//================================================================================
// UI PARAMETERS — TYNDALL SCATTERING
//================================================================================

UI_WHITESPACE(32)
UI_ELEMENT(UITYN_Header, "     >>>>>>TYNDALL SCATTERING<<<<<<")
UI_WHITESPACE(33)
float UITYN_Enable       < string UIGroup = "TyndallScattering"; string UIName="|- Tyndall - Enable";                   float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UITYN_Intensity    < string UIGroup = "TyndallScattering"; string UIName="|- Tyndall - Intensity";                float UIMin= 0.0; float UIMax= 3.0; > = { 0.5};
float UITYN_Anisotropy   < string UIGroup = "TyndallScattering"; string UIName="|- Tyndall - Anisotropy (g)";           float UIMin=-0.9; float UIMax= 0.9; > = { 0.7};
float UITYN_Turbidity    < string UIGroup = "TyndallScattering"; string UIName="|- Tyndall - Turbidity";                float UIMin= 0.0; float UIMax= 3.0; > = { 0.6};
float UITYN_WavelengthDep< string UIGroup = "TyndallScattering"; string UIName="|- Tyndall - Wavelength Dependence";    float UIMin= 0.0; float UIMax= 1.0; > = { 0.5};


//================================================================================
// UI PARAMETERS — WET LENS EFFECT
//================================================================================

UI_WHITESPACE(34)
UI_ELEMENT(UIWL_Header, "       >>>>>>WET LENS EFFECT<<<<<<")
UI_WHITESPACE(35)
float UIWL_Enable        < string UIGroup = "WetLens"; string UIName="|- WetLens - Enable";                   float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UIWL_Intensity     < string UIGroup = "WetLens"; string UIName="|- WetLens - Intensity";                float UIMin= 0.0; float UIMax= 1.0; > = { 0.5};
float UIWL_DropletDensity< string UIGroup = "WetLens"; string UIName="|- WetLens - Droplet Density";          float UIMin= 0.0; float UIMax= 3.0; > = { 1.0};
float UIWL_DropletSize   < string UIGroup = "WetLens"; string UIName="|- WetLens - Droplet Size";             float UIMin= 0.5; float UIMax= 5.0; > = { 2.0};
float UIWL_Refraction    < string UIGroup = "WetLens"; string UIName="|- WetLens - Refraction Strength";      float UIMin= 0.0; float UIMax= 1.0; > = { 0.4};
float UIWL_Streaks       < string UIGroup = "WetLens"; string UIName="|- WetLens - Water Streaks";            float UIMin= 0.0; float UIMax= 1.0; > = { 0.3};
float UIWL_DrySpeed      < string UIGroup = "WetLens"; string UIName="|- WetLens - Dry Speed";               float UIMin= 0.1; float UIMax= 4.0; > = { 0.5};


//================================================================================
// UI PARAMETERS — LENS DISTORTION
//================================================================================

UI_WHITESPACE(18)
UI_WHITESPACE(19)
UI_ELEMENT(UID_Header, "      >>>>>>LENS DISTORTION<<<<<<")
UI_WHITESPACE(20)
float UID_Curve          < string UIGroup = "LensDistortion"; string UIName="|- LD - Lens Curve";                    float UIMin= 1.5; float UIMax=10.0; > = { 4.0};
float UID_Amount         < string UIGroup = "LensDistortion"; string UIName="|- LD - Distortion Amount";             float UIMin=-1.0; float UIMax= 1.0; > = {-0.1};
float UID_Chroma         < string UIGroup = "LensDistortion"; string UIName="|- LD - Chromatic Distortion"; float UIStep=0.001; float UIMin= 0.0; float UIMax= 1.0; > = {0.01};


//================================================================================
// UI PARAMETERS — VIGNETTE
//================================================================================

UI_WHITESPACE(21)
UI_WHITESPACE(22)
UI_ELEMENT(UIV_Header, "          >>>>>>VIGNETTE<<<<<<")
UI_WHITESPACE(23)
float UIV_Enable         < string UIGroup = "Vignette"; string UIName="|- Vignette - Enable";                  float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UIV_Intensity      < string UIGroup = "Vignette"; string UIName="|- Vignette - Intensity";               float UIMin= 0.0; float UIMax= 2.0; > = { 0.5};
float UIV_Radius         < string UIGroup = "Vignette"; string UIName="|- Vignette - Radius";                  float UIMin= 0.3; float UIMax= 1.5; > = { 0.8};
float UIV_Softness       < string UIGroup = "Vignette"; string UIName="|- Vignette - Softness";               float UIMin= 0.1; float UIMax= 2.0; > = { 0.7};
float UIV_ColorR         < string UIGroup = "Vignette"; string UIName="|- Vignette - Tint R";                  float UIMin= 0.0; float UIMax= 1.0; > = { 0.00};
float UIV_ColorG         < string UIGroup = "Vignette"; string UIName="|- Vignette - Tint G";                  float UIMin= 0.0; float UIMax= 1.0; > = { 0.02};
float UIV_ColorB         < string UIGroup = "Vignette"; string UIName="|- Vignette - Tint B";                  float UIMin= 0.0; float UIMax= 1.0; > = { 0.05};
float UIV_PressureWarp   < string UIGroup = "Vignette"; string UIName="|- Vignette - Depth Pressure Warp";     float UIMin= 0.0; float UIMax= 1.0; > = { 0.2};



//----------------------------------------------------------------------------------------------//
//                           External ENB parameters — do not modify                            //
//----------------------------------------------------------------------------------------------//

float4 Timer;           // x = generic timer [0..1], period 16777216ms (~4.6h)
                        // y = avg fps, z = frame counter (wraps at 9999), w = frame time (s)
float4 ScreenSize;      // x = Width, y = 1/Width, z = aspect (W/H), w = 1/aspect
float  AdaptiveQuality;
float4 TimeOfDay1;      // x=dawn y=sunrise z=day w=sunset [0..1]
float4 TimeOfDay2;      // x=dusk y=night [0..1]
float  ENightDayFactor; // 0=night, 1=day
float  EInteriorFactor; // 0=exterior, 1=interior
float  FieldOfView;
float4 Weather;         // x=current y=outgoing z=transition w=hour (needs ENB Helper)

float4 tempF1;
float4 tempF2;
float4 tempF3;
float4 tempInfo1;
float4 tempInfo2;


//----------------------------------------------------------------------------------------------//
//                                    Textures & Samplers                                       //
//----------------------------------------------------------------------------------------------//

Texture2D TextureOriginal;      // R10B10G10A2 32-bit LDR
Texture2D TextureColor;         // output of previous technique
Texture2D TextureDepth;         // R32F depth
Texture2D TextureMask;          // underwater mask

Texture2D RenderTargetRGBA32;
Texture2D RenderTargetRGBA64;
Texture2D RenderTargetRGBA64F;
Texture2D RenderTargetR16F;
Texture2D RenderTargetR32F;
Texture2D RenderTargetRGB32F;

float4 TintColor;               // xyz=tint rgb, w=tint amount

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

SamplerState Linear_Sampler_Noise
{
    Filter   = MIN_MAG_MIP_LINEAR;
    AddressU = Wrap;
    AddressV = Wrap;
};

SamplerState Linear_Sampler_Mirror
{
    Filter   = MIN_MAG_MIP_LINEAR;
    AddressU = Mirror;
    AddressV = Mirror;
};

Texture2D UnderwaterNoiseTex <string ResourceName="Textures/UnderwaterNoise.png";>;

// Common helper (provides FastLinDepth, sRGB2Lin, Lin2sRGB, BicubicFilter, PI, DELTA, etc.)
#include "Helper/enbHelper_Common.fxh"
// SkyrimBridge integration (provides SB_Sun_Direction, SB_Atmos_Sunlight, SB_Wind, etc.)
#include "Helper/SkyrimBridge.fxh"
// PixelSize and ScreenRes provided by enbHelper_Common.fxh

// Correct depth linearization: SB when active, fallback otherwise
float GetLinearDepth(float rawDepth)
{
    if (SB_IsActive()) return SB_LinearizeDepth(rawDepth);
    return FastLinDepth(rawDepth, 200.0);
}


//----------------------------------------------------------------------------------------------//
//  SkyrimBridge Helper Functions (must be defined before first use in pixel shaders)           //
//----------------------------------------------------------------------------------------------//

// SkyrimBridge: use actual sun direction for god rays when available
float2 GetUnderwaterSunPos()
{
    if (SB_IsActive())
        return SB_Sun_NDC.xy * 0.5 + 0.5;  // NDC [-1,1] -> screen [0,1]
    // Fallback: UI-configured sun position
    return float2(UIR_SunPosX, UIR_SunPosY);
}

// SkyrimBridge: tint caustics by actual sunlight color
float3 GetCausticTint()
{
    if (SB_IsActive())
        return SB_Atmos_Sunlight.rgb * SB_Atmos_Sunlight.a;
    return float3(0.8, 0.95, 1.0);  // cool default (original hardcoded value)
}

// SkyrimBridge: modulate wave intensity by wind speed
float GetWindWaveScale()
{
    if (SB_IsActive())
        return lerp(1.0, 1.5, saturate(SB_Wind.x / 20.0));
    return 1.0;
}

// SkyrimBridge: bioluminescence only at night
float GetBiolumIntensity()
{
    if (SB_IsActive())
    {
        float nightFactor = 1.0 - saturate(SB_Time.w); // .w = dayProgress [0,1]
        return nightFactor;
    }
    return 1.0;
}

// SkyrimBridge: interior water detection — reduce caustics 60%, disable god rays
bool IsInteriorWater()
{
    if (SB_IsActive())
        return SB_Interior_Flags.x > 0.5;
    return EInteriorFactor > 0.5;
}

float GetInteriorCausticScale()
{
    return IsInteriorWater() ? 0.4 : 1.0;
}

// SkyrimBridge: fog tint blend for underwater color
float3 GetUnderwaterFogTint()
{
    if (SB_IsActive())
    {
        float3 fogCol = SB_Fog_FarColor.rgb;
        if (dot(fogCol, 1.0) > 0.01)
            return fogCol;
    }
    return float3(0.01, 0.06, 0.10);  // default deep ocean tint
}

// SkyrimBridge: submersion depth for Beer-Lambert absorption
float GetSubmersionDepth()
{
    if (SB_IsActive())
        return max(SB_Player_Water.z, 0.0);  // submersionDepth in world units
    return 1.0;  // default: assume 1 unit below surface
}


//----------------------------------------------------------------------------------------------//
//                                    Structs                                                   //
//----------------------------------------------------------------------------------------------//

struct VertexShaderInput
{
    float3 pos     : POSITION;
    float2 txcoord : TEXCOORD0;
};

struct VertexShaderOutput
{
    float4 pos      : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};


//----------------------------------------------------------------------------------------------//
//                              Constants & Utility Functions                                    //
//----------------------------------------------------------------------------------------------//

static const float  BlurQuality[3]  = { 1.9, 1.5, 1.2 };
static const float  SqrtTwoPI       = sqrt(2.0 * PI);
static const float  TAU             = 2.0 * PI;
static const float3 LUM_WEIGHTS     = float3(0.2126, 0.7152, 0.0722);  // Rec.709 luminance

// Continuous time in seconds (avoids the 0..1 wraparound of Timer.x for smooth animation)
float GetTime()
{
    return Timer.x * 16777.216;
}

float SampleUnderwaterMask(float2 Coords)
{
    return TextureMask.SampleLevel(Point_Sampler, Coords, 0).x;
}

// Water refractive index (Cauchy equation for seawater, 20°C)
//   n(λ) = A + B/λ² + C/λ⁴  where λ is in micrometers
//   This gives wavelength-dependent refraction for chromatic dispersion
static const float3 WATER_IOR = float3(1.3311, 1.3330, 1.3360); // R, G, B (red refracts least)
static const float  WATER_IOR_AVG = 1.333;

// Critical angle for total internal reflection (water → air)
//   θ_c = arcsin(n_air / n_water) ≈ 48.6° from normal
static const float  CRITICAL_ANGLE = 0.8480; // radians ≈ 48.6°
static const float  COS_CRITICAL   = 0.6614; // cos(48.6°)


//================================================================================
// NOISE FUNCTIONS
//================================================================================

// Single animated noise sample from the tileable noise texture
float SampleAnimatedNoise(float2 uv, float speed, float timeOffset)
{
    float2 noiseUV = uv + frac((Timer.x + timeOffset) * 16777.21 * speed);
    return UnderwaterNoiseTex.SampleLevel(Linear_Sampler_Noise, noiseUV, 0).x;
}

// Two-component animated noise (uses flipped UV for decorrelation)
float2 SampleAnimatedNoiseVec(float2 uv, float speed, float timeOffset)
{
    float2 noiseUV = uv + frac((Timer.x + timeOffset) * 16777.21 * speed);
    return float2(UnderwaterNoiseTex.SampleLevel(Linear_Sampler_Noise,       noiseUV, 0).x,
                  UnderwaterNoiseTex.SampleLevel(Linear_Sampler_Noise, 1.0 - noiseUV, 0).x);
}

// Fractal Brownian Motion using the noise texture — multi-octave for organic distortion
float2 FBMNoiseVec(float2 uv, float speed, float timeOffset, float octaves, float lacunarity, float persistence)
{
    float2 result    = 0.0;
    float  amplitude = 1.0;
    float  frequency = 1.0;
    float  totalAmp  = 0.0;

    int oct = (int)clamp(octaves, 1.0, 3.0);

    [unroll(3)]
    for (int i = 0; i < oct; i++)
    {
        float2 n = SampleAnimatedNoiseVec(uv * frequency, speed * frequency, timeOffset + i * 0.137);
        result   += (n * 2.0 - 1.0) * amplitude;
        totalAmp += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }

    return result / totalAmp;
}


//================================================================================
// HASH / PROCEDURAL FUNCTIONS
//================================================================================

// Fast 2D → 2D hash (Inigo Quilez style)
float2 Hash22(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.xx + p3.yz) * p3.zy);
}

// 2D → 1D hash
float Hash21(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

// 1D → 1D hash
float Hash11(float p)
{
    p = frac(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return frac(p);
}

// Interleaved gradient noise (Jimenez 2014)
float IGN(float2 PixelCoord)
{
    return frac(52.9829189 * frac(dot(PixelCoord, float2(0.06711056, 0.00583715))));
}


//================================================================================
// HENYEY-GREENSTEIN PHASE FUNCTION
//================================================================================
// Describes angular distribution of scattered light in participating media.
// g > 0: forward scattering (typical for water particles)
// g = 0: isotropic (Rayleigh-like)
// g < 0: backward scattering

float HenyeyGreenstein(float cosTheta, float g)
{
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / (4.0 * PI * pow(max(denom, DELTA), 1.5));
}


//================================================================================
// CAUSTIC FUNCTIONS
//================================================================================

#if CAUSTIC_METHOD == 0
// ------- Dual-layer noise caustics (fast) -------
float ComputeCaustics(float2 worldUV, float time)
{
    float scale = UIC_Scale;
    float spd   = UIC_Speed * 0.3;

    // Layer 1: slow drift
    float2 uv1 = worldUV * scale + float2(time * spd * 0.7, time * spd * 0.5);
    float  c1  = UnderwaterNoiseTex.SampleLevel(Linear_Sampler_Noise, uv1, 0).x;

    // Layer 2: counter-drift at slightly different frequency
    float2 uv2 = worldUV * scale * 1.37 + float2(-time * spd * 0.5, time * spd * 0.65);
    float  c2  = UnderwaterNoiseTex.SampleLevel(Linear_Sampler_Noise, uv2, 0).x;

    // Layer 3: fine detail
    float2 uv3 = worldUV * scale * 2.13 + float2(time * spd * 0.3, -time * spd * 0.4);
    float  c3  = UnderwaterNoiseTex.SampleLevel(Linear_Sampler_Noise, uv3, 0).x;

    float caustic = (c1 + c2 + c3) / 3.0;
    caustic = pow(saturate(caustic), UIC_Sharpness);

    return caustic * caustic;
}

// Chromatic caustics: compute separate R/G/B with slight scale offsets
// simulating wavelength-dependent refraction through the water surface
float3 ComputeChromaticCaustics(float2 worldUV, float time)
{
    float3 result;
    
    // Wavelength-dependent scale offsets (red refracts less → larger pattern)
    float3 scaleOffset = float3(0.97, 1.0, 1.04) * UIC_Chromatic + (1.0 - UIC_Chromatic);
    
    result.r = ComputeCaustics(worldUV * scaleOffset.r, time);
    result.g = ComputeCaustics(worldUV * scaleOffset.g, time);
    result.b = ComputeCaustics(worldUV * scaleOffset.b, time);
    
    return result;
}

#else
// ------- Voronoi-based caustics (high quality) -------
float ComputeCaustics(float2 worldUV, float time)
{
    float scale = UIC_Scale;
    float spd   = UIC_Speed * 0.4;
    float2 uv   = worldUV * scale;

    float v1 = 1e5;
    float v2 = 1e5;

    float2 iuv1 = floor(uv + float2(time * spd *  0.3, time * spd *  0.2));
    float2 iuv2 = floor(uv + float2(time * spd * -0.2, time * spd *  0.35));

    [unroll] for (int y1 = -1; y1 <= 1; y1++)
    [unroll] for (int x1 = -1; x1 <= 1; x1++)
    {
        float2 cellOffset = float2(x1, y1);
        float2 cellID     = iuv1 + cellOffset;
        float2 cellCenter = cellOffset + Hash22(cellID) * 0.8 +
                            0.2 * sin(time * spd * TAU + Hash22(cellID) * TAU);
        float2 toCenter   = cellCenter - frac(uv + float2(time * spd * 0.3, time * spd * 0.2));
        float  d          = dot(toCenter, toCenter);
        v1 = min(v1, d);
    }

    [unroll] for (int y2 = -1; y2 <= 1; y2++)
    [unroll] for (int x2 = -1; x2 <= 1; x2++)
    {
        float2 cellOffset = float2(x2, y2);
        float2 cellID     = iuv2 + cellOffset;
        float2 cellCenter = cellOffset + Hash22(cellID + 71.0) * 0.8 +
                            0.2 * sin(time * spd * 1.3 * TAU + Hash22(cellID + 71.0) * TAU);
        float2 toCenter   = cellCenter - frac(uv + float2(time * spd * -0.2, time * spd * 0.35));
        float  d          = dot(toCenter, toCenter);
        v2 = min(v2, d);
    }

    float caustic = sqrt(v1) * sqrt(v2);
    caustic = pow(saturate(1.0 - caustic * 2.0), UIC_Sharpness);

    return caustic;
}

float3 ComputeChromaticCaustics(float2 worldUV, float time)
{
    float3 result;
    float3 scaleOffset = float3(0.97, 1.0, 1.04) * UIC_Chromatic + (1.0 - UIC_Chromatic);
    
    result.r = ComputeCaustics(worldUV * scaleOffset.r, time);
    result.g = ComputeCaustics(worldUV * scaleOffset.g, time);
    result.b = ComputeCaustics(worldUV * scaleOffset.b, time);
    
    return result;
}
#endif


//================================================================================
// BEER-LAMBERT VOLUMETRIC ABSORPTION
//================================================================================
// Physically-based wavelength-dependent light absorption through water.
// T(λ,d) = exp(−α(λ) · d)
// With in-scattering: L_out = L_in · T + L_scatter · (1 − T)

float3 ApplyBeerLambert(float3 color, float linearDepth)
{
    if (UIA_Enable < 0.5) return color;

    float3 absorption = float3(UIA_AbsorbR, UIA_AbsorbG, UIA_AbsorbB);
    float  depth      = linearDepth * UIA_DepthScale;

    // SkyrimBridge: account for player submersion depth
    float submersion = GetSubmersionDepth();
    depth += submersion * 0.1;  // bias absorption by how deep player is

    // Transmittance per wavelength channel
    float3 T = exp(-absorption * depth * UIA_Density);

    // Water fog / ambient scatter color — SB fog tint blend
    float3 fogColor = lerp(float3(UIA_FogR, UIA_FogG, UIA_FogB), GetUnderwaterFogTint(), 0.4);

    // In-scattering: Henyey-Greenstein-inspired directional scatter
    float3 scatter = fogColor * UIA_ScatterAmount * (1.0 - T);

    return color * T + scatter;
}


//================================================================================
// DEPTH-BASED COLOR GRADING (Photic Zone Simulation)
//================================================================================
// Models the natural light attenuation zones in real oceans:
//   Epipelagic/Photic (0-200m):   Full spectrum, warm surface light
//   Mesopelagic/Twilight (200-1000m): Blue-dominated, low contrast
//   Bathypelagic/Midnight (1000m+):   Near-total darkness, monochrome blue-black
//
// In Skyrim's compressed depth scale, these zones are mapped to the
// visible range using configurable depth thresholds.

float3 ApplyDepthColorGrade(float3 color, float linearDepth)
{
    if (UIDCG_Enable < 0.5) return color;

    float depth = linearDepth;

    // Zone boundaries (normalized)
    float photicEnd   = UIDCG_PhDepth;
    float twilightEnd = UIDCG_TwDepth;

    // Photic zone: warm surface light (golden hour subsurface scattering)
    float photicWeight = 1.0 - smoothstep(0.0, photicEnd, depth);
    float3 warmShift   = float3(1.05, 1.02, 0.92); // slight warm cast
    color = lerp(color, color * warmShift, photicWeight * UIDCG_SurfWarmth);

    // Twilight zone: progressive blue shift and desaturation
    float twilightWeight = smoothstep(photicEnd, twilightEnd, depth);
    float3 coldShift     = float3(0.85, 0.92, 1.15); // blue-green cast
    float  luma          = dot(color, LUM_WEIGHTS);

    // Desaturate toward monochrome blue
    float desat = twilightWeight * UIDCG_DeepDesat;
    color = lerp(color, float3(luma, luma, luma), desat);
    color = lerp(color, color * coldShift, twilightWeight * UIDCG_DeepCold);

    // Abyssal: contrast compression (everything converges toward fog color)
    float abyssalWeight = smoothstep(twilightEnd, twilightEnd + 0.3, depth);
    float3 fogTarget    = float3(UIA_FogR, UIA_FogG, UIA_FogB) * 0.5;
    float  contrastLoss = abyssalWeight * UIDCG_Contrast;
    color = lerp(color, lerp(fogTarget, color, 0.4), contrastLoss);

    return color;
}


//================================================================================
// PARTICLE / SEDIMENT OVERLAY
//================================================================================

float ComputeParticles(float2 uv, float time, float linearDepth)
{
    float particles = 0.0;
    float totalWeight = 0.0;

    float size = UIP_Size * 100.0;

    // Three depth layers for parallax
    [unroll] for (int layer = 0; layer < 3; layer++)
    {
        float layerScale  = size * (1.0 + layer * 0.5);
        float layerSpeed  = UIP_AnimSpeed * (0.6 + layer * 0.25);
        float layerOffset = layer * 37.0;

        float2 driftUV = uv * layerScale +
                         float2(time * layerSpeed * 0.02 + layerOffset,
                                time * layerSpeed * 0.05 - layerOffset * 0.7);

        float2 cell   = floor(driftUV);
        float2 localP = frac(driftUV) - 0.5;

        [unroll] for (int cy = -1; cy <= 1; cy++)
        [unroll] for (int cx = -1; cx <= 1; cx++)
        {
            float2 neighbor   = float2(cx, cy);
            float2 cellID     = cell + neighbor;
            float  h          = Hash21(cellID + layerOffset);

            if (h > UIP_Density * 0.4) continue;

            float2 particlePos = neighbor + Hash22(cellID + layerOffset) - 0.5;
            float  dist        = length(localP - particlePos);

            float radius   = 0.08 * (0.5 + Hash21(cellID * 1.7 + layerOffset) * 0.5);
            float particle = saturate(1.0 - dist / radius);
            particle      *= particle;

            float brightness = 0.5 + 0.5 * Hash21(cellID * 2.3 + layerOffset);
            particles   += particle * brightness;
            totalWeight += 1.0;
        }
    }

    float depthFade = saturate(linearDepth * UIP_DepthFade) * saturate(3.0 - linearDepth * 8.0);

    return saturate(particles) * UIP_Brightness * depthFade;
}


//================================================================================
// PROCEDURAL BUBBLES
//================================================================================
// Generates multiple layers of rising air bubbles with:
//   - Sinusoidal wobble (real bubble path oscillation from vortex shedding)
//   - Fresnel specular highlights on the sphere surface
//   - Background refraction through the bubble body
//   - Size variation following a power-law distribution (many small, few large)

struct BubbleResult
{
    float3 color;       // Final composited bubble color
    float  alpha;       // Bubble opacity for blending
};

BubbleResult ComputeBubbles(float2 uv, float time, float3 sceneColor)
{
    BubbleResult result;
    result.color = 0.0;
    result.alpha = 0.0;

    float2 aspectUV = float2(uv.x * ScreenSize.z, uv.y); // Aspect-correct

    // 4 layers at different depths for parallax
    [loop] for (int layer = 0; layer < 4; layer++)
    {
        float layerSeed   = layer * 127.0 + 31.0;
        float layerScale  = 30.0 / UIBB_MaxSize * (1.0 + layer * 0.4);
        float layerSpeed  = UIBB_RiseSpeed * (0.7 + layer * 0.2);

        float2 gridUV = aspectUV * layerScale;
        gridUV.y -= time * layerSpeed * 0.3; // Rising motion

        float2 cell   = floor(gridUV);
        float2 localP = frac(gridUV) - 0.5;

        // Check current cell and neighbors
        [unroll] for (int cy = -1; cy <= 1; cy++)
        [unroll] for (int cx = -1; cx <= 1; cx++)
        {
            float2 neighbor = float2(cx, cy);
            float2 cellID   = cell + neighbor + layerSeed;

            // Density check: skip empty cells
            float spawnChance = Hash21(cellID);
            if (spawnChance > UIBB_Density * 0.3) continue;

            // Bubble properties derived from cell hash
            float2 cellHash  = Hash22(cellID);
            float  bubbleSize = lerp(UIBB_MinSize, UIBB_MaxSize, pow(cellHash.x, 2.0)) * 0.02;

            // Wobble: horizontal sinusoidal drift (vortex shedding oscillation)
            float wobblePhase = cellHash.y * TAU + time * (1.5 + cellHash.x) * UIBB_Wobble;
            float wobbleAmt   = sin(wobblePhase) * 0.15 * UIBB_Wobble * bubbleSize * 10.0;

            float2 bubbleCenter = neighbor + Hash22(cellID * 1.7) - 0.5;
            bubbleCenter.x += wobbleAmt;

            float dist = length(localP - bubbleCenter);
            float normDist = dist / bubbleSize;

            if (normDist > 1.0) continue;

            // --- Bubble surface shading ---
            // Approximate a sphere: normal from distance to center
            float2 surfaceDir = (localP - bubbleCenter) / bubbleSize;
            float  nz = sqrt(max(1.0 - dot(surfaceDir, surfaceDir), 0.0));
            float3 normal = float3(surfaceDir, nz);

            // Fresnel reflection (Schlick approximation)
            // Air-water interface: F0 for water ≈ 0.02
            float F0 = 0.02;
            float fresnel = F0 + (1.0 - F0) * pow(1.0 - nz, 5.0);
            fresnel *= UIBB_Highlight;

            // Specular highlight: approximate sun direction
            float3 lightDir = normalize(float3(0.3, -0.7, 0.6)); // rough overhead light
            float3 halfVec  = normalize(lightDir + float3(0, 0, 1));
            float  spec     = pow(max(dot(normal, halfVec), 0.0), 64.0) * 2.0;

            // Refraction: offset sample behind bubble
            float2 refractOffset = surfaceDir * UIBB_Refraction * PixelSize * 20.0;
            float3 behindColor   = TextureColor.SampleLevel(Linear_Sampler, uv + refractOffset, 0).rgb;

            // Bubble body: thin film of air creates slight color shift
            // Approximate thin-film interference with bubble wall thickness
            float thickness = (1.0 - normDist * normDist) * TAU * 3.0;
            float3 filmColor = float3(
                0.9 + 0.1 * cos(thickness),
                0.9 + 0.1 * cos(thickness + TAU / 3.0),
                0.9 + 0.1 * cos(thickness + 2.0 * TAU / 3.0)
            );

            // Composite: refracted background + film color + fresnel reflection + specular
            float3 bubbleCol = behindColor * filmColor * (1.0 - fresnel * 0.5);
            bubbleCol += fresnel * float3(0.7, 0.85, 1.0); // reflected water color
            bubbleCol += spec * UIBB_Highlight;

            // Edge darkening (rim absorption)
            float rimDark = smoothstep(0.6, 1.0, normDist);
            bubbleCol *= 1.0 - rimDark * 0.4;

            // Soft edge antialiasing
            float edgeAA = smoothstep(1.0, 0.85, normDist);

            float alpha = edgeAA * UIBB_Opacity;

            // Alpha-blend this bubble over accumulated result
            result.color = lerp(result.color, bubbleCol, alpha * (1.0 - result.alpha));
            result.alpha = result.alpha + alpha * (1.0 - result.alpha);
        }
    }

    return result;
}


//================================================================================
// SNELL'S WINDOW / TOTAL INTERNAL REFLECTION
//================================================================================
// When looking upward from underwater, there's a circular cone (≈97° wide)
// through which you can see the sky — Snell's window. Outside this cone,
// the water surface becomes a perfect mirror due to total internal reflection.
//
// The boundary is at the critical angle: θ_c = arcsin(1/n_water) ≈ 48.6°
// from the surface normal. This creates a distinctive circular window effect
// with strong chromatic dispersion at the edge (different wavelengths have
// slightly different critical angles).

float3 ApplySnellsWindow(float3 color, float2 texcoord, float time)
{
    if (UISW_Enable < 0.5) return color;

    // View direction: texcoord maps to view angle
    // Center of screen = looking straight (horizontal in world)
    // We approximate the "looking up" angle from vertical screen position
    // Upper screen = looking more toward surface = inside Snell's window
    float2 centered = texcoord * 2.0 - 1.0;
    centered.x *= ScreenSize.z; // Aspect correction

    // Approximate incidence angle with water surface normal (pointing down)
    // At screen center, we're looking horizontally → angle from normal ≈ 90°
    // At top of screen, looking up → angle from normal decreases
    // The camera vertical angle maps roughly to the incidence angle
    float viewAngle = length(centered); // 0 at center, >1 at corners
    float surfaceAngle = viewAngle * 0.8 * UISW_CritAngle; // Normalize to useful range

    // The critical angle boundary
    float criticalAngle = CRITICAL_ANGLE; // ≈ 0.848 radians

    // Ripple distortion at the boundary (water surface waves distort the critical edge)
    float2 rippleUV = texcoord * 8.0 + float2(time * 0.02, time * 0.015);
    float ripple = SampleAnimatedNoise(rippleUV, 0.05, 0.0) * 2.0 - 1.0;
    float rippleOffset = ripple * UISW_RippleStr * 0.15;

    float effectiveAngle = surfaceAngle + rippleOffset;

    // TIR transition: soft boundary around critical angle
    // Inside Snell's window: full transmission (see sky/above surface)
    // Outside: total internal reflection (mirror of underwater scene)
    float tirMask = smoothstep(criticalAngle - 0.08, criticalAngle + 0.05, effectiveAngle);

    // Chromatic critical angle: different wavelengths have different critical angles
    // Blue light (higher IOR) has a slightly smaller critical angle
    float3 chromaTIR;
    float chromaSpread = UISW_ChromaShift * 0.04;
    chromaTIR.r = smoothstep(criticalAngle + chromaSpread - 0.08, criticalAngle + chromaSpread + 0.05, effectiveAngle);
    chromaTIR.g = tirMask;
    chromaTIR.b = smoothstep(criticalAngle - chromaSpread - 0.08, criticalAngle - chromaSpread + 0.05, effectiveAngle);

    // Outside Snell's window: darken and shift toward underwater mirror
    float3 tirColor = color * lerp(1.0, (1.0 - UISW_TIRDarkness), tirMask);

    // Per-channel chromatic effect at the boundary
    tirColor.r = lerp(color.r, tirColor.r, chromaTIR.r);
    tirColor.g = lerp(color.g, tirColor.g, chromaTIR.g);
    tirColor.b = lerp(color.b, tirColor.b, chromaTIR.b);

    // Fresnel glow at the critical angle boundary
    // Near the critical angle, there's a bright caustic-like ring
    float edgeDist  = abs(effectiveAngle - criticalAngle);
    float edgeGlow  = exp(-edgeDist * edgeDist * 200.0) * UISW_Fresnel;
    float3 glowTint = float3(0.75, 0.90, 1.0); // Blue-white glow
    tirColor += edgeGlow * glowTint * ENightDayFactor;

    return tirColor;
}


//================================================================================
// BIOLUMINESCENCE
//================================================================================
// Generates softly glowing procedural plankton/organisms that pulse
// and react to scene motion. Deep-sea organisms like dinoflagellates,
// comb jellies, and anglerfish create natural light in dark waters.

float3 ComputeBioluminescence(float2 uv, float time, float linearDepth)
{
    float3 bioLight = 0.0;
    float3 bioColor = float3(UIBIO_ColorR, UIBIO_ColorG, UIBIO_ColorB);

    // Bioluminescence is more visible at depth (fewer competing light sources)
    float depthBoost = smoothstep(0.05, 0.3, linearDepth * UIBIO_DepthBias);

    // Night boost: bioluminescence is most visible at night
    // SkyrimBridge: use actual time-of-day for more accurate night detection
    float nightBoost = lerp(1.5, 0.7, ENightDayFactor) * GetBiolumIntensity();

    float2 aspectUV = float2(uv.x * ScreenSize.z, uv.y);

    [loop] for (int layer = 0; layer < 3; layer++)
    {
        float seed   = layer * 83.0 + 17.0;
        float scale  = 20.0 * (1.0 + layer * 0.6);
        float drift  = UIBIO_PulseSpeed * (0.3 + layer * 0.15);

        float2 gridUV = aspectUV * scale + float2(time * drift * 0.01, time * drift * 0.02);
        float2 cell   = floor(gridUV);
        float2 local  = frac(gridUV) - 0.5;

        [unroll] for (int cy = -1; cy <= 1; cy++)
        [unroll] for (int cx = -1; cx <= 1; cx++)
        {
            float2 nb = float2(cx, cy);
            float2 cid = cell + nb + seed;

            if (Hash21(cid) > UIBIO_Density * 0.25) continue;

            float2 pos = nb + Hash22(cid) - 0.5;
            float d = length(local - pos);
            float radius = 0.06 + Hash21(cid * 3.1) * 0.08;

            if (d > radius * 2.0) continue;

            // Soft glow falloff (inverse square-ish)
            float glow = exp(-d * d / (radius * radius * 0.5));

            // Pulsing: each organism pulses at its own phase and frequency
            float pulsePhase = Hash21(cid * 2.7) * TAU;
            float pulseFreq  = 0.5 + Hash21(cid * 4.3) * 1.5;
            float pulse = 0.3 + 0.7 * (0.5 + 0.5 * sin(time * UIBIO_PulseSpeed * pulseFreq + pulsePhase));

            // Color variation per organism
            float hueShift = Hash21(cid * 5.9) * 0.3 - 0.15;
            float3 orgColor = bioColor;
            orgColor.r += hueShift * 0.5;
            orgColor.b -= hueShift * 0.3;
            orgColor = saturate(orgColor);

            bioLight += glow * pulse * orgColor;
        }
    }

    // Motion reactivity: bio light brightens near areas with wave distortion
    // (simulates organisms reacting to water disturbance)
    float motionBoost = 1.0;
    if (UIBIO_Reactive > 0.0)
    {
        float2 motionNoise = FBMNoiseVec(uv, 0.2, 0.0, 2.0, 2.0, 0.5);
        motionBoost = 1.0 + length(motionNoise) * UIBIO_Reactive * 2.0;
    }

    return bioLight * UIBIO_Brightness * depthBoost * nightBoost * motionBoost;
}


//================================================================================
// TYNDALL SCATTERING
//================================================================================
// When light passes through water with suspended particles, shorter
// wavelengths scatter more (Rayleigh regime for small particles) or
// all wavelengths scatter similarly (Mie regime for larger particles).
//
// The Tyndall effect is the visible beam of light through a
// participating medium — underwater, this creates visible light cones
// and general haziness with directional dependence.

float3 ApplyTyndallScattering(float3 color, float2 texcoord, float linearDepth, float time)
{
    if (UITYN_Enable < 0.5) return color;

    // Approximate view-to-light angle for phase function
    // SkyrimBridge: use actual sun screen position when available
    float2 sunPos = GetUnderwaterSunPos();
    float2 toSun  = normalize(sunPos - texcoord + DELTA);
    float cosTheta = dot(toSun, float2(0, -1)); // Approximate

    // Henyey-Greenstein phase: how much light scatters toward the viewer
    float phase = HenyeyGreenstein(cosTheta, UITYN_Anisotropy);
    phase = lerp(0.25, phase, 0.7); // Prevent total darkness in backscatter

    // Wavelength-dependent scattering coefficient
    // Small particles (Rayleigh): σ ∝ 1/λ⁴  →  blue scatters much more
    // Large particles (Mie): σ roughly constant  →  white/grey haze
    // UITYN_WavelengthDep blends between these regimes
    float3 scatterCoeff = lerp(
        float3(1.0, 1.0, 1.0),                                    // Mie (wavelength-independent)
        float3(0.5, 0.75, 1.0) * float3(0.5, 0.75, 1.0),          // Rayleigh (blue-heavy)
        UITYN_WavelengthDep
    );

    // Scattering increases with path length through turbid water
    float scatterMask = 1.0 - exp(-linearDepth * UITYN_Turbidity * 3.0);

    // Turbidity noise: non-uniform particle distribution
    float2 turbUV = texcoord * 3.0 + float2(time * 0.005, time * 0.003);
    float turbNoise = SampleAnimatedNoise(turbUV, 0.02, 0.3);
    turbNoise = lerp(0.7, 1.3, turbNoise);

    // Fog color as the scattered light source
    float3 fogColor = float3(UIA_FogR, UIA_FogG, UIA_FogB);
    float3 scatteredLight = fogColor * scatterCoeff * phase * scatterMask * turbNoise;

    // Composite: original + additive scattered light
    color += scatteredLight * UITYN_Intensity * ENightDayFactor;

    // In-scattering also slightly desaturates the direct light
    float3 directAtten = exp(-scatterCoeff * linearDepth * UITYN_Turbidity * 0.5);
    color *= lerp(1.0, directAtten, UITYN_Intensity * 0.3);

    return color;
}


//================================================================================
// WET LENS EFFECT
//================================================================================
// When the camera is near the water surface, droplets and streaks of
// water cling to the lens. This creates localized refraction spots and
// thin water films that slowly dry.
//
// The effect intensity can be modulated externally (e.g., by depth) —
// strongest right at the surface, fading as the camera goes deeper.

float3 ApplyWetLens(float3 color, float2 texcoord, float time, float uwMask)
{
    if (UIWL_Enable < 0.5) return color;

    // Wet lens is strongest near surface transitions
    // Approximate: full effect when mask is just barely 1 (near surface)
    float surfaceProximity = uwMask; // Use mask as rough proximity

    float wetAmount = surfaceProximity * UIWL_Intensity;
    if (wetAmount < 0.01) return color;

    // Droplet field using cell noise
    float2 dropUV = texcoord * 15.0 * UIWL_DropletDensity;
    float2 cell   = floor(dropUV);
    float2 local  = frac(dropUV) - 0.5;

    float3 finalColor = color;

    // Check nearby cells for droplets
    [unroll] for (int dy = -1; dy <= 1; dy++)
    [unroll] for (int dx = -1; dx <= 1; dx++)
    {
        float2 nb  = float2(dx, dy);
        float2 cid = cell + nb;
        float  h   = Hash21(cid);

        if (h > 0.35 * UIWL_DropletDensity) continue;

        float2 dropPos  = nb + Hash22(cid) * 0.8 - 0.4;
        float  dist     = length(local - dropPos);
        float  radius   = (0.1 + Hash21(cid * 2.3) * 0.2) * UIWL_DropletSize * 0.15;

        if (dist > radius) continue;

        float normDist = dist / radius;

        // Droplet normal (hemispherical)
        float nz = sqrt(max(1.0 - normDist * normDist, 0.0));

        // Refraction offset through water droplet
        float2 refractDir = (local - dropPos) / radius;
        float2 refractUV  = texcoord + refractDir * UIWL_Refraction * PixelSize * 15.0 * nz;

        float3 refractedColor = TextureColor.SampleLevel(Linear_Sampler_Mirror, refractUV, 0).rgb;

        // Droplet edge darkening and highlight
        float edgeDark  = smoothstep(0.5, 1.0, normDist) * 0.3;
        float highlight = pow(max(nz, 0.0), 8.0) * 0.4;

        float3 dropColor = refractedColor * (1.0 - edgeDark) + highlight;

        // Blend droplet over scene
        float alpha = smoothstep(1.0, 0.8, normDist) * wetAmount;
        finalColor = lerp(finalColor, dropColor, alpha);
    }

    // Water streaks (vertical bands of thin water film)
    if (UIWL_Streaks > 0.01)
    {
        float streakNoise = SampleAnimatedNoise(
            float2(texcoord.x * 30.0, texcoord.y * 2.0 - time * 0.01 * UIWL_DrySpeed),
            0.01, 0.0
        );
        float streak = pow(streakNoise, 4.0) * UIWL_Streaks * wetAmount;

        // Streaks cause slight vertical refraction
        float2 streakRefract = float2(0, streak * 3.0) * PixelSize;
        float3 streakColor = TextureColor.SampleLevel(Linear_Sampler, texcoord + streakRefract, 0).rgb;

        finalColor = lerp(finalColor, streakColor, streak * 0.5);
    }

    return finalColor;
}


//================================================================================
// CURRENT / DIRECTIONAL FLOW DISTORTION
//================================================================================
// Adds a directional bias to the wave distortion, simulating water currents.
// The current direction creates anisotropic noise stretching and a subtle
// directional drift to all underwater elements.

float2 ComputeCurrentOffset(float2 uv, float time)
{
    if (UIW_CurrentStrength < 0.01) return 0.0;

    float angle = UIW_CurrentAngle * PI / 180.0;
    float2 dir  = float2(cos(angle), sin(angle));

    // Base current drift
    float2 drift = dir * UIW_CurrentStrength * time * 0.02;

    // Add turbulent variation to current (not perfectly laminar)
    float turbNoise = SampleAnimatedNoise(uv * 5.0 + drift, 0.1, 0.5);
    float2 turb = (float2(turbNoise, SampleAnimatedNoise(uv * 5.0 + drift + 0.5, 0.1, 0.7)) - 0.5)
                  * UIW_CurrentTurb * 0.02;

    return drift + turb;
}


//================================================================================
// VERTEX SHADER
//================================================================================

VertexShaderOutput VS_Draw(VertexShaderInput IN)
{
    VertexShaderOutput OUT;
    OUT.pos      = float4(IN.pos.xyz, 1.0);
    OUT.texcoord = IN.txcoord.xy;
    return OUT;
}


//================================================================================
// PASS 0 — WAVE DISTORTION  (with current system)
//================================================================================

float4 PS_WaveDistortion(VertexShaderOutput IN) : SV_Target
{
    float centerDepth = GetLinearDepth(TextureDepth.SampleLevel(Point_Sampler, IN.texcoord, 0).x);
    float distAmount  = pow(abs(centerDepth), UIW_DepthCurve);
          distAmount *= UIW_Amount * 0.1 * SampleUnderwaterMask(IN.texcoord);
    // SkyrimBridge: modulate wave intensity by wind speed
          distAmount *= GetWindWaveScale();

    float time = GetTime();

    // Current offset modulates the noise sampling position
    float2 currentOff = ComputeCurrentOffset(IN.texcoord, time);

    // Multi-octave FBM noise for organic wave distortion
    float2 distVec   = FBMNoiseVec(IN.texcoord + currentOff, 0.1 * UIW_AnimSpeed, 0.5,
                                   UIW_Octaves, UIW_Lacunarity, UIW_Persistence);

    // Add current directional bias to the distortion vector
    float angle = UIW_CurrentAngle * PI / 180.0;
    float2 currentDir = float2(cos(angle), sin(angle));
    distVec += currentDir * UIW_CurrentStrength * 0.3;

    distVec *= distAmount;
    float2 distCoords = IN.texcoord + distVec;

    float4 centerColor = float4(TextureColor.SampleLevel(Point_Sampler, IN.texcoord, 0).rgb, centerDepth);

    // Chromatic dispersion: offset R and B along the distortion vector
    float4 color;
    color.r = TextureColor.SampleLevel(Linear_Sampler, distCoords + distVec * UIW_Dispersion, 0).r;
    color.g = TextureColor.SampleLevel(Linear_Sampler, distCoords, 0).g;
    color.b = TextureColor.SampleLevel(Linear_Sampler, distCoords - distVec * UIW_Dispersion, 0).b;

    // Depth-aware edge protection
    float distDepth  = GetLinearDepth(TextureDepth.SampleLevel(Point_Sampler, distCoords, 0).x);
    float depthDelta = saturate((centerDepth - distDepth) * 100.0);
          color.w    = distDepth;

    color     = lerp(color, centerColor, depthDelta);
    color.rgb = sRGB2Lin(color.rgb);

    return color;
}


//================================================================================
// PASS 1 — BLUR MASK GENERATION
//================================================================================

float PS_DrawBlurMask(VertexShaderOutput IN) : SV_Target
{
    #if ENABLE_DEPTH_PRE_BLUR
        static const float2 offsets[4] = {
            float2( 0.5, 0.5) * PixelSize,
            float2(-0.5, 0.5) * PixelSize,
            float2(-0.5,-0.5) * PixelSize,
            float2( 0.5,-0.5) * PixelSize
        };

        float depth = 0.0;
        [unroll] for (int i = 0; i < 4; i++)
            depth += RenderTargetRGBA64.SampleLevel(Linear_Sampler, IN.texcoord + offsets[i], 0).w;
        depth /= 4.0;
    #else
        float depth = RenderTargetRGBA64.SampleLevel(Point_Sampler, IN.texcoord, 0).w;
    #endif

    float mask = SampleUnderwaterMask(IN.texcoord) * pow(abs(depth), UIB_DepthCurve);

    float noise = SampleAnimatedNoise(IN.texcoord, 0.1 * UIB_AnimSpeed, 0.0);
          mask *= lerp(1.0, noise, UIB_AnimWeight);

    return mask;
}


//================================================================================
// PASS 2 & 3 — GAUSSIAN BLUR (bilateral, depth-aware)
//================================================================================

float4 PS_GaussBlur(VertexShaderOutput IN, uniform float2 Axis, uniform Texture2D InputTex) : SV_Target
{
    float  mask  = RenderTargetR16F.SampleLevel(Point_Sampler, IN.texcoord, 0).x;
    float3 color = InputTex.SampleLevel(Point_Sampler, IN.texcoord, 0).rgb;

    float sigma       = UIB_MaxAmount * mask + DELTA;
    float offsetScale = 1.0 - sqrt(saturate(sigma / 3.0)) * 0.5;

    float4 stepSize     = PixelSize.xyxy * float4(Axis, -Axis);
    float4 halfStepSize = stepSize * offsetScale;
           stepSize    *= 2.0;

    float weightFactor = rcp(sigma * SqrtTwoPI);
    float loopCount    = ceil(sigma * BlurQuality[UIB_Quality] - 0.01);
          sigma        = rcp(sigma * sigma);

    float  weightSum  = weightFactor * offsetScale;
    float3 blurredCol = weightSum * color;
           mask      *= UIB_EdgeBleedReduc;

    [loop] for (float i = 1.0; i <= loopCount; i++)
    {
        float  gaussWeight  = weightFactor * exp(i * i * -sigma);
        float4 currOffset   = IN.texcoord.xyxy + i * stepSize - halfStepSize;

        float2 finalWeights = {
            RenderTargetR16F.SampleLevel(Linear_Sampler, currOffset.xy, 0).x,
            RenderTargetR16F.SampleLevel(Linear_Sampler, currOffset.zw, 0).x
        };
        finalWeights = saturate(1.0 - (mask - finalWeights * UIB_EdgeBleedReduc) * i) * gaussWeight;

        blurredCol += finalWeights.x * InputTex.SampleLevel(Linear_Sampler, currOffset.xy, 0).rgb;
        blurredCol += finalWeights.y * InputTex.SampleLevel(Linear_Sampler, currOffset.zw, 0).rgb;
        weightSum  += finalWeights.x + finalWeights.y;
    }

    color = blurredCol / weightSum;
    if (Axis.x == 0.0) color = Lin2sRGB(color);

    return float4(color, 1.0);
}


//================================================================================
// PASS 4 — VOLUMETRIC COMPOSITE
//================================================================================
// All volumetric / additive effects composited in a single pass.
// Now includes: Absorption, Depth Color Grading, Caustics (chromatic),
// God Rays (absorption-tinted), Particles, Bubbles, Bioluminescence,
// Tyndall Scattering, and Snell's Window.

float4 PS_VolumetricComposite(VertexShaderOutput IN) : SV_Target
{
    float  uwMask      = SampleUnderwaterMask(IN.texcoord);
    float3 color       = TextureColor.SampleLevel(Point_Sampler, IN.texcoord, 0).rgb;
    float  linearDepth = GetLinearDepth(TextureDepth.SampleLevel(Point_Sampler, IN.texcoord, 0).x);
    float  time        = GetTime();

    // Skip all processing for pixels outside the underwater mask
    if (uwMask < 0.01) return float4(color, 1.0);


    // --- Beer-Lambert Absorption ---
    color = ApplyBeerLambert(color, linearDepth);


    // --- Depth-Based Color Grading ---
    color = ApplyDepthColorGrade(color, linearDepth);


    // --- Tyndall Scattering ---
    #if ENABLE_TYNDALL
    color = ApplyTyndallScattering(color, IN.texcoord, linearDepth, time);
    #endif


    // --- Chromatic Caustics ---
    if (UIC_Enable > 0.5)
    {
        float2 worldUV = IN.texcoord * (1.0 + linearDepth * 2.0);

        // Current offsets caustic pattern position (caustics drift with current)
        float2 currentOff = ComputeCurrentOffset(IN.texcoord, time) * 5.0;
        worldUV += currentOff;

        float3 caustic = (UIC_Chromatic > 0.01)
            ? ComputeChromaticCaustics(worldUV, time)
            : ComputeCaustics(worldUV, time);

        float depthFade   = exp(-linearDepth * UIC_DepthFade);
        float surfaceFade = saturate(linearDepth * UIC_SurfaceFade * 10.0);

        float causticMask = UIC_Intensity * depthFade * surfaceFade * uwMask;

        float sceneLum = dot(color, LUM_WEIGHTS);
        causticMask   *= lerp(0.3, 1.0, saturate(sceneLum * 2.0));
        causticMask   *= ENightDayFactor;

        // SkyrimBridge: interior water reduces caustics by 60%
        causticMask *= GetInteriorCausticScale();

        // SkyrimBridge: tint caustics by actual sunlight color when available
        float3 causticColor = GetCausticTint();
        color += caustic * causticMask * causticColor;
    }


    // --- God Rays (with absorption tinting) ---
    #if ENABLE_GOD_RAYS
    if (UIR_Enable > 0.5 && ENightDayFactor > 0.1 && !IsInteriorWater())
    {
        // SkyrimBridge: use actual sun screen position when available
        float2 sunPos   = GetUnderwaterSunPos();
        float2 toSun    = sunPos - IN.texcoord;
        float  sunDist  = length(toSun);
        float2 rayDir   = toSun / (float)UIR_Samples * UIR_Density;

        float3 absorption = float3(UIA_AbsorbR, UIA_AbsorbG, UIA_AbsorbB);

        float  rayWeight = 1.0;
        float3 rayAccum  = 0.0;
        float2 rayUV     = IN.texcoord;

        [loop] for (int s = 0; s < UIR_Samples; s++)
        {
            rayUV += rayDir;

            float2 clampedUV = saturate(rayUV);
            float3 raySample = TextureColor.SampleLevel(Linear_Sampler, clampedUV, 0).rgb;
            float  rayLum    = dot(raySample, LUM_WEIGHTS);

            float rayDepth = GetLinearDepth(TextureDepth.SampleLevel(Point_Sampler, clampedUV, 0).x);
            float depthW   = exp(-rayDepth * 3.0);

            // Absorption-tinted ray accumulation: light loses red first
            float3 rayTrans = exp(-absorption * rayDepth * UIA_DepthScale * UIR_AbsorbTint);
            float3 rayContrib = rayLum * depthW * rayWeight * SampleUnderwaterMask(clampedUV) * rayTrans;

            rayAccum  += rayContrib;
            rayWeight *= UIR_Decay;
        }

        float sunFalloff = exp(-sunDist * sunDist * 4.0);

        float3 rayIntensity = rayAccum * UIR_Intensity * sunFalloff * ENightDayFactor * uwMask;
        rayIntensity /= (float)UIR_Samples;

        float3 rayColor = float3(0.9, 0.95, 1.0);
        color += rayIntensity * rayColor;
    }
    #endif


    // --- Particles / Sediment ---
    #if ENABLE_PARTICLES
    if (UIP_Enable > 0.5)
    {
        float particles = ComputeParticles(IN.texcoord, time, linearDepth);
        float3 particleColor = float3(UIA_FogR, UIA_FogG, UIA_FogB) * 2.0 + 0.15;
        color += particles * particleColor * UIP_Density * uwMask;
    }
    #endif


    // --- Bubbles ---
    #if ENABLE_BUBBLES
    if (UIBB_Enable > 0.5)
    {
        BubbleResult bubbles = ComputeBubbles(IN.texcoord, time, color);
        color = lerp(color, bubbles.color, bubbles.alpha * uwMask);
    }
    #endif


    // --- Bioluminescence ---
    #if ENABLE_BIOLUMINESCENCE
    if (UIBIO_Enable > 0.5)
    {
        float3 bio = ComputeBioluminescence(IN.texcoord, time, linearDepth);
        color += bio * uwMask;
    }
    #endif


    // --- Snell's Window ---
    #if ENABLE_SNELLS_WINDOW
    color = ApplySnellsWindow(color, IN.texcoord, time);
    #endif


    return float4(color, 1.0);
}


//================================================================================
// PASS 5 — LENS DISTORTION + VIGNETTE + WET LENS
//================================================================================

float4 PS_LensDistortion(VertexShaderOutput IN) : SV_Target
{
    float uwMask = SampleUnderwaterMask(IN.texcoord);
    float maskedDistAmount = UID_Amount * uwMask;

    float2 distortVec  = IN.texcoord * 2.0 - 1.0;
    float  distortGrad = length(distortVec);
           distortVec /= float2(1.0, ScreenSize.z);

    float distortShift = pow(distortGrad / 1.15, UID_Curve);
          distortVec  *= distortShift * maskedDistAmount / distortGrad;

    float2 distortCoords  = distortVec + IN.texcoord;
    float2 correctionZoom = saturate(abs(maskedDistAmount)) * float2(3.0, 1.0) + 1.0;
           correctionZoom.x = (maskedDistAmount > 0.0) ? correctionZoom.x : correctionZoom.y;
           distortCoords    = (distortCoords - 0.5) / correctionZoom.x + 0.5;

    #if FILLING_METHOD != 0
        bool outOfBounds = !all(saturate(distortCoords - distortCoords * distortCoords));
    #endif
    #if FILLING_METHOD == 1
        distortCoords = outOfBounds ? IN.texcoord : distortCoords;
    #endif

    float3 color;

    #if USE_BICUBIC_LENS_DISTORTION_UPSCALING
        color.r = BicubicFilter(TextureColor, distortCoords + distortVec * UID_Chroma).r;
        color.g = BicubicFilter(TextureColor, distortCoords).g;
        color.b = BicubicFilter(TextureColor, distortCoords - distortVec * UID_Chroma).b;
    #else
        color.r = TextureColor.SampleLevel(Linear_Sampler, distortCoords + distortVec * UID_Chroma, 0).r;
        color.g = TextureColor.SampleLevel(Linear_Sampler, distortCoords, 0).g;
        color.b = TextureColor.SampleLevel(Linear_Sampler, distortCoords - distortVec * UID_Chroma, 0).b;
    #endif

    #if FILLING_METHOD == 2
        color = outOfBounds ? TintColor.rgb * TintColor.w : color;
    #endif


    // --- Wet Lens Effect ---
    #if ENABLE_WET_LENS
    {
        float time = GetTime();
        color = ApplyWetLens(color, IN.texcoord, time, uwMask);
    }
    #endif


    // --- Underwater Vignette (with pressure warp) ---
    if (UIV_Enable > 0.5)
    {
        float2 centeredUV = IN.texcoord * 2.0 - 1.0;
               centeredUV.x *= ScreenSize.z;
        float  dist = length(centeredUV);

        // Depth-based "pressure" warp: vignette tightens with depth
        float linearDepth = GetLinearDepth(TextureDepth.SampleLevel(Point_Sampler, IN.texcoord, 0).x);
        float pressureWarp = 1.0 - linearDepth * UIV_PressureWarp;
        float effectiveRadius = UIV_Radius * max(pressureWarp, 0.3);

        float vignette = 1.0 - smoothstep(effectiveRadius, effectiveRadius + UIV_Softness, dist);
        vignette = lerp(1.0, vignette, UIV_Intensity * uwMask);

        float3 vignetteTint = float3(UIV_ColorR, UIV_ColorG, UIV_ColorB);
        color = lerp(vignetteTint, color, vignette);
    }

    return float4(color, 1.0);
}


//================================================================================
// TECHNIQUE PIPELINE
//================================================================================

#undef  TECH11
#define TECH11(NAME, VS, PS) \
technique11 NAME {pass p0 {SetVertexShader(CompileShader(vs_5_0, VS)); SetPixelShader(CompileShader(ps_5_0, PS));}}

// Pass 0: Wave distortion → temp buffer (linear color + depth in alpha)
TECH11(Underwater  <string UIName="Underwater - Advanced PBR";
                    string RenderTarget="RenderTargetRGBA64";>, VS_Draw(), PS_WaveDistortion())

// Pass 1: Generate depth-aware blur mask
TECH11(Underwater1 <string RenderTarget="RenderTargetR16F";>,    VS_Draw(), PS_DrawBlurMask())

// Pass 2: Horizontal Gaussian blur
TECH11(Underwater2,                                              VS_Draw(), PS_GaussBlur(float2(1,0), RenderTargetRGBA64))

// Pass 3: Vertical Gaussian blur
TECH11(Underwater3,                                              VS_Draw(), PS_GaussBlur(float2(0,1), TextureColor))

// Pass 4: Volumetric composite (absorption + grading + caustics + rays + particles + bubbles + bio + tyndall + snell)
TECH11(Underwater4,                                              VS_Draw(), PS_VolumetricComposite())

// Pass 5: Lens distortion + wet lens + vignette (final output)
TECH11(Underwater5,                                              VS_Draw(), PS_LensDistortion())
