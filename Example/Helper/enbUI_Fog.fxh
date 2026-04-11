#ifndef ENBUI_FOG_FXH
#define ENBUI_FOG_FXH

//----------------------------------------------------------------------------------------------//
//                                                                                              //
//                UI Parameters for Atmospheric Fog  v4.1                                       //
//                                                                                              //
//  v4.2.0 - IMPROVED with SkyrimBridge v3.0.0 SHADERGROUP 99 integration                      //
//                                                                                              //
//  Companion to the enblens.fx atmospheric fog addon (Effect_AtmosphericFog.fxh).              //
//                                                                                              //
//  SHADERGROUP 12: Global parameters (included once, no TODIE)                                 //
//  SHADERGROUP 13: Day/Night/Interior parameters (included 3x with TODIE)                      //
//  SHADERGROUP 99: SkyrimBridge Fog Integration [NEW]                                          //
//                                                                                              //
//  v4.1: Added all missing params that were previously expected from                           //
//        enbeffectprepass.fx declarations (Density, Enable, MaxFog,                            //
//        Height controls, Sky sampling controls, Inscatter).                                    //
//        Now self-contained — no cross-file dependency on prepass params.                       //
//                                                                                              //
//  v4.2: Added SHADERGROUP 99 with SkyrimBridge integration for weather smoothing,            //
//        moon tinting, combat reduction, interior ambient, and lightning flash.                //
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


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 99

//----------------------------------------------------------------------------------------------//
//                     SKYRIMBRIDGE FOG ENHANCEMENTS                                            //
//----------------------------------------------------------------------------------------------//
// Requires SkyrimBridge SKSE plugin + SkyrimBridge.fxh in Helper/                              //
// Weather transition smoothing, moon tinting, combat reduction, interior ambient.              //
//----------------------------------------------------------------------------------------------//

#ifdef SKYRIMBRIDGE_FXH

UI_ELEMENT(SB_FOG_Header,      "    >>>>>>SKYRIMBRIDGE FOG<<<<<<")

//--- Weather Transition Smoothing ---
int SB_FOG_Spacer0   <string UIName="|--------- Weather Smoothing"; int UIMin=0; int UIMax=0;> = {0};
float UISBFOG_WeatherSmooth    <string UIName="|- SB - Weather Transition Smoothing";  float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UISBFOG_TransitionDesatAmt <string UIName="|- SB - Transition Desaturation";     float UIMin= 0.0; float UIMax= 0.3; > = { 0.1};

//--- Moon Awareness ---
int SB_FOG_Spacer1   <string UIName="|--------- Moon Fog Tinting"; int UIMin=0; int UIMax=0;> = {0};
float UISBFOG_MoonTint         <string UIName="|- SB - Moon-Aware Fog Tinting";        float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UISBFOG_MoonInfluence    <string UIName="|- SB - Moon Tint Influence";           float UIMin= 0.0; float UIMax= 0.5; > = { 0.2};
float3 UISBFOG_MoonTintColor   <string UIName="|- SB - Moon Tint Color";               string UIWidget="Color";            > = {0.85, 0.9, 1.0};

//--- Combat Visibility ---
int SB_FOG_Spacer2   <string UIName="|--------- Combat Visibility"; int UIMin=0; int UIMax=0;> = {0};
float UISBFOG_CombatReduce     <string UIName="|- SB - Combat Fog Reduction";          float UIMin= 0.0; float UIMax= 1.0; > = { 0.0};
float UISBFOG_CombatReductAmt  <string UIName="|- SB - Combat Reduction Amount";       float UIMin= 0.0; float UIMax= 0.5; > = { 0.25};

//--- Snow Coverage ---
int SB_FOG_Spacer3   <string UIName="|--------- Snow & Weather"; int UIMin=0; int UIMax=0;> = {0};
float UISBFOG_SnowFog          <string UIName="|- SB - Snow Coverage Ground Fog";      float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UISBFOG_SnowFogBoost     <string UIName="|- SB - Snow Fog Boost Amount";         float UIMin= 0.0; float UIMax= 0.5; > = { 0.2};

//--- Lightning Flash ---
int SB_FOG_Spacer4   <string UIName="|--------- Lightning In Fog"; int UIMin=0; int UIMax=0;> = {0};
float UISBFOG_LightningFog     <string UIName="|- SB - Lightning Illuminates Fog";     float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UISBFOG_LightningStr     <string UIName="|- SB - Lightning Flash Strength";      float UIMin= 0.0; float UIMax= 1.0; > = { 0.4};

//--- Interior Fog ---
int SB_FOG_Spacer5   <string UIName="|--------- Interior Fog"; int UIMin=0; int UIMax=0;> = {0};
float UISBFOG_IntAmbient       <string UIName="|- SB - Interior Ambient Fog Color";    float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UISBFOG_IntAmbientBlend  <string UIName="|- SB - Ambient Color Blend";           float UIMin= 0.0; float UIMax= 0.6; > = { 0.4};

//--- Game Fog Anchoring ---
int SB_FOG_Spacer6   <string UIName="|--------- Game Fog Anchoring"; int UIMin=0; int UIMax=0;> = {0};
float UISBFOG_GameFogAnchor    <string UIName="|- SB - Game Fog Color Anchoring";      float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UISBFOG_GameFogBlend     <string UIName="|- SB - Game Fog Blend Weight";         float UIMin= 0.1; float UIMax= 0.5; > = { 0.3};

#endif //SKYRIMBRIDGE_FXH


#endif

#endif // ENBUI_FOG_FXH
