#pragma once

#include "ReShadeUI.fxh"

uniform float4 ScanlineFilterParams < hidden = true; >;
uniform float4 VignettingColour < hidden = true; >;
uniform float DaylightTime < hidden = true; >;

#if defined(NVE_RAIN)
uniform float RainValue < hidden = true; >;
uniform float LightIntensity < hidden = true; > = 0.0f;
uniform float3 LightColour < hidden = true; > = float3(0.0f, 0.0f, 0.0f);
uniform bool InVehicle < hidden = true; > = false;


uniform float RainFadeInSpeed <
    ui_tooltip = "Time (in seconds) for rain effect to fade in.";
    ui_label = "Rain: Fade In Time";
    hidden = false;
> = 3.0f;

uniform float RainFadeOutSpeed <
    ui_tooltip = "Time (in seconds) for rain effect to fade out.";
    ui_label = "Rain: Fade Out Time";
    hidden = false;
> = 8.0f;

uniform float LightFadeSpeed < __UNIFORM_SLIDER_FLOAT1
    ui_label = "Lights: Fade Speed";
    ui_min = 0.0f; ui_max = 2.0f;
    hidden = false;
> = 0.085f;

uniform float lightInt < __UNIFORM_SLIDER_FLOAT1
    ui_label = "Lights: Maximum Intensity";
    ui_min = 0.0f; ui_max = 2.0f;
    hidden = false;
> = 2.0f;

uniform float LightIntensityMinDistance < __UNIFORM_SLIDER_FLOAT1
    ui_tooltip = "Distance at which the intensity reaches maximum.";
    ui_label = "Lights: Maximum Intensity Distance";
    ui_min = 0.0f; ui_max = 50.0f;
    hidden = false;
> = 1.97f;

uniform float LightIntensityMaxDistance < __UNIFORM_SLIDER_FLOAT1
    ui_tooltip = "Distance at which the light starts affecting raindrops.";
    ui_label = "Lights: Intensity Affect Distance";
    ui_min = 0.0f; ui_max = 100.0f;
    hidden = false;
> = 18.0f;

uniform float safezoneIn < __UNIFORM_SLIDER_FLOAT1
    ui_label = "Center Safezone";
    ui_min = 0.0f; ui_max = 1.0f;
    hidden = false;
> = 0.431f;

uniform float safezoneOut < __UNIFORM_SLIDER_FLOAT1
    ui_label = "Center Safezone 2";
    ui_min = 0.0f; ui_max = 1.0f;
    hidden = false;
> = 0.493f;

uniform float timer < source = "timer";>;

texture2D renderTarget
{
	Width = BUFFER_WIDTH;
	Height = BUFFER_HEIGHT;
	MipLevels = 1;
	Format = RGBA8;
};

sampler2D targetSampler
{
	Texture = renderTarget;

	AddressU = CLAMP;
	AddressV = CLAMP;
	AddressW = CLAMP;

	MagFilter = LINEAR;
	MinFilter = LINEAR;
	MipFilter = LINEAR;

	MinLOD = 0.0f;
	MaxLOD = 1000.0f;

	MipLODBias = 0.0f;

	SRGBTexture = false;
};

sampler2D colorSampler2
{
    Texture = ReShade::BackBufferTex;

    AddressU = MIRROR;
    AddressV = MIRROR;
    AddressW = MIRROR;

    MagFilter = LINEAR;
    MinFilter = LINEAR;
    MipFilter = LINEAR;

    MinLOD = 0.0f;
    MaxLOD = 1000.0f;

    MipLODBias = 0.0f;

    SRGBTexture = false;
};
#endif

#if defined(NVE_BLOOM)
uniform float MinAmount < 
    ui_tooltip = "Bloom will never go below this value";
    ui_label = "Bloom: Minimum amount";
    ui_min = 0.0f; ui_max = 1.0f;
    ui_type = "slider";
> = 0.350f;

uniform float threshold < 
    ui_tooltip = "Bloom threshold";
    ui_label = "Bloom: Threshold";
    ui_min = 0.0f; ui_max = 10.0f;
    ui_type = "slider";
> = 10.0f;

uniform float thresholdcurve < 
    ui_tooltip = "Bloom threshold curve";
    ui_label = "Bloom: Threshold Curve";
    ui_min = 0.0f; ui_max = 10.0f;
    ui_type = "slider";
> = 1.0f;

uniform float EFalloff < 
    ui_tooltip = "Falloff";
    ui_label = "Bloom: Falloff";
    ui_min = 0.0f; ui_max = 1.0f;
    ui_type = "slider";
> = 0.5f;

uniform int EFalloffType < __UNIFORM_SLIDER_INT1
    ui_tooltip = "Falloff type";
    ui_label = "Bloom: Falloff type";
    ui_min = 0; ui_max = 5;
> = 0;

uniform float EOctaveWeight1 < 
    ui_tooltip = "Octave weight 1";
    ui_label = "Bloom: Octave weight 1";
    ui_min = -0.3f; ui_max = 1.0f;
    ui_type = "slider";
> = 0.022f;

uniform float EOctaveWeight2 < 
    ui_tooltip = "Octave weight 2";
    ui_label = "Bloom: Octave weight 2";
    ui_min = -0.3f; ui_max = 1.0f;
    ui_type = "slider";
> = 0.11f;

uniform float EOctaveWeight3 < 
    ui_tooltip = "Octave weight 3";
    ui_label = "Bloom: Octave weight 3";
    ui_min = -0.3f; ui_max = 1.0f;
    ui_type = "slider";
> = 0.25f;

uniform float EOctaveWeight4 < 
    ui_tooltip = "Octave weight 4";
    ui_label = "Bloom: Octave weight 4";
    ui_min = -0.3f; ui_max = 1.0f;
    ui_type = "slider";
> = 0.44f;

uniform float EOctaveWeight5 < 
    ui_tooltip = "Octave weight 5";
    ui_label = "Bloom: Octave weight 5";
    ui_min = -0.3f; ui_max = 1.0f;
    ui_type = "slider";
> = 0.7f;

uniform float EOctaveWeight6 < 
    ui_tooltip = "Octave weight 6";
    ui_label = "Bloom: Octave weight 6";
    ui_min = -0.3f; ui_max = 1.0f;
    ui_type = "slider";
> = 1.0f;

uniform float BloomIntensity < 
    ui_tooltip = "Intensity";
    ui_label = "Bloom: Intensity";
    ui_min = 0.0f; ui_max = 10.0f;
    ui_type = "slider";
> = 0.3f;

uniform float3 Color0 < 
    ui_tooltip = "Color 0";
    ui_label = "Bloom: Color 0";
    ui_min = 0.0f; ui_max = 1.0f;
    ui_type = "color";
> = float3(1.0f,1.0f,1.0f);

uniform float3 Color1 < 
    ui_tooltip = "Color 1";
    ui_label = "Bloom: Color 1";
    ui_min = 0.0f; ui_max = 1.0f;
    ui_type = "color";
> = float3(1.0f,1.0f,1.0f);

uniform float3 Color2 < 
    ui_tooltip = "Color 2";
    ui_label = "Bloom: Color 2";
    ui_min = 0.0f; ui_max = 1.0f;
    ui_type = "color";
> = float3(1.0f,1.0f,1.0f);

uniform float3 Color3 < 
    ui_tooltip = "Color 3";
    ui_label = "Bloom: Color 3";
    ui_min = 0.0f; ui_max = 1.0f;
    ui_type = "color";
> = float3(1.0f,1.0f,1.0f);

uniform float3 Color4 < 
    ui_tooltip = "Color 4";
    ui_label = "Bloom: Color 4";
    ui_min = 0.0f; ui_max = 1.0f;
    ui_type = "color";
> = float3(1.0f,1.0f,1.0f);

uniform float3 Color5 < 
    ui_tooltip = "Color 5";
    ui_label = "Bloom: Color 5";
    ui_min = 0.0f; ui_max = 1.0f;
    ui_type = "color";
> = float3(1.0f,1.0f,1.0f);
#endif

texture2D RenderTarget16    {	Width = 16; Height = 16; Format = RGBA32F;};
texture2D RenderTarget32     {	Width = 32; Height = 32; Format = RGBA32F;};
texture2D RenderTarget64     {	Width = 64; Height = 64; Format = RGBA32F;};
texture2D RenderTarget128     {	Width = 128; Height = 128; Format = RGBA32F;};
texture2D RenderTarget256     {	Width = 256; Height = 256; Format = RGBA32F;};
texture2D RenderTarget256_2     {	Width = 256; Height = 256; Format = RGBA32F;};
texture2D RenderTarget512_2     {Width = BUFFER_WIDTH/4; Height = BUFFER_HEIGHT/4; Format = RGBA32F;};
texture2D RenderTarget512_3     {Width = BUFFER_WIDTH/4; Height = BUFFER_HEIGHT/4; Format = RGBA32F;};
texture2D RenderTarget512       {Width = BUFFER_WIDTH/4; Height = BUFFER_HEIGHT/4; Format = RGBA32F;};
texture2D RenderTarget1024       {	Width = 1024; Height = 1024; Format = RGBA32F;};
texture2D RenderTarget1024_2     {	Width = 1024; Height = 1024; Format = RGBA32F;};
texture2D RenderTarget1024_3       {	Width = 1024; Height = 1024; Format = RGBA32F;};
texture2D RenderTargetHalf       {	Width = BUFFER_WIDTH/2; Height = BUFFER_HEIGHT/2; Format = RGBA32F;};
texture2D RenderTargetFull      {	Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = RGBA32F;};
texture2D RenderTargetFull2      {	Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = RGBA32F;};
texture2D RenderTargetFullMask      {	Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = R8;};
texture2D RenderTargetFullMaskGrow      {	Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = R8;};
texture2D TextureDownsampled      {	Width = 1024; Height = 1024; Format = RGBA32F;};

sampler2D RTSampler16         { Texture = RenderTarget16; };
sampler2D RTSampler32         { Texture = RenderTarget32; };
sampler2D RTSampler64         { Texture = RenderTarget64; };
sampler2D RTSampler128        { Texture = RenderTarget128; };
sampler2D RTSampler256        { Texture = RenderTarget256; };
sampler2D RTSampler256_2       { Texture = RenderTarget256_2; };
sampler2D RTSampler512_2        { Texture = RenderTarget512_2; MagFilter = LINEAR; MinFilter = LINEAR; MipFilter = LINEAR; };
sampler2D RTSampler512_3        { Texture = RenderTarget512_3; MagFilter = LINEAR; MinFilter = LINEAR; MipFilter = LINEAR;  };
sampler2D RTSampler512          { Texture = RenderTarget512; MagFilter = LINEAR; MinFilter = LINEAR; MipFilter = LINEAR; };
sampler2D RTSampler1024          { Texture = RenderTarget1024; };
sampler2D RTSampler1024_2       { Texture = RenderTarget1024_2; };
sampler2D RTSampler1024_3          { Texture = RenderTarget1024_3; };
sampler2D RTSamplerHalf          { Texture = RenderTargetHalf; };
sampler2D RTSamplerFull         { Texture = RenderTargetFull; MagFilter = LINEAR; MinFilter = LINEAR; MipFilter = LINEAR; };
sampler2D RTSamplerFull2         { Texture = RenderTargetFull2; MagFilter = LINEAR; MinFilter = LINEAR; MipFilter = LINEAR; };
sampler2D RTSamplerFullMask         { Texture = RenderTargetFullMask;};
sampler2D RTSamplerFullMaskGrow         { Texture = RenderTargetFullMaskGrow;};
sampler2D TDSampler        { Texture = TextureDownsampled;};