#ifndef ENBUI_FOG_FXH
#define ENBUI_FOG_FXH

//----------------------------------------------------------------------------------------------//
//                                                                                              //
//                UI Parameters for Atmospheric Fog  v4.1                                       //
//                                                                                              //
//  Companion to the enblens.fx atmospheric fog addon (Effect_AtmosphericFog.fxh).              //
//                                                                                              //
//  SHADERGROUP 12: Global parameters (included once, no TODIE)                                 //
//  SHADERGROUP 13: Day/Night/Interior parameters (included 3x with TODIE)                      //
//                                                                                              //
//  v4.1: Added all missing params that were previously expected from                           //
//        enbeffectprepass.fx declarations (Density, Enable, MaxFog,                            //
//        Height controls, Sky sampling controls, Inscatter).                                    //
//        Now self-contained — no cross-file dependency on prepass params.                       //
//                                                                                              //
//----------------------------------------------------------------------------------------------//


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#if SHADERGROUP == 12

//--- Master Enable: declared in enbUI_Lens.fxh (UIFOG_Enable) ---
int FogHeader   <string UIName="      >>>>>>ATMOSPHERIC FOG<<<<<<"; int UIMin=0; int UIMax=0;> = {0};

//--- Distance Controls ---
int FogSpacer0  <string UIName="|--------- Distance"; int UIMin=0; int UIMax=0;> = {0};
float  UIFOG_StartDist    <string UIName="|- Start Distance";                  float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.001; > = {0.5};
float  UIFOG_EndDist      <string UIName="|- End Distance";                    float UIMin=0.01; float UIMax=1.0;  float UIStep=0.001; > = {0.995};
float  UIFOG_DepthPower   <string UIName="|- Depth Power";                     float UIMin=0.1;  float UIMax=5.0;  float UIStep=0.01;  > = {1.0};
float  UIFOG_Curve        <string UIName="|- Density Curve";                   float UIMin=0.1;  float UIMax=10.0; float UIStep=0.01;  > = {1.5};
float  UIFOG_MaxFog       <string UIName="|- Maximum Opacity";                 float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01;  > = {0.85};

//--- Height Fog ---
int FogSpacer1  <string UIName="|--------- Height Fog"; int UIMin=0; int UIMax=0;> = {0};
bool   UIFOG_HeightEnable <string UIName="|- Enable Height Fog";                                                                         > = {true};
float  UIFOG_HeightFalloff<string UIName="|- Height Falloff";                  float UIMin=0.001;float UIMax=0.1;  float UIStep=0.001; > = {0.015};
float  UIFOG_HeightMix    <string UIName="|- Height Influence";                float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01;  > = {0.50};

//--- Sky Color Sampling ---
int FogSpacer2  <string UIName="|--------- Sky Sampling"; int UIMin=0; int UIMax=0;> = {0};
float  UIFOG_SkyThreshold <string UIName="|- Sky Depth Threshold";             float UIMin=0.99; float UIMax=1.0;  float UIStep=0.0001;> = {0.9998};
float  UIFOG_SkySampleY   <string UIName="|- Sky Sample Height";               float UIMin=0.0;  float UIMax=0.5;  float UIStep=0.01;  > = {0.25};
float  UIFOG_SkySpread    <string UIName="|- Sky Sample Spread";               float UIMin=0.01; float UIMax=0.5;  float UIStep=0.01;  > = {0.15};
float  UIFOG_SkyDesaturate<string UIName="|- Sky Desaturation";                float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01;  > = {0.30};

//--- Bloom Source ---
int FogSpacer3  <string UIName="|--------- Bloom Color Source"; int UIMin=0; int UIMax=0;> = {0};
float  UIFOG_BloomMix     <string UIName="|- Bloom Mix Ratio";                 float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01;  > = {0.15};
float  UIFOG_BloomDesat   <string UIName="|- Bloom Desaturation";              float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01;  > = {0.30};


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 13

float  SEPARATE_VAR(UIFOG_Density)   <string UIName=TO_STRING(|- TODIE - Fog Density);          float UIMin=0.0;  float UIMax=0.05; float UIStep=0.0005;> = {0.005};
float  SEPARATE_VAR(UIFOG_Intensity) <string UIName=TO_STRING(|- TODIE - Fog Intensity);         float UIMin=0.0;  float UIMax=5.0;  float UIStep=0.01;  > = {1.0};
float3 SEPARATE_VAR(UIFOG_Tint)      <string UIName=TO_STRING(|- TODIE - Fog Tint);              string UIWidget="Color";                                > = {1.0, 1.0, 1.0};
float  SEPARATE_VAR(UIFOG_TintWt)   <string UIName=TO_STRING(|- TODIE - Tint Weight);           float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01;  > = {0.0};
float  SEPARATE_VAR(UIFOG_Bright)    <string UIName=TO_STRING(|- TODIE - Brightness);            float UIMin=0.0;  float UIMax=5.0;  float UIStep=0.01;  > = {1.0};
float  SEPARATE_VAR(UIFOG_Scatter)   <string UIName=TO_STRING(|- TODIE - Inscatter);             float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01;  > = {0.15};
float  SEPARATE_VAR(UIFOG_HeightBase)<string UIName=TO_STRING(|- TODIE - Height Baseline);       float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01;  > = {0.50};

#endif

#endif // ENBUI_FOG_FXH
