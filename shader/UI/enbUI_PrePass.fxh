//----------------------------------------------------------------------------------------------//
//                                                                                              //
//            ENB PrePass UI - Screen-Space Subsurface Scattering Parameters                    //
//                                                                                              //
//                     Original UI system by LonelyKitsuune aka Skratzer                        //
//                  SSS parameter design by Zain Dana Harper - Feb 2026                         //
//                                                                                              //
//  Group 0: Debug Visualization Tools                                                          //
//  Group 1: SSS Detection & Diffusion Profile (global, physically constant)                    //
//  Group 2: Skin Color Grading (DNI-separated per lighting condition)                          //
//  Group 3: Translucency & Advanced (DNI-separated per lighting condition)                     //
//                                                                                              //
//----------------------------------------------------------------------------------------------//

#ifndef _UI_PRIMER_
#error UI_Primer couldnt be found!
#endif


//=============================================================================//
//  Group 0: Debug Visualization Tools                                         //
//=============================================================================//

#if SHADERGROUP == 0

UI_FileHeaderLong(">>>     Subsurface Scattering PrePass     <<<",
                  ">>>   by LonelyKitsuune + Zain D. Harper    <<<")

UI_WHITESPACE(1)
#if ENABLE_TOOLS
UI_ELEMENT(_Tools_Header,"               >>>>>>TOOLS<<<<<<")
UI_WHITESPACE(2)
bool UI_Vis_SkinMask    < string UIGroup = "Developer Tools"; string UIName="Visualize Skin Mask";           > = {false};
bool UI_Vis_DiffuseOnly < string UIGroup = "Developer Tools"; string UIName="Visualize Diffusion Only";      > = {false};
bool UI_Vis_SpecMask    < string UIGroup = "Developer Tools"; string UIName="Visualize Specular Estimation"; > = {false};
bool UI_Vis_Curvature   < string UIGroup = "Developer Tools"; string UIName="Visualize Curvature";           > = {false};
bool UI_Vis_Translucency< string UIGroup = "Developer Tools"; string UIName="Visualize Translucency";        > = {false};
bool UI_Vis_Thickness   < string UIGroup = "Developer Tools"; string UIName="Visualize Thickness Estimate";  > = {false};
bool UI_Vis_Normals     < string UIGroup = "Developer Tools"; string UIName="Visualize Screen Normals";      > = {false};
bool UI_Vis_KernelColor < string UIGroup = "Developer Tools"; string UIName="Visualize Kernel Chromaticity"; > = {false};
bool UI_Vis_WrapLight   < string UIGroup = "Developer Tools"; string UIName="Visualize Wrap Lighting";       > = {false};
bool UI_Vis_ShadowBleed < string UIGroup = "SSS.ShadowBleed"; string UIName="Visualize Shadow Bleed Map";    > = {false};
UI_WHITESPACE(3)
UI_WHITESPACE(4)
#endif //ENABLE_TOOLS

#undef SHADERGROUP


//=============================================================================//
//  Group 1: SSS Detection & Diffusion Profile (Global)                        //
//                                                                             //
//  Physical scattering properties of skin tissue. These are constant across   //
//  lighting conditions (the medium doesn't change with time of day).          //
//=============================================================================//

#elif SHADERGROUP == 1

UI_ELEMENT(_SSS_Header,"    >>>>>>SSS DIFFUSION PROFILE<<<<<<")
UI_WHITESPACE(5)

// Master enable
bool  UISSS_Enable          < string UIGroup = "SSS"; string UIName="|- SSS - Enable";                                                                   > = {true};

// --- Skin Detection & Masking ---
UI_WHITESPACE(6)
UI_ELEMENT(_Mask_Header,          "|---- Skin Detection")
float UISSS_MaskSmooth      < string UIGroup = "SSS.Detection"; string UIName="|- Mask - Transition Width";       float UIStep=0.001; float UIMin=0.01; float UIMax=0.25;  > = {0.08};
float UISSS_MaskThreshold   < string UIGroup = "SSS.Detection"; string UIName="|- Mask - Detection Threshold";    float UIStep=0.001; float UIMin=0.01; float UIMax=0.5;   > = {0.10};
float UISSS_DepthFadeNear   < string UIGroup = "SSS.Detection"; string UIName="|- Mask - Depth Fade Near";        float UIStep=0.001; float UIMin=0.0;  float UIMax=0.1;   > = {0.001};
float UISSS_DepthFadeFar    < string UIGroup = "SSS.Detection"; string UIName="|- Mask - Depth Fade Far";         float UIStep=0.001; float UIMin=0.01; float UIMax=0.5;   > = {0.12};
float UISSS_NormalWeight    < string UIGroup = "SSS.Detection"; string UIName="|- Mask - Normal Variance Weight"; float UIStep=0.01;  float UIMin=0.0;  float UIMax=2.0;   > = {0.5};

// --- Diffusion Kernel ---
UI_WHITESPACE(7)
UI_ELEMENT(_Diff_Header,          "|---- Diffusion Kernel")
float UISSS_DiffRadius      < string UIGroup = "SSS.Diffusion"; string UIName="|- Diffusion - Radius (px)";      float UIStep=0.1;   float UIMin=0.5;  float UIMax=30.0;  > = {8.0};
bool  UISSS_DepthScale      < string UIGroup = "SSS.Diffusion"; string UIName="|- Diffusion - Depth-Scale Radius";                                                        > = {true};
float UISSS_DepthScaleRef   < string UIGroup = "SSS.Diffusion"; string UIName="|- Diffusion - Reference Depth";   float UIStep=0.001; float UIMin=0.005;float UIMax=0.2;   > = {0.03};
float UISSS_DiffFollowSfc   < string UIGroup = "SSS.Diffusion"; string UIName="|- Diffusion - Follow Surface";    float UIStep=0.01;  float UIMin=0.0;  float UIMax=1.0;   > = {0.5};
bool  UISSS_TemporalJitter  < string UIGroup = "SSS.Diffusion"; string UIName="|- Diffusion - Temporal Jitter";                                                           > = {true};

// --- Bilateral Rejection ---
UI_WHITESPACE(8)
UI_ELEMENT(_Bilat_Header,         "|---- Edge Rejection")
float UISSS_DepthBias       < string UIGroup = "SSS.EdgeReject"; string UIName="|- Bilateral - Depth Sensitivity"; float UIStep=0.1;   float UIMin=1.0;  float UIMax=500.0; > = {100.0};
float UISSS_NormalBilateral < string UIGroup = "SSS.EdgeReject"; string UIName="|- Bilateral - Normal Sensitivity";float UIStep=0.1;   float UIMin=0.0;  float UIMax=16.0;  > = {4.0};

// --- Per-Channel Scattering Distances ---
UI_WHITESPACE(9)
UI_ELEMENT(_Scatter_Header,       "|---- Scattering Distances")
float UISSS_ScatterR        < string UIGroup = "SSS.Scattering"; string UIName="|- Scatter - Red Distance";        float UIStep=0.01;  float UIMin=0.1;  float UIMax=3.0;   > = {1.0};
float UISSS_ScatterG        < string UIGroup = "SSS.Scattering"; string UIName="|- Scatter - Green Distance";      float UIStep=0.01;  float UIMin=0.05; float UIMax=2.0;   > = {0.42};
float UISSS_ScatterB        < string UIGroup = "SSS.Scattering"; string UIName="|- Scatter - Blue Distance";       float UIStep=0.01;  float UIMin=0.01; float UIMax=1.5;   > = {0.20};

// --- Shadow Bleed ---
UI_WHITESPACE(10)
UI_ELEMENT(_Bleed_Header,         "|---- Shadow Bleed")
float UISSS_ShadowBleed     < string UIGroup = "SSS.ShadowBleed"; string UIName="|- Shadow Bleed - Asymmetry";      float UIStep=0.01;  float UIMin=0.0;  float UIMax=1.0;   > = {0.4};

// --- Specular Handling ---
UI_WHITESPACE(11)
UI_ELEMENT(_Spec_Header,          "|---- Specular Handling")
float UISSS_SpecPreserve    < string UIGroup = "SSS.Specular"; string UIName="|- Specular - Preservation";        float UIStep=0.01;  float UIMin=0.0;  float UIMax=1.0;   > = {0.8};
bool  UISSS_SpecFresnel     < string UIGroup = "SSS.Specular"; string UIName="|- Specular - Fresnel Detection";                                                          > = {true};

#undef SHADERGROUP


//=============================================================================//
//  Group 2: Skin Color Grading (DNI-separated)                                //
//=============================================================================//

#elif SHADERGROUP == 2

#ifndef NOTFIRSTTIME
#define NOTFIRSTTIME
UI_WHITESPACE(12)
UI_ELEMENT(_Grade_Header,"       >>>>>>SKIN COLOR GRADING<<<<<<")
UI_WHITESPACE(13)
UI_SPECIAL_WHITESPACE(1)
#endif //NOTFIRSTTIME

float  SEPARATE_VAR(UISSS_Exposure)   < string UIGroup = "SSS.ColorGrading"; string UIName=TO_STRING(|- TODIE - Exposure);                  float UIStep=0.001; float UIMin=0.0; float UIMax=5.0;  string Separation = "ExteriorWeather";> = {1.0};
float  SEPARATE_VAR(UISSS_Contrast)   < string UIGroup = "SSS.ColorGrading"; string UIName=TO_STRING(|- TODIE - Contrast);                  float UIStep=0.001; float UIMin=0.0; float UIMax=5.0;  string Separation = "ExteriorWeather";> = {1.0};
float  SEPARATE_VAR(UISSS_Saturation) < string UIGroup = "SSS.ColorGrading"; string UIName=TO_STRING(|- TODIE - Saturation);                float UIStep=0.001; float UIMin=0.0; float UIMax=3.0;  string Separation = "ExteriorWeather";> = {1.0};
float3 SEPARATE_VAR(UISSS_Tint)       < string UIGroup = "SSS.ColorGrading"; string UIName=TO_STRING(|- TODIE - Tint);                      string UIWidget="Color";                               string Separation = "ExteriorWeather";> = {1.0, 1.0, 1.0};
float  SEPARATE_VAR(UISSS_OutBlack)   < string UIGroup = "SSS.ColorGrading"; string UIName=TO_STRING(|- TODIE - Out Black);                 float UIStep=0.001; float UIMin=0.0; float UIMax=1.0;  string Separation = "ExteriorWeather";> = {0.0};
float  SEPARATE_VAR(UISSS_OutWhite)   < string UIGroup = "SSS.ColorGrading"; string UIName=TO_STRING(|- TODIE - Out White);                 float UIStep=0.001; float UIMin=0.0; float UIMax=1.0;  string Separation = "ExteriorWeather";> = {1.0};
float  SEPARATE_VAR(UISSS_SDILum)     < string UIGroup = "SSS.ColorGrading"; string UIName=TO_STRING(|- TODIE - SDI (Direct Illumination)); float UIStep=0.001; float UIMin=0.0; float UIMax=5.0;  string Separation = "ExteriorWeather";> = {1.0};


//=============================================================================//
//  Group 3: Translucency & Advanced SSS (DNI-separated)                       //
//=============================================================================//

#elif SHADERGROUP == 3

#ifndef NOTFIRSTTIME
#define NOTFIRSTTIME
UI_WHITESPACE(14)
UI_ELEMENT(_Trans_Header,"    >>>>>>TRANSLUCENCY & ADVANCED<<<<<<")
UI_WHITESPACE(15)

// Physical translucency properties
float UISSS_TransPower    < string UIGroup = "SSS.Translucency"; string UIName="|- Translucency - Power Curve";      float UIStep=0.1;  float UIMin=0.5;  float UIMax=12.0;  > = {4.0};
float UISSS_CurvatureMult < string UIGroup = "SSS.Translucency"; string UIName="|- Curvature - Thickness Mult";      float UIStep=0.1;  float UIMin=0.0;  float UIMax=10.0;  > = {3.0};
float UISSS_CurvatureRad  < string UIGroup = "SSS.Translucency"; string UIName="|- Curvature - Sample Radius";       float UIStep=0.1;  float UIMin=1.0;  float UIMax=8.0;   > = {2.0};
float UISSS_WrapMult      < string UIGroup = "SSS.Translucency"; string UIName="|- Wrap Light - Curvature Mult";     float UIStep=0.1;  float UIMin=0.0;  float UIMax=5.0;   > = {1.5};

UI_SPECIAL_WHITESPACE(2)
#endif //NOTFIRSTTIME

// Per-TOD translucency, subsurface color, and skin softening
float  SEPARATE_VAR(UISSS_TransAmount)  < string UIGroup = "SSS.Translucency"; string UIName=TO_STRING(|- TODIE - Translucency Amount);    float UIStep=0.01;  float UIMin=0.0; float UIMax=3.0;   string Separation = "ExteriorWeather";> = {0.6};
float3 SEPARATE_VAR(UISSS_SubColor)     < string UIGroup = "SSS.Translucency"; string UIName=TO_STRING(|- TODIE - Subsurface Color);       string UIWidget="Color";                                string Separation = "ExteriorWeather";> = {1.0, 0.35, 0.2};
float  SEPARATE_VAR(UISSS_SubShift)     < string UIGroup = "SSS.Translucency"; string UIName=TO_STRING(|- TODIE - Subsurface Color Shift); float UIStep=0.01;  float UIMin=0.0; float UIMax=1.0;   string Separation = "ExteriorWeather";> = {0.25};
float  SEPARATE_VAR(UISSS_Softening)    < string UIGroup = "SSS.Translucency"; string UIName=TO_STRING(|- TODIE - Skin Softening);         float UIStep=0.01;  float UIMin=0.0; float UIMax=1.0;   string Separation = "ExteriorWeather";> = {0.3};
float  SEPARATE_VAR(UISSS_WrapAmount)   < string UIGroup = "SSS.Translucency"; string UIName=TO_STRING(|- TODIE - Wrap Light Amount);      float UIStep=0.01;  float UIMin=0.0; float UIMax=2.0;   string Separation = "ExteriorWeather";> = {0.5};



//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 99

//----------------------------------------------------------------------------------------------//
//                     SKYRIMBRIDGE SSS / PREPASS ENHANCEMENTS                                  //
//----------------------------------------------------------------------------------------------//
// Requires SkyrimBridge SKSE plugin + SkyrimBridge.fxh in Helper/                              //
//----------------------------------------------------------------------------------------------//

#ifdef SKYRIMBRIDGE_FXH

UI_ELEMENT(SB_PP_Header,       "    >>>>>>SKYRIMBRIDGE SSS<<<<<<")

//--- Skin Scatter ---
int SB_PP_Spacer0   <string UIName="|--------- Skin & Scatter"; int UIMin=0; int UIMax=0;> = {0};
float UISBPP_SmoothDayNight  < string UIGroup = "SSS.SkyrimBridge"; string UIName="|- SB - Smooth Day/Night Factor";       float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UISBPP_WetSkinScatter  < string UIGroup = "SSS.SkyrimBridge"; string UIName="|- SB - Wet Skin Scatter Boost";         float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UISBPP_WetScatterMult  < string UIGroup = "SSS.SkyrimBridge"; string UIName="|- SB - Wet Scatter Distance Mult";      float UIMin= 1.0; float UIMax= 2.0; > = { 1.3};
float UISBPP_NightEyeSSS     < string UIGroup = "SSS.SkyrimBridge"; string UIName="|- SB - Night Eye SSS Reduction";        float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UISBPP_NightEyeReduce  < string UIGroup = "SSS.SkyrimBridge"; string UIName="|- SB - Night Eye Skin Reduce Amt";      float UIMin= 0.0; float UIMax= 1.0; > = { 0.5};

//--- Atmosphere ---
int SB_PP_Spacer1   <string UIName="|--------- Atmosphere"; int UIMin=0; int UIMax=0;> = {0};
float UISBPP_InteriorAccurate< string UIGroup = "SSS.SkyrimBridge"; string UIName="|- SB - Accurate Interior Detection";    float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UISBPP_FogBlendAmt     < string UIGroup = "SSS.SkyrimBridge"; string UIName="|- SB - Game Fog Color Blend";           float UIMin= 0.0; float UIMax= 1.0; > = { 0.3};

//--- Skin Reactivity ---
int SB_PP_Spacer2   <string UIName="|--------- Skin Reactivity"; int UIMin=0; int UIMax=0;> = {0};
float UISBPP_AtmosSSS         < string UIGroup = "SSS.SkyrimBridge"; string UIName="|- SB - Atmosphere SSS Tint";           float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UISBPP_AtmosSSSBlend    < string UIGroup = "SSS.SkyrimBridge"; string UIName="|- SB - Atmosphere SSS Blend";          float UIMin= 0.0; float UIMax= 1.0; > = { 0.3};
float UISBPP_DamageFlush      < string UIGroup = "SSS.SkyrimBridge"; string UIName="|- SB - Damage Skin Flush";             float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UISBPP_DmgFlushInt      < string UIGroup = "SSS.SkyrimBridge"; string UIName="|- SB - Damage Flush Intensity";        float UIMin= 0.0; float UIMax= 2.0; > = { 0.5};
float UISBPP_HealthPallor     < string UIGroup = "SSS.SkyrimBridge"; string UIName="|- SB - Low Health Pallor";             float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UISBPP_HealthPallorThr  < string UIGroup = "SSS.SkyrimBridge"; string UIName="|- SB - Pallor Health Threshold";       float UIMin= 0.0; float UIMax= 0.5; > = { 0.25};
float UISBPP_ColdExposure     < string UIGroup = "SSS.SkyrimBridge"; string UIName="|- SB - Cold Exposure Skin Shift";      float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UISBPP_DrunkFlush       < string UIGroup = "SSS.SkyrimBridge"; string UIName="|- SB - Drunk Skin Flush";              float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UISBPP_InteriorSSS      < string UIGroup = "SSS.SkyrimBridge"; string UIName="|- SB - Interior SSS Warmth";           float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};

#endif //SKYRIMBRIDGE_FXH

#endif //SHADERGROUP

#undef TODIE
