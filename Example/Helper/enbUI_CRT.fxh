#ifndef ENBUI_CRT_FXH
#define ENBUI_CRT_FXH

//----------------------------------------------------------------------------------------------//
//                                                                                              //
//                UI Parameters for CRT Display Simulation  v1.0                                //
//                by LonelyKitsuune / Extended by Zain Dana Harper                              //
//                                                                                              //
//  v2.0.0 - IMPROVED with SkyrimBridge v3.0.0 SHADERGROUP 99 integration                      //
//                                                                                              //
//  SHADERGROUP 14: All CRT parameters (no DNI — display simulation is time-independent)        //
//  SHADERGROUP 99: SkyrimBridge CRT Integration [NEW]                                          //
//                                                                                              //
//----------------------------------------------------------------------------------------------//

#if SHADERGROUP == 14

int CRTHeader   <string UIName="      >>>>>>CRT DISPLAY<<<<<<"; int UIMin=0; int UIMax=0;> = {0};

//--- Scanlines ---
int CRTSpacer0   <string UIName="|--------- Scanlines"; int UIMin=0; int UIMax=0;> = {0};
float  UICRT_ScanIntensity <string UIName="|- CRT - Scanline Intensity";    float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.25};
float  UICRT_ScanWidth     <string UIName="|- CRT - Scanline Width";        float UIMin=0.2;  float UIMax=2.0;  float UIStep=0.01; > = {1.0};
float  UICRT_ScanBrightBoost <string UIName="|- CRT - Bright Line Boost";   float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.15};

//--- Phosphor Mask ---
int CRTSpacer1   <string UIName="|--------- Phosphor Mask"; int UIMin=0; int UIMax=0;> = {0};
float  UICRT_MaskIntensity <string UIName="|- CRT - Mask Intensity";        float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.15};
int    UICRT_MaskType      <string UIName="|- CRT - Mask (0=Aperture 1=Slot 2=Shadow)"; int UIMin=0; int UIMax=2;                 > = {0};
float  UICRT_MaskScale     <string UIName="|- CRT - Mask Scale";            float UIMin=0.5;  float UIMax=4.0;  float UIStep=0.1;  > = {1.0};

//--- Screen Shape ---
int CRTSpacer2   <string UIName="|--------- Screen Shape"; int UIMin=0; int UIMax=0;> = {0};
float  UICRT_Curvature     <string UIName="|- CRT - Screen Curvature";      float UIMin=0.0;  float UIMax=0.5;  float UIStep=0.01; > = {0.03};
float  UICRT_CornerRadius  <string UIName="|- CRT - Corner Radius";         float UIMin=0.0;  float UIMax=0.15; float UIStep=0.005;> = {0.03};
float  UICRT_Overscan      <string UIName="|- CRT - Overscan";              float UIMin=0.0;  float UIMax=0.1;  float UIStep=0.005;> = {0.01};

//--- Bloom & Color ---
int CRTSpacer3   <string UIName="|--------- Bloom & Color"; int UIMin=0; int UIMax=0;> = {0};
float  UICRT_Bloom         <string UIName="|- CRT - Phosphor Bloom";        float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.10};
float  UICRT_Saturation    <string UIName="|- CRT - Color Saturation";      float UIMin=0.5;  float UIMax=1.5;  float UIStep=0.01; > = {1.05};
float  UICRT_Brightness    <string UIName="|- CRT - Brightness";            float UIMin=0.5;  float UIMax=1.5;  float UIStep=0.01; > = {1.0};
float  UICRT_Contrast      <string UIName="|- CRT - Contrast";              float UIMin=0.5;  float UIMax=2.0;  float UIStep=0.01; > = {1.05};


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 99

//----------------------------------------------------------------------------------------------//
//                     SKYRIMBRIDGE CRT ENHANCEMENTS                                            //
//----------------------------------------------------------------------------------------------//
// Requires SkyrimBridge SKSE plugin + SkyrimBridge.fxh in Helper/                              //
// Combat scanline reduction, interior adaptation, health feedback, menu awareness.             //
//----------------------------------------------------------------------------------------------//

#ifdef SKYRIMBRIDGE_FXH

UI_ELEMENT(SB_CRT_Header,      "    >>>>>>SKYRIMBRIDGE CRT<<<<<<")

//--- Combat Visibility ---
int SB_CRT_Spacer0   <string UIName="|--------- Combat Visibility"; int UIMin=0; int UIMax=0;> = {0};
float UISBCRT_CombatReduce     <string UIName="|- SB - Combat Scanline Reduction";     float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UISBCRT_CombatReductAmt  <string UIName="|- SB - Combat Reduction Amount";       float UIMin= 0.0; float UIMax= 0.7; > = { 0.4};

//--- Menu Awareness ---
int SB_CRT_Spacer1   <string UIName="|--------- Menu Awareness"; int UIMin=0; int UIMax=0;> = {0};
float UISBCRT_MenuReduce       <string UIName="|- SB - Reduce CRT In Menus";           float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UISBCRT_MenuReductAmt    <string UIName="|- SB - Menu Reduction Amount";         float UIMin= 0.0; float UIMax= 0.8; > = { 0.5};

//--- Health Feedback ---
int SB_CRT_Spacer2   <string UIName="|--------- Health Feedback"; int UIMin=0; int UIMax=0;> = {0};
float UISBCRT_HealthFeedback   <string UIName="|- SB - Health Phosphor Shift";         float UIMin= 0.0; float UIMax= 1.0; > = { 0.0};
float UISBCRT_HealthRedShift   <string UIName="|- SB - Health Red Shift Amount";       float UIMin= 0.0; float UIMax= 0.5; > = { 0.2};
float UISBCRT_HealthThresh     <string UIName="|- SB - Health Threshold";              float UIMin= 0.1; float UIMax= 0.5; > = { 0.3};
float UISBCRT_HealthFlicker    <string UIName="|- SB - Critical Health Flicker";       float UIMin= 0.0; float UIMax= 1.0; > = { 0.0};

//--- Interior/Exterior Adaptation ---
int SB_CRT_Spacer3   <string UIName="|--------- Interior Adaptation"; int UIMin=0; int UIMax=0;> = {0};
float UISBCRT_IntBrightBoost   <string UIName="|- SB - Interior Brightness Boost";     float UIMin= 0.0; float UIMax= 0.3; > = { 0.1};
float UISBCRT_IntCurvReduce    <string UIName="|- SB - Interior Curvature Reduction";  float UIMin= 0.0; float UIMax= 0.3; > = { 0.15};

//--- Weather Adaptation ---
int SB_CRT_Spacer4   <string UIName="|--------- Weather Adaptation"; int UIMin=0; int UIMax=0;> = {0};
float UISBCRT_CloudyDim        <string UIName="|- SB - Cloudy Weather Dimming";        float UIMin= 0.0; float UIMax= 0.15; > = { 0.05};

#endif //SKYRIMBRIDGE_FXH


#endif

#endif // ENBUI_CRT_FXH
