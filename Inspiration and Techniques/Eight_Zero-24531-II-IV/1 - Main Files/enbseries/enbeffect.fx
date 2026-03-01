/////////////////////////////////////////////////////////
//                                                     //
//          888888888               000000000          //
//        88:::::::::88           00:::::::::00        //
//      88:::::::::::::88       00:::::::::::::00      //
//     8::::::88888::::::8     0:::::::000:::::::0     //
//     8:::::8     8:::::8     0::::::0   0::::::0     //
//     8:::::8     8:::::8     0:::::0     0:::::0     //
//      8:::::88888:::::8      0:::::0     0:::::0     //
//       8:::::::::::::8       0:::::0 000 0:::::0     //
//      8:::::88888:::::8      0:::::0 000 0:::::0     //
//     8:::::8     8:::::8     0:::::0     0:::::0     //
//     8:::::8     8:::::8     0:::::0     0:::::0     //
//     8:::::8     8:::::8     0::::::0   0::::::0     //
//     8::::::88888::::::8     0:::::::000:::::::0     //
//      88:::::::::::::88       00:::::::::::::00      //
//        88:::::::::88           00:::::::::00        //
//          888888888               000000000          //
//                                                     //
//                     EIGHT ZERO                      //
//                  An ENB by TreyM                    //
//                                                     //
/////////////////////////////////////////////////////////
//                                                     //
//                ENBSeries effect file                //
//         visit http://enbdev.com for updates         //
//       Copyright (c) 2007-2017 Boris Vorontsov       //
//                                                     //
//-----------------------CREDITS-----------------------//
//                                                     //
// Boris Vorontsov: For ENBSeries                      //
//                                                     //
// JawZ:  Author and developer of the MSL code         //
//                                                     //
// TreyM:  Shader Setup, Presets and Settings          //
//                                                     //
/////////////////////////////////////////////////////////

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// EXTERNAL PARAMETERS BEGINS HERE, SHOULD NOT BE MODIFIED UNLESS YOU KNOW WHAT YOU ARE DOING
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

//x = generic timer in range 0..1, period of 16777216 ms (4.6 hours), y = average fps, w = frame time elapsed (in seconds)
float4	Timer;
//x = Width, y = 1/Width, z = aspect, w = 1/aspect, aspect is Width/Height
float4	ScreenSize;
//changes in range 0..1, 0 means full quality, 1 lowest dynamic quality (0.33, 0.66 are limits for quality levels)
float	AdaptiveQuality;
//x = current weather index, y = outgoing weather index, z = weather transition, w = time of the day in 24 standart hours. Weather index is value from weather ini file, for example WEATHER002 means index==2, but index==0 means that weather not captured.
float4	Weather;
//x = dawn, y = sunrise, z = day, w = sunset. Interpolators range from 0..1
float4	TimeOfDay1;
//x = dusk, y = night. Interpolators range from 0..1
float4	TimeOfDay2;
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

float4 Params01[6];  /// MOD PARAMATER, DO NOT MODIFY!
float4 ENBParams01;  /// x - bloom amount; y - lens amount

// TEXTURES
Texture2D TextureColor;       /// HDR color
Texture2D TextureBloom;       /// Fallout4 or ENB bloom
Texture2D TextureLens;        /// ENB lens fx
Texture2D TextureAdaptation;  /// Fallout4 or ENB adaptation
Texture2D TextureDepth;       /// Scene depth
Texture2D TextureAperture;    /// This frame aperture 1*1 R32F hdr red channel only. computed in depth of field shader file

// SAMPLERS
SamplerState Sampler0
{
  Filter=MIN_MAG_MIP_POINT;  AddressU=Clamp;  AddressV=Clamp;  /// MIN_MAG_MIP_LINEAR;
};
SamplerState Sampler1
{
  Filter=MIN_MAG_MIP_LINEAR;  AddressU=Clamp;  AddressV=Clamp;
};

SamplerState 		SamplerDirt
{
   Filter = MIN_MAG_MIP_LINEAR;
   AddressU=Clamp;
   AddressV=Clamp;
};

// DATA STRUCTURE
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

   #include "Include/Functions/enbeffect/msHelpers.fxh"
   #include "Include/Functions/enbeffect/enbAGCC.fxh"
   #include "Include/Functions/enbeffect/enbPP2.fxh"
   #include "Include/Functions/enbeffect/HDRFilmic2D.fxh"

// VERTEX SHADER
VS_OUTPUT_POST VS_Draw(VS_INPUT_POST IN)
{
  VS_OUTPUT_POST OUT;

    float4 pos;
    pos.xyz=IN.pos.xyz;
    pos.w=1.0;
    OUT.pos=pos;
    OUT.txcoord0.xy=IN.txcoord.xy;

  return OUT;
}


// PIXEL SHADER
float4 PS_Draw(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
  float4 res;     /// Output
  float4 color;   /// HDR scene color
  float4 ebloom;  /// ENB and FO4 bloom
  float2 bloom_offset = Params01[4].zw;  //bloom coord scaling
  float3 bloom        = TextureBloom.Sample(Sampler1, bloom_offset * IN.txcoord0.xy).rgb;
  float3 lens;    /// ENB lens FX
  float4 eadapt;  /// ENB and FO4 adaptation
  float3 adaptation   = Params01[1].xyz;  //.x = adaption max, .y = min, .z = scale/sensitivity
  float  middlegray   = TextureAdaptation.Sample(Sampler0, IN.txcoord0.xy).x;
  color    = TextureColor.Sample(Sampler0, IN.txcoord0.xy);
  ebloom   = TextureBloom.Sample(Sampler1, IN.txcoord0.xy);
  lens.xyz = TextureLens.Sample(Sampler1, IN.txcoord0.xy).xyz;
  eadapt   = TextureAdaptation.Sample(Sampler0, IN.txcoord0.xy);
  float4 eblur;       /// For Local Operators
  eblur      = TextureBloom.Sample(Sampler1, IN.txcoord0.xy);
  eadapt.xyz = AvgLuma(eadapt.xyz).y;
  color.xyz += lens.xyz * ENBParams01.y;
  color = enbAGCC(color, IN.txcoord0.xy);  /// Requires enbAGCC.fxh active
  color.rgb = enbPP(color.rgb, eadapt.xyz);
  color.rgb = msHDRTonemap(color.rgb, eblur.xyz, eadapt.xyz);
  res.xyz=saturate(color);
  res.w=1.0;
  return res;
}

// VANILLA POST PROCESS, DO NOT MODIFY!
float4 PS_DrawOriginal(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
    float4 res;
    float4 color;
    color=TextureColor.Sample(Sampler0, IN.txcoord0.xy); /// HDR scene color

// Combines bloom, adaptation
    float4 r0, r1, r2, r3;
    r0.xyz = color.xyz;
    r1.xy  = Params01[4].zw * IN.txcoord0.xy;
    r1.xyz = TextureBloom.Sample(Sampler1, r1.xy).xyz;
    r0.w   = TextureAdaptation.Sample(Sampler0, IN.txcoord0.xy).x;
    r1.w   = Params01[1].z / (0.001 + r0.w);
    r2.x   = r1.w < Params01[1].y;
    r1.w   = r2.x ? Params01[1].y : r1.w;
    r2.x   = Params01[1].x < r1.w;
    r1.w   = r2.x ? Params01[1].x : r1.w;
    r0.xyz = r1.xyz + r0.xyz;
    r0.xyz = r0.xyz * r1.w;
// returns color_adapt

// filmic tonemap
    r1.xyz = r0.xyz + r0.xyz;
    r2.xyz = r0.xyz * 0.3 + 0.05;
    r3.xy  = float2(0.2, 3.333333) * Params01[1].w;
    r2.xyz = r1.xyz * r2.xyz + r3.x;
    r0.xyz = r0.xyz * 0.3 + 0.5;
    r0.xyz = r1.xyz * r0.xyz + 0.06;
    r0.xyz = r2.xyz / r0.xyz;
    r0.xyz = -Params01[1].w * 3.333333 + r0.xyz;
    r1.x   = Params01[1].w * 0.2 + 19.376;
    r1.x   = r1.x * 0.0408564 - r3.y;
    r1.xyz = r0.xyz / r1.x;
// returns filmic result

// post process
    r0.x    = dot(r1.xyz, float3(0.2125, 0.7154, 0.0721));
    r1.xyz  = r1.xyz - r0.x;
    r1.xyz  = Params01[2].x * r1.xyz + r0.x;
    r2.xyz  = r0.x * Params01[3].xyz - r1.xyz;
    r1.xyz  = Params01[3].w * r2.xyz + r1.xyz;
    r1.xyz  = Params01[2].w * r1.xyz - r0.w;
    r0.xyz  = Params01[2].z * r1.xyz + r0.w;
    res.xyz = lerp(r0.xyz, Params01[5].xyz, Params01[5].w);  /// Last color filter used only for certain conditions, like rifle night scope

    res.xyz = pow(res.xyz, 1.0/2.2);  /// Gamma correction for LDR output, instead of Linear output

  res.w=1.0;
  return res;
}

// TECHNIQUES
technique11 Draw <string UIName="EIGHT ZERO ";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
    SetPixelShader(CompileShader(ps_5_0, PS_Draw()));
  }
}

technique11 ORIGINALPOSTPROCESS <string UIName="Vanilla";> //do not modify this technique
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
    SetPixelShader(CompileShader(ps_5_0, PS_DrawOriginal()));
  }
}
