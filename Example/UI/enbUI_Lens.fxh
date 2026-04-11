//----------------------------------------------------------------------------------------------//
//																								//
//								  Main ENB Lens UI file											//
//						      by LonelyKitsuune aka Skratzer									//
//																								//
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

UI_FileHeaderLong(">>>            ENB Master Lens 4.0            <<<",
				  ">>>              by LonelyKitsuune              <<<")
UI_WHITESPACE(1)


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//

UI_WHITESPACE(2)
UI_ELEMENT(ML_Features,"      >>>>>>LENS FEATURE-SET<<<<<<")
UI_WHITESPACE(3)
UI_ELEMENT(ML_TechNote1,		 "|         Toggle Ghosts and Starburst")
UI_ELEMENT(ML_TechNote2,		 "|        Lensflares through TECHNIQUE")
UI_SPECIAL_WHITESPACE(1)
bool UIWFXRain_Enable  < string UIGroup = "LensFeatures"; string UIName="|- Enable Weather FX - Rain";	  > = {false};
bool UIWFXFrost_Enable < string UIGroup = "LensFeatures"; string UIName="|- Enable Weather FX - Frost";	  > = {false};
bool UIFOG_Enable      < string UIGroup = "LensFeatures"; string UIName="|- Enable Bloom Atmosphere";      > = {false};
bool UIAF_Enable       < string UIGroup = "LensFeatures"; string UIName="|- Enable Anamorphic Lens Flares"; > = {false};
bool UIR_Enable        < string UIGroup = "LensFeatures"; string UIName="|- Enable Lens Reflections";		  > = {false};
bool UID_Enable        < string UIGroup = "LensFeatures"; string UIName="|- Enable Lens Dirt";			  > = {false};
bool UICA_Enable       < string UIGroup = "GhostFlares.CA"; string UIName="|- Enable Chromatic Aberration";   > = {false};
bool UILO_Enable       < string UIGroup = "GhostFlares.CA"; string UIName="|- Enable Lens Optics";            > = {false};
bool UIHL_Enable       < string UIGroup = "GhostFlares.CA"; string UIName="|- Enable Halation";               > = {false};
bool UIVG_Enable       < string UIGroup = "Halation.VeilingGlare"; string UIName="|- Enable Veiling Glare";          > = {false};
bool UIFG_Enable       < string UIGroup = "Halation.VeilingGlare"; string UIName="|- Enable Film Grain";             > = {false};
bool UISR_Enable       < string UIGroup = "Halation.VeilingGlare"; string UIName="|- Enable Sensor Response";        > = {false};
bool UICRT_Enable      < string UIGroup = "Halation.VeilingGlare"; string UIName="|- Enable CRT Display";            > = {false};
bool UIDIFF_Enable     < string UIGroup = "Halation.VeilingGlare"; string UIName="|- Enable Lens Diffusion";          > = {false};
bool UIFHALO_Enable    < string UIGroup = "Halation.VeilingGlare"; string UIName="|- Enable Film Halation";           > = {false};
bool UILEAK_Enable     < string UIGroup = "Halation.VeilingGlare"; string UIName="|- Enable Light Leaks";             > = {false};
bool UIWEAVE_Enable    < string UIGroup = "Halation.VeilingGlare"; string UIName="|- Enable Gate Weave";              > = {false};
bool UILBOX_Enable     < string UIGroup = "Halation.VeilingGlare"; string UIName="|- Enable Letterbox";               > = {false};
bool UIANAM_Enable     < string UIGroup = "Halation.VeilingGlare"; string UIName="|- Enable Anamorphic Lens";          > = {false};
bool UIVIG_Enable      < string UIGroup = "Halation.VeilingGlare"; string UIName="|- Enable Optical Vignette";         > = {false};
bool UIDMG_Enable      < string UIGroup = "Halation.VeilingGlare"; string UIName="|- Enable Film Damage";              > = {false};
bool UI_IgnoreSky      < string UIGroup = "Halation.VeilingGlare"; string UIName="|- Lensflares ignore Sun/Sky";	  > = {false};


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 1

UI_ELEMENT(ML_WeatherFX,"      >>>>>>WEATHER EFFECTS<<<<<<")
UI_WHITESPACE(4)
float  Day_UIWFXRain_Intensity	  < string UIGroup = "WeatherFX"; string UIName="|- RainFX - Day - Intensity";			   float UIMin=0.0;  float UIMax=10.0; float UIStep=0.01; > = {1.0};
float4 Day_UIWFXRain_SkyColor	  < string UIGroup = "WeatherFX"; string UIName="|- RainFX - Day - Sky Color";			   string UIWidget="Color";								  > = {1.0, 1.0, 1.0, 1.0};
float4 Day_UIWFXRain_EnvColor	  < string UIGroup = "WeatherFX"; string UIName="|- RainFX - Day - Environment Color";    string UIWidget="Color";								  > = {0.3, 0.3, 0.3, 1.0};
float  Night_UIWFXRain_Intensity  < string UIGroup = "WeatherFX"; string UIName="|- RainFX - Night - Intensity";		   float UIMin=0.0;  float UIMax=10.0; float UIStep=0.01; > = {1.0};
float4 Night_UIWFXRain_SkyColor	  < string UIGroup = "WeatherFX"; string UIName="|- RainFX - Night - Sky Color";		   string UIWidget="Color";								  > = {1.0, 1.0, 1.0, 1.0};
float4 Night_UIWFXRain_EnvColor   < string UIGroup = "WeatherFX"; string UIName="|- RainFX - Night - Environment Color";  string UIWidget="Color";								  > = {0.3, 0.3, 0.3, 1.0};
UI_SPECIAL_WHITESPACE(2)
float  Day_UIWFXFrost_Intensity   < string UIGroup = "WeatherFX"; string UIName="|- FrostFX - Day - Intensity";		   float UIMin=0.0;  float UIMax=5.0;  float UIStep=0.01; > = {0.5};
float4 Day_UIWFXFrost_SkyColor	  < string UIGroup = "WeatherFX"; string UIName="|- FrostFX - Day - Sky Color";		   string UIWidget="Color";								  > = {1.0, 1.0, 1.0, 1.0};
float4 Day_UIWFXFrost_EnvColor	  < string UIGroup = "WeatherFX"; string UIName="|- FrostFX - Day - Environment Color";   string UIWidget="Color";								  > = {0.3, 0.3, 0.3, 1.0};
float  Night_UIWFXFrost_Intensity < string UIGroup = "WeatherFX"; string UIName="|- FrostFX - Night - Intensity";		   float UIMin=0.0;  float UIMax=5.0;  float UIStep=0.01; > = {0.5};
float4 Night_UIWFXFrost_SkyColor  < string UIGroup = "WeatherFX"; string UIName="|- FrostFX - Night - Sky Color";		   string UIWidget="Color";								  > = {1.0, 1.0, 1.0, 1.0};
float4 Night_UIWFXFrost_EnvColor  < string UIGroup = "WeatherFX"; string UIName="|- FrostFX - Night - Environment Color"; string UIWidget="Color";								  > = {0.3, 0.3, 0.3, 1.0};
UI_SPECIAL_WHITESPACE(3)
UI_ELEMENT(ML_RainFX,							 "|--------- Rain Droplets")
float  UIWFXRain_Tickrate		  < string UIGroup = "WeatherFX.Rain"; string UIName="|- RainFX - Tickrate";		    float UIMin=0.01; float UIMax=10.0; float UIStep=0.01;  > = {0.15};
float  UIWFXRain_FadeCurve		  < string UIGroup = "WeatherFX.Rain"; string UIName="|- RainFX - Fade Curve";		    float UIMin=0.01; float UIMax=10.0; float UIStep=0.01;  > = {1.8};
float  UIWFXRain_MinSize		  < string UIGroup = "WeatherFX.Rain"; string UIName="|- RainFX - Min Size";		    float UIMin=0.0;  float UIMax=10.0; float UIStep=0.001; > = {0.03};
float  UIWFXRain_MaxSize		  < string UIGroup = "WeatherFX.Rain"; string UIName="|- RainFX - Max Size";		    float UIMin=0.01; float UIMax=10.0; float UIStep=0.001; > = {0.15};
float  UIWFXRain_MaxDeformation	  < string UIGroup = "WeatherFX.Rain"; string UIName="|- RainFX - Max Deformation";		float UIMin=0.0;  float UIMax= 2.0; float UIStep=0.01;  > = {0.15};
#if RAINFX_ENABLE_DISPERSION
float  UIWFXRain_Dispersion       < string UIGroup = "WeatherFX.Rain"; string UIName="|- RainFX - Dispersion";		    float UIMin=0.0;  float UIMax=99.0; float UIStep=0.01;  > = {1.0};
#endif
float  UIWFXRain_DripSpeed		  < string UIGroup = "WeatherFX.Rain"; string UIName="|- RainFX - Dripping Speed";	    float UIMin=0.0;  float UIMax= 5.0; float UIStep=0.001; > = {0.2};
float  UIWFXRain_DripDrift		  < string UIGroup = "WeatherFX.Rain"; string UIName="|- RainFX - Dripping Drift";	    float UIMin=0.0;  float UIMax= 5.0; float UIStep=0.001; > = {0.1};
float  UIWFXRain_Curvature		  < string UIGroup = "WeatherFX.Rain"; string UIName="|- RainFX - Droplet Curvature";   float UIMin=0.0;  float UIMax= 2.0; float UIStep=0.01;  > = {1.0};
float  UIWFXRain_Sigma			  < string UIGroup = "WeatherFX.Rain"; string UIName="|- RainFX - Blur Amount";		    float UIMin=0.0;  float UIMax=20.0; float UIStep=0.01;  > = {0.0};
UI_SPECIAL_WHITESPACE(4)
UI_ELEMENT(ML_FrostFX,							 "|--------- Frost Vignette")
float  UIWFXFrost_PulseRate		  < string UIGroup = "WeatherFX.Frost"; string UIName="|- FrostFX - Pulse Rate";		    float UIMin=0.0;  float UIMax=10.0; float UIStep=0.01; > = {0.1};
float  UIWFXFrost_PulseStrength	  < string UIGroup = "WeatherFX.Frost"; string UIName="|- FrostFX - Pulse Strength";	    float UIMin=0.0;  float UIMax=10.0; float UIStep=0.01; > = {0.5};
float  UIWFXFrost_Curve			  < string UIGroup = "WeatherFX.Frost"; string UIName="|- FrostFX - Curve";			    float UIMin=0.01; float UIMax=15.0; float UIStep=0.01; > = {1.5};
float  UIWFXFrost_RadiusInner	  < string UIGroup = "WeatherFX.Frost"; string UIName="|- FrostFX - Inner Radius";	    float UIMin=0.0;  float UIMax= 2.0; float UIStep=0.01; > = {1.2};
float  UIWFXFrost_RadiusOuter	  < string UIGroup = "WeatherFX.Frost"; string UIName="|- FrostFX - Outer Radius";	    float UIMin=0.0;  float UIMax= 2.0; float UIStep=0.01; > = {0.5};
float3 UIWFXFrost_Tint			  < string UIGroup = "WeatherFX.Frost"; string UIName="|- FrostFX - Tint";			    string UIWidget="Color";							   > = {1.0, 1.0, 1.0};
//UI_SPECIAL_WHITESPACE(5)
#if FROSTFX_USE_REFRACTION_METHOD
float  UIWFXFrost_Opacity         < string UIGroup = "WeatherFX.Frost"; string UIName="|- FrostFX - Opacity";            float UIMin=0.0;  float UIMax=99.0; float UIStep=0.1;  > = {50.0};
float  UIWFXFrost_RefracRange	  < string UIGroup = "WeatherFX.Frost"; string UIName="|- FrostFX - Refraction Range";   float UIMin=0.0;  float UIMax= 1.0; float UIStep=0.01; > = {0.2};
bool   UIWFXFrost_AllowOvershoot  < string UIGroup = "WeatherFX.Frost"; string UIName="|- FrostFX - Approx. missing Data";													   > = {true};
#else
float3 UIWFXFrost_LayerInt		  < string UIGroup = "WeatherFX.Frost"; string UIName="|- FrostFX - Layer Intensity - "; float UIMin=0.0;  float UIMax= 1.0; float UIStep=0.01; > = {1.0, 1.0, 1.0};
#endif


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 3

#ifndef NOTFIRSTTIME
#define NOTFIRSTTIME
UI_ELEMENT(ML_StartBurst,">>>>>>STARBURST LENS FLARES<<<<<<")
UI_WHITESPACE(9)
#endif //NOTFIRSTTIME

float SEPARATE_VAR(UISB_Strength) < string UIGroup = "StarburstFlares"; string UIName=TO_STRING(|- Starburst - TODIE - Intensity);		float UIMin=0.0; float UIMax=100.0; > = {1.0};
float SEPARATE_VAR(UISB_Thresh)   < string UIGroup = "StarburstFlares"; string UIName=TO_STRING(|- Starburst - TODIE - Threshold);		float UIMin=0.1; float UIMax=100.0; > = {5.0};

#ifdef LASTTIME
UI_SPECIAL_WHITESPACE(6)
bool   UISB_HQ			  < string UIGroup = "StarburstFlares"; string UIName="|- Starburst - Enable HQ Mode";														 > = {false};
float  UISB_Shape		  < string UIGroup = "StarburstFlares"; string UIName="|- Starburst - Lens Shape";	     float UIMin=4.0; float UIMax=  9.0; float UIStep=1; > = {0.0};
float  UISB_AngleOffset	  < string UIGroup = "StarburstFlares"; string UIName="|- Starburst - Lens Angle (\xB0)"; float UIMin=0.0; float UIMax=360.0; float UIStep=1; > = {0.0};
float  UISB_Width		  < string UIGroup = "StarburstFlares"; string UIName="|- Starburst - Scale";		     float UIMin=0.0;									 > = {20.0};
float  UISB_ThreshCurve	  < string UIGroup = "StarburstFlares"; string UIName="|- Starburst - Threshold Curve";   float UIMin=0.0; float UIMax= 30.0;				 > = {2.0};
float  UISB_Falloff		  < string UIGroup = "StarburstFlares"; string UIName="|- Starburst - Falloff";		     float UIMin=0.5; float UIMax=  2.0;				 > = {1.0};
float  UISB_Imperfections < string UIGroup = "StarburstFlares"; string UIName="|- Starburst - Imperfections";     float UIMin=0.0; float UIMax= 10.0;				 > = {0.0};
float  UISB_Saturation	  < string UIGroup = "StarburstFlares"; string UIName="|- Starburst - Saturation";	     float UIMin=0.0; float UIMax= 10.0;				 > = {1.5};
float4 UISB_Tint		  < string UIGroup = "StarburstFlares"; string UIName="|- Starburst - Tint";			     string UIWidget = "Color";							 > = {1.0, 1.0, 1.0, 0.0};

#undef LASTTIME
#endif //LASTTIME


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 2

#ifndef NOTFIRSTTIME
#define NOTFIRSTTIME
UI_ELEMENT(ML_AnamFlare,"  >>>>>ANAMORPHIC LENS FLARES<<<<<")
UI_WHITESPACE(5)
#endif //NOTFIRSTTIME

float SEPARATE_VAR(UIAF_Int)    < string UIGroup = "AnamorphicFlares"; string UIName=TO_STRING(|- AnamFlare - TODIE - Intensity); float UIMin=0.0; float UIMax=10.0; > = {1.0};
float SEPARATE_VAR(UIAF_Thresh) < string UIGroup = "AnamorphicFlares"; string UIName=TO_STRING(|- AnamFlare - TODIE - Threshold); float UIMin=0.0; float UIMax=20.0; > = {1.0};

#ifdef LASTTIME
UI_SPECIAL_WHITESPACE(7)
float UIAF_Rotation  < string UIGroup = "AnamorphicFlares"; string UIName="|- AnamFlare - Rotation";	    float UIStep=1.0; float UIMin=0.0; float UIMax=180.0; > = {90.0};
float UIAF_CoreFoc   < string UIGroup = "AnamorphicFlares"; string UIName="|- AnamFlare - Core Focus";				      float UIMin=0.0; float UIMax=20.0;  > = { 0.4};
float UIAF_CoreInt   < string UIGroup = "AnamorphicFlares"; string UIName="|- AnamFlare - Core Intensity";			      float UIMin=0.0; float UIMax=10.0;  > = { 1.5};
float UIAF_CoreMax   < string UIGroup = "AnamorphicFlares"; string UIName="|- AnamFlare - Core Max";					      float UIMin=0.0; float UIMax=10.0;  > = { 2.0};
float UIAF_Width     < string UIGroup = "AnamorphicFlares"; string UIName="|- AnamFlare - Width";						      float UIMin=1.0; float UIMax=100.0; > = {15.0};
float UIAF_Sat	     < string UIGroup = "AnamorphicFlares"; string UIName="|- AnamFlare - Saturation";				      float UIMin=0.0; float UIMax=10.0;  > = { 1.0};
#if ANAM_ENABLE_MIRRORED_FLARE
float UIAF_MirrorInt < string UIGroup = "AnamorphicFlares"; string UIName="|- AnamFlare - Mirror Intensity";			      float UIMin=0.0; float UIMax=10.0;  > = {0.5};
#endif

#undef LASTTIME
#endif //LASTTIME


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 4

#ifndef NOTFIRSTTIME
#define NOTFIRSTTIME
UI_ELEMENT(ML_Dirt,"             >>>>>>LENS DIRT<<<<<<")
UI_WHITESPACE(7)
#endif //NOTFIRSTTIME

float SEPARATE_VAR(UID_Strength) < string UIGroup = "LensDirt"; string UIName=TO_STRING(|- Dirt - TODIE - Intensity); float UIMin=0.0; float UIMax=100.0; > = {0.5};
float SEPARATE_VAR(UID_Power)    < string UIGroup = "LensDirt"; string UIName=TO_STRING(|- Dirt - TODIE - Power);     float UIMin=0.0; float UIMax=100.0; > = {1.5};

#ifdef LASTTIME
UI_SPECIAL_WHITESPACE(8)
int UID_Select < string UIGroup = "LensDirt"; string UIName="|- Dirt - Select Texture"; int UIMin=1; int UIMax=4; > = {1};

#undef LASTTIME
#endif //LASTTIME


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 5
UI_ELEMENT(_CA_Header,">>>>>>CHROMATIC ABERRATION<<<<<<")
UI_WHITESPACE(8)

float UICA_Strength    < string UIGroup = "ChromaticAberration"; string UIName="|- CA - Strength";            float UIMin=-8.0;  float UIMax= 8.0;  float UIStep=0.01; > = {1.0};
float UICA_Curve       < string UIGroup = "ChromaticAberration"; string UIName="|- CA - Curve";               float UIMin= 0.01; float UIMax=10.0;  float UIStep=0.01; > = {1.5};
UI_SPECIAL_WHITESPACE(15)
UI_ELEMENT(_CA_Spectral,                  "|--------- Spectral Dispersion")
float UICA_Spread      < string UIGroup = "ChromaticAberration.Spectral"; string UIName="|- CA - Spectral Spread";    float UIMin= 0.0;  float UIMax= 3.0;  float UIStep=0.01; > = {1.0};
float UICA_Barrel      < string UIGroup = "ChromaticAberration.Spectral"; string UIName="|- CA - Barrel Distortion";   float UIMin=-2.0;  float UIMax= 2.0;  float UIStep=0.01; > = {0.0};
float UICA_FringeSat   < string UIGroup = "ChromaticAberration.Spectral"; string UIName="|- CA - Fringe Saturation";   float UIMin= 0.0;  float UIMax= 2.0;  float UIStep=0.01; > = {1.0};
UI_SPECIAL_WHITESPACE(16)
UI_ELEMENT(_CA_Tuning,                    "|--------- Fine Tuning")
float UICA_RedBias     < string UIGroup = "ChromaticAberration.Tuning"; string UIName="|- CA - Red Channel Bias";    float UIMin=-2.0;  float UIMax= 2.0;  float UIStep=0.01; > = {1.0};
float UICA_BlueBias    < string UIGroup = "ChromaticAberration.Tuning"; string UIName="|- CA - Blue Channel Bias";   float UIMin=-2.0;  float UIMax= 2.0;  float UIStep=0.01; > = {1.0};
float UICA_Deadzone    < string UIGroup = "ChromaticAberration.Tuning"; string UIName="|- CA - Center Deadzone";     float UIMin= 0.0;  float UIMax= 0.5;  float UIStep=0.01; > = {0.05};


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 6

#ifndef NOTFIRSTTIME
#define NOTFIRSTTIME
UI_ELEMENT(ML_Reflections,"      >>>>>>LENS REFLECTIONS<<<<<<")
UI_WHITESPACE(6)
#endif //NOTFIRSTTIME

float SEPARATE_VAR(UIR_Strength) < string UIGroup = "LensReflections"; string UIName=TO_STRING(|- Reflection - TODIE - Intensity); float UIMin=0.0; float UIMax=100.0; > = {5.0};
float SEPARATE_VAR(UIR_Power)    < string UIGroup = "LensReflections"; string UIName=TO_STRING(|- Reflection - TODIE - Power);     float UIMin=0.0; float UIMax=100.0; > = {1.5};

#ifdef LASTTIME
UI_SPECIAL_WHITESPACE(9)
float3 UIR_ColorFilter1 < string UIGroup = "LensReflections"; string UIName="|- Reflection - Color Filter 1"; string UIWidget = "Color"; > = {0.3, 0.4, 0.4};
float3 UIR_ColorFilter2 < string UIGroup = "LensReflections"; string UIName="|- Reflection - Color Filter 2"; string UIWidget = "Color"; > = {0.2, 0.4, 0.5};
float3 UIR_ColorFilter3 < string UIGroup = "LensReflections"; string UIName="|- Reflection - Color Filter 3"; string UIWidget = "Color"; > = {0.5, 0.3, 0.7};
float3 UIR_ColorFilter4 < string UIGroup = "LensReflections"; string UIName="|- Reflection - Color Filter 4"; string UIWidget = "Color"; > = {0.1, 0.2, 0.7};

#undef LASTTIME
#endif //LASTTIME


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 7

#ifndef NOTFIRSTTIME
#define NOTFIRSTTIME
UI_ELEMENT(ML_Ghosts,"     >>>>>>GHOST LENS FLARES<<<<<<")
UI_WHITESPACE(10)
#endif //NOTFIRSTTIME

float SEPARATE_VAR(UIG_Strength) < string UIGroup = "GhostFlares"; string UIName=TO_STRING(|- Ghosts - TODIE - Intensity); float UIMin=0.0; float UIMax=100.0; float UIStep=0.1; > = {1.0};
float SEPARATE_VAR(UIG_Power)    < string UIGroup = "GhostFlares"; string UIName=TO_STRING(|- Ghosts - TODIE - Power);     float UIMin=0.0; float UIMax=100.0;					> = {8.0};

#ifdef LASTTIME
UI_SPECIAL_WHITESPACE(10)
float UIG_FNumber 		< string UIGroup = "GhostFlares"; string UIName="|- Ghosts - Lens F Number";		 	float UIMin=2.0; float UIMax=8.0;				    > = {7.0};
float UIG_Shape 		< string UIGroup = "GhostFlares"; string UIName="|- Ghosts - Lens Shape"; 			float UIMin=4.0; float UIMax=9.0;   float UIStep=1; > = {0.0};
float UIG_AngleOffset	< string UIGroup = "GhostFlares"; string UIName="|- Ghosts - Lens Angle"; 			float UIMin=0.0; float UIMax=360.0; float UIStep=1; > = {0.0};
float UIG_Ratio			< string UIGroup = "GhostFlares"; string UIName="|- Ghosts - Lens Ratio"; 			float UIMin=0.0;								    > = {1.0};
bool  UIG_ForceDeform	< string UIGroup = "GhostFlares"; string UIName="|- Ghosts - Enable Forced Deform";													    > = {false};
float UIG_Deform 		< string UIGroup = "GhostFlares"; string UIName="|- Ghosts - Forced Deform Ratio"; 	float UIMin=0.0; float UIMax=1.0;				    > = {1.0}; //( 0 == circle, 1 == polygon)
float UIG_ShapeWeight	< string UIGroup = "GhostFlares"; string UIName="|- Ghosts - Lens Shape Weight";		float UIMin=0.0; float UIMax=2.0;				    > = {2.0};
float UIG_Grain			< string UIGroup = "GhostFlares"; string UIName="|- Ghosts - Lens Grain"; 			float UIMin=0.0;								    > = {0.2};

#if MULTIIRIS_ENABLE_CHROMATICABERRATION
UI_SPECIAL_WHITESPACE(11)
UI_ELEMENT(ML_CA,				       "|- Ghosts --------- Chromatic Aberration")
float  UIG_CACurve		< string UIGroup = "GhostFlares.CA"; string UIName="|- Ghosts - CA Curve";	float UIMin=0.0;		   > = { 0.8 };
float  UIG_CAScale		< string UIGroup = "GhostFlares.CA"; string UIName="|- Ghosts - CA Scale";							   > = {-1.05};
float3 UIG_CAColor0		< string UIGroup = "GhostFlares.CA"; string UIName="|- Ghosts - CA Color0"; string UIWidget = "Color"; > = {1.0, 0.0, 0.0};
float3 UIG_CAColor1		< string UIGroup = "GhostFlares.CA"; string UIName="|- Ghosts - CA Color1"; string UIWidget = "Color"; > = {0.0, 1.0, 0.0};
float3 UIG_CAColor2		< string UIGroup = "GhostFlares.CA"; string UIName="|- Ghosts - CA Color2"; string UIWidget = "Color"; > = {0.0, 0.0, 1.0};
#endif //MULTIIRIS_ENABLE_CHROMATICABERRATION

UI_SPECIAL_WHITESPACE(12)
UI_ELEMENT(ML_Flare1,			       "|- Ghosts --------- Flares 1")
float4 Intensity1		< string UIGroup = "GhostFlares.Set1"; string UIName="|- Ghosts - Intensity1";					   > = {1.0,  1.0,   1.0, 1.0};
float4 Curve1 			< string UIGroup = "GhostFlares.Set1"; string UIName="|- Ghosts - Curve1";						   > = {1.0,  1.25,  1.0, 1.0};
float4 Scale1 			< string UIGroup = "GhostFlares.Set1"; string UIName="|- Ghosts - Scale1";						   > = {0.0, -0.30, -0.5, 1.0};
float4 Tint1 			< string UIGroup = "GhostFlares.Set1"; string UIName="|- Ghosts - Tint1"; string UIWidget = "Color"; > = {0.25, 0.25,  1.0, 1.0};
UI_SPECIAL_WHITESPACE(13)
UI_ELEMENT(ML_Flare2,			       "|- Ghosts --------- Flares 2")
float4 Intensity2 		< string UIGroup = "GhostFlares.Set2"; string UIName="|- Ghosts - Intensity2";					   > = {1.0,  1.0,  1.0,   1.0};
float4 Curve2 			< string UIGroup = "GhostFlares.Set2"; string UIName="|- Ghosts - Curve2";						   > = {1.4,  1.0,  1.0,   0.7};
float4 Scale2 			< string UIGroup = "GhostFlares.Set2"; string UIName="|- Ghosts - Scale2";						   > = {0.66, 2.0, -0.66, -0.5};
float4 Tint2 			< string UIGroup = "GhostFlares.Set2"; string UIName="|- Ghosts - Tint2"; string UIWidget = "Color"; > = {0.66, 1.0,  1.0,   0.25};
UI_SPECIAL_WHITESPACE(14)
UI_ELEMENT(ML_Flare3,			       "|- Ghosts --------- Flares 3")
float4 Intensity3 		< string UIGroup = "GhostFlares.Set3"; string UIName="|- Ghosts - Intensity3";					   > = {1.0,  1.0,  1.0,   1.0};
float4 Curve3 			< string UIGroup = "GhostFlares.Set3"; string UIName="|- Ghosts - Curve3";						   > = {1.0,  0.8,  1.0,   3.0};
float4 Scale3 			< string UIGroup = "GhostFlares.Set3"; string UIName="|- Ghosts - Scale3";						   > = {3.0, -1.5, -2.0, -20.0};
float4 Tint3 			< string UIGroup = "GhostFlares.Set3"; string UIName="|- Ghosts - Tint3"; string UIWidget = "Color"; > = {1.0,  1.0,  1.0,   0.0};

#undef LASTTIME
#endif //LASTTIME


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 8

#ifndef NOTFIRSTTIME
#define NOTFIRSTTIME
UI_ELEMENT(ML_FilmGrain,"        >>>>>>FILM GRAIN<<<<<<")
UI_WHITESPACE(25)
#endif //NOTFIRSTTIME

float SEPARATE_VAR(UIFG_Intensity) < string UIGroup = "FilmGrain"; string UIName=TO_STRING(|- Film Grain - TODIE - Intensity); float UIMin=0.0; float UIMax=1.0; float UIStep=0.001; > = {0.12};

#ifdef LASTTIME
UI_SPECIAL_WHITESPACE(17)
UI_ELEMENT(ML_FGStructure,                "|--------- Grain Structure")
float  UIFG_Size          < string UIGroup = "FilmGrain.Structure"; string UIName="|- Film Grain - Size";             float UIMin=0.5;  float UIMax=4.0;  float UIStep=0.01; > = {1.0};
float  UIFG_Roughness     < string UIGroup = "FilmGrain.Structure"; string UIName="|- Film Grain - Roughness";        float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.5};
float  UIFG_AnimSpeed     < string UIGroup = "FilmGrain.Structure"; string UIName="|- Film Grain - Animation Speed";  float UIMin=0.0;  float UIMax=2.0;  float UIStep=0.01; > = {1.0};
UI_SPECIAL_WHITESPACE(18)
UI_ELEMENT(ML_FGResponse,                 "|--------- Luminance Response")
float  UIFG_ShadowGrain   < string UIGroup = "FilmGrain.Response"; string UIName="|- Film Grain - Shadow Grain";    float UIMin=0.0;  float UIMax=2.0;  float UIStep=0.01; > = {1.4};
float  UIFG_MidtoneGrain  < string UIGroup = "FilmGrain.Response"; string UIName="|- Film Grain - Midtone Grain";   float UIMin=0.0;  float UIMax=2.0;  float UIStep=0.01; > = {1.0};
float  UIFG_HighlightGrain< string UIGroup = "FilmGrain.Response"; string UIName="|- Film Grain - Highlight Grain";  float UIMin=0.0;  float UIMax=2.0;  float UIStep=0.01; > = {0.3};
UI_SPECIAL_WHITESPACE(19)
UI_ELEMENT(ML_FGEmulsion,                 "|--------- Emulsion Layers")
float3 UIFG_ChannelWeight < string UIGroup = "FilmGrain.Emulsion"; string UIName="|- Film Grain - RGB Weight";       float UIMin=0.0;  float UIMax=2.0;  float UIStep=0.01; > = {0.95, 1.0, 1.15};
float  UIFG_ChannelDecorr < string UIGroup = "FilmGrain.Emulsion"; string UIName="|- Film Grain - Channel Decorrelation"; float UIMin=0.0; float UIMax=1.0; float UIStep=0.01; > = {0.35};

#undef LASTTIME
#endif //LASTTIME


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 9

UI_ELEMENT(ML_LensOptics,"        >>>>>>LENS OPTICS<<<<<<")
UI_WHITESPACE(26)

UI_ELEMENT(_LO_Distort,                    "|--------- Barrel/Pincushion Distortion")
float  UILO_DistortK1     < string UIGroup = "LensOptics.Distortion"; string UIName="|- Optics - Radial K1";          float UIMin=-1.0;  float UIMax=1.0;  float UIStep=0.001; > = {0.0};
float  UILO_DistortK2     < string UIGroup = "LensOptics.Distortion"; string UIName="|- Optics - Radial K2";          float UIMin=-1.0;  float UIMax=1.0;  float UIStep=0.001; > = {0.0};
float  UILO_DistortK3     < string UIGroup = "LensOptics.Distortion"; string UIName="|- Optics - Radial K3";          float UIMin=-0.5;  float UIMax=0.5;  float UIStep=0.001; > = {0.0};
float  UILO_DistortP1     < string UIGroup = "LensOptics.Distortion"; string UIName="|- Optics - Tangential P1";      float UIMin=-0.1;  float UIMax=0.1;  float UIStep=0.001; > = {0.0};
float  UILO_DistortP2     < string UIGroup = "LensOptics.Distortion"; string UIName="|- Optics - Tangential P2";      float UIMin=-0.1;  float UIMax=0.1;  float UIStep=0.001; > = {0.0};
UI_SPECIAL_WHITESPACE(20)
UI_ELEMENT(_LO_Vignette,                   "|--------- Optical Vignetting")
float  UILO_VigNatural    < string UIGroup = "LensOptics.Vignette"; string UIName="|- Optics - Natural Vignette (cos4)"; float UIMin=0.0; float UIMax=3.0; float UIStep=0.01; > = {0.0};
float  UILO_VigMechanical < string UIGroup = "LensOptics.Vignette"; string UIName="|- Optics - Mechanical Vignette";float UIMin=0.0;  float UIMax=2.0;  float UIStep=0.01; > = {0.0};
float  UILO_VigMechRatio  < string UIGroup = "LensOptics.Vignette"; string UIName="|- Optics - Mech. Aspect Ratio"; float UIMin=0.5;  float UIMax=2.0;  float UIStep=0.01; > = {1.0};
float  UILO_VigRoundness  < string UIGroup = "LensOptics.Vignette"; string UIName="|- Optics - Mech. Roundness";    float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.8};
float3 UILO_VigColorShift < string UIGroup = "LensOptics.Vignette"; string UIName="|- Optics - Edge Color Shift";   string UIWidget="Color";                                > = {1.0, 0.95, 0.85};
float  UILO_VigColorAmt   < string UIGroup = "LensOptics.Vignette"; string UIName="|- Optics - Color Shift Amount"; float UIMin=0.0;  float UIMax=2.0;  float UIStep=0.01; > = {0.0};
UI_SPECIAL_WHITESPACE(21)
UI_ELEMENT(_LO_Coma,                       "|--------- Coma Aberration")
float  UILO_ComaStrength  < string UIGroup = "LensOptics.Coma"; string UIName="|- Optics - Coma Strength";      float UIMin=0.0;  float UIMax=5.0;  float UIStep=0.01; > = {0.0};
float  UILO_ComaFalloff   < string UIGroup = "LensOptics.Coma"; string UIName="|- Optics - Coma Falloff";       float UIMin=0.5;  float UIMax=5.0;  float UIStep=0.01; > = {2.5};
float  UILO_ComaSamples   < string UIGroup = "LensOptics.Coma"; string UIName="|- Optics - Coma Quality";       float UIMin=3.0;  float UIMax=12.0; float UIStep=1.0;  > = {6.0};
UI_SPECIAL_WHITESPACE(22)
UI_ELEMENT(_LO_Spherical,                  "|--------- Spherical Aberration")
float  UILO_SphericalStr  < string UIGroup = "LensOptics.Spherical"; string UIName="|- Optics - Spherical Strength"; float UIMin=0.0;  float UIMax=3.0;  float UIStep=0.01; > = {0.0};
float  UILO_SphericalBias < string UIGroup = "LensOptics.Spherical"; string UIName="|- Optics - Spherical Luma Bias";float UIMin=0.0;  float UIMax=5.0;  float UIStep=0.01; > = {1.5};
float  UILO_SphericalRadius< string UIGroup = "LensOptics.Spherical"; string UIName="|- Optics - Spherical Radius";  float UIMin=0.5;  float UIMax=8.0;  float UIStep=0.1;  > = {2.0};
UI_SPECIAL_WHITESPACE(23)
UI_ELEMENT(_LO_FieldCurve,                 "|--------- Field Curvature")
float  UILO_FieldCurveStr < string UIGroup = "LensOptics.FieldCurve"; string UIName="|- Optics - Field Curvature";    float UIMin=0.0;  float UIMax=3.0;  float UIStep=0.01; > = {0.0};
float  UILO_FieldCurveExp < string UIGroup = "LensOptics.FieldCurve"; string UIName="|- Optics - Curvature Falloff";  float UIMin=1.0;  float UIMax=5.0;  float UIStep=0.01; > = {2.0};


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 10

#ifndef NOTFIRSTTIME_HL
#define NOTFIRSTTIME_HL
UI_ELEMENT(ML_Halation,"          >>>>>>HALATION<<<<<<")
UI_WHITESPACE(27)
#endif //NOTFIRSTTIME_HL

float SEPARATE_VAR(UIHL_Intensity) < string UIGroup = "Halation"; string UIName=TO_STRING(|- Halation - TODIE - Intensity); float UIMin=0.0; float UIMax=2.0; float UIStep=0.01; > = {0.3};

#ifdef LASTTIME
UI_SPECIAL_WHITESPACE(24)
UI_ELEMENT(_HL_Settings,                   "|--------- Halation Settings")
float  UIHL_Threshold     < string UIGroup = "Halation.Settings"; string UIName="|- Halation - Threshold";        float UIMin=0.0;  float UIMax=5.0;  float UIStep=0.01; > = {0.8};
float  UIHL_ThreshCurve   < string UIGroup = "Halation.Settings"; string UIName="|- Halation - Threshold Curve";  float UIMin=0.1;  float UIMax=5.0;  float UIStep=0.01; > = {2.0};
float  UIHL_Radius        < string UIGroup = "Halation.Settings"; string UIName="|- Halation - Spread Radius";    float UIMin=1.0;  float UIMax=32.0; float UIStep=0.5;  > = {8.0};
float3 UIHL_Color         < string UIGroup = "Halation.Settings"; string UIName="|- Halation - Color Tint";       string UIWidget="Color";                                > = {1.0, 0.35, 0.15};
float  UIHL_Saturation    < string UIGroup = "Halation.Settings"; string UIName="|- Halation - Saturation";       float UIMin=0.0;  float UIMax=2.0;  float UIStep=0.01; > = {0.8};
UI_SPECIAL_WHITESPACE(25)
UI_ELEMENT(_VG_Settings,                   "|--------- Veiling Glare")
float  UIVG_Intensity     < string UIGroup = "Halation.VeilingGlare"; string UIName="|- Veiling Glare - Intensity";   float UIMin=0.0;  float UIMax=2.0;  float UIStep=0.01; > = {0.15};
float  UIVG_Threshold     < string UIGroup = "Halation.VeilingGlare"; string UIName="|- Veiling Glare - Threshold";   float UIMin=0.0;  float UIMax=5.0;  float UIStep=0.01; > = {1.2};
float  UIVG_Radius        < string UIGroup = "Halation.VeilingGlare"; string UIName="|- Veiling Glare - Spread";      float UIMin=2.0;  float UIMax=64.0; float UIStep=1.0;  > = {24.0};
float3 UIVG_Tint          < string UIGroup = "Halation.VeilingGlare"; string UIName="|- Veiling Glare - Tint";        string UIWidget="Color";                                > = {1.0, 0.98, 0.92};

#undef LASTTIME
#endif //LASTTIME


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 11

UI_ELEMENT(ML_Sensor,"      >>>>>>SENSOR RESPONSE<<<<<<")
UI_WHITESPACE(28)

UI_ELEMENT(_SR_Curve,                      "|--------- Film Response Curve")
float  UISR_Shoulder      < string UIGroup = "SensorResponse.Curve"; string UIName="|- Sensor - Highlight Shoulder"; float UIMin=0.0;  float UIMax=3.0;  float UIStep=0.01; > = {0.0};
float  UISR_ShoulderStart < string UIGroup = "SensorResponse.Curve"; string UIName="|- Sensor - Shoulder Start";     float UIMin=0.3;  float UIMax=2.0;  float UIStep=0.01; > = {0.8};
float  UISR_Toe           < string UIGroup = "SensorResponse.Curve"; string UIName="|- Sensor - Shadow Toe";         float UIMin=0.0;  float UIMax=2.0;  float UIStep=0.01; > = {0.0};
float  UISR_ToeEnd        < string UIGroup = "SensorResponse.Curve"; string UIName="|- Sensor - Toe End";            float UIMin=0.01; float UIMax=0.5;  float UIStep=0.01; > = {0.1};
UI_SPECIAL_WHITESPACE(26)
UI_ELEMENT(_SR_Crosstalk,                  "|--------- Channel Crosstalk")
float  UISR_Crosstalk     < string UIGroup = "SensorResponse.Crosstalk"; string UIName="|- Sensor - Crosstalk Amount";   float UIMin=0.0;  float UIMax=0.5;  float UIStep=0.01; > = {0.0};
float  UISR_CrossRtoG     < string UIGroup = "SensorResponse.Crosstalk"; string UIName="|- Sensor - Red to Green Bleed"; float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.3};
float  UISR_CrossGtoB     < string UIGroup = "SensorResponse.Crosstalk"; string UIName="|- Sensor - Green to Blue Bleed";float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.25};
float  UISR_CrossBtoR     < string UIGroup = "SensorResponse.Crosstalk"; string UIName="|- Sensor - Blue to Red Bleed";  float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.15};
UI_SPECIAL_WHITESPACE(27)
UI_ELEMENT(_SR_Noise,                      "|--------- Sensor Noise")
float  UISR_ReadNoise     < string UIGroup = "SensorResponse.Noise"; string UIName="|- Sensor - Read Noise";         float UIMin=0.0;  float UIMax=0.05; float UIStep=0.001;> = {0.0};
float  UISR_HotPixels     < string UIGroup = "SensorResponse.Noise"; string UIName="|- Sensor - Hot Pixel Rate";     float UIMin=0.0;  float UIMax=0.01; float UIStep=0.0001;> = {0.0};
float  UISR_PhotonNoise   < string UIGroup = "SensorResponse.Noise"; string UIName="|- Sensor - Photon (Shot) Noise";float UIMin=0.0;  float UIMax=0.1;  float UIStep=0.001;> = {0.0};



//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 99

//----------------------------------------------------------------------------------------------//
//                     SKYRIMBRIDGE LENS ENHANCEMENTS                                           //
//----------------------------------------------------------------------------------------------//
// Requires SkyrimBridge SKSE plugin + SkyrimBridge.fxh in Helper/                              //
//----------------------------------------------------------------------------------------------//

#ifdef SKYRIMBRIDGE_FXH

UI_ELEMENT(SB_LENS_Header,     "    >>>>>>SKYRIMBRIDGE LENS<<<<<<")

//--- Weather FX ---
int SB_LENS_Spacer0   <string UIName="|--------- Weather Integration"; int UIMin=0; int UIMax=0;> = {0};
float UIBRIDGE_RainFromWeather  < string UIGroup = "SB.Lens"; string UIName="|- SB - Rain Intensity From Weather";    float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UIBRIDGE_FrostFromSnow    < string UIGroup = "SB.Lens"; string UIName="|- SB - Frost From Snow Weather";         float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UIBRIDGE_InteriorAccurate < string UIGroup = "SB.Lens"; string UIName="|- SB - Accurate Interior Detection";     float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};

//--- Flare Tinting ---
int SB_LENS_Spacer1   <string UIName="|--------- Flare & Dirt"; int UIMin=0; int UIMax=0;> = {0};
float UIBRIDGE_SunFlare         < string UIGroup = "SB.Lens"; string UIName="|- SB - Sun Color Flare Tinting";         float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UIBRIDGE_SunFlareBlend    < string UIGroup = "SB.Lens"; string UIName="|- SB - Flare Sun Color Blend";           float UIMin= 0.0; float UIMax= 1.0; > = { 0.4};
float UIBRIDGE_WetDirt          < string UIGroup = "SB.Lens"; string UIName="|- SB - Rain Wet Lens Dirt";              float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UIBRIDGE_WetDirtMult      < string UIGroup = "SB.Lens"; string UIName="|- SB - Wet Dirt Intensity Mult";         float UIMin= 1.0; float UIMax= 3.0; > = { 1.5};


//--- Damage Screen FX ---
int SB_LENS_Spacer2   <string UIName="|--------- Damage Screen FX"; int UIMin=0; int UIMax=0;> = {0};
float UIBRIDGE_DamageFX       < string UIGroup = "SB.Lens"; string UIName="|- SB - Elemental Damage Screen FX";    float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UIBRIDGE_DmgFireInt     < string UIGroup = "SB.Lens"; string UIName="|- SB - Fire Damage Intensity";          float UIMin= 0.0; float UIMax= 1.0; > = { 0.5};
float UIBRIDGE_DmgFrostInt    < string UIGroup = "SB.Lens"; string UIName="|- SB - Frost Damage Intensity";         float UIMin= 0.0; float UIMax= 1.0; > = { 0.4};
float UIBRIDGE_DmgShockInt    < string UIGroup = "SB.Lens"; string UIName="|- SB - Shock Damage Intensity";         float UIMin= 0.0; float UIMax= 1.0; > = { 0.6};

//--- Player Vitals FX ---
int SB_LENS_Spacer3   <string UIName="|--------- Player Vitals"; int UIMin=0; int UIMax=0;> = {0};
float UIBRIDGE_HealthVignette < string UIGroup = "SB.Lens"; string UIName="|- SB - Low-Health Blood Vignette";      float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UIBRIDGE_HealthThresh   < string UIGroup = "SB.Lens"; string UIName="|- SB - Health Vignette Threshold";      float UIMin= 0.1; float UIMax= 0.6; > = { 0.3};
float UIBRIDGE_BleedoutOverlay< string UIGroup = "SB.Lens"; string UIName="|- SB - Bleedout Blood Overlay";         float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UIBRIDGE_StaminaDesat   < string UIGroup = "SB.Lens"; string UIName="|- SB - Low-Stamina Desaturation";       float UIMin= 0.0; float UIMax= 1.0; > = { 0.0};

//--- Atmospheric FX ---
int SB_LENS_Spacer4   <string UIName="|--------- Atmospheric FX"; int UIMin=0; int UIMax=0;> = {0};
float UIBRIDGE_LightningFlash < string UIGroup = "SB.Lens"; string UIName="|- SB - Lightning Screen Flash";         float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UIBRIDGE_LightningInt   < string UIGroup = "SB.Lens"; string UIName="|- SB - Lightning Flash Intensity";      float UIMin= 0.0; float UIMax= 2.0; > = { 0.6};
float UIBRIDGE_TorchGlow      < string UIGroup = "SB.Lens"; string UIName="|- SB - Torch Warm Lens Glow";           float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UIBRIDGE_TorchGlowInt   < string UIGroup = "SB.Lens"; string UIName="|- SB - Torch Glow Intensity";           float UIMin= 0.0; float UIMax= 0.5; > = { 0.08};
float UIBRIDGE_WindRain        < string UIGroup = "SB.Lens"; string UIName="|- SB - Wind-Directed Rain Streaks";     float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UIBRIDGE_SnowLens        < string UIGroup = "SB.Lens"; string UIName="|- SB - Snow Lens Accumulation";         float UIMin= 0.0; float UIMax= 1.0; > = { 0.0};

//--- Intoxication & Magic ---
int SB_LENS_Spacer5   <string UIName="|--------- Intoxication & Magic"; int UIMin=0; int UIMax=0;> = {0};
float UIBRIDGE_DrunkFX        < string UIGroup = "SB.Lens"; string UIName="|- SB - Drunk/Skooma Distortion";        float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UIBRIDGE_DrunkStrength  < string UIGroup = "SB.Lens"; string UIName="|- SB - Drunk Blur Strength";            float UIMin= 0.0; float UIMax= 2.0; > = { 0.5};
float UIBRIDGE_NightEyeTint   < string UIGroup = "SB.Lens"; string UIName="|- SB - Night Eye Green Tint";           float UIMin= 0.0; float UIMax= 1.0; > = { 0.0};
float UIBRIDGE_EtherealFade   < string UIGroup = "SB.Lens"; string UIName="|- SB - Ethereal Desaturation";          float UIMin= 0.0; float UIMax= 1.0; > = { 0.0};

#endif //SKYRIMBRIDGE_FXH




////////////////////////////////////////////////////////////////////////////////
//  ATMOSPHERIC FOG (merged from enbUI_Fog.fxh)
////////////////////////////////////////////////////////////////////////////////

#elif SHADERGROUP == 12

//--- Master Enable: declared in enbUI_Lens.fxh (UIFOG_Enable) ---
int FogHeader   <string UIName="      >>>>>>ATMOSPHERIC FOG<<<<<<"; int UIMin=0; int UIMax=0;> = {0};

//--- Distance Controls ---
int FogSpacer0  <string UIName="|--------- Distance"; int UIMin=0; int UIMax=0;> = {0};
float  UIFOG_StartDist    < string UIGroup = "AtmoFog.Distance"; string UIName="|- Start Distance";                  float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.001; > = {0.5};
float  UIFOG_EndDist      < string UIGroup = "AtmoFog.Distance"; string UIName="|- End Distance";                    float UIMin=0.01; float UIMax=1.0;  float UIStep=0.001; > = {0.995};
float  UIFOG_DepthPower   < string UIGroup = "AtmoFog.Distance"; string UIName="|- Depth Power";                     float UIMin=0.1;  float UIMax=5.0;  float UIStep=0.01;  > = {1.0};
float  UIFOG_Curve        < string UIGroup = "AtmoFog.Distance"; string UIName="|- Density Curve";                   float UIMin=0.1;  float UIMax=10.0; float UIStep=0.01;  > = {1.5};
float  UIFOG_MaxFog       < string UIGroup = "AtmoFog.Distance"; string UIName="|- Maximum Opacity";                 float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01;  > = {0.85};

//--- Height Fog ---
int FogSpacer1  <string UIName="|--------- Height Fog"; int UIMin=0; int UIMax=0;> = {0};
bool   UIFOG_HeightEnable < string UIGroup = "AtmoFog.Height"; string UIName="|- Enable Height Fog";                                                                         > = {true};
float  UIFOG_HeightFalloff< string UIGroup = "AtmoFog.Height"; string UIName="|- Height Falloff";                  float UIMin=0.001;float UIMax=0.1;  float UIStep=0.001; > = {0.015};
float  UIFOG_HeightMix    < string UIGroup = "AtmoFog.Height"; string UIName="|- Height Influence";                float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01;  > = {0.50};

//--- Sky Color Sampling ---
int FogSpacer2  <string UIName="|--------- Sky Sampling"; int UIMin=0; int UIMax=0;> = {0};
float  UIFOG_SkyThreshold < string UIGroup = "AtmoFog.Sky"; string UIName="|- Sky Depth Threshold";             float UIMin=0.99; float UIMax=1.0;  float UIStep=0.0001;> = {0.9998};
float  UIFOG_SkySampleY   < string UIGroup = "AtmoFog.Sky"; string UIName="|- Sky Sample Height";               float UIMin=0.0;  float UIMax=0.5;  float UIStep=0.01;  > = {0.25};
float  UIFOG_SkySpread    < string UIGroup = "AtmoFog.Sky"; string UIName="|- Sky Sample Spread";               float UIMin=0.01; float UIMax=0.5;  float UIStep=0.01;  > = {0.15};
float  UIFOG_SkyDesaturate< string UIGroup = "AtmoFog.Sky"; string UIName="|- Sky Desaturation";                float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01;  > = {0.30};

//--- Bloom Source ---
int FogSpacer3  <string UIName="|--------- Bloom Color Source"; int UIMin=0; int UIMax=0;> = {0};
float  UIFOG_BloomMix     < string UIGroup = "AtmoFog.Sky"; string UIName="|- Bloom Mix Ratio";                 float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01;  > = {0.15};
float  UIFOG_BloomDesat   < string UIGroup = "AtmoFog.Sky"; string UIName="|- Bloom Desaturation";              float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01;  > = {0.30};


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 13

float  SEPARATE_VAR(UIFOG_Density)   < string UIGroup = "AtmoFog.Sky"; string UIName=TO_STRING(|- TODIE - Fog Density);          float UIMin=0.0;  float UIMax=0.05; float UIStep=0.0005;> = {0.005};
float  SEPARATE_VAR(UIFOG_Intensity) < string UIGroup = "AtmoFog.Sky"; string UIName=TO_STRING(|- TODIE - Fog Intensity);         float UIMin=0.0;  float UIMax=5.0;  float UIStep=0.01;  > = {1.0};
float3 SEPARATE_VAR(UIFOG_Tint)      < string UIGroup = "AtmoFog.Sky"; string UIName=TO_STRING(|- TODIE - Fog Tint);              string UIWidget="Color";                                > = {1.0, 1.0, 1.0};
float  SEPARATE_VAR(UIFOG_TintWt)   < string UIGroup = "AtmoFog.Sky"; string UIName=TO_STRING(|- TODIE - Tint Weight);           float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01;  > = {0.0};
float  SEPARATE_VAR(UIFOG_Bright)    < string UIGroup = "AtmoFog.Sky"; string UIName=TO_STRING(|- TODIE - Brightness);            float UIMin=0.0;  float UIMax=5.0;  float UIStep=0.01;  > = {1.0};
float  SEPARATE_VAR(UIFOG_Scatter)   < string UIGroup = "AtmoFog.Sky"; string UIName=TO_STRING(|- TODIE - Inscatter);             float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01;  > = {0.15};
float  SEPARATE_VAR(UIFOG_HeightBase)< string UIGroup = "AtmoFog.Sky"; string UIName=TO_STRING(|- TODIE - Height Baseline);       float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01;  > = {0.50};



////////////////////////////////////////////////////////////////////////////////
//  CRT DISPLAY (merged from enbUI_CRT.fxh)
////////////////////////////////////////////////////////////////////////////////

#elif SHADERGROUP == 14

int CRTHeader   <string UIName="      >>>>>>CRT DISPLAY<<<<<<"; int UIMin=0; int UIMax=0;> = {0};

//--- Scanlines ---
int CRTSpacer0   <string UIName="|--------- Scanlines"; int UIMin=0; int UIMax=0;> = {0};
float  UICRT_ScanIntensity < string UIGroup = "CRT"; string UIName="|- CRT - Scanline Intensity";    float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.25};
float  UICRT_ScanWidth     < string UIGroup = "CRT"; string UIName="|- CRT - Scanline Width";        float UIMin=0.2;  float UIMax=2.0;  float UIStep=0.01; > = {1.0};
float  UICRT_ScanBrightBoost < string UIGroup = "CRT"; string UIName="|- CRT - Bright Line Boost";   float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.15};

//--- Phosphor Mask ---
int CRTSpacer1   <string UIName="|--------- Phosphor Mask"; int UIMin=0; int UIMax=0;> = {0};
float  UICRT_MaskIntensity < string UIGroup = "CRT"; string UIName="|- CRT - Mask Intensity";        float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.15};
int    UICRT_MaskType      < string UIGroup = "CRT"; string UIName="|- CRT - Mask (0=Aperture 1=Slot 2=Shadow)"; int UIMin=0; int UIMax=2;                 > = {0};
float  UICRT_MaskScale     < string UIGroup = "CRT"; string UIName="|- CRT - Mask Scale";            float UIMin=0.5;  float UIMax=4.0;  float UIStep=0.1;  > = {1.0};

//--- Screen Shape ---
int CRTSpacer2   <string UIName="|--------- Screen Shape"; int UIMin=0; int UIMax=0;> = {0};
float  UICRT_Curvature     < string UIGroup = "CRT"; string UIName="|- CRT - Screen Curvature";      float UIMin=0.0;  float UIMax=0.5;  float UIStep=0.01; > = {0.03};
float  UICRT_CornerRadius  < string UIGroup = "CRT"; string UIName="|- CRT - Corner Radius";         float UIMin=0.0;  float UIMax=0.15; float UIStep=0.005;> = {0.03};
float  UICRT_Overscan      < string UIGroup = "CRT"; string UIName="|- CRT - Overscan";              float UIMin=0.0;  float UIMax=0.1;  float UIStep=0.005;> = {0.01};

//--- Bloom & Color ---
int CRTSpacer3   <string UIName="|--------- Bloom & Color"; int UIMin=0; int UIMax=0;> = {0};
float  UICRT_Bloom         < string UIGroup = "CRT"; string UIName="|- CRT - Phosphor Bloom";        float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.10};
float  UICRT_Saturation    < string UIGroup = "CRT"; string UIName="|- CRT - Color Saturation";      float UIMin=0.5;  float UIMax=1.5;  float UIStep=0.01; > = {1.05};
float  UICRT_Brightness    < string UIGroup = "CRT"; string UIName="|- CRT - Brightness";            float UIMin=0.5;  float UIMax=1.5;  float UIStep=0.01; > = {1.0};
float  UICRT_Contrast      < string UIGroup = "CRT"; string UIName="|- CRT - Contrast";              float UIMin=0.5;  float UIMax=2.0;  float UIStep=0.01; > = {1.05};



////////////////////////////////////////////////////////////////////////////////
//  CINEMATIC FX (merged from enbUI_CinematicFX.fxh)
////////////////////////////////////////////////////////////////////////////////

#elif SHADERGROUP == 15

int DiffHeader   <string UIName="      >>>>>>LENS DIFFUSION v2<<<<<<"; int UIMin=0; int UIMax=0;> = {0};

float  UIDIFF_Strength     < string UIGroup = "CineFX.LensDiffusion"; string UIName="|- Diffusion Strength";           float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.25};
float  UIDIFF_HighlightBias < string UIGroup = "CineFX.LensDiffusion"; string UIName="|- Highlight Bias";              float UIMin=0.5;  float UIMax=4.0;  float UIStep=0.01; > = {1.5};
float  UIDIFF_HighlightRetain < string UIGroup = "CineFX.LensDiffusion"; string UIName="|- Highlight Retention";       float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.50};
float  UIDIFF_BlackLift    < string UIGroup = "CineFX.LensDiffusion"; string UIName="|- Black Lift";                   float UIMin=0.0;  float UIMax=0.15; float UIStep=0.001;> = {0.02};
float  UIDIFF_Desaturate   < string UIGroup = "CineFX.LensDiffusion"; string UIName="|- Diffusion Desaturation";       float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.30};
float  UIDIFF_LocalRadius  < string UIGroup = "CineFX.LensDiffusion"; string UIName="|- Local Blur Radius (px)";       float UIMin=0.0;  float UIMax=6.0;  float UIStep=0.1;  > = {2.0};
float  UIDIFF_LocalWeight  < string UIGroup = "CineFX.LensDiffusion"; string UIName="|- Local vs Wide Blur Mix";       float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.35};
float3 UIDIFF_Tint         < string UIGroup = "CineFX.LensDiffusion"; string UIName="|- Diffusion Tint";               string UIWidget="Color";                               > = {1.0, 0.98, 0.95};


//========================= SHADERGROUP 16 — FILM HALATION ====================================//
#elif SHADERGROUP == 16

int FHaloHeader  <string UIName="      >>>>>>FILM HALATION v2<<<<<<"; int UIMin=0; int UIMax=0;> = {0};

float  UIFHALO_Strength    < string UIGroup = "CineFX.FilmHalation"; string UIName="|- Halation Strength";            float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.20};
float  UIFHALO_Threshold   < string UIGroup = "CineFX.FilmHalation"; string UIName="|- Brightness Threshold";         float UIMin=0.0;  float UIMax=2.0;  float UIStep=0.01; > = {0.60};
float  UIFHALO_Knee        < string UIGroup = "CineFX.FilmHalation"; string UIName="|- Threshold Knee (softness)";    float UIMin=0.01; float UIMax=1.0;  float UIStep=0.01; > = {0.25};
float  UIFHALO_SpreadMix   < string UIGroup = "CineFX.FilmHalation"; string UIName="|- Wide vs Narrow Spread";        float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.65};
float  UIFHALO_WaveSpread  < string UIGroup = "CineFX.FilmHalation"; string UIName="|- Wavelength Spread (R > B)";    float UIMin=0.5;  float UIMax=3.0;  float UIStep=0.01; > = {1.50};
float  UIFHALO_Desaturate  < string UIGroup = "CineFX.FilmHalation"; string UIName="|- Halation Desaturation";        float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.40};
float3 UIFHALO_Tint        < string UIGroup = "CineFX.FilmHalation"; string UIName="|- Halation Color (film base)";   string UIWidget="Color";                               > = {1.0, 0.75, 0.50};


//========================= SHADERGROUP 17 — LIGHT LEAKS ======================================//
#elif SHADERGROUP == 17

int LeakHeader   <string UIName="      >>>>>>LIGHT LEAKS v2<<<<<<"; int UIMin=0; int UIMax=0;> = {0};

float  UILEAK_Intensity    < string UIGroup = "CineFX.LightLeaks"; string UIName="|- Leak Intensity";               float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.15};
float  UILEAK_Speed        < string UIGroup = "CineFX.LightLeaks"; string UIName="|- Animation Speed";              float UIMin=0.05; float UIMax=3.0;  float UIStep=0.05; > = {0.40};
float  UILEAK_Coverage     < string UIGroup = "CineFX.LightLeaks"; string UIName="|- Coverage (how much screen)";   float UIMin=0.1;  float UIMax=1.0;  float UIStep=0.01; > = {0.45};
float  UILEAK_EdgeBias     < string UIGroup = "CineFX.LightLeaks"; string UIName="|- Edge Bias";                    float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.60};
float  UILEAK_Softness     < string UIGroup = "CineFX.LightLeaks"; string UIName="|- Softness";                     float UIMin=0.5;  float UIMax=5.0;  float UIStep=0.1;  > = {2.0};
float  UILEAK_SceneAdapt   < string UIGroup = "CineFX.LightLeaks"; string UIName="|- Scene Brightness Adapt";       float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.30};
float3 UILEAK_Color1       < string UIGroup = "CineFX.LightLeaks"; string UIName="|- Warm Color A";                 string UIWidget="Color";                               > = {1.0, 0.65, 0.20};
float3 UILEAK_Color2       < string UIGroup = "CineFX.LightLeaks"; string UIName="|- Warm Color B";                 string UIWidget="Color";                               > = {0.95, 0.35, 0.55};
int    UILEAK_BlendMode    < string UIGroup = "CineFX.LightLeaks"; string UIName="|- Blend (0=Screen 1=Add 2=Softlight)"; int UIMin=0; int UIMax=2;                       > = {0};


//========================= SHADERGROUP 18 — GATE WEAVE =======================================//
#elif SHADERGROUP == 18

int WeaveHeader  <string UIName="      >>>>>>GATE WEAVE v2<<<<<<"; int UIMin=0; int UIMax=0;> = {0};

float  UIWEAVE_AmplitudeX  < string UIGroup = "CineFX.GateWeave"; string UIName="|- Horizontal Amplitude (px)";   float UIMin=0.0;  float UIMax=3.0;  float UIStep=0.01; > = {0.40};
float  UIWEAVE_AmplitudeY  < string UIGroup = "CineFX.GateWeave"; string UIName="|- Vertical Amplitude (px)";     float UIMin=0.0;  float UIMax=3.0;  float UIStep=0.01; > = {0.60};
float  UIWEAVE_Speed       < string UIGroup = "CineFX.GateWeave"; string UIName="|- Weave Speed";                 float UIMin=0.1;  float UIMax=5.0;  float UIStep=0.1;  > = {1.0};
float  UIWEAVE_Rotation    < string UIGroup = "CineFX.GateWeave"; string UIName="|- Rotational Jitter (deg)";     float UIMin=0.0;  float UIMax=0.5;  float UIStep=0.01; > = {0.05};
float  UIWEAVE_Breathe     < string UIGroup = "CineFX.GateWeave"; string UIName="|- Breathing (zoom jitter)";     float UIMin=0.0;  float UIMax=0.01; float UIStep=0.0001;> = {0.001};
float  UIWEAVE_MotionBlur  < string UIGroup = "CineFX.GateWeave"; string UIName="|- Motion Blur from Weave";      float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.30};
float  UIWEAVE_ExpJitter   < string UIGroup = "CineFX.GateWeave"; string UIName="|- Per-Frame Exposure Jitter";   float UIMin=0.0;  float UIMax=0.10; float UIStep=0.001;> = {0.02};


//========================= SHADERGROUP 19 — CINEMATIC LETTERBOX ==============================//
#elif SHADERGROUP == 19

int LboxHeader   <string UIName="      >>>>>>LETTERBOX v2<<<<<<"; int UIMin=0; int UIMax=0;> = {0};

int    UILBOX_Ratio        < string UIGroup = "CineFX.Letterbox"; string UIName="|- Ratio (0=2.39 1=2.00 2=1.85 3=1.66 4=4:3 5=Custom)"; int UIMin=0; int UIMax=5;     > = {0};
float  UILBOX_CustomRatio  < string UIGroup = "CineFX.Letterbox"; string UIName="|- Custom Aspect Ratio";          float UIMin=1.0;  float UIMax=3.5;  float UIStep=0.01; > = {2.39};
float  UILBOX_Opacity      < string UIGroup = "CineFX.Letterbox"; string UIName="|- Bar Opacity";                  float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {1.0};
float  UILBOX_EdgeSoftness < string UIGroup = "CineFX.Letterbox"; string UIName="|- Bar Edge Softness";            float UIMin=0.0;  float UIMax=0.05; float UIStep=0.001;> = {0.005};
float3 UILBOX_BarColor     < string UIGroup = "CineFX.Letterbox"; string UIName="|- Bar Color";                    string UIWidget="Color";                               > = {0.0, 0.0, 0.0};


//========================= SHADERGROUP 20 — ANAMORPHIC LENS ==================================//
#elif SHADERGROUP == 20

int AnamHeader   <string UIName="      >>>>>>ANAMORPHIC LENS v2<<<<<<"; int UIMin=0; int UIMax=0;> = {0};

float  UIANAM_HBlur        < string UIGroup = "CineFX.AnamorphicLens"; string UIName="|- Horizontal Blur (reduced MTF)";  float UIMin=0.0;  float UIMax=4.0;  float UIStep=0.01; > = {1.5};
float  UIANAM_FieldCurve   < string UIGroup = "CineFX.AnamorphicLens"; string UIName="|- Field Curvature (edge blur+)";   float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.20};
float  UIANAM_ChromaH      < string UIGroup = "CineFX.AnamorphicLens"; string UIName="|- Horizontal Chroma Aberration";   float UIMin=0.0;  float UIMax=3.0;  float UIStep=0.01; > = {0.50};
float  UIANAM_Mumps        < string UIGroup = "CineFX.AnamorphicLens"; string UIName="|- Edge Stretch (mumps)";           float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.15};
float  UIANAM_Breathe      < string UIGroup = "CineFX.AnamorphicLens"; string UIName="|- Focus Breathing Amount";         float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.15};
float  UIANAM_BreatheSpeed < string UIGroup = "CineFX.AnamorphicLens"; string UIName="|- Breathing Speed";                float UIMin=0.1;  float UIMax=3.0;  float UIStep=0.1;  > = {0.50};
float  UIANAM_Streak       < string UIGroup = "CineFX.AnamorphicLens"; string UIName="|- Vertical Streak (highlight)";    float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.20};


//========================= SHADERGROUP 21 — OPTICAL VIGNETTE ================================//
#elif SHADERGROUP == 21

int VigHeader    <string UIName="      >>>>>>OPTICAL VIGNETTE v2<<<<<<"; int UIMin=0; int UIMax=0;> = {0};

float  UIVIG_Strength      < string UIGroup = "CineFX.OpticalVignette"; string UIName="|- Vignette Strength";              float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.50};
float  UIVIG_Softness      < string UIGroup = "CineFX.OpticalVignette"; string UIName="|- Plateau (center flat zone)";     float UIMin=0.0;  float UIMax=0.95; float UIStep=0.01; > = {0.55};
float  UIVIG_Power         < string UIGroup = "CineFX.OpticalVignette"; string UIName="|- Falloff Steepness";              float UIMin=1.0;  float UIMax=12.0; float UIStep=0.1;  > = {3.5};
float  UIVIG_Roundness     < string UIGroup = "CineFX.OpticalVignette"; string UIName="|- Roundness (< 1 = wide)";        float UIMin=0.3;  float UIMax=2.0;  float UIStep=0.01; > = {0.85};
float  UIVIG_CatEye        < string UIGroup = "CineFX.OpticalVignette"; string UIName="|- Cat-Eye Shape at Edges";         float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.30};
float  UIVIG_ChromaShift   < string UIGroup = "CineFX.OpticalVignette"; string UIName="|- Edge Color Shift";               float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.10};
float3 UIVIG_ChromaTint    < string UIGroup = "CineFX.OpticalVignette"; string UIName="|- Edge Color Tint";                string UIWidget="Color";                               > = {1.0, 0.92, 0.85};


//========================= SHADERGROUP 22 — FILM DAMAGE =====================================//
#elif SHADERGROUP == 22

int DmgHeader    <string UIName="      >>>>>>FILM DAMAGE v2<<<<<<"; int UIMin=0; int UIMax=0;> = {0};

int DmgSpacer0   <string UIName="|--------- Scratches ---------"; int UIMin=0; int UIMax=0;> = {0};
float  UIDMG_ScratchInt    < string UIGroup = "CineFX.FilmDamage"; string UIName="|- Scratch Intensity";              float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.20};
float  UIDMG_ScratchWidth  < string UIGroup = "CineFX.FilmDamage"; string UIName="|- Scratch Width";                  float UIMin=0.5;  float UIMax=4.0;  float UIStep=0.1;  > = {1.5};
float  UIDMG_ScratchDensity < string UIGroup = "CineFX.FilmDamage"; string UIName="|- Scratch Density";               float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.30};

int DmgSpacer1   <string UIName="|--------- Dust / Dirt ---------"; int UIMin=0; int UIMax=0;> = {0};
float  UIDMG_DustInt       < string UIGroup = "CineFX.FilmDamage"; string UIName="|- Dust Intensity";                 float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.15};
float  UIDMG_DustDensity   < string UIGroup = "CineFX.FilmDamage"; string UIName="|- Dust Particle Count";            float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.40};

int DmgSpacer2   <string UIName="|--------- Gate Hair ---------"; int UIMin=0; int UIMax=0;> = {0};
float  UIDMG_HairInt       < string UIGroup = "CineFX.FilmDamage"; string UIName="|- Gate Hair Intensity";            float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.15};

int DmgSpacer3   <string UIName="|--------- Film Aging ----------"; int UIMin=0; int UIMax=0;> = {0};
float  UIDMG_FlickerInt    < string UIGroup = "CineFX.FilmDamage"; string UIName="|- Exposure Flicker";               float UIMin=0.0;  float UIMax=0.5;  float UIStep=0.01; > = {0.05};
float  UIDMG_SpliceInt     < string UIGroup = "CineFX.FilmDamage"; string UIName="|- Splice Marks (reel change)";     float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.10};
float  UIDMG_ColorFade     < string UIGroup = "CineFX.FilmDamage"; string UIName="|- Color Fading (vinegar)";         float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.0};
float3 UIDMG_VintageTint   < string UIGroup = "CineFX.FilmDamage"; string UIName="|- Aging Tint";                     string UIWidget="Color";                               > = {1.0, 0.94, 0.82};



#endif //SHADERGROUP

#undef TODIE










