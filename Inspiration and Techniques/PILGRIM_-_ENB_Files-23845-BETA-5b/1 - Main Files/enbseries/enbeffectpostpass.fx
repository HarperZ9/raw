/////////////////////////////////////////////////////////
//                                                     //
//                      PILGRIM                        //
//     An ENB and weather mod by L00ping & TreyM       //
//                                                     //
/////////////////////////////////////////////////////////
//                                                     //
//                ENBSeries effect file                //
//         visit http://enbdev.com for updates         //
//       Copyright (c) 2007-2015 Boris Vorontsov       //
//                                                     //
//-----------------------CREDITS-----------------------//
//                                                     //
// Boris Vorontsov: For ENBSeries                      //
//                                                     //
// JawZ:  Author and developer of the MSL code         //
//                                                     //
// CeeJay.dk-Crosire-GemFx-Marty McFly-Lucifer Hawk:   //
//        Original Authors of sweeFX & Reshade code    //
//                                                     //
// martinsh:                                           //
//        Original author of film grain shader         //
//                                                     //
// roxahris:                                           //
//        Port of film martinsh's film grain to ENB    //
//                                                     //
// L00 and TreyM :  Shader Setup, and Settings,        //
//        Port and Modification of ReShade Shaders     //
//        and author of this file                      //
//                                                     //
/////////////////////////////////////////////////////////

#include "pilgrimquality.fxh"

/////////////////////////////////////////////////////////
//               INTERNAL PARAMETERS & GUI             //
/////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////
//                       EFFECTS                       //
/////////////////////////////////////////////////////////

float Empty_Row90
<
  string UIName="                      DREAD THE COMMONWEALTH";
  string UIWidget="spinner";
  float UIMin=0.0;
  float UIMax=0.0;
> = {0.0};

#if(PilgrimQuality != 2)
// Show performance mode tag when in performance mode
float Empty_Row16
<
  string UIName="                                                 VERSION - B5";
  string UIWidget="spinner";
  float UIMin=0.0;
  float UIMax=0.0;
> = {0.0};
#else
float Empty_Row16
<
  string UIName="PERFORMANCE MODE             VERSION - B5";
  string UIWidget="spinner";
  float UIMin=0.0;
  float UIMax=0.0;
> = {0.0};
#endif

float Empty_Row95
<
  string UIName="xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
  string UIWidget="spinner";
  float UIMin=0.0;
  float UIMax=0.0;
> = {0.0};

////////////////////// SHARPENING //////////////////////

int USER_SHARPEN
<
        string UIName="Sharpening";
		string UIWidget="Spinner";
		float UIMin=0;
		float UIMax=3;
		float UIStep=1;
> = {0};

#define sharp_strength2 (USER_SHARPEN * 0.73)
#define LUMA_SHARPEN true
#define sharp_strength 0.56
#define sharp_clamp 0.035
#define offset_bias 1.0
#define CoefLuma float3(0.2126, 0.7152, 0.0722)

// Disable these effects when in performance mode
#if(PilgrimQuality != 2)

// Lens aberration:
bool ENABLE_LENS <
    string UIName = "Lens Emulation";
> = {true};

#define LCAStrength 0.125
#define USE_DISTORTION         1
#define fFisheyeZoom 0.501
#define fFisheyeDistortion (LCAStrength * 0.1)
#define fFisheyeColorshift 0.0

// Vignette:
#define ENABLE_VIGNETTE true
#define VignetteAmount -1.35
#define VignetteRatio border_ratio
#define VignetteType       1
#define VignetteRadius  2.50
#define VignetteSlope      2
#define VignetteCenter float2(0.500, 0.500)
#endif

// Disable this effect when in performance mode
#if(PilgrimQuality != 2)

bool ENABLE_GRAIN <
    string UIName = "Film Grain";
> = {true};

#define grainamount 0.020
#define Use_GrainColour 1

// 35mm Film Grain
#define coloramount35mm 0.37
#define grainsize35mm 1.80
#define lumamount35mm 0.43
#endif

bool ENABLE_CALIBRATION <
    string UIName = "Lift Shadows";
> = {true};

#define cal_black -7
#define cal_white 255

///////////////////// IMAGE FORMAT /////////////////////

//Letterbox:

bool USE_BORDER <
    string UIName = "Letterbox";
> = {true};
#define border_width float2(0,0)
#define border_color float3(0, 0, 0)

float border_ratio
<
        string UIName="Letterbox Ratio";
		string UIWidget="Spinner";
		float UIMin=0.10;
		float UIMax=10.00;
		float UIStep=0.01;
> = {2.35};


/////////////////////////////\\\\\\\\\\\\\\\\\\\\\\\\\\\\
//           EXTERNAL PARAMETERS BEGINS HERE,          //
//                 SHOULD NOT BE MODIFIED              //
//          UNLESS YOU KNOW WHAT YOU ARE DOING         //
/////////////////////////////////////////////////////////

float4 Timer;       /// x = generic timer in range 0..1, period of 16777216 ms (4.6 hours), y = average fps, w = frame time elapsed (in seconds)
float4 ScreenSize;  /// x = Width, y = 1/Width, z = aspect, w = 1/aspect, aspect is Width/Height

//changes in range 0..1, 0 means that night time, 1 - day time
float	ENightDayFactor;
//changes 0 or 1. 0 means that exterior, 1 - interior
float	EInteriorFactor;

// External ENB debugging paramaters
float4 tempF1;     /// 0,1,2,3  // Keyboard controlled temporary variables.
float4 tempF2;     /// 5,6,7,8  // Press and hold key 1,2,3...8 together with PageUp or PageDown to modify.
float4 tempF3;     /// 9,0
float4 tempInfo1;  /// xy = cursor position in range 0..1 of screen, z = is shader editor window active, w = mouse buttons with values 0..7
/// tempInfo1 assigned mouse button values
///    0 = none
///    1 = left
///    2 = right
///    3 = left+right
///    4 = middle
///    5 = left+middle
///    6 = right+middle
///    7 = left+right+middle (or rather cat is sitting on your mouse)


////////////////////// TEXTURES \\\\\\\\\\\\\\\\\\\\\\\\\

Texture2D TextureOriginal;  /// LDR color
Texture2D TextureColor;     /// LDR color which is output of previous technique
Texture2D TextureDepth;     /// scene depth

/// Temporary textures which can be set as render target for techniques via annotations like <string RenderTarget="RenderTargetRGBA32";>
Texture2D RenderTargetRGBA32;   // R8G8B8A8 32 bit ldr format
Texture2D RenderTargetRGBA64;   // R16B16G16A16 64 bit ldr format
Texture2D RenderTargetRGBA64F;  // R16B16G16A16F 64 bit hdr format
Texture2D RenderTargetR16F;     // R16F 16 bit hdr format with red channel only
Texture2D RenderTargetR32F;     // R32F 32 bit hdr format with red channel only
Texture2D RenderTargetRGB32F;   // 32 bit hdr format without alpha


////////////////////// SAMPLERS \\\\\\\\\\\\\\\\\\\\\\\\\

SamplerState Sampler0
{
  Filter=MIN_MAG_MIP_POINT;  AddressU=Clamp;  AddressV=Clamp;  // MIN_MAG_MIP_LINEAR;
};
SamplerState Sampler1
{
  Filter=MIN_MAG_MIP_LINEAR;  AddressU=Clamp;  AddressV=Clamp;
};
SamplerState SamplerLut
{
   Filter=MIN_MAG_MIP_LINEAR;  AddressU=Clamp;  AddressV=Clamp;
};

/////////////////// DATA STRUCTURE \\\\\\\\\\\\\\\\\\\\\\

struct VS_INPUT_POST
{
  float3 pos     : POSITION;
  float2 txcoord : TEXCOORD0;
};
struct VS_OUTPUT_POST
{
  float4 pos      : SV_POSITION;
  float2 txcoord0 : TEXCOORD0;
};

#include "Shaders/MSLHelper.fxh"

/////////////////////////////\\\\\\\\\\\\\\\\\\\\\\\\\\\\
//                      SHADER CODES                   //
/////////////////////////////////////////////////////////

#include "Shaders/enbeffectpostpass/LumaSharpen.fxh"

// Disable these effects when in performance mode
#if(PilgrimQuality != 2)
#include "Shaders/enbeffectpostpass/LensCA.fxh"
#include "Shaders/enbeffectpostpass/Vignette.fxh"
#include "Shaders/enbeffectpostpass/FilmGrain.fxh"
#endif

#include "Shaders/enbeffectpostpass/3DLUT.fxh"
#include "Shaders/enbeffectpostpass/Levels.fxh"

// Disable this effect when in performance mode
#if(PilgrimQuality != 2)
#include "Shaders/enbeffectpostpass/Dither.fxh"
#endif

#include "Shaders/enbeffectpostpass/Aspect.fxh"

//21 tap reduced to 6 weights ( 41 pixels wide )
static const float sampleOffsets_3[6]   = { 0.0,         1.452313744966,      3.390210239952,      5.331472958797,      7.277552900121,      9.229394260785   };
static const float sampleWeights_3[6]   = { 0.142479385858,   0.244115579374,      0.131636577371,      0.043283482080,      0.008668409765,      0.001056258481   };

float3 screen(float3 a, float3 b)
{
   return (1.0f - (1.0f - a) * (1.0f - b));
}


// VERTEX SHADER
VS_OUTPUT_POST VS_PostProcess(VS_INPUT_POST IN)
{
  VS_OUTPUT_POST OUT;

    float4 pos;
    pos.xyz=IN.pos.xyz;
    pos.w=1.0;
    OUT.pos=pos;
    OUT.txcoord0.xy=IN.txcoord.xy;

  return OUT;
}

/////////////////////////////\\\\\\\\\\\\\\\\\\\\\\\\\\\\
//                      PIXEL SHADER                   //
/////////////////////////////////////////////////////////

// INITIAL LENS STAGE ///////////////////////////////////
// Physically based lens chromatic aberration          //
/////////////////////////////////////////////////////////

// Disable this effect when in performance mode

float4 PS_CA(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
  float4 res = TextureColor.Sample(Sampler0, IN.txcoord0.xy);
  // Allow lens distortion bypass
  #if(PilgrimQuality != 2)
  if(ENABLE_LENS == false) return res;
  #else
  return res;
  #endif


  #if(PilgrimQuality != 2)
  float2 FishEyeRcoord, FishEyeGcoord, FishEyeBcoord;
  FishEyePass(IN.txcoord0.xy, FishEyeRcoord, FishEyeGcoord, FishEyeBcoord);
  return float4( LensCA(FishEyeRcoord).r, LensCA(FishEyeGcoord).g, LensCA(FishEyeBcoord).b, 1.0); //24 taps
  #endif
}



// SECONDARY LENS STAGE /////////////////////////////////
// First do slight sharpening to offset lens blur,     //
// then render lens vignette.                          //
/////////////////////////////////////////////////////////

float4	PS_Vig(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
    float4 res = float4(LumaSharpenPass(IN.txcoord0.xy), 1.0);

    // Disable this effect when in performance mode
    #if(PilgrimQuality != 2)
    if(ENABLE_LENS)      res     = VignettePass(res,IN.txcoord0.xy);
    #endif

	return res;
}


// FINAL STAGE 35MM GRAIN ///////////////////////////////
// Do second stage sharpening, film grain, color grade //
// LUT, dithering, shadow lifting pass, and letterbox. //
/////////////////////////////////////////////////////////

float4	PS_Shared35mm(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
    float4 res = float4(LumaSharpenPass2(IN.txcoord0.xy), 1.0);

    // Disable this effect when in performance mode
    #if(PilgrimQuality != 2)
    if(ENABLE_GRAIN)     res.xyz = GrainPass35mm(IN.txcoord0.xy, res.xyz);
    #endif

    res.rgb = LUTfunc(saturate(res.rgb));

    // Disable these effects when in performance mode
    #if(PilgrimQuality != 2)
    res.rgb = msDither(res.rgb,  IN.txcoord0.xy);
    #endif

    if(ENABLE_CALIBRATION) res   = CalibrationPass(res);
    if(USE_BORDER)       res     = BorderPass(res, IN.txcoord0.xy);

	return res;
}

/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////

/////////////////////////////\\\\\\\\\\\\\\\\\\\\\\\\\\\\
//                      TECHNIQUES                     //
/////////////////////////////////////////////////////////

///// INITIAL LENS STAGE
technique11 VVITCH <string UIName="PILGRIM";>
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_CA()));
  }
}

///// SECONDARY LENS STAGE
technique11 VVITCH1
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Vig()));
  }
}

///// FINAL STAGE 35MM GRAIN
technique11 VVITCH2
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Shared35mm()));
  }
}

/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
