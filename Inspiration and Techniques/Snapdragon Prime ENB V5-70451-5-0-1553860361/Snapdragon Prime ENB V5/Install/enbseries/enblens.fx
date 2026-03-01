//////////////////////////////////////////////////////////////////////////
//                                                                      //
//  ENBSeries effect file                                               //
//  visit http://enbdev.com for updates                                 //
//  Copyright (c) 2007-2013 Boris Vorontsov                             //
//                                                                      //
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
//                                                                      //
//  Crazy Flare with Weather FX by kingeric1992                         //
//         inspired by John Chapman,    Pseudo Lens Flare               //
//                     Boris Vorontsov, Original enblens.fx             // 
//  for more info, visit                                                //
//    Crazy Flare                                                       // 
//      http://enbseries.enbdev.com/forum/viewtopic.php?f=7&t=3697      //
//    Weather FX                                                        //
//      http://enbseries.enbdev.com/forum/viewtopic.php?f=7&t=3293      //
//                                                                      //
//  update: June 19, 2015                                               //
//////////////////////////////////////////////////////////////////////////
//                                                                      //
//  internal parameters, can be modified                                //
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
//                                                                      //
//  place "//" before "#define" to disable specific feature entirely,   //
//  equivalent to setting effect intensity 0, but save some performance //
//  by skipping computation.                                            //
//                                                                      //
//  example:                                                            //
//      //#define example                                               //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#define ENABLE_SPRITE       //Enable sprite(aka strips at bright spot)
#define ENABLE_DIRT         //Enable lens dirt
#define ENABLE_CHROMA       //Enable post chromatic aberration
#define ENABLE_FLARE        //Enable flare
#define ENABLE_STARBURST    //Enable starburst flare mask
//#define ENABLE_WEATHER_FX   //Enable weather effects

//////////////////////////////////////////////////////////////////////////
//  Global Variables                                                    //
//////////////////////////////////////////////////////////////////////////

#define LUMA_MODE               0   //[0,  2], luminance weight, 0== rgb average, 1== Rec.601, 2== Rec.709.
#define SPRITE_SAMPLE           15  //[6, 20], lower to increawse performance.   
#define SPRITE_COUNT            4   //[1,  4], number of sprites.
#define FLARE_COUNT             5   //[1,  5], number of flares.
#define WEATHER_RAIN_TYPE       0   //[0,  1], Select rain type, 0== droplets, 1== dynamic lens dirt.
#define WEATHER_DROPLET_GRID    8   //grid size of droplet texture, 5 means it use 5x5 grid with total 25 textures

//select sampling textures
//SamplerBloom1~ SamplerBloom6, from clear to blur
#define FLARE1_TEXTURE  SamplerBloom2
#define FLARE2_TEXTURE  SamplerBloom3
#define FLARE3_TEXTURE  SamplerBloom3
#define FLARE4_TEXTURE  SamplerBloom3
#define FLARE5_TEXTURE  SamplerBloom3
#define WEATHER_TEXTURE SamplerBloom3

//change weather id according to _weatherlist.ini [WEATHER###]
//for example:
//rain, 62~66 as lvl 1; 67~69 as lvl 2; 70 as lvl 3
#define RAIN_1_START    62
#define RAIN_1_ENDS     66
#define RAIN_2_START    67
#define RAIN_2_ENDS     70
#define RAIN_3_START    71
#define RAIN_3_ENDS     72
//for example:
//snow, 33 as lvl 1; 34,35 as lvl 2; 36 as lvl 3
#define SNOW_1_START    33
#define SNOW_1_ENDS     33
#define SNOW_2_START    34
#define SNOW_2_ENDS     34
#define SNOW_3_START    35
#define SNOW_3_ENDS     35


//internal oprator, do not change
#if     (LUMA_MODE  == 0)
#define LUMA_CONST float3(0.333, 0.333, 0.333)
#elif   (LUMA_MODE  == 1)
#define LUMA_CONST float3( 0.299, 0.587, 0.114)
#elif   (LUMA_MODE  == 2)
#define LUMA_CONST float3( 0.2126, 0.7152, 0.0722)
#endif

#ifndef ENABLE_FLARE
#define FLARE_COUNT     0
#endif
#ifdef  ENABLE_SPRITE
#define SPRITE1_PRA     float4(0, 0, 1, 0)
#define SPRITE2_PRA     float4(0, 0, 1, 0)
#define SPRITE3_PRA     float4(0, 0, 1, 0)
#define SPRITE4_PRA     float4(0, 0, 1, 0)
#else
#define SPRITE_COUNT    0
#endif
//internal oprator ends

#ifdef  ENABLE_WEATHER_FX
string  Weather_Settings        ="+++++ Weather FX ++++++";
string  Rain_Settings           ="++++++++ Rain +++++++++";
float   Rain_SizeMin            <string UIName="Rain SizeMin";          float UIMin=0; float UIMax=2;   > = {0.05};
float   Rain_SizeMax            <string UIName="Rain SizeMax";          float UIMin=0; float UIMax=2;   > = {0.15};
float   Rain_SizeCurve          = 0.25;
float   Rain_Freqency           <string UIName="Rain Freqency(Hz)";     float UIMin=0; float UIMax=10;  > = {0.8};
float   Rain_Intensity_D        <string UIName="Rain Intensity-D";      float UIMin=0; float UIMax=100; > = {1};
float   Rain_Intensity_N        <string UIName="Rain Intensity-N";      float UIMin=0; float UIMax=100; > = {1};
#if     (WEATHER_RAIN_TYPE == 0)
float   Droplet_Offset_Curve    = 1.5;
float   Droplet_Offset_Scale    = 0.5;
float   Droplet_Ratio_Min       <string UIName="Droplet Ratio Min";                    float UIMin=0;   > = {0.9};
float   Droplet_Ratio_Max       <string UIName="Droplet Ratio Max";                    float UIMin=0;   > = {1.1};
float2  Droplet_Feather         = {0.25, 1};
#else
int     Dirt_ShutterLeaf        <string UIName="Shutter Leaf";          float UIMin=5; float UIMax=8;   > = {7};
float   Dirt_ShutterShape       <string UIName="Shutter Shape";         float UIMin=0; float UIMax=1;   > = {1};
int     Dirt_Angle              <string UIName="Offset Angle(\xB0)";    float UIMin=0; float UIMax=36;  > = {0};
float   Dirt_Curve_D            <string UIName="Dirt Curve-D";          float UIMin=0; float UIMax=100; > = {0.5};
float   Dirt_Curve_N            <string UIName="Dirt Curve-N";          float UIMin=0; float UIMax=100; > = {0.9};
float   Dirt_DestWeight         = 0.2;
#endif
string  Frost_Settings          ="++++++++ Frost +++++++++";
float   Frost_FeatherRadii      <string UIName="Frost Feather Radii";   float UIMin=0; float UIMax=0.6; > = {0.4};
float   Frost_MinRadii          <string UIName="Frost MinRadii";        float UIMin=0; float UIMax=0.6; > = {0.2};
float   Frost_Intensity_D       <string UIName="Frost Intensity-D";     float UIMin=0; float UIMax=1;   > = {0.6};
float   Frost_Intensity_N       <string UIName="Frost Intensity-N";     float UIMin=0; float UIMax=1;   > = {0.6};
float   Frost_Frequency         <string UIName="Frost Frequency(Hz)";   float UIMin=0; float UIMax=2;   > = {0.1};
#endif
#if     (FLARE_COUNT > 0)
string  Flare_Settings          = "++++++ Flare  ++++++";
string  Flare_Info1             = "Flare gloabal setting is";
string  Flare_Info2             = "under LENS in enbseries.ini";
string  Flare1_Settings         = "++++++ Flare1 ++++++";
float   Flare1_Scale            <string UIName="Flare1_Scale";                              > = {-0.6};
float   Flare1_Ratio            <string UIName="Flare1_Ratio";              float UIMin=0;  > = {1};
float   Flare1_Intensity        <string UIName="Flare1_Intensity";          float UIMin=0;  > = {10};
float   Flare1_InnerEdge        <string UIName="Flare1_InnerEdge";          float UIMax=1;  > = {0};
float   Flare1_OuterEdge        <string UIName="Flare1_OuterEdge";          float UIMin=0;  > = {0.8};
float   Flare1_Distort_Curve    <string UIName="Flare1_Distort_Curve";      float UIMin=0;  > = {2};
float   Flare1_Distort_Scale    <string UIName="Flare1_Distort_Scale";      float UIMax=0;  > = {0};
float3  Flare1_Tint             <string UIName="Flare1_Tint"; string UIWidget="color";      > = {1, 0.5, 1};
#endif
#if     (FLARE_COUNT > 1)
string  Flare2_Settings         = "++++++ Flare2 ++++++";
float   Flare2_Scale            <string UIName="Flare2_Scale";                              > = {1.4};
float   Flare2_Ratio            <string UIName="Flare2_Ratio";              float UIMin=0;  > = {1};
float   Flare2_Intensity        <string UIName="Flare2_Intensity";          float UIMin=0;  > = {10};
float   Flare2_InnerEdge        <string UIName="Flare2_InnerEdge";          float UIMax=1;  > = {0};
float   Flare2_OuterEdge        <string UIName="Flare2_OuterEdge";          float UIMin=0;  > = {1.2};
float   Flare2_Distort_Curve    <string UIName="Flare2_Distort_Curve";      float UIMin=0;  > = {2};
float   Flare2_Distort_Scale    <string UIName="Flare2_Distort_Scale";      float UIMax=0;  > = {-0.2};
float3  Flare2_Tint             <string UIName="Flare2_Tint"; string UIWidget="color";      > = {1, 1, 1};
#endif
#if     (FLARE_COUNT > 2)
string  Flare3_Settings         = "++++++ Flare3 ++++++";
float   Flare3_Scale            <string UIName="Flare3_Scale";                              > = {-1};
float   Flare3_Ratio            <string UIName="Flare3_Ratio";              float UIMin=0;  > = {1};
float   Flare3_Intensity        <string UIName="Flare3_Intensity";          float UIMin=0;  > = {10};
float   Flare3_InnerEdge        <string UIName="Flare3_InnerEdge";          float UIMax=1;  > = {0};
float   Flare3_OuterEdge        <string UIName="Flare3_OuterEdge";          float UIMin=0;  > = {1.2};
float   Flare3_Distort_Curve    <string UIName="Flare3_Distort_Curve";      float UIMin=0;  > = {2};
float   Flare3_Distort_Scale    <string UIName="Flare3_Distort_Scale";      float UIMax=0;  > = {-0.2};
float3  Flare3_Tint             <string UIName="Flare3_Tint"; string UIWidget="color";      > = {1, 1, 1};
#endif
#if     (FLARE_COUNT > 3)
string  Flare4_Settings         = "++++++ Flare4 ++++++";
float   Flare4_Scale            <string UIName="Flare4_Scale";                              > = {-1.6};
float   Flare4_Ratio            <string UIName="Flare4_Ratio";              float UIMin=0;  > = {1};
float   Flare4_Intensity        <string UIName="Flare4_Intensity";          float UIMin=0;  > = {10};
float   Flare4_InnerEdge        <string UIName="Flare4_InnerEdge";          float UIMax=1;  > = {0.4};
float   Flare4_OuterEdge        <string UIName="Flare4_OuterEdge";          float UIMin=0;  > = {2};
float   Flare4_Distort_Curve    <string UIName="Flare4_Distort_Curve";      float UIMin=0;  > = {2};
float   Flare4_Distort_Scale    <string UIName="Flare4_Distort_Scale";      float UIMax=0;  > = {-1};
float3  Flare4_Tint             <string UIName="Flare4_Tint"; string UIWidget="color";      > = {1, 1, 1};
#endif
#if     (FLARE_COUNT > 4)
string  Flare5_Settings         = "++++++ Flare5 ++++++";
float   Flare5_Scale            <string UIName="Flare5_Scale";                              > = {1.5};
float   Flare5_Ratio            <string UIName="Flare5_Ratio";              float UIMin=0;  > = {1};
float   Flare5_Intensity        <string UIName="Flare5_Intensity";          float UIMin=0;  > = {10};
float   Flare5_InnerEdge        <string UIName="Flare5_InnerEdge";          float UIMax=1;  > = {0.2};
float   Flare5_OuterEdge        <string UIName="Flare5_OuterEdge";          float UIMin=0;  > = {2};
float   Flare5_Distort_Curve    <string UIName="Flare5_Distort_Curve";      float UIMin=0;  > = {2};
float   Flare5_Distort_Scale    <string UIName="Flare5_Distort_Scale";      float UIMax=0;  > = {-1};
float3  Flare5_Tint             <string UIName="Flare5_Tint"; string UIWidget="color";      > = {1, 1, 1};
#endif
#ifdef  ENABLE_STARBURST
string  Starburst_Settings      = "+++++ Starburst +++++";
float   Starburst_Intensity     <string UIName="Starburst_Intensity";       float UIMin=0;  > = {1};
float   Starburst_Curve         <string UIName="Starburst_Curve";           float UIMin=0;  > = {1.5};
float   Starburst_InnerEdge     <string UIName="Starburst_InnerEdge";       float UIMax=1;  > = {0.2};
float   Starburst_OuterEdge     <string UIName="Starburst_OuterEdge";       float UIMin=0;  > = {2.0};
float   Starburst_Scale         = 0.2;
#endif
#if     (SPRITE_COUNT > 0)
string  Sprite_Setting01        = "++++++++ Sprite ++++++++";
string  Sprite_Setting02        = "Sprite gloabal settings";
string  Sprite_Setting03        = "+++++++++++++++++++++";
float   Sprite_Decay_Weight     = 0.6;  // recommand setting: (decay weight)^(sprite count/2) >= 0.01
float   Sprite_Curve_D          <string UIName="Sprite_Curve_D";                    float UIMin=0;  > = {2};    //Sprite sensitivity, day
float   Sprite_Curve_N          <string UIName="Sprite_Curve_N";                    float UIMin=0;  > = {2};    //Sprite sensitivity, night
float   Sprite_Curve_I          <string UIName="Sprite_Curve_I";                    float UIMin=0;  > = {2};    //Sprite sensitivity, interior
float   Sprite_Intensity_D      <string UIName="Sprite_Intensity_D";                float UIMin=0;  > = {1.5};  //Sprite intensity, day
float   Sprite_Intensity_N      <string UIName="Sprite_Intensity_N";                float UIMin=0;  > = {1.5};  //Sprite intensity, night
float   Sprite_Intensity_I      <string UIName="Sprite_Intensity_I";                float UIMin=0;  > = {1.5};  //Sprite intersity, interior    
float3  Sprite_Tint             <string UIName="Sprite_Tint";        string UIWidget="color"; > = {0.5, 1, 1};  //Sprite color filter
string  Sprite1_Setting         = "++++++ Sprite1 ++++++";
int     Sprite1_Angle           <string UIName="Sprite1_Angle(\xB0)";float UIMin=0; float UIMax=180;> = {20};   //Sprite 1 angle
float   Sprite1_Width           <string UIName="Sprite1_Width";      float UIMin=0; float UIMax=1;  > = {0.5};  //Sprite 1 width, 1==screen width
float   Sprite1_Curve           <string UIName="Sprite1_Curve";                     float UIMin=0;  > = {7};    //Sprite 1 sensitivity
float   Sprite1_Intensity       <string UIName="Sprite1_Intensity";                 float UIMin=0;  > = {1.5};  //Sprite 1 intensity
float   Sprite1_Dynamic         <string UIName="Sprite1_Dynamic";                   float UIStep=5; > = {-5};   //Sprite 1 rotation speed by scene info
#define SPRITE1_PRA             float4(Sprite1_Angle + S_angle.x * Sprite1_Dynamic, Sprite1_Width, Sprite1_Curve, Sprite1_Intensity)
#endif
#if     (SPRITE_COUNT > 1)
string  Sprite2_Setting         = "++++++ Sprite2 ++++++";
int     Sprite2_Angle           <string UIName="Sprite2_Angle(\xB0)";float UIMin=0; float UIMax=180;> = {80};   //Sprite 2 angle
float   Sprite2_Width           <string UIName="Sprite2_Width";      float UIMin=0; float UIMax=1;  > = {0.5};  //Sprite 2 width, 1==screen width
float   Sprite2_Curve           <string UIName="Sprite2_Curve";                     float UIMin=0;  > = {7};    //Sprite 2 sensitivity
float   Sprite2_Intensity       <string UIName="Sprite2_Intensity";                 float UIMin=0;  > = {2};    //Sprite 2 intensity
float   Sprite2_Dynamic         <string UIName="Sprite2_Dynamic";                   float UIStep=5; > = {10};   //Sprite 2 rotation speed by scene info
#define SPRITE2_PRA             float4(Sprite2_Angle + S_angle.y * Sprite2_Dynamic, Sprite2_Width, Sprite2_Curve, Sprite2_Intensity)
#endif
#if     (SPRITE_COUNT > 2)
string  Sprite3_Setting         = "++++++ Sprite3 ++++++";
int     Sprite3_Angle           <string UIName="Sprite3_Angle(\xB0)";float UIMin=0; float UIMax=180;> = {140};  //Sprite 3 angle
float   Sprite3_Width           <string UIName="Sprite3_Width";      float UIMin=0; float UIMax=1;  > = {0.5};  //Sprite 3 width, 1==screen width
float   Sprite3_Curve           <string UIName="Sprite3_Curve";                     float UIMin=0;  > = {7};    //Sprite 3 sensitivity
float   Sprite3_Intensity       <string UIName="Sprite3_Intensity";                 float UIMin=0;  > = {2};    //Sprite 3 intensity
float   Sprite3_Dynamic         <string UIName="Sprite3_Dynamic";                   float UIStep=5; > = {-15};  //Sprite 3 rotation speed by scene info
#define SPRITE3_PRA             float4(Sprite3_Angle + S_angle.z * Sprite3_Dynamic, Sprite3_Width, Sprite3_Curve, Sprite3_Intensity)
#endif
#if     (SPRITE_COUNT > 3)
string  Sprite4_Setting         = "++++++ Sprite4 ++++++";
int     Sprite4_Angle           <string UIName="Sprite4_Angle(\xB0)";float UIMin=0; float UIMax=180;> = {120};  //Sprite 4 angle
float   Sprite4_Width           <string UIName="Sprite4_Width";      float UIMin=0; float UIMax=1;  > = {0.5};  //Sprite 4 width, 1==screen width
float   Sprite4_Curve           <string UIName="Sprite4_Curve";                     float UIMin=0;  > = {7};    //Sprite 4 sensitivity
float   Sprite4_Intensity       <string UIName="Sprite4_Intensity";                 float UIMin=0;  > = {2};    //Sprite 4 intensity
float   Sprite4_Dynamic         <string UIName="Sprite4_Dynamic";                   float UIStep=5; > = {20};   //Sprite 4 rotation speed by scene info
#define SPRITE4_PRA             float4(Sprite4_Angle + S_angle.w * Sprite4_Dynamic, Sprite4_Width, Sprite4_Curve, Sprite4_Intensity)
#endif
#ifdef  ENABLE_CHROMA
string  Chroma_Setting01        = "++++++ Chroma ++++++";
string  Chroma_Setting02        = "++++++++++++++++++";
float   Chroma_Scale            <string UIName="Chroma_Scale";float UIStep = 0.001; float UIMin=0; > = {0.005}; //Post Chroma strength
float   Chroma_Curve            <string UIName="Chroma_Curve";                      float UIMin=0; > = {2};     //Post Chroma strength curve from screen center to edge.
#endif

//////////////////////////////////////////////////////////////////////////
//  external parameters, do not modify                                  //
//////////////////////////////////////////////////////////////////////////

//keyboard controlled temporary variables (in some versions exists in the config file). 
//Press and hold key 1,2,3...8 together with PageUp or PageDown to modify. By default all set to 1.0
float4  tempF1;         //1,2,3,4
float4  tempF2;         //5,6,7,8
float4  tempF3;         //9,0
float4  ScreenSize;     //x=Width, y=1/Width, z=ScreenScaleY, w=1/ScreenScaleY
float   ENightDayFactor;//changes in range 0..1, 0 means that night time, 1 - day time
float   EInteriorFactor;//changes 0 or 1. 0 means that exterior, 1 - interior
float4  Timer;          //x=generic timer in range 0..1, period of 16777216 ms (4.6 hours), w=frame time elapsed (in seconds)
float4  TempParameters; //additional info for computations
float4  LensParameters; //x=reflection intensity, y=reflection power, z=dirt intensity, w=dirt power
float   FieldOfView;    //fov in degrees
float4  WeatherAndTime; //.x - current weather index, .y - outgoing weather index, .z - weather transition, .w - time of the day in 24 standart hours.

texture2D texColor;     //output of Draw
texture2D texMask;      //enblensmask texture
texture2D texBloom1;    //Original ENB blurred input;fullres, output of bloom prepass
texture2D texBloom2;    //Downsampled to 512
texture2D texBloom3;    //Downsampled to 256
texture2D texBloom4;    //Downsampled to 128
texture2D texBloom5;    //Downsampled to 64
texture2D texBloom6;    //Downsampled to 32
texture2D texBloom7;    //empty
texture2D texBloom8;    //empty

#ifdef ENABLE_WEATHER_FX
texture2D texWeather    <string ResourceName="enbweather.bmp"; >;//.rgb is droplet normal map, .a is frost mask
sampler2D SamplerWeather = sampler_state
{
    Texture = <texWeather>;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
    MipFilter = NONE;
    AddressU  = Clamp;
    AddressV  = Clamp;
    SRGBTexture = FALSE;
    MaxMipLevel = 0;
    MipMapLodBias = 0;
};
#endif


sampler2D SamplerColor = sampler_state
{
    Texture   = <texColor>;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
    MipFilter = NONE;
    AddressU  = Clamp;
    AddressV  = Clamp;
    SRGBTexture=FALSE;
    MaxMipLevel=0;
    MipMapLodBias=0;
};

sampler2D SamplerMask = sampler_state
{
    Texture   = <texMask>;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
    MipFilter = NONE;
    AddressU  = Clamp;
    AddressV  = Clamp;
    SRGBTexture=FALSE;
    MaxMipLevel=0;
    MipMapLodBias=0;
};

sampler2D SamplerBloom1 = sampler_state
{
    Texture   = <texBloom1>;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
    MipFilter = NONE;
    AddressU  = Clamp;
    AddressV  = Clamp;
    SRGBTexture=FALSE;
    MaxMipLevel=0;
    MipMapLodBias=0;
};

sampler2D SamplerBloom2 = sampler_state
{
    Texture   = <texBloom2>;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
    MipFilter = NONE;
    AddressU  = Clamp;
    AddressV  = Clamp;
    SRGBTexture=FALSE;
    MaxMipLevel=0;
    MipMapLodBias=0;
};

sampler2D SamplerBloom3 = sampler_state
{
    Texture   = <texBloom3>;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
    MipFilter = NONE;
    AddressU  = Clamp;
    AddressV  = Clamp;
    SRGBTexture=FALSE;
    MaxMipLevel=0;
    MipMapLodBias=0;
};

sampler2D SamplerBloom4 = sampler_state
{
    Texture   = <texBloom4>;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
    MipFilter = NONE;
    AddressU  = Clamp;
    AddressV  = Clamp;
    SRGBTexture=FALSE;
    MaxMipLevel=0;
    MipMapLodBias=0;
};

sampler2D SamplerBloom5 = sampler_state
{
    Texture   = <texBloom5>;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
    MipFilter = NONE;
    AddressU  = Clamp;
    AddressV  = Clamp;
    SRGBTexture=FALSE;
    MaxMipLevel=0;
    MipMapLodBias=0;
};

sampler2D SamplerBloom6 = sampler_state
{
    Texture   = <texBloom6>;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
    MipFilter = NONE;
    AddressU  = Clamp;
    AddressV  = Clamp;
    SRGBTexture=FALSE;
    MaxMipLevel=0;
    MipMapLodBias=0;
};

sampler2D SamplerBloom7 = sampler_state
{
    Texture   = <texBloom7>;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
    MipFilter = NONE;
    AddressU  = Clamp;
    AddressV  = Clamp;
    SRGBTexture=FALSE;
    MaxMipLevel=0;
    MipMapLodBias=0;
};

sampler2D SamplerBloom8 = sampler_state
{
    Texture   = <texBloom8>;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
    MipFilter = NONE;
    AddressU  = Clamp;
    AddressV  = Clamp;
    SRGBTexture=FALSE;
    MaxMipLevel=0;
    MipMapLodBias=0;
};

//////////////////////////////////////////////////////////////////////////
//  Structs                                                             //
//////////////////////////////////////////////////////////////////////////
struct VS_OUTPUT_POST
{
    float4 vpos     : POSITION;
    float2 txcoord0 : TEXCOORD0;
};
struct VS_INPUT_POST
{
    float3 pos      : POSITION;
    float2 txcoord0 : TEXCOORD0;
};
#ifdef ENABLE_WEATHER_FX
struct VS_OUTPUT_WEATHER
{
    float4 vpos     : POSITION;
    float2 txcoord0 : TEXCOORD0;
    float3 wconst   : TEXCOORD1;
};
#endif
//////////////////////////////////////////////////////////////////////////
//  Funstions                                                           //
//////////////////////////////////////////////////////////////////////////

float   Lum(float3 color)
{
    return dot(color, LUMA_CONST);
}

float2 Distort( float2 coord, float curve, float scale)
{
    float2 dist   = coord - 0.5;
    float  r      = length(float2( dist.x * ScreenSize.z, dist.y));
    float2 offset = pow( 2 * r, curve) * (dist / r) * scale;
    
    return coord + offset;
}

//temp.x == curve, .y == scale
float4 Chroma( sampler2D inputtex, float2 coord, float2 temp)
{
    float4 res = 0;
    res.r  = tex2D(inputtex, Distort(coord, temp.x, temp.y)).r;
    res.ga = tex2D(inputtex, coord).ga;
    res.b  = tex2D(inputtex, Distort(coord, temp.x, -temp.y)).b;
    return res;
}

float   Random(float2 co)
{
    return frac(sin(dot(co.xy, float2(12.9898, 78.233))) * 43758.5453);
}

//////////////////////////////////////////////////////////////////////////
//  Shaders                                                             //
//////////////////////////////////////////////////////////////////////////
#ifdef ENABLE_WEATHER_FX
// Dynamic Lens Dirt(rainFX) VS
//seed.xy == offset; .z == size; .w == ratio
VS_OUTPUT_WEATHER VS_Rain(VS_INPUT_POST IN, uniform float4 seed, uniform float toffset)
{
    VS_OUTPUT_WEATHER OUT;  

    float2 rainlvl;
    if(WeatherAndTime.x > (RAIN_1_START - 0.2) && WeatherAndTime.x < (RAIN_1_ENDS + 0.2))
        rainlvl.x = 1;
    else if(WeatherAndTime.x > (RAIN_2_START - 0.2) && WeatherAndTime.x < (RAIN_2_ENDS + 0.2))
        rainlvl.x = 2;
    else if(WeatherAndTime.x > (RAIN_3_START - 0.2) && WeatherAndTime.x < (RAIN_3_ENDS + 0.2))
        rainlvl.x = 3;
    else
        rainlvl.x = 0;
    
    if(WeatherAndTime.y > (RAIN_1_START - 0.2) && WeatherAndTime.y < (RAIN_1_ENDS + 0.2))
        rainlvl.y = 1;
    else if(WeatherAndTime.y > (RAIN_2_START - 0.2) && WeatherAndTime.y < (RAIN_2_ENDS + 0.2))
        rainlvl.y = 2;
    else if(WeatherAndTime.y > (RAIN_3_START - 0.2) && WeatherAndTime.y < (RAIN_3_ENDS + 0.2))
        rainlvl.y = 3;
    else
        rainlvl.y = 0;
    
    rainlvl /= 3;
    rainlvl *= (1 - EInteriorFactor);
    
    float  frequency = lerp( 0, Rain_Freqency, lerp(rainlvl.x, rainlvl.y, step(WeatherAndTime.z, 0.5)));
    float  time      = (Timer.x * 16777.216 + toffset) * frequency;
    float2 timeconst = float2(floor(time), frac(time));
    float  random    = Random( float2(timeconst.x, seed.z));       
    float  size      = lerp( Rain_SizeMin, Rain_SizeMax, pow(random, lerp(1, Rain_SizeCurve, rainlvl.y)));
    float2 offset    = float2( Random( float2(timeconst.x, seed.x)), Random( float2(timeconst.x, seed.y))) * 2 - 1;
    float  angle     = Random(float2(timeconst.x, seed.y)) * 6.28;
    size            *= 1 + timeconst.y * 0.2;//enlarge
#if (WEATHER_RAIN_TYPE == 0)
    float  ratio     = lerp(Droplet_Ratio_Min, Droplet_Ratio_Max, Random(float2(timeconst.x, seed.w)));
    float2 rotate;
    sincos(angle, rotate.y, rotate.x);
    float2 pos       = IN.pos.xy * size * float2(ratio, ScreenSize.z);
    pos = float2( dot(pos, rotate * float2( 1, -1)), dot(pos, rotate.yx));
#else
    float2 pos       = IN.pos.xy * size * float2(1, ScreenSize.z);
#endif
    OUT.vpos         = float4(pos - offset, IN.pos.z, 1.0);;
    OUT.txcoord0.xy  = IN.txcoord0.xy;
    OUT.wconst       = (frequency > 0 )? pow( 1 - timeconst.y, 0.3) * (abs(WeatherAndTime.z - 0.5) * 2): 0; //Alpha, fades during transition  
    OUT.wconst.x     = timeconst;
    OUT.wconst.y     = angle;
    return OUT;
}
#if (WEATHER_RAIN_TYPE == 0)
// Droplets(rainFX) PS
float4 PS_Droplets(VS_OUTPUT_WEATHER IN, float2 vPos : VPOS, uniform float2 seed) : COLOR
{
    clip((IN.wconst.z < 0.001)? -1:1 );//discard if weather constant == 0
    float2 coord = IN.txcoord0.xy;
    float2 sscoord = ScreenSize.y * vPos;
    sscoord.y *= ScreenSize.z;  

    float  intensity = lerp(Rain_Intensity_N, Rain_Intensity_D, ENightDayFactor);
    
    float2 index = float2(Random(float2(IN.wconst.x, seed.x)), Random(float2(IN.wconst.x, seed.y)));
    index *= WEATHER_DROPLET_GRID;
    index  = floor(index) + coord;

    float3 normal = tex2D(SamplerWeather, index / WEATHER_DROPLET_GRID).rgb - 0.5;
    normal *= 2;
    normal.xy = normalize(normal.xy) * pow(length(normal.xy), Droplet_Offset_Curve);
    
    float2 rotate;
    sincos(-IN.wconst.y, rotate.y, rotate.x);
    normal.xy = float2( dot(normal.xy, rotate * float2( 1, -1)), dot(normal.xy, rotate.yx));

    float4 res = tex2D(WEATHER_TEXTURE, sscoord + normal.xy * Droplet_Offset_Scale);
    res.a = IN.wconst.z * smoothstep(Droplet_Feather.x, Droplet_Feather.y, normal.z);
	res.rgb *= intensity * res.a;
    res.a   *= 0.75;
    return res;
}

#else
// Dynamic Lens Dirt(rainFX) PS
float4 PS_Rain(VS_OUTPUT_WEATHER IN, float2 vPos : VPOS) : COLOR
{
    clip((IN.wconst.z < 0.001)? -1:1 );//discard if weather constant == 0
    
    float2 coord     = IN.txcoord0.xy - 0.5;
    float2 pixelSize = ScreenSize.y;
    pixelSize.y     *= ScreenSize.z;
    float  theta     = ( length(coord) == 0)? 0 : atan2(coord.y, coord.x) + 3.1415926;
    float  leaf      = 6.28318530 / Dirt_ShutterLeaf;
    float  delta     = (theta + Dirt_Angle) % leaf;
    delta           -= leaf * 0.5;
    float  r         = lerp(1, cos(leaf * 0.5) / cos(delta), Dirt_ShutterShape) * 0.5;
    
    float  intensity = lerp(Rain_Intensity_N, Rain_Intensity_D, ENightDayFactor);
    float  curve     = lerp(Dirt_Curve_N, Dirt_Curve_D, ENightDayFactor);
    
    //Lens Dirt coloring by Boris
    float4 templens  = tex2D(SamplerBloom6, vPos * pixelSize);
    float  maxlens   = max(templens.r, max( templens.g, templens.b));
    float  tempnor   = maxlens / ( 1.0 + maxlens);
    templens.rgb    *= pow(tempnor, curve) * intensity * IN.wconst.z;
    templens.a       = lerp(IN.wconst.z, 0, Dirt_DestWeight);
    templens        *= 1 - smoothstep(r - 0.1, r,length(coord));
    return templens;
}
#endif

// Frost vignette VS
VS_OUTPUT_WEATHER VS_Snow(VS_INPUT_POST IN)
{
    VS_OUTPUT_WEATHER OUT;

    float2 lvl;
    if(WeatherAndTime.x > (SNOW_1_START - 0.2) && WeatherAndTime.x < (SNOW_1_ENDS + 0.2))
        lvl.x = 1;
    else if(WeatherAndTime.x > (SNOW_2_START - 0.2) && WeatherAndTime.x < (SNOW_2_ENDS + 0.2))
        lvl.x = 2;
    else if(WeatherAndTime.x > (SNOW_3_START - 0.2) && WeatherAndTime.x < (SNOW_3_ENDS + 0.2))
        lvl.x = 3;
    else
        lvl.x = 0;
        
    if(WeatherAndTime.y > (SNOW_1_START - 0.2) && WeatherAndTime.y < (SNOW_1_ENDS + 0.2))
        lvl.y = 1;
    else if(WeatherAndTime.y > (SNOW_2_START - 0.2) && WeatherAndTime.y < (SNOW_2_ENDS + 0.2))
        lvl.y = 2;
    else if(WeatherAndTime.y > (SNOW_3_START - 0.2) && WeatherAndTime.y < (SNOW_3_ENDS + 0.2))
        lvl.y = 3;
    else
        lvl.y = 0;
        
    lvl.xy /= 3;
    lvl    *= (1 - EInteriorFactor);
        
    OUT.vpos        = float4(IN.pos.x, IN.pos.y, IN.pos.z, 1.0);
    OUT.txcoord0.xy = IN.txcoord0.xy + TempParameters.xy;
    OUT.wconst      = lerp(lvl.y, lvl.x, WeatherAndTime.z);
    
    return OUT;
}

// Frost vignette PS
float4 PS_Frost(VS_OUTPUT_WEATHER IN, float2 vPos : VPOS) : COLOR
{
    clip((IN.wconst.x < 0.001)? -1:1 );//discard if weather constant == 0
    float  r          = length(IN.txcoord0.xy - 0.5);   
    float4 res        = tex2D(SamplerBloom6, IN.txcoord0.xy);
    float4 frostmask  = tex2D(SamplerWeather, IN.txcoord0.xy).a;
    float  frostradii = lerp( 0.7071, Frost_MinRadii, IN.wconst.x);
    float  intensity  = lerp(Frost_Intensity_N, Frost_Intensity_D, ENightDayFactor);
    
    res   += frostmask * intensity * smoothstep( frostradii, 0.7071, r);
    res.a *= smoothstep( frostradii + Frost_FeatherRadii * (sin(Timer.x * 105360.91648 * Frost_Frequency) + 1) * 0.2, frostradii + Frost_FeatherRadii, r);
    
    return res;
}
#endif

//flare.x == scale, flare.y == ratio
VS_OUTPUT_POST VS_Draw(VS_INPUT_POST IN, uniform float2 flare)
{
    VS_OUTPUT_POST OUT;
    OUT.vpos        = float4(IN.pos.xyz, 1.0);
    OUT.vpos.xy    *= flare.x;
    OUT.vpos.x     *= flare.y;
    OUT.txcoord0.xy = IN.txcoord0.xy + TempParameters.xy;//1.0/(bloomtexsize*2.0)
    return OUT;
}

//Sprite + Dirt, FullScreen, reduced res
float4 PS_Draw(VS_OUTPUT_POST IN) : COLOR
{
    float2 coord = IN.txcoord0;
    float4 res   = 0;
#ifdef  ENABLE_DIRT
    float3 dirt   = tex2D(SamplerBloom6, coord).rgb;
    float3 mask   = tex2D(SamplerMask, coord).rgb;
    float  normal = max(dirt.x, max(dirt.y, dirt.z));
    normal = pow( normal / ( 1.0 + normal), LensParameters.w);
    res.rgb += mask * dirt * normal * LensParameters.z;
#endif  //Lens Dirt
#ifdef  ENABLE_SPRITE
    float2 S_coord = IN.txcoord0.xy * 2;
    float4 S_angle = tex2D(SamplerBloom6, 0.5)  + tex2D(SamplerBloom5, 0.5);
    S_angle.w = Lum(S_angle.xyz);    
    float2 S_offset= step(1, S_coord);
    S_coord -= S_offset;
    float4 S_setting = lerp(lerp(SPRITE1_PRA, SPRITE2_PRA, S_offset.x), lerp(SPRITE3_PRA, SPRITE4_PRA, S_offset.x), S_offset.y);
    sincos(radians(S_setting.x), S_offset.y, S_offset.x);
    S_offset *= S_setting.y / SPRITE_SAMPLE;
    float2 offset = S_coord - S_offset * (1 + SPRITE_SAMPLE) * 0.5;
    float2 S_weight = 0;
    for(int i=0; i < SPRITE_SAMPLE; i++)
    {
        offset += S_offset;
        float S_edge = (abs(offset.x - 0.5) >= 0.5 || abs(offset.y - 0.5) >= 0.5)? 0:1;
        float S_alpha= Lum(tex2D(SamplerBloom1, offset).rgb);
        S_weight.x = pow(Sprite_Decay_Weight, floor(abs(SPRITE_SAMPLE * 0.5 - i))) * S_edge;
        res.a  += pow( S_alpha / (S_alpha + 1), S_setting.z) * S_weight.x * S_setting.w;
        S_weight.y += S_weight.x;
    }
    res.a /= S_weight.y;
#endif  //Sprite prepass
    return res;
}

#ifdef ENABLE_FLARE
//flare PS with distortion, center/edge vignette
//distortion.x == curve, .y == scale, .z == inner edge, .w == outer edge
float4 PS_Flare(VS_OUTPUT_POST IN, uniform sampler2D inputtex, uniform float4 tint, uniform float4 flare) : COLOR
{
    float2 coord = Distort(IN.txcoord0.xy, flare.x, flare.y);
    float4 res   = tex2D(inputtex, coord);
    res.a    = max( res.r, max( res.g, res.b));//normal weight
    res.a    = pow( res.a / ( 1.0 + res.a), LensParameters.y);
    coord    = IN.txcoord0.xy;
    coord   -= 0.5;
    coord.y *= ScreenSize.w;
    coord.y  = length(coord) * 2;
    res.rgb *= smoothstep(flare.z, 1, coord.y);//center vignette
    res.rgb *= 1 - smoothstep(0, flare.w, coord.y);//edge vignette
    res.rgb *= res.a * tint.rgb * LensParameters.x * tint.a;//intensity
    return res;
}
#endif  //Flare

//Post Process, Chromatic, Starburst, sprite
float4  PS_PostProcess(VS_OUTPUT_POST IN) : COLOR
{
    float2 coord = IN.txcoord0.xy;
    
#ifdef ENABLE_CHROMA
    float4 res = Chroma(SamplerColor, coord, float2(Chroma_Curve, Chroma_Scale));
#else
    float4 res = tex2D(SamplerColor, coord);
#endif //Chromatic Aberration
    
#ifdef ENABLE_STARBURST
    float alpha;
    float dist   = length((coord - 0.5) * float2(1, ScreenSize.w)) * 2;
    float weight = smoothstep(Starburst_InnerEdge, 1, dist);
    weight  *= 1 - smoothstep(0, Starburst_OuterEdge, dist);
    coord    = Distort(0.5 + (coord - 0.5) * Starburst_Scale, 0, 1);
    alpha    = Lum(tex2D(SamplerBloom1, coord).rgb);
    coord    = float2( coord.x - coord.y, coord.x + coord.y - 1) * 0.7071 + 0.5; //rotate 45 degree
    alpha   -= Lum(tex2D(SamplerBloom1, coord).rgb);
    res.rgb *= 1.0 + pow(abs(alpha), Starburst_Curve) * Starburst_Intensity * 1000 * weight;
#endif  //Starburst

#ifdef  ENABLE_SPRITE
    res.a = 0;
    coord = IN.txcoord0.xy;
    float4 S_angle = tex2D(SamplerBloom6, 0.5)  + tex2D(SamplerBloom5, 0.5);
    S_angle.w = Lum(S_angle.xyz);
    float4  bloom = 0;
    float2  S_offset;
    for(int j=0; j<4; j++)
    {
        float2 S_grid    = float2(saturate(j - 1), 1.5 - abs(j - 1.5));
        float4 S_setting = lerp(lerp(SPRITE1_PRA, SPRITE2_PRA, S_grid.x), lerp(SPRITE3_PRA, SPRITE4_PRA, S_grid.x), S_grid.y);
        sincos(radians(S_setting.x), S_offset.y, S_offset.x);
        S_offset *= S_setting.y / SPRITE_SAMPLE / SPRITE_SAMPLE;
        float2 offset = S_grid + coord - S_offset * (1 + SPRITE_SAMPLE);
        offset *= 0.5;
        S_grid += 0.5;
        S_grid *= 0.5;
        for(int i=0; i < SPRITE_SAMPLE; i++)
        {
            offset += S_offset;
            float S_edge = (abs(S_grid.x - offset.x) >= 0.25 || abs(S_grid.y - offset.y) >= 0.25 )? 0:1;
            res.a += tex2D(SamplerColor, offset).a * S_edge * S_setting.w;
        }
    }
    float S_Curve     = lerp(lerp(Sprite_Curve_N, Sprite_Curve_D, ENightDayFactor), Sprite_Curve_I, EInteriorFactor);
    float S_Intensity = lerp(lerp(Sprite_Intensity_N, Sprite_Intensity_D, ENightDayFactor), Sprite_Intensity_I, EInteriorFactor);
    bloom   += tex2D(SamplerBloom1, coord) + tex2D(SamplerBloom2, coord) + tex2D(SamplerBloom3, coord);
    bloom   += tex2D(SamplerBloom4, coord) + tex2D(SamplerBloom5, coord) + tex2D(SamplerBloom6, coord);
    bloom.a  = max(max(bloom.r, bloom.g), bloom.b);
    res.rgb += bloom.rgb * pow(bloom.a / (1 + bloom.a), S_Curve) * res.a * Sprite_Tint * S_Intensity;
#endif  //Sprite
    res.rgb  = max( min( res.rgb, 32768.0), 0.0);
    return res;
}

//////////////////////////////////////////////////////////////////////////
//  Passes                                                              //
//////////////////////////////////////////////////////////////////////////

//actual computation, draw all effects to small texture
technique Draw
{
    pass p0
    {
        VertexShader = compile vs_3_0 VS_Draw(float2(1, 1));
        PixelShader  = compile ps_3_0 PS_Draw();
        
        ColorWriteEnable=RED|GREEN|BLUE|ALPHA;
        DitherEnable=FALSE;
        ZEnable=FALSE;
        CullMode=NONE;
        ALPHATESTENABLE=FALSE;
        SEPARATEALPHABLENDENABLE=FALSE;
        StencilEnable=FALSE;
        FogEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
    }
#if     (FLARE_COUNT > 0)
    pass p1
    {
        VertexShader = compile vs_3_0 VS_Draw(float2(Flare1_Scale, Flare1_Ratio));
        PixelShader  = compile ps_3_0 PS_Flare(FLARE1_TEXTURE, float4(Flare1_Tint, Flare1_Intensity), 
                        float4(Flare1_Distort_Curve, Flare1_Distort_Scale, Flare1_InnerEdge, Flare1_OuterEdge));

        AlphaBlendEnable=TRUE;
        SrcBlend=ONE;
        DestBlend=ONE;
        ColorWriteEnable=RED|GREEN|BLUE;//warning, no alpha output!
        DitherEnable=FALSE;
        ZEnable=FALSE;
        CullMode=NONE;
        ALPHATESTENABLE=FALSE;
        SEPARATEALPHABLENDENABLE=FALSE;
        StencilEnable=FALSE;
        FogEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
    }
#endif
#if     (FLARE_COUNT > 1)
    pass p2
    {
        VertexShader = compile vs_3_0 VS_Draw(float2(Flare2_Scale, Flare2_Ratio));
        PixelShader  = compile ps_3_0 PS_Flare(FLARE2_TEXTURE, float4(Flare2_Tint, Flare2_Intensity), 
                        float4(Flare2_Distort_Curve, Flare2_Distort_Scale, Flare2_InnerEdge, Flare2_OuterEdge));
    }
#endif
#if     (FLARE_COUNT > 2)
    pass p3
    {
        VertexShader = compile vs_3_0 VS_Draw(float2(Flare3_Scale, Flare3_Ratio));
        PixelShader  = compile ps_3_0 PS_Flare(FLARE3_TEXTURE, float4(Flare3_Tint, Flare3_Intensity), 
                        float4(Flare3_Distort_Curve, Flare3_Distort_Scale, Flare3_InnerEdge, Flare3_OuterEdge));
    }
#endif
#if     (FLARE_COUNT > 3)
    pass p4
    {
        VertexShader = compile vs_3_0 VS_Draw(float2(Flare4_Scale, Flare4_Ratio));
        PixelShader  = compile ps_3_0 PS_Flare(FLARE4_TEXTURE, float4(Flare4_Tint, Flare4_Intensity), 
                        float4(Flare4_Distort_Curve, Flare4_Distort_Scale, Flare4_InnerEdge, Flare4_OuterEdge));
    }
#endif
#if     (FLARE_COUNT > 4)
    pass p5
    {
        VertexShader = compile vs_3_0 VS_Draw(float2(Flare5_Scale, Flare5_Ratio));
        PixelShader  = compile ps_3_0 PS_Flare(FLARE5_TEXTURE, float4(Flare5_Tint, Flare5_Intensity), 
                        float4(Flare5_Distort_Curve, Flare5_Distort_Scale, Flare5_InnerEdge, Flare5_OuterEdge));
    }
#endif
}

//final pass, output to screen with additive blending and no alpha
technique LensPostPass
{
    pass p0
    {
        VertexShader = compile vs_3_0 VS_Draw(float2(1, 1));
        PixelShader  = compile ps_3_0 PS_PostProcess();

        AlphaBlendEnable=TRUE;
        SrcBlend=ONE;
        DestBlend=ONE;
        ColorWriteEnable=RED|GREEN|BLUE;//warning, no alpha output!
        DitherEnable=FALSE;
        ZEnable=FALSE;
        CullMode=NONE;
        ALPHATESTENABLE=FALSE;
        SEPARATEALPHABLENDENABLE=FALSE;
        StencilEnable=FALSE;
        FogEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
    }
#ifdef ENABLE_WEATHER_FX
    pass p1
    {
        VertexShader = compile vs_3_0 VS_Rain(float4(72, 11, 19, 5), 1.7);
#if (WEATHER_RAIN_TYPE == 0)
        PixelShader  = compile ps_3_0 PS_Droplets(float2(3, 5));
#else
        PixelShader  = compile ps_3_0 PS_Rain();
#endif
        AlphaBlendEnable=TRUE;
        SrcBlend=ONE;
        DestBlend=INVSRCALPHA;
        ColorWriteEnable=RED|GREEN|BLUE;//warning, no alpha output!
        CullMode=NONE;
        AlphaTestEnable=FALSE;
        SeparateAlphaBlendEnable=FALSE;
        SRGBWriteEnable=FALSE;
    }
    pass p2
    {
        VertexShader = compile vs_3_0 VS_Rain(float4(88, 3, 500, 11), 2.9);
#if (WEATHER_RAIN_TYPE == 0)       
        PixelShader  = compile ps_3_0 PS_Droplets(float2(2, 7));
#else
        PixelShader  = compile ps_3_0 PS_Rain();
#endif
    }
    
    pass p3
    {
        VertexShader = compile vs_3_0 VS_Rain(float4(20, 100, 700, 9), 3.7);
#if (WEATHER_RAIN_TYPE == 0)      
        PixelShader  = compile ps_3_0 PS_Droplets(float2(11, 13));
#else
        PixelShader  = compile ps_3_0 PS_Rain();
#endif
    }
    
    pass p4
    {
        VertexShader = compile vs_3_0 VS_Rain(float4(150, 3, 250, 7), 1.3);
#if (WEATHER_RAIN_TYPE == 0)      
        PixelShader  = compile ps_3_0 PS_Droplets(float2(17, 19));
#else
        PixelShader  = compile ps_3_0 PS_Rain();
#endif
    }
    
    pass p5
    {
        VertexShader = compile vs_3_0 VS_Rain(float4(66, 220, 250, 19), 0);
#if (WEATHER_RAIN_TYPE == 0)       
        PixelShader  = compile ps_3_0 PS_Droplets(float2(23, 29));
#else
        PixelShader  = compile ps_3_0 PS_Rain();
#endif
    }
    
    pass p6
    {
        VertexShader = compile vs_3_0 VS_Snow();
        PixelShader  = compile ps_3_0 PS_Frost();

        AlphaBlendEnable=TRUE;
        SrcBlend=SRCALPHA;
        DestBlend=INVSRCALPHA;
        ColorWriteEnable=RED|GREEN|BLUE;//warning, no alpha output!
        CullMode=NONE;
        AlphaTestEnable=FALSE;
        SeparateAlphaBlendEnable=FALSE;
        SRGBWriteEnable=FALSE;
    }    
#endif
}