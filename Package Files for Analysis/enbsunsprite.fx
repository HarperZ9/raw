//----------------------------------------------------------------------------------------------//
//																								//
//						enbsunsprite.fx file by LonelyKitsuune aka Skratzer						//
//						   for Skyrim SE ENB (DirectX 11 Shader Model 5)						//
//																								//
//			   Copyright (c) 2019-2020 LonelyKitsuune / T.Thanner - CC BY-NC-ND 4.0				//
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
//Enables a more accurate intensity and sun color detection
#define USE_CUSTOM_SUNINTENSITY_DETECTOR	1 //[0-1]
#define ENABLE_DETECTOR_DEPTH_TESTING		0 //[0-1]

//SkyrimBridge: Use game-engine sun color instead of screen sampling
//Requires SkyrimBridge SKSE plugin + SkyrimBridge.fxh in Helper/
#define USE_SKYRIMBRIDGE_SUNCOLOR			1 //[0-1] Blend game sun color with detected color
#define USE_SKYRIMBRIDGE_LIGHTNING			1 //[0-1] Flash all flares during lightning strikes
#define SKYRIMBRIDGE_SUNCOLOR_BLEND			0.6 //[0-1] How much game color vs detected (1=full game)


//Compute randomness pattern with subpixel precision to
//produce a "supersampled" or blurred result
#define ENABLE_HOOP_ANTIALIASING			1 //[0-1]
#define ENABLE_STARBURST_ANTIALIASING		1 //[0-1]


//----------------------------------------------------------------------------------------------//
//										Internal Parameters										//
//----------------------------------------------------------------------------------------------//

#include "UI/enbUI_Primer.fxh"


//Lens settings with single-section-UI
#define SHADERGROUP 0
#include "UI/enbUI_SunSprite.fxh"
UI_WHITESPACE(14)
UI_WHITESPACE(15)


//Anamorphic Lens Flare
#define SHADERGROUP 1
#define PASS 1
#define SWS  1
#include "UI/enbUI_SunSprite.fxh"

#define PASS 2
#define SWS  2
#include "UI/enbUI_SunSprite.fxh"

#define PASS 3
#define SWS  3
#include "UI/enbUI_SunSprite.fxh"

UI_WHITESPACE(16)
UI_WHITESPACE(17)
#undef SHADERGROUP


//Ghost Lens Flare (first single then double)
#define SHADERGROUP 2
#define PASS 1
#define SWS  4
#include "UI/enbUI_SunSprite.fxh"

#define PASS 2
#define SWS  5
#include "UI/enbUI_SunSprite.fxh"

#define PASS 3
#define SWS  6
#include "UI/enbUI_SunSprite.fxh"

#define PASS 4
#define SWS  7
#include "UI/enbUI_SunSprite.fxh"

#define PASS 5
#define SWS  8
#include "UI/enbUI_SunSprite.fxh"

#define PASS 6
#define SWS  9
#include "UI/enbUI_SunSprite.fxh"

#define PASS 7
#define SWS  10
#include "UI/enbUI_SunSprite.fxh"

#define PASS 8
#define SWS  11
#include "UI/enbUI_SunSprite.fxh"

#define PASS 9
#define SWS  12
#include "UI/enbUI_SunSprite.fxh"
#undef SHADERGROUP


#define SHADERGROUP 3
#define PASS 1
#define SWS  13
#include "UI/enbUI_SunSprite.fxh"

#define PASS 2
#define SWS  14
#include "UI/enbUI_SunSprite.fxh"

#define PASS 3
#define SWS  15
#include "UI/enbUI_SunSprite.fxh"

#define PASS 4
#define SWS  16
#include "UI/enbUI_SunSprite.fxh"

#define PASS 5
#define SWS  17
#include "UI/enbUI_SunSprite.fxh"

#define PASS 6
#define SWS  18
#include "UI/enbUI_SunSprite.fxh"

#define PASS 7
#define SWS  19
#include "UI/enbUI_SunSprite.fxh"

#define PASS 8
#define SWS  20
#include "UI/enbUI_SunSprite.fxh"
#undef SHADERGROUP


//Aureole, Corona, and Spectral Starburst
#define SHADERGROUP 4
#include "UI/enbUI_SunSprite.fxh"
#undef SHADERGROUP

//Ice Crystal Halos and Sun Dogs
#define SHADERGROUP 5
#include "UI/enbUI_SunSprite.fxh"
#undef SHADERGROUP

//Veiling Glare, Eyelash Diffraction, Sensor Bloom Cross
#define SHADERGROUP 6
#include "UI/enbUI_SunSprite.fxh"
#undef SHADERGROUP

//=============================================================================//
//  CINEMATIC & REALISM EXTENSIONS                                             //
//=============================================================================//

//Dirty Lens Coating: procedural dust and smudge overlay
#define SHADERGROUP 7
#include "UI/enbUI_SunSprite.fxh"
#undef SHADERGROUP

//=============================================================================//
//  INLINE UI — CINEMATIC FLARE EXTENSIONS                                     //
//=============================================================================//

// ─────────────────── Dirty Lens Coating ────────────────────────────────────
//  Procedural dust particles, fingerprint smudges, and micro-scratches
//  on the lens surface that scatter and diffract incoming light into
//  a textured flare overlay. Most visible when sun is near frame center.
int _spcDL1 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrDIRT < string UIName = "======= DIRTY LENS COATING ======="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool  UIDirt_Enable       < string UIName = "Dirt | Enable"; > = false;
float UIDirt_Intensity    < string UIName = "Dirt | Intensity";       string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0;  float UIStep = 0.01; > = 0.35;
float UIDirt_Density      < string UIName = "Dirt | Particle Density"; string UIWidget = "spinner"; float UIMin = 5.0; float UIMax = 40.0; float UIStep = 1.0;  > = 20.0;
float UIDirt_SmudgeAmt    < string UIName = "Dirt | Smudge Amount";   string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0;  float UIStep = 0.01; > = 0.2;
float UIDirt_ScratchAmt   < string UIName = "Dirt | Scratch Amount";  string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0;  float UIStep = 0.01; > = 0.1;
float UIDirt_ChromaSpread < string UIName = "Dirt | Chromatic Spread"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.3;

// ─────────────────── Thin-Film Lens Coating Reflections ────────────────────
//  Multi-layer anti-reflective coatings create colored reflections when
//  light hits the lens at oblique angles — the cause of "green/purple
//  lens flare tint" in real photography. Uses Fabry-Pérot thin-film
//  interference model for wavelength-dependent reflectance.
int _spcTF1 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrTF < string UIName = "======= THIN-FILM COATING ======="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool  UITF_Enable         < string UIName = "TF | Enable"; > = true;
float UITF_Intensity      < string UIName = "TF | Intensity";         string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0;  float UIStep = 0.01; > = 0.2;
float UITF_CoatingThick   < string UIName = "TF | Coating Thickness"; string UIWidget = "spinner"; float UIMin = 50.0; float UIMax = 500.0; float UIStep = 5.0; > = 200.0;
float UITF_IOR            < string UIName = "TF | Coating IOR";       string UIWidget = "spinner"; float UIMin = 1.2; float UIMax = 2.5;  float UIStep = 0.01; > = 1.5;
float3 UITF_BaseTint      < string UIName = "TF | Base Tint"; string UIWidget = "color"; > = {0.4, 1.0, 0.6};

// ─────────────────── Atmospheric Extinction ────────────────────────────────
//  Flares dim and shift warm when the sun is near the horizon due to
//  increased atmospheric path length. Models Beer-Lambert extinction
//  with wavelength-dependent Rayleigh scattering.
int _spcAE1 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrAEXT < string UIName = "======= ATMOSPHERIC EXTINCTION ======="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool  UIExt_Enable        < string UIName = "Ext | Enable"; > = true;
float UIExt_Strength      < string UIName = "Ext | Extinction Amount"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 0.5;
float UIExt_WarmShift     < string UIName = "Ext | Warm Color Shift";  string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.4;

// ─────────────────── Weather Flare Dampening ───────────────────────────────
//  Overcast/rain/fog weather reduces flare visibility.
//  Uses ENBSeries weather transition system for smooth changes.
int _spcWD1 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrWDMP < string UIName = "======= WEATHER DAMPENING ======="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool  UIWDamp_Enable      < string UIName = "WD | Enable"; > = false;
float UIWDamp_CloudDamp   < string UIName = "WD | Cloud Dampening";  string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.7;
float UIWDamp_RainDamp    < string UIName = "WD | Rain Dampening";   string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.9;

// ─────────────────── Cinematic Flare Aging ─────────────────────────────────
//  Applies a color grade to all flare elements for cohesive cinematic look.
//  Simulates aged or specialty lens coatings (warm vintage, cool clinical).
int _spcFA1 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrFAGE < string UIName = "======= FLARE COLOR GRADE ======="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool  UIFAge_Enable       < string UIName = "FCG | Enable"; > = false;
float UIFAge_Temperature  < string UIName = "FCG | Temperature (-1=cool, 1=warm)"; string UIWidget = "spinner"; float UIMin = -1.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.2;
float UIFAge_Saturation   < string UIName = "FCG | Saturation";      string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 1.0;
float UIFAge_Contrast     < string UIName = "FCG | Contrast";        string UIWidget = "spinner"; float UIMin = 0.5; float UIMax = 2.0; float UIStep = 0.01; > = 1.0;



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
float4 LightParameters; // xy = sun position on screen, w = visibility


//----------------------------------------------------------------------------------------------//
//						       External enb debugging parameters for								//
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
//                         SkyrimBridge — Inline Parameters Only                                //
//                                                                                              //
//   Only the 3 float4 params actually referenced by sunsprite are declared.                    //
//----------------------------------------------------------------------------------------------//

#ifndef SKYRIMBRIDGE_FXH
#define SKYRIMBRIDGE_FXH 1

float4 SB_Render_Frame;      // .x = frameCount, .y = deltaTime, .z = screenW, .w = screenH
float4 SB_Sun_Color;         // .rgb = sun color (linear), .a = sun intensity
float4 SB_Lightning;         // .x = frequency, .y = isFlashing, .z = flashIntensity, .w = timeSince

bool SB_IsActive() { return SB_Render_Frame.x > 0.0; }

#endif


//----------------------------------------------------------------------------------------------//
//						     Game and mod parameters, do not modify								//
//																								//
//----------------------------------------------------------------------------------------------//
Texture2D TextureMask;	//Mask of sun as visibility factor
Texture2D TextureColor;	//Scene color R16B16G16A16 64 bit hdr format
Texture2D TextureDepth;	//Scene depth R32F 32 bit hdr format

Texture2D LensDirtAtlas <string ResourceName="Textures/LensDirtTexAtlas.png";>;

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

#if ENABLE_DETECTOR_DEPTH_TESTING
SamplerComparisonState Linear_Sampler_Eql
{
	Filter = COMPARISON_MIN_MAG_MIP_LINEAR;
	AddressU = Clamp;
	AddressV = Clamp;
	ComparisonFunc = EQUAL;
};
#endif


//Helper with some useful macros, variables and functions
#include "Helper/enbHelper_Common.fxh"
static const float2 PixelSize = _HLP_PixelSize;
static const float2 ScreenRes = _HLP_ScreenRes;


//----------------------------------------------------------------------------------------------//
//										   Structs												//
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

struct VertexShaderOutputGSprite
{
   float4 pos                  : SV_POSITION;
   float4 texcoord             : TEXCOORD0;
NI float  SunInt               : SPRITE0;
NI float  RoundingRadius       : SPRITE1;
NI float2 CAOffsetVec          : SPRITE2;
NI float3 FlareTint            : SPRITE3;
NI float  Scale                : SPRITE4;
NI float2 ApertureVertices[10] : SPRITE5;
};

struct VertexShaderOutputSBSprite
{
   float4 pos                  : SV_POSITION;
   float4 texcoord             : TEXCOORD0;
NI float  SunInt               : SPRITE0;
NI float3 FlareTint            : SPRITE1;
NI bool   OddShape             : SPRITE2;
NI float  SafeZone             : SPRITE3;
NI float2 ApertureVertices[10] : SPRITE4;
};

struct VertexShaderOutputHoopSprite
{
   float4 pos                  : SV_POSITION;
   float4 texcoord             : TEXCOORD0;
NI float  SunInt               : SPRITE0;
NI float  RoundingRadius       : SPRITE1;
NI float2 CAOffsetVec          : SPRITE2;
NI float3 FlareTint            : SPRITE3;
NI float  MaxPossibleVertDist  : SPRITE4;
NI float2 ApertureVertices[10] : SPRITE5;
};

struct VertexShaderOutputAnamSprite
{
   float4 pos       : SV_POSITION;
   float4 texcoord  : TEXCOORD0;
NI float  SunInt    : SPRITE0;
NI float3 FlareTint : SPRITE3;
};

struct VertexShaderOutputLensGlareSprite
{
   float4 pos      : SV_POSITION;
   float4 texcoord : TEXCOORD0;
NI float4 Glare    : SPRITE0;
NI float  SunDist  : SPRITE1;
NI float  MinRatio : SPRITE2;
NI float  MaxRatio : SPRITE3;
};

struct VertexShaderOutputSunGlareSprite
{
   float4 pos      : SV_POSITION;
   float4 texcoord : TEXCOORD0;
NI float4 Glare    : SPRITE0;
};

struct VertexShaderOutputAureole
{
   float4 pos      : SV_POSITION;
   float4 texcoord : TEXCOORD0;   //xy = UV, zw = centered [-1,1]
NI float  SunInt   : AUR0;
NI float3 SunCol   : AUR1;
NI float  GoldenHr : AUR2;        //golden hour warmth factor [0,1]
NI float  Breathe  : AUR3;        //organic temporal animation
};

struct VertexShaderOutputCorona
{
   float4 pos      : SV_POSITION;
   float4 texcoord : TEXCOORD0;
NI float  SunInt   : COR0;
NI float3 SunCol   : COR1;
};

struct VertexShaderOutputHalo
{
   float4 pos       : SV_POSITION;
   float4 texcoord  : TEXCOORD0;    //xy = UV, zw = centered [-1,1]
NI float  SunInt    : HALO0;
NI float3 SunCol    : HALO1;
NI float  AngRadius : HALO2;        //ring center in texcoord-normalized space
};

struct VertexShaderOutputSunDog
{
   float4 pos      : SV_POSITION;
   float4 texcoord : TEXCOORD0;
NI float  SunInt   : DOG0;
NI float3 SunCol   : DOG1;
};

struct VertexShaderOutputVeil
{
   float4 pos      : SV_POSITION;
   float2 texcoord : TEXCOORD0;     //screen UV [0,1]
NI float  SunInt   : VEIL0;
NI float3 SunCol   : VEIL1;
NI float2 SunUV    : VEIL2;         //sun position in UV space
};

struct VertexShaderOutputEyelash
{
   float4 pos      : SV_POSITION;
   float4 texcoord : TEXCOORD0;
NI float  SunInt   : LASH0;
NI float3 SunCol   : LASH1;
};

struct VertexShaderOutputBloomCross
{
   float4 pos      : SV_POSITION;
   float4 texcoord : TEXCOORD0;
NI float  SunInt   : XBLM0;
NI float3 SunCol   : XBLM1;
};


//----------------------------------------------------------------------------------------------//
//										    Functions											//
//																								//
//----------------------------------------------------------------------------------------------//

//Lowest sun intensity before shaders are skipped
#define SUN_INTENSITY_CLIP 0.01


float3 DiffractionF3(float3 x, float freq, float phase, float ampli)
{
	float3 sinc = PI * (x * freq + phase) + DELTA;
		   sinc = sin(sinc) / sinc;
	return sinc * sinc * ampli;
}

static float GetSunIntensity(float FlareInt, out float3 SunCol)
{
	#if USE_CUSTOM_SUNINTENSITY_DETECTOR
		static const float2 offset[5] = { 0.0,0.0, 0.0,-1.5, -1.5,0.0, 1.5,0.0, 0.0,1.5 };
		
		float2 SunUV  = LightParameters.xy * float2(0.5, -0.5) + 0.5;
		float3 AvgCol = 0.0;
		
		[unroll] for(int i=0; i<5; i++)
		{
			float2 CurrCoords = SunUV + offset[i] * PixelSize;
			float3 CurrSample = TextureColor.SampleLevel(Linear_Sampler, CurrCoords,0).rgb;
			
			#if ENABLE_DETECTOR_DEPTH_TESTING
				CurrSample *= TextureDepth.SampleCmpLevelZero(Linear_Sampler_Eql, CurrCoords, 1.0).x;
			#endif
			
			AvgCol += CurrSample;
		}
		
		AvgCol *= 0.2;
		SunCol  = AvgCol / (AvgCol + 1.0);
		
		float RawLum  = dot(AvgCol, N_LUM);
		float MaskVis = saturate(TextureMask.Load(0).x * LightParameters.w);
		
		//--- DAY (sun): luminance + mask, original behavior ---
		float DayInt  = pow(saturate(RawLum * MaskVis / 2.5), 4.0);
		
		//--- NIGHT (moon): time-of-day + depth occlusion ---
		//
		//  Previous approaches tried to detect the moon by sampling
		//  TextureColor luminance.  This fails because:
		//    - TextureMask / LightParameters.w are sun-specific (≈0 at night)
		//    - The moon's tonemapped luminance varies wildly by ENB preset
		//      (0.001 to 0.1+), making any fixed threshold unreliable
		//
		//  Robust approach:
		//    1. ENightDayFactor → it's night, moon should produce flare
		//    2. UV bounds → moon is on screen (reject edge-clamped samples)
		//    3. Depth at light pos → sky means visible, geometry means occluded
		//    4. RawLum as optional boost → brighter moon = slightly stronger flare
		
		//  UV screen bounds: smooth fade at edges
		float2 EdgeDist = min(SunUV, 1.0 - SunUV);
		float  OnScreen = saturate(min(EdgeDist.x, EdgeDist.y) * 20.0);
		
		//  Depth occlusion: sample scene depth at the light position.
		//  Sky/far-plane → depth ≈ 1.0 → moon visible.
		//  Terrain/building in front → depth < 1.0 → moon hidden.
		//  5-tap average for sub-pixel stability (moon disc spans few pixels).
		float DepthVis = 0.0;
		[unroll] for(int d=0; d<5; d++)
		{
			float2 DepthCoords = SunUV + offset[d] * PixelSize;
			DepthVis += TextureDepth.SampleLevel(Point_Sampler, DepthCoords, 0).x;
		}
		DepthVis *= 0.2;
		//  Skyrim depth: 1.0 = far plane (sky). Threshold at 0.9999 to
		//  accommodate slight depth buffer precision issues at max range.
		//  Smoothstep gives clean transition when moon grazes mountain edges.
		DepthVis = smoothstep(0.9970, 0.9999, DepthVis);
		
		//  Night factor: controls day→night flare transition sharpness
		//  UIMoon_Curve > 1.0 = faster onset, < 1.0 = more gradual
		float NightFactor = pow(1.0 - ENightDayFactor, 1.0 + UIMoon_Curve);
		
		//  Optional luminance boost: brighter moon = slightly stronger flare,
		//  but NOT required for detection (floor of 0.5 ensures visibility)
		float LumBoost = saturate(RawLum / UIMoon_Sensitivity);
		LumBoost = lerp(0.5, 1.0, LumBoost);
		
		float NightInt = NightFactor * LumBoost * UIMoon_IntMult * UIMoon_Enable;
		NightInt *= OnScreen * DepthVis;
		
		//  Moon color: if scene sample is too dim, fall back to tint
		//  so the flare is always properly tinted blue-white
		float3 MoonTint = UIMoon_Tint / max(dot(UIMoon_Tint, N_LUM), DELTA);
		float  ColLum   = dot(SunCol, N_LUM);
		float3 MoonCol  = (ColLum > 0.01) ? SunCol * MoonTint : MoonTint;
		
		//--- BLEND ---
		float Intensity = lerp(NightInt, DayInt, ENightDayFactor);
		SunCol = lerp(MoonCol, SunCol, ENightDayFactor);

		//--- SKYRIMBRIDGE: blend in game-engine sun color ---
		#if USE_SKYRIMBRIDGE_SUNCOLOR && defined(SKYRIMBRIDGE_FXH)
		[branch] if (SB_IsActive())
		{
			float3 GameSunCol = SB_Sun_Color.rgb;
			//Normalize to preserve tint but not blow out brightness
			float GameLum = dot(GameSunCol, N_LUM);
			[branch] if (GameLum > 0.01)
			{
				GameSunCol /= GameLum;
				SunCol = lerp(SunCol, SunCol * GameSunCol, SKYRIMBRIDGE_SUNCOLOR_BLEND);
			}
		}
		#endif

		//--- SKYRIMBRIDGE: lightning flash boost ---
		#if USE_SKYRIMBRIDGE_LIGHTNING && defined(SKYRIMBRIDGE_FXH)
		[branch] if (SB_IsActive() && SB_Lightning.y > 0.5)
		{
			Intensity = max(Intensity, SB_Lightning.z * 0.3);
			SunCol = lerp(SunCol, float3(0.85, 0.9, 1.0), SB_Lightning.z * 0.5);
		}
		#endif

		//--- ATMOSPHERIC EXTINCTION: dim and warm flares at low sun angles ---
		[branch] if(UIExt_Enable)
		{
			//Estimate sun height from screen position and time of day
			float SunHeight = saturate(ENightDayFactor * 0.8 + 0.1);
			//Dawn/dusk = low sun = more extinction
			float LowSunFactor = saturate(
				TimeOfDay1.x * 0.9 + TimeOfDay1.y * 0.4 +
				TimeOfDay1.w * 0.5 + TimeOfDay2.x * 0.9
			);
			SunHeight = lerp(SunHeight, 0.05, LowSunFactor);
			
			SunCol = AtmosphericExtinction(SunCol, SunHeight);
		}
		
		//--- WEATHER DAMPENING: reduce flares in overcast/rain ---
		[branch] if(UIWDamp_Enable)
		{
			//Use scene luminance as proxy for sky brightness
			//Overcast sky = lower luminance average = more dampening
			float2 skyUV = LightParameters.xy * float2(0.5, -0.5) + 0.5;
			float3 skySample = TextureColor.SampleLevel(Linear_Sampler, skyUV, 0).rgb;
			float skyLuma = dot(skySample, N_LUM);
			
			//Low sky luminance → more dampening (overcast indicator)
			float weatherDamp = 1.0 - UIWDamp_CloudDamp * saturate(1.0 - skyLuma * 3.0);
			
			//Night factor: don't dampen moon flares differently
			weatherDamp = lerp(1.0, weatherDamp, ENightDayFactor);
			
			Intensity *= weatherDamp;
		}
		
		//--- CINEMATIC FLARE COLOR GRADE: warm/cool shift ---
		[branch] if(UIFAge_Enable)
		{
			SunCol = FlareColorGrade(SunCol);
		}

		return Intensity * FlareInt;
	#else
		SunCol = 1.0;
		return saturate(TextureMask.Load(0) * LightParameters.w) * FlareInt;
	#endif
}


//----------------------------------------------------------------------------------------------//
//  Spectral Color Mapping                                                                      //
//                                                                                              //
//  Maps a normalized t in [0,1] to approximate visible spectrum RGB.                           //
//  t=0 → violet/blue, t=0.5 → green, t=1 → deep red                                        //
//  Simplified CIE 1931 color matching function approximation.                                  //
//----------------------------------------------------------------------------------------------//

float3 SpectralColor(float t)
{
	t = saturate(t);
	float3 c;
	c.r = smoothstep(0.38, 0.72, t) + smoothstep(0.90, 1.0, t) * 0.15;
	c.g = smoothstep(0.08, 0.38, t) * (1.0 - smoothstep(0.58, 0.88, t));
	c.b = 1.0 - smoothstep(0.05, 0.42, t);
	return c * c; //gamma-ish for more saturated appearance
}


//----------------------------------------------------------------------------------------------//
//									      Shaders											    //
//																								//
//----------------------------------------------------------------------------------------------//


//--------------------------------------Atmospheric Aureole-------------------------------------//
//                                                                                              //
//  Multi-layered Mie forward-scattering haze around the solar disc.                            //
//                                                                                              //
//  Physics: atmospheric aerosols scatter sunlight into a bright aureole                        //
//  concentrated within ~5 degrees of the disc, with color shifting warm at                    //
//  low sun angles (increased atmospheric path length → Rayleigh extinction                    //
//  of blue → golden/amber haze).                                                              //
//                                                                                              //
//  Implementation: two concentric exponential envelopes (tight core + wide                     //
//  haze) modulated by a subtle angular scintillation noise and breathing                       //
//  animation for organic quality. Golden-hour factor derived from TimeOfDay.                    //
//----------------------------------------------------------------------------------------------//

VertexShaderOutputAureole VS_Aureole(VertexShaderInput IN)
{
	VertexShaderOutputAureole OUT;
	OUT.pos.zw      = float2(IN.pos.z, 1.0);
	OUT.texcoord.xy = IN.txcoord.xy;
	OUT.texcoord.zw = IN.txcoord.xy * 2.0 - 1.0;
	
	float3 SunCol;
	OUT.SunInt = GetSunIntensity(UIAur_Int * UIAur_Enable, SunCol);
	OUT.SunCol = SunCol;
	
	//Moon: scale aureole independently at night
	OUT.SunInt *= lerp(UIMoon_AureoleMult, 1.0, ENightDayFactor);
	
	//Scale quad large enough for the atmospheric haze envelope
	float Scale = max(UIAur_InnerScale, UIAur_OuterScale) * 1.6;
	OUT.pos.xy  = IN.pos.xy * float2(Scale, Scale * ScreenSize.z);
	OUT.pos.xy += LightParameters.xy;
	
	//Golden-hour factor: dawn/sunrise/sunset/dusk → warmer, more spread aureole
	OUT.GoldenHr = saturate(
		TimeOfDay1.x * 1.0 +   //dawn
		TimeOfDay1.y * 0.6 +   //sunrise
		TimeOfDay1.w * 0.8 +   //sunset
		TimeOfDay2.x * 1.0     //dusk
	);
	
	//Organic breathing: three incommensurate sine waves → quasi-random pulse
	float Time  = Timer.x * 16777.216;
	OUT.Breathe = sin(Time * 0.07) * 0.5
	            + sin(Time * 0.13) * 0.3
	            + sin(Time * 0.23) * 0.2;
	OUT.Breathe *= UIAur_Breathe;
	
	return OUT;
}

float4 PS_Aureole(VertexShaderOutputAureole IN) : SV_Target
{
	clip(IN.SunInt - SUN_INTENSITY_CLIP);
	
	float Dist = length(IN.texcoord.zw);
	clip(1.0 - Dist);
	
	//Horizon boost: aureole spreads and intensifies at low sun angles
	float HBoost    = 1.0 + IN.GoldenHr * UIAur_HorizonBoost;
	float BreatheMod = 1.0 + IN.Breathe;
	
	//Core: intense white-hot inner glow (Mie forward-scattering peak)
	float CoreDist = Dist / max(UIAur_InnerScale * BreatheMod, DELTA);
	float CoreGlow = exp(-CoreDist * UIAur_CoreFalloff) * 1.5;
	
	//Haze: broad atmospheric scattering envelope
	float HazeDist = Dist / max(UIAur_OuterScale * HBoost * BreatheMod, DELTA);
	float HazeGlow = exp(-HazeDist * UIAur_HazeFalloff) * 0.5;
	
	//Angular scintillation: subtle radial noise for living, organic quality
	float Angle = atan2(IN.texcoord.w, IN.texcoord.z);
	float Scintillation = Random(floor(Angle * 40.0 + Timer.x * 16777.216 * 0.3));
	Scintillation = Scintillation * 0.15 + 0.85;
	HazeGlow *= Scintillation;
	
	//Color: core stays close to sun color, haze picks up warmth
	float3 WarmShift = float3(1.0, 0.85, 0.6);
	WarmShift = lerp(1.0, WarmShift, UIAur_Warmth * (1.0 + IN.GoldenHr * 0.5));
	
	float3 CoreColor = IN.SunCol;
	float3 HazeColor = IN.SunCol * WarmShift;
	
	//Tint override (alpha controls blend toward user tint)
	CoreColor = lerp(CoreColor, ColorToChroma(UIAur_Tint.rgb), UIAur_Tint.a);
	HazeColor = lerp(HazeColor, ColorToChroma(UIAur_Tint.rgb) * WarmShift, UIAur_Tint.a);
	
	float3 Result = CoreGlow * CoreColor + HazeGlow * HazeColor;
	Result *= IN.SunInt * HBoost;
	
	return float4(Result, 1.0);
}


//---------------------------------------Ciliary Corona-----------------------------------------//
//                                                                                              //
//  Diffraction through the radial fiber structure of the crystalline lens.                      //
//                                                                                              //
//  Physics: lens fibers act as a radial diffraction grating. Different                         //
//  wavelengths diffract at different angles, creating color-separated                          //
//  concentric rings. The fiber orientation produces angular intensity                          //
//  modulation — bright/dark radial streaks overlaid on the rings.                              //
//                                                                                              //
//  Spectral order: blue (inner) → green → red (outer), because shorter                       //
//  wavelengths diffract at smaller angles.                                                     //
//----------------------------------------------------------------------------------------------//

VertexShaderOutputCorona VS_Corona(VertexShaderInput IN)
{
	VertexShaderOutputCorona OUT;
	OUT.pos.zw      = float2(IN.pos.z, 1.0);
	OUT.texcoord.xy = IN.txcoord.xy;
	OUT.texcoord.zw = IN.txcoord.xy * 2.0 - 1.0;
	
	OUT.SunInt = GetSunIntensity(UICorona_Int * UICorona_Enable, OUT.SunCol);
	
	//Moon: scale corona independently at night (lunar corona is prominent)
	OUT.SunInt *= lerp(UIMoon_CoronaMult, 1.0, ENightDayFactor);
	
	float Scale = UICorona_Scale * 1.3;
	OUT.pos.xy  = IN.pos.xy * float2(Scale, Scale * ScreenSize.z);
	OUT.pos.xy += LightParameters.xy;
	
	return OUT;
}

float4 PS_Corona(VertexShaderOutputCorona IN) : SV_Target
{
	clip(IN.SunInt - SUN_INTENSITY_CLIP);
	
	float Dist  = length(IN.texcoord.zw);
	float Angle = atan2(IN.texcoord.w, IN.texcoord.z);
	clip(1.0 - Dist);
	
	//Normalize distance for ring evaluation
	float NormDist = Dist / max(UICorona_Scale, DELTA);
	
	//Fiber structure: angular modulation from crystalline lens fibers
	//Real fibers are nearly radial — we approximate with angular noise
	//that creates bright/dark radial streaks
	float FiberAngle = Angle * INV_PI * 0.5 + 0.5; //[0,1]
	float FiberPhase = floor(FiberAngle * UICorona_FiberFreq);
	float FiberRand  = Random(FiberPhase);
	float FiberMask  = lerp(1.0, smoothstep(0.3, 0.7, FiberRand), UICorona_FiberAmt);
	
	//Spectral ring pattern: each wavelength has slightly different ring spacing
	//  lambda_red > lambda_green > lambda_blue → red rings wider-spaced
	float BaseFreq  = UICorona_RingCount * PI;
	float Spread    = UICorona_Spectral;
	float Sharpness = UICorona_RingSharp;
	
	float3 Rings;
	Rings.r = pow(abs(sin(NormDist * BaseFreq * (1.0 - Spread))), Sharpness);
	Rings.g = pow(abs(sin(NormDist * BaseFreq)),                   Sharpness);
	Rings.b = pow(abs(sin(NormDist * BaseFreq * (1.0 + Spread))), Sharpness);
	
	//Pure spectral contribution by radial position
	//Inner corona blue-shifted, outer red-shifted (diffraction order)
	float  SpectralT    = saturate(NormDist * 1.2);
	float3 PureSpectral = SpectralColor(SpectralT) * 0.3;
	Rings = Rings * (0.7 + PureSpectral);
	
	//Radial falloff
	float Falloff = exp(-NormDist * UICorona_Falloff);
	
	//Inner deadzone: avoid overlapping the solar disc core
	float InnerFade = smoothstep(0.05, 0.2, NormDist);
	
	float3 Color = Rings * FiberMask * Falloff * InnerFade;
	Color *= IN.SunCol * IN.SunInt;
	
	return float4(Color, 1.0);
}


//--------------------------------------22/46 Degree Halo---------------------------------------//
//                                                                                              //
//  Hexagonal ice crystals in cirrus clouds refract sunlight at minimum                         //
//  deviation angles of 22° and 46°, producing bright rings at fixed                           //
//  angular distances from the sun. The inner edge is a caustic (sharp,                         //
//  red-tinted) and the outer fades gradually. Each channel refracts at a                       //
//  slightly different angle (dispersion), producing a rainbow inner edge.                      //
//----------------------------------------------------------------------------------------------//

VertexShaderOutputHalo VS_Halo(VertexShaderInput IN,
	uniform float HaloAngle, uniform float HaloInt, uniform bool HaloEnable)
{
	VertexShaderOutputHalo OUT;
	OUT.pos.zw      = float2(IN.pos.z, 1.0);
	OUT.texcoord.xy = IN.txcoord.xy;
	OUT.texcoord.zw = IN.txcoord.xy * 2.0 - 1.0;
	
	OUT.SunInt = GetSunIntensity(HaloInt * HaloEnable, OUT.SunCol);
	
	//Angular radius: convert degrees to clip-space units via FOV
	float HalfFovTan = tan(radians(FieldOfView * 0.5));
	float AngRadClip = tan(radians(HaloAngle)) / HalfFovTan;
	
	//Quad must be large enough to contain the ring + thickness + dispersion
	float QuadScale  = AngRadClip * 1.5;
	OUT.pos.xy = IN.pos.xy * float2(QuadScale, QuadScale * ScreenSize.z);
	OUT.pos.xy += LightParameters.xy;
	
	//Normalized angular radius in texcoord space [0,1]
	OUT.AngRadius = AngRadClip / QuadScale;
	
	return OUT;
}

float4 PS_Halo(VertexShaderOutputHalo IN,
	uniform float Thickness, uniform float InnerEdge) : SV_Target
{
	clip(IN.SunInt - SUN_INTENSITY_CLIP);
	
	float Dist = length(IN.texcoord.zw);
	clip(1.0 - Dist);
	
	//Per-channel ring center: dispersion shifts red inside, blue outside
	float Spread = UIHalo_Dispersion;
	float3 RingCenter = IN.AngRadius * float3(1.0 - Spread, 1.0, 1.0 + Spread);
	
	//Signed distance from ring center per channel
	float3 SignedDist = Dist - RingCenter;
	
	//Inner edge: sharp caustic (light piles up at minimum deviation angle)
	float3 InnerFade = smoothstep(-0.003, 0.0, SignedDist);
	InnerFade = pow(InnerFade, InnerEdge);
	
	//Outer edge: gradual exponential decay
	float3 OuterFade = exp(-max(SignedDist, 0.0) / max(Thickness, DELTA));
	
	float3 Ring = InnerFade * OuterFade;
	
	//Tint override
	float3 Col = IN.SunCol;
	Col = lerp(Col, ColorToChroma(UIHalo_Tint.rgb), UIHalo_Tint.a);
	
	return float4(Ring * Col * IN.SunInt, 1.0);
}


//------------------------------------------Sun Dogs--------------------------------------------//
//                                                                                              //
//  Parhelia: paired bright spectral spots at ±22° horizontally from the                       //
//  sun, caused by horizontally-oriented hexagonal plate crystals. The                          //
//  inner edge (sunward) is red, trailing to blue away from the sun.                            //
//  More prominent at low sun elevations when plate crystals align.                             //
//----------------------------------------------------------------------------------------------//

VertexShaderOutputSunDog VS_SunDog(VertexShaderInput IN, uniform bool IsLeft)
{
	VertexShaderOutputSunDog OUT;
	OUT.pos.zw      = float2(IN.pos.z, 1.0);
	OUT.texcoord.xy = IN.txcoord.xy;
	OUT.texcoord.zw = IN.txcoord.xy * 2.0 - 1.0;
	
	OUT.SunInt = GetSunIntensity(UIDog_Int * UIDog_Enable, OUT.SunCol);
	
	//Low-sun boost: sun dogs intensify near the horizon
	float GoldenFactor = saturate(
		TimeOfDay1.x + TimeOfDay1.y * 0.5 + TimeOfDay1.w * 0.7 + TimeOfDay2.x
	);
	OUT.SunInt *= 1.0 + GoldenFactor * UIDog_HorizonBoost;
	
	//Position at 22° horizontal offset from sun
	float HalfFovTan = tan(radians(FieldOfView * 0.5));
	float AngOffset  = tan(radians(22.0)) / (HalfFovTan * ScreenSize.z);
	float2 DogCenter = LightParameters.xy;
	DogCenter.x += AngOffset * (IsLeft ? -1.0 : 1.0);
	
	//Sun dog blob: stretched horizontally
	float2 DogScale = UIDog_Size * float2(UIDog_Stretch, ScreenSize.z);
	OUT.pos.xy = IN.pos.xy * DogScale + DogCenter;
	
	return OUT;
}

float4 PS_SunDog(VertexShaderOutputSunDog IN, uniform bool IsLeft) : SV_Target
{
	clip(IN.SunInt - SUN_INTENSITY_CLIP);
	
	float Dist = length(IN.texcoord.zw);
	clip(1.0 - Dist);
	
	//Spectral gradient: red on the sunward edge, blue trailing away
	//texcoord.z > 0 is away from sun for left dog, toward sun for right dog
	float SpectralPos = IN.texcoord.z * (IsLeft ? 1.0 : -1.0);
	SpectralPos = SpectralPos * UIDog_Dispersion * 3.0 + 0.5;
	
	float3 DogSpectral;
	DogSpectral.r = smoothstep(0.38, 0.72, SpectralPos);
	DogSpectral.g = smoothstep(0.08, 0.38, SpectralPos) * (1.0 - smoothstep(0.58, 0.88, SpectralPos));
	DogSpectral.b = 1.0 - smoothstep(0.05, 0.42, SpectralPos);
	DogSpectral = DogSpectral * DogSpectral;
	DogSpectral = DogSpectral / max(dot(DogSpectral, N_LUM), DELTA);
	DogSpectral = lerp(1.0, DogSpectral, saturate(UIDog_Dispersion * 5.0));
	
	//Radial falloff
	float Falloff = exp(-Dist * 3.0);
	Falloff *= smoothstep(1.0, 0.5, Dist);
	
	//Tint
	float3 Col = IN.SunCol;
	Col = lerp(Col, ColorToChroma(UIDog_Tint.rgb), UIDog_Tint.a);
	
	return float4(Falloff * DogSpectral * Col * IN.SunInt, 1.0);
}


//----------------------------------------Veiling Glare-----------------------------------------//
//                                                                                              //
//  Broad, low-frequency contrast reduction from intraocular scattering                         //
//  (vitreous humor + cornea). Not a discrete flare — it's a soft wash                         //
//  over the entire image that lifts the black level toward the sun.                            //
//  The difference between "flares on a dark image" and "the whole scene                        //
//  feels bathed in light."                                                                     //
//----------------------------------------------------------------------------------------------//

VertexShaderOutputVeil VS_VeilingGlare(VertexShaderInput IN)
{
	VertexShaderOutputVeil OUT;
	OUT.pos      = float4(IN.pos.xyz, 1.0);
	OUT.texcoord = IN.txcoord.xy;
	
	OUT.SunInt = GetSunIntensity(UIVeil_Int * UIVeil_Enable, OUT.SunCol);
	OUT.SunUV  = LightParameters.xy * float2(0.5, -0.5) + 0.5;
	
	return OUT;
}

float4 PS_VeilingGlare(VertexShaderOutputVeil IN) : SV_Target
{
	clip(IN.SunInt - SUN_INTENSITY_CLIP);
	
	//Distance from sun in aspect-corrected UV space
	float2 Delta = IN.texcoord - IN.SunUV;
	Delta.x *= ScreenSize.z; //aspect correction
	float Dist = length(Delta);
	
	//Very broad, gentle Gaussian falloff
	float Veil = exp(-Dist * Dist * UIVeil_Falloff * UIVeil_Falloff);
	
	//Color: blend between white wash and sun-colored wash
	float3 Col = lerp(1.0, IN.SunCol, UIVeil_SunColor);
	
	return float4(Veil * Col * IN.SunInt, 1.0);
}


//--------------------------------------Eyelash Diffraction-------------------------------------//
//                                                                                              //
//  When squinting at a bright source, eyelashes act as a horizontal                            //
//  diffraction grating, producing tall vertical streaks. Each lash fiber                       //
//  creates an Airy-like diffraction pattern; the ensemble creates a                            //
//  modulated vertical column with slight spectral spread (red extends                          //
//  farther than blue due to longer wavelength).                                                //
//----------------------------------------------------------------------------------------------//

VertexShaderOutputEyelash VS_Eyelash(VertexShaderInput IN)
{
	VertexShaderOutputEyelash OUT;
	OUT.pos.zw      = float2(IN.pos.z, 1.0);
	OUT.texcoord.xy = IN.txcoord.xy;
	OUT.texcoord.zw = IN.txcoord.xy * 2.0 - 1.0;
	
	OUT.SunInt = GetSunIntensity(UILash_Int * UILash_Enable, OUT.SunCol);
	
	//Tall, narrow quad centered on sun
	float2 LashScale = float2(UILash_Width, UILash_Height * ScreenSize.z);
	OUT.pos.xy = IN.pos.xy * LashScale + LightParameters.xy;
	
	return OUT;
}

float4 PS_Eyelash(VertexShaderOutputEyelash IN) : SV_Target
{
	clip(IN.SunInt - SUN_INTENSITY_CLIP);
	
	float VertDist  = abs(IN.texcoord.w);
	float HorizDist = abs(IN.texcoord.z);
	
	//Vertical: exponential falloff (diffraction envelope)
	//Per-channel: red extends farther (longer wavelength)
	float3 VertFalloff;
	VertFalloff.r = exp(-VertDist * (2.5 - UILash_Spectral * 8.0));
	VertFalloff.g = exp(-VertDist * 2.5);
	VertFalloff.b = exp(-VertDist * (2.5 + UILash_Spectral * 8.0));
	
	//Horizontal: tight Gaussian focus
	float HorizFalloff = exp(-HorizDist * HorizDist * 50.0);
	
	//Eyelash fiber modulation: vertical interference pattern from lash spacing
	float FiberPhase = IN.texcoord.w * UILash_FiberCount * PI;
	float FiberPattern = pow(abs(cos(FiberPhase)), 2.0);
	
	//Add per-fiber randomness: each bright band has slightly different intensity
	float FiberIdx  = floor(IN.texcoord.w * UILash_FiberCount * 0.5 + 0.5);
	float FiberRand = Random(FiberIdx) * UILash_FiberRand + (1.0 - UILash_FiberRand);
	FiberPattern *= FiberRand;
	
	//Smooth core (don't modulate near the sun disc)
	float CoreBlend = smoothstep(0.0, 0.15, VertDist);
	FiberPattern = lerp(1.0, FiberPattern, CoreBlend);
	
	float3 Col = IN.SunCol;
	Col = lerp(Col, ColorToChroma(UILash_Tint.rgb), UILash_Tint.a);
	
	float3 Color = VertFalloff * HorizFalloff * FiberPattern * Col * IN.SunInt;
	return float4(Color, 1.0);
}


//--------------------------------------Sensor Bloom Cross--------------------------------------//
//                                                                                              //
//  CCD/CMOS sensors produce a faint 4-pointed cross from diffraction                          //
//  off the rectangular pixel grid. Spikes are extremely thin and long,                         //
//  aligned to 0°/90° (or rotated by microlens alignment). Aesthetically                      //
//  different from aperture starburst — these are clinical, geometric.                          //
//----------------------------------------------------------------------------------------------//

VertexShaderOutputBloomCross VS_BloomCross(VertexShaderInput IN)
{
	VertexShaderOutputBloomCross OUT;
	OUT.pos.zw      = float2(IN.pos.z, 1.0);
	OUT.texcoord.xy = IN.txcoord.xy;
	OUT.texcoord.zw = IN.txcoord.xy * 2.0 - 1.0;
	
	OUT.SunInt = GetSunIntensity(UIBloom_Int * UIBloom_Enable, OUT.SunCol);
	
	//Large quad to contain long spikes
	float Scale = UIBloom_Length * 1.2;
	OUT.pos.xy = IN.pos.xy * float2(Scale, Scale * ScreenSize.z);
	OUT.pos.xy += LightParameters.xy;
	
	return OUT;
}

float4 PS_BloomCross(VertexShaderOutputBloomCross IN) : SV_Target
{
	clip(IN.SunInt - SUN_INTENSITY_CLIP);
	
	//Rotate the coordinate system
	float2 Local = MatrixRotate(IN.texcoord.zw, UIBloom_Rotation, false);
	
	//Distance from horizontal and vertical axes through center
	float DistH = abs(Local.y);
	float DistV = abs(Local.x);
	
	//Each spike: ultra-thin Gaussian perpendicular, exponential along axis
	float HSpike = exp(-DistH / max(UIBloom_Thickness, DELTA))
	             * exp(-abs(Local.x) * UIBloom_Falloff);
	float VSpike = exp(-DistV / max(UIBloom_Thickness, DELTA))
	             * exp(-abs(Local.y) * UIBloom_Falloff);
	
	//Take max (additive would double at center)
	float Cross = max(HSpike, VSpike);
	
	//Slight core bloom to connect the spikes
	float CoreDist = length(IN.texcoord.zw);
	Cross += exp(-CoreDist * 15.0) * 0.3;
	
	float3 Col = IN.SunCol;
	Col = lerp(Col, ColorToChroma(UIBloom_Tint.rgb), UIBloom_Tint.a);
	
	return float4(Cross * Col * IN.SunInt, 1.0);
}


//------------------------------------------Ghost Flares-----------------------------------------//

VertexShaderOutputGSprite VS_GhostSprite(VertexShaderInput IN,
	uniform float  FlareOffset, uniform float FlareScale,
	uniform float  FlareInt,    uniform float CAScale,
	uniform float4 FlareTint)
{
	VertexShaderOutputGSprite OUT;
	OUT.pos          = float4(IN.pos.xyz, 1.0);
	OUT.texcoord     = IN.txcoord.xyxy;
	OUT.texcoord.zw *= 2.0 * CAScale + 2.0;
	OUT.texcoord.zw -= CAScale + 1.0;
	
	OUT.SunInt = GetSunIntensity(FlareInt * UIG_Int * UI_EnableGhost, OUT.FlareTint);
	
	[branch] if(OUT.SunInt > SUN_INTENSITY_CLIP)
	{
		float  ParamLength = length(LightParameters.xy);
		float2 LightParams = LightParameters.xy / ParamLength * nRoot(ParamLength, UIG_OffsetCurve);
		
		float2   Offset = LightParams * -FlareOffset * ScreenSize.z * 0.75;
		float2x2 RotMat = GetDirVec(UI_ApertureRotation).xyyx * float4(1.0, -1.0, 1.0, 1.0);
		
		OUT.Scale  = FlareScale * 0.1 * (CAScale + 1.0);
		OUT.pos.xy = mul(RotMat, OUT.pos.xy);
		OUT.pos.y *= ScreenSize.z;
		OUT.pos.xy = OUT.pos.xy * OUT.Scale + Offset;
		
		float OffsetMovement = Random(floor(FlareOffset * 10.0) + UIG_OMoveSeed) * 2.0 - 1.0;
			  OUT.pos.xy    += Offset * OffsetMovement * UIG_OMoveWeight * length(Offset);
		
		
		[unroll] for(int i=0; i<9; i++)
		OUT.ApertureVertices[i] = GetDirVec(360.0 / UI_ApertureShape * i);
		OUT.ApertureVertices[9] = OUT.ApertureVertices[0];
		
		
		OUT.CAOffsetVec = normalize(LightParams - Offset) * CAScale * 0.3;
		OUT.CAOffsetVec = mul(RotMat, OUT.CAOffsetVec * float2(ScreenSize.z, -1.0));
		OUT.FlareTint   = lerp(OUT.FlareTint, ColorToChroma(FlareTint.rgb), FlareTint.a);
		
		float RandomDirt = floor(FlareOffset * 10.0);
			  RandomDirt = Random(RandomDirt) * 0.5 + 0.5;
		OUT.texcoord.xy  = AtlasFetch_4(OUT.texcoord.xy * RandomDirt,4);
		OUT.Scale        = max(saturate(OUT.Scale * 10.0), 0.5);
	}
	
	return OUT;
}

float4 PS_GhostSprite(VertexShaderOutputGSprite IN,
	uniform float FlareFeather, uniform float FlareDiffInt,
	uniform float FlareDirtInt, uniform float FlareWeight,
	uniform float FlareVignette) : SV_Target
{
	float3 CenterDistance = { length(IN.texcoord.zw - IN.CAOffsetVec),
							  length(IN.texcoord.zw),
							  length(IN.texcoord.zw + IN.CAOffsetVec) };
	
	clip(min(1.0 - min3(CenterDistance), IN.SunInt - SUN_INTENSITY_CLIP));
	
	float2 SSCoords    = IN.pos.xy * PixelSize;
	float3 RadialDist  = saturate(1.0 - CenterDistance);
	float3 MinEdgeDist = RadialDist;
	
	[branch] if(UI_ApertureRoundness < 1.0)
	{
		[loop]   for(int i=0; i < UI_ApertureShape; i++)
		[unroll] for(int j=-1; j<2;)
		{
			//Test if coords are outside or inside the aperture shape (only works for convex polygons)
			float2 Edge     = float2(-1.0, 1.0) * (IN.ApertureVertices[i+1] - IN.ApertureVertices[i]);
			float  EdgeDist = dot(Edge.yx, IN.ApertureVertices[i]) -
							  dot(Edge.yx, IN.texcoord.zw + IN.CAOffsetVec * j);
			
			EdgeDist = lerp(EdgeDist, RadialDist[++j], (-EdgeDist * EdgeDist + 1.0) * UI_ApertureRoundness);
			
			MinEdgeDist[j] = min(MinEdgeDist[j], EdgeDist);
		}
		
		clip(max3(MinEdgeDist));
	}
	
	float3 Color = IN.SunInt * (MinEdgeDist > 0.0) * IN.FlareTint;
	
	float PhaseShift = min(FlareFeather + DELTA, 0.1) * -18.0;
	float Frequency  = 30.0 * UIG_DiffFreq * IN.Scale;
	Color += DiffractionF3(MinEdgeDist, Frequency, PhaseShift, FlareDiffInt) * Color;
	
	Color *= smoothstep(0.0, FlareFeather + DELTA, MinEdgeDist);
	Color *= smoothstep(6.0 * pow(FlareWeight,3), 0.0, MinEdgeDist);
	
	float VigWeight = ScreenSize.z - FlareVignette * ScreenSize.z;
	Color *= LinearStep(VigWeight + 0.1, VigWeight - 0.1, length(SSCoords * 2.0 - 1.0));
	
	Color += Color * LensDirtAtlas.Sample(Linear_Sampler, IN.texcoord.xy) * FlareDirtInt;
	return float4(Color, 1.0);
}


//-------------------------------------Hoop/Ring Lens Flare-------------------------------------//

VertexShaderOutputHoopSprite VS_HoopSprite(VertexShaderInput IN)
{
	VertexShaderOutputHoopSprite OUT;
	OUT.pos          = float4(IN.pos.xyz,1.0);
	OUT.texcoord     = IN.txcoord.xyxy;
	OUT.texcoord.zw *= 2.0 * UIHoop_CA + 2.0;
	OUT.texcoord.zw -= UIHoop_CA + 1.0;
	
	OUT.SunInt = GetSunIntensity(UIHoop_Int * UI_EnableHoop, OUT.FlareTint);
	
	[branch] if(OUT.SunInt > SUN_INTENSITY_CLIP)
	{
		float SunDistance = length(LightParameters.xy) * UIHoop_SunDistMod;
		
		float2   Offset = LightParameters.xy * -UIHoop_Offset * ScreenSize.z * 0.75;
		float2x2 RotMat = GetDirVec(UI_ApertureRotation).xyyx * float4(1.0, -1.0, 1.0, 1.0);
		
		float2 Scale = UIHoop_Scale * 0.1 * (UIHoop_CA + 1.0) * SunDistance;
		OUT.SunInt  *= saturate(SunDistance);
		
		OUT.pos.xy = mul(RotMat, OUT.pos.xy);
		OUT.pos.y *= ScreenSize.z;
		OUT.pos.xy = OUT.pos.xy * Scale + Offset;
		
		
		[unroll] for(int i=0; i<9; i++)
		OUT.ApertureVertices[i] = GetDirVec(360.0 / UI_ApertureShape * i);
		OUT.ApertureVertices[9] = OUT.ApertureVertices[0];
		
		float2 HalfVertex = OUT.ApertureVertices[0] + 0.5 * (OUT.ApertureVertices[1] - OUT.ApertureVertices[0]);
		
		float HalfVertexLength  = length(HalfVertex);
		OUT.RoundingRadius      = lerp(HalfVertexLength, 1.0, 1.0 - UIHoop_Roundness);
		OUT.MaxPossibleVertDist = distance(HalfVertex / HalfVertexLength, OUT.ApertureVertices[0]);
		
		OUT.CAOffsetVec = (LightParameters.xy - Offset) * UIHoop_CA * 0.3;
		OUT.CAOffsetVec = mul(RotMat, OUT.CAOffsetVec * float2(ScreenSize.z, -1.0));
		OUT.FlareTint   = lerp(OUT.FlareTint, ColorToChroma(UIHoop_Tint.rgb), UIHoop_Tint.a);
	}
	
	return OUT;
}

float4 PS_HoopSprite(VertexShaderOutputHoopSprite IN) : SV_Target
{
	float3 CenterDistance = { length(IN.texcoord.zw - IN.CAOffsetVec),
							  length(IN.texcoord.zw),
							  length(IN.texcoord.zw + IN.CAOffsetVec) };
	
	clip(min(1.0 - min3(CenterDistance), IN.SunInt - SUN_INTENSITY_CLIP));
	
	float3 RadialDist  = saturate(1.0 - CenterDistance);
	float3 MinEdgeDist = RadialDist;
	
	[branch] if(UI_ApertureRoundness < 1.0)
	{
		[loop]   for(int i=0; i < UI_ApertureShape; i++)
		[unroll] for(int j=-1; j<2;)
		{
			float2 Edge     = float2(-1.0, 1.0) * (IN.ApertureVertices[i+1] - IN.ApertureVertices[i]);
			float  EdgeDist = dot(Edge.yx, IN.ApertureVertices[i]) -
							  dot(Edge.yx, IN.texcoord.zw + IN.CAOffsetVec * j);
			
			EdgeDist = lerp(EdgeDist, RadialDist[++j], (-EdgeDist * EdgeDist + 1.0) * UI_ApertureRoundness);
			
			MinEdgeDist[j] = min(MinEdgeDist[j], EdgeDist);
		}
		
		float3 Interpolator = LinearStep(IN.RoundingRadius, 1.0, CenterDistance);
			   MinEdgeDist  = lerp(MinEdgeDist, pow(RadialDist,2), Interpolator);
		
		clip(max3(MinEdgeDist));
	}
	
	float3 Color = IN.SunInt * (MinEdgeDist > 0.0) * IN.FlareTint;
	
	Color *= smoothstep(0.0, 0.05, MinEdgeDist);
	Color *= smoothstep(6.0 * pow(UIHoop_Width,3), 0.0, MinEdgeDist);
	Color *= pow(saturate(UIHoop_Fade - dot(IN.texcoord.zw, normalize(IN.CAOffsetVec))),4);
	
	float  MinVertDist = 1.0;
	float2 NormCoords  = normalize(IN.texcoord.zw);
	
	[loop] for(int i=0; i < UI_ApertureShape; i++)
		MinVertDist = min(MinVertDist, distance(NormCoords, IN.ApertureVertices[i]));
	
	float VertGradient = LinearStep(0.0, IN.MaxPossibleVertDist, MinVertDist);
	float HoopPattern  = pow(sin(VertGradient * UIHoop_PatternFreq * PI),2);
	
	#if ENABLE_HOOP_ANTIALIASING
		float PatternInt = 0.0;
		float StepSize   = max2(PixelSize);
		
		[unroll] for(float i=-2.0; i <= 2.0; i += 2.0)
		{
			float CurrPattern = floor((VertGradient + i * StepSize) * UIHoop_IntRandom);
				  PatternInt += HoopPattern * Random(CurrPattern);
		}
		
		Color *= PatternInt / 3.0;
	#else
		Color *= HoopPattern * Random(floor(VertGradient * UIHoop_IntRandom));
	#endif
	
	return float4(Color, 1.0);
}


//-------------------------------------Starburst Lens Flare-------------------------------------//

VertexShaderOutputSBSprite VS_StarburstSprite(VertexShaderInput IN)
{
	VertexShaderOutputSBSprite OUT;
	OUT.pos         = float4(IN.pos.xyz, 1.0);
	OUT.texcoord.xy = IN.txcoord.xy;
	OUT.texcoord.zw = IN.txcoord.xy * 2.0 - 1.0;
	
	OUT.pos.y *= ScreenSize.z;
	OUT.pos.xy = OUT.pos.xy * UISB_Scale + LightParameters.xy;
	
	
	[unroll] for(int i=0; i<9; i++)
	OUT.ApertureVertices[i] = GetDirVec(360.0 / UI_ApertureShape * (i + 0.5) + UI_ApertureRotation);
	OUT.ApertureVertices[9] = OUT.ApertureVertices[0];
	
	OUT.SunInt    = GetSunIntensity(UISB_FlareInt * UI_EnableStarburst, OUT.FlareTint);
	OUT.FlareTint = lerp(OUT.FlareTint, ColorToChroma(UISB_FlareTint.rgb), UISB_FlareTint.a);
	OUT.OddShape  = UI_ApertureShape & 1;
	OUT.SafeZone  = rcp(0.005 / UISB_Scale);
	
	return OUT;
}

float4 PS_StarburstSprite(VertexShaderOutputSBSprite IN) : SV_Target
{
	float SunDist = length(IN.texcoord.zw);
	clip(min(1.0 - SunDist, IN.SunInt - SUN_INTENSITY_CLIP));
	
	float  InvertedRoundness = 1.0 - UI_ApertureRoundness;
	float  MinVertDist       = 1.0;
	float2 NormCoords        = normalize(IN.texcoord.zw);
	
	[loop] for(int i=0; i < UI_ApertureShape; i++)
		MinVertDist = min(MinVertDist, distance( NormCoords, IN.ApertureVertices[i]));
	
	[loop] for(;i > 0 && IN.OddShape; i--)
		MinVertDist = min(MinVertDist, distance(-NormCoords, IN.ApertureVertices[i]));
	
	float BurstWidth  = pow(SunDist, UISB_BurstWidthCurve);
		  BurstWidth *= 250.0 - 250.0 * sqrt(UISB_BurstWidth);
	
	float StarMask = pow(saturate(1.0 - MinVertDist), BurstWidth * InvertedRoundness);
		  StarMask = lerp(1.0, StarMask, saturate(SunDist * IN.SafeZone));
	
	#if ENABLE_STARBURST_ANTIALIASING
		static const float2 offsets[4] = { 0.5,0.5, -0.5,0.5, 0.5,-0.5, -0.5,-0.5 };
		float Randomness    = 0.0;
		float RandFrequency = UISB_RandFrequency * 10.0;
		
		[unroll] for(int i=0; i<4; i++)
			Randomness += Random(floor(normalize(IN.texcoord.zw + offsets[i] * PixelSize) * RandFrequency));
		Randomness *= 0.25;
	#else
		float Randomness = Random(floor(NormCoords * UISB_RandFrequency * 10.0));
	#endif
	
	float FalloffGradient = saturate(1.0 - SunDist);
	float StrayBursts     = Randomness > 1.0 - DELTA - UISB_StrayBurstAmount;
		  StrayBursts    *= UISB_StrayBurstInt * InvertedRoundness;
		  Randomness      = Randomness * UISB_RandIntensity + 1.0;
	
	StarMask  = LinearStep(0.4, 0.6, StarMask);
	StarMask *= pow(FalloffGradient, UISB_Falloff * Randomness);
	StarMask += StrayBursts * pow(FalloffGradient, UISB_StrayBurstFalloff * Randomness);
	StarMask *= saturate(0.5 + 1.0 - (Randomness - 1.0));
	
	//Spectral ray coloring: each spike carries a prismatic gradient
	//  The angular position between two aperture vertices maps to [0,1]
	//  which drives a visible-spectrum lookup, tinting each ray from
	//  blue (near vertex) through green to red (midway between vertices).
	float3 SpectralTint = 1.0;
	[branch] if(UISB_SpectralAmt > 0.0)
	{
		//Find which angular segment we're in and our position within it
		float PixelAngle = atan2(NormCoords.y, NormCoords.x);
		float SegAngle   = TWO_PI / UI_ApertureShape;
		float Offset     = radians(UI_ApertureRotation + 180.0 / UI_ApertureShape);
		float LocalAngle = frac((PixelAngle - Offset) / SegAngle + 1.0);
		
		//Map local position to spectral parameter with configurable cycle count
		float SpectralT  = frac(LocalAngle * UISB_SpectralFreq);
		//Ping-pong so the spectrum reverses at each ray (avoids hard seam)
		SpectralT = 1.0 - abs(SpectralT * 2.0 - 1.0);
		
		float3 RaySpectrum;
		RaySpectrum.r = smoothstep(0.38, 0.72, SpectralT);
		RaySpectrum.g = smoothstep(0.08, 0.38, SpectralT) * (1.0 - smoothstep(0.58, 0.88, SpectralT));
		RaySpectrum.b = 1.0 - smoothstep(0.05, 0.42, SpectralT);
		RaySpectrum = RaySpectrum * RaySpectrum; //saturate the spectrum
		
		//Normalize so it doesn't darken overall, then blend with white
		RaySpectrum  = RaySpectrum / max(dot(RaySpectrum, N_LUM), DELTA);
		SpectralTint = lerp(1.0, RaySpectrum, UISB_SpectralAmt * SunDist);
	}
	
	return float4(StarMask * IN.SunInt * IN.FlareTint * SpectralTint, 1.0);
}


//------------------------------------Anamorphic Lens Flare-------------------------------------//

VertexShaderOutputAnamSprite VS_AnamorphicSprite(VertexShaderInput IN,
	uniform float FlareOffset, uniform float FlareWidth,
	uniform float FlareInt,    uniform bool  FlareMirror)
{
	VertexShaderOutputAnamSprite OUT;
	OUT.pos.zw      = float2(IN.pos.z, 1.0);
	OUT.texcoord.xy = IN.txcoord.xy;
	OUT.texcoord.zw = IN.txcoord.xy * 2.0 - 1.0;
	
	float2 Offset = LightParameters.xy * FlareOffset;
	float2 Scale  = { FlareWidth, 0.05 * ScreenSize.z };
	
	Offset.x  *= FlareMirror ? -1.0 : 1.0;
	OUT.pos.xy = IN.pos.xy * Scale + Offset;
	
	OUT.SunInt    = GetSunIntensity(FlareInt * UI_EnableAnam * UIAF_Int, OUT.FlareTint);
	OUT.FlareTint = lerp(OUT.FlareTint, ColorToChroma(UIAF_Tint), UIAF_Tint.a);
	
	return OUT;
}

float4 PS_AnamorphicSprite(VertexShaderOutputAnamSprite IN, uniform float FlareCurve) : SV_Target
{
	clip(IN.SunInt - SUN_INTENSITY_CLIP);
	
	IN.texcoord.zw = 1.0 - abs(IN.texcoord.zw);
	IN.SunInt     *= IN.texcoord.z;
	
	float  FlareGradient = LimitedHighPass(IN.texcoord.w, 1.0, FlareCurve, IN.SunInt);
	float3 Color         = IN.texcoord.z * FlareGradient * IN.FlareTint;
	
	return float4(Color, 1.0);
}


//------------------------------------------Lens Glare------------------------------------------//

VertexShaderOutputLensGlareSprite VS_LensGlareSprite(VertexShaderInput IN)
{
	VertexShaderOutputLensGlareSprite OUT;
	OUT.pos.zw      = float2(IN.pos.z,1.0);
	OUT.texcoord.xy = IN.txcoord.xy;
	OUT.texcoord.zw = IN.txcoord.xy * 2.0 - 1.0;
	
	float2 ScreenRatio = { 1.0, ScreenSize.z };
	
	OUT.pos.xy  = IN.pos.xy * ScreenRatio * ScreenSize.z;
	OUT.pos.xy -= LightParameters.xy;
	
	OUT.MinRatio = min2(ScreenRatio);
	OUT.MaxRatio = max2(ScreenRatio);
	
	OUT.Glare.a   = GetSunIntensity(UI_LensGlareInt * UI_EnableGlares, OUT.Glare.rgb);
	OUT.Glare.rgb = lerp(OUT.Glare.rgb, ColorToChroma(UI_LensGlareCol.rgb), UI_LensGlareCol.a);
	OUT.SunDist   = distance(-LightParameters.xy, LightParameters.xy);
	OUT.SunDist   = pow(saturate(1.0 - OUT.SunDist),2) * UI_LensGlareInt * 2.0;
	
	return OUT;
}

float4 PS_LensGlareSprite(VertexShaderOutputLensGlareSprite IN) : SV_Target
{
	clip(IN.Glare.a - SUN_INTENSITY_CLIP);
	float GlareShape = 1.0 - length(IN.texcoord.zw);
		  GlareShape = smoothstep(IN.MaxRatio, IN.MinRatio, GlareShape) * IN.SunDist;
	return float4(GlareShape * GlareShape * IN.Glare.rgb * IN.Glare.a, 1.0);
}


//-------------------------------------------Sun Glare------------------------------------------//

VertexShaderOutputSunGlareSprite VS_SunGlareSprite(VertexShaderInput IN)
{
	VertexShaderOutputSunGlareSprite OUT;
	OUT.pos.zw      = float2(IN.pos.z,1.0);
	OUT.texcoord.xy = IN.txcoord.xy;
	OUT.texcoord.zw = IN.txcoord.xy * 2.0 - 1.0;
	
	OUT.pos.xy  = IN.pos.xy * float2(1.0, ScreenSize.z) * 0.8;
	OUT.pos.xy += LightParameters.xy;
	
	OUT.Glare.a   = GetSunIntensity(UI_SunGlareInt * UI_EnableGlares, OUT.Glare.rgb);
	OUT.Glare.rgb = lerp(OUT.Glare.rgb, ColorToChroma(UI_SunGlareCol.rgb), UI_SunGlareCol.a);
	
	return OUT;
}

float4 PS_SunGlareSprite(VertexShaderOutputSunGlareSprite IN) : SV_Target
{
	clip(IN.Glare.a - SUN_INTENSITY_CLIP);
	float GlareShape = 1.0 - length(IN.texcoord.zw);
		  GlareShape = smoothstep(0.0, 1.0, GlareShape);
	return float4(GlareShape * IN.Glare.a * IN.Glare.rgb, 1.0);
}


//----------------------------------------------------------------------------------------------//
//  CINEMATIC & REALISM EXTENSION FUNCTIONS                                                     //
//----------------------------------------------------------------------------------------------//

//Procedural cell noise for dust particle generation
float2 DirtHash22(float2 p)
{
	float3 p3 = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
	p3 += dot(p3, p3.yzx + 33.33);
	return frac((p3.xx + p3.yz) * p3.zy);
}

float DirtHash11(float p)
{
	p = frac(p * 0.1031);
	p *= p + 33.33;
	p *= p + p;
	return frac(p);
}


//Thin-film interference: Fabry-Pérot reflectance for multi-layer coatings
//Returns per-channel reflectance based on coating thickness and angle
float3 ThinFilmReflectance(float cosTheta, float thickness, float coatingIOR)
{
	//Optical path difference through coating layer
	float sinTheta2 = 1.0 - cosTheta * cosTheta;
	float cosThetaCoating = sqrt(max(1.0 - sinTheta2 / (coatingIOR * coatingIOR), 0.0));
	
	//Phase difference for each wavelength (RGB approximation)
	//  δ = 4π·n·d·cos(θ_coating) / λ
	float3 wavelengths = float3(630.0, 532.0, 465.0); //nm: R, G, B
	float3 phaseDiff = 4.0 * PI * coatingIOR * thickness * cosThetaCoating / wavelengths;
	
	//Fabry-Pérot: R ∝ sin²(δ/2) for single-layer
	float3 reflectance = sin(phaseDiff * 0.5);
	reflectance *= reflectance;
	
	//Fresnel base reflectance at this angle
	float F0 = (coatingIOR - 1.0) / (coatingIOR + 1.0);
	F0 *= F0;
	float fresnel = F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
	
	return reflectance * fresnel;
}


//Atmospheric extinction: Beer-Lambert with wavelength-dependent scattering
float3 AtmosphericExtinction(float3 color, float sunHeight)
{
	//Air mass approximation: path length increases at low sun angles
	float airMass = 1.0 / max(sunHeight, 0.02);
	airMass = min(airMass, 40.0); //cap at very low angles
	
	//Rayleigh scattering coefficients (blue scattered more → red survives)
	float3 rayleighTau = float3(0.004, 0.011, 0.030) * UIExt_Strength;
	
	//Beer-Lambert transmittance
	float3 transmittance = exp(-rayleighTau * airMass);
	
	//Warm shift: sunset reddening of flares
	float3 warmShift = lerp(1.0, float3(1.2, 0.7, 0.3), UIExt_WarmShift * (1.0 - sunHeight));
	
	return color * transmittance * warmShift;
}


//Cinematic flare color grade: temperature + saturation + contrast
float3 FlareColorGrade(float3 color)
{
	//Temperature: shift warm/cool
	float temp = UIFAge_Temperature;
	color.r *= 1.0 + temp * 0.15;
	color.b *= 1.0 - temp * 0.15;
	color.g *= 1.0 + abs(temp) * 0.03; //slight green boost for either direction
	
	//Saturation
	float luma = dot(color, N_LUM);
	color = lerp(luma, color, UIFAge_Saturation);
	
	//Contrast around midpoint
	color = pow(max(color, 0.0), UIFAge_Contrast);
	
	return max(color, 0.0);
}


//----------------------------------Dirty Lens Coating Overlay-----------------------------------//
//                                                                                              //
//  Procedural dust particles, fingerprint smudges, and micro-scratches                         //
//  on the front lens element. When a bright source (sun) illuminates the                       //
//  lens, these imperfections scatter light into visible patterns.                               //
//  The pattern is static relative to the camera (screen-space).                                //
//----------------------------------------------------------------------------------------------//

struct DirtVSOutput
{
	float4 pos      : SV_POSITION;
	float2 texcoord : TEXCOORD0;
NI float  SunInt   : DIRT0;
NI float3 SunCol   : DIRT1;
NI float2 SunUV    : DIRT2;
};

DirtVSOutput VS_DirtyLens(VertexShaderInput IN)
{
	DirtVSOutput OUT;
	OUT.pos      = float4(IN.pos.xyz, 1.0);
	OUT.texcoord = IN.txcoord.xy;
	
	OUT.SunInt = GetSunIntensity(UIDirt_Intensity * UIDirt_Enable, OUT.SunCol);
	OUT.SunUV  = LightParameters.xy * float2(0.5, -0.5) + 0.5;
	
	return OUT;
}

float4 PS_DirtyLens(DirtVSOutput IN) : SV_Target
{
	clip(IN.SunInt - SUN_INTENSITY_CLIP);
	
	float2 UV = IN.texcoord;
	
	//Distance from sun in UV space (affects brightness of dirt scatter)
	float2 SunDelta = UV - IN.SunUV;
	SunDelta.x *= ScreenSize.z;
	float SunDist = length(SunDelta);
	float SunProximity = exp(-SunDist * 2.0); //strongest near sun
	
	//=== Dust Particles: cell-noise distributed specks ===//
	float2 CellUV = UV * UIDirt_Density;
	float2 CellID = floor(CellUV);
	float2 CellFrac = frac(CellUV);
	
	float DustMask = 0.0;
	
	[unroll] for(int dx = -1; dx <= 1; dx++)
	[unroll] for(int dy = -1; dy <= 1; dy++)
	{
		float2 Neighbor = float2(dx, dy);
		float2 Pos = DirtHash22(CellID + Neighbor); //particle position within cell
		float  Radius = DirtHash11(dot(CellID + Neighbor, float2(127.1, 311.7))) * 0.3 + 0.05;
		
		float Dist = length(CellFrac - Neighbor - Pos);
		float Particle = smoothstep(Radius, Radius * 0.3, Dist);
		DustMask += Particle;
	}
	
	//=== Fingerprint Smudges: large-scale noise blobs ===//
	float2 SmudgeUV = UV * 3.5;
	float SmudgeNoise = DirtHash11(dot(floor(SmudgeUV), float2(269.5, 183.3)));
	SmudgeNoise *= DirtHash11(dot(floor(SmudgeUV * 1.7), float2(419.2, 371.9)));
	float SmudgeMask = smoothstep(0.3, 0.8, SmudgeNoise) * UIDirt_SmudgeAmt;
	
	//=== Micro-Scratches: thin directional lines ===//
	float ScratchPhase = UV.x * 80.0 + UV.y * 20.0;
	float ScratchMask = pow(abs(sin(ScratchPhase)), 40.0);
	ScratchMask *= DirtHash11(floor(ScratchPhase * 0.5)) > 0.7;
	ScratchMask *= UIDirt_ScratchAmt;
	
	//Combine all dirt layers
	float TotalDirt = saturate(DustMask + SmudgeMask + ScratchMask);
	
	//Chromatic spread: different channels scatter slightly differently
	float3 DirtColor;
	DirtColor.r = TotalDirt;
	DirtColor.g = saturate(TotalDirt * (1.0 + UIDirt_ChromaSpread * 0.3));
	DirtColor.b = saturate(TotalDirt * (1.0 - UIDirt_ChromaSpread * 0.2));
	
	DirtColor *= IN.SunCol * IN.SunInt * SunProximity;
	
	return float4(DirtColor, 1.0);
}


//-------------------------------Thin-Film Coating Reflection Pass-------------------------------//
//                                                                                              //
//  Renders the colored reflection pattern from multi-layer AR coatings.                        //
//  Appears as colored ghost reflections (typically green/purple) in the                         //
//  direction opposing the sun, with spectral color varying by angle.                           //
//----------------------------------------------------------------------------------------------//

struct ThinFilmVSOutput
{
	float4 pos      : SV_POSITION;
	float4 texcoord : TEXCOORD0;    //xy=UV, zw=centered [-1,1]
NI float  SunInt   : TF0;
NI float3 SunCol   : TF1;
NI float2 SunUV    : TF2;
};

ThinFilmVSOutput VS_ThinFilm(VertexShaderInput IN)
{
	ThinFilmVSOutput OUT;
	OUT.pos      = float4(IN.pos.xyz, 1.0);
	OUT.texcoord.xy = IN.txcoord.xy;
	OUT.texcoord.zw = IN.txcoord.xy * 2.0 - 1.0;
	
	OUT.SunInt = GetSunIntensity(UITF_Intensity * UITF_Enable, OUT.SunCol);
	OUT.SunUV  = LightParameters.xy * float2(0.5, -0.5) + 0.5;
	
	return OUT;
}

float4 PS_ThinFilm(ThinFilmVSOutput IN) : SV_Target
{
	clip(IN.SunInt - SUN_INTENSITY_CLIP);
	
	//Reflected ghost position: symmetric about frame center
	float2 ReflUV = 1.0 - IN.texcoord.xy;
	float2 Delta = ReflUV - IN.SunUV;
	Delta.x *= ScreenSize.z;
	float Dist = length(Delta);
	
	//Angle of incidence: center of lens = normal, edges = oblique
	float EdgeDist = length(IN.texcoord.zw);
	float CosTheta = sqrt(saturate(1.0 - EdgeDist * EdgeDist * 0.5));
	
	//Thin-film interference reflectance per channel
	float3 CoatingRefl = ThinFilmReflectance(CosTheta, UITF_CoatingThick, UITF_IOR);
	
	//Tint with base coating color
	CoatingRefl *= UITF_BaseTint;
	
	//Spatial mask: coating reflection is a broad ghost centered opposite sun
	float GhostMask = exp(-Dist * Dist * 4.0);
	
	//Combine
	float3 Color = CoatingRefl * GhostMask * IN.SunCol * IN.SunInt;
	
	return float4(Color, 1.0);
}


//=== Post-Processing: apply atmospheric extinction, weather, color grade ===//
//  These modify the GetSunIntensity result before it reaches individual
//  flare shaders. Since sunsprite uses additive blending per-pass, we
//  apply global modifications at the intensity detection level.
//  The functions above are called from within GetSunIntensity via the
//  following inline modifications to the existing function:

//Note: The atmospheric extinction, weather dampening, and flare color
//grade are integrated directly into the GetSunIntensity function at the
//end of the function, where the final Intensity * FlareInt is computed.
//See the modifications to GetSunIntensity below.


//----------------------------------------------------------------------------------------------//
//										    Techniques											//
//																								//
//								   Boris' comment on techniques:								//
//    In this shader all techniques have additive blending mode (srcblend=one, destblend=one)	//
//								 No temporary render targets used								//
//----------------------------------------------------------------------------------------------//

//---------------------------------------Technique Macros---------------------------------------//

#define GHOST_PASS(a) \
pass GhostPass##a { \
SetVertexShader(CompileShader(vs_5_0, VS_GhostSprite(UIG_Offset##a, \
												     UIG_Scale##a,   UIG_Int##a, \
												     UIG_CA##a,      UIG_Tint##a))); \
SetPixelShader (CompileShader(ps_5_0, PS_GhostSprite(UIG_Feather##a, UIG_DiffInt##a, UIG_DirtInt##a, \
												     UIG_Weight##a,  UIG_Vignette##a))); }

#define DGHOST_PASS(a) \
pass DuplicateGhostPassA##a { \
SetVertexShader(CompileShader(vs_5_0, VS_GhostSprite(UIDG_Offset##a, \
												     UIDG_Scale##a,   UIDG_Int##a, \
												     UIDG_CA##a,      UIDG_Tint##a))); \
SetPixelShader (CompileShader(ps_5_0, PS_GhostSprite(UIDG_Feather##a, UIDG_DiffInt##a, UIDG_DirtInt##a, \
												     UIDG_Weight##a,  UIDG_Vignette##a))); } \
pass DuplicateGhostPassB##a { \
SetVertexShader(CompileShader(vs_5_0, VS_GhostSprite(UIDG_Offset##a + UIDG_Dupl_Offset##a, \
												     UIDG_Scale##a,   UIDG_Int##a * (0.0 < abs(UIDG_Dupl_Offset##a)), \
												     UIDG_CA##a,      UIDG_Tint##a))); \
SetPixelShader (CompileShader(ps_5_0, PS_GhostSprite(UIDG_Feather##a, UIDG_DiffInt##a, UIDG_DirtInt##a, \
												     UIDG_Weight##a,  UIDG_Vignette##a))); }

#define ANAM_PASS(a) \
pass AnamPass##a { \
SetVertexShader(CompileShader(vs_5_0, VS_AnamorphicSprite(UIAF_Offset##a, UIAF_Width##a, \
														  UIAF_Int##a,    UIAF_Mirror##a))); \
SetPixelShader (CompileShader(ps_5_0, PS_AnamorphicSprite(UIAF_Curve##a))); }

#define LENSGLARE_PASS pass LensGlarePass { \
SetVertexShader(CompileShader(vs_5_0, VS_LensGlareSprite())); \
SetPixelShader (CompileShader(ps_5_0, PS_LensGlareSprite())); }

#define SUNGLARE_PASS pass SunGlarePass { \
SetVertexShader(CompileShader(vs_5_0, VS_SunGlareSprite())); \
SetPixelShader (CompileShader(ps_5_0, PS_SunGlareSprite())); }

#define STARBURST_PASS pass StarburstPass { \
SetVertexShader(CompileShader(vs_5_0, VS_StarburstSprite())); \
SetPixelShader (CompileShader(ps_5_0, PS_StarburstSprite())); }

#define HOOP_PASS pass HoopPass { \
SetVertexShader(CompileShader(vs_5_0, VS_HoopSprite())); \
SetPixelShader (CompileShader(ps_5_0, PS_HoopSprite())); }

#define AUREOLE_PASS pass AureolePass { \
SetVertexShader(CompileShader(vs_5_0, VS_Aureole())); \
SetPixelShader (CompileShader(ps_5_0, PS_Aureole())); }

#define CORONA_PASS pass CoronaPass { \
SetVertexShader(CompileShader(vs_5_0, VS_Corona())); \
SetPixelShader (CompileShader(ps_5_0, PS_Corona())); }

#define HALO22_PASS pass Halo22Pass { \
SetVertexShader(CompileShader(vs_5_0, VS_Halo(22.0, UIHalo22_Int, UIHalo22_Enable))); \
SetPixelShader (CompileShader(ps_5_0, PS_Halo(UIHalo22_Thickness, UIHalo22_InnerEdge))); }

#define HALO46_PASS pass Halo46Pass { \
SetVertexShader(CompileShader(vs_5_0, VS_Halo(46.0, UIHalo46_Int, UIHalo46_Enable))); \
SetPixelShader (CompileShader(ps_5_0, PS_Halo(UIHalo46_Thickness, 8.0))); }

#define SUNDOG_LEFT_PASS pass SunDogLeftPass { \
SetVertexShader(CompileShader(vs_5_0, VS_SunDog(true))); \
SetPixelShader (CompileShader(ps_5_0, PS_SunDog(true))); }

#define SUNDOG_RIGHT_PASS pass SunDogRightPass { \
SetVertexShader(CompileShader(vs_5_0, VS_SunDog(false))); \
SetPixelShader (CompileShader(ps_5_0, PS_SunDog(false))); }

#define VEILGLARE_PASS pass VeilingGlarePass { \
SetVertexShader(CompileShader(vs_5_0, VS_VeilingGlare())); \
SetPixelShader (CompileShader(ps_5_0, PS_VeilingGlare())); }

#define EYELASH_PASS pass EyelashPass { \
SetVertexShader(CompileShader(vs_5_0, VS_Eyelash())); \
SetPixelShader (CompileShader(ps_5_0, PS_Eyelash())); }

#define BLOOMCROSS_PASS pass BloomCrossPass { \
SetVertexShader(CompileShader(vs_5_0, VS_BloomCross())); \
SetPixelShader (CompileShader(ps_5_0, PS_BloomCross())); }

#define DIRTYLENS_PASS pass DirtyLensPass { \
SetVertexShader(CompileShader(vs_5_0, VS_DirtyLens())); \
SetPixelShader (CompileShader(ps_5_0, PS_DirtyLens())); }

#define THINFILM_PASS pass ThinFilmPass { \
SetVertexShader(CompileShader(vs_5_0, VS_ThinFilm())); \
SetPixelShader (CompileShader(ps_5_0, PS_ThinFilm())); }


//-------------------------------------Sunsprite Technique--------------------------------------//


technique11 KitsuuneSunspriteGD <string UIName="Sunsprite - Kitsuune";>
{
	//Atmospheric scattering
	VEILGLARE_PASS AUREOLE_PASS
	//Ice crystal optics
	HALO22_PASS    HALO46_PASS
	SUNDOG_LEFT_PASS SUNDOG_RIGHT_PASS
	//Ocular diffraction
	CORONA_PASS    EYELASH_PASS
	//Lens optics
	ANAM_PASS(1)   ANAM_PASS(2)   ANAM_PASS(3)
	LENSGLARE_PASS SUNGLARE_PASS  STARBURST_PASS
	GHOST_PASS(1)  GHOST_PASS(2)  GHOST_PASS(3)
	GHOST_PASS(4)  GHOST_PASS(5)  GHOST_PASS(6)
	GHOST_PASS(7)  GHOST_PASS(8)  GHOST_PASS(9)
	DGHOST_PASS(1) DGHOST_PASS(2) DGHOST_PASS(3)
	DGHOST_PASS(4) DGHOST_PASS(5) DGHOST_PASS(6)
	DGHOST_PASS(7) DGHOST_PASS(8) HOOP_PASS
	//Sensor
	BLOOMCROSS_PASS
	//Cinematic & Realism extensions
	DIRTYLENS_PASS
	THINFILM_PASS
}






