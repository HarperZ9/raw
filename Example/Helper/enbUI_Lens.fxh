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
bool UIWFXRain_Enable  <string UIName="|- Enable Weather FX - Rain";	  > = {false};
bool UIWFXFrost_Enable <string UIName="|- Enable Weather FX - Frost";	  > = {false};
bool UIFOG_Enable      <string UIName="|- Enable Bloom Atmosphere";      > = {false};
bool UIAF_Enable       <string UIName="|- Enable Anamorphic Lens Flares"; > = {false};
bool UIR_Enable        <string UIName="|- Enable Lens Reflections";		  > = {false};
bool UID_Enable        <string UIName="|- Enable Lens Dirt";			  > = {false};
bool UICA_Enable       <string UIName="|- Enable Chromatic Aberration";   > = {false};
bool UILO_Enable       <string UIName="|- Enable Lens Optics";            > = {false};
bool UIHL_Enable       <string UIName="|- Enable Halation";               > = {false};
bool UIVG_Enable       <string UIName="|- Enable Veiling Glare";          > = {false};
bool UIFG_Enable       <string UIName="|- Enable Film Grain";             > = {false};
bool UISR_Enable       <string UIName="|- Enable Sensor Response";        > = {false};
bool UICRT_Enable      <string UIName="|- Enable CRT Display";            > = {false};
bool UIDIFF_Enable     <string UIName="|- Enable Lens Diffusion";          > = {false};
bool UIFHALO_Enable    <string UIName="|- Enable Film Halation";           > = {false};
bool UILEAK_Enable     <string UIName="|- Enable Light Leaks";             > = {false};
bool UIWEAVE_Enable    <string UIName="|- Enable Gate Weave";              > = {false};
bool UILBOX_Enable     <string UIName="|- Enable Letterbox";               > = {false};
bool UIANAM_Enable     <string UIName="|- Enable Anamorphic Lens";          > = {false};
bool UIVIG_Enable      <string UIName="|- Enable Optical Vignette";         > = {false};
bool UIDMG_Enable      <string UIName="|- Enable Film Damage";              > = {false};
bool UI_IgnoreSky      <string UIName="|- Lensflares ignore Sun/Sky";	  > = {false};


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 1

UI_ELEMENT(ML_WeatherFX,"      >>>>>>WEATHER EFFECTS<<<<<<")
UI_WHITESPACE(4)
float  Day_UIWFXRain_Intensity	  <string UIName="|- RainFX - Day - Intensity";			   float UIMin=0.0;  float UIMax=10.0; float UIStep=0.01; > = {1.0};
float4 Day_UIWFXRain_SkyColor	  <string UIName="|- RainFX - Day - Sky Color";			   string UIWidget="Color";								  > = {1.0, 1.0, 1.0, 1.0};
float4 Day_UIWFXRain_EnvColor	  <string UIName="|- RainFX - Day - Environment Color";    string UIWidget="Color";								  > = {0.3, 0.3, 0.3, 1.0};
float  Night_UIWFXRain_Intensity  <string UIName="|- RainFX - Night - Intensity";		   float UIMin=0.0;  float UIMax=10.0; float UIStep=0.01; > = {1.0};
float4 Night_UIWFXRain_SkyColor	  <string UIName="|- RainFX - Night - Sky Color";		   string UIWidget="Color";								  > = {1.0, 1.0, 1.0, 1.0};
float4 Night_UIWFXRain_EnvColor   <string UIName="|- RainFX - Night - Environment Color";  string UIWidget="Color";								  > = {0.3, 0.3, 0.3, 1.0};
UI_SPECIAL_WHITESPACE(2)
float  Day_UIWFXFrost_Intensity   <string UIName="|- FrostFX - Day - Intensity";		   float UIMin=0.0;  float UIMax=5.0;  float UIStep=0.01; > = {0.5};
float4 Day_UIWFXFrost_SkyColor	  <string UIName="|- FrostFX - Day - Sky Color";		   string UIWidget="Color";								  > = {1.0, 1.0, 1.0, 1.0};
float4 Day_UIWFXFrost_EnvColor	  <string UIName="|- FrostFX - Day - Environment Color";   string UIWidget="Color";								  > = {0.3, 0.3, 0.3, 1.0};
float  Night_UIWFXFrost_Intensity <string UIName="|- FrostFX - Night - Intensity";		   float UIMin=0.0;  float UIMax=5.0;  float UIStep=0.01; > = {0.5};
float4 Night_UIWFXFrost_SkyColor  <string UIName="|- FrostFX - Night - Sky Color";		   string UIWidget="Color";								  > = {1.0, 1.0, 1.0, 1.0};
float4 Night_UIWFXFrost_EnvColor  <string UIName="|- FrostFX - Night - Environment Color"; string UIWidget="Color";								  > = {0.3, 0.3, 0.3, 1.0};
UI_SPECIAL_WHITESPACE(3)
UI_ELEMENT(ML_RainFX,							 "|--------- Rain Droplets")
float  UIWFXRain_Tickrate		  <string UIName="|- RainFX - Tickrate";		    float UIMin=0.01; float UIMax=10.0; float UIStep=0.01;  > = {0.15};
float  UIWFXRain_FadeCurve		  <string UIName="|- RainFX - Fade Curve";		    float UIMin=0.01; float UIMax=10.0; float UIStep=0.01;  > = {1.8};
float  UIWFXRain_MinSize		  <string UIName="|- RainFX - Min Size";		    float UIMin=0.0;  float UIMax=10.0; float UIStep=0.001; > = {0.03};
float  UIWFXRain_MaxSize		  <string UIName="|- RainFX - Max Size";		    float UIMin=0.01; float UIMax=10.0; float UIStep=0.001; > = {0.15};
float  UIWFXRain_MaxDeformation	  <string UIName="|- RainFX - Max Deformation";		float UIMin=0.0;  float UIMax= 2.0; float UIStep=0.01;  > = {0.15};
#if RAINFX_ENABLE_DISPERSION
float  UIWFXRain_Dispersion       <string UIName="|- RainFX - Dispersion";		    float UIMin=0.0;  float UIMax=99.0; float UIStep=0.01;  > = {1.0};
#endif
float  UIWFXRain_DripSpeed		  <string UIName="|- RainFX - Dripping Speed";	    float UIMin=0.0;  float UIMax= 5.0; float UIStep=0.001; > = {0.2};
float  UIWFXRain_DripDrift		  <string UIName="|- RainFX - Dripping Drift";	    float UIMin=0.0;  float UIMax= 5.0; float UIStep=0.001; > = {0.1};
float  UIWFXRain_Curvature		  <string UIName="|- RainFX - Droplet Curvature";   float UIMin=0.0;  float UIMax= 2.0; float UIStep=0.01;  > = {1.0};
float  UIWFXRain_Sigma			  <string UIName="|- RainFX - Blur Amount";		    float UIMin=0.0;  float UIMax=20.0; float UIStep=0.01;  > = {0.0};
UI_SPECIAL_WHITESPACE(4)
UI_ELEMENT(ML_FrostFX,							 "|--------- Frost Vignette")
float  UIWFXFrost_PulseRate		  <string UIName="|- FrostFX - Pulse Rate";		    float UIMin=0.0;  float UIMax=10.0; float UIStep=0.01; > = {0.1};
float  UIWFXFrost_PulseStrength	  <string UIName="|- FrostFX - Pulse Strength";	    float UIMin=0.0;  float UIMax=10.0; float UIStep=0.01; > = {0.5};
float  UIWFXFrost_Curve			  <string UIName="|- FrostFX - Curve";			    float UIMin=0.01; float UIMax=15.0; float UIStep=0.01; > = {1.5};
float  UIWFXFrost_RadiusInner	  <string UIName="|- FrostFX - Inner Radius";	    float UIMin=0.0;  float UIMax= 2.0; float UIStep=0.01; > = {1.2};
float  UIWFXFrost_RadiusOuter	  <string UIName="|- FrostFX - Outer Radius";	    float UIMin=0.0;  float UIMax= 2.0; float UIStep=0.01; > = {0.5};
float3 UIWFXFrost_Tint			  <string UIName="|- FrostFX - Tint";			    string UIWidget="Color";							   > = {1.0, 1.0, 1.0};
//UI_SPECIAL_WHITESPACE(5)
#if FROSTFX_USE_REFRACTION_METHOD
float  UIWFXFrost_Opacity         <string UIName="|- FrostFX - Opacity";            float UIMin=0.0;  float UIMax=99.0; float UIStep=0.1;  > = {50.0};
float  UIWFXFrost_RefracRange	  <string UIName="|- FrostFX - Refraction Range";   float UIMin=0.0;  float UIMax= 1.0; float UIStep=0.01; > = {0.2};
bool   UIWFXFrost_AllowOvershoot  <string UIName="|- FrostFX - Approx. missing Data";													   > = {true};
#else
float3 UIWFXFrost_LayerInt		  <string UIName="|- FrostFX - Layer Intensity - "; float UIMin=0.0;  float UIMax= 1.0; float UIStep=0.01; > = {1.0, 1.0, 1.0};
#endif


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 3

#ifndef NOTFIRSTTIME
#define NOTFIRSTTIME
UI_ELEMENT(ML_StartBurst,">>>>>>STARBURST LENS FLARES<<<<<<")
UI_WHITESPACE(9)
#endif //NOTFIRSTTIME

float SEPARATE_VAR(UISB_Strength) <string UIName=TO_STRING(|- Starburst - TODIE - Intensity);		float UIMin=0.0; float UIMax=100.0; > = {1.0};
float SEPARATE_VAR(UISB_Thresh)   <string UIName=TO_STRING(|- Starburst - TODIE - Threshold);		float UIMin=0.1; float UIMax=100.0; > = {5.0};

#ifdef LASTTIME
UI_SPECIAL_WHITESPACE(6)
bool   UISB_HQ			  <string UIName="|- Starburst - Enable HQ Mode";														 > = {false};
float  UISB_Shape		  <string UIName="|- Starburst - Lens Shape";	     float UIMin=4.0; float UIMax=  9.0; float UIStep=1; > = {0.0};
float  UISB_AngleOffset	  <string UIName="|- Starburst - Lens Angle (\xB0)"; float UIMin=0.0; float UIMax=360.0; float UIStep=1; > = {0.0};
float  UISB_Width		  <string UIName="|- Starburst - Scale";		     float UIMin=0.0;									 > = {20.0};
float  UISB_ThreshCurve	  <string UIName="|- Starburst - Threshold Curve";   float UIMin=0.0; float UIMax= 30.0;				 > = {2.0};
float  UISB_Falloff		  <string UIName="|- Starburst - Falloff";		     float UIMin=0.5; float UIMax=  2.0;				 > = {1.0};
float  UISB_Imperfections <string UIName="|- Starburst - Imperfections";     float UIMin=0.0; float UIMax= 10.0;				 > = {0.0};
float  UISB_Saturation	  <string UIName="|- Starburst - Saturation";	     float UIMin=0.0; float UIMax= 10.0;				 > = {1.5};
float4 UISB_Tint		  <string UIName="|- Starburst - Tint";			     string UIWidget = "Color";							 > = {1.0, 1.0, 1.0, 0.0};

#undef LASTTIME
#endif //LASTTIME


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 2

#ifndef NOTFIRSTTIME
#define NOTFIRSTTIME
UI_ELEMENT(ML_AnamFlare,"  >>>>>ANAMORPHIC LENS FLARES<<<<<")
UI_WHITESPACE(5)
#endif //NOTFIRSTTIME

float SEPARATE_VAR(UIAF_Int)    <string UIName=TO_STRING(|- AnamFlare - TODIE - Intensity); float UIMin=0.0; float UIMax=10.0; > = {1.0};
float SEPARATE_VAR(UIAF_Thresh) <string UIName=TO_STRING(|- AnamFlare - TODIE - Threshold); float UIMin=0.0; float UIMax=20.0; > = {1.0};

#ifdef LASTTIME
UI_SPECIAL_WHITESPACE(7)
float UIAF_Rotation  <string UIName="|- AnamFlare - Rotation";	    float UIStep=1.0; float UIMin=0.0; float UIMax=180.0; > = {90.0};
float UIAF_CoreFoc   <string UIName="|- AnamFlare - Core Focus";				      float UIMin=0.0; float UIMax=20.0;  > = { 0.4};
float UIAF_CoreInt   <string UIName="|- AnamFlare - Core Intensity";			      float UIMin=0.0; float UIMax=10.0;  > = { 1.5};
float UIAF_CoreMax   <string UIName="|- AnamFlare - Core Max";					      float UIMin=0.0; float UIMax=10.0;  > = { 2.0};
float UIAF_Width     <string UIName="|- AnamFlare - Width";						      float UIMin=1.0; float UIMax=100.0; > = {15.0};
float UIAF_Sat	     <string UIName="|- AnamFlare - Saturation";				      float UIMin=0.0; float UIMax=10.0;  > = { 1.0};
#if ANAM_ENABLE_MIRRORED_FLARE
float UIAF_MirrorInt <string UIName="|- AnamFlare - Mirror Intensity";			      float UIMin=0.0; float UIMax=10.0;  > = {0.5};
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

float SEPARATE_VAR(UID_Strength) <string UIName=TO_STRING(|- Dirt - TODIE - Intensity); float UIMin=0.0; float UIMax=100.0; > = {0.5};
float SEPARATE_VAR(UID_Power)    <string UIName=TO_STRING(|- Dirt - TODIE - Power);     float UIMin=0.0; float UIMax=100.0; > = {1.5};

#ifdef LASTTIME
UI_SPECIAL_WHITESPACE(8)
int UID_Select <string UIName="|- Dirt - Select Texture"; int UIMin=1; int UIMax=4; > = {1};

#undef LASTTIME
#endif //LASTTIME


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 5
UI_ELEMENT(_CA_Header,">>>>>>CHROMATIC ABERRATION<<<<<<")
UI_WHITESPACE(8)

float UICA_Strength    <string UIName="|- CA - Strength";            float UIMin=-8.0;  float UIMax= 8.0;  float UIStep=0.01; > = {1.0};
float UICA_Curve       <string UIName="|- CA - Curve";               float UIMin= 0.01; float UIMax=10.0;  float UIStep=0.01; > = {1.5};
UI_SPECIAL_WHITESPACE(15)
UI_ELEMENT(_CA_Spectral,                  "|--------- Spectral Dispersion")
float UICA_Spread      <string UIName="|- CA - Spectral Spread";    float UIMin= 0.0;  float UIMax= 3.0;  float UIStep=0.01; > = {1.0};
float UICA_Barrel      <string UIName="|- CA - Barrel Distortion";   float UIMin=-2.0;  float UIMax= 2.0;  float UIStep=0.01; > = {0.0};
float UICA_FringeSat   <string UIName="|- CA - Fringe Saturation";   float UIMin= 0.0;  float UIMax= 2.0;  float UIStep=0.01; > = {1.0};
UI_SPECIAL_WHITESPACE(16)
UI_ELEMENT(_CA_Tuning,                    "|--------- Fine Tuning")
float UICA_RedBias     <string UIName="|- CA - Red Channel Bias";    float UIMin=-2.0;  float UIMax= 2.0;  float UIStep=0.01; > = {1.0};
float UICA_BlueBias    <string UIName="|- CA - Blue Channel Bias";   float UIMin=-2.0;  float UIMax= 2.0;  float UIStep=0.01; > = {1.0};
float UICA_Deadzone    <string UIName="|- CA - Center Deadzone";     float UIMin= 0.0;  float UIMax= 0.5;  float UIStep=0.01; > = {0.05};


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 6

#ifndef NOTFIRSTTIME
#define NOTFIRSTTIME
UI_ELEMENT(ML_Reflections,"      >>>>>>LENS REFLECTIONS<<<<<<")
UI_WHITESPACE(6)
#endif //NOTFIRSTTIME

float SEPARATE_VAR(UIR_Strength) <string UIName=TO_STRING(|- Reflection - TODIE - Intensity); float UIMin=0.0; float UIMax=100.0; > = {5.0};
float SEPARATE_VAR(UIR_Power)    <string UIName=TO_STRING(|- Reflection - TODIE - Power);     float UIMin=0.0; float UIMax=100.0; > = {1.5};

#ifdef LASTTIME
UI_SPECIAL_WHITESPACE(9)
float3 UIR_ColorFilter1 <string UIName="|- Reflection - Color Filter 1"; string UIWidget = "Color"; > = {0.3, 0.4, 0.4};
float3 UIR_ColorFilter2 <string UIName="|- Reflection - Color Filter 2"; string UIWidget = "Color"; > = {0.2, 0.4, 0.5};
float3 UIR_ColorFilter3 <string UIName="|- Reflection - Color Filter 3"; string UIWidget = "Color"; > = {0.5, 0.3, 0.7};
float3 UIR_ColorFilter4 <string UIName="|- Reflection - Color Filter 4"; string UIWidget = "Color"; > = {0.1, 0.2, 0.7};

#undef LASTTIME
#endif //LASTTIME


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 7

#ifndef NOTFIRSTTIME
#define NOTFIRSTTIME
UI_ELEMENT(ML_Ghosts,"     >>>>>>GHOST LENS FLARES<<<<<<")
UI_WHITESPACE(10)
#endif //NOTFIRSTTIME

float SEPARATE_VAR(UIG_Strength) <string UIName=TO_STRING(|- Ghosts - TODIE - Intensity); float UIMin=0.0; float UIMax=100.0; float UIStep=0.1; > = {1.0};
float SEPARATE_VAR(UIG_Power)    <string UIName=TO_STRING(|- Ghosts - TODIE - Power);     float UIMin=0.0; float UIMax=100.0;					> = {8.0};

#ifdef LASTTIME
UI_SPECIAL_WHITESPACE(10)
float UIG_FNumber 		<string UIName="|- Ghosts - Lens F Number";		 	float UIMin=2.0; float UIMax=8.0;				    > = {7.0};
float UIG_Shape 		<string UIName="|- Ghosts - Lens Shape"; 			float UIMin=4.0; float UIMax=9.0;   float UIStep=1; > = {0.0};
float UIG_AngleOffset	<string UIName="|- Ghosts - Lens Angle"; 			float UIMin=0.0; float UIMax=360.0; float UIStep=1; > = {0.0};
float UIG_Ratio			<string UIName="|- Ghosts - Lens Ratio"; 			float UIMin=0.0;								    > = {1.0};
bool  UIG_ForceDeform	<string UIName="|- Ghosts - Enable Forced Deform";													    > = {false};
float UIG_Deform 		<string UIName="|- Ghosts - Forced Deform Ratio"; 	float UIMin=0.0; float UIMax=1.0;				    > = {1.0}; //( 0 == circle, 1 == polygon)
float UIG_ShapeWeight	<string UIName="|- Ghosts - Lens Shape Weight";		float UIMin=0.0; float UIMax=2.0;				    > = {2.0};
float UIG_Grain			<string UIName="|- Ghosts - Lens Grain"; 			float UIMin=0.0;								    > = {0.2};

#if MULTIIRIS_ENABLE_CHROMATICABERRATION
UI_SPECIAL_WHITESPACE(11)
UI_ELEMENT(ML_CA,				       "|- Ghosts --------- Chromatic Aberration")
float  UIG_CACurve		<string UIName="|- Ghosts - CA Curve";	float UIMin=0.0;		   > = { 0.8 };
float  UIG_CAScale		<string UIName="|- Ghosts - CA Scale";							   > = {-1.05};
float3 UIG_CAColor0		<string UIName="|- Ghosts - CA Color0"; string UIWidget = "Color"; > = {1.0, 0.0, 0.0};
float3 UIG_CAColor1		<string UIName="|- Ghosts - CA Color1"; string UIWidget = "Color"; > = {0.0, 1.0, 0.0};
float3 UIG_CAColor2		<string UIName="|- Ghosts - CA Color2"; string UIWidget = "Color"; > = {0.0, 0.0, 1.0};
#endif //MULTIIRIS_ENABLE_CHROMATICABERRATION

UI_SPECIAL_WHITESPACE(12)
UI_ELEMENT(ML_Flare1,			       "|- Ghosts --------- Flares 1")
float4 Intensity1		<string UIName="|- Ghosts - Intensity1";					   > = {1.0,  1.0,   1.0, 1.0};
float4 Curve1 			<string UIName="|- Ghosts - Curve1";						   > = {1.0,  1.25,  1.0, 1.0};
float4 Scale1 			<string UIName="|- Ghosts - Scale1";						   > = {0.0, -0.30, -0.5, 1.0};
float4 Tint1 			<string UIName="|- Ghosts - Tint1"; string UIWidget = "Color"; > = {0.25, 0.25,  1.0, 1.0};
UI_SPECIAL_WHITESPACE(13)
UI_ELEMENT(ML_Flare2,			       "|- Ghosts --------- Flares 2")
float4 Intensity2 		<string UIName="|- Ghosts - Intensity2";					   > = {1.0,  1.0,  1.0,   1.0};
float4 Curve2 			<string UIName="|- Ghosts - Curve2";						   > = {1.4,  1.0,  1.0,   0.7};
float4 Scale2 			<string UIName="|- Ghosts - Scale2";						   > = {0.66, 2.0, -0.66, -0.5};
float4 Tint2 			<string UIName="|- Ghosts - Tint2"; string UIWidget = "Color"; > = {0.66, 1.0,  1.0,   0.25};
UI_SPECIAL_WHITESPACE(14)
UI_ELEMENT(ML_Flare3,			       "|- Ghosts --------- Flares 3")
float4 Intensity3 		<string UIName="|- Ghosts - Intensity3";					   > = {1.0,  1.0,  1.0,   1.0};
float4 Curve3 			<string UIName="|- Ghosts - Curve3";						   > = {1.0,  0.8,  1.0,   3.0};
float4 Scale3 			<string UIName="|- Ghosts - Scale3";						   > = {3.0, -1.5, -2.0, -20.0};
float4 Tint3 			<string UIName="|- Ghosts - Tint3"; string UIWidget = "Color"; > = {1.0,  1.0,  1.0,   0.0};

#undef LASTTIME
#endif //LASTTIME


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 8

#ifndef NOTFIRSTTIME
#define NOTFIRSTTIME
UI_ELEMENT(ML_FilmGrain,"        >>>>>>FILM GRAIN<<<<<<")
UI_WHITESPACE(25)
#endif //NOTFIRSTTIME

float SEPARATE_VAR(UIFG_Intensity) <string UIName=TO_STRING(|- Film Grain - TODIE - Intensity); float UIMin=0.0; float UIMax=1.0; float UIStep=0.001; > = {0.12};

#ifdef LASTTIME
UI_SPECIAL_WHITESPACE(17)
UI_ELEMENT(ML_FGStructure,                "|--------- Grain Structure")
float  UIFG_Size          <string UIName="|- Film Grain - Size";             float UIMin=0.5;  float UIMax=4.0;  float UIStep=0.01; > = {1.0};
float  UIFG_Roughness     <string UIName="|- Film Grain - Roughness";        float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.5};
float  UIFG_AnimSpeed     <string UIName="|- Film Grain - Animation Speed";  float UIMin=0.0;  float UIMax=2.0;  float UIStep=0.01; > = {1.0};
UI_SPECIAL_WHITESPACE(18)
UI_ELEMENT(ML_FGResponse,                 "|--------- Luminance Response")
float  UIFG_ShadowGrain   <string UIName="|- Film Grain - Shadow Grain";    float UIMin=0.0;  float UIMax=2.0;  float UIStep=0.01; > = {1.4};
float  UIFG_MidtoneGrain  <string UIName="|- Film Grain - Midtone Grain";   float UIMin=0.0;  float UIMax=2.0;  float UIStep=0.01; > = {1.0};
float  UIFG_HighlightGrain<string UIName="|- Film Grain - Highlight Grain";  float UIMin=0.0;  float UIMax=2.0;  float UIStep=0.01; > = {0.3};
UI_SPECIAL_WHITESPACE(19)
UI_ELEMENT(ML_FGEmulsion,                 "|--------- Emulsion Layers")
float3 UIFG_ChannelWeight <string UIName="|- Film Grain - RGB Weight";       float UIMin=0.0;  float UIMax=2.0;  float UIStep=0.01; > = {0.95, 1.0, 1.15};
float  UIFG_ChannelDecorr <string UIName="|- Film Grain - Channel Decorrelation"; float UIMin=0.0; float UIMax=1.0; float UIStep=0.01; > = {0.35};

#undef LASTTIME
#endif //LASTTIME


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 9

UI_ELEMENT(ML_LensOptics,"        >>>>>>LENS OPTICS<<<<<<")
UI_WHITESPACE(26)

UI_ELEMENT(_LO_Distort,                    "|--------- Barrel/Pincushion Distortion")
float  UILO_DistortK1     <string UIName="|- Optics - Radial K1";          float UIMin=-1.0;  float UIMax=1.0;  float UIStep=0.001; > = {0.0};
float  UILO_DistortK2     <string UIName="|- Optics - Radial K2";          float UIMin=-1.0;  float UIMax=1.0;  float UIStep=0.001; > = {0.0};
float  UILO_DistortK3     <string UIName="|- Optics - Radial K3";          float UIMin=-0.5;  float UIMax=0.5;  float UIStep=0.001; > = {0.0};
float  UILO_DistortP1     <string UIName="|- Optics - Tangential P1";      float UIMin=-0.1;  float UIMax=0.1;  float UIStep=0.001; > = {0.0};
float  UILO_DistortP2     <string UIName="|- Optics - Tangential P2";      float UIMin=-0.1;  float UIMax=0.1;  float UIStep=0.001; > = {0.0};
UI_SPECIAL_WHITESPACE(20)
UI_ELEMENT(_LO_Vignette,                   "|--------- Optical Vignetting")
float  UILO_VigNatural    <string UIName="|- Optics - Natural Vignette (cos4)"; float UIMin=0.0; float UIMax=3.0; float UIStep=0.01; > = {0.0};
float  UILO_VigMechanical <string UIName="|- Optics - Mechanical Vignette";float UIMin=0.0;  float UIMax=2.0;  float UIStep=0.01; > = {0.0};
float  UILO_VigMechRatio  <string UIName="|- Optics - Mech. Aspect Ratio"; float UIMin=0.5;  float UIMax=2.0;  float UIStep=0.01; > = {1.0};
float  UILO_VigRoundness  <string UIName="|- Optics - Mech. Roundness";    float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.8};
float3 UILO_VigColorShift <string UIName="|- Optics - Edge Color Shift";   string UIWidget="Color";                                > = {1.0, 0.95, 0.85};
float  UILO_VigColorAmt   <string UIName="|- Optics - Color Shift Amount"; float UIMin=0.0;  float UIMax=2.0;  float UIStep=0.01; > = {0.0};
UI_SPECIAL_WHITESPACE(21)
UI_ELEMENT(_LO_Coma,                       "|--------- Coma Aberration")
float  UILO_ComaStrength  <string UIName="|- Optics - Coma Strength";      float UIMin=0.0;  float UIMax=5.0;  float UIStep=0.01; > = {0.0};
float  UILO_ComaFalloff   <string UIName="|- Optics - Coma Falloff";       float UIMin=0.5;  float UIMax=5.0;  float UIStep=0.01; > = {2.5};
float  UILO_ComaSamples   <string UIName="|- Optics - Coma Quality";       float UIMin=3.0;  float UIMax=12.0; float UIStep=1.0;  > = {6.0};
UI_SPECIAL_WHITESPACE(22)
UI_ELEMENT(_LO_Spherical,                  "|--------- Spherical Aberration")
float  UILO_SphericalStr  <string UIName="|- Optics - Spherical Strength"; float UIMin=0.0;  float UIMax=3.0;  float UIStep=0.01; > = {0.0};
float  UILO_SphericalBias <string UIName="|- Optics - Spherical Luma Bias";float UIMin=0.0;  float UIMax=5.0;  float UIStep=0.01; > = {1.5};
float  UILO_SphericalRadius<string UIName="|- Optics - Spherical Radius";  float UIMin=0.5;  float UIMax=8.0;  float UIStep=0.1;  > = {2.0};
UI_SPECIAL_WHITESPACE(23)
UI_ELEMENT(_LO_FieldCurve,                 "|--------- Field Curvature")
float  UILO_FieldCurveStr <string UIName="|- Optics - Field Curvature";    float UIMin=0.0;  float UIMax=3.0;  float UIStep=0.01; > = {0.0};
float  UILO_FieldCurveExp <string UIName="|- Optics - Curvature Falloff";  float UIMin=1.0;  float UIMax=5.0;  float UIStep=0.01; > = {2.0};


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 10

#ifndef NOTFIRSTTIME_HL
#define NOTFIRSTTIME_HL
UI_ELEMENT(ML_Halation,"          >>>>>>HALATION<<<<<<")
UI_WHITESPACE(27)
#endif //NOTFIRSTTIME_HL

float SEPARATE_VAR(UIHL_Intensity) <string UIName=TO_STRING(|- Halation - TODIE - Intensity); float UIMin=0.0; float UIMax=2.0; float UIStep=0.01; > = {0.3};

#ifdef LASTTIME
UI_SPECIAL_WHITESPACE(24)
UI_ELEMENT(_HL_Settings,                   "|--------- Halation Settings")
float  UIHL_Threshold     <string UIName="|- Halation - Threshold";        float UIMin=0.0;  float UIMax=5.0;  float UIStep=0.01; > = {0.8};
float  UIHL_ThreshCurve   <string UIName="|- Halation - Threshold Curve";  float UIMin=0.1;  float UIMax=5.0;  float UIStep=0.01; > = {2.0};
float  UIHL_Radius        <string UIName="|- Halation - Spread Radius";    float UIMin=1.0;  float UIMax=32.0; float UIStep=0.5;  > = {8.0};
float3 UIHL_Color         <string UIName="|- Halation - Color Tint";       string UIWidget="Color";                                > = {1.0, 0.35, 0.15};
float  UIHL_Saturation    <string UIName="|- Halation - Saturation";       float UIMin=0.0;  float UIMax=2.0;  float UIStep=0.01; > = {0.8};
UI_SPECIAL_WHITESPACE(25)
UI_ELEMENT(_VG_Settings,                   "|--------- Veiling Glare")
float  UIVG_Intensity     <string UIName="|- Veiling Glare - Intensity";   float UIMin=0.0;  float UIMax=2.0;  float UIStep=0.01; > = {0.15};
float  UIVG_Threshold     <string UIName="|- Veiling Glare - Threshold";   float UIMin=0.0;  float UIMax=5.0;  float UIStep=0.01; > = {1.2};
float  UIVG_Radius        <string UIName="|- Veiling Glare - Spread";      float UIMin=2.0;  float UIMax=64.0; float UIStep=1.0;  > = {24.0};
float3 UIVG_Tint          <string UIName="|- Veiling Glare - Tint";        string UIWidget="Color";                                > = {1.0, 0.98, 0.92};

#undef LASTTIME
#endif //LASTTIME


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 11

UI_ELEMENT(ML_Sensor,"      >>>>>>SENSOR RESPONSE<<<<<<")
UI_WHITESPACE(28)

UI_ELEMENT(_SR_Curve,                      "|--------- Film Response Curve")
float  UISR_Shoulder      <string UIName="|- Sensor - Highlight Shoulder"; float UIMin=0.0;  float UIMax=3.0;  float UIStep=0.01; > = {0.0};
float  UISR_ShoulderStart <string UIName="|- Sensor - Shoulder Start";     float UIMin=0.3;  float UIMax=2.0;  float UIStep=0.01; > = {0.8};
float  UISR_Toe           <string UIName="|- Sensor - Shadow Toe";         float UIMin=0.0;  float UIMax=2.0;  float UIStep=0.01; > = {0.0};
float  UISR_ToeEnd        <string UIName="|- Sensor - Toe End";            float UIMin=0.01; float UIMax=0.5;  float UIStep=0.01; > = {0.1};
UI_SPECIAL_WHITESPACE(26)
UI_ELEMENT(_SR_Crosstalk,                  "|--------- Channel Crosstalk")
float  UISR_Crosstalk     <string UIName="|- Sensor - Crosstalk Amount";   float UIMin=0.0;  float UIMax=0.5;  float UIStep=0.01; > = {0.0};
float  UISR_CrossRtoG     <string UIName="|- Sensor - Red to Green Bleed"; float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.3};
float  UISR_CrossGtoB     <string UIName="|- Sensor - Green to Blue Bleed";float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.25};
float  UISR_CrossBtoR     <string UIName="|- Sensor - Blue to Red Bleed";  float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.15};
UI_SPECIAL_WHITESPACE(27)
UI_ELEMENT(_SR_Noise,                      "|--------- Sensor Noise")
float  UISR_ReadNoise     <string UIName="|- Sensor - Read Noise";         float UIMin=0.0;  float UIMax=0.05; float UIStep=0.001;> = {0.0};
float  UISR_HotPixels     <string UIName="|- Sensor - Hot Pixel Rate";     float UIMin=0.0;  float UIMax=0.01; float UIStep=0.0001;> = {0.0};
float  UISR_PhotonNoise   <string UIName="|- Sensor - Photon (Shot) Noise";float UIMin=0.0;  float UIMax=0.1;  float UIStep=0.001;> = {0.0};



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
float UIBRIDGE_RainFromWeather  <string UIName="|- SB - Rain Intensity From Weather";    float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UIBRIDGE_FrostFromSnow    <string UIName="|- SB - Frost From Snow Weather";         float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UIBRIDGE_InteriorAccurate <string UIName="|- SB - Accurate Interior Detection";     float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};

//--- Flare Tinting ---
int SB_LENS_Spacer1   <string UIName="|--------- Flare & Dirt"; int UIMin=0; int UIMax=0;> = {0};
float UIBRIDGE_SunFlare         <string UIName="|- SB - Sun Color Flare Tinting";         float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UIBRIDGE_SunFlareBlend    <string UIName="|- SB - Flare Sun Color Blend";           float UIMin= 0.0; float UIMax= 1.0; > = { 0.4};
float UIBRIDGE_WetDirt          <string UIName="|- SB - Rain Wet Lens Dirt";              float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UIBRIDGE_WetDirtMult      <string UIName="|- SB - Wet Dirt Intensity Mult";         float UIMin= 1.0; float UIMax= 3.0; > = { 1.5};


//--- Damage Screen FX ---
int SB_LENS_Spacer2   <string UIName="|--------- Damage Screen FX"; int UIMin=0; int UIMax=0;> = {0};
float UIBRIDGE_DamageFX       <string UIName="|- SB - Elemental Damage Screen FX";    float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UIBRIDGE_DmgFireInt     <string UIName="|- SB - Fire Damage Intensity";          float UIMin= 0.0; float UIMax= 1.0; > = { 0.5};
float UIBRIDGE_DmgFrostInt    <string UIName="|- SB - Frost Damage Intensity";         float UIMin= 0.0; float UIMax= 1.0; > = { 0.4};
float UIBRIDGE_DmgShockInt    <string UIName="|- SB - Shock Damage Intensity";         float UIMin= 0.0; float UIMax= 1.0; > = { 0.6};

//--- Player Vitals FX ---
int SB_LENS_Spacer3   <string UIName="|--------- Player Vitals"; int UIMin=0; int UIMax=0;> = {0};
float UIBRIDGE_HealthVignette <string UIName="|- SB - Low-Health Blood Vignette";      float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UIBRIDGE_HealthThresh   <string UIName="|- SB - Health Vignette Threshold";      float UIMin= 0.1; float UIMax= 0.6; > = { 0.3};
float UIBRIDGE_BleedoutOverlay<string UIName="|- SB - Bleedout Blood Overlay";         float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UIBRIDGE_StaminaDesat   <string UIName="|- SB - Low-Stamina Desaturation";       float UIMin= 0.0; float UIMax= 1.0; > = { 0.0};

//--- Atmospheric FX ---
int SB_LENS_Spacer4   <string UIName="|--------- Atmospheric FX"; int UIMin=0; int UIMax=0;> = {0};
float UIBRIDGE_LightningFlash <string UIName="|- SB - Lightning Screen Flash";         float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UIBRIDGE_LightningInt   <string UIName="|- SB - Lightning Flash Intensity";      float UIMin= 0.0; float UIMax= 2.0; > = { 0.6};
float UIBRIDGE_TorchGlow      <string UIName="|- SB - Torch Warm Lens Glow";           float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UIBRIDGE_TorchGlowInt   <string UIName="|- SB - Torch Glow Intensity";           float UIMin= 0.0; float UIMax= 0.5; > = { 0.08};
float UIBRIDGE_WindRain        <string UIName="|- SB - Wind-Directed Rain Streaks";     float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UIBRIDGE_SnowLens        <string UIName="|- SB - Snow Lens Accumulation";         float UIMin= 0.0; float UIMax= 1.0; > = { 0.0};

//--- Intoxication & Magic ---
int SB_LENS_Spacer5   <string UIName="|--------- Intoxication & Magic"; int UIMin=0; int UIMax=0;> = {0};
float UIBRIDGE_DrunkFX        <string UIName="|- SB - Drunk/Skooma Distortion";        float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UIBRIDGE_DrunkStrength  <string UIName="|- SB - Drunk Blur Strength";            float UIMin= 0.0; float UIMax= 2.0; > = { 0.5};
float UIBRIDGE_NightEyeTint   <string UIName="|- SB - Night Eye Green Tint";           float UIMin= 0.0; float UIMax= 1.0; > = { 0.0};
float UIBRIDGE_EtherealFade   <string UIName="|- SB - Ethereal Desaturation";          float UIMin= 0.0; float UIMax= 1.0; > = { 0.0};

#endif //SKYRIMBRIDGE_FXH


#endif //SHADERGROUP

#undef TODIE










