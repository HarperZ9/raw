//----------------------------------------------------------------------------------------------//
//										   MASTER LENS											//
//----------------------------------------------------------------------------------------------//
//																								//
//						enblens.fx file by LonelyKitsuune aka Skratzer							//
//						      for ENB (DirectX 11 Shader Model 5)								//
//																								//
//			   Copyright (c) 2018-2020 LonelyKitsuune / T.Thanner - CC BY-NC-ND 4.0				//
//																								//
//-------------------------------------------CREDITS--------------------------------------------//
//																								//
//				 Boris Vorontsov for ENBSeries and the initial lens reflection code				//
//						  Kingeric1992 for the initial ghost lensflare code						//
//																								//
//----------------------------------------------------------------------------------------------//
//								==================================								//
//								//     Silent Horizons ENB      //								//
//								//								//								//
//								//		by LonelyKitsuune		//								//
//								==================================								//
//----------------------------------------------------------------------------------------------//

/*¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯\
|		MASTER LENS FEATURES:																	|
|				Ghosts and Hoop/Ring lens flares												|
|				Lens Reflections																|
|				Anamorphic lens flare															|
|				Selectable lens dirt															|
|				Starburst lens flare															|
|				WeatherFX (Rain Droplets + Frost Vignette)										|
\______________________________________________________________________________________________*/

//This file needs my helper and UI files to work!


//--------------------------------------------------------------------------------
//Ghosts and Starburst use techniques as their disabling method to safe performance when they aren't needed.
//This allows them to have basically no performance impact when disabled (without pre-processor directives).
// -> Both are multipass shaders that would need a bunch of clip or branching ops to be disabled in realtime otherwise.


//--------------------------------------------------------------------------------
//Enables full local control over this shader file
#define LOCAL_OVERRIDE							0 //[0-1]


//--------------------------------------------------------------------------------
//GHOST LENS FLARE OPTIONS
//--------------------------------------------------------------------------------
//Enables additional ghosts specific chromatic aberration
#define GHOSTS_ENABLE_CHROMATICABERRATION		0 //[0-1]


//--------------------------------------------------------------------------------
//STARBURST LENS FLARE OPTIONS
//--------------------------------------------------------------------------------
//Use only the brightest burst when more than one intersect
#define STARBURST_USE_MAX_COLOR					0 //[0-1]-G


//--------------------------------------------------------------------------------
//ANAMORPHIC LENS FLARE OPTIONS
//--------------------------------------------------------------------------------
//Enable an additional mirrored anamorphic flare
#define ANAM_ENABLE_MIRRORED_FLARE				0 //[0-1]


//--------------------------------------------------------------------------------
//WEATHER EFFECT OPTIONS
//--------------------------------------------------------------------------------
//Enables individual per droplet light dispersion
#define RAINFX_ENABLE_DISPERSION				0 //[0-1]-G


//Switches to a single layer frost texture with approximated refractions
#define FROSTFX_USE_REFRACTION_METHOD			1 //[0-1]-G



//----------------------------------------------------------------------------------------------//
//										 Global Parameters										//
//----------------------------------------------------------------------------------------------//

#if !LOCAL_OVERRIDE
#undef RAINFX_ENABLE_DISPERSION
#undef STARBURST_USE_MAX_COLOR
#undef FROSTFX_USE_REFRACTION_METHOD
#undef RAINFX_ENABLE_FAKE_SKY_REFLECTION
#include "enbglobals.fxh"
#endif //LOCAL_OVERRIDE


//----------------------------------------------------------------------------------------------//
//										Internal Parameters										//
//----------------------------------------------------------------------------------------------//

#include "UI/enbUI_Primer.fxh"

//Weather index file no longer required - rain/frost/fog are purely UI-controlled

//Feature-set switches
#define SHADERGROUP 0
#include "UI/enbUI_Lens.fxh"
#undef SHADERGROUP
UI_WHITESPACE(11)
UI_WHITESPACE(12)

//Weather Effect (always available, no weather list required)
#define SHADERGROUP 1
#include "UI/enbUI_Lens.fxh"
#undef SHADERGROUP
UI_WHITESPACE(13)
UI_WHITESPACE(14)


//Anamorphic Lensflare (DNI)
#define SHADERGROUP 2
#define TODIE Day
#include "UI/enbUI_Lens.fxh"

#define TODIE Night
#include "UI/enbUI_Lens.fxh"

#define LASTTIME
#define TODIE Interior
#include "UI/enbUI_Lens.fxh"
UI_WHITESPACE(15)
UI_WHITESPACE(16)
#undef SHADERGROUP
#undef NOTFIRSTTIME


//Lens Reflections (DNI)
#define SHADERGROUP 3
#define TODIE Day
#include "UI/enbUI_Lens.fxh"

#define TODIE Night
#include "UI/enbUI_Lens.fxh"

#define LASTTIME
#define TODIE Interior
#include "UI/enbUI_Lens.fxh"
UI_WHITESPACE(17)
UI_WHITESPACE(18)
#undef SHADERGROUP
#undef NOTFIRSTTIME


//Lens Dirt (DNI)
#define SHADERGROUP 4
#define TODIE Day
#include "UI/enbUI_Lens.fxh"

#define TODIE Night
#include "UI/enbUI_Lens.fxh"

#define LASTTIME
#define TODIE Interior
#include "UI/enbUI_Lens.fxh"
UI_WHITESPACE(19)
UI_WHITESPACE(20)
#undef SHADERGROUP
#undef NOTFIRSTTIME


//Chromatic Aberration
#define SHADERGROUP 5
#include "UI/enbUI_Lens.fxh"
#undef SHADERGROUP
UI_WHITESPACE(21)
UI_WHITESPACE(22)


//Starburst (DNI)
#define SHADERGROUP 6
#define TODIE Day
#include "UI/enbUI_Lens.fxh"

#define TODIE Night
#include "UI/enbUI_Lens.fxh"

#define LASTTIME
#define TODIE Interior
#include "UI/enbUI_Lens.fxh"
UI_WHITESPACE(23)
UI_WHITESPACE(24)
#undef SHADERGROUP
#undef NOTFIRSTTIME


//Ghosts (DNI)
#define SHADERGROUP 7
#define TODIE Day
#include "UI/enbUI_Lens.fxh"

#define TODIE Night
#include "UI/enbUI_Lens.fxh"

#define LASTTIME
#define TODIE Interior
#include "UI/enbUI_Lens.fxh"
#undef SHADERGROUP
#undef NOTFIRSTTIME


//Film Grain (DNI)
#define SHADERGROUP 8
#define TODIE Day
#include "UI/enbUI_Lens.fxh"

#define TODIE Night
#include "UI/enbUI_Lens.fxh"

#define LASTTIME
#define TODIE Interior
#include "UI/enbUI_Lens.fxh"
#undef SHADERGROUP
#undef NOTFIRSTTIME


//Lens Optics (Distortion, Vignetting, Coma, Spherical, Field Curvature)
#define SHADERGROUP 9
#include "UI/enbUI_Lens.fxh"
#undef SHADERGROUP


//Halation and Veiling Glare (DNI)
#define SHADERGROUP 10
#define TODIE Day
#include "UI/enbUI_Lens.fxh"

#define TODIE Night
#include "UI/enbUI_Lens.fxh"

#define LASTTIME
#define TODIE Interior
#include "UI/enbUI_Lens.fxh"
#undef SHADERGROUP
#undef NOTFIRSTTIME


//Sensor Response
#define SHADERGROUP 11
#include "UI/enbUI_Lens.fxh"
#undef SHADERGROUP


//Atmospheric Fog - Global parameters
#define SHADERGROUP 12
#include "UI/enbUI_Fog.fxh"
#undef SHADERGROUP

//Atmospheric Fog - Day/Night/Interior appearance
#define SHADERGROUP 13
#define TODIE Day
#include "UI/enbUI_Fog.fxh"

#define TODIE Night
#include "UI/enbUI_Fog.fxh"

#define TODIE Interior
#include "UI/enbUI_Fog.fxh"
#undef SHADERGROUP


//CRT Display - All parameters (no DNI, display simulation is time-independent)
#define SHADERGROUP 14
#include "UI/enbUI_CRT.fxh"
#undef SHADERGROUP

//Cinematic FX Suite
#define SHADERGROUP 15
#include "UI/enbUI_CinematicFX.fxh"
#undef SHADERGROUP

#define SHADERGROUP 16
#include "UI/enbUI_CinematicFX.fxh"
#undef SHADERGROUP

#define SHADERGROUP 17
#include "UI/enbUI_CinematicFX.fxh"
#undef SHADERGROUP

#define SHADERGROUP 18
#include "UI/enbUI_CinematicFX.fxh"
#undef SHADERGROUP

#define SHADERGROUP 19
#include "UI/enbUI_CinematicFX.fxh"
#undef SHADERGROUP

#define SHADERGROUP 20
#include "UI/enbUI_CinematicFX.fxh"
#undef SHADERGROUP

#define SHADERGROUP 21
#include "UI/enbUI_CinematicFX.fxh"
#undef SHADERGROUP

#define SHADERGROUP 22
#include "UI/enbUI_CinematicFX.fxh"
#undef SHADERGROUP

//=============================================================================//
//  SKYRIMBRIDGE LENS UI PARAMETERS                                            //
//=============================================================================//

#define SHADERGROUP 99
#include "UI/enbUI_Lens.fxh"
#undef SHADERGROUP


//================================================================================
// PURPLE / GREEN FRINGING (Longitudinal Chromatic Aberration)
//================================================================================
// Real lenses focus different wavelengths at slightly different distances along
// the optical axis. At high-contrast edges, this creates purple halos on one side
// and green halos on the other — the signature of fast or poorly corrected lenses.
// Controlled separately from lateral CA (spectral dispersion) which is radial.

UI_WHITESPACE(25)
UI_WHITESPACE(26)

bool  UIPF_Enable     <string UIName="Purple Fringing - Enable"; > = {false};
float UIPF_Strength   <string UIName="|- PF - Strength";           float UIMin=0.0; float UIMax=2.0;  > = {0.5};
float UIPF_Threshold  <string UIName="|- PF - Edge Threshold";     float UIMin=0.0; float UIMax=1.0;  > = {0.15};
float UIPF_Spread     <string UIName="|- PF - Spread (pixels)";    float UIMin=1.0; float UIMax=8.0;  > = {3.0};
float UIPF_Bias       <string UIName="|- PF - Purple/Green Bias";  float UIMin=-1.0;float UIMax=1.0;  > = {0.3};
float UIPF_RadialFade <string UIName="|- PF - Radial Increase";    float UIMin=0.0; float UIMax=2.0;  > = {0.8};

UI_WHITESPACE(27)

//================================================================================
// LENS BREATHING
//================================================================================
// When a real lens changes focus distance, the focal length shifts slightly,
// causing a subtle FOV change. This "breathing" is noticeable in cinema lenses
// and adds an organic quality to focus transitions. We approximate it by
// slightly scaling the UV based on scene average brightness (proxy for
// aperture/focus state from the adaptation texture).

bool  UILB_Enable     <string UIName="Lens Breathing - Enable"; > = {false};
float UILB_Amount     <string UIName="|- Breathing - Amount";       float UIMin=0.0; float UIMax=0.05; > = {0.01};
float UILB_Speed      <string UIName="|- Breathing - Smoothing";    float UIMin=0.1; float UIMax=4.0;  > = {1.0};
float UILB_Center     <string UIName="|- Breathing - Neutral EV";   float UIMin=0.0; float UIMax=1.0;  > = {0.3};

UI_WHITESPACE(28)

//================================================================================
// ASTIGMATISM
//================================================================================
// Off-axis optical aberration where points are imaged as short lines or
// crosses instead of dots. The sagittal and tangential focal surfaces
// don't coincide, creating directional blur that rotates around the frame.
// Most visible at intermediate field angles (not center, not extreme edge).

bool  UIAST_Enable    <string UIName="Astigmatism - Enable"; > = {false};
float UIAST_Strength  <string UIName="|- Astigmatism - Strength";   float UIMin=0.0; float UIMax=3.0;  > = {0.5};
float UIAST_Onset     <string UIName="|- Astigmatism - Onset Radius"; float UIMin=0.1; float UIMax=1.0; > = {0.3};
float UIAST_Falloff   <string UIName="|- Astigmatism - Falloff";    float UIMin=1.0; float UIMax=4.0;  > = {2.0};
int   UIAST_Samples   <string UIName="|- Astigmatism - Samples";    int   UIMin=4;   int   UIMax=12;   > = {6};


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
//                         SkyrimBridge external data parameters                                //
//                                                                                              //
//   69 float4 game-state parameters pushed per-frame by SkyrimBridge SKSE plugin.              //
//   See SkyrimBridge.fxh for full documentation of each parameter.                             //
//----------------------------------------------------------------------------------------------//
#include "Helper/SkyrimBridge.fxh"

//----------------------------------------------------------------------------------------------//
//                      SkyrimBridge Lens Integration Helpers                                    //
//                                                                                              //
//   Weather-aware enables, precipitation scaling, wind direction, fog suppression.              //
//   All functions return neutral values when SB is inactive or not installed.                   //
//----------------------------------------------------------------------------------------------//
#ifdef SKYRIMBRIDGE_FXH

// Returns true if we're definitively indoors (more reliable than EInteriorFactor
// which has transition regions where weather FX flash on/off)
bool SB_LENS_IsInterior()
{
    [branch] if(!SB_IsActive() || UIBRIDGE_InteriorAccurate < 0.5)
        return (bool)EInteriorFactor;
    return SB_Interior_Flags.x > 0.5; // .x = isInterior
}

// Returns precipitation intensity factor [0,1] for scaling rain/snow density
// Falls back to 1.0 when SB unavailable
float SB_LENS_GetRainIntensity()
{
    [branch] if(!SB_IsActive() || UIBRIDGE_RainFromWeather < 0.5)
        return 1.0;
    return saturate(SB_Precipitation.y); // .y = intensity
}

// Returns frost enable based on weather coldness
bool SB_LENS_ShouldEnableFrost()
{
    [branch] if(!SB_IsActive() || UIBRIDGE_FrostFromSnow < 0.5)
        return false;
    return SB_Weather_Flags.w > 0.5; // .w = cold
}

// Returns fog-based flare/ghost suppression factor [0,1]
// 1.0 = no suppression, 0.0 = fully suppressed
float SB_LENS_GetFogSuppression()
{
    [branch] if(!SB_IsActive()) return 1.0;
    float FogDensity = SB_Fog_Density.y; // .y = density
    return saturate(1.0 - FogDensity * 0.7); // Dense fog suppresses up to 70%
}

// Returns wind direction for angled rain streaks (xy = direction, z = speed)
float3 SB_LENS_GetWind()
{
    [branch] if(!SB_IsActive() || UIBRIDGE_WindRain < 0.5)
        return float3(0.0, 0.0, 0.0);
    return float3(SB_Wind.xy, SB_Wind.z); // .xy = direction, .z = speed
}

// Returns lightning flash intensity for momentary flare bursts
float SB_LENS_GetLightningFlash()
{
    [branch] if(!SB_IsActive() || UIBRIDGE_LightningFlash < 0.5)
        return 0.0;
    return SB_Lightning.z * UIBRIDGE_LightningInt; // .z = intensity
}

// Returns damage screen FX intensities (fire, frost, shock)
float3 SB_LENS_GetDamageIntensity()
{
    [branch] if(!SB_IsActive() || UIBRIDGE_DamageFX < 0.5)
        return 0.0;
    return float3(SB_FX_Damage.x * UIBRIDGE_DmgFireInt,
                  SB_FX_Damage.y * UIBRIDGE_DmgFrostInt,
                  SB_FX_Damage.z * UIBRIDGE_DmgShockInt);
}

// Returns wet dirt multiplier for rainy lens dirt enhancement
float SB_LENS_GetWetDirtMult()
{
    [branch] if(!SB_IsActive() || UIBRIDGE_WetDirt < 0.5)
        return 1.0;
    float Wetness = saturate(SB_Precip_Surface.x); // .x = surface wetness
    return lerp(1.0, UIBRIDGE_WetDirtMult, Wetness);
}

#endif //SKYRIMBRIDGE_FXH


//----------------------------------------------------------------------------------------------//
//						     Game and mod parameters, do not modify								//
//																								//
//----------------------------------------------------------------------------------------------//

Texture2D TextureDownsampled;  //color R16B16G16A16 64 bit or R11G11B10 32 bit hdr format. 1024*1024 size
Texture2D TextureColor;        //color which is output of previous technique (except when drawed to temporary render target), R16B16G16A16 64 bit hdr format. screen size
Texture2D TextureOriginal;     //color R16B16G16A16 64 bit or R11G11B10 32 bit hdr format, screen size. PLEASE AVOID USING IT BECAUSE OF ALIASING ARTIFACTS, UNLESS YOU FIX THEM
Texture2D TextureDepth;        //scene depth R32F 32 bit hdr format, screen size. PLEASE AVOID USING IT BECAUSE OF ALIASING ARTIFACTS, UNLESS YOU FIX THEM
Texture2D TextureAperture;     //this frame aperture 1*1 R32F hdr red channel only. computed in PS_Aperture of enbdepthoffield.fx

//temporary textures which can be set as render target for techniques via annotations like <string RenderTarget="RenderTargetRGBA32";>
//RenderTarget1024 to RenderTarget16 are prefilled with enbbloom.fx output
Texture2D RenderTarget1024;    //R16B16G16A16F 64 bit hdr format, 1024*1024 size -- Bugged? Overwrites bloom output somehow
Texture2D RenderTarget512;     //R16B16G16A16F 64 bit hdr format, 512*512 size   -- Used by Reflection
Texture2D RenderTarget256;     //R16B16G16A16F 64 bit hdr format, 256*256 size   -- Used by AnamorphicFlare + Reflection
Texture2D RenderTarget128;     //R16B16G16A16F 64 bit hdr format, 128*128 size
Texture2D RenderTarget64;      //R16B16G16A16F 64 bit hdr format, 64*64 size
Texture2D RenderTarget32;      //R16B16G16A16F 64 bit hdr format, 32*32 size
Texture2D RenderTarget16;      //R16B16G16A16F 64 bit hdr format, 16*16 size
Texture2D RenderTargetRGBA32;  //R8G8B8A8      32 bit ldr format, screen size
Texture2D RenderTargetRGBA64F; //R16B16G16A16F 64 bit hdr format, screen size    -- Used by AnamorphicFlare, Ghosts and WeatherFX


//Atlas with 4 different lensdirt textures
Texture2D LensDirtAtlas <string ResourceName="Textures/LensDirtTexAtlas.png";>;

//Atlas with various lens raindrop normal maps
//Procedural replacement: RainDrops texture no longer needed
//Texture2D RainDrops <string ResourceName="Textures/LensRainDroplets.png";>;

//Procedural replacement: Frost textures no longer needed
//R=Main Frost - G=Background Frost - B=Single Crystals
//Texture2D FrostTex <string ResourceName="Textures/LensFrostTextures.png";>;

//Refraction frost texture
//R,G=precomputed sobel operator - B=Frost Mask
//Texture2D FrostTexRefrac <string ResourceName="Textures/LensFrostRefractionTexture.png";>;

//----------------------------------------------------------------------------------------------//

SamplerState Point_Sampler
{
	Filter = MIN_MAG_MIP_POINT;
	AddressU = Border;
	AddressV = Border;
};

SamplerState Linear_Sampler
{
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = Border;
	AddressV = Border;
};


SamplerState Point_Sampler_Rain
{
//Using mirroring can produce wrong refractions and reflections,
//but ensures that all droplets have enough data to sample
// -> workaround for screen space limitation
	Filter = MIN_MAG_MIP_POINT;
	AddressU = Mirror;
	AddressV = Mirror;
};

SamplerState Linear_Sampler_Rain
{
	Filter = MIN_MAG_MIP_POINT;
	AddressU = Mirror;
	AddressV = Mirror;
};


SamplerState Linear_Sampler_CA
{
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = Clamp;
	AddressV = Clamp;
};


//----------------------------------------------------------------------------------------------//

SamplerComparisonState Linear_Sampler_Grt
{
	Filter = COMPARISON_MIN_MAG_MIP_LINEAR;
	AddressU = Clamp;
	AddressV = Clamp;
	ComparisonFunc = Greater;
};

float MaskDaySky(float2 Coords)
{ return saturate(TextureDepth.SampleCmpLevelZero(Linear_Sampler_Grt,Coords,1.0).x + (1.0 - ENightDayFactor)); }


//----------------------------------------------------------------------------------------------//

//Raindrop blending
BlendState RainBlending
{
	BlendEnable[0] = TRUE;
	SrcBlend       = ONE;
	DestBlend      = ZERO;
	BlendOp        = ADD;
};


//----------------------------------------------------------------------------------------------//

//Helper with some useful macros, variables and functions
#include "Helper/enbHelper_Common.fxh"
static const float2 PixelSize = _HLP_PixelSize;
static const float2 ScreenRes = _HLP_ScreenRes;

//Procedural rain droplets + frost crystals (texture-free)
#include "Addons/Effect_ProceduralWeatherFX.fxh"

//Procedural lens dirt (texture-free alternative to atlas)
#include "Addons/Effect_ProceduralLensDirt.fxh"

//Constant for gaussian weight computation
static const float SqrtTwoPI = sqrt(2.0 * PI);


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
//								      ANAMORPHIC LENS FLARE										//
//																								//
//----------------------------------------------------------------------------------------------//

//----------------------------------------------------------------------------------------------//
//Structs

struct VertexShaderOutputAnamFlarePrePass
{
   float4 pos         : SV_POSITION;
   float2 texcoord    : TEXCOORD0;
NI float2 UIAF_Thresh : TEXCOORD1;
};

struct VertexShaderOutputAnamFlare
{
   float4 pos	       : SV_POSITION;
   float2 texcoord     : TEXCOORD0;
NI float  UIAF_Int     : ANAM0;
NI float  AnamWidth    : ANAM1;
NI float  WeightFactor : ANAM2;
NI float  LoopCount    : ANAM3;
NI float2 RotPixelSize : ANAM4;
};


//----------------------------------------------------------------------------------------------//
//Final rendertarget scalings (for RenderTarget256)

static const float2 AF_RTSize      = 256.0;
static const float2 AF_RTPixelSize = 1.0 / 256.0;
static       float2 AF_RTResMult   = rcp(ScreenRes * AF_RTPixelSize);


//----------------------------------Anamorphic Lensflare PrePass--------------------------------//

VertexShaderOutputAnamFlarePrePass VS_AnamFlarePrePass(VertexShaderInput IN)
{
	VertexShaderOutputAnamFlarePrePass OUT;
	OUT.pos      = float4(IN.pos.xyz, 1.0);
	OUT.texcoord = IN.txcoord.xy;
	
	ScaleScreenQuad_Res(OUT.pos.xy, AF_RTSize);
	
	OUT.UIAF_Thresh.x = DNI_SEPARATION(UIAF_Thresh);
	OUT.UIAF_Thresh.y = OUT.UIAF_Thresh.x + UIAF_CoreFoc;
	
	return OUT;
}

float4 PS_AnamFlarePrePass(VertexShaderOutputAnamFlarePrePass IN) : SV_Target
{
	if(!UIAF_Enable) discard;
	float3 Color = 0.0;
	
	static const float2 SourcePixelSize = 1.0 / 1024.0; //TextureDownsampled
	static const float2 Offset[4] =
	{ SourcePixelSize * float2( 1.0, 1.0),
	  SourcePixelSize * float2(-1.0, 1.0),
	  SourcePixelSize * float2( 1.0,-1.0),
	  SourcePixelSize * float2(-1.0,-1.0) };
	
	
	[branch] if (UI_IgnoreSky)
	[unroll] for(int i=0; i<4; i++)
	{
	float2 CurrOffset = IN.texcoord + Offset[i];
		   Color      = max(Color, MaskDaySky(CurrOffset) * TextureDownsampled.Sample(Linear_Sampler, CurrOffset).rgb);
	}
	else
	[unroll] for(int i=0; i<4; i++)
		Color = max(Color, TextureDownsampled.Sample(Linear_Sampler, IN.texcoord + Offset[i]).rgb);
	
	
	float Luma   = dot(Color, N_LUM);
		  Color /= deltalim(Luma);
	
	Luma  *= smoothstep(IN.UIAF_Thresh.x, IN.UIAF_Thresh.y, Luma);
	Luma  *= min(Luma, UIAF_CoreMax);
	Color *= pow(min(Luma, UIAF_CoreMax * 4.0), UIAF_CoreInt);
	
	return float4(Color, 1.0);
}

//-----------------------------------Anamorphic Lensflare Main----------------------------------//

VertexShaderOutputAnamFlare VS_AnamFlare(VertexShaderInput IN)
{
	VertexShaderOutputAnamFlare OUT;
	OUT.pos	     = float4(IN.pos.xyz, 1.0);
	OUT.texcoord = IN.txcoord.xy * AF_RTResMult;
	
	OUT.UIAF_Int	 = DNI_SEPARATION(UIAF_Int);
	OUT.RotPixelSize = GetDirVec(UIAF_Rotation) * PixelSize * float2(1.0, ScreenSize.z);
	
	OUT.AnamWidth    = UIAF_Width * 2.0;
	OUT.WeightFactor =  rcp(OUT.AnamWidth * SqrtTwoPI);
	OUT.LoopCount    = ceil(mad(OUT.AnamWidth, 2.0, 2.0));
	OUT.AnamWidth	 = -rcp(OUT.AnamWidth * OUT.AnamWidth);
	
	return OUT;
}

float4 PS_AnamFlare(VertexShaderOutputAnamFlare IN) : SV_Target
{
	//1D blur at low res to achieve bigger flares with lower performance impact
	//-> Aliasing artifacts aren't that noticable when the blurring
	//   axis is perfectly vertical/horizontal (0°/90°/180°)
	if(!UIAF_Enable) discard;
	
	float3 AnamFlare = IN.WeightFactor * RenderTargetRGBA64F.Sample(Linear_Sampler, IN.texcoord).rgb;
	float  WeightSum = IN.WeightFactor;
	
	#if ANAM_ENABLE_MIRRORED_FLARE
		AnamFlare += UIAF_MirrorInt * IN.WeightFactor *
		RenderTargetRGBA64F.Sample(Linear_Sampler, AF_RTResMult - IN.texcoord).rgb;
	#endif
	
	[loop] for(float i=1.0; i <= IN.LoopCount; i++)
	{
		float4 Offset      = IN.texcoord.xyxy + IN.RotPixelSize.xyxy * float4(i.xx, -i.xx);
		float  GaussWeight = IN.WeightFactor * exp(i*i*IN.AnamWidth);
		
		AnamFlare += GaussWeight * RenderTargetRGBA64F.SampleLevel(Linear_Sampler, Offset.xy, 0).rgb;
		AnamFlare += GaussWeight * RenderTargetRGBA64F.SampleLevel(Linear_Sampler, Offset.zw, 0).rgb;
		WeightSum += GaussWeight * 2.0;
		
		#if ANAM_ENABLE_MIRRORED_FLARE
			Offset       = AF_RTResMult.xyxy - Offset;
			GaussWeight *= UIAF_MirrorInt;
			AnamFlare   += GaussWeight * RenderTargetRGBA64F.SampleLevel(Linear_Sampler, Offset.xy, 0).rgb;
			AnamFlare   += GaussWeight * RenderTargetRGBA64F.SampleLevel(Linear_Sampler, Offset.zw, 0).rgb;
		#endif
	}
	
	AnamFlare /= WeightSum;
	AnamFlare *= IN.UIAF_Int;
	
	float AnamLuma  = dot(AnamFlare, K_LUM);
		  AnamFlare = zerolim(lerp(AnamLuma, AnamFlare, UIAF_Sat));
	
	return float4(AnamFlare, saturate(0.25 + (AnamLuma * 2.0 - 1.0) * 0.25));
}



//----------------------------------------------------------------------------------------------//
//								        GHOST LENS FLARES										//
//																								//
//		       Original - http://enbseries.enbdev.com/forum/viewtopic.php?f=7&t=5347			//
//----------------------------------------------------------------------------------------------//

//----------------------------------------------------------------------------------------------//
//Structs

struct VertexShaderInputPrepass
{
   float3 pos    : POSITION;
   float4 offset : TEXCOORD0;
};

struct VertexShaderOutputPrepass
{
   float4 pos          : SV_POSITION;
   float4 offset       : TEXCOORD0;
NI float  UIG_Power    : DNI0;
NI float  UIG_Strength : DNI1;
};

struct VertexShaderOutputGhostsMain
{
   float4 pos          : SV_POSITION;
   float2 texcoord     : TEXCOORD0;
NI float  Shape        : TEXCOORD1;
NI float2 vertices[10] : TEXCOORD2;
};


//----------------------------------------------------------------------------------------------//
//Global settings

#define FLARECOUNT 12
static const float Fmin             = 2.8; //maximum F-number to have circle shape
static const float Fmax             = 4.0; //minimum F-number to have solid polygon shape
static const float GaussSensitivity = 0.1; //controles the sensitivity of gaussian mask thats used to create ring flares
static const float PrefilterRange   = 4.0;


//----------------------------------------------------------------------------------------------//
//Input structs

struct FlareStruct
{
   float  index;
   float  curve;
   float  scale;
   float4 tint;
};

static FlareStruct FlareData[FLARECOUNT] =
{
	{ 1, Curve1.x, Scale1.x, Tint1 * Intensity1.x },
	{ 1, Curve1.y, Scale1.y, Tint1 * Intensity1.y },
	{ 1, Curve1.z, Scale1.z, Tint1 * Intensity1.z },
	{ 1, Curve1.w, Scale1.w, Tint1 * Intensity1.w },
	
	{ 2, Curve2.x, Scale2.x, Tint2 * Intensity2.x },
	{ 2, Curve2.y, Scale2.y, Tint2 * Intensity2.y },
	{ 2, Curve2.z, Scale2.z, Tint2 * Intensity2.z },
	{ 2, Curve2.w, Scale2.w, Tint2 * Intensity2.w },
	
	{ 3, Curve3.x, Scale3.x, Tint3 * Intensity3.x },
	{ 3, Curve3.y, Scale3.y, Tint3 * Intensity3.y },
	{ 3, Curve3.z, Scale3.z, Tint3 * Intensity3.z },
	{ 3, Curve3.w, Scale3.w, Tint3 * Intensity3.w }
};


//----------------------------------------------------------------------------------------------//
//Functions + static inputs

static float2 blurratio = ScreenSize.y * float2(min(UIG_Ratio, 1.0), ScreenSize.z / max(UIG_Ratio, 1.0) );
static float  blurshape = UIG_ForceDeform ? UIG_Deform : smoothstep(Fmin, Fmax, UIG_FNumber);

float2 Distort(float2 coord, float curve, float scale)
{
	float r = length(coord * ScreenSize.y / blurratio);
	return coord + pow(2.0 * r, curve) * (coord / r) * scale;
}

float4 FlareFunc(float2 coord, FlareStruct IN)
{
	static const float2 FlareUV[4] = { float2(0.25, 0.75), float2(0.75, 0.75), float2(0.75, 0.25), float2(0.25, 0.25) }; 
	
		   coord    = Distort(coord, IN.curve, IN.scale) + FlareUV[IN.index];
	float2 border   = 1.0 - saturate((coord <= FlareUV[IN.index] - 0.25) + (coord >= FlareUV[IN.index] + 0.25));
	float4 lens     = border.x * border.y * TextureColor.Sample(Linear_Sampler, coord); 
		   lens.rgb = saturate(lens.rgb - lens.a * 4.0 * (1.0 - IN.tint.a) / UIG_FNumber) * IN.tint.rgb;
		   lens.rgb = saturate(lens.rgb - Random(coord) * clamp(abs(IN.curve - 1.0), 0.2, 0.5) * UIG_Grain / UIG_FNumber);
	return lens;
}


//--------------------------------------- Ghosts Prepass ---------------------------------------//

VertexShaderOutputPrepass VS_GhostsPrePass(VertexShaderInputPrepass IN)
{
	VertexShaderOutputPrepass OUT;
	float2 steps    = ScreenSize.y * PrefilterRange;
		   steps.y *= ScreenSize.z;
	
	OUT.offset = (IN.offset.xyxy * 3.0 - 1.5) + float4(steps, -steps);
	OUT.pos    = float4(IN.pos.xy * 0.5 - float2(0.5, -0.5), IN.pos.z, 1.0);
	
	OUT.UIG_Power    = DNI_SEPARATION(UIG_Power);
	OUT.UIG_Strength = DNI_SEPARATION(UIG_Strength);
	
	return OUT;
}

float4 PS_GhostsPrePass(VertexShaderOutputPrepass IN) : SV_Target
{
	float4 color  = 0.0;
	float2 offset = (IN.offset.xy + IN.offset.zw) * 0.5;
	
	IN.offset += 0.5;
	float2 coord[4] = { IN.offset.xy, IN.offset.xw,
						IN.offset.zy, IN.offset.zw };
	
	#if MULTIIRIS_ENABLE_CHROMATICABERRATION
	static const float4 CA_Tint[3] = { float4(UIG_CAColor0, 1.0),
									   float4(UIG_CAColor1, 1.0),
									   float4(UIG_CAColor2, 1.0) };
	
	offset = Distort(offset, UIG_CACurve, UIG_CAScale * 0.01) - offset;
	
	[unroll] for (float i=0.0; i<3.0; i++)
	{
		float4 sum = 0.0;
		[unroll] for (int j=0; j<4; j++)
		{
			float2 curros = coord[j] + offset * i;
			float4 temp   = TextureColor.Sample(Linear_Sampler, curros);
			
			[branch] if(UI_IgnoreSky) temp *= MaskDaySky(curros);
			
			curros	 = abs(curros * 1.8 - 0.9);
			curros.x = saturate(max(curros.x, curros.y));
			temp	*= 1.0 - pow(curros.x,4);
			
			temp.a = max3(temp.rgb);
			sum   += temp / (temp.a + 1.0);
		}
		color += saturate(pow(sum * 0.25, IN.UIG_Power) * IN.UIG_Strength) * CA_Tint[i];
	}
	
	#else
	
	float2 distort = length(offset * ScreenSize.y / blurratio);
		   offset  = distort * (offset / distort) * 0.02;
	
	[unroll] for (int j=0; j<4; j++)
	{
		float2 curros = coord[j] + offset;
		float4 temp   = TextureColor.Sample(Linear_Sampler, curros);
		
		[branch] if(UI_IgnoreSky) temp *= MaskDaySky(curros);
		
		curros	 = abs(curros * 1.8 - 0.9);
		curros.x = saturate(max(curros.x, curros.y));
		temp	*= 1.0 - pow(curros.x,4); //screen border fade
		
		temp.a = max3(temp.rgb);
		color += temp / (temp.a + 1.0);
	}
	
	color *= 0.25;
	
	//better flare color
	float2 maxcol = { max3(color.rgb), color.a };
	
	color  = float4(color.rgb / maxcol.x, 1.0);
	color *= saturate(pow(maxcol.xxxy, IN.UIG_Power) * IN.UIG_Strength) *
			 float4(1.0 + pow(color.rgb,3), 1.0);
	#endif
	
	return zerolim(color * 0.33333);
}


//----------------------------------------- Ghosts Main ----------------------------------------//

VertexShaderOutputGhostsMain VS_GhostsMain(VertexShaderInput IN)
{
	VertexShaderOutputGhostsMain OUT;
	OUT.texcoord = IN.txcoord * 0.5;
	OUT.pos      = float4(IN.pos.xy * 0.5 - float2(0.5, -0.5), IN.pos.z, 1.0);
	OUT.Shape    = UIG_Shape * (1.0 + UIG_ShapeWeight) + 1.0;
	
	[unroll] for(int i=0; i<10; i++)
		OUT.vertices[i] = GetDirVec(UIG_AngleOffset + i * 360.0 / UIG_Shape);
	
	return OUT;
}

float4 PS_GhostsMain(VertexShaderOutputGhostsMain IN, uniform float range) : SV_Target
{
	float4 color = TextureColor.Sample(Linear_Sampler, IN.texcoord);
	float  avg   = color.a;
	
	[unroll] for(float i=1.0; i < 3.0; i++)
	[loop] for(int j=0; j < UIG_Shape; j++)
	{
		float2 offset = IN.texcoord + IN.vertices[j] * blurratio * range * i;
		if(any(offset >= 0.495)) break;
		float4 tmp    = TextureColor.SampleLevel(Linear_Sampler, offset, 0.0);
			   avg   += tmp.a;
			   color  = max(color, tmp);
	}
	
	[loop] for(int k=0; k < UIG_Shape; k++)
	{
		float2 offset = IN.vertices[k] + IN.vertices[k + 1];
			   offset = IN.texcoord + offset / lerp(length(offset), 2.0, blurshape) * blurratio * range * 2.0;
		if(any(offset >= 0.495)) break;
		float4 tmp    = TextureColor.SampleLevel(Linear_Sampler, offset, 0.0);
			   avg   += tmp.a;
			   color  = max(color, tmp);
	}
	
	return float4(color.rgb, lerp(avg / IN.Shape, color.a, GaussSensitivity));
}


//---------------------------------------- Ghosts Bypass ---------------------------------------//

VertexShaderOutput VS_GhostsBypass(VertexShaderInput IN, uniform float2 offset, uniform float2 scale)
{
	VertexShaderOutput OUT;
	OUT.texcoord = IN.txcoord * scale;
	OUT.pos	     = float4((IN.pos.xy - offset) * scale, IN.pos.z, 1.0);
	return OUT;
}

float4 PS_GhostsBypass(VertexShaderOutput IN) : SV_Target
{ return TextureColor.Sample(Linear_Sampler, IN.texcoord); }


//----------------------------------------- Ghosts Mix -----------------------------------------//

VertexShaderOutput VS_GhostsMix(VertexShaderInput IN)
{
	VertexShaderOutput OUT;
	OUT.texcoord = IN.txcoord * 0.5 - 0.25;
	OUT.pos      = float4(IN.pos.xy * 0.5 - float2(0.5, -0.5), IN.pos.z, 1.0);
	return OUT;
}

/* 16x | 8x
 ------------
    2x | 4x */

float4 PS_GhostsMix(VertexShaderOutput IN) : SV_Target
{
	float4 flare = 0.0;
	
	[unroll] for (int i=0; i<FLARECOUNT; i++)
		flare += FlareFunc(IN.texcoord, FlareData[i]);
	return flare;
}


//---------------------------------------- Ghosts Fullres --------------------------------------//

VertexShaderOutput VS_GhostsFullres(VertexShaderInput IN)
{
	VertexShaderOutput OUT;
	OUT.pos			= float4(IN.pos.xyz,1.0);
	OUT.texcoord    = IN.txcoord * 0.5;
	OUT.texcoord.x -= PixelSize.x; //fixes screen border bug
	return OUT;
}

float4 PS_GhostsFullres(VertexShaderOutput IN) : SV_Target
{ return TextureColor.Sample(Linear_Sampler, IN.texcoord); }




//----------------------------------------------------------------------------------------------//
//										LENS REFLECTIONS										//
//																								//
//----------------------------------------------------------------------------------------------//

//----------------------------------------------------------------------------------------------//
//Struct

struct VertexShaderOutputReflection
{
   float4 pos      : SV_POSITION;
   float2 texcoord : TEXCOORD0;
NI float2 Lens     : TEXCOORD1;
};


//----------------------------------------Lens Reflection----------------------------------------//

VertexShaderOutputReflection VS_Reflection(VertexShaderInput IN)
{
	VertexShaderOutputReflection OUT;
	OUT.pos			= float4(IN.pos.xyz,1.0);
	OUT.texcoord.xy = IN.txcoord.xy;
	
	OUT.Lens.x = DNI_SEPARATION(UIR_Strength) * 0.25;
	OUT.Lens.y = DNI_SEPARATION(UIR_Power);
	
	return OUT;
}

float4 PS_Reflection(VertexShaderOutputReflection IN) : SV_Target
{
	if(!UIR_Enable) discard;
	float3 LensRef = 0.0;
	
	//8 lens element reflections with physically-motivated parameters
	//  x = depth (axial position along optical axis)
	//  y = curvature power (surface curvature -> distortion amount)
	//  z = inverse size (magnification factor)
	//  w = coating thickness (drives thin-film chromatic shift)
	static const float4 RefOffset[8] = {
		float4( 1.60, 4.0, 1.0, 0.0),   //Element 1: strong barrel, large
		float4( 0.70, 0.25, 2.0, 0.3),   //Element 2: wide, small distortion
		float4( 0.30, 1.5, 0.5, 0.5),    //Element 3: subtle, medium
		float4(-0.50, 1.0, 1.0, 0.7),    //Element 4: inverted position
		float4( 1.20, 2.5, 1.5, 0.2),    //Element 5: moderate barrel
		float4(-0.25, 0.8, 0.8, 0.9),    //Element 6: slight inversion
		float4( 0.90, 3.0, 2.5, 0.4),    //Element 7: tight, high distortion
		float4(-0.80, 1.2, 1.8, 1.0),    //Element 8: deep inverted
	};
	
	//Per reflection color filter (first 4 from UI, additional 4 derived)
	float3 RefColor[8] = {
		UIR_ColorFilter1,
		UIR_ColorFilter2,
		UIR_ColorFilter3,
		UIR_ColorFilter4,
		//Derived: complementary tints for elements 5-8
		lerp(UIR_ColorFilter1, UIR_ColorFilter3, 0.5) * 0.7,
		lerp(UIR_ColorFilter2, UIR_ColorFilter4, 0.5) * 0.6,
		UIR_ColorFilter1.gbr * 0.5,
		UIR_ColorFilter2.brg * 0.4
	};
	
	[unroll] for(int i=0; i<8; i++)
	{
		float2 DistFact = IN.texcoord - 0.5;
		
		//Aspect-corrected radial distance
		float2 AspCorr = float2(DistFact.x * ScreenSize.z, DistFact.y);
		float  RadDist = length(AspCorr);
		
		//Curvature distortion with smooth power curve
		float  Distort = pow(2.0 * RadDist + DELTA, RefOffset[i].y);
		float2 Coords  = DistFact * Distort * RefOffset[i].x * RefOffset[i].z;
		       Coords  = 0.5 - Coords;
		
		//---- EDGE ARTIFACT FIX ----//
		//Smooth falloff near screen borders prevents hard cutoff artifacts
		float2 EdgeDist = Coords * (1.0 - Coords); //0 at edges, 0.25 at center
		float  EdgeMask = smoothstep(0.0, 0.04, min(EdgeDist.x, EdgeDist.y));
		
		//Additional vignette mask: reflections fade at extreme radii
		float2 VigFact = Coords * 2.0 - 1.0;
		float  VigMask = saturate(1.0 - dot(VigFact, VigFact) * 1.1);
		       VigMask = smoothstep(0.0, 0.15, VigMask);
		
		//Skip if completely outside valid sampling region
		if(EdgeMask < 0.001 || VigMask < 0.001) continue;
		
		//Clamp coordinates to valid range (prevents mirrored sampling artifacts)
		float2 SafeCoords = clamp(Coords, 0.002, 0.998);
		
		float3 TempLens = RenderTarget256.SampleLevel(Linear_Sampler, SafeCoords, 0);
		
		[branch] if(UI_IgnoreSky) TempLens *= MaskDaySky(SafeCoords);
		
		//Thin-film interference chromatic shift (anti-reflective coating)
		float CoatPhase = RefOffset[i].w * 2.0 * PI;
		float3 CoatShift = float3(
			0.85 + 0.15 * cos(CoatPhase),
			0.85 + 0.15 * cos(CoatPhase + TWO_PI * 0.333),
			0.85 + 0.15 * cos(CoatPhase + TWO_PI * 0.667)
		);
		TempLens *= RefColor[i] * CoatShift;
		
		//Apply all masking
		TempLens *= EdgeMask * VigMask;
		
		//Soft power curve for threshold/intensity control
		float TempNor  = max3(TempLens);
		      TempNor /= 1.0 + TempNor;
		      TempNor  = pow(TempNor, IN.Lens.y);
		
		LensRef += TempLens * TempNor;
	}
	
	return float4(LensRef * IN.Lens.x, 1.0);
}


//----------------------------------------------------------------------------------------------//
//								       STARBURST LENSFLARE										//
//																								//
//----------------------------------------------------------------------------------------------//

//----------------------------------------------------------------------------------------------//
//Structs

struct VertexShaderOutputSBPrePass
{
   float4 pos            : SV_POSITION;
   float2 texcoord       : TEXCOORD0;
NI float2 UISB_Thresh    : TEXCOORD1;
NI float  UISB_Strength  : SB0;
NI float3 UISB_Tint      : SB1;
NI float  SB_ThreshCurve : SB2;
NI float  SB_PowerLimit  : SB3;
};

struct VertexShaderOutputSBMain
{
   float4 pos             : SV_POSITION;
   float2 texcoord        : TEXCOORD0;
NI float2 LoopCount       : TEXCOORD1;
NI float  FlareSigma      : SB0;
NI float  WeightFactor    : SB1;
NI float2 RotPixelSize[9] : SB2;
};

struct VertexShaderOutputSBPostPass
{
   float4 pos      : SV_POSITION;
   float4 texcoord : TEXCOORD0;
};


//----------------------------------------------------------------------------------------------//
//Sacling factors for high quality and normal mode

static float4 Scalings = UISB_HQ ? float4(2.0, 0.5, 0.5 , 4.0):
								   float4(4.0, 1.0, 0.25, 2.0);


//---------------------------------------Starburst Prepass--------------------------------------//

VertexShaderOutputSBPrePass VS_StarburstPrePass(VertexShaderInput IN)
{
	VertexShaderOutputSBPrePass OUT;
	OUT.pos      = float4(IN.pos.xyz, 1.0);
	OUT.texcoord = IN.txcoord.xy;
	
	ScaleScreenQuad_Mult(OUT.pos.xy, Scalings.z);
	
	OUT.UISB_Thresh.x = DNI_SEPARATION(UISB_Thresh);
	OUT.UISB_Thresh.y = rcp(OUT.UISB_Thresh.x);
	OUT.UISB_Strength = DNI_SEPARATION(UISB_Strength);
	
	OUT.SB_ThreshCurve = UISB_ThreshCurve * 2.0 * OUT.UISB_Thresh.x;
	OUT.SB_PowerLimit  = pow(UISB_ThreshCurve,4) * OUT.UISB_Strength;
	OUT.UISB_Tint      = UISB_Tint.rgb / deltalim(dot(UISB_Tint.rgb, N_LUM));
	
	return OUT;
}

float4 PS_StarburstPrePass(VertexShaderOutputSBPrePass IN) : SV_Target
{
	static const float2 offset[4] =
	{ PixelSize.x,  PixelSize.y,  -PixelSize.x,  PixelSize.y,
	  PixelSize.x, -PixelSize.y,  -PixelSize.x, -PixelSize.y };
	
	float3 Color = 0.0;
	
	[branch] if(UI_IgnoreSky)
	[unroll] for(int i=0; i<4; i++)
	{
		float2 CurrOffset = IN.texcoord + offset[i];
		Color += MaskDaySky(CurrOffset) * TextureDownsampled.Sample(Linear_Sampler, CurrOffset).rgb;
	}
	else
	[unroll] for(int i=0; i<4; i++)
		Color += TextureDownsampled.SampleLevel(Linear_Sampler, IN.texcoord + offset[i],0).rgb;
	
	float Luma;
	Color *= 0.25;
	Luma   = dot(Color, N_LUM);
	Color /= deltalim(Luma);
	
	Luma   = pow(Luma * IN.UISB_Thresh.y, IN.SB_ThreshCurve);
	Luma   = min(Luma, IN.SB_PowerLimit);
	Luma   = (Luma * IN.UISB_Strength) / (Luma + IN.UISB_Thresh.x);
	Color  = lerp(Color, IN.UISB_Tint, UISB_Tint.a);
	Color  = lerp(Luma, Color * Luma, UISB_Saturation);
	Color *= 1.0 + UISB_Imperfections * (RandomGauss(IN.texcoord) - 0.5);
	
	return float4(zerolim(Color), 1.0);
}


//--------------------------------------Main Starburst Pass-------------------------------------//

VertexShaderOutputSBMain VS_StarburstMain(VertexShaderInput IN)
{
	VertexShaderOutputSBMain OUT;
	OUT.pos      = float4(IN.pos.xyz, 1.0);
	OUT.texcoord = IN.txcoord.xy * Scalings.z;
	
	ScaleScreenQuad_Mult(OUT.pos.xy, Scalings.z);
	
	OUT.FlareSigma   = UISB_Width * Scalings.w;
	OUT.WeightFactor =  rcp(OUT.FlareSigma * SqrtTwoPI);
	OUT.LoopCount.x  = ceil(OUT.FlareSigma * 2.0);
	OUT.FlareSigma	 = -rcp(OUT.FlareSigma * OUT.FlareSigma) * UISB_Falloff;
	
	OUT.LoopCount.y = ceil(UISB_Shape * saturate(UISB_Shape % 2.0 + 0.5));
	float2 ScaledPS = PixelSize * 1.25;
	
	[unroll] for(int i=0; i<9; i++)
	OUT.RotPixelSize[i] = GetDirVec(360.0 / UISB_Shape * (i + 0.5) + UISB_AngleOffset) * ScaledPS;
	
	return OUT;
}

float4 PS_StarburstMain(VertexShaderOutputSBMain IN) : SV_Target
{
	#if STARBURST_USE_MAX_COLOR
		#define ColSum Colors[j]
	#else
		#define ColSum Color
	#endif
	
	float3 Color     = IN.WeightFactor * TextureColor.Sample(Point_Sampler, IN.texcoord).rgb;
	float  WeightSum = IN.WeightFactor;
	
	#if STARBURST_USE_MAX_COLOR
		float3 Colors[9];
		[unroll] for(int k=0; k<9; k++) Colors[k] = 0.0;
	#endif
	
	[loop] for(float i=1.0; i <= IN.LoopCount.x; i++)
	{
	float GaussWeight = IN.WeightFactor * exp(i*i*IN.FlareSigma);

		[loop] for(int j=0; j < IN.LoopCount.y; j++)
		{
		float4 Offset     = IN.texcoord.xyxy + IN.RotPixelSize[j].xyxy * float4(i.xx, -i.xx);
			   ColSum    += GaussWeight * TextureColor.SampleLevel(Linear_Sampler, Offset.xy, 0).rgb;
			   ColSum    += GaussWeight * TextureColor.SampleLevel(Linear_Sampler, Offset.zw, 0).rgb;
			   WeightSum += GaussWeight * 2.0;
		}
	}
	
	#if STARBURST_USE_MAX_COLOR
			[unroll] for(k=0; k<9; k++) Colors[0] = max(Colors[0], Colors[k]);
			Color += Colors[0];
	#endif
	
	return float4(Color / WeightSum, 1.0); 
}

//--------------------------------------Starburst Postpass--------------------------------------//

VertexShaderOutputSBPostPass VS_StarburstPostPass(VertexShaderInput IN)
{
	VertexShaderOutputSBPostPass OUT;
	OUT.pos          = float4(IN.pos.xyz, 1.0);
	OUT.texcoord     = IN.txcoord.xyxy;
	OUT.texcoord.xy *= Scalings.z;
	return OUT;
}

float4 PS_StarburstPostPass(VertexShaderOutputSBPostPass IN, uniform bool SampleGhosts) : SV_Target
{
	float3 Color;
	
	[branch]
	if(!UISB_HQ)     Color  = BicubicFilter(TextureColor, IN.texcoord.xy, ScreenRes).rgb;
	else             Color  = TextureColor.Sample(Linear_Sampler, IN.texcoord.xy).rgb;
	if(SampleGhosts) Color += RenderTargetRGBA64F.Sample(Point_Sampler, IN.texcoord.zw).rgb;
	
	return float4(Color, 1.0);
}


//----------------------------------------------------------------------------------------------//
//								  LENS POSTPASS -> COMBINE + DIRT								//
//																								//
//----------------------------------------------------------------------------------------------//

//----------------------------------------------------------------------------------------------//
//Struct

struct VertexShaderOutputPostPass
{
   float4 pos            : SV_POSITION;
   float2 texcoord       : TEXCOORD0;
   float2 DirtCoords     : TEXCOORD1;
NI float  UID_Power      : DNI0;
NI float  UID_Strength   : DNI1;
NI bool   AFBlurSampling : DNI2;
};


//-----------------------------------------Lens Postpass----------------------------------------//

VertexShaderOutputPostPass VS_DirtAndPostPass(VertexShaderInput IN)
{
	VertexShaderOutputPostPass OUT;
	OUT.pos				= float4(IN.pos.xyz,1.0);
	OUT.texcoord.xy = IN.txcoord.xy;
	
	OUT.UID_Power      = DNI_SEPARATION(UID_Power);
	OUT.UID_Strength   = DNI_SEPARATION(UID_Strength);
	OUT.DirtCoords     = AtlasFetch_4(IN.txcoord, UID_Select);
	OUT.AFBlurSampling = fmod(UIAF_Rotation, 90.0) > 0.0;
	
	return OUT;
}


float4 PS_DirtAndPostPass(VertexShaderOutputPostPass IN, uniform bool SampleInput) : SV_Target
{
	float3 Color = 0.0;
	
	//--- SkyrimBridge: Fog-based flare/ghost suppression ---
	#ifdef SKYRIMBRIDGE_FXH
		float FogSuppress = SB_LENS_GetFogSuppression();
	#else
		float FogSuppress = 1.0;
	#endif
	
	if(SampleInput) Color  =    TextureColor.Sample(Point_Sampler,  IN.texcoord).rgb;
	if(UIR_Enable ) Color += RenderTarget512.Sample(Linear_Sampler, IN.texcoord).rgb * FogSuppress;
	if(UIAF_Enable)
	{
		float3 AnamCol = RenderTarget256.Sample(Linear_Sampler, IN.texcoord).rgb;
		
		[branch] if(IN.AFBlurSampling)
		{
			float  WeightSum = DELTA;
			float3 AnamBlur  = 0.0;
			
			static const float2 Offset[4] = { AF_RTPixelSize * float2( 1.0, 0.5),
											  AF_RTPixelSize * float2(-0.5, 1.0),
											  AF_RTPixelSize * float2(-1.0,-0.5),
											  AF_RTPixelSize * float2( 0.5,-1.0) };
			
			[unroll] for(int i=0; i<4; i++)
			{
				float4 CurrSample = RenderTarget256.Sample(Linear_Sampler, IN.texcoord + Offset[i]);
				
				AnamBlur  += CurrSample.a * CurrSample.rgb;
				WeightSum += CurrSample.a * 2.0;
			}
			AnamBlur /= WeightSum;
			AnamCol   = AnamBlur + AnamCol * 0.5;
		}
		Color += AnamCol * FogSuppress;
	}
	
	
	[branch] if(UID_Enable)
	{
		float3 DirtMask, BloomMask;
		float  DirtMax;
		
		//Use procedural dirt generation (texture-free, infinite resolution)
		float3 ProcDirt = ProceduralLensDirt(IN.texcoord);
		
		//Blend procedural with atlas texture for hybrid approach
		//  Procedural provides non-repeating detail, atlas provides artistic control
		float3 AtlasDirt = LensDirtAtlas.Sample(Linear_Sampler, IN.DirtCoords).rgb;
		
		//Combine: atlas shapes modulated by procedural detail
		DirtMask  = AtlasDirt * 0.6 + ProcDirt * 0.4;
		DirtMask += ProcDirt * AtlasDirt * 0.3; //Cross-modulation adds richness
		
		DirtMax   = max3(DirtMask);
		DirtMax	  = pow(DirtMax / (1.0 + DirtMax), IN.UID_Power);
		DirtMask *= DirtMax * IN.UID_Strength;
		BloomMask = RenderTarget128.Sample(Linear_Sampler, 1.0 - IN.texcoord).rgb * 0.1 +
					RenderTarget64 .Sample(Linear_Sampler,       IN.texcoord).rgb * 0.9;
		
		//--- SkyrimBridge: Rain wet lens dirt enhancement ---
		#ifdef SKYRIMBRIDGE_FXH
			DirtMask *= SB_LENS_GetWetDirtMult();
		#endif
		
		Color += DirtMask * BloomMask;
	}
	
	//--- SkyrimBridge: Lightning flash burst ---
	#ifdef SKYRIMBRIDGE_FXH
	{
		float LFlash = SB_LENS_GetLightningFlash();
		[branch] if(LFlash > 0.01)
			Color += float3(0.85, 0.88, 1.0) * LFlash;
	}
	#endif
	
	return float4(Color, 1.0);
}


//----------------------------------------------------------------------------------------------//
//										LENS WEATHER EFFECTS									//
//																								//
//----------------------------------------------------------------------------------------------//

//----------------------------------------------------------------------------------------------//
//Structs

struct VertexShaderOutputRain
{
   float4 pos        : SV_POSITION;
   float4 texcoord   : TEXCOORD0;
NI float  Fade       : PSINFO0;
NI bool   EnableRain : PSINFO1;
NI float  Rotation   : PSINFO2;
NI float4 SkyColor   : PSINFO3;
NI float4 EnvColor   : PSINFO4;
};

struct VertexShaderOutputRainBlur
{
   float4 pos          : SV_POSITION;
   float2 texcoord     : TEXCOORD0;
NI float  Sigma        : TEXCOORD1;
NI float  WeightFactor : TEXCOORD2;
NI float  LoopCount    : PSINFO0;
NI bool   EnableRain   : PSINFO1;
NI float  OffsetScale  : PSINFO2;
};

struct VertexShaderOutputFrost
{
   float4 pos          : SV_POSITION;
   float2 texcoord     : TEXCOORD0;
NI float2 Radii        : TEXCOORD1;
NI float3 Vignette     : PSINFO0;
NI bool   EnableFrost  : PSINFO1;
NI float3 FrostTint    : PSINFO2;
NI float  Sigma        : PSINFO3;
NI float  WeightFactor : PSINFO4;
NI float  LoopCount    : PSINFO5;
NI bool   EnableRain   : PSINFO6;
NI float  OffsetScale  : PSINFO7;
NI float4 SkyColor     : PSINFO8;
NI float4 EnvColor     : PSINFO9;
};


//----------------------------------------------------------------------------------------------//

//Size of the droplet texture atlas
#define RAINDROP_GRID_SIZE 4

//WeatherFX no longer depends on a weather list - effects are purely UI-controlled
//Rain and Frost enables come directly from enbUI_Lens.fxh SHADERGROUP 1


//----------------------------------------------------------------------------------------------//

void OOSSampleApprox(inout float3 SampleCol, float2 SampleCoords, float4 SkyColor, float4 EnvColor)
{
	float SkyColWeight  = saturate(SampleCoords.y * -2.0 * SkyColor.a);
	float EnvColWeight  = max2(abs(SampleCoords.x - 0.5) - 0.5);
	      EnvColWeight += saturate(SampleCoords.y - 1.0);
	      EnvColWeight  = saturate(EnvColWeight * 2.0 * EnvColor.a);
	
	SampleCol = lerp(SampleCol, SkyColor.rgb, SkyColWeight);
	SampleCol = lerp(SampleCol, EnvColor.rgb, EnvColWeight);
}

void OOSSampleApprox(inout float3 SampleCol, uint Index, float2 SampleCoords, float4 SkyColor, float4 EnvColor)
{
	float SkyColWeight  = saturate(SampleCoords.y * -2.0 * SkyColor.a);
	float EnvColWeight  = max2(abs(SampleCoords.x - 0.5) - 0.5);
	      EnvColWeight += saturate(SampleCoords.y - 1.0);
	      EnvColWeight  = saturate(EnvColWeight * 2.0 * EnvColor.a);
	
	SampleCol[Index] = lerp(SampleCol[Index], SkyColor[Index], SkyColWeight);
	SampleCol[Index] = lerp(SampleCol[Index], EnvColor[Index], EnvColWeight);
}


//--------------------------------------Rain Droplet Effect-------------------------------------//

VertexShaderOutputRain VS_Rain(VertexShaderInput IN, uniform float4 Seeds, uniform float TOffset)
{
	VertexShaderOutputRain OUT;
	OUT.pos        = float4(IN.pos.xy * PixelSize * 0.25, IN.pos.z, 1.0);
	OUT.texcoord   = IN.txcoord.xyxy;
	OUT.EnableRain = !EInteriorFactor && UIWFXRain_Enable;
	
	//--- SkyrimBridge: Accurate interior detection + precipitation intensity ---
	#ifdef SKYRIMBRIDGE_FXH
		OUT.EnableRain = !SB_LENS_IsInterior() && UIWFXRain_Enable;
	#endif
	
	[branch] if(OUT.EnableRain)
	{
		//Rain strength scaled by SB precipitation intensity
		float RainStrength = 1.0;
		#ifdef SKYRIMBRIDGE_FXH
			RainStrength = SB_LENS_GetRainIntensity();
			OUT.EnableRain = OUT.EnableRain && (RainStrength > 0.01);
		#endif
		
		float2 Time; //x = seconds, y = frac part between seconds
		Time.x = Timer.x * 16777.216 * UIWFXRain_Tickrate;
		Time.x = Time.x * ceil(RainStrength) + TOffset;
		Time.y = modf(Time.x, Time.x);
		
		float4 RanValue1 = RandomF4(Time.x * Seeds);
		float4 RanValue2 = RandomF4(RanValue1);
		
		float  RScale	 = MapToRange(RanValue1.x, UIWFXRain_MinSize, UIWFXRain_MaxSize);
			   RScale	*= 1.0 + Time.y * 0.25;
		float  RRotation = RanValue1.y * 360.0;
		float2 ROffset	 = RanValue1.zw * 2.0 - 1.0;
		float2 RRatio	 = 1.0 + (RanValue2.xw * 2.0 - 1.0) * UIWFXRain_MaxDeformation;
		
		float2 DripData = saturate(CatmullRom(Time.y, RanValue2.xz, 0.0, 1.0, RanValue2.yw));
		float  RDripping  = DripData.x;
		float  RDripDrift = DripData.y;
		
		RDripping  *= UIWFXRain_DripSpeed / UIWFXRain_Tickrate;
		RDripDrift *= UIWFXRain_DripDrift;
		RDripDrift  = step(RanValue2.x, 0.45) * RDripDrift * RanValue2.y -
					  step(0.75, RanValue2.z) * RDripDrift * RanValue2.w;
		
		//Quad Scale, Offset, Rotate
		OUT.pos.xy = IN.pos.xy * RScale * RRatio + ROffset;
		OUT.pos.xy = MatrixRotate(OUT.pos.xy, RRotation, false);
		
		OUT.Rotation = RRotation;
		
		//Make base quad squared and apply dripping motion
		OUT.pos.y  = OUT.pos.y * ScreenSize.z - RDripping;
		OUT.pos.x += RDripDrift;
		
		//--- SkyrimBridge: Wind-directed rain angle ---
		// Shifts rain droplet position based on wind direction for realistic angled rain
		#ifdef SKYRIMBRIDGE_FXH
		{
			float3 Wind = SB_LENS_GetWind();
			float  WindSpeed = Wind.z * 0.15; // Scale wind effect on droplet position
			OUT.pos.xy += Wind.xy * WindSpeed;
		}
		#endif
		
		//Additional info for ps computations
		float2 AtlasCoords = floor(RanValue2.xy * RAINDROP_GRID_SIZE);
		   OUT.texcoord.zw = (AtlasCoords + OUT.texcoord.zw) / RAINDROP_GRID_SIZE;
		
		OUT.Fade  = lerp(Night_UIWFXRain_Intensity, Day_UIWFXRain_Intensity, ENightDayFactor);
		OUT.Fade *= min(RainStrength, 1.2);
		OUT.Fade *= 1.0 - pow(Time.y, UIWFXRain_FadeCurve);
		
		//--- SkyrimBridge: Precipitation-scaled droplet intensity ---
		#ifdef SKYRIMBRIDGE_FXH
			OUT.Fade *= SB_LENS_GetRainIntensity();
		#endif
		
		OUT.SkyColor = lerp(Night_UIWFXRain_SkyColor, Day_UIWFXRain_SkyColor, ENightDayFactor);
		OUT.EnvColor = lerp(Night_UIWFXRain_EnvColor, Day_UIWFXRain_EnvColor, ENightDayFactor);
	}
	
	return OUT;
}


float4 PS_Rain(VertexShaderOutputRain IN) : SV_Target
{
	if(!IN.EnableRain) discard;
	float4 DropCol = 0.0;
	
	static const float3 SourceRay   = { 0.0, 0.0, -1.0 };
	static const float4 RefracIndex = 1.000277 / float4(1.3310, 1.3330, 1.3358, 1.3325); //Air -> Water(R,G,B,~)
	
	float2 SSCoords      = IN.pos.xy * PixelSize;
	float2 DropletLocalUV = frac(IN.texcoord.zw * RAINDROP_GRID_SIZE);
	float2 DropletSeed    = floor(IN.texcoord.zw * RAINDROP_GRID_SIZE);
	float4 DropletNormal  = ProceduralRainDroplet(DropletLocalUV, DropletSeed);
	
	clip(DropletNormal.w - 0.01);
	
	DropletNormal.xyz = DropletNormal.xyz * 2.0 - 1.0;
	DropletNormal.xy *= UIWFXRain_Curvature;
	
	float3 RefracSample;
	
	#if !RAINFX_ENABLE_DISPERSION
		float3 RefracVec    = refract(SourceRay, DropletNormal.xyz, RefracIndex.w);
		float2 RefracCoords = SSCoords + MatrixRotate(RefracVec.xy / RefracVec.z, IN.Rotation, true);
			   RefracSample = TextureOriginal.Sample(Linear_Sampler_Rain, RefracCoords).rgb;
		OOSSampleApprox(RefracSample, RefracCoords, IN.SkyColor, IN.EnvColor);
	
	#else
		float3 ScaledRefracIndex;
		ScaledRefracIndex.y  = RefracIndex.y;
		ScaledRefracIndex.xz = lerp(RefracIndex.y, RefracIndex.xz, UIWFXRain_Dispersion);
		
		[unroll] for(int i=0; i<3; i++)
		{
			float3 RefracVec       = refract(SourceRay, DropletNormal.xyz, ScaledRefracIndex[i]);
			float2 RefracCoords    = SSCoords + RefracVec.xy / RefracVec.z;
				   RefracSample[i] = TextureOriginal.Sample(Linear_Sampler_Rain, RefracCoords)[i];
			OOSSampleApprox(RefracSample, i, RefracCoords, IN.SkyColor, IN.EnvColor);
		}
	#endif
	
	IN.Fade *= DropletNormal.w;
	DropCol  = float4(RefracSample, 1.0) * IN.Fade;
	
	return saturate(DropCol);
}


VertexShaderOutputRainBlur VS_RainBlur(VertexShaderInput IN)
{
	VertexShaderOutputRainBlur OUT;
	OUT.pos			= float4(IN.pos.xyz, 1.0);
	OUT.texcoord.xy = IN.txcoord.xy;
	OUT.EnableRain  = !EInteriorFactor && UIWFXRain_Enable;
	
	//--- SkyrimBridge: Accurate interior detection ---
	#ifdef SKYRIMBRIDGE_FXH
		OUT.EnableRain = !SB_LENS_IsInterior() && UIWFXRain_Enable;
	#endif
	
	[branch] if(OUT.EnableRain)
	{
		OUT.Sigma        = UIWFXRain_Sigma + DELTA;
		OUT.WeightFactor =  rcp(OUT.Sigma * SqrtTwoPI);
		OUT.LoopCount    = ceil(OUT.Sigma * 1.5 - 0.01);
		OUT.Sigma        = -rcp(OUT.Sigma * OUT.Sigma);
		OUT.OffsetScale  = 1.0 - sqrt(saturate(UIWFXRain_Sigma / 3.0)) * 0.5;
	}
	
	return OUT;
}

float4 PS_RainBlur(VertexShaderOutputRainBlur IN) : SV_Target
{
	if(!IN.EnableRain) discard;
	float4 BlurredCol = 0.0;
	
	float4 StepSize     = PixelSize.xyxy * float4(1.0, 0.0, -1.0, 0.0);
	float4 HalfStepSize = StepSize * IN.OffsetScale;
		   StepSize    *= 2.0;
	
	float WeightSum  = IN.WeightFactor * IN.OffsetScale;
		  BlurredCol = WeightSum * RenderTargetRGBA32.Sample(Point_Sampler, IN.texcoord);
	
	[loop] for(float i=1.0; i <= IN.LoopCount; i++)
	{
		float  GaussWeight = IN.WeightFactor * exp(i*i*IN.Sigma);
		float4 CurrOffset  = IN.texcoord.xyxy + i * StepSize - HalfStepSize;
		
		BlurredCol += GaussWeight * RenderTargetRGBA32.SampleLevel(Linear_Sampler, CurrOffset.xy,0);
		BlurredCol += GaussWeight * RenderTargetRGBA32.SampleLevel(Linear_Sampler, CurrOffset.zw,0);
		WeightSum  += GaussWeight * 2.0;
	}
	
	BlurredCol /= WeightSum;
	
	return BlurredCol;
}


//----------------------------------------------------------------------------------------------//
//								SPECTRAL CHROMATIC ABERRATION									//
//																								//
//	  Multi-wavelength dispersion with CIE-approximate spectral weights							//
//	  Cauchy-derived offsets: n(l)=A+B/l^2, centered at 550nm green								//
//----------------------------------------------------------------------------------------------//

// 6 spectral samples covering 430-660nm visible range
// Positive dispersion = blue/violet end (shorter wavelengths refract more)
// Negative dispersion = red end (longer wavelengths refract less)
static const float  CA_Offset[6] = {  1.0,   0.58,   0.20,  -0.10,  -0.52,  -1.0  };
static const float3 CA_Weight[6] = { float3(0.02, 0.00, 0.80),   //~430nm Violet-Blue
                                     float3(0.00, 0.20, 0.85),   //~470nm Blue-Cyan
                                     float3(0.05, 0.85, 0.10),   //~520nm Green
                                     float3(0.35, 0.90, 0.00),   //~560nm Yellow-Green
                                     float3(0.95, 0.25, 0.00),   //~610nm Orange
                                     float3(0.55, 0.02, 0.00) }; //~660nm Deep Red


float3 SpectralCA(float2 Texcoord, float2 VigDir, float VigDist)
{
	//Base dispersion magnitude (backward-compatible with original Strength/Curve)
	float Magnitude = pow(VigDist / 1.15, UICA_Curve);
	      Magnitude *= UICA_Strength / VigDist * 10.0;
	
	//Center deadzone: smoothly fades CA at screen center to prevent sampling artifacts
	Magnitude *= smoothstep(UICA_Deadzone, UICA_Deadzone + 0.15, VigDist);
	
	//Direction vector scaled by magnitude and pixel size
	float2 BaseVector = VigDir * Magnitude * PixelSize;
	
	//Accumulate spectral samples
	float3 AccColor  = float3(0.0, 0.0, 0.0);
	float3 AccWeight = float3(0.0, 0.0, 0.0);
	
	[unroll] for(int s = 0; s < 6; s++)
	{
		//Per-wavelength dispersion scaled by spread control
		float Disp = CA_Offset[s] * UICA_Spread;
		
		//Asymmetric red/blue bias: interpolate between RedBias and BlueBias based on wavelength position
		float Bias = lerp(UICA_RedBias, UICA_BlueBias, saturate(CA_Offset[s] * 0.5 + 0.5));
		Disp *= Bias;
		
		//Barrel distortion: wavelength-dependent radial magnification (lateral CA)
		float BarrelMul = 1.0 + Disp * UICA_Barrel * VigDist * 0.5;
		
		//Final sample UV for this wavelength
		float2 SampleUV = Texcoord + BaseVector * Disp * BarrelMul;
		
		//Accumulate weighted spectral contribution
		float3 Tap = TextureColor.Sample(Linear_Sampler_CA, SampleUV).rgb;
		
		AccColor  += Tap * CA_Weight[s];
		AccWeight += CA_Weight[s];
	}
	
	//Normalize per channel independently
	AccColor /= deltalim(AccWeight);
	
	//Fringe saturation control: blend between monochrome and full color
	float  Luma = dot(AccColor, K_LUM);
	float3 Mono = float3(Luma, Luma, Luma);
	AccColor = lerp(Mono, AccColor, UICA_FringeSat);
	
	return AccColor;
}


//----------------------------------------------------------------------------------------------//
//						BROWN-CONRADY LENS DISTORTION MODEL									//
//																							//
//	  Radial (k1,k2,k3) + Tangential (p1,p2) distortion following the standard				//
//	  photogrammetric model: r' = r(1 + k1*r² + k2*r⁴ + k3*r⁶) + tangential				//
//----------------------------------------------------------------------------------------------//

float2 BrownConradyDistort(float2 Texcoord)
{
	float2 Centered = Texcoord * 2.0 - 1.0;
	
	//Correct for aspect ratio so distortion is circular not elliptical
	Centered.x *= ScreenSize.z; //ScreenSize.z = width/height
	
	float R2 = dot(Centered, Centered);
	float R4 = R2 * R2;
	float R6 = R4 * R2;
	
	//Radial distortion: k1*r² + k2*r⁴ + k3*r⁶
	float RadialFactor = 1.0 + UILO_DistortK1 * R2
	                         + UILO_DistortK2 * R4
	                         + UILO_DistortK3 * R6;
	
	//Tangential distortion (decentering): lens element misalignment
	float2 Tangential;
	Tangential.x = 2.0 * UILO_DistortP1 * Centered.x * Centered.y
	             + UILO_DistortP2 * (R2 + 2.0 * Centered.x * Centered.x);
	Tangential.y = UILO_DistortP1 * (R2 + 2.0 * Centered.y * Centered.y)
	             + 2.0 * UILO_DistortP2 * Centered.x * Centered.y;
	
	float2 Distorted = Centered * RadialFactor + Tangential;
	
	//Undo aspect ratio correction
	Distorted.x /= ScreenSize.z;
	
	return Distorted * 0.5 + 0.5;
}


//----------------------------------------------------------------------------------------------//
//						OPTICAL VIGNETTING													//
//																							//
//	  cos⁴θ natural falloff (radiometric) + mechanical vignetting (cat's eye)				//
//	  with chromatic edge shift from lens element dispersion								//
//----------------------------------------------------------------------------------------------//

float3 OpticalVignette(float3 Color, float2 Texcoord)
{
	float2 Centered = Texcoord * 2.0 - 1.0;
	float  Dist     = length(Centered);
	
	//Natural vignetting: cos⁴(θ) law from radiometry
	//θ = atan(r * tan(half_fov)), simplified to cos⁴(atan(r * scale))
	//Approximation: (1/(1+r²))² closely matches cos⁴(atan(r))
	float CosTheta  = 1.0 / (1.0 + Dist * Dist * 0.5);
	float NaturalVig = pow(CosTheta, 2.0 * UILO_VigNatural);
	
	//Mechanical vignetting: lens barrel obstruction creating cat's eye shapes at edges
	//Modeled as a soft-edged superellipse (squircle blend between circle and rectangle)
	float2 MechCoord = abs(Centered);
	MechCoord.x *= UILO_VigMechRatio;
	float  ShapeExp  = lerp(2.0, 8.0, UILO_VigRoundness); //2=circle, 8=rounded rect
	float  MechDist  = pow(pow(MechCoord.x, ShapeExp) + pow(MechCoord.y, ShapeExp), 1.0 / ShapeExp);
	float  MechVig   = 1.0 - saturate((MechDist - (1.2 - UILO_VigMechanical)) * 3.0);
	MechVig = smoothstep(0.0, 1.0, MechVig);
	
	//Combined vignette factor
	float VigFactor = NaturalVig * MechVig;
	
	//Chromatic edge shift: shorter wavelengths vignette more than longer ones
	//This creates a warm color cast at frame edges (blue falls off faster)
	float3 ChromaVig = lerp(float3(1.0, 1.0, 1.0), UILO_VigColorShift, Dist * UILO_VigColorAmt);
	
	return Color * VigFactor * ChromaVig;
}


//----------------------------------------------------------------------------------------------//
//						COMA ABERRATION														//
//																							//
//	  Off-axis comet-shaped PSF smearing: points at frame edges get stretched				//
//	  radially outward, creating characteristic comet tails									//
//----------------------------------------------------------------------------------------------//

float3 ComaAberration(float2 Texcoord, float2 VigDir, float VigDist)
{
	//Coma scales with cube of field angle: C ∝ r³
	float ComaAmount = pow(VigDist, UILO_ComaFalloff) * UILO_ComaStrength;
	
	//Direction: radially outward from center (sagittal coma)
	float2 ComaDir = normalize(VigDir + float2(DELTA, DELTA)) * ComaAmount * PixelSize;
	
	//Asymmetric sampling: more samples in the outward direction (comet tail)
	float3 AccColor  = float3(0.0, 0.0, 0.0);
	float  AccWeight = 0.0;
	int    NumSamples = (int)UILO_ComaSamples;
	
	[loop] for(int i = 0; i < NumSamples; i++)
	{
		//Asymmetric kernel: biased outward (positive direction gets more spread)
		float T = (float)i / (float)(NumSamples - 1); //0 to 1
		float Offset = lerp(-0.3, 1.0, T); //Asymmetric: slight inward, full outward
		
		//Weight: gaussian-ish with heavier center
		float Weight = exp(-Offset * Offset * 2.0);
		
		float2 SampleUV = Texcoord + ComaDir * Offset;
		float3 Tap = TextureColor.Sample(Linear_Sampler_CA, SampleUV).rgb;
		
		AccColor  += Tap * Weight;
		AccWeight += Weight;
	}
	
	return AccColor / max(AccWeight, DELTA);
}


//----------------------------------------------------------------------------------------------//
//						SPHERICAL ABERRATION												//
//																							//
//	  On-axis aberration: bright regions develop a soft halo due to marginal rays			//
//	  focusing at different distances than paraxial rays									//
//----------------------------------------------------------------------------------------------//

float3 SphericalAberration(float3 Color, float2 Texcoord)
{
	float Luma = dot(Color, K_LUM);
	
	//Spherical aberration affects bright areas more (wider pupil = more marginal rays)
	float HaloWeight = pow(saturate(Luma), UILO_SphericalBias) * UILO_SphericalStr * 0.3;
	
	//Sample a disc pattern to create the soft halo
	float3 Halo = float3(0.0, 0.0, 0.0);
	float2 Radius = UILO_SphericalRadius * PixelSize;
	
	//8-sample disc with golden angle spacing
	static const float GoldenAngle = 2.39996;
	float TotalWeight = 0.0;
	
	[unroll] for(int i = 0; i < 8; i++)
	{
		float Angle = (float)i * GoldenAngle;
		float R = sqrt((float)(i + 1) / 8.0); //Uniform disc distribution
		float2 Offset = float2(cos(Angle), sin(Angle)) * R * Radius;
		
		float3 Tap = TextureColor.Sample(Linear_Sampler_CA, Texcoord + Offset).rgb;
		float  W   = 1.0 - R * 0.5; //Center-weighted
		Halo += Tap * W;
		TotalWeight += W;
	}
	
	Halo /= TotalWeight;
	
	//Blend halo over original, weighted by brightness
	return lerp(Color, Halo, HaloWeight);
}


//----------------------------------------------------------------------------------------------//
//						FIELD CURVATURE													//
//																							//
//	  Petzval field curvature: the focal plane is curved, causing edges to be				//
//	  progressively out of focus. Simulated via mip-level biased sampling.					//
//----------------------------------------------------------------------------------------------//

float3 FieldCurvature(float2 Texcoord, float VigDist)
{
	//Curvature increases with distance from center following power law
	float CurveAmount = pow(VigDist, UILO_FieldCurveExp) * UILO_FieldCurveStr;
	
	//Use SampleLevel with mip bias to create progressive softening
	float MipLevel = CurveAmount * 3.0; //Scale to reasonable mip range
	
	return TextureColor.SampleLevel(Linear_Sampler_CA, Texcoord, MipLevel).rgb;
}


//----------------------------------------------------------------------------------------------//
//					PURPLE / GREEN FRINGING (Longitudinal CA)								//
//																							//
//	  Detects high-contrast edges via luminance Sobel gradient, then adds					//
//	  purple halos on the bright side and green halos on the dark side.						//
//	  This models axial color: different wavelengths focusing at different					//
//	  distances along the optical axis, visible at hard edges.								//
//----------------------------------------------------------------------------------------------//

float3 PurpleFringing(float2 Texcoord, float VigDist)
{
	//Radial modulation: fringing increases toward frame edges (more off-axis)
	float RadialWeight = pow(VigDist, UIPF_RadialFade) * 0.5 + 0.5;
	
	float2 SpreadPx = UIPF_Spread * PixelSize * RadialWeight;
	
	//Compute luminance gradient via Sobel-like 4-tap cross
	float LumaC = dot(TextureColor.Sample(Linear_Sampler_CA, Texcoord).rgb, K_LUM);
	float LumaL = dot(TextureColor.Sample(Linear_Sampler_CA, Texcoord + float2(-SpreadPx.x, 0)).rgb, K_LUM);
	float LumaR = dot(TextureColor.Sample(Linear_Sampler_CA, Texcoord + float2( SpreadPx.x, 0)).rgb, K_LUM);
	float LumaU = dot(TextureColor.Sample(Linear_Sampler_CA, Texcoord + float2(0, -SpreadPx.y)).rgb, K_LUM);
	float LumaD = dot(TextureColor.Sample(Linear_Sampler_CA, Texcoord + float2(0,  SpreadPx.y)).rgb, K_LUM);
	
	//Gradient magnitude (how strong is the edge)
	float2 Grad = float2(LumaR - LumaL, LumaD - LumaU);
	float  GradMag = length(Grad);
	
	//Threshold: only fringe at strong edges
	float EdgeMask = smoothstep(UIPF_Threshold, UIPF_Threshold + 0.1, GradMag);
	
	if (EdgeMask < 0.001) return TextureColor.Sample(Point_Sampler, Texcoord).rgb;
	
	//Sample along gradient direction (perpendicular to edge)
	float2 GradDir = normalize(Grad + float2(DELTA, DELTA));
	
	//Bright side sample (where luminance is higher)
	float2 BrightUV = Texcoord + GradDir * SpreadPx;
	float2 DarkUV   = Texcoord - GradDir * SpreadPx;
	
	float3 BrightSample = TextureColor.Sample(Linear_Sampler_CA, BrightUV).rgb;
	float3 DarkSample   = TextureColor.Sample(Linear_Sampler_CA, DarkUV).rgb;
	float3 CenterSample = TextureColor.Sample(Point_Sampler, Texcoord).rgb;
	
	//Purple fringe: defocused blue+red on the bright side of the edge
	//  Longitudinal CA causes blue and red to defocus opposite to green
	//  On the bright side → purple (R+B excess)
	//  On the dark side → green (G excess)
	float3 PurpleTint = float3(0.6, 0.1, 0.9); //Purple
	float3 GreenTint  = float3(0.2, 0.8, 0.2); //Green
	
	//Bias control: shift purple/green balance
	float BrightFringe = saturate(0.5 + UIPF_Bias);
	float DarkFringe   = saturate(0.5 - UIPF_Bias);
	
	float3 FringeColor = 0.0;
	FringeColor += (BrightSample - CenterSample) * PurpleTint * BrightFringe;
	FringeColor += (DarkSample - CenterSample)   * GreenTint  * DarkFringe;
	
	return CenterSample + FringeColor * EdgeMask * UIPF_Strength * RadialWeight;
}


//----------------------------------------------------------------------------------------------//
//					LENS BREATHING SIMULATION											//
//																							//
//	  Real cinema lenses change focal length slightly when refocusing.						//
//	  We approximate this by subtly scaling the UV based on scene average					//
//	  brightness (from TextureAperture/TextureDownsampled as a proxy for					//
//	  the focus state). Creates organic, barely-perceptible FOV shifts.						//
//----------------------------------------------------------------------------------------------//

float2 LensBreathing(float2 Texcoord)
{
	//Sample average scene brightness from downsampled texture center
	//  This approximates the aperture/focus-distance that would cause breathing
	float AvgBrightness = TextureDownsampled.SampleLevel(Linear_Sampler, float2(0.5, 0.5), 8).r;
	AvgBrightness = saturate(AvgBrightness);
	
	//Breathing offset: deviation from neutral brightness causes FOV change
	//  Bright scenes (wide aperture) → slight zoom in
	//  Dark scenes (narrow aperture) → slight zoom out
	float Deviation = (AvgBrightness - UILB_Center) * UILB_Amount;
	
	//Smooth the response (real lens breathing is mechanically damped)
	//  Using a power curve for gentle onset
	Deviation = sign(Deviation) * pow(abs(Deviation), UILB_Speed);
	
	//Apply as uniform scale from center
	float2 Centered = Texcoord - 0.5;
	float Scale = 1.0 + Deviation;
	
	return Centered * Scale + 0.5;
}


//----------------------------------------------------------------------------------------------//
//					ASTIGMATISM (Seidel Aberration III)									//
//																							//
//	  Off-axis points are imaged as short line segments instead of dots.					//
//	  The direction of the line rotates tangentially around the optical axis.				//
//	  Sagittal and tangential focal surfaces diverge with field angle,						//
//	  creating directional blur that changes orientation across the frame.					//
//----------------------------------------------------------------------------------------------//

float3 Astigmatism(float2 Texcoord, float2 VigDir, float VigDist)
{
	//Astigmatism onset: minimal at center, increases with field angle
	float AstigAmount = smoothstep(UIAST_Onset, UIAST_Onset + 0.3, VigDist);
	AstigAmount *= pow(VigDist, UIAST_Falloff) * UIAST_Strength;
	
	if (AstigAmount < 0.001) return TextureColor.Sample(Point_Sampler, Texcoord).rgb;
	
	//Blur direction: tangential to the radial direction from center
	//  Sagittal plane blur is radial, tangential is perpendicular
	//  Real astigmatism shows a cross pattern; we approximate with tangential blur
	float2 RadialDir    = normalize(VigDir + float2(DELTA, DELTA));
	float2 TangentialDir = float2(-RadialDir.y, RadialDir.x);
	
	//Anisotropic blur along the tangential direction
	float3 AccColor  = 0.0;
	float  AccWeight = 0.0;
	float2 BlurVec   = TangentialDir * AstigAmount * PixelSize;
	
	int Samples = UIAST_Samples;
	
	[loop] for (int i = 0; i < Samples; i++)
	{
		float T = ((float)i / (float)(Samples - 1)) * 2.0 - 1.0; // -1 to +1
		float Weight = exp(-T * T * 2.0); // Gaussian profile
		
		float2 SampleUV = Texcoord + BlurVec * T;
		float3 Tap = TextureColor.Sample(Linear_Sampler_CA, SampleUV).rgb;
		
		AccColor  += Tap * Weight;
		AccWeight += Weight;
	}
	
	return AccColor / max(AccWeight, DELTA);
}


//-------------------------------------Frost Vignette Effect------------------------------------//

VertexShaderOutputFrost VS_Frost(VertexShaderInput IN)
{
	VertexShaderOutputFrost OUT;
	OUT.pos         = float4(IN.pos.xyz, 1.0);
	OUT.texcoord    = IN.txcoord.xy;
	OUT.EnableFrost = !EInteriorFactor && UIWFXFrost_Enable;
	OUT.EnableRain  = !EInteriorFactor && UIWFXRain_Enable;
	
	//--- SkyrimBridge: Accurate interior detection + weather-based frost ---
	#ifdef SKYRIMBRIDGE_FXH
	{
		bool IsOutdoor = !SB_LENS_IsInterior();
		OUT.EnableRain  = IsOutdoor && UIWFXRain_Enable;
		OUT.EnableFrost = IsOutdoor && UIWFXFrost_Enable;
		// Auto-enable frost when weather is cold (snow/blizzard)
		if(IsOutdoor && SB_LENS_ShouldEnableFrost())
			OUT.EnableFrost = true;
	}
	#endif
	
	[branch] if(OUT.EnableFrost)
	{
		//Frost strength is directly controlled by UI (no weather list needed)
		float FrostStrength = 1.0;
		
		float2 Time; //x = seconds, y = frac part between seconds
		Time.y = modf(Timer.x * 16777.216 * UIWFXFrost_PulseRate, Time.x);
		Time.y = abs(ceil(fmod(Time.x, 2.0)) - Time.y);
		
		//Additional info for ps computations
		float UIWFXFrost_Intensity = lerp(Night_UIWFXFrost_Intensity, Day_UIWFXFrost_Intensity, ENightDayFactor);
		OUT.Vignette.z = 4.0 - FrostStrength * 1.5;
		
		FrostStrength *= 1.0 + Time.y * UIWFXFrost_PulseStrength;
		OUT.Vignette.x = UIWFXFrost_Curve + (2.0 - FrostStrength) * 0.1;
		OUT.Vignette.y = UIWFXFrost_Intensity * saturate(FrostStrength);
		
		OUT.Radii.x  = zerolim(UIWFXFrost_RadiusInner - FrostStrength * 0.1);
		OUT.Radii.y  = rcp(max(UIWFXFrost_RadiusOuter, OUT.Radii.x + DELTA) - OUT.Radii.x);
		OUT.Radii.x *= OUT.Radii.y;
		
		OUT.FrostTint = UIWFXFrost_Tint / deltalim(dot(UIWFXFrost_Tint, N_LUM));
		
		OUT.SkyColor = lerp(Night_UIWFXFrost_SkyColor, Day_UIWFXFrost_SkyColor, ENightDayFactor);
		OUT.EnvColor = lerp(Night_UIWFXFrost_EnvColor, Day_UIWFXFrost_EnvColor, ENightDayFactor);
	}
	
	[branch] if(OUT.EnableRain)
	{
		OUT.Sigma        = UIWFXRain_Sigma + DELTA;
		OUT.WeightFactor =  rcp(OUT.Sigma * SqrtTwoPI);
		OUT.LoopCount    = ceil(OUT.Sigma * 1.5 - 0.01);
		OUT.Sigma        = -rcp(OUT.Sigma * OUT.Sigma);
		OUT.OffsetScale  = 1.0 - sqrt(saturate(UIWFXRain_Sigma / 3.0)) * 0.5;
	}
	
	return OUT;
}


float4 PS_Frost(VertexShaderOutputFrost IN) : SV_Target
{
	//---------- LENS BREATHING ----------//
	//Subtle FOV change based on scene brightness (focus-distance proxy)
	//Must come before all sampling as it modifies the working UV
	
	float2 BaseUV = IN.texcoord;
	
	[branch] if(UILB_Enable)
	{
		BaseUV = LensBreathing(IN.texcoord);
	}
	
	//---------- LENS DISTORTION ----------//
	//Apply Brown-Conrady distortion model to UV coordinates before any sampling
	//This must come first as it affects all subsequent texture reads
	
	float2 WorkUV = BaseUV;
	
	[branch] if(UILO_Enable && (abs(UILO_DistortK1) + abs(UILO_DistortK2) + abs(UILO_DistortK3)
	           + abs(UILO_DistortP1) + abs(UILO_DistortP2)) > 0.0)
	{
		WorkUV = BrownConradyDistort(BaseUV);
	}
	
	//Compute radial vectors from (possibly distorted) center
	float2 VigVector   = WorkUV * 2.0 - 1.0;
	float  VigGradient = length(VigVector);
	
	
	//---------- FIELD CURVATURE ----------//
	//Petzval field curvature: edges are progressively out of focus
	//Applied before other effects to establish the base image
	
	float4 Color;
	
	[branch] if(UILO_Enable && UILO_FieldCurveStr > 0.0)
	{
		Color.rgb = FieldCurvature(WorkUV, VigGradient);
		Color.a   = 0.0;
	}
	else
	{
		Color = float4(TextureColor.Sample(Point_Sampler, WorkUV).rgb, 0.0);
	}
	
	
	//---------- SPECTRAL CHROMATIC ABERRATION ----------//
	//6-wavelength dispersion with Cauchy-derived offsets
	
	[branch] if(UICA_Enable)
	{
		Color.rgb = SpectralCA(WorkUV, VigVector, VigGradient);
	}
	
	
	//---------- COMA ABERRATION ----------//
	//Off-axis comet smearing, increases with distance from optical axis
	
	[branch] if(UILO_Enable && UILO_ComaStrength > 0.0 && VigGradient > 0.2)
	{
		float ComaBlend = smoothstep(0.2, 0.5, VigGradient);
		float3 ComaResult = ComaAberration(WorkUV, VigVector, VigGradient);
		Color.rgb = lerp(Color.rgb, ComaResult, ComaBlend);
	}
	
	
	//---------- ASTIGMATISM ----------//
	//Off-axis tangential blur from sagittal/tangential focus surface divergence
	
	[branch] if(UIAST_Enable && VigGradient > UIAST_Onset)
	{
		float AstigBlend = smoothstep(UIAST_Onset, UIAST_Onset + 0.2, VigGradient);
		float3 AstigResult = Astigmatism(WorkUV, VigVector, VigGradient);
		Color.rgb = lerp(Color.rgb, AstigResult, AstigBlend);
	}
	
	
	//---------- PURPLE / GREEN FRINGING ----------//
	//Longitudinal chromatic aberration at high-contrast edges
	
	[branch] if(UIPF_Enable)
	{
		Color.rgb = PurpleFringing(WorkUV, VigGradient);
	}
	
	
	//---------- SPHERICAL ABERRATION ----------//
	//Bright regions develop soft halos from marginal ray focusing error
	
	[branch] if(UILO_Enable && UILO_SphericalStr > 0.0)
	{
		Color.rgb = SphericalAberration(Color.rgb, WorkUV);
	}
	
	
	//---------- OPTICAL VIGNETTING ----------//
	//cos⁴θ natural + mechanical barrel + chromatic edge shift
	
	[branch] if(UILO_Enable && (UILO_VigNatural > 0.0 || UILO_VigMechanical > 0.0 || UILO_VigColorAmt > 0.0))
	{
		Color.rgb = OpticalVignette(Color.rgb, WorkUV);
	}
	
	
	//---------- HALATION + VEILING GLARE COMPOSITE ----------//
	//Read prepass result from RenderTarget512 and additively blend
	
	[branch] if(UIHL_Enable || UIVG_Enable)
	{
		float4 HalationData = RenderTarget512.Sample(Linear_Sampler, IN.texcoord).rgba;
		Color.rgb += HalationData.rgb;
	}
	
	
	//---------- WEATHER EFFECTS ----------//
	[branch] if(IN.EnableRain)
	{
		float4 StepSize     = PixelSize.xyxy * float4(0.0, 1.0, 0.0, -1.0);
		float4 HalfStepSize = StepSize * IN.OffsetScale;
			   StepSize    *= 2.0;
		
		float  WeightSum  = IN.WeightFactor * IN.OffsetScale;
		float4 BlurredCol = WeightSum * RenderTargetRGBA64F.Sample(Point_Sampler, IN.texcoord);
		
		[loop] for(float i=1.0; i <= IN.LoopCount; i++)
		{
			float  GaussWeight = IN.WeightFactor * exp(i*i*IN.Sigma);
			float4 CurrOffset  = IN.texcoord.xyxy + i * StepSize - HalfStepSize;
			
			BlurredCol += GaussWeight * RenderTargetRGBA64F.SampleLevel(Linear_Sampler, CurrOffset.xy,0);
			BlurredCol += GaussWeight * RenderTargetRGBA64F.SampleLevel(Linear_Sampler, CurrOffset.zw,0);
			WeightSum  += GaussWeight * 2.0;
		}
		
		Color += BlurredCol / WeightSum;
	}
	
	[branch] if(IN.EnableFrost)
	{
		float VigWeight = VigGradient * IN.Radii.y - IN.Radii.x; //LinearStep
			  VigWeight = saturate(pow(VigWeight, IN.Vignette.x) * IN.Vignette.y);
		
		#if FROSTFX_USE_REFRACTION_METHOD
			//Refraction approximation using procedural frost normals
			float3 FrostData = ProceduralFrostRefraction(IN.texcoord);
			
			float2 MappedCoords  = FrostData.xy * 2.0 + 1.0;
				   MappedCoords *= UIWFXFrost_RefracRange;
				   MappedCoords -= UIWFXFrost_RefracRange * 2.0;
				   MappedCoords += IN.texcoord;
			
			float3 RefracCol   = TextureOriginal.Sample(Linear_Sampler_Rain, MappedCoords).rgb;
			bool   NoOvershoot = all(saturate(MappedCoords - MappedCoords * MappedCoords));
				   NoOvershoot = NoOvershoot || UIWFXFrost_AllowOvershoot;
				   VigWeight   = saturate(VigWeight * FrostData.z * NoOvershoot);
			
			float Opacity   = FrostData.z / dot(abs(FrostData.xy - 0.5), 99.0 - UIWFXFrost_Opacity);
				  RefracCol = lerp(RefracCol, IN.FrostTint, saturate(Opacity));
			
			OOSSampleApprox(RefracCol, MappedCoords, IN.SkyColor, IN.EnvColor);
			Color = lerp(Color, float4(RefracCol, 1.0), VigWeight);
			
		#else
			//Non-refraction frost: coverage + thickness + sparkle composite
			float3 FrostData = ProceduralFrostLayers(IN.texcoord);
			float  FrostCoverage  = FrostData.x; //Where frost exists
			float  FrostThickness = FrostData.y; //How opaque: 0=thin film, 1=thick white
			float  FrostSparkle   = FrostData.z; //Micro-facet glints

			//Scene color for blending
			float3 SceneUnder = Color.rgb;
			float  SceneLuma  = dot(SceneUnder, K_LUM);

			//Desaturate scene under frost (ice diffuses light, washing out color)
			float DesatAmount = FrostThickness * 0.75;
			float3 SceneDesat = lerp(SceneUnder, SceneLuma, DesatAmount);

			//Frost surface color: thin ice has blue tint, thick frost is white
			float3 ThinIceColor  = float3(0.82, 0.90, 1.0);  //Noticeable blue shift
			float3 ThickFrostCol = IN.FrostTint;               //User tint (default white)
			float3 FrostSurface  = lerp(ThinIceColor, ThickFrostCol, FrostThickness);

			//Brightness: frost scatters ambient light, thicker = brighter
			float AmbientBright = lerp(0.35, 1.0, FrostThickness);

			//Subsurface scattering approximation: thin frost transmits tinted scene light
			float3 Subsurface = SceneDesat * ThinIceColor * (1.0 - FrostThickness * 0.8);
			FrostSurface = lerp(FrostSurface * AmbientBright + Subsurface * 0.15, FrostSurface * AmbientBright, FrostThickness);

			//Composite: thin ice = translucent, thick frost = opaque
			float  Opacity     = lerp(0.12, 0.95, FrostThickness) * FrostCoverage;
			float3 FrostResult = lerp(SceneDesat, FrostSurface, Opacity);

			//Additive sparkle highlights (bright crystal facet reflections)
			float SparkleBoost = (1.0 + SceneLuma * 0.5) * 1.8;
			FrostResult += FrostSparkle * SparkleBoost * IN.FrostTint;

			//Apply vignette weight and blend
			float FinalWeight = saturate(VigWeight * FrostCoverage);
			Color.rgb = lerp(Color.rgb, FrostResult, FinalWeight);
		#endif //FROSTFX_USE_REFRACTION_METHOD
	}
	
	return Color;
}



//----------------------------------------------------------------------------------------------//
//									  PROFESSIONAL FILM GRAIN									//
//																								//
//	 Multi-layer emulsion grain with luminance response and per-channel decorrelation			//
//----------------------------------------------------------------------------------------------//

//----------------------------------------------------------------------------------------------//
//Struct

struct VertexShaderOutputFilmGrain
{
   float4 pos            : SV_POSITION;
   float2 texcoord       : TEXCOORD0;
NI float  FG_Int         : FG0;
NI float  FG_Size        : FG1;
NI float  FG_Roughness   : FG2;
NI float  FG_FrameSeed   : FG3;
NI float3 FG_LumaResp    : FG4; //shadow, midtone, highlight weights
NI float3 FG_ChanWeight   : FG5;
NI float  FG_ChanDecorr   : FG6;
};


//----------------------------------------------------------------------------------------------//
//Grain noise generation using interleaved gradient noise (IGN)
//  Superior to sin-based PRNGs: better statistical distribution, no visible
//  patterns at any scale, and compatible with TAA temporal integration.
//  Reference: Jimenez 2014 "Next Generation Post Processing in Call of Duty"

float FG_IGN(float2 PixelCoord, float FrameOffset)
{
	//Interleaved gradient noise: excellent spatial distribution
	float3 Magic = float3(0.06711056, 0.00583715, 52.9829189);
	float2 Shifted = PixelCoord + FrameOffset * float2(5.588238, 5.588238);
	return frac(Magic.z * frac(dot(Shifted, Magic.xy)));
}

//Blue-noise-quality hash (better distribution than sin-based)
float FG_Hash(float2 Coord, float Seed)
{
	uint2 IC = uint2(Coord);
	uint  H  = IC.x * 0x9e3779b9u + IC.y * 0x517cc1b7u + asuint(Seed);
	H ^= H >> 16u;
	H *= 0x45d9f3bu;
	H ^= H >> 16u;
	return float(H) / 4294967295.0;
}


//Multi-layer grain with gaussian distribution and organic clumping
float FG_Structured(float2 Coord, float Seed, float Roughness)
{
	//Primary grain: IGN-based fine structure
	float Fine1 = FG_IGN(Coord, Seed);
	float Fine2 = FG_IGN(Coord, Seed + 3.17);
	float Fine3 = FG_IGN(Coord, Seed + 7.89);
	
	//Central Limit Theorem: average of 3 uniform -> approximately gaussian
	float FineGrain = (Fine1 + Fine2 + Fine3) * 0.333333;
	
	//Coarser clumps at half and quarter resolution (halide crystal clusters)
	float Coarse1 = FG_Hash(floor(Coord * 0.5), Seed + 11.0);
	float Coarse2 = FG_Hash(floor(Coord * 0.25), Seed + 23.0);
	
	//Blend grain scales: roughness controls crystal cluster visibility
	float Grain = lerp(FineGrain,
	                    FineGrain * 0.5 + Coarse1 * 0.35 + Coarse2 * 0.15,
	                    Roughness);
	
	//Sharpen the gaussian tail slightly for more film-like character
	//  Real film grain has heavier tails than pure gaussian
	float Excess = abs(Grain - 0.5);
	Grain = lerp(Grain, sign(Grain - 0.5) * 0.5 * pow(2.0 * Excess, 0.85) + 0.5, 0.2);
	
	//Center around zero [-0.5, +0.5]
	return Grain - 0.5;
}


//Film sensitometry: luminance-dependent grain response
//  Based on Hurter-Driffield (H&D) characteristic curves:
//  - Toe region (shadows): heavy grain from underexposed silver halide clusters
//    Grain is coarser here because fewer crystals develop, creating clumps
//  - Straight-line (midtones): moderate, well-distributed grain
//  - Shoulder (highlights): minimal grain as emulsion is fully developed
//  - Solarization edge: very bright highlights can show increased grain
//  Uses smooth Hermite blends to avoid banding at region transitions
float FG_LuminanceResponse(float Luma, float3 Response)
{
	//Wider overlap zones for smoother transitions (prevents banding)
	float Shadow    = 1.0 - smoothstep(0.0,  0.40, Luma);
	float Highlight = smoothstep(0.50, 0.95, Luma);
	float Midtone   = smoothstep(0.05, 0.30, Luma) * (1.0 - smoothstep(0.60, 0.90, Luma));
	
	//Normalize to ensure weights sum to 1.0 (energy conservation)
	float Total = Shadow + Midtone + Highlight + DELTA;
	
	return (Shadow * Response.x + Midtone * Response.y + Highlight * Response.z) / Total;
}


//----------------------------------------------------------------------------------------------//
//						SENSOR RESPONSE FUNCTIONS											//
//																							//
//	  Film characteristic S-curve (shoulder + toe), channel crosstalk,						//
//	  and sensor noise models (read noise, shot noise, hot pixels)							//
//----------------------------------------------------------------------------------------------//

float SensorShoulder(float X, float Start)
{
	//Soft shoulder compression: mimics film highlight rolloff
	//Uses a smooth tanh-like curve starting at Start luminance
	float Excess = max(X - Start, 0.0);
	float Compressed = Start + Excess / (1.0 + Excess);
	return (X > Start) ? Compressed : X;
}

float SensorToe(float X, float ToeEnd)
{
	//Shadow toe: lifts deep shadows and compresses them (film base fog density)
	//Smooth power curve that bends shadows upward
	float Normalized = saturate(X / max(ToeEnd, DELTA));
	float ToeShape   = Normalized * Normalized * (3.0 - 2.0 * Normalized); //smoothstep
	return (X < ToeEnd) ? ToeShape * ToeEnd : X;
}

float3 ApplySensorResponse(float3 Color, float2 PixelPos, float FrameSeed)
{
	//Film response curve: shoulder (highlight compression) + toe (shadow lift)
	[branch] if(UISR_Shoulder > 0.0 || UISR_Toe > 0.0)
	{
		float ShoulderAmt = UISR_Shoulder;
		float ToeAmt      = UISR_Toe;
		
		[unroll] for(int c = 0; c < 3; c++)
		{
			if(ShoulderAmt > 0.0)
				Color[c] = lerp(Color[c], SensorShoulder(Color[c], UISR_ShoulderStart), ShoulderAmt);
			if(ToeAmt > 0.0)
				Color[c] = lerp(Color[c], SensorToe(Color[c], UISR_ToeEnd), ToeAmt);
		}
	}
	
	//Channel crosstalk: color filter array bleed between adjacent photodiodes
	//Models physical dye overlap in Bayer filter
	[branch] if(UISR_Crosstalk > 0.0)
	{
		float3 Bleed;
		Bleed.r = Color.r + Color.g * UISR_CrossGtoB * UISR_Crosstalk
		                   + Color.b * UISR_CrossBtoR * UISR_Crosstalk;
		Bleed.g = Color.g + Color.r * UISR_CrossRtoG * UISR_Crosstalk
		                   + Color.b * UISR_CrossBtoR * UISR_Crosstalk * 0.5;
		Bleed.b = Color.b + Color.g * UISR_CrossGtoB * UISR_Crosstalk
		                   + Color.r * UISR_CrossRtoG * UISR_Crosstalk * 0.5;
		
		//Renormalize to preserve overall brightness
		float OrigLuma = dot(Color, K_LUM);
		float BleedLuma = dot(Bleed, K_LUM);
		Color = Bleed * (OrigLuma / max(BleedLuma, DELTA));
	}
	
	//Sensor noise: read noise (constant) + shot noise (signal-dependent) + hot pixels
	//  Using IGN for better distribution (no banding artifacts from sin-hash)
	float NoiseBase  = FG_IGN(PixelPos, FrameSeed);
	float NoiseBase2 = FG_IGN(PixelPos, FrameSeed + 3.17);
	float NoiseBase3 = FG_IGN(PixelPos, FrameSeed + 7.89);
	
	//Read noise: gaussian-approximated, constant across all pixels
	[branch] if(UISR_ReadNoise > 0.0)
	{
		float3 ReadN = float3(NoiseBase, NoiseBase2, NoiseBase3);
		ReadN = (ReadN - 0.5) * UISR_ReadNoise;
		Color += ReadN;
	}
	
	//Shot noise: Poisson-distributed, proportional to sqrt(signal)
	[branch] if(UISR_PhotonNoise > 0.0)
	{
		float3 ShotN = float3(NoiseBase2, NoiseBase3, NoiseBase);
		ShotN = (ShotN - 0.5) * UISR_PhotonNoise * sqrt(max(Color, DELTA));
		Color += ShotN;
	}
	
	//Hot pixels: stuck-on photodiodes that appear as bright dots
	[branch] if(UISR_HotPixels > 0.0)
	{
		float HotThresh = 1.0 - UISR_HotPixels;
		//Use stable hash so hot pixels stay in same position
		float StableHash = abs(frac(sin(dot(PixelPos, float2(127.1, 311.7))) * 43758.5453));
		if(StableHash > HotThresh)
		{
			//Which channel is hot (deterministic per pixel)
			float HotVal = 0.95 + StableHash * 0.05;
			float ChanHash = frac(StableHash * 7.31);
			if(ChanHash < 0.333)
				Color.r = max(Color.r, HotVal);
			else if(ChanHash < 0.666)
				Color.g = max(Color.g, HotVal);
			else
				Color.b = max(Color.b, HotVal);
		}
	}
	
	return saturate(Color);
}

//------------------------------Film Grain Vertex Shader------------------------------------------//

VertexShaderOutputFilmGrain VS_FilmGrain(VertexShaderInput IN)
{
	VertexShaderOutputFilmGrain OUT;
	OUT.pos      = float4(IN.pos.xyz, 1.0);
	OUT.texcoord = IN.txcoord.xy;
	
	//DNI intensity separation
	OUT.FG_Int        = DNI_SEPARATION(UIFG_Intensity);
	OUT.FG_Size       = UIFG_Size;
	OUT.FG_Roughness  = UIFG_Roughness;
	OUT.FG_ChanWeight  = UIFG_ChannelWeight;
	OUT.FG_ChanDecorr  = UIFG_ChannelDecorr;
	OUT.FG_LumaResp    = float3(UIFG_ShadowGrain, UIFG_MidtoneGrain, UIFG_HighlightGrain);
	
	//Temporal seed for per-frame grain animation
	OUT.FG_FrameSeed = floor(Timer.z * UIFG_AnimSpeed);
	
	return OUT;
}


//-------------------------------Film Grain Pixel Shader------------------------------------------//

float4 PS_FilmGrain(VertexShaderOutputFilmGrain IN) : SV_Target
{
	float3 Color = TextureColor.Sample(Point_Sampler, IN.texcoord).rgb;
	
	[branch] if(UIFG_Enable && IN.FG_Int > 0.0)
	{
		//Pixel coordinates scaled by grain size
		float2 GrainCoord = IN.pos.xy / IN.FG_Size;
		
		//Temporal offset using golden ratio for optimal temporal decorrelation
		//  Each frame samples a maximally different point in noise space
		float TSeed = IN.FG_FrameSeed * 1.618033988;
		
		//Base grain (red channel / monochrome base)
		float3 Grain;
		Grain.r = FG_Structured(GrainCoord, TSeed, IN.FG_Roughness);
		
		//Per-channel decorrelation simulating independent emulsion dye layers
		//  In real film, R/G/B sensitive layers have independent silver halide
		//  crystal distributions: cyan dye layer (R), magenta (G), yellow (B)
		float GrainG = FG_Structured(GrainCoord + float2(1.47, 2.93), TSeed + 5.39, IN.FG_Roughness);
		float GrainB = FG_Structured(GrainCoord + float2(3.71, 0.59), TSeed + 9.71, IN.FG_Roughness);
		Grain.g = lerp(Grain.r, GrainG, IN.FG_ChanDecorr);
		Grain.b = lerp(Grain.r, GrainB, IN.FG_ChanDecorr);
		
		//Per-channel emulsion weights (blue typically grainier in real film stock)
		Grain *= IN.FG_ChanWeight;
		
		//Luminance-adaptive response
		float Luma     = dot(Color, K_LUM);
		float Response = FG_LuminanceResponse(Luma, IN.FG_LumaResp);
		
		//Final grain amount
		float3 GrainAmount = Grain * IN.FG_Int * Response;
		
		//Photographic soft clip: grain saturates naturally near black/white
		//  Uses tanh-like curve that preserves grain character at extremes
		//  while preventing harsh clipping artifacts
		float3 AbsGrain = abs(GrainAmount);
		float3 SoftClip = 1.0 - exp(-AbsGrain * 3.5);
		GrainAmount = sign(GrainAmount) * SoftClip * 0.285;
		
		//Apply grain in perceptual space for more natural appearance
		//  Grain added to sqrt(color) then squared back - this makes grain
		//  less visible in dark areas where it would otherwise dominate
		float3 SqrtColor = sqrt(max(Color, 0.0));
		SqrtColor += GrainAmount * 0.5;
		Color = saturate(SqrtColor * SqrtColor + GrainAmount * 0.5);
	}
	
	//---------- SENSOR RESPONSE ----------//
	//Film S-curve, channel crosstalk, and sensor noise
	[branch] if(UISR_Enable)
	{
		Color = ApplySensorResponse(Color, IN.pos.xy, IN.FG_FrameSeed);
	}
	
	return float4(Color, 1.0);
}



//----------------------------------------------------------------------------------------------//
//						HALATION + VEILING GLARE PREPASS									//
//																							//
//	  Halation: subsurface scatter through film base/sensor cover glass.					//
//	  Bright highlights bleed into surrounding area with warm red-orange tint.				//
//	  Veiling Glare: low-frequency scatter from lens surface imperfections.					//
//	  Reduces overall contrast by spreading bright light across the entire image.			//
//----------------------------------------------------------------------------------------------//

//16-sample Poisson disc for halation blur (well-distributed, low-discrepancy)
static const float2 PoissonDisc16[16] = {
	float2(-0.94201624, -0.39906216), float2( 0.94558609, -0.76890725),
	float2(-0.09418410, -0.92938870), float2( 0.34495938,  0.29387760),
	float2(-0.91588581,  0.45771432), float2(-0.81544232, -0.87912464),
	float2(-0.38277543,  0.27676845), float2( 0.97484398,  0.75648379),
	float2( 0.44323325, -0.97511554), float2( 0.53742981, -0.47373420),
	float2(-0.26496911, -0.41893023), float2( 0.79197514,  0.19090188),
	float2(-0.24188840,  0.99706507), float2(-0.81409955,  0.91437590),
	float2( 0.19984126,  0.78641367), float2( 0.14383161, -0.14100790)
};

//8-sample ring for veiling glare (wider scatter)
static const float2 GlareRing8[8] = {
	float2( 1.0,  0.0), float2( 0.707,  0.707),
	float2( 0.0,  1.0), float2(-0.707,  0.707),
	float2(-1.0,  0.0), float2(-0.707, -0.707),
	float2( 0.0, -1.0), float2( 0.707, -0.707)
};


struct VertexShaderOutputHalation
{
   float4 pos          : SV_POSITION;
   float2 texcoord     : TEXCOORD0;
NI float  HL_Int        : HL0;
NI float  HL_Thresh     : HL1;
NI float  HL_ThCurve    : HL2;
NI float  HL_Radius     : HL3;
NI float3 HL_Color      : HL4;
NI float  HL_Sat        : HL5;
NI float  VG_Int        : HL6;
NI float  VG_Thresh     : HL7;
NI float  VG_Radius     : HL8;
NI float3 VG_Tint       : HL9;
};


VertexShaderOutputHalation VS_Halation(VertexShaderInput IN)
{
	VertexShaderOutputHalation OUT;
	OUT.pos       = float4(IN.pos.xyz, 1.0);
	OUT.texcoord  = IN.txcoord.xy;
	
	OUT.HL_Int      = UIHL_Enable ? DNI_SEPARATION(UIHL_Intensity) : 0.0;
	OUT.HL_Thresh   = UIHL_Threshold;
	OUT.HL_ThCurve  = UIHL_ThreshCurve;
	OUT.HL_Radius   = UIHL_Radius;
	OUT.HL_Color    = UIHL_Color;
	OUT.HL_Sat      = UIHL_Saturation;
	
	OUT.VG_Int      = UIVG_Enable ? UIVG_Intensity : 0.0;
	OUT.VG_Thresh   = UIVG_Threshold;
	OUT.VG_Radius   = UIVG_Radius;
	OUT.VG_Tint     = UIVG_Tint;
	
	return OUT;
}


float4 PS_Halation(VertexShaderOutputHalation IN) : SV_Target
{
	float4 Result = float4(0.0, 0.0, 0.0, 0.0);
	
	//Early-out if both systems are disabled
	if(IN.HL_Int <= 0.0 && IN.VG_Int <= 0.0) return Result;
	
	float2 RadiusHL = IN.HL_Radius * PixelSize;
	float2 RadiusVG = IN.VG_Radius * PixelSize;
	
	//Per-channel wavelength scatter multipliers for halation.
	//  In real film stock, red light scatters ~1.4× further through the
	//  anti-halation layer than blue. This creates the characteristic
	//  warm red-orange glow around bright highlights on film.
	static const float3 WavelengthScale = float3(1.4, 1.0, 0.7); //R, G, B
	
	//---------- HALATION ----------//
	//Subsurface scatter: bright light passes through film base and scatters back
	//Result is a warm, soft glow around highlights with red reaching further
	
	float3 HalationAccum = float3(0.0, 0.0, 0.0);
	
	[branch] if(IN.HL_Int > 0.0)
	{
		float3 HalationWeight = float3(DELTA, DELTA, DELTA);
		
		[unroll] for(int h = 0; h < 16; h++)
		{
			//Disc falloff: center samples contribute less (we want the scatter ring)
			float  DiscR   = length(PoissonDisc16[h]);
			float  RingW   = smoothstep(0.1, 0.6, DiscR);
			float  BaseW   = 0.5 + RingW * 0.5;
			
			//Per-channel sampling at wavelength-dependent radii.
			//  Red channel samples further out, blue samples tighter.
			float3 TapR, TapG, TapB;
			float2 Dir = PoissonDisc16[h];
			
			float2 UV_R = IN.texcoord + Dir * RadiusHL * WavelengthScale.r;
			float2 UV_G = IN.texcoord + Dir * RadiusHL * WavelengthScale.g;
			float2 UV_B = IN.texcoord + Dir * RadiusHL * WavelengthScale.b;
			
			TapR = TextureColor.Sample(Linear_Sampler, UV_R).rgb;
			TapG = TextureColor.Sample(Linear_Sampler, UV_G).rgb;
			TapB = TextureColor.Sample(Linear_Sampler, UV_B).rgb;
			
			//Per-channel luminance thresholding
			float3 Lumas  = float3(dot(TapR, K_LUM), dot(TapG, K_LUM), dot(TapB, K_LUM));
			float3 Excess = max(Lumas - IN.HL_Thresh, 0.0);
			float3 Mask   = pow(saturate(Excess * 2.0), IN.HL_ThCurve);
			float3 W      = Mask * BaseW;
			
			//Accumulate per-channel: each channel gets its own wavelength-appropriate sample
			HalationAccum.r += TapR.r * W.r;
			HalationAccum.g += TapG.g * W.g;
			HalationAccum.b += TapB.b * W.b;
			HalationWeight  += W;
		}
		
		HalationAccum /= HalationWeight;
		
		//Wavelength-dependent saturation and color tint
		float  HaloLuma = dot(HalationAccum, K_LUM);
		float3 HaloMono = float3(HaloLuma, HaloLuma, HaloLuma);
		HalationAccum = lerp(HaloMono, HalationAccum, IN.HL_Sat);
		HalationAccum *= IN.HL_Color;
		
		Result.rgb += HalationAccum * IN.HL_Int;
	}
	
	
	//---------- VEILING GLARE ----------//
	//Lens scatter: stray light from surface reflections and imperfections
	//Creates a low-frequency luminance veil across the whole image
	//Improved: Gaussian-weighted sampling for energy-conserving scatter
	
	[branch] if(IN.VG_Int > 0.0)
	{
		float3 GlareAccum = float3(0.0, 0.0, 0.0);
		float  GlareWeight = 0.0;
		
		//Multi-ring sampling: 3 concentric rings with Gaussian falloff
		//  Inner rings are more important (Gaussian peaks at center)
		[unroll] for(int ring = 0; ring < 3; ring++)
		{
			float RingNorm = (float)(ring + 1) / 3.0;
			//Gaussian weight: exp(-r²/2σ²) with σ=0.5 gives natural falloff
			float RingW = exp(-RingNorm * RingNorm * 2.0);
			
			[unroll] for(int g = 0; g < 8; g++)
			{
				float2 SampleUV = IN.texcoord + GlareRing8[g] * RadiusVG * RingNorm;
				float3 Tap = TextureColor.Sample(Linear_Sampler, SampleUV).rgb;
				
				float TapLuma = dot(Tap, K_LUM);
				float Excess  = max(TapLuma - IN.VG_Thresh, 0.0);
				//Soft knee: smooth transition instead of hard threshold
				float W = saturate(Excess * 2.0) * RingW;
				
				GlareAccum += Tap * W;
				GlareWeight += W;
			}
		}
		
		GlareAccum /= max(GlareWeight, DELTA);
		GlareAccum *= IN.VG_Tint;
		
		//Store veiling glare in alpha channel for compositing
		float GlareAmount = dot(GlareAccum, K_LUM) * IN.VG_Int;
		Result.rgb += GlareAccum * IN.VG_Int;
		Result.a    = saturate(GlareAmount);
	}
	
	return Result;
}





//----------------------------------------------------------------------------------------------//
//										   Techniques												//
//																								//
//----------------------------------------------------------------------------------------------//
//"love and hate are just two words for passion"


//-----------------------------------Atmospheric Fog Addon--------------------------------------//

#include "Addons/Effect_AtmosphericFog.fxh"

//--------------------------------------CRT Display Addon--------------------------------------//

#include "Addons/Effect_CRTShader.fxh"

//------------------------------------Cinematic FX Addon--------------------------------------//

#include "Addons/Effect_CinematicFX.fxh"


//-----------------------------------Compilation Optimization-----------------------------------//

VertexShader VS_GhostsPrePass_Comp   =   CompileShader(vs_5_0, VS_GhostsPrePass());
VertexShader VS_GhostsMain_Comp      =   CompileShader(vs_5_0, VS_GhostsMain());
VertexShader VS_GhostsBypass_Comp[3] = { CompileShader(vs_5_0, VS_GhostsBypass(float2( 1.0,1.0), float2(0.5,0.5))),
										 CompileShader(vs_5_0, VS_GhostsBypass(float2( 1.0,1.0), float2(0.5,0.5))),
										 CompileShader(vs_5_0, VS_GhostsBypass(float2(-1.0,0.0), float2(0.5,1.0))) };
VertexShader VS_GhostsMix_Comp       =   CompileShader(vs_5_0, VS_GhostsMix());
VertexShader VS_GhostsFullres_Comp   =   CompileShader(vs_5_0, VS_GhostsFullres());

PixelShader  PS_GhostsPrePass_Comp   =   CompileShader(ps_5_0, PS_GhostsPrePass());
PixelShader  PS_GhostsMain_Comp[5]   = { CompileShader(ps_5_0, PS_GhostsMain(4.0 / UIG_FNumber       )),
										 CompileShader(ps_5_0, PS_GhostsMain(4.0 / UIG_FNumber *  2.0)),
										 CompileShader(ps_5_0, PS_GhostsMain(4.0 / UIG_FNumber *  4.0)),
										 CompileShader(ps_5_0, PS_GhostsMain(4.0 / UIG_FNumber *  8.0)),
										 CompileShader(ps_5_0, PS_GhostsMain(4.0 / UIG_FNumber * 16.0)) };
PixelShader  PS_GhostsBypass_Comp    =   CompileShader(ps_5_0, PS_GhostsBypass());
PixelShader  PS_GhostsMix_Comp       =   CompileShader(ps_5_0, PS_GhostsMix());
PixelShader  PS_GhostsFullres_Comp   =   CompileShader(ps_5_0, PS_GhostsFullres());


VertexShader VS_StarburstPrePass_Comp     =   CompileShader(vs_5_0, VS_StarburstPrePass());
VertexShader VS_StarburstMain_Comp        =   CompileShader(vs_5_0, VS_StarburstMain());
VertexShader VS_StarburstPostPass_Comp    =   CompileShader(vs_5_0, VS_StarburstPostPass());

PixelShader  PS_StarburstPrePass_Comp     =   CompileShader(ps_5_0, PS_StarburstPrePass());
PixelShader  PS_StarburstMain_Comp        =   CompileShader(ps_5_0, PS_StarburstMain());
PixelShader  PS_StarburstPostPass_Comp[2] = { CompileShader(ps_5_0, PS_StarburstPostPass(false)),
										      CompileShader(ps_5_0, PS_StarburstPostPass(true )) };


VertexShader VS_Basic_Comp = CompileShader(vs_5_0, VS_Basic());
PixelShader  PS_Blank_Comp = CompileShader(ps_5_0, PS_Blank());
PixelShader  PS_Rain_Comp  = CompileShader(ps_5_0, PS_Rain());

VertexShader VS_RainBlur_Comp = CompileShader(vs_5_0, VS_RainBlur());
PixelShader  PS_RainBlur_Comp = CompileShader(ps_5_0, PS_RainBlur());

VertexShader VS_Reflection_Comp = CompileShader(vs_5_0, VS_Reflection());
PixelShader  PS_Reflection_Comp = CompileShader(ps_5_0, PS_Reflection());

VertexShader VS_AnamFlarePrePass_Comp = CompileShader(vs_5_0, VS_AnamFlarePrePass());
PixelShader  PS_AnamFlarePrePass_Comp = CompileShader(ps_5_0, PS_AnamFlarePrePass());
VertexShader VS_AnamFlare_Comp        = CompileShader(vs_5_0, VS_AnamFlare());
PixelShader  PS_AnamFlare_Comp        = CompileShader(ps_5_0, PS_AnamFlare());

VertexShader VS_DirtAndPostPass_Comp    =   CompileShader(vs_5_0, VS_DirtAndPostPass());
PixelShader  PS_DirtAndPostPass_Comp[2] = { CompileShader(ps_5_0, PS_DirtAndPostPass(false)),
										    CompileShader(ps_5_0, PS_DirtAndPostPass(true )) };

VertexShader VS_Frost_Comp = CompileShader(vs_5_0, VS_Frost());
PixelShader  PS_Frost_Comp = CompileShader(ps_5_0, PS_Frost());

VertexShader VS_FilmGrain_Comp = CompileShader(vs_5_0, VS_FilmGrain());
PixelShader  PS_FilmGrain_Comp = CompileShader(ps_5_0, PS_FilmGrain());

VertexShader VS_Halation_Comp = CompileShader(vs_5_0, VS_Halation());
PixelShader  PS_Halation_Comp = CompileShader(ps_5_0, PS_Halation());

VertexShader VS_Fog_Comp = CompileShader(vs_5_0, VS_Fog());
PixelShader  PS_Fog_Comp = CompileShader(ps_5_0, PS_Fog());

VertexShader VS_CRT_Comp = CompileShader(vs_5_0, VS_CRT());
PixelShader  PS_CRT_Comp = CompileShader(ps_5_0, PS_CRT());

VertexShader VS_CineFX_Comp     = CompileShader(vs_5_0, VS_CineFX());
PixelShader  PS_Diffusion_Comp  = CompileShader(ps_5_0, PS_Diffusion());
PixelShader  PS_FilmHalo_Comp   = CompileShader(ps_5_0, PS_FilmHalation());
VertexShader VS_LightLeaks_Comp = CompileShader(vs_5_0, VS_LightLeaks());
PixelShader  PS_LightLeaks_Comp = CompileShader(ps_5_0, PS_LightLeaks());
VertexShader VS_GateWeave_Comp  = CompileShader(vs_5_0, VS_GateWeave());
PixelShader  PS_GateWeave_Comp  = CompileShader(ps_5_0, PS_GateWeave());
PixelShader  PS_Letterbox_Comp  = CompileShader(ps_5_0, PS_Letterbox());
VertexShader VS_Anamorphic_Comp  = CompileShader(vs_5_0, VS_Anamorphic());
PixelShader  PS_Anamorphic_Comp   = CompileShader(ps_5_0, PS_Anamorphic());
VertexShader VS_OptVignette_Comp  = CompileShader(vs_5_0, VS_OptVignette());
PixelShader  PS_OptVignette_Comp  = CompileShader(ps_5_0, PS_OptVignette());
VertexShader VS_FilmDamage_Comp   = CompileShader(vs_5_0, VS_FilmDamage());
PixelShader  PS_FilmDamage_Comp   = CompileShader(ps_5_0, PS_FilmDamage());



//---------------------------------------Technique Macros---------------------------------------//

#undef  TECH11
#define TECH11(NAME, VS, PS) \
technique11 NAME {pass p0 {SetVertexShader(VS); SetPixelShader (PS);}}

#undef  TWOPASSTECH11
#define TWOPASSTECH11(NAME, VS1, PS1, VS2, PS2) \
technique11 NAME {pass p0 {SetVertexShader(VS1); SetPixelShader (PS1);}\
				  pass p1 {SetVertexShader(VS2); SetPixelShader (PS2);}}


#define GHOSTS_TECHBLOCK(a) \
TECH11        (COMBINE(a,1), VS_GhostsMain_Comp,      PS_GhostsMain_Comp[0]) \
TECH11        (COMBINE(a,2), VS_GhostsMain_Comp,      PS_GhostsMain_Comp[1]) \
TWOPASSTECH11 (COMBINE(a,3), VS_GhostsMain_Comp,      PS_GhostsMain_Comp[2], \
						     VS_GhostsBypass_Comp[0], PS_GhostsBypass_Comp)  \
TWOPASSTECH11 (COMBINE(a,4), VS_GhostsMain_Comp,      PS_GhostsMain_Comp[3], \
						     VS_GhostsBypass_Comp[1], PS_GhostsBypass_Comp)  \
TWOPASSTECH11 (COMBINE(a,5), VS_GhostsMain_Comp,      PS_GhostsMain_Comp[4], \
						     VS_GhostsBypass_Comp[2], PS_GhostsBypass_Comp)  \
TECH11        (COMBINE(a,6), VS_GhostsMix_Comp,       PS_GhostsMix_Comp)

#define STARBURST_TECHBLOCK(a,b1,b2,b3,b4,b5) \
TECH11        (COMBINE(a,b1), VS_Basic_Comp,			 PS_Blank_Comp) \
TWOPASSTECH11 (COMBINE(a,b2), VS_Basic_Comp,			 PS_Blank_Comp, \
						      VS_StarburstPrePass_Comp,	 PS_StarburstPrePass_Comp) \
TECH11        (COMBINE(a,b3), VS_StarburstMain_Comp,	 PS_StarburstMain_Comp) \
TECH11        (COMBINE(a,b4), VS_StarburstPostPass_Comp, PS_StarburstPostPass_Comp[b5])

#define RAINPASS(PN,VS) \
pass PN{ SetVertexShader(CompileShader(vs_5_0, VS_Rain##VS)); \
		 SetPixelShader (PS_Rain_Comp); \
		 SetBlendState  (RainBlending, float4(1.0,1.0,1.0,1.0), 0xFFFFFFFF); }

#define CLEARPASS(PN) \
pass PN{ SetVertexShader(VS_Basic_Comp);\
		 SetPixelShader (PS_Blank_Comp);}

#define RAINTECH(NAME) \
technique11 NAME <string RenderTarget="RenderTargetRGBA32";> \
{ CLEARPASS(Clear) \
  RAINPASS(Pass0,  (float4(967.0, 296.0, 477.0, 806.0),       0.0))  RAINPASS(Pass1,  (float4( 63.0, 278.0, 501.0, 392.0),  1.0/16.0)) \
  RAINPASS(Pass2,  (float4(615.0, 735.0, 628.0, 128.0),  2.0/16.0))  RAINPASS(Pass3,  (float4(490.0, 339.0, 887.0, 289.0),  3.0/16.0)) \
  RAINPASS(Pass4,  (float4(665.0, 708.0, 408.0, 518.0),  4.0/16.0))  RAINPASS(Pass5,  (float4(173.0, 683.0, 784.0, 453.0),  5.0/16.0)) \
  RAINPASS(Pass6,  (float4( 72.0, 866.0,  83.0, 292.0),  6.0/16.0))  RAINPASS(Pass7,  (float4(493.0, 338.0, 694.0, 133.0),  7.0/16.0)) \
  RAINPASS(Pass8,  (float4(217.0, 612.0, 251.0, 867.0),  8.0/16.0))  RAINPASS(Pass9,  (float4(368.0, 199.0, 834.0, 959.0),  9.0/16.0)) \
  RAINPASS(Pass10, (float4(567.0, 913.0, 780.0, 545.0), 10.0/16.0))  RAINPASS(Pass11, (float4(649.0, 764.0, 304.0, 620.0), 11.0/16.0)) \
  RAINPASS(Pass12, (float4(924.0, 104.0, 226.0, 849.0), 12.0/16.0))  RAINPASS(Pass13, (float4(993.0, 495.0, 320.0, 382.0), 13.0/16.0)) \
  RAINPASS(Pass14, (float4(352.0, 472.0, 213.0, 382.0), 14.0/16.0))  RAINPASS(Pass15, (float4( 67.0, 144.0,  16.0, 861.0), 15.0/16.0)) }


//----------------------------------Technique with everything-----------------------------------//

TECH11			    (KitsuuneMasterLensA   <string UIName="Master Lens";>,				  VS_GhostsPrePass_Comp,    PS_GhostsPrePass_Comp)
GHOSTS_TECHBLOCK    (KitsuuneMasterLensA)
TECH11			    (KitsuuneMasterLensA7  <string RenderTarget="RenderTarget512";>,      VS_Reflection_Comp,	    PS_Reflection_Comp)
TWOPASSTECH11	    (KitsuuneMasterLensA8  <string RenderTarget="RenderTargetRGBA64F";>,  VS_Basic_Comp,		    PS_Blank_Comp,
																						  VS_AnamFlarePrePass_Comp, PS_AnamFlarePrePass_Comp)
TECH11			    (KitsuuneMasterLensA9  <string RenderTarget="RenderTarget256";>,      VS_AnamFlare_Comp,	    PS_AnamFlare_Comp)
TECH11			    (KitsuuneMasterLensA10 <string RenderTarget="RenderTargetRGBA64F";>,  VS_GhostsFullres_Comp,    PS_GhostsFullres_Comp)
STARBURST_TECHBLOCK (KitsuuneMasterLensA,11,12,13,14,1)
TECH11			    (KitsuuneMasterLensA15,												  VS_DirtAndPostPass_Comp,  PS_DirtAndPostPass_Comp[1])
RAINTECH		    (KitsuuneMasterLensA16)
TECH11			    (KitsuuneMasterLensA17 <string RenderTarget="RenderTargetRGBA64F";>,  VS_RainBlur_Comp,		    PS_RainBlur_Comp)
TECH11			    (KitsuuneMasterLensA18 <string RenderTarget="RenderTarget512";>,      VS_Halation_Comp,		    PS_Halation_Comp)
TECH11			    (KitsuuneMasterLensA19,												  VS_Frost_Comp,		    PS_Frost_Comp)
TECH11			    (KitsuuneMasterLensA20,												  VS_Fog_Comp,			    PS_Fog_Comp)
TECH11			    (KitsuuneMasterLensA21,												  VS_CineFX_Comp,		    PS_Diffusion_Comp)
TECH11			    (KitsuuneMasterLensA22,												  VS_CineFX_Comp,		    PS_FilmHalo_Comp)
TECH11			    (KitsuuneMasterLensA23,												  VS_LightLeaks_Comp,	    PS_LightLeaks_Comp)
TECH11			    (KitsuuneMasterLensA24,												  VS_GateWeave_Comp,	    PS_GateWeave_Comp)
TECH11			    (KitsuuneMasterLensA25,												  VS_Anamorphic_Comp,	    PS_Anamorphic_Comp)
TECH11			    (KitsuuneMasterLensA26,												  VS_OptVignette_Comp,	    PS_OptVignette_Comp)
TECH11			    (KitsuuneMasterLensA27,												  VS_CRT_Comp,			    PS_CRT_Comp)
TECH11			    (KitsuuneMasterLensA28,												  VS_FilmDamage_Comp,	    PS_FilmDamage_Comp)
TECH11			    (KitsuuneMasterLensA29,												  VS_CineFX_Comp,		    PS_Letterbox_Comp)
TECH11			    (KitsuuneMasterLensA30,												  VS_FilmGrain_Comp,	    PS_FilmGrain_Comp)


//-----------------------------------Technique without Ghosts-----------------------------------//

TECH11				   (KitsuuneMasterLensB  <string UIName="Master Lens - No Ghosts";
										  string RenderTarget="RenderTarget512";>,		  VS_Reflection_Comp,	    PS_Reflection_Comp)
TWOPASSTECH11	    (KitsuuneMasterLensB1 <string RenderTarget="RenderTargetRGBA64F";>,	  VS_Basic_Comp,		    PS_Blank_Comp,
																						  VS_AnamFlarePrePass_Comp, PS_AnamFlarePrePass_Comp)
TECH11			    (KitsuuneMasterLensB2 <string RenderTarget="RenderTarget256";>,		  VS_AnamFlare_Comp,	    PS_AnamFlare_Comp)
STARBURST_TECHBLOCK (KitsuuneMasterLensB,3,4,5,6,0)
TECH11			    (KitsuuneMasterLensB7,												  VS_DirtAndPostPass_Comp, PS_DirtAndPostPass_Comp[1])
RAINTECH		    (KitsuuneMasterLensB8)
TECH11			    (KitsuuneMasterLensB9 <string RenderTarget="RenderTargetRGBA64F";>,	  VS_RainBlur_Comp,		   PS_RainBlur_Comp)
TECH11			    (KitsuuneMasterLensB10 <string RenderTarget="RenderTarget512";>,	  VS_Halation_Comp,		   PS_Halation_Comp)
TECH11			    (KitsuuneMasterLensB11,												  VS_Frost_Comp,		   PS_Frost_Comp)
TECH11			    (KitsuuneMasterLensB12,												  VS_Fog_Comp,			   PS_Fog_Comp)
TECH11			    (KitsuuneMasterLensB13,												  VS_CineFX_Comp,		   PS_Diffusion_Comp)
TECH11			    (KitsuuneMasterLensB14,												  VS_CineFX_Comp,		   PS_FilmHalo_Comp)
TECH11			    (KitsuuneMasterLensB15,												  VS_LightLeaks_Comp,	   PS_LightLeaks_Comp)
TECH11			    (KitsuuneMasterLensB16,												  VS_GateWeave_Comp,	   PS_GateWeave_Comp)
TECH11			    (KitsuuneMasterLensB17,												  VS_Anamorphic_Comp,	   PS_Anamorphic_Comp)
TECH11			    (KitsuuneMasterLensB18,												  VS_OptVignette_Comp,	   PS_OptVignette_Comp)
TECH11			    (KitsuuneMasterLensB19,												  VS_CRT_Comp,			   PS_CRT_Comp)
TECH11			    (KitsuuneMasterLensB20,												  VS_FilmDamage_Comp,	   PS_FilmDamage_Comp)
TECH11			    (KitsuuneMasterLensB21,												  VS_CineFX_Comp,		   PS_Letterbox_Comp)
TECH11			    (KitsuuneMasterLensB22,												  VS_FilmGrain_Comp,	   PS_FilmGrain_Comp)


//---------------------------------Technique without StarBurst----------------------------------//

TECH11			    (KitsuuneMasterLensC   <string UIName="Master Lens - No Starburst";>, VS_GhostsPrePass_Comp,    PS_GhostsPrePass_Comp)
GHOSTS_TECHBLOCK    (KitsuuneMasterLensC)
TECH11			    (KitsuuneMasterLensC7,												  VS_GhostsFullres_Comp,    PS_GhostsFullres_Comp)
TECH11			    (KitsuuneMasterLensC8  <string RenderTarget="RenderTarget512";>,      VS_Reflection_Comp,	    PS_Reflection_Comp)
TWOPASSTECH11	    (KitsuuneMasterLensC9  <string RenderTarget="RenderTargetRGBA64F";>,  VS_Basic_Comp,		    PS_Blank_Comp,
																						  VS_AnamFlarePrePass_Comp, PS_AnamFlarePrePass_Comp)
TECH11			    (KitsuuneMasterLensC10 <string RenderTarget="RenderTarget256";>,	  VS_AnamFlare_Comp,	    PS_AnamFlare_Comp)
TECH11			    (KitsuuneMasterLensC11,												  VS_DirtAndPostPass_Comp,  PS_DirtAndPostPass_Comp[1])
RAINTECH		    (KitsuuneMasterLensC12)
TECH11			    (KitsuuneMasterLensC13 <string RenderTarget="RenderTargetRGBA64F";>,  VS_RainBlur_Comp,		    PS_RainBlur_Comp)
TECH11			    (KitsuuneMasterLensC14 <string RenderTarget="RenderTarget512";>,  VS_Halation_Comp,		    PS_Halation_Comp)
TECH11			    (KitsuuneMasterLensC15,												  VS_Frost_Comp,		    PS_Frost_Comp)
TECH11			    (KitsuuneMasterLensC16,												  VS_Fog_Comp,			    PS_Fog_Comp)
TECH11			    (KitsuuneMasterLensC17,												  VS_CineFX_Comp,		    PS_Diffusion_Comp)
TECH11			    (KitsuuneMasterLensC18,												  VS_CineFX_Comp,		    PS_FilmHalo_Comp)
TECH11			    (KitsuuneMasterLensC19,												  VS_LightLeaks_Comp,	    PS_LightLeaks_Comp)
TECH11			    (KitsuuneMasterLensC20,												  VS_GateWeave_Comp,	    PS_GateWeave_Comp)
TECH11			    (KitsuuneMasterLensC21,												  VS_Anamorphic_Comp,	    PS_Anamorphic_Comp)
TECH11			    (KitsuuneMasterLensC22,												  VS_OptVignette_Comp,	    PS_OptVignette_Comp)
TECH11			    (KitsuuneMasterLensC23,												  VS_CRT_Comp,			    PS_CRT_Comp)
TECH11			    (KitsuuneMasterLensC24,												  VS_FilmDamage_Comp,	    PS_FilmDamage_Comp)
TECH11			    (KitsuuneMasterLensC25,												  VS_CineFX_Comp,		    PS_Letterbox_Comp)
TECH11			    (KitsuuneMasterLensC26,												  VS_FilmGrain_Comp,	    PS_FilmGrain_Comp)


//----------------------------Technique without Ghosts and StarBurst----------------------------//

TECH11			    (KitsuuneMasterLensD  <string UIName="Master Lens - No Ghosts - No Starburst";
										  string RenderTarget="RenderTarget512";>,		  VS_Reflection_Comp,	    PS_Reflection_Comp)
TWOPASSTECH11	    (KitsuuneMasterLensD1 <string RenderTarget="RenderTargetRGBA64F";>,	  VS_Basic_Comp,		    PS_Blank_Comp,
																						  VS_AnamFlarePrePass_Comp, PS_AnamFlarePrePass_Comp)
TECH11			    (KitsuuneMasterLensD2 <string RenderTarget="RenderTarget256";>,		  VS_AnamFlare_Comp,	    PS_AnamFlare_Comp)
TECH11			    (KitsuuneMasterLensD3,												  VS_DirtAndPostPass_Comp,  PS_DirtAndPostPass_Comp[0])
RAINTECH		    (KitsuuneMasterLensD4)
TECH11			    (KitsuuneMasterLensD5 <string RenderTarget="RenderTargetRGBA64F";>,	  VS_RainBlur_Comp,		    PS_RainBlur_Comp)
TECH11			    (KitsuuneMasterLensD6  <string RenderTarget="RenderTarget512";>,	  VS_Halation_Comp,		    PS_Halation_Comp)
TECH11			    (KitsuuneMasterLensD7,												  VS_Frost_Comp,		    PS_Frost_Comp)
TECH11			    (KitsuuneMasterLensD8,												  VS_Fog_Comp,			    PS_Fog_Comp)
TECH11			    (KitsuuneMasterLensD9,												  VS_CineFX_Comp,		    PS_Diffusion_Comp)
TECH11			    (KitsuuneMasterLensD10,												  VS_CineFX_Comp,		    PS_FilmHalo_Comp)
TECH11			    (KitsuuneMasterLensD11,												  VS_LightLeaks_Comp,	    PS_LightLeaks_Comp)
TECH11			    (KitsuuneMasterLensD12,												  VS_GateWeave_Comp,	    PS_GateWeave_Comp)
TECH11			    (KitsuuneMasterLensD13,												  VS_Anamorphic_Comp,	    PS_Anamorphic_Comp)
TECH11			    (KitsuuneMasterLensD14,												  VS_OptVignette_Comp,	    PS_OptVignette_Comp)
TECH11			    (KitsuuneMasterLensD15,												  VS_CRT_Comp,			    PS_CRT_Comp)
TECH11			    (KitsuuneMasterLensD16,												  VS_FilmDamage_Comp,	    PS_FilmDamage_Comp)
TECH11			    (KitsuuneMasterLensD17,												  VS_CineFX_Comp,		    PS_Letterbox_Comp)
TECH11			    (KitsuuneMasterLensD18,												  VS_FilmGrain_Comp,	    PS_FilmGrain_Comp)

