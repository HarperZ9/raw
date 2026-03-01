//////////////////////////////////////////////////////////////////////
//                                                                  //
//    ██████╗ ███████╗ █████╗  ██████╗████████╗ ██████╗ ██████╗     //
//    ██╔══██╗██╔════╝██╔══██╗██╔════╝╚══██╔══╝██╔═══██╗██╔══██╗    //
//    ██████╔╝█████╗  ███████║██║        ██║   ██║   ██║██████╔╝    //
//    ██╔══██╗██╔══╝  ██╔══██║██║        ██║   ██║   ██║██╔══██╗    //
//    ██║  ██║███████╗██║  ██║╚██████╗   ██║   ╚██████╔╝██║  ██║    //
//    ╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝ ╚═════╝   ╚═╝    ╚═════╝ ╚═╝  ╚═╝    //
//                                                                  //
//                         A FALLOUT4 ENB                           //
//                                                                  //
///// MOD PAGE ///////////////////////////////////////////////////////
//                                                                  //
//    https://www.nexusmods.com/fallout4/mods/57321                 //
//                                                                  //
//////////////////////////////////////////////////////////////////////
//                                                                  //
//                          H E L P E R                             //
//                                                                  //
//////////////////////////////////////////////////////////////////////

///// EXTERNAL PARAMETERS ////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

float4  Timer;                      // x = generic timer in range 0..1, period of 16777216 ms (4.6 hours), y = average fps, w = frame time elapsed (in seconds)
float4  ScreenSize;                 // x = Width, y = 1/Width, z = Width/Height, w = Height/Width
float   AdaptiveQuality;            // changes in range 0..1, 0 means full quality, 1 lowest dynamic quality (0.33, 0.66 are limits for quality levels)
float4  Weather;                    // x = current weather index, y = outgoing weather index, z = weather transition, w = time of the day in 24 standart hours.
float4  TimeOfDay1;                 // x = dawn, y = sunrise, z = day, w = sunset. Interpolators range from 0..1
float4  TimeOfDay2;                 // x = dusk, y = night. Interpolators range from 0..1
float   ENightDayFactor;            // changes in range 0..1, 0 means that night time, 1 - day time
float   EInteriorFactor;            // changes 0 or 1. 0 means that exterior, 1 - interior
float4  SunDirection;               // Prepass exclusive. Refrence here: https://cdn.discordapp.com/attachments/335788870849265675/532859588203249666/unknown.png
float4  Params01[7];                // mod parameters
float4  ENBParams01;                // x - bloom amount, y - lens amount
float4  DofParameters;              // z = ApertureTime multiplied by time elapsed, w = FocusingTime multiplied by time elapsed
float4  LightParameters;            // xy = sun position on screen, w = visibility

float4  tempF1, tempF2, tempF3;     // 0,1,2,3,4,5,6,7,8,9
float4  tempInfo1;
float4  tempInfo2;                  // xy = cursor position of previous left click, zw = cursor position of previous right click

static const float2 PixelSize  = float2(ScreenSize.y, ScreenSize.y * ScreenSize.z); // As in Reshade
static const float2 Resolution = float2(ScreenSize.x, ScreenSize.x * ScreenSize.w); // Display Resolution

///// TEXTURES ///////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

Texture2D   TextureOriginal;        // color R16B16G16A16 64 bit or R11G11B10 32 bit hdr format, screen size. PLEASE AVOID USING IT BECAUSE OF ALIASING ARTIFACTS, UNLESS YOU FIX THEM
Texture2D   TextureColor;           // color which is output of previous technique (except when drawed to temporary render target), R16B16G16A16 64 bit hdr format. screen size
Texture2D	TextureNormal;			// normal map
Texture2D   TextureDownsampled;     // color R16B16G16A16 64 bit or R11G11B10 32 bit hdr format. 1024*1024 size
Texture2D   TextureDepth;           // scene depth R32F 32 bit hdr format, screen size. PLEASE AVOID USING IT BECAUSE OF ALIASING ARTIFACTS, UNLESS YOU FIX THEM
Texture2D   TextureFocus;           // this frame focus 1*1 R32F hdr red channel only. computed in PS_Focus
Texture2D   TextureAperture;        // this frame aperture 1*1 R32F hdr red channel only. computed in PS_Aperture of enbdepthoffield.fx
Texture2D   TextureMask;            // mask of underwater area of screen
Texture2D   TextureBloom;           // vanilla or enb bloom
Texture2D   TextureLens;            // enb lens fx
Texture2D   TextureAdaptation;      // vanilla or enb adaptation
Texture2D   TextureCurrent;
Texture2D   TexturePrevious;
Texture2D   TexturePalette;         //enbpalette texture, if loaded and enabled in [colorcorrection].

///// RENDER TARGETS /////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

// temporary textures which can be set as render target for techniques via annotations like <string RenderTarget="RenderTargetRGBA32";>

Texture2D   RenderTarget1024;       // R16B16G16A16F 64 bit hdr format, 1024*1024 size
Texture2D   RenderTarget512;        // R16B16G16A16F 64 bit hdr format, 512*512 size
Texture2D   RenderTarget256;        // R16B16G16A16F 64 bit hdr format, 256*256 size
Texture2D   RenderTarget128;        // R16B16G16A16F 64 bit hdr format, 128*128 size
Texture2D   RenderTarget64;         // R16B16G16A16F 64 bit hdr format, 64*64 size
Texture2D   RenderTarget32;         // R16B16G16A16F 64 bit hdr format, 32*32 size
Texture2D   RenderTarget16;         // R16B16G16A16F 64 bit hdr format, 16*16 size
Texture2D   RenderTargetRGBA32;     // R8G8B8A8 32 bit ldr format
Texture2D   RenderTargetRGBA64;     // R16B16G16A16 64 bit ldr format
Texture2D   RenderTargetRGBA64F;    // R16B16G16A16F 64 bit hdr format
Texture2D   RenderTargetR16F;       // R16F 16 bit hdr format with red channel only
Texture2D   RenderTargetR32F;       // R32F 32 bit hdr format with red channel only
Texture2D   RenderTargetRGB32F;     // 32 bit hdr format without alpha

///// SAMPLERS ///////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

SamplerState   PointSampler         // Sampler0
{
    Filter   = MIN_MAG_MIP_POINT;
    AddressU = Clamp;
    AddressV = Clamp;
};

SamplerState   LinearSampler        // Sampler1
{
    Filter   = MIN_MAG_MIP_LINEAR;
    AddressU = Clamp;
    AddressV = Clamp;
};

SamplerState   WrapSampler
{
    Filter   = MIN_MAG_MIP_LINEAR;
    AddressU = Wrap;
    AddressV = Wrap;
};

SamplerState   MirrorSampler
{
    Filter   = MIN_MAG_MIP_LINEAR;
    AddressU = Mirror;
    AddressV = Mirror;
};

SamplerState   BorderSampler
{
    Filter   = MIN_MAG_MIP_LINEAR;
    AddressU = Border;
    AddressV = Border;
};

SamplerState   Sampler0 
{
    Filter   = MIN_MAG_MIP_POINT;
    AddressU = Clamp;   
    AddressV = Clamp;
};

SamplerState   Sampler1 
{
    Filter   = MIN_MAG_MIP_LINEAR;
    AddressU = Clamp;   
    AddressV = Clamp;
};

///// STRUCTURE //////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

struct VS_INPUT_POST
{
    float3 pos     : POSITION;
    float2 txcoord : TEXCOORD0;
};

struct VS_INPUT
{
    float3 pos     : POSITION;
    float2 txcoord : TEXCOORD0;
};

struct VS_OUTPUT_POST
{
    float4 pos     : SV_POSITION;
    float2 txcoord : TEXCOORD0;
};

struct VS_OUTPUT_DOF
{
    float4 pos          : SV_POSITION;
    float2 txcoord      : TEXCOORD0;
    float2 vertices[10] : TEXCOORD1;
};

struct VS_OUTPUT
{
    float4 pos     : SV_POSITION;
    float2 txcoord : TEXCOORD0;
};

///// VERTEX SHADER///////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

void VS_Quad(inout float4 pos : SV_POSITION, inout float2 txcoord0 : TEXCOORD0)
{
    pos.w     = 1.0;
    txcoord0 -= 7.0 / 256.0;
}

VS_OUTPUT_POST  VS_Draw(VS_INPUT_POST IN)
{
    VS_OUTPUT_POST OUT;
    OUT.pos = float4(IN.pos.xyz, 1.0);
    OUT.txcoord.xy = IN.txcoord.xy;
    return OUT;
}

VS_OUTPUT_POST  VS_PostProcess(VS_INPUT_POST IN)
{
    VS_OUTPUT_POST OUT;
    float4  pos;
    pos.xyz = IN.pos.xyz;
    pos.w = 1.0;
    OUT.pos = pos;
    OUT.txcoord.xy = IN.txcoord.xy;
    return OUT;
}