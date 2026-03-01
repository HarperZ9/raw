/*/////////////////////////////////////////////////////////
//                ENBSeries effect file                //
//         visit http://enbdev.com for updates         //
//       Copyright (c) 2007-2018 Boris Vorontsov       //
//----------------------ENB PRESET---------------------//
			MM"""""""`YM MM"""""""`MM M""""""""M 
			MM  mmmmm  M MM  mmmm,  M Mmmm  mmmM 
			M'        .M M'        .M MMMM  MMMM 
			MM  MMMMMMMM MM  MMMb. "M MMMM  MMMM 
			MM  MMMMMMMM MM  MMMMM  M MMMM  MMMM 
			MM  MMMMMMMM MM  MMMMM  M MMMM  MMMM 
			MMMMMMMMMMMM MMMMMMMMMMMM MMMMMMMMMM 
https://www.nexusmods.com/skyrimspecialedition/mods/4743
//-----------------------CREDITS-----------------------//
//     Please do not redistribute without credits      //
// Boris: For ENBSeries and his knowledge and codes    //
// JawZ:  Author and developer of the MSL code         //
// CeeJay.dk-Crosire-GemFx-Marty McFly-Lucifer Hawk:   //
//        Original Authors of sweeFX & Reshade code    //
// Roxahris: ENB Port of MartinGrain and Overlay.      //
// Romain Dura: Original author of blending maths      //
// L00 :  Shader Setup, Presets and Settings,          //
//        Port and Modification of ReShade Shaders     //
//        and author of this file                      //
/////////////////////////////////////////////////////////
//            PHOTOREALISTIC TAMRIEL X           	   //
						12.7
/////////////////////////////////////////////////////////
//               	  INTERFACE                        //
///////////////////////////////////////////////////////*/

float 	Title_Row1 		<string UIName="                            ::POST-PROCESSING::";   string UIWidget="spinner";  float UIMin=0.0;  float UIMax=0.0; float UIStep=1.0;> = {0.0};
float 	dropBlueAmount	<string UIName="Drop Blue";         string UIWidget="Spinner";  float UIMin=-0.0f;  float UIMax=1.0f;  float UIStep=0.1f;> = {0.0f};
bool 	Filmic_SFX		< string UIName="Filmic Range"; > = {false};
int		iColorRange 	<string UIName="Color Range"; 		string UIWidget="dropdown";string UIList="Full (0-255), Limited (16-235)";int UIMin=0;int UIMax=1;>;
float 	sharp_strength	<string UIName="Sharpness";         string UIWidget="Spinner";  float UIMin=-1.0f;  float UIMax=5.0f;  float UIStep=0.1f;> = {1.0f};

float 	Empty_Row1 		<string UIName=" ";   string UIWidget="spinner";  				float UIMin=0.0;float UIMax=0.0;float UIStep=1.0f;> = {0.0};
bool 	bCGList 		< string UIName="     ::SHOW AVAILABLE COLORGRADES:: ->"; > = {false};
int		iCG 			< string UIName="Colorgrade to apply"; string UIWidget="dropdown"; int UIMin=0; int UIMax=60;string UIList="none,50's Postcard,300,Alien,BatmanVSSuperman,Book of Eli,Branna-Instagram,Breaking Bad,Captein America: Civil War,Casino Royal - BW,Casino Royale 2 - BW,The Conjuring,Dark Age,Dark Science Fiction,Diesel Punk,Drive,Early Bird - Instagram,50's Filmstock,Fuel,Game of Thrones,Gone Girl,Grand Budapest Hotel,Gravity,Guardian of the Galaxy,Harry Potter,HDR - Instagram,House of Cards,Indiana Jones,Inglorious Basterds,Inkwell - Instagram,Interstellar,Lo-Fi - Instagram,Lord Kelvin - Instagram,Lord of the Ring,Mad Max Fury Road,Matrix,Minority Report,Mission Impossible,Moonrise Kingdom,Nashville - Instagram,Noir I, Noir II, Noir III,Nuclear Desert,Post Apocalypse,Prometheus,The Revenant,The Ring,Saving Private Ryan,Sky Fall,Star Wars the Force Awaken,Suicide Squad,Sutro - Instagram,Teal Cold,Tron,Vintage 70s,Vintage Warm,Waiden - Instagram,the Walking Dead,Washed Light - Instagram,XProll - Instagram";> = {0};
float 	CGAmount 		<string UIName="Colorgrade: Intensity                                   %"; string UIWidget="Spinner";    float UIMin=-100.0f;  float UIMax=100.0f; float UIStep=5.0f;> = {50.0f}; 
float 	CGAmount_N 		<string UIName="Colorgrade: Intensity Night                             %"; string UIWidget="Spinner";    float UIMin=-100.0f;  float UIMax=100.0f; float UIStep=5.0f;> = {50.0f};
int		iCG_i 			< string UIName="Colorgrade to apply interiors"; string UIWidget="dropdown"; int UIMin=0; int UIMax=60;string UIList="none,50's Postcard,300,Alien,Batman VS Superman,Book of Eli,Branna-Instagram,Breaking Bad,Captain America,Casino Royal - BW,Casino Royale 2 - BW,The Conjuring,Dark Age,Dark Science Fiction,Diesel Punk,Drive,Early Bird - Instagram,50's Filmstock,Fuel,Game of Thrones,Gone Girl,Grand Budapest Hotel,Gravity,Guardian of the Galaxy,Harry Potter,HDR - Instagram,House of Cards,Indiana Jones,Inglorious Basterds,Inkwell - Instagram,Interstellar,Lo-Fi - Instagram,Lord Kelvin - Instagram,Lord of the Ring,Mad Max Fury Road,Matrix,Minority Report,Mission Impossible,Moonrise Kingdom,Nashville - Instagram,Noir I, Noir II, Noir III,Nuclear Desert,Post Apocalypse,Prometheus,The Revenant,The Ring,Saving Private Ryan,Sky Fall,Star Wars the Force Awaken,Suicide Squad,Sutro - Instagram,Teal Cold,Tron,Vintage 70s,Vintage Warm,Waiden - Instagram,the Walking Dead,Washed Light - Instagram,XProll - Instagram";> = {0};
float 	CGAmount_i 		<string UIName="Colorgrade interiors: Intensity                     %"; string UIWidget="Spinner";    float UIMin=-100.0f;  float UIMax=100.0f; float UIStep=5.0f;> = {50.0f};
int		iCGBlend 		< string UIName="Colorgrade: Blending Type"; string UIWidget="dropdown"; int UIMin=0; int UIMax=10;string UIList="Normal,Hue,Color,Luminosity,Lighten,Linear Additive,Screen,Linear,Darken,Multiply,SoftLight";> = {0};   

float 	Empty_Row2 		<string UIName="  ";   string UIWidget="spinner";  				float UIMin=0.0;float UIMax=0.0;float UIStep=1.0f;> = {0.0};
bool 	bCCLUT			< string UIName="Color: Apply to selected Colorgrade Only"; > = {false};
float 	camSat			<string UIName="Color: Saturation";								string UIWidget="spinner";float UIMin=0.0f;float UIMax=1.9f;float UIStep=0.05f;> = {1.0f};
float 	TonemapExposure	<string UIName="Color: Brightness";string UIWidget="spinner";float UIMin=-2.0f;float UIMax=2.0f;float UIStep=0.05f;> = {1.0f};
float 	TonemapGammaD 	<string UIName="Color: Gamma Day";string UIWidget="spinner";float UIMin=0.1f;float UIMax=2.0f;float UIStep=0.05f;> = {1.0f};
float 	TonemapGammaN 	<string UIName="Color: Gamma Night";string UIWidget="spinner";float UIMin=0.1f;float UIMax=2.0f;float UIStep=0.05f;> = {1.0f};
float 	TonemapGammaI 	<string UIName="Color: Gamma Interior";string UIWidget="spinner";float UIMin=0.1f;float UIMax=2.0f;float UIStep=0.05f;> = {1.0f};
float 	GuiBlack		<string UIName="Color: Black lift";string UIWidget="spinner";float UIMin=-100.0f;float UIMax=100.0f;float UIStep=5.0f;> = {0.0f}; 

int		iFinish 		<string UIName="Color: Finish"; string UIWidget="dropdown"; int UIMin=0; int UIMax=32;string UIList="None,Filmic Neutral,Filmic Warm,Cashmere I,Cashmere II,Cashmere III,Century I,Century II,Century III,Dark Monochrome I,Dark Monochrome II,Dark Monochrome III,Earthy Monochrome I,Earthy Monochrome II,Earthy Monochrome III,Explorer Teal I,Explorer Teal II,Explorer Teal III,Golden Age I,Golden Age II,Golden Age III,Pacific Coast I,Pacific Coast II,Pacific Coast III,Super Gold I,Super Gold II,Super Gold III,Tonal Blue I,Tonal Blue II,Tonal Blue III,Tonal Sand I,Tonal Sand II,Tonal Sand III";> = {0};
float 	FinishAmount 	<string UIName="Color: Finish Amount"; string UIWidget="Spinner";    float UIMin=0.5f;  float UIMax=1.0f; float UIStep=0.25f;> = {0.0f};
float 	Empty_Row3 		<string UIName="   ";   string UIWidget="spinner";  				float UIMin=0.0;float UIMax=0.0;float UIStep=1.0f;> = {0.0};

float 	Title_Row2 		<string UIName="                               ::FINAL OUTPUT::";   string UIWidget="spinner";  float UIMin=0.0;  float UIMax=0.0; float UIStep=1.0;> = {0.0};

float 	border_ratio 	<string UIName="Border Ratio";   string UIWidget="Spinner";  float UIMin=-10.0f;  float UIMax=10.0f;  float UIStep=0.05f;> = {0.0f};
float 	EVignetteAmount 	<string UIName="Vignette";       string UIWidget="Spinner";    float UIMin=0.0f;float UIMax=100.0f;		float UIStep=1.0f;> = {5.0f};
int		iFrame 			< string UIName="Negative Print & Papers"; string UIWidget="dropdown"; int UIMin=0; int UIMax=25; string UIList="None,Negative I,Negative II,Fuji pro160C,Negative III,Kodak 160NC,Ilford 100 Delta PRO, Ilford Pro 130D,Ilford HP5 Plus,PFRM c-400,EPP,NP 400PR,Positive I,Ratio 2.75,Paper I Black,Paper I White,Daguerreotype,Paper III White,Paper IV White,Polaroid I,Polaroid II,Polaroid III,Polaroid IV,Polaroid V,Polaroid VI,Paper A4 (old)";> = {0};

int		iDirt 			< string UIName="Scratches & dirt"; string UIWidget="spinner"; int UIMin=0; int UIMax=10;> = {0};
float 	fDirtIntensity 	<string UIName="Scratches & dirt Intensity";   string UIWidget="Spinner";  float UIMin=0.0f;  float UIMax=2.0f;  float UIStep=0.1f;> = {1.0f};
float 	fGrainIntensity <string UIName="Negative Print Grain";   string UIWidget="Spinner";  float UIMin=0.0f;  float UIMax=1.0f;  float UIStep=0.01f;> = {0.1f};

int		iVHSType 		< string UIName="NTSC-VHS-TV"; string UIWidget="dropdown"; string UIList="None,NTSC,VHS,TV"; int UIMin=0; int UIMax=3;> = {2};
float 	fVHSIntensity 	<string UIName="NTSC-VHS Intensity"					;string UIWidget="Spinner"	;float UIMin=0.0f	;float UIMax=1.0f	;float UIStep=0.1f;> = {0.1f};
float 	fVHSnoise 		< string UIName="Static Noise";   string UIWidget="Spinner";  float UIMin=0.0f;  float UIMax=2.0f;  float UIStep=0.1f;> = {0.1f};
float 	Empty_Row4 		<string UIName="    ";   string UIWidget="spinner";  				float UIMin=0.0;float UIMax=0.0;float UIStep=1.0f;> = {0.0};

float 	Title_Row3 		<string UIName="                                       ::MISC::";   string UIWidget="spinner";  float UIMin=0.0;  float UIMax=0.0; float UIStep=1.0;> = {0.0};
bool 	BGrid 			< string UIName="Composition Grid"; > = {false};
int 	iHelp 			< string UIName="                        ::SHOW TUTORIAL:: ->"; string UIWidget="dropdown"; int UIMin=0; int UIMax=5; string UIList="Off, Introduction, Camera, Lens, Post processing, Tips";> = {0};

/////////////////////////////////////////////////////////
//               	INTERNAL SETTINGS                  //
/////////////////////////////////////////////////////////

//LEVELS:
#define Levels_white_point 			260
#define Levels_black_point 			GuiBlack

//LUT
#define TuningColorLUTTileAmountX 	4096 
#define TuningColorLUTTileAmountY 	64 
#define TuningColorLUTNorm        	float2(1.0/float(TuningColorLUTTileAmountX),1.0/float(TuningColorLUTTileAmountY))
#define TuningColorLUTCount       	61
#define TuningColorLUTFCount       	33

//SHARPENING
//CloseDepth:
#define fSharpening 				sharp_strength
#define Sharpradius 				offset_bias*2.5
#define fFarDepth 					35
#define fLimiter  					0.5
//FarDepth:
#define show_sharpen  				0
#define pattern  					1
#define sharp_clamp  				0.035
#define offset_bias  				0.7
#define CoefLuma  					float3(0.2126, 0.7152, 0.0722)

//GRAIN
#define Use_GrainColour 			1
#define coloramount 				(0.35+fVHSIntensity*0.5)
#define grainsize 					(2.1+fVHSIntensity*0.1)
#define lumamount 					0.25

//VHS
#define VHS_Index 					iVHSType
#define tNoise_Color 				0.60
#define lNoise_Color 				0.55
#define VHS_bUseTapeNoise 			true
#define VHS_bUseLayerNoise 			true

//LETTERBOX
#define border_width 				float2(0,0)
#define border_colorBlack 			float3(0, 0, 0)
#define border_colorWhite 			float3(250, 250, 250)
#define screen_size 				float2(ScreenSize.x,ScreenSize.x*ScreenSize.w)
#define screen_ratio 				(screen_size.x / screen_size.y)
#define pixel 						float2(ScreenSize.y,ScreenSize.y*ScreenSize.z)

//VIGNETTE
#define EVignetteCurve 				1.5	
#define	EVignetteRadius 			border_ratio*0.25 
#define EVignetteColor 				(0.25,0.25,0.0)	

//TONEMAP
#define GammaD 						TonemapGammaD
#define GammaN 						TonemapGammaN
#define GammaI 						TonemapGammaI
#define Exposure 					TonemapExposure-1.0f
#define Defog 						0.0f
#define Saturation 					(camSat-1.0)
#define Bleach 						0.00 
#define FogColor 					float3(0.50,1.00,2.55)

//FILMIC RANGE
//Filmic Pass:
#define Strenght  					0.75 
#define BaseGamma  					1.0 
#define Fade  						0.0 
#define Contrast  					1.0 
#define FBleach  					0.00 
#define FSaturation  				-0.15 
#define FRedCurve  					1.0 
#define FGreenCurve  				1.0 
#define FBlueCurve  				1.0 
#define BaseCurve  					0.5 
#define Linearization  				0.50 
#define EffectGammaR  				1.0 
#define EffectGammaG  				1.0 
#define EffectGammaB  				1.0 
#define EffectGamma  				0.85 
#define LumCoeff  					float3(0.212656,0.715158,0.072186) 
//SEPIA:
#define ColorTone  					float3(1.00, 1.00, 1.1) 
#define GreyPower  					0.1 
#define SepiaPower  				0.1
//CURVES:
#define Curves_formula  			2
#define Curves_mode  				0
#define Curves_contrast  			0.12
//VIBRANCE:
#define Vibrance  					0.10
#define Vibrance_RGB_balance 		float3(0.00,0.00,1.10)

float4	Timer;				//x = generic timer in range 0..1, period of 16777216 ms (4.6 hours), y = average fps, w = frame time elapsed (in seconds)
float4	ScreenSize; 		//x = Width, y = 1/Width, z = aspect, w = 1/aspect, aspect is Width/Height
float	AdaptiveQuality; 	//changes in range 0..1, 0 means full quality, 1 lowest dynamic quality (0.33, 0.66 are limits for quality levels)
float4	Weather; 			//x = current weather index, y = outgoing weather index, z = weather transition, w = time of the day in 24 standart hours. Weather index is value from weather ini file, for example WEATHER002 means index==2, but index==0 means that weather not captured.
float4	TimeOfDay1; 		//x = dawn, y = sunrise, z = day, w = sunset. Interpolators range from 0..1
float4	TimeOfDay2; 		//x = dusk, y = night. Interpolators range from 0..1
float	ENightDayFactor; 	//changes in range 0..1, 0 means that night time, 1 - day time
float	EInteriorFactor; 	//changes 0 or 1. 0 means that exterior, 1 - interior

//keyboard controlled temporary variables. Press and hold key 1,2,3...8 together with PageUp or PageDown to modify. By default all set to 1.0
float4	tempF1; //0,1,2,3
float4	tempF2; //5,6,7,8
float4	tempF3; //9,0
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
float4	tempInfo1;
// xy = cursor position of previous left mouse button click
// zw = cursor position of previous right mouse button click
float4	tempInfo2;


/////////////////////////////////////////////////////////
//               		TEXTURES                       //
/////////////////////////////////////////////////////////

Texture2D TextureOriginal;  /// LDR color
Texture2D TextureColor;     /// LDR color which is output of previous technique
Texture2D TextureDepth;     /// scene depth

Texture2D RenderTargetRGBA32;   // R8G8B8A8 32 bit ldr format
Texture2D RenderTargetRGBA64;   // R16B16G16A16 64 bit ldr format
Texture2D RenderTargetRGBA64F;  // R16B16G16A16F 64 bit hdr format
Texture2D RenderTargetR16F;     // R16F 16 bit hdr format with red channel only
Texture2D RenderTargetR32F;     // R32F 32 bit hdr format with red channel only
Texture2D RenderTargetRGB32F;   // 32 bit hdr format without alpha

//COLORGRADES
Texture2D TextureColorGrades 		<string ResourceName = "Textures/Colorgrades/PRColorgrades.png";>;
Texture2D TextureFinish 			<string ResourceName = "Textures/Colorgrades/PRFinish.png";>;
Texture2D TextureDropBlue 			<string ResourceName = "Textures/Colorgrades/DropBlue.png";>;
Texture2D TextureLimitedRange 		<string ResourceName = "Textures/Colorgrades/LimitedRange.png";>;
///GRID & LIST

Texture2D TextureGrid 				<string ResourceName = "Textures/Overlays/CompositionGrid.png";>;
Texture2D TextureCGList 			<string ResourceName = "Textures/Overlays/List_CG.png";>;

///TUTORIALS
Texture2D TextureHelper1 			<string ResourceName = "Textures/Overlays/Tutorial/tuto_01.png";>;
Texture2D TextureHelper2 			<string ResourceName = "Textures/Overlays/Tutorial/tuto_02.png";>;
Texture2D TextureHelper3 			<string ResourceName = "Textures/Overlays/Tutorial/tuto_03.png";>;
Texture2D TextureHelper4 			<string ResourceName = "Textures/Overlays/Tutorial/tuto_04.png";>;
Texture2D TextureHelper5 			<string ResourceName = "Textures/Overlays/Tutorial/tuto_05.png";>;

//FRAMES
Texture2D TextureFrame01 			<string ResourceName = "Textures/Overlays/frame_01.png";>;
Texture2D TextureFrame02 			<string ResourceName = "Textures/Overlays/frame_02.png";>;
Texture2D TextureFrame03 			<string ResourceName = "Textures/Overlays/frame_03.png";>;
Texture2D TextureFrame04 			<string ResourceName = "Textures/Overlays/frame_04.png";>;
Texture2D TextureFrame05 			<string ResourceName = "Textures/Overlays/frame_05.png";>;
Texture2D TextureFrame06 			<string ResourceName = "Textures/Overlays/frame_06.png";>;
Texture2D TextureFrame07 			<string ResourceName = "Textures/Overlays/Frame_07OD.png";>;
Texture2D TextureFrame08 			<string ResourceName = "Textures/Overlays/frame_08.png";>;
Texture2D TextureFrame09 			<string ResourceName = "Textures/Overlays/frame_09.png";>;
Texture2D TextureFrame10 			<string ResourceName = "Textures/Overlays/frame_10.png";>;
Texture2D TextureFrame11 			<string ResourceName = "Textures/Overlays/frame_11.png";>;
Texture2D TextureFrame12 			<string ResourceName = "Textures/Overlays/Frame_12OD.png";>;
Texture2D TextureFrame13 			<string ResourceName = "Textures/Overlays/frame_13.png";>;
Texture2D TextureFrame14 			<string ResourceName = "Textures/Overlays/frame_14.png";>;
Texture2D TextureFrame15 			<string ResourceName = "Textures/Overlays/frame_15.png";>;
Texture2D TextureFrame16 			<string ResourceName = "Textures/Overlays/frame_16.png";>;
Texture2D TextureFrame17 			<string ResourceName = "Textures/Overlays/frame_17.png";>;
Texture2D TextureFrame18 			<string ResourceName = "Textures/Overlays/frame_18.png";>;
Texture2D TextureFrame19 			<string ResourceName = "Textures/Overlays/frame_19.png";>;
Texture2D TextureFrame20 			<string ResourceName = "Textures/Overlays/frame_20.png";>;
Texture2D TextureFrame21 			<string ResourceName = "Textures/Overlays/frame_21.png";>;
Texture2D TextureFrame22 			<string ResourceName = "Textures/Overlays/frame_22.png";>;
Texture2D TextureFrame23 			<string ResourceName = "Textures/Overlays/frame_23.png";>;
Texture2D TextureFrame24 			<string ResourceName = "Textures/Overlays/frame_24.png";>;
Texture2D TextureFrame25 			<string ResourceName = "Textures/Overlays/frame_25.png";>;

//DIRT
Texture2D TextureDirt01 			<string ResourceName = "Textures/Overlays/Dirt01.jpg";>;
Texture2D TextureDirt02 			<string ResourceName = "Textures/Overlays/Dirt02.jpg";>;
Texture2D TextureDirt03 			<string ResourceName = "Textures/Overlays/Dirt03.jpg";>;
Texture2D TextureDirt04 			<string ResourceName = "Textures/Overlays/Dirt04.jpg";>;
Texture2D TextureDirt05 			<string ResourceName = "Textures/Overlays/Dirt05.jpg";>;
Texture2D TextureDirt06 			<string ResourceName = "Textures/Overlays/Dirt06.jpg";>;
Texture2D TextureDirt07 			<string ResourceName = "Textures/Overlays/Dirt07.jpg";>;
Texture2D TextureDirt08 			<string ResourceName = "Textures/Overlays/Dirt08.png";>;
Texture2D TextureDirt09 			<string ResourceName = "Textures/Overlays/Dirt09.png";>;
Texture2D TextureDirt10 			<string ResourceName = "Textures/Overlays/Dirt10.png";>;

/////////////////////////////////////////////////////////
//               	    SAMPLERS                       //
/////////////////////////////////////////////////////////

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
SamplerState SamplerLut2
{
   Filter=MIN_MAG_MIP_LINEAR;  AddressU=Clamp;  AddressV=Clamp;
};
SamplerState SamplerPaper
{
   Filter = MIN_MAG_MIP_LINEAR; AddressU=Clamp;  AddressV=Clamp;
};

/////////////////////////////////////////////////////////
//               	   FUNCTIONS                 	   //
/////////////////////////////////////////////////////////

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

//+++++++++++++++++++++++++++++++++++++++++++++++++++++//
//       Contains helper functions and constants       //
//           used by the Modular Shader files          //
//-----------------------CREDITS-----------------------//
// JawZ: Author and developer of this file             //
// Erik Reinhard: Photographic Tone Reproduction       //
// Michael Stark: Photographic Tone Reproduction       //
// Peter Shirley: Photographic Tone Reproduction       //
// James Ferwerda: Photographic Tone Reproduction      //
// easyrgb.com: Example of the RGB>XYZ>Yxy color space //
// Charles Poynton: Color FAQ                          //
// Prod80: For code inspiration and general help       //
// CeeJay.dk: Split Screen                             //
// Matso: Texture atlas tiles sampling system          //
//+++++++++++++++++++++++++++++++++++++++++++++++++++++//
#define BlendSoftLightf(base, blend) 	((blend < 0.5) ? (2.0 * base * blend + base * base * (1.0 - 2.0 * blend)) : (sqrt(base) * (2.0 * blend - 1.0) + 2.0 * base * (1.0 - blend)))
#define BlendSoftLight(base, blend) 	Blend(base, blend, BlendSoftLightf)

// PI, required to calculate Gaussian weight
static const float PI = 3.1415926535897932384626433832795;

// Compute the average of the 4 necessary samples
float4 GreyScale(uniform SamplerState inSampler, float2 inTexCoords)
{
    float average = 0.0f;
    float maximum = -1e20;  /// 1e20 = 10000000000000000000.0
    float4 lum    = 0.0f;

        lum = tex2D(inSampler, inTexCoords.xy);

        float GreyValue = max(max(lum.r, lum.g), lum.b);  /// Compute the luminance component as per the HSL colour space
        //float GreyValue = max(lum.r, max(lum.g, lum.b)); /// Take the maximum value of the incoming, same as computing the brightness/value for an HSV/HSB conversion
        //float GreyValue = 0.5f * (max(lum.r, max(lum.g, lum.b)) + min(lum.r, min(lum.g, lum.b))); /// Compute the luminance component as per the HSL colour space
        //float GreyValue = length(lum.rgb); /// Use the magnitude of the colour

        maximum = max( maximum, GreyValue );
        average += (0.25f * log( 1e-5 + GreyValue )); /// 1e-5 necessary to stop the singularity at GreyValue=0, 1e-5 = 0.00001
        average = exp( average );

    return float4( average, maximum, 0.0f, 1.0f ); /// Output the luminance to the render target
}

// Luma coefficient gray value for use with color perception effects. Multiple versions
float4 AvgLuma(float3 inColor)
{
    return float4(dot(inColor, float3(0.2125f, 0.7154f, 0.0721f)),                 /// Perform a weighted average
                  max(inColor.r, max(inColor.g, inColor.b)),                       /// Take the maximum value of the incoming value
                  max(max(inColor.x, inColor.y), inColor.z),                       /// Compute the luminance component as per the HSL colour space
                  sqrt((inColor.x*inColor.x*0.2125f)+(inColor.y*inColor.y*0.7154f)+(inColor.z*inColor.z*0.0721f)));
}

// RGB to XYZ conversion
float3 RGBtoXYZ(float3 inColor)
{
  static const float3x3 RGB2XYZ = {0.412453f, 0.357580f, 0.180423f,
                                   0.212671f,  0.715160f, 0.072169f,
                                   0.019334f, 0.119193f,  0.950227f};
  return mul(RGB2XYZ, inColor.rgb);
}

// XYZ to Yxy conversion
float3 XYZtoYxy(float3 inXYZ)
{
   float4 inYxy = 0.0f;

   inYxy.r = inXYZ.g;                                  /// Copy luminance Y
   inYxy.g = inXYZ.r / (inXYZ.r + inXYZ.g + inXYZ.b ); /// x = X / (X + Y + Z)
   inYxy.b = inXYZ.g / (inXYZ.r + inXYZ.g + inXYZ.b ); /// y = Y / (X + Y + Z)

  return inYxy.rgb;
}

// Yxy to XYZ conversion
float3 YxytoXYZ(float3 inXYZ, float3 inYxy)
{
    inXYZ.r = inYxy.r * inYxy.g / inYxy. b;                /// X = Y * x / y
    inXYZ.g = inYxy.r;                                     /// Copy luminance Y
    inXYZ.b = inYxy.r * (1 - inYxy.g - inYxy.b) / inYxy.b; /// Z = Y * (1-x-y) / y

  return inXYZ;
  return inYxy;
}

// XYZ to RGB conversion
float3 XYZtoRGB(float3 inXYZ)
{
  static const float3x3 XYZ2RGB  = {3.240479f, -1.537150f, -0.498535f,
                                    -0.969256f, 1.875992f, 0.041556f, 
                                    0.055648f, -0.204043f, 1.057311f};
  return mul(XYZ2RGB, inXYZ);
}

// RGB to HSL conversion
float3 RGBToHSL(float3 color)
{
    float3 hsl; /// init to 0 to avoid warnings ? (and reverse if + remove first part)

    float fmin = min(min(color.r, color.g), color.b);
    float fmax = max(max(color.r, color.g), color.b);
    float delta = fmax - fmin;

    hsl.z = (fmax + fmin) / 2.0;

    if (delta == 0.0) /// No chroma
    {
        hsl.x = 0.0;  /// Hue
        hsl.y = 0.0;  /// Saturation
    }
    else /// Chromatic data
    {
        if (hsl.z < 0.5)
            hsl.y = delta / (fmax + fmin); /// Saturation
        else
            hsl.y = delta / (2.0 - fmax - fmin); /// Saturation

        float deltaR = (((fmax - color.r) / 6.0) + (delta / 2.0)) / delta;
        float deltaG = (((fmax - color.g) / 6.0) + (delta / 2.0)) / delta;
        float deltaB = (((fmax - color.b) / 6.0) + (delta / 2.0)) / delta;

        if (color.r == fmax )
            hsl.x = deltaB - deltaG; /// Hue
        else if (color.g == fmax)
            hsl.x = (1.0 / 3.0) + deltaR - deltaB; /// Hue
        else if (color.b == fmax)
            hsl.x = (2.0 / 3.0) + deltaG - deltaR; /// Hue

        if (hsl.x < 0.0)
            hsl.x += 1.0; /// Hue
        else if (hsl.x > 1.0)
            hsl.x -= 1.0; /// Hue
    }

    return hsl;
}

// HUE to RGB conversion
float HueToRGB(float f1, float f2, float hue)
{
    if (hue < 0.0)
        hue += 1.0;
    else if (hue > 1.0)
        hue -= 1.0;
    float res;
    if ((6.0 * hue) < 1.0)
        res = f1 + (f2 - f1) * 6.0 * hue;
    else if ((2.0 * hue) < 1.0)
        res = f2;
    else if ((3.0 * hue) < 2.0)
        res = f1 + (f2 - f1) * ((2.0 / 3.0) - hue) * 6.0;
    else
        res = f1;
    return res;
}

// HSL to RGB conversion
float3 HSLToRGB(float3 hsl)
{
    float3 rgb;

    if (hsl.y == 0.0)
        rgb = float3(hsl.z, hsl.z, hsl.z); // Luminance
    else
    {
        float f2;

        if (hsl.z < 0.5)
            f2 = hsl.z * (1.0 + hsl.y);
        else
        f2 = (hsl.z + hsl.y) - (hsl.y * hsl.z);

        float f1 = 2.0 * hsl.z - f2;

        rgb.r = HueToRGB(f1, f2, hsl.x + (1.0/3.0));
        rgb.g = HueToRGB(f1, f2, hsl.x);
        rgb.b= HueToRGB(f1, f2, hsl.x - (1.0/3.0));
    }

    return rgb;
}

// RGB to HSV conversion
float RGBCVtoHUE(in float3 RGB, in float C, in float V)
{
  float3 Delta = (V - RGB) / C;
    Delta.rgb -= Delta.brg;
    Delta.rgb += float3(2.0f, 4.0f, 6.0f);
    Delta.brg  = step(V, RGB) * Delta.brg;

  float H;
    H = max(Delta.r, max(Delta.g, Delta.b));

  return frac(H / 6.0f);
}

// RGB to HSV conversion
float3 RGBtoHSV(in float3 RGB)
{
  float3 HSV = 0.0f;
    HSV.z    = max(RGB.r, max(RGB.g, RGB.b));
    float M  = min(RGB.r, min(RGB.g, RGB.b));
    float C  = HSV.z - M;

  if (C != 0.0f)
  {
    HSV.x = RGBCVtoHUE(RGB, C, HSV.z);
    HSV.y = C / HSV.z;
  }

  return HSV;
}

// RGB to HSV conversion
float3 HUEtoRGBhsv(in float H)
{
    float R = abs(H * 6.0f - 3.0f) - 1.0f;
    float G = 2.0f - abs(H * 6.0f - 2.0f);
    float B = 2.0f - abs(H * 6.0f - 4.0f);

  return saturate(float3(R,G,B));
}

// RGB to HSV conversion
float3 HSVtoRGB(in float3 HSV)
{
    float3 RGB = HUEtoRGBhsv(HSV.x);

  return ((RGB - 1.0f) * HSV.y + 1.0f) * HSV.z;
}

// Luminance Blend
float3 BlendLuma(float3 base, float3 blend)
{
    float3 HSLBase 	= RGBToHSL(base);
    float3 HSLBlend	= RGBToHSL(blend);
    return HSLToRGB(float3(HSLBase.x, HSLBase.y, HSLBlend.z));
}

// Pseudo Random Number generator. 
float random(in float2 uv)
{
    float2 noise = (frac(sin(dot(uv , float2(12.9898,78.233) * 2.0)) * 43758.5453));
    return abs(noise.x + noise.y) * 0.5;
}

// Linear depth
float linearDepth(float d, float n, float f)
{
    return (2.0 * n)/(f + n - d * (f - n));
}
float linearDepth2(float nonLinDepth, float fZNear, float fZFar)
{
  float LinDepth = 1.0/max(1.0-nonLinDepth, 0.0000000001);
    LinDepth = -fZFar * fZNear / (LinDepth * (fZFar - fZNear) - fZFar);

  return LinDepth;
}

// Split screen, show applied effects only on a specified area of the screen. ENBSeries before and user altered After
float4 SplitScreen(float4 inColor2, float4 inColor, float2 inTexCoords, float inVar)
{
    return (inTexCoords.x < inVar) ? inColor2 : inColor;
}

// Clip Mode. Show which pixels are over and under exposed.
float3 ClipMode(float3 inColor)
{
  if (inColor.x >= 0.99999 && inColor.y >= 0.99999 && inColor.z >= 0.99999) inColor.xyz = float3(1.0f, 0.0f, 0.0f);
  if (inColor.x <= 0.00001 && inColor.y <= 0.00001 && inColor.z <= 0.00001) inColor.xyz = float3(0.0f, 0.0f, 1.0f);

    return inColor;
}

// Hue Blend mode creates the result color by combining the luminance and saturation of the base color with the hue of the blend color.
float3 BlendHue(float3 base, float3 blend)
{
	float3 baseHSL = RGBToHSL(base);
	return HSLToRGB(float3(RGBToHSL(blend).r, baseHSL.g, baseHSL.b));
}

// Saturation Blend mode creates the result color by combining the luminance and hue of the base color with the saturation of the blend color.
float3 BlendSaturation(float3 base, float3 blend)
{
	float3 baseHSL = RGBToHSL(base);
	return HSLToRGB(float3(baseHSL.r, RGBToHSL(blend).g, baseHSL.b));
}

// Color Mode keeps the brightness of the base color and applies both the hue and saturation of the blend color.
float3 BlendColor(float3 base, float3 blend)
{
	float3 blendHSL = RGBToHSL(blend);
	return HSLToRGB(float3(blendHSL.r, blendHSL.g, RGBToHSL(base).b));
}

// Luminosity Blend mode creates the result color by combining the hue and saturation of the base color with the luminance of the blend color.
float3 BlendLuminosity(float3 base, float3 blend)
{
	float3 baseHSL = RGBToHSL(base);
	return HSLToRGB(float3(baseHSL.r, baseHSL.g, RGBToHSL(blend).b));
}


/////////////////////////////////////////////////////////
//               	   VHS                 	   		   //
/////////////////////////////////////////////////////////
//Ported to SM5 HLSL by KINGERIC

//function overloading
float  SinC(float  x) { return (x==0.0)? 1.0: sin(x)/x; }
float2 SinC(float2 x) { return (x==0.0)? 1.0: sin(x)/x; }
float3 SinC(float3 x) { return (x==0.0)? 1.0: sin(x)/x; }
float4 SinC(float4 x) { return (x==0.0)? 1.0: sin(x)/x; }

float  ramp(float  y, float  start, float  end) { return saturate(1.0 - (y-start)/(end-start)); }
float2 ramp(float2 y, float2 start, float2 end) { return saturate(1.0 - (y-start)/(end-start)); }
float3 ramp(float3 y, float3 start, float3 end) { return saturate(1.0 - (y-start)/(end-start)); }
float4 ramp(float4 y, float4 start, float4 end) { return saturate(1.0 - (y-start)/(end-start)); }

 //index 0 == orig, 1 == NTSC, 2 == VHS, 3 == VCR, 4 == dirtyTV
//int  VHS_Index     <string UIName="VHS_Index"; float UIMin = 0.0; float UIMax = 4.0;> = {0};      //quality, [-1,0,1,2]

SamplerState VHS_SamplerLinear {
    Filter   = MIN_MAG_MIP_LINEAR;
    AddressU = Clamp;
    AddressV = Clamp;
};

SamplerState VHS_SamplerRepeat {
    Filter   = MIN_MAG_MIP_LINEAR;
    AddressU = Wrap;
    AddressV = Wrap;
};

#ifdef VHS_LOAD_UI
    #define UI
#else
    #define UI const static
#endif

Texture2D VHS_TexNoise <string ResourceName = "Textures/vhsNoise.png";>;

static const float  VHS_Timer      = Timer.x * 16777.216;
static const float2 VHS_ScreenSize = float2(ScreenSize.x, ScreenSize.x * ScreenSize.w);


struct VHS_struct {
    float4 pos    : SV_POSITION;
    float2 coord  : TEXCOORD0;
    float4 rand   : TEXCOORD1;
    float4 NTSCuv : TEXCOORD2;
};

VHS_struct VS_Shared( float4 pos, float2 coord)
{
    VHS_struct o = { float4(pos.rgb, 1.0),
             coord,
             VHS_TexNoise.SampleLevel(VHS_SamplerRepeat, VHS_Timer * float2( 0.01, 0.02), 0),
             coord.xxxy - float4(11.0, 10.0, 9.0, 0.0) * ScreenSize.y };
    return o;
}


float4 hash42(float2 p)
{
    float4 p4  = frac( p.xyxy * float4(443.8975,397.2973, 491.1871, 470.7827));
           p4 += dot( p4.wzxy, p4 + 19.19);
    return frac(p4.xxyx * p4.yzww);
}

float hash( float n ){ return frac(sin(n)*43758.5453123); }


/***************************************************************************
 *
 *  NTSC Codec: UltraMoogleMan (https://www.shadertoy.com/view/ldXGRf)
 *
 ***************************************************************************/

UI int    NTSC_Space          <string UIName="::::NTSC::::"; float UIMin = 1.0; float UIMax = 1.0;> = {1};
UI float  NTSC_A              <string UIName="NTSC_A"; > = { 0.5 };  //A value for NTSC signal processing.
UI float  NTSC_NotchHalfWidth <string UIName="NTSC_NotchHalfWidth"; > = { 2.0 };   // Phase Count value for NTSC signal processing. ?
UI float  NTSC_ScanTime       <string UIName="NTSC_ScanTime (u sec)"; > = { 52.6 };               // Horizontal scanline duration for NTSC signal processing. (usec)

static const float  NTSC_B        = 0.55;                // B value for NTSC signal processing.
static const float  NTSC_P        = 1.0;                // Y filter notch width for NTSC signal processing. (fixed to 1.0 for optimization.)
static const float  NTSC_CC       = 5.0;         // Color Carrier frequency for NTSC signal processing.
static const float3 NTSC_YIQ      = { 6.0, 1.2, 0.6 };  // YQI filter cutoff frequency for NTSC signal processing.
static const float  NTSC_MaxC     = 2.1183;
static const float  NTSC_MinC     = -1.1183;
static const float  NTSC_CRange   = NTSC_MaxC - NTSC_MinC;
static const float  NTSC_W        = 6.283185307 * NTSC_CC * NTSC_ScanTime;

static const float3x3 NTSC_Transform = {
        0.299,     0.587,     0.114,
        0.595716, -0.274453, -0.321263,
        0.211456, -0.522591,  0.311135
    };

static const float3x3 NTSC_InvTransform = {
        1.0,  0.956,  0.621,
        1.0, -0.272, -0.647,
        1.0, -1.106,  1.703
    };

float4 NTSC_encoder(float4 WT, float3 P0, float3 P1, float3 P2)
{
    float4x3 M  = { lerp(P1, P0, 0.25), P1, lerp(P1, P2, 0.25), lerp(P1, P2, 0.5)};
             M *= transpose(float3x4(float(1.0).xxxx, cos(WT), sin(WT)));
    return (mul(M, float(1.0).xxx) - NTSC_MinC) / NTSC_CRange;
}

//sinc
float3 VHS_NTSC_decoder(Texture2D texIn, float4 UV)
{
    // Frequency cutoffs for the individual portions of the signal that we extract.
    // Y1 and Y2 are the positive and negative frequency limits of the notch filter on Y.
    //
    float Fc_y1  = 0.5 * ScreenSize.y * NTSC_ScanTime * (NTSC_CC + NTSC_NotchHalfWidth);
    float Fc_y2  = 0.5 * ScreenSize.y * NTSC_ScanTime * (NTSC_CC - NTSC_NotchHalfWidth);
    float Fc_y   = 0.5 * ScreenSize.y * NTSC_ScanTime * NTSC_YIQ.x;
    float Fc_i   = 0.5 * ScreenSize.y * NTSC_ScanTime * NTSC_YIQ.y;
    float Fc_q   = 0.5 * ScreenSize.y * NTSC_ScanTime * NTSC_YIQ.z;

    float3x4 filter = { float(0.0).xxxx, float(0.0).xxxx, float(0.0).xxxx };

    float3 p0 = 0.0;
    float3 p1 = mul( NTSC_Transform, saturate(texIn.Sample(VHS_SamplerLinear, UV.xw).rgb));
    float3 p2 = mul( NTSC_Transform, saturate(texIn.Sample(VHS_SamplerLinear, UV.yw).rgb));

    UV.y += 10.0 * ScreenSize.y;

    //optimization: sampling 80 times -> 22 times
    for(float n = -41.0; n < 42.0; n += 4.0)
    {
        p0 = p1;
        p1 = p2;
        p2 = mul( NTSC_Transform, saturate(texIn.Sample(VHS_SamplerLinear, UV.zw).rgb));

        float4 tmp = float(n) + float4(0.0, 1.0, 2.0, 3.0);

        float3x4 IdealYIQ = {
            Fc_y2 * SinC(3.1415926535 * Fc_y2 * tmp) -
            Fc_y1 * SinC(3.1415926535 * Fc_y1 * tmp) +
            Fc_y  * SinC(3.1415926535 * Fc_y  * tmp),
            Fc_i  * SinC(3.1415926535 * Fc_i  * tmp),
            Fc_q  * SinC(3.1415926535 * Fc_q  * tmp)
        };

        float4 WT  = NTSC_A * 2.0 * ScreenSize.x * UV.w + NTSC_B + UV.y + ScreenSize.y * tmp * 0.25;
               WT *= NTSC_W;
        tmp   = 0.54 + 0.46 * cos(6.283185307 / 82.0 * tmp);
        tmp  *= NTSC_encoder(WT, p0, p1, p2) * NTSC_CRange + NTSC_MinC; // buffer for optimize sampling
        UV.z += ScreenSize.y;

        filter += IdealYIQ * float3x4(tmp, tmp*cos(WT), tmp*sin(WT));
    }

    return mul(NTSC_InvTransform, mul(filter, float(1.0).xxxx) * float3(1.0, 2.0, 2.0));
}

/***************************************************************************
 *
 *  VHS: drmelon (https://www.shadertoy.com/view/4dBGzK)
 *
 ***************************************************************************/

UI int   VHS_Space  <string UIName="::::VHS::::"; float UIMin = 2.0; float UIMax = 2.0;> = {2};
UI float VHS_magnitude <string UIName="VHS_magnitude";> = { 0.9 };
UI float fVHSredDistortion <string UIName="Red Distortion";> = { -0.0025 };
UI float fVHSimagenoise <string UIName="Image Noise";> = { -0.00114 };

float VHS_rand(float2 co)
{
    float3 parm = { 12.9898, 78.233, 43758.5453 };
    return frac(sin(dot(co, parm.xy) % 3.14) * parm.z);
}

float4 VHS_VHS(Texture2D texIN, float2 uv )
{
    float4 coord = uv.xxxy;

    coord.r += VHS_rand(float2(VHS_Timer * 0.03, coord.w * 0.42)) * lerp(0.0,(-0.005 , 0.005),fVHSIntensity); // fVHSredDistortion
    coord.r += sin(VHS_rand(float2(VHS_Timer*0.1, coord.w))) * fVHSIntensity * 0.000135;

    coord.g += VHS_rand(float2(VHS_Timer*0.0003,coord.w*0.2)) * lerp(0.0,(-0.002 , 0.002),fVHSIntensity*0.33); // fVHSimagenoise
    coord.g += sin(VHS_Timer * 0.108) * 0.0009;

    return float4( texIN.Sample(VHS_SamplerLinear, coord.ra).r,
                   texIN.Sample(VHS_SamplerLinear, coord.ga).g,
                   texIN.Sample(VHS_SamplerLinear, coord.ba).ba);
}


/***************************************************************************
 *
 *  Dirty old CRT: Klowner (https://www.shadertoy.com/view/MsXGD4)
 *
 ***************************************************************************/

UI int   dCRT_Space           <string UIName="::::CRT::::"; float UIMin = 4.0; float UIMax = 4.0;> = {4};
UI float dCRT_NoiseScale      <string UIName="CRT_NoiseScale";    > = { 4.0 };
UI float dCRT_StripCount      <string UIName="CRT_StripCount";    > = { 0.60 };
UI float dCRT_StripSpeed      <string UIName="CRT_StripSpeed";    > = { 0.67 };
UI float dCRT_StripStrength   <string UIName="CRT_StripStrength"; > = { 1.5 };

UI float dCRT_ScanlineCount    <string UIName="CRT_ScanlineCount";    > = { 1.0 };
UI float dCRT_ScanlineSpeed    <string UIName="CRT_ScanlineSpeed";    > = { 1.0 };
UI float dCRT_ScanlineStrength <string UIName="CRT_ScanlineStrength"; > = { 0.1 };


//unused
//float2 dCRT_colorshift0(float2 uv) {
//  return uv + float2( 0.0, sin(VHS_Timer)*0.02 );
//}

float2 dCRT_colorshift1(float2 uv, float amount)
{
    return uv + float2( 0.0, amount ); // amount * sin(uv.y * ScreenSize.x * ScreenSize.w * 0.12 + VHS_Timer)
}

// original note:
// from https://www.shadertoy.com/view/4sf3Dr
// Thanks, Jasper
float2 dCRT_crt(float2 coord, float bend)
{
    coord -= 0.5;
	
    coord.x *= 1.0 + pow((abs(coord.y) / bend), 2.0);
    coord.y *= 1.0 + pow((abs(coord.x) / bend), 2.0);

    return coord + 0.5;
}

//pre-compute rand in vertex shader
float3 VHS_DirtyCRT(Texture2D texIn, float2 txcoord, uniform float4 rand : TEXCOORD1 = {0.0, 0.0, 0.0, 0.0})
{
    float2 uv = txcoord;

	
    uv.x  = saturate(cos(uv.y * 2.0 + VHS_Timer));
    uv.x *= saturate(cos(uv.y * 2.0 + VHS_Timer + 4.0) * 10.0);
    uv.x  = txcoord.x - 0.05 * uv.x * lerp(VHS_TexNoise.Sample(VHS_SamplerRepeat, float2(txcoord.x, uv.x)).r, 1.0, 0.9);

    float3 color = {
        texIn.Sample(VHS_SamplerLinear, dCRT_crt(dCRT_colorshift1(uv, rand.r * 0.025), 2.0)).r,
        texIn.Sample(VHS_SamplerLinear, dCRT_crt(dCRT_colorshift1(uv, rand.g * 0.01 ), 2.0)).g,
        texIn.Sample(VHS_SamplerLinear, dCRT_crt(dCRT_colorshift1(uv, rand.b * 0.024), 2.0)).b
        };

    uv = dCRT_crt(uv.xy, 2.0);

    float scanline = sin(ScreenSize.x * ScreenSize.w * 0.7  * uv.y * dCRT_ScanlineCount - VHS_Timer * 10.0 * dCRT_ScanlineSpeed);
    float slowscan = sin(ScreenSize.x * ScreenSize.w * 0.02 * uv.y * dCRT_StripCount    - VHS_Timer * 3.0  * dCRT_StripSpeed);

    uv = pow(cos((txcoord - 0.5) * 0.98 * 3.1415926), 1.2);


    float noise = VHS_TexNoise.Sample(VHS_SamplerRepeat, txcoord * ScreenSize.x * 0.001 * dCRT_NoiseScale + VHS_Timer*6.0).r +
                  VHS_TexNoise.Sample(VHS_SamplerRepeat, txcoord * ScreenSize.x * 0.001 * dCRT_NoiseScale - VHS_Timer*4.0).g;

    return lerp(color, lerp(scanline * dCRT_ScanlineStrength, slowscan * dCRT_StripStrength, 0.5), 0.05)* clamp(noise, 0.96, 1.0);
}

/***************************************************************************
 *
 *  VHS Tape Noise: Vladmir Storm (https://www.shadertoy.com/view/MlfSWr)
 *
 ***************************************************************************/

UI int   tNoise_Space    <string UIName="::::TapeNoise::::"; > = {0};
UI float tNoise_linesN   <string UIName="TapeNoise_linesN"; float UIStep = 1.0;> = {960.0};
UI float tNoise_Curve    <string UIName="TapeNoise_Curve";> = {9.0};
UI float tNoise_Strength <string UIName="TapeNoise_Strength";> = {3.0};
//UI float3 tNoise_Color   <string UIName="TapeNoise_Color"; string UIWidget = "color"; > = { 0.78, 0.78, 0.78 };

// 3d noise function (iq's)
float n( float3 x )
{
    float3 p = floor(x);
    float3 f = frac(x);
    f *= f*(3.0-2.0*f);
    float n = dot(p, float3(1.0, 57.0, 113.0));

    return lerp(lerp(lerp( hash(n+  0.0), hash(n+  1.0),f.x),
                     lerp( hash(n+ 57.0), hash(n+ 58.0),f.x),f.y),
                lerp(lerp( hash(n+113.0), hash(n+114.0),f.x),
                     lerp( hash(n+170.0), hash(n+171.0),f.x),f.y),f.z);
}

float nn(float2 uv)
{
    float v = -VHS_Timer*2.0;
    v = (n(float3(uv.y * 0.01           + v, 1.0, 1.0)) + 0.0) *
        (n(float3(uv.y * 0.011 + 1000.0 + v, 1.0, 1.0)) + 0.0) *
        (n(float3(uv.y * 0.51  + 421.0  + v, 1.0, 1.0)) + 0.0);

    v *= hash42( float2(uv.x + v*0.01, uv.y)).x + 0.3;

    return  min(pow(v, tNoise_Curve)*tNoise_Strength, 1.0);
}

float4 VHS_TapeNoise(float4 color, float2 coord)
{
    const float Steps = VHS_ScreenSize.y/tNoise_linesN;
    float2 pos = coord*VHS_ScreenSize;
    float4 uv;
    uv.xy = floor(pos/Steps)*Steps;
    uv.zw = ceil(pos/Steps)*Steps;

    float2 w = (pos-uv.xy)/Steps;

    return float4(lerp(color.rgb, tNoise_Color,
                  lerp(lerp(nn(uv.xy), nn(uv.xw), w.y),
                       lerp(nn(uv.zy), nn(uv.zw), w.y), w.x)), color.a);
}

/***************************************************************************
 *
 *  Layered Noise:
 *
 ***************************************************************************/

UI int    lNoise_Space  <string UIName="::::LayerNoise::::"; > = {0};
UI float  lNoise_Ratio  <string UIName="LayerNoise_Ratio";> = {40.0};
UI float  lNoise_Scale  <string UIName="LayerNoise_Scale";> = {0.6};
//UI float3 lNoise_Color  <string UIName="LayerNoise_Color"; string UIWidget = "color"; > = { 0.78, 0.78, 0.78 };
UI float  lNoise_freq   <string UIName="LayerNoise_freq ";> = {2.0};
UI float  lNoise_Amp    <string UIName="LayerNoise_Amp  ";> = {0.02};
UI float  lNoise_inner  <string UIName="LayerNoise_inner";> = {1.0};
UI float  lNoise_outer  <string UIName="LayerNoise_outer";> = {1.0};

//implement bilinear
float4 Layer_noise(float2 seed)
{
    float2 size  = float2(lNoise_Ratio, 1.0)*lNoise_Scale;
    float4 p;
    p.xy = floor( seed.xy / size) * size;
    p.zw = ceil( seed.xy / size) * size;

    float2 w = (seed - p.xy)/size;
    float2 q = float2(frac(sin(VHS_Timer)+sin(0.1*VHS_Timer)), 0.0);

    return lerp(lerp(hash42(p.xy+q), hash42(p.xw+q), w.y),
               lerp(hash42(p.zy+q), hash42(p.zw+q), w.y), w.x);
}

float4 VHS_LayerNoise(float4 color, float2 uv )
{
    float2 fragCoord = uv*VHS_ScreenSize;

    float4 noise0 = Layer_noise(fragCoord);
    float4 noise1 = Layer_noise(VHS_ScreenSize-fragCoord)*noise0;

    float  d = smoothstep(lNoise_inner+lNoise_Amp*sin(VHS_Timer*lNoise_freq),
                          lNoise_outer+lNoise_Amp*sin(VHS_Timer*lNoise_freq),
                          length(fragCoord/VHS_ScreenSize-0.5));
    float4 noise = lerp(noise1 * 1.2, noise0, d);

	return float4(lerp(color.rgb, lNoise_Color, min(noise.x*noise.y*noise.z*noise.w*4.0, 1.0)), color.a);
}


float3 CleanEdges( float2 uv, float radius)
{
	float4 res;
	float3 color = TextureColor.Sample(Sampler1, uv).rgb;       
	float4 mask;
	float2	pixeloffset = ScreenSize.y;
	pixeloffset.y*= ScreenSize.z;
	
	//Edges
	float2	offsets[4] =
	{
		float2( 1.0,  0.0),
		float2(-1.0,  0.0),
		float2( 0.0,  1.0),
		float2( 0.0, -1.0)
	};
	float neighbours[4];
	for (int i = 0; i < 4; i++)
	{
		float2 coord = pixeloffset * (offsets[i] * radius) + uv.xy;
		neighbours[i] = TextureDepth.Sample(Sampler1, coord).x;
	}

	float Mid = TextureDepth.Sample(Sampler1, uv.xy).x; 

	float Right = neighbours[0];
	float Left = neighbours[1];
	float Bottom = neighbours[2];
	float Top = neighbours[3];

	float diffR = abs(Mid - Right);
	float diffL = abs(Mid - Left);
	float diffB = abs(Mid - Bottom);
	float diffT = abs(Mid - Top);

	float addRL = diffR + diffL;
	float addBT = diffB + diffT;

	float addAll = addRL + addBT;
	addAll *= 100;
	addAll = addAll * 5.0;
	addAll = pow(abs(addAll), 1.0);
	addAll = clamp(addAll, 0.0, 1.0);
	res = (addAll);

	return res;
}


/////////////////////////////////////////////////////////
//-------------------LUMASHARPENING--------------------//
// by Christian Cann Schuldt Jensen ~ CeeJay.dk        //
/////////////////////////////////////////////////////////

float3 CloseSharp(float2 uv, float Intensity, float radius) {

	float3 color = TextureColor.Sample(Sampler1, uv).rgb;       
	float4 mask= float4(CleanEdges(uv.xy, Sharpradius),1.0);
	float Depth = TextureDepth.Sample(Sampler0, uv.xy ).x;
	float linDepthFromS = linearDepth(Depth, 0.5f, fFarDepth);
	
	float px = ScreenSize.y;
	float py = ScreenSize.y * ScreenSize.z;

	float3 sharp_strength_luma = (CoefLuma * Intensity); 


	float3 blur_ori = TextureColor.Sample(Sampler1, uv + float2(0.4*px,-1.2*py)* Sharpradius).rgb;  
	blur_ori += TextureColor.Sample(Sampler1, uv + float2(-1.2*px,-0.4*py) * Sharpradius).rgb; 
	blur_ori += TextureColor.Sample(Sampler1, uv + float2(1.2*px,0.4*py) * Sharpradius).rgb; 
	blur_ori += TextureColor.Sample(Sampler1, uv + float2(-0.4*px,1.2*py) * Sharpradius).rgb; 

	blur_ori *= 0.25;  

	sharp_strength_luma *= 0.51;
  

	float3 sharp = color - blur_ori;  
	sharp = sharp * (1.0f - linDepthFromS);
	
	float4 sharp_strength_luma_clamp = float4(sharp_strength_luma * (0.5 / fLimiter),0.5); 
	float sharp_luma = saturate(dot(float4(sharp,1.0), sharp_strength_luma_clamp)); 
	sharp_luma = (fLimiter * 2.0) * sharp_luma - fLimiter; 
	
	float3 outputcolor = show_sharpen? saturate(0.5 + (sharp_luma * 4.0)).rrr:  color + sharp_luma;    

	if (Intensity>0.0)	outputcolor=lerp(outputcolor,color,saturate(mask));

	return saturate(outputcolor);
}
float3 CleanSharp( float2 uv, float Intensity, float radius)
{
	float3 color = TextureColor.Sample(Sampler1, uv).rgb;       
	float4 mask= float4(CleanEdges(uv.xy, radius),1.0);
	float Depth = TextureDepth.Sample(Sampler0, uv.xy ).x;
	float linDepthFromS = linearDepth(Depth, 0.5f, fFarDepth*2.0);
	
	float px = ScreenSize.y;
	float py = ScreenSize.y * ScreenSize.z;

	float3 sharp_strength_luma = (CoefLuma * (Intensity*2)); 

#if pattern == 1

	float3 blur_ori = TextureColor.Sample(Sampler1, uv + (float2(px,py) / 3.0) * radius).rgb;  
	blur_ori += TextureColor.Sample(Sampler1, uv + (float2(-px,-py) / 3.0) * radius).rgb; 

	blur_ori /= 2;  

	sharp_strength_luma *= 1.5; 

  #endif
  #if pattern == 2

	float3 blur_ori = TextureColor.Sample(Sampler1, uv + float2(px,-py) * 0.5 * radius).rgb; 
	blur_ori += TextureColor.Sample(Sampler1, uv + float2(-px,-py) * 0.5 * radius).rgb;  
	blur_ori += TextureColor.Sample(Sampler1, uv + float2(px,py) * 0.5 * radius).rgb; 
	blur_ori += TextureColor.Sample(Sampler1, uv + float2(-px,py) * 0.5 * radius).rgb;

	blur_ori *= 0.25;  

  #endif
  #if pattern == 3

	float3 blur_ori = TextureColor.Sample(Sampler1, uv + float2(0.4*px,-1.2*py)* radius).rgb;  
	blur_ori += TextureColor.Sample(Sampler1, uv + float2(-1.2*px,-0.4*py) * radius).rgb; 
	blur_ori += TextureColor.Sample(Sampler1, uv + float2(1.2*px,0.4*py) * radius).rgb; 
	blur_ori += TextureColor.Sample(Sampler1, uv + float2(-0.4*px,1.2*py) * radius).rgb; 

	blur_ori *= 0.25;  

	sharp_strength_luma *= 0.51;
  #endif
  #if pattern == 4

	float3 blur_ori = TextureColor.Sample(Sampler1, uv + float2(0.5 * px,-py * radius)).rgb;  
	blur_ori += TextureColor.Sample(Sampler1, uv + float2(radius * -px,0.5 * -py)).rgb; 
	blur_ori += TextureColor.Sample(Sampler1, uv + float2(radius * px,0.5 * py)).rgb; 
	blur_ori += TextureColor.Sample(Sampler1, uv + float2(0.5 * -px,py * radius)).rgb;

	blur_ori /= 4.0;  

	sharp_strength_luma *= 0.666; 
  #endif

	float3 sharp = color - blur_ori;  
	sharp = sharp * (1.0f - linDepthFromS);
	
	float4 sharp_strength_luma_clamp = float4(sharp_strength_luma * (0.5 / sharp_clamp),0.5); 
	float sharp_luma = saturate(dot(float4(sharp,1.0), sharp_strength_luma_clamp)); 
	sharp_luma = (sharp_clamp * 2.0) * sharp_luma - sharp_clamp; 
	
	float3 outputcolor = show_sharpen? saturate(0.5 + (sharp_luma * 4.0)).rrr:  color + sharp_luma;    

	if (Intensity>0.0)	outputcolor=lerp(outputcolor,color,saturate(mask));

	return saturate(outputcolor);
}

/////////////////////////////////////////////////////////
//-----------------------TONEMAP-----------------------//
// by Christian Cann Schuldt Jensen ~ CeeJay.dk        //
/////////////////////////////////////////////////////////
 float4 SaturationPass( float4 colorInput)
{
	float3 color = colorInput.rgb;

	float3 lumCoeff = float3(0.2126, 0.7152, 0.0722);
	float lum = dot(lumCoeff, color.rgb);
	
	float3 blend = lum.rrr;
	
	float L = saturate( 10.0 * (lum - 0.45) );
  	
	float3 result1 = 2.0f * color.rgb * blend;
	float3 result2 = 1.0f - 2.0f * (1.0f - blend) * (1.0f - color.rgb);
	
	float3 newColor = lerp(result1, result2, L);
	float3 A2 = 0.0f * color.rgb; 
	float3 mixRGB = A2 * newColor;
	
	color.rgb += ((1.0f - A2) * mixRGB);

	float3 middlegray = dot(color,(1.0/3.0));
	
	float3 diffcolor = color - middlegray;
	if (Saturation>=1.0) 	colorInput.rgb = (color + diffcolor * Saturation)/(1+(diffcolor*Saturation));
	if (Saturation<1.0) 	colorInput.rgb = (color + diffcolor * Saturation);

	return saturate(colorInput);
}

   float4 TonemapPass( float4 colorInput)
{
	float3 color = colorInput.rgb;
	
	float Gamma   = lerp(lerp(GammaN, GammaD, ENightDayFactor), GammaI, EInteriorFactor);

	color *= pow(2.0f, Exposure);
	//color *= BlendLuminosity(color, pow(2.0f, Exposure));
	color = pow(color, Gamma);

	float3 lumCoeff = float3(0.2126, 0.7152, 0.0722);
	float lum = dot(lumCoeff, color.rgb);
	
	float3 blend = lum.rrr;
	
	float L = saturate( 10.0 * (lum - 0.45) );
  	
	float3 result1 = 2.0f * color.rgb * blend;
	float3 result2 = 1.0f - 2.0f * (1.0f - blend) * (1.0f - color.rgb);
	
	float3 newColor = lerp(result1, result2, L);
	float3 A2 = Bleach * color.rgb;
	float3 mixRGB = A2 * newColor;
	
	color.rgb += ((1.0f - A2) * mixRGB);

	float3 middlegray = dot(color,(1.0/3.0));
	
	float3 diffcolor = color - middlegray;
	colorInput.rgb = (color + diffcolor * 0.0f)/(1+(diffcolor*0.0f));

	return saturate(colorInput);
}


/////////////////////////////////////////////////////////
//-----------------------LEVELS------------------------//
// by Christian Cann Schuldt Jensen ~ CeeJay.dk        //
/////////////////////////////////////////////////////////

   #define black_point_float ( Levels_black_point / 255.0 )

#if (Levels_white_point == Levels_black_point) //avoid division by zero if the white and black point are the same
  #define white_point_float ( 255.0 / 0.00025)
#else
  #define white_point_float ( 255.0 / (Levels_white_point - Levels_black_point))
#endif

float4 LevelsPass( float4 colorInput )
{
  colorInput.rgb = colorInput.rgb * white_point_float - (black_point_float *  white_point_float);

  #if (Levels_highlight_clipping == 1)

    float3 clipped_colors = any(colorInput.rgb > saturate(colorInput.rgb)) //any colors whiter than white?
                    ? float3(1.0, 0.0, 0.0)
                    : colorInput.rgb;
                    
    clipped_colors = all(colorInput.rgb > saturate(colorInput.rgb)) //all colors whiter than white?
                    ? float3(1.0, 1.0, 0.0)
                    : clipped_colors;
                    
    clipped_colors = any(colorInput.rgb < saturate(colorInput.rgb)) //any colors blacker than black?
                    ? float3(0.0, 0.0, 1.0)
                    : clipped_colors;
                    
    clipped_colors = all(colorInput.rgb < saturate(colorInput.rgb)) //all colors blacker than black?
                    ? float3(0.0, 1.0, 1.0)
                    : clipped_colors;                    
                    
    colorInput.rgb = clipped_colors;
    
  #endif

  return colorInput;
}

/////////////////////////////////////////////////////////
//----------------------VIBRANCE-----------------------//
// by Christian Cann Schuldt Jensen ~ CeeJay.dk        //
/////////////////////////////////////////////////////////

   float4 VibrancePass( float4 colorInput )
{ /*
  #ifndef Vibrance_RGB_balance //for backwards compatibility with setting presets for older version.
    #define Vibrance_RGB_balance float3(1.00, 1.00, 1.00)
  #endif
  */
  #define Vibrance_coeff float3(Vibrance_RGB_balance * Vibrance)

	float4 color = colorInput; //original input color
  float3 lumCoeff = float3(0.212656, 0.715158, 0.072186);  //Values to calculate luma with

	float luma = dot(lumCoeff, color.rgb); //calculate luma (grey)


	float max_color = max(colorInput.r, max(colorInput.g,colorInput.b)); //Find the strongest color
	float min_color = min(colorInput.r, min(colorInput.g,colorInput.b)); //Find the weakest color

	float color_saturation = max_color - min_color; //The difference between the two is the saturation

/*
	float3 sort = colorInput.rgb;
	float2 sort1 = (sort.r > sort.g) ? sort.gr : sort.rg;
	float2 sort2 = (sort.g > sort.b) ? sort.bg : sort.gb;

	sort.gb = (sort1.g > sort2.g) ? float2(sort2.g,sort1.g) : float2(sort1.g,sort2.g); //max is now stored in .b
	sort.r = (sort1.r < sort2.r) ? sort1.r : sort2.r; //sorted : min is .r , med is .g and max is .b
	
	float color_saturation = sort.b - sort.r; //The difference between the two is the saturation
*/

/*	
	float3 sort = colorInput.rgb;
	sort.rg = (sort.r > sort.g) ? sort.gr : sort.rg;
	sort.gb = (sort.g > sort.b) ? sort.bg : sort.gb; //max is now stored in .b
	sort.rg = (sort.r > sort.g) ? sort.gr : sort.rg; //sorted : min is .r , med is .g and max is .b
	
	float color_saturation = sort.b - sort.r; //The difference between the two is the saturation
*/


/*
	float4 sort = colorInput;
	sort.rg = (sort.r > sort.g) ? sort.gr : sort.rg;
	sort.gb = (sort.g > sort.b) ? sort.bg : sort.gb; //max is now stored in .b
	
	float color_saturation = sort.b - min(sort.r,sort.g); //The difference between the two is the saturation
*/

  //color.rgb = lerp(luma, color.rgb, (1.0 + (Vibrance * (1.0 - color_saturation)))); //extrapolate between luma and original by 1 + (1-saturation) - simple

  //color.rgb = lerp(luma, color.rgb, (1.0 + (Vibrance * (1.0 - (sign(Vibrance) * color_saturation))))); //extrapolate between luma and original by 1 + (1-saturation) - current
  color.rgb = lerp(luma, color.rgb, (1.0 + (Vibrance_coeff * (1.0 - (sign(Vibrance_coeff) * color_saturation))))); //extrapolate between luma and original by 1 + (1-saturation) - current

  //color.rgb = lerp(luma, color.rgb, 1.0 + (1.0-pow(color_saturation, 1.0 - (1.0-Vibrance))) ); //pow version

	return color; //return the result
	//return color_saturation.xxxx; //Visualize the saturation
}

/////////////////////////////////////////////////////////
//-----------------------CURVES------------------------//
// by Christian Cann Schuldt Jensen ~ CeeJay.dk        //
/////////////////////////////////////////////////////////

   float4 CurvesPass( float4 colorInput )
{
  float3 lumCoeff = float3(0.2126, 0.7152, 0.0722);  //Values to calculate luma with
  float Curves_contrast_blend = Curves_contrast;
  
  #ifndef PI
    #define PI 3.1415927
  #endif

   /*-----------------------------------------------------------.
  /               Separation of Luma and Chroma                 /
  '-----------------------------------------------------------*/

  // -- Calculate Luma and Chroma if needed --
  #if Curves_mode != 2

    //calculate luma (grey)
    float luma = dot(lumCoeff, colorInput.rgb);

    //calculate chroma
	  float3 chroma = colorInput.rgb - luma;
  #endif

  // -- Which value to put through the contrast formula? --
  // I name it x because makes it easier to copy-paste to Graphtoy or Wolfram Alpha or another graphing program
  #if Curves_mode == 2
	  float3 x = colorInput.rgb; //if the curve should be applied to both Luma and Chroma
	#elif Curves_mode == 1
	  float3 x = chroma; //if the curve should be applied to Chroma
	  x = x * 0.5 + 0.5; //adjust range of Chroma from -1 -> 1 to 0 -> 1
  #else // Curves_mode == 0
    float x = luma; //if the curve should be applied to Luma
  #endif

   /*-----------------------------------------------------------.
  /                     Contrast formulas                       /
  '-----------------------------------------------------------*/

  // -- Curve 1 --
  #if Curves_formula == 1
    x = sin(PI * 0.5 * x); // Sin - 721 amd fps, +vign 536 nv
    x *= x;
    
    //x = 0.5 - 0.5*cos(PI*x);
    //x = 0.5 * -sin(PI * -x + (PI*0.5)) + 0.5;
  #endif

  // -- Curve 2 --
  #if Curves_formula == 2
    x = x - 0.5;  
    x = ( x / (0.5 + abs(x)) ) + 0.5;
    
    //x = ( (x - 0.5) / (0.5 + abs(x-0.5)) ) + 0.5;
  #endif

  // -- Curve 3 --
  #if Curves_formula == 3
    //x = smoothstep(0.0,1.0,x); //smoothstep
    x = x*x*(3.0-2.0*x); //faster smoothstep alternative - 776 amd fps, +vign 536 nv
    //x = x - 2.0 * (x - 1.0) * x* (x- 0.5);  //2.0 is contrast. Range is 0.0 to 2.0
  #endif

  // -- Curve 4 --
  #if Curves_formula == 4
    x = (1.0524 * exp(6.0 * x) - 1.05248) / (exp(6.0 * x) + 20.0855); //exp formula
  #endif

  // -- Curve 5 --
  #if Curves_formula == 5
    //x = 0.5 * (x + 3.0 * x * x - 2.0 * x * x * x); //a simplified catmull-rom (0,0,1,1) - btw smoothstep can also be expressed as a simplified catmull-rom using (1,0,1,0)
    //x = (0.5 * x) + (1.5 -x) * x*x; //estrin form - faster version
    x = x * (x * (1.5-x) + 0.5); //horner form - fastest version

    Curves_contrast_blend = Curves_contrast * 2.0; //I multiply by two to give it a strength closer to the other curves.
  #endif

 	// -- Curve 6 --
  #if Curves_formula == 6
    x = x*x*x*(x*(x*6.0 - 15.0) + 10.0); //Perlins smootherstep
	#endif

	// -- Curve 7 --
  #if Curves_formula == 7
    //x = ((x-0.5) / ((0.5/(4.0/3.0)) + abs((x-0.5)*1.25))) + 0.5;
	x = x - 0.5;
	x = x / ((abs(x)*1.25) + 0.375 ) + 0.5;
	//x = ( (x-0.5) / ((abs(x-0.5)*1.25) + (0.5/(4.0/3.0))) ) + 0.5;
  #endif

  // -- Curve 8 --
  #if Curves_formula == 8
    x = (x * (x * (x * (x * (x * (x * (1.6 * x - 7.2) + 10.8) - 4.2) - 3.6) + 2.7) - 1.8) + 2.7) * x * x; //Techicolor Cinestyle - almost identical to curve 1
  #endif

  // -- Curve 9 --
  #if Curves_formula == 9
    x =  -0.5 * (x*2.0-1.0) * (abs(x*2.0-1.0)-2.0) + 0.5; //parabola
  #endif

  // -- Curve 10 --
  #if Curves_formula == 10 //Half-circles

    #if Curves_mode == 0

			float xstep = step(x,0.5); //tenary might be faster here
			float xstep_shift = (xstep - 0.5);

			/*
			float xstep = (x < 0.5) ? 1.0 : 0.0; //tenary version
			float xstep_shift = (x < 0.5) ? 0.5 : -0.5;
			*/

			float shifted_x = x + xstep_shift;
	  
	  
    #else
			float3 xstep = step(x,0.5);
			float3 xstep_shift = (xstep - 0.5);
	  
			/*
			float3 xstep = float3(0.0,0.0,0.0);
			xstep.r = (x.r < 0.5) ? 1.0 : 0.0;
			xstep.g = (x.g < 0.5) ? 1.0 : 0.0;
			xstep.b = (x.b < 0.5) ? 1.0 : 0.0;
			float3 xstep_shift = float3(0.0,0.0,0.0);
			xstep_shift.r = (x.r < 0.5) ? 0.5 : -0.5;
			xstep_shift.g = (x.g < 0.5) ? 0.5 : -0.5;
			xstep_shift.b = (x.b < 0.5) ? 0.5 : -0.5;
			*/

			float3 shifted_x = x + xstep_shift;
    #endif

	x = abs(xstep - sqrt(-shifted_x * shifted_x + shifted_x) ) - xstep_shift;

  //x = abs(step(x,0.5)-sqrt(-(x+step(x,0.5)-0.5)*(x+step(x,0.5)-0.5)+(x+step(x,0.5)-0.5)))-(step(x,0.5)-0.5); //single line version of the above
    
  //x = 0.5 + (sign(x-0.5)) * sqrt(0.25-(x-trunc(x*2))*(x-trunc(x*2))); //worse
  
  /* // if/else - even worse
  if (x-0.5)
  x = 0.5-sqrt(0.25-x*x);
  else
  x = 0.5+sqrt(0.25-(x-1)*(x-1));
	*/

  //x = (abs(step(0.5,x)-clamp( 1-sqrt(1-abs(step(0.5,x)- frac(x*2%1)) * abs(step(0.5,x)- frac(x*2%1))),0 ,1))+ step(0.5,x) )*0.5; //worst so far
	
	//TODO: Check if I could use an abs split instead of step. It might be more efficient
	
	Curves_contrast_blend = Curves_contrast * 0.5; //I divide by two to give it a strength closer to the other curves.
  #endif
  
    // -- Curve 11 --
  #if Curves_formula == 11 //
  	#if Curves_mode == 0
			float a = 0.0;
			float b = 0.0;
		#else
			float3 a = float3(0.0,0.0,0.0);
			float3 b = float3(0.0,0.0,0.0);
		#endif

    a = x * x * 2.0;
    b = (2.0 * -x + 4.0) * x - 1.0;
    x = (x < 0.5) ? a : b;
  #endif


  // -- Curve 21 --
  #if Curves_formula == 21 //Cubic catmull
    float a = 1.00; //control point 1
    float b = 0.00; //start point
    float c = 1.00; //endpoint
    float d = 0.20; //control point 2
    x = 0.5 * ((-a + 3*b -3*c + d)*x*x*x + (2*a -5*b + 4*c - d)*x*x + (-a+c)*x + 2*b); //A customizable cubic catmull-rom spline
  #endif

  // -- Curve 22 --
  #if Curves_formula == 22 //Cubic Bezier spline
    float a = 0.00; //start point
    float b = 0.00; //control point 1
    float c = 1.00; //control point 2
    float d = 1.00; //endpoint

    float r  = (1-x);
	float r2 = r*r;
	float r3 = r2 * r;
	float x2 = x*x;
	float x3 = x2*x;
	//x = dot(float4(a,b,c,d),float4(r3,3*r2*x,3*r*x2,x3));

	//x = a * r*r*r + r * (3 * b * r * x + 3 * c * x*x) + d * x*x*x;
	//x = a*(1-x)*(1-x)*(1-x) +(1-x) * (3*b * (1-x) * x + 3 * c * x*x) + d * x*x*x;
	x = a*(1-x)*(1-x)*(1-x) + 3*b*(1-x)*(1-x)*x + 3*c*(1-x)*x*x + d*x*x*x;
  #endif

  // -- Curve 23 --
  #if Curves_formula == 23 //Cubic Bezier spline - alternative implementation.
    float3 a = float3(0.00,0.00,0.00); //start point
    float3 b = float3(0.25,0.15,0.85); //control point 1
    float3 c = float3(0.75,0.85,0.15); //control point 2
    float3 d = float3(1.00,1.00,1.00); //endpoint

    float3 ab = lerp(a,b,x);           // point between a and b
    float3 bc = lerp(b,c,x);           // point between b and c
    float3 cd = lerp(c,d,x);           // point between c and d
    float3 abbc = lerp(ab,bc,x);       // point between ab and bc
    float3 bccd = lerp(bc,cd,x);       // point between bc and cd
    float3 dest = lerp(abbc,bccd,x);   // point on the bezier-curve
    x = dest;
  #endif

  // -- Curve 24 --
  #if Curves_formula == 24
    x = 1.0 / (1.0 + exp(-(x * 10.0 - 5.0))); //alternative exp formula
  #endif

   /*-----------------------------------------------------------.
  /                 Joining of Luma and Chroma                  /
  '-----------------------------------------------------------*/

  #if Curves_mode == 2 //Both Luma and Chroma
	float3 color = x;  //if the curve should be applied to both Luma and Chroma
	colorInput.rgb = lerp(colorInput.rgb, color, Curves_contrast_blend); //Blend by Curves_contrast

  #elif Curves_mode == 1 //Only Chroma
	x = x * 2.0 - 1.0; //adjust the Chroma range back to -1 -> 1
	float3 color = luma + x; //Luma + Chroma
	colorInput.rgb = lerp(colorInput.rgb, color, Curves_contrast_blend); //Blend by Curves_contrast

  #else // Curves_mode == 0 //Only Luma
    x = lerp(luma, x, Curves_contrast_blend); //Blend by Curves_contrast
    colorInput.rgb = x + chroma; //Luma + Chroma

  #endif

  //Return the result
  return colorInput;
}

/////////////////////////////////////////////////////////
//------------------------SEPIA------------------------//
// by Christian Cann Schuldt Jensen ~ CeeJay.dk        //
/////////////////////////////////////////////////////////

float4 SepiaPass( float4 colorInput )
{
	// calculating amounts of input, grey and sepia colors to blend and combine
	float  grey   = dot(colorInput.rgb, float3(0.2126, 0.7152, 0.0722));
    float3 sepia  = colorInput.rgb * ColorTone;
    float3 blend2 = (grey * GreyPower) + (colorInput.rgb / (GreyPower + 1));
	return float4(lerp(blend2, sepia, SepiaPower), colorInput.a); 	// returning the final color
}

/////////////////////////////////////////////////////////
//---------------------FILMICPASS----------------------//
// by Christian Cann Schuldt Jensen ~ CeeJay.dk        //
/////////////////////////////////////////////////////////

   float4 FilmPass(float4 colorInput)
{
	float3 B = colorInput.rgb;	

	float3 G = B;
	float3 H = 0.01;
 
	B = saturate(B);
	B = pow(B, Linearization); 
	B = lerp(H, B, Contrast);
 
	float A = dot(B.rgb, LumCoeff);
	float3 D = A;
 
	B = pow(B, 1.0 / BaseGamma);
 
	float a = FRedCurve;
	float b = FGreenCurve;
	float c = FBlueCurve;
	float d = BaseCurve;
 
	float y = 1.0 / (1.0 + exp(a / 2.0));
	float z = 1.0 / (1.0 + exp(b / 2.0));
	float w = 1.0 / (1.0 + exp(c / 2.0));
	float v = 1.0 / (1.0 + exp(d / 2.0));
 
	float3 C = B;
 
	D.r = (1.0 / (1.0 + exp(-a * (D.r - 0.5))) - y) / (1.0 - 2.0 * y);
	D.g = (1.0 / (1.0 + exp(-b * (D.g - 0.5))) - z) / (1.0 - 2.0 * z);
	D.b = (1.0 / (1.0 + exp(-c * (D.b - 0.5))) - w) / (1.0 - 2.0 * w);
 
	D = pow(D, 1.0 / EffectGamma);
 
	float3 Di = 1.0 - D;
 
	D = lerp(D, Di, FBleach);
 
	D.r = pow(abs(D.r), 1.0 / EffectGammaR);
	D.g = pow(abs(D.g), 1.0 / EffectGammaG);
	D.b = pow(abs(D.b), 1.0 / EffectGammaB);
 
	if (D.r < 0.5)
		C.r = (2.0 * D.r - 1.0) * (B.r - B.r * B.r) + B.r;
	else
		C.r = (2.0 * D.r - 1.0) * (sqrt(B.r) - B.r) + B.r;
 
	if (D.g < 0.5)
		C.g = (2.0 * D.g - 1.0) * (B.g - B.g * B.g) + B.g;
	else
		C.g = (2.0 * D.g - 1.0) * (sqrt(B.g) - B.g) + B.g;
 	//if (AgainstAllAutority) 
	if (D.b < 0.5)
		C.b = (2.0 * D.b - 1.0) * (B.b - B.b * B.b) + B.b;
	else
		C.b = (2.0 * D.b - 1.0) * (sqrt(B.b) - B.b) + B.b;
 
	float3 F = lerp(B, C, Strenght);
 
	F = (1.0 / (1.0 + exp(-d * (F - 0.5))) - v) / (1.0 - 2.0 * v);
 
	float r2R = 1.0 - FSaturation;
	float g2R = 0.0 + FSaturation;
	float b2R = 0.0 + FSaturation;
 
	float r2G = 0.0 + FSaturation;
	float g2G = (1.0 - Fade) - FSaturation;
	float b2G = (0.0 + Fade) + FSaturation;
 
	float r2B = 0.0 + FSaturation;
	float g2B = (0.0 + Fade) + FSaturation;
	float b2B = (1.0 - Fade) - FSaturation;
 
	float3 iF = F;
 
	F.r = (iF.r * r2R + iF.g * g2R + iF.b * b2R);
	F.g = (iF.r * r2G + iF.g * g2G + iF.b * b2G);
	F.b = (iF.r * r2B + iF.g * g2B + iF.b * b2B);
 
	float N = dot(F.rgb, LumCoeff);
	float3 Cn = F;
 
	if (N < 0.5)
		Cn = (2.0 * N - 1.0) * (F - F * F) + F;
	else
		Cn = (2.0 * N - 1.0) * (sqrt(F) - F) + F;
 
	Cn = pow(max(Cn,0), 1.0 / Linearization);
 
	float3 Fn = lerp(B, Cn, Strenght);
	
	colorInput.rgb = Fn;
	
	return colorInput;
	
}

//-----------------------DITHER------------------------//

//  SandvichDISH: Author of Dither code                //


#define remap(v, a, b) (((v) - (a)) / ((b) - (a)))
float rand21(float2 uv)
{
    float2 noise = frac(sin(dot(uv, float2(12.9898, 78.233) * 2.0)) * 43758.5453);
    return (noise.x + noise.y) * 0.5;
}

float rand11(float x) { return frac(x * 0.024390243); }
float permute(float x) { return ((34.0 * x + 1.0) * x) % 289.0; }

#define DITHER_QUALITY_LEVEL 2
#define BIT_DEPTH 8

float3 triDither(float3 color, float2 uv, float timer)
{
    static const float bitstep = pow(2.0, BIT_DEPTH) - 1.0;
    static const float lsb = 1.0 / bitstep;
    static const float lobit = 0.5 / bitstep;
    static const float hibit = (bitstep - 0.5) / bitstep;

    float3 m = float3(uv, rand21(uv + timer)) + 1.0;
    float h = permute(permute(permute(m.x) + m.y) + m.z);

    float3 noise1, noise2;
    noise1.x = rand11(h); h = permute(h);
    noise2.x = rand11(h); h = permute(h);
    noise1.y = rand11(h); h = permute(h);
    noise2.y = rand11(h); h = permute(h);
    noise1.z = rand11(h); h = permute(h);
    noise2.z = rand11(h);

    float3 lo = saturate(remap(color.xyz, 0.0, lobit));
    float3 hi = saturate(remap(color.xyz, 1.0, hibit));
    float3 uni = noise1 - 0.5;
    float3 tri = noise1 - noise2;
    return float3(
        lerp(uni.x, tri.x, min(lo.x, hi.x)),
        lerp(uni.y, tri.y, min(lo.y, hi.y)),
        lerp(uni.z, tri.z, min(lo.z, hi.z))) * lsb;


}

/////////////////////////////////////////////////////////
//-----------------------GRAIN-------------------------//
// martinsh:          Author of Film Grain             //
// Angelo Gonzalez:   port to ReShade                  //
// roxahris:          port to ENB                      //
/////////////////////////////////////////////////////////

// a random texture generator, but you can also use a pre-computed perturbation texture
float4 rnm(in float2 tc)
  {
    float noise = sin(dot(float3(tc.x, tc.y, Timer.x*16777216), float3(12.9898, 78.233, 0.0025216))) * 43758.5453;

    float noiseR =  frac(noise)*2.0-1.0;
    float noiseG =  frac(noise*1.2154)*2.0-1.0;
    float noiseB =  frac(noise*1.3453)*2.0-1.0;
    float noiseA =  frac(noise*1.3647)*2.0-1.0;

    return float4(noiseR,noiseG,noiseB,noiseA);
    }

  float fade(in float t)
  {
    return t*t*t*(t*(t*6.0-15.0)+10.0);
  }

  float pnoise3D(in float3 p)
  {
    static const float permTexUnit = 1.0/256.0;        // Perm texture texel-size
    static const float permTexUnitHalf = 0.5/256.0;    // Half perm texture texel-size
    float3 pi = permTexUnit*floor(p)+permTexUnitHalf; // Integer part, scaled so +1 moves permTexUnit texel
    // and offset 1/2 texel to sample texel centers
    float3 pf = frac(p);     // Fractional part for interpolation
	
    // Noise contributions from (x=0, y=0), z=0 and z=1
    float perm00 = rnm(pi.xy).a ;
    float3  grad000 = rnm(float2(perm00, pi.z)).rgb * 4.0 - 1.0;
    float n000 = dot(grad000, pf);
    float3  grad001 = rnm(float2(perm00, pi.z + permTexUnit)).rgb * 4.0 - 1.0;
    float n001 = dot(grad001, pf - float3(0.0, 0.0, 1.0));

    // Noise contributions from (x=0, y=1), z=0 and z=1
    float perm01 = rnm(pi.xy + float2(0.0, permTexUnit)).a ;
    float3  grad010 = rnm(float2(perm01, pi.z)).rgb * 4.0 - 1.0;
    float n010 = dot(grad010, pf - float3(0.0, 1.0, 0.0));
    float3  grad011 = rnm(float2(perm01, pi.z + permTexUnit)).rgb * 4.0 - 1.0;
    float n011 = dot(grad011, pf - float3(0.0, 1.0, 1.0));

    // Noise contributions from (x=1, y=0), z=0 and z=1
    float perm10 = rnm(pi.xy + float2(permTexUnit, 0.0)).a ;
    float3  grad100 = rnm(float2(perm10, pi.z)).rgb * 4.0 - 1.0;
    float n100 = dot(grad100, pf - float3(1.0, 0.0, 0.0));
    float3  grad101 = rnm(float2(perm10, pi.z + permTexUnit)).rgb * 4.0 - 1.0;
    float n101 = dot(grad101, pf - float3(1.0, 0.0, 1.0));

    // Noise contributions from (x=1, y=1), z=0 and z=1
    float perm11 = rnm(pi.xy + float2(permTexUnit, permTexUnit)).a ;
    float3  grad110 = rnm(float2(perm11, pi.z)).rgb * 4.0 - 1.0;
    float n110 = dot(grad110, pf - float3(1.0, 1.0, 0.0));
    float3  grad111 = rnm(float2(perm11, pi.z + permTexUnit)).rgb * 4.0 - 1.0;
    float n111 = dot(grad111, pf - float3(1.0, 1.0, 1.0));

    // Blend contributions along x
    float4 n_x = lerp(float4(n000, n001, n010, n011), float4(n100, n101, n110, n111), fade(pf.x));

    // Blend contributions along y
    float2 n_xy = lerp(n_x.xy, n_x.zw, fade(pf.y));

    // Blend contributions along z
    float n_xyz = lerp(n_xy.x, n_xy.y, fade(pf.z));

    // We're done, return the final noise value.
    return n_xyz;
    }

    //2d coordinate orientation thing
    float2 coordRot(in float2 tc, in float angle)
    {
    #define aspectr ScreenSize.z
    float rotX = ((tc.x*2.0-1.0)*aspectr*cos(angle)) - ((tc.y*2.0-1.0)*sin(angle));
    float rotY = ((tc.y*2.0-1.0)*cos(angle)) + ((tc.x*2.0-1.0)*aspectr*sin(angle));
    rotX = ((rotX/aspectr)*0.5+0.5);
    rotY = rotY*0.5+0.5;
    return float2(rotX,rotY);
    #undef aspectr
    }

    float3 GrainPass(float2 position, float3 col)
    {

    float width  = ScreenSize.x; // Would #define be better?
    float height = ScreenSize.x/ScreenSize.z;
	float3 grey = 0.5;
	
    float3 rotOffset = float3(1.425,3.892,5.835); //rotation offset values
    float2 rotCoordsR = coordRot(position, Timer.x*16777216 + rotOffset.x);
    float2 rot = rotCoordsR*float2(width/grainsize,height/grainsize);
    float pNoise = pnoise3D(float3(rot.x,rot.y,0.0));
    float3 noise = float3(pNoise, pNoise, pNoise);


    float2 rotCoordsG = coordRot(position, Timer.x*16777216 + rotOffset.y);
    float2 rotCoordsB = coordRot(position, Timer.x*16777216 + rotOffset.z);
    noise.g = lerp(noise.r,pnoise3D(float3(rotCoordsG*float2(width/grainsize,height/grainsize),1.0)),coloramount);
    noise.b = lerp(noise.r,pnoise3D(float3(rotCoordsB*float2(width/grainsize,height/grainsize),2.0)),coloramount);
    

    //noisiness response curve based on scene luminance
    float3 lumcoeff = float3(0.299,0.587,0.114);
    float luminance = lerp(0.0,dot(col, lumcoeff),lumamount);
    float lum = smoothstep(0.2,0.0,luminance);
    lum += luminance;

    float2 thepow = pow(lum, 4.0);

    noise = lerp(noise,float3(0.0, 0.0, 0.0),pow(lum,4.0));
    
	grey += noise*(fGrainIntensity/5);
	col = BlendSoftLightf(col, grey);

    return float4(col,1.0);

    //return noise;
}

/////////////////////////////////////////////////////////
//               	   LETTERBOX                 	   //
/////////////////////////////////////////////////////////

float4 Letterbox(float4 colorInput, float2 tex)
{
    if(border_ratio > 0.0f) {
		float3 border_color_float = border_colorBlack / 255.0;

		float2 border_width_variable = border_width;

		if (border_width.x == -border_width.y)
			if (screen_ratio < border_ratio)
				border_width_variable = float2(0.0, (screen_size.y - (screen_size.x / border_ratio)) * 0.5);
		else
			border_width_variable = float2((screen_size.x - (screen_size.y * border_ratio)) * 0.5, 0.0);

		float2 border = (pixel * border_width_variable);

		float2 within_border = saturate((-tex * tex + tex) - (-border * border + border));

		colorInput.rgb = all(within_border) ?  colorInput.rgb : border_color_float ;
	}
	
	if(border_ratio < 0.0f) {
		float border_ratioINV= border_ratio;
		border_ratioINV*=-1;
		
		float3 border_color_float = border_colorWhite / 255.0;

		float2 border_width_variable = border_width;

		if (border_width.x == -border_width.y)
			if (screen_ratio < border_ratioINV)
				border_width_variable = float2(0.0, (screen_size.y - (screen_size.x / border_ratioINV)) * 0.5);
		else
			border_width_variable = float2((screen_size.x - (screen_size.y * border_ratioINV)) * 0.5, 0.0);

		float2 border = (pixel * border_width_variable);

		float2 within_border = saturate((-tex * tex + tex) - (-border * border + border));

		colorInput.rgb = all(within_border) ?  colorInput.rgb : border_color_float ;
	}
	
return colorInput;
}

/////////////////////////////////////////////////////////
//               	   VIGNETTE                 	   //
/////////////////////////////////////////////////////////

float4 Vignette(float4 inColor, float2 inTexCoords)
{
    float3 origcolor = inColor;
	float nVignetteRadius;
	
	if (EVignetteRadius == 0.0f) nVignetteRadius = screen_ratio*0.33;
	else nVignetteRadius = EVignetteRadius;
	
      float2 uv      = (inTexCoords.xy - 0.5f) * nVignetteRadius;
      float vignette = saturate(dot(uv.xy, uv.xy));
      vignette       = pow(vignette, EVignetteCurve);
	  
      inColor.xyz    = lerp(origcolor.xyz, EVignetteColor, vignette * EVignetteAmount);

	return inColor;

}

/////////////////////////////////////////////////////////
//               	   COLORGRADER                 	   //
/////////////////////////////////////////////////////////
float3 Colorgrader(float3 inColor)
{
    float4 ColorLUTCG = float4( inColor * ( TuningColorLUTTileAmountY - 1.0f ), 1.0f);
    ColorLUTCG.xy = (ColorLUTCG.xy + 0.5f) * TuningColorLUTNorm;
    ColorLUTCG.x += trunc(ColorLUTCG.z) * TuningColorLUTNorm.y;
    ColorLUTCG.w  = ColorLUTCG.x + TuningColorLUTNorm.y;
    ColorLUTCG.y  = (ColorLUTCG.y + lerp(iCG, iCG_i, EInteriorFactor)) / float(TuningColorLUTCount) ;
    
    ColorLUTCG = lerp(
      TextureColorGrades.SampleLevel(SamplerLut, ColorLUTCG.xy, 0),
      TextureColorGrades.SampleLevel(SamplerLut, ColorLUTCG.wy, 0),frac(ColorLUTCG.z));
	
/// LUT TONEMAPING
	if (bCCLUT) {
	float Gamma   = lerp(lerp(GammaN, GammaD, ENightDayFactor), GammaI, EInteriorFactor);

	ColorLUTCG.xyz *= pow(2.0f, Exposure);
	ColorLUTCG.xyz = pow(ColorLUTCG.xyz, Gamma);
	
	float3 lumCoeff = float3(0.2126, 0.7152, 0.0722);
	float lum = dot(lumCoeff, ColorLUTCG.xyz);
	
	float3 blend = lum.rrr;
	
	float L = saturate( 10.0 * (lum - 0.45) );
  	
	float3 result1 = 2.0f * ColorLUTCG.xyz * blend;
	float3 result2 = 1.0f - 2.0f * (1.0f - blend) * (1.0f - ColorLUTCG.xyz);
	
	float3 newColor = lerp(result1, result2, L);
	float3 A2 = 0.0f * ColorLUTCG.xyz; 
	float3 mixRGB = A2 * newColor;
	
	ColorLUTCG.xyz += ((1.0f - A2) * mixRGB);

	float3 middlegray = dot(ColorLUTCG.xyz,(1.0/3.0));
	
	float3 diffcolor = ColorLUTCG.xyz - middlegray;
	if (Saturation>=1.0) 	ColorLUTCG.xyz = (ColorLUTCG.xyz + diffcolor * Saturation)/(1+(diffcolor*Saturation));
	if (Saturation<1.0) 	ColorLUTCG.xyz = (ColorLUTCG.xyz + diffcolor * Saturation);
	}
	
//LUT OUTPUT
	
	float lutAmount= lerp(lerp(CGAmount_N*0.01, CGAmount*0.01, ENightDayFactor), CGAmount_i*0.01, EInteriorFactor);
	
	if(iCGBlend == 0)	inColor.xyz=	lerp(inColor.xyz, ColorLUTCG.xyz, lutAmount);
	if(iCGBlend == 1) 	inColor.xyz = 	lerp(inColor.xyz,(BlendHue(inColor.xyz,ColorLUTCG.xyz)), lutAmount);
	if(iCGBlend == 2) 	inColor.xyz = 	lerp(inColor.xyz,(BlendColor(inColor.xyz,ColorLUTCG.xyz)), lutAmount);
	if(iCGBlend == 3) 	inColor.xyz = 	lerp(inColor.xyz,(BlendLuminosity(inColor.xyz,ColorLUTCG.xyz)), lutAmount);
	if(iCGBlend == 4) 	inColor.xyz = 	lerp(inColor.xyz,(max(ColorLUTCG.xyz, inColor.xyz)), lutAmount);
	if(iCGBlend == 5) 	inColor.xyz = 	lerp(inColor.xyz,(min(inColor.xyz + ColorLUTCG.xyz, 1.0)), lutAmount);
	if(iCGBlend == 6) 	inColor.xyz = 	lerp(inColor.xyz,(1-(1-inColor.xyz)*(1-ColorLUTCG.xyz)), lutAmount);
	if(iCGBlend == 7) 	inColor.xyz = 	lerp(inColor.xyz,(max(0.0f,max(inColor.xyz,lerp(inColor.xyz,(1.0f - (1.0f - saturate(ColorLUTCG.xyz)) *(1.0f - saturate(ColorLUTCG.xyz * 1.0))),1.0)))), lutAmount);
	if(iCGBlend == 8) 	inColor.xyz = 	lerp(inColor.xyz,(min(inColor.xyz, ColorLUTCG.xyz)), lerp(lerp(CGAmount_N*0.01, CGAmount*0.01,ENightDayFactor), CGAmount_i*0.01 ,EInteriorFactor));
	if(iCGBlend == 9) 	inColor.xyz = 	lerp(inColor.xyz,(inColor.xyz * ColorLUTCG.xyz), lutAmount);
	if(iCGBlend == 10) 	inColor.xyz = 	lerp(inColor.xyz,(((ColorLUTCG.xyz < 0.5) ? (2.0 * inColor.xyz * ColorLUTCG.xyz + inColor.xyz * inColor.xyz * (1.0 - 2.0 * ColorLUTCG.xyz)) : (sqrt(inColor.xyz) * (2.0 * ColorLUTCG.xyz - 1.0) + 2.0 * inColor.xyz * (1.0 - ColorLUTCG.xyz)))) , lutAmount);
	
	return inColor.xyz;
	
}


float3 Finish(float3 inColor)
{
    float4 ColorFinish = float4( inColor * ( TuningColorLUTTileAmountY - 1.0f ), 1.0f);
    ColorFinish.xy = (ColorFinish.xy + 0.5f) * TuningColorLUTNorm;
    ColorFinish.x += trunc(ColorFinish.z) * TuningColorLUTNorm.y;
    ColorFinish.w  = ColorFinish.x + TuningColorLUTNorm.y;
    ColorFinish.y  = (ColorFinish.y + iFinish) / float(TuningColorLUTFCount) ;
    
    ColorFinish = lerp(
      TextureFinish.SampleLevel(SamplerLut, ColorFinish.xy, 0),
      TextureFinish.SampleLevel(SamplerLut, ColorFinish.wy, 0),frac(ColorFinish.z));


	inColor.xyz=	lerp(inColor.xyz, ColorFinish.xyz, FinishAmount);
	
	return inColor.xyz;
	
}


//21 tap reduced to 6 weights ( 41 pixels wide )
static const float sampleOffsets_3[6]   = { 0.0,         1.452313744966,      3.390210239952,      5.331472958797,      7.277552900121,      9.229394260785   };
static const float sampleWeights_3[6]   = { 0.142479385858,   0.244115579374,      0.131636577371,      0.043283482080,      0.008668409765,      0.001056258481   };

float3 screen(float3 a, float3 b)
{
   return (1.0f - (1.0f - a) * (1.0f - b));
}

/////////////////////////////////////////////////////////
//---------------------OVERLAY-------------------------//
// OtisInf:          Author of Overlay for Reshade     //
// roxahris:         port to ENB                       //
/////////////////////////////////////////////////////////

#define PO_PhiScale 0
#define PO_GridScale 0

///GRID
float3 GridPass(float3 Color, float2 txCoord) {

    if (!BGrid) {
        return Color;
    }

    // Initial variable setup
    float4 GridColour;

    const float phiValue = ((1.0 + sqrt(5.0))/2.0);
    float aspectRatio = ScreenSize.z;
	
    float screenWidth = ScreenSize.x;
    float screenHeight = ScreenSize.x * ScreenSize.w; // Simple solution, but not the best

    float idealWidth =  screenHeight * phiValue;
    float idealHeight = screenWidth / phiValue;

    float4 sourceCoordFactor = float4(1.0, 1.0, 1.0, 1.0);

#if PO_GridScale
    if(aspectRatio < phiValue)
    {
        // display spirals at full width, but resize across height
        sourceCoordFactor = float4(1.0, screenHeight/idealHeight, 1.0, idealHeight/screenHeight);
    }
    else
    {
        // display spirals at full height, but resize across width
        sourceCoordFactor = float4(screenWidth/idealWidth, 1.0, idealWidth/screenWidth, 1.0);
    }
#endif

    float2 coords = float2( (txCoord.x * sourceCoordFactor.x) - ((1.0-sourceCoordFactor.z)/2.0),
                            (txCoord.y * sourceCoordFactor.y) - ((1.0-sourceCoordFactor.w)/2.0));


        GridColour = TextureGrid.Sample(Sampler1, coords);


    return lerp(Color, GridColour.xyz*float3(1.0, 1.0, 1.0), GridColour.w*0.75);
}

//CGLIST
float3 CGListPass(float3 Color, float2 txCoord) {
    // Return nothing if not active
    if (!bCGList) {
        return Color;
    }

    // Initial variable setup
    float4 CGListColour;

    const float phiValue = ((1.0 + sqrt(5.0))/2.0);
    float aspectRatio = ScreenSize.z;

    float screenWidth = ScreenSize.x;
    float screenHeight = ScreenSize.x * ScreenSize.w; // Simple solution, but not the best

    float idealWidth =  screenHeight * phiValue;
    float idealHeight = screenWidth / phiValue;

    float4 sourceCoordFactor = float4(1.0, 1.0, 1.0, 1.0);

#if PO_PhiScale
    if(aspectRatio < phiValue)
    {
        // display spirals at full width, but resize across height
        sourceCoordFactor = float4(1.0, screenHeight/idealHeight, 1.0, idealHeight/screenHeight);
    }
    else
    {
        // display spirals at full height, but resize across width
        sourceCoordFactor = float4(screenWidth/idealWidth, 1.0, idealWidth/screenWidth, 1.0);
    }
#endif

    float2 coords = float2( (txCoord.x * sourceCoordFactor.x) - ((1.0-sourceCoordFactor.z)/2.0),
                            (txCoord.y * sourceCoordFactor.y) - ((1.0-sourceCoordFactor.w)/2.0));


    CGListColour = TextureCGList.Sample(Sampler1, coords);
    
    return lerp(Color, CGListColour.xyz*float3(1.0, 1.0, 1.0), CGListColour.w*1.0);
}

/// HELPER
float3 TutorialPass(float3 Color, float2 txCoord) {
    // Return nothing if not active
    if (iHelp==0) {
        return Color;
    }

    // Initial variable setup
    float4 HelperColour;

    const float phiValue = ((1.0 + sqrt(5.0))/2.0);
    float aspectRatio = ScreenSize.z;

    float screenWidth = ScreenSize.x;
    float screenHeight = ScreenSize.x * ScreenSize.w; // Simple solution, but not the best

    float idealWidth =  screenHeight * phiValue;
    float idealHeight = screenWidth / phiValue;

    float4 sourceCoordFactor = float4(1.0, 1.0, 1.0, 1.0);

#if PO_PhiScale
    if(aspectRatio < phiValue)
    {
        // display spirals at full width, but resize across height
        sourceCoordFactor = float4(1.0, screenHeight/idealHeight, 1.0, idealHeight/screenHeight);
    }
    else
    {
        // display spirals at full height, but resize across width
        sourceCoordFactor = float4(screenWidth/idealWidth, 1.0, idealWidth/screenWidth, 1.0);
    }
#endif

    float2 coords = float2( (txCoord.x * sourceCoordFactor.x) - ((1.0-sourceCoordFactor.z)/2.0),
                            (txCoord.y * sourceCoordFactor.y) - ((1.0-sourceCoordFactor.w)/2.0));

  
    if (iHelp==1) HelperColour = TextureHelper1.Sample(Sampler1, coords);
    if (iHelp==2) HelperColour = TextureHelper2.Sample(Sampler1, coords);
	if (iHelp==3) HelperColour = TextureHelper3.Sample(Sampler1, coords);
	if (iHelp==4) HelperColour = TextureHelper4.Sample(Sampler1, coords);
	if (iHelp==5) HelperColour = TextureHelper5.Sample(Sampler1, coords);
	
    return lerp(Color, HelperColour.xyz*float3(1.0, 1.0, 1.0), HelperColour.w*1.0);
}

/// SCRATCHES
float3 DirtPass(float3 Color, float2 txCoord) {
    // Return nothing if not active
    if (iDirt==0) {
        return Color;
    }

    // Initial variable setup
    float4 DirtColour;
	
    const float phiValue = ((1.0 + sqrt(5.0))/2.0);
    float aspectRatio = ScreenSize.z;

    float screenWidth = ScreenSize.x;
    float screenHeight = ScreenSize.x * ScreenSize.w; // Simple solution, but not the best

    float idealWidth =  screenHeight * phiValue;
    float idealHeight = screenWidth / phiValue;

    float4 sourceCoordFactor = float4(1.0, 1.0, 1.0, 1.0);

#if PO_PhiScale
    if(aspectRatio < phiValue)
    {
        // display spirals at full width, but resize across height
        sourceCoordFactor = float4(1.0, screenHeight/idealHeight, 1.0, idealHeight/screenHeight);
    }
    else
    {
        // display spirals at full height, but resize across width
        sourceCoordFactor = float4(screenWidth/idealWidth, 1.0, idealWidth/screenWidth, 1.0);
    }
#endif

    float2 coords = float2( (txCoord.x * sourceCoordFactor.x) - ((1.0-sourceCoordFactor.z)/2.0),
                            (txCoord.y * sourceCoordFactor.y) - ((1.0-sourceCoordFactor.w)/2.0));

  
	if (iDirt==1) {
        DirtColour = TextureDirt01.Sample(Sampler1, coords);
		DirtColour = DirtColour*0.5*fDirtIntensity;
		Color.xyz = 1-(1-Color.xyz)*(1-DirtColour.xyz);
    }
	if (iDirt==2) {
        DirtColour = TextureDirt02.Sample(Sampler1, coords);
		DirtColour = DirtColour*0.5*fDirtIntensity;
		Color.xyz = 1-(1-Color.xyz)*(1-DirtColour.xyz);
    }
	if (iDirt==3) {
        DirtColour = TextureDirt03.Sample(Sampler1, coords);
		DirtColour = DirtColour*0.5*fDirtIntensity;
		Color.xyz = 1-(1-Color.xyz)*(1-DirtColour.xyz);
    }
	if (iDirt==4) {
        DirtColour = TextureDirt04.Sample(Sampler1, coords);
		DirtColour = DirtColour*0.45*fDirtIntensity;
		Color.xyz = max(0.0f,max(Color.xyz,lerp(Color.xyz,(1.0f - (1.0f - saturate(DirtColour.xyz)) *(1.0f - saturate(DirtColour.xyz * 1.0))),1.0)));
    }
	if (iDirt==5) {
        DirtColour = TextureDirt05.Sample(Sampler1, coords);
		DirtColour = DirtColour*0.45*fDirtIntensity;
		Color.xyz = max(0.0f,max(Color.xyz,lerp(Color.xyz,(1.0f - (1.0f - saturate(DirtColour.xyz)) *(1.0f - saturate(DirtColour.xyz * 1.0))),1.0)));
    }
	if (iDirt==6) {
        DirtColour = TextureDirt06.Sample(Sampler1, coords);
		DirtColour = DirtColour*0.75*fDirtIntensity;
		Color.xyz = 1-(1-Color.xyz)*(1-DirtColour.xyz);
    }
	if (iDirt==7) {
        DirtColour = TextureDirt07.Sample(Sampler1, coords);
		DirtColour = DirtColour*0.5*fDirtIntensity;
		Color.xyz = 1-(1-Color.xyz)*(1-DirtColour.xyz);
    }
	if (iDirt==8) {
        DirtColour = TextureDirt08.Sample(Sampler1, coords);
		Color.xyz= lerp(Color, DirtColour.xyz*float3(1.0, 1.0, 1.0), DirtColour.w*0.9*fDirtIntensity);
		return Color;
    }
	if (iDirt==9) {
        DirtColour = TextureDirt09.Sample(Sampler1, coords);
		Color.xyz= lerp(Color, DirtColour.xyz*float3(1.0, 1.0, 1.0), DirtColour.w*0.95*fDirtIntensity);

    }
	if (iDirt==10) {
        DirtColour = TextureDirt10.Sample(Sampler1, coords);
		Color.xyz= lerp(Color, DirtColour.xyz*float3(1.0, 1.0, 1.0), DirtColour.w*0.85*fDirtIntensity);
		
    }
  return Color;
}

/// FRAME
float3 FramePass(float3 Color, float2 txCoord) {
    // Return nothing if not active
    if (iFrame==0) {
        return Color;
    }

    // Initial variable setup
    float4 FrameColour;
	
    const float phiValue = ((1.0 + sqrt(5.0))/2.0);
    float aspectRatio = ScreenSize.z;

    float screenWidth = ScreenSize.x;
    float screenHeight = ScreenSize.x * ScreenSize.w; // Simple solution, but not the best

    float idealWidth =  screenHeight * phiValue;
    float idealHeight = screenWidth / phiValue;

    float4 sourceCoordFactor = float4(1.0, 1.0, 1.0, 1.0);

#if PO_PhiScale
    if(aspectRatio < phiValue)
    {
        // display spirals at full width, but resize across height
        sourceCoordFactor = float4(1.0, screenHeight/idealHeight, 1.0, idealHeight/screenHeight);
    }
    else
    {
        // display spirals at full height, but resize across width
        sourceCoordFactor = float4(screenWidth/idealWidth, 1.0, idealWidth/screenWidth, 1.0);
    }
#endif

    float2 coords = float2( (txCoord.x * sourceCoordFactor.x) - ((1.0-sourceCoordFactor.z)/2.0),
                            (txCoord.y * sourceCoordFactor.y) - ((1.0-sourceCoordFactor.w)/2.0));

  
    if (iFrame==1)	FrameColour = TextureFrame01.Sample(Sampler1, coords);
    if (iFrame==2)	FrameColour = TextureFrame02.Sample(Sampler1, coords);
	if (iFrame==3)	FrameColour = TextureFrame03.Sample(Sampler1, coords);
	if (iFrame==4)	FrameColour = TextureFrame04.Sample(Sampler1, coords);
	if (iFrame==5)	FrameColour = TextureFrame05.Sample(Sampler1, coords);
	if (iFrame==6)	FrameColour = TextureFrame06.Sample(Sampler1, coords);
	if (iFrame==7)	FrameColour = TextureFrame07.Sample(Sampler1, coords);
	if (iFrame==8)	FrameColour = TextureFrame08.Sample(Sampler1, coords);
	if (iFrame==9)	FrameColour = TextureFrame09.Sample(Sampler1, coords);
	if (iFrame==10)	FrameColour = TextureFrame10.Sample(Sampler1, coords);
	if (iFrame==11)	FrameColour = TextureFrame11.Sample(Sampler1, coords);
	if (iFrame==12)	FrameColour = TextureFrame12.Sample(Sampler1, coords);
	if (iFrame==13)	FrameColour = TextureFrame13.Sample(Sampler1, coords);
	if (iFrame==14)	FrameColour = TextureFrame14.Sample(Sampler1, coords);
	if (iFrame==15)	FrameColour = TextureFrame15.Sample(Sampler1, coords);
	if (iFrame==16)	FrameColour = TextureFrame16.Sample(Sampler1, coords);
	if (iFrame==17)	FrameColour = TextureFrame17.Sample(Sampler1, coords);
	if (iFrame==18)	FrameColour = TextureFrame18.Sample(Sampler1, coords);
	if (iFrame==19)	FrameColour = TextureFrame19.Sample(Sampler1, coords);
	if (iFrame==20)	FrameColour = TextureFrame20.Sample(Sampler1, coords);
	if (iFrame==21)	FrameColour = TextureFrame21.Sample(Sampler1, coords);
	if (iFrame==22)	FrameColour = TextureFrame22.Sample(Sampler1, coords);
	if (iFrame==23)	FrameColour = TextureFrame23.Sample(Sampler1, coords);
	if (iFrame==24)	FrameColour = TextureFrame24.Sample(Sampler1, coords);
	if (iFrame==25)	FrameColour = TextureFrame25.Sample(Sampler1, coords);
	
    return lerp(Color, FrameColour.xyz*float3(1.0, 1.0, 1.0), FrameColour.w*1.0);
}

/////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////
//               	  PIXEL SHADER                     //
/////////////////////////////////////////////////////////

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


float4 PS_CloseSharpen(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{

	return float4(CloseSharp(IN.txcoord0.xy, fSharpening, Sharpradius), 1.0);
}

/// VHS
VHS_struct VS_VHSnPost(VS_INPUT_POST IN)
{
    return VS_Shared(float4(IN.pos, 1.0), IN.txcoord);
}

float4	PS_VHSnPost(VHS_struct IN, uniform int ID) : SV_Target
{
    float4 res;
	float4 color = TextureColor.Sample(Sampler0, IN.coord);
   
   switch(ID){
		case 1:  res = lerp(color,float4(VHS_NTSC_decoder(TextureColor, IN.NTSCuv), 1.0),fVHSIntensity); break;
        case 2:  if(fVHSIntensity>0.0f) res = lerp(color,VHS_VHS(TextureColor, IN.coord),fVHSIntensity); break;
        case 3:  res = float4(VHS_DirtyCRT(TextureColor, IN.coord), 1.0); break;
        
default: res = TextureColor.Sample(Sampler0, IN.coord); break;
    }
   
	return res;
}

/// POST PROCESS
float4	PS_PostPro(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
    float4 res = float4(CleanSharp(IN.txcoord0.xy, sharp_strength, offset_bias), 1.0);

								res = LevelsPass(saturate(res));
	if (!bCCLUT)				res = TonemapPass(saturate(res));
	
	if (iDirt>0) 				res.rgb = DirtPass(res.rgb, IN.txcoord0.xy);
	if (EVignetteAmount > 0.0f) res = min(Vignette(res,IN.txcoord0.xy), res);
	if (Filmic_SFX) {			res = FilmPass(saturate(res));
								res = VibrancePass(saturate(res));
								res = SepiaPass(saturate(res));}

	if (fGrainIntensity > 0.0f) res.xyz = GrainPass(IN.txcoord0.xy, res.xyz);							
	if(fVHSnoise>0.0f)  	res = lerp(res, VHS_TapeNoise(res, IN.txcoord0), fVHSnoise) ;
    if(fVHSnoise>0.0f) 		res = lerp(res, VHS_LayerNoise(res, IN.txcoord0), fVHSnoise);							
	
	if (iCG!=0)					res.rgb = Colorgrader(saturate(res.rgb));
														
	if (!bCCLUT)				res = SaturationPass(saturate(res));
	
	// DROP BLUE
	if (dropBlueAmount >= 0.1f) {
	float4 LUTColor = float4((res.rg*float(TuningColorLUTTileAmountY-1)+0.5f)*TuningColorLUTNorm,res.b*float(TuningColorLUTTileAmountY-1),1);
	LUTColor.x += trunc(LUTColor.z)*TuningColorLUTNorm.y;
	LUTColor = lerp(
		TextureDropBlue.SampleLevel(SamplerLut2, LUTColor.xy, 0),
		TextureDropBlue.SampleLevel(SamplerLut2, float2(LUTColor.x+TuningColorLUTNorm.y,LUTColor.y), 0),frac(LUTColor.z));
		
		res.xyz = saturate(lerp(res.xyz, LUTColor.xyz, dropBlueAmount));
	}							
	
	res.rgb = Finish(saturate(res.rgb));
	
	// LIMITED RANGE
	if (iColorRange == 1) {
		float4 LimitedLUTColor = float4((res.rg*float(TuningColorLUTTileAmountY-1)+0.5f)*TuningColorLUTNorm,res.b*float(TuningColorLUTTileAmountY-1),1);
		LimitedLUTColor.x += trunc(LimitedLUTColor.z)*TuningColorLUTNorm.y;
		LimitedLUTColor = lerp(
			TextureLimitedRange.SampleLevel(SamplerLut2, LimitedLUTColor.xy, 0),
			TextureLimitedRange.SampleLevel(SamplerLut2, float2(LimitedLUTColor.x+TuningColorLUTNorm.y,LimitedLUTColor.y), 0),frac(LimitedLUTColor.z));
			
	res.xyz = saturate(LimitedLUTColor.xyz);
	}					

	res.w = 1.0;
	return res;
}

/// GUI
float4	PS_GUI(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
    float4 res = TextureColor.Sample(Sampler0, IN.txcoord0.xy);
						
	if (BGrid) 					res.rgb = GridPass(res.rgb, IN.txcoord0.xy);
	if (iFrame==0) 				res = Letterbox(res, IN.txcoord0.xy);
   
	if (iFrame>0) 				res.rgb = FramePass(res.rgb, IN.txcoord0.xy);
	
	if (bCGList) 				res.rgb = CGListPass(res.rgb, IN.txcoord0.xy);
	if (iHelp>0) 				res.rgb = TutorialPass(res.rgb, IN.txcoord0.xy);
	
	res.xyz += triDither(res.xyz, IN.txcoord0.xy, Timer.x);
	
	res.w = 1.0;
	return res;
}
/////////////////////////////////////////////////////////
//               	    TECHNIQUES                     //
/////////////////////////////////////////////////////////

technique11 PRC <string UIName="PRT.X: POST-PROCESSING";> 
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_CloseSharpen()));
  }
}

technique11 PRC1
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_PostPro()));
  }
}

technique11 PRC2
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_VHSnPost()));
    SetPixelShader(CompileShader(ps_5_0, PS_VHSnPost(VHS_Index)));
  }
}

technique11 PRC3
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_GUI()));
  }
}



