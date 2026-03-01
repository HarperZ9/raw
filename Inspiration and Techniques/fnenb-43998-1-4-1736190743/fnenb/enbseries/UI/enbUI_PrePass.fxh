//----------------------------------------------------------------------------------------------//
//																								//
//								 Main ENB PrePass UI file										//
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

UI_FileHeaderLong(">>>            ENB PrePass for SSE            <<<",
				  ">>>              by LonelyKitsuune              <<<")

UI_WHITESPACE(1)
#if ENABLE_TOOLS
UI_ELEMENT(_Tools_Header,"               >>>>>>TOOLS<<<<<<")
UI_WHITESPACE(2)
bool UI_Visualize_Mask  <string UIName="Visualize SSS Mask";          > = {false};
bool UI_Visualize_SDI   <string UIName="Visualize SDI Mask";          > = {false};
bool UI_Visualize_Occlu <string UIName="Visualize Occlusion Texture"; > = {false};
bool UI_Visualize_Rays  <string UIName="Visualize Sun Rays";          > = {false};
UI_WHITESPACE(3)
UI_WHITESPACE(4)
#endif //ENABLE_TOOLS

#undef SHADERGROUP


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 1

#ifndef NOTFIRSTTIME
#define NOTFIRSTTIME
UI_ELEMENT(_DSRS_Header,"  >>>>>>DUAL SUN RAY SYSTEM<<<<<<")
UI_WHITESPACE(5)
int   UIDSRS_Quality   <string UIName="|- DSRS - Quality";						  string UIWidget="Quality";	     > = {1};
float UIDSRS_RayLength <string UIName="|- DSRS - Length";						  float UIMin=1.0; float UIMax= 4.0; > = {1.0};
float UIDSRS_HDRDark   <string UIName="|- DSRS - HDR Darkening";				  float UIMin=0.0; float UIMax= 1.0; > = {0.3};
float UIDSRS_Desat     <string UIName="|- DSRS - Weather dependant Desaturation"; float UIMin=0.0; float UIMax= 1.0; > = {1.0};
UI_SPECIAL_WHITESPACE(1)
UI_ELEMENT(_DSR_Header,               "|------------ Direct Sun Rays")
bool  UIDSR_Enable     <string UIName="|- DSR - Enable";												  > = {true};
float UIDSR_Falloff    <string UIName="|- DSR - Falloff";		       float UIMin=0.0; float UIMax= 1.0; > = {1.0};
#ifdef ENABLE_SUNRAY_WEATHERSEPARATION
UI_SPECIAL_WHITESPACE(2)
float UIDSR_IntClear   <string UIName="|- DSR - Intensity - Clear";    float UIMin=0.0; float UIMax=10.0; > = {1.0};
#ifndef DISABLE_AURORA_WEATHERSEPARATION
float UIDSR_IntClearA  <string UIName="|- DSR - Intensity - Clear_A";  float UIMin=0.0; float UIMax=10.0; > = {1.0};
#endif //DISABLE_AURORA_WEATHERSEPARATION
float UIDSR_IntCloudy  <string UIName="|- DSR - Intensity - Cloudy";   float UIMin=0.0; float UIMax=10.0; > = {1.0};
#ifndef DISABLE_AURORA_WEATHERSEPARATION
float UIDSR_IntCloudyA <string UIName="|- DSR - Intensity - Cloudy_A"; float UIMin=0.0; float UIMax=10.0; > = {1.0};
#endif //DISABLE_AURORA_WEATHERSEPARATION
float UIDSR_IntRainy   <string UIName="|- DSR - Intensity - Rainy";    float UIMin=0.0; float UIMax=10.0; > = {1.0};
float UIDSR_IntSnowy   <string UIName="|- DSR - Intensity - Snowy";    float UIMin=0.0; float UIMax=10.0; > = {1.0};
float UIDSR_IntFoggy   <string UIName="|- DSR - Intensity - Foggy";    float UIMin=0.0; float UIMax=10.0; > = {1.0};
float UIDSR_IntAsh     <string UIName="|- DSR - Intensity - Ash";      float UIMin=0.0; float UIMax=10.0; > = {1.0};
#endif //ENABLE_SUNRAY_WEATHERSEPARATION
#endif //NOTFIRSTTIME

UI_SPECIAL_WHITESPACE(SWSN)
float  SEPARATE_VAR(UIDSR_Int)       <string UIName=TO_STRING(|- DSR - TODIE - Intensity);		float UIMin=0.0; float UIMax=10.0; > = {1.0};
float  SEPARATE_VAR(UIDSR_CloudDens) <string UIName=TO_STRING(|- DSR - TODIE - Cloud Density);	float UIMin=0.1; float UIMax=16.0; > = {3.0};
float3 SEPARATE_VAR(UIDSR_Color)     <string UIName=TO_STRING(|- DSR - TODIE - Color);			string UIWidget="Color";		   > = {1,1,1};


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 2

#ifndef NOTFIRSTTIME
#define NOTFIRSTTIME
UI_SPECIAL_WHITESPACE(3)
UI_SPECIAL_WHITESPACE(4)
UI_ELEMENT(_DVR_Header,               "|------------ Distant Volumetric Rays")
bool  UIDVR_Enable     <string UIName="|- DVR - Enable";												       > = {true};
float UIDVR_Falloff    <string UIName="|- DVR - Falloff";				    float UIMin=0.0; float UIMax= 1.0; > = {1.0};
int   UIDVR_RayType    <string UIName="|- DVR - Ray Occlusion Type";	    int   UIMin=  1; int   UIMax=   4; > = {4};
float UIDVR_DepthStart <string UIName="|- DVR - Type 4 - Depth Fade Start"; float UIMin=0.0; float UIMax= 1.0; > = {0.4};
float UIDVR_DepthEnd   <string UIName="|- DVR - Type 4 - Depth Fade End";   float UIMin=0.0; float UIMax= 1.0; > = {0.8};
#ifdef ENABLE_SUNRAY_WEATHERSEPARATION
UI_SPECIAL_WHITESPACE(5)
float UIDVR_IntClear   <string UIName="|- DVR - Intensity - Clear";		    float UIMin=0.0; float UIMax=10.0; > = {1.0};
#ifndef DISABLE_AURORA_WEATHERSEPARATION
float UIDVR_IntClearA  <string UIName="|- DVR - Intensity - Clear_A";	    float UIMin=0.0; float UIMax=10.0; > = {1.0};
#endif //DISABLE_AURORA_WEATHERSEPARATION
float UIDVR_IntCloudy  <string UIName="|- DVR - Intensity - Cloudy";	    float UIMin=0.0; float UIMax=10.0; > = {1.0};
#ifndef DISABLE_AURORA_WEATHERSEPARATION
float UIDVR_IntCloudyA <string UIName="|- DVR - Intensity - Cloudy_A";	    float UIMin=0.0; float UIMax=10.0; > = {1.0};
#endif //DISABLE_AURORA_WEATHERSEPARATION
float UIDVR_IntRainy   <string UIName="|- DVR - Intensity - Rainy";		    float UIMin=0.0; float UIMax=10.0; > = {1.0};
float UIDVR_IntSnowy   <string UIName="|- DVR - Intensity - Snowy";		    float UIMin=0.0; float UIMax=10.0; > = {1.0};
float UIDVR_IntFoggy   <string UIName="|- DVR - Intensity - Foggy";		    float UIMin=0.0; float UIMax=10.0; > = {1.0};
float UIDVR_IntAsh     <string UIName="|- DVR - Intensity - Ash";		    float UIMin=0.0; float UIMax=10.0; > = {1.0};
#endif //ENABLE_SUNRAY_WEATHERSEPARATION
#endif //NOTFIRSTTIME

UI_SPECIAL_WHITESPACE(SWSN)
float  SEPARATE_VAR(UIDVR_Int)       <string UIName=TO_STRING(|- DVR - TODIE - Intensity);		float UIMin=0.0; float UIMax=10.0; > = {1.0};
float  SEPARATE_VAR(UIDVR_CloudDens) <string UIName=TO_STRING(|- DVR - TODIE - Cloud Density);	float UIMin=0.1; float UIMax=16.0; > = {3.0};
float3 SEPARATE_VAR(UIDVR_Color)     <string UIName=TO_STRING(|- DVR - TODIE - Color);			string UIWidget="Color";		   > = {1,1,1};


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 3

#ifndef NOTFIRSTTIME
#define NOTFIRSTTIME
UI_ELEMENT(_SSSC_Header,"      >>>>>>SSS CUSTOMIZER<<<<<<")
UI_WHITESPACE(6)
bool UISkin_Enable <string UIName="|- Enbale SSS Customizer"; > = {true};
UI_SPECIAL_WHITESPACE(6)
#endif //NOTFIRSTTIME

float  SEPARATE_VAR(UISkin_Exposure)	<string UIName=TO_STRING(|- TODIE - SSS - Exposure);					float UIStep=0.001; float UIMin=0.0; float UIMax=5.0; > = {1.0};
float  SEPARATE_VAR(UISkin_Saturation)	<string UIName=TO_STRING(|- TODIE - SSS - Saturation);					float UIStep=0.001; float UIMin=0.0; float UIMax=2.0; > = {1.0};
float  SEPARATE_VAR(UISkin_Contrast)	<string UIName=TO_STRING(|- TODIE - SSS - Contrast);					float UIStep=0.001; float UIMin=0.0; float UIMax=5.0; > = {1.0};
float  SEPARATE_VAR(UISkin_OutBlack)	<string UIName=TO_STRING(|- TODIE - SSS - Out Black);					float UIStep=0.001; float UIMin=0.0; float UIMax=1.0; > = {0.0};
float  SEPARATE_VAR(UISkin_OutWhite)	<string UIName=TO_STRING(|- TODIE - SSS - Out White);					float UIStep=0.001; float UIMin=0.0; float UIMax=1.0; > = {1.0};
float3 SEPARATE_VAR(UISkin_Tint)		<string UIName=TO_STRING(|- TODIE - SSS - Tint);						string UIWidget="Color";						      > = {1,1,1};
float  SEPARATE_VAR(UISkin_SDILum)		<string UIName=TO_STRING(|- TODIE - SSS - SDI (Direct Illumination));	float UIStep=0.001; float UIMin=0.0; float UIMax=5.0; > = {1.0};

#endif //SHADERGROUP

#undef TODIE









