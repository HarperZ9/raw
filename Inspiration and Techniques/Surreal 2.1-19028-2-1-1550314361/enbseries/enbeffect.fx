//================= V2.1 ===================//
//     _____                            __  //
//    / ___/__  _______________  ____ _/ /  //
//    \__ \/ / / / ___/ ___/ _ \/ __ `/ /   //
//   ___/ / /_/ / /  / /  /  __/ /_/ / /    //
//  /____/\__,_/_/  /_/   \___/\__,_/_/     //
//                                          //
//==========================================//
// Reforged code by THE SANDVICH MAKER      //
// Micro Bloom by Sonic Ether               //
// ENB by Boris                             //
// Eye Adaptation and Color EQ by Prod80    //
// UC2 code assembling by JawZ              //
// Shader Setup and Tweaks by Adyss         //
//==========================================//

//========================================//
// Game and mod parameters, do not modify //
//========================================//
float4				Params01[7];       //skyrimse parameters
float4				ENBParams01;       //enb parameters

Texture2D			TextureColor;      //hdr color
Texture2D			TextureBloom;      //vanilla or enb bloom
Texture2D			TextureLens;       //enb lens fx
Texture2D			TextureDepth;      //scene depth
Texture2D			TextureAdaptation; //vanilla or enb adaptation
Texture2D			TextureAperture;   //this frame aperture 1*1 R32F hdr red channel only. computed in depth of field shader file


// Include Needes Values
#include "Shaders/ENBcommon.fxh"
#include "Shaders/Globals.fxh"
#include "Shaders/ReforgedUI.fxh"

// UI
UI_SEPARATOR_CUSTOM                    ("\xD7 Surreal V2.0 \xD7")
UI_WHITESPACE(1)
UI_INT(WeatherSet,                     "Select Weather Patch",      0.0, 8.0, 0.0)
UI_MESSAGE(w1,                         "0 = NAT, 1 = Dolomite, 2 = CoT")
UI_MESSAGE(w2,                         "3 = Vivid, 4 = True Storms, 5 = Obsidian")
UI_MESSAGE(w3,                         "6 = Aequinoctium, 7 = Mythical, 8 = Rustic")
UI_WHITESPACE(2)
#define UI_CATEGORY Tonemap
UI_SEPARATOR
UI_FLOAT_DNI(KeyValue,                  "Exposure Scale",           0.0, 10.0, 1.0)
UI_FLOAT_DNI(Contrast,                  "Contrast",                 0.0, 5.0, 1.0)
UI_FLOAT_DNI(Gamma,                     "Gamma",                    0.0, 5.0, 1.0)
UI_FLOAT(satLimit,                      "Saturation Limit",         0.0, 5.0, 0.8)
UI_FLOAT_DNI(ToneMappingOversaturation, "Oversaturation",           0.0, 500.0, 180.0)
UI_FLOAT_DNI(ToneMappingCurve,          "ToneMapping Curve",        0.0, 50.0, 5.0)
UI_FLOAT3_DNI(ColorDesat,               "Color Deaturation",        1.0, 1.0, 1.0)
UI_FLOAT_FINE_DNI(fxcolorMix,           "Equalizer Intensity",      0.0, 1.0,  0.1, 0.001)
UI_WHITESPACE(3)
#define UI_CATEGORY DaytimeColors
UI_SEPARATOR
UI_FLOAT_TODE_DNI(Saturation,           "Saturation",               0.0, 10.0, 2.0)
UI_FLOAT_TODE_DNI(hueMid,               "Hue",                      0.0, 1.0, 0.5)
UI_FLOAT_TODE_DNI(tStrength,            "Tint Power",               0.0, 1.0, 0.5)
UI_FLOAT3_TODE_DNI(Tint,                "Tint",                     1.0, 1.0, 1.0)
UI_WHITESPACE(4)
#define UI_CATEGORY Adaptation
UI_SEPARATOR
UI_FLOAT_FINE_DNI(minAdapt,             "Adaptation: Min",          0.0, 10.0, 0.025, 0.001)
UI_FLOAT_FINE_DNI(maxAdapt,             "Adaptation: Max",          0.0, 10.0, 0.011, 0.001)
UI_FLOAT_DNI(middleGray,                "Adaptation: Middle Grey",  0.0, 2.0, 0.6)
UI_FLOAT_DNI(maxLuma,                   "Max Luminosity",           0.0, 16.0, 16.0)
UI_WHITESPACE(5)
#define UI_CATEGORY Uncharted2DTonemap
UI_SEPARATOR
UI_FLOAT_DNI(A,                         "Shoulder Strength",        0.0, 2.0,  0.2)
UI_FLOAT_DNI(B,                         "Linear Strength",          0.0, 5.0,  0.3)
UI_FLOAT_FINE_DNI(C,                    "Linear Angle",             0.0, 1.0,  0.1, 0.001)
UI_FLOAT_DNI(D,                         "Toe Strength",             0.0, 2.0,  0.2)
UI_FLOAT_FINE_DNI(E,                    "Toe Numerator",            0.0, 0.5,  0.1, 0.001)
UI_FLOAT_FINE_DNI(F,                    "Toe Denominator",          0.0, 2.0,  0.3, 0.001)
UI_FLOAT_DNI(LinearWhite,               "Linear White",             0.0, 20.0, 10.0)

//===========//
// Functions //
//===========//

#include "Shaders/MicroBloom.fxh"
#include "Shaders/BloomBlend.fxh"
#include "Shaders/ColorEQ.fxh"
#include "Shaders/UC2.fxh"
#include "Shaders/WeatherPatches.fxh"

//==============//
// Pixel Shader //
//==============//

float3 Tonemap( float3 x )
{
    float a = 0.010;
    float b = 0.132;
    float c = 0.010;
    float d = 0.163;
    float e = 0.101;

    return ( x * ( a * x + b ) ) / ( x * ( c * x + d ) + e );
}

float4	PS_SendNudes(VS_OUTPUT IN) : SV_Target
{
	float2 coord = IN.txcoord.xy;
	float4 Color = TextureColor.Sample(PointSampler, coord); //Can someone explain me whats the diffrence between point and linear? I thought i understood it but it has no visual diffrence
	float4 Bloom = TextureBloom.Sample(LinearSampler, coord) * ENBParams01.x;
    float4 Adapt = TextureAdaptation.Sample(LinearSampler, coord);
    float3 Lens  = TextureLens.Sample(LinearSampler, coord).rgb * ENBParams01.y;



	Color = Color / (1 + Color);

	Color.rgb = Tonemap(Color);

    Color.rgb = ldexp(Color, KeyValue);

    Color.rgb = HDRScreen(Color, saturate(Bloom));

    Color.rgb = WeatherPatch(Color);

    // AGCC without contrast and brightness cuz it made my image look shit. But the rest made quite some nice lookig impact + Nighteye and stuff like that :3
	float  saturation  = Params01[3].x;         // 0 == gray scale
    float3 tint_color  = Params01[4].rgb;       // tint color
    float  tint_weight = Params01[4].w;         // 0 == no tint
    float3 fade        = Params01[5].xyz;       // fade current scene to specified color, mostly used in special effects
    float  fade_weight = Params01[5].w * 0.1;   // 0 == no fade NOTE: *0.1 cuz in most cases it made my sceen go white

    Color.a   = dot(Color.rgb, LUM_709);
    Color.rgb = lerp(Color.a, Color.rgb, saturation);
    Color.rgb = lerp(Color.rgb, tint_color * Color.a , tint_weight);
    Color.rgb = pow(saturate(Color.rgb), Params01[6].w);
    Color.rgb = lerp(Color.rgb, fade, fade_weight);
    Color.a = 1.0;

    // Channel Desat
	float greyscale = dot(Color.xyz, float3(0.3, 0.59, 0.11));
    Color.r = lerp(greyscale, Color.r, ColorDesat.r);
    Color.g = lerp(greyscale, Color.g, ColorDesat.g);
    Color.b = lerp(greyscale, Color.b, ColorDesat.b);

	Color = pow(Color, Gamma);

    float AdaptBypass = Adapt.x; // Before altering .x catch it and then restore it later.

    Adapt.x = max(Adapt.x, 0.0);
	Adapt.x = min(Adapt.x, 50.0);
	Color.xyz = Color.xyz / (Adapt.x * maxAdapt + minAdapt);

    Adapt.x = AdaptBypass; //restore .x for next adaptive effects

    // Pord80s Eye Adaptation.
    float EyeAdapt      = grayValue( TextureAdaptation.Sample(LinearSampler, 0.5 ).xyz );
    float pixelLuma     = grayValue( Color.xyz );
    EyeAdapt            = clamp( EyeAdapt, minAdapt, maxAdapt );

    float scaledLuma    = ( pixelLuma * middleGray ) / EyeAdapt;
    float compLuma      = ( scaledLuma * ( 1.0f + ( scaledLuma / ( maxLuma * maxLuma )))) / ( 1.0f + scaledLuma );

    Color.xyz          *= compLuma;

    // PP2
    Color.xyz    += 0.000001;
    float3 xncol  = normalize(Color.xyz);
    float3 scl    = Color.xyz / xncol.xyz;
    scl           = pow(scl, Contrast);
    xncol.xyz     = pow(xncol.xyz, Saturation);
    Color.xyz     = scl * xncol.xyz;

    float lumamax = ToneMappingOversaturation;
    Color.xyz     = (Color.xyz * (1.0 + Color.xyz / lumamax)) / (Color.xyz + ToneMappingCurve);

    Color.rgb = ToneMapOperator(Color);

    Color.rgb = lerp(Color, Color * Tint * 2.55, tStrength);

    Color = ColorEQ(Color);

	//Color = saturate(Color);
    return pow(Color, 1.0/2.2);
}


technique11 IBecameHungryWritingThisFileXD <string UIName="Surreal";>
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
		SetPixelShader (CompileShader(ps_5_0, PS_SendNudes()));
	}
}

// I really have no fucking idea what i am doing XD dont mind me
