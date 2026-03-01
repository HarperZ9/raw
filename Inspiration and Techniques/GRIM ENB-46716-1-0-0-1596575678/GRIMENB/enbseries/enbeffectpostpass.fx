//////////////////////////////////////////////////////////
//                ENBSeries effect file                	//
//         visit http://enbdev.com for updates         	//
//       Copyright (c) 2007-2019 Boris Vorontsov       	//
//----------------------ENB PRESET----------------------//
//										  				//
//	  .g8"""bgd `7MM"""Mq.  `7MMF'`7MMM.     ,MMF'		//
//	.dP'     `M   MM   `MM.   MM    MMMb    dPMM  		//
//	dM'       `   MM   ,M9    MM    M YM   ,M MM  		//
//	MM            MMmmdM9     MM    M  Mb  M' MM  		//
//	MM.    `7MMF' MM  YM.     MM    M  YM.P'  MM  		//
//	`Mb.     MM   MM   `Mb.   MM    M  `YM'   MM  		//
//	  `"bmmmdPY .JMML. .JMM..JMML..JML. `'  .JMML.		//
//												  		//
//												   		//
//-----------------------CREDITS------------------------//
// Boris: For ENBSeries and his knowledge and codes    	//
// JawZ:  Author and developer of the MSL code         	//
// CeeJay.dk-Crosire-GemFx-Marty McFly-Lucifer Hawk:   	//
//        Original Authors of sweeFX & Reshade code    	//
// Roxahris: ENB Port of MartinGrain.      			   	//
// L00 :  Shader Setup, Presets and Settings,           //
//        Port and Modification of Shaders     		    //
//     Please do not redistribute without credits      	//
////////////////////////////////////////////////////////*/
//            			GRIM ENB           			   	//

float	Empty0 				<string UIName=" "										;string UIWidget="spinner"	;float UIMin=0.0	;float UIMax=0.0	;float UIStep=0.0;> = {0.0};
float	Colormix 			<string UIName="Colormix : Grim <%> Technique Preset ^"	;string UIWidget="spinner"	;float UIMin=0.0	;float UIMax=100.0	;float UIStep=5.0;> = {50.0};
int		iCGBlend 			<string UIName="Colormix: 0:Normal 1:Luma 2:Chroma"		; string UIWidget="spinner"	;int UIMin=0		;int UIMax=2;> = {0};   
float 	fSaturation 		<string UIName="Color intensity"						;string UIWidget="Spinner"	;float UIMin=0.0f	;float UIMax=2.0	;float UIStep=0.1f;> = {1.0f};

float	Empty1 				<string UIName="  "										;string UIWidget="spinner"	;float UIMin=0.0	;float UIMax=0.0	;float UIStep=0.0;> = {0.0};
float	VQ 					<string UIName="Visual Degradation %"						;string UIWidget="spinner"	;float UIMin=0.0	;float UIMax=100.0	;float UIStep=5.0;> = {50.0};
float 	fVHSIntensity 		<string UIName="Found Footage Distortion"					;string UIWidget="Spinner"	;float UIMin=0.0f	;float UIMax=1.0f	;float UIStep=0.1f;> = {0.1f};

float	Empty2 				<string UIName="   "									;string UIWidget="spinner"	;float UIMin=0.0	;float UIMax=0.0	;float UIStep=0.0;> = {0.0};
float 	fGrainIntensity0 	<string UIName="Grain"									;string UIWidget="Spinner"	;float UIMin=0.0	;float UIMax=50.0	;float UIStep=1.0;> = {0.0};
float 	EVignetteAmount  	<string UIName="Vignette"								;string UIWidget="Spinner"	;float UIMin=0.0	;float UIMax=100.0	;float UIStep=1.0;> = {5.0};
float 	border_ratio  		<string UIName="Letterbox - Black Bars"					;string UIWidget="Spinner"	;float UIMin=0.0f	;float UIMax=10.0	;float UIStep=0.05;> = {0.0f};


//VISUAL DEGRADATION
#define EBlurAmount					lerp(0.15,0.9,VQ*0.01)
#define EBlurRange					lerp(0.1,1.1,VQ*0.01)
#define ESharpAmount0				lerp(0.2,1.9,VQ*0.01)
#define ESharpRange0				lerp(0.1,2.0,VQ*0.01)
#define ESharpAmount 				(ESharpAmount0+fVHSIntensity*2)
#define ESharpRange 				(ESharpRange0+fVHSIntensity)
	//LENS CA
#define LCAStrength 				lerp((0.1+(fVHSIntensity*0.1)),(0.9+(fVHSIntensity*0.1)),VQ*0.01)

//LUT
#define TuningColorLUTTileAmountX 	4096 
#define TuningColorLUTTileAmountY 	64 
#define TuningColorLUTNorm        	float2(1.0/float(TuningColorLUTTileAmountX),1.0/float(TuningColorLUTTileAmountY))
#define TuningColorLUTCount       	11

//LEVELS
#define Levels_white_point 			(265+(fVHSIntensity*50))
#define Levels_black_point 			-3

//TONEMAPPING
#define GammaD 						(1.05+fVHSIntensity*0.1) //1.05
#define GammaN 						(1.05+fVHSIntensity*0.1)
#define GammaI 						(1.1+fVHSIntensity*0.1) //1.1
#define Exposure 					(0.05+(fVHSIntensity*0.1))
#define Defog 						(-0.005-(fVHSIntensity*0.01))
#define Saturation 					(-0.05+(fVHSIntensity*0.1))
#define Bleach 						0.020
#define FogColor 					float3(0.50,1.00,2.55)

// SHARPENING
#define fSharpening 				(1.0+VQ*0.0033)
#define SMOOTH_EDGES          		1
#define SHOW_EDGES			   		0
#define LUMA_SHARPEN 				true	
#define fBlurSigma 					1.0	
#define fFarDepth 					55
#define fThreshold 					0		
#define fLimiter 					0.35		
#define fSmooth 					1.0

//Luma
#define SharpAmount 	1.2
#define SharpRadius 	2.0
#define ENABLE_DEPTH 	1
#define fSharpFDepth 	500
#define ENABLE_EDGES 	1
#define LineThickness 	SharpRadius
#define DepthLineMult 	5.0
#define DepthLinePower 	1.0
#define show_sharpen 	0
#define sharp_clamp 	0.035
#define fSharpAmount ((lerp(lerp(SharpAmount*1.2,SharpAmount,ENightDayFactor),SharpAmount*1.15,EInteriorFactor))+VQ*0.001)

//VIGNETTE
#define EVignetteCurve 				1.5	
#define	EVignetteRadiusBorder 		border_ratio*0.25
#define EVignetteRadius				screen_ratio*0.25
#define EVignetteColor 				(0.25,0.25,0.0)		

//GRAIN
#define Use_GrainColour 			1
#define fGrainIntensity 			(fGrainIntensity0+fVHSIntensity*0.025)
#define coloramount 				(0.2+fVHSIntensity*0.5)
#define grainsize 					(1.75+fVHSIntensity*0.1)
#define lumamount 					0.25

///VHS

#define tNoise_Color 				lerp(0.6,2.0,(fGrainIntensity0*0.01+fVHSIntensity*0.5))
#define lNoise_Color 				lerp(0.3,0.6,(fGrainIntensity0*0.01+fVHSIntensity*0.5))


//LETTERBOX
#define border_width 				float2(0,0)
#define border_colorBlack 			float3(0, 0, 0)
#define border_colorWhite 			float3(250, 250, 250)
#define screen_size 				float2(ScreenSize.x,ScreenSize.x*ScreenSize.w)
#define screen_ratio 				(screen_size.x / screen_size.y)
#define pixel 						float2(ScreenSize.y,ScreenSize.y*ScreenSize.z)

//FISHEYE:
#define fFisheyeZoom 				lerp(0.51,0.545,VQ*0.01) // lerp(0.51,0.52,VQ*0.01) //
#define fFisheyeDistortion 			0.02 
#define fFisheyeDistortionCubic 	lerp(0.0,0.5,VQ*0.01)
#define fFisheyeColorshift 			0.0 //lerp(0.0,0.02,VQ*0.01)

#define CoefLuma   float3(0.212656,0.715158,0.072186) 

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


////////////////////// TEXTURES \\\\\\\\\\\\\\\\\\\\\\\\\

Texture2D TextureOriginal;  /// LDR color
Texture2D TextureColor;     /// LDR color which is output of previous technique
Texture2D TextureDepth;     /// scene depth

/// Temporary textures which can be set as render target for techniques via annotations like <string RenderTarget="RenderTargetRGBA32";>
Texture2D RenderTargetRGBA32;   // R8G8B8A8 32 bit ldr format
Texture2D RenderTargetRGBA64;   // R16B16G16A16 64 bit ldr format
Texture2D RenderTargetRGBA64F;  // R16B16G16A16F 64 bit hdr format
Texture2D RenderTargetR16F;     // R16F 16 bit hdr format with red channel only
Texture2D RenderTargetR32F;     // R32F 32 bit hdr format with red channel only
Texture2D RenderTargetRGB32F;   // 32 bit hdr format without alpha


Texture2D AtlasD 			<string ResourceName = "Textures/AtlasDay.png";>;
Texture2D AtlasN 			<string ResourceName = "Textures/AtlasNight.png";>;
Texture2D AtlasI 			<string ResourceName = "Textures/AtlasNight.png";>;

////////////////////// SAMPLERS \\\\\\\\\\\\\\\\\\\\\\\\\

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

/////////////////// DATA STRUCTURE \\\\\\\\\\\\\\\\\\\\\\

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


/////////////////////////////\\\\\\\\\\\\\\\\\\\\\\\\\\\\
//                      MSL HELPER                     //
/////////////////////////////////////////////////////////

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

// ------------------- //
//   HELPER CONSTANTS  //
// ------------------- //

// PI, required to calculate Gaussian weight
static const float PI = 3.1415926535897932384626433832795;

// ------------------- //
//   HELPER FUNCTIONS  //
// ------------------- //

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

float linearDepth3(float nonLinDepth, float depthNearVar, float depthFarVar) {

  return (2.0 * depthNearVar) / (depthFarVar + depthNearVar - nonLinDepth * (depthFarVar - depthNearVar));
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


/////////////////////////////\\\\\\\\\\\\\\\\\\\\\\\\\\\\
//                      SHADER CODES                   //
/////////////////////////////////////////////////////////
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

Texture2D VHS_TexNoise <string ResourceName = "Textures/vhsNoise.jpg";>;

static const float  VHS_Timer      = Timer.x * 16777.216;
static const float2 VHS_ScreenSize = float2(ScreenSize.x, ScreenSize.x * ScreenSize.w);
#define t2 		Timer*0.001

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
 *  VHS: drmelon (https://www.shadertoy.com/view/4dBGzK)
 *
 ***************************************************************************/

UI int   VHS_Space  <string UIName="::::VHS::::"; float UIMin = 2.0; float UIMax = 2.0;> = {2};

float VHS_rand(float2 co)
{
    float3 parm = { 12.9898, 78.233, 43758.5453 };
    return frac(sin(dot(co, parm.xy) % 3.14) * parm.z);
}

float4 VHS_VHS(Texture2D texIN, float2 uv )
{
    float4 coord = uv.xxxy;

    coord.r += VHS_rand(float2(VHS_Timer * 0.03, coord.w * 0.42)) * lerp(0.0,(-0.005 , 0.005),fVHSIntensity);
    coord.r += sin(VHS_rand(float2(VHS_Timer*0.1, coord.w))) * fVHSIntensity * 0.000135;

    coord.g += VHS_rand(float2(VHS_Timer*0.0003,coord.w*0.2)) * lerp(0.0,(-0.002 , 0.002),fVHSIntensity*0.33);
    coord.g += sin(VHS_Timer * 0.108) * 0.0009;

    return float4( texIN.Sample(VHS_SamplerLinear, coord.ra).r,
                   texIN.Sample(VHS_SamplerLinear, coord.ga).g,
                   texIN.Sample(VHS_SamplerLinear, coord.ba).ba);
}


UI int    VCR_Space      <string UIName="::::VCR::::"; float UIMin = 3.0; float UIMax = 3.0;> = {3};
UI float2 VCR_noiseScale <string UIName="VCR_noiseScale";> = { 10.0, 20.0 };
UI float  VCR_Brightness <string UIName="VCR_Brightness";> = { 0.4 };

//this can go to Vertex shader
float VCR_onOff(float a, float b, float c)
{
    return step(c, sin(VHS_Timer + a*cos(VHS_Timer*b)));
}

float noise(float2 p)
{
	float sampless = VHS_TexNoise.SampleLevel(VHS_SamplerRepeat,VHS_Timer * float2( 0.01, 0.02), 0).x;
	sampless *= sampless;
	return sampless;
}

float3 getVideo(float2 uv)
{
	float2 look = uv;
	float window = 0.4/(1.0+40.0*(look.y-(t2/25.0 % 2.2))*(look.y-(t2/20.0 % 2.0))); //this was broken
	look.x = look.x + sin(look.y*2.5 + t2)/2500.*VCR_onOff(.0,.0,.2)*(0.01+cos(t2*900.))*window;
	float vShift = 0.0*VCR_onOff(1.0,3.,0.9)*(sin(t2)*sin(t2*900.0) + (0.0 + 0.0*sin(t2*0.1)*cos(t2)));
	look.y = (look.y + vShift % 0.1); //this too
	float3 video = TextureColor.Sample(Sampler1,look).rgb;
	return video;
}

float2 screenDistort(float2 uv)
{
	uv -= float2(.5,.5);
	uv = uv*1.0*(1./1.0+0.*uv.x*uv.x*uv.y*uv.y);
	uv += float2(.5,.5);
	return uv;
}

float3 VHS_Bright(Texture2D texIN, float2 uv)
{

	float4 origcolor3=texIN.Sample(VHS_SamplerLinear, float4(uv, 0, 0));
	uv = screenDistort(uv);
	float3 video = getVideo(uv);
	float vigAmt = 0.0+.0*sin(t2 + 5.*cos(t2*5.));
	float vignette = (1.-vigAmt*(uv.y-0.5)*(uv.y-0.5))*(1.0-vigAmt*(uv.x-0.5)*(uv.x-0.5));
	
	//video += stripes(uv);
	video += noise(uv*9.)/9.;
	video *= vignette;
  
	origcolor3.xyz = video;
	
	return origcolor3;

}

//pre-compute rand in vertex shader
UI int   dCRT_Space           <string UIName="::::CRT::::"; float UIMin = 4.0; float UIMax = 4.0;> = {4};
UI float dCRT_NoiseScale      <string UIName="CRT_NoiseScale";    > = { 4.0 };
UI float dCRT_StripCount      <string UIName="CRT_StripCount";    > = { 0.60 };
UI float dCRT_StripSpeed      <string UIName="CRT_StripSpeed";    > = { 0.67 };
UI float dCRT_StripStrength   <string UIName="CRT_StripStrength"; > = { 1.5 };

UI float dCRT_ScanlineCount    <string UIName="CRT_ScanlineCount";    > = { 0.0 };
UI float dCRT_ScanlineSpeed    <string UIName="CRT_ScanlineSpeed";    > = { 1.0 };
//UI float dCRT_ScanlineStrength <string UIName="CRT_ScanlineStrength"; > = { 0.5 };
#define dCRT_ScanlineStrength  lerp(0.0,0.4,fVHSIntensity)
float2 dCRT_colorshift1(float2 uv, float amount)
{
    return uv + float2( 0.0, amount ); // amount * sin(uv.y * ScreenSize.x * ScreenSize.w * 0.12 + VHS_Timer)
}

float2 dCRT_crt(float2 coord, float bend)
{
    coord -= 0.5;
	
    coord.x *= 1.0 + pow((abs(coord.y) / bend), 2.0);
    coord.y *= 1.0 + pow((abs(coord.x) / bend), 2.0);

    return coord + 0.5;
}

float3 VHS_DirtyCRT(float3 colorIn, Texture2D texIn, float2 txcoord, uniform float4 rand : TEXCOORD1 = {0.0, 0.0, 0.0, 0.0})
{
    float2 uv = txcoord;
	
	
    uv.x  = saturate(cos(uv.y * 2.0 + VHS_Timer));
    uv.x *= saturate(cos(uv.y * 2.0 + VHS_Timer + 4.0) * 10.0);
    uv.x  = txcoord.x - 0.05 * uv.x * lerp(VHS_TexNoise.Sample(VHS_SamplerRepeat, float2(txcoord.x, uv.x)).r, 1.0, 0.9);

	/* //float3 color;
    if(fVHSIntensity>0.8f){  
	colorIn.r = texIn.Sample(VHS_SamplerLinear, dCRT_crt(dCRT_colorshift1(uv, rand.r * 0.025), 2.0));
	colorIn.g = texIn.Sample(VHS_SamplerLinear, dCRT_crt(dCRT_colorshift1(uv, rand.g * 0.01 ), 2.0));
	colorIn.b = texIn.Sample(VHS_SamplerLinear, dCRT_crt(dCRT_colorshift1(uv, rand.b * 0.024), 2.0));
    } */

	//else color = colorIn;
		
    uv = dCRT_crt(uv.xy, 2.0);

    float scanline = sin(ScreenSize.x * ScreenSize.w * 0.7  * uv.y * dCRT_ScanlineCount - VHS_Timer * 10.0 * dCRT_ScanlineSpeed);
    float slowscan = sin(ScreenSize.x * ScreenSize.w * 0.02 * uv.y * dCRT_StripCount    - VHS_Timer * 3.0  * dCRT_StripSpeed);

    uv = pow(cos((txcoord - 0.5) * 0.98 * 3.1415926), 1.2);


    float noise = VHS_TexNoise.Sample(VHS_SamplerRepeat, txcoord * ScreenSize.x * 0.001 * dCRT_NoiseScale + VHS_Timer*6.0).r +
                  VHS_TexNoise.Sample(VHS_SamplerRepeat, txcoord * ScreenSize.x * 0.001 * dCRT_NoiseScale - VHS_Timer*4.0).g;

   return lerp(colorIn, lerp(scanline * dCRT_ScanlineStrength, slowscan * dCRT_StripStrength, 0.5), 0.05)* clamp(noise, 0.96, 1.0);
}

/* float3 VHS_DirtyCRT2(Texture2D texIn, float2 txcoord, uniform float4 rand : TEXCOORD1 = {0.0, 0.0, 0.0, 0.0})
{
    float2 uv = txcoord;

	
    uv.x  = saturate(cos(uv.y * 2.0 + VHS_Timer));
    uv.x *= saturate(cos(uv.y * 2.0 + VHS_Timer + 4.0) * 10.0);
    uv.x  = txcoord.x - 0.05 * uv.x * lerp(VHS_TexNoise.Sample(Sampler0, float2(txcoord.x, uv.x)).r, 1.0, 0.9);

    float3 color = {
        texIn.Sample(Sampler0, dCRT_crt(dCRT_colorshift1(uv, rand.r * 0.025), 2.0)).r,
        texIn.Sample(Sampler0, dCRT_crt(dCRT_colorshift1(uv, rand.g * 0.01 ), 2.0)).g,
        texIn.Sample(Sampler0, dCRT_crt(dCRT_colorshift1(uv, rand.b * 0.024), 2.0)).b
        };

    uv = dCRT_crt(uv.xy, 2.0);


    float slowscan = sin(ScreenSize.x * ScreenSize.w * 0.02 * uv.y * dCRT_StripCount    - VHS_Timer * 3.0  * dCRT_StripSpeed);

    uv = pow(cos((txcoord - 0.5) * 0.98 * 3.1415926), 1.2);


    float noise = VHS_TexNoise.Sample(Sampler0, txcoord * ScreenSize.x * 0.001 * dCRT_NoiseScale + VHS_Timer*6.0).r +
                  VHS_TexNoise.Sample(Sampler0, txcoord * ScreenSize.x * 0.001 * dCRT_NoiseScale - VHS_Timer*4.0).g;

    return lerp(color, lerp(dCRT_ScanlineStrength, slowscan * dCRT_StripStrength, 0.5), 0.05)* clamp(noise, 0.96, 1.0);
} */

/***************************************************************************
 *
 *  VHS Tape Noise: Vladmir Storm (https://www.shadertoy.com/view/MlfSWr)
 *
 ***************************************************************************/

UI int   tNoise_Space    <string UIName="::::TapeNoise::::"; > = {0};
UI float tNoise_linesN   <string UIName="TapeNoise_linesN"; float UIStep = 1.0;> = {960.0};
UI float tNoise_Curve    <string UIName="TapeNoise_Curve";> = {15.0};
UI float tNoise_Strength <string UIName="TapeNoise_Strength";> = {2.0};
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
    float v = -VHS_Timer*60.0;
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


VHS_struct VS_VHSnPost(VS_INPUT_POST IN)
{
    return VS_Shared(float4(IN.pos, 1.0), IN.txcoord);
}

//-------------------DEPTHSHARPENING-------------------//
// JawZ: Author and developer of this file             //
// Prod80: Initial depth based Sharpening code         //
//+++++++++++++++++++++++++++++++++++++++++++++++++++++//

float4 msGaussian(float2 inCoord, float2 dir)
{
  float2 pixelsize    = ScreenSize.y * dir;
         pixelsize.y *= ScreenSize.z;
         
  float Depth    = TextureDepth.Sample(Sampler0, inCoord.xy ).x;
  float linDepth = linearDepth( Depth, 0.5f, fFarDepth );

  float SigmaSum     = 0.0f;
  float sampleOffset = 1.0f;

/// Gaussian
      float fBlurSigma1 = max( fBlurSigma * ( 1.0f - linDepth ), 0.3f );
    float3 Sigma;
      Sigma.x = 1.0f / ( sqrt( 2.0f * PI ) * fBlurSigma1 );
      Sigma.y = exp( -0.5f / ( fBlurSigma1 * fBlurSigma1 ));
      Sigma.z = Sigma.y * Sigma.y;

/// Center weight
    float4 color = TextureColor.Sample(Sampler1, inCoord.xy);
    color  *= Sigma.x;
    SigmaSum += Sigma.x;
    Sigma.xy *= Sigma.yz;

    for(int i = 0; i < 7; ++i)
    {
      color     += TextureColor.Sample(Sampler1, inCoord.xy + pixelsize * sampleOffset) * Sigma.x;
      color     += TextureColor.Sample(Sampler1, inCoord.xy - pixelsize * sampleOffset) * Sigma.x;
      SigmaSum    += ( 2.0f * Sigma.x );
      sampleOffset = sampleOffset + 1.0f;
      Sigma.xy    *= Sigma.yz;
    }

  color.xyz /= SigmaSum;
  color.w    = 1.0f;
  return color;
}

float4 msEdges(float4 inColor, float2 inCoord)
{
  float4 orig    = TextureOriginal.Sample(Sampler1, inCoord.xy);
  float4 blurred = inColor;

/// Find edges
    orig.xyz           = saturate( orig.xyz );
    blurred.xyz        = saturate( blurred.xyz );
    float3 Edges       = max( saturate( orig.xyz - blurred.xyz ) - fThreshold, 0.0f );
    float3 invBlur     = saturate( 1.0f - blurred.xyz );
    float3 originvBlur = saturate( orig.xyz + invBlur.xyz );
    float3 invOrigBlur = max( saturate( 1.0f - originvBlur.xyz ) - fThreshold, 0.0f );

    float3 edges = max(( saturate( fSharpening * Edges.xyz )) - ( saturate( fSharpening * invOrigBlur.xyz )), 0.0f );

  return float4(edges, 1.0);
}

float4 msSharpen(float2 inCoord, float2 dir)
{
/// Smooth out edges (reduce aliasing) - expensive, likely

    float2 pixelsize    = ScreenSize.y * dir;
           pixelsize.y *= ScreenSize.z;

    float SigmaSum     = 0.0f;
    float sampleOffset = 1.0f;

/// Gaussian
    float BlurSigma = fSmooth;
    float3 Sigma;
    Sigma.x         = 1.0f / ( sqrt( 2.0f * PI ) * BlurSigma );
    Sigma.y         = exp( -0.5f / ( BlurSigma * BlurSigma ));
    Sigma.z         = Sigma.y * Sigma.y;

/// Center weight
    float4 edges = TextureColor.Sample(Sampler1, inCoord.xy);
    edges    *= Sigma.x;
    SigmaSum += Sigma.x;
    Sigma.xy *= Sigma.yz;

    for(int i = 0; i < 5; ++i)
    {
      edges       += TextureColor.Sample(Sampler1, inCoord.xy + sampleOffset * pixelsize) * Sigma.x;
      edges       += TextureColor.Sample(Sampler1, inCoord.xy - sampleOffset * pixelsize) * Sigma.x;
      SigmaSum    += ( 2.0f * Sigma.x );
      sampleOffset = sampleOffset + 1.0f;
      Sigma.xy    *= Sigma.yz;
    }

  return float4( edges.xyz / SigmaSum, 1.0);
}

// SHARPENING FUNCTION by CRASHAHOLIC (edges finder), CEEJAY.DK (lumasharpening)
float3 CleanSharp( float2 uv )
{
	float3 color = TextureColor.Sample(Sampler1, uv).rgb;       
	float4 mask;
	float2	pixeloffset = ScreenSize.y;
	pixeloffset.y*= ScreenSize.z;
	float px = ScreenSize.y;
	float py = ScreenSize.y * ScreenSize.z;
	
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
		float2 coord = pixeloffset * (offsets[i] * LineThickness) + uv.xy;
		neighbours[i] = TextureDepth.Sample(Sampler1, coord).x;
	}

	float Mid = TextureDepth.Sample(Sampler1, uv.xy).x; 
	float Depth = TextureDepth.Sample(Sampler0, uv.xy ).x;
	float linDepthFromS = linearDepth3(Depth, 0.5f, fSharpFDepth);
	
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
	addAll = addAll * DepthLineMult;
	addAll = pow(abs(addAll), DepthLinePower);
	addAll = clamp(addAll, 0.0, 1.0);

	//Sharpening
	float3 sharp_strength_luma = (CoefLuma * fSharpAmount); 

	float3 blur_ori = TextureColor.Sample(Sampler1, uv + float2(0.4*px,-1.2*py)* SharpRadius).rgb;  
	blur_ori += TextureColor.Sample(Sampler1, uv + float2(-1.2*px,-0.4*py) * SharpRadius).rgb; 
	blur_ori += TextureColor.Sample(Sampler1, uv + float2(1.2*px,0.4*py) * SharpRadius).rgb; 
	blur_ori += TextureColor.Sample(Sampler1, uv + float2(-0.4*px,1.2*py) * SharpRadius).rgb; 

	blur_ori *= 0.25;  
	sharp_strength_luma *= 0.51;

	float3 sharp = color - blur_ori;  
	if (ENABLE_DEPTH) sharp = sharp * (1.0f - linDepthFromS);
	
	float4 sharp_strength_luma_clamp = float4(sharp_strength_luma * (0.5 / sharp_clamp),0.5); 
	float sharp_luma = saturate(dot(float4(sharp,1.0), sharp_strength_luma_clamp)); 
	sharp_luma = (sharp_clamp * 2.0) * sharp_luma - sharp_clamp; 
	
	float3 outputcolor = show_sharpen? saturate(0.5 + (sharp_luma * 4.0)).rrr:  color + sharp_luma;    

	if (ENABLE_EDGES) outputcolor=lerp(outputcolor,color,saturate(addAll));

	return saturate(outputcolor);
}


//------------------LENS DISTORTION--------------------//
// by Weaseltron                                       //
//+++++++++++++++++++++++++++++++++++++++++++++++++++++//

   float2 barrelDistortion( float2 p, float2 amt )
{
    p = 2.0 * p - 1.0;
    float maxBarrelPower = sqrt(5.0);
    float radius = dot(p,p); //faster but doesn't match above accurately
    p *= pow(radius, maxBarrelPower * amt);
	

    return p * 0.5 + 0.5;
}

float2 brownConradyDistortion(float2 uv, float scalar)
{
    uv = (uv - 0.5 ) * 2.0;
    
    if( true )
    {
        float barrelDistortion1 = -0.02 * scalar; // K1 in text books
        float barrelDistortion2 = 0.0 * scalar; // K2 in text books

        float r2 = dot(uv,uv);
        uv *= 1.0 + barrelDistortion1 * r2 + barrelDistortion2 * r2 * r2;
       
    }
    
   return (uv / 2.0) + 0.5;
}

float3 LensCA( float2 uv )
{
	
    float maxDistort = LCAStrength;//4 * (1.0-iMouse.x/iResolution.x);

    float scalar = 1.0 * maxDistort;
//    float4 colourScalar = float4(2.0, 1.5, 1.0, 1.0);
    float4 colourScalar = float4(700.0, 560.0, 490.0, 1.0);	// Based on the true wavelengths of red, green, blue light.
    colourScalar /= max(max(colourScalar.x, colourScalar.y), colourScalar.z);
    colourScalar *= 2.0;
    
    colourScalar *= scalar;

    const float numTaps = 8.0; // Original value: 8
    
    float3 fragColor = 0.0;
    for( float tap = 0.0; tap < numTaps; tap += 1.0 )
    {
        fragColor.r += TextureColor.Sample(Sampler1, brownConradyDistortion(uv, colourScalar.r)).r;
        fragColor.g += TextureColor.Sample(Sampler1, brownConradyDistortion(uv, colourScalar.g)).g;
        fragColor.b += TextureColor.Sample(Sampler1, brownConradyDistortion(uv, colourScalar.b)).b;
     
        colourScalar *= 0.95;
    }
    
    fragColor /= numTaps;
  
    return fragColor;
}

//------------------------FISHEYE----------------------//
// by Gilcher Pascal aka Marty McFly                   //
//+++++++++++++++++++++++++++++++++++++++++++++++++++++//

//kingeric1992:  modified to ouput distortion coordnate only.
void FishEyePass(float2 texcoord, out float2 rCoords, out float2 gCoords, out float2 bCoords)
{
	float4 coord=0.0;
	coord.xy=texcoord.xy;
	coord.w=0.0;

	float4 color = 0.0.xxxx;
	  
	float3 eta = float3(1.0+fFisheyeColorshift*0.9,1.0+fFisheyeColorshift*0.6,1.0+fFisheyeColorshift*0.3);
	float2 center;
	center.x = coord.x-0.5;
	center.y = coord.y-0.5;
	float LensZoom = 1.0/fFisheyeZoom;

	float r2 = (texcoord.x-0.5) * (texcoord.x-0.5) + (texcoord.y-0.5) * (texcoord.y-0.5);     
	float f = 0;

	if( fFisheyeDistortionCubic == 0.0){
		f = 1 + r2 * fFisheyeDistortion;
	}else{
                f = 1 + r2 * (fFisheyeDistortion + fFisheyeDistortionCubic * sqrt(r2));
	};

//	float x = f*LensZoom*(coord.x-0.5)+0.5;
//	float y = f*LensZoom*(coord.y-0.5)+0.5;
	
    rCoords = (f*eta.r)*LensZoom*(center.xy*0.5)+0.5;
	gCoords = (f*eta.g)*LensZoom*(center.xy*0.5)+0.5;
	bCoords = (f*eta.b)*LensZoom*(center.xy*0.5)+0.5;
	
//	color.x = tex2D(RFX_backbufferColor,rCoords).r;
//	color.y = tex2D(RFX_backbufferColor,gCoords).g;
//	color.z = tex2D(RFX_backbufferColor,bCoords).b;

//	return color;
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

float3 lin2srgb_fast(float3 v) { return sqrt(v); }
float3 srgb2lin_fast(float3 v) { return v * v; }

//-----------------------GRAIN-------------------------//
// martinsh:          Author of Film Grain             //
// Angelo Gonzalez:   port to ReShade                  //
// roxahris:          port to ENB                      //
//+++++++++++++++++++++++++++++++++++++++++++++++++++++//

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

    float3 rotOffset = float3(1.425,3.892,5.835); //rotation offset values
    float2 rotCoordsR = coordRot(position, Timer.x*16777216 + rotOffset.x);
    float2 rot = rotCoordsR*float2(width/grainsize,height/grainsize);
    float pNoise = pnoise3D(float3(rot.x,rot.y,0.0));
    float3 noise = float3(pNoise, pNoise, pNoise);

    if (Use_GrainColour == 1)
    {
        float2 rotCoordsG = coordRot(position, Timer.x*16777216 + rotOffset.y);
        float2 rotCoordsB = coordRot(position, Timer.x*16777216 + rotOffset.z);
        noise.g = lerp(noise.r,pnoise3D(float3(rotCoordsG*float2(width/grainsize,height/grainsize),1.0)),coloramount);
        noise.b = lerp(noise.r,pnoise3D(float3(rotCoordsB*float2(width/grainsize,height/grainsize),2.0)),coloramount);
    }

    //noisiness response curve based on scene luminance
    float3 lumcoeff = float3(0.299,0.587,0.114);
    float luminance = lerp(0.0,dot(col, lumcoeff),lumamount);
    float lum = smoothstep(0.2,0.0,luminance);
    lum += luminance;

    float2 thepow = pow(lum, 4.0);

    noise = lerp(noise,float3(0.0, 0.0, 0.0),pow(lum,4.0));
    col += noise*(fGrainIntensity*0.0033); 

    return float4(col,1.0);

    //return noise;
}



float4 Letterbox(float4 colorInput, float2 tex)
{
    //if(border_ratio > 0.0f) 
	//{
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
	//}
return colorInput;
}

float4 VignetteBorder(float4 inColor, float2 inTexCoords)
{
     float3 origcolor = inColor;

      float2 uv      = (inTexCoords.xy - 0.5f) * EVignetteRadiusBorder;
      float vignette = saturate(dot(uv.xy, uv.xy));
      vignette       = pow(vignette, EVignetteCurve);
	  
      inColor.xyz    = lerp(origcolor.xyz, EVignetteColor, vignette * EVignetteAmount);

	return inColor;

}

float4 Vignette(float4 inColor, float2 inTexCoords)
{
     float3 origcolor = inColor;

      float2 uv      = (inTexCoords.xy - 0.5f) * EVignetteRadius;
      float vignette = saturate(dot(uv.xy, uv.xy));
      vignette       = pow(vignette, EVignetteCurve);
	  
      inColor.xyz    = lerp(origcolor.xyz, EVignetteColor, vignette * EVignetteAmount);

	return inColor;

}

//-----------------------LEVELS------------------------//
// by Christian Cann Schuldt Jensen ~ CeeJay.dk        //
//+++++++++++++++++++++++++++++++++++++++++++++++++++++//

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

//-----------------------TONEMAP-----------------------//
// by Christian Cann Schuldt Jensen ~ CeeJay.dk        //
//+++++++++++++++++++++++++++++++++++++++++++++++++++++//

   float4 TonemapPass( float4 colorInput)
{
	float3 color = colorInput.rgb;
	
	float Gamma   = lerp(lerp(GammaN, GammaD, ENightDayFactor), GammaI, EInteriorFactor);
	
	color = saturate(color - Defog * FogColor); // Defog
	
	color *= pow(2.0f, Exposure); // Exposure
	
	color = pow(color, Gamma);    // Gamma -- roll into the first gamma correction in main.h ?

	//#define BlueShift 0.00	//Blueshift
	//float4 d = color * float4(1.05f, 0.97f, 1.27f, color.a);
	//color = lerp(color, d, BlueShift);
	
	float3 lumCoeff = float3(0.2126, 0.7152, 0.0722);
	float lum = dot(lumCoeff, color.rgb);
	
	float3 blend = lum.rrr; //dont use float3
	
	float L = saturate( 10.0 * (lum - 0.45) );
  	
	float3 result1 = 2.0f * color.rgb * blend;
	float3 result2 = 1.0f - 2.0f * (1.0f - blend) * (1.0f - color.rgb);
	
	float3 newColor = lerp(result1, result2, L);
	//float A2 = Bleach * color.rgb; //why use a float for A2 here and then multiply by color.rgb (a float3)?
	float3 A2 = Bleach * color.rgb; //
	float3 mixRGB = A2 * newColor;
	
	color.rgb += ((1.0f - A2) * mixRGB);
	
	//float3 middlegray = float(color.r + color.g + color.b) / 3;
	float3 middlegray = dot(color,(1.0/3.0)); //1fps slower than the original on nvidia, 2 fps faster on AMD
	
	float3 diffcolor = color - middlegray; //float 3 here
	colorInput.rgb = (color + diffcolor * Saturation)/(1+(diffcolor*Saturation)); //saturation
	
	return colorInput;
}

//21 tap reduced to 6 weights ( 41 pixels wide )
static const float sampleOffsets_3[6]   = { 0.0,         1.452313744966,      3.390210239952,      5.331472958797,      7.277552900121,      9.229394260785   };
static const float sampleWeights_3[6]   = { 0.142479385858,   0.244115579374,      0.131636577371,      0.043283482080,      0.008668409765,      0.001056258481   };

float3 screen(float3 a, float3 b)
{
   return (1.0f - (1.0f - a) * (1.0f - b));
}



float3 Colorgrader(float3 inColor, int iCG)
{
	//NATURAL 0
	float3 NaturalColor;
	float4 Natural = float4( inColor * ( TuningColorLUTTileAmountY - 1.0f ), 1.0f);
    
	Natural.xy = (Natural.xy + 0.5f) * TuningColorLUTNorm;
    Natural.x += trunc(Natural.z) * TuningColorLUTNorm.y;
    Natural.w  = Natural.x + TuningColorLUTNorm.y;
    Natural.y  = (Natural.y + 0) / float(TuningColorLUTCount) ;
	
	Natural = 	lerp(
					lerp(
					(lerp(AtlasN.SampleLevel(SamplerLut, Natural.xy, 0), AtlasN.SampleLevel(SamplerLut, Natural.wy, 0),frac(Natural.z))),
					(lerp(AtlasD.SampleLevel(SamplerLut, Natural.xy, 0), AtlasD.SampleLevel(SamplerLut, Natural.wy, 0),frac(Natural.z))), ENightDayFactor),
					(lerp(AtlasI.SampleLevel(SamplerLut, Natural.xy, 0), AtlasI.SampleLevel(SamplerLut, Natural.wy, 0),frac(Natural.z))), EInteriorFactor);
	
	if (EInteriorFactor) NaturalColor.xyz=	lerp(inColor.xyz, Natural.xyz, 0.8);
	else NaturalColor.xyz=	lerp(inColor.xyz, Natural.xyz, 1.1f);
	
    //GRIM 1
	float3 GrimColor;
	float4 GRIM = float4( inColor * ( TuningColorLUTTileAmountY - 1.0f ), 1.0f);
    
	GRIM.xy = (GRIM.xy + 0.5f) * TuningColorLUTNorm;
    GRIM.x += trunc(GRIM.z) * TuningColorLUTNorm.y;
    GRIM.w  = GRIM.x + TuningColorLUTNorm.y;
    GRIM.y  = (GRIM.y + 1) / float(TuningColorLUTCount) ;
	
	GRIM = 	lerp(
					lerp(
					(lerp(AtlasN.SampleLevel(SamplerLut, GRIM.xy, 0), AtlasN.SampleLevel(SamplerLut, GRIM.wy, 0),frac(GRIM.z))),
					(lerp(AtlasD.SampleLevel(SamplerLut, GRIM.xy, 0), AtlasD.SampleLevel(SamplerLut, GRIM.wy, 0),frac(GRIM.z))), ENightDayFactor),
					(lerp(AtlasI.SampleLevel(SamplerLut, GRIM.xy, 0), AtlasI.SampleLevel(SamplerLut, GRIM.wy, 0),frac(GRIM.z))), EInteriorFactor);
	
	if (EInteriorFactor) GrimColor.xyz=	lerp(inColor.xyz, GRIM.xyz, 0.8);
	else GrimColor.xyz=	lerp(inColor.xyz, GRIM.xyz, 1.1f);
	

	// COLORGRADES
	float4 ColorLUTCG = float4( inColor * ( TuningColorLUTTileAmountY - 1.0f ), 1.0f);
    
	ColorLUTCG.xy = (ColorLUTCG.xy + 0.5f) * TuningColorLUTNorm;
    ColorLUTCG.x += trunc(ColorLUTCG.z) * TuningColorLUTNorm.y;
    ColorLUTCG.w  = ColorLUTCG.x + TuningColorLUTNorm.y;
    ColorLUTCG.y  = (ColorLUTCG.y + iCG) / float(TuningColorLUTCount) ;
    
    ColorLUTCG = 	lerp(
					lerp(
					(lerp(AtlasN.SampleLevel(SamplerLut, ColorLUTCG.xy, 0), AtlasN.SampleLevel(SamplerLut, ColorLUTCG.wy, 0),frac(ColorLUTCG.z))),
					(lerp(AtlasD.SampleLevel(SamplerLut, ColorLUTCG.xy, 0), AtlasD.SampleLevel(SamplerLut, ColorLUTCG.wy, 0),frac(ColorLUTCG.z))), ENightDayFactor),
					(lerp(AtlasI.SampleLevel(SamplerLut, ColorLUTCG.xy, 0), AtlasI.SampleLevel(SamplerLut, ColorLUTCG.wy, 0),frac(ColorLUTCG.z))), EInteriorFactor);
	
	
	if(iCG != -1) {
		if(iCGBlend == 0)	inColor.xyz=	lerp(GrimColor.xyz, ColorLUTCG.xyz, Colormix*0.01);
		if(iCGBlend == 1) 	inColor.xyz = 	lerp(GrimColor.xyz,(BlendLuminosity(GrimColor.xyz,ColorLUTCG.xyz)),Colormix*0.01);
		if(iCGBlend == 2) 	inColor.xyz = 	lerp(GrimColor.xyz,(BlendColor(GrimColor.xyz,ColorLUTCG.xyz)),Colormix*0.01);
	}
	else {
		if(iCGBlend == 0) 	inColor.xyz=	lerp(GrimColor.xyz, NaturalColor.xyz, Colormix*0.01);
		if(iCGBlend == 1) 	inColor.xyz=	lerp(GrimColor.xyz,(BlendLuminosity(GrimColor.xyz,NaturalColor.xyz)),Colormix*0.01);
		if(iCGBlend == 2) 	inColor.xyz=	lerp(GrimColor.xyz,(BlendColor(GrimColor.xyz,NaturalColor.xyz)),Colormix*0.01);
	}	
	
	float grey = dot(inColor.rgb, float3(0.3, 0.59, 0.11));
	if (fSaturation!=1.0f) inColor.xyz = saturate(BlendColor(inColor.xyz,(lerp(grey, inColor, fSaturation))));
	return inColor.xyz;
	
}

// VERTEX SHADER
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

/////////////////////////////\\\\\\\\\\\\\\\\\\\\\\\\\\\\
//                      PIXEL SHADER                   //
/////////////////////////////////////////////////////////
float4	PS_Blur(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
	float4	res;
	float4	color;
	float4	centercolor;
	float2	pixeloffset=ScreenSize.y;
	pixeloffset.y*=ScreenSize.z;

	centercolor=TextureColor.Sample(Sampler0, IN.txcoord0.xy);
	color=0.0;
	float2	offsets[4]=
	{
		float2(-1.0,-1.0),
		float2(-1.0, 1.0),
		float2( 1.0,-1.0),
		float2( 1.0, 1.0),
	};
	for (int i=0; i<4; i++)
	{
		float2	coord=offsets[i].xy * pixeloffset.xy * EBlurRange + IN.txcoord0.xy;
		color.xyz+=TextureColor.Sample(Sampler1, coord.xy);
	}
	color.xyz+=centercolor.xyz;
	color.xyz *= 0.2;

	res.xyz=lerp(centercolor.xyz, color.xyz, EBlurAmount);

	res.w=1.0;
	return res;
}

float4	PS_Blur2(VHS_struct IN) : SV_Target
{
	float4	res;
	float4	color;
	float4	centercolor;
	float2	pixeloffset=ScreenSize.y;
	pixeloffset.y*=ScreenSize.z;

	if(fVHSIntensity>0.0f)centercolor=VHS_VHS(TextureColor, IN.coord);
	else centercolor=TextureColor.Sample(Sampler0, IN.coord.xy);
	color=0.0;
	float2	offsets[4]=
	{
		float2(-1.0,-1.0),
		float2(-1.0, 1.0),
		float2( 1.0,-1.0),
		float2( 1.0, 1.0),
	};
	for (int i=0; i<4; i++)
	{
		float2	coord=offsets[i].xy * pixeloffset.xy * 1.45 + IN.coord.xy;
		color.xyz+=TextureColor.Sample(Sampler1, coord.xy);
	}
	color.xyz+=centercolor.xyz;
	color.xyz *= 0.2;

	res.xyz=lerp(centercolor.xyz, color.xyz, 0.95*fVHSIntensity);

	res.w=1.0;
	return res;
}

float4	PS_Sharp(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
	float4	res;
	float4	color;
	float4	centercolor;
	float2	pixeloffset=ScreenSize.y;
	pixeloffset.y*=ScreenSize.z;

	centercolor=TextureColor.Sample(Sampler0, IN.txcoord0.xy);
	color=0.0;
	float2	offsets[4]=
	{
		float2(-1.0,-1.0),
		float2(-1.0, 1.0),
		float2( 1.0,-1.0),
		float2( 1.0, 1.0),
	};
	for (int i=0; i<4; i++)
	{
		float2	coord=offsets[i].xy * pixeloffset.xy * ESharpRange + IN.txcoord0.xy;
		color.xyz+=TextureColor.Sample(Sampler1, coord.xy);
	}
	color.xyz *= 0.25;

	float	diffgray=dot((centercolor.xyz-color.xyz), 0.3333);
	res.xyz=ESharpAmount * centercolor.xyz*diffgray + centercolor.xyz;

	res.xyz += triDither(res.xyz, IN.txcoord0.xy, Timer.x);
	
	if(border_ratio > 0.0f) 	res = Letterbox(res, IN.txcoord0.xy);
	res.w=1.0;
	return res;
}

/// SharpenFX
float4 PS_BlurH(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
  return msGaussian(IN.txcoord0.xy, float2(1.0, 0.0)); //15 taps
}

float4 PS_BlurV_and_Edges(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
  return msEdges( msGaussian(IN.txcoord0.xy, float2(0.0, 1.0)), IN.txcoord0.xy);  //15 taps
}

float4 PS_Sharpen1(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
  return msSharpen(IN.txcoord0.xy, float2(1.0, 0.0)); //11 taps
}

float4 PS_Sharpen2(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
  float4 orig  = TextureOriginal.Sample(Sampler1, IN.txcoord0.xy);
  float4 edges = msSharpen(IN.txcoord0.xy, float2(0.0, 1.0)); //11 taps
  float4 color = 1.0;

  edges.rgb = LUMA_SHARPEN ? min( dot( edges.rgb, float3( 0.2126, 0.7152, 0.0722 )), fLimiter) : edges.rgb * fLimiter;
 
  if (SHOW_EDGES==true)  return edges;

  color.rgb = saturate( orig.xyz + edges.rgb );

  return LUMA_SHARPEN ? float4( BlendLuma( orig.rgb, color.rgb), color.a) : color;
}



/////C.A LENS FX
float4 PS_CA(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
   float2 FishEyeRcoord, FishEyeGcoord, FishEyeBcoord;
   FishEyePass(IN.txcoord0.xy, FishEyeRcoord, FishEyeGcoord, FishEyeBcoord);
   return float4( LensCA(FishEyeRcoord).r, LensCA(FishEyeGcoord).g, LensCA(FishEyeBcoord).b, 1.0); //24 taps
}

///////////FISHEYE ////2dSharpening/// Shared/////LUT
float4	PS_Shared(VS_OUTPUT_POST IN, float4 v0 : SV_Position0, uniform int iCG) : SV_Target
{
    float4 res = float4(CleanSharp(IN.txcoord0.xy), 1.0);
	//float grey = dot(res.rgb, float3(0.3, 0.59, 0.11));
	
	res.xyz = GrainPass(IN.txcoord0.xy, res.xyz);
	if(fVHSIntensity>0.0f)  	res = lerp(res, VHS_TapeNoise(res, IN.txcoord0), fVHSIntensity) ;
    if(fVHSIntensity>0.0f) 		res = lerp(res, VHS_LayerNoise(res, IN.txcoord0), fVHSIntensity);
	if(fVHSIntensity>0.0f) 		res = lerp(res, float4(VHS_Bright(TextureColor, IN.txcoord0), 1.0),fVHSIntensity*0.5);
	
	res = LevelsPass(saturate(res));
	res = TonemapPass(saturate(res));

	res.rgb = Colorgrader(saturate(res.rgb),iCG);
	
	//if (fSaturation!=1.0f) 		res.rgb = saturate(BlendColor(res.rgb,(lerp(grey, res, fSaturation))));
	
	if(border_ratio > 0.0f)		res = min(VignetteBorder(res,IN.txcoord0.xy), res);
	if(border_ratio == 0.0f)	res = min(Vignette(res,IN.txcoord0.xy), res);
    

	
	return res;
}


float4	PS_VHS(VHS_struct IN) : SV_Target
    {
		float4 color = TextureColor.Sample(Sampler0, IN.coord);

		if(fVHSIntensity>0.0f) color = lerp(color,VHS_VHS(TextureColor, IN.coord),fVHSIntensity);
		if(fVHSIntensity>0.0f) color = max(lerp(color,float4(VHS_DirtyCRT(color,TextureColor, IN.coord), 1.0),fVHSIntensity),color);

		return color;
    }
		
///////////////////////////////////////////////////////////////////////////////////////


// TECHNIQUES
///++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
/// Techniques are drawed one after another and they use as result of
/// previous as input color. Number of techniques is limited to 255.
/// If UIName specified, then it's base technique which may have extra
/// techniques with indexing
///++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
technique11 NL <string UIName="NATURAL";>  /// Horizontal Gaussian blur
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_BlurH()));
  }
}

technique11 NL1  /// Vertical Gaussian blur
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_BlurV_and_Edges()));
  }
}

technique11 NL2  /// First Sharpening Pass
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Sharpen1()));
  }
}

technique11 NL3 /// Second Sharpening Pass
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Sharpen2()));

  }
}

technique11 NL4  /// C.A
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_CA()));
  }
}

technique11 NL5  /// blur1
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Blur()));
  }
}

technique11 NL6  /// SHARED
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Shared(-1)));
  }
}


technique11 NL7  /// VHS
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_VHSnPost()));
    SetPixelShader(CompileShader(ps_5_0, PS_VHS()));
  }
}

technique11 NL8  /// blur2
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_VHSnPost()));
    SetPixelShader(CompileShader(ps_5_0, PS_Blur2()));
  }
}
technique11 NL9  /// sharp Boris
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Sharp()));
  }
}

/////////////////// BLACK////////////////////////////////

technique11 Black <string UIName="BLACK";>  /// Horizontal Gaussian blur
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_BlurH()));
  }
}

technique11 Black1  /// Vertical Gaussian blur
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_BlurV_and_Edges()));
  }
}

technique11 Black2  /// First Sharpening Pass
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Sharpen1()));
  }
}

technique11 Black3 /// Second Sharpening Pass
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Sharpen2()));

  }
}

technique11 Black4  /// C.A
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_CA()));
  }
}

technique11 Black5  /// blur1
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Blur()));
  }
}

technique11 Black6  /// SHARED
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Shared(2)));
  }
}

technique11 Black7  /// VHS
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_VHSnPost()));
    SetPixelShader(CompileShader(ps_5_0, PS_VHS()));
  }
}

technique11 Black8  /// blur2
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_VHSnPost()));
    SetPixelShader(CompileShader(ps_5_0, PS_Blur2()));
  }
}
technique11 Black9  /// sharp Boris
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Sharp()));
  }
}

/////////////////// Constantine////////////////////////////////

technique11 Constantine <string UIName="CONSTANTINE";>  /// Horizontal Gaussian blur
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_BlurH()));
  }
}

technique11 Constantine1  /// Vertical Gaussian blur
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_BlurV_and_Edges()));
  }
}

technique11 Constantine2  /// First Sharpening Pass
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Sharpen1()));
  }
}

technique11 Constantine3 /// Second Sharpening Pass
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Sharpen2()));

  }
}

technique11 Constantine4  /// C.A
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_CA()));
  }
}

technique11 Constantine5  /// blur1
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Blur()));
  }
}

technique11 Constantine6  /// SHARED
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Shared(3)));
  }
}

technique11 Constantine7  /// VHS
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_VHSnPost()));
    SetPixelShader(CompileShader(ps_5_0, PS_VHS()));
  }
}

technique11 Constantine8  /// blur2
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_VHSnPost()));
    SetPixelShader(CompileShader(ps_5_0, PS_Blur2()));
  }
}
technique11 Constantine9  /// sharp Boris
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Sharp()));
  }
}

/////////////////// Hammer////////////////////////////////

technique11 Hammer <string UIName="HAMMER HORROR";>  /// Horizontal Gaussian blur
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_BlurH()));
  }
}

technique11 Hammer1  /// Vertical Gaussian blur
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_BlurV_and_Edges()));
  }
}

technique11 Hammer2  /// First Sharpening Pass
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Sharpen1()));
  }
}

technique11 Hammer3 /// Second Sharpening Pass
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Sharpen2()));

  }
}

technique11 Hammer4  /// C.A
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_CA()));
  }
}

technique11 Hammer5  /// blur1
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Blur()));
  }
}

technique11 Hammer6  /// SHARED
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Shared(4)));
  }
}

technique11 Hammer7  /// VHS
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_VHSnPost()));
    SetPixelShader(CompileShader(ps_5_0, PS_VHS()));
  }
}

technique11 Hammer8  /// blur2
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_VHSnPost()));
    SetPixelShader(CompileShader(ps_5_0, PS_Blur2()));
  }
}
technique11 Hammer9  /// sharp Boris
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Sharp()));
  }
}

/////////////////// Hostel////////////////////////////////

technique11 Hostel <string UIName="HOSTEL";>  /// Horizontal Gaussian blur
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_BlurH()));
  }
}

technique11 Hostel1  /// Vertical Gaussian blur
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_BlurV_and_Edges()));
  }
}

technique11 Hostel2  /// First Sharpening Pass
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Sharpen1()));
  }
}

technique11 Hostel3 /// Second Sharpening Pass
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Sharpen2()));

  }
}

technique11 Hostel4  /// C.A
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_CA()));
  }
}

technique11 Hostel5  /// blur1
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Blur()));
  }
}

technique11 Hostel6  /// SHARED
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Shared(5)));
  }
}

technique11 Hostel7  /// VHS
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_VHSnPost()));
    SetPixelShader(CompileShader(ps_5_0, PS_VHS()));
  }
}

technique11 Hostel8  /// blur2
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_VHSnPost()));
    SetPixelShader(CompileShader(ps_5_0, PS_Blur2()));
  }
}
technique11 Hostel9  /// sharp Boris
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Sharp()));
  }
}

/////////////////// RE////////////////////////////////

technique11 RE <string UIName="RESIDENT EVIL";>  /// Horizontal Gaussian blur
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_BlurH()));
  }
}

technique11 RE1  /// Vertical Gaussian blur
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_BlurV_and_Edges()));
  }
}

technique11 RE2  /// First Sharpening Pass
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Sharpen1()));
  }
}

technique11 RE3 /// Second Sharpening Pass
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Sharpen2()));

  }
}

technique11 RE4  /// C.A
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_CA()));
  }
}

technique11 RE5  /// blur1
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Blur()));
  }
}

technique11 RE6  /// SHARED
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Shared(6)));
  }
}

technique11 RE7  /// VHS
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_VHSnPost()));
    SetPixelShader(CompileShader(ps_5_0, PS_VHS()));
  }
}

technique11 RE8  /// blur2
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_VHSnPost()));
    SetPixelShader(CompileShader(ps_5_0, PS_Blur2()));
  }
}
technique11 RE9  /// sharp Boris
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Sharp()));
  }
}

/////////////////// SleepyHollow////////////////////////////////

technique11 SleepyHollow <string UIName="SLEEPY HOLLOW";>  /// Horizontal Gaussian blur
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_BlurH()));
  }
}

technique11 SleepyHollow1  /// Vertical Gaussian blur
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_BlurV_and_Edges()));
  }
}

technique11 SleepyHollow2  /// First Sharpening Pass
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Sharpen1()));
  }
}

technique11 SleepyHollow3 /// Second Sharpening Pass
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Sharpen2()));

  }
}

technique11 SleepyHollow4  /// C.A
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_CA()));
  }
}

technique11 SleepyHollow5  /// blur1
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Blur()));
  }
}

technique11 SleepyHollow6  /// SHARED
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Shared(7)));
  }
}

technique11 SleepyHollow7 /// VHS
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_VHSnPost()));
    SetPixelShader(CompileShader(ps_5_0, PS_VHS()));
  }
}

technique11 SleepyHollow8  /// blur2
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_VHSnPost()));
    SetPixelShader(CompileShader(ps_5_0, PS_Blur2()));
  }
}
technique11 SleepyHollow9  /// sharp Boris
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Sharp()));
  }
}

/////////////////// Suspiria////////////////////////////////

technique11 Suspiria <string UIName="SUSPIRIA";>  /// Horizontal Gaussian blur
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_BlurH()));
  }
}

technique11 Suspiria1  /// Vertical Gaussian blur
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_BlurV_and_Edges()));
  }
}

technique11 Suspiria2  /// First Sharpening Pass
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Sharpen1()));
  }
}

technique11 Suspiria3 /// Second Sharpening Pass
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Sharpen2()));

  }
}

technique11 Suspiria4  /// C.A
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_CA()));
  }
}

technique11 Suspiria5  /// blur1
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Blur()));
  }
}

technique11 Suspiria6  /// SHARED
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Shared(8)));
  }
}


technique11 Suspiria7  /// VHS
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_VHSnPost()));
    SetPixelShader(CompileShader(ps_5_0, PS_VHS()));
  }
}

technique11 Suspiria8  /// blur2
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_VHSnPost()));
    SetPixelShader(CompileShader(ps_5_0, PS_Blur2()));
  }
}
technique11 Suspiria9  /// sharp Boris
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Sharp()));
  }
}

/////////////////// Texas////////////////////////////////

technique11 Texas <string UIName="TEXAS CHAINSAW MASSACRE";>  /// Horizontal Gaussian blur
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_BlurH()));
  }
}

technique11 Texas1  /// Vertical Gaussian blur
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_BlurV_and_Edges()));
  }
}

technique11 Texas2  /// First Sharpening Pass
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Sharpen1()));
  }
}

technique11 Texas3 /// Second Sharpening Pass
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Sharpen2()));

  }
}

technique11 Texas4  /// C.A
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_CA()));
  }
}

technique11 Texas5  /// blur1
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Blur()));
  }
}

technique11 Texas6  /// SHARED
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Shared(9)));
  }
}

technique11 Texas7  /// VHS
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_VHSnPost()));
    SetPixelShader(CompileShader(ps_5_0, PS_VHS()));
  }
}

technique11 Texas8  /// blur2
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_VHSnPost()));
    SetPixelShader(CompileShader(ps_5_0, PS_Blur2()));
  }
}
technique11 Texas9  /// sharp Boris
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Sharp()));
  }
}

/////////////////// Woman////////////////////////////////

technique11 Woman <string UIName="THE WOMAN IN BLACK";>  /// Horizontal Gaussian blur
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_BlurH()));
  }
}

technique11 Woman1  /// Vertical Gaussian blur
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_BlurV_and_Edges()));
  }
}

technique11 Woman2  /// First Sharpening Pass
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Sharpen1()));
  }
}

technique11 Woman3 /// Second Sharpening Pass
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Sharpen2()));

  }
}

technique11 Woman4  /// C.A
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_CA()));
  }
}

technique11 Woman5  /// blur1
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Blur()));
  }
}

technique11 Woman6  /// SHARED
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Shared(10)));
  }
}


technique11 Woman7  /// VHS
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_VHSnPost()));
    SetPixelShader(CompileShader(ps_5_0, PS_VHS()));
  }
}

technique11 Woman8  /// blur2
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_VHSnPost()));
    SetPixelShader(CompileShader(ps_5_0, PS_Blur2()));
  }
}
technique11 Woman9  /// sharp Boris
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Sharp()));
  }
}