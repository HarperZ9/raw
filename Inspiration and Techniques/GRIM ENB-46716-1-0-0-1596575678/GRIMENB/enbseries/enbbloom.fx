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
// SonicEther&MaxG3D: Original authors of Bloom code   	//
// L00 :  Shader Setup, Presets and Settings,           //
//        Port and Modification of Shaders     		    //
//     Please do not redistribute without credits      	//
////////////////////////////////////////////////////////*/
//            			GRIM ENB           			   	//
float	BloomSlider					< string UIName="Bloom Intensity                                          %";string UIWidget="spinner";float UIMin=0.0f;float UIMax=150.0f;float UIStep=1.0f;> = {100.0}; 

/* float 	INF10 						<string UIName="::::::::::::::::::::BLOOM::::::::::::::::::::::::::";  string UIWidget="spinner";  float UIMin=0.0;  float UIMax=0.0;float UIStep=1.0;> = {1};	
bool 	GaussianBloomColorEffect 	<string UIName="Use Bloom Color";> = {false};
float3 	post_mixer_bloomColor 		<string UIName="Bloom: Color";string UIWidget="Color";> = {1.0, 1.0, 1.0};
bool 	bUseBloomDepth 				<string UIName="Use Bloom Depth";> = {false};
float 	INFO0 						<string UIName=":::::::::::::::::::::::::DIRT::::::::::::::::::::::::::";  string UIWidget="spinner";  float UIMin=0.0;  float UIMax=0.0;> = {0.0};
int		iLensLeakMixmode 			< string UIName="Dirt:: Mixmode"; string UIWidget="spinner"; int UIMin=0; int UIMax=4;> = {1};
float	fLensLeakSaturation			< string UIName="Dirt:: saturation";string UIWidget="spinner";float UIMin=0.0;float UIMax=2.0;> = {2.0};	



float 	INFO1 						<string UIName=":::::::::::::::::::::::::DAYTIME::::::::::::::::::::::::::";  string UIWidget="spinner";  float UIMin=0.0;  float UIMax=0.0;> = {0.0};

float	fContrastDay 				<string UIName="Contrast Day";string UIWidget="spinner";float UIMin=0.0;float UIMax=1000.0;> = {1.0};
float	fLensLeakIntensityDay 		< string UIName="Dirt:: intensity Day"; string UIWidget="spinner"; float UIMin=0.0; float UIMax=20.0;> = {2.0};
float	ECCInBlackDay 				<string UIName="CC Day: In black";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;> = {0.0};
float	ECCInWhiteDay 				<string UIName="CC Day: In white";string UIWidget="Spinner";float UIMin=0.0;float UIMax=500.0;> = {1.0};
float	ECCOutBlackDay 				<string UIName="CC Day: Out black";string UIWidget="Spinner";float UIMin=0.0;float UIMax=1.0;> = {0.0};
float	ECCOutWhiteDay 				<string UIName="CC Day: Out white";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;> = {1.0};
float4	fSaturationDay 				<string UIName="CC Day: Saturation";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;> = {1.0, 1.0, 1.0, 1.0};
float 	post_mixer_bloomShapeDay 	<string UIName="Gaussian: Bloom Shape Day";string UIWidget="Spinner";float UIMin=0.0;float UIMax=32.0;float UIStep=0.01;> = {1.0};
float	fBloomDistanceMaxDay 		<string UIName="Bloom: Distance Max Day";string UIWidget="spinner";float UIMin=0.0;float UIMax=10000.0;float UIStep=1.0;> = {1.0};
float	fBloomDistanceMinDay		<string UIName="Bloom: Distance Min Day";string UIWidget="spinner";float UIMin=0.0;float UIMax=1000.0;> = {1.0};
float	FarMultiplierDay			<string UIName="Bloom: multiplier Day";string UIWidget="spinner";float UIMin=0.0;float UIMax=100.0;> = {2.0};

float 	INFO2 						<string UIName=":::::::::::::::::::::::::NIGHTTIME::::::::::::::::::::::::::";  string UIWidget="spinner";  float UIMin=0.0;  float UIMax=0.0;> = {0.0};

float	fContrastNight 				<string UIName="Contrast Night";string UIWidget="spinner";float UIMin=0.0;float UIMax=1000.0;> = {1.0};
float	fLensLeakIntensityNight 	< string UIName="Dirt:: intensity Night"; string UIWidget="spinner"; float UIMin=0.0; float UIMax=20.0;> = {2.0};
float	ECCInBlackNight 			<string UIName="CC Night: In black";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;> = {0.0};
float	ECCInWhiteNight 			<string UIName="CC Night: In white";string UIWidget="Spinner";float UIMin=0.0;float UIMax=500.0;> = {1.0};
float	ECCOutBlackNight 			<string UIName="CC Night: Out black";string UIWidget="Spinner";float UIMin=0.0;float UIMax=1.0;> = {0.0};
float	ECCOutWhiteNight 			<string UIName="CC Night: Out white";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;> = {1.0};
float4	fSaturationNight 			<string UIName="CC Night: Saturation";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;> = {1.0, 1.0, 1.0, 1.0};
float 	post_mixer_bloomShapeNight 	<string UIName="Gaussian: Bloom Shape Night";string UIWidget="Spinner";float UIMin=0.0;float UIMax=32.0;float UIStep=0.01;> = {1.0};
float	fBloomDistanceMaxNight 		<string UIName="Bloom: Distance Max Night";string UIWidget="spinner";float UIMin=0.0;float UIMax=10000.0;float UIStep=1.0;> = {1.0};
float	fBloomDistanceMinNight		<string UIName="Bloom: Distance Min Night";string UIWidget="spinner";float UIMin=0.0;float UIMax=1000.0;> = {1.0};
float	FarMultiplierNight			<string UIName="Bloom: multiplier Night";string UIWidget="spinner";float UIMin=0.0;float UIMax=100.0;> = {2.0};

float 	INFO7 						<string UIName=":::::::::::::::::::::::::INTERIORS::::::::::::::::::::::::::";  string UIWidget="spinner";  float UIMin=0.0;  float UIMax=0.0;> = {0.0};

float	fContrastInterior 			<string UIName="Contrast Interior";string UIWidget="spinner";float UIMin=0.0;float UIMax=1000.0;> = {1.0};
float	fLensLeakIntensityInterior 	< string UIName="Dirt:: intensity Interior"; string UIWidget="spinner"; float UIMin=0.0; float UIMax=20.0;> = {2.0};
float	ECCInBlackInterior 			<string UIName="CC Interior: In black";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;> = {0.0};
float	ECCInWhiteInterior 			<string UIName="CC Interior: In white";string UIWidget="Spinner";float UIMin=0.0;float UIMax=500.0;> = {1.0};
float	ECCOutBlackInterior 		<string UIName="CC Interior: Out black";string UIWidget="Spinner";float UIMin=0.0;float UIMax=1.0;> = {0.0};
float	ECCOutWhiteInterior 		<string UIName="CC Interior: Out white";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;> = {1.0};
float4	fSaturationInterior 		<string UIName="CC Interior: Saturation";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;> = {1.0, 1.0, 1.0, 1.0};
float 	post_mixer_bloomShapeInterior 	<string UIName="Gaussian: Bloom Shape Interior";string UIWidget="Spinner";float UIMin=0.0;float UIMax=32.0;float UIStep=0.01;> = {1.0};
float	fBloomDistanceMaxInterior 	<string UIName="Bloom: Distance Max Interior";string UIWidget="spinner";float UIMin=0.0;float UIMax=10000.0;float UIStep=1.0;> = {1.0};
float	fBloomDistanceMinInterior	<string UIName="Bloom: Distance Min Interior";string UIWidget="spinner";float UIMin=0.0;float UIMax=1000.0;> = {1.0};
float	FarMultiplierInterior		<string UIName="Bloom: multiplier Interior";string UIWidget="spinner";float UIMin=0.0;float UIMax=100.0;> = {2.0};
 */
///
#define GaussianBloomColorEffect 1
#define bUseBloomDepth 1
#define iLensLeakMixmode 3
#define fLensLeakSaturation 1.8
#define fLensLeakIntensity 4.2

#define post_mixer_bloomColor float3(0.0431, 0.251, 0)
#define fSaturation float4(0.45,0.6,0.65,1.0)

#define fContrast 1.0
#define ECCInBlack 0.0
#define ECCInWhite 1.0
#define ECCOutBlack 0.0
#define ECCOutWhite lerp(0.35,0.38,EInteriorFactor)

#define post_mixer_bloomShape 5.0
#define fBloomDistanceMax 2000
#define fMultiplierMax lerp(1.25,1.40,EInteriorFactor)
#define fDistMin lerp(125,140,EInteriorFactor)
#define Farmultiplier lerp(0.25,fMultiplierMax,BloomSlider*0.01)
#define fBloomDistanceMin lerp(100,fDistMin,BloomSlider*0.01)

//#define XXX lerp(lerp(XXXNight,XXXDay,ENightDayFactor),XXXInterior,EInteriorFactor)

/////////////////////////////////////////////////////////
//               	INTERNAL SETTINGS                  //
/////////////////////////////////////////////////////////

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
Texture2D			TextureDownsampled; //color R16B16G16A16 64 bit or R11G11B10 32 bit hdr format. 1024*1024 size
Texture2D			TextureColor; //color which is output of previous technique (except when drawed to temporary render target), R16B16G16A16 64 bit hdr format. 1024*1024 size

Texture2D			TextureOriginal; //color R16B16G16A16 64 bit or R11G11B10 32 bit hdr format, screen size. PLEASE AVOID USING IT BECAUSE OF ALIASING ARTIFACTS, UNLESS YOU FIX THEM
Texture2D			TextureDepth; //scene depth R32F 32 bit hdr format, screen size. PLEASE AVOID USING IT BECAUSE OF ALIASING ARTIFACTS, UNLESS YOU FIX THEM
Texture2D			TextureAperture; //this frame aperture 1*1 R32F hdr red channel only. computed in PS_Aperture of enbdepthoffield.fx

//temporary textures which can be set as render target for techniques via annotations like <string RenderTarget="RenderTargetRGBA32";>
Texture2D			RenderTarget1024; //R16B16G16A16F 64 bit hdr format, 1024*1024 size
Texture2D			RenderTarget512; //R16B16G16A16F 64 bit hdr format, 512*512 size
Texture2D			RenderTarget256; //R16B16G16A16F 64 bit hdr format, 256*256 size
Texture2D			RenderTarget128; //R16B16G16A16F 64 bit hdr format, 128*128 size
Texture2D			RenderTarget64; //R16B16G16A16F 64 bit hdr format, 64*64 size
Texture2D			RenderTarget32; //R16B16G16A16F 64 bit hdr format, 32*32 size
Texture2D			RenderTarget16; //R16B16G16A16F 64 bit hdr format, 16*16 size
Texture2D			RenderTargetRGBA32; //R8G8B8A8 32 bit ldr format, screen size
Texture2D			RenderTargetRGBA64F; //R16B16G16A16F 64 bit hdr format, screen size

Texture2D TextureLeak 			<string ResourceName = "Textures/BloomDirt.jpg";>;

/////////////////////////////////////////////////////////
//               	    SAMPLERS                       //
/////////////////////////////////////////////////////////

SamplerState		Sampler0
{
	Filter = MIN_MAG_MIP_POINT;
	AddressU = Clamp;
	AddressV = Clamp;
};
SamplerState		Sampler1
{
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = Clamp;
	AddressV = Clamp;
};
SamplerState 		SamplerLeak
{
   Filter = MIN_MAG_MIP_LINEAR;  
   AddressU=Clamp;  
   AddressV=Clamp;
};

/////////////////////////////////////////////////////////
//               	   FUNCTIONS                 	   //
/////////////////////////////////////////////////////////

struct VS_INPUT_POST
{
	float3 pos		: POSITION;
	float2 txcoord	: TEXCOORD0;
};
struct VS_OUTPUT_POST
{
	float4 pos		: SV_POSITION;
	float2 txcoord0	: TEXCOORD0;
};

float4 AvgLuma(float3 inColor)
{
	return float4(dot(inColor, float3(0.2125f, 0.7154f, 0.0721f)),                 /// Perform a weighted average
                max(inColor.r, max(inColor.g, inColor.b)),                       /// Take the maximum value of the incoming value
                max(max(inColor.x, inColor.y), inColor.z),                       /// Compute the luminance component as per the HSL colour space
                sqrt((inColor.x*inColor.x*0.2125f)+(inColor.y*inColor.y*0.7154f)+(inColor.z*inColor.z*0.0721f)));
}

float linearDepth(float d, float n, float f)
{
  return (2.0 * n)/(f + n - d * (f - n));
}

float linearDepth2(float nonLinDepth, float depthNearVar, float depthFarVar) {
/// Implementation example;
///   VAR = linearDepth(eDepth, fFromFarDepth, fFromNearDepth);

  return (2.0 * depthNearVar) / (depthFarVar + depthNearVar - nonLinDepth * (depthFarVar - depthNearVar));
}

float BloomExpansion(float Input, float2 uv) 
{
	float Depth    = TextureDepth.Sample(Sampler0, uv.xy).x;
	float linDepth = linearDepth2(Depth, fBloomDistanceMin, fBloomDistanceMax);

	return lerp(Input, (Input*Farmultiplier) * linDepth, 1.0);
}

float3 ColorFetch(Texture2D inputtex, float2 coord)
{
 	return inputtex.Sample(Sampler1, coord).rgb;   
}


//Horizontal gaussian blur leveraging hardware filtering for fewer texture lookups.
float3  FuncHoriBlur(Texture2D inputtex, float2 uvsrc, float2 pxSize, float Iteration)
{    
float weights[5];
float offsets[5];
    
    weights[0] = 2.0;
    weights[1] = 1.67;
    weights[2] = 0.8;
    weights[3] = 0.23;
    weights[4] = 0.09;
    
    offsets[0] = 0.0;
    offsets[1] = 1.0;
    offsets[2] = 2.0;
    offsets[3] = 3.0;
    offsets[4] = 4.0;
    
    float2 uv = uvsrc; //fragCoord.xy / iResolution.xy;
    
    float3 color = 0.0;
    float weightSum = 0.0;
    
    //if (uv.x < 0.52)
    {
        color += ColorFetch(inputtex, uv) * weights[0];
        weightSum += weights[0];

        for(int i = 1; i < 5; i++)
        {
            float2 offset = offsets[i] * pxSize;
            color += ColorFetch(inputtex, uv + offset * float2(0.6, 0.0)) * weights[i];
            color += ColorFetch(inputtex, uv - offset * float2(0.6, 0.0)) * weights[i];
            weightSum += weights[i] * 2.0;
        }

        color /= weightSum;
    }

    return color;
}

//Vertical gaussian blur leveraging hardware filtering for fewer texture lookups.
float3  FuncVertBlur(Texture2D inputtex, float2 uvsrc, float2 pxSize, float Iteration)
{    
float weights[5];
float offsets[5];
    
    weights[0] = 2.0;
    weights[1] = 1.67;
    weights[2] = 0.8;
    weights[3] = 0.23;
    weights[4] = 0.09;
    
    offsets[0] = 0.0;
    offsets[1] = 1.0;
    offsets[2] = 2.0;
    offsets[3] = 3.0;
    offsets[4] = 4.0;
    
    float2 uv = uvsrc; //fragCoord.xy / iResolution.xy;
    
    float3 color = 0.0;
    float weightSum = 0.0;
    
    //if (uv.x < 0.52)
    {
        color += ColorFetch(inputtex, uv) * weights[0];
        weightSum += weights[0];

        for(int i = 1; i < 5; i++)
        {
            float2 offset = offsets[i] * pxSize;
            color += ColorFetch(inputtex, uv + offset * float2(0.0, 0.6)) * weights[i];
            color += ColorFetch(inputtex, uv - offset * float2(0.0, 0.6)) * weights[i];
            weightSum += weights[i] * 2.0;
        }

        color /= weightSum;
    }

    return color;
}

/////////////////////////////////////////////////////////
//               	  PIXEL SHADER                     //
/////////////////////////////////////////////////////////

VS_OUTPUT_POST	VS_Quad(VS_INPUT_POST IN)
{
	VS_OUTPUT_POST	OUT;
	float4	pos;
	pos.xyz=IN.pos.xyz;
	pos.w=1.0;
	OUT.pos=pos;
	OUT.txcoord0.xy=IN.txcoord.xy;
	return OUT;
}

float4  PS_GaussHResize(VS_OUTPUT_POST IN, float4 v0 : SV_Position0,
  uniform Texture2D inputtex, uniform float texsize, uniform float Iteration) : SV_Target
{
  float4  res;

  float2 pxSize = (1/(texsize))*float2(1, ScreenSize.z);
  res.xyz=FuncHoriBlur(inputtex, IN.txcoord0.xy, pxSize*2.0, Iteration);

  #if 1 // Double blur. This removes artifacing in the raw bloom texture.
  // However, it costs more than ENB's bloom and does like 60 passes. 
  // The aliasing is only noticeable at 100% bloom anyway. 
  res.xyz+=FuncHoriBlur(inputtex, IN.txcoord0.xy, pxSize*2.0, Iteration+1);
  res.xyz/=2;
  #endif
  
  res=max(res, 0.0);
  res=min(res, 16384.0);

  res.w=1.0;
  return res;
}

float4  PS_GaussVResize(VS_OUTPUT_POST IN, float4 v0 : SV_Position0,
  uniform Texture2D inputtex, uniform float texsize, uniform float Iteration) : SV_Target
{
  float4  res;

  float2 pxSize = (1/(texsize))*float2(1, ScreenSize.z);
  res.xyz=FuncVertBlur(inputtex, IN.txcoord0.xy, pxSize*2.0, Iteration);

  #if 1 // Double blur. This removes artifacing in the raw bloom texture.
  // However, it costs more than ENB's bloom and does like 60 passes. 
  // The aliasing is only noticeable at 100% bloom anyway. 
  res.xyz+=FuncVertBlur(inputtex, IN.txcoord0.xy, pxSize*2.0, Iteration+1);
  res.xyz/=2;
  #endif
  
  res=max(res, 0.0);
  res=min(res, 16384.0);

  res.w=1.0;
  return res;
}

float4  PS_GaussHResizeFirst(VS_OUTPUT_POST IN, float4 v0 : SV_Position0,
  uniform Texture2D inputtex, uniform float texsize, uniform float Iteration) : SV_Target
{
  float4  res;

  float2 pxSize = (1/(texsize))*float2(1, ScreenSize.z);
  res.xyz=FuncHoriBlur(inputtex, IN.txcoord0.xy, pxSize*2.0, Iteration);

  float fECCOutWhite;
  if (bUseBloomDepth) fECCOutWhite = BloomExpansion(ECCOutWhite,IN.txcoord0);
  else fECCOutWhite=ECCOutWhite;
  
  #if 1 // Double blur. This removes artifacing in the raw bloom texture.
  // However, it costs more than ENB's bloom and does like 60 passes. 
  // The aliasing is only noticeable at 100% bloom anyway. 
  res.xyz+=FuncHoriBlur(inputtex, IN.txcoord0.xy, pxSize*2.0, Iteration+1);
  res.xyz/=2;
  #endif

  	res.xyz=max(res.xyz-(ECCInBlack*0.1), 0.0) / max(ECCInWhite-(ECCInBlack*0.1), 0.00001);
	if (fContrast!=1.0) res.xyz=pow(res.xyz, fContrast);
	res.xyz=res.xyz*((fECCOutWhite)-ECCOutBlack) + ECCOutBlack;

  res=max(res, 0.0);
  res=min(res, 16384.0);

  res.w=1.0;
  return res;
}

float4  PS_GaussVResizeFirst(VS_OUTPUT_POST IN, float4 v0 : SV_Position0,
  uniform Texture2D inputtex, uniform float texsize, uniform float Iteration) : SV_Target
{
 float4  res;

    float2 pxSize = (1/(texsize))*float2(1, ScreenSize.z);
    res.xyz=FuncVertBlur(inputtex, IN.txcoord0.xy, pxSize*2.0, Iteration);
	
	  float fECCOutWhite;
	  if (bUseBloomDepth) fECCOutWhite = BloomExpansion(ECCOutWhite,IN.txcoord0);
	  else fECCOutWhite=ECCOutWhite;
        #if 1 // Double blur. This removes artifacing in the raw bloom texture.
            // However, it costs more than ENB's bloom and does like 60 passes.
            // The aliasing is only noticeable at 100% bloom anyway.
            res.xyz+=FuncVertBlur(inputtex, IN.txcoord0.xy, pxSize*2.0, Iteration+1);
            res.xyz/=2;
        #endif

        res.xyz=max(res.xyz-(ECCInBlack*0.1), 0.0) / max(ECCInWhite-(ECCInBlack*0.1), 0.00001);
        if (fContrast!=1.0) res.xyz=pow(res.xyz, fContrast);
        res.xyz=res.xyz*((fECCOutWhite)-ECCOutBlack) + ECCOutBlack;

		res=max(res, 0.0);
        res=min(res, 16384.0);

        res.w=1.0;
        return res;
}

float4  PS_GaussMix(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
  float4  res = 0.0;

  // Mercury bloom blending code
  // Source: https://imgur.com/a/MZD3l
  // This is kind of messy... sorry! 
  float weightSum = 0;
  int maxlevel = 5;
  #define TAU 6.28318
 
    // This should get optimised by the compiler. 
    float weight[6];
    float x[6];

    [unroll]
    for (int i=0; i <= maxlevel; i++) {
        weight[i] = pow(i+1, post_mixer_bloomShape);
        weightSum += weight[i];
        x[i] = i*2;
    }

    if (GaussianBloomColorEffect) {
    res.xyz += ColorFetch(RenderTarget1024, IN.txcoord0.xy) * weight[0] * (1 + post_mixer_bloomColor*float3(sin(x[0]), sin(x[0]+TAU/3), sin(x[0]-TAU/3)));
    res.xyz += ColorFetch(RenderTarget512, IN.txcoord0.xy)  * weight[1] * (1 + post_mixer_bloomColor*float3(sin(x[1]), sin(x[1]+TAU/3), sin(x[1]-TAU/3)));
    res.xyz += ColorFetch(RenderTarget256, IN.txcoord0.xy)  * weight[2] * (1 + post_mixer_bloomColor*float3(sin(x[2]), sin(x[2]+TAU/3), sin(x[2]-TAU/3)));
    res.xyz += ColorFetch(RenderTarget128, IN.txcoord0.xy)  * weight[3] * (1 + post_mixer_bloomColor*float3(sin(x[3]), sin(x[3]+TAU/3), sin(x[3]-TAU/3)));
    res.xyz += ColorFetch(RenderTarget64, IN.txcoord0.xy)   * weight[4] * (1 + post_mixer_bloomColor*float3(sin(x[4]), sin(x[4]+TAU/3), sin(x[4]-TAU/3)));
    res.xyz += ColorFetch(RenderTarget32, IN.txcoord0.xy)   * weight[5] * (1 + post_mixer_bloomColor*float3(sin(x[5]), sin(x[5]+TAU/3), sin(x[5]-TAU/3)));
    } else { 
    res.xyz += ColorFetch(RenderTarget1024, IN.txcoord0.xy) * weight[0] * (1 + (post_mixer_bloomColor * 4.0)); 
    res.xyz += ColorFetch(RenderTarget512, IN.txcoord0.xy)  * weight[1] * (1 + (post_mixer_bloomColor * 2.7889)); 
    res.xyz += ColorFetch(RenderTarget256, IN.txcoord0.xy)  * weight[2] * (1 + (post_mixer_bloomColor * 0.064)); 
    res.xyz += ColorFetch(RenderTarget128, IN.txcoord0.xy)  * weight[3] * (1 + (post_mixer_bloomColor * 0.0529)); 
    res.xyz += ColorFetch(RenderTarget64, IN.txcoord0.xy)   * weight[4] * (1 + (post_mixer_bloomColor * 0.0081)); 
    res.xyz += ColorFetch(RenderTarget32, IN.txcoord0.xy)   * weight[5] * (1 + (post_mixer_bloomColor * 0.0)); 
    };

  res /= weightSum;

	float3 Temp = AvgLuma(res.xyz).w;
    res.xyz = lerp(Temp.xyz, res.xyz, fSaturation);
  
  
	
						float3 Leaktex = TextureLeak.Sample(SamplerLeak, IN.txcoord0.xy);
						float3 lensLeak = Leaktex.xyz*res*fLensLeakIntensity;
						
						res.xyz=saturate(res.xyz);
						
						lensLeak = lerp(dot(lensLeak.xyz,0.333), lensLeak.xyz, fLensLeakSaturation);

						if(iLensLeakMixmode == 1) res.xyz = res.xyz + lensLeak.xyz;
						if(iLensLeakMixmode == 2) res.xyz = 1-(1-res.xyz)*(1-lensLeak.xyz);
						if(iLensLeakMixmode == 3) res.xyz = max(0.0f,max(res.xyz,lerp(res.xyz,(1.0f - (1.0f - saturate(lensLeak.xyz)) *(1.0f - saturate(lensLeak.xyz * 1.0))),1.0)));
						if(iLensLeakMixmode == 4) res.xyz = max(res.xyz, lensLeak.xyz);

  res=max(res, 0.0);
  res=min(res, 16384.0);

  
  res.w=1.0;
  return res;
}

/////////////////////////////////////////////////////////
//               	    TECHNIQUES                     //
/////////////////////////////////////////////////////////

technique11 GrimBloom <string UIName="GRIM: BLOOM";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_GaussHResizeFirst(TextureDownsampled, 1536.0, 2)));
  }
}
technique11 GrimBloom1 <string RenderTarget="RenderTarget1024";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_GaussVResizeFirst(TextureColor, 1536.0, 2)));
  }
}
technique11 GrimBloom2
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_GaussHResizeFirst(RenderTarget1024, 512.0, 4)));
  }
}
technique11 GrimBloom3 <string RenderTarget="RenderTarget512";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_GaussVResizeFirst(TextureColor, 512.0, 4)));
  }
}
technique11 GrimBloom4
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_GaussHResize(RenderTarget512, 256.0, 5)));
  }
}
technique11 GrimBloom5 <string RenderTarget="RenderTarget256";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_GaussVResize(TextureColor, 256.0, 5)));
  }
}
technique11 GrimBloom6
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_GaussHResize(RenderTarget256, 128.0, 6)));
  }
}
technique11 GrimBloom7 <string RenderTarget="RenderTarget128";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_GaussVResize(TextureColor, 128.0, 6)));
  }
}
technique11 GrimBloom8
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_GaussHResize(RenderTarget128, 64.0, 7)));
  }
}
technique11 GrimBloom9 <string RenderTarget="RenderTarget64";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_GaussVResize(TextureColor, 64.0, 7)));
  }
}
technique11 GrimBloom10 
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_GaussHResize(RenderTarget64, 32.0, 8)));
  }
}
technique11 GrimBloom11 <string RenderTarget="RenderTarget32";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_GaussVResize(TextureColor, 32.0, 8)));
  }
}
// last pass output to bloom texture
technique11 GrimBloom12
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_GaussMix()));
  }
}
/* technique11 GrimBloom13
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_ProcessPass_Anamorphic()));
  }
} */
