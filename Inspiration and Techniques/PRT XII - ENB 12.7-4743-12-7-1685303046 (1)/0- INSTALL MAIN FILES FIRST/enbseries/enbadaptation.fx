/* 
/////////////////////////////////////////////////////////
//                ENBSeries effect file                //
//         visit http://enbdev.com for updates         //
//       Copyright (c) 2007-2019 Boris Vorontsov       //
//----------------------ENB PRESET---------------------//
			
			NATURAL & ATMOSPHERIC TAMRIEL ENB 3.0.4
			
//-----------------------CREDITS-----------------------//
// Boris: For ENBSeries and his knowledge and codes    //
// L00 :  Shader Setup, Presets and Settings,          //
//        Port, Modification and author of this file   //
//     Please do not redistribute without credits      //
///////////////////////////////////////////////////////*/

bool 	ENABLE_ADAPT 					<string UIName = "Override EnbSeries.ini values";> = {true};

int		Empty00							<string UIName="";		string UIWidget="Spinner";float UIMin=0;float UIMax=0;> = {0};

float 	AdaptationSensitivity_Dawn 		<string UIName="Sensitivity to Light : Dawn";  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=1.0; float UIStep=0.001;> = {0.5};
float 	AdaptationMin_Dawn 				<string UIName="Max.Brighten: 	Adaptation to Shadow : Dawn";  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=100.0; float UIStep=0.001;> = {0.0};
float 	AdaptationMax_Dawn 				<string UIName="Max.Darken: 	Adaptation to Light : Dawn";  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=100.0; float UIStep=0.001;> = {1.0};
int		Empty01							<string UIName=" ";		string UIWidget="Spinner";float UIMin=0;float UIMax=0;> = {0};

float 	AdaptationSensitivity_Sunrise 	<string UIName="Sensitivity to Light : Sunrise";  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=1.0; float UIStep=0.001;> = {0.5};
float 	AdaptationMin_Sunrise 			<string UIName="Max.Brighten: 	Adaptation to Shadow : Sunrise";  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=100.0; float UIStep=0.001;> = {0.0};
float 	AdaptationMax_Sunrise 			<string UIName="Max.Darken: 	Adaptation to Light : Sunrise";  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=100.0; float UIStep=0.001;> = {1.0};
int		Empty02							<string UIName="  ";		string UIWidget="Spinner";float UIMin=0;float UIMax=0;> = {0};

float 	AdaptationSensitivity_Day 		<string UIName="Sensitivity to Light : Day";  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=1.0; float UIStep=0.001;> = {0.5};
float 	AdaptationMin_Day 				<string UIName="Max.Brighten: 	Adaptation to Shadow : Day";  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=100.0; float UIStep=0.001;> = {0.0};
float 	AdaptationMax_Day 				<string UIName="Max.Darken: 	Adaptation to Light : Day";  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=100.0; float UIStep=0.001;> = {1.0};
int		Empty03							<string UIName="   ";		string UIWidget="Spinner";float UIMin=0;float UIMax=0;> = {0};

float 	AdaptationSensitivity_Sunset 	<string UIName="Sensitivity to Light : Sunset";  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=1.0; float UIStep=0.001;> = {0.5};
float 	AdaptationMin_Sunset 			<string UIName="Max.Brighten: 	Adaptation to Shadow : Sunset";  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=100.0; float UIStep=0.001;> = {0.0};
float 	AdaptationMax_Sunset 			<string UIName="Max.Darken: 	Adaptation to Light : Sunset";  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=100.0; float UIStep=0.001;> = {1.0};
int		Empty04							<string UIName="    ";		string UIWidget="Spinner";float UIMin=0;float UIMax=0;> = {0};

float 	AdaptationSensitivity_Dusk 		<string UIName="Sensitivity to Light : Dusk";  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=1.0; float UIStep=0.001;> = {0.5};
float 	AdaptationMin_Dusk 				<string UIName="Max.Brighten: 	Adaptation to Shadow : Dusk";  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=100.0; float UIStep=0.001;> = {0.0};
float 	AdaptationMax_Dusk 				<string UIName="Max.Darken: 	Adaptation to Light : Dusk";  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=100.0; float UIStep=0.001;> = {1.0};
int		Empty05							<string UIName="     ";		string UIWidget="Spinner";float UIMin=0;float UIMax=0;> = {0};

float 	AdaptationSensitivity_Night 	<string UIName="Sensitivity to Light : Night";  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=1.0; float UIStep=0.001;> = {0.5};
float 	AdaptationMin_Night 			<string UIName="Max.Brighten: 	Adaptation to Shadow : Night";  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=100.0; float UIStep=0.001;> = {0.0};
float 	AdaptationMax_Night 			<string UIName="Max.Darken: 	Adaptation to Light : Night";  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=100.0; float UIStep=0.001;> = {1.0};
int		Empty06							<string UIName="      ";		string UIWidget="Spinner";float UIMin=0;float UIMax=0;> = {0};

float 	AdaptationSensitivity_Interior 	<string UIName="Sensitivity to Light : Interior";  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=1.0; float UIStep=0.001;> = {0.5};
float 	AdaptationMin_Interior 			<string UIName="Max.Brighten: 	Adaptation to Shadow : Interior";  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=100.0; float UIStep=0.001;> = {0.0};
float 	AdaptationMax_Interior 			<string UIName="Max.Darken: 	Adaptation to Light : Interior";  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=100.0; float UIStep=0.001;> = {1.0};
int		Empty07							<string UIName="       ";		string UIWidget="Spinner";float UIMin=0;float UIMax=0;> = {0};

float 	AdaptationTime 					<string UIName="Time Multiplier to Adapt the Exposure";  string UIWidget="Spinner";  float UIMin=0.1;  float UIMax=10.0; float UIStep=0.1;> = {1.0};


/* 
#define AdaptationSensitivityDawn 	0.35
#define AdaptationMinDawn 			2.0
#define AdaptationMaxDawn 			1.0

#define AdaptationSensitivitySR 	1.0
#define AdaptationMinSR 			1.0
#define AdaptationMaxSR 			0.9

#define AdaptationSensitivityD 		1.2
#define AdaptationMinD 				1.0
#define AdaptationMaxD 				1.0

#define AdaptationSensitivitySS 	0.8
#define AdaptationMinSS 			1.2
#define AdaptationMaxSS 			1.0

#define AdaptationSensitivityDusk 	0.75
#define AdaptationMinDusk 			2.0
#define AdaptationMaxDusk 			1.0

#define AdaptationSensitivityN 		1.0
#define AdaptationMinN 				10.0
#define AdaptationMaxN 				5.0

#define AdaptationSensitivityI 		0.5
#define AdaptationMinI 				0.5
#define AdaptationMaxI 				0.75

#define AdaptationTime 				0.7
 */



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

//// TODIE HELPER MACRO
static const float timeweight() {
    return TimeOfDay1.x +
           TimeOfDay1.y +
           TimeOfDay1.z +
           TimeOfDay1.w +
           TimeOfDay2.x +
           TimeOfDay2.y;
}

#define TOD(a) \
  ((TimeOfDay1.x * a##_Dawn    + \
    TimeOfDay1.y * a##_Sunrise + \
    TimeOfDay1.z * a##_Day     + \
    TimeOfDay1.w * a##_Sunset  + \
    TimeOfDay2.x * a##_Dusk    + \
    TimeOfDay2.y * a##_Night) / timeweight())

#define TODIE(a) lerp( TOD(a), a##_Interior, EInteriorFactor )

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
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



//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//output size is 16*16
//TextureCurrent size is 256*256, it's internally downscaled from full screen
//input texture is R16G16B16A16 or R11G11B10 float format (alpha ignored)
//output texture is R32 float format (red channel only)
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
float4	PS_Downsample(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
	float4	res;

	float2	pos;
	float2	coord;
	float4	curr=0.0;
	float4	currmax=0.0;
	const float	scale=1.0/16.0;
	const float	step=1.0/16.0;
	const float	halfstep=0.5/16.0;
	pos.x=-0.5+halfstep;
	for (int x=0; x<16; x++)
	{
		pos.y=-0.5+halfstep;
		for (int y=0; y<16; y++)
		{
			coord=pos.xy * scale;
			float4	tempcurr=TextureCurrent.Sample(Sampler0, IN.txcoord0.xy + coord.xy);
			currmax=max(currmax, tempcurr);
			curr+=tempcurr;

			pos.y+=step;
		}
		pos.x+=step;
	}
	curr*=1.0/(16.0*16.0);

	res=curr;

	res=max(res.x, max(res.y, res.z));

	res.w=1.0;
	return res;
}



//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//output size is 1*1
//TexturePrevious size is 1*1
//TextureCurrent size is 16*16
//output and input textures are R32 float format (red channel only)
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
float4	PS_Adaptation(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
	float4	res;

	float	prev=TexturePrevious.Sample(Sampler0, IN.txcoord0.xy).x;

	float2	pos;
	float	curr=0.0;
	float	currmax=0.0;
	const float	step=1.0/16.0;
	const float	halfstep=0.5/16.0;
	float	valmax;
	float	valcut;


	pos.x=-0.5+halfstep;
	for (int x=0; x<16; x++)
	{
		pos.y=-0.5+halfstep;
		for (int y=0; y<16; y++)
		{
			float	tempcurr=TextureCurrent.Sample(Sampler0, IN.txcoord0.xy + pos.xy).x;
			currmax=max(currmax, tempcurr);
			curr+=tempcurr;

			pos.y+=step;
		}
		pos.x+=step;
	}
	curr*=1.0/(16.0*16.0);

	 if (ENABLE_ADAPT) {

		//adjust sensitivity to small bright areas on the screen 
		curr=lerp(curr, currmax, TODIE(AdaptationSensitivity)); //AdaptationSensitivity // AdaptationParameters.z

		//smooth by time
		res=lerp(prev, curr, AdaptationParameters.w*AdaptationTime); //AdaptationTime with elapsed time // AdaptationParameters.w

		//clamp to avoid bugs in post process shader, which have much lower floating point precision
		res=max(res, 0.001);
		res=min(res, 16384.0);

		valmax=max(res.x, max(res.y, res.z));

		valcut=max(valmax, TODIE(AdaptationMin)); //AdaptationMin // AdaptationParameters.x
		valcut=min(valcut, TODIE(AdaptationMax)); //AdaptationMax // AdaptationParameters.y
		
		res*=valcut/(valmax + 0.000000001f);

	}
	 else 
	{
		//adjust sensitivity to small bright areas on the screen  
		curr=lerp(curr, currmax, AdaptationParameters.z); //AdaptationSensitivity // AdaptationParameters.z

		//smooth by time
		res=lerp(prev, curr, AdaptationParameters.w); //AdaptationTime with elapsed time // AdaptationParameters.w

		//clamp to avoid bugs in post process shader, which have much lower floating point precision
		res=max(res, 0.001);
		res=min(res, 16384.0);

		//limit value if ForceMinMaxValues=true

		valmax=max(res.x, max(res.y, res.z));
		valcut=max(valmax, AdaptationParameters.x); //AdaptationMin // AdaptationParameters.x
		valcut=min(valcut, AdaptationParameters.y); //AdaptationMax // AdaptationParameters.y
		res*=valcut/(valmax + 0.000000001f);
	} 

	res.w=1.0;
	return res;
}



//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//techniques
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//first pass for downscaling and computing sensitivity
technique11 Downsample
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_Quad(0.0, 0.0)));
		SetPixelShader(CompileShader(ps_5_0, PS_Downsample()));
	}
}

//last pass for mixing everything
technique11 Draw <string UIName="NAT: Exposure";>
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_Quad(0.0, 0.0)));
		SetPixelShader(CompileShader(ps_5_0, PS_Adaptation()));
	}
}

