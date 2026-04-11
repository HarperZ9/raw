//----------------------------------------------------------------------------------------------//
//                                                                                              //
//                UI Parameters for CRT Display Simulation  v1.0                                //
//                by LonelyKitsuune / Extended by Zain Dana Harper                              //
//                                                                                              //
//  SHADERGROUP 14: All CRT parameters (no DNI — display simulation is time-independent)        //
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

#endif
