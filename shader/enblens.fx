//=============================================================================
//  Master Lens v3.0 — Ground-Up Rewrite for SkyrimBridge
//
//  Based on enblens.fx by LonelyKitsuune (Silent Horizons ENB)
//  For ENB (DirectX 11 Shader Model 5)
//
//  Changes: All UI inline, single technique variant, 8-ghost single-pass,
//  removed Atmospheric Fog / CRT / Cinematic FX addons.
//=============================================================================

//--- Compile-time options ---
#define LOCAL_OVERRIDE                    0
#define STARBURST_USE_MAX_COLOR           0
#define ANAM_ENABLE_MIRRORED_FLARE        0
#define RAINFX_ENABLE_DISPERSION          0
#define FROSTFX_USE_REFRACTION_METHOD     1

#if !LOCAL_OVERRIDE
#undef RAINFX_ENABLE_DISPERSION
#undef STARBURST_USE_MAX_COLOR
#undef FROSTFX_USE_REFRACTION_METHOD
#include "enbglobals.fxh"
#endif

//--- UI macros ---
#include "UI/enbUI_Primer.fxh"



//=============================================================================
//  INLINE UI PARAMETERS
//=============================================================================

//--- Feature Enables ---
UI_WHITESPACE(1)
UI_ELEMENT(ML_Features, "      >>>>>>LENS FEATURE-SET<<<<<<")
UI_WHITESPACE(3)
bool UIWFXRain_Enable  < string UIGroup = "LensFeatures"; string UIName="|- Enable Weather FX - Rain"; > = {false};
bool UIWFXFrost_Enable < string UIGroup = "LensFeatures"; string UIName="|- Enable Weather FX - Frost"; > = {false};
bool UIAF_Enable       < string UIGroup = "LensFeatures"; string UIName="|- Enable Anamorphic Lens Flares"; > = {false};
bool UIR_Enable        < string UIGroup = "LensFeatures"; string UIName="|- Enable Lens Reflections"; > = {false};
bool UID_Enable        < string UIGroup = "LensFeatures"; string UIName="|- Enable Lens Dirt"; > = {false};
bool UICA_Enable       < string UIGroup = "LensFeatures"; string UIName="|- Enable Chromatic Aberration"; > = {false};
bool UILO_Enable       < string UIGroup = "LensFeatures"; string UIName="|- Enable Lens Optics"; > = {false};
bool UIHL_Enable       < string UIGroup = "LensFeatures"; string UIName="|- Enable Halation"; > = {false};
bool UIVG_Enable       < string UIGroup = "LensFeatures"; string UIName="|- Enable Veiling Glare"; > = {false};
bool UIFG_Enable       < string UIGroup = "LensFeatures"; string UIName="|- Enable Film Grain"; > = {false};
bool UISR_Enable       < string UIGroup = "LensFeatures"; string UIName="|- Enable Sensor Response"; > = {false};
bool UI_IgnoreSky      < string UIGroup = "LensFeatures"; string UIName="|- Lensflares ignore Sun/Sky"; > = {false};
UI_WHITESPACE(11)

//--- Weather FX ---
UI_ELEMENT(ML_WeatherFX, "      >>>>>>WEATHER EFFECTS<<<<<<")
UI_WHITESPACE(4)
float  Day_UIWFXRain_Intensity   < string UIGroup = "WeatherFX"; string UIName="|- RainFX - Day - Intensity"; float UIMin=0.0; float UIMax=10.0; > = {1.0};
float4 Day_UIWFXRain_SkyColor    < string UIGroup = "WeatherFX"; string UIName="|- RainFX - Day - Sky Color"; string UIWidget="Color"; > = {1.0, 1.0, 1.0, 1.0};
float4 Day_UIWFXRain_EnvColor    < string UIGroup = "WeatherFX"; string UIName="|- RainFX - Day - Environment Color"; string UIWidget="Color"; > = {0.3, 0.3, 0.3, 1.0};
float  Night_UIWFXRain_Intensity < string UIGroup = "WeatherFX"; string UIName="|- RainFX - Night - Intensity"; float UIMin=0.0; float UIMax=10.0; > = {1.0};
float4 Night_UIWFXRain_SkyColor  < string UIGroup = "WeatherFX"; string UIName="|- RainFX - Night - Sky Color"; string UIWidget="Color"; > = {1.0, 1.0, 1.0, 1.0};
float4 Night_UIWFXRain_EnvColor  < string UIGroup = "WeatherFX"; string UIName="|- RainFX - Night - Environment Color"; string UIWidget="Color"; > = {0.3, 0.3, 0.3, 1.0};
float  Day_UIWFXFrost_Intensity  < string UIGroup = "WeatherFX"; string UIName="|- FrostFX - Day - Intensity"; float UIMin=0.0; float UIMax=5.0; > = {0.5};
float4 Day_UIWFXFrost_SkyColor   < string UIGroup = "WeatherFX"; string UIName="|- FrostFX - Day - Sky Color"; string UIWidget="Color"; > = {1.0, 1.0, 1.0, 1.0};
float4 Day_UIWFXFrost_EnvColor   < string UIGroup = "WeatherFX"; string UIName="|- FrostFX - Day - Environment Color"; string UIWidget="Color"; > = {0.3, 0.3, 0.3, 1.0};
float  Night_UIWFXFrost_Intensity< string UIGroup = "WeatherFX"; string UIName="|- FrostFX - Night - Intensity"; float UIMin=0.0; float UIMax=5.0; > = {0.5};
float4 Night_UIWFXFrost_SkyColor < string UIGroup = "WeatherFX"; string UIName="|- FrostFX - Night - Sky Color"; string UIWidget="Color"; > = {1.0, 1.0, 1.0, 1.0};
float4 Night_UIWFXFrost_EnvColor < string UIGroup = "WeatherFX"; string UIName="|- FrostFX - Night - Environment Color"; string UIWidget="Color"; > = {0.3, 0.3, 0.3, 1.0};
UI_ELEMENT(ML_RainFX, "|--------- Rain Droplets")
float  UIWFXRain_Tickrate       < string UIGroup = "WeatherFX.Rain"; string UIName="|- RainFX - Tickrate"; float UIMin=0.01; float UIMax=10.0; > = {0.15};
float  UIWFXRain_FadeCurve      < string UIGroup = "WeatherFX.Rain"; string UIName="|- RainFX - Fade Curve"; float UIMin=0.01; float UIMax=10.0; > = {1.8};
float  UIWFXRain_MinSize        < string UIGroup = "WeatherFX.Rain"; string UIName="|- RainFX - Min Size"; float UIMin=0.0; float UIMax=10.0; > = {0.03};
float  UIWFXRain_MaxSize        < string UIGroup = "WeatherFX.Rain"; string UIName="|- RainFX - Max Size"; float UIMin=0.01; float UIMax=10.0; > = {0.15};
float  UIWFXRain_MaxDeformation < string UIGroup = "WeatherFX.Rain"; string UIName="|- RainFX - Max Deformation"; float UIMin=0.0; float UIMax=2.0; > = {0.15};
#if RAINFX_ENABLE_DISPERSION
float  UIWFXRain_Dispersion     < string UIGroup = "WeatherFX.Rain"; string UIName="|- RainFX - Dispersion"; float UIMin=0.0; float UIMax=99.0; > = {1.0};
#endif
float  UIWFXRain_DripSpeed      < string UIGroup = "WeatherFX.Rain"; string UIName="|- RainFX - Dripping Speed"; float UIMin=0.0; float UIMax=5.0; > = {0.2};
float  UIWFXRain_DripDrift      < string UIGroup = "WeatherFX.Rain"; string UIName="|- RainFX - Dripping Drift"; float UIMin=0.0; float UIMax=5.0; > = {0.1};
float  UIWFXRain_Curvature      < string UIGroup = "WeatherFX.Rain"; string UIName="|- RainFX - Droplet Curvature"; float UIMin=0.0; float UIMax=2.0; > = {1.0};
float  UIWFXRain_Sigma          < string UIGroup = "WeatherFX.Rain"; string UIName="|- RainFX - Blur Amount"; float UIMin=0.0; float UIMax=20.0; > = {0.0};
UI_ELEMENT(ML_FrostFX, "|--------- Frost Vignette")
float  UIWFXFrost_PulseRate     < string UIGroup = "WeatherFX.Frost"; string UIName="|- FrostFX - Pulse Rate"; float UIMin=0.0; float UIMax=10.0; > = {0.1};
float  UIWFXFrost_PulseStrength < string UIGroup = "WeatherFX.Frost"; string UIName="|- FrostFX - Pulse Strength"; float UIMin=0.0; float UIMax=10.0; > = {0.5};
float  UIWFXFrost_Curve         < string UIGroup = "WeatherFX.Frost"; string UIName="|- FrostFX - Curve"; float UIMin=0.01; float UIMax=15.0; > = {1.5};
float  UIWFXFrost_RadiusInner   < string UIGroup = "WeatherFX.Frost"; string UIName="|- FrostFX - Inner Radius"; float UIMin=0.0; float UIMax=2.0; > = {1.2};
float  UIWFXFrost_RadiusOuter   < string UIGroup = "WeatherFX.Frost"; string UIName="|- FrostFX - Outer Radius"; float UIMin=0.0; float UIMax=2.0; > = {0.5};
float3 UIWFXFrost_Tint          < string UIGroup = "WeatherFX.Frost"; string UIName="|- FrostFX - Tint"; string UIWidget="Color"; > = {1.0, 1.0, 1.0};
#if FROSTFX_USE_REFRACTION_METHOD
float  UIWFXFrost_Opacity       < string UIGroup = "WeatherFX.Frost"; string UIName="|- FrostFX - Opacity"; float UIMin=0.0; float UIMax=99.0; > = {50.0};
float  UIWFXFrost_RefracRange   < string UIGroup = "WeatherFX.Frost"; string UIName="|- FrostFX - Refraction Range"; float UIMin=0.0; float UIMax=1.0; > = {0.2};
bool   UIWFXFrost_AllowOvershoot< string UIGroup = "WeatherFX.Frost"; string UIName="|- FrostFX - Approx. missing Data"; > = {true};
#else
float3 UIWFXFrost_LayerInt      < string UIGroup = "WeatherFX.Frost"; string UIName="|- FrostFX - Layer Intensity"; float UIMin=0.0; float UIMax=1.0; > = {1.0, 1.0, 1.0};
#endif
UI_WHITESPACE(13)

//--- Anamorphic Lensflare (DNI) ---
UI_ELEMENT(ML_AnamFlare, "  >>>>>ANAMORPHIC LENS FLARES<<<<<")
UI_WHITESPACE(5)
float Day_UIAF_Int          < string UIGroup = "AnamorphicFlares"; string UIName="|- AnamFlare - Day - Intensity"; float UIMin=0.0; float UIMax=10.0; > = {1.0};
float Night_UIAF_Int        < string UIGroup = "AnamorphicFlares"; string UIName="|- AnamFlare - Night - Intensity"; float UIMin=0.0; float UIMax=10.0; > = {1.0};
float Interior_UIAF_Int     < string UIGroup = "AnamorphicFlares"; string UIName="|- AnamFlare - Interior - Intensity"; float UIMin=0.0; float UIMax=10.0; > = {1.0};
float Day_UIAF_Thresh       < string UIGroup = "AnamorphicFlares"; string UIName="|- AnamFlare - Day - Threshold"; float UIMin=0.0; float UIMax=20.0; > = {1.0};
float Night_UIAF_Thresh     < string UIGroup = "AnamorphicFlares"; string UIName="|- AnamFlare - Night - Threshold"; float UIMin=0.0; float UIMax=20.0; > = {1.0};
float Interior_UIAF_Thresh  < string UIGroup = "AnamorphicFlares"; string UIName="|- AnamFlare - Interior - Threshold"; float UIMin=0.0; float UIMax=20.0; > = {1.0};
float UIAF_Rotation         < string UIGroup = "AnamorphicFlares"; string UIName="|- AnamFlare - Rotation"; float UIMin=0.0; float UIMax=180.0; float UIStep=1.0; > = {90.0};
float UIAF_CoreFoc          < string UIGroup = "AnamorphicFlares"; string UIName="|- AnamFlare - Core Focus"; float UIMin=0.0; float UIMax=20.0; > = {0.4};
float UIAF_CoreInt          < string UIGroup = "AnamorphicFlares"; string UIName="|- AnamFlare - Core Intensity"; float UIMin=0.0; float UIMax=10.0; > = {1.5};
float UIAF_CoreMax          < string UIGroup = "AnamorphicFlares"; string UIName="|- AnamFlare - Core Max"; float UIMin=0.0; float UIMax=10.0; > = {2.0};
float UIAF_Width            < string UIGroup = "AnamorphicFlares"; string UIName="|- AnamFlare - Width"; float UIMin=1.0; float UIMax=100.0; > = {15.0};
float UIAF_Sat              < string UIGroup = "AnamorphicFlares"; string UIName="|- AnamFlare - Saturation"; float UIMin=0.0; float UIMax=10.0; > = {1.0};
#if ANAM_ENABLE_MIRRORED_FLARE
float UIAF_MirrorInt        < string UIGroup = "AnamorphicFlares"; string UIName="|- AnamFlare - Mirror Intensity"; float UIMin=0.0; float UIMax=10.0; > = {0.5};
#endif
UI_WHITESPACE(15)

//--- Lens Reflections (DNI) ---
UI_ELEMENT(ML_Reflections, "      >>>>>>LENS REFLECTIONS<<<<<<")
UI_WHITESPACE(6)
float Day_UIR_Strength      < string UIGroup = "LensReflections"; string UIName="|- Reflection - Day - Intensity"; float UIMin=0.0; float UIMax=100.0; > = {5.0};
float Night_UIR_Strength    < string UIGroup = "LensReflections"; string UIName="|- Reflection - Night - Intensity"; float UIMin=0.0; float UIMax=100.0; > = {5.0};
float Interior_UIR_Strength < string UIGroup = "LensReflections"; string UIName="|- Reflection - Interior - Intensity"; float UIMin=0.0; float UIMax=100.0; > = {5.0};
float Day_UIR_Power         < string UIGroup = "LensReflections"; string UIName="|- Reflection - Day - Power"; float UIMin=0.0; float UIMax=100.0; > = {1.5};
float Night_UIR_Power       < string UIGroup = "LensReflections"; string UIName="|- Reflection - Night - Power"; float UIMin=0.0; float UIMax=100.0; > = {1.5};
float Interior_UIR_Power    < string UIGroup = "LensReflections"; string UIName="|- Reflection - Interior - Power"; float UIMin=0.0; float UIMax=100.0; > = {1.5};
float3 UIR_ColorFilter1     < string UIGroup = "LensReflections"; string UIName="|- Reflection - Color Filter 1"; string UIWidget="Color"; > = {0.3, 0.4, 0.4};
float3 UIR_ColorFilter2     < string UIGroup = "LensReflections"; string UIName="|- Reflection - Color Filter 2"; string UIWidget="Color"; > = {0.2, 0.4, 0.5};
float3 UIR_ColorFilter3     < string UIGroup = "LensReflections"; string UIName="|- Reflection - Color Filter 3"; string UIWidget="Color"; > = {0.5, 0.3, 0.7};
float3 UIR_ColorFilter4     < string UIGroup = "LensReflections"; string UIName="|- Reflection - Color Filter 4"; string UIWidget="Color"; > = {0.1, 0.2, 0.7};
UI_WHITESPACE(17)

//--- Lens Dirt (DNI) ---
UI_ELEMENT(ML_Dirt, "             >>>>>>LENS DIRT<<<<<<")
UI_WHITESPACE(7)
float Day_UID_Strength      < string UIGroup = "LensDirt"; string UIName="|- Dirt - Day - Intensity"; float UIMin=0.0; float UIMax=100.0; > = {0.5};
float Night_UID_Strength    < string UIGroup = "LensDirt"; string UIName="|- Dirt - Night - Intensity"; float UIMin=0.0; float UIMax=100.0; > = {0.5};
float Interior_UID_Strength < string UIGroup = "LensDirt"; string UIName="|- Dirt - Interior - Intensity"; float UIMin=0.0; float UIMax=100.0; > = {0.5};
float Day_UID_Power         < string UIGroup = "LensDirt"; string UIName="|- Dirt - Day - Power"; float UIMin=0.0; float UIMax=100.0; > = {1.5};
float Night_UID_Power       < string UIGroup = "LensDirt"; string UIName="|- Dirt - Night - Power"; float UIMin=0.0; float UIMax=100.0; > = {1.5};
float Interior_UID_Power    < string UIGroup = "LensDirt"; string UIName="|- Dirt - Interior - Power"; float UIMin=0.0; float UIMax=100.0; > = {1.5};
int   UID_Select            < string UIGroup = "LensDirt"; string UIName="|- Dirt - Select Texture"; int UIMin=1; int UIMax=4; > = {1};
UI_WHITESPACE(19)

//--- Chromatic Aberration ---
UI_ELEMENT(_CA_Header, ">>>>>>CHROMATIC ABERRATION<<<<<<")
UI_WHITESPACE(8)
float UICA_Strength    < string UIGroup = "ChromaticAberration"; string UIName="|- CA - Strength"; float UIMin=-8.0; float UIMax=8.0; > = {1.0};
float UICA_Curve       < string UIGroup = "ChromaticAberration"; string UIName="|- CA - Curve"; float UIMin=0.01; float UIMax=10.0; > = {1.5};
float UICA_Spread      < string UIGroup = "ChromaticAberration.Spectral"; string UIName="|- CA - Spectral Spread"; float UIMin=0.0; float UIMax=3.0; > = {1.0};
float UICA_Barrel      < string UIGroup = "ChromaticAberration.Spectral"; string UIName="|- CA - Barrel Distortion"; float UIMin=-2.0; float UIMax=2.0; > = {0.0};
float UICA_FringeSat   < string UIGroup = "ChromaticAberration.Spectral"; string UIName="|- CA - Fringe Saturation"; float UIMin=0.0; float UIMax=2.0; > = {1.0};
float UICA_RedBias     < string UIGroup = "ChromaticAberration.Tuning"; string UIName="|- CA - Red Channel Bias"; float UIMin=-2.0; float UIMax=2.0; > = {1.0};
float UICA_BlueBias    < string UIGroup = "ChromaticAberration.Tuning"; string UIName="|- CA - Blue Channel Bias"; float UIMin=-2.0; float UIMax=2.0; > = {1.0};
float UICA_Deadzone    < string UIGroup = "ChromaticAberration.Tuning"; string UIName="|- CA - Center Deadzone"; float UIMin=0.0; float UIMax=0.5; > = {0.05};
UI_WHITESPACE(21)

//--- Starburst (DNI) ---
UI_ELEMENT(ML_Starburst, ">>>>>>STARBURST LENS FLARES<<<<<<")
UI_WHITESPACE(9)
float Day_UISB_Strength      < string UIGroup = "StarburstFlares"; string UIName="|- Starburst - Day - Intensity"; float UIMin=0.0; float UIMax=100.0; > = {1.0};
float Night_UISB_Strength    < string UIGroup = "StarburstFlares"; string UIName="|- Starburst - Night - Intensity"; float UIMin=0.0; float UIMax=100.0; > = {1.0};
float Interior_UISB_Strength < string UIGroup = "StarburstFlares"; string UIName="|- Starburst - Interior - Intensity"; float UIMin=0.0; float UIMax=100.0; > = {1.0};
float Day_UISB_Thresh        < string UIGroup = "StarburstFlares"; string UIName="|- Starburst - Day - Threshold"; float UIMin=0.1; float UIMax=100.0; > = {5.0};
float Night_UISB_Thresh      < string UIGroup = "StarburstFlares"; string UIName="|- Starburst - Night - Threshold"; float UIMin=0.1; float UIMax=100.0; > = {5.0};
float Interior_UISB_Thresh   < string UIGroup = "StarburstFlares"; string UIName="|- Starburst - Interior - Threshold"; float UIMin=0.1; float UIMax=100.0; > = {5.0};
bool  UISB_HQ               < string UIGroup = "StarburstFlares"; string UIName="|- Starburst - Enable HQ Mode"; > = {false};
float UISB_Shape             < string UIGroup = "StarburstFlares"; string UIName="|- Starburst - Lens Shape"; float UIMin=4.0; float UIMax=9.0; float UIStep=1; > = {0.0};
float UISB_AngleOffset       < string UIGroup = "StarburstFlares"; string UIName="|- Starburst - Lens Angle"; float UIMin=0.0; float UIMax=360.0; float UIStep=1; > = {0.0};
float UISB_Width             < string UIGroup = "StarburstFlares"; string UIName="|- Starburst - Scale"; float UIMin=0.0; > = {20.0};
float UISB_ThreshCurve       < string UIGroup = "StarburstFlares"; string UIName="|- Starburst - Threshold Curve"; float UIMin=0.0; float UIMax=30.0; > = {2.0};
float UISB_Falloff           < string UIGroup = "StarburstFlares"; string UIName="|- Starburst - Falloff"; float UIMin=0.5; float UIMax=2.0; > = {1.0};
float UISB_Imperfections     < string UIGroup = "StarburstFlares"; string UIName="|- Starburst - Imperfections"; float UIMin=0.0; float UIMax=10.0; > = {0.0};
float UISB_Saturation        < string UIGroup = "StarburstFlares"; string UIName="|- Starburst - Saturation"; float UIMin=0.0; float UIMax=10.0; > = {1.5};
float4 UISB_Tint             < string UIGroup = "StarburstFlares"; string UIName="|- Starburst - Tint"; string UIWidget="Color"; > = {1.0, 1.0, 1.0, 0.0};
UI_WHITESPACE(23)

//--- Ghost Lens Flares (DNI) ---
UI_ELEMENT(ML_Ghosts, "     >>>>>>GHOST LENS FLARES<<<<<<")
UI_WHITESPACE(10)
float Day_UIG_Strength      < string UIGroup = "GhostFlares"; string UIName="|- Ghosts - Day - Intensity"; float UIMin=0.0; float UIMax=100.0; > = {1.0};
float Night_UIG_Strength    < string UIGroup = "GhostFlares"; string UIName="|- Ghosts - Night - Intensity"; float UIMin=0.0; float UIMax=100.0; > = {1.0};
float Interior_UIG_Strength < string UIGroup = "GhostFlares"; string UIName="|- Ghosts - Interior - Intensity"; float UIMin=0.0; float UIMax=100.0; > = {1.0};
float Day_UIG_Power         < string UIGroup = "GhostFlares"; string UIName="|- Ghosts - Day - Power"; float UIMin=0.0; float UIMax=100.0; > = {8.0};
float Night_UIG_Power       < string UIGroup = "GhostFlares"; string UIName="|- Ghosts - Night - Power"; float UIMin=0.0; float UIMax=100.0; > = {8.0};
float Interior_UIG_Power    < string UIGroup = "GhostFlares"; string UIName="|- Ghosts - Interior - Power"; float UIMin=0.0; float UIMax=100.0; > = {8.0};
float UIG_Grain             < string UIGroup = "GhostFlares"; string UIName="|- Ghosts - Grain"; float UIMin=0.0; > = {0.2};
UI_ELEMENT(ML_Flare1, "|- Ghosts --------- Flares 1")
float4 Intensity1           < string UIGroup = "GhostFlares.Set1"; string UIName="|- Ghosts - Intensity1"; > = {1.0, 1.0, 1.0, 1.0};
float4 Curve1               < string UIGroup = "GhostFlares.Set1"; string UIName="|- Ghosts - Curve1"; > = {1.0, 1.25, 1.0, 1.0};
float4 Scale1               < string UIGroup = "GhostFlares.Set1"; string UIName="|- Ghosts - Scale1"; > = {0.0, -0.30, -0.5, 1.0};
float4 Tint1                < string UIGroup = "GhostFlares.Set1"; string UIName="|- Ghosts - Tint1"; string UIWidget="Color"; > = {0.25, 0.25, 1.0, 1.0};
UI_ELEMENT(ML_Flare2, "|- Ghosts --------- Flares 2")
float4 Intensity2           < string UIGroup = "GhostFlares.Set2"; string UIName="|- Ghosts - Intensity2"; > = {1.0, 1.0, 1.0, 1.0};
float4 Curve2               < string UIGroup = "GhostFlares.Set2"; string UIName="|- Ghosts - Curve2"; > = {1.4, 1.0, 1.0, 0.7};
float4 Scale2               < string UIGroup = "GhostFlares.Set2"; string UIName="|- Ghosts - Scale2"; > = {0.66, 2.0, -0.66, -0.5};
float4 Tint2                < string UIGroup = "GhostFlares.Set2"; string UIName="|- Ghosts - Tint2"; string UIWidget="Color"; > = {0.66, 1.0, 1.0, 0.25};
UI_WHITESPACE(25)

//--- Film Grain (DNI) ---
UI_ELEMENT(ML_FilmGrain, "        >>>>>>FILM GRAIN<<<<<<")
float Day_UIFG_Intensity      < string UIGroup = "FilmGrain"; string UIName="|- Film Grain - Day - Intensity"; float UIMin=0.0; float UIMax=1.0; > = {0.12};
float Night_UIFG_Intensity    < string UIGroup = "FilmGrain"; string UIName="|- Film Grain - Night - Intensity"; float UIMin=0.0; float UIMax=1.0; > = {0.12};
float Interior_UIFG_Intensity < string UIGroup = "FilmGrain"; string UIName="|- Film Grain - Interior - Intensity"; float UIMin=0.0; float UIMax=1.0; > = {0.12};
UI_ELEMENT(ML_FGStructure, "|--------- Grain Structure")
float  UIFG_Size          < string UIGroup = "FilmGrain.Structure"; string UIName="|- Film Grain - Size"; float UIMin=0.5; float UIMax=4.0; > = {1.0};
float  UIFG_Roughness     < string UIGroup = "FilmGrain.Structure"; string UIName="|- Film Grain - Roughness"; float UIMin=0.0; float UIMax=1.0; > = {0.5};
float  UIFG_AnimSpeed     < string UIGroup = "FilmGrain.Structure"; string UIName="|- Film Grain - Animation Speed"; float UIMin=0.0; float UIMax=2.0; > = {1.0};
UI_ELEMENT(ML_FGResponse, "|--------- Luminance Response")
float  UIFG_ShadowGrain   < string UIGroup = "FilmGrain.Response"; string UIName="|- Film Grain - Shadow Grain"; float UIMin=0.0; float UIMax=2.0; > = {1.4};
float  UIFG_MidtoneGrain  < string UIGroup = "FilmGrain.Response"; string UIName="|- Film Grain - Midtone Grain"; float UIMin=0.0; float UIMax=2.0; > = {1.0};
float  UIFG_HighlightGrain< string UIGroup = "FilmGrain.Response"; string UIName="|- Film Grain - Highlight Grain"; float UIMin=0.0; float UIMax=2.0; > = {0.3};
UI_ELEMENT(ML_FGEmulsion, "|--------- Emulsion Layers")
float3 UIFG_ChannelWeight < string UIGroup = "FilmGrain.Emulsion"; string UIName="|- Film Grain - RGB Weight"; float UIMin=0.0; float UIMax=2.0; > = {0.95, 1.0, 1.15};
float  UIFG_ChannelDecorr < string UIGroup = "FilmGrain.Emulsion"; string UIName="|- Film Grain - Channel Decorrelation"; float UIMin=0.0; float UIMax=1.0; > = {0.35};
UI_WHITESPACE(26)

//--- Lens Optics ---
UI_ELEMENT(ML_LensOptics, "        >>>>>>LENS OPTICS<<<<<<")
UI_ELEMENT(_LO_Distort, "|--------- Barrel/Pincushion Distortion")
float  UILO_DistortK1     < string UIGroup = "LensOptics.Distortion"; string UIName="|- Optics - Radial K1"; float UIMin=-1.0; float UIMax=1.0; float UIStep=0.001; > = {0.0};
float  UILO_DistortK2     < string UIGroup = "LensOptics.Distortion"; string UIName="|- Optics - Radial K2"; float UIMin=-1.0; float UIMax=1.0; float UIStep=0.001; > = {0.0};
float  UILO_DistortK3     < string UIGroup = "LensOptics.Distortion"; string UIName="|- Optics - Radial K3"; float UIMin=-0.5; float UIMax=0.5; float UIStep=0.001; > = {0.0};
float  UILO_DistortP1     < string UIGroup = "LensOptics.Distortion"; string UIName="|- Optics - Tangential P1"; float UIMin=-0.1; float UIMax=0.1; float UIStep=0.001; > = {0.0};
float  UILO_DistortP2     < string UIGroup = "LensOptics.Distortion"; string UIName="|- Optics - Tangential P2"; float UIMin=-0.1; float UIMax=0.1; float UIStep=0.001; > = {0.0};
UI_ELEMENT(_LO_Vignette, "|--------- Optical Vignetting")
float  UILO_VigNatural    < string UIGroup = "LensOptics.Vignette"; string UIName="|- Optics - Natural Vignette (cos4)"; float UIMin=0.0; float UIMax=3.0; > = {0.0};
float  UILO_VigMechanical < string UIGroup = "LensOptics.Vignette"; string UIName="|- Optics - Mechanical Vignette"; float UIMin=0.0; float UIMax=2.0; > = {0.0};
float  UILO_VigMechRatio  < string UIGroup = "LensOptics.Vignette"; string UIName="|- Optics - Mech. Aspect Ratio"; float UIMin=0.5; float UIMax=2.0; > = {1.0};
float  UILO_VigRoundness  < string UIGroup = "LensOptics.Vignette"; string UIName="|- Optics - Mech. Roundness"; float UIMin=0.0; float UIMax=1.0; > = {0.8};
float3 UILO_VigColorShift < string UIGroup = "LensOptics.Vignette"; string UIName="|- Optics - Edge Color Shift"; string UIWidget="Color"; > = {1.0, 0.95, 0.85};
float  UILO_VigColorAmt   < string UIGroup = "LensOptics.Vignette"; string UIName="|- Optics - Color Shift Amount"; float UIMin=0.0; float UIMax=2.0; > = {0.0};
UI_ELEMENT(_LO_Coma, "|--------- Coma Aberration")
float  UILO_ComaStrength  < string UIGroup = "LensOptics.Coma"; string UIName="|- Optics - Coma Strength"; float UIMin=0.0; float UIMax=5.0; > = {0.0};
float  UILO_ComaFalloff   < string UIGroup = "LensOptics.Coma"; string UIName="|- Optics - Coma Falloff"; float UIMin=0.5; float UIMax=5.0; > = {2.5};
float  UILO_ComaSamples   < string UIGroup = "LensOptics.Coma"; string UIName="|- Optics - Coma Quality"; float UIMin=3.0; float UIMax=12.0; float UIStep=1.0; > = {6.0};
UI_ELEMENT(_LO_Spherical, "|--------- Spherical Aberration")
float  UILO_SphericalStr  < string UIGroup = "LensOptics.Spherical"; string UIName="|- Optics - Spherical Strength"; float UIMin=0.0; float UIMax=3.0; > = {0.0};
float  UILO_SphericalBias < string UIGroup = "LensOptics.Spherical"; string UIName="|- Optics - Spherical Luma Bias"; float UIMin=0.0; float UIMax=5.0; > = {1.5};
float  UILO_SphericalRadius< string UIGroup = "LensOptics.Spherical"; string UIName="|- Optics - Spherical Radius"; float UIMin=0.5; float UIMax=8.0; > = {2.0};
UI_ELEMENT(_LO_FieldCurve, "|--------- Field Curvature")
float  UILO_FieldCurveStr < string UIGroup = "LensOptics.FieldCurve"; string UIName="|- Optics - Field Curvature"; float UIMin=0.0; float UIMax=3.0; > = {0.0};
float  UILO_FieldCurveExp < string UIGroup = "LensOptics.FieldCurve"; string UIName="|- Optics - Curvature Falloff"; float UIMin=1.0; float UIMax=5.0; > = {2.0};
UI_WHITESPACE(27)

//--- Halation + Veiling Glare (DNI) ---
UI_ELEMENT(ML_Halation, "          >>>>>>HALATION<<<<<<")
float Day_UIHL_Intensity      < string UIGroup = "Halation"; string UIName="|- Halation - Day - Intensity"; float UIMin=0.0; float UIMax=2.0; > = {0.3};
float Night_UIHL_Intensity    < string UIGroup = "Halation"; string UIName="|- Halation - Night - Intensity"; float UIMin=0.0; float UIMax=2.0; > = {0.3};
float Interior_UIHL_Intensity < string UIGroup = "Halation"; string UIName="|- Halation - Interior - Intensity"; float UIMin=0.0; float UIMax=2.0; > = {0.3};
UI_ELEMENT(_HL_Settings, "|--------- Halation Settings")
float  UIHL_Threshold     < string UIGroup = "Halation.Settings"; string UIName="|- Halation - Threshold"; float UIMin=0.0; float UIMax=5.0; > = {0.8};
float  UIHL_ThreshCurve   < string UIGroup = "Halation.Settings"; string UIName="|- Halation - Threshold Curve"; float UIMin=0.1; float UIMax=5.0; > = {2.0};
float  UIHL_Radius        < string UIGroup = "Halation.Settings"; string UIName="|- Halation - Spread Radius"; float UIMin=1.0; float UIMax=32.0; > = {8.0};
float3 UIHL_Color         < string UIGroup = "Halation.Settings"; string UIName="|- Halation - Color Tint"; string UIWidget="Color"; > = {1.0, 0.35, 0.15};
float  UIHL_Saturation    < string UIGroup = "Halation.Settings"; string UIName="|- Halation - Saturation"; float UIMin=0.0; float UIMax=2.0; > = {0.8};
UI_ELEMENT(_VG_Settings, "|--------- Veiling Glare")
float  UIVG_Intensity     < string UIGroup = "Halation.VeilingGlare"; string UIName="|- Veiling Glare - Intensity"; float UIMin=0.0; float UIMax=2.0; > = {0.15};
float  UIVG_Threshold     < string UIGroup = "Halation.VeilingGlare"; string UIName="|- Veiling Glare - Threshold"; float UIMin=0.0; float UIMax=5.0; > = {1.2};
float  UIVG_Radius        < string UIGroup = "Halation.VeilingGlare"; string UIName="|- Veiling Glare - Spread"; float UIMin=2.0; float UIMax=64.0; > = {24.0};
float3 UIVG_Tint          < string UIGroup = "Halation.VeilingGlare"; string UIName="|- Veiling Glare - Tint"; string UIWidget="Color"; > = {1.0, 0.98, 0.92};
UI_WHITESPACE(28)

//--- Sensor Response ---
UI_ELEMENT(ML_Sensor, "      >>>>>>SENSOR RESPONSE<<<<<<")
UI_ELEMENT(_SR_Curve, "|--------- Film Response Curve")
float  UISR_Shoulder      < string UIGroup = "SensorResponse.Curve"; string UIName="|- Sensor - Highlight Shoulder"; float UIMin=0.0; float UIMax=3.0; > = {0.0};
float  UISR_ShoulderStart < string UIGroup = "SensorResponse.Curve"; string UIName="|- Sensor - Shoulder Start"; float UIMin=0.3; float UIMax=2.0; > = {0.8};
float  UISR_Toe           < string UIGroup = "SensorResponse.Curve"; string UIName="|- Sensor - Shadow Toe"; float UIMin=0.0; float UIMax=2.0; > = {0.0};
float  UISR_ToeEnd        < string UIGroup = "SensorResponse.Curve"; string UIName="|- Sensor - Toe End"; float UIMin=0.01; float UIMax=0.5; > = {0.1};
UI_ELEMENT(_SR_Crosstalk, "|--------- Channel Crosstalk")
float  UISR_Crosstalk     < string UIGroup = "SensorResponse.Crosstalk"; string UIName="|- Sensor - Crosstalk Amount"; float UIMin=0.0; float UIMax=0.5; > = {0.0};
float  UISR_CrossRtoG     < string UIGroup = "SensorResponse.Crosstalk"; string UIName="|- Sensor - Red to Green Bleed"; float UIMin=0.0; float UIMax=1.0; > = {0.3};
float  UISR_CrossGtoB     < string UIGroup = "SensorResponse.Crosstalk"; string UIName="|- Sensor - Green to Blue Bleed"; float UIMin=0.0; float UIMax=1.0; > = {0.25};
float  UISR_CrossBtoR     < string UIGroup = "SensorResponse.Crosstalk"; string UIName="|- Sensor - Blue to Red Bleed"; float UIMin=0.0; float UIMax=1.0; > = {0.15};
UI_ELEMENT(_SR_Noise, "|--------- Sensor Noise")
float  UISR_ReadNoise     < string UIGroup = "SensorResponse.Noise"; string UIName="|- Sensor - Read Noise"; float UIMin=0.0; float UIMax=0.05; > = {0.0};
float  UISR_HotPixels     < string UIGroup = "SensorResponse.Noise"; string UIName="|- Sensor - Hot Pixel Rate"; float UIMin=0.0; float UIMax=0.01; > = {0.0};
float  UISR_PhotonNoise   < string UIGroup = "SensorResponse.Noise"; string UIName="|- Sensor - Photon (Shot) Noise"; float UIMin=0.0; float UIMax=0.1; > = {0.0};

//--- Purple Fringing ---
UI_WHITESPACE(50)
bool  UIPF_Enable     <string UIName="Purple Fringing - Enable"; > = {false};
float UIPF_Strength   <string UIName="|- PF - Strength"; float UIMin=0.0; float UIMax=2.0; > = {0.5};
float UIPF_Threshold  <string UIName="|- PF - Edge Threshold"; float UIMin=0.0; float UIMax=1.0; > = {0.15};
float UIPF_Spread     <string UIName="|- PF - Spread (pixels)"; float UIMin=1.0; float UIMax=8.0; > = {3.0};
float UIPF_Bias       <string UIName="|- PF - Purple/Green Bias"; float UIMin=-1.0; float UIMax=1.0; > = {0.3};
float UIPF_RadialFade <string UIName="|- PF - Radial Increase"; float UIMin=0.0; float UIMax=2.0; > = {0.8};

//--- Lens Breathing ---
bool  UILB_Enable     <string UIName="Lens Breathing - Enable"; > = {false};
float UILB_Amount     <string UIName="|- Breathing - Amount"; float UIMin=0.0; float UIMax=0.05; > = {0.01};
float UILB_Speed      <string UIName="|- Breathing - Smoothing"; float UIMin=0.1; float UIMax=4.0; > = {1.0};
float UILB_Center     <string UIName="|- Breathing - Neutral EV"; float UIMin=0.0; float UIMax=1.0; > = {0.3};

//--- Astigmatism ---
bool  UIAST_Enable    <string UIName="Astigmatism - Enable"; > = {false};
float UIAST_Strength  <string UIName="|- Astigmatism - Strength"; float UIMin=0.0; float UIMax=3.0; > = {0.5};
float UIAST_Onset     <string UIName="|- Astigmatism - Onset Radius"; float UIMin=0.1; float UIMax=1.0; > = {0.3};
float UIAST_Falloff   <string UIName="|- Astigmatism - Falloff"; float UIMin=1.0; float UIMax=4.0; > = {2.0};
int   UIAST_Samples   <string UIName="|- Astigmatism - Samples"; int UIMin=4; int UIMax=12; > = {6};


//=============================================================================
//  ENB EXTERNAL PARAMETERS
//=============================================================================
float4 Timer;
float4 ScreenSize;
float  AdaptiveQuality;
float4 TimeOfDay1;
float4 TimeOfDay2;
float  ENightDayFactor;
float  EInteriorFactor;
float  FieldOfView;
float4 Weather;

//--- SkyrimBridge data parameters (must be after Timer for SB_Retain) ---
#define SB_WEATHER_PARAMS
#include "Helper/SkyrimBridge.fxh"

//--- SkyrimBridge Lens ---
#ifdef SKYRIMBRIDGE_FXH
UI_ELEMENT(SB_LENS_Header, "    >>>>>>SKYRIMBRIDGE LENS<<<<<<")
int SB_LENS_Spacer0   <string UIName="|--------- Weather Integration"; int UIMin=0; int UIMax=0;> = {0};
float UIBRIDGE_RainFromWeather  < string UIGroup = "SB.Lens"; string UIName="|- SB - Rain Intensity From Weather"; float UIMin=0.0; float UIMax=1.0; > = {1.0};
float UIBRIDGE_FrostFromSnow   < string UIGroup = "SB.Lens"; string UIName="|- SB - Frost From Cold Weather"; float UIMin=0.0; float UIMax=1.0; > = {1.0};
float UIBRIDGE_InteriorAccurate< string UIGroup = "SB.Lens"; string UIName="|- SB - Accurate Interior Detection"; float UIMin=0.0; float UIMax=1.0; > = {1.0};
float UIBRIDGE_WindRain        < string UIGroup = "SB.Lens"; string UIName="|- SB - Wind-Directed Rain"; float UIMin=0.0; float UIMax=1.0; > = {1.0};
int SB_LENS_Spacer1   <string UIName="|--------- Lightning + Damage FX"; int UIMin=0; int UIMax=0;> = {0};
float UIBRIDGE_LightningFlash  < string UIGroup = "SB.Lens"; string UIName="|- SB - Lightning Flash Burst"; float UIMin=0.0; float UIMax=1.0; > = {1.0};
float UIBRIDGE_LightningInt    < string UIGroup = "SB.Lens"; string UIName="|- SB - Lightning Intensity"; float UIMin=0.0; float UIMax=5.0; > = {1.5};
float UIBRIDGE_DamageFX        < string UIGroup = "SB.Lens"; string UIName="|- SB - Damage Screen FX"; float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UIBRIDGE_DmgFireInt      < string UIGroup = "SB.Lens"; string UIName="|- SB - Fire Damage Intensity"; float UIMin=0.0; float UIMax=3.0; > = {1.0};
float UIBRIDGE_DmgFrostInt     < string UIGroup = "SB.Lens"; string UIName="|- SB - Frost Damage Intensity"; float UIMin=0.0; float UIMax=3.0; > = {1.0};
float UIBRIDGE_DmgShockInt     < string UIGroup = "SB.Lens"; string UIName="|- SB - Shock Damage Intensity"; float UIMin=0.0; float UIMax=3.0; > = {1.0};
int SB_LENS_Spacer2   <string UIName="|--------- Lens Dirt Enhancement"; int UIMin=0; int UIMax=0;> = {0};
float UIBRIDGE_WetDirt         < string UIGroup = "SB.Lens"; string UIName="|- SB - Rain Wet Dirt Enable"; float UIMin=0.0; float UIMax=1.0; > = {1.0};
float UIBRIDGE_WetDirtMult     < string UIGroup = "SB.Lens"; string UIName="|- SB - Wet Dirt Multiplier"; float UIMin=1.0; float UIMax=5.0; > = {2.0};
#endif


float4 tempF1;
float4 tempF2;
float4 tempF3;
float4 tempInfo1;
float4 tempInfo2;


//=============================================================================
//  SKYRIMBRIDGE LENS HELPERS
//=============================================================================
#ifdef SKYRIMBRIDGE_FXH

bool SB_LENS_IsInterior()
{
    bool result = (bool)EInteriorFactor;
    if(SB_IsActive() && UIBRIDGE_InteriorAccurate >= 0.5)
        result = SB_Interior_Flags.x > 0.5;
    return result;
}

float SB_LENS_GetRainIntensity()
{
    float result = 1.0;
    if(SB_IsActive() && UIBRIDGE_RainFromWeather >= 0.5)
        result = saturate(SB_Precipitation.y);
    return result;
}

bool SB_LENS_ShouldEnableFrost()
{
    bool result = false;
    if(SB_IsActive() && UIBRIDGE_FrostFromSnow >= 0.5)
        result = SB_Weather_Flags.w > 0.5;
    return result;
}

float SB_LENS_GetFogSuppression()
{
    float result = 1.0;
    if(SB_IsActive())
        result = saturate(1.0 - SB_Fog_Density.y * 0.7);
    return result;
}

float3 SB_LENS_GetWind()
{
    float3 result = float3(0.0, 0.0, 0.0);
    if(SB_IsActive() && UIBRIDGE_WindRain >= 0.5)
        result = float3(SB_Wind.xy, SB_Wind.z);
    return result;
}

float SB_LENS_GetLightningFlash()
{
    float result = 0.0;
    if(SB_IsActive() && UIBRIDGE_LightningFlash >= 0.5)
        result = SB_Lightning.z * UIBRIDGE_LightningInt;
    return result;
}

float3 SB_LENS_GetDamageIntensity()
{
    float3 result = float3(0.0, 0.0, 0.0);
    if(SB_IsActive() && UIBRIDGE_DamageFX >= 0.5)
        result = float3(SB_FX_Damage.x * UIBRIDGE_DmgFireInt,
                        SB_FX_Damage.y * UIBRIDGE_DmgFrostInt,
                        SB_FX_Damage.z * UIBRIDGE_DmgShockInt);
    return result;
}

float SB_LENS_GetWetDirtMult()
{
    float result = 1.0;
    if(SB_IsActive() && UIBRIDGE_WetDirt >= 0.5)
    {
        float Wetness = saturate(SB_Precip_Surface.x);
        result = lerp(1.0, UIBRIDGE_WetDirtMult, Wetness);
    }
    return result;
}

#endif //SKYRIMBRIDGE_FXH


//=============================================================================
//  TEXTURES AND SAMPLERS
//=============================================================================

Texture2D TextureDownsampled;
Texture2D TextureColor;
Texture2D TextureOriginal;
Texture2D TextureDepth;
Texture2D TextureAperture;

Texture2D RenderTarget1024;
Texture2D RenderTarget512;
Texture2D RenderTarget256;
Texture2D RenderTarget128;
Texture2D RenderTarget64;
Texture2D RenderTarget32;
Texture2D RenderTarget16;
Texture2D RenderTargetRGBA32;
Texture2D RenderTargetRGBA64F;

Texture2D LensDirtAtlas <string ResourceName="Textures/LensDirtTexAtlas.png";>;

SamplerState Point_Sampler
{ Filter = MIN_MAG_MIP_POINT; AddressU = Border; AddressV = Border; };

SamplerState Linear_Sampler
{ Filter = MIN_MAG_MIP_LINEAR; AddressU = Border; AddressV = Border; };

SamplerState Point_Sampler_Rain
{ Filter = MIN_MAG_MIP_POINT; AddressU = Mirror; AddressV = Mirror; };

SamplerState Linear_Sampler_Rain
{ Filter = MIN_MAG_MIP_POINT; AddressU = Mirror; AddressV = Mirror; };

SamplerState Linear_Sampler_CA
{ Filter = MIN_MAG_MIP_LINEAR; AddressU = Clamp; AddressV = Clamp; };

SamplerComparisonState Linear_Sampler_Grt
{ Filter = COMPARISON_MIN_MAG_MIP_LINEAR; AddressU = Clamp; AddressV = Clamp; ComparisonFunc = Greater; };

float MaskDaySky(float2 Coords)
{ return saturate(TextureDepth.SampleCmpLevelZero(Linear_Sampler_Grt,Coords,1.0).x + (1.0 - ENightDayFactor)); }

BlendState RainBlending
{ BlendEnable[0] = TRUE; SrcBlend = ONE; DestBlend = ZERO; BlendOp = ADD; };


//=============================================================================
//  HELPER INCLUDES
//=============================================================================
#include "Helper/enbHelper_Common.fxh"

#include "Addons/Effect_ProceduralWeatherFX.fxh"
#include "Addons/Effect_ProceduralLensDirt.fxh"

static const float SqrtTwoPI = sqrt(2.0 * PI);


//=============================================================================
//  GENERIC STRUCTS
//=============================================================================

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

//=============================================================================
//  ANAMORPHIC LENS FLARE
//=============================================================================

struct VertexShaderOutputAnamFlarePrePass
{
   float4 pos         : SV_POSITION;
   float2 texcoord    : TEXCOORD0;
NI float2 UIAF_Thresh : TEXCOORD1;
};

struct VertexShaderOutputAnamFlare
{
   float4 pos         : SV_POSITION;
   float2 texcoord    : TEXCOORD0;
NI float  UIAF_Int    : ANAM0;
NI float  AnamWidth   : ANAM1;
NI float  WeightFactor: ANAM2;
NI float  LoopCount   : ANAM3;
NI float2 RotPixelSize: ANAM4;
};

static const float2 AF_RTSize      = 256.0;
static const float2 AF_RTPixelSize = 1.0 / 256.0;
static       float2 AF_RTResMult   = rcp(ScreenRes * AF_RTPixelSize);


VertexShaderOutputAnamFlarePrePass VS_AnamFlarePrePass(VertexShaderInput IN)
{
	VertexShaderOutputAnamFlarePrePass OUT;
	OUT.pos      = float4(IN.pos.xyz, 1.0);
	OUT.texcoord = IN.txcoord.xy;
	ScaleScreenQuad_Res(OUT.pos.xy, AF_RTSize);
	OUT.UIAF_Thresh.x = DNI_SEPARATION(UIAF_Thresh);
	OUT.UIAF_Thresh.y = OUT.UIAF_Thresh.x + UIAF_CoreFoc;
	return OUT;
}

float4 PS_AnamFlarePrePass(VertexShaderOutputAnamFlarePrePass IN) : SV_Target
{
	if(!UIAF_Enable) discard;
	float3 Color = 0.0;
	static const float2 SourcePixelSize = 1.0 / 1024.0;
	static const float2 Offset[4] =
	{ SourcePixelSize * float2( 1.0, 1.0), SourcePixelSize * float2(-1.0, 1.0),
	  SourcePixelSize * float2( 1.0,-1.0), SourcePixelSize * float2(-1.0,-1.0) };

	[branch] if (UI_IgnoreSky)
	[unroll] for(int i=0; i<4; i++)
	{
		float2 CurrOffset = IN.texcoord + Offset[i];
		Color = max(Color, MaskDaySky(CurrOffset) * TextureDownsampled.SampleLevel(Linear_Sampler, CurrOffset, 0).rgb);
	}
	else
	[unroll] for(int i=0; i<4; i++)
		Color = max(Color, TextureDownsampled.SampleLevel(Linear_Sampler, IN.texcoord + Offset[i], 0).rgb);

	float Luma = dot(Color, N_LUM);
	Color /= deltalim(Luma);
	Luma  *= smoothstep(IN.UIAF_Thresh.x, IN.UIAF_Thresh.y, Luma);
	Luma  *= min(Luma, UIAF_CoreMax);
	Color *= pow(min(Luma, UIAF_CoreMax * 4.0), UIAF_CoreInt);
	return float4(Color, 1.0);
}


VertexShaderOutputAnamFlare VS_AnamFlare(VertexShaderInput IN)
{
	VertexShaderOutputAnamFlare OUT;
	OUT.pos      = float4(IN.pos.xyz, 1.0);
	OUT.texcoord = IN.txcoord.xy * AF_RTResMult;
	OUT.UIAF_Int     = DNI_SEPARATION(UIAF_Int);
	OUT.RotPixelSize = GetDirVec(UIAF_Rotation) * PixelSize * float2(1.0, ScreenSize.z);
	OUT.AnamWidth    = UIAF_Width * 2.0;
	OUT.WeightFactor = rcp(OUT.AnamWidth * SqrtTwoPI);
	OUT.LoopCount    = ceil(mad(OUT.AnamWidth, 2.0, 2.0));
	OUT.AnamWidth    = -rcp(OUT.AnamWidth * OUT.AnamWidth);
	return OUT;
}

float4 PS_AnamFlare(VertexShaderOutputAnamFlare IN) : SV_Target
{
	if(!UIAF_Enable) discard;
	float3 AnamFlare = IN.WeightFactor * RenderTargetRGBA64F.SampleLevel(Linear_Sampler, IN.texcoord, 0).rgb;
	float  WeightSum = IN.WeightFactor;

	#if ANAM_ENABLE_MIRRORED_FLARE
		AnamFlare += UIAF_MirrorInt * IN.WeightFactor *
		RenderTargetRGBA64F.SampleLevel(Linear_Sampler, AF_RTResMult - IN.texcoord, 0).rgb;
	#endif

	[loop] for(float i=1.0; i <= IN.LoopCount; i++)
	{
		float4 Offset      = IN.texcoord.xyxy + IN.RotPixelSize.xyxy * float4(i.xx, -i.xx);
		float  GaussWeight = IN.WeightFactor * exp(i*i*IN.AnamWidth);
		AnamFlare += GaussWeight * RenderTargetRGBA64F.SampleLevel(Linear_Sampler, Offset.xy, 0).rgb;
		AnamFlare += GaussWeight * RenderTargetRGBA64F.SampleLevel(Linear_Sampler, Offset.zw, 0).rgb;
		WeightSum += GaussWeight * 2.0;
		#if ANAM_ENABLE_MIRRORED_FLARE
			Offset       = AF_RTResMult.xyxy - Offset;
			GaussWeight *= UIAF_MirrorInt;
			AnamFlare   += GaussWeight * RenderTargetRGBA64F.SampleLevel(Linear_Sampler, Offset.xy, 0).rgb;
			AnamFlare   += GaussWeight * RenderTargetRGBA64F.SampleLevel(Linear_Sampler, Offset.zw, 0).rgb;
		#endif
	}

	AnamFlare /= WeightSum;
	AnamFlare *= IN.UIAF_Int;
	float AnamLuma = dot(AnamFlare, K_LUM);
	AnamFlare = zerolim(lerp(AnamLuma, AnamFlare, UIAF_Sat));
	return float4(AnamFlare, saturate(0.25 + (AnamLuma * 2.0 - 1.0) * 0.25));
}


//=============================================================================
//  GHOST LENS FLARES — Single-Pass Accumulation (8 ghosts)
//=============================================================================

#define FLARECOUNT 8

struct FlareStruct
{
   float  dist;
   float  curve;
   float  scale;
   float4 tint;
};

static FlareStruct FlareData[FLARECOUNT] =
{
	{ -1.5, Curve1.x, Scale1.x, Tint1 * Intensity1.x },
	{  0.8, Curve1.y, Scale1.y, Tint1 * Intensity1.y },
	{ -0.5, Curve1.z, Scale1.z, Tint1 * Intensity1.z },
	{  2.0, Curve1.w, Scale1.w, Tint1 * Intensity1.w },
	{ -2.5, Curve2.x, Scale2.x, Tint2 * Intensity2.x },
	{  1.2, Curve2.y, Scale2.y, Tint2 * Intensity2.y },
	{ -0.3, Curve2.z, Scale2.z, Tint2 * Intensity2.z },
	{  3.0, Curve2.w, Scale2.w, Tint2 * Intensity2.w },
};

float4 GhostSample(float2 texcoord, FlareStruct F)
{
	// Mirror + offset from center
	float2 GhostUV = 0.5 + (0.5 - texcoord) * F.dist;

	// Barrel distortion per ghost
	float2 Centered = GhostUV - 0.5;
	float R = length(Centered * ScreenSize.y * float2(1.0, ScreenSize.z));
	GhostUV = 0.5 + Centered + pow(2.0 * R + DELTA, F.curve) * (Centered / max(R, DELTA)) * F.scale;

	// Border fade
	float2 Edge = saturate(GhostUV) * saturate(1.0 - GhostUV);
	float EdgeMask = smoothstep(0.0, 0.05, min(Edge.x, Edge.y));
	if(EdgeMask < 0.001) return 0.0;

	float3 Tap = TextureDownsampled.SampleLevel(Linear_Sampler, GhostUV, 0).rgb;
	[branch] if(UI_IgnoreSky) Tap *= MaskDaySky(GhostUV);

	// Grain overlay
	Tap = saturate(Tap - Random(GhostUV) * clamp(abs(F.curve - 1.0), 0.2, 0.5) * UIG_Grain);

	return float4(Tap * F.tint.rgb, 1.0) * EdgeMask * F.tint.a;
}


VertexShaderOutput VS_GhostAccumulate(VertexShaderInput IN)
{
	VertexShaderOutput OUT;
	OUT.pos      = float4(IN.pos.xyz, 1.0);
	OUT.texcoord = IN.txcoord.xy;
	return OUT;
}

float4 PS_GhostAccumulate(VertexShaderOutput IN) : SV_Target
{
	float GhostStr = DNI_SEPARATION(UIG_Strength);
	float GhostPow = DNI_SEPARATION(UIG_Power);
	if(GhostStr <= 0.0) discard;

	float3 Accum = 0.0;

	[unroll] for(int i = 0; i < FLARECOUNT; i++)
	{
		float4 Ghost = GhostSample(IN.texcoord, FlareData[i]);
		float GhostLuma = max3(Ghost.rgb);
		float Mask = pow(GhostLuma / (1.0 + GhostLuma), GhostPow);
		Accum += Ghost.rgb * Mask;
	}

	Accum *= GhostStr * 0.33333;
	return float4(zerolim(Accum), 1.0);
}


//=============================================================================
//  LENS REFLECTIONS
//=============================================================================

struct VertexShaderOutputReflection
{
   float4 pos      : SV_POSITION;
   float2 texcoord : TEXCOORD0;
NI float2 Lens     : TEXCOORD1;
};

VertexShaderOutputReflection VS_Reflection(VertexShaderInput IN)
{
	VertexShaderOutputReflection OUT;
	OUT.pos      = float4(IN.pos.xyz, 1.0);
	OUT.texcoord = IN.txcoord.xy;
	OUT.Lens.x = DNI_SEPARATION(UIR_Strength) * 0.25;
	OUT.Lens.y = DNI_SEPARATION(UIR_Power);
	return OUT;
}

float4 PS_Reflection(VertexShaderOutputReflection IN) : SV_Target
{
	if(!UIR_Enable) discard;
	float3 LensRef = 0.0;

	static const float4 RefOffset[8] = {
		float4( 1.60, 4.0, 1.0, 0.0),   float4( 0.70, 0.25, 2.0, 0.3),
		float4( 0.30, 1.5, 0.5, 0.5),   float4(-0.50, 1.0, 1.0, 0.7),
		float4( 1.20, 2.5, 1.5, 0.2),   float4(-0.25, 0.8, 0.8, 0.9),
		float4( 0.90, 3.0, 2.5, 0.4),   float4(-0.80, 1.2, 1.8, 1.0),
	};

	float3 RefColor[8] = {
		UIR_ColorFilter1, UIR_ColorFilter2, UIR_ColorFilter3, UIR_ColorFilter4,
		lerp(UIR_ColorFilter1, UIR_ColorFilter3, 0.5) * 0.7,
		lerp(UIR_ColorFilter2, UIR_ColorFilter4, 0.5) * 0.6,
		UIR_ColorFilter1.gbr * 0.5,
		UIR_ColorFilter2.brg * 0.4
	};

	[unroll] for(int i=0; i<8; i++)
	{
		float2 DistFact = IN.texcoord - 0.5;
		float2 AspCorr  = float2(DistFact.x * ScreenSize.z, DistFact.y);
		float  RadDist  = length(AspCorr);
		float  Distort  = pow(2.0 * RadDist + DELTA, RefOffset[i].y);
		float2 Coords   = 0.5 - DistFact * Distort * RefOffset[i].x * RefOffset[i].z;

		float2 EdgeDist = Coords * (1.0 - Coords);
		float  EdgeMask = smoothstep(0.0, 0.04, min(EdgeDist.x, EdgeDist.y));
		float2 VigFact  = Coords * 2.0 - 1.0;
		float  VigMask  = smoothstep(0.0, 0.15, saturate(1.0 - dot(VigFact, VigFact) * 1.1));
		if(EdgeMask < 0.001 || VigMask < 0.001) continue;

		float2 SafeCoords = clamp(Coords, 0.002, 0.998);
		float3 TempLens = RenderTarget256.SampleLevel(Linear_Sampler, SafeCoords, 0);
		[branch] if(UI_IgnoreSky) TempLens *= MaskDaySky(SafeCoords);

		float  CoatPhase = RefOffset[i].w * 2.0 * PI;
		float3 CoatShift = float3(
			0.85 + 0.15 * cos(CoatPhase),
			0.85 + 0.15 * cos(CoatPhase + TWO_PI * 0.333),
			0.85 + 0.15 * cos(CoatPhase + TWO_PI * 0.667)
		);
		TempLens *= RefColor[i] * CoatShift * EdgeMask * VigMask;

		float TempNor  = max3(TempLens);
		      TempNor /= 1.0 + TempNor;
		      TempNor  = pow(TempNor, IN.Lens.y);
		LensRef += TempLens * TempNor;
	}

	return float4(LensRef * IN.Lens.x, 1.0);
}


//=============================================================================
//  STARBURST LENSFLARE
//=============================================================================

struct VertexShaderOutputSBPrePass
{
   float4 pos            : SV_POSITION;
   float2 texcoord       : TEXCOORD0;
NI float2 UISB_Thresh    : TEXCOORD1;
NI float  UISB_Strength  : SB0;
NI float3 UISB_Tint      : SB1;
NI float  SB_ThreshCurve : SB2;
NI float  SB_PowerLimit  : SB3;
};

struct VertexShaderOutputSBMain
{
   float4 pos             : SV_POSITION;
   float2 texcoord        : TEXCOORD0;
NI float2 LoopCount       : TEXCOORD1;
NI float  FlareSigma      : SB0;
NI float  WeightFactor    : SB1;
NI float2 RotPixelSize[9] : SB2;
};

struct VertexShaderOutputSBPostPass
{
   float4 pos      : SV_POSITION;
   float4 texcoord : TEXCOORD0;
};

static float4 Scalings = UISB_HQ ? float4(2.0, 0.5, 0.5 , 4.0) :
                                    float4(4.0, 1.0, 0.25, 2.0);


VertexShaderOutputSBPrePass VS_StarburstPrePass(VertexShaderInput IN)
{
	VertexShaderOutputSBPrePass OUT;
	OUT.pos      = float4(IN.pos.xyz, 1.0);
	OUT.texcoord = IN.txcoord.xy;
	ScaleScreenQuad_Mult(OUT.pos.xy, Scalings.z);
	OUT.UISB_Thresh.x  = DNI_SEPARATION(UISB_Thresh);
	OUT.UISB_Thresh.y  = rcp(OUT.UISB_Thresh.x);
	OUT.UISB_Strength   = DNI_SEPARATION(UISB_Strength);
	OUT.SB_ThreshCurve  = UISB_ThreshCurve * 2.0 * OUT.UISB_Thresh.x;
	OUT.SB_PowerLimit   = pow(UISB_ThreshCurve, 4) * OUT.UISB_Strength;
	OUT.UISB_Tint       = UISB_Tint.rgb / deltalim(dot(UISB_Tint.rgb, N_LUM));
	return OUT;
}

float4 PS_StarburstPrePass(VertexShaderOutputSBPrePass IN) : SV_Target
{
	if(IN.UISB_Strength <= 0.0) discard;

	static const float2 offset[4] =
	{ PixelSize.x, PixelSize.y, -PixelSize.x, PixelSize.y,
	  PixelSize.x,-PixelSize.y, -PixelSize.x,-PixelSize.y };

	float3 Color = 0.0;
	[branch] if(UI_IgnoreSky)
	[unroll] for(int i=0; i<4; i++)
	{ float2 C = IN.texcoord + offset[i]; Color += MaskDaySky(C) * TextureDownsampled.SampleLevel(Linear_Sampler, C, 0).rgb; }
	else
	[unroll] for(int i=0; i<4; i++)
		Color += TextureDownsampled.SampleLevel(Linear_Sampler, IN.texcoord + offset[i], 0).rgb;

	float Luma;
	Color *= 0.25;
	Luma   = dot(Color, N_LUM);
	Color /= deltalim(Luma);
	Luma   = pow(Luma * IN.UISB_Thresh.y, IN.SB_ThreshCurve);
	Luma   = min(Luma, IN.SB_PowerLimit);
	Luma   = (Luma * IN.UISB_Strength) / (Luma + IN.UISB_Thresh.x);
	Color  = lerp(Color, IN.UISB_Tint, UISB_Tint.a);
	Color  = lerp(Luma, Color * Luma, UISB_Saturation);
	Color *= 1.0 + UISB_Imperfections * (RandomGauss(IN.texcoord) - 0.5);
	return float4(zerolim(Color), 1.0);
}


VertexShaderOutputSBMain VS_StarburstMain(VertexShaderInput IN)
{
	VertexShaderOutputSBMain OUT;
	OUT.pos      = float4(IN.pos.xyz, 1.0);
	OUT.texcoord = IN.txcoord.xy * Scalings.z;
	ScaleScreenQuad_Mult(OUT.pos.xy, Scalings.z);
	OUT.FlareSigma   = UISB_Width * Scalings.w;
	OUT.WeightFactor = rcp(OUT.FlareSigma * SqrtTwoPI);
	OUT.LoopCount.x  = ceil(OUT.FlareSigma * 2.0);
	OUT.FlareSigma   = -rcp(OUT.FlareSigma * OUT.FlareSigma) * UISB_Falloff;
	OUT.LoopCount.y  = ceil(UISB_Shape * saturate(UISB_Shape % 2.0 + 0.5));
	float2 ScaledPS  = PixelSize * 1.25;
	[unroll] for(int i=0; i<9; i++)
		OUT.RotPixelSize[i] = GetDirVec(360.0 / UISB_Shape * (i + 0.5) + UISB_AngleOffset) * ScaledPS;
	return OUT;
}

float4 PS_StarburstMain(VertexShaderOutputSBMain IN) : SV_Target
{
	#if STARBURST_USE_MAX_COLOR
		#define ColSum Colors[j]
	#else
		#define ColSum Color
	#endif

	float3 Color     = IN.WeightFactor * TextureColor.SampleLevel(Point_Sampler, IN.texcoord, 0).rgb;
	float  WeightSum = IN.WeightFactor;

	#if STARBURST_USE_MAX_COLOR
		float3 Colors[9];
		[unroll] for(int k=0; k<9; k++) Colors[k] = 0.0;
	#endif

	[loop] for(float i=1.0; i <= IN.LoopCount.x; i++)
	{
		float GaussWeight = IN.WeightFactor * exp(i*i*IN.FlareSigma);
		[loop] for(int j=0; j < IN.LoopCount.y; j++)
		{
			float4 Offset  = IN.texcoord.xyxy + IN.RotPixelSize[j].xyxy * float4(i.xx, -i.xx);
			ColSum += GaussWeight * TextureColor.SampleLevel(Linear_Sampler, Offset.xy, 0).rgb;
			ColSum += GaussWeight * TextureColor.SampleLevel(Linear_Sampler, Offset.zw, 0).rgb;
			WeightSum += GaussWeight * 2.0;
		}
	}

	#if STARBURST_USE_MAX_COLOR
		[unroll] for(k=0; k<9; k++) Colors[0] = max(Colors[0], Colors[k]);
		Color += Colors[0];
	#endif

	return float4(Color / WeightSum, 1.0);
}


VertexShaderOutputSBPostPass VS_StarburstPostPass(VertexShaderInput IN)
{
	VertexShaderOutputSBPostPass OUT;
	OUT.pos      = float4(IN.pos.xyz, 1.0);
	OUT.texcoord = IN.txcoord.xyxy;
	OUT.texcoord.xy *= Scalings.z;
	return OUT;
}

float4 PS_StarburstPostPass(VertexShaderOutputSBPostPass IN) : SV_Target
{
	float3 Color;
	[branch]
	if(!UISB_HQ) Color = BicubicFilter(TextureColor, IN.texcoord.xy, ScreenRes).rgb;
	else          Color = TextureColor.SampleLevel(Linear_Sampler, IN.texcoord.xy, 0).rgb;
	// Always add ghosts from RGBA64F (will be 0 if ghosts disabled)
	Color += RenderTargetRGBA64F.SampleLevel(Point_Sampler, IN.texcoord.zw, 0).rgb;
	return float4(Color, 1.0);
}


//=============================================================================
//  LENS POSTPASS: COMBINE + DIRT
//=============================================================================

struct VertexShaderOutputPostPass
{
   float4 pos            : SV_POSITION;
   float2 texcoord       : TEXCOORD0;
   float2 DirtCoords     : TEXCOORD1;
NI float  UID_Power      : DNI0;
NI float  UID_Strength   : DNI1;
NI bool   AFBlurSampling : DNI2;
};

VertexShaderOutputPostPass VS_DirtAndPostPass(VertexShaderInput IN)
{
	VertexShaderOutputPostPass OUT;
	OUT.pos         = float4(IN.pos.xyz, 1.0);
	OUT.texcoord    = IN.txcoord.xy;
	OUT.UID_Power      = DNI_SEPARATION(UID_Power);
	OUT.UID_Strength   = DNI_SEPARATION(UID_Strength);
	OUT.DirtCoords     = AtlasFetch_4(IN.txcoord, UID_Select);
	OUT.AFBlurSampling = fmod(UIAF_Rotation, 90.0) > 0.0;
	return OUT;
}

float4 PS_DirtAndPostPass(VertexShaderOutputPostPass IN) : SV_Target
{
	float3 Color = TextureColor.SampleLevel(Point_Sampler, IN.texcoord, 0).rgb;

	#ifdef SKYRIMBRIDGE_FXH
		float FogSuppress = SB_LENS_GetFogSuppression();
	#else
		float FogSuppress = 1.0;
	#endif

	if(UIR_Enable) Color += RenderTarget512.SampleLevel(Linear_Sampler, IN.texcoord, 0).rgb * FogSuppress;

	if(UIAF_Enable)
	{
		float3 AnamCol = RenderTarget256.SampleLevel(Linear_Sampler, IN.texcoord, 0).rgb;
		[branch] if(IN.AFBlurSampling)
		{
			float  WeightSum = DELTA;
			float3 AnamBlur  = 0.0;
			static const float2 Offset[4] = { AF_RTPixelSize * float2( 1.0, 0.5),
			                                   AF_RTPixelSize * float2(-0.5, 1.0),
			                                   AF_RTPixelSize * float2(-1.0,-0.5),
			                                   AF_RTPixelSize * float2( 0.5,-1.0) };
			[unroll] for(int i=0; i<4; i++)
			{
				float4 CurrSample = RenderTarget256.SampleLevel(Linear_Sampler, IN.texcoord + Offset[i], 0);
				AnamBlur  += CurrSample.a * CurrSample.rgb;
				WeightSum += CurrSample.a * 2.0;
			}
			AnamCol = AnamBlur / WeightSum + AnamCol * 0.5;
		}
		Color += AnamCol * FogSuppress;
	}

	[branch] if(UID_Enable)
	{
		float3 ProcDirt  = ProceduralLensDirt(IN.texcoord);
		float3 AtlasDirt = LensDirtAtlas.SampleLevel(Linear_Sampler, IN.DirtCoords, 0).rgb;
		float3 DirtMask  = AtlasDirt * 0.6 + ProcDirt * 0.4 + ProcDirt * AtlasDirt * 0.3;
		float  DirtMax   = max3(DirtMask);
		       DirtMax   = pow(DirtMax / (1.0 + DirtMax), IN.UID_Power);
		DirtMask *= DirtMax * IN.UID_Strength;
		float3 BloomMask = RenderTarget128.SampleLevel(Linear_Sampler, 1.0 - IN.texcoord, 0).rgb * 0.1 +
		                   RenderTarget64.SampleLevel(Linear_Sampler, IN.texcoord, 0).rgb * 0.9;
		#ifdef SKYRIMBRIDGE_FXH
			DirtMask *= SB_LENS_GetWetDirtMult();
		#endif
		Color += DirtMask * BloomMask;
	}

	#ifdef SKYRIMBRIDGE_FXH
	{
		float LFlash = SB_LENS_GetLightningFlash();
		[branch] if(LFlash > 0.01)
			Color += float3(0.85, 0.88, 1.0) * LFlash;
	}
	#endif

	return float4(Color, 1.0);
}

//=============================================================================
//  RAIN WEATHER EFFECTS
//=============================================================================

struct VertexShaderOutputRain
{
   float4 pos        : SV_POSITION;
   float4 texcoord   : TEXCOORD0;
NI float  Fade       : PSINFO0;
NI bool   EnableRain : PSINFO1;
NI float  Rotation   : PSINFO2;
NI float4 SkyColor   : PSINFO3;
NI float4 EnvColor   : PSINFO4;
};

struct VertexShaderOutputRainBlur
{
   float4 pos          : SV_POSITION;
   float2 texcoord     : TEXCOORD0;
NI float  Sigma        : TEXCOORD1;
NI float  WeightFactor : TEXCOORD2;
NI float  LoopCount    : PSINFO0;
NI bool   EnableRain   : PSINFO1;
NI float  OffsetScale  : PSINFO2;
};

#define RAINDROP_GRID_SIZE 4

void OOSSampleApprox(inout float3 SampleCol, float2 SampleCoords, float4 SkyColor, float4 EnvColor)
{
	float SkyColWeight  = saturate(SampleCoords.y * -2.0 * SkyColor.a);
	float EnvColWeight  = max2(abs(SampleCoords.x - 0.5) - 0.5);
	      EnvColWeight += saturate(SampleCoords.y - 1.0);
	      EnvColWeight  = saturate(EnvColWeight * 2.0 * EnvColor.a);
	SampleCol = lerp(SampleCol, SkyColor.rgb, SkyColWeight);
	SampleCol = lerp(SampleCol, EnvColor.rgb, EnvColWeight);
}

void OOSSampleApprox(inout float3 SampleCol, uint Index, float2 SampleCoords, float4 SkyColor, float4 EnvColor)
{
	float SkyColWeight  = saturate(SampleCoords.y * -2.0 * SkyColor.a);
	float EnvColWeight  = max2(abs(SampleCoords.x - 0.5) - 0.5);
	      EnvColWeight += saturate(SampleCoords.y - 1.0);
	      EnvColWeight  = saturate(EnvColWeight * 2.0 * EnvColor.a);
	SampleCol[Index] = lerp(SampleCol[Index], SkyColor[Index], SkyColWeight);
	SampleCol[Index] = lerp(SampleCol[Index], EnvColor[Index], EnvColWeight);
}


VertexShaderOutputRain VS_Rain(VertexShaderInput IN, uniform float4 Seeds, uniform float TOffset)
{
	VertexShaderOutputRain OUT = (VertexShaderOutputRain)0;
	OUT.pos        = float4(IN.pos.xy * PixelSize * 0.25, IN.pos.z, 1.0);
	OUT.texcoord   = IN.txcoord.xyxy;
	OUT.EnableRain = !EInteriorFactor && UIWFXRain_Enable;

	#ifdef SKYRIMBRIDGE_FXH
		OUT.EnableRain = !SB_LENS_IsInterior() && UIWFXRain_Enable;
	#endif

	[branch] if(OUT.EnableRain)
	{
		float RainStrength = 1.0;
		#ifdef SKYRIMBRIDGE_FXH
			RainStrength = SB_LENS_GetRainIntensity();
			OUT.EnableRain = OUT.EnableRain && (RainStrength > 0.01);
		#endif

		float2 Time;
		Time.x = Timer.x * 16777.216 * UIWFXRain_Tickrate;
		Time.x = Time.x * ceil(RainStrength) + TOffset;
		Time.y = modf(Time.x, Time.x);

		float4 RanValue1 = RandomF4(Time.x * Seeds);
		float4 RanValue2 = RandomF4(RanValue1);

		float  RScale    = MapToRange(RanValue1.x, UIWFXRain_MinSize, UIWFXRain_MaxSize);
		       RScale   *= 1.0 + Time.y * 0.25;
		float  RRotation = RanValue1.y * 360.0;
		float2 ROffset   = RanValue1.zw * 2.0 - 1.0;
		float2 RRatio    = 1.0 + (RanValue2.xw * 2.0 - 1.0) * UIWFXRain_MaxDeformation;

		float2 DripData   = saturate(CatmullRom(Time.y, RanValue2.xz, 0.0, 1.0, RanValue2.yw));
		float  RDripping  = DripData.x * UIWFXRain_DripSpeed / UIWFXRain_Tickrate;
		float  RDripDrift = DripData.y * UIWFXRain_DripDrift;
		RDripDrift = step(RanValue2.x, 0.45) * RDripDrift * RanValue2.y -
		             step(0.75, RanValue2.z) * RDripDrift * RanValue2.w;

		OUT.pos.xy = IN.pos.xy * RScale * RRatio + ROffset;
		OUT.pos.xy = MatrixRotate(OUT.pos.xy, RRotation, false);
		OUT.Rotation = RRotation;
		OUT.pos.y  = OUT.pos.y * ScreenSize.z - RDripping;
		OUT.pos.x += RDripDrift;

		#ifdef SKYRIMBRIDGE_FXH
		{
			float3 Wind = SB_LENS_GetWind();
			OUT.pos.xy += Wind.xy * Wind.z * 0.15;
		}
		#endif

		float2 AtlasCoords = floor(RanValue2.xy * RAINDROP_GRID_SIZE);
		OUT.texcoord.zw = (AtlasCoords + OUT.texcoord.zw) / RAINDROP_GRID_SIZE;

		OUT.Fade  = lerp(Night_UIWFXRain_Intensity, Day_UIWFXRain_Intensity, ENightDayFactor);
		OUT.Fade *= min(RainStrength, 1.2);
		OUT.Fade *= 1.0 - pow(Time.y, UIWFXRain_FadeCurve);
		#ifdef SKYRIMBRIDGE_FXH
			OUT.Fade *= SB_LENS_GetRainIntensity();
		#endif
		OUT.SkyColor = lerp(Night_UIWFXRain_SkyColor, Day_UIWFXRain_SkyColor, ENightDayFactor);
		OUT.EnvColor = lerp(Night_UIWFXRain_EnvColor, Day_UIWFXRain_EnvColor, ENightDayFactor);
	}
	return OUT;
}

float4 PS_Rain(VertexShaderOutputRain IN) : SV_Target
{
	if(!IN.EnableRain) discard;

	static const float3 SourceRay   = { 0.0, 0.0, -1.0 };
	static const float4 RefracIndex = 1.000277 / float4(1.3310, 1.3330, 1.3358, 1.3325);

	float2 SSCoords       = IN.pos.xy * PixelSize;
	float2 DropletLocalUV = frac(IN.texcoord.zw * RAINDROP_GRID_SIZE);
	float2 DropletSeed    = floor(IN.texcoord.zw * RAINDROP_GRID_SIZE);
	float4 DropletNormal  = ProceduralRainDroplet(DropletLocalUV, DropletSeed);
	clip(DropletNormal.w - 0.01);

	DropletNormal.xyz = DropletNormal.xyz * 2.0 - 1.0;
	DropletNormal.xy *= UIWFXRain_Curvature;

	float3 RefracSample;
	#if !RAINFX_ENABLE_DISPERSION
		float3 RefracVec    = refract(SourceRay, DropletNormal.xyz, RefracIndex.w);
		float2 RefracCoords = SSCoords + MatrixRotate(RefracVec.xy / RefracVec.z, IN.Rotation, true);
		RefracSample = TextureOriginal.SampleLevel(Linear_Sampler_Rain, RefracCoords, 0).rgb;
		OOSSampleApprox(RefracSample, RefracCoords, IN.SkyColor, IN.EnvColor);
	#else
		float3 ScaledRefracIndex;
		ScaledRefracIndex.y  = RefracIndex.y;
		ScaledRefracIndex.xz = lerp(RefracIndex.y, RefracIndex.xz, UIWFXRain_Dispersion);
		[unroll] for(int i=0; i<3; i++)
		{
			float3 RefracVec       = refract(SourceRay, DropletNormal.xyz, ScaledRefracIndex[i]);
			float2 RefracCoords    = SSCoords + RefracVec.xy / RefracVec.z;
			RefracSample[i] = TextureOriginal.SampleLevel(Linear_Sampler_Rain, RefracCoords, 0)[i];
			OOSSampleApprox(RefracSample, i, RefracCoords, IN.SkyColor, IN.EnvColor);
		}
	#endif

	IN.Fade *= DropletNormal.w;
	return saturate(float4(RefracSample, 1.0) * IN.Fade);
}


VertexShaderOutputRainBlur VS_RainBlur(VertexShaderInput IN)
{
	VertexShaderOutputRainBlur OUT = (VertexShaderOutputRainBlur)0;
	OUT.pos      = float4(IN.pos.xyz, 1.0);
	OUT.texcoord = IN.txcoord.xy;
	OUT.EnableRain = !EInteriorFactor && UIWFXRain_Enable;
	#ifdef SKYRIMBRIDGE_FXH
		OUT.EnableRain = !SB_LENS_IsInterior() && UIWFXRain_Enable;
	#endif
	[branch] if(OUT.EnableRain)
	{
		OUT.Sigma        = UIWFXRain_Sigma + DELTA;
		OUT.WeightFactor = rcp(OUT.Sigma * SqrtTwoPI);
		OUT.LoopCount    = ceil(OUT.Sigma * 1.5 - 0.01);
		OUT.Sigma        = -rcp(OUT.Sigma * OUT.Sigma);
		OUT.OffsetScale  = 1.0 - sqrt(saturate(UIWFXRain_Sigma / 3.0)) * 0.5;
	}
	return OUT;
}

float4 PS_RainBlur(VertexShaderOutputRainBlur IN) : SV_Target
{
	if(!IN.EnableRain) discard;
	float4 StepSize     = PixelSize.xyxy * float4(1.0, 0.0, -1.0, 0.0);
	float4 HalfStepSize = StepSize * IN.OffsetScale;
	       StepSize    *= 2.0;
	float  WeightSum    = IN.WeightFactor * IN.OffsetScale;
	float4 BlurredCol   = WeightSum * RenderTargetRGBA32.SampleLevel(Point_Sampler, IN.texcoord, 0);

	[loop] for(float i=1.0; i <= IN.LoopCount; i++)
	{
		float  GaussWeight = IN.WeightFactor * exp(i*i*IN.Sigma);
		float4 CurrOffset  = IN.texcoord.xyxy + i * StepSize - HalfStepSize;
		BlurredCol += GaussWeight * RenderTargetRGBA32.SampleLevel(Linear_Sampler, CurrOffset.xy, 0);
		BlurredCol += GaussWeight * RenderTargetRGBA32.SampleLevel(Linear_Sampler, CurrOffset.zw, 0);
		WeightSum  += GaussWeight * 2.0;
	}
	return BlurredCol / WeightSum;
}


//=============================================================================
//  SPECTRAL CHROMATIC ABERRATION
//=============================================================================

static const float  CA_Offset[6] = {  1.0,   0.58,   0.20,  -0.10,  -0.52,  -1.0  };
static const float3 CA_Weight[6] = { float3(0.02, 0.00, 0.80),
                                     float3(0.00, 0.20, 0.85),
                                     float3(0.05, 0.85, 0.10),
                                     float3(0.35, 0.90, 0.00),
                                     float3(0.95, 0.25, 0.00),
                                     float3(0.55, 0.02, 0.00) };

float3 SpectralCA(float2 Texcoord, float2 VigDir, float VigDist)
{
	float Magnitude = pow(VigDist / 1.15, UICA_Curve) * UICA_Strength / VigDist * 10.0;
	Magnitude *= smoothstep(UICA_Deadzone, UICA_Deadzone + 0.15, VigDist);
	float2 BaseVector = VigDir * Magnitude * PixelSize;

	float3 AccColor = 0.0, AccWeight = 0.0;
	[unroll] for(int s = 0; s < 6; s++)
	{
		float Disp = CA_Offset[s] * UICA_Spread;
		float Bias = lerp(UICA_RedBias, UICA_BlueBias, saturate(CA_Offset[s] * 0.5 + 0.5));
		Disp *= Bias;
		float BarrelMul = 1.0 + Disp * UICA_Barrel * VigDist * 0.5;
		float2 SampleUV = Texcoord + BaseVector * Disp * BarrelMul;
		float3 Tap = TextureColor.SampleLevel(Linear_Sampler_CA, SampleUV, 0).rgb;
		AccColor  += Tap * CA_Weight[s];
		AccWeight += CA_Weight[s];
	}
	AccColor /= deltalim(AccWeight);
	float Luma = dot(AccColor, K_LUM);
	return lerp(float3(Luma, Luma, Luma), AccColor, UICA_FringeSat);
}


//=============================================================================
//  BROWN-CONRADY LENS DISTORTION
//=============================================================================

float2 BrownConradyDistort(float2 Texcoord)
{
	float2 Centered = Texcoord * 2.0 - 1.0;
	Centered.x *= ScreenSize.z;
	float R2 = dot(Centered, Centered);
	float R4 = R2 * R2;
	float R6 = R4 * R2;
	float RadialFactor = 1.0 + UILO_DistortK1*R2 + UILO_DistortK2*R4 + UILO_DistortK3*R6;
	float2 Tangential;
	Tangential.x = 2.0*UILO_DistortP1*Centered.x*Centered.y + UILO_DistortP2*(R2 + 2.0*Centered.x*Centered.x);
	Tangential.y = UILO_DistortP1*(R2 + 2.0*Centered.y*Centered.y) + 2.0*UILO_DistortP2*Centered.x*Centered.y;
	float2 Distorted = Centered * RadialFactor + Tangential;
	Distorted.x /= ScreenSize.z;
	return Distorted * 0.5 + 0.5;
}


//=============================================================================
//  OPTICAL VIGNETTING
//=============================================================================

float3 OpticalVignette(float3 Color, float2 Texcoord)
{
	float2 Centered = Texcoord * 2.0 - 1.0;
	float  Dist     = length(Centered);
	float  CosTheta = 1.0 / (1.0 + Dist * Dist * 0.5);
	float  NaturalVig = pow(CosTheta, 2.0 * UILO_VigNatural);
	float2 MechCoord = abs(Centered);
	MechCoord.x *= UILO_VigMechRatio;
	float  ShapeExp = lerp(2.0, 8.0, UILO_VigRoundness);
	float  MechDist = pow(pow(MechCoord.x, ShapeExp) + pow(MechCoord.y, ShapeExp), 1.0 / ShapeExp);
	float  MechVig  = smoothstep(0.0, 1.0, 1.0 - saturate((MechDist - (1.2 - UILO_VigMechanical)) * 3.0));
	float  VigFactor = NaturalVig * MechVig;
	float3 ChromaVig = lerp(1.0, UILO_VigColorShift, Dist * UILO_VigColorAmt);
	return Color * VigFactor * ChromaVig;
}


//=============================================================================
//  COMA ABERRATION
//=============================================================================

float3 ComaAberration(float2 Texcoord, float2 VigDir, float VigDist)
{
	float  ComaAmount = pow(VigDist, UILO_ComaFalloff) * UILO_ComaStrength;
	float2 ComaDir = normalize(VigDir + DELTA) * ComaAmount * PixelSize;
	float3 AccColor = 0.0;
	float  AccWeight = 0.0;
	int    NumSamples = (int)UILO_ComaSamples;
	[loop] for(int i = 0; i < NumSamples; i++)
	{
		float T = (float)i / (float)(NumSamples - 1);
		float Offset = lerp(-0.3, 1.0, T);
		float Weight = exp(-Offset * Offset * 2.0);
		AccColor  += TextureColor.SampleLevel(Linear_Sampler_CA, Texcoord + ComaDir * Offset, 0).rgb * Weight;
		AccWeight += Weight;
	}
	return AccColor / max(AccWeight, DELTA);
}


//=============================================================================
//  SPHERICAL ABERRATION
//=============================================================================

float3 SphericalAberration(float3 Color, float2 Texcoord)
{
	float Luma = dot(Color, K_LUM);
	float HaloWeight = pow(saturate(Luma), UILO_SphericalBias) * UILO_SphericalStr * 0.3;
	float3 Halo = 0.0;
	float2 Radius = UILO_SphericalRadius * PixelSize;
	static const float GoldenAngle = 2.39996;
	float TotalWeight = 0.0;
	[unroll] for(int i = 0; i < 8; i++)
	{
		float Angle = (float)i * GoldenAngle;
		float R = sqrt((float)(i + 1) / 8.0);
		float2 Offset = float2(cos(Angle), sin(Angle)) * R * Radius;
		float W = 1.0 - R * 0.5;
		Halo += TextureColor.SampleLevel(Linear_Sampler_CA, Texcoord + Offset, 0).rgb * W;
		TotalWeight += W;
	}
	return lerp(Color, Halo / TotalWeight, HaloWeight);
}


//=============================================================================
//  FIELD CURVATURE
//=============================================================================

float3 FieldCurvature(float2 Texcoord, float VigDist)
{
	float CurveAmount = pow(VigDist, UILO_FieldCurveExp) * UILO_FieldCurveStr;
	return TextureColor.SampleLevel(Linear_Sampler_CA, Texcoord, CurveAmount * 3.0).rgb;
}


//=============================================================================
//  PURPLE / GREEN FRINGING
//=============================================================================

float3 PurpleFringing(float2 Texcoord, float VigDist)
{
	float RadialWeight = pow(VigDist, UIPF_RadialFade) * 0.5 + 0.5;
	float2 SpreadPx = UIPF_Spread * PixelSize * RadialWeight;

	float LumaC = dot(TextureColor.SampleLevel(Linear_Sampler_CA, Texcoord, 0).rgb, K_LUM);
	float LumaL = dot(TextureColor.SampleLevel(Linear_Sampler_CA, Texcoord + float2(-SpreadPx.x, 0), 0).rgb, K_LUM);
	float LumaR = dot(TextureColor.SampleLevel(Linear_Sampler_CA, Texcoord + float2( SpreadPx.x, 0), 0).rgb, K_LUM);
	float LumaU = dot(TextureColor.SampleLevel(Linear_Sampler_CA, Texcoord + float2(0, -SpreadPx.y), 0).rgb, K_LUM);
	float LumaD = dot(TextureColor.SampleLevel(Linear_Sampler_CA, Texcoord + float2(0,  SpreadPx.y), 0).rgb, K_LUM);

	float2 Grad = float2(LumaR - LumaL, LumaD - LumaU);
	float  GradMag = length(Grad);
	float  EdgeMask = smoothstep(UIPF_Threshold, UIPF_Threshold + 0.1, GradMag);
	if (EdgeMask < 0.001) return TextureColor.SampleLevel(Point_Sampler, Texcoord, 0).rgb;

	float2 GradDir = normalize(Grad + DELTA);
	float3 BrightSample = TextureColor.SampleLevel(Linear_Sampler_CA, Texcoord + GradDir * SpreadPx, 0).rgb;
	float3 DarkSample   = TextureColor.SampleLevel(Linear_Sampler_CA, Texcoord - GradDir * SpreadPx, 0).rgb;
	float3 CenterSample = TextureColor.SampleLevel(Point_Sampler, Texcoord, 0).rgb;

	float3 PurpleTint = float3(0.6, 0.1, 0.9);
	float3 GreenTint  = float3(0.2, 0.8, 0.2);
	float BrightFringe = saturate(0.5 + UIPF_Bias);
	float DarkFringe   = saturate(0.5 - UIPF_Bias);

	float3 FringeColor = (BrightSample - CenterSample) * PurpleTint * BrightFringe
	                   + (DarkSample - CenterSample) * GreenTint * DarkFringe;
	return CenterSample + FringeColor * EdgeMask * UIPF_Strength * RadialWeight;
}


//=============================================================================
//  LENS BREATHING
//=============================================================================

float2 LensBreathing(float2 Texcoord)
{
	float AvgBrightness = saturate(TextureDownsampled.SampleLevel(Linear_Sampler, float2(0.5, 0.5), 8).r);
	float Deviation = (AvgBrightness - UILB_Center) * UILB_Amount;
	Deviation = sign(Deviation) * pow(abs(Deviation), UILB_Speed);
	return (Texcoord - 0.5) * (1.0 + Deviation) + 0.5;
}


//=============================================================================
//  ASTIGMATISM
//=============================================================================

float3 Astigmatism(float2 Texcoord, float2 VigDir, float VigDist)
{
	float AstigAmount = smoothstep(UIAST_Onset, UIAST_Onset + 0.3, VigDist);
	AstigAmount *= pow(VigDist, UIAST_Falloff) * UIAST_Strength;
	if (AstigAmount < 0.001) return TextureColor.SampleLevel(Point_Sampler, Texcoord, 0).rgb;

	float2 RadialDir     = normalize(VigDir + DELTA);
	float2 TangentialDir = float2(-RadialDir.y, RadialDir.x);
	float2 BlurVec = TangentialDir * AstigAmount * PixelSize;

	float3 AccColor = 0.0;
	float  AccWeight = 0.0;
	int    Samples = UIAST_Samples;
	[loop] for (int i = 0; i < Samples; i++)
	{
		float T = ((float)i / (float)(Samples - 1)) * 2.0 - 1.0;
		float Weight = exp(-T * T * 2.0);
		AccColor  += TextureColor.SampleLevel(Linear_Sampler_CA, Texcoord + BlurVec * T, 0).rgb * Weight;
		AccWeight += Weight;
	}
	return AccColor / max(AccWeight, DELTA);
}



//=============================================================================
//  FROST / LENS-OPTICS COMPOSITE  (Tech 12)
//
//  This is the main pipeline pass that chains all optical aberrations,
//  reads halation prepass from RT512, composites weather effects (rain/frost),
//  and outputs the final lens-processed image.
//=============================================================================

struct VertexShaderOutputFrost
{
   float4 pos          : SV_POSITION;
   float2 texcoord     : TEXCOORD0;
NI float2 Radii        : TEXCOORD1;
NI float3 Vignette     : PSINFO0;
NI bool   EnableFrost  : PSINFO1;
NI float3 FrostTint    : PSINFO2;
NI float  Sigma        : PSINFO3;
NI float  WeightFactor : PSINFO4;
NI float  LoopCount    : PSINFO5;
NI bool   EnableRain   : PSINFO6;
NI float  OffsetScale  : PSINFO7;
NI float4 SkyColor     : PSINFO8;
NI float4 EnvColor     : PSINFO9;
};


VertexShaderOutputFrost VS_Frost(VertexShaderInput IN)
{
	VertexShaderOutputFrost OUT = (VertexShaderOutputFrost)0;
	OUT.pos         = float4(IN.pos.xyz, 1.0);
	OUT.texcoord    = IN.txcoord.xy;
	OUT.EnableFrost = !EInteriorFactor && UIWFXFrost_Enable;
	OUT.EnableRain  = !EInteriorFactor && UIWFXRain_Enable;

	//--- SkyrimBridge: Accurate interior detection + weather-based frost ---
	#ifdef SKYRIMBRIDGE_FXH
	{
		bool IsOutdoor = !SB_LENS_IsInterior();
		OUT.EnableRain  = IsOutdoor && UIWFXRain_Enable;
		OUT.EnableFrost = IsOutdoor && UIWFXFrost_Enable;
		// Auto-enable frost when weather is cold (snow/blizzard)
		if(IsOutdoor && SB_LENS_ShouldEnableFrost())
			OUT.EnableFrost = true;
	}
	#endif

	[branch] if(OUT.EnableFrost)
	{
		float FrostStrength = 1.0;

		float2 Time;
		Time.y = modf(Timer.x * 16777.216 * UIWFXFrost_PulseRate, Time.x);
		Time.y = abs(ceil(fmod(Time.x, 2.0)) - Time.y);

		float UIWFXFrost_Intensity = lerp(Night_UIWFXFrost_Intensity, Day_UIWFXFrost_Intensity, ENightDayFactor);
		OUT.Vignette.z = 4.0 - FrostStrength * 1.5;

		FrostStrength *= 1.0 + Time.y * UIWFXFrost_PulseStrength;
		OUT.Vignette.x = UIWFXFrost_Curve + (2.0 - FrostStrength) * 0.1;
		OUT.Vignette.y = UIWFXFrost_Intensity * saturate(FrostStrength);

		OUT.Radii.x  = zerolim(UIWFXFrost_RadiusInner - FrostStrength * 0.1);
		OUT.Radii.y  = rcp(max(UIWFXFrost_RadiusOuter, OUT.Radii.x + DELTA) - OUT.Radii.x);
		OUT.Radii.x *= OUT.Radii.y;

		OUT.FrostTint = UIWFXFrost_Tint / deltalim(dot(UIWFXFrost_Tint, N_LUM));

		OUT.SkyColor = lerp(Night_UIWFXFrost_SkyColor, Day_UIWFXFrost_SkyColor, ENightDayFactor);
		OUT.EnvColor = lerp(Night_UIWFXFrost_EnvColor, Day_UIWFXFrost_EnvColor, ENightDayFactor);
	}

	[branch] if(OUT.EnableRain)
	{
		OUT.Sigma        = UIWFXRain_Sigma + DELTA;
		OUT.WeightFactor =  rcp(OUT.Sigma * SqrtTwoPI);
		OUT.LoopCount    = ceil(OUT.Sigma * 1.5 - 0.01);
		OUT.Sigma        = -rcp(OUT.Sigma * OUT.Sigma);
		OUT.OffsetScale  = 1.0 - sqrt(saturate(UIWFXRain_Sigma / 3.0)) * 0.5;
	}

	return OUT;
}


float4 PS_Frost(VertexShaderOutputFrost IN) : SV_Target
{
	//---------- LENS BREATHING ----------//
	float2 BaseUV = IN.texcoord;

	[branch] if(UILB_Enable)
	{
		BaseUV = LensBreathing(IN.texcoord);
	}

	//---------- LENS DISTORTION ----------//
	float2 WorkUV = BaseUV;

	[branch] if(UILO_Enable && (abs(UILO_DistortK1) + abs(UILO_DistortK2) + abs(UILO_DistortK3)
	           + abs(UILO_DistortP1) + abs(UILO_DistortP2)) > 0.0)
	{
		WorkUV = BrownConradyDistort(BaseUV);
	}

	float2 VigVector   = WorkUV * 2.0 - 1.0;
	float  VigGradient = length(VigVector);


	//---------- FIELD CURVATURE ----------//
	float4 Color;

	[branch] if(UILO_Enable && UILO_FieldCurveStr > 0.0)
	{
		Color.rgb = FieldCurvature(WorkUV, VigGradient);
		Color.a   = 0.0;
	}
	else
	{
		Color = float4(TextureColor.SampleLevel(Point_Sampler, WorkUV, 0).rgb, 0.0);
	}


	//---------- SPECTRAL CHROMATIC ABERRATION ----------//
	[branch] if(UICA_Enable)
	{
		Color.rgb = SpectralCA(WorkUV, VigVector, VigGradient);
	}


	//---------- COMA ABERRATION ----------//
	[branch] if(UILO_Enable && UILO_ComaStrength > 0.0 && VigGradient > 0.2)
	{
		float ComaBlend = smoothstep(0.2, 0.5, VigGradient);
		float3 ComaResult = ComaAberration(WorkUV, VigVector, VigGradient);
		Color.rgb = lerp(Color.rgb, ComaResult, ComaBlend);
	}


	//---------- ASTIGMATISM ----------//
	[branch] if(UIAST_Enable && VigGradient > UIAST_Onset)
	{
		float AstigBlend = smoothstep(UIAST_Onset, UIAST_Onset + 0.2, VigGradient);
		float3 AstigResult = Astigmatism(WorkUV, VigVector, VigGradient);
		Color.rgb = lerp(Color.rgb, AstigResult, AstigBlend);
	}


	//---------- PURPLE / GREEN FRINGING ----------//
	[branch] if(UIPF_Enable)
	{
		Color.rgb = PurpleFringing(WorkUV, VigGradient);
	}


	//---------- SPHERICAL ABERRATION ----------//
	[branch] if(UILO_Enable && UILO_SphericalStr > 0.0)
	{
		Color.rgb = SphericalAberration(Color.rgb, WorkUV);
	}


	//---------- OPTICAL VIGNETTING ----------//
	[branch] if(UILO_Enable && (UILO_VigNatural > 0.0 || UILO_VigMechanical > 0.0 || UILO_VigColorAmt > 0.0))
	{
		Color.rgb = OpticalVignette(Color.rgb, WorkUV);
	}


	//---------- HALATION + VEILING GLARE COMPOSITE ----------//
	[branch] if(UIHL_Enable || UIVG_Enable)
	{
		float4 HalationData = RenderTarget512.SampleLevel(Linear_Sampler, IN.texcoord, 0).rgba;
		Color.rgb += HalationData.rgb;
	}


	//---------- WEATHER: RAIN BLUR COMPOSITE ----------//
	[branch] if(IN.EnableRain)
	{
		float4 StepSize     = PixelSize.xyxy * float4(0.0, 1.0, 0.0, -1.0);
		float4 HalfStepSize = StepSize * IN.OffsetScale;
		       StepSize    *= 2.0;

		float  WeightSum  = IN.WeightFactor * IN.OffsetScale;
		float4 BlurredCol = WeightSum * RenderTargetRGBA64F.SampleLevel(Point_Sampler, IN.texcoord, 0);

		[loop] for(float i=1.0; i <= IN.LoopCount; i++)
		{
			float  GaussWeight = IN.WeightFactor * exp(i*i*IN.Sigma);
			float4 CurrOffset  = IN.texcoord.xyxy + i * StepSize - HalfStepSize;

			BlurredCol += GaussWeight * RenderTargetRGBA64F.SampleLevel(Linear_Sampler, CurrOffset.xy,0);
			BlurredCol += GaussWeight * RenderTargetRGBA64F.SampleLevel(Linear_Sampler, CurrOffset.zw,0);
			WeightSum  += GaussWeight * 2.0;
		}

		Color += BlurredCol / WeightSum;
	}

	//---------- WEATHER: FROST ----------//
	[branch] if(IN.EnableFrost)
	{
		float VigWeight = VigGradient * IN.Radii.y - IN.Radii.x;
		      VigWeight = saturate(pow(VigWeight, IN.Vignette.x) * IN.Vignette.y);

		#if FROSTFX_USE_REFRACTION_METHOD
			float3 FrostData = ProceduralFrostRefraction(IN.texcoord);

			float2 MappedCoords  = FrostData.xy * 2.0 + 1.0;
			       MappedCoords *= UIWFXFrost_RefracRange;
			       MappedCoords -= UIWFXFrost_RefracRange * 2.0;
			       MappedCoords += IN.texcoord;

			float3 RefracCol   = TextureOriginal.SampleLevel(Linear_Sampler_Rain, MappedCoords, 0).rgb;
			bool   NoOvershoot = all(saturate(MappedCoords - MappedCoords * MappedCoords));
			       NoOvershoot = NoOvershoot || UIWFXFrost_AllowOvershoot;
			       VigWeight   = saturate(VigWeight * FrostData.z * NoOvershoot);

			float Opacity   = FrostData.z / dot(abs(FrostData.xy - 0.5), 99.0 - UIWFXFrost_Opacity);
			      RefracCol = lerp(RefracCol, IN.FrostTint, saturate(Opacity));

			OOSSampleApprox(RefracCol, MappedCoords, IN.SkyColor, IN.EnvColor);
			Color = lerp(Color, float4(RefracCol, 1.0), VigWeight);

		#else
			float3 FrostData = ProceduralFrostLayers(IN.texcoord);
			float  FrostCoverage  = FrostData.x;
			float  FrostThickness = FrostData.y;
			float  FrostSparkle   = FrostData.z;

			float3 SceneUnder = Color.rgb;
			float  SceneLuma  = dot(SceneUnder, K_LUM);

			float DesatAmount = FrostThickness * 0.75;
			float3 SceneDesat = lerp(SceneUnder, SceneLuma, DesatAmount);

			float3 ThinIceColor  = float3(0.82, 0.90, 1.0);
			float3 ThickFrostCol = IN.FrostTint;
			float3 FrostSurface  = lerp(ThinIceColor, ThickFrostCol, FrostThickness);

			float AmbientBright = lerp(0.35, 1.0, FrostThickness);

			float3 Subsurface = SceneDesat * ThinIceColor * (1.0 - FrostThickness * 0.8);
			FrostSurface = lerp(FrostSurface * AmbientBright + Subsurface * 0.15, FrostSurface * AmbientBright, FrostThickness);

			float  Opacity     = lerp(0.12, 0.95, FrostThickness) * FrostCoverage;
			float3 FrostResult = lerp(SceneDesat, FrostSurface, Opacity);

			float SparkleBoost = (1.0 + SceneLuma * 0.5) * 1.8;
			FrostResult += FrostSparkle * SparkleBoost * IN.FrostTint;

			float FinalWeight = saturate(VigWeight * FrostCoverage);
			Color.rgb = lerp(Color.rgb, FrostResult, FinalWeight);
		#endif
	}

	return Color;
}




//=============================================================================
//  PROFESSIONAL FILM GRAIN  (Tech 13 — combined with Sensor Response)
//
//  Multi-layer emulsion grain with luminance response and per-channel
//  decorrelation. Uses IGN (Jimenez 2014) for TAA-compatible noise.
//=============================================================================

struct VertexShaderOutputFilmGrain
{
   float4 pos            : SV_POSITION;
   float2 texcoord       : TEXCOORD0;
NI float  FG_Int         : FG0;
NI float  FG_Size        : FG1;
NI float  FG_Roughness   : FG2;
NI float  FG_FrameSeed   : FG3;
NI float3 FG_LumaResp    : FG4;
NI float3 FG_ChanWeight   : FG5;
NI float  FG_ChanDecorr   : FG6;
};


//Interleaved gradient noise (Jimenez 2014, CoD: Advanced Warfare)
float FG_IGN(float2 PixelCoord, float FrameOffset)
{
	float3 Magic = float3(0.06711056, 0.00583715, 52.9829189);
	float2 Shifted = PixelCoord + FrameOffset * float2(5.588238, 5.588238);
	return frac(Magic.z * frac(dot(Shifted, Magic.xy)));
}

//Blue-noise-quality hash
float FG_Hash(float2 Coord, float Seed)
{
	uint2 IC = uint2(Coord);
	uint  H  = IC.x * 0x9e3779b9u + IC.y * 0x517cc1b7u + asuint(Seed);
	H ^= H >> 16u;
	H *= 0x45d9f3bu;
	H ^= H >> 16u;
	return float(H) / 4294967295.0;
}


//Multi-layer grain with gaussian distribution and organic clumping
float FG_Structured(float2 Coord, float Seed, float Roughness)
{
	float Fine1 = FG_IGN(Coord, Seed);
	float Fine2 = FG_IGN(Coord, Seed + 3.17);
	float Fine3 = FG_IGN(Coord, Seed + 7.89);

	//CLT: average of 3 uniform → approximately gaussian
	float FineGrain = (Fine1 + Fine2 + Fine3) * 0.333333;

	//Coarser clumps (halide crystal clusters)
	float Coarse1 = FG_Hash(floor(Coord * 0.5), Seed + 11.0);
	float Coarse2 = FG_Hash(floor(Coord * 0.25), Seed + 23.0);

	float Grain = lerp(FineGrain,
	                    FineGrain * 0.5 + Coarse1 * 0.35 + Coarse2 * 0.15,
	                    Roughness);

	//Sharpen gaussian tail for film-like character
	float Excess = abs(Grain - 0.5);
	Grain = lerp(Grain, sign(Grain - 0.5) * 0.5 * pow(2.0 * Excess, 0.85) + 0.5, 0.2);

	return Grain - 0.5;
}


//H&D sensitometry: luminance-dependent grain response
float FG_LuminanceResponse(float Luma, float3 Response)
{
	float Shadow    = 1.0 - smoothstep(0.0,  0.40, Luma);
	float Highlight = smoothstep(0.50, 0.95, Luma);
	float Midtone   = smoothstep(0.05, 0.30, Luma) * (1.0 - smoothstep(0.60, 0.90, Luma));

	float Total = Shadow + Midtone + Highlight + DELTA;

	return (Shadow * Response.x + Midtone * Response.y + Highlight * Response.z) / Total;
}


//=============================================================================
//  SENSOR RESPONSE — Film S-curve, channel crosstalk, sensor noise
//=============================================================================

float SensorShoulder(float X, float Start)
{
	float Excess = max(X - Start, 0.0);
	float Compressed = Start + Excess / (1.0 + Excess);
	return (X > Start) ? Compressed : X;
}

float SensorToe(float X, float ToeEnd)
{
	float Normalized = saturate(X / max(ToeEnd, DELTA));
	float ToeShape   = Normalized * Normalized * (3.0 - 2.0 * Normalized);
	return (X < ToeEnd) ? ToeShape * ToeEnd : X;
}

float3 ApplySensorResponse(float3 Color, float2 PixelPos, float FrameSeed)
{
	//Film response curve
	[branch] if(UISR_Shoulder > 0.0 || UISR_Toe > 0.0)
	{
		float ShoulderAmt = UISR_Shoulder;
		float ToeAmt      = UISR_Toe;

		[unroll] for(int c = 0; c < 3; c++)
		{
			if(ShoulderAmt > 0.0)
				Color[c] = lerp(Color[c], SensorShoulder(Color[c], UISR_ShoulderStart), ShoulderAmt);
			if(ToeAmt > 0.0)
				Color[c] = lerp(Color[c], SensorToe(Color[c], UISR_ToeEnd), ToeAmt);
		}
	}

	//Channel crosstalk: CFA dye overlap
	[branch] if(UISR_Crosstalk > 0.0)
	{
		float3 Bleed;
		Bleed.r = Color.r + Color.g * UISR_CrossGtoB * UISR_Crosstalk
		                   + Color.b * UISR_CrossBtoR * UISR_Crosstalk;
		Bleed.g = Color.g + Color.r * UISR_CrossRtoG * UISR_Crosstalk
		                   + Color.b * UISR_CrossBtoR * UISR_Crosstalk * 0.5;
		Bleed.b = Color.b + Color.g * UISR_CrossGtoB * UISR_Crosstalk
		                   + Color.r * UISR_CrossRtoG * UISR_Crosstalk * 0.5;

		float OrigLuma = dot(Color, K_LUM);
		float BleedLuma = dot(Bleed, K_LUM);
		Color = Bleed * (OrigLuma / max(BleedLuma, DELTA));
	}

	//Sensor noise (IGN-based for TAA compatibility)
	float NoiseBase  = FG_IGN(PixelPos, FrameSeed);
	float NoiseBase2 = FG_IGN(PixelPos, FrameSeed + 3.17);
	float NoiseBase3 = FG_IGN(PixelPos, FrameSeed + 7.89);

	//Read noise: gaussian-approximated, constant
	[branch] if(UISR_ReadNoise > 0.0)
	{
		float3 ReadN = float3(NoiseBase, NoiseBase2, NoiseBase3);
		ReadN = (ReadN - 0.5) * UISR_ReadNoise;
		Color += ReadN;
	}

	//Shot noise: Poisson-distributed, proportional to sqrt(signal)
	[branch] if(UISR_PhotonNoise > 0.0)
	{
		float3 ShotN = float3(NoiseBase2, NoiseBase3, NoiseBase);
		ShotN = (ShotN - 0.5) * UISR_PhotonNoise * sqrt(max(Color, DELTA));
		Color += ShotN;
	}

	//Hot pixels: stuck-on photodiodes
	[branch] if(UISR_HotPixels > 0.0)
	{
		float HotThresh = 1.0 - UISR_HotPixels;
		float StableHash = abs(frac(sin(dot(PixelPos, float2(127.1, 311.7))) * 43758.5453));
		if(StableHash > HotThresh)
		{
			float HotVal = 0.95 + StableHash * 0.05;
			float ChanHash = frac(StableHash * 7.31);
			if(ChanHash < 0.333)
				Color.r = max(Color.r, HotVal);
			else if(ChanHash < 0.666)
				Color.g = max(Color.g, HotVal);
			else
				Color.b = max(Color.b, HotVal);
		}
	}

	return saturate(Color);
}


VertexShaderOutputFilmGrain VS_FilmGrain(VertexShaderInput IN)
{
	VertexShaderOutputFilmGrain OUT;
	OUT.pos      = float4(IN.pos.xyz, 1.0);
	OUT.texcoord = IN.txcoord.xy;

	OUT.FG_Int        = DNI_SEPARATION(UIFG_Intensity);
	OUT.FG_Size       = UIFG_Size;
	OUT.FG_Roughness  = UIFG_Roughness;
	OUT.FG_ChanWeight  = UIFG_ChannelWeight;
	OUT.FG_ChanDecorr  = UIFG_ChannelDecorr;
	OUT.FG_LumaResp    = float3(UIFG_ShadowGrain, UIFG_MidtoneGrain, UIFG_HighlightGrain);

	OUT.FG_FrameSeed = floor(Timer.z * UIFG_AnimSpeed);

	return OUT;
}


float4 PS_FilmGrain(VertexShaderOutputFilmGrain IN) : SV_Target
{
	float3 Color = TextureColor.SampleLevel(Point_Sampler, IN.texcoord, 0).rgb;

	[branch] if(UIFG_Enable && IN.FG_Int > 0.0)
	{
		float2 GrainCoord = IN.pos.xy / IN.FG_Size;
		float TSeed = IN.FG_FrameSeed * 1.618033988;

		float3 Grain;
		Grain.r = FG_Structured(GrainCoord, TSeed, IN.FG_Roughness);

		float GrainG = FG_Structured(GrainCoord + float2(1.47, 2.93), TSeed + 5.39, IN.FG_Roughness);
		float GrainB = FG_Structured(GrainCoord + float2(3.71, 0.59), TSeed + 9.71, IN.FG_Roughness);
		Grain.g = lerp(Grain.r, GrainG, IN.FG_ChanDecorr);
		Grain.b = lerp(Grain.r, GrainB, IN.FG_ChanDecorr);

		Grain *= IN.FG_ChanWeight;

		float Luma     = dot(Color, K_LUM);
		float Response = FG_LuminanceResponse(Luma, IN.FG_LumaResp);

		float3 GrainAmount = Grain * IN.FG_Int * Response;

		//Photographic soft clip
		float3 AbsGrain = abs(GrainAmount);
		float3 SoftClip = 1.0 - exp(-AbsGrain * 3.5);
		GrainAmount = sign(GrainAmount) * SoftClip * 0.285;

		//Apply in perceptual space
		float3 SqrtColor = sqrt(max(Color, 0.0));
		SqrtColor += GrainAmount * 0.5;
		Color = saturate(SqrtColor * SqrtColor + GrainAmount * 0.5);
	}

	//---------- SENSOR RESPONSE ----------//
	[branch] if(UISR_Enable)
	{
		Color = ApplySensorResponse(Color, IN.pos.xy, IN.FG_FrameSeed);
	}

	return float4(Color, 1.0);
}




//=============================================================================
//  HALATION + VEILING GLARE PREPASS  (Tech 11 → RT512)
//
//  Halation: subsurface scatter through film base / sensor cover glass.
//  Bright highlights bleed with warm red-orange tint (wavelength-dependent).
//  Veiling Glare: low-frequency scatter from lens surface imperfections.
//=============================================================================

static const float2 PoissonDisc16[16] = {
	float2(-0.94201624, -0.39906216), float2( 0.94558609, -0.76890725),
	float2(-0.09418410, -0.92938870), float2( 0.34495938,  0.29387760),
	float2(-0.91588581,  0.45771432), float2(-0.81544232, -0.87912464),
	float2(-0.38277543,  0.27676845), float2( 0.97484398,  0.75648379),
	float2( 0.44323325, -0.97511554), float2( 0.53742981, -0.47373420),
	float2(-0.26496911, -0.41893023), float2( 0.79197514,  0.19090188),
	float2(-0.24188840,  0.99706507), float2(-0.81409955,  0.91437590),
	float2( 0.19984126,  0.78641367), float2( 0.14383161, -0.14100790)
};

static const float2 GlareRing8[8] = {
	float2( 1.0,  0.0), float2( 0.707,  0.707),
	float2( 0.0,  1.0), float2(-0.707,  0.707),
	float2(-1.0,  0.0), float2(-0.707, -0.707),
	float2( 0.0, -1.0), float2( 0.707, -0.707)
};


struct VertexShaderOutputHalation
{
   float4 pos          : SV_POSITION;
   float2 texcoord     : TEXCOORD0;
NI float  HL_Int        : HL0;
NI float  HL_Thresh     : HL1;
NI float  HL_ThCurve    : HL2;
NI float  HL_Radius     : HL3;
NI float3 HL_Color      : HL4;
NI float  HL_Sat        : HL5;
NI float  VG_Int        : HL6;
NI float  VG_Thresh     : HL7;
NI float  VG_Radius     : HL8;
NI float3 VG_Tint       : HL9;
};


VertexShaderOutputHalation VS_Halation(VertexShaderInput IN)
{
	VertexShaderOutputHalation OUT;
	OUT.pos       = float4(IN.pos.xyz, 1.0);
	OUT.texcoord  = IN.txcoord.xy;

	OUT.HL_Int      = UIHL_Enable ? DNI_SEPARATION(UIHL_Intensity) : 0.0;
	OUT.HL_Thresh   = UIHL_Threshold;
	OUT.HL_ThCurve  = UIHL_ThreshCurve;
	OUT.HL_Radius   = UIHL_Radius;
	OUT.HL_Color    = UIHL_Color;
	OUT.HL_Sat      = UIHL_Saturation;

	OUT.VG_Int      = UIVG_Enable ? UIVG_Intensity : 0.0;
	OUT.VG_Thresh   = UIVG_Threshold;
	OUT.VG_Radius   = UIVG_Radius;
	OUT.VG_Tint     = UIVG_Tint;

	return OUT;
}


float4 PS_Halation(VertexShaderOutputHalation IN) : SV_Target
{
	float4 Result = float4(0.0, 0.0, 0.0, 0.0);

	if(IN.HL_Int <= 0.0 && IN.VG_Int <= 0.0) return Result;

	float2 RadiusHL = IN.HL_Radius * PixelSize;
	float2 RadiusVG = IN.VG_Radius * PixelSize;

	//Wavelength-dependent scatter: red > green > blue (film antihalation layer)
	static const float3 WavelengthScale = float3(1.4, 1.0, 0.7);

	//---------- HALATION ----------//
	float3 HalationAccum = float3(0.0, 0.0, 0.0);

	[branch] if(IN.HL_Int > 0.0)
	{
		float3 HalationWeight = float3(DELTA, DELTA, DELTA);

		[unroll] for(int h = 0; h < 16; h++)
		{
			float  DiscR   = length(PoissonDisc16[h]);
			float  RingW   = smoothstep(0.1, 0.6, DiscR);
			float  BaseW   = 0.5 + RingW * 0.5;

			float2 Dir = PoissonDisc16[h];

			float2 UV_R = IN.texcoord + Dir * RadiusHL * WavelengthScale.r;
			float2 UV_G = IN.texcoord + Dir * RadiusHL * WavelengthScale.g;
			float2 UV_B = IN.texcoord + Dir * RadiusHL * WavelengthScale.b;

			float3 TapR = TextureColor.SampleLevel(Linear_Sampler, UV_R, 0).rgb;
			float3 TapG = TextureColor.SampleLevel(Linear_Sampler, UV_G, 0).rgb;
			float3 TapB = TextureColor.SampleLevel(Linear_Sampler, UV_B, 0).rgb;

			float3 Lumas  = float3(dot(TapR, K_LUM), dot(TapG, K_LUM), dot(TapB, K_LUM));
			float3 Excess = max(Lumas - IN.HL_Thresh, 0.0);
			float3 Mask   = pow(saturate(Excess * 2.0), IN.HL_ThCurve);
			float3 W      = Mask * BaseW;

			HalationAccum.r += TapR.r * W.r;
			HalationAccum.g += TapG.g * W.g;
			HalationAccum.b += TapB.b * W.b;
			HalationWeight  += W;
		}

		HalationAccum /= HalationWeight;

		float  HaloLuma = dot(HalationAccum, K_LUM);
		float3 HaloMono = float3(HaloLuma, HaloLuma, HaloLuma);
		HalationAccum = lerp(HaloMono, HalationAccum, IN.HL_Sat);
		HalationAccum *= IN.HL_Color;

		Result.rgb += HalationAccum * IN.HL_Int;
	}


	//---------- VEILING GLARE ----------//
	[branch] if(IN.VG_Int > 0.0)
	{
		float3 GlareAccum = float3(0.0, 0.0, 0.0);
		float  GlareWeight = 0.0;

		[unroll] for(int ring = 0; ring < 3; ring++)
		{
			float RingNorm = (float)(ring + 1) / 3.0;
			float RingW = exp(-RingNorm * RingNorm * 2.0);

			[unroll] for(int g = 0; g < 8; g++)
			{
				float2 SampleUV = IN.texcoord + GlareRing8[g] * RadiusVG * RingNorm;
				float3 Tap = TextureColor.SampleLevel(Linear_Sampler, SampleUV, 0).rgb;

				float TapLuma = dot(Tap, K_LUM);
				float Excess  = max(TapLuma - IN.VG_Thresh, 0.0);
				float W = saturate(Excess * 2.0) * RingW;

				GlareAccum += Tap * W;
				GlareWeight += W;
			}
		}

		GlareAccum /= max(GlareWeight, DELTA);
		GlareAccum *= IN.VG_Tint;

		float GlareAmount = dot(GlareAccum, K_LUM) * IN.VG_Int;
		Result.rgb += GlareAccum * IN.VG_Int;
		Result.a    = saturate(GlareAmount);
	}

	return Result;
}




//=============================================================================
//  COMPILED SHADER OBJECTS
//=============================================================================

VertexShader VS_Basic_Comp = CompileShader(vs_5_0, VS_Basic());
PixelShader  PS_Blank_Comp = CompileShader(ps_5_0, PS_Blank());

VertexShader VS_AnamFlarePrePass_Comp = CompileShader(vs_5_0, VS_AnamFlarePrePass());
PixelShader  PS_AnamFlarePrePass_Comp = CompileShader(ps_5_0, PS_AnamFlarePrePass());
VertexShader VS_AnamFlare_Comp        = CompileShader(vs_5_0, VS_AnamFlare());
PixelShader  PS_AnamFlare_Comp        = CompileShader(ps_5_0, PS_AnamFlare());

VertexShader VS_Reflection_Comp = CompileShader(vs_5_0, VS_Reflection());
PixelShader  PS_Reflection_Comp = CompileShader(ps_5_0, PS_Reflection());

VertexShader VS_GhostAccum_Comp = CompileShader(vs_5_0, VS_Basic());
PixelShader  PS_GhostAccum_Comp = CompileShader(ps_5_0, PS_GhostAccumulate());

VertexShader VS_StarburstPrePass_Comp  = CompileShader(vs_5_0, VS_StarburstPrePass());
PixelShader  PS_StarburstPrePass_Comp  = CompileShader(ps_5_0, PS_StarburstPrePass());
VertexShader VS_StarburstMain_Comp     = CompileShader(vs_5_0, VS_StarburstMain());
PixelShader  PS_StarburstMain_Comp     = CompileShader(ps_5_0, PS_StarburstMain());
VertexShader VS_StarburstPostPass_Comp = CompileShader(vs_5_0, VS_StarburstPostPass());
PixelShader  PS_StarburstPostPass_Comp = CompileShader(ps_5_0, PS_StarburstPostPass());

VertexShader VS_DirtAndPostPass_Comp = CompileShader(vs_5_0, VS_DirtAndPostPass());
PixelShader  PS_DirtAndPostPass_Comp = CompileShader(ps_5_0, PS_DirtAndPostPass());

PixelShader  PS_Rain_Comp  = CompileShader(ps_5_0, PS_Rain());
VertexShader VS_RainBlur_Comp = CompileShader(vs_5_0, VS_RainBlur());
PixelShader  PS_RainBlur_Comp = CompileShader(ps_5_0, PS_RainBlur());

VertexShader VS_Halation_Comp = CompileShader(vs_5_0, VS_Halation());
PixelShader  PS_Halation_Comp = CompileShader(ps_5_0, PS_Halation());

VertexShader VS_Frost_Comp = CompileShader(vs_5_0, VS_Frost());
PixelShader  PS_Frost_Comp = CompileShader(ps_5_0, PS_Frost());

VertexShader VS_FilmGrain_Comp = CompileShader(vs_5_0, VS_FilmGrain());
PixelShader  PS_FilmGrain_Comp = CompileShader(ps_5_0, PS_FilmGrain());


//=============================================================================
//  TECHNIQUE MACROS
//=============================================================================

#undef  TECH11
#define TECH11(NAME, VS, PS) \
technique11 NAME {pass p0 {SetVertexShader(VS); SetPixelShader (PS);}}

#undef  TWOPASSTECH11
#define TWOPASSTECH11(NAME, VS1, PS1, VS2, PS2) \
technique11 NAME {pass p0 {SetVertexShader(VS1); SetPixelShader (PS1);}\
				  pass p1 {SetVertexShader(VS2); SetPixelShader (PS2);}}

#define RAINPASS(PN,VS) \
pass PN{ SetVertexShader(CompileShader(vs_5_0, VS_Rain##VS)); \
		 SetPixelShader (PS_Rain_Comp); \
		 SetBlendState  (RainBlending, float4(1.0,1.0,1.0,1.0), 0xFFFFFFFF); }

#define CLEARPASS(PN) \
pass PN{ SetVertexShader(VS_Basic_Comp);\
		 SetPixelShader (PS_Blank_Comp);}

#define RAINTECH(NAME) \
technique11 NAME <string RenderTarget="RenderTargetRGBA32";> \
{ CLEARPASS(Clear) \
  RAINPASS(Pass0,  (float4(967.0, 296.0, 477.0, 806.0),       0.0))  RAINPASS(Pass1,  (float4( 63.0, 278.0, 501.0, 392.0),  1.0/16.0)) \
  RAINPASS(Pass2,  (float4(615.0, 735.0, 628.0, 128.0),  2.0/16.0))  RAINPASS(Pass3,  (float4(490.0, 339.0, 887.0, 289.0),  3.0/16.0)) \
  RAINPASS(Pass4,  (float4(665.0, 708.0, 408.0, 518.0),  4.0/16.0))  RAINPASS(Pass5,  (float4(173.0, 683.0, 784.0, 453.0),  5.0/16.0)) \
  RAINPASS(Pass6,  (float4( 72.0, 866.0,  83.0, 292.0),  6.0/16.0))  RAINPASS(Pass7,  (float4(493.0, 338.0, 694.0, 133.0),  7.0/16.0)) \
  RAINPASS(Pass8,  (float4(217.0, 612.0, 251.0, 867.0),  8.0/16.0))  RAINPASS(Pass9,  (float4(368.0, 199.0, 834.0, 959.0),  9.0/16.0)) \
  RAINPASS(Pass10, (float4(567.0, 913.0, 780.0, 545.0), 10.0/16.0))  RAINPASS(Pass11, (float4(649.0, 764.0, 304.0, 620.0), 11.0/16.0)) \
  RAINPASS(Pass12, (float4(924.0, 104.0, 226.0, 849.0), 12.0/16.0))  RAINPASS(Pass13, (float4(993.0, 495.0, 320.0, 382.0), 13.0/16.0)) \
  RAINPASS(Pass14, (float4(352.0, 472.0, 213.0, 382.0), 14.0/16.0))  RAINPASS(Pass15, (float4( 67.0, 144.0,  16.0, 861.0), 15.0/16.0)) }


//=============================================================================
//  TECHNIQUE BLOCK — Single variant, 14 techniques
//
//  Render target time-sharing:
//    RGBA64F: Tech 0 (anam prepass) → Tech 3 (ghost accum) → Tech 10 (rain blur)
//    RT512:   Tech 2 (reflections) → Tech 11 (halation)
//    RT256:   Tech 1 (anam main)
//    RGBA32:  Tech 9 (rain droplets)
//=============================================================================

//--- Tech 0: Anamorphic prepass → RGBA64F ---
TWOPASSTECH11 (KitsuuneMasterLens   <string UIName="Master Lens";
                                      string RenderTarget="RenderTargetRGBA64F";>,
               VS_Basic_Comp,             PS_Blank_Comp,
               VS_AnamFlarePrePass_Comp,  PS_AnamFlarePrePass_Comp)

//--- Tech 1: Anamorphic main → RT256 ---
TECH11        (KitsuuneMasterLens1  <string RenderTarget="RenderTarget256";>,
               VS_AnamFlare_Comp,         PS_AnamFlare_Comp)

//--- Tech 2: Reflections → RT512 ---
TECH11        (KitsuuneMasterLens2  <string RenderTarget="RenderTarget512";>,
               VS_Reflection_Comp,        PS_Reflection_Comp)

//--- Tech 3: Ghost single-pass accumulation → RGBA64F ---
TECH11        (KitsuuneMasterLens3  <string RenderTarget="RenderTargetRGBA64F";>,
               VS_GhostAccum_Comp,        PS_GhostAccum_Comp)

//--- Tech 4: Starburst clear ---
TECH11        (KitsuuneMasterLens4,
               VS_Basic_Comp,             PS_Blank_Comp)

//--- Tech 5: Starburst init (blank + prefilter) ---
TWOPASSTECH11 (KitsuuneMasterLens5,
               VS_Basic_Comp,             PS_Blank_Comp,
               VS_StarburstPrePass_Comp,  PS_StarburstPrePass_Comp)

//--- Tech 6: Starburst main ---
TECH11        (KitsuuneMasterLens6,
               VS_StarburstMain_Comp,     PS_StarburstMain_Comp)

//--- Tech 7: Starburst postpass (reads RGBA64F for ghosts) ---
TECH11        (KitsuuneMasterLens7,
               VS_StarburstPostPass_Comp, PS_StarburstPostPass_Comp)

//--- Tech 8: Dirt and post-pass (reads RT512 reflections, RT256 anam, TextureColor) ---
TECH11        (KitsuuneMasterLens8,
               VS_DirtAndPostPass_Comp,   PS_DirtAndPostPass_Comp)

//--- Tech 9: Rain droplets → RGBA32 ---
RAINTECH      (KitsuuneMasterLens9)

//--- Tech 10: Rain blur → RGBA64F ---
TECH11        (KitsuuneMasterLens10 <string RenderTarget="RenderTargetRGBA64F";>,
               VS_RainBlur_Comp,          PS_RainBlur_Comp)

//--- Tech 11: Halation prepass → RT512 ---
TECH11        (KitsuuneMasterLens11 <string RenderTarget="RenderTarget512";>,
               VS_Halation_Comp,          PS_Halation_Comp)

//--- Tech 12: Lens Optics composite (frost + all aberrations + weather + halation readback) ---
TECH11        (KitsuuneMasterLens12,
               VS_Frost_Comp,             PS_Frost_Comp)

//--- Tech 13: Film Grain + Sensor Response ---
TECH11        (KitsuuneMasterLens13,
               VS_FilmGrain_Comp,         PS_FilmGrain_Comp)

