//----------------------------------------------------------------------------------------------//
//								    HISTOGRAM BASED ADAPTATION									//
//----------------------------------------------------------------------------------------------//
//																								//
//				       enbadaption.fx file for ENB (DirectX 11 Shader Model 5)					//
//																								//
//----------------------------------------------------------------------------------------------//
//																								//
//				Adaption file is based on kingeric1992's Histogram based adaptation				//
//																								//
//										     Source:											//
//				    http://enbseries.enbdev.com/forum/viewtopic.php?f=7&t=5321 					//
//																								//
//										    Reference: 											//
//https://docs.unrealengine.com/latest/INT/Engine/Rendering/PostProcessEffects/AutomaticExposure//
//																								//
//----------------------------------------------------------------------------------------------//
//								==================================								//
//								//     Silent Horizons ENB      //								//
//								//								//								//
//								//		by LonelyKitsuune		//								//
//								==================================								//
//----------------------------------------------------------------------------------------------//

//----------------------------------------------------------------------------------------------//
//										Internal Parameters										//
//----------------------------------------------------------------------------------------------//

#include "UI/enbUI_Primer.fxh"

UI_FileHeaderLong(">>>        Histogram based Adaption        <<<",
				  ">>>                by Kingeric1992                <<<")
UI_WHITESPACE(1)
UI_ELEMENT(_AdaptInfo,			   "|Adaption Time is controlled by enbseries.ini")
UI_SPECIAL_WHITESPACE(1)
float Bias 		    <string UIName="|- Auto Exposure Bias";										  > = { 0.00 };
float MaxBrightness <string UIName="|- Adapt Max Brightness"; float UIMin= -9.0; float UIMax=3.0; > = { 1.00 };
float MinBrightness <string UIName="|- Adapt Min Brightness"; float UIMin= -9.0; float UIMax=3.0; > = {-4.00 };
float LowPercent    <string UIName="|- Adapt Low  Percent";   float UIMin=  0.5; float UIMax=1.0; > = { 0.80 };
float HighPercent   <string UIName="|- Adapt High Percent";   float UIMin=  0.5; float UIMax=1.0; > = { 0.95 };


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
//						     Game and mod parameters, do not modify								//
//																								//
//----------------------------------------------------------------------------------------------//
float4    AdaptationParameters;		//x = AdaptationMin, y = AdaptationMax, z = AdaptationSensitivity, w = AdaptationTime multiplied by time elapsed
Texture2D TextureCurrent;
Texture2D TexturePrevious;

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

#include "Helper/enbHelper_Common.fxh"
//Helper with some useful macros, variables and functions


//----------------------------------------------------------------------------------------------//
//										      Shaders											//
//																								//
//----------------------------------------------------------------------------------------------//

void VS_Quad(inout float4 pos : SV_POSITION, inout float2 txcoord0 : TEXCOORD0)
{
    pos.w     = 1.0;
    txcoord0 -= 7.0 / 256.0;
}

//------------------------------------------------------------------------------
//output size is 16*16
//TextureCurrent size is 256*256, it's internally downscaled from full screen
//input texture is R16G16B16A16 or R11G11B10 float format (alpha ignored)
//output texture is R32 float format (red channel only)


float4 PS_Downsample( float4 pos : SV_POSITION, float2 txcoord0 : TEXCOORD0) : SV_Target
{
		float  res = 0.0;
		float4 coord = { txcoord0.xyy, 1.0 / 128.0 };

		for (int x=0; x<8; x++)
		{
				coord.y = coord.z;
				for (int y=0; y<8; y++)
				{
						float4 color = TextureCurrent.Sample(Linear_Sampler, coord.xy);
						res     += max3(color);
						coord.y += coord.w;
				}
				coord.x += coord.w;
		}

		return log2(res) - 6.0; //log2( res / 64.0)
}

//------------------------------------------------------------------------------
//output size is 1*1
//TexturePrevious size is 1*1
//TextureCurrent size is 16*16
//output and input textures are R32 float format (red channel only)


float4 PS_Histogram() : SV_Target
{
    float4 coord = { 1.0 / 32.0, 1.0 / 32.0, 1.0 / 32.0, 1.0 / 16.0};
    float4 bin[16];

    for(int k=0; k<16; k++)
    {
        bin[k]=float4(0.0, 0.0, 0.0, 0.0);
    }

    [loop] for(int i=0; i < 16.0; i++)
    {
        coord.y  = coord.z;
        [loop] for(int j=0; j<16.0; j++)
        {
            float  color   = TextureCurrent.SampleLevel(Point_Sampler, coord.xy, 0.0).r;
            float  level   = saturate(( color + 9.0 ) / 12) * 63; // [-9, 3]
            bin[ level * 0.25 ] += float4(0.0, 1.0, 2.0, 3.0) == float4(trunc(level % 4).xxxx); //bitwise ?
            coord.y  += coord.w;
        }
        coord.x += coord.w;
    }

    
    
    float2 adaptAnchor = 0.5; //.x = high, .y = low
    float2 accumulate  = float2( HighPercent - 1.0, LowPercent - 1.0) * 256.0;

    [loop]
    for(int l=15; l>0; l--)
    {
        accumulate += bin[l].w;
        adaptAnchor = (accumulate.xy < bin[l].ww)? l * 4.0 + accumulate.xy / bin[l].ww + 3.0: adaptAnchor;

        accumulate += bin[l].z;
        adaptAnchor = (accumulate.xy < bin[l].zz)? l * 4.0 + accumulate.xy / bin[l].zz + 2.0: adaptAnchor;

        accumulate += bin[l].y;
        adaptAnchor = (accumulate.xy < bin[l].yy)? l * 4.0 + accumulate.xy / bin[l].yy + 1.0: adaptAnchor;

        accumulate += bin[l].x;
        adaptAnchor = (accumulate.xy < bin[l].xx)? l * 4.0 + accumulate.xy / bin[l].xx + 0.0: adaptAnchor;
    }

    
    float adapt = (adaptAnchor.x + adaptAnchor.y) * 0.5 / 63.0  * 12.0 - 9.0;
          adapt =  pow(2.0, clamp( adapt, MinBrightness, MaxBrightness) + Bias);  // min max on log2 scale

   		return lerp(TexturePrevious.Sample(Point_Sampler, 0.5).x, adapt, AdaptationParameters.w);
}


//----------------------------------------------------------------------------------------------//
//										   Techniques											//
//																								//
//----------------------------------------------------------------------------------------------//

TECH11 (Downsample,										 VS_Quad(), PS_Downsample())
TECH11 (Draw <string UIName="Adaption - Kingeric1992";>, VS_Quad(), PS_Histogram())






