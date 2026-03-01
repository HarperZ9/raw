//=============================================================================
//  ENB Sunsprite v3.0 — Ground-Up Rewrite for SkyrimBridge
//
//  Based on enbsunsprite.fx by LonelyKitsuune (Silent Horizons ENB)
//  Atmospheric & ocular physics by Zain Dana Harper
//  For ENB (DirectX 11 Shader Model 5)
//
//  Copyright (c) 2019-2020 LonelyKitsuune / T.Thanner - CC BY-NC-ND 4.0
//  Extensions: Zain Dana Harper 2026 — all rights reserved
//=============================================================================

//=============================================================================
//  OPTIONS
//=============================================================================

//Custom sun intensity detector (screen-sample + mask-based)
#define USE_CUSTOM_SUNINTENSITY_DETECTOR  1 //[0-1]
#define ENABLE_DETECTOR_DEPTH_TESTING     0 //[0-1]

//Hoop/starburst anti-aliasing (supersampled randomness)
#define ENABLE_HOOP_ANTIALIASING          1 //[0-1]
#define ENABLE_STARBURST_ANTIALIASING     1 //[0-1]


//=============================================================================
//  INCLUDES
//=============================================================================

#include "UI/enbUI_Primer.fxh"


//=============================================================================
//  INLINE UI PARAMETERS
//=============================================================================

// ─────────────────── Lens Settings ─────────────────────────────────────────
UI_FileHeaderLong(">>>       ENB Sunsprite 3.0 for SSE       <<<",
                  ">>>     SkyrimBridge Ground-Up Rewrite     <<<")

UI_WHITESPACE(1)
UI_ELEMENT(Lens_Settings,"         >>>>>>LENS SETTINGS<<<<<<")
UI_WHITESPACE(2)
bool  UI_EnableGlares      < string UIGroup = "Lens"; string UIName="|- Enable Glares";          > = {false};
bool  UI_EnableStarburst   < string UIGroup = "Lens"; string UIName="|- Enable Starburst";       > = {false};
bool  UI_EnableGhost       < string UIGroup = "Lens"; string UIName="|- Enable Ghost Flares";    > = {false};
bool  UI_EnableHoop        < string UIGroup = "Lens"; string UIName="|- Enable Hoop/Ring Flare"; > = {false};
UI_SPECIAL_WHITESPACE(21)
int   UI_ApertureShape     < string UIGroup = "Lens"; string UIName="|- Aperture - Shape";          int   UIMin=4;    int   UIMax=9;   string Separation = "ExteriorWeather";> = {6};
int   UI_ApertureRotation  < string UIGroup = "Lens"; string UIName="|- Aperture - Rotation(\xB0)"; int   UIMin=0;    int   UIMax=360; string Separation = "ExteriorWeather";> = {0};
float UI_ApertureRoundness < string UIGroup = "Lens"; string UIName="|- Aperture - Roundness";      float UIMin=0.0;  float UIMax=1.0; string Separation = "ExteriorWeather";> = {0.1};


// ─────────────────── Starburst ─────────────────────────────────────────────
UI_WHITESPACE(3)
UI_ELEMENT(SB_Settings," >>>>>>STARBURST LENS FLARE<<<<<<")
UI_WHITESPACE(5)

float  UISB_FlareInt          < string UIGroup = "Starburst"; string UIName="|- Starburst - Intensity";              float UIMin=0.0; float UIMax=10.0; string Separation = "ExteriorWeather";> = {1.0};
float  UISB_Scale             < string UIGroup = "Starburst"; string UIName="|- Starburst - Scale";                  float UIMin=0.0; float UIMax= 3.0; string Separation = "ExteriorWeather";> = {1.0};
float  UISB_Falloff           < string UIGroup = "Starburst"; string UIName="|- Starburst - Falloff";                float UIMin=1.0; float UIMax=10.0; string Separation = "ExteriorWeather";> = {1.0};
float  UISB_BurstWidth        < string UIGroup = "Starburst"; string UIName="|- Starburst - Burst Width";            float UIMin=0.0; float UIMax= 1.0; string Separation = "ExteriorWeather";> = {0.8};
float  UISB_BurstWidthCurve   < string UIGroup = "Starburst"; string UIName="|- Starburst - Burst Width Curve";      float UIMin=0.0; float UIMax=10.0; string Separation = "ExteriorWeather";> = {0.7};
float  UISB_RandFrequency     < string UIGroup = "Starburst"; string UIName="|- Starburst - Randomness Frequency";   float UIMin=0.0; float UIMax=50.0; string Separation = "ExteriorWeather";> = {10.0};
float  UISB_RandIntensity     < string UIGroup = "Starburst"; string UIName="|- Starburst - Randomness Intensity";   float UIMin=0.0; float UIMax= 5.0; string Separation = "ExteriorWeather";> = {1.0};
float  UISB_StrayBurstAmount  < string UIGroup = "Starburst"; string UIName="|- Starburst - Stray Bursts Amount";    float UIMin=0.0; float UIMax= 1.0; string Separation = "ExteriorWeather";> = {0.1};
float  UISB_StrayBurstInt     < string UIGroup = "Starburst"; string UIName="|- Starburst - Stray Bursts Intensity"; float UIMin=0.0; float UIMax=10.0; string Separation = "ExteriorWeather";> = {0.8};
float  UISB_StrayBurstFalloff < string UIGroup = "Starburst"; string UIName="|- Starburst - Stray Bursts Falloff";   float UIMin=1.0; float UIMax=10.0; string Separation = "ExteriorWeather";> = {5.0};
float4 UISB_FlareTint         < string UIGroup = "Starburst"; string UIName="|- Starburst - Tint";                   string UIWidget="Color";           string Separation = "ExteriorWeather";> = {1,1,1,1};
float  UISB_SpectralAmt       < string UIGroup = "Starburst"; string UIName="|- Starburst - Spectral Amount";        float UIMin=0.0; float UIMax=1.0;  string Separation = "ExteriorWeather";> = {0.4};
float  UISB_SpectralFreq      < string UIGroup = "Starburst"; string UIName="|- Starburst - Spectral Cycles";        float UIMin=0.5; float UIMax=5.0;  string Separation = "ExteriorWeather";> = {1.5};


// ─────────────────── Hoop / Ring Flare ─────────────────────────────────────
UI_WHITESPACE(6)
UI_ELEMENT(HOOP_Settings,"      >>>>>>HOOP LENS FLARE<<<<<<")
UI_WHITESPACE(8)

float  UIHoop_Int         < string UIGroup = "HoopFlare"; string UIName="|- Hoop - Intensity";           float UIMin=0.0;  float UIMax=20.0;  string Separation = "ExteriorWeather";> = {2.0};
float  UIHoop_Scale       < string UIGroup = "HoopFlare"; string UIName="|- Hoop - Scale";               float UIMin=0.0;  float UIMax=20.0;  string Separation = "ExteriorWeather";> = {7.5};
float  UIHoop_Offset      < string UIGroup = "HoopFlare"; string UIName="|- Hoop - Offset";              float UIMin=-5.0; float UIMax=5.0;   string Separation = "ExteriorWeather";> = {0.7};
float  UIHoop_Width       < string UIGroup = "HoopFlare"; string UIName="|- Hoop - Width";               float UIMin=0.0;  float UIMax=1.0;   string Separation = "ExteriorWeather";> = {0.25};
float  UIHoop_Roundness   < string UIGroup = "HoopFlare"; string UIName="|- Hoop - Roundness";           float UIMin=0.0;  float UIMax=10.0;  string Separation = "ExteriorWeather";> = {1.0};
float  UIHoop_CA          < string UIGroup = "HoopFlare"; string UIName="|- Hoop - Chromatic Aberration"; float UIMin=0.0; float UIMax=5.0;   string Separation = "ExteriorWeather";> = {0.1};
float  UIHoop_SunDistMod  < string UIGroup = "HoopFlare"; string UIName="|- Hoop - Distance Modifier";   float UIMin=0.0;  float UIMax=10.0;  string Separation = "ExteriorWeather";> = {1.5};
float  UIHoop_Fade        < string UIGroup = "HoopFlare"; string UIName="|- Hoop - Fade";                float UIMin=0.0;  float UIMax=3.0;   string Separation = "ExteriorWeather";> = {0.7};
float  UIHoop_PatternFreq < string UIGroup = "HoopFlare"; string UIName="|- Hoop - Pattern Frequency";   float UIStep=1.0; float UIMin=0.0; float UIMax=500.0; string Separation = "ExteriorWeather";> = {100.0};
float  UIHoop_IntRandom   < string UIGroup = "HoopFlare"; string UIName="|- Hoop - Intensity Randomness"; float UIStep=1.0; float UIMin=0.0; float UIMax=300.0; string Separation = "ExteriorWeather";> = {80.0};
float4 UIHoop_Tint        < string UIGroup = "HoopFlare"; string UIName="|- Hoop - Tint";                string UIWidget="Color";             string Separation = "ExteriorWeather";> = {1,1,1,1};


// ─────────────────── Sun and Lens Glare ────────────────────────────────────
UI_WHITESPACE(9)
UI_ELEMENT(G_Settings,"     >>>>>>SUN AND LENS GLARE<<<<<<")
UI_WHITESPACE(11)

float4 UI_LensGlareCol < string UIGroup = "SunGlare"; string UIName="|- Glare - Lens Color";      string UIWidget="Color";           string Separation = "ExteriorWeather";> = {1,1,1,1};
float  UI_LensGlareInt < string UIGroup = "SunGlare"; string UIName="|- Glare - Lens Intensity";  float UIMin=0.0; float UIMax=10.0; string Separation = "ExteriorWeather";> = {0.5};
float4 UI_SunGlareCol  < string UIGroup = "SunGlare"; string UIName="|- Glare - Sun Color";       string UIWidget="Color";           string Separation = "ExteriorWeather";> = {1,1,1,1};
float  UI_SunGlareInt  < string UIGroup = "SunGlare"; string UIName="|- Glare - Sun Intensity";   float UIMin=0.0; float UIMax=10.0; string Separation = "ExteriorWeather";> = {0.2};


// ─────────────────── Ghost Flares (6 ghosts) ──────────────────────────────
UI_WHITESPACE(12)
UI_ELEMENT(G_Flares,"          >>>>>>GHOST FLARES<<<<<<")
UI_WHITESPACE(13)

float UIG_DiffFreq    < string UIGroup = "GhostFlares"; string UIName="|- Ghost - Diffraction Frequency";  float UIMin=1.0; float UIMax= 1.5; string Separation = "ExteriorWeather";> = {1.0};
float UIG_Int         < string UIGroup = "GhostFlares"; string UIName="|- Ghost - Intensity";              float UIMin=0.0; float UIMax=10.0; string Separation = "ExteriorWeather";> = {1.0};
float UIG_OMoveWeight < string UIGroup = "GhostFlares"; string UIName="|- Ghost - Offset Movement Weight"; float UIMin=0.0; float UIMax=10.0; string Separation = "ExteriorWeather";> = {0.0};
float UIG_OMoveSeed   < string UIGroup = "GhostFlares"; string UIName="|- Ghost - Offset Movement Seed";   float UIMin=0.0; float UIStep=1.0; string Separation = "ExteriorWeather";> = {0.0};
float UIG_OffsetCurve < string UIGroup = "GhostFlares"; string UIName="|- Ghost - Offset Curve";           float UIMin=1.0; float UIMax=10.0; string Separation = "ExteriorWeather";> = {1.0};

#define GHOST_UI(N, DEFOFFSET, DEFSCALE, DEFINT) \
float  UIG_Int##N      < string UIGroup = "GhostFlares"; string UIName="|- Ghost " #N " - Intensity";             float UIMin=0.01; float UIMax=10.0; string Separation = "ExteriorWeather";> = {DEFINT}; \
float  UIG_Offset##N   < string UIGroup = "GhostFlares"; string UIName="|- Ghost " #N " - Offset";                float UIMin=-3.0; float UIMax=3.0;  string Separation = "ExteriorWeather";> = {DEFOFFSET}; \
float  UIG_Scale##N    < string UIGroup = "GhostFlares"; string UIName="|- Ghost " #N " - Scale";                 float UIMin=0.01; float UIMax=20.0; string Separation = "ExteriorWeather";> = {DEFSCALE}; \
float4 UIG_Tint##N     < string UIGroup = "GhostFlares"; string UIName="|- Ghost " #N " - Tint";                  string UIWidget="Color";            string Separation = "ExteriorWeather";> = {1,1,1,1}; \
float  UIG_Feather##N  < string UIGroup = "GhostFlares"; string UIName="|- Ghost " #N " - Feathering";            float UIMin=0.0;  float UIMax=1.0;  string Separation = "ExteriorWeather";> = {0.05}; \
float  UIG_Weight##N   < string UIGroup = "GhostFlares"; string UIName="|- Ghost " #N " - Shape Weight";          float UIMin=0.0;  float UIMax=1.0;  string Separation = "ExteriorWeather";> = {1.0}; \
float  UIG_Vignette##N < string UIGroup = "GhostFlares"; string UIName="|- Ghost " #N " - Vignette";              float UIMin=0.0;  float UIMax=1.0;  string Separation = "ExteriorWeather";> = {0.0}; \
float  UIG_CA##N       < string UIGroup = "GhostFlares"; string UIName="|- Ghost " #N " - Chromatic Aberration";  float UIMin=0.0;  float UIMax=1.0;  string Separation = "ExteriorWeather";> = {0.1}; \
float  UIG_DiffInt##N  < string UIGroup = "GhostFlares"; string UIName="|- Ghost " #N " - Diffraction Intensity"; float UIMin=0.0;  float UIMax=20.0; string Separation = "ExteriorWeather";> = {0.5}; \
float  UIG_DirtInt##N  < string UIGroup = "GhostFlares"; string UIName="|- Ghost " #N " - Dirt Intensity";        float UIMin=0.0;  float UIMax=10.0; string Separation = "ExteriorWeather";> = {0.2};

GHOST_UI(1,  1.0,  1.0, 0.8)
GHOST_UI(2, -0.5,  0.6, 0.6)
GHOST_UI(3,  0.8,  1.5, 0.7)
GHOST_UI(4, -1.2,  0.4, 0.5)
GHOST_UI(5,  1.5,  2.0, 0.4)
GHOST_UI(6, -2.0,  0.8, 0.3)


// ─────────────────── Atmospheric Aureole ───────────────────────────────────
UI_WHITESPACE(18)
UI_ELEMENT(Aureole_Settings,"   >>>>>>ATMOSPHERIC AUREOLE<<<<<<")
UI_WHITESPACE(19)

bool   UIAur_Enable       < string UIGroup = "Aureole"; string UIName="|- Aureole - Enable";                > = {true};
float  UIAur_Int          < string UIGroup = "Aureole"; string UIName="|- Aureole - Intensity";          float UIMin=0.0;  float UIMax=5.0;  string Separation = "ExteriorWeather";> = {0.8};
float  UIAur_InnerScale   < string UIGroup = "Aureole"; string UIName="|- Aureole - Core Scale";         float UIMin=0.01; float UIMax=3.0;  string Separation = "ExteriorWeather";> = {0.35};
float  UIAur_OuterScale   < string UIGroup = "Aureole"; string UIName="|- Aureole - Haze Scale";         float UIMin=0.1;  float UIMax=5.0;  string Separation = "ExteriorWeather";> = {1.5};
float  UIAur_CoreFalloff  < string UIGroup = "Aureole"; string UIName="|- Aureole - Core Falloff";       float UIMin=1.0;  float UIMax=20.0; string Separation = "ExteriorWeather";> = {6.0};
float  UIAur_HazeFalloff  < string UIGroup = "Aureole"; string UIName="|- Aureole - Haze Falloff";       float UIMin=0.1;  float UIMax=10.0; string Separation = "ExteriorWeather";> = {1.8};
float  UIAur_Warmth       < string UIGroup = "Aureole"; string UIName="|- Aureole - Warmth";             float UIMin=0.0;  float UIMax=2.0;  string Separation = "ExteriorWeather";> = {0.5};
float  UIAur_HorizonBoost < string UIGroup = "Aureole"; string UIName="|- Aureole - Horizon Boost";      float UIMin=0.0;  float UIMax=3.0;  string Separation = "ExteriorWeather";> = {1.0};
float  UIAur_Breathe      < string UIGroup = "Aureole"; string UIName="|- Aureole - Breathing Amount";   float UIMin=0.0;  float UIMax=0.5;  string Separation = "ExteriorWeather";> = {0.04};
float4 UIAur_Tint         < string UIGroup = "Aureole"; string UIName="|- Aureole - Tint";               string UIWidget="Color";            string Separation = "ExteriorWeather";> = {1.0, 0.95, 0.85, 0.0};


// ─────────────────── Ciliary Corona ────────────────────────────────────────
UI_SPECIAL_WHITESPACE(22)
UI_ELEMENT(Corona_Settings,"    >>>>>>CILIARY CORONA<<<<<<")
UI_SPECIAL_WHITESPACE(23)

bool   UICorona_Enable    < string UIGroup = "Corona"; string UIName="|- Corona - Enable";           > = {true};
float  UICorona_Int       < string UIGroup = "Corona"; string UIName="|- Corona - Intensity";           float UIMin=0.0;  float UIMax=3.0;  string Separation = "ExteriorWeather";> = {0.4};
float  UICorona_Scale     < string UIGroup = "Corona"; string UIName="|- Corona - Scale";               float UIMin=0.1;  float UIMax=3.0;  string Separation = "ExteriorWeather";> = {0.8};
float  UICorona_RingCount < string UIGroup = "Corona"; string UIName="|- Corona - Ring Count";          float UIMin=1.0;  float UIMax=20.0; float UIStep=0.5; string Separation = "ExteriorWeather";> = {6.0};
float  UICorona_RingSharp < string UIGroup = "Corona"; string UIName="|- Corona - Ring Sharpness";      float UIMin=0.5;  float UIMax=8.0;  string Separation = "ExteriorWeather";> = {2.0};
float  UICorona_Spectral  < string UIGroup = "Corona"; string UIName="|- Corona - Spectral Spread";     float UIMin=0.0;  float UIMax=0.3;  string Separation = "ExteriorWeather";> = {0.08};
float  UICorona_FiberAmt  < string UIGroup = "Corona"; string UIName="|- Corona - Fiber Detail";        float UIMin=0.0;  float UIMax=1.0;  string Separation = "ExteriorWeather";> = {0.45};
float  UICorona_FiberFreq < string UIGroup = "Corona"; string UIName="|- Corona - Fiber Count";         float UIMin=10.0; float UIMax=300.0; float UIStep=1.0; string Separation = "ExteriorWeather";> = {80.0};
float  UICorona_Falloff   < string UIGroup = "Corona"; string UIName="|- Corona - Falloff";             float UIMin=0.5;  float UIMax=10.0; string Separation = "ExteriorWeather";> = {3.0};


// ─────────────────── Moonlight Flares ──────────────────────────────────────
UI_SPECIAL_WHITESPACE(26)
UI_ELEMENT(Moon_Settings,"      >>>>>>MOONLIGHT FLARES<<<<<<")
UI_SPECIAL_WHITESPACE(27)

bool   UIMoon_Enable      < string UIGroup = "MoonFlares"; string UIName="|- Moon - Enable";                   > = {true};
float  UIMoon_Sensitivity < string UIGroup = "MoonFlares"; string UIName="|- Moon - Detection Sensitivity"; float UIMin=0.005;float UIMax=1.0;  string Separation = "ExteriorWeather";> = {0.08};
float  UIMoon_Curve       < string UIGroup = "MoonFlares"; string UIName="|- Moon - Detection Curve";       float UIMin=0.3;  float UIMax=4.0;  string Separation = "ExteriorWeather";> = {1.0};
float  UIMoon_IntMult     < string UIGroup = "MoonFlares"; string UIName="|- Moon - Intensity Multiplier";  float UIMin=0.0;  float UIMax=10.0; string Separation = "ExteriorWeather";> = {3.5};
float  UIMoon_AureoleMult < string UIGroup = "MoonFlares"; string UIName="|- Moon - Aureole Multiplier";    float UIMin=0.0;  float UIMax=5.0;  string Separation = "ExteriorWeather";> = {1.0};
float  UIMoon_CoronaMult  < string UIGroup = "MoonFlares"; string UIName="|- Moon - Corona Multiplier";     float UIMin=0.0;  float UIMax=5.0;  string Separation = "ExteriorWeather";> = {1.8};
float3 UIMoon_Tint        < string UIGroup = "MoonFlares"; string UIName="|- Moon - Color Tint";            string UIWidget="Color";            string Separation = "ExteriorWeather";> = {0.75, 0.82, 1.0};


// ─────────────────── Ice Crystal Halos ─────────────────────────────────────
UI_WHITESPACE(28)
UI_ELEMENT(Halo22_Settings,"    >>>>>>22\xB0 ICE CRYSTAL HALO<<<<<<")
UI_WHITESPACE(29)

bool   UIHalo22_Enable    < string UIGroup = "IceHalo22"; string UIName="|- 22\xB0 Halo - Enable";             > = {false};
float  UIHalo22_Int       < string UIGroup = "IceHalo22"; string UIName="|- 22\xB0 Halo - Intensity";        float UIMin=0.0;  float UIMax=3.0;  string Separation = "ExteriorWeather";> = {0.35};
float  UIHalo22_Thickness < string UIGroup = "IceHalo22"; string UIName="|- 22\xB0 Halo - Thickness";        float UIMin=0.005;float UIMax=0.15; string Separation = "ExteriorWeather";> = {0.04};
float  UIHalo22_InnerEdge < string UIGroup = "IceHalo22"; string UIName="|- 22\xB0 Halo - Inner Sharpness";  float UIMin=1.0;  float UIMax=30.0; string Separation = "ExteriorWeather";> = {12.0};

UI_SPECIAL_WHITESPACE(30)
UI_ELEMENT(Halo46_Settings,"    >>>>>>46\xB0 CIRCUMSCRIBED HALO<<<<<<")
UI_SPECIAL_WHITESPACE(31)

bool   UIHalo46_Enable    < string UIGroup = "IceHalo46"; string UIName="|- 46\xB0 Halo - Enable";             > = {false};
float  UIHalo46_Int       < string UIGroup = "IceHalo46"; string UIName="|- 46\xB0 Halo - Intensity";        float UIMin=0.0;  float UIMax=2.0;  string Separation = "ExteriorWeather";> = {0.12};
float  UIHalo46_Thickness < string UIGroup = "IceHalo46"; string UIName="|- 46\xB0 Halo - Thickness";        float UIMin=0.005;float UIMax=0.2;  string Separation = "ExteriorWeather";> = {0.06};

UI_SPECIAL_WHITESPACE(32)
float  UIHalo_Dispersion  < string UIGroup = "IceHalo46"; string UIName="|- Halos - Spectral Dispersion";    float UIMin=0.0;  float UIMax=0.1;  string Separation = "ExteriorWeather";> = {0.025};
float4 UIHalo_Tint        < string UIGroup = "IceHalo46"; string UIName="|- Halos - Tint";                   string UIWidget="Color";            string Separation = "ExteriorWeather";> = {1,1,1,0};


// ─────────────────── Sun Dogs ──────────────────────────────────────────────
UI_SPECIAL_WHITESPACE(33)
UI_ELEMENT(SunDog_Settings,"       >>>>>>SUN DOGS<<<<<<")
UI_SPECIAL_WHITESPACE(34)

bool   UIDog_Enable       < string UIGroup = "SunDogs"; string UIName="|- Sun Dogs - Enable";                > = {false};
float  UIDog_Int          < string UIGroup = "SunDogs"; string UIName="|- Sun Dogs - Intensity";           float UIMin=0.0;  float UIMax=5.0;  string Separation = "ExteriorWeather";> = {1.0};
float  UIDog_Size         < string UIGroup = "SunDogs"; string UIName="|- Sun Dogs - Size";                float UIMin=0.02; float UIMax=0.5;  string Separation = "ExteriorWeather";> = {0.12};
float  UIDog_Stretch      < string UIGroup = "SunDogs"; string UIName="|- Sun Dogs - Horizontal Stretch";  float UIMin=1.0;  float UIMax=5.0;  string Separation = "ExteriorWeather";> = {2.5};
float  UIDog_Dispersion   < string UIGroup = "SunDogs"; string UIName="|- Sun Dogs - Spectral Spread";     float UIMin=0.0;  float UIMax=0.4;  string Separation = "ExteriorWeather";> = {0.15};
float  UIDog_HorizonBoost < string UIGroup = "SunDogs"; string UIName="|- Sun Dogs - Low-Sun Boost";       float UIMin=0.0;  float UIMax=3.0;  string Separation = "ExteriorWeather";> = {1.5};
float4 UIDog_Tint         < string UIGroup = "SunDogs"; string UIName="|- Sun Dogs - Tint";                string UIWidget="Color";            string Separation = "ExteriorWeather";> = {1,1,1,0};


// ─────────────────── Veiling Glare ─────────────────────────────────────────
UI_WHITESPACE(35)
UI_ELEMENT(Veil_Settings,"      >>>>>>VEILING GLARE<<<<<<")
UI_WHITESPACE(36)

bool   UIVeil_Enable      < string UIGroup = "VeilingGlare"; string UIName="|- Veil - Enable";              > = {true};
float  UIVeil_Int         < string UIGroup = "VeilingGlare"; string UIName="|- Veil - Intensity";              float UIMin=0.0;  float UIMax=1.0;  string Separation = "ExteriorWeather";> = {0.15};
float  UIVeil_Falloff     < string UIGroup = "VeilingGlare"; string UIName="|- Veil - Falloff";               float UIMin=0.3;  float UIMax=5.0;  string Separation = "ExteriorWeather";> = {1.2};
float  UIVeil_SunColor    < string UIGroup = "VeilingGlare"; string UIName="|- Veil - Sun Color Amount";      float UIMin=0.0;  float UIMax=1.0;  string Separation = "ExteriorWeather";> = {0.6};


// ─────────────────── Eyelash Diffraction ───────────────────────────────────
UI_SPECIAL_WHITESPACE(37)
UI_ELEMENT(Lash_Settings,"   >>>>>>EYELASH DIFFRACTION<<<<<<")
UI_SPECIAL_WHITESPACE(38)

bool   UILash_Enable      < string UIGroup = "EyelashDiffraction"; string UIName="|- Eyelash - Enable";              > = {false};
float  UILash_Int         < string UIGroup = "EyelashDiffraction"; string UIName="|- Eyelash - Intensity";           float UIMin=0.0;  float UIMax=3.0;  string Separation = "ExteriorWeather";> = {0.5};
float  UILash_Height      < string UIGroup = "EyelashDiffraction"; string UIName="|- Eyelash - Streak Height";       float UIMin=0.1;  float UIMax=2.0;  string Separation = "ExteriorWeather";> = {0.8};
float  UILash_Width       < string UIGroup = "EyelashDiffraction"; string UIName="|- Eyelash - Streak Width";        float UIMin=0.005;float UIMax=0.15; string Separation = "ExteriorWeather";> = {0.04};
float  UILash_FiberCount  < string UIGroup = "EyelashDiffraction"; string UIName="|- Eyelash - Fiber Count";         float UIMin=3.0;  float UIMax=40.0; float UIStep=1.0; string Separation = "ExteriorWeather";> = {12.0};
float  UILash_FiberRand   < string UIGroup = "EyelashDiffraction"; string UIName="|- Eyelash - Fiber Randomness";    float UIMin=0.0;  float UIMax=1.0;  string Separation = "ExteriorWeather";> = {0.5};
float  UILash_Spectral    < string UIGroup = "EyelashDiffraction"; string UIName="|- Eyelash - Spectral Spread";     float UIMin=0.0;  float UIMax=0.15; string Separation = "ExteriorWeather";> = {0.03};
float4 UILash_Tint        < string UIGroup = "EyelashDiffraction"; string UIName="|- Eyelash - Tint";                string UIWidget="Color";            string Separation = "ExteriorWeather";> = {1,1,1,0};


// ─────────────────── Sensor Bloom Cross ────────────────────────────────────
UI_SPECIAL_WHITESPACE(39)
UI_ELEMENT(Bloom_Settings,"   >>>>>>SENSOR BLOOM CROSS<<<<<<")
UI_SPECIAL_WHITESPACE(40)

bool   UIBloom_Enable     < string UIGroup = "SensorBloom"; string UIName="|- Bloom Cross - Enable";          > = {false};
float  UIBloom_Int        < string UIGroup = "SensorBloom"; string UIName="|- Bloom Cross - Intensity";       float UIMin=0.0;  float UIMax=2.0;  string Separation = "ExteriorWeather";> = {0.3};
float  UIBloom_Length     < string UIGroup = "SensorBloom"; string UIName="|- Bloom Cross - Spike Length";     float UIMin=0.05; float UIMax=2.0;  string Separation = "ExteriorWeather";> = {0.6};
float  UIBloom_Thickness  < string UIGroup = "SensorBloom"; string UIName="|- Bloom Cross - Spike Width";     float UIMin=0.001;float UIMax=0.05; string Separation = "ExteriorWeather";> = {0.008};
float  UIBloom_Falloff    < string UIGroup = "SensorBloom"; string UIName="|- Bloom Cross - Falloff";         float UIMin=0.5;  float UIMax=10.0; string Separation = "ExteriorWeather";> = {3.0};
int    UIBloom_Rotation   < string UIGroup = "SensorBloom"; string UIName="|- Bloom Cross - Rotation(\xB0)";  int   UIMin=0;    int   UIMax=90;   string Separation = "ExteriorWeather";> = {0};
float4 UIBloom_Tint       < string UIGroup = "SensorBloom"; string UIName="|- Bloom Cross - Tint";            string UIWidget="Color";            string Separation = "ExteriorWeather";> = {1,1,1,0};


// ─────────────────── Dirty Lens Coating ────────────────────────────────────
int _spcDL1 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrDIRT < string UIName = "======= DIRTY LENS COATING ======="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool  UIDirt_Enable       < string UIName = "Dirt | Enable"; > = false;
float UIDirt_Intensity    < string UIName = "Dirt | Intensity";       string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0;  float UIStep = 0.01; > = 0.35;
float UIDirt_Density      < string UIName = "Dirt | Particle Density"; string UIWidget = "spinner"; float UIMin = 5.0; float UIMax = 40.0; float UIStep = 1.0;  > = 20.0;
float UIDirt_SmudgeAmt    < string UIName = "Dirt | Smudge Amount";   string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0;  float UIStep = 0.01; > = 0.2;
float UIDirt_ScratchAmt   < string UIName = "Dirt | Scratch Amount";  string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0;  float UIStep = 0.01; > = 0.1;
float UIDirt_ChromaSpread < string UIName = "Dirt | Chromatic Spread"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.3;

// ─────────────────── Thin-Film Coating ─────────────────────────────────────
int _spcTF1 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrTF < string UIName = "======= THIN-FILM COATING ======="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool  UITF_Enable         < string UIName = "TF | Enable"; > = true;
float UITF_Intensity      < string UIName = "TF | Intensity";         string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0;  float UIStep = 0.01; > = 0.2;
float UITF_CoatingThick   < string UIName = "TF | Coating Thickness"; string UIWidget = "spinner"; float UIMin = 50.0; float UIMax = 500.0; float UIStep = 5.0; > = 200.0;
float UITF_IOR            < string UIName = "TF | Coating IOR";       string UIWidget = "spinner"; float UIMin = 1.2; float UIMax = 2.5;  float UIStep = 0.01; > = 1.5;
float3 UITF_BaseTint      < string UIName = "TF | Base Tint"; string UIWidget = "color"; > = {0.4, 1.0, 0.6};

// ─────────────────── Atmospheric Extinction ────────────────────────────────
int _spcAE1 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrAEXT < string UIName = "======= ATMOSPHERIC EXTINCTION ======="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool  UIExt_Enable        < string UIName = "Ext | Enable"; > = true;
float UIExt_Strength      < string UIName = "Ext | Extinction Amount"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 0.5;
float UIExt_WarmShift     < string UIName = "Ext | Warm Color Shift";  string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.4;

// ─────────────────── Weather Dampening ─────────────────────────────────────
int _spcWD1 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrWDMP < string UIName = "======= WEATHER DAMPENING ======="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool  UIWDamp_Enable      < string UIName = "WD | Enable"; > = false;
float UIWDamp_CloudDamp   < string UIName = "WD | Cloud Dampening";  string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.7;
float UIWDamp_RainDamp    < string UIName = "WD | Rain Dampening";   string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.9;

// ─────────────────── Flare Color Grade ─────────────────────────────────────
int _spcFA1 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrFAGE < string UIName = "======= FLARE COLOR GRADE ======="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool  UIFAge_Enable       < string UIName = "FCG | Enable"; > = false;
float UIFAge_Temperature  < string UIName = "FCG | Temperature (-1=cool, 1=warm)"; string UIWidget = "spinner"; float UIMin = -1.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.2;
float UIFAge_Saturation   < string UIName = "FCG | Saturation";      string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 1.0;
float UIFAge_Contrast     < string UIName = "FCG | Contrast";        string UIWidget = "spinner"; float UIMin = 0.5; float UIMax = 2.0; float UIStep = 0.01; > = 1.0;


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
float4 LightParameters;

//--- SkyrimBridge data parameters (must be after Timer for SB_Retain) ---
#include "Helper/SkyrimBridge.fxh"

// ─────────────────── SkyrimBridge Enhancements ─────────────────────────────
#ifdef SKYRIMBRIDGE_FXH

UI_ELEMENT(SB_SS_Header,       "    >>>>>>SKYRIMBRIDGE FLARES<<<<<<")

int SB_SS_Spacer0   <string UIName="|--------- Weather Response"; int UIMin=0; int UIMax=0;> = {0};
float UISBSS_WeatherSuppress  < string UIGroup = "SB.Flares"; string UIName="|- SB - Weather Flare Suppression";     float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UISBSS_RainReduce       < string UIGroup = "SB.Flares"; string UIName="|- SB - Rain Flare Reduction";           float UIMin= 0.0; float UIMax= 1.0; > = { 0.8};
float UISBSS_CloudReduce      < string UIGroup = "SB.Flares"; string UIName="|- SB - Overcast Flare Reduction";       float UIMin= 0.0; float UIMax= 0.8; > = { 0.4};

int SB_SS_Spacer1   <string UIName="|--------- Sun Color"; int UIMin=0; int UIMax=0;> = {0};
float UISBSS_SunColorBlend    < string UIGroup = "SB.Flares"; string UIName="|- SB - Sun Color Blend Amount";         float UIMin= 0.0; float UIMax= 1.0; > = { 0.6};

int SB_SS_Spacer2   <string UIName="|--------- Atmosphere"; int UIMin=0; int UIMax=0;> = {0};
float UISBSS_LightningFlash   < string UIGroup = "SB.Flares"; string UIName="|- SB - Lightning Flash Strength";       float UIMin= 0.0; float UIMax= 1.0; > = { 0.3};
float UISBSS_FogExtinction    < string UIGroup = "SB.Flares"; string UIName="|- SB - Fog Flare Extinction";           float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};

int SB_SS_Spacer3   <string UIName="|--------- Moon & Special"; int UIMin=0; int UIMax=0;> = {0};
float UISBSS_MoonFlares       < string UIGroup = "SB.Flares"; string UIName="|- SB - Moon Flare Enable";              float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};

#endif //SKYRIMBRIDGE_FXH


float4 tempF1;
float4 tempF2;
float4 tempF3;
float4 tempInfo1;
float4 tempInfo2;


//=============================================================================
//  TEXTURES & SAMPLERS
//=============================================================================

Texture2D TextureMask;
Texture2D TextureColor;
Texture2D TextureDepth;
Texture2D LensDirtAtlas <string ResourceName="Textures/LensDirtTexAtlas.png";>;

SamplerState Point_Sampler
{
	Filter = MIN_MAG_MIP_POINT;
	AddressU = Clamp;
	AddressV = Clamp;
};

SamplerState Linear_Sampler
{
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = Clamp;
	AddressV = Clamp;
};

#if ENABLE_DETECTOR_DEPTH_TESTING
SamplerComparisonState Linear_Sampler_Eql
{
	Filter = COMPARISON_MIN_MAG_MIP_LINEAR;
	AddressU = Clamp;
	AddressV = Clamp;
	ComparisonFunc = EQUAL;
};
#endif

#include "Helper/enbHelper_Common.fxh"
// PixelSize and ScreenRes provided by enbHelper_Common.fxh


//=============================================================================
//  STRUCTS
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

struct VertexShaderOutputGSprite
{
   float4 pos                  : SV_POSITION;
   float4 texcoord             : TEXCOORD0;
NI float  SunInt               : SPRITE0;
NI float  RoundingRadius       : SPRITE1;
NI float2 CAOffsetVec          : SPRITE2;
NI float3 FlareTint            : SPRITE3;
NI float  Scale                : SPRITE4;
NI float2 ApertureVertices[10] : SPRITE5;
};

struct VertexShaderOutputSBSprite
{
   float4 pos                  : SV_POSITION;
   float4 texcoord             : TEXCOORD0;
NI float  SunInt               : SPRITE0;
NI float3 FlareTint            : SPRITE1;
NI bool   OddShape             : SPRITE2;
NI float  SafeZone             : SPRITE3;
NI float2 ApertureVertices[10] : SPRITE4;
};

struct VertexShaderOutputHoopSprite
{
   float4 pos                  : SV_POSITION;
   float4 texcoord             : TEXCOORD0;
NI float  SunInt               : SPRITE0;
NI float  RoundingRadius       : SPRITE1;
NI float2 CAOffsetVec          : SPRITE2;
NI float3 FlareTint            : SPRITE3;
NI float  MaxPossibleVertDist  : SPRITE4;
NI float2 ApertureVertices[10] : SPRITE5;
};

struct VertexShaderOutputLensGlareSprite
{
   float4 pos      : SV_POSITION;
   float4 texcoord : TEXCOORD0;
NI float4 Glare    : SPRITE0;
NI float  SunDist  : SPRITE1;
NI float  MinRatio : SPRITE2;
NI float  MaxRatio : SPRITE3;
};

struct VertexShaderOutputSunGlareSprite
{
   float4 pos      : SV_POSITION;
   float4 texcoord : TEXCOORD0;
NI float4 Glare    : SPRITE0;
};

struct VertexShaderOutputAureole
{
   float4 pos      : SV_POSITION;
   float4 texcoord : TEXCOORD0;
NI float  SunInt   : AUR0;
NI float3 SunCol   : AUR1;
NI float  GoldenHr : AUR2;
NI float  Breathe  : AUR3;
};

struct VertexShaderOutputCorona
{
   float4 pos      : SV_POSITION;
   float4 texcoord : TEXCOORD0;
NI float  SunInt   : COR0;
NI float3 SunCol   : COR1;
};

struct VertexShaderOutputHalo
{
   float4 pos       : SV_POSITION;
   float4 texcoord  : TEXCOORD0;
NI float  SunInt    : HALO0;
NI float3 SunCol    : HALO1;
NI float  AngRadius : HALO2;
};

struct VertexShaderOutputSunDog
{
   float4 pos      : SV_POSITION;
   float4 texcoord : TEXCOORD0;
NI float  SunInt   : DOG0;
NI float3 SunCol   : DOG1;
};

struct VertexShaderOutputVeil
{
   float4 pos      : SV_POSITION;
   float2 texcoord : TEXCOORD0;
NI float  SunInt   : VEIL0;
NI float3 SunCol   : VEIL1;
NI float2 SunUV    : VEIL2;
};

struct VertexShaderOutputEyelash
{
   float4 pos      : SV_POSITION;
   float4 texcoord : TEXCOORD0;
NI float  SunInt   : LASH0;
NI float3 SunCol   : LASH1;
};

struct VertexShaderOutputBloomCross
{
   float4 pos      : SV_POSITION;
   float4 texcoord : TEXCOORD0;
NI float  SunInt   : XBLM0;
NI float3 SunCol   : XBLM1;
};

struct DirtVSOutput
{
	float4 pos      : SV_POSITION;
	float2 texcoord : TEXCOORD0;
NI float  SunInt   : DIRT0;
NI float3 SunCol   : DIRT1;
NI float2 SunUV    : DIRT2;
};

struct ThinFilmVSOutput
{
	float4 pos      : SV_POSITION;
	float4 texcoord : TEXCOORD0;
NI float  SunInt   : TF0;
NI float3 SunCol   : TF1;
NI float2 SunUV    : TF2;
};



//=============================================================================
//  FUNCTIONS
//=============================================================================

#define SUN_INTENSITY_CLIP 0.01


float3 DiffractionF3(float3 x, float freq, float phase, float ampli)
{
	float3 sinc = PI * (x * freq + phase) + DELTA;
	       sinc = sin(sinc) / sinc;
	return sinc * sinc * ampli;
}

float3 AtmosphericExtinction(float3 color, float sunHeight)
{
	float airMass = 1.0 / max(sunHeight, 0.02);
	airMass = min(airMass, 40.0);

	float3 rayleighTau = float3(0.004, 0.011, 0.030) * UIExt_Strength;
	float3 transmittance = exp(-rayleighTau * airMass);
	float3 warmShift = lerp(1.0, float3(1.2, 0.7, 0.3), UIExt_WarmShift * (1.0 - sunHeight));

	return color * transmittance * warmShift;
}

float3 FlareColorGrade(float3 color)
{
	float temp = UIFAge_Temperature;
	color.r *= 1.0 + temp * 0.15;
	color.b *= 1.0 - temp * 0.15;
	color.g *= 1.0 + abs(temp) * 0.03;

	float luma = dot(color, N_LUM);
	color = lerp(luma, color, UIFAge_Saturation);
	color = pow(max(color, 0.0), UIFAge_Contrast);

	return max(color, 0.0);
}

float3 SpectralColor(float t)
{
	t = saturate(t);
	float3 c;
	c.r = smoothstep(0.38, 0.72, t) + smoothstep(0.90, 1.0, t) * 0.15;
	c.g = smoothstep(0.08, 0.38, t) * (1.0 - smoothstep(0.58, 0.88, t));
	c.b = 1.0 - smoothstep(0.05, 0.42, t);
	return c * c;
}

float2 DirtHash22(float2 p)
{
	float3 p3 = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
	p3 += dot(p3, p3.yzx + 33.33);
	return frac((p3.xx + p3.yz) * p3.zy);
}

float DirtHash11(float p)
{
	p = frac(p * 0.1031);
	p *= p + 33.33;
	p *= p + p;
	return frac(p);
}

float3 ThinFilmReflectance(float cosTheta, float thickness, float coatingIOR)
{
	float sinTheta2 = 1.0 - cosTheta * cosTheta;
	float cosThetaCoating = sqrt(max(1.0 - sinTheta2 / (coatingIOR * coatingIOR), 0.0));

	float3 wavelengths = float3(630.0, 532.0, 465.0);
	float3 phaseDiff = 4.0 * PI * coatingIOR * thickness * cosThetaCoating / wavelengths;

	float3 reflectance = sin(phaseDiff * 0.5);
	reflectance *= reflectance;

	float F0 = (coatingIOR - 1.0) / (coatingIOR + 1.0);
	F0 *= F0;
	float fresnel = F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);

	return reflectance * fresnel;
}


//=============================================================================
//  SUN INTENSITY DETECTION
//=============================================================================

static float GetSunIntensity(float FlareInt, out float3 SunCol)
{
	#if USE_CUSTOM_SUNINTENSITY_DETECTOR
		static const float2 offset[5] = { 0.0,0.0, 0.0,-1.5, -1.5,0.0, 1.5,0.0, 0.0,1.5 };

		float2 SunUV  = LightParameters.xy * float2(0.5, -0.5) + 0.5;
		float3 AvgCol = 0.0;

		[unroll] for(int i=0; i<5; i++)
		{
			float2 CurrCoords = SunUV + offset[i] * PixelSize;
			float3 CurrSample = TextureColor.SampleLevel(Linear_Sampler, CurrCoords,0).rgb;

			#if ENABLE_DETECTOR_DEPTH_TESTING
				CurrSample *= TextureDepth.SampleCmpLevelZero(Linear_Sampler_Eql, CurrCoords, 1.0).x;
			#endif

			AvgCol += CurrSample;
		}

		AvgCol *= 0.2;
		SunCol  = AvgCol / (AvgCol + 1.0);

		float RawLum  = dot(AvgCol, N_LUM);
		float MaskVis = saturate(TextureMask.Load(0).x * LightParameters.w);

		//--- DAY (sun) ---
		float DayInt  = pow(saturate(RawLum * MaskVis / 2.5), 4.0);

		//--- NIGHT (moon) ---
		float2 EdgeDist = min(SunUV, 1.0 - SunUV);
		float  OnScreen = saturate(min(EdgeDist.x, EdgeDist.y) * 20.0);

		float DepthVis = 0.0;
		[unroll] for(int d=0; d<5; d++)
		{
			float2 DepthCoords = SunUV + offset[d] * PixelSize;
			DepthVis += TextureDepth.SampleLevel(Point_Sampler, DepthCoords, 0).x;
		}
		DepthVis *= 0.2;
		DepthVis = smoothstep(0.9970, 0.9999, DepthVis);

		float NightFactor = pow(1.0 - ENightDayFactor, 1.0 + UIMoon_Curve);
		float LumBoost = saturate(RawLum / UIMoon_Sensitivity);
		LumBoost = lerp(0.5, 1.0, LumBoost);

		float NightInt = NightFactor * LumBoost * UIMoon_IntMult * UIMoon_Enable;
		NightInt *= OnScreen * DepthVis;

		float3 MoonTint = UIMoon_Tint / max(dot(UIMoon_Tint, N_LUM), DELTA);
		float  ColLum   = dot(SunCol, N_LUM);
		float3 MoonCol  = (ColLum > 0.01) ? SunCol * MoonTint : MoonTint;

		//--- BLEND day/night ---
		float Intensity = lerp(NightInt, DayInt, ENightDayFactor);
		SunCol = lerp(MoonCol, SunCol, ENightDayFactor);

		//--- SKYRIMBRIDGE: game-engine sun color ---
		#ifdef SKYRIMBRIDGE_FXH
		[branch] if (SB_IsActive())
		{
			float3 GameSunCol = SB_Sun_Color.rgb;
			float GameLum = dot(GameSunCol, N_LUM);
			[branch] if (GameLum > 0.01)
			{
				GameSunCol /= GameLum;
				SunCol = lerp(SunCol, SunCol * GameSunCol, UISBSS_SunColorBlend);
			}

			//Lightning flash boost
			[branch] if (SB_Lightning.y > 0.5)
			{
				Intensity = max(Intensity, SB_Lightning.z * UISBSS_LightningFlash);
				SunCol = lerp(SunCol, float3(0.85, 0.9, 1.0), SB_Lightning.z * 0.5);
			}

			//Fog-based flare extinction
			[branch] if (UISBSS_FogExtinction > 0.0)
			{
				float FogDensity = saturate(SB_Fog_Density.y);
				Intensity *= lerp(1.0, 1.0 - FogDensity, UISBSS_FogExtinction);
			}

			//Interior suppression
			[branch] if (SB_Interior_Flags.x > 0.5)
			{
				Intensity = 0.0;
			}
		}
		#endif

		//--- ATMOSPHERIC EXTINCTION ---
		[branch] if(UIExt_Enable)
		{
			float SunHeight = saturate(ENightDayFactor * 0.8 + 0.1);
			float LowSunFactor = saturate(
				TimeOfDay1.x * 0.9 + TimeOfDay1.y * 0.4 +
				TimeOfDay1.w * 0.5 + TimeOfDay2.x * 0.9
			);
			SunHeight = lerp(SunHeight, 0.05, LowSunFactor);
			SunCol = AtmosphericExtinction(SunCol, SunHeight);
		}

		//--- WEATHER DAMPENING ---
		[branch] if(UIWDamp_Enable)
		{
			float2 skyUV = LightParameters.xy * float2(0.5, -0.5) + 0.5;
			float3 skySample = TextureColor.SampleLevel(Linear_Sampler, skyUV, 0).rgb;
			float skyLuma = dot(skySample, N_LUM);
			float weatherDamp = 1.0 - UIWDamp_CloudDamp * saturate(1.0 - skyLuma * 3.0);
			weatherDamp = lerp(1.0, weatherDamp, ENightDayFactor);
			Intensity *= weatherDamp;
		}

		//--- CINEMATIC FLARE COLOR GRADE ---
		[branch] if(UIFAge_Enable)
		{
			SunCol = FlareColorGrade(SunCol);
		}

		return Intensity * FlareInt;
	#else
		SunCol = 1.0;
		return saturate(TextureMask.Load(0) * LightParameters.w) * FlareInt;
	#endif
}


//=============================================================================
//  ATMOSPHERIC AUREOLE — Mie forward-scattering haze
//=============================================================================

VertexShaderOutputAureole VS_Aureole(VertexShaderInput IN)
{
	VertexShaderOutputAureole OUT;
	OUT.pos.zw      = float2(IN.pos.z, 1.0);
	OUT.texcoord.xy = IN.txcoord.xy;
	OUT.texcoord.zw = IN.txcoord.xy * 2.0 - 1.0;

	float3 SunCol;
	OUT.SunInt = GetSunIntensity(UIAur_Int * UIAur_Enable, SunCol);
	OUT.SunCol = SunCol;
	OUT.SunInt *= lerp(UIMoon_AureoleMult, 1.0, ENightDayFactor);

	float Scale = max(UIAur_InnerScale, UIAur_OuterScale) * 1.6;
	OUT.pos.xy  = IN.pos.xy * float2(Scale, Scale * ScreenSize.z);
	OUT.pos.xy += LightParameters.xy;

	OUT.GoldenHr = saturate(
		TimeOfDay1.x * 1.0 + TimeOfDay1.y * 0.6 +
		TimeOfDay1.w * 0.8 + TimeOfDay2.x * 1.0
	);

	float Time  = Timer.x * 16777.216;
	OUT.Breathe = sin(Time * 0.07) * 0.5
	            + sin(Time * 0.13) * 0.3
	            + sin(Time * 0.23) * 0.2;
	OUT.Breathe *= UIAur_Breathe;

	return OUT;
}

float4 PS_Aureole(VertexShaderOutputAureole IN) : SV_Target
{
	clip(IN.SunInt - SUN_INTENSITY_CLIP);

	float Dist = length(IN.texcoord.zw);
	clip(1.0 - Dist);

	float HBoost    = 1.0 + IN.GoldenHr * UIAur_HorizonBoost;
	float BreatheMod = 1.0 + IN.Breathe;

	float CoreDist = Dist / max(UIAur_InnerScale * BreatheMod, DELTA);
	float CoreGlow = exp(-CoreDist * UIAur_CoreFalloff) * 1.5;

	float HazeDist = Dist / max(UIAur_OuterScale * HBoost * BreatheMod, DELTA);
	float HazeGlow = exp(-HazeDist * UIAur_HazeFalloff) * 0.5;

	float Angle = atan2(IN.texcoord.w, IN.texcoord.z);
	float Scintillation = Random(floor(Angle * 40.0 + Timer.x * 16777.216 * 0.3));
	Scintillation = Scintillation * 0.15 + 0.85;
	HazeGlow *= Scintillation;

	float3 WarmShift = float3(1.0, 0.85, 0.6);
	WarmShift = lerp(1.0, WarmShift, UIAur_Warmth * (1.0 + IN.GoldenHr * 0.5));

	float3 CoreColor = IN.SunCol;
	float3 HazeColor = IN.SunCol * WarmShift;
	CoreColor = lerp(CoreColor, ColorToChroma(UIAur_Tint.rgb), UIAur_Tint.a);
	HazeColor = lerp(HazeColor, ColorToChroma(UIAur_Tint.rgb) * WarmShift, UIAur_Tint.a);

	float3 Result = CoreGlow * CoreColor + HazeGlow * HazeColor;
	Result *= IN.SunInt * HBoost;

	return float4(Result, 1.0);
}


//=============================================================================
//  CILIARY CORONA — Crystalline lens diffraction
//=============================================================================

VertexShaderOutputCorona VS_Corona(VertexShaderInput IN)
{
	VertexShaderOutputCorona OUT;
	OUT.pos.zw      = float2(IN.pos.z, 1.0);
	OUT.texcoord.xy = IN.txcoord.xy;
	OUT.texcoord.zw = IN.txcoord.xy * 2.0 - 1.0;

	OUT.SunInt = GetSunIntensity(UICorona_Int * UICorona_Enable, OUT.SunCol);
	OUT.SunInt *= lerp(UIMoon_CoronaMult, 1.0, ENightDayFactor);

	float Scale = UICorona_Scale * 1.3;
	OUT.pos.xy  = IN.pos.xy * float2(Scale, Scale * ScreenSize.z);
	OUT.pos.xy += LightParameters.xy;

	return OUT;
}

float4 PS_Corona(VertexShaderOutputCorona IN) : SV_Target
{
	clip(IN.SunInt - SUN_INTENSITY_CLIP);

	float Dist  = length(IN.texcoord.zw);
	float Angle = atan2(IN.texcoord.w, IN.texcoord.z);
	clip(1.0 - Dist);

	float NormDist = Dist / max(UICorona_Scale, DELTA);

	float FiberAngle = Angle * INV_PI * 0.5 + 0.5;
	float FiberPhase = floor(FiberAngle * UICorona_FiberFreq);
	float FiberRand  = Random(FiberPhase);
	float FiberMask  = lerp(1.0, smoothstep(0.3, 0.7, FiberRand), UICorona_FiberAmt);

	float BaseFreq  = UICorona_RingCount * PI;
	float Spread    = UICorona_Spectral;
	float Sharpness = UICorona_RingSharp;

	float3 Rings;
	Rings.r = pow(abs(sin(NormDist * BaseFreq * (1.0 - Spread))), Sharpness);
	Rings.g = pow(abs(sin(NormDist * BaseFreq)),                   Sharpness);
	Rings.b = pow(abs(sin(NormDist * BaseFreq * (1.0 + Spread))), Sharpness);

	float  SpectralT    = saturate(NormDist * 1.2);
	float3 PureSpectral = SpectralColor(SpectralT) * 0.3;
	Rings = Rings * (0.7 + PureSpectral);

	float Falloff = exp(-NormDist * UICorona_Falloff);
	float InnerFade = smoothstep(0.05, 0.2, NormDist);

	float3 Color = Rings * FiberMask * Falloff * InnerFade;
	Color *= IN.SunCol * IN.SunInt;

	return float4(Color, 1.0);
}


//=============================================================================
//  22/46 DEGREE ICE CRYSTAL HALO
//=============================================================================

VertexShaderOutputHalo VS_Halo(VertexShaderInput IN,
	uniform float HaloAngle, uniform float HaloInt, uniform bool HaloEnable)
{
	VertexShaderOutputHalo OUT;
	OUT.pos.zw      = float2(IN.pos.z, 1.0);
	OUT.texcoord.xy = IN.txcoord.xy;
	OUT.texcoord.zw = IN.txcoord.xy * 2.0 - 1.0;

	OUT.SunInt = GetSunIntensity(HaloInt * HaloEnable, OUT.SunCol);

	float HalfFovTan = tan(radians(FieldOfView * 0.5));
	float AngRadClip = tan(radians(HaloAngle)) / HalfFovTan;

	float QuadScale  = AngRadClip * 1.5;
	OUT.pos.xy = IN.pos.xy * float2(QuadScale, QuadScale * ScreenSize.z);
	OUT.pos.xy += LightParameters.xy;

	OUT.AngRadius = AngRadClip / QuadScale;

	return OUT;
}

float4 PS_Halo(VertexShaderOutputHalo IN,
	uniform float Thickness, uniform float InnerEdge) : SV_Target
{
	clip(IN.SunInt - SUN_INTENSITY_CLIP);

	float Dist = length(IN.texcoord.zw);
	clip(1.0 - Dist);

	float Spread = UIHalo_Dispersion;
	float3 RingCenter = IN.AngRadius * float3(1.0 - Spread, 1.0, 1.0 + Spread);
	float3 SignedDist = Dist - RingCenter;

	float3 InnerFade = smoothstep(-0.003, 0.0, SignedDist);
	InnerFade = pow(InnerFade, InnerEdge);
	float3 OuterFade = exp(-max(SignedDist, 0.0) / max(Thickness, DELTA));

	float3 Ring = InnerFade * OuterFade;

	float3 Col = IN.SunCol;
	Col = lerp(Col, ColorToChroma(UIHalo_Tint.rgb), UIHalo_Tint.a);

	return float4(Ring * Col * IN.SunInt, 1.0);
}


//=============================================================================
//  SUN DOGS — Parhelia
//=============================================================================

VertexShaderOutputSunDog VS_SunDog(VertexShaderInput IN, uniform bool IsLeft)
{
	VertexShaderOutputSunDog OUT;
	OUT.pos.zw      = float2(IN.pos.z, 1.0);
	OUT.texcoord.xy = IN.txcoord.xy;
	OUT.texcoord.zw = IN.txcoord.xy * 2.0 - 1.0;

	OUT.SunInt = GetSunIntensity(UIDog_Int * UIDog_Enable, OUT.SunCol);

	float GoldenFactor = saturate(
		TimeOfDay1.x + TimeOfDay1.y * 0.5 + TimeOfDay1.w * 0.7 + TimeOfDay2.x
	);
	OUT.SunInt *= 1.0 + GoldenFactor * UIDog_HorizonBoost;

	float HalfFovTan = tan(radians(FieldOfView * 0.5));
	float AngOffset  = tan(radians(22.0)) / (HalfFovTan * ScreenSize.z);
	float2 DogCenter = LightParameters.xy;
	DogCenter.x += AngOffset * (IsLeft ? -1.0 : 1.0);

	float2 DogScale = UIDog_Size * float2(UIDog_Stretch, ScreenSize.z);
	OUT.pos.xy = IN.pos.xy * DogScale + DogCenter;

	return OUT;
}

float4 PS_SunDog(VertexShaderOutputSunDog IN, uniform bool IsLeft) : SV_Target
{
	clip(IN.SunInt - SUN_INTENSITY_CLIP);

	float Dist = length(IN.texcoord.zw);
	clip(1.0 - Dist);

	float SpectralPos = IN.texcoord.z * (IsLeft ? 1.0 : -1.0);
	SpectralPos = SpectralPos * UIDog_Dispersion * 3.0 + 0.5;

	float3 DogSpectral;
	DogSpectral.r = smoothstep(0.38, 0.72, SpectralPos);
	DogSpectral.g = smoothstep(0.08, 0.38, SpectralPos) * (1.0 - smoothstep(0.58, 0.88, SpectralPos));
	DogSpectral.b = 1.0 - smoothstep(0.05, 0.42, SpectralPos);
	DogSpectral = DogSpectral * DogSpectral;
	DogSpectral = DogSpectral / max(dot(DogSpectral, N_LUM), DELTA);
	DogSpectral = lerp(1.0, DogSpectral, saturate(UIDog_Dispersion * 5.0));

	float Falloff = exp(-Dist * 3.0);
	Falloff *= smoothstep(1.0, 0.5, Dist);

	float3 Col = IN.SunCol;
	Col = lerp(Col, ColorToChroma(UIDog_Tint.rgb), UIDog_Tint.a);

	return float4(Falloff * DogSpectral * Col * IN.SunInt, 1.0);
}


//=============================================================================
//  VEILING GLARE — Intraocular scatter
//=============================================================================

VertexShaderOutputVeil VS_VeilingGlare(VertexShaderInput IN)
{
	VertexShaderOutputVeil OUT;
	OUT.pos      = float4(IN.pos.xyz, 1.0);
	OUT.texcoord = IN.txcoord.xy;

	OUT.SunInt = GetSunIntensity(UIVeil_Int * UIVeil_Enable, OUT.SunCol);
	OUT.SunUV  = LightParameters.xy * float2(0.5, -0.5) + 0.5;

	return OUT;
}

float4 PS_VeilingGlare(VertexShaderOutputVeil IN) : SV_Target
{
	clip(IN.SunInt - SUN_INTENSITY_CLIP);

	float2 Delta = IN.texcoord - IN.SunUV;
	Delta.x *= ScreenSize.z;
	float Dist = length(Delta);

	float Veil = exp(-Dist * Dist * UIVeil_Falloff * UIVeil_Falloff);
	float3 Col = lerp(1.0, IN.SunCol, UIVeil_SunColor);

	return float4(Veil * Col * IN.SunInt, 1.0);
}


//=============================================================================
//  EYELASH DIFFRACTION — Vertical streaks from lash fiber grating
//=============================================================================

VertexShaderOutputEyelash VS_Eyelash(VertexShaderInput IN)
{
	VertexShaderOutputEyelash OUT;
	OUT.pos.zw      = float2(IN.pos.z, 1.0);
	OUT.texcoord.xy = IN.txcoord.xy;
	OUT.texcoord.zw = IN.txcoord.xy * 2.0 - 1.0;

	OUT.SunInt = GetSunIntensity(UILash_Int * UILash_Enable, OUT.SunCol);

	float2 LashScale = float2(UILash_Width, UILash_Height * ScreenSize.z);
	OUT.pos.xy = IN.pos.xy * LashScale + LightParameters.xy;

	return OUT;
}

float4 PS_Eyelash(VertexShaderOutputEyelash IN) : SV_Target
{
	clip(IN.SunInt - SUN_INTENSITY_CLIP);

	float VertDist  = abs(IN.texcoord.w);
	float HorizDist = abs(IN.texcoord.z);

	float3 VertFalloff;
	VertFalloff.r = exp(-VertDist * (2.5 - UILash_Spectral * 8.0));
	VertFalloff.g = exp(-VertDist * 2.5);
	VertFalloff.b = exp(-VertDist * (2.5 + UILash_Spectral * 8.0));

	float HorizFalloff = exp(-HorizDist * HorizDist * 50.0);

	float FiberPhase = IN.texcoord.w * UILash_FiberCount * PI;
	float FiberPattern = pow(abs(cos(FiberPhase)), 2.0);

	float FiberIdx  = floor(IN.texcoord.w * UILash_FiberCount * 0.5 + 0.5);
	float FiberRand = Random(FiberIdx) * UILash_FiberRand + (1.0 - UILash_FiberRand);
	FiberPattern *= FiberRand;

	float CoreBlend = smoothstep(0.0, 0.15, VertDist);
	FiberPattern = lerp(1.0, FiberPattern, CoreBlend);

	float3 Col = IN.SunCol;
	Col = lerp(Col, ColorToChroma(UILash_Tint.rgb), UILash_Tint.a);

	float3 Color = VertFalloff * HorizFalloff * FiberPattern * Col * IN.SunInt;
	return float4(Color, 1.0);
}


//=============================================================================
//  SENSOR BLOOM CROSS — CCD/CMOS diffraction spikes
//=============================================================================

VertexShaderOutputBloomCross VS_BloomCross(VertexShaderInput IN)
{
	VertexShaderOutputBloomCross OUT;
	OUT.pos.zw      = float2(IN.pos.z, 1.0);
	OUT.texcoord.xy = IN.txcoord.xy;
	OUT.texcoord.zw = IN.txcoord.xy * 2.0 - 1.0;

	OUT.SunInt = GetSunIntensity(UIBloom_Int * UIBloom_Enable, OUT.SunCol);

	float Scale = UIBloom_Length * 1.2;
	OUT.pos.xy = IN.pos.xy * float2(Scale, Scale * ScreenSize.z);
	OUT.pos.xy += LightParameters.xy;

	return OUT;
}

float4 PS_BloomCross(VertexShaderOutputBloomCross IN) : SV_Target
{
	clip(IN.SunInt - SUN_INTENSITY_CLIP);

	float2 Local = MatrixRotate(IN.texcoord.zw, UIBloom_Rotation, false);

	float DistH = abs(Local.y);
	float DistV = abs(Local.x);

	float HSpike = exp(-DistH / max(UIBloom_Thickness, DELTA))
	             * exp(-abs(Local.x) * UIBloom_Falloff);
	float VSpike = exp(-DistV / max(UIBloom_Thickness, DELTA))
	             * exp(-abs(Local.y) * UIBloom_Falloff);

	float Cross = max(HSpike, VSpike);

	float CoreDist = length(IN.texcoord.zw);
	Cross += exp(-CoreDist * 15.0) * 0.3;

	float3 Col = IN.SunCol;
	Col = lerp(Col, ColorToChroma(UIBloom_Tint.rgb), UIBloom_Tint.a);

	return float4(Cross * Col * IN.SunInt, 1.0);
}



//=============================================================================
//  GHOST FLARES — Aperture-shaped sprites (6 ghosts)
//=============================================================================

VertexShaderOutputGSprite VS_GhostSprite(VertexShaderInput IN,
	uniform float  FlareOffset, uniform float FlareScale,
	uniform float  FlareInt,    uniform float CAScale,
	uniform float4 FlareTint)
{
	VertexShaderOutputGSprite OUT;
	OUT.pos          = float4(IN.pos.xyz, 1.0);
	OUT.texcoord     = IN.txcoord.xyxy;
	OUT.texcoord.zw *= 2.0 * CAScale + 2.0;
	OUT.texcoord.zw -= CAScale + 1.0;

	OUT.SunInt = GetSunIntensity(FlareInt * UIG_Int * UI_EnableGhost, OUT.FlareTint);

	[branch] if(OUT.SunInt > SUN_INTENSITY_CLIP)
	{
		float  ParamLength = length(LightParameters.xy);
		float2 LightParams = LightParameters.xy / ParamLength * nRoot(ParamLength, UIG_OffsetCurve);

		float2   Offset = LightParams * -FlareOffset * ScreenSize.z * 0.75;
		float2x2 RotMat = GetDirVec(UI_ApertureRotation).xyyx * float4(1.0, -1.0, 1.0, 1.0);

		OUT.Scale  = FlareScale * 0.1 * (CAScale + 1.0);
		OUT.pos.xy = mul(RotMat, OUT.pos.xy);
		OUT.pos.y *= ScreenSize.z;
		OUT.pos.xy = OUT.pos.xy * OUT.Scale + Offset;

		float OffsetMovement = Random(floor(FlareOffset * 10.0) + UIG_OMoveSeed) * 2.0 - 1.0;
		      OUT.pos.xy    += Offset * OffsetMovement * UIG_OMoveWeight * length(Offset);

		[unroll] for(int i=0; i<9; i++)
		OUT.ApertureVertices[i] = GetDirVec(360.0 / UI_ApertureShape * i);
		OUT.ApertureVertices[9] = OUT.ApertureVertices[0];

		OUT.CAOffsetVec = normalize(LightParams - Offset) * CAScale * 0.3;
		OUT.CAOffsetVec = mul(RotMat, OUT.CAOffsetVec * float2(ScreenSize.z, -1.0));
		OUT.FlareTint   = lerp(OUT.FlareTint, ColorToChroma(FlareTint.rgb), FlareTint.a);

		float RandomDirt = floor(FlareOffset * 10.0);
		      RandomDirt = Random(RandomDirt) * 0.5 + 0.5;
		OUT.texcoord.xy  = AtlasFetch_4(OUT.texcoord.xy * RandomDirt,4);
		OUT.Scale        = max(saturate(OUT.Scale * 10.0), 0.5);
	}

	return OUT;
}

float4 PS_GhostSprite(VertexShaderOutputGSprite IN,
	uniform float FlareFeather, uniform float FlareDiffInt,
	uniform float FlareDirtInt, uniform float FlareWeight,
	uniform float FlareVignette) : SV_Target
{
	float3 CenterDistance = { length(IN.texcoord.zw - IN.CAOffsetVec),
	                          length(IN.texcoord.zw),
	                          length(IN.texcoord.zw + IN.CAOffsetVec) };

	clip(min(1.0 - min3(CenterDistance), IN.SunInt - SUN_INTENSITY_CLIP));

	float2 SSCoords    = IN.pos.xy * PixelSize;
	float3 RadialDist  = saturate(1.0 - CenterDistance);
	float3 MinEdgeDist = RadialDist;

	[branch] if(UI_ApertureRoundness < 1.0)
	{
		[loop]   for(int i=0; i < UI_ApertureShape; i++)
		[unroll] for(int j=-1; j<2;)
		{
			float2 Edge     = float2(-1.0, 1.0) * (IN.ApertureVertices[i+1] - IN.ApertureVertices[i]);
			float  EdgeDist = dot(Edge.yx, IN.ApertureVertices[i]) -
			                  dot(Edge.yx, IN.texcoord.zw + IN.CAOffsetVec * j);

			EdgeDist = lerp(EdgeDist, RadialDist[++j], (-EdgeDist * EdgeDist + 1.0) * UI_ApertureRoundness);
			MinEdgeDist[j] = min(MinEdgeDist[j], EdgeDist);
		}
		clip(max3(MinEdgeDist));
	}

	float3 Color = IN.SunInt * (MinEdgeDist > 0.0) * IN.FlareTint;

	float PhaseShift = min(FlareFeather + DELTA, 0.1) * -18.0;
	float Frequency  = 30.0 * UIG_DiffFreq * IN.Scale;
	Color += DiffractionF3(MinEdgeDist, Frequency, PhaseShift, FlareDiffInt) * Color;

	Color *= smoothstep(0.0, FlareFeather + DELTA, MinEdgeDist);
	Color *= smoothstep(6.0 * pow(FlareWeight,3), 0.0, MinEdgeDist);

	float VigWeight = ScreenSize.z - FlareVignette * ScreenSize.z;
	Color *= LinearStep(VigWeight + 0.1, VigWeight - 0.1, length(SSCoords * 2.0 - 1.0));

	Color += Color * LensDirtAtlas.SampleLevel(Linear_Sampler, IN.texcoord.xy, 0) * FlareDirtInt;
	return float4(Color, 1.0);
}


//=============================================================================
//  HOOP / RING LENS FLARE
//=============================================================================

VertexShaderOutputHoopSprite VS_HoopSprite(VertexShaderInput IN)
{
	VertexShaderOutputHoopSprite OUT;
	OUT.pos          = float4(IN.pos.xyz,1.0);
	OUT.texcoord     = IN.txcoord.xyxy;
	OUT.texcoord.zw *= 2.0 * UIHoop_CA + 2.0;
	OUT.texcoord.zw -= UIHoop_CA + 1.0;

	OUT.SunInt = GetSunIntensity(UIHoop_Int * UI_EnableHoop, OUT.FlareTint);

	[branch] if(OUT.SunInt > SUN_INTENSITY_CLIP)
	{
		float SunDistance = length(LightParameters.xy) * UIHoop_SunDistMod;

		float2   Offset = LightParameters.xy * -UIHoop_Offset * ScreenSize.z * 0.75;
		float2x2 RotMat = GetDirVec(UI_ApertureRotation).xyyx * float4(1.0, -1.0, 1.0, 1.0);

		float2 Scale = UIHoop_Scale * 0.1 * (UIHoop_CA + 1.0) * SunDistance;
		OUT.SunInt  *= saturate(SunDistance);

		OUT.pos.xy = mul(RotMat, OUT.pos.xy);
		OUT.pos.y *= ScreenSize.z;
		OUT.pos.xy = OUT.pos.xy * Scale + Offset;

		[unroll] for(int i=0; i<9; i++)
		OUT.ApertureVertices[i] = GetDirVec(360.0 / UI_ApertureShape * i);
		OUT.ApertureVertices[9] = OUT.ApertureVertices[0];

		float2 HalfVertex = OUT.ApertureVertices[0] + 0.5 * (OUT.ApertureVertices[1] - OUT.ApertureVertices[0]);

		float HalfVertexLength  = length(HalfVertex);
		OUT.RoundingRadius      = lerp(HalfVertexLength, 1.0, 1.0 - UIHoop_Roundness);
		OUT.MaxPossibleVertDist = distance(HalfVertex / HalfVertexLength, OUT.ApertureVertices[0]);

		OUT.CAOffsetVec = (LightParameters.xy - Offset) * UIHoop_CA * 0.3;
		OUT.CAOffsetVec = mul(RotMat, OUT.CAOffsetVec * float2(ScreenSize.z, -1.0));
		OUT.FlareTint   = lerp(OUT.FlareTint, ColorToChroma(UIHoop_Tint.rgb), UIHoop_Tint.a);
	}

	return OUT;
}

float4 PS_HoopSprite(VertexShaderOutputHoopSprite IN) : SV_Target
{
	float3 CenterDistance = { length(IN.texcoord.zw - IN.CAOffsetVec),
	                          length(IN.texcoord.zw),
	                          length(IN.texcoord.zw + IN.CAOffsetVec) };

	clip(min(1.0 - min3(CenterDistance), IN.SunInt - SUN_INTENSITY_CLIP));

	float3 RadialDist  = saturate(1.0 - CenterDistance);
	float3 MinEdgeDist = RadialDist;

	[branch] if(UI_ApertureRoundness < 1.0)
	{
		[loop]   for(int i=0; i < UI_ApertureShape; i++)
		[unroll] for(int j=-1; j<2;)
		{
			float2 Edge     = float2(-1.0, 1.0) * (IN.ApertureVertices[i+1] - IN.ApertureVertices[i]);
			float  EdgeDist = dot(Edge.yx, IN.ApertureVertices[i]) -
			                  dot(Edge.yx, IN.texcoord.zw + IN.CAOffsetVec * j);

			EdgeDist = lerp(EdgeDist, RadialDist[++j], (-EdgeDist * EdgeDist + 1.0) * UI_ApertureRoundness);
			MinEdgeDist[j] = min(MinEdgeDist[j], EdgeDist);
		}

		float3 Interpolator = LinearStep(IN.RoundingRadius, 1.0, CenterDistance);
		       MinEdgeDist  = lerp(MinEdgeDist, pow(RadialDist,2), Interpolator);

		clip(max3(MinEdgeDist));
	}

	float3 Color = IN.SunInt * (MinEdgeDist > 0.0) * IN.FlareTint;

	Color *= smoothstep(0.0, 0.05, MinEdgeDist);
	Color *= smoothstep(6.0 * pow(UIHoop_Width,3), 0.0, MinEdgeDist);
	Color *= pow(saturate(UIHoop_Fade - dot(IN.texcoord.zw, normalize(IN.CAOffsetVec))),4);

	float  MinVertDist = 1.0;
	float2 NormCoords  = normalize(IN.texcoord.zw);

	[loop] for(int i=0; i < UI_ApertureShape; i++)
		MinVertDist = min(MinVertDist, distance(NormCoords, IN.ApertureVertices[i]));

	float VertGradient = LinearStep(0.0, IN.MaxPossibleVertDist, MinVertDist);
	float HoopPattern  = pow(sin(VertGradient * UIHoop_PatternFreq * PI),2);

	#if ENABLE_HOOP_ANTIALIASING
		float PatternInt = 0.0;
		float StepSize   = max2(PixelSize);

		[unroll] for(float i=-2.0; i <= 2.0; i += 2.0)
		{
			float CurrPattern = floor((VertGradient + i * StepSize) * UIHoop_IntRandom);
			      PatternInt += HoopPattern * Random(CurrPattern);
		}

		Color *= PatternInt / 3.0;
	#else
		Color *= HoopPattern * Random(floor(VertGradient * UIHoop_IntRandom));
	#endif

	return float4(Color, 1.0);
}


//=============================================================================
//  STARBURST — Aperture diffraction spikes
//=============================================================================

VertexShaderOutputSBSprite VS_StarburstSprite(VertexShaderInput IN)
{
	VertexShaderOutputSBSprite OUT;
	OUT.pos         = float4(IN.pos.xyz, 1.0);
	OUT.texcoord.xy = IN.txcoord.xy;
	OUT.texcoord.zw = IN.txcoord.xy * 2.0 - 1.0;

	OUT.pos.y *= ScreenSize.z;
	OUT.pos.xy = OUT.pos.xy * UISB_Scale + LightParameters.xy;

	[unroll] for(int i=0; i<9; i++)
	OUT.ApertureVertices[i] = GetDirVec(360.0 / UI_ApertureShape * (i + 0.5) + UI_ApertureRotation);
	OUT.ApertureVertices[9] = OUT.ApertureVertices[0];

	OUT.SunInt    = GetSunIntensity(UISB_FlareInt * UI_EnableStarburst, OUT.FlareTint);
	OUT.FlareTint = lerp(OUT.FlareTint, ColorToChroma(UISB_FlareTint.rgb), UISB_FlareTint.a);
	OUT.OddShape  = UI_ApertureShape & 1;
	OUT.SafeZone  = rcp(0.005 / UISB_Scale);

	return OUT;
}

float4 PS_StarburstSprite(VertexShaderOutputSBSprite IN) : SV_Target
{
	float SunDist = length(IN.texcoord.zw);
	clip(min(1.0 - SunDist, IN.SunInt - SUN_INTENSITY_CLIP));

	float  InvertedRoundness = 1.0 - UI_ApertureRoundness;
	float  MinVertDist       = 1.0;
	float2 NormCoords        = normalize(IN.texcoord.zw);

	[loop] for(int i=0; i < UI_ApertureShape; i++)
		MinVertDist = min(MinVertDist, distance( NormCoords, IN.ApertureVertices[i]));

	[loop] for(;i > 0 && IN.OddShape; i--)
		MinVertDist = min(MinVertDist, distance(-NormCoords, IN.ApertureVertices[i]));

	float BurstWidth  = pow(SunDist, UISB_BurstWidthCurve);
	      BurstWidth *= 250.0 - 250.0 * sqrt(UISB_BurstWidth);

	float StarMask = pow(saturate(1.0 - MinVertDist), BurstWidth * InvertedRoundness);
	      StarMask = lerp(1.0, StarMask, saturate(SunDist * IN.SafeZone));

	#if ENABLE_STARBURST_ANTIALIASING
		static const float2 offsets[4] = { 0.5,0.5, -0.5,0.5, 0.5,-0.5, -0.5,-0.5 };
		float Randomness    = 0.0;
		float RandFrequency = UISB_RandFrequency * 10.0;

		[unroll] for(int i=0; i<4; i++)
			Randomness += Random(floor(normalize(IN.texcoord.zw + offsets[i] * PixelSize) * RandFrequency));
		Randomness *= 0.25;
	#else
		float Randomness = Random(floor(NormCoords * UISB_RandFrequency * 10.0));
	#endif

	float FalloffGradient = saturate(1.0 - SunDist);
	float StrayBursts     = Randomness > 1.0 - DELTA - UISB_StrayBurstAmount;
	      StrayBursts    *= UISB_StrayBurstInt * InvertedRoundness;
	      Randomness      = Randomness * UISB_RandIntensity + 1.0;

	StarMask  = LinearStep(0.4, 0.6, StarMask);
	StarMask *= pow(FalloffGradient, UISB_Falloff * Randomness);
	StarMask += StrayBursts * pow(FalloffGradient, UISB_StrayBurstFalloff * Randomness);
	StarMask *= saturate(0.5 + 1.0 - (Randomness - 1.0));

	//Spectral ray coloring
	float3 SpectralTint = 1.0;
	[branch] if(UISB_SpectralAmt > 0.0)
	{
		float PixelAngle = atan2(NormCoords.y, NormCoords.x);
		float SegAngle   = TWO_PI / UI_ApertureShape;
		float Offset     = radians(UI_ApertureRotation + 180.0 / UI_ApertureShape);
		float LocalAngle = frac((PixelAngle - Offset) / SegAngle + 1.0);

		float SpectralT  = frac(LocalAngle * UISB_SpectralFreq);
		SpectralT = 1.0 - abs(SpectralT * 2.0 - 1.0);

		float3 RaySpectrum;
		RaySpectrum.r = smoothstep(0.38, 0.72, SpectralT);
		RaySpectrum.g = smoothstep(0.08, 0.38, SpectralT) * (1.0 - smoothstep(0.58, 0.88, SpectralT));
		RaySpectrum.b = 1.0 - smoothstep(0.05, 0.42, SpectralT);
		RaySpectrum = RaySpectrum * RaySpectrum;

		RaySpectrum  = RaySpectrum / max(dot(RaySpectrum, N_LUM), DELTA);
		SpectralTint = lerp(1.0, RaySpectrum, UISB_SpectralAmt * SunDist);
	}

	return float4(StarMask * IN.SunInt * IN.FlareTint * SpectralTint, 1.0);
}


//=============================================================================
//  LENS GLARE SPRITE — broad glow opposing sun position
//=============================================================================

VertexShaderOutputLensGlareSprite VS_LensGlareSprite(VertexShaderInput IN)
{
	VertexShaderOutputLensGlareSprite OUT;
	OUT.pos.zw      = float2(IN.pos.z, 1.0);
	OUT.texcoord.xy = IN.txcoord.xy;
	OUT.texcoord.zw = IN.txcoord.xy * 2.0 - 1.0;

	float2 ScreenRatio = { 1.0, ScreenSize.z };

	OUT.pos.xy  = IN.pos.xy * ScreenRatio * ScreenSize.z;
	OUT.pos.xy -= LightParameters.xy;

	OUT.MinRatio = min2(ScreenRatio);
	OUT.MaxRatio = max2(ScreenRatio);

	OUT.Glare.a   = GetSunIntensity(UI_LensGlareInt * UI_EnableGlares, OUT.Glare.rgb);
	OUT.Glare.rgb = lerp(OUT.Glare.rgb, ColorToChroma(UI_LensGlareCol.rgb), UI_LensGlareCol.a);
	OUT.SunDist   = distance(-LightParameters.xy, LightParameters.xy);
	OUT.SunDist   = pow(saturate(1.0 - OUT.SunDist), 2) * UI_LensGlareInt * 2.0;

	return OUT;
}

float4 PS_LensGlareSprite(VertexShaderOutputLensGlareSprite IN) : SV_Target
{
	clip(IN.Glare.a - SUN_INTENSITY_CLIP);
	float GlareShape = 1.0 - length(IN.texcoord.zw);
	      GlareShape = smoothstep(IN.MaxRatio, IN.MinRatio, GlareShape) * IN.SunDist;
	return float4(GlareShape * GlareShape * IN.Glare.rgb * IN.Glare.a, 1.0);
}


//=============================================================================
//  SUN GLARE SPRITE — centered glow at sun position
//=============================================================================

VertexShaderOutputSunGlareSprite VS_SunGlareSprite(VertexShaderInput IN)
{
	VertexShaderOutputSunGlareSprite OUT;
	OUT.pos.zw      = float2(IN.pos.z, 1.0);
	OUT.texcoord.xy = IN.txcoord.xy;
	OUT.texcoord.zw = IN.txcoord.xy * 2.0 - 1.0;

	OUT.pos.xy  = IN.pos.xy * float2(1.0, ScreenSize.z) * 0.8;
	OUT.pos.xy += LightParameters.xy;

	OUT.Glare.a   = GetSunIntensity(UI_SunGlareInt * UI_EnableGlares, OUT.Glare.rgb);
	OUT.Glare.rgb = lerp(OUT.Glare.rgb, ColorToChroma(UI_SunGlareCol.rgb), UI_SunGlareCol.a);

	return OUT;
}

float4 PS_SunGlareSprite(VertexShaderOutputSunGlareSprite IN) : SV_Target
{
	clip(IN.Glare.a - SUN_INTENSITY_CLIP);
	float GlareShape = 1.0 - length(IN.texcoord.zw);
	      GlareShape = smoothstep(0.0, 1.0, GlareShape);
	return float4(GlareShape * IN.Glare.a * IN.Glare.rgb, 1.0);
}


//=============================================================================
//  DIRTY LENS — procedural dust particles, smudges, and micro-scratches
//=============================================================================

DirtVSOutput VS_DirtyLens(VertexShaderInput IN)
{
	DirtVSOutput OUT;
	OUT.pos      = float4(IN.pos.xyz, 1.0);
	OUT.texcoord = IN.txcoord.xy;

	OUT.SunInt = GetSunIntensity(UIDirt_Intensity * UIDirt_Enable, OUT.SunCol);
	OUT.SunUV  = LightParameters.xy * float2(0.5, -0.5) + 0.5;

	return OUT;
}

float4 PS_DirtyLens(DirtVSOutput IN) : SV_Target
{
	clip(IN.SunInt - SUN_INTENSITY_CLIP);

	float2 UV = IN.texcoord;

	//Distance from sun in UV space
	float2 SunDelta = UV - IN.SunUV;
	SunDelta.x *= ScreenSize.z;
	float SunDist = length(SunDelta);
	float SunProximity = exp(-SunDist * 2.0);

	//=== Dust Particles: cell-noise distributed specks ===//
	float2 CellUV = UV * UIDirt_Density;
	float2 CellID = floor(CellUV);
	float2 CellFrac = frac(CellUV);

	float DustMask = 0.0;

	[unroll] for (int dx = -1; dx <= 1; dx++)
	[unroll] for (int dy = -1; dy <= 1; dy++)
	{
		float2 Neighbor = float2(dx, dy);
		float2 Pos    = DirtHash22(CellID + Neighbor);
		float  Radius = DirtHash11(dot(CellID + Neighbor, float2(127.1, 311.7))) * 0.3 + 0.05;

		float Dist     = length(CellFrac - Neighbor - Pos);
		float Particle = smoothstep(Radius, Radius * 0.3, Dist);
		DustMask += Particle;
	}

	//=== Fingerprint Smudges: large-scale noise blobs ===//
	float2 SmudgeUV = UV * 3.5;
	float SmudgeNoise  = DirtHash11(dot(floor(SmudgeUV), float2(269.5, 183.3)));
	      SmudgeNoise *= DirtHash11(dot(floor(SmudgeUV * 1.7), float2(419.2, 371.9)));
	float SmudgeMask   = smoothstep(0.3, 0.8, SmudgeNoise) * UIDirt_SmudgeAmt;

	//=== Micro-Scratches: thin directional lines ===//
	float ScratchPhase = UV.x * 80.0 + UV.y * 20.0;
	float ScratchMask  = pow(abs(sin(ScratchPhase)), 40.0);
	      ScratchMask *= DirtHash11(floor(ScratchPhase * 0.5)) > 0.7;
	      ScratchMask *= UIDirt_ScratchAmt;

	//Combine all dirt layers
	float TotalDirt = saturate(DustMask + SmudgeMask + ScratchMask);

	//Chromatic spread: different channels scatter slightly differently
	float3 DirtColor;
	DirtColor.r = TotalDirt;
	DirtColor.g = saturate(TotalDirt * (1.0 + UIDirt_ChromaSpread * 0.3));
	DirtColor.b = saturate(TotalDirt * (1.0 - UIDirt_ChromaSpread * 0.2));

	DirtColor *= IN.SunCol * IN.SunInt * SunProximity;

	return float4(DirtColor, 1.0);
}


//=============================================================================
//  THIN FILM — Fabry-Pérot lens coating reflections
//=============================================================================

ThinFilmVSOutput VS_ThinFilm(VertexShaderInput IN)
{
	ThinFilmVSOutput OUT;
	OUT.pos         = float4(IN.pos.xyz, 1.0);
	OUT.texcoord.xy = IN.txcoord.xy;
	OUT.texcoord.zw = IN.txcoord.xy * 2.0 - 1.0;

	OUT.SunInt = GetSunIntensity(UITF_Intensity * UITF_Enable, OUT.SunCol);
	OUT.SunUV  = LightParameters.xy * float2(0.5, -0.5) + 0.5;

	return OUT;
}

float4 PS_ThinFilm(ThinFilmVSOutput IN) : SV_Target
{
	clip(IN.SunInt - SUN_INTENSITY_CLIP);

	//Reflected ghost position: symmetric about frame center
	float2 ReflUV = 1.0 - IN.texcoord.xy;
	float2 Delta  = ReflUV - IN.SunUV;
	Delta.x *= ScreenSize.z;
	float Dist = length(Delta);

	//Angle of incidence: center = normal, edges = oblique
	float EdgeDist = length(IN.texcoord.zw);
	float CosTheta = sqrt(saturate(1.0 - EdgeDist * EdgeDist * 0.5));

	//Thin-film interference reflectance per channel
	float3 CoatingRefl = ThinFilmReflectance(CosTheta, UITF_CoatingThick, UITF_IOR);

	//Tint with base coating color
	CoatingRefl *= UITF_BaseTint;

	//Spatial mask: coating reflection is a broad ghost centered opposite sun
	float GhostMask = exp(-Dist * Dist * 4.0);

	//Combine
	float3 Color = CoatingRefl * GhostMask * IN.SunCol * IN.SunInt;

	return float4(Color, 1.0);
}


//=============================================================================
//  TECHNIQUE PASS MACROS
//=============================================================================

#define GHOST_PASS(a) \
pass GhostPass##a { \
SetVertexShader(CompileShader(vs_5_0, VS_GhostSprite(UIG_Offset##a, \
                                                     UIG_Scale##a,   UIG_Int##a, \
                                                     UIG_CA##a,      UIG_Tint##a))); \
SetPixelShader (CompileShader(ps_5_0, PS_GhostSprite(UIG_Feather##a, UIG_DiffInt##a, UIG_DirtInt##a, \
                                                     UIG_Weight##a,  UIG_Vignette##a))); }

#define LENSGLARE_PASS pass LensGlarePass { \
SetVertexShader(CompileShader(vs_5_0, VS_LensGlareSprite())); \
SetPixelShader (CompileShader(ps_5_0, PS_LensGlareSprite())); }

#define SUNGLARE_PASS pass SunGlarePass { \
SetVertexShader(CompileShader(vs_5_0, VS_SunGlareSprite())); \
SetPixelShader (CompileShader(ps_5_0, PS_SunGlareSprite())); }

#define STARBURST_PASS pass StarburstPass { \
SetVertexShader(CompileShader(vs_5_0, VS_StarburstSprite())); \
SetPixelShader (CompileShader(ps_5_0, PS_StarburstSprite())); }

#define HOOP_PASS pass HoopPass { \
SetVertexShader(CompileShader(vs_5_0, VS_HoopSprite())); \
SetPixelShader (CompileShader(ps_5_0, PS_HoopSprite())); }

#define AUREOLE_PASS pass AureolePass { \
SetVertexShader(CompileShader(vs_5_0, VS_Aureole())); \
SetPixelShader (CompileShader(ps_5_0, PS_Aureole())); }

#define CORONA_PASS pass CoronaPass { \
SetVertexShader(CompileShader(vs_5_0, VS_Corona())); \
SetPixelShader (CompileShader(ps_5_0, PS_Corona())); }

#define HALO22_PASS pass Halo22Pass { \
SetVertexShader(CompileShader(vs_5_0, VS_Halo(22.0, UIHalo22_Int, UIHalo22_Enable))); \
SetPixelShader (CompileShader(ps_5_0, PS_Halo(UIHalo22_Thickness, UIHalo22_InnerEdge))); }

#define HALO46_PASS pass Halo46Pass { \
SetVertexShader(CompileShader(vs_5_0, VS_Halo(46.0, UIHalo46_Int, UIHalo46_Enable))); \
SetPixelShader (CompileShader(ps_5_0, PS_Halo(UIHalo46_Thickness, 8.0))); }

#define SUNDOG_LEFT_PASS pass SunDogLeftPass { \
SetVertexShader(CompileShader(vs_5_0, VS_SunDog(true))); \
SetPixelShader (CompileShader(ps_5_0, PS_SunDog(true))); }

#define SUNDOG_RIGHT_PASS pass SunDogRightPass { \
SetVertexShader(CompileShader(vs_5_0, VS_SunDog(false))); \
SetPixelShader (CompileShader(ps_5_0, PS_SunDog(false))); }

#define VEILGLARE_PASS pass VeilingGlarePass { \
SetVertexShader(CompileShader(vs_5_0, VS_VeilingGlare())); \
SetPixelShader (CompileShader(ps_5_0, PS_VeilingGlare())); }

#define EYELASH_PASS pass EyelashPass { \
SetVertexShader(CompileShader(vs_5_0, VS_Eyelash())); \
SetPixelShader (CompileShader(ps_5_0, PS_Eyelash())); }

#define BLOOMCROSS_PASS pass BloomCrossPass { \
SetVertexShader(CompileShader(vs_5_0, VS_BloomCross())); \
SetPixelShader (CompileShader(ps_5_0, PS_BloomCross())); }

#define DIRTYLENS_PASS pass DirtyLensPass { \
SetVertexShader(CompileShader(vs_5_0, VS_DirtyLens())); \
SetPixelShader (CompileShader(ps_5_0, PS_DirtyLens())); }

#define THINFILM_PASS pass ThinFilmPass { \
SetVertexShader(CompileShader(vs_5_0, VS_ThinFilm())); \
SetPixelShader (CompileShader(ps_5_0, PS_ThinFilm())); }


//=============================================================================
//  TECHNIQUE — 21 passes (down from 43 in original)
//=============================================================================

technique11 KitsuuneSunspriteGD <string UIName="Sunsprite - Kitsuune";>
{
	//Atmospheric scattering
	VEILGLARE_PASS
	AUREOLE_PASS
	//Ice crystal optics
	HALO22_PASS
	HALO46_PASS
	SUNDOG_LEFT_PASS
	SUNDOG_RIGHT_PASS
	//Ocular diffraction
	CORONA_PASS
	EYELASH_PASS
	//Lens optics
	LENSGLARE_PASS
	SUNGLARE_PASS
	STARBURST_PASS
	//Ghost flares (6, reduced from 9+8 double)
	GHOST_PASS(1)
	GHOST_PASS(2)
	GHOST_PASS(3)
	GHOST_PASS(4)
	GHOST_PASS(5)
	GHOST_PASS(6)
	HOOP_PASS
	//Sensor
	BLOOMCROSS_PASS
	//Cinematic extensions
	DIRTYLENS_PASS
	THINFILM_PASS
}
