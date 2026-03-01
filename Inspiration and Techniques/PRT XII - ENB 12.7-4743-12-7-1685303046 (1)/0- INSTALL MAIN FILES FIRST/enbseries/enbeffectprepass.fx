/*////////////////////////////////////////////////////////
//                ENBSeries effect file                 //
//         visit http://enbdev.com for updates          //
//       Copyright (c) 2007-2019 Boris Vorontsov        //
//----------------------ENB PRESET----------------------//
//						NAT.ENB					    	//                                                 
//-----------------------CREDITS------------------------//
//     Please do not redistribute without credits       //
// Boris: For ENBSeries and his knowledge and codes     //
// LonelyKitsuune / T.Thanner: Author of Snow Code      //
// L00 :  Shader Setup, Presets and Settings,           //
//        and Modification of Shaders     		    	//
//														//
//     Please do not redistribute without credits      	//
////////////////////////////////////////////////////////*/

/* float3 SnowColorDawn			<string UIName="Snow Color:Dawn";		string UIWidget="Color";		   > = {0.929, 0.973, 1};
float3 SnowColorSunrise			<string UIName="Snow Color:Sunrise";	string UIWidget="Color";		   > = {0.929, 0.973, 1};
float3 SnowColorDay				<string UIName="Snow Color:Day";		string UIWidget="Color";		   > = {0.929, 0.973, 1};
float3 SnowColorSunset			<string UIName="Snow Color:Sunset";		string UIWidget="Color";		   > = {0.929, 0.973, 1};
float3 SnowColorDusk			<string UIName="Snow Color:Dusk";		string UIWidget="Color";		   > = {0.929, 0.973, 1};
float3 SnowColorNight			<string UIName="Snow Color:Night";		string UIWidget="Color";		   > = {0.114, 0.137, 0.153}; */

#define SnowColorDawn 		float3(0.0588, 0.529, 1)
#define SnowColorSunrise 	float3(0.294, 0.647, 1)
#define SnowColorDay 		float3(1.0, 1.0, 1.0) //float3(0.645, 0.806, 0.967)
#define SnowColorSunset 	float3(0.294, 0.647, 1)
#define SnowColorDusk 		float3(0.137, 0.569, 1)
#define SnowColorNight 		float3(0.0588, 0.529, 1)

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

#define SNOWY_WEATHERS_START 	48
#define SNOWSTORM 				51
#define SNOWY_WEATHERS_END 		51

#define UISC_Day_Intensity 		1.0
#define UISC_Night_Intensity 	0.6

#define UISC_SnowyWeatherAmount 7.5
#define UISC_SnowStormAmount 	8.0

#define snowDepth 				1000.0
#define snowStormDepth 			12.0

#define K_LUM					float3(0.0,   0.0,   0.0)
#define DELTA					1e-6
#define PI						3.1415926535897932384626433832795
#define NI 						nointerpolation

static const float2 PixelSize = float2(ScreenSize.y, ScreenSize.y * ScreenSize.z);
static const float2 ScreenRes = float2(ScreenSize.x, ScreenSize.x * ScreenSize.w); //(Width,Height)

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
float4 	SunDirection;


//+++++++++++++++++++++++++++++
//mod parameters, do not modify
//+++++++++++++++++++++++++++++
Texture2D			TextureOriginal; //color R16B16G16A16 64 bit hdr format
Texture2D			TextureColor; //color which is output of previous technique (except when drawed to temporary render target), R16B16G16A16 64 bit hdr format
Texture2D			TextureDepth; //scene depth R32F 32 bit hdr format
Texture2D			TextureJitter; //blue noise
Texture2D			TextureMask; //alpha channel is mask for skinned objects (less than 1) and amount of sss

//temporary textures which can be set as render target for techniques via annotations like <string RenderTarget="RenderTargetRGBA32";>
Texture2D			RenderTargetRGBA32; //R8G8B8A8 32 bit ldr format
Texture2D			RenderTargetRGBA64; //R16B16G16A16 64 bit ldr format
Texture2D			RenderTargetRGBA64F; //R16B16G16A16F 64 bit hdr format
Texture2D			RenderTargetR16F; //R16F 16 bit hdr format with red channel only
Texture2D			RenderTargetR32F; //R32F 32 bit hdr format with red channel only
Texture2D			RenderTargetRGB32F; //32 bit hdr format without alpha

Texture2D 			TextureNormal;


struct VertexShaderInput
	{
		float3 pos     : POSITION;
		float2 txcoord : TEXCOORD0;
	};

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

struct VertexShaderOutputSnow
{
	float4 pos			: SV_POSITION;
	float2 texcoord		: TEXCOORD0;
NI	bool   EnableSnow	: PSINFO0;
NI	float3 SnowColor	: PSINFO1;
NI	float  SnowAmount   : PSINFO3;
NI	float  SnowInt		: PSINFO4;
NI  float3 SnowIncVec	: PSINFO6;

};

struct VertexShaderOutputSnowGauss
{
	float4 pos			: SV_POSITION;
	float2 texcoord		: TEXCOORD0;
NI	float  Sigma        : TEXCOORD1;
NI	float  WeightFactor	: TEXCOORD2;
NI	float  LoopCount	: PSINFO0;
NI	float  OffsetScale	: PSINFO1;
};


// HELPER

static const float SqrtTwoPI = sqrt(2.0 * PI);

float  FastLinDepth(float  Depth, float Far)
{ return Depth / mad(-Depth, Far, Far + 1.0); }

float  LinearStep(float  Low, float  Up, float  x)
{ return saturate((x - Low) / (Up - Low)); }

float2 LinearStep(float2 Low, float2 Up, float2 x)
{ return saturate((x - Low) / (Up - Low)); }

float3 LinearStep(float3 Low, float3 Up, float3 x)
{ return saturate((x - Low) / (Up - Low)); }

float Random(float2 coord)
{ return abs(frac(sin(dot(coord, float2(25.9796, 156.466))) * 43758.5453)); }

float4 RandomF4(float4 seed)
{ return abs(frac(sin(seed * float4(25.9796, 156.466, 78.233, 51.9592)) * 43758.5453)); }

float RandomGauss(float2 Coords)
{
	float4 Noise  = { 0.0, 0.25, 0.5, 0.75 };
	       Noise += dot(Coords, Random(Coords));
	return dot(RandomF4(Noise), 0.25);
}

//FUNCTIONS

float WeatherToEffectStrength_SC(float Outgoing, float Incoming, float WeatherTran, float Step)
{
	float2 Weather    = { Outgoing, Incoming };
	float2 Transition = { WeatherTran, 1.0 - WeatherTran };
	
	Transition = saturate(Transition - Step) * rcp(1.0 - Step);
	Transition = lerp(Weather.xy, Weather.yx, Transition);
	
	return (Incoming >= Outgoing) ? Transition.x : Transition.y;
}

float GetVirtualSunZenith()
{
	float MidndightZenith = saturate(1.0 - Weather.w / 5.0); // 0-5 O'Clock
	MidndightZenith += saturate((Weather.w - 19.0) / 5.0); // 19-24 O'Clock
	
	float NoonZenith = TimeOfDay1.z;
	
	return saturate(MidndightZenith + NoonZenith * NoonZenith);
}

float GetCorrectedSkinMask(float2 Coords, float LinDepth)
{
	float  SkinMask  = TextureMask.Sample(Point_Sampler, Coords).w;
	       SkinMask  = pow(saturate(1.0 - SkinMask),4);
	       SkinMask *= saturate(1.0 - LinDepth * 15.0);
	return SkinMask > 0.1;
}


//----------------------------------------------------------------------------------------------//
//										    Shaders												//
//																								//
//----------------------------------------------------------------------------------------------//

VertexShaderOutputSnow VS_SnowCover(VertexShaderInput IN)
{
	VertexShaderOutputSnow OUT;

	OUT.pos			= float4(IN.pos.xyz, 1.0);
	OUT.texcoord.xy = IN.txcoord.xy;
	
	const float IncomingTransitionStep = 0.7;
	const float OutgoingTransitionStep = 0.2;
	
	const float3 SnowIncidentVec = { 0.0, -1.0, 0.0 };
	const float3 SkyDirectionVec = { 0.0,  1.0, 0.0 };
	
	float   timeweight=0.000001;
	float   timevalue=0.0;
	
	timevalue+=TimeOfDay1.x * SnowColorDawn;
	timevalue+=TimeOfDay1.y * SnowColorSunrise;
	timevalue+=TimeOfDay1.z * SnowColorDay;
	timevalue+=TimeOfDay1.w * SnowColorSunset;
	timevalue+=TimeOfDay2.x * SnowColorDusk;
	timevalue+=TimeOfDay2.y * SnowColorNight;

	timeweight+=TimeOfDay1.x;
	timeweight+=TimeOfDay1.y;
	timeweight+=TimeOfDay1.z;
	timeweight+=TimeOfDay1.w;
	timeweight+=TimeOfDay2.x;
	timeweight+=TimeOfDay2.y;

	
	if (!EInteriorFactor) 
	{
		//Determine snow strength
		float  IncomingSnow, OutgoingSnow;
		float2(IncomingSnow, OutgoingSnow) =
		(Weather.xy >= SNOWY_WEATHERS_START && Weather.xy <= SNOWY_WEATHERS_END) +
		(Weather.xy == SNOWSTORM);
		
		
		float TransitionStep = (IncomingSnow > OutgoingSnow) ? IncomingTransitionStep : OutgoingTransitionStep;
		float SnowWeather    = WeatherToEffectStrength_SC(OutgoingSnow, IncomingSnow, Weather.z, TransitionStep);
		OUT.EnableSnow       = SnowWeather > 0.0;
		
		if(OUT.EnableSnow)
		{
			float SnowStormStrength   = saturate(SnowWeather - 1.0);
			float SnowWeatherStrength = saturate(SnowWeather);
			
			float ZenithWeight = GetVirtualSunZenith();
			float3 SunDir = { -SunDirection.xy, SunDirection.z };
			
			//Tries to "anchor" the incident vector with the sun direction when possible
			//to avoid moving the snow layer when changing the camera angle
			OUT.SnowIncVec = lerp( SnowIncidentVec, SunDir, ZenithWeight);

			OUT.SnowAmount = lerp(UISC_SnowyWeatherAmount, UISC_SnowStormAmount, SnowStormStrength);
			OUT.SnowAmount = 10.0 + DELTA - OUT.SnowAmount * SnowWeatherStrength;
			
			OUT.SnowColor   = lerp( (timevalue / timeweight), 0.0, EInteriorFactor );
			
			OUT.SnowInt  = lerp(UISC_Night_Intensity, UISC_Day_Intensity, ENightDayFactor);
			OUT.SnowInt *= pow(SnowWeatherStrength, 0.1);
		}
	}

	return OUT;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
VS_OUTPUT_POST	VS_PostProcess(VS_INPUT_POST IN)
{
	VS_OUTPUT_POST	OUT;
	float4	pos;
	pos.xyz=IN.pos.xyz;
	pos.w=1.0;
	OUT.pos=pos;
	OUT.txcoord0.xy=IN.txcoord.xy;
	return OUT;
}

float4 PS_SnowCover(VertexShaderOutputSnow IN) : SV_Target
{
	const uint   DistributionSeed  = 1;
	const float3 EyeIncidentVec    = { 0.0, 0.0, -1.0 };
	const float  MaxFPSWeaponDepth = 0.00003468;
	
	float4 Color = TextureColor.Sample(Point_Sampler, IN.texcoord);

	if (!EInteriorFactor)
	{
		if(!IN.EnableSnow) return float4(Color.rgb, 0.0);
		
		float3 SSNormal = TextureNormal.Sample(Point_Sampler, IN.texcoord).xyz;
		       SSNormal = SSNormal * float3(-2.0, -2.0, 2.0) + float3(1.0, 1.0, -1.0);
		
		float SnowAmount     = saturate(dot(IN.SnowIncVec, SSNormal));

		SnowAmount      = pow(SnowAmount * IN.SnowInt, IN.SnowAmount);

		//Remove snow from bright pixels ("fixes" fires for example)
		//SnowAmount *= saturate(1.0 - pow(dot(Color.rgb, K_LUM),2));
		
		//Depth fade and fps weapon removal
		float Depth = TextureDepth.Sample(Point_Sampler, IN.texcoord).x;
		Depth       = FastLinDepth(Depth, lerp(snowDepth, snowStormDepth, (Weather.xy==SNOWSTORM)));
		SnowAmount *= LinearStep(lerp(0.25,1.0,ENightDayFactor), 0.1, Depth);
		SnowAmount *= Depth > MaxFPSWeaponDepth;
		
		
		//Skin control

		float SkinMask = GetCorrectedSkinMask(IN.texcoord, Depth);

		SnowAmount *= lerp(1.0, 0.1, SkinMask);
		
		float Noise = RandomGauss(floor(SSNormal.xy * 10.0 + DistributionSeed));
		SnowAmount *= (Noise * 2.0 - 1.0) * 0.15 + 1.0;
		
		Color.rgb = lerp(Color.rgb, lerp(IN.SnowColor, 0.0, 0.0), SnowAmount);
		
		return float4(Color.rgb, saturate(SnowAmount * 4.0));
	}
	return Color;
}

float4 PS_SnowCoverBilateralGauss(VertexShaderOutputSnow IN, uniform float4 Axis) : SV_Target
{
	//  Bilateral filtering on a two pass gaussian variant isn't very accurate,
	//  but the performance hit from a higher quality single pass version
	//  isn't worth it when using bigger kernels
	
	float4 CenterTap   = TextureColor.Sample(Point_Sampler, IN.texcoord);

	if (!EInteriorFactor)
	{
		float  Sigma       = 1.0 * CenterTap.w + DELTA;
		float3 BlurredSnow = CenterTap.rgb;
		
		[branch] if(Sigma > 0.666)
		{
			float WeightFactor =  rcp(Sigma * SqrtTwoPI);
			float LoopCount    = ceil(Sigma * 1.5 - 0.01);
				  Sigma        =  rcp(Sigma * Sigma);
			
			float4 StepSize     = PixelSize.xyxy * Axis;
			float4 HalfStepSize = StepSize * 0.25;
			
			float WeightSum    = WeightFactor;
			      BlurredSnow *= WeightFactor;
			
			[loop] for(float i=1.0; i <= LoopCount; i++)
			{
				float  GaussWeight = WeightFactor * exp(i*i*-Sigma);
				float4 CurrOffset  = IN.texcoord.xyxy + i * StepSize - HalfStepSize;
				
				float4 CurrSample;
				float  CurrWeight;
				
				CurrSample  = TextureColor.SampleLevel(Linear_Sampler, CurrOffset.xy,0);
				CurrWeight  = saturate(1.0 - sqrt(distance(CenterTap.rgb, CurrSample.rgb) * 1.5));
				CurrWeight *= GaussWeight * CurrSample.w;
				
				BlurredSnow += CurrWeight * CurrSample;
				WeightSum   += CurrWeight;
				
				
				CurrSample  = TextureColor.SampleLevel(Linear_Sampler, CurrOffset.zw,0);
				CurrWeight  = saturate(1.0 - sqrt(distance(CenterTap.rgb, CurrSample.rgb) * 1.5));
				CurrWeight *= GaussWeight * CurrSample.w;
				
				BlurredSnow += CurrWeight * CurrSample;
				WeightSum   += CurrWeight;
			}
			
			BlurredSnow /= WeightSum;
		}
		

			float AlphaOut = 1.0;

		
		return float4(BlurredSnow, (Axis.x == 0.0) ? AlphaOut : CenterTap.w);
	}
	return CenterTap;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Techniques are drawn one after another and they use the result of
// the previous technique as input color to the next one.  The number
// of techniques is limited to 255.  If UIName is specified, then it
// is a base technique which may have extra techniques with indexing
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

technique11 Snow <string UIName="NAT: Kitsuune.Snow";>
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_SnowCover()));
		SetPixelShader(CompileShader(ps_5_0, PS_SnowCover()));
	}
}

technique11 Snow1
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_SnowCover()));
		SetPixelShader(CompileShader(ps_5_0, PS_SnowCoverBilateralGauss(float4(2.0, 0.0, -2.0, 0.0))));
	}
}

technique11 Snow2
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_SnowCover()));
		SetPixelShader(CompileShader(ps_5_0, PS_SnowCoverBilateralGauss(float4(0.0, 2.0, 0.0, -2.0))));
	}
}


