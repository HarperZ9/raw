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
//     Please do not redistribute without credits      	//
// Boris: For ENBSeries and his knowledge and codes    	//
// L00 :  Shader Setup, Presets and Settings,           //
//        Port and Modification of Shaders     		    //
//     Please do not redistribute without credits      	//
////////////////////////////////////////////////////////*/
//            			GRIM ENB           			   	//

bool ENABLE_HLSENSE 			<string UIName = "Enable Hightlight Sensitivity";> = {false};
int CHOOSE_OUTPUT 				<string UIName = "Choose Grey Output";  string UIWidget="Spinner";  int UIMin=1;  int UIMax=2;> = {1};
float3 fRGB_Out 				<string UIName = "Alter Grey RGB Output nr2";  string UIWidget="Color";> = {0.3333, 0.3333, 0.3333};

float 	INFO1 					<string UIName=":::::::::::::::::::::::::DAYTIME::::::::::::::::::::::::::";  string UIWidget="spinner";  float UIMin=0.0;  float UIMax=0.0;> = {0.0};
float 	fAdaptMinDay 			<string UIName="Adapt.MIN Day";              string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=100.0;  float UIStep=0.001;> = {0.0};
float 	fAdaptMaxDay			<string UIName="Adapt.MAX Day";              string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=100.0;  float UIStep=0.001;> = {0.04};

float 	INFO2 					<string UIName=":::::::::::::::::::::::::NIGHTTIME::::::::::::::::::::::::::";  string UIWidget="spinner";  float UIMin=0.0;  float UIMax=0.0;> = {0.0};
float 	fAdaptMinNight 			<string UIName="Adapt.MIN Night";              string UIWidget="Spinner";  float UIMin=0.0;  float UIMaNight=100.0;  float UIStep=0.001;> = {0.0};
float 	fAdaptMaxNight			<string UIName="Adapt.MAX Night";              string UIWidget="Spinner";  float UIMin=0.0;  float UIMaNight=100.0;  float UIStep=0.001;> = {0.3};

float 	INFO7 					<string UIName=":::::::::::::::::::::::::INTERIORS::::::::::::::::::::::::::";  string UIWidget="spinner";  float UIMin=0.0;  float UIMax=0.0;> = {0.0};
float 	fAdaptMinInterior 		<string UIName="Adapt.MIN Interior";              string UIWidget="Spinner";  float UIMin=0.0;  float UIMaInterior=100.0;  float UIStep=0.001;> = {0.0};
float 	fAdaptMaxInterior		<string UIName="Adapt.MAX Interior";              string UIWidget="Spinner";  float UIMin=0.0;  float UIMaInterior=100.0;  float UIStep=0.001;> = {0.35};

#define fAdaptMin lerp(lerp(fAdaptMinNight,fAdaptMinDay,ENightDayFactor),fAdaptMinInterior,EInteriorFactor)
#define fAdaptMax lerp(lerp(fAdaptMaxNight,fAdaptMaxDay,ENightDayFactor),fAdaptMaxInterior,EInteriorFactor)

//+++++++++++++++++++++++++++++
//external enb parameters, do not modify
//+++++++++++++++++++++++++++++
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

//+++++++++++++++++++++++++++++
//external enb debugging parameters for shader programmers, do not modify
//+++++++++++++++++++++++++++++
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

//+++++++++++++++++++++++++++++
//game and mod parameters, do not modify
//+++++++++++++++++++++++++++++
//x = AdaptationMin, y = AdaptationMax, z = AdaptationSensitivity, w = AdaptationTime multiplied by time elapsed
float4				AdaptationParameters;

Texture2D			TextureCurrent;
Texture2D			TexturePrevious;

SamplerState		Sampler0
{
	Filter = MIN_MAG_MIP_POINT;//MIN_MAG_MIP_LINEAR;
	AddressU = Clamp;
	AddressV = Clamp;
};
SamplerState		Sampler1
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
	float3 pos		: POSITION;
	float2 txcoord	: TEXCOORD0;
};
struct VS_OUTPUT_POST
{
	float4 pos		: SV_POSITION;
	float2 txcoord0	: TEXCOORD0;
};


// HELPER FUNCTIONS
// Luma coefficient gray value for use with color perception effects. Multiple versions
float4 AvgLuma(float3 inColor)
{
  return float4(dot(inColor, float3(0.2125f, 0.7154f, 0.0721f)),                 /// Perform a weighted average
                max(inColor.r, max(inColor.g, inColor.b)),                       /// Take the maximum value of the incoming value
                max(max(inColor.x, inColor.y), inColor.z),                       /// Compute the luminance component as per the HSL colour space
                sqrt((inColor.x*inColor.x*0.2125f)+(inColor.y*inColor.y*0.7154f)+(inColor.z*inColor.z*0.0721f)));
}                  
   
   //+++++++++++++++++++++++++++++++++++++++++++++++++++++//
//               Contains ENB Adaptation               //
//           used by the Modular Shader files          //
//-----------------------CREDITS-----------------------//
// Boris: For ENBSeries and his knowledge and codes    //
// JawZ: Author and developer of this file             //
//+++++++++++++++++++++++++++++++++++++++++++++++++++++//


///++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
/// output size is 16*16
/// TextureCurrent size is 256*256, it's internally downscaled from full screen
/// input texture is R16G16B16A16 or R11G11B10 float format (alpha ignored)
/// output texture is R32 float format (red channel only)
///++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
float4 msDownsample(float4 inColor, float2 inCoord)
{

/// Downsample 256*256 to 16*16
/// More complex blurring methods affect result if sensitivity uncommented
    float2 pos;
    float2 coord;
    float4 curr          = 0.0;
    float4 currmax       = 0.0;
    const float scale    = 1.0 / 16.0;
    const float step     = 1.0 / 16.0;
    const float halfstep = 0.5 / 16.0;
    pos.x                = -0.5 + halfstep;
    for (int x=0; x<16; x++)
    {
      pos.y = -0.5 + halfstep;
      for (int y=0; y<16; y++)
      {
        coord           = pos.xy * scale;
        float4 tempcurr = TextureCurrent.Sample(Sampler0, inCoord.xy + coord.xy);
        currmax         = max(currmax, tempcurr);
        curr           += tempcurr;
        pos.y          += step;
      }
      pos.x += step;
    }
    curr *= 1.0 / (16.0 * 16.0);

    inColor = curr;

  // Adjust sensitivity to small bright areas on the screen
  // Warning! Uncommenting the next line increases sensitivity a lot
  if (ENABLE_HLSENSE==true)    inColor = lerp(curr, currmax, AdaptationParameters.z); // fAdaptSense = AdaptationParameters.z

  ///TODO modify this math to your taste, for example lower intensity for blue colors
  ///gray output
  if (CHOOSE_OUTPUT==1)       inColor = AvgLuma(inColor.xyz).y;
  else if (CHOOSE_OUTPUT==2)  inColor = dot(inColor.xyz, float3(fRGB_Out.x, fRGB_Out.y, fRGB_Out.z));
  return inColor;
}


///++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
/// output size is 1*1
/// TexturePrevious size is 1*1
/// TextureCurrent size is 16*16
/// output and input textures are R32 float format (red channel only)
///++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
float4 msAdaptation(float4 inColor, float2 inCoord)
{


  float prev = TexturePrevious.Sample(Sampler0, inCoord.xy).x;

/// Downsample 16*16 to 1*1
    float2 pos;
    float curr           = 0.0;
    float currmax        = 0.0;
    const float step     = 1.0/16.0;
    const float halfstep = 0.5/16.0;
    pos.x                = halfstep;

    for (int x=0; x<16; x++)
    {
      pos.y = halfstep;
      for (int y=0; y<16; y++)
        {
          float tempcurr = TextureCurrent.Sample(Sampler0, inCoord.xy + pos.xy).x;
          currmax        = max(currmax, tempcurr);
          curr          += tempcurr;
          pos.y         += step;
        }
      pos.x += step;
    }
    curr *= 1.0/(16.0*16.0);

/// Adjust sensitivity to small bright areas on the screen
    curr = lerp(curr, currmax, AdaptationParameters.z); // fAdaptSense = AdaptationParameters.z

/// Smooth by time
    inColor = lerp(prev, curr, AdaptationParameters.w); // ~fAdaptTime * Timer.w

/// Clamp to avoid bugs in post process shader, which have much lower floating point precision
    inColor = max(inColor, 0.001);
    inColor = min(inColor, 16384.0);

/// Limit value if ForceMinMaxValues=true
    float valmax;
    float valcut;
    valmax   = max(inColor.x, max(inColor.y, inColor.z));
    valcut   = max(valmax, fAdaptMin); // fAdaptMin = AdaptationParameters.x
    valcut   = min(valcut, fAdaptMax); // fAdaptMax = AdaptationParameters.y
    inColor *= valcut/(valmax + 0.000000001f);

  return inColor;
}


// VERTEX SHADER
VS_OUTPUT_POST	VS_Quad(VS_INPUT_POST IN, uniform float sizeX, uniform float sizeY)
{
	VS_OUTPUT_POST	OUT;
	float4	pos;
	pos.xyz=IN.pos.xyz;
	pos.w=1.0;
	OUT.pos=pos;
	float2	offset;
	offset.x=sizeX;
	offset.y=sizeY;
	OUT.txcoord0.xy=IN.txcoord.xy + offset.xy;
	return OUT;
}


// PIXEL SHADERS
float4 PS_Downsample(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
  float4 res;

    res = msDownsample(res, IN.txcoord0.xy);    /// Requires enbAdaptation.fxh active

  res.w=1.0;
  return res;
}

float4 PS_Adaptation(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
  float4 res;

    res = msAdaptation(res, IN.txcoord0.xy);    /// Requires enbAdaptation.fxh active

  res.w=1.0;
  return res;
}


// TECHNIQUES
technique11 Downsample  /// First pass for downscaling and computing sensitivity
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad(0.0, 0.0)));
    SetPixelShader(CompileShader(ps_5_0, PS_Downsample()));
  }
}

technique11 Draw  <string UIName="GRIM.ENB: Exposure";>      /// Last pass for mixing everything
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad(0.0, 0.0)));
    SetPixelShader(CompileShader(ps_5_0, PS_Adaptation()));
  }
}