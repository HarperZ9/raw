//////////////////////////////////////////////////
//                                              //
//         :::::::::::::::    ::::::    :::     //
//            :+:    :+:+:   :+::+:   :+:       //
//           +:+    :+:+:+  +:++:+  +:+         //
//          +#+    +#+ +:+ +#++#++:++           //
//         +#+    +#+  +#+#+#+#+  +#+           //
//        #+#    #+#   #+#+##+#   #+#           //
//   ##############    #######    ###           //
//                                              //
//           THE ENB GRAPHIC NOVEL              //
//        CREATED WITH LOVE BY TREYM            //
//                                              //
//////////////////////////////////////////////////

//////////////////////////////////////////////////
// Development Variables                        //
//////////////////////////////////////////////////
#define DebugMode 1

//////////////////////////////////////////////////
// External ENB Parameters                      //
//////////////////////////////////////////////////
float4	Timer;           //x = generic timer in range 0..1, period of 16777216 ms (4.6 hours), y = average fps, w = frame time elapsed (in seconds)
float4	ScreenSize;      //x = Width, y = 1/Width, z = Width/Height, w = Height/Width
float	AdaptiveQuality; //changes in range 0..1, 0 means full quality, 1 lowest dynamic quality (0.33, 0.66 are limits for quality levels)
float4	Weather;         //x = current weather index, y = outgoing weather index, z = weather transition, w = time of the day in 24 standart hours.
float4	TimeOfDay1;      //x = dawn, y = sunrise, z = day, w = sunset. Interpolators range from 0..1
float4	TimeOfDay2;      //x = dusk, y = night. Interpolators range from 0..1
float	ENightDayFactor; //changes in range 0..1, 0 means that night time, 1 - day time
float	EInteriorFactor; //changes 0 or 1. 0 means that exterior, 1 - interior
SC_FLOAT2(PixelSize,  {ScreenSize.y, ScreenSize.y * ScreenSize.z}) // As in Reshade
SC_FLOAT2(Resolution, {ScreenSize.x, ScreenSize.x * ScreenSize.w}) // Display Resolution

//////////////////////////////////////////////////
// Debugging ENB Parameters                     //
//////////////////////////////////////////////////
float4	tempF1, tempF2, tempF3; //0,1,2,3,4,5,6,7,8,9

float4	tempInfo1;
float4	tempInfo2; // xy = cursor position of previous left click, zw = cursor position of previous right click

//////////////////////////////////////////////////
// Game & Mod Parameters                        //
//////////////////////////////////////////////////
float4				Params01[6]; //fallout4 parameters
float4				ENBParams01; //x - bloom amount; y - lens amount

//////////////////////////////////////////////////
// Textures                                     //
//////////////////////////////////////////////////
Texture2D			TextureOriginal; //color R10B10G10A2 32 bit ldr format
Texture2D			TextureColor; //color which is output of previous technique (except when drawed to temporary render target), R10B10G10A2 32 bit ldr format
Texture2D			TextureDepth; //scene depth R32F 32 bit hdr format
Texture2D			TextureBloom; //vanilla or enb bloom
Texture2D			TextureLens; //enb lens fx
Texture2D			TextureAdaptation; //vanilla or enb adaptation
Texture2D			TextureAperture; //this frame aperture 1*1 R32F hdr red channel only. computed in depth of field shader file
Texture2D			RenderTargetRGBA32; //R8G8B8A8 32 bit ldr format
Texture2D			RenderTargetRGBA64; //R16B16G16A16 64 bit ldr format
Texture2D			RenderTargetRGBA64F; //R16B16G16A16F 64 bit hdr format
Texture2D			RenderTargetR16F; //R16F 16 bit hdr format with red channel only
Texture2D			RenderTargetR32F; //R32F 32 bit hdr format with red channel only
Texture2D			RenderTargetRGB32F; //32 bit hdr format without alpha

//////////////////////////////////////////////////
// Samplers                                     //
//////////////////////////////////////////////////
SAMPLER(PointSampler,  POINT,  Clamp)
SAMPLER(LinearSampler, LINEAR, Clamp)
SAMPLER(WrapSampler,   LINEAR, Wrap)
SAMPLER(MirrorSampler, LINEAR, Wrap)
SAMPLER(BorderSampler, LINEAR, BORDER)

//////////////////////////////////////////////////
// Structure                                    //
//////////////////////////////////////////////////
struct VS_INPUT
{
    float3 pos     : POSITION;
    float2 txcoord : TEXCOORD0;
};
struct VS_OUTPUT
{
    float4 pos     : SV_POSITION;
    float2 txcoord : TEXCOORD0;
};

//////////////////////////////////////////////////
// Vertex Shaders                               //
//////////////////////////////////////////////////
VS_OUTPUT VS_Draw(VS_INPUT IN)
{
    VS_OUTPUT OUT;
    OUT.pos = float4(IN.pos.xyz, 1.0);
    OUT.txcoord.xy = IN.txcoord.xy;
    return OUT;
}

#if(DebugMode != 1)
    #pragma message ("///////////////////////////////////////////////////////////////////////////////////////////")
    #pragma message ("//   ooooooooo.   ooooo ooooo          .oooooo.    ooooooooo.   ooooo ooo        ooooo   //")
    #pragma message ("//   `888   `Y88. `888' `888'         d8P'  `Y8b   `888   `Y88. `888' `88.       .888'   //")
    #pragma message ("//    888   .d88'  888   888         888            888   .d88'  888   888b     d'888    //")
    #pragma message ("//    888ooo88P'   888   888         888            888ooo88P'   888   8 Y88. .P  888    //")
    #pragma message ("//    888          888   888         888     ooooo  888`88b.     888   8  `888'   888    //")
    #pragma message ("//    888          888   888       o `88.    .88'   888  `88b.   888   8    Y     888    //")
    #pragma message ("//   o888o        o888o o888ooooood8  `Y8bood8P'   o888o  o888o o888o o8o        o888o   //")
    #pragma message ("//                                                                                       //")
    #pragma message ("//                               FEAR THE COMMONWEALTH                                   //")
    #pragma message ("//                                                                                       //")
    #pragma message ("///////////////////////////////////////////////////////////////////////////////////////////")
    #pragma message ("// Created by: TreyM, Adyss, Dr_Mabuse1981, --JawZ--, and kingeric1992                   //")
    #pragma message ("///////////////////////////////////////////////////////////////////////////////////////////")
    #pragma message ("")
    #pragma message ("A path is revealed:")
    #pragma message ("https://TreyM.us/PilgrimEasterEgg")
    #pragma message ("")
#endif
