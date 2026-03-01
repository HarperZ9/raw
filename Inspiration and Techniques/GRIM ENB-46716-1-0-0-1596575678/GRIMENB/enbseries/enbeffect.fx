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

// Day
float 	INFO3 						<string UIName=":::::::::::::::::::::::::DAYTIME::::::::::::::::::::::::::";  string UIWidget="spinner";  float UIMin=0.0;  float UIMax=0.0;> = {0.0};

float 	AdaptationMinDay 			<string UIName="Adaptation Min Day";              string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=100.0;  float UIStep=0.0001;> = {0.05};
float 	AdaptationMaxDay 			<string UIName="Adaptation Max Day";              string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=100.0;  float UIStep=0.001;> = {0.05};
float 	BrightnessDay 				<string UIName="Brightness Day";                  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=10.0;> = {1.0};
float 	IntensityContrastDay 		< string UIName="Intensity Contrast Day";          string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=10.0;> = {1.0};
float	GammaDay					<string UIName="Gamma Day";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;> = {1.0};
float 	SaturationDay 				<string UIName="Color Saturation Day";            string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=10.0;> = {1.0};
float 	ToneMappingOversaturationDay <string UIName="Tonemapping Oversaturation Day";  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=1000.0;> = {180.0};
float 	ToneMappingCurveDay 		<string UIName="Tonemapping Curve Day";           string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=50.0;> = {8.0};

// Night
float 	INFO6 						<string UIName=":::::::::::::::::::::::::NIGHTTIME::::::::::::::::::::::::::";  string UIWidget="spinner";  float UIMin=0.0;  float UIMax=0.0;> = {0.0};

float 	AdaptationMinNight 			<string UIName="Adaptation Min Night";              string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=100.0;  float UIStep=0.0001;> = {0.05};
float 	AdaptationMaxNight 			<string UIName="Adaptation Max Night";              string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=100.0;  float UIStep=0.001;> = {0.05};
float 	BrightnessNight 			<string UIName="Brightness Night";                  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=10.0;> = {1.0};
float 	IntensityContrastNight 		<string UIName="Intensity Contrast Night";          string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=10.0;> = {1.0};
float	GammaNight					<string UIName="Gamma Night";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;> = {1.0};
float 	SaturationNight 			<string UIName="Color Saturation Night";            string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=10.0;> = {1.0};
float 	ToneMappingOversaturationNight <string UIName="Tonemapping Oversaturation Night";  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=1000.0;> = {180.0};
float 	ToneMappingCurveNight 		< string UIName="Tonemapping Curve Night";           string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=50.0;> = {8.0};

// Interiors
float 	INFO7 						<string UIName=":::::::::::::::::::::::::INTERIORS::::::::::::::::::::::::::";  string UIWidget="spinner";  float UIMin=0.0;  float UIMax=0.0;> = {0.0};

float 	AdaptationMinInterior 		<string UIName="Adaptation Min Interior";              string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=100.0;  float UIStep=0.0001;> = {0.05};
float 	AdaptationMaxInterior 		<string UIName="Adaptation Max Interior";              string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=100.0;  float UIStep=0.001;> = {0.05};
float 	BrightnessInterior 			<string UIName="Brightness Interior";                  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=10.0;> = {1.0};
float 	IntensityContrastInterior 	<string UIName="Intensity Contrast Interior";          string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=10.0;> = {1.0};
float	GammaInterior				<string UIName="Gamma Interior";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;> = {1.1};
float 	SaturationInterior 			<string UIName="Color Saturation Interior";            string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=10.0;> = {1.0};
float 	ToneMappingOversaturationInterior <string UIName="Tonemapping Oversaturation Interior";  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=1000.0;> = {180.0};
float 	ToneMappingCurveInterior 	<string UIName="Tonemapping Curve Interior";           string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=50.0;> = {8.0};

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
float4				Params01[7]; //fallout4 parameters
//x - bloom amount; y - lens amount
float4				ENBParams01; //enb parameters

Texture2D			TextureColor; //hdr color
Texture2D			TextureBloom; //vanilla or enb bloom
Texture2D			TextureLens; //enb lens fx
Texture2D			TextureDepth; //scene depth
Texture2D			TextureAdaptation; //vanilla or enb adaptation
Texture2D			TextureAperture; //this frame aperture 1*1 R32F hdr red channel only. computed in depth of field shader file

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

////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FUNCTIONS

#define LUM_709 float3(0.2125, 0.7154, 0.0721) 

float4 AvgLuma(float3 inColor)
{
  return float4(dot(inColor, float3(0.2125f, 0.7154f, 0.0721f)),                 /// Perform a weighted average
                max(inColor.r, max(inColor.g, inColor.b)),                       /// Take the maximum value of the incoming value
                max(max(inColor.x, inColor.y), inColor.z),                       /// Compute the luminance component as per the HSL colour space
                sqrt((inColor.x*inColor.x*0.2125f)+(inColor.y*inColor.y*0.7154f)+(inColor.z*inColor.z*0.0721f)));
}

////ENBPP2

float3 enbPP(float3 inColor, float3 inAdaptation)
{
	// TIME OF DAY SEPARATION 

	float AdaptationMin=lerp(lerp(AdaptationMinNight, AdaptationMinDay, ENightDayFactor), AdaptationMinInterior, EInteriorFactor);
	float AdaptationMax=lerp(lerp(AdaptationMaxNight, AdaptationMaxDay, ENightDayFactor), AdaptationMaxInterior, EInteriorFactor);
	float Brightness=lerp(lerp(BrightnessNight, BrightnessDay, ENightDayFactor), BrightnessInterior, EInteriorFactor);
	float IntensityContrast=lerp(lerp(IntensityContrastNight, IntensityContrastDay, ENightDayFactor), IntensityContrastInterior, EInteriorFactor);
	float Saturation=lerp(lerp(SaturationNight, SaturationDay, ENightDayFactor), SaturationInterior, EInteriorFactor);
	float ToneMappingCurve=lerp(lerp(ToneMappingCurveNight, ToneMappingCurveDay, ENightDayFactor), ToneMappingCurveInterior, EInteriorFactor);
	float ToneMappingOversaturation=lerp(lerp(ToneMappingOversaturationNight, ToneMappingOversaturationDay, ENightDayFactor), ToneMappingOversaturationInterior, EInteriorFactor);

///+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++///

	  float grayadaptation = AvgLuma(inAdaptation.xyz).y;
      grayadaptation     = max(grayadaptation, 0.0);
      grayadaptation     = min(grayadaptation, 50.0);
      inColor.xyz        = inColor.xyz / (grayadaptation * AdaptationMax + AdaptationMin);

    inColor.xyz  *= Brightness;
    inColor.xyz  += 0.000001;
    float3 xncol  = normalize(inColor.xyz);
    float3 scl    = inColor.xyz / xncol.xyz;
    scl           = pow(scl, IntensityContrast);
    xncol.xyz     = pow(xncol.xyz, Saturation);
    inColor.xyz   = scl * xncol.xyz;
    float lumamax = ToneMappingOversaturation;
    inColor.xyz   = (inColor.xyz * (1.0 + inColor.xyz / lumamax)) / (inColor.xyz + ToneMappingCurve);
	
  return inColor;
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

#define BIT_DEPTH 10
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

////////////////////////////////////////////////////////////////////////////////////////////////////////////

// VERTEX SHADER
VS_OUTPUT_POST VS_Draw(VS_INPUT_POST IN)
{
  VS_OUTPUT_POST OUT;
  
    float4 pos;
    pos.xyz=IN.pos.xyz;
    pos.w=1.0;
    OUT.pos=pos;
    OUT.txcoord0.xy=IN.txcoord.xy;

  return OUT;
}

// PIXEL SHADER
float4 PS_Draw(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
	float4	res;
	float   timeweight;
	float   timevalue;
	
	float4	color = 	TextureColor.Sample(Sampler0, IN.txcoord0.xy);
	
	float3	lens = 		TextureLens.Sample(Sampler1, IN.txcoord0.xy).xyz;
	float3	bloom = 	TextureBloom.Sample(Sampler1, IN.txcoord0.xy);

	float4 	eadapt = 	TextureAdaptation.Sample(Sampler0, IN.txcoord0.xy);
	float 	middlegray=	TextureAdaptation.Sample(Sampler0, IN.txcoord0.xy).x;

/// ENB Adaptation

    eadapt.xyz = AvgLuma(eadapt.xyz).y;

//GAMMA

	float Gamma=lerp(lerp(GammaNight, GammaDay, ENightDayFactor), GammaInterior, EInteriorFactor);
	color = pow(color, Gamma);
	 	
/// AGCC

	float TonemapE;
	if(Params01[1].w > 0.02) TonemapE=0.02;
	else TonemapE=Params01[1].w;
	
	float contrastE;
	if(Params01[2].z > 1.01) contrastE=1.01;
	else contrastE=Params01[2].z;
	
	float4	r0, r1, r2, r3;
	r0.xyz = color.xyz;
	r1.xy = Params01[4].zw * IN.txcoord0.xy;
	r1.xyz = TextureBloom.Sample(Sampler1, r1.xy).xyz;
	r0.w = TextureAdaptation.Sample(Sampler0, IN.txcoord0.xy).x;
	r1.w = Params01[1].z / (0.001 + r0.w);
	r2.x = r1.w < Params01[1].y;
	r1.w = r2.x ? Params01[1].y : r1.w;
	r2.x = Params01[1].x < r1.w;
	r1.w = r2.x ? Params01[1].x : r1.w;
	r0.xyz = r1.xyz + r0.xyz;
	r0.xyz = r0.xyz * r1.w;
	r1.xyz = r0.xyz + r0.xyz;
	r2.xyz = r0.xyz * 0.3 + 0.05;
	r3.xy = float2(0.2, 3.333333) * TonemapE;	// Params01[1].w;
	r2.xyz = r1.xyz * r2.xyz + r3.x;
	r0.xyz = r0.xyz * 0.3 + 0.5;
	r0.xyz = r1.xyz * r0.xyz + 0.06;
	r0.xyz = r2.xyz / r0.xyz;
	r0.xyz = -TonemapE * 3.333333 + r0.xyz;
	r1.x = TonemapE * 0.2 + 19.376;
	r1.x = r1.x * 0.0408564 - r3.y;
	r1.xyz = r0.xyz / r1.x;
	r0.x = dot(r1.xyz, float3(0.2125, 0.7154, 0.0721));
	r1.xyz = r1.xyz - r0.x;
	r1.xyz = Params01[2].x * r1.xyz + r0.x; //SAT
	r2.xyz = r0.x * Params01[3].xyz - r1.xyz;
	r1.xyz = Params01[3].w * r2.xyz + r1.xyz;
	r1.xyz = Params01[2].w * r1.xyz - r0.w;
	r0.xyz = contrastE * r1.xyz + r0.w;//CONTRAST

	color.xyz = lerp(r0.xyz, Params01[5].xyz, (Params01[5].w*0.1));

	color.xyz = saturate(color.xyzw);

///BLOOM-Pass1

	color.xyz+=bloom.xyz*(ENBParams01.x*0.16);

/// ENB PP2

	color.rgb = enbPP(color.rgb, eadapt.xyz);

///BLOOM-Pass2

	color.xyz+=max(0.0, bloom.xyz - color.xyz)*ENBParams01.x;

/// ENB Lens

    color.xyz +=max(0.0, lens.xyz - color.xyz)*ENBParams01.y;

/// END

	color.xyz = pow(color.xyz, 1.0/2.2);

  res.xyz=saturate(color);
  res.xyz += triDither(res.xyz, IN.txcoord0.xy, Timer.x);
  res.w=1.0;
  return res;
}

// VANILLA POST PROCESS, DO NOT MODIFY!
float4	PS_DrawOriginal(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
	float4	res;
	float4	color;
	color=TextureColor.Sample(Sampler0, IN.txcoord0.xy); //hdr scene color

	float4	r0, r1, r2, r3;
	r0.xyz = color.xyz;
	r1.xy = Params01[4].zw * IN.txcoord0.xy;
	r1.xyz = TextureBloom.Sample(Sampler1, r1.xy).xyz;
	r0.w = TextureAdaptation.Sample(Sampler0, IN.txcoord0.xy).x;
	r1.w = Params01[1].z / (0.001 + r0.w);
	r2.x = r1.w < Params01[1].y;
	r1.w = r2.x ? Params01[1].y : r1.w;
	r2.x = Params01[1].x < r1.w;
	r1.w = r2.x ? Params01[1].x : r1.w;
	r0.xyz = r1.xyz + r0.xyz;
	r0.xyz = r0.xyz * r1.w;
	r1.xyz = r0.xyz + r0.xyz;
	r2.xyz = r0.xyz * 0.3 + 0.05;
	r3.xy = float2(0.2, 3.333333) * Params01[1].w;
	r2.xyz = r1.xyz * r2.xyz + r3.x;
	r0.xyz = r0.xyz * 0.3 + 0.5;
	r0.xyz = r1.xyz * r0.xyz + 0.06;
	r0.xyz = r2.xyz / r0.xyz;
	r0.xyz = -Params01[1].w * 3.333333 + r0.xyz;
	r1.x = Params01[1].w * 0.2 + 19.376;
	r1.x = r1.x * 0.0408564 - r3.y;
	r1.xyz = r0.xyz / r1.x;
	r0.x = dot(r1.xyz, float3(0.2125, 0.7154, 0.0721));
	r1.xyz = r1.xyz - r0.x;
	r1.xyz = Params01[2].x * r1.xyz + r0.x;
	r2.xyz = r0.x * Params01[3].xyz - r1.xyz;
	r1.xyz = Params01[3].w * r2.xyz + r1.xyz;
	r1.xyz = Params01[2].w * r1.xyz - r0.w;
	r0.xyz = Params01[2].z * r1.xyz + r0.w;
	//last color filter used only for certain conditions, like rifle night scope
	res.xyz = lerp(r0.xyz, Params01[5].xyz, Params01[5].w);

	res.xyz = pow(res.xyz, 1.0/2.2);
	res.w=1.0;
	return res;
}

// TECHNIQUES
technique11 GrimFull <string UIName="GRIM.ENB";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
    SetPixelShader(CompileShader(ps_5_0, PS_Draw()));
  }
}

technique11 ORIGINALPOSTPROCESS <string UIName="VANILLA";> //do not modify this technique
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
    SetPixelShader(CompileShader(ps_5_0, PS_DrawOriginal()));
  }
}