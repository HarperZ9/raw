//----------------------------------------------------------------------------------------------//
//																								//
//				     enbeffectprepass.fx file by LonelyKitsuune aka Skratzer					//
//						 for Skyrim SE ENB (DirectX 11 Shader Model 5)							//
//																								//
//			   Copyright (c) 2018-2020 LonelyKitsuune / T.Thanner - CC BY-NC-ND 4.0				//
//																								//
//-------------------------------------------CREDITS--------------------------------------------//
//																								//
//								  Boris Vorontsov for ENBSeries									//
//																								//
//----------------------------------------------------------------------------------------------//
//								==================================								//
//								//     Silent Horizons ENB      //								//
//								//								//								//
//								//		by LonelyKitsuune		//								//
//								==================================								//
//----------------------------------------------------------------------------------------------//

//This file needs my helper and UI files to work!

//--------------------------------------------------------------------------------
//OPTIONS
//--------------------------------------------------------------------------------
//Enable additional debugging tools
#define ENABLE_TOOLS		0 //[0-1]


//----------------------------------------------------------------------------------------------//
//										Internal Parameters										//
//----------------------------------------------------------------------------------------------//

//Load weather indices from external file (for DSRS)
#include "Weather/_ShaderWeatherIndex.fxh"


//----------------------------------------------------------------------------------------------//

#include "UI/enbUI_Primer.fxh"

//Tools (Global)
#define SHADERGROUP 0
#include "UI/enbUI_PrePass.fxh"


//DSRS globals + DVR settings
#define SHADERGROUP 1
#define TODIE Sunrise
#define SWSN 7
#include "UI/enbUI_PrePass.fxh"

#define TODIE Day
#define SWSN 8
#include "UI/enbUI_PrePass.fxh"

#define TODIE Sunset
#define SWSN 9
#include "UI/enbUI_PrePass.fxh"

#undef SHADERGROUP
#undef NOTFIRSTTIME


//DSR settings
#define SHADERGROUP 2
#define TODIE Sunrise
#define SWSN 10
#include "UI/enbUI_PrePass.fxh"

#define TODIE Day
#define SWSN 11
#include "UI/enbUI_PrePass.fxh"

#define TODIE Sunset
#define SWSN 12
#include "UI/enbUI_PrePass.fxh"

UI_WHITESPACE(7)
UI_WHITESPACE(8)

#undef SHADERGROUP
#undef NOTFIRSTTIME


//SSS Customizer (DNI)
#define SHADERGROUP 3
#define TODIE Day
#include "UI/enbUI_PrePass.fxh"
UI_SPECIAL_WHITESPACE(13)

#define TODIE Night
#include "UI/enbUI_PrePass.fxh"
UI_SPECIAL_WHITESPACE(14)

#define TODIE Interior
#include "UI/enbUI_PrePass.fxh"

#undef SHADERGROUP
#undef NOTFIRSTTIME


//----------------------------------------------------------------------------------------------//
//						     External enb parameters, do not modify								//
//																								//
//----------------------------------------------------------------------------------------------//

float4 Timer;			// x = generic timer in range 0..1, period of 16777216 ms (4.6 hours), y = average fps,
						// z = frame number (cyclically wraps to 0 after 9999 frames passed), w = frame time elapsed (in seconds)
float4 ScreenSize;		// x = Width, y = 1/Width, z = aspect, w = 1/aspect, aspect is Width/Height
float  AdaptiveQuality;	// changes in range 0..1, 0 means full quality, 1 lowest dynamic quality (0.33, 0.66 are limits for quality levels)
float4 TimeOfDay1;		// x = dawn, y = sunrise, z = day, w = sunset. Interpolators range from 0..1
float4 TimeOfDay2;		// x = dusk, y = night. Interpolators range from 0..1
float  ENightDayFactor;	// changes in range 0..1, 0 means that night time, 1 - day time
float  EInteriorFactor;	// changes 0 or 1. 0 means that exterior, 1 - interior
float  FieldOfView;		// FOV in degrees
float4 Weather;			// x = current weather index, y = outgoing weather index, z = weather transition, w = time of the day in 24 standard hours.
						// Weather index is value from weather ini file, for example WEATHER002 means index==2, but index==0 means that weather not captured.
						// -> Needs ENB Helper skse64 plugin to work correctly!
float4 SunDirection;	// Vector of the current sunlight direction (screen space)
float4 SunColor;		// xyz = unmodified sun color, w = sun transparency/visibility


//----------------------------------------------------------------------------------------------//
//						       External enb debugging parameters for							//
//								 shader programmers, do not modify								//
//----------------------------------------------------------------------------------------------//

float4 tempF1; //0,1,2,3
float4 tempF2; //5,6,7,8
float4 tempF3; //9,0
//keyboard controlled temporary variables. Press and hold key 1,2,3...8 together with PageUp or PageDown to modify.
//By default all set to 1.0

float4 tempInfo1;
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

float4 tempInfo2;
// xy = cursor position of previous left mouse button click
// zw = cursor position of previous right mouse button click


//----------------------------------------------------------------------------------------------//
//						      Game and mod parameters, do not modify								//
//																								//
//----------------------------------------------------------------------------------------------//

Texture2D TextureOriginal;		// Color R16B16G16A16 64 bit hdr format
Texture2D TextureColor;			// Color which is output of previous technique (except when drawed to temporary render target), R16B16G16A16 64 bit hdr format
Texture2D TextureDepth;			// Scene depth R32F 32 bit hdr format
Texture2D TextureJitter;		// Blue noise
Texture2D TextureMask;			// Alpha channel is mask for skinned objects (less than 1) and amount of sss -> rgb = Albedo, a = SSS
Texture2D TextureNormal;		// Screen space normal map -> xyz = normal vector
Texture2D TextureSunMask;		// Cloud texture -> unofficial feature, could be removed in a future ENB version!

//temporary textures which can be set as render target for techniques via annotations like <string RenderTarget="RenderTargetRGBA32";>
Texture2D RenderTargetRGBA32;	//R8G8B8A8		32 bit ldr format
Texture2D RenderTargetRGBA64;	//R16B16G16A16	64 bit ldr format
Texture2D RenderTargetRGBA64F;	//R16B16G16A16F 64 bit hdr format
Texture2D RenderTargetR16F;		//R16F			16 bit hdr format with red channel only
Texture2D RenderTargetR32F;		//R32F			32 bit hdr format with red channel only
Texture2D RenderTargetRGB32F;	//R8G8B8F		32 bit hdr format without alpha

SamplerState Point_Sampler
{
	Filter = MIN_MAG_MIP_POINT;
	AddressU = Clamp;
	AddressV = Clamp;
};

SamplerState Linear_Sampler
{
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = Clamp;
	AddressV = Clamp;
};

SamplerComparisonState Linear_Sampler_Eql
{
	Filter = COMPARISON_MIN_MAG_MIP_LINEAR;
	AddressU = Clamp;
	AddressV = Clamp;
	ComparisonFunc = EQUAL;
};

SamplerState Linear_Sampler_SR
{
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = Border;
	AddressV = Border;
	BorderColor = float4(0,0,0,0);
};


//Helper with some useful macros, variables and functions
#include "Helper/enbHelper_Common.fxh"


//----------------------------------------------------------------------------------------------//
//										   Generic Structs										//
//																								//
//----------------------------------------------------------------------------------------------//

struct VertexShaderInput
{
   float3 pos     : POSITION;
   float2 txcoord : TEXCOORD0;
};

struct VertexShaderOutput
{
   float4 pos      : SV_POSITION;
   float2 texcoord : TEXCOORD0;
};


//----------------------------------------------------------------------------------------------//
//										      Addons											//
//																								//
//----------------------------------------------------------------------------------------------//

//Initialize prepass part of the stylization suite addon if installed
#define STYLE_WS1 9
#define STYLE_WS2 10
#define STYLE_WS3 11
#include "Addons/PrePass_StylizationSuite.fxh"


//Initialize particle field addon if installed
#define PARTICLE_WS1  12
#define PARTICLE_WS2  13
#define PARTICLE_WS3  14
#define PARTICLE_SWS1 17
#define PARTICLE_SWS2 18
#define PARTICLE_SWS3 19
#define PARTICLE_SWS4 20
#include "Addons/PrePass_ParticleField.fxh"


//Initialize photo studio addon if installed
#define PHOTO_WS1  15
#define PHOTO_WS2  16
#define PHOTO_WS3  17
#define PHOTO_SWS1 21
#define PHOTO_SWS2 22
#include "Addons/PrePass_PhotoStudio.fxh"


//Initialize snow cover addon if installed
#define SNOW_WS1  18
#define SNOW_WS2  19
#define SNOW_WS3  20
#define SNOW_SWS1 23
#define SNOW_SWS2 24
#include "Addons/PrePass_SnowCover.fxh"


#ifdef _STYLIZATION_SUITE_
	#define STYLE_LOADED 1
#else
	#define STYLE_LOADED 0
#endif

#ifdef _PARTICLE_FIELD_
	#define PARTICLE_LOADED 1
#else
	#define PARTICLE_LOADED 0
#endif

#ifdef _PHOTO_STUDIO_
	#define PHOTO_LOADED 1
#else
	#define PHOTO_LOADED 0
#endif

#ifdef _SNOW_COVER_
	#define SNOW_LOADED 1
#else
	#define SNOW_LOADED 0
#endif


//----------------------------------------------------------------------------------------------//
//										DUAL SUN RAY SYSTEM										//
//																								//
//----------------------------------------------------------------------------------------------//

//----------------------------------------------------------------------------------------------//
//Structs

struct VertexShaderOutputDSRSOcclusion
{
   float4 pos             : SV_POSITION;
   float2 texcoord        : TEXCOORD0;
   float2 VecToSun        : TEXCOORD1;
NI float  ClipDist        : SUNRAYS0;
NI float2 CloudDensity    : SUNRAYS1;
NI float  StaticCloudMask : SUNRAYS2;
};

struct VertexShaderOutputDSRSMain
{
   float4 pos        : SV_POSITION;
   float4 texcoord   : TEXCOORD0;
   float2 VecToSun   : SUNRAYS0;
NI float2 ClipDist   : SUNRAYS1;
   float  Overshoot  : SUNRAYS2;
   float2 CorrSunVec : SUNRAYS3;
NI float2 Falloff    : SUNRAYS4;
};

struct SeparationData
{
   float2 Sunrise;
   float2 Day;
   float2 Sunset;
#ifdef ENABLE_SUNRAY_WEATHERSEPARATION
   float2 Weather[8];
#endif
};


//----------------------------------------------------------------------------------------------//
//Global settings and constants

static const float SR_HorizonFadeMult  = 1.0;   //How slow the sun should fade when it reaches the horizon
static const float SR_RayCastingRadius = 800.0; //Only affects direct sun rays -> Higher == smaller radius


static const float2 SR_QualityPresets[4] = { 1.0,1.0, 0.5,1.0, 0.4,0.8, 0.25,0.5 };
static float2 SR_Scalings  = SR_QualityPresets[UIDSRS_Quality+1]; //[Occlusion, Rays]
static float  MaxPixelSize = max2(PixelSize) * 1.5;


//----------------------------------------------------------------------------------------------//
//Base amount of desaturation that all ray types should receive during certain weathers

float WeatherDesatAmount[8] =
{
	0.0, 0.0,   //Clear,  ClearA
	0.0, 0.0,   //Cloudy, CloudyA
	1.0, 1.0,   //Rainy,  Snowy
	1.0, 0.5	//Foggy,  Ash
};


//----------------------------------------------------------------------------------------------//
//Disables separation between weathers with and without aurora borealis
//if the multi weather system doesn't support it

#ifdef DISABLE_AURORA_WEATHERSEPARATION
	static float UIDSR_IntClearA  = UIDSR_IntClear;
	static float UIDVR_IntClearA  = UIDVR_IntClear;
	static float UIDSR_IntCloudyA = UIDSR_IntCloudy;
	static float UIDVR_IntCloudyA = UIDVR_IntCloudy;
	
	#define SR_CLEAR_A_WEATHERS_START  SR_CLEAR_WEATHERS_START
	#define SR_CLOUDY_A_WEATHERS_START SR_CLOUDY_WEATHERS_START
#endif


//----------------------------------------------------------------------------------------------//
//Project sundirection vector to screen uv coordinates and get sun visibility

float GetSunData(out float2 SunUV)
{
	static const float2 UVScale = { 0.47, ScreenSize.z * -0.47 };
	SunUV = (SunDirection.xy / SunDirection.z) * UVScale + 0.5;
	
	float SunVis = saturate(SunColor.w * SR_HorizonFadeMult);
	
	//Legacy horizon fade using only time of day
	/*static const float2 Fade = float2(0.2, 0.2) * 20.0; //[Sunrise, Sunset]
	float SunVis = (TimeOfDay1.y > TimeOfDay1.w) ? Fade.x : Fade.y;
		  SunVis = saturate(TimeOfDay1.z * 20.0 - SunVis) * UISR_Enable;*/
	
	return SunVis * (SunDirection.w > 0.0) * (1.0 - EInteriorFactor);
}


//----------------------------------------------------------------------------------------------//
//Separation functions with overloads

float WeatherToEffectStrength(float Outgoing, float Incoming, float WeatherTran, float Step)
{
	float2 Weather    = { Outgoing, Incoming };
	float2 Transition = { WeatherTran, 1.0 - WeatherTran };
	
	Transition = saturate(Transition - Step) * rcp(1.0 - Step);
	Transition = lerp(Weather.xy, Weather.yx, Transition);
	
	return (Incoming >= Outgoing) ? Transition.x : Transition.y;
}

SeparationData GetUIIntensity()
{
	SeparationData SD;
	
	SD.Sunrise = float2(Sunrise_UIDSR_Int, Sunrise_UIDVR_Int);
	SD.Day     = float2(Day_UIDSR_Int, Sunrise_UIDVR_Int);
	SD.Sunset  = float2(Sunset_UIDSR_Int, Sunrise_UIDVR_Int);
	
	#ifdef ENABLE_SUNRAY_WEATHERSEPARATION
		SD.Weather[0] = float2(UIDSR_IntClear,   UIDVR_IntClear);
		SD.Weather[1] = float2(UIDSR_IntClearA,  UIDVR_IntClearA);
		SD.Weather[2] = float2(UIDSR_IntCloudy,  UIDVR_IntCloudy);
		SD.Weather[3] = float2(UIDSR_IntCloudyA, UIDVR_IntCloudyA);
		SD.Weather[4] = float2(UIDSR_IntRainy,   UIDVR_IntRainy);
		SD.Weather[5] = float2(UIDSR_IntSnowy,   UIDVR_IntSnowy);
		SD.Weather[6] = float2(UIDSR_IntFoggy,   UIDVR_IntFoggy);
		SD.Weather[7] = float2(UIDSR_IntAsh,     UIDVR_IntAsh);
	#endif
	
	return SD;
}

float2 SR_Separation(SeparationData SD, out float DesatWeather, float UIDesatAmount)
{
	float2 Value = TimeOfDay1.y * SD.Sunrise + TimeOfDay1.z * SD.Day + TimeOfDay1.w * SD.Sunset;
	
	#ifdef ENABLE_SUNRAY_WEATHERSEPARATION
		float2 WeatherValue, WeatherIndex = -1.0;
		WeatherIndex += Weather.xy >= SR_CLEAR_WEATHERS_START;
		WeatherIndex += Weather.xy >= SR_CLEAR_A_WEATHERS_START;
		WeatherIndex += Weather.xy >= SR_CLOUDY_WEATHERS_START;
		WeatherIndex += Weather.xy >= SR_CLOUDY_A_WEATHERS_START;
		WeatherIndex += Weather.xy >= SR_RAINY_WEATHERS_START;
		WeatherIndex += Weather.xy >= SR_SNOWY_WEATHERS_START;
		WeatherIndex += Weather.xy >= SR_FOGGY_WEATHERS_START;
		WeatherIndex += Weather.xy >= SR_ASH_WEATHERS_START;
		WeatherIndex -=(Weather.xy >= SR_WEATHERS_END) * 8.0;
		
		uint2 uWeatherIndex = (uint2)WeatherIndex;
		
		WeatherValue = (WeatherIndex < 0.0) ? 0.0 : float2(SD.Weather[uWeatherIndex.x].x,
														   SD.Weather[uWeatherIndex.y].x);
		Value.x *= WeatherToEffectStrength(WeatherValue.y, WeatherValue.x, Weather.z, 0.0);
		
		WeatherValue = (WeatherIndex < 0.0) ? 0.0 : float2(SD.Weather[uWeatherIndex.x].y,
														   SD.Weather[uWeatherIndex.y].y);
		Value.y *= WeatherToEffectStrength(WeatherValue.y, WeatherValue.x, Weather.z, 0.0);
		
		
		DesatWeather = UIDesatAmount;
		if(UIDesatAmount > 0.0)
		{
			WeatherValue = (WeatherIndex < 0.0) ? 0.0 : float2(WeatherDesatAmount[uWeatherIndex.x],
															   WeatherDesatAmount[uWeatherIndex.y]);
			DesatWeather *= WeatherToEffectStrength(WeatherValue.y, WeatherValue.x, Weather.z, 0.0);
		}
	#else
		Value *= 0.5;
	#endif
	
	Value *= float2(UIDSR_Enable, UIDVR_Enable);
	
	return Value;
}

float2 SR_Separation(SeparationData SD)
{ float DiscardedVar; return SR_Separation(SD, DiscardedVar, 0.0); }


float SR_Separation(float Sunrise, float Day, float Sunset)
{ return dot(TimeOfDay1.yzw, float3(Sunrise, Day, Sunset)); }

float3 SR_Separation(float3 Sunrise, float3 Day, float3 Sunset)
{ return TimeOfDay1.y * Sunrise + TimeOfDay1.z * Day + TimeOfDay1.w * Sunset; }

#define SRSEPMACRO(x) SR_Separation(Sunrise_##x, Day_##x, Sunset_##x)


//-----------------------------------Sun Rays Occlusion Pass------------------------------------//

VertexShaderOutputDSRSOcclusion VS_DSRSOcclusion(VertexShaderInput IN)
{
	VertexShaderOutputDSRSOcclusion OUT;
	
	OUT.pos      = float4(IN.pos.xyz, 1.0);
	OUT.texcoord = IN.txcoord.xy;
	
	ScaleScreenQuad_Mult(OUT.pos.xy, SR_Scalings.y);
	
	float2 SunPos;
	float SunInt  = GetSunData(SunPos);
	OUT.VecToSun  = SunPos - OUT.texcoord;
	OUT.VecToSun *= float2(ScreenSize.z, 1.0);
	
	SunInt      *= max2(SR_Separation(GetUIIntensity()));
	OUT.ClipDist = (SunInt > 0.01) ? UIDSRS_RayLength : -1.0;
	
	static const float  SampleArea      = 2.0;
	static const float2 DoublePixelSize = 2.0 * PixelSize;
	static const float2 HalfPixelSize   = 0.5 * PixelSize;
	
	float4 CloudMaskAvg = 0.0;
	
	[unroll] for(float i=-SampleArea; i<=SampleArea; i++)
	[unroll] for(float j=-SampleArea; j<=SampleArea; j++)
	{
		float2 Coords  = SunPos + DoublePixelSize * float2(i,j);
			   Coords -= HalfPixelSize * clamp(float2(i,j), -1.0, 1.0);
		CloudMaskAvg  += TextureSunMask.SampleLevel(Linear_Sampler, Coords,0);
	}
	
	float ScreenEdgeFade = LinearStep(0.8, 1.1, max2(abs(SunPos * 2.0 - 1.0)));
	OUT.StaticCloudMask  = max4(CloudMaskAvg / pow(SampleArea * 2.0 + 1.0,2));
	OUT.StaticCloudMask  = max(OUT.StaticCloudMask, ScreenEdgeFade * ScreenEdgeFade);
	
	OUT.CloudDensity = rcp(float2(SRSEPMACRO(UIDSR_CloudDens), SRSEPMACRO(UIDVR_CloudDens)));
	
	return OUT;
}

float2 PS_DSRSOcclusion(VertexShaderOutputDSRSOcclusion IN) : SV_Target
{
	float SunDistance = length(IN.VecToSun);
	clip(IN.ClipDist - SunDistance);
	
	//Manual bilinear filtering with early depth linearization if needed
	float OcclusionDepth;
	float4 DepthSamples = TextureDepth.Gather(Linear_Sampler, IN.texcoord);
		 //DepthSamples = FastLinDepth(DepthSamples, 3000.0);
		   DepthSamples = DepthSamples == 1.0;
	
	float2 FracPos = frac(IN.texcoord * ScreenRes - 0.5);
	float2 InterpX = lerp(DepthSamples.wx, DepthSamples.zy, FracPos.x);
	OcclusionDepth = lerp(InterpX.x, InterpX.y, FracPos.y);
	
	//Create occlusion masks for both ray types
	float2 MaskGradients;
	MaskGradients    = SunDistance * SunDistance;
	MaskGradients.x *= MaskGradients.x * SR_RayCastingRadius;
	MaskGradients    = LinearStep(IN.ClipDist, 0.0, zerolim(MaskGradients));
	
	float2 CloudMasks = { IN.StaticCloudMask, max4(TextureSunMask.Sample(Linear_Sampler, IN.texcoord)) };
		   CloudMasks = saturate(1.0 - pow(CloudMasks, IN.CloudDensity));
	
	float2 OcclusionMasks  = { OcclusionDepth, (UIDVR_RayType == 2) ? OcclusionDepth : 1.0 };
		   OcclusionMasks *= CloudMasks * MaskGradients;
		   OcclusionMasks *= OcclusionMasks;// * saturate(dot(Color, N_LUM) + 0.5);
	
	//Better DVR screen edge fade
	OcclusionMasks.y *= saturate(10.0 - max2(abs(IN.texcoord * 2.0 - 1.0)) * 10.0);
	
	//Legacy cloud masking filter using only color data and time of day
	/*float3 Color = TextureOriginal.Sample(Linear_Sampler, IN.texcoord).rgb;
	float CloudMask, CloudColorMask;
	CloudMask = dot(Color, N_LUM);
	Color    /= deltalim(CloudMask);
	Color    *= OcclusionMask > 0.75;
	
	OcclusionMask *= LinearStep(IN.ClipDist, saturate(IN.ClipDist - 1.0), SunDistance);
	CloudMask      = saturate(CloudMask - 0.6) * OcclusionMask;
	CloudColorMask = saturate(1e+2 * pow(saturate(dot(Color.rb, 0.5) - Color.g),3));
	CloudColorMask = saturate(CloudColorMask + CloudMask * saturate(SunDistance * -6.667 + 1.0));
	CloudMask      = saturate(CloudMask * 4.0);
	OcclusionMask *= lerp(CloudMask, CloudColorMask, saturate(TimeOfDay1.z * 10.0 - 4.5));
	OcclusionMask  = min(OcclusionMask, 0.7);*/
	
	return OcclusionMasks;
}


//--------------------------------------Main Sun Rays Pass--------------------------------------//

VertexShaderOutputDSRSMain VS_DSRSMain(VertexShaderInput IN)
{
	VertexShaderOutputDSRSMain OUT;
	
	OUT.pos         = float4(IN.pos.xyz, 1.0);
	OUT.texcoord.xy = IN.txcoord.xy * SR_Scalings.y;
	OUT.texcoord.zw = IN.txcoord.xy;
	
	ScaleScreenQuad_Mult(OUT.pos.xy, SR_Scalings.x);
	
	float2 SunPos;
	float SunInt   = GetSunData(SunPos);
	OUT.VecToSun   = SunPos * SR_Scalings.y - OUT.texcoord.xy;
	OUT.CorrSunVec = OUT.VecToSun * float2(ScreenSize.z, 1.0);
	
	float4 Overshoot = { saturate(abs(OUT.texcoord.xy + OUT.VecToSun) - 1.0),
						 saturate(  -(OUT.texcoord.xy + OUT.VecToSun)) };
	
	OUT.Overshoot = floor(zerolim(max4(Overshoot / MaxPixelSize) - 0.5));
	
	SunInt        *= max2(SR_Separation(GetUIIntensity()));
	OUT.ClipDist.x = UIDSRS_RayLength * SR_Scalings.y;
	OUT.ClipDist.x = (SunInt > 0.01) ? OUT.ClipDist.x : -1.0;
	
	OUT.ClipDist.y = rcp(OUT.ClipDist.x * 0.5 - OUT.ClipDist.x);
	
	//Can't be passed directly into the pixel shader
	//Causes absolute chaos otherwise for some odd reason
	OUT.Falloff = float2(UIDSR_Falloff, UIDVR_Falloff);
	
	return OUT;
}

float2 PS_DSRSMain(VertexShaderOutputDSRSMain IN) : SV_Target
{
	float SunDistance = length(IN.CorrSunVec);
	clip(IN.ClipDist.x - SunDistance);
	
	float  StepCount = ceil(SunDistance / MaxPixelSize);
	float2 StepSize  = IN.VecToSun / StepCount;
		   StepCount = zerolim(StepCount - IN.Overshoot);
	
	float2 RaySum    = 0.0;
	float2 WeightSum = DELTA;
	
	[loop] for(float i=0.0; i < StepCount; i++)
	{
		float2 CurrOffset = IN.texcoord + StepSize * i;
		float3 CurrSample = RenderTargetRGBA64.SampleLevel(Linear_Sampler_SR, CurrOffset,0).xyz;
		float2 Weight     = StepCount - i * IN.Falloff;
			   RaySum	 += CurrSample.xy * Weight;
			   WeightSum += Weight;
	}
	
	RaySum /= WeightSum;
	RaySum *= saturate((SunDistance - IN.ClipDist.x) * IN.ClipDist.y);
	
	return RaySum;
}



//----------------------------------------------------------------------------------------------//
//				   SUBSURFACE SCATTERING CUSTOMIZER AND DSRS UPSCALE + POSTPASS					//
//																								//
//----------------------------------------------------------------------------------------------//

//----------------------------------------------------------------------------------------------//
//Structs

struct VertexShaderOutput_DSRSPost_SSSC
{
   float4 pos               : SV_POSITION;
   float4 texcoord          : TEXCOORD0;
NI float  UISkin_Exposure   : PS0;
NI float  UISkin_Saturation : PS1;
NI float  UISkin_Contrast   : PS2;
NI float  UISkin_OutWhite   : PS3;
NI float  UISkin_OutBlack   : PS4;
NI float  UISkin_SDILum     : PS5;
NI float3 UISkin_Tint       : PS6;
   float2 VecToSun          : PS7;
NI bool   SampleRays        : PS8;
NI float2 RayIntensity      : PS9;
NI float3 RayColor[2]       : PS10;
};


//----------------------------------------------------------------------------------------------//
//Shaders

VertexShaderOutput_DSRSPost_SSSC VS_DSRSPost_SSSC(VertexShaderInput IN)
{
	VertexShaderOutput_DSRSPost_SSSC OUT;
	OUT.pos				= float4(IN.pos.xyz, 1.0);
	OUT.texcoord.xy = IN.txcoord.xy;
	OUT.texcoord.zw = IN.txcoord.xy * SR_Scalings.x;
	
	float2 SunPos;
	OUT.RayIntensity = GetSunData(SunPos);
	OUT.VecToSun     = SunPos - OUT.texcoord.xy;
	OUT.VecToSun    *= float2(ScreenSize.z, 1.0);
	
	float DesatAmount;
	OUT.RayIntensity *= SR_Separation(GetUIIntensity(), DesatAmount, UIDSRS_Desat);
	OUT.SampleRays    = max2(OUT.RayIntensity) > 0.01;
	
	OUT.RayColor[0] = ColorToChroma(SRSEPMACRO(UIDSR_Color)) * OUT.RayIntensity.x;
	OUT.RayColor[0] = lerp(OUT.RayColor[0], OUT.RayIntensity.x, DesatAmount);
	
	OUT.RayColor[1] = ColorToChroma(SRSEPMACRO(UIDVR_Color)) * OUT.RayIntensity.y;
	OUT.RayColor[1] = lerp(OUT.RayColor[1], OUT.RayIntensity.y, DesatAmount);
	
	OUT.UISkin_Exposure   = DNI_SEPARATION(UISkin_Exposure);
	OUT.UISkin_Saturation = DNI_SEPARATION(UISkin_Saturation);
	OUT.UISkin_Contrast   = DNI_SEPARATION(UISkin_Contrast);
	OUT.UISkin_OutWhite   = DNI_SEPARATION(UISkin_OutWhite);
	OUT.UISkin_OutBlack   = DNI_SEPARATION(UISkin_OutBlack);
	OUT.UISkin_SDILum     = DNI_SEPARATION(UISkin_SDILum);
	OUT.UISkin_Tint       = DNI_SEPARATION(UISkin_Tint);
	OUT.UISkin_Tint       = ColorToChroma(OUT.UISkin_Tint);
	
	return OUT;
}


float4 PS_DSRSPost_SSSC(VertexShaderOutput_DSRSPost_SSSC IN) : SV_Target
{
	float3 Color = TextureOriginal.Sample(Point_Sampler, IN.texcoord.xy).rgb;
	float4 Mask  =     TextureMask.Sample(Point_Sampler, IN.texcoord.xy);
	float  Depth =    TextureDepth.Sample(Point_Sampler, IN.texcoord.xy).x;
	
	Depth = FastLinDepth(Depth, 3000.0);
	
	//--------------------------------- DSRS Postpass and Upscale ----------------------------------//
	
	#if ENABLE_TOOLS
		if(UI_Visualize_Occlu)
			return RenderTargetRGBA64.Sample(Linear_Sampler, IN.texcoord.xy * SR_Scalings.y);
		if(UI_Visualize_Rays)
		{
			Color  = 0.0;
			Mask.w = 1.0;
		}
	#endif
	
	[branch] if(IN.SampleRays)
	{
		float2 Rays;
		
		[branch] if(UIDSRS_Quality > 0)
			Rays = BicubicFilter(TextureColor, IN.texcoord.zw).xy;
		else
			Rays = TextureColor.Sample(Linear_Sampler, IN.texcoord.zw).xy;
		
		float SkyMask        = Depth == 1.0;
		float DepthOcclusion = (UIDVR_RayType == 1) ? SkyMask : saturate(Depth + SkyMask);
		float DepthFadeStart = min(UIDVR_DepthStart, UIDVR_DepthEnd);
		
		float DistVolRays;
		DistVolRays  = sqrt(Depth) - min(length(IN.VecToSun), 0.4);
		DistVolRays *= LinearStep(DepthFadeStart, UIDVR_DepthEnd, Depth) * 2.0;
		DistVolRays  = (UIDVR_RayType == 3) ? sqrt(DepthOcclusion) : DistVolRays;
		DistVolRays  = saturate(DistVolRays * DepthOcclusion) * Rays.y;
		
		float  RayLuma  = dot(float2(Rays.x, DistVolRays), IN.RayIntensity);
		float3 RayMix   = Rays.x * IN.RayColor[0] + DistVolRays * IN.RayColor[1];
		float  HDRScale = UIDSRS_HDRDark * saturate(2.0 - dot(Color, K_LUM) * 1.5);
		
		Color = Color * saturate(1.0 - RayLuma * HDRScale) + RayMix;
	}
	
	
	//------------------------------ Subsurface Scattering Customizer ------------------------------//
	
	float SkinMask; float3 SkinDirectIllu = 0.0;
	SkinMask  = pow(saturate(1.0 - Mask.w),4);
	SkinMask *= saturate(UISkin_Enable - Depth * 15.0);
	SkinMask  = SkinMask > 0.1;
	
	[branch] if(SkinMask)
	{
		Color = exp2(lerp(0.18, log2(Color * IN.UISkin_Exposure + DELTA), IN.UISkin_Contrast));
		Color = zerolim(Color - DELTA) * IN.UISkin_Tint;
		Color = zerolim(lerp(dot(Color, K_LUM), Color, IN.UISkin_Saturation));
		Color = mad(IN.UISkin_OutWhite - IN.UISkin_OutBlack, Color, IN.UISkin_OutBlack);
		
		SkinDirectIllu = zerolim(dot(Color, K_LUM) - dot(Mask.rgb, K_LUM)) * SkinMask * Mask.rgb;
		Color          = zerolim(Color - SkinDirectIllu + SkinDirectIllu * IN.UISkin_SDILum);
	}
	
	
	#if ENABLE_TOOLS
	if(UI_Visualize_Mask) return float4(SkinMask.xxx,   1.0);
	if(UI_Visualize_SDI)  return float4(SkinDirectIllu, 1.0);
	#endif //ENABLE_TOOLS
	
	return float4(Color, 1.0 - SkinMask);
}


//----------------------------------------------------------------------------------------------//
//										    Techniques											//
//																								//
//----------------------------------------------------------------------------------------------//
//								   Boris' comment on techniques:								//
//				Techniques are drawn one after another and they use the result of				//
//						the previous technique as input color to the next one.					//
//																								//
//						    The number of techniques is limited to 255.							//
//																								//
//				If UIName is specified, then it is a base technique which may have extra		//
//								     techniques with indexing									//
//----------------------------------------------------------------------------------------------//

TWOPASSTECH11 (KitsuunePrePass <string UIName="Pre Processing - Kitsuune"; string RenderTarget="RenderTargetRGBA64";>,
								 VS_Basic(),		 PS_Blank(),
								 VS_DSRSOcclusion(), PS_DSRSOcclusion())
TWOPASSTECH11 (KitsuunePrePass1, VS_Basic(),		 PS_Blank(),
								 VS_DSRSMain(),      PS_DSRSMain())
TECH11        (KitsuunePrePass2, VS_DSRSPost_SSSC(), PS_DSRSPost_SSSC())

//Include all addon techniques (auto generated file)
#include "Helper/PrePassAddonTechniques.fxh"

