//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// ENBSeries Fallout 4 hlsl DX11 format, sample file of bloom
// visit http://enbdev.com for updates
// Author: Boris Vorontsov
// It's works with hdr input and output
// Bloom texture is always forced to 1024*1024 resolution
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

//+++++++++++++++++++++++++++++
//internal parameters, modify or add new
//+++++++++++++++++++++++++++++
/*
//example parameters with annotations for in-game editor
float ExampleScalar
<
  string UIName="Example scalar";
  string UIWidget="spinner";
  float UIMin=0.0;
  float UIMax=1000.0;
> = {1.0};

float3  ExampleColor
<
  string UIName = "Example color";
  string UIWidget = "color";
> = {0.0, 1.0, 0.0};

float4  ExampleVector
<
  string UIName="Example vector";
  string UIWidget="vector";
> = {0.0, 1.0, 0.0, 0.0};

int ExampleQuality
<
  string UIName="Example quality";
  string UIWidget="quality";
  int UIMin=0;
  int UIMax=3;
> = {1};

Texture2D ExampleTexture
<
  string UIName = "Example texture";
  string ResourceName = "test.bmp";
>;
SamplerState ExampleSampler
{
  Filter = MIN_MAG_MIP_LINEAR;
  AddressU = Clamp;
  AddressV = Clamp;
};
*/




//+++++++++++++++++++++++++++++
//external enb parameters, do not modify
//+++++++++++++++++++++++++++++
//x = generic timer in range 0..1, period of 16777216 ms (4.6 hours), y = average fps, w = frame time elapsed (in seconds)
float4  Timer;
//x = Width, y = 1/Width, z = aspect, w = 1/aspect, aspect is Width/Height
float4  ScreenSize;

//+++++++++++++++++++++++++++++
//external enb debugging parameters for shader programmers, do not modify
//+++++++++++++++++++++++++++++
//keyboard controlled temporary variables. Press and hold key 1,2,3...8 together with PageUp or PageDown to modify. By default all set to 1.0
float4  tempF1; //0,1,2,3
float4  tempF2; //5,6,7,8
float4  tempF3; //9,0
// xy = cursor position in range 0..1 of screen;
// z = is shader editor window active;
// w = mouse buttons with values 0..7 as follows:
//    0 = none
//    1 = left
//    2 = right
//    3 = left+right
//    4 = middle
//    5 = left+middle
//    6 = right+middle
//    7 = left+right+middle (or rather cat is sitting on your mouse)
float4  tempInfo1;
// xy = cursor position of previous left mouse button click
// zw = cursor position of previous right mouse button click
float4  tempInfo2;



//+++++++++++++++++++++++++++++
//mod parameters, do not modify
//+++++++++++++++++++++++++++++
Texture2D     TextureDownsampled; //color R16B16G16A16 64 bit or R11G11B10 32 bit hdr format. 1024*1024 size
Texture2D     TextureColor; //color which is output of previous technique (except when drawed to temporary render target), R16B16G16A16 64 bit hdr format. 1024*1024 size

Texture2D     TextureOriginal; //color R16B16G16A16 64 bit or R11G11B10 32 bit hdr format, screen size. PLEASE AVOID USING IT BECAUSE OF ALIASING ARTIFACTS, UNLESS YOU FIX THEM
Texture2D     TextureDepth; //scene depth R32F 32 bit hdr format, screen size. PLEASE AVOID USING IT BECAUSE OF ALIASING ARTIFACTS, UNLESS YOU FIX THEM
Texture2D     TextureAperture; //this frame aperture 1*1 R32F hdr red channel only. computed in PS_Aperture of enbdepthoffield.fx

//temporary textures which can be set as render target for techniques via annotations like <string RenderTarget="RenderTargetRGBA32";>
Texture2D     RenderTarget1024; //R16B16G16A16F 64 bit hdr format, 1024*1024 size
Texture2D     RenderTarget512; //R16B16G16A16F 64 bit hdr format, 512*512 size
Texture2D     RenderTarget256; //R16B16G16A16F 64 bit hdr format, 256*256 size
Texture2D     RenderTarget128; //R16B16G16A16F 64 bit hdr format, 128*128 size
Texture2D     RenderTarget64; //R16B16G16A16F 64 bit hdr format, 64*64 size
Texture2D     RenderTarget32; //R16B16G16A16F 64 bit hdr format, 32*32 size
Texture2D     RenderTarget16; //R16B16G16A16F 64 bit hdr format, 16*16 size
Texture2D     RenderTargetRGBA32; //R8G8B8A8 32 bit ldr format, screen size
Texture2D     RenderTargetRGBA64F; //R16B16G16A16F 64 bit hdr format, screen size

SamplerState    Sampler0
{
  Filter = MIN_MAG_MIP_POINT;
  AddressU = Clamp;
  AddressV = Clamp;
};
SamplerState    Sampler1
{
  Filter = MIN_MAG_MIP_LINEAR;
  AddressU = Clamp;
  AddressV = Clamp;
};



//+++++++++++++++++++++++++++++
//
//+++++++++++++++++++++++++++++
struct VS_INPUT_POST
{
  float3 pos    : POSITION;
  float2 txcoord  : TEXCOORD0;
};
struct VS_OUTPUT_POST
{
  float4 pos    : SV_POSITION;
  float2 txcoord0 : TEXCOORD0;
};



//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
VS_OUTPUT_POST  VS_Quad(VS_INPUT_POST IN)
{
  VS_OUTPUT_POST  OUT;
  float4  pos;
  pos.xyz=IN.pos.xyz;
  pos.w=1.0;
  OUT.pos=pos;
  OUT.txcoord0.xy=IN.txcoord.xy;
  return OUT;
}

	// Define AvgLuma
	float4 AvgLuma(float3 inColor)
{
	return float4(dot(inColor, float3(0.2125f, 0.7154f, 0.0721f)),                 /// Perform a weighted average
                max(inColor.r, max(inColor.g, inColor.b)),                       /// Take the maximum value of the incoming value
                max(max(inColor.x, inColor.y), inColor.z),                       /// Compute the luminance component as per the HSL colour space
                sqrt((inColor.x*inColor.x*0.2125f)+(inColor.y*inColor.y*0.7154f)+(inColor.z*inColor.z*0.0721f)));
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Helper functions

float linearDepth(float d, float n, float f)
{
  return (2.0 * n)/(f + n - d * (f - n));
}


// SHADERS ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Techniques are drawn one after another and they use the result of
// the previous technique as input color to the next one.  The number
// of techniques is limited to 255.  If UIName is specified, then it
// is a base technique which may have extra techniques with indexing
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

// Fast gaussian bloom
#include "Include/Functions/enbbloom/GaussBlur2.h"
// Kawase Bloom
//#include "Include/Functions/enbbloom/KawaseBloom.h"
