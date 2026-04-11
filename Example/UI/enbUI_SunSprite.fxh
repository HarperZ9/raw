//----------------------------------------------------------------------------------------------//
//																								//
//								Main ENB Sunsprite UI file										//
//						      by LonelyKitsuune aka Skratzer									//
//						Updated for use with ENB Extender by Cr0w								//
//----------------------------------------------------------------------------------------------//

//This file requires my UI Primer header to work!
#ifndef _UI_PRIMER_
#error UI_Primer couldnt be found!
#endif

//----------------------------------------------------------------------------------------------//
//								  User Interface Parameters										//
//																								//
//----------------------------------------------------------------------------------------------//


#if SHADERGROUP == 0

UI_FileHeaderLong(">>>        ENB Sunsprite 2.1 for SSE        <<<",
				  ">>>              by LonelyKitsuune              <<<")

//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//

UI_WHITESPACE(1)
UI_ELEMENT(Lens_Settings,"         >>>>>>LENS SETTINGS<<<<<<")
UI_WHITESPACE(2)
bool  UI_EnableGlares      < string UIGroup = "Lens"; string UIName="|- Enable Glares";                                                    > = {false};	
bool  UI_EnableStarburst   < string UIGroup = "Lens"; string UIName="|- Enable Starburst";                                                 > = {false};
bool  UI_EnableAnam        < string UIGroup = "Lens"; string UIName="|- Enable Anamorphic Flares";                                         > = {false};
bool  UI_EnableGhost       < string UIGroup = "Lens"; string UIName="|- Enable Ghost Flares";                                              > = {false};
bool  UI_EnableHoop        < string UIGroup = "Lens"; string UIName="|- Enable Hoop/Ring Flare";                                           > = {false};
UI_SPECIAL_WHITESPACE(21)
int   UI_ApertureShape     < string UIGroup = "Lens"; string UIName="|- Aperture - Shape";             int   UIMin=4;    int   UIMax=9;	string Separation = "ExteriorWeather";> = {6};
int   UI_ApertureRotation  < string UIGroup = "Lens"; string UIName="|- Aperture - Rotation(\xB0)";    int   UIMin=0;    int   UIMax=360;  string Separation = "ExteriorWeather";> = {0};
float UI_ApertureRoundness < string UIGroup = "Lens"; string UIName="|- Aperture - Roundness";         float UIMin=0.0;  float UIMax=1.0;  string Separation = "ExteriorWeather";> = {0.1};


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//

UI_WHITESPACE(3)
UI_WHITESPACE(4)
UI_ELEMENT(SB_Settings," >>>>>>STARBURST LENS FLARE<<<<<<")
UI_WHITESPACE(5)

float  UISB_FlareInt          < string UIGroup = "Starburst"; string UIName="|- Starburst - TODIE -  Intensity";              float UIMin=0.0; float UIMax=10.0; > = {1.0};
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


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//

UI_WHITESPACE(6)
UI_WHITESPACE(7)
UI_ELEMENT(HOOP_Settings,"      >>>>>>HOOP LENS FLARE<<<<<<")
UI_WHITESPACE(8)

float  UIHoop_Int         < string UIGroup = "HoopFlare"; string UIName="|- Hoop - Intensity";							     float UIMin=0.0;  float UIMax=20.0;  string Separation = "ExteriorWeather";> = {2.0};
float  UIHoop_Scale       < string UIGroup = "HoopFlare"; string UIName="|- Hoop - Scale";								     float UIMin=0.0;  float UIMax=20.0;  string Separation = "ExteriorWeather";> = {7.5};
float  UIHoop_Offset      < string UIGroup = "HoopFlare"; string UIName="|- Hoop - Offset";							     float UIMin=-5.0; float UIMax=5.0;   string Separation = "ExteriorWeather";> = {0.7};
float  UIHoop_Width       < string UIGroup = "HoopFlare"; string UIName="|- Hoop - Width";								     float UIMin=0.0;  float UIMax=1.0;   string Separation = "ExteriorWeather";> = {0.25};
float  UIHoop_Roundness   < string UIGroup = "HoopFlare"; string UIName="|- Hoop - Roundness";							     float UIMin=0.0;  float UIMax=10.0;  string Separation = "ExteriorWeather";> = {1.0};
float  UIHoop_CA          < string UIGroup = "HoopFlare"; string UIName="|- Hoop - Chromatic Aberration";				     float UIMin=0.0;  float UIMax=5.0;   string Separation = "ExteriorWeather";> = {0.1};
float  UIHoop_SunDistMod  < string UIGroup = "HoopFlare"; string UIName="|- Hoop - Distance Modifier";					     float UIMin=0.0;  float UIMax=10.0;  string Separation = "ExteriorWeather";> = {1.5};
float  UIHoop_Fade        < string UIGroup = "HoopFlare"; string UIName="|- Hoop - Fade";								     float UIMin=0.0;  float UIMax=3.0;   string Separation = "ExteriorWeather";> = {0.7};
float  UIHoop_PatternFreq < string UIGroup = "HoopFlare"; string UIName="|- Hoop - Pattern Frequency";    float UIStep=1.0; float UIMin=0.0;  float UIMax=500.0; string Separation = "ExteriorWeather";> = {100.0};
float  UIHoop_IntRandom   < string UIGroup = "HoopFlare"; string UIName="|- Hoop - Intensity Randomness"; float UIStep=1.0; float UIMin=0.0;  float UIMax=300.0; string Separation = "ExteriorWeather";> = {80.0};
float4 UIHoop_Tint        < string UIGroup = "HoopFlare"; string UIName="|- Hoop - Tint";								     string UIWidget="Color";             string Separation = "ExteriorWeather";> = {1,1,1,1};


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//

UI_WHITESPACE(9)
UI_WHITESPACE(10)
UI_ELEMENT(G_Settings,"     >>>>>>SUN AND LENS GLARE<<<<<<")
UI_WHITESPACE(11)

float4 UI_LensGlareCol < string UIGroup = "SunGlare"; string UIName="|- Glare - Lens Color";      string UIWidget="Color";           string Separation = "ExteriorWeather";> = {1,1,1,1};
float  UI_LensGlareInt < string UIGroup = "SunGlare"; string UIName="|- Glare - Lens Intensity";  float UIMin=0.0; float UIMax=10.0; string Separation = "ExteriorWeather";> = {0.5};
float4 UI_SunGlareCol  < string UIGroup = "SunGlare"; string UIName="|- Glare - Sun Color";       string UIWidget="Color";           string Separation = "ExteriorWeather";> = {1,1,1,1};
float  UI_SunGlareInt  < string UIGroup = "SunGlare"; string UIName="|- Glare - Sun Intensity";   float UIMin=0.0; float UIMax=10.0; string Separation = "ExteriorWeather";> = {0.2};

#undef SHADERGROUP


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 1

#if PASS == 1
UI_ELEMENT(AF_Flares,">>>>>>ANAMORPHIC LENS FLARE<<<<<<")
UI_WHITESPACE(12)
float4 UIAF_Tint < string UIGroup = "AnamorphicFlare"; string UIName="|- AnamFlares - Tint";      string UIWidget="Color";          > = {1,1,1,1};
float  UIAF_Int  < string UIGroup = "AnamorphicFlare"; string UIName="|- AnamFlares - Intensity"; float UIMin=0.0; float UIMax=2.0; > = {1.0};
#endif

UI_SPECIAL_WHITESPACE(SWS)
UI_ELEMENT(COMBINE(AF_Flare,PASS),             TO_STRING(|- Anam PASS ------------------------------------------))
bool  COMBINE(UIAF_Mirror,PASS) < string UIGroup = "AnamorphicFlare"; string UIName=TO_STRING(|- Anam PASS - Mirror);                                        > = {false};
float COMBINE(UIAF_Int,PASS)    < string UIGroup = "AnamorphicFlare"; string UIName=TO_STRING(|- Anam PASS - Intensity); float UIMin=0.01; float UIMax=10.0; string Separation = "ExteriorWeather";> = {1.5};
float COMBINE(UIAF_Curve,PASS)  < string UIGroup = "AnamorphicFlare"; string UIName=TO_STRING(|- Anam PASS - Curve);     float UIMin=1.0;  float UIMax=10.0; string Separation = "ExteriorWeather";> = {3.0};
float COMBINE(UIAF_Offset,PASS) < string UIGroup = "AnamorphicFlare"; string UIName=TO_STRING(|- Anam PASS - Offset);    float UIMin=-3.0; float UIMax=3.0;  string Separation = "ExteriorWeather";> = {1.0};
float COMBINE(UIAF_Width,PASS)  < string UIGroup = "AnamorphicFlare"; string UIName=TO_STRING(|- Anam PASS - Width);     float UIMin=0.01; float UIMax=3.0;  string Separation = "ExteriorWeather";> = {2.0};

#undef PASS
#undef SWS


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 2

#if PASS == 1
UI_ELEMENT(G_Flares,"          >>>>>>GHOST FLARES<<<<<<")
UI_WHITESPACE(13)
float UIG_DiffFreq    < string UIGroup = "GhostFlares"; string UIName="|- Ghost - Diffraction Frequency";  float UIMin=1.0; float UIMax= 1.5; string Separation = "ExteriorWeather";> = {1.0};
float UIG_Int         < string UIGroup = "GhostFlares"; string UIName="|- Ghost - Intensity";              float UIMin=0.0; float UIMax=10.0; string Separation = "ExteriorWeather";> = {1.0};
float UIG_OMoveWeight < string UIGroup = "GhostFlares"; string UIName="|- Ghost - Offset Movement Weight"; float UIMin=0.0; float UIMax=10.0; string Separation = "ExteriorWeather";> = {0.0};
float UIG_OMoveSeed   < string UIGroup = "GhostFlares"; string UIName="|- Ghost - Offset Movement Seed";   float UIMin=0.0; float UIStep=1.0; string Separation = "ExteriorWeather";> = {0.0};
float UIG_OffsetCurve < string UIGroup = "GhostFlares"; string UIName="|- Ghost - Offset Curve";           float UIMin=1.0; float UIMax=10.0; string Separation = "ExteriorWeather";> = {1.0};
#endif

UI_SPECIAL_WHITESPACE(SWS)
UI_ELEMENT(COMBINE(G_Flare,PASS),                   TO_STRING(|- Flare PASS ------------------------------------------))
float  COMBINE(UIG_Int,PASS)         < string UIGroup = "GhostFlares"; string UIName=TO_STRING(|- Flare PASS - Intensity);             float UIMin=0.01; float UIMax=10.0; string Separation = "ExteriorWeather";> = {0.8};
float  COMBINE(UIG_Offset,PASS)      < string UIGroup = "GhostFlares"; string UIName=TO_STRING(|- Flare PASS - Offset);                float UIMin=-3.0; float UIMax=3.0;  string Separation = "ExteriorWeather";> = {1.0};
float  COMBINE(UIG_Scale,PASS)       < string UIGroup = "GhostFlares"; string UIName=TO_STRING(|- Flare PASS - Scale);                 float UIMin=0.01; float UIMax=20.0; string Separation = "ExteriorWeather";> = {1.0};
float4 COMBINE(UIG_Tint,PASS)        < string UIGroup = "GhostFlares"; string UIName=TO_STRING(|- Flare PASS - Tint);                  string UIWidget="Color";            string Separation = "ExteriorWeather";> = {1,1,1,1};
float  COMBINE(UIG_Feather,PASS)     < string UIGroup = "GhostFlares"; string UIName=TO_STRING(|- Flare PASS - Feathering);            float UIMin=0.0;  float UIMax=1.0;  string Separation = "ExteriorWeather";> = {0.05};
float  COMBINE(UIG_Weight,PASS)      < string UIGroup = "GhostFlares"; string UIName=TO_STRING(|- Flare PASS - Shape Weight);          float UIMin=0.0;  float UIMax=1.0;  string Separation = "ExteriorWeather";> = {1.0};
float  COMBINE(UIG_Vignette,PASS)    < string UIGroup = "GhostFlares"; string UIName=TO_STRING(|- Flare PASS - Vignette);              float UIMin=0.0;  float UIMax=1.0;  string Separation = "ExteriorWeather";> = {0.0};
float  COMBINE(UIG_CA,PASS)          < string UIGroup = "GhostFlares"; string UIName=TO_STRING(|- Flare PASS - Chromatic Aberration);  float UIMin=0.0;  float UIMax=1.0;  string Separation = "ExteriorWeather";> = {0.1};
float  COMBINE(UIG_DiffInt,PASS)     < string UIGroup = "GhostFlares"; string UIName=TO_STRING(|- Flare PASS - Diffraction Intensity); float UIMin=0.0;  float UIMax=20.0; string Separation = "ExteriorWeather";> = {0.5};
float  COMBINE(UIG_DirtInt,PASS)     < string UIGroup = "GhostFlares"; string UIName=TO_STRING(|- Flare PASS - Dirt Intensity);        float UIMin=0.0;  float UIMax=10.0; string Separation = "ExteriorWeather";> = {0.2};

#undef PASS
#undef SWS


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 3

UI_SPECIAL_WHITESPACE(SWS)
UI_ELEMENT(COMBINE(DG_Flare,PASS),                   TO_STRING(|- Double Flare PASS ---------------------------------))
float  COMBINE(UIDG_Int,PASS)         < string UIGroup = "GhostFlares.Double"; string UIName=TO_STRING(|- Double Flare PASS - Intensity);             float UIMin=0.01; float UIMax=10.0; string Separation = "ExteriorWeather";> = {0.8};
float  COMBINE(UIDG_Offset,PASS)      < string UIGroup = "GhostFlares.Double"; string UIName=TO_STRING(|- Double Flare PASS - Offset);                float UIMin=-3.0; float UIMax=3.0;  string Separation = "ExteriorWeather";> = {1.0};
float  COMBINE(UIDG_Scale,PASS)       < string UIGroup = "GhostFlares.Double"; string UIName=TO_STRING(|- Double Flare PASS - Scale);                 float UIMin=0.01; float UIMax=20.0; string Separation = "ExteriorWeather";> = {1.0};
float4 COMBINE(UIDG_Tint,PASS)        < string UIGroup = "GhostFlares.Double"; string UIName=TO_STRING(|- Double Flare PASS - Tint);                  string UIWidget="Color";            string Separation = "ExteriorWeather";> = {1,1,1,1};
float  COMBINE(UIDG_Feather,PASS)     < string UIGroup = "GhostFlares.Double"; string UIName=TO_STRING(|- Double Flare PASS - Feathering);            float UIMin=0.0;  float UIMax=1.0;  string Separation = "ExteriorWeather";> = {0.05};
float  COMBINE(UIDG_Weight,PASS)      < string UIGroup = "GhostFlares.Double"; string UIName=TO_STRING(|- Double Flare PASS - Shape Weight);          float UIMin=0.0;  float UIMax=1.0;  string Separation = "ExteriorWeather";> = {1.0};
float  COMBINE(UIDG_Vignette,PASS)    < string UIGroup = "GhostFlares.Double"; string UIName=TO_STRING(|- Double Flare PASS - Vignette);              float UIMin=0.0;  float UIMax=1.0;  string Separation = "ExteriorWeather";> = {0.0};
float  COMBINE(UIDG_CA,PASS)          < string UIGroup = "GhostFlares.Double"; string UIName=TO_STRING(|- Double Flare PASS - Chromatic Aberration);  float UIMin=0.0;  float UIMax=1.0;  string Separation = "ExteriorWeather";> = {0.1};
float  COMBINE(UIDG_DiffInt,PASS)     < string UIGroup = "GhostFlares.Double"; string UIName=TO_STRING(|- Double Flare PASS - Diffraction Intensity); float UIMin=0.0;  float UIMax=20.0; string Separation = "ExteriorWeather";> = {0.5};
float  COMBINE(UIDG_DirtInt,PASS)     < string UIGroup = "GhostFlares.Double"; string UIName=TO_STRING(|- Double Flare PASS - Dirt Intensity);        float UIMin=0.0;  float UIMax=10.0; string Separation = "ExteriorWeather";> = {0.2};
float  COMBINE(UIDG_Dupl_Offset,PASS) < string UIGroup = "GhostFlares.Double"; string UIName=TO_STRING(|- Double Flare PASS - Duplicate Offset-Mod);  float UIMin=-6.0; float UIMax=6.0;  string Separation = "ExteriorWeather";> = {0.0};

#undef PASS
#undef SWS


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
//  New effects by Zain Dana Harper — physically-motivated atmospheric and ocular phenomena
//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 4

UI_WHITESPACE(18)
UI_ELEMENT(Aureole_Settings,"   >>>>>>ATMOSPHERIC AUREOLE<<<<<<")
UI_WHITESPACE(19)

bool   UIAur_Enable       < string UIGroup = "Aureole"; string UIName="|- Aureole - Enable";                                                               > = {true};
float  UIAur_Int          < string UIGroup = "Aureole"; string UIName="|- Aureole - Intensity";          float UIMin=0.0;  float UIMax=5.0;  string Separation = "ExteriorWeather";> = {0.8};
float  UIAur_InnerScale   < string UIGroup = "Aureole"; string UIName="|- Aureole - Core Scale";         float UIMin=0.01; float UIMax=3.0;  string Separation = "ExteriorWeather";> = {0.35};
float  UIAur_OuterScale   < string UIGroup = "Aureole"; string UIName="|- Aureole - Haze Scale";         float UIMin=0.1;  float UIMax=5.0;  string Separation = "ExteriorWeather";> = {1.5};
float  UIAur_CoreFalloff  < string UIGroup = "Aureole"; string UIName="|- Aureole - Core Falloff";       float UIMin=1.0;  float UIMax=20.0; string Separation = "ExteriorWeather";> = {6.0};
float  UIAur_HazeFalloff  < string UIGroup = "Aureole"; string UIName="|- Aureole - Haze Falloff";       float UIMin=0.1;  float UIMax=10.0; string Separation = "ExteriorWeather";> = {1.8};
float  UIAur_Warmth       < string UIGroup = "Aureole"; string UIName="|- Aureole - Warmth";             float UIMin=0.0;  float UIMax=2.0;  string Separation = "ExteriorWeather";> = {0.5};
float  UIAur_HorizonBoost < string UIGroup = "Aureole"; string UIName="|- Aureole - Horizon Boost";      float UIMin=0.0;  float UIMax=3.0;  string Separation = "ExteriorWeather";> = {1.0};
float  UIAur_Breathe      < string UIGroup = "Aureole"; string UIName="|- Aureole - Breathing Amount";   float UIMin=0.0;  float UIMax=0.5;  string Separation = "ExteriorWeather";> = {0.04};
float4 UIAur_Tint         < string UIGroup = "Aureole"; string UIName="|- Aureole - Tint";               string UIWidget="Color";            string Separation = "ExteriorWeather";> = {1.0, 0.95, 0.85, 0.0};

UI_SPECIAL_WHITESPACE(22)
UI_ELEMENT(Corona_Settings,"    >>>>>>CILIARY CORONA<<<<<<")
UI_SPECIAL_WHITESPACE(23)

bool   UICorona_Enable    < string UIGroup = "Corona"; string UIName="|- Corona - Enable";                                                                > = {true};
float  UICorona_Int       < string UIGroup = "Corona"; string UIName="|- Corona - Intensity";           float UIMin=0.0;  float UIMax=3.0;  string Separation = "ExteriorWeather";> = {0.4};
float  UICorona_Scale     < string UIGroup = "Corona"; string UIName="|- Corona - Scale";               float UIMin=0.1;  float UIMax=3.0;  string Separation = "ExteriorWeather";> = {0.8};
float  UICorona_RingCount < string UIGroup = "Corona"; string UIName="|- Corona - Ring Count";          float UIMin=1.0;  float UIMax=20.0; float UIStep=0.5; string Separation = "ExteriorWeather";> = {6.0};
float  UICorona_RingSharp < string UIGroup = "Corona"; string UIName="|- Corona - Ring Sharpness";      float UIMin=0.5;  float UIMax=8.0;  string Separation = "ExteriorWeather";> = {2.0};
float  UICorona_Spectral  < string UIGroup = "Corona"; string UIName="|- Corona - Spectral Spread";     float UIMin=0.0;  float UIMax=0.3;  string Separation = "ExteriorWeather";> = {0.08};
float  UICorona_FiberAmt  < string UIGroup = "Corona"; string UIName="|- Corona - Fiber Detail";        float UIMin=0.0;  float UIMax=1.0;  string Separation = "ExteriorWeather";> = {0.45};
float  UICorona_FiberFreq < string UIGroup = "Corona"; string UIName="|- Corona - Fiber Count";         float UIMin=10.0; float UIMax=300.0; float UIStep=1.0; string Separation = "ExteriorWeather";> = {80.0};
float  UICorona_Falloff   < string UIGroup = "Corona"; string UIName="|- Corona - Falloff";             float UIMin=0.5;  float UIMax=10.0; string Separation = "ExteriorWeather";> = {3.0};

UI_SPECIAL_WHITESPACE(24)
UI_ELEMENT(SpecSB_Settings,"  >>>>>>SPECTRAL STARBURST<<<<<<")
UI_SPECIAL_WHITESPACE(25)

float  UISB_SpectralAmt  < string UIGroup = "SpectralStarburst"; string UIName="|- Starburst - Spectral Amount";  float UIMin=0.0;  float UIMax=1.0;  string Separation = "ExteriorWeather";> = {0.4};
float  UISB_SpectralFreq < string UIGroup = "SpectralStarburst"; string UIName="|- Starburst - Spectral Cycles";  float UIMin=0.5;  float UIMax=5.0;  string Separation = "ExteriorWeather";> = {1.5};

UI_SPECIAL_WHITESPACE(26)
UI_ELEMENT(Moon_Settings,"      >>>>>>MOONLIGHT FLARES<<<<<<")
UI_SPECIAL_WHITESPACE(27)

bool   UIMoon_Enable      < string UIGroup = "MoonFlares"; string UIName="|- Moon - Enable";                                                                > = {true};
float  UIMoon_Sensitivity < string UIGroup = "MoonFlares"; string UIName="|- Moon - Detection Sensitivity"; float UIMin=0.005;float UIMax=1.0;  string Separation = "ExteriorWeather";> = {0.08};
float  UIMoon_Curve       < string UIGroup = "MoonFlares"; string UIName="|- Moon - Detection Curve";       float UIMin=0.3;  float UIMax=4.0;  string Separation = "ExteriorWeather";> = {1.0};
float  UIMoon_IntMult     < string UIGroup = "MoonFlares"; string UIName="|- Moon - Intensity Multiplier";  float UIMin=0.0;  float UIMax=10.0; string Separation = "ExteriorWeather";> = {3.5};
float  UIMoon_AureoleMult < string UIGroup = "MoonFlares"; string UIName="|- Moon - Aureole Multiplier";    float UIMin=0.0;  float UIMax=5.0;  string Separation = "ExteriorWeather";> = {1.0};
float  UIMoon_CoronaMult  < string UIGroup = "MoonFlares"; string UIName="|- Moon - Corona Multiplier";     float UIMin=0.0;  float UIMax=5.0;  string Separation = "ExteriorWeather";> = {1.8};
float3 UIMoon_Tint        < string UIGroup = "MoonFlares"; string UIName="|- Moon - Color Tint";            string UIWidget="Color";            string Separation = "ExteriorWeather";> = {0.75, 0.82, 1.0};


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
//  Ice crystal atmospheric optics
//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 5

UI_WHITESPACE(28)
UI_ELEMENT(Halo22_Settings,"    >>>>>>22\xB0 ICE CRYSTAL HALO<<<<<<")
UI_WHITESPACE(29)

bool   UIHalo22_Enable    < string UIGroup = "IceHalo22"; string UIName="|- 22\xB0 Halo - Enable";                                                          > = {false};
float  UIHalo22_Int       < string UIGroup = "IceHalo22"; string UIName="|- 22\xB0 Halo - Intensity";        float UIMin=0.0;  float UIMax=3.0;  string Separation = "ExteriorWeather";> = {0.35};
float  UIHalo22_Thickness < string UIGroup = "IceHalo22"; string UIName="|- 22\xB0 Halo - Thickness";        float UIMin=0.005;float UIMax=0.15; string Separation = "ExteriorWeather";> = {0.04};
float  UIHalo22_InnerEdge < string UIGroup = "IceHalo22"; string UIName="|- 22\xB0 Halo - Inner Sharpness";  float UIMin=1.0;  float UIMax=30.0; string Separation = "ExteriorWeather";> = {12.0};

UI_SPECIAL_WHITESPACE(30)
UI_ELEMENT(Halo46_Settings,"    >>>>>>46\xB0 CIRCUMSCRIBED HALO<<<<<<")
UI_SPECIAL_WHITESPACE(31)

bool   UIHalo46_Enable    < string UIGroup = "IceHalo46"; string UIName="|- 46\xB0 Halo - Enable";                                                          > = {false};
float  UIHalo46_Int       < string UIGroup = "IceHalo46"; string UIName="|- 46\xB0 Halo - Intensity";        float UIMin=0.0;  float UIMax=2.0;  string Separation = "ExteriorWeather";> = {0.12};
float  UIHalo46_Thickness < string UIGroup = "IceHalo46"; string UIName="|- 46\xB0 Halo - Thickness";        float UIMin=0.005;float UIMax=0.2;  string Separation = "ExteriorWeather";> = {0.06};

UI_SPECIAL_WHITESPACE(32)

float  UIHalo_Dispersion  < string UIGroup = "IceHalo46"; string UIName="|- Halos - Spectral Dispersion";    float UIMin=0.0;  float UIMax=0.1;  string Separation = "ExteriorWeather";> = {0.025};
float4 UIHalo_Tint        < string UIGroup = "IceHalo46"; string UIName="|- Halos - Tint";                   string UIWidget="Color";            string Separation = "ExteriorWeather";> = {1,1,1,0};

UI_SPECIAL_WHITESPACE(33)
UI_ELEMENT(SunDog_Settings,"       >>>>>>SUN DOGS<<<<<<")
UI_SPECIAL_WHITESPACE(34)

bool   UIDog_Enable       < string UIGroup = "SunDogs"; string UIName="|- Sun Dogs - Enable";                                                              > = {false};
float  UIDog_Int          < string UIGroup = "SunDogs"; string UIName="|- Sun Dogs - Intensity";           float UIMin=0.0;  float UIMax=5.0;  string Separation = "ExteriorWeather";> = {1.0};
float  UIDog_Size         < string UIGroup = "SunDogs"; string UIName="|- Sun Dogs - Size";                float UIMin=0.02; float UIMax=0.5;  string Separation = "ExteriorWeather";> = {0.12};
float  UIDog_Stretch      < string UIGroup = "SunDogs"; string UIName="|- Sun Dogs - Horizontal Stretch";  float UIMin=1.0;  float UIMax=5.0;  string Separation = "ExteriorWeather";> = {2.5};
float  UIDog_Dispersion   < string UIGroup = "SunDogs"; string UIName="|- Sun Dogs - Spectral Spread";     float UIMin=0.0;  float UIMax=0.4;  string Separation = "ExteriorWeather";> = {0.15};
float  UIDog_HorizonBoost < string UIGroup = "SunDogs"; string UIName="|- Sun Dogs - Low-Sun Boost";       float UIMin=0.0;  float UIMax=3.0;  string Separation = "ExteriorWeather";> = {1.5};
float4 UIDog_Tint         < string UIGroup = "SunDogs"; string UIName="|- Sun Dogs - Tint";                string UIWidget="Color";            string Separation = "ExteriorWeather";> = {1,1,1,0};


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
//  Ocular and camera effects
//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 6

UI_WHITESPACE(35)
UI_ELEMENT(Veil_Settings,"      >>>>>>VEILING GLARE<<<<<<")
UI_WHITESPACE(36)

bool   UIVeil_Enable      < string UIGroup = "VeilingGlare"; string UIName="|- Veil - Enable";                                                                 > = {true};
float  UIVeil_Int         < string UIGroup = "VeilingGlare"; string UIName="|- Veil - Intensity";              float UIMin=0.0;  float UIMax=1.0;  string Separation = "ExteriorWeather";> = {0.15};
float  UIVeil_Falloff     < string UIGroup = "VeilingGlare"; string UIName="|- Veil - Falloff";               float UIMin=0.3;  float UIMax=5.0;  string Separation = "ExteriorWeather";> = {1.2};
float  UIVeil_SunColor    < string UIGroup = "VeilingGlare"; string UIName="|- Veil - Sun Color Amount";      float UIMin=0.0;  float UIMax=1.0;  string Separation = "ExteriorWeather";> = {0.6};

UI_SPECIAL_WHITESPACE(37)
UI_ELEMENT(Lash_Settings,"   >>>>>>EYELASH DIFFRACTION<<<<<<")
UI_SPECIAL_WHITESPACE(38)

bool   UILash_Enable      < string UIGroup = "EyelashDiffraction"; string UIName="|- Eyelash - Enable";                                                              > = {false};
float  UILash_Int         < string UIGroup = "EyelashDiffraction"; string UIName="|- Eyelash - Intensity";           float UIMin=0.0;  float UIMax=3.0;  string Separation = "ExteriorWeather";> = {0.5};
float  UILash_Height      < string UIGroup = "EyelashDiffraction"; string UIName="|- Eyelash - Streak Height";       float UIMin=0.1;  float UIMax=2.0;  string Separation = "ExteriorWeather";> = {0.8};
float  UILash_Width       < string UIGroup = "EyelashDiffraction"; string UIName="|- Eyelash - Streak Width";        float UIMin=0.005;float UIMax=0.15; string Separation = "ExteriorWeather";> = {0.04};
float  UILash_FiberCount  < string UIGroup = "EyelashDiffraction"; string UIName="|- Eyelash - Fiber Count";         float UIMin=3.0;  float UIMax=40.0; float UIStep=1.0; string Separation = "ExteriorWeather";> = {12.0};
float  UILash_FiberRand   < string UIGroup = "EyelashDiffraction"; string UIName="|- Eyelash - Fiber Randomness";    float UIMin=0.0;  float UIMax=1.0;  string Separation = "ExteriorWeather";> = {0.5};
float  UILash_Spectral    < string UIGroup = "EyelashDiffraction"; string UIName="|- Eyelash - Spectral Spread";     float UIMin=0.0;  float UIMax=0.15; string Separation = "ExteriorWeather";> = {0.03};
float4 UILash_Tint        < string UIGroup = "EyelashDiffraction"; string UIName="|- Eyelash - Tint";                string UIWidget="Color";            string Separation = "ExteriorWeather";> = {1,1,1,0};

UI_SPECIAL_WHITESPACE(39)
UI_ELEMENT(Bloom_Settings,"   >>>>>>SENSOR BLOOM CROSS<<<<<<")
UI_SPECIAL_WHITESPACE(40)

bool   UIBloom_Enable     < string UIGroup = "SensorBloom"; string UIName="|- Bloom Cross - Enable";                                                           > = {false};
float  UIBloom_Int        < string UIGroup = "SensorBloom"; string UIName="|- Bloom Cross - Intensity";       float UIMin=0.0;  float UIMax=2.0;  string Separation = "ExteriorWeather";> = {0.3};
float  UIBloom_Length     < string UIGroup = "SensorBloom"; string UIName="|- Bloom Cross - Spike Length";     float UIMin=0.05; float UIMax=2.0;  string Separation = "ExteriorWeather";> = {0.6};
float  UIBloom_Thickness  < string UIGroup = "SensorBloom"; string UIName="|- Bloom Cross - Spike Width";     float UIMin=0.001;float UIMax=0.05; string Separation = "ExteriorWeather";> = {0.008};
float  UIBloom_Falloff    < string UIGroup = "SensorBloom"; string UIName="|- Bloom Cross - Falloff";         float UIMin=0.5;  float UIMax=10.0; string Separation = "ExteriorWeather";> = {3.0};
int    UIBloom_Rotation   < string UIGroup = "SensorBloom"; string UIName="|- Bloom Cross - Rotation(\xB0)";  int   UIMin=0;    int   UIMax=90;   string Separation = "ExteriorWeather";> = {0};
float4 UIBloom_Tint       < string UIGroup = "SensorBloom"; string UIName="|- Bloom Cross - Tint";            string UIWidget="Color";            string Separation = "ExteriorWeather";> = {1,1,1,0};



//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 99

//----------------------------------------------------------------------------------------------//
//                     SKYRIMBRIDGE SUNSPRITE ENHANCEMENTS                                      //
//----------------------------------------------------------------------------------------------//
// Requires SkyrimBridge SKSE plugin + SkyrimBridge.fxh in Helper/                              //
//----------------------------------------------------------------------------------------------//

#ifdef SKYRIMBRIDGE_FXH

UI_ELEMENT(SB_SS_Header,       "    >>>>>>SKYRIMBRIDGE FLARES<<<<<<")

//--- Weather ---
int SB_SS_Spacer0   <string UIName="|--------- Weather Response"; int UIMin=0; int UIMax=0;> = {0};
float UISBSS_WeatherSuppress  < string UIGroup = "SB.Flares"; string UIName="|- SB - Weather Flare Suppression";     float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UISBSS_RainReduce       < string UIGroup = "SB.Flares"; string UIName="|- SB - Rain Flare Reduction";           float UIMin= 0.0; float UIMax= 1.0; > = { 0.8};
float UISBSS_CloudReduce      < string UIGroup = "SB.Flares"; string UIName="|- SB - Overcast Flare Reduction";       float UIMin= 0.0; float UIMax= 0.8; > = { 0.4};

//--- Sun Color ---
int SB_SS_Spacer1   <string UIName="|--------- Sun Color"; int UIMin=0; int UIMax=0;> = {0};
float UISBSS_SunColorBlend    < string UIGroup = "SB.Flares"; string UIName="|- SB - Sun Color Blend Amount";         float UIMin= 0.0; float UIMax= 1.0; > = { 0.6};
float UISBSS_SunElevation     < string UIGroup = "SB.Flares"; string UIName="|- SB - Sun Elevation Golden Hour";      float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};

//--- Atmosphere ---
int SB_SS_Spacer2   <string UIName="|--------- Atmosphere"; int UIMin=0; int UIMax=0;> = {0};
float UISBSS_LightningFlash   < string UIGroup = "SB.Flares"; string UIName="|- SB - Lightning Flash Strength";       float UIMin= 0.0; float UIMax= 1.0; > = { 0.3};
float UISBSS_SmoothDayNight   < string UIGroup = "SB.Flares"; string UIName="|- SB - Smooth Day/Night Transition";    float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UISBSS_MenuSuppress     < string UIGroup = "SB.Flares"; string UIName="|- SB - Skip Flares In Menus";           float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};

//--- Atmospheric ---
int SB_SS_Spacer3   <string UIName="|--------- Atmospheric"; int UIMin=0; int UIMax=0;> = {0};
float UISBSS_AtmosExtinction  < string UIGroup = "SB.Flares"; string UIName="|- SB - Rayleigh Atmos Extinction";     float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UISBSS_FogExtinction    < string UIGroup = "SB.Flares"; string UIName="|- SB - Fog Flare Extinction";           float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};

//--- Moon & Special ---
int SB_SS_Spacer4   <string UIName="|--------- Moon & Special"; int UIMin=0; int UIMax=0;> = {0};
float UISBSS_MoonFlares       < string UIGroup = "SB.Flares"; string UIName="|- SB - Moon Flare Enable";              float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UISBSS_MasserIntMult    < string UIGroup = "SB.Flares"; string UIName="|- SB - Masser Intensity Mult";          float UIMin= 0.0; float UIMax= 3.0; > = { 1.0};
float UISBSS_SecundaIntMult   < string UIGroup = "SB.Flares"; string UIName="|- SB - Secunda Intensity Mult";         float UIMin= 0.0; float UIMax= 3.0; > = { 0.5};
float UISBSS_NightEyeSuppress < string UIGroup = "SB.Flares"; string UIName="|- SB - Night Eye Suppress Flares";      float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UISBSS_EtherealDim      < string UIGroup = "SB.Flares"; string UIName="|- SB - Ethereal Dim Flares";            float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};

#endif //SKYRIMBRIDGE_FXH

#endif //SHADERGROUP










