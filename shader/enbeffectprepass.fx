//----------------------------------------------------------------------------------------------//
//                                                                                              //
//               enbeffectprepass.fx - Screen-Space Rendering Pipeline                          //
//                    GTAO / SSGI / Contact Shadows / SSS / Clarity /                           //
//                        Micro-Detail / God Rays / Fog / Watercolor                            //
//                   for Skyrim SE ENB (DirectX 11 Shader Model 5)                              //
//                                                                                              //
//         Physically-motivated rendering pipeline by Zain Dana Harper - Feb 2026               //
//                            Boris Vorontsov for ENBSeries                                     //
//                                                                                              //
//----------------------------------------------------------------------------------------------//
//                                                                                              //
//  v3.0.0 - Ground-up rewrite with native SkyrimBridge v3.0.0 integration                     //
//                                                                                              //
//  Architecture: 12 passes across 8 techniques                                                 //
//                                                                                              //
//    Tech 0 (two-pass -> RenderTargetRGBA64):                                                  //
//        Pass 0: Clear + Horizontal SSS diffusion (Christensen-Burley, 12-tap)                 //
//        Pass 1: Vertical SSS diffusion                                                        //
//    Tech 1: SSS final composite -> TextureColor                                               //
//    Tech 2 (two-pass -> RenderTargetRGBA64):                                                  //
//        Pass 0: Clear                                                                         //
//        Pass 1: GTAO (16-sample hard cap, 4 slices x 4 steps)                                //
//    Tech 3 (two-pass -> RenderTargetRGBA64F):                                                 //
//        Pass 0: Clear                                                                         //
//        Pass 1: SSGI (8-sample golden angle)                                                  //
//    Tech 4 (two-pass -> RenderTargetR16F):                                                    //
//        Pass 0: Clear                                                                         //
//        Pass 1: Contact shadows (16-step hard cap)                                            //
//    Tech 5: Effects composite (AO + GI + CS + detail + clarity)                               //
//    Tech 6: Volumetric god rays + atmospheric fog composite                                   //
//    Tech 7: Watercolor filter (anisotropic Kuwahara)                                          //
//                                                                                              //
//  SSS: Christensen-Burley normalized diffusion, 12-tap separable                              //
//  GTAO: Jimenez 2016, 4 slices x 4 steps = 16 samples                                       //
//  SSGI: Golden-angle low-discrepancy, 8 samples                                               //
//  Contact Shadows: Ray-march + binary refine, 16 steps hard cap                               //
//                                                                                              //
//----------------------------------------------------------------------------------------------//


//=============================================================================//
//                                OPTIONS                                      //
//=============================================================================//

#define ENABLE_TOOLS 0


//=============================================================================//
//                           ENB EXTERNAL PARAMETERS                           //
//=============================================================================//

float4 Timer;
float4 ScreenSize;
float  AdaptiveQuality;
float4 TimeOfDay1;      // x=dawn, y=sunrise, z=day, w=sunset
float4 TimeOfDay2;      // x=dusk, y=night
float  ENightDayFactor;  // 0=night, 1=day
float  EInteriorFactor;  // 0=exterior, 1=interior
float  FieldOfView;
float4 Weather;
float4 SunDirection;
float4 SunColor;

float4 tempF1;
float4 tempF2;
float4 tempF3;
float4 tempInfo1;
float4 tempInfo2;


//=============================================================================//
//                    SkyrimBridge External Data Parameters                     //
//=============================================================================//
// Inline params only — full header (102 float4s) overflows constant buffer
#define SKYRIMBRIDGE_FXH 1
float4 SB_Render_Frame;        // .x = frameCount
float4 SB_Camera_Info;         // .x = fov, .y = near, .z = far, .w = aspect
float4 SB_Atmos_Ambient;       // .rgb = ambient color
float4 SB_Atmos_Sunlight;      // .rgb = sunlight color
float4 SB_Fog_FarColor;        // .rgb = far fog color
float4 SB_Interior_DirDir;     // .xyz = interior directional light direction
float4 SB_Interior_Flags;      // .x = isInterior
float4 SB_Lightning;           // .z = flashIntensity
float4 SB_Masser_Direction;    // .xyz = masser direction
float4 SB_Player_Combat;       // .x = combatIntensity
float4 SB_Player_Position;     // .xyz = position, .w = worldY
float4 SB_Precipitation;       // .y = intensity
float4 SB_Sun_Direction;       // .xyz = sun direction
float4 SB_Sun_NDC;             // .xy = NDC position
float4 SB_Time;                // .x = gameHour, .y = sunrise, .z = sunset
float4 SB_Weather_Transition;  // .x = transition progress
bool SB_IsActive() { return SB_Render_Frame.x > 0.0; }
float SB_LinearizeDepth(float rawDepth) {
    float n = SB_Camera_Info.y;
    float f = SB_Camera_Info.z;
    return n * f / (f - rawDepth * (f - n));
}
bool SB_IsNight() { return SB_Time.x < SB_Time.y || SB_Time.x > SB_Time.z; }
float2 SB_SunScreenUV() { return SB_Sun_NDC.xy * float2(0.5, -0.5) + 0.5; }
float SB_SmoothWeatherTransition() { return SB_Weather_Transition.x; }


//=============================================================================//
//                              GAME TEXTURES                                  //
//=============================================================================//

Texture2D TextureOriginal;      // R16G16B16A16 HDR scene
Texture2D TextureColor;         // Previous technique output
Texture2D TextureDepth;         // R32F depth
Texture2D TextureJitter;        // Blue noise
Texture2D TextureMask;          // rgb=skin albedo, a=SSS flag
Texture2D TextureNormal;        // xyz=screen-space normals [0,1]
Texture2D TextureSunMask;       // Cloud occlusion

Texture2D RenderTargetRGBA32;
Texture2D RenderTargetRGBA64;
Texture2D RenderTargetRGBA64F;
Texture2D RenderTargetR16F;
Texture2D RenderTargetR32F;
Texture2D RenderTargetRGB32F;

// Read-back textures (ENB convention: write to RenderTarget*, read from Texture*)
Texture2D TextureRGBA32;
Texture2D TextureRGBA64;
Texture2D TextureRGBA64F;
Texture2D TextureR16F;

SamplerState Point_Sampler
{
    Filter   = MIN_MAG_MIP_POINT;
    AddressU = Clamp;
    AddressV = Clamp;
};

SamplerState Linear_Sampler
{
    Filter   = MIN_MAG_MIP_LINEAR;
    AddressU = Clamp;
    AddressV = Clamp;
};

#include "Helper/enbHelper_Common.fxh"
// PixelSize and ScreenRes provided by enbHelper_Common.fxh


//=============================================================================//
//                        INLINE UI PARAMETERS                                 //
//=============================================================================//

// =================== SSS ===================
int _spc00 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrSSS < string UIName = "=== SUBSURFACE SCATTERING (SSS) ==="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool  UISSS_Enable       < string UIName = "SSS | Enable"; > = true;
float UISSS_Radius        < string UIName = "SSS | Diffusion Radius"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 3.0; float UIStep = 0.01; > = 0.60;
float UISSS_ScatterR      < string UIName = "SSS | Scatter Dist Red";  string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 5.0; float UIStep = 0.01; > = 2.00;
float UISSS_ScatterG      < string UIName = "SSS | Scatter Dist Green"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 5.0; float UIStep = 0.01; > = 0.80;
float UISSS_ScatterB      < string UIName = "SSS | Scatter Dist Blue"; string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 5.0; float UIStep = 0.01; > = 0.40;
float UISSS_DepthScale    < string UIName = "SSS | Depth Scaling";    string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 10.0; float UIStep = 0.1; > = 3.0;
float UISSS_NormPow       < string UIName = "SSS | Normal Rejection"; string UIWidget = "spinner"; float UIMin = 0.5; float UIMax = 8.0; float UIStep = 0.1; > = 4.0;
float UISSS_ShadowFade    < string UIName = "SSS | Shadow Bleed";     string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.35;
float3 UISSS_WrapTint     < string UIName = "SSS | Wrap Light Tint"; string UIWidget = "color"; > = {1.0, 0.85, 0.7};

// =================== GTAO ===================
int _spc10 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrGTAO < string UIName = "=== AMBIENT OCCLUSION (GTAO) ==="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool  UIGTAO_Enable       < string UIName = "AO | Enable"; > = true;
float UIGTAO_Intensity    < string UIName = "AO | Intensity";        string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 3.0;   float UIStep = 0.01; > = 1.5;
float UIGTAO_Radius       < string UIName = "AO | World Radius";     string UIWidget = "spinner"; float UIMin = 0.1;  float UIMax = 8.0;   float UIStep = 0.1;  > = 2.0;
float UIGTAO_Power        < string UIName = "AO | Power Curve";      string UIWidget = "spinner"; float UIMin = 0.5;  float UIMax = 4.0;   float UIStep = 0.1;  > = 1.2;
float UIGTAO_DepthFade    < string UIName = "AO | Depth Fade Start"; string UIWidget = "spinner"; float UIMin = 10.0; float UIMax = 500.0; float UIStep = 5.0;  > = 200.0;
float UIGTAO_ThickBias    < string UIName = "AO | Thickness Bias";   string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 1.0;   float UIStep = 0.01; > = 0.15;
float UIGTAO_NormBias     < string UIName = "AO | Normal Bias";      string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 0.5;   float UIStep = 0.01; > = 0.05;
bool  UIGTAO_Temporal     < string UIName = "AO | Temporal Rotation"; > = true;
bool  UIGTAO_MultiBounce  < string UIName = "AO | Multi-Bounce Approx."; > = true;
float3 UIGTAO_Tint        < string UIName = "AO | Shadow Tint"; string UIWidget = "color"; > = {0.7, 0.75, 0.85};

// =================== SSGI ===================
int _spc20 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrSSGI < string UIName = "=== GLOBAL ILLUMINATION (SSGI) ==="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool  UISSGI_Enable       < string UIName = "GI | Enable"; > = true;
float UISSGI_Intensity    < string UIName = "GI | Intensity";        string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 5.0;   float UIStep = 0.01; > = 1.5;
float UISSGI_Radius       < string UIName = "GI | Sample Radius";    string UIWidget = "spinner"; float UIMin = 0.5;  float UIMax = 12.0;  float UIStep = 0.1;  > = 4.0;
float UISSGI_DepthReject  < string UIName = "GI | Depth Rejection";  string UIWidget = "spinner"; float UIMin = 0.5;  float UIMax = 50.0;  float UIStep = 0.5;  > = 5.0;
float UISSGI_NormWeight   < string UIName = "GI | SSDO Balance";     string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 2.0;   float UIStep = 0.01; > = 1.0;
float UISSGI_ColorBleed   < string UIName = "GI | Color Bleed";      string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 2.0;   float UIStep = 0.01; > = 1.2;
float UISSGI_DepthFade    < string UIName = "GI | Depth Fade Start"; string UIWidget = "spinner"; float UIMin = 10.0; float UIMax = 500.0; float UIStep = 5.0;  > = 200.0;
bool  UISSGI_Temporal     < string UIName = "GI | Temporal Jitter"; > = true;
float UISSGI_Saturation   < string UIName = "GI | Bounce Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 3.0;   float UIStep = 0.01; > = 1.3;

// =================== Contact Shadows ===================
int _spc30 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrCS < string UIName = "=== CONTACT SHADOWS ==="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool  UICS_Enable         < string UIName = "CS | Enable"; > = true;
float UICS_Intensity      < string UIName = "CS | Intensity";        string UIWidget = "spinner"; float UIMin = 0.0;   float UIMax = 2.0;   float UIStep = 0.01; > = 1.0;
float UICS_MaxDist        < string UIName = "CS | Max Ray Distance"; string UIWidget = "spinner"; float UIMin = 0.5;   float UIMax = 20.0;  float UIStep = 0.5;  > = 5.0;
float UICS_Thickness      < string UIName = "CS | Occluder Thickness"; string UIWidget = "spinner"; float UIMin = 0.001; float UIMax = 0.5;  float UIStep = 0.005; > = 0.04;
float UICS_DepthFade      < string UIName = "CS | Depth Fade";       string UIWidget = "spinner"; float UIMin = 10.0;  float UIMax = 500.0; float UIStep = 5.0;  > = 200.0;
float UICS_NdLFade        < string UIName = "CS | NdL Fade Power";   string UIWidget = "spinner"; float UIMin = 0.5;   float UIMax = 4.0;   float UIStep = 0.1;  > = 1.5;
bool  UICS_Temporal       < string UIName = "CS | Temporal Dither"; > = true;
float UICS_Softness       < string UIName = "CS | Shadow Softness";  string UIWidget = "spinner"; float UIMin = 0.0;   float UIMax = 1.0;   float UIStep = 0.01; > = 0.5;
bool  UICS_MoonAware      < string UIName = "CS | Moon-Aware (Night)"; > = true;
bool  UICS_WeatherSoften  < string UIName = "CS | Rain Softening"; > = true;

// =================== SSR ===================
int _spc31 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrSSR < string UIName = "=== SCREEN-SPACE REFLECTIONS ==="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool  UISSR_Enable        < string UIName = "SSR | Enable"; > = true;
float UISSR_Intensity     < string UIName = "SSR | Intensity";       string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 2.0;   float UIStep = 0.01; > = 0.7;
float UISSR_MaxDist       < string UIName = "SSR | Max Distance";    string UIWidget = "spinner"; float UIMin = 5.0;  float UIMax = 200.0; float UIStep = 1.0;  > = 60.0;
int   UISSR_Steps         < string UIName = "SSR | Ray Steps";       string UIWidget = "spinner"; int   UIMin = 16;   int   UIMax = 64;                         > = 32;
int   UISSR_RefineSteps   < string UIName = "SSR | Refine Steps";    string UIWidget = "spinner"; int   UIMin = 0;    int   UIMax = 8;                          > = 4;
float UISSR_Thickness     < string UIName = "SSR | Hit Thickness";   string UIWidget = "spinner"; float UIMin = 0.1;  float UIMax = 10.0;  float UIStep = 0.1;  > = 2.0;
float UISSR_DepthFade     < string UIName = "SSR | Depth Fade";      string UIWidget = "spinner"; float UIMin = 10.0; float UIMax = 500.0; float UIStep = 5.0;  > = 250.0;
float UISSR_EdgeFade      < string UIName = "SSR | Edge Fade";       string UIWidget = "spinner"; float UIMin = 0.01; float UIMax = 0.3;   float UIStep = 0.01; > = 0.08;
float UISSR_FresnelPow    < string UIName = "SSR | Fresnel Power";   string UIWidget = "spinner"; float UIMin = 0.5;  float UIMax = 8.0;   float UIStep = 0.1;  > = 3.0;
bool  UISSR_Temporal      < string UIName = "SSR | Temporal Jitter"; > = true;
bool  UISSR_WetnessAware  < string UIName = "SSR | Wetness Boost"; > = true;

// =================== God Rays ===================
int _spc40 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrGR < string UIName = "=== GOD RAYS ==="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool  UIGR_Enable         < string UIName = "Rays | Enable"; > = true;
float UIGR_Intensity      < string UIName = "Rays | Intensity";      string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 3.0;  float UIStep = 0.01; > = 0.50;
float UIGR_Density        < string UIName = "Rays | Density";        string UIWidget = "spinner"; float UIMin = 0.3;  float UIMax = 2.0;  float UIStep = 0.01; > = 0.80;
float UIGR_Decay          < string UIName = "Rays | Decay";          string UIWidget = "spinner"; float UIMin = 0.90; float UIMax = 1.0;   float UIStep = 0.001;> = 0.970;
float UIGR_Exposure       < string UIName = "Rays | Exposure";       string UIWidget = "spinner"; float UIMin = 0.01; float UIMax = 1.0;  float UIStep = 0.01; > = 0.20;
float UIGR_Threshold      < string UIName = "Rays | Sky Threshold";  string UIWidget = "spinner"; float UIMin = 0.5;  float UIMax = 1.0;  float UIStep = 0.01; > = 0.92;
float3 UIGR_Tint          < string UIName = "Rays | Sun Tint"; string UIWidget = "color"; > = {1.0, 0.95, 0.85};
bool  UIGR_CombatReactive < string UIName = "Rays | Combat Reactive"; > = false;
float UIGR_CombatBoost    < string UIName = "Rays | Combat Boost";   string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.05; > = 0.3;

// =================== Atmospheric Fog ===================
int _spc50 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrFOG < string UIName = "=== ATMOSPHERIC FOG ==="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool  UIFOG_Enable        < string UIName = "Fog | Enable"; > = false;
float UIFOG_Density       < string UIName = "Fog | Density";         string UIWidget = "spinner"; float UIMin = 0.0;   float UIMax = 0.05;  float UIStep = 0.0005;> = 0.005;
float UIFOG_MaxDist       < string UIName = "Fog | Max Distance";    string UIWidget = "spinner"; float UIMin = 50.0;  float UIMax = 3000.0;float UIStep = 10.0; > = 800.0;
float UIFOG_MaxOpacity    < string UIName = "Fog | Max Opacity";     string UIWidget = "spinner"; float UIMin = 0.0;   float UIMax = 1.0;   float UIStep = 0.01; > = 0.85;
float UIFOG_SkyThreshold  < string UIName = "Fog | Sky Threshold";   string UIWidget = "spinner"; float UIMin = 0.90;  float UIMax = 1.0;   float UIStep = 0.001;> = 0.98;
float UIFOG_HeightFalloff < string UIName = "Fog | Height Falloff";  string UIWidget = "spinner"; float UIMin = 0.001; float UIMax = 0.1;   float UIStep = 0.001; > = 0.015;
float UIFOG_BaseHeight    < string UIName = "Fog | Base Height";     string UIWidget = "spinner"; float UIMin = -500.0;float UIMax = 500.0; float UIStep = 5.0;  > = 0.0;
bool  UIFOG_SkyColorEnable< string UIName = "Fog | Sample Sky Color"; > = true;
float UIFOG_SkySampleY    < string UIName = "Fog | Sky Sample Height";string UIWidget = "spinner"; float UIMin = 0.0;   float UIMax = 0.5;   float UIStep = 0.01; > = 0.25;
float UIFOG_SkySpread     < string UIName = "Fog | Sky Sample Spread";string UIWidget = "spinner"; float UIMin = 0.01;  float UIMax = 0.5;   float UIStep = 0.01; > = 0.15;
float UIFOG_SkyDesat      < string UIName = "Fog | Sky Desaturation"; string UIWidget = "spinner"; float UIMin = 0.0;   float UIMax = 1.0;   float UIStep = 0.01; > = 0.30;
float3 UIFOG_Tint         < string UIName = "Fog | Tint Color"; string UIWidget = "color"; > = {1.0, 1.0, 1.0};
float UIFOG_TintWeight    < string UIName = "Fog | Tint Weight";     string UIWidget = "spinner"; float UIMin = 0.0;   float UIMax = 1.0;   float UIStep = 0.01; > = 0.0;
float UIFOG_Brightness    < string UIName = "Fog | Brightness";      string UIWidget = "spinner"; float UIMin = 0.0;   float UIMax = 3.0;   float UIStep = 0.01; > = 1.0;
float3 UIFOG_ColorFallback< string UIName = "Fog | Fallback Color"; string UIWidget = "color"; > = {0.65, 0.70, 0.78};
float UIFOG_Inscatter     < string UIName = "Fog | Inscatter";       string UIWidget = "spinner"; float UIMin = 0.0;   float UIMax = 1.0;   float UIStep = 0.01; > = 0.15;
float3 UIFOG_InscatterTint< string UIName = "Fog | Inscatter Color"; string UIWidget = "color"; > = {1.0, 0.90, 0.70};
float UIFOG_AerialDesat   < string UIName = "Fog | Aerial Desat";    string UIWidget = "spinner"; float UIMin = 0.0;   float UIMax = 0.5;   float UIStep = 0.01; > = 0.15;
bool  UIFOG_WeatherSmooth < string UIName = "Fog | Weather Smoothing"; > = true;

// =================== Skin Micro-Detail ===================
int _spc60 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrSMD < string UIName = "=== SKIN MICRO-DETAIL ==="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool  UISMD_Enable        < string UIName = "Detail | Enable"; > = true;
float UISMD_Intensity     < string UIName = "Detail | Pore Intensity"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 3.0; float UIStep = 0.01; > = 0.6;
float UISMD_BlurRadius    < string UIName = "Detail | Blur Radius";    string UIWidget = "spinner"; float UIMin = 1.0; float UIMax = 6.0; float UIStep = 0.1;  > = 2.5;
float UISMD_HighPassGain  < string UIName = "Detail | High-Pass Gain"; string UIWidget = "spinner"; float UIMin = 0.5; float UIMax = 5.0; float UIStep = 0.1;  > = 2.0;
float UISMD_SkinMaskPow   < string UIName = "Detail | Mask Tightness"; string UIWidget = "spinner"; float UIMin = 0.5; float UIMax = 4.0; float UIStep = 0.1;  > = 2.0;
bool  UISMD_LumaOnly      < string UIName = "Detail | Luminance Only"; > = true;

// =================== Clarity ===================
int _spc70 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrCLR < string UIName = "=== CLARITY / LOCAL CONTRAST ==="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool  UICLR_Enable        < string UIName = "Clarity | Enable"; > = true;
float UICLR_Amount        < string UIName = "Clarity | Amount";       string UIWidget = "spinner"; float UIMin = -1.0; float UIMax = 2.0;  float UIStep = 0.01; > = 0.50;
float UICLR_MidPoint      < string UIName = "Clarity | Mid-Point";    string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 1.0;  float UIStep = 0.01; > = 0.5;
float UICLR_Radius        < string UIName = "Clarity | Blur Radius";  string UIWidget = "spinner"; float UIMin = 2.0;  float UIMax = 16.0; float UIStep = 0.5;  > = 8.0;
float UICLR_DepthAware    < string UIName = "Clarity | Depth Aware";  string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 1.0;  float UIStep = 0.01; > = 0.7;
bool  UICLR_PreserveSkin  < string UIName = "Clarity | Preserve Skin"; > = true;

// =================== Watercolor Filter ===================
int _spc80 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrPAINT < string UIName = "=== WATERCOLOR FILTER ==="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool  UIPAINT_Enable       < string UIName = "WC | Enable"; > = true;
float UIPAINT_Intensity    < string UIName = "WC | Intensity";       string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 1.0;   float UIStep = 0.01; > = 0.90;
float UIPAINT_Radius       < string UIName = "WC | Wash Radius";     string UIWidget = "spinner"; float UIMin = 1.0;  float UIMax = 12.0;  float UIStep = 0.1;  > = 5.0;
int   UIPAINT_Sectors      < string UIName = "WC | Sectors";         string UIWidget = "spinner"; int   UIMin = 4;    int   UIMax = 8;                          > = 8;
float UIPAINT_Sharpness    < string UIName = "WC | Wash Flatness";   string UIWidget = "spinner"; float UIMin = 1.0;  float UIMax = 32.0;  float UIStep = 0.5;  > = 14.0;
float UIPAINT_Anisotropy   < string UIName = "WC | Flow Anisotropy"; string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 1.0;   float UIStep = 0.01; > = 0.8;
float UIPAINT_Hardness     < string UIName = "WC | Brush Softness";  string UIWidget = "spinner"; float UIMin = 0.5;  float UIMax = 8.0;   float UIStep = 0.1;  > = 1.5;
float UIPAINT_EdgeInk      < string UIName = "WC | Edge Ink";        string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 2.0;   float UIStep = 0.01; > = 0.50;
float UIPAINT_EdgeWidth    < string UIName = "WC | Edge Width";      string UIWidget = "spinner"; float UIMin = 0.5;  float UIMax = 4.0;   float UIStep = 0.1;  > = 1.5;
float UIPAINT_Posterize    < string UIName = "WC | Color Levels";    string UIWidget = "spinner"; float UIMin = 4.0;  float UIMax = 48.0;  float UIStep = 1.0;  > = 18.0;
float UIPAINT_PaperGrain   < string UIName = "WC | Paper Grain";     string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 1.0;   float UIStep = 0.01; > = 0.12;
float UIPAINT_WetEdge      < string UIName = "WC | Wet Edge";        string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 2.0;   float UIStep = 0.01; > = 0.35;
float UIPAINT_Bleed        < string UIName = "WC | Pigment Bleed";   string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 1.0;   float UIStep = 0.01; > = 0.25;
float UIPAINT_DepthFade    < string UIName = "WC | Depth Fade";      string UIWidget = "spinner"; float UIMin = 10.0; float UIMax = 500.0; float UIStep = 5.0;  > = 300.0;
bool  UIPAINT_PreserveSkin < string UIName = "WC | Preserve Skin"; > = true;
float UIPAINT_SkinReduce   < string UIName = "WC | Skin Reduction";  string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 1.0;   float UIStep = 0.01; > = 0.6;

// =================== Realism Extensions ===================
int _spc90 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrREAL < string UIName = "=== REALISM EXTENSIONS ==="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool  UICA_Enable         < string UIName = "CA | Chromatic Adaptation"; > = false;
float UICA_Strength       < string UIName = "CA | Adaptation Strength"; string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 1.0;  float UIStep = 0.01; > = 0.3;
float3 UICA_TargetWhite   < string UIName = "CA | Target White Point"; string UIWidget = "color"; > = {1.0, 1.0, 1.0};

bool  UIMS_Enable         < string UIName = "MSh | Micro-Shadow Enable"; > = true;
float UIMS_Intensity      < string UIName = "MSh | Intensity";     string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 2.0;  float UIStep = 0.01; > = 0.4;
float UIMS_Radius         < string UIName = "MSh | Detail Radius"; string UIWidget = "spinner"; float UIMin = 1.0;  float UIMax = 6.0;  float UIStep = 0.5;  > = 2.0;
float UIMS_DepthFade      < string UIName = "MSh | Depth Fade";    string UIWidget = "spinner"; float UIMin = 10.0; float UIMax = 300.0; float UIStep = 5.0;  > = 100.0;


//=============================================================================//
//                              STRUCTS                                        //
//=============================================================================//

struct VS_OUTPUT
{
    float4 pos      : SV_POSITION;
    float2 uv       : TEXCOORD0;
};


//=============================================================================//
//                          DEPTH HELPERS                                       //
//=============================================================================//

// Correct depth linearization: SB when active, fallback otherwise
// Skyrim uses nearClip=0.5 (not 1.0), farClip ~15000-30000
float GetLinearDepth(float rawDepth)
{
    if (SB_IsActive()) return SB_LinearizeDepth(rawDepth);
    return FastLinDepth(rawDepth, 3000.0);
}

float GetLinearDepthFar(float rawDepth, float farPlane)
{
    if (SB_IsActive()) return SB_LinearizeDepth(rawDepth);
    return FastLinDepth(rawDepth, farPlane);
}


//=============================================================================//
//                  SB-AWARE LIGHT DIRECTION                                    //
//=============================================================================//

float3 GetPrimaryLightDirection()
{
    if (SB_IsActive())
    {
        if (SB_Interior_Flags.x > 0.5)
            return normalize(SB_Interior_DirDir.xyz);
        if (SB_IsNight())
            return normalize(SB_Masser_Direction.xyz);
        return normalize(SB_Sun_Direction.xyz);
    }
    return normalize(SunDirection.xyz);
}

float3 GetSunlightColor()
{
    if (SB_IsActive()) return SB_Atmos_Sunlight.rgb;
    return SunColor.xyz;
}


//=============================================================================//
//                          VIEW-SPACE HELPERS                                  //
//=============================================================================//

float3 ViewPosFromUV(float2 uv, float viewZ)
{
    float fov = FieldOfView;
    float aspect = ScreenSize.z;
    float2 ndc = uv * 2.0 - 1.0;
    float tanHalf = tan(fov * 0.5);
    return float3(ndc.x * tanHalf * aspect * viewZ, -ndc.y * tanHalf * viewZ, -viewZ);
}

float2 ProjectToScreen(float3 viewPos)
{
    float fov = FieldOfView;
    float aspect = ScreenSize.z;
    float tanHalf = tan(fov * 0.5);
    float2 proj;
    proj.x = viewPos.x / (-viewPos.z * tanHalf * aspect);
    proj.y = -viewPos.y / (-viewPos.z * tanHalf);
    return proj * 0.5 + 0.5;
}

float3 NormalFromDepth(float2 uv)
{
    float c  = GetLinearDepth(TextureDepth.SampleLevel(Point_Sampler, uv, 0).x);
    float l  = GetLinearDepth(TextureDepth.SampleLevel(Point_Sampler, uv + float2(-PixelSize.x, 0), 0).x);
    float r  = GetLinearDepth(TextureDepth.SampleLevel(Point_Sampler, uv + float2( PixelSize.x, 0), 0).x);
    float u  = GetLinearDepth(TextureDepth.SampleLevel(Point_Sampler, uv + float2(0, -PixelSize.y), 0).x);
    float d  = GetLinearDepth(TextureDepth.SampleLevel(Point_Sampler, uv + float2(0,  PixelSize.y), 0).x);

    // Use smallest derivative to avoid edge artifacts
    float dx = (abs(l - c) < abs(r - c)) ? (c - l) : (r - c);
    float dy = (abs(u - c) < abs(d - c)) ? (c - u) : (d - c);

    float3 viewPos = ViewPosFromUV(uv, c);
    float3 ddx_pos = float3(PixelSize.x * 2.0, 0, dx);
    float3 ddy_pos = float3(0, PixelSize.y * 2.0, dy);

    return normalize(cross(ddy_pos, ddx_pos));
}


//=============================================================================//
//                       SKY COLOR ESTIMATION                                   //
//=============================================================================//

static const float2 SkyPoissonDisc[8] = {
    float2(-0.94201, -0.39906), float2( 0.94558, -0.76891),
    float2(-0.09418, -0.92938), float2( 0.34495,  0.29387),
    float2(-0.91588,  0.45771), float2(-0.81544, -0.87912),
    float2( 0.19984,  0.78641), float2( 0.44323, -0.97511)
};

float3 EstimateSkyColor(float2 center, float spread, float desat, float skyThreshLin)
{
    float3 sum = 0.0;
    float  count = 0.0;

    [unroll]
    for (int i = 0; i < 8; i++)
    {
        float2 sampleUV = center + SkyPoissonDisc[i] * spread;
        sampleUV = saturate(sampleUV);

        float d = TextureDepth.SampleLevel(Point_Sampler, sampleUV, 0).x;
        if (d >= skyThreshLin)
        {
            float3 col = TextureColor.SampleLevel(Linear_Sampler, sampleUV, 0).rgb;
            col = col / (1.0 + dot(col, K_LUM));  // Reinhard soft-clamp
            sum += col;
            count += 1.0;
        }
    }

    if (count < 1.0) return 0.0;
    sum /= count;

    float luma = dot(sum, K_LUM);
    sum = lerp(luma, sum, 1.0 - desat);
    return sum;
}


//=============================================================================//
//                     VERTEX / PIXEL SHADERS                                   //
//=============================================================================//

// VS_Basic and PS_Blank provided by enbHelper_Common.fxh


//=============================================================================//
//  PASS 0-1: SSS Horizontal + Vertical (Christensen-Burley, 12-tap)           //
//=============================================================================//

#define SSS_TAPS 12

static const float SSS_Offsets[SSS_TAPS] = {
     1.0,  -1.0,
     2.0,  -2.0,
     3.25, -3.25,
     5.0,  -5.0,
     7.5,  -7.5,
    11.0, -11.0
};

static const float3 SSS_Weights[SSS_TAPS] = {
    float3(0.1000, 0.0930, 0.0560),
    float3(0.1000, 0.0930, 0.0560),
    float3(0.0780, 0.0650, 0.0280),
    float3(0.0780, 0.0650, 0.0280),
    float3(0.0550, 0.0370, 0.0115),
    float3(0.0550, 0.0370, 0.0115),
    float3(0.0340, 0.0175, 0.0042),
    float3(0.0340, 0.0175, 0.0042),
    float3(0.0175, 0.0070, 0.0012),
    float3(0.0175, 0.0070, 0.0012),
    float3(0.0055, 0.0013, 0.0002),
    float3(0.0055, 0.0013, 0.0002)
};

static const float3 SSS_CenterWeight = float3(0.2300, 0.4400, 0.6200);

float4 PS_SSSBlur(float4 pos : SV_POSITION, float4 txcoord : TEXCOORD0, float2 axis) : SV_Target
{
    float2 uv = txcoord.xy;
    float4 center = TextureColor.SampleLevel(Point_Sampler, uv, 0);
    float4 mask = TextureMask.SampleLevel(Point_Sampler, uv, 0);

    // SSS mask: alpha channel indicates skin
    float sssAmount = mask.a;
    if (!UISSS_Enable || sssAmount < 0.01)
        return center;

    float depth = GetLinearDepth(TextureDepth.SampleLevel(Point_Sampler, uv, 0).x);

    // Depth-scaled radius (world-space consistent)
    float radius = UISSS_Radius / max(depth * UISSS_DepthScale, 0.1);
    radius *= sssAmount;

    // Interior: reduce radius 30%
    if (SB_IsActive() && SB_Interior_Flags.x > 0.5)
        radius *= 0.7;

    // Temporal IGN jitter
    float jitter = 1.0;
    if (UICS_Temporal)
    {
        float noise = InterleavedGradientNoise(pos.xy + Timer.x * 5.588238);
        jitter = 0.85 + noise * 0.3;
    }

    float3 centerN = TextureNormal.SampleLevel(Point_Sampler, uv, 0).xyz * 2.0 - 1.0;
    float3 totalColor = center.rgb * SSS_CenterWeight;
    float3 totalWeight = SSS_CenterWeight;

    [unroll]
    for (int i = 0; i < SSS_TAPS; i++)
    {
        float2 offset = axis * SSS_Offsets[i] * radius * jitter * PixelSize;
        float2 sampleUV = uv + offset;

        float3 sampleCol = TextureColor.SampleLevel(Linear_Sampler, sampleUV, 0).rgb;
        float  sampleDepth = GetLinearDepth(TextureDepth.SampleLevel(Point_Sampler, sampleUV, 0).x);
        float3 sampleN = TextureNormal.SampleLevel(Point_Sampler, sampleUV, 0).xyz * 2.0 - 1.0;

        // Depth rejection: prevent bleeding across depth discontinuities
        float depthDiff = abs(sampleDepth - depth);
        float depthWeight = 1.0 - saturate(depthDiff / (depth * 0.1 + 0.01));

        // Normal bilateral: reject cross-surface bleeding
        float normalWeight = pow(abs(saturate(dot(centerN, sampleN))), UISSS_NormPow);

        // Shadow asymmetry: lit samples bleed into shadow but not reverse
        float lumaDiff = dot(sampleCol, K_LUM) - dot(center.rgb, K_LUM);
        float shadowWeight = saturate(1.0 + lumaDiff * UISSS_ShadowFade * 3.0);

        float3 w = SSS_Weights[i] * depthWeight * normalWeight * shadowWeight;
        totalColor += sampleCol * w;
        totalWeight += w;
    }

    float3 result = totalColor / max(totalWeight, DELTA);

    // Wrap lighting tint with SB sunlight
    if (SB_IsActive())
    {
        float3 sunCol = SB_Atmos_Sunlight.rgb;
        float sunLuma = dot(sunCol, K_LUM);
        if (sunLuma > 0.01)
            result *= lerp(1.0, sunCol / sunLuma * UISSS_WrapTint, sssAmount * 0.3);
    }

    return float4(result, center.a);
}

float4 PS_SSSBlurH(float4 pos : SV_POSITION, float4 txcoord : TEXCOORD0) : SV_Target
{
    return PS_SSSBlur(pos, txcoord, float2(1.0, 0.0));
}

float4 PS_SSSBlurV(float4 pos : SV_POSITION, float4 txcoord : TEXCOORD0) : SV_Target
{
    return PS_SSSBlur(pos, txcoord, float2(0.0, 1.0));
}


//=============================================================================//
//  PASS 2: SSS Final Composite                                                //
//=============================================================================//

float4 PS_SSSComposite(float4 pos : SV_POSITION, float4 txcoord : TEXCOORD0) : SV_Target
{
    float2 uv = txcoord.xy;
    float4 original = TextureOriginal.SampleLevel(Point_Sampler, uv, 0);
    float4 blurred = TextureColor.SampleLevel(Point_Sampler, uv, 0);
    float4 mask = TextureMask.SampleLevel(Point_Sampler, uv, 0);

    float sssAmount = mask.a;
    if (!UISSS_Enable || sssAmount < 0.01)
        return original;

    // Specular preservation: don't blur highlights
    float specular = EstimateSpecular(original.rgb, blurred.rgb);

    // Blend: use blurred for diffuse, keep original specular
    float3 result = lerp(blurred.rgb, original.rgb, specular);
    result = lerp(original.rgb, result, sssAmount);

    return float4(result, original.a);
}


//=============================================================================//
//  PASS 3: GTAO — Ground Truth Ambient Occlusion (16 samples: 4x4)           //
//=============================================================================//

static const int GTAO_SLICES = 4;
static const int GTAO_STEPS  = 4;

float4 PS_GTAO(float4 pos : SV_POSITION, float4 txcoord : TEXCOORD0) : SV_Target
{
    if (!UIGTAO_Enable) return float4(1.0, 0.5, 0.5, 0.5);

    float2 uv = txcoord.xy;
    float depth = GetLinearDepth(TextureDepth.SampleLevel(Point_Sampler, uv, 0).x);

    // Sky early-out
    if (depth > UIGTAO_DepthFade * 1.5) return float4(1.0, 0.5, 0.5, 0.5);

    float3 viewPos = ViewPosFromUV(uv, depth);
    float3 viewNorm = NormalFromDepth(uv);
    viewNorm += float3(0, 0, UIGTAO_NormBias);
    viewNorm = normalize(viewNorm);

    // Screen-space radius clamped to reasonable pixel range
    float radiusSS = UIGTAO_Radius / max(depth, 0.1);
    radiusSS = clamp(radiusSS, 2.0 * max(PixelSize.x, PixelSize.y), 0.15);

    // Temporal rotation
    float baseAngle = 0.0;
    if (UIGTAO_Temporal)
        baseAngle = InterleavedGradientNoise(pos.xy + frac(Timer.x) * 5.588238) * TWO_PI;

    float totalOcclusion = 0.0;

    [unroll]
    for (int s = 0; s < GTAO_SLICES; s++)
    {
        float sliceAngle = (PI / GTAO_SLICES) * (s + 0.5) + baseAngle;
        float2 sliceDir;
        sincos(sliceAngle, sliceDir.y, sliceDir.x);

        // Horizon search in both directions
        float2 maxHorizon = float2(-1.0, -1.0);

        [unroll]
        for (int step = 1; step <= GTAO_STEPS; step++)
        {
            float t = (float)step / GTAO_STEPS;
            float2 sampleOffset = sliceDir * radiusSS * t;

            // Positive direction
            float2 sampleUV_pos = uv + sampleOffset;
            float sampleDepth_pos = GetLinearDepth(TextureDepth.SampleLevel(Point_Sampler, sampleUV_pos, 0).x);
            float3 sampleVP_pos = ViewPosFromUV(sampleUV_pos, sampleDepth_pos);
            float3 horizDir_pos = sampleVP_pos - viewPos;
            float horizAngle_pos = dot(normalize(horizDir_pos), viewNorm);

            // Thickness heuristic
            float dist_pos = length(horizDir_pos);
            float thickFade_pos = saturate(1.0 - (dist_pos / UIGTAO_Radius) * UIGTAO_ThickBias);
            horizAngle_pos *= thickFade_pos;
            maxHorizon.x = max(maxHorizon.x, horizAngle_pos);

            // Negative direction
            float2 sampleUV_neg = uv - sampleOffset;
            float sampleDepth_neg = GetLinearDepth(TextureDepth.SampleLevel(Point_Sampler, sampleUV_neg, 0).x);
            float3 sampleVP_neg = ViewPosFromUV(sampleUV_neg, sampleDepth_neg);
            float3 horizDir_neg = sampleVP_neg - viewPos;
            float horizAngle_neg = dot(normalize(horizDir_neg), viewNorm);

            float dist_neg = length(horizDir_neg);
            float thickFade_neg = saturate(1.0 - (dist_neg / UIGTAO_Radius) * UIGTAO_ThickBias);
            horizAngle_neg *= thickFade_neg;
            maxHorizon.y = max(maxHorizon.y, horizAngle_neg);
        }

        // Analytic visibility integral
        float h1 = acos(clamp(maxHorizon.x, -1.0, 1.0));
        float h2 = acos(clamp(maxHorizon.y, -1.0, 1.0));
        float sliceVis = 0.25 * (-cos(2.0 * h1) + cos(2.0 * h2) + 2.0 * h1 + 2.0 * h2 - 2.0 * PI);
        sliceVis = saturate(sliceVis * INV_PI + 0.5);

        totalOcclusion += sliceVis;
    }

    totalOcclusion /= GTAO_SLICES;
    float ao = pow(abs(totalOcclusion), UIGTAO_Power) * UIGTAO_Intensity;
    ao = saturate(ao);

    // Depth fade
    float depthFade = saturate(1.0 - depth / UIGTAO_DepthFade);
    ao = lerp(1.0, ao, depthFade);

    // Interior: reduce AO radius by 30%
    if (SB_IsActive() && SB_Interior_Flags.x > 0.5)
        ao = lerp(1.0, ao, 0.7);

    return float4(ao, viewNorm.xy * 0.5 + 0.5, 1.0);
}


//=============================================================================//
//  PASS 4: SSGI — Screen-Space Global Illumination (8 samples)                //
//=============================================================================//

static const int SSGI_SAMPLES = 8;
static const float GOLDEN_ANGLE = 2.3999632;  // PI * (3 - sqrt(5))

float4 PS_SSGI(float4 pos : SV_POSITION, float4 txcoord : TEXCOORD0) : SV_Target
{
    if (!UISSGI_Enable) return 0.0;

    float2 uv = txcoord.xy;
    float depth = GetLinearDepth(TextureDepth.SampleLevel(Point_Sampler, uv, 0).x);

    if (depth > UISSGI_DepthFade * 1.5) return 0.0;

    float3 viewPos = ViewPosFromUV(uv, depth);
    float3 viewNorm = NormalFromDepth(uv);

    float radiusSS = UISSGI_Radius / max(depth, 0.1);
    radiusSS = clamp(radiusSS, 3.0 * max(PixelSize.x, PixelSize.y), 0.2);

    // Temporal jitter
    float baseAngle = 0.0;
    if (UISSGI_Temporal)
        baseAngle = InterleavedGradientNoise(pos.xy + frac(Timer.x) * 3.14159) * TWO_PI;

    float3 totalGI = 0.0;
    float  totalWeight = 0.0;

    [unroll]
    for (int i = 0; i < SSGI_SAMPLES; i++)
    {
        float angle = baseAngle + i * GOLDEN_ANGLE;
        float radius = radiusSS * sqrt((float)(i + 1) / SSGI_SAMPLES);

        float2 offset;
        sincos(angle, offset.y, offset.x);
        offset *= radius;

        float2 sampleUV = uv + offset;
        if (any(sampleUV < 0.0) || any(sampleUV > 1.0)) continue;

        float sampleDepth = GetLinearDepth(TextureDepth.SampleLevel(Point_Sampler, sampleUV, 0).x);
        float3 samplePos = ViewPosFromUV(sampleUV, sampleDepth);
        float3 sampleColor = TextureColor.SampleLevel(Linear_Sampler, sampleUV, 0).rgb;

        float3 diff = samplePos - viewPos;
        float dist = length(diff);
        float3 dir = diff / max(dist, DELTA);

        // Form factor
        float NdL = saturate(dot(viewNorm, dir));
        float falloff = saturate(1.0 - dist / UISSGI_Radius);
        falloff *= falloff;

        // Depth rejection
        float depthDiff = abs(sampleDepth - depth);
        float depthWeight = 1.0 - saturate(depthDiff / UISSGI_DepthReject);

        float weight = NdL * falloff * depthWeight;

        // Color bleed with saturation control
        float sampleLuma = dot(sampleColor, K_LUM);
        float3 chromaSample = sampleColor / max(sampleLuma, DELTA);
        float3 bleedColor = lerp(sampleLuma, sampleColor, UISSGI_Saturation);

        totalGI += bleedColor * weight * UISSGI_ColorBleed;
        totalWeight += weight;
    }

    if (totalWeight > DELTA)
        totalGI /= totalWeight;

    // Depth fade
    float depthFade = saturate(1.0 - depth / UISSGI_DepthFade);
    totalGI *= depthFade * UISSGI_Intensity;

    return float4(totalGI, 1.0);
}


//=============================================================================//
//  PASS 5: Contact Shadows (16-step ray march, hard cap)                      //
//=============================================================================//

static const int CS_STEPS = 16;

float4 PS_ContactShadows(float4 pos : SV_POSITION, float4 txcoord : TEXCOORD0) : SV_Target
{
    if (!UICS_Enable) return 1.0;

    float2 uv = txcoord.xy;
    float depth = GetLinearDepth(TextureDepth.SampleLevel(Point_Sampler, uv, 0).x);

    if (depth > UICS_DepthFade * 1.5) return 1.0;

    float3 viewPos = ViewPosFromUV(uv, depth);
    float3 viewNorm = NormalFromDepth(uv);

    // Light direction in view space (approximate)
    float3 lightDir = GetPrimaryLightDirection();

    // N.L fade: don't trace from back-facing surfaces
    float NdL = dot(viewNorm, lightDir);
    float ndlFade = pow(abs(saturate(NdL)), UICS_NdLFade);
    if (ndlFade < 0.01) return 1.0;

    // Project light direction to screen space
    float2 lightScreen = normalize(lightDir.xy);

    // Temporal dithering
    float jitter = 0.0;
    if (UICS_Temporal)
        jitter = InterleavedGradientNoise(pos.xy + frac(Timer.x) * 5.588238);

    // Ray march parameters
    float stepSize = UICS_MaxDist / CS_STEPS / max(depth, 0.1);
    float shadow = 1.0;

    // Thickness window scales with depth
    float thickness = UICS_Thickness * (1.0 + depth * 0.003);
    float selfBias = 0.08 + depth * 0.0003;

    [loop]
    for (int i = 0; i < CS_STEPS; i++)
    {
        float t = (float(i) + jitter) / CS_STEPS;
        float2 sampleUV = uv + lightScreen * stepSize * (i + 1 + jitter);

        if (any(sampleUV < 0.0) || any(sampleUV > 1.0)) break;

        float sampleDepth = GetLinearDepth(TextureDepth.SampleLevel(Point_Sampler, sampleUV, 0).x);
        float expectedDepth = depth + float(i + 1) * stepSize * depth;

        float depthDiff = sampleDepth - expectedDepth;

        // Hit test: sample is in front of expected ray position but within thickness window
        if (depthDiff < -selfBias && depthDiff > -(thickness + selfBias))
        {
            // Distance-based penumbra softness
            float penumbra = lerp(1.0, t, UICS_Softness);
            shadow = min(shadow, 1.0 - penumbra);
        }
    }

    shadow = lerp(1.0, shadow, ndlFade);

    // Depth fade
    float depthFade = saturate(1.0 - depth / UICS_DepthFade);
    shadow = lerp(1.0, shadow, depthFade * UICS_Intensity);

    // Weather softening: rain makes contact shadows 40% softer
    if (UICS_WeatherSoften && SB_IsActive())
    {
        float rainSoften = saturate(SB_Precipitation.y) * 0.4;
        shadow = lerp(shadow, 1.0, rainSoften);
    }

    return shadow;
}


//=============================================================================//
//  PASS 6: Effects Composite (AO + GI + CS + Detail + Clarity)                //
//=============================================================================//

float4 PS_Composite(float4 pos : SV_POSITION, float4 txcoord : TEXCOORD0) : SV_Target
{
    float2 uv = txcoord.xy;
    float3 color = TextureColor.SampleLevel(Point_Sampler, uv, 0).rgb;
    float  depth = GetLinearDepth(TextureDepth.SampleLevel(Point_Sampler, uv, 0).x);
    float4 mask = TextureMask.SampleLevel(Point_Sampler, uv, 0);
    float  skinAmount = mask.a;

    // Read buffers
    float4 aoData  = TextureRGBA64.SampleLevel(Point_Sampler, uv, 0);
    float3 giData  = TextureRGBA64F.SampleLevel(Linear_Sampler, uv, 0).rgb;
    float  csShadow = TextureR16F.SampleLevel(Point_Sampler, uv, 0).r;

    float ao = aoData.x;

    // Multi-bounce AO approximation
    if (UIGTAO_MultiBounce && UIGTAO_Enable)
    {
        float3 albedo = color;
        float luma = dot(albedo, K_LUM);
        // Jimenez multi-bounce: darks bounce less, brights bounce more
        float a = 2.0404 * luma - 0.3324;
        float b = -4.7951 * luma + 0.6417;
        float c = 2.7552 * luma + 0.6903;
        float multiBounce = max(a * ao * ao + b * ao + c, 0.0);
        ao = lerp(ao, multiBounce, 0.5);
    }

    // AO tint
    if (UIGTAO_Enable)
    {
        float3 aoColor = lerp(UIGTAO_Tint, 1.0, ao);
        color *= aoColor;
    }

    // GI color bleed
    if (UISSGI_Enable)
    {
        color += giData;
    }

    // Contact shadows (multiplicative)
    if (UICS_Enable)
        color *= csShadow;

    // Skin micro-detail (high-pass enhancement)
    if (UISMD_Enable && skinAmount > 0.1)
    {
        float3 blurred = 0.0;
        float blurW = 0.0;
        float2 blurOff = PixelSize * UISMD_BlurRadius;
        [unroll]
        for (int bx = -1; bx <= 1; bx++)
        {
            [unroll]
            for (int by = -1; by <= 1; by++)
            {
                float w = 1.0 / (1.0 + abs(bx) + abs(by));
                blurred += TextureColor.SampleLevel(Linear_Sampler, uv + float2(bx, by) * blurOff, 0).rgb * w;
                blurW += w;
            }
        }
        blurred /= blurW;

        float3 detail = color - blurred;
        float detailMask = pow(abs(skinAmount), UISMD_SkinMaskPow);

        if (UISMD_LumaOnly)
        {
            float lumaDetail = dot(detail, K_LUM);
            color += lumaDetail * UISMD_Intensity * UISMD_HighPassGain * detailMask;
        }
        else
        {
            color += detail * UISMD_Intensity * UISMD_HighPassGain * detailMask;
        }
    }

    // Clarity (local contrast enhancement)
    if (UICLR_Enable)
    {
        float3 blurredClarity = 0.0;
        float blurCW = 0.0;
        float2 clarityOff = PixelSize * UICLR_Radius;
        [unroll]
        for (int cx = -2; cx <= 2; cx++)
        {
            [unroll]
            for (int cy = -2; cy <= 2; cy++)
            {
                float w = exp(-0.5 * (cx * cx + cy * cy) / 2.0);
                blurredClarity += TextureColor.SampleLevel(Linear_Sampler, uv + float2(cx, cy) * clarityOff * 0.5, 0).rgb * w;
                blurCW += w;
            }
        }
        blurredClarity /= blurCW;

        float luma = dot(color, K_LUM);
        float blurLuma = dot(blurredClarity, K_LUM);
        float localContrast = luma - blurLuma;

        // Depth-aware application
        float clarityDepthFade = 1.0;
        if (UICLR_DepthAware > 0.01)
            clarityDepthFade = saturate(1.0 - depth / (200.0 / UICLR_DepthAware));

        // Preserve skin option
        float clarityMask = 1.0;
        if (UICLR_PreserveSkin && skinAmount > 0.3)
            clarityMask = 1.0 - skinAmount * 0.7;

        // Midpoint curve: attenuate near midpoint, boost at extremes
        float midWeight = 1.0 - exp(-abs(localContrast - UICLR_MidPoint) * 4.0);

        color += localContrast * UICLR_Amount * clarityDepthFade * clarityMask * midWeight;
    }

    // Chromatic adaptation
    if (UICA_Enable)
    {
        float3 avgColor = 0.0;
        if (SB_IsActive())
            avgColor = SB_Atmos_Ambient.rgb;
        else
            avgColor = UICA_TargetWhite;

        float3 avgLum = dot(avgColor, K_LUM);
        float3 adaptation = avgLum / max(avgColor, DELTA);
        adaptation = lerp(1.0, adaptation, UICA_Strength);
        color *= adaptation;
    }

    // Micro-shadow enhancement
    if (UIMS_Enable)
    {
        float aoHighPass = ao;
        float aoBlurred = 0.0;
        float aoW = 0.0;
        float2 msOff = PixelSize * UIMS_Radius;
        [unroll]
        for (int mx = -1; mx <= 1; mx++)
        {
            [unroll]
            for (int my = -1; my <= 1; my++)
            {
                float w = 1.0 / (1.0 + abs(mx) + abs(my));
                aoBlurred += TextureRGBA64.SampleLevel(Linear_Sampler, uv + float2(mx, my) * msOff, 0).x * w;
                aoW += w;
            }
        }
        aoBlurred /= aoW;

        float microShadow = saturate(ao - aoBlurred) * UIMS_Intensity;
        float msFade = saturate(1.0 - depth / UIMS_DepthFade);
        color *= 1.0 - microShadow * msFade;
    }

    return float4(max(color, 0.0), 1.0);
}


//=============================================================================//
//  PASS 7: God Rays + Atmospheric Fog                                         //
//=============================================================================//

float4 PS_GodRaysAndFog(float4 pos : SV_POSITION, float4 txcoord : TEXCOORD0) : SV_Target
{
    float2 uv = txcoord.xy;
    float3 color = TextureColor.SampleLevel(Point_Sampler, uv, 0).rgb;
    float  rawDepth = TextureDepth.SampleLevel(Point_Sampler, uv, 0).x;
    float  depth = GetLinearDepth(rawDepth);

    // === GOD RAYS ===
    if (UIGR_Enable)
    {
        // Sun screen position
        float2 sunUV;
        if (SB_IsActive())
            sunUV = SB_SunScreenUV();
        else
            sunUV = SunDirection.xy * 0.5 + 0.5;

        float2 deltaTexCoord = (uv - sunUV) * UIGR_Density / 64.0;
        float2 marchUV = uv;
        float illumination = 0.0;
        float sampleDecay = 1.0;

        [loop]
        for (int r = 0; r < 64; r++)
        {
            marchUV -= deltaTexCoord;
            float2 clampedUV = saturate(marchUV);

            float sampleD = TextureDepth.SampleLevel(Point_Sampler, clampedUV, 0).x;
            float skyMask = step(UIGR_Threshold, sampleD);

            // Bright geometry also contributes
            float3 sampleCol = TextureColor.SampleLevel(Linear_Sampler, clampedUV, 0).rgb;
            float brightMask = saturate(dot(sampleCol, K_LUM) - 1.5);

            float contribution = max(skyMask, brightMask * 0.3);
            illumination += contribution * sampleDecay;
            sampleDecay *= UIGR_Decay;
        }

        illumination /= 64.0;
        illumination *= UIGR_Exposure;

        // Combat boost
        if (UIGR_CombatReactive && SB_IsActive())
        {
            float combatInt = SB_Player_Combat.x;
            illumination *= 1.0 + combatInt * UIGR_CombatBoost;
        }

        // Lightning flash integration
        if (SB_IsActive())
        {
            float flash = SB_Lightning.z;
            illumination += flash * 0.15;
        }

        float3 rayColor = illumination * UIGR_Tint * UIGR_Intensity;

        // SB sunlight tint
        if (SB_IsActive())
            rayColor *= lerp(1.0, GetSunlightColor(), 0.5);

        color += rayColor;
    }

    // === ATMOSPHERIC FOG ===
    if (UIFOG_Enable && rawDepth < UIFOG_SkyThreshold)
    {
        // Fog color estimation
        float3 fogColor = UIFOG_ColorFallback;
        if (UIFOG_SkyColorEnable)
        {
            float2 skySampleCenter = float2(0.5, UIFOG_SkySampleY);
            float3 skyCol = EstimateSkyColor(skySampleCenter, UIFOG_SkySpread, UIFOG_SkyDesat, UIFOG_SkyThreshold);
            if (dot(skyCol, 1.0) > 0.01)
                fogColor = skyCol;
        }

        // SB fog color anchor
        if (SB_IsActive())
        {
            float3 sbFogColor = SB_Fog_FarColor.rgb;
            if (dot(sbFogColor, 1.0) > 0.01)
                fogColor = lerp(fogColor, sbFogColor, 0.6);
        }

        fogColor *= UIFOG_Tint;
        fogColor = lerp(fogColor, UIFOG_Tint, UIFOG_TintWeight);
        fogColor *= UIFOG_Brightness;

        // Exponential distance fog
        float fogAmount = 1.0 - exp(-depth * UIFOG_Density);
        fogAmount = min(fogAmount, UIFOG_MaxOpacity);
        fogAmount *= saturate(depth / UIFOG_MaxDist);

        // Height fog
        float worldY = 0.0;
        if (SB_IsActive())
            worldY = SB_Player_Position.w + (uv.y - 0.5) * depth * 0.1;
        float heightFog = exp(-max(worldY - UIFOG_BaseHeight, 0.0) * UIFOG_HeightFalloff);
        fogAmount *= heightFog;

        // Inscatter (sun-facing glow)
        if (UIFOG_Inscatter > 0.0)
        {
            float2 sunUV;
            if (SB_IsActive())
                sunUV = SB_SunScreenUV();
            else
                sunUV = SunDirection.xy * 0.5 + 0.5;

            float sunDot = saturate(1.0 - length(uv - sunUV) * 2.0);
            sunDot = pow(abs(sunDot), 3.0);
            fogColor += UIFOG_InscatterTint * sunDot * UIFOG_Inscatter;
        }

        // Aerial perspective desaturation
        if (UIFOG_AerialDesat > 0.0)
        {
            float luma = dot(color, K_LUM);
            color = lerp(color, luma, fogAmount * UIFOG_AerialDesat);
        }

        // Weather smoothing
        if (UIFOG_WeatherSmooth && SB_IsActive())
        {
            float transition = SB_SmoothWeatherTransition();
            fogAmount *= lerp(0.8, 1.0, transition);
        }

        color = lerp(color, fogColor, fogAmount);
    }

    return float4(max(color, 0.0), 1.0);
}


//=============================================================================//
//  PASS 8: Watercolor / Painterly Filter (Anisotropic Kuwahara)               //
//=============================================================================//

float4 PS_Watercolor(float4 pos : SV_POSITION, float4 txcoord : TEXCOORD0) : SV_Target
{
    float2 uv = txcoord.xy;
    float3 color = TextureColor.SampleLevel(Point_Sampler, uv, 0).rgb;

    if (!UIPAINT_Enable) return float4(color, 1.0);

    float depth = GetLinearDepth(TextureDepth.SampleLevel(Point_Sampler, uv, 0).x);
    float4 mask = TextureMask.SampleLevel(Point_Sampler, uv, 0);
    float skinAmount = mask.a;

    // Depth fade
    float paintFade = saturate(1.0 - depth / UIPAINT_DepthFade);
    if (paintFade < 0.01) return float4(color, 1.0);

    // Skin preservation
    float paintMask = 1.0;
    if (UIPAINT_PreserveSkin && skinAmount > 0.3)
        paintMask = 1.0 - skinAmount * UIPAINT_SkinReduce;

    float effectAmount = UIPAINT_Intensity * paintFade * paintMask;
    if (effectAmount < 0.01) return float4(color, 1.0);

    // Structure tensor for anisotropic direction
    float gx = dot(TextureColor.SampleLevel(Linear_Sampler, uv + float2(PixelSize.x, 0), 0).rgb -
                    TextureColor.SampleLevel(Linear_Sampler, uv - float2(PixelSize.x, 0), 0).rgb, K_LUM);
    float gy = dot(TextureColor.SampleLevel(Linear_Sampler, uv + float2(0, PixelSize.y), 0).rgb -
                    TextureColor.SampleLevel(Linear_Sampler, uv - float2(0, PixelSize.y), 0).rgb, K_LUM);

    float angle = atan2(gy, gx + DELTA);
    float edgeStrength = sqrt(gx * gx + gy * gy);

    // Kuwahara sectors
    int sectors = UIPAINT_Sectors;
    float sectorAngle = TWO_PI / sectors;
    float radius = UIPAINT_Radius * PixelSize.x;

    float3 bestMean = 0.0;
    float  bestVar = 1e10;

    [loop]
    for (int sec = 0; sec < sectors; sec++)
    {
        float sAngle = angle + sec * sectorAngle;

        // Elliptical kernel (anisotropic)
        float2 majorDir;
        sincos(sAngle, majorDir.y, majorDir.x);

        float3 sectorSum = 0.0;
        float3 sectorSum2 = 0.0;
        float  sectorWeight = 0.0;

        [unroll]
        for (int k = 0; k < 5; k++)
        {
            float t = (k + 0.5) / 5.0;
            float r = radius * t;

            float2 offset = majorDir * r;
            // Anisotropy: squeeze perpendicular
            float2 perpDir = float2(-majorDir.y, majorDir.x);
            offset += perpDir * r * (1.0 - UIPAINT_Anisotropy) * (k % 2 == 0 ? 0.5 : -0.5);

            float3 s = TextureColor.SampleLevel(Linear_Sampler, uv + offset, 0).rgb;
            float w = exp(-t * t * UIPAINT_Hardness);

            sectorSum += s * w;
            sectorSum2 += s * s * w;
            sectorWeight += w;
        }

        sectorSum /= sectorWeight;
        sectorSum2 /= sectorWeight;
        float3 variance = sectorSum2 - sectorSum * sectorSum;
        float totalVar = dot(variance, 1.0);

        if (totalVar < bestVar)
        {
            bestVar = totalVar;
            bestMean = sectorSum;
        }
    }

    // Posterization
    float levels = UIPAINT_Posterize;
    float3 posterized = floor(bestMean * levels + 0.5) / levels;
    bestMean = lerp(bestMean, posterized, 0.3);

    // Edge ink overlay
    float ink = 0.0;
    if (UIPAINT_EdgeInk > 0.01)
    {
        float edgeW = UIPAINT_EdgeWidth * PixelSize.x;
        float3 laplacian = -4.0 * color;
        laplacian += TextureColor.SampleLevel(Linear_Sampler, uv + float2(edgeW, 0), 0).rgb;
        laplacian += TextureColor.SampleLevel(Linear_Sampler, uv - float2(edgeW, 0), 0).rgb;
        laplacian += TextureColor.SampleLevel(Linear_Sampler, uv + float2(0, edgeW), 0).rgb;
        laplacian += TextureColor.SampleLevel(Linear_Sampler, uv - float2(0, edgeW), 0).rgb;
        ink = saturate(dot(abs(laplacian), K_LUM) * UIPAINT_EdgeInk * 3.0);
    }

    // Paper grain
    float grain = 0.0;
    if (UIPAINT_PaperGrain > 0.01)
    {
        grain = Random(uv * 500.0 + Timer.x) * UIPAINT_PaperGrain;
    }

    // Wet edge darkening
    float wetEdge = edgeStrength * UIPAINT_WetEdge;
    bestMean *= 1.0 - wetEdge * 0.5;

    // Pigment bleed
    if (UIPAINT_Bleed > 0.01)
    {
        float3 bleedSample = TextureColor.SampleLevel(Linear_Sampler, uv + float2(gx, gy) * PixelSize * UIPAINT_Bleed * 10.0, 0).rgb;
        bestMean = lerp(bestMean, bleedSample, UIPAINT_Bleed * 0.3);
    }

    float3 result = lerp(color, bestMean, effectAmount);
    result = result * (1.0 - ink) + grain;

    return float4(max(result, 0.0), 1.0);
}


//=============================================================================//
//                        UTILITY PIXEL SHADERS                                 //
//=============================================================================//

// Pass-through: returns TextureColor unchanged (for unused technique slots)
float4 PS_Passthrough(float4 pos : SV_POSITION, float4 txcoord : TEXCOORD0) : SV_Target
{
    return TextureColor.SampleLevel(Point_Sampler, txcoord.xy, 0);
}


//=============================================================================//
//                         TECHNIQUE DEFINITIONS                                //
//=============================================================================//

// Tech 0: SSS Horizontal
technique11 KitsuunePrePass
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_SSSBlurH())); }
}

// Tech 1: SSS Vertical
technique11 KitsuunePrePass1
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_SSSBlurV())); }
}

// Tech 2: SSS Final Composite
technique11 KitsuunePrePass2
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_SSSComposite())); }
}

// Tech 3: GTAO (clear + compute) → RenderTargetRGBA64
technique11 KitsuunePrePass3 <string RenderTarget="RenderTargetRGBA64";>
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Blank())); }
    pass p1 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_GTAO())); }
}

// Tech 4: SSGI (clear + compute) → RenderTargetRGBA64F
technique11 KitsuunePrePass4 <string RenderTarget="RenderTargetRGBA64F";>
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Blank())); }
    pass p1 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_SSGI())); }
}

// Tech 5: Contact Shadows (clear + compute) → RenderTargetR16F
technique11 KitsuunePrePass5 <string RenderTarget="RenderTargetR16F";>
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Blank())); }
    pass p1 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_ContactShadows())); }
}

// Tech 6: Effects Composite
technique11 KitsuunePrePass6
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Composite())); }
}

// Tech 7: God Rays + Fog
technique11 KitsuunePrePass7
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_GodRaysAndFog())); }
}

// Tech 8: Watercolor Filter
technique11 KitsuunePrePass8
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Watercolor())); }
}

// Tech 9-11: Pass-through (unused technique slots — must return TextureColor, not black)
technique11 KitsuunePrePass9
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
}

technique11 KitsuunePrePass10
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
}

technique11 KitsuunePrePass11
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
}
