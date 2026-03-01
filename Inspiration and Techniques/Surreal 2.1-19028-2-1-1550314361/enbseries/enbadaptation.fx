//================= V2.0 ===================//
//     _____                            __  //
//    / ___/__  _______________  ____ _/ /  //
//    \__ \/ / / / ___/ ___/ _ \/ __ `/ /   //
//   ___/ / /_/ / /  / /  /  __/ /_/ / /    //
//  /____/\__,_/_/  /_/   \___/\__,_/_/     //
//                                          //
//==========================================//
// Histogram Adaptation by kingeric1992     //
// Setup by Adyss                           //
//==========================================//

// Include Needes Values
#include "Shaders/ENBcommon.fxh"
#include "Shaders/ReforgedUI.fxh"


#define UI_CATEGORY HistogramAdaptation
UI_SEPARATOR
UI_FLOAT_TODE_DNI(Bias,           "Auto Exposure Bias",               -100.0, 100.0, 0.0)
UI_WHITESPACE(1)
UI_FLOAT_TODE_DNI(MaxBrightness,  "Adapt Max Brightness",             -9.0,   3.0,   0.8)
UI_WHITESPACE(2)
UI_FLOAT_TODE_DNI(MinBrightness,  "Adapt Min Brightness",             -9.0,   3.0,   0.8)
UI_WHITESPACE(3)
UI_FLOAT_DNI(LowPercent,          "Adapt Low Percent",                 0.5,   1.0,   0.8)
UI_FLOAT_DNI(HighPercent,         "Adapt High Percent",                0.5,   1.0,   0.95)


//========================================//
// game and mod parameters, do not modify //
//========================================//
float4				AdaptationParameters; //x = AdaptationMin, y = AdaptationMax, z = AdaptationSensitivity, w = AdaptationTime multiplied by time elapsed
Texture2D			TextureCurrent;
Texture2D			TexturePrevious;


void VS_Quad( inout float4 pos : SV_POSITION, inout float2 txcoord0 : TEXCOORD0)
{
    pos.w     = 1.0;
    txcoord0 -= 7.0 / 256.0;
}

//=============================================================================//
//output size is 16*16                                                         //
//TextureCurrent size is 256*256, it's internally downscaled from full screen  //
//input texture is R16G16B16A16 or R11G11B10 float format (alpha ignored)      //
//output texture is R32 float format (red channel only)                        //  
//=============================================================================//

float4	PS_Downsample( float4 pos : SV_POSITION, float2 txcoord0 : TEXCOORD0) : SV_Target
{
	float  res = 0.0;
    float4 coord = { txcoord0.xyy, 1.0 / 128.0 };

	for (int x=0; x<8; x++)
	{
		coord.y = coord.z;
		for (int y=0; y<8; y++)
		{
			float4 color = TextureCurrent.Sample(LinearSampler, coord.xy);
			res     += max( color.r, max(color.g, color.b));
			coord.y += coord.w;
		}
		coord.x += coord.w;
	}

	return log2(res) - 6.0; //log2( res / 64.0)
}

//==================================================================//
//output size is 1*1                                                //
//TexturePrevious size is 1*1                                       //
//TextureCurrent size is 16*16                                      //
//output and input textures are R32 float format (red channel only) //
//==================================================================//

float4	PS_Histogram() : SV_Target
{
    float4 coord = { 1.0 / 32.0, 1.0 / 32.0, 1.0 / 32.0, 1.0 / 16.0};
    float4 bin[16];

    for(int k=0; k<16; k++)
    {
        bin[k]=float4(0.0, 0.0, 0.0, 0.0);
    }

    [loop]
    for(int i=0; i < 16.0; i++)
    {
        coord.y  = coord.z;
        [loop]
        for(int j=0; j<16.0; j++)
        {
            float  color   = TextureCurrent.SampleLevel(PointSampler, coord.xy, 0.0).r;
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

   	return lerp(TexturePrevious.Sample(PointSampler, 0.5).x, adapt, AdaptationParameters.w);
}
//============//
// techniques //
//============//

technique11 Downsample
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
		SetPixelShader(CompileShader(ps_5_0, PS_Downsample()));
	}
}

technique11 Draw  <string UIName="Exposure Correction";>
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
		SetPixelShader(CompileShader(ps_5_0, PS_Histogram()));
	}
}

