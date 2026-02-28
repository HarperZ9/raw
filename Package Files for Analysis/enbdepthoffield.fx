//----------------------------------------------------------------------------------------------//
//							ADVANCED DEPTH OF FIELD KITSUUNE EDITION							//
//----------------------------------------------------------------------------------------------//
//																								//
//				     enbdepthoffield.fx file by LonelyKitsuune aka Skratzer						//
//						      for ENB (DirectX 11 Shader Model 5)								//
//																								//
//			   Copyright (c) 2019-2020 LonelyKitsuune / T.Thanner - CC BY-NC-ND 4.0				//
//																								//
//-------------------------------------------CREDITS--------------------------------------------//
//																								//
//				 Marty McFly for his Advanced Depth of Field 3.0 shader which					//
//								 served as a basis for this file								//
//		       BY-NC-ND 3.0 Copyright (c) 2008-2018 Marty McFly / Pascal Gilcher				//
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
//Enables full local control over this shader file
#define LOCAL_OVERRIDE					0	//[0-1]


//--------------------------------------------------------------------------------
//DOF OPTIONS
//--------------------------------------------------------------------------------
//Enables partial occlusion of bokeh discs at screen corners
#define ENABLE_OPTICAL_VIGNETTE			1	//[0-1]-G


//Enables bokeh specific chromatic aberration
#define ENABLE_CHROMATIC_ABERRATION		1	//[0-1]-G


//Enables uneven weighting of the bokeh discs
#define ENABLE_SPHERICAL_ABERRATION		1	//[0-1]-G


//Enables bokeh shape diffraction
#define ENABLE_DIFFRACTION				1	//[0-1]-G


//Enables DoF limited graining
// 1 - Individual bokeh shape grain
// 2 - Full screen grain
#define ENABLE_GRAINING					2	//[0-2]-G


//Enables the option to visualize the focusing area
#define ENABLE_FOCUSING_TOOL			1	//[0-1]-G


//Enables custom texture-based bokeh shapes
#define ENABLE_STYLIZED_BOKEHSHAPES		0	//[0-1]-G


//EXPERIMENTAL - Enables the option to freezes the
//current background bokeh blur
#define ENABLE_FAR_BOKEH_FREEZING		0	//[0-1]


//Enable bilateral filtering to get sharper bokeh shapes while
//still being able to use higher "gaussian width" values
#define ENABLE_BILATERAL_GAUSSIAN_BLUR	0	//[0-1]


//=============================================================================//
//  CINEMATIC & REALISM EXTENSIONS                                             //
//=============================================================================//

//Enables tilt-shift simulation with adjustable focus plane angle
//Creates miniature/diorama effect or cinematic selective focus
#define ENABLE_TILT_SHIFT				0	//[0-1]-G

//Enables cat's eye bokeh (mechanical vignetting)
//Bokeh discs become lemon/oval shaped toward frame edges
//due to physical lens barrel clipping — a hallmark of fast primes
#define ENABLE_CATS_EYE					1	//[0-1]-G

//Enables swirly bokeh (Petzval lens simulation)
//Background blur rotates around center, producing a helicoidal swirl
//characteristic of vintage Petzval and Helios 44-2 lenses
#define ENABLE_SWIRLY_BOKEH				0	//[0-1]-G

//Enables anamorphic bokeh stretch
//Vertically squeezes bokeh discs to create the oval highlights
//characteristic of cinemascope anamorphic projection lenses
#define ENABLE_ANAMORPHIC_BOKEH			0	//[0-1]-G

//Enables longitudinal chromatic aberration on bokeh disc edges
//Green/magenta fringing on foreground vs background bokeh
//Models axial color shift from uncorrected lens elements
#define ENABLE_BOKEH_FRINGING			1	//[0-1]-G

//Enables subtle FOV shift that compensates focus breathing
//Real cinema lenses change focal length slightly when refocusing
#define ENABLE_FOCUS_BREATHING			0	//[0-1]-G

//Enables bloom enhancement on bright bokeh highlights
//Adds a soft glow around specular bokeh for cinematic pop
#define ENABLE_HIGHLIGHT_BLOOM			1	//[0-1]-G

//Enables depth-aware dithering to eliminate banding
//in smooth DoF gradient transitions
#define ENABLE_DEPTH_DITHER				1	//[0-1]-G


//----------------------------------------------------------------------------------------------//
//										 Global Parameters										//
//----------------------------------------------------------------------------------------------//

#if !LOCAL_OVERRIDE
#undef ENABLE_OPTICAL_VIGNETTE
#undef ENABLE_CHROMATIC_ABERRATION
#undef ENABLE_FOCUSING_TOOL
#undef ENABLE_STYLIZED_BOKEHSHAPES
#undef ENABLE_SPHERICAL_ABERRATION
#undef ENABLE_DIFFRACTION
#undef ENABLE_GRAINING
#undef ENABLE_TILT_SHIFT
#undef ENABLE_CATS_EYE
#undef ENABLE_SWIRLY_BOKEH
#undef ENABLE_ANAMORPHIC_BOKEH
#undef ENABLE_BOKEH_FRINGING
#undef ENABLE_FOCUS_BREATHING
#undef ENABLE_HIGHLIGHT_BLOOM
#undef ENABLE_DEPTH_DITHER
#include "enbglobals.fxh"
#endif //LOCAL_OVERRIDE


//----------------------------------------------------------------------------------------------//
//										Internal Parameters										//
//----------------------------------------------------------------------------------------------//

//Gives the option to visualize the different CoC textures
#define ENABLE_COC_DEBUGGING 0

#include "UI/enbUI_Primer.fxh"

#define SHADERGROUP 0
#include "UI/enbUI_DepthOfField.fxh"
#undef SHADERGROUP

//Focus, DOF and Bokeh
#define SHADERGROUP 1
#include "UI/enbUI_DepthOfField.fxh"
#undef SHADERGROUP

//DOF Color Correction
UI_WHITESPACE(11)
UI_WHITESPACE(12)
#define SHADERGROUP 2
#define TODIE Day
#include "UI/enbUI_DepthOfField.fxh"
UI_SPECIAL_WHITESPACE(12)

#define TODIE Night
#include "UI/enbUI_DepthOfField.fxh"
UI_SPECIAL_WHITESPACE(13)

#define TODIE Interior
#include "UI/enbUI_DepthOfField.fxh"
UI_WHITESPACE(13)
UI_WHITESPACE(14)
#undef SHADERGROUP
#undef NOTFIRSTTIME


//=============================================================================//
//  CINEMATIC & REALISM UI PARAMETERS                                          //
//=============================================================================//

// ─────────────────── Tilt-Shift ────────────────────────────────────────────
#if ENABLE_TILT_SHIFT
int _hdrTS < string UIName = "======= TILT-SHIFT (Cinematic) ======="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
bool  UITS_Enable     < string UIName = "TS | Enable"; > = false;
float UITS_Angle      < string UIName = "TS | Plane Angle";     string UIWidget = "spinner"; float UIMin = -90.0; float UIMax = 90.0;  float UIStep = 1.0;  > = 0.0;
float UITS_Offset     < string UIName = "TS | Plane Offset";    string UIWidget = "spinner"; float UIMin = -0.5;  float UIMax = 0.5;   float UIStep = 0.01; > = 0.0;
float UITS_Width      < string UIName = "TS | Focus Band Width"; string UIWidget = "spinner"; float UIMin = 0.02;  float UIMax = 0.5;   float UIStep = 0.01; > = 0.15;
float UITS_Falloff    < string UIName = "TS | Blur Falloff";    string UIWidget = "spinner"; float UIMin = 0.5;   float UIMax = 4.0;   float UIStep = 0.1;  > = 1.5;
float UITS_BlurMax    < string UIName = "TS | Max Blur Amount"; string UIWidget = "spinner"; float UIMin = 0.1;   float UIMax = 1.0;   float UIStep = 0.01; > = 1.0;
#endif

// ─────────────────── Cat's Eye Bokeh ───────────────────────────────────────
#if ENABLE_CATS_EYE
int _hdrCE < string UIName = "======= CAT'S EYE BOKEH (Realism) ======="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
float UICE_Amount     < string UIName = "CE | Clipping Amount";  string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 1.0;   float UIStep = 0.01; > = 0.45;
float UICE_Onset      < string UIName = "CE | Field Onset";      string UIWidget = "spinner"; float UIMin = 0.1;  float UIMax = 0.8;   float UIStep = 0.01; > = 0.3;
float UICE_Squeeze    < string UIName = "CE | Radial Squeeze";   string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 1.0;   float UIStep = 0.01; > = 0.6;
#endif

// ─────────────────── Swirly Bokeh ──────────────────────────────────────────
#if ENABLE_SWIRLY_BOKEH
int _hdrSW < string UIName = "======= SWIRLY BOKEH (Cinematic) ======="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
bool  UISW_Enable     < string UIName = "SW | Enable"; > = false;
float UISW_Amount     < string UIName = "SW | Rotation Amount";  string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 2.0;   float UIStep = 0.01; > = 0.5;
float UISW_Onset      < string UIName = "SW | Center Dead Zone"; string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 0.5;   float UIStep = 0.01; > = 0.1;
float UISW_Direction   < string UIName = "SW | Direction (CW/CCW)"; string UIWidget = "spinner"; float UIMin = -1.0; float UIMax = 1.0;  float UIStep = 0.1;  > = 1.0;
#endif

// ─────────────────── Anamorphic Bokeh ──────────────────────────────────────
#if ENABLE_ANAMORPHIC_BOKEH
int _hdrAB < string UIName = "======= ANAMORPHIC BOKEH (Cinematic) ======="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
bool  UIAB_Enable     < string UIName = "AB | Enable"; > = false;
float UIAB_Squeeze    < string UIName = "AB | Squeeze Ratio";    string UIWidget = "spinner"; float UIMin = 1.0;  float UIMax = 2.0;   float UIStep = 0.01; > = 1.33;
float UIAB_Angle      < string UIName = "AB | Squeeze Angle";    string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 180.0; float UIStep = 1.0;  > = 90.0;
#endif

// ─────────────────── Bokeh Fringing ────────────────────────────────────────
#if ENABLE_BOKEH_FRINGING
int _hdrBF < string UIName = "======= BOKEH FRINGING (Realism) ======="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
float UIBF_Amount     < string UIName = "BF | Fringe Intensity"; string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 1.0;   float UIStep = 0.01; > = 0.25;
int   UIBF_Mode       < string UIName = "BF | Mode (0=green/mag, 1=blue/amber)"; string UIWidget = "spinner"; int UIMin = 0; int UIMax = 1; > = 0;
float UIBF_FarBias    < string UIName = "BF | Far Fringe Bias";  string UIWidget = "spinner"; float UIMin = -1.0; float UIMax = 1.0;   float UIStep = 0.01; > = 0.5;
#endif

// ─────────────────── Focus Breathing ───────────────────────────────────────
#if ENABLE_FOCUS_BREATHING
int _hdrFB < string UIName = "======= FOCUS BREATHING (Realism) ======="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
bool  UIFB_Enable     < string UIName = "FB | Enable"; > = false;
float UIFB_Amount     < string UIName = "FB | Breathing Amount";  string UIWidget = "spinner"; float UIMin = 0.0;   float UIMax = 0.03;  float UIStep = 0.001; > = 0.008;
float UIFB_NeutralDist < string UIName = "FB | Neutral Distance"; string UIWidget = "spinner"; float UIMin = 0.01;  float UIMax = 0.5;   float UIStep = 0.01;  > = 0.1;
#endif

// ─────────────────── Highlight Bloom ───────────────────────────────────────
#if ENABLE_HIGHLIGHT_BLOOM
int _hdrHB < string UIName = "======= HIGHLIGHT BLOOM (Cinematic) ======="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
float UIHB_Threshold  < string UIName = "HB | Luminance Threshold"; string UIWidget = "spinner"; float UIMin = 0.5;  float UIMax = 5.0;  float UIStep = 0.01; > = 1.5;
float UIHB_Amount     < string UIName = "HB | Bloom Amount";       string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 1.0;  float UIStep = 0.01; > = 0.3;
float UIHB_Radius     < string UIName = "HB | Bloom Radius";       string UIWidget = "spinner"; float UIMin = 0.5;  float UIMax = 3.0;  float UIStep = 0.1;  > = 1.2;
float UIHB_Saturation < string UIName = "HB | Bloom Saturation";   string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 1.5;  float UIStep = 0.01; > = 0.7;
#endif

// ─────────────────── Depth Dithering ───────────────────────────────────────
#if ENABLE_DEPTH_DITHER
float UIDD_Amount     < string UIName = "DD | Dither Strength"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.5;
#endif


//=============================================================================//
//  SKYRIMBRIDGE DOF UI PARAMETERS                                             //
//=============================================================================//

#define SHADERGROUP 99
#include "UI/enbUI_DepthOfField.fxh"
#undef SHADERGROUP


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
float4 DofParameters;	// z = ApertureTime multiplied by time elapsed, w = FocusingTime multiplied by time elapsed


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
//                      SkyrimBridge DOF Integration Helpers                                     //
//                                                                                              //
//   Game-aware modifiers for focus, CoC, and DOF state.                                        //
//   All functions return neutral values when SB is inactive or not installed.                   //
//----------------------------------------------------------------------------------------------//
#ifdef SKYRIMBRIDGE_FXH

// Returns a CoC multiplier based on current game state (combat, killcam, slow-time, etc.)
// Values < 1 reduce blur (combat clarity), values > 1 increase blur (cinematic)
float SB_DOF_GetCoCScale()
{
    float Scale = 1.0;
    
    [branch] if(!SB_IsActive()) return Scale;
    
    // Combat clarity: reduce DOF for gameplay readability
    [flatten] if(UISB_CombatClarity > 0.5 && SB_Player_Combat.x > 0.5)
        Scale *= lerp(1.0, UISB_CombatReduce, SB_Player_Combat.x);
    
    // Killcam: cinematic shallow DOF rack focus
    [flatten] if(UISB_KillcamDOF > 0.5 && SB_Player_Combat.z > 0.5)
        Scale *= UISB_KillcamStrength;
    
    // Bleedout: heavy cinematic blur
    [flatten] if(UISB_BleedoutDOF > 0.5 && SB_Player_Combat.y > 0.5)
        Scale *= UISB_BleedoutStrength;
    
    // Slow-time: tighter focus for bullet-time effect
    [flatten] if(UISB_SlowTimeDOF > 0.5 && SB_FX_Time.x < 0.99)
        Scale *= lerp(UISB_SlowTimeMult, 1.0, SB_FX_Time.x);
    
    // Night Eye: reduce DOF (flat lighting destroys depth cues)
    [flatten] if(UISB_NightEyeClarity > 0.5 && SB_FX_Vision.x > 0.5)
        Scale *= (1.0 - UISB_NightEyeReduceAmt);
    
    // Mounted: push focus range out for wider depth of field
    [flatten] if(UISB_MountedDOF > 0.5 && SB_Player_Movement.w > 0.5)
        Scale *= rcp(UISB_MountedRangeMult);
    
    return Scale;
}


// Returns true if DOF should be completely skipped this frame
bool SB_DOF_ShouldSkipFrame()
{
    [branch] if(!SB_IsActive()) return false;
    
    // Skip DOF when in menus (prevents autofocus hunting on UI overlays)
    if(UISB_MenuBypass > 0.5 && SB_UI_Menus.x > 0.5)
        return true;
    
    // Skip DOF underwater (water optics replace DOF)
    if(UISB_UnderwaterSkip > 0.5 && SB_Player_Water.x > 0.5)
        return true;
    
    return false;
}


// Returns true if focus transition should be frozen (menus, dialogue)
bool SB_DOF_ShouldFreezeFocus()
{
    [branch] if(!SB_IsActive()) return false;
    
    // Freeze during menus to prevent focus hunting
    if(UISB_MenuBypass > 0.5 && SB_UI_Menus.x > 0.5)
        return true;
    
    // Freeze during dialogue (shallow DOF on speaker)
    if(UISB_DialogueDOF > 0.5 && SB_UI_Menus.y > 0.5)
        return true;
    
    return false;
}


// Returns crosshair world depth as focus target, or -1 if unavailable
// SB_XHair_Info.y contains the distance to the crosshair target
float SB_DOF_GetCrosshairFocusDepth()
{
    [branch] if(!SB_IsActive()) return -1.0;
    if(UISB_CrosshairFocus < 0.5)  return -1.0;
    
    // XHair_Info.x > 0 means crosshair has a valid hit target
    if(SB_XHair_Info.x < 0.5) return -1.0;
    
    // Convert world distance to normalized depth [0,1] matching FastLinDepth output
    float WorldDist = SB_XHair_Info.y;
    float FarPlane  = (UISB_AccurateFarPlane > 0.5) ? SB_Camera_Info.z : 2999.0;
    return saturate(WorldDist / FarPlane);
}


// Computes physical Circle of Confusion using thin-lens model
// Returns CoC in pixels for a given normalized depth and focus depth
float SB_DOF_PhysicalCoC(float Depth, float FocusDepth)
{
    [branch] if(!SB_IsActive() || UISB_PhysicalCoC < 0.5) return -1.0;
    
    // Derive focal length from FOV and sensor size
    // f = (sensor / 2) / tan(FOV/2)
    float FOV_rad    = radians(SB_Camera_Info.x);
    float FocalLen   = (UISB_SensorSize * 0.5) / tan(FOV_rad * 0.5);
    float Aperture   = FocalLen / max(UISB_FStop, 0.5);
    
    // Convert normalized depths to world units
    float FarPlane = (UISB_AccurateFarPlane > 0.5) ? SB_Camera_Info.z : 2999.0;
    float D_focus  = FocusDepth * FarPlane;
    float D_object = max(Depth * FarPlane, FocalLen + 0.01);
    
    // Thin lens CoC = |A * f * (d - D)| / (d * (D - f))
    float CoC = abs(Aperture * FocalLen * (D_object - D_focus))
              / (D_object * max(D_focus - FocalLen, 0.01));
    
    // Convert mm to pixels (sensor size is in mm, CoC is in mm on sensor)
    float PixelsPerMM = ScreenSize.x / UISB_SensorSize;
    return CoC * PixelsPerMM;
}

#endif //SKYRIMBRIDGE_FXH

//----------------------------------------------------------------------------------------------//
//						      Game and mod parameters, do not modify							//
//																								//
//----------------------------------------------------------------------------------------------//

Texture2D TextureCurrent; 		//current frame focus depth or aperture. unused in dof computation
Texture2D TexturePrevious; 		//previous frame focus depth or aperture. unused in dof computation
Texture2D TextureOriginal; 		//color R16B16G16A16 64 bit hdr format
Texture2D TextureColor; 		//color which is output of previous technique (except when drawed to temporary render target), R16B16G16A16 64 bit hdr format
Texture2D TextureDepth; 		//scene depth R32F 32 bit hdr format
Texture2D TextureFocus; 		//this frame focus 1*1 R32F hdr red channel only. computed in PS_Focus
Texture2D TextureAperture; 		//this frame aperture 1*1 R32F hdr red channel only. computed in PS_Aperture
Texture2D TextureAdaptation;	//previous frame vanilla or enb adaptation 1*1 R32F hdr red channel only (computed after depth of field)

//temporary textures which can be set as render target for techniques via annotations like <string RenderTarget="RenderTargetRGBA32";>
Texture2D RenderTargetRGBA32; 		//R8G8B8A8		32 bit ldr format
Texture2D RenderTargetRGBA64; 		//R16B16G16A16	64 bit ldr format
Texture2D RenderTargetRGBA64F; 		//R16B16G16A16F 64 bit hdr format
Texture2D RenderTargetR16F; 		//R16F			16 bit hdr format with red channel only
Texture2D RenderTargetR32F; 		//R32F			32 bit hdr format with red channel only
Texture2D RenderTargetRGB32F; 		//R8G8B8F		32 bit hdr format without alpha

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

//Helper with some useful macros, variables and functions
#include "Helper/enbHelper_Common.fxh"
static const float2 PixelSize = _HLP_PixelSize;
static const float2 ScreenRes = _HLP_ScreenRes;


//----------------------------------------------------------------------------------------------//

#if ENABLE_STYLIZED_BOKEHSHAPES
//Mask atlas texture for stylized bokeh shapes (greyscale image)
Texture2D BokehMaskAtlas <string ResourceName="Textures/BokehMaskAtlas.png";>;

//Atlas grid size and maximum mipmap level (higher max LOD = lower possible res)
#define BOKEHMASK_GRID_SIZE 3
#define BOKEHMASK_MAX_LOD   4
#endif

static const float BlurQuality[3]      = { 1.9, 1.5, 1.2 };
static const float SqrtTwoPI           = sqrt(2.0 * PI);
static const float GaussBleedReduction = 0.5;


//----------------------------------------------------------------------------------------------//

#define DISCRADIUS_RESOLUTION_BOUNDARY_LOWER_FAR 0.25   // Used for blending blurred scene
#define DISCRADIUS_RESOLUTION_BOUNDARY_UPPER_FAR 1.0    // Used for blending blurred scene
#define DISCRADIUS_RESOLUTION_BOUNDARY_CURVE_FAR 0.5    // Used for blending blurred scene

#define BOKEH_CURVE_DISCARD_BOUNDARY_FAR		 3.6    // Skip far  bokeh computation if far  blur curve parameter >= x
#define BOKEH_CURVE_DISCARD_BOUNDARY_NEAR 		 2.4    // Skip near bokeh computation if near blur curve parameter >= x

#define ENABLE_AGRESSIVE_FPS_WEAPON_FIX			 1		// [0-1] Ignore autofocus samples even if they are only partially occluded by fps hands or weapons
#define FPS_HAND_BLUR_CUTOFF_DIST				 0.3468 // FPS hand depth (x10.000), change if you perceive blurred fps weapons
#define FPS_HAND_BLUR_CUTOFF_CHECK				 0      // [0-1] blur = max if depth > hand depth, else 0, useful for tweaking above param

#define GAUSSIAN_BUILDUP_MULT					 4.0    // Value of x -> gaussian blur        reaches max radius   at |CoC| == 1/x
#define CHROMA_BUILUP_MULT						 2.0    // Value of x -> chromatic aberration reaches max strength at |CoC| == 1/x


//----------------------------------------------------------------------------------------------//

//Alpha passthrough for prepass skin mask
#if ENABLE_SKIN_ATTENUATION
		#define ALPHAOUT TextureOriginal.Sample(Point_Sampler, IN.texcoord).a
#else
		#define ALPHAOUT 1.0
#endif


//----------------------------------------------------------------------------------------------//
//										      Structs											//
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

struct VertexShaderOutputDrawCoC
{
   float4 pos      : SV_POSITION;
   float2 texcoord : TEXCOORD0;
NI float  Focus    : TEXCOORD1;
};

struct VertexShaderOutputNearCoC
{
   float4 pos          : SV_POSITION;
   float2 texcoord     : TEXCOORD0;
NI float  BlurSteps    : TEXCOORD1;
NI float  Sigma        : TEXCOORD2;
NI float  WeightFactor : PSINFO0;
NI float4 StepSize     : PSINFO1;
};

struct VertexShaderOutputCombineCoC
{
   float4 pos          : SV_POSITION;
   float4 texcoord     : TEXCOORD0;
NI float  BlurSteps    : PSINFO0;
NI float  Sigma        : PSINFO1;
NI float  WeightFactor : PSINFO2;
NI float4 StepSize     : PSINFO3;
};

struct VertexShaderOutputDOF
{
   float4 pos          : SV_POSITION;
   float2 texcoord     : TEXCOORD0;
#if ENABLE_STYLIZED_BOKEHSHAPES
NI float2 AtlasSelect  : TEXCOORD1;
NI float2x2 RotMat     : TEXCOORD2;
NI float2 AtlasTileRes : TEXCOORD4;
#endif
NI float2 vertices[10] : VERTICES0;
};

struct VertexShaderOutputCombine
{
   float4 pos             : SV_POSITION;
   float4 texcoord        : TEXCOORD;
NI float DOFCG_Saturation : DNI0;
NI float DOFCG_Brightness : DNI1;
NI float DOFCG_Contrast   : DNI2;
};

struct BokehUIParams
{
   float ShapeQuality;
   float VertexCount;
   float Curvature;
   float Intensity;
   float SphericalAmount;
   float GrainAmount;
   float DiffractionAmount;
   float ShapeVignetteAmount;
};


//----------------------------------------------------------------------------------------------//
//										    Functions											//
//																								//
//----------------------------------------------------------------------------------------------//

float4 GetCircleOfConfusion(float2 Coords)
{
	float4 CoC   = RenderTargetRGBA32.Sample(Point_Sampler, Coords);
		   CoC.y = CoC.y * 2.0 - 1.0;
	return CoC;
}


float4 DebugCircleOfConfusion(float2 Coords)
{ return RenderTargetRGBA32.Sample(Point_Sampler, Coords); }


//----------------------------------------------------------------------------------------------//

float GetLinearAndMinDepth_Precise(float2 Coords, out float MinDepth)
{
	//Early depth linearization and manual bilinear filtering
	float4 DepthSamples = TextureDepth.Gather(Linear_Sampler, Coords);
		   DepthSamples = FastLinDepth(DepthSamples, 2999.0);
		   MinDepth     = min4(DepthSamples);
	
	float2 FracPos = frac(Coords * ScreenRes - 0.5);
	float2 LinearX = lerp(DepthSamples.wx, DepthSamples.zy, FracPos.x);
	float  LinearY = lerp(LinearX.x, LinearX.y, FracPos.y);
	
	return LinearY;
}


//----------------------------------------------------------------------------------------------//

void ShapeRoundness(inout float2 sampleOffset, in float roundness)
{ sampleOffset *= (1.0 - roundness) + roundness * rsqrt(dot(sampleOffset, sampleOffset)); }

void OpticalVignette(in float2 sampleOffset, in float2 centerVec, inout float sampleWeight)
{
	sampleOffset -= centerVec; //scaled by vignette intensity
	sampleWeight *= saturate(3.33 - dot(sampleOffset, sampleOffset) * 1.666); //notsosmoothstep to avoid aliasing
}


float2 CoC2BlurRadius(float CoC)
{
	float ShapeRadius   = UI_ShapeRadius;
	float AnamorphRatio = UI_ShapeAnamorphRatio;
	
	#if ENABLE_STYLIZED_BOKEHSHAPES
	if(UI_EnableStyle)
	{
		ShapeRadius   = UI_StyleRadius;
		AnamorphRatio = 1.0;
	}
	#endif
	
	return float2(AnamorphRatio, ScreenSize.z) * CoC * ShapeRadius * 6e-4;
}


//----------------------------------------------------------------------------------------------//

void GetGaussianWeight(out float CurrWeight, float BaseWeight, float4 CurrTap, float4 CentTap, float Iterator)
{
	//CurrWeight = BaseWeight * saturate(CurrTap.a - CentTap.a * 0.25);
	CurrWeight = BaseWeight * saturate(1.0 - (CentTap.a - CurrTap.a * GaussBleedReduction) * Iterator);
	
	#if ENABLE_BILATERAL_GAUSSIAN_BLUR
	CurrWeight *= saturate(1.0 - sqrt(distance(CentTap.rgb, CurrTap.rgb) * 1.5));
	#endif
}


void ApplyDoFPostGraining(inout float3 Color, float2 Coords, float GrainInt)
{
	[branch] if(GrainInt > 0.01)
	{
		float3 Grain;
		float3 TimerSeed = Timer.x * UI_GrainMotion + float3(0.01, 0.02, 0.03);
		
		Grain.r = RandomGauss(Coords + float2(0.0, TimerSeed.x));
		Grain.g = RandomGauss(Coords + float2(TimerSeed.y, 0.0));
		Grain.b = RandomGauss(Coords + TimerSeed.z);
		Grain	= lerp(dot(Grain, N_LUM), Grain, UI_GrainSaturation);
		
		Color = zerolim(Color + Grain * GrainInt - GrainInt * 0.5);
	}
}


//----------------------------------------------------------------------------------------------//

#if ENABLE_STYLIZED_BOKEHSHAPES
float2 GetBokehAtlasTileRes()
{
	float2 Res;
	BokehMaskAtlas.GetDimensions(Res.x, Res.y);
	return Res / BOKEHMASK_GRID_SIZE;
}


void MaskBokehShape(float2 MaskCoords, float2 AtlasSelect, float LOD, inout float sampleWeight)
{    
	MaskCoords    = MaskCoords * -0.75 + 0.5;
	sampleWeight *= all(saturate(MaskCoords - MaskCoords * MaskCoords));
	MaskCoords    = (AtlasSelect + MaskCoords) / BOKEHMASK_GRID_SIZE;
	sampleWeight *= BokehMaskAtlas.SampleLevel(Linear_Sampler, MaskCoords, LOD).x;
}


float2 VecMatrixMul2x2(float2 Vec, float2x2 Matrix)
{ return float2(dot(Vec, Matrix._11_12), dot(Vec, Matrix._21_22)); }
#endif


float GetSmootheningAmount(float SampleWeight)
{
	float SmootheningAmount =
	#if ENABLE_STYLIZED_BOKEHSHAPES
		UI_EnableStyle ? UI_StyleSmoothening :
	#endif
	UI_SmootheningAmount;
	
	return SampleWeight * SmootheningAmount + DELTA;
}


BokehUIParams GetUIBokehParams()
{
	BokehUIParams UI;
	#if ENABLE_STYLIZED_BOKEHSHAPES
	if(UI_EnableStyle)
	{
		UI.ShapeQuality = UI_StyleRadius * 0.2 * UI_StyleQualityLevel;
		UI.VertexCount  = 4.0;
		UI.Curvature    = 0.0;
		UI.Intensity    = UI_StyleIntensity;
		
		#if ENABLE_SPHERICAL_ABERRATION
			UI.SphericalAmount     = 0.0;
		#endif
		#if ENABLE_GRAINING == 1
			UI.GrainAmount         = 0.0;
		#endif
		#if ENABLE_DIFFRACTION
			UI.DiffractionAmount   = 0.0;
		#endif
		#if ENABLE_OPTICAL_VIGNETTE
			UI.ShapeVignetteAmount = 0.0;
		#endif
	}
	else
	#endif
	{
		UI.ShapeQuality = UI_ShapeQuality;
		UI.VertexCount  = UI_ShapeVertices;
		UI.VertexCount -= UI_ShapeCut ? ceil(UI.VertexCount * 0.5) : 0.0;
		UI.Curvature    = UI_ShapeCurvatureAmount;
		UI.Intensity    = UI_BokehIntensity;
		
		#if ENABLE_SPHERICAL_ABERRATION
			UI.SphericalAmount     = UI_SphericalAmount;
		#endif
		#if ENABLE_GRAINING == 1
			UI.GrainAmount         = UI_GrainAmount;
		#endif
		#if ENABLE_DIFFRACTION
			UI.DiffractionAmount   = UI_DiffractionAmount;
		#endif
		#if ENABLE_OPTICAL_VIGNETTE
			UI.ShapeVignetteAmount = UI_ShapeVignetteAmount;
		#endif
	}
	return UI;
}


//----------------------------------------------------------------------------------------------//
//                        Cinematic & Realism Extension Functions                               //
//----------------------------------------------------------------------------------------------//

//Interleaved gradient noise (Jimenez 2014) for stable temporal dithering
float IGNoise_DoF(float2 PixelCoord)
{
	return frac(52.9829189 * frac(dot(PixelCoord, float2(0.06711056, 0.00583715))));
}


#if ENABLE_TILT_SHIFT
//Computes tilt-shift CoC override based on angled focus plane
float ComputeTiltShiftCoC(float2 Coords)
{
	float2 CenteredUV = Coords - 0.5 - float2(0.0, UITS_Offset);
	float  AngleRad   = radians(UITS_Angle);
	float PlaneDist = CenteredUV.x * sin(AngleRad) + CenteredUV.y * cos(AngleRad);
	float Blur = saturate(abs(PlaneDist) / max(UITS_Width, DELTA));
	Blur = pow(Blur, UITS_Falloff) * UITS_BlurMax;
	return Blur;
}
#endif


#if ENABLE_CATS_EYE
//Cats eye mechanical vignetting: clips bokeh disc into lemon shape
//at frame edges, simulating lens barrel obstruction of fast primes
void ApplyCatsEye(inout float2 sampleOff, float2 ScreenUV)
{
	float2 FieldPos  = ScreenUV - 0.5;
	float  FieldDist = length(FieldPos) * 2.0;
	float ClipFactor = smoothstep(UICE_Onset, 1.0, FieldDist) * UICE_Amount;
	[branch] if(ClipFactor > DELTA)
	{
		float2 RadDir = normalize(FieldPos + DELTA);
		float  RadComp = dot(sampleOff, RadDir);
		float  Squeeze = lerp(1.0, 1.0 - UICE_Squeeze, ClipFactor);
		sampleOff -= RadDir * RadComp * (1.0 - Squeeze);
	}
}
#endif


#if ENABLE_SWIRLY_BOKEH
//Petzval swirly bokeh: rotates samples based on field angle
void ApplySwirlRotation(inout float2 sampleOff, float2 ScreenUV, float CoC)
{
	float2 FieldPos  = ScreenUV - 0.5;
	float  FieldDist = length(FieldPos) * 2.0;
	float SwirlAngle = smoothstep(UISW_Onset, 1.0, FieldDist);
	SwirlAngle *= UISW_Amount * UISW_Direction * CoC * 0.5;
	float S2, C2;
	sincos(SwirlAngle, S2, C2);
	sampleOff = float2(sampleOff.x*C2 - sampleOff.y*S2, sampleOff.x*S2 + sampleOff.y*C2);
}
#endif


#if ENABLE_ANAMORPHIC_BOKEH
//Anamorphic squeeze: creates oval bokeh from cinemascope-style lenses
void ApplyAnamorphicSqueeze(inout float2 sampleOff)
{
	float AngleRad = radians(UIAB_Angle);
	float S3, C3;
	sincos(AngleRad, S3, C3);
	float2 Rot = float2(sampleOff.x*C3 + sampleOff.y*S3, sampleOff.x*-S3 + sampleOff.y*C3);
	Rot.y /= UIAB_Squeeze;
	sampleOff = float2(Rot.x*C3 - Rot.y*S3, Rot.x*S3 + Rot.y*C3);
}
#endif


#if ENABLE_BOKEH_FRINGING
//Longitudinal CA on bokeh disc edges: green/magenta or blue/amber fringing
float3 ApplyBokehFringing(float3 BCol, float RingPos, float CoCSigned)
{
	float EdgeFactor = smoothstep(0.3, 0.9, RingPos);
	float FM = EdgeFactor * UIBF_Amount;
	float Dir = sign(CoCSigned + UIBF_FarBias * 0.01);
	float3 R = BCol;
	if(UIBF_Mode == 0) { R.g += FM*Dir*0.15; R.rb -= FM*Dir*0.08; }
	else { R.b += FM*Dir*0.12; R.r += FM*Dir*0.06; R.g -= FM*Dir*0.1; }
	return max(R, 0.0);
}
#endif


#if ENABLE_HIGHLIGHT_BLOOM
//Cinematic bokeh highlight bloom: soft glow on specular highlights
float3 ApplyHighlightBloom(float3 BSumC, float3 BMaxC, float CoCVal)
{
	float Luma = dot(BMaxC, N_LUM);
	float BM = saturate((Luma - UIHB_Threshold) * 2.0) * UIHB_Amount;
	BM *= saturate(CoCVal * 3.0);
	float3 BC = lerp(BMaxC, Luma, 1.0 - UIHB_Saturation);
	return BSumC + BC * BM;
}
#endif


#if ENABLE_DEPTH_DITHER
//Anti-banding dither for smooth DoF transitions
float3 ApplyDepthDither(float3 Color, float2 PixPos, float BW)
{
	float Noise = IGNoise_DoF(PixPos + Timer.z * 0.7);
	float DM = saturate(BW * (1.0 - BW) * 4.0);
	Color += (Noise - 0.5) * UIDD_Amount * 0.008 * DM;
	return max(Color, 0.0);
} 


//----------------------------------------------------------------------------------------------//
//										      Shaders											//
//																								//
//----------------------------------------------------------------------------------------------//

//------------------------------------Generic Vertex Shader-------------------------------------//

VertexShaderOutput VS_DoF(VertexShaderInput IN)
{
	VertexShaderOutput OUT;
	OUT.pos			= float4(IN.pos.xyz, 1.0);
	OUT.texcoord.xy	= IN.txcoord.xy;
	return OUT;
}

//----------------------------------------Focusing Pass-----------------------------------------//

//Kitsuune - Optimized focus pass by scaling down screen quad, improved depth
//			 sample accuracy and vastly improved FPS weapon cut out

VertexShaderOutput VS_ReadFocus(VertexShaderInput IN)
{
	VertexShaderOutput OUT;
	OUT.pos			= float4(IN.pos.xy * 0.0625 + float2(-0.9375, 0.9375), IN.pos.z, 1.0);
	OUT.texcoord.xy	= IN.txcoord.xy;
	return OUT;
}


//fullres -> 16x16 R32F (scaled down to 1x1 through VS)
float4 PS_ReadFocus(VertexShaderOutput IN) : SV_Target
{
	float scenefocus = 0.0;
	
	[branch] if(UI_FocusType != 3)
	{
		float weightsum = 1e-6;
		
		for(float xcoord = 0.0; xcoord < 10.0; xcoord++)
		for(float ycoord = 0.0; ycoord < 10.0; ycoord++)
		{
			float2 sampleOffset;
			sampleOffset  = float2(xcoord, ycoord) + 0.5;
			sampleOffset  = sampleOffset * 0.2 - 1.0;
			sampleOffset *=  (UI_FocusType == 1) ? UI_AutofocusRadius : UI_MousefocusRadius;
			sampleOffset += ((UI_FocusType == 1) ? UI_AutofocusCenter : tempInfo2.zw) - 0.5;
			
			float MinDepth, SampleWeight, TempFocus;
			SampleWeight  = saturate(1.2 * exp2(dot(sampleOffset, sampleOffset) * -4.0));
			TempFocus     = GetLinearAndMinDepth_Precise(sampleOffset + 0.5, MinDepth);
			SampleWeight /= TempFocus + DELTA;
			
			//Remove fps hands and weapons from focus calculations
			#if ENABLE_AGRESSIVE_FPS_WEAPON_FIX
				SampleWeight *= MinDepth  > FPS_HAND_BLUR_CUTOFF_DIST * 1e-4;
			#else
				SampleWeight *= TempFocus > FPS_HAND_BLUR_CUTOFF_DIST * 1e-4;
			#endif
			
			scenefocus += TempFocus * SampleWeight;
			weightsum  += SampleWeight;
		}
		
		scenefocus /= weightsum;
		
		#if ENABLE_AGRESSIVE_FPS_WEAPON_FIX
			scenefocus = (weightsum > 1e-6) ? scenefocus : -1.0;
		#endif
		
		//--- SkyrimBridge: Crosshair-locked autofocus ---
		// Uses actual crosshair world position for more reliable focus than
		// screen-center depth sampling, especially with first-person weapon occlusion
		#ifdef SKYRIMBRIDGE_FXH
		{
			float XHairDepth = SB_DOF_GetCrosshairFocusDepth();
			[branch] if(XHairDepth > 0.0)
				scenefocus = lerp(scenefocus, XHairDepth, UISB_CrosshairPriority);
		}
		#endif
	}
	else	scenefocus = UI_ManualfocusDepth;
	return	scenefocus;
}


//---------------------------------------Focus Transition---------------------------------------//

//16x16 (now 1x1 at [0,0]) -> 1x1 R32F
float4 PS_Focus(VertexShaderOutput IN) : SV_Target
{
	//Kitsuune - Optimized focus sampling
	float prevFocus = TexturePrevious.Load(0).x;
	float currFocus = TextureCurrent. Load(0).x;
	
	float Interpolator = DofParameters.w;
	
	//--- SkyrimBridge: Freeze focus during menus/dialogue ---
	// Prevents autofocus from hunting when game overlays UI elements
	#ifdef SKYRIMBRIDGE_FXH
		[flatten] if(SB_DOF_ShouldFreezeFocus())
			Interpolator = 0.0;
	#endif
	
	#if ENABLE_AGRESSIVE_FPS_WEAPON_FIX
		//Freeze focus transition when sampling area is blocked by FPS weapons or hand
		Interpolator *= currFocus > -DELTA;
	#endif
	
	return (UI_FocusType == 3) ? currFocus :
	lerp(prevFocus, currFocus, Interpolator);
}


//------------------------------------- Circle of Confusion ------------------------------------//

//Kitsuune - Complete CoC rework for better performance and near CoC bleeding
// -> Bleeding is still not nearly as good as I want it to be, but its a start

VertexShaderOutputDrawCoC VS_DrawCoC(VertexShaderInput IN)
{
	VertexShaderOutputDrawCoC OUT;
	OUT.pos			= float4(IN.pos.xyz, 1.0);
	OUT.texcoord.xy	= IN.txcoord.xy;
	OUT.Focus       = TextureFocus.Load(0).x;
	
	return OUT;
}

float4 PS_DrawCoC(VertexShaderOutputDrawCoC IN) : SV_Target
{
	float3 DepthData; //xy - linear scene depth, z - linear scene focus
	float2 SceneCoC;  //Circle of confusion signed by position relative to focus plane
	
	float2 GatheringCoords = IN.texcoord - PixelSize * 0.5;
	float4 DepthGather[2]  = { TextureDepth.Gather(Point_Sampler, GatheringCoords  ),
							   TextureDepth.Gather(Point_Sampler, GatheringCoords,1) };
	
	DepthGather[0] = FastLinDepth(DepthGather[0], 2999.0);
	DepthGather[1] = FastLinDepth(DepthGather[1], 2999.0);
	
	DepthData.x = min(min(min(DepthGather[0].x, DepthGather[0].z), min(DepthGather[1].x, DepthGather[1].z)), DepthGather[0].y);
	
	DepthData.x = lerp(DepthData.x, DepthGather[0].y, 0.001);
	DepthData.y = DepthGather[0].y;
	DepthData.z = IN.Focus;
	
	float2 handdepth  = DepthData.xy;
	float  HyperFocus = saturate(UI_HyperFocus + (UI_FocusType > 1.0));
		   DepthData  = saturate(DepthData / HyperFocus);
	
	SceneCoC = (DepthData.xy < DepthData.z) ?
			   ldexp(DepthData.xy / DepthData.z - 1.0, -0.5 * UI_NearBlurCurve * UI_NearBlurCurve):
			   saturate((DepthData.xy - DepthData.z) / (ldexp(DepthData.z, UI_FarBlurCurve * UI_FarBlurCurve) - DepthData.z));
	
	#if FPS_HAND_BLUR_CUTOFF_CHECK
		SceneCoC = (handdepth < FPS_HAND_BLUR_CUTOFF_DIST * 1e-4 && UI_RemoveFPSObjects) ? 0.0 : 1.0;
	#else
		SceneCoC = (handdepth < FPS_HAND_BLUR_CUTOFF_DIST * 1e-4 && UI_RemoveFPSObjects) ? 0.0 : SceneCoC;
	#endif
	
	float4 SeparatedCoC;
	SeparatedCoC    = saturate(float4(SceneCoC, -SceneCoC));
	SeparatedCoC.zw = SeparatedCoC.zw * SeparatedCoC.zw * (3.0 - 2.0 * SeparatedCoC.zw);
	
	//--- SkyrimBridge: Game-state DOF modifiers ---
	#ifdef SKYRIMBRIDGE_FXH
	[branch] if(SB_IsActive())
	{
		// Skip DOF entirely in menus or underwater
		[branch] if(SB_DOF_ShouldSkipFrame())
		{
			SeparatedCoC = 0.0;
		}
		else
		{
			// Dialogue: override to shallow DOF for cinematic speaker focus
			[branch] if(UISB_DialogueDOF > 0.5 && SB_UI_Menus.y > 0.5)
			{
				SeparatedCoC.x  *= UISB_DialogueStrength;
				SeparatedCoC.zw *= 0.0; // suppress near blur during dialogue
			}
			
			// Apply state-based CoC scale (combat, killcam, slow-time, etc.)
			float CoCScale = SB_DOF_GetCoCScale();
			SeparatedCoC.x  *= CoCScale;
			SeparatedCoC.zw *= saturate(CoCScale); // near CoC only reduces, never amplifies
		}
	}
	#endif
	
	#if ENABLE_TILT_SHIFT
	//Tilt-shift override: replace CoC with angular focus plane distance
	[branch] if(UITS_Enable)
	{
		float TSCoC = ComputeTiltShiftCoC(IN.texcoord);
		//Blend: tilt-shift replaces far CoC, preserves near for foreground objects
		SeparatedCoC.x = max(SeparatedCoC.x, TSCoC);
		SeparatedCoC.y = SeparatedCoC.y * 0.5 + 0.5; //re-encode
	}
	#endif
	
	return SeparatedCoC;
}


VertexShaderOutputNearCoC VS_NearCoC(VertexShaderInput IN)
{
	VertexShaderOutputNearCoC OUT;
	OUT.pos			= float4(IN.pos.xyz, 1.0);
	OUT.texcoord.xy	= IN.txcoord.xy;
	
	ScaleScreenQuad_Mult(OUT.pos.xy, 0.5);
	
	OUT.BlurSteps    = UI_ShapeRadius * 0.5 * UI_NearBlurBleed + DELTA;
	OUT.Sigma        = OUT.BlurSteps / 1.5;
	OUT.WeightFactor =  rcp(OUT.Sigma * SqrtTwoPI);
	OUT.Sigma        = -rcp(OUT.Sigma * OUT.Sigma);
	
	OUT.StepSize = PixelSize.xyxy * float4(1.0, 0.0, -1.0, 0.0);
	
	return OUT;
}

float PS_DownsampleNearCoC(VertexShaderOutputNearCoC IN) : SV_Target
{
	//Downsampling + horizontal gaussian blur
	float WeightSum      = IN.WeightFactor;
	float BlurredNearCoC = IN.WeightFactor * TextureColor.Sample(Linear_Sampler, IN.texcoord).w;
	
	[loop] for(float i=1.0; i <= IN.BlurSteps; i++)
	{
		float  GaussWeight = IN.WeightFactor * exp(i*i*IN.Sigma);
		float4 CurrOffset  = IN.texcoord.xyxy + i * IN.StepSize;
		
		BlurredNearCoC += GaussWeight * TextureColor.Sample(Linear_Sampler, CurrOffset.xy).w;
		BlurredNearCoC += GaussWeight * TextureColor.Sample(Linear_Sampler, CurrOffset.zw).w;
		WeightSum      += GaussWeight * 2.0;
	}
	
	return BlurredNearCoC / WeightSum;
}


VertexShaderOutputCombineCoC VS_CombineCoC(VertexShaderInput IN)
{
	VertexShaderOutputCombineCoC OUT;
	OUT.pos			= float4(IN.pos.xyz, 1.0);
	OUT.texcoord.xy	= IN.txcoord.xy;
	OUT.texcoord.zw = IN.txcoord.xy * 0.5;
	
	OUT.BlurSteps    = UI_ShapeRadius * 0.25 * UI_NearBlurBleed + DELTA;
	OUT.Sigma        = OUT.BlurSteps / 1.5;
	OUT.WeightFactor =  rcp(OUT.Sigma * SqrtTwoPI);
	OUT.Sigma        = -rcp(OUT.Sigma * OUT.Sigma);
	
	OUT.StepSize  = PixelSize.xyxy * float4(0.0, 1.0, 0.0, -1.0);
	
	return OUT;
}

float4 PS_CombineCoC(VertexShaderOutputCombineCoC IN) : SV_Target
{
	float4 SceneCoC = TextureColor.Sample(Point_Sampler, IN.texcoord);
	
	//Upsampling + vertical gaussian blur
	float WeightSum      = IN.WeightFactor;
	float BlurredNearCoC = IN.WeightFactor * RenderTargetR16F.Sample(Linear_Sampler, IN.texcoord.zw).x;
	
	[loop] for(float i=1.0; i <= IN.BlurSteps; i++)
	{
		float  GaussWeight = IN.WeightFactor * exp(i*i*IN.Sigma);
		float4 CurrOffset  = min(IN.texcoord.zwzw + i * IN.StepSize, 0.5);
		
		BlurredNearCoC += GaussWeight * RenderTargetR16F.Sample(Linear_Sampler, CurrOffset.xy).x;
		BlurredNearCoC += GaussWeight * RenderTargetR16F.Sample(Linear_Sampler, CurrOffset.zw).x;
		WeightSum      += GaussWeight * 2.0;
	}
	
	BlurredNearCoC /= WeightSum;
	
	
	float  MinCoCSampleRadius = CoC2BlurRadius(max(BlurredNearCoC, SceneCoC.w)).x * 300.0;
	float  MinNearCoC         = SceneCoC.w;
	float2 MinCoCOffsetScale  = saturate(MinCoCSampleRadius * float2(2.0, 1.0));
	
	float2 InnerOffsets[4] = { 0.5,-1.5,  1.5,0.5,  -0.5,1.5,  -1.5,-0.5 };
	float2 OuterOffsets[8] = { 0.5,-3.5,  1.5,2.5,  -0.5,3.5,  -1.5,-2.5,
							   2.5,-1.5,  3.5,0.5,  -2.5,1.5,  -3.5,-0.5 };
	
	[unroll] for(int j=0; j<4; j++)
	{
		InnerOffsets[  j] *= MinCoCOffsetScale.x;
		OuterOffsets[  j] *= MinCoCOffsetScale.y;
		OuterOffsets[7-j] *= MinCoCOffsetScale.y;
	}
	
	float4 InnerSamples[4];
	
	[unroll] for(int k=0; k<4; k++)
		InnerSamples[k] = TextureColor.GatherBlue(Linear_Sampler, IN.texcoord + PixelSize * InnerOffsets[k]);
	
	[branch] if(MinCoCOffsetScale.x < 1.0)
	{
		MinNearCoC = min(MinNearCoC, min3(InnerSamples[0].xyw));
		MinNearCoC = min(MinNearCoC, min3(InnerSamples[1].xzw));
		MinNearCoC = min(MinNearCoC, min3(InnerSamples[2].yzw));
		MinNearCoC = min(MinNearCoC, min3(InnerSamples[3].xyz));
	}
	else
	{
		[unroll] for(int l=0; l<4; l++)
			MinNearCoC = min(MinNearCoC, min4(InnerSamples[l]));
		
		MinNearCoC = min(MinNearCoC, min3(TextureColor.GatherBlue(Linear_Sampler, IN.texcoord + PixelSize * OuterOffsets[0]).xyw));
		MinNearCoC = min(MinNearCoC, min3(TextureColor.GatherBlue(Linear_Sampler, IN.texcoord + PixelSize * OuterOffsets[4]).xyw));
		
		MinNearCoC = min(MinNearCoC, min3(TextureColor.GatherBlue(Linear_Sampler, IN.texcoord + PixelSize * OuterOffsets[1]).xzw));
		MinNearCoC = min(MinNearCoC, min3(TextureColor.GatherBlue(Linear_Sampler, IN.texcoord + PixelSize * OuterOffsets[5]).xzw));
		
		MinNearCoC = min(MinNearCoC, min3(TextureColor.GatherBlue(Linear_Sampler, IN.texcoord + PixelSize * OuterOffsets[2]).yzw));
		MinNearCoC = min(MinNearCoC, min3(TextureColor.GatherBlue(Linear_Sampler, IN.texcoord + PixelSize * OuterOffsets[6]).yzw));
		
		MinNearCoC = min(MinNearCoC, min3(TextureColor.GatherBlue(Linear_Sampler, IN.texcoord + PixelSize * OuterOffsets[3]).xyz));
		MinNearCoC = min(MinNearCoC, min3(TextureColor.GatherBlue(Linear_Sampler, IN.texcoord + PixelSize * OuterOffsets[7]).xyz));
	}
	
	MinNearCoC = lerp(SceneCoC.w, MinNearCoC, saturate(MinCoCSampleRadius * 8.0));
	
	float NearBleed = max(SceneCoC.w, nRootCurve(BlurredNearCoC, 10.0));
	
	float4 CoC;
	CoC.x = SceneCoC.x;				  // Far (aggressive leak reduction)
	CoC.y = MinNearCoC - SceneCoC.y;  // + == Near min, - == Far (without agressive leak reduction)
	CoC.z = NearBleed;				  // Near bleeding
	CoC.w = BlurredNearCoC;			  // Near blurred
	
	CoC.y = CoC.y * 0.5 + 0.5;
	return CoC;
}


//---------------------------------------- DoF Prepass -----------------------------------------//

float4 PS_DoF_PrePass(VertexShaderOutput IN, uniform uint CoCIndex, uniform bool SkipPass) : SV_Target
{
	if(SkipPass) discard;
	
	float3 color = TextureOriginal.Sample(Linear_Sampler, IN.texcoord.xy).rgb;
	
	static const float2 sampleOffsets[4] = { float2( 1.5, 0.5) * PixelSize,
											 float2( 0.5,-1.5) * PixelSize,
											 float2(-1.5,-0.5) * PixelSize,
											 float2(-0.5, 1.5) * PixelSize };
	
	float4 compColor   = 0.0;
	float  centerDepth = TextureDepth.Sample(Linear_Sampler, IN.texcoord.xy).x;
		   centerDepth = rcp(FastLinDepth(centerDepth, 2999.0));
	
	[unroll] for (int i=0; i<4; i++)
	{
		float2 sampleCoord  = IN.texcoord.xy + sampleOffsets[i];
		float3 sampleColor  = TextureOriginal.SampleLevel(Linear_Sampler, sampleCoord,0).rgb;
		float4 sampleDepths = TextureDepth.Gather(Linear_Sampler, sampleCoord);
		
			  sampleColor   /= max3(sampleColor) + 1.0;
		float sampleDepthMin = FastLinDepth(min4(sampleDepths), 2999.0);
		
		float sampleWeight = saturate(sampleDepthMin * centerDepth + 1e-3);
			  compColor   += float4(sampleColor.rgb * sampleWeight, sampleWeight);
	}
	
	compColor.rgb /= compColor.a;
	compColor.rgb /= 1.0 - max3(compColor.rgb);
	color	       = lerp(color, compColor.rgb, saturate(compColor.w * 8.0));
	
	return float4(color, saturate(GetCircleOfConfusion(IN.texcoord)[CoCIndex]));
}


//--------------------------------------- Main DOF Pass ----------------------------------------//

//Kitsuune - Added support for custom texture-based bokeh shapes, spherical aberration,
//           diffraction, graining and half shapes + a few micro-optimizations
VertexShaderOutputDOF VS_DoF_Main(VertexShaderInput IN)
{
	VertexShaderOutputDOF OUT;
	OUT.pos				= float4(IN.pos.xyz, 1.0);
	OUT.texcoord.xy		= IN.txcoord.xy;
	
	ScaleScreenQuad_Mult(OUT.pos.xy, UI_RenderResMult);
	
	float VertexCount = UI_ShapeVertices;
	float Rotation    = UI_ShapeRotation;
	
	#if ENABLE_STYLIZED_BOKEHSHAPES
	if(UI_EnableStyle)
	{
		float UISelection = UI_StyleTex - 1.0;
		float AtlasColumn = floor(UISelection / BOKEHMASK_GRID_SIZE);
		float AtlasRow    = UISelection - AtlasColumn * BOKEHMASK_GRID_SIZE;
		OUT.AtlasSelect   = float2(AtlasRow, AtlasColumn);
		OUT.RotMat        = GetDirVec(UI_StyleRotation).xyyx * float4(1.0, -1.0, 1.0, 1.0);
		OUT.AtlasTileRes  = GetBokehAtlasTileRes();
		VertexCount       = 4.0;
		Rotation          = 45.0;
	}
	#endif
	
	[unroll] for(int i=0; i<10; i++)
		sincos(i * 6.2831853 / VertexCount + radians(Rotation), OUT.vertices[i].y, OUT.vertices[i].x);
	
	return OUT;
}

float4 PS_DoF_Main(VertexShaderOutputDOF IN, uniform uint CoCIndex, uniform bool SkipPass) : SV_Target
{
	if(SkipPass) discard;
	
	float4 CenterBokeh = TextureColor.Sample(Linear_Sampler, IN.texcoord);
	float  CoC         = GetCircleOfConfusion(IN.texcoord)[CoCIndex];
	float  WeightSum   = saturate(1e6 * (CenterBokeh.a - CoC) + 9e5) + DELTA;
	float2 BokehRadius = CoC2BlurRadius(CoC);
	
	if(BokehRadius.x < DISCRADIUS_RESOLUTION_BOUNDARY_LOWER_FAR * ScreenSize.y) return CenterBokeh;
	
	#if ENABLE_STYLIZED_BOKEHSHAPES
		float2 ShapeRes, rMaxRadius; float MaskLOD;
		
		[branch] if(UI_EnableStyle)
		{
			ShapeRes   = (BokehRadius * 2.0) / PixelSize;
			MaskLOD    = max(dot(IN.AtlasTileRes / ShapeRes, 0.5), 1.0);
			MaskLOD    = min(log2(MaskLOD), BOKEHMASK_MAX_LOD);
			rMaxRadius = rcp(BokehRadius);
			
			float2 MaskCenterCoords = (IN.AtlasSelect + 0.5) / BOKEHMASK_GRID_SIZE;
			WeightSum *= BokehMaskAtlas.SampleLevel(Linear_Sampler, MaskCenterCoords, MaskLOD).x;
		}
	#endif
	
	BokehUIParams UI = GetUIBokehParams();
	float nRings  = lerp(1.0, UI.ShapeQuality, saturate(CoC));
		  nRings += fmod(IN.pos.x + IN.pos.y, 2.0) * 0.5;
	
	#if ENABLE_SPHERICAL_ABERRATION
		float SphericalType = UI.SphericalAmount < 0.0;
		UI.SphericalAmount *= saturate(CoC * 0.9 + SphericalType);
		WeightSum          *= saturate(1.0 - UI.SphericalAmount);
		UI.SphericalAmount  = rcp(UI.SphericalAmount * nRings + DELTA);
		
		float SphericalCorrection = UI.SphericalAmount * (nRings + 0.1) * SphericalType;
	#endif
	
	#if ENABLE_DIFFRACTION
		float DiffractionAmount   = (1.0 - UI.DiffractionAmount) * 0.25 + 0.75;
		float CenterDiffracWeight = Diffraction(1.0, UI_DiffractionFreq, 0.0, 1.0);
			  WeightSum          *= saturate(CenterDiffracWeight + DiffractionAmount);
	#endif
	
	#if ENABLE_GRAINING == 1
		float GrainAmount = (1.0 - UI.GrainAmount) * 0.5 + 0.5;
			  WeightSum  *= saturate(Random(UI_GrainSeed) * 0.5 + GrainAmount);
	#endif
	
	#if ENABLE_OPTICAL_VIGNETTE
		float2 centerVec  = IN.texcoord.xy - 0.5;
		float  centerDist = sqrt(dot(centerVec, centerVec));
		float  vignette		  = pow(centerDist, UI_ShapeVignetteCurve) * UI.ShapeVignetteAmount;
			   centerVec  = centerVec / centerDist * vignette;
			   WeightSum *= saturate(3.33 - vignette * 2.0);
	#endif
	
	BokehRadius /= nRings;
	CoC         /= nRings;
	
	//float3 BokehCenterDistance = 0.0;
	float3 BokehSum, BokehMax;
	BokehSum.rgb = BokehMax.rgb = WeightSum * CenterBokeh.rgb;
	
	[loop] for(float iRings = 1; iRings <= nRings; iRings++)
	{
		float2 RingOffsetScale  = BokehRadius * iRings;
		float  RingSampleWeight = 1.0;
		
		#if ENABLE_DIFFRACTION || ENABLE_OPTICAL_VIGNETTE
			float rnRings = rcp(nRings);
		#endif
		
		#if ENABLE_DIFFRACTION
			float DiffractionWeight = Diffraction(1.0 - iRings * rnRings, UI_DiffractionFreq, 0.0, 1.0);
				  RingSampleWeight *= saturate(DiffractionWeight + DiffractionAmount);
		#endif
		
		#if ENABLE_SPHERICAL_ABERRATION
			float SphericalWeight   = SphericalCorrection - iRings * UI.SphericalAmount;
				  RingSampleWeight *= saturate(pow(abs(SphericalWeight), UI_SphericalCurve));
		#endif
		
		[loop] for(float iSamplesPerRing = 0; iSamplesPerRing < iRings; iSamplesPerRing++)
		[loop] for(int   iVertices       = 0; iVertices       < UI.VertexCount; )
		{
			float2 OffsetVec = lerp(IN.vertices[iVertices], IN.vertices[++iVertices], iSamplesPerRing / iRings);
			ShapeRoundness(OffsetVec, UI.Curvature);
			
			float2 SampleOffset = OffsetVec * RingOffsetScale;
			float  SampleWeight = RingSampleWeight;
			
			//--- Cinematic & Realism bokeh shape modifications ---
			#if ENABLE_CATS_EYE
				ApplyCatsEye(SampleOffset, IN.texcoord);
			#endif
			
			#if ENABLE_SWIRLY_BOKEH
				[branch] if(UISW_Enable)
					ApplySwirlRotation(SampleOffset, IN.texcoord, CoC * nRings);
			#endif
			
			#if ENABLE_ANAMORPHIC_BOKEH
				[branch] if(UIAB_Enable)
					ApplyAnamorphicSqueeze(SampleOffset);
			#endif
			
			#if ENABLE_STYLIZED_BOKEHSHAPES
			[branch] if(UI_EnableStyle)
			{
				//Manual matrix multiplication to avoid weird compiler "optimizations"
				float2 MaskSampleOffset = VecMatrixMul2x2(SampleOffset * rMaxRadius, IN.RotMat);
				MaskBokehShape(MaskSampleOffset, IN.AtlasSelect, MaskLOD, SampleWeight);
				if(SampleWeight < 0.01) continue;
			}
			#endif
			
			float4 sampleBokeh;
			sampleBokeh   = TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + SampleOffset,0);
			SampleWeight *= saturate(1e6 * (sampleBokeh.a - CoC * iRings) + 1.0);
			
			#if ENABLE_GRAINING == 1
				SampleWeight *= saturate(Random(OffsetVec + UI_GrainSeed) * 0.5 + GrainAmount);
			#endif
			
			#if ENABLE_OPTICAL_VIGNETTE
				OpticalVignette(OffsetVec * iRings * rnRings, centerVec, SampleWeight);
			#endif
			
			sampleBokeh.rgb *= SampleWeight;
			
			WeightSum += SampleWeight;
			BokehSum  += sampleBokeh.rgb;
			BokehMax   = max(BokehMax, sampleBokeh.rgb);
			//BokehCenterDistance = (BokehMax == sampleBokeh.rgb) ? iRings : BokehCenterDistance;
		}
	}
	
	BokehSum     /= WeightSum;
	UI.Intensity  = saturate(UI.Intensity * pow(dot(BokehMax, N_LUM), UI_BokehCurve));
	UI.Intensity *= saturate(CoC * nRings * 4.0);
	BokehMax      = max(BokehMax, BokehSum);
	
	float3 FinalBokeh = lerp(BokehSum, BokehMax, UI.Intensity);
	
	#if ENABLE_BOKEH_FRINGING
		//Apply longitudinal CA fringing based on CoC direction
		float4 RawCoC = GetCircleOfConfusion(IN.texcoord);
		float CoCSigned = RawCoC.y * 2.0 - 1.0; //signed: negative=near, positive=far
		FinalBokeh = ApplyBokehFringing(FinalBokeh, saturate(CoC * nRings), CoCSigned);
	#endif
	
	#if ENABLE_HIGHLIGHT_BLOOM
		FinalBokeh = ApplyHighlightBloom(FinalBokeh, BokehMax, CoC * nRings);
	#endif
	
	return float4(FinalBokeh, 1.0);
}


//-------------------------------------DOF Combine/PostPass-------------------------------------//

VertexShaderOutputCombine VS_DoF_Combine(VertexShaderInput IN)
{
	VertexShaderOutputCombine OUT;
	OUT.pos				= float4(IN.pos.xyz, 1.0);
	OUT.texcoord.xy		= IN.txcoord.xy;
	OUT.texcoord.zw = IN.txcoord.xy * UI_RenderResMult;
	
	OUT.DOFCG_Saturation = DNI_SEPARATION(UICG_Saturation);
	OUT.DOFCG_Brightness = DNI_SEPARATION(UICG_Brightness);
	OUT.DOFCG_Contrast   = DNI_SEPARATION(UICG_Contrast);
	
	return OUT;
}

float4 PS_DoF_Combine(VertexShaderOutputCombine IN) : SV_Target
{
	float3 NearBokeh     = TextureColor.       Sample(Linear_Sampler, IN.texcoord.zw).rgb;
	float3 FarBokeh      = RenderTargetRGBA64F.Sample(Linear_Sampler, IN.texcoord.zw).rgb;
	float3 originalColor = TextureOriginal.    Sample(Point_Sampler,  IN.texcoord.xy).rgb;
	float4 CoC           = GetCircleOfConfusion(IN.texcoord);
	float  AbsCoC        = max(CoC.w, saturate(-CoC.y));
	
	#if ENABLE_FOCUS_BREATHING
	//Focus breathing: cinema lenses shift FOV slightly during refocus
	//Simulate by applying subtle zoom based on focus distance
	[branch] if(UIFB_Enable)
	{
		float FocusDist  = TextureFocus.Load(0).x;
		float Deviation  = (FocusDist - UIFB_NeutralDist) * UIFB_Amount;
		float2 BreathUV  = IN.texcoord.xy;
		BreathUV = (BreathUV - 0.5) * (1.0 - Deviation) + 0.5;
		originalColor = TextureOriginal.Sample(Linear_Sampler, BreathUV).rgb;
	}
	#endif
	
	float blendWeightFar = LinearStep(DISCRADIUS_RESOLUTION_BOUNDARY_LOWER_FAR,
									  DISCRADIUS_RESOLUTION_BOUNDARY_UPPER_FAR,
									  abs(CoC2BlurRadius(CoC.x).x * ScreenSize.x));
	
		  blendWeightFar  = pow(blendWeightFar, DISCRADIUS_RESOLUTION_BOUNDARY_CURVE_FAR);
	float blendWeightNear = saturate(sqrt(CoC.w) * 1.5);
	
	blendWeightFar  *= UI_FarBlurCurve  < BOKEH_CURVE_DISCARD_BOUNDARY_FAR;
	blendWeightNear *= UI_NearBlurCurve < BOKEH_CURVE_DISCARD_BOUNDARY_NEAR;
	
	float4 BokehSum;
	BokehSum.rgb = lerp(originalColor, FarBokeh, blendWeightFar);
	BokehSum.rgb = lerp(BokehSum.rgb, NearBokeh, blendWeightNear);
	BokehSum.a   = saturate(AbsCoC * GAUSSIAN_BUILDUP_MULT);
	
	#if ENABLE_DEPTH_DITHER
		BokehSum.rgb = ApplyDepthDither(BokehSum.rgb, IN.texcoord.xy * ScreenRes,
		                                max(blendWeightFar, blendWeightNear));
	#endif
	
	[branch] if(UICG_Enable) //Depth of Field Color Grading
	{
		float3 DoFCol;
		DoFCol = log2(mad(BokehSum.rgb, IN.DOFCG_Brightness, DELTA));
		DoFCol = exp2(lerp(UICG_ConGrayLvl, DoFCol, IN.DOFCG_Contrast));
		DoFCol = zerolim(lerp(dot(DoFCol - DELTA, K_LUM), DoFCol, IN.DOFCG_Saturation));
		
		float Mask   = (UICG_Mask == 1) ? AbsCoC : max(blendWeightFar, blendWeightNear);
		BokehSum.rgb = lerp(BokehSum.rgb, DoFCol, saturate(Mask));
	}
	
	return BokehSum;
}


//-----------------------------------Gaussian PostBlur Pass-------------------------------------//

float4 PS_DoF_Gauss(VertexShaderOutput IN, uniform float2 Axis, uniform bool OverrideAlpha) : SV_Target
{
	//Kitsuune - Replaced entire gaussian blur shader and added bilateral mode + grain
	float4 Color     = TextureColor.Sample(Linear_Sampler, IN.texcoord);
	float  OrigSigma = Color.a;
		   Color.a  *= GaussBleedReduction;
	
	float  Sigma       = GetSmootheningAmount(OrigSigma);
	float  OffsetScale = 1.0 - sqrt(saturate(Sigma / 3.0)) * 0.5;
	
	float4 StepSize     = PixelSize.xyxy * float4(Axis, -Axis);
	float4 HalfStepSize = StepSize * OffsetScale;
		   StepSize    *= 2.0;
	
	float WeightFactor =  rcp(Sigma * SqrtTwoPI);
	float LoopCount    = ceil(Sigma * BlurQuality[UI_GaussQuality] - 0.01);
		  Sigma        =  rcp(Sigma * Sigma);
	
	float  WeightSum  = WeightFactor * OffsetScale;
	float3 BlurredCol = WeightSum * Color.rgb;
	
	
	[loop] for(float i=1.0; i <= LoopCount; i++)
	{
		float  GaussWeight = WeightFactor * exp(i*i*-Sigma);
		float4 CurrOffset  = IN.texcoord.xyxy + i * StepSize - HalfStepSize;
		
		float4 CurrSample; float CurrWeight;
		CurrSample = TextureColor.SampleLevel(Linear_Sampler, CurrOffset.xy, 0);
		GetGaussianWeight(CurrWeight, GaussWeight, CurrSample, Color, i);
		
		BlurredCol += CurrWeight * CurrSample.rgb;
		WeightSum  += CurrWeight;
		
		CurrSample = TextureColor.SampleLevel(Linear_Sampler, CurrOffset.zw, 0);
		GetGaussianWeight(CurrWeight, GaussWeight, CurrSample, Color, i);
		
		BlurredCol += CurrWeight * CurrSample.rgb;
		WeightSum  += CurrWeight;
	}
	
	Color.a   = OrigSigma;
	Color.rgb = BlurredCol / WeightSum;
	
	#if ENABLE_CHROMATIC_ABERRATION
		if(OverrideAlpha)
		{
			float4 CoC = GetCircleOfConfusion(IN.texcoord);
			Color.a    = max(CoC.x, CoC.w);
		}
	#else
		if(OverrideAlpha)
		{
			#if ENABLE_GRAINING == 2
				ApplyDoFPostGraining(Color.rgb, IN.texcoord, Color.a * UI_GrainInt);
			#endif
			Color.a = ALPHAOUT;
		}
		
		#if ENABLE_COC_DEBUGGING
			if(UI_CoCDebug > 0)
				Color.rgb = DebugCircleOfConfusion(IN.texcoord)[UI_CoCDebug-1];
		#endif
	#endif
	
	return Color;
}


//-------------------------------------Chromatic Aberration-------------------------------------//

#if ENABLE_CHROMATIC_ABERRATION
float4 PS_DoF_CA(VertexShaderOutput IN) : SV_Target
{
	#if ENABLE_COC_DEBUGGING
		if(UI_CoCDebug > 0)
			return float4((DebugCircleOfConfusion(IN.texcoord)[UI_CoCDebug-1]).xxx, ALPHAOUT);
	#endif
	
	
	//Kitsuune - Added lateral chromatic aberration and a more aggressive leak reduction
	// -> mixing both CA types into one pass isn't ideal but saves performance and looks good enough
	
	float  Alpha = ALPHAOUT;
	float4 colorVals[5];
	colorVals[0] = TextureColor.Load(int3(IN.pos.x,   IN.pos.y,   0)); //C
	
	if(colorVals[0].w < DELTA) return float4(colorVals[0].rgb, Alpha);
	
	colorVals[1] = TextureColor.Load(int3(IN.pos.x-1, IN.pos.y,   0)); //L
	colorVals[2] = TextureColor.Load(int3(IN.pos.x,   IN.pos.y-1, 0)); //T
	colorVals[3] = TextureColor.Load(int3(IN.pos.x+1, IN.pos.y,   0)); //R
	colorVals[4] = TextureColor.Load(int3(IN.pos.x,   IN.pos.y+1, 0)); //B
	
	float2 bokehRadiusScaled = CoC2BlurRadius(colorVals[0].a);
	
	float4 vGradTwosided = float4(dot(colorVals[0].rgb - colorVals[1].rgb, 1),  //C - L
								  dot(colorVals[0].rgb - colorVals[2].rgb, 1),  //C - T
								  dot(colorVals[3].rgb - colorVals[0].rgb, 1),  //R - C
								  dot(colorVals[4].rgb - colorVals[0].rgb, 1)); //B - C
	
	float2 vGrad = min(vGradTwosided.xy, vGradTwosided.zw);
	
	float vGradLen = sqrt(dot(vGrad, vGrad)) + 1e-6;
	vGrad = vGrad / vGradLen * saturate(vGradLen * 32.0) * bokehRadiusScaled * 0.125 * UI_LongitudChromaAmount;
	
	
	float2 LateralVector   = IN.texcoord * 2.0 - 1.0;
	float  LateralGradient = length(LateralVector);
		   LateralVector  *= float2(ScreenSize.z, 1.0);
	
	float LateralShift  = LateralGradient / 1.15 + DELTA;
		  LateralShift  = pow(LateralShift, UI_LateralChromaCurve);
		  LateralShift *= UI_LateralChromaAmount * 5.0;
		  LateralShift *= saturate(CHROMA_BUILUP_MULT * colorVals[0].a);
	
	vGrad *= saturate(1.0 - LateralShift * 0.2);
	vGrad += LateralVector * PixelSize * (LateralShift / LateralGradient);
	
	
	float4 chromaVals[3];
	
	chromaVals[0] = colorVals[0];
	chromaVals[1] = TextureColor.Sample(Linear_Sampler, IN.texcoord + vGrad);
	chromaVals[2] = TextureColor.Sample(Linear_Sampler, IN.texcoord - vGrad);
	
	//Agressive leak reduction for CA
	float2 LeakScanScale = saturate(1.0 - bokehRadiusScaled) * 1.15;
	chromaVals[1].w = min(min4(TextureColor.GatherAlpha(Linear_Sampler, IN.texcoord + vGrad * LeakScanScale)), colorVals[0].w);
	chromaVals[2].w = min(min4(TextureColor.GatherAlpha(Linear_Sampler, IN.texcoord - vGrad * LeakScanScale)), colorVals[0].w);
	
	chromaVals[1].rgb = lerp(chromaVals[0].rgb, chromaVals[1].rgb, saturate(CHROMA_BUILUP_MULT * chromaVals[1].w));
	chromaVals[2].rgb = lerp(chromaVals[0].rgb, chromaVals[2].rgb, saturate(CHROMA_BUILUP_MULT * chromaVals[2].w));
	
	int3 ChromaMode = (int3(0,1,2) + UI_ChromaMode) % 3;
	
	float4 BokehSum;
	BokehSum = float4(chromaVals[ChromaMode.x].r,
					  chromaVals[ChromaMode.y].g,
					  chromaVals[ChromaMode.z].b, Alpha);
	
	
	#if ENABLE_GRAINING == 2
		ApplyDoFPostGraining(BokehSum.rgb, IN.texcoord, UI_GrainInt * chromaVals[0].w);
	#endif
	
	return BokehSum;
}
#endif


//--------------------------------------Focusing Visualizer-------------------------------------//

#if ENABLE_FOCUSING_TOOL

//Visualizer parameters
static const float  FocusingTool_LineSize    = 1.5;
static const float3 FocusingTool_LineColor   = { 1.0, 1.0, 1.0 };
static const float3 FocusingTool_CenterColor = { 1.0, 0.0, 0.0 };


//Clip functions
bool ANDTexcoordClip(float2 Coords, float4 Bound)
{ return all(Coords > Bound.xy && Coords < Bound.zw); }

bool ORTexcoordClip(float2 Coords, float4 Bound)
{ return any(Coords > Bound.xy && Coords < Bound.zw); }


float4 PS_DoF_Overlay(VertexShaderOutput IN) : SV_Target
{
	//Kitsuune - Added optional focusing visualizer to better see the actually sampled area and
	//focusing center (useful when adjusting autofocus offsets for third person gameplay)
	// -> Not the most efficient way of doing it but this will do for now
	//    And its turned off most of the time anyway, so it shouldn't really matter
	
	if(UI_FocusType == 3 || !UI_VisualizeFocus) discard;
	
	float4 PixelScale, FocusCoords; float FocusRadius;
	PixelScale  = float4(-PixelSize, PixelSize);
	PixelScale *= FocusingTool_LineSize;
	
	
	[flatten] if(UI_FocusType == 2)
	{ FocusCoords = tempInfo2.zwzw;
	  FocusRadius = UI_MousefocusRadius; }
	else
	{ FocusCoords = UI_AutofocusCenter.xyxy;
	  FocusRadius = UI_AutofocusRadius;  }
	
	float4 CrosshairCoords		= PixelScale + FocusCoords;
	float4 SampleAreaCoords = FocusCoords + float4(-FocusRadius.xx, FocusRadius.xx);
	
	bool DrawCrosshair  = ORTexcoordClip(IN.texcoord, CrosshairCoords);
	bool DrawSampleArea = ANDTexcoordClip(IN.texcoord, SampleAreaCoords);
		 DrawCrosshair  = DrawCrosshair && !DrawSampleArea;
	
	SampleAreaCoords = PixelScale * -2.0 + SampleAreaCoords;
	DrawSampleArea   = DrawSampleArea && !ANDTexcoordClip(IN.texcoord, SampleAreaCoords);
	
	bool DrawFocusCenter = ANDTexcoordClip(IN.texcoord, FocusCoords + PixelScale * 2.0);
	if(!(DrawSampleArea || DrawCrosshair || DrawFocusCenter)) discard;
	
	return float4(DrawFocusCenter ? FocusingTool_CenterColor : FocusingTool_LineColor, 0.0);
}

//Old visualization shader that shows each sample position separately
/*float4 PS_DoF_Debug_Overlay(VertexShaderOutput IN) : SV_Target
{
	#define PS_DoF_Overlay PS_DoF_Debug_Overlay
	float4 FocusCoords		   = 0.0;
	float  FocusRadius		   = 0.0;
	bool   ShowSamplePositions = true;
	
	if (UI_FocusType == 2)
	{ FocusCoords = tempInfo2.zwzw;
	  FocusRadius = UI_MousefocusRadius; }
	else
	{ FocusCoords = UI_AutofocusCenter.xyxy;
	  FocusRadius = UI_AutofocusRadius;  }
	
	float4 ClippingArea = FocusCoords.xyxy + float4(-FocusRadius.xx, FocusRadius.xx);
	clip(ANDTexcoordClip(IN.texcoord, ClippingArea) && UI_FocusType != 3 && UI_VisualizeFocus ? 1:-1);
	
	float4 OverlayPxlSize = float4(-PixelSize, PixelSize) * 2.0;
	float3 Color		      = TextureColor.Sample(Point_Sampler, IN.texcoord).rgb;
	
	[branch] if (ShowSamplePositions)
	[unroll] for(float xcoord = 0.0; xcoord < 5.0; xcoord++)
	[unroll] for(float ycoord = 0.0; ycoord < 5.0; ycoord++)
	{
		float4 SampleOffset; float SampleWeight;
		SampleOffset.xy = (float2(xcoord,ycoord) + 0.5) * 0.2;
		SampleOffset.xy = mad(SampleOffset.xy, 2.0, -1.0);
		SampleWeight    = 1.0; //saturate(1.2 * exp2(dot(sampleOffset, sampleOffset) * -4.0));
		
		SampleOffset.xy = mad(SampleOffset.xy, FocusRadius, FocusCoords.xy);
		SampleOffset    = SampleOffset.xyxy + OverlayPxlSize;
		
		Color = ANDTexcoordClip(IN.texcoord, SampleOffset) ? SampleWeight : Color;
	}
	else Color = ANDTexcoordClip(IN.texcoord, mad(OverlayPxlSize,-2.0, ClippingArea)) ? Color : 1.0;
		 Color = ANDTexcoordClip(IN.texcoord, mad(OverlayPxlSize, 2.0, FocusCoords)) ? float3(1.0, 0.0, 0.0) : Color;
	
	return float4(Color, 1.0);
}*/
#endif


//----------------------------------------------------------------------------------------------//
//										   Techniques												//
//																								//
//----------------------------------------------------------------------------------------------//

//--------------------------------------------Focus---------------------------------------------//

TECH11(ReadFocus, VS_ReadFocus(), PS_ReadFocus())
TECH11(Focus,     VS_DoF(),       PS_Focus())


//---------------------------------------------DOF----------------------------------------------//

#if ENABLE_FAR_BOKEH_FREEZING
	static bool SkipFarBokehPass  = UI_FarBlurCurve  >= BOKEH_CURVE_DISCARD_BOUNDARY_FAR || UI_FreezeFarBokeh;
#else
	static bool SkipFarBokehPass  = UI_FarBlurCurve  >= BOKEH_CURVE_DISCARD_BOUNDARY_FAR;
#endif
	static bool SkipNearBokehPass = UI_NearBlurCurve >= BOKEH_CURVE_DISCARD_BOUNDARY_NEAR;


TECH11       (DOF  <string UIName="ADoF - Kitsuune - McFly";>,   VS_DrawCoC(),     PS_DrawCoC())
TWOPASSTECH11(DOF1 <string RenderTarget="RenderTargetR16F";>,    VS_Basic(),       PS_Blank(),
																 VS_NearCoC(),     PS_DownsampleNearCoC())
TECH11       (DOF2 <string RenderTarget="RenderTargetRGBA32";>,  VS_CombineCoC(),  PS_CombineCoC())
TECH11       (DOF3,												 VS_DoF(),         PS_DoF_PrePass(0, SkipFarBokehPass))
TECH11       (DOF4 <string RenderTarget="RenderTargetRGBA64F";>, VS_DoF_Main(),    PS_DoF_Main(0, SkipFarBokehPass))
TECH11       (DOF5,												 VS_DoF(),         PS_DoF_PrePass(1, SkipNearBokehPass))
TWOPASSTECH11(DOF6,												 VS_Basic(),       PS_Blank(),
																 VS_DoF_Main(),    PS_DoF_Main(2, SkipNearBokehPass))
TECH11       (DOF7,												 VS_DoF_Combine(), PS_DoF_Combine())
TECH11       (DOF8,												 VS_DoF(),         PS_DoF_Gauss(float2(0.0,1.0), false))

#if ENABLE_FOCUSING_TOOL
	TWOPASSTECH11(DOF9, VS_DoF(), PS_DoF_Gauss(float2(1.0,0.0), true),
					    VS_DoF(), PS_DoF_Overlay())
#else
	TECH11       (DOF9, VS_DoF(), PS_DoF_Gauss(float2(1.0,0.0), true))
#endif

#if ENABLE_CHROMATIC_ABERRATION
	TECH11       (DOF10, VS_DoF(), PS_DoF_CA())
#endif

