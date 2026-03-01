//////////////////////////////////////////////////////////////////////
//                                                                  //
//    ██████╗ ███████╗ █████╗  ██████╗████████╗ ██████╗ ██████╗     //
//    ██╔══██╗██╔════╝██╔══██╗██╔════╝╚══██╔══╝██╔═══██╗██╔══██╗    //
//    ██████╔╝█████╗  ███████║██║        ██║   ██║   ██║██████╔╝    //
//    ██╔══██╗██╔══╝  ██╔══██║██║        ██║   ██║   ██║██╔══██╗    //
//    ██║  ██║███████╗██║  ██║╚██████╗   ██║   ╚██████╔╝██║  ██║    //
//    ╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝ ╚═════╝   ╚═╝    ╚═════╝ ╚═╝  ╚═╝    //
//                                                                  //
//                         A FALLOUT4 ENB                           //
//                                                                  //
///// MOD PAGE ///////////////////////////////////////////////////////
//                                                                  //
//    https://www.nexusmods.com/fallout4/mods/57321                 //
//                                                                  //
//////////////////////////////////////////////////////////////////////
//                                                                  //
//    ENBSeries Fallout 4 hlsl DX11 format                          //
//    visit http://enbdev.com for updates                           //
//    Copyright (c) Boris Vorontsov                                 //
//                                                                  //
///// CREDITS ////////////////////////////////////////////////////////
//                                                                  //
//  - Additional shaders, setup,                                    //
//    modifications, tweaks and                                     //
//    author of this file:          Sevenence                       //
//                                                                  //
//  - Fog and weather stuff:        Adyss                           //
//                                                                  //
//  - Reforged code:                The Sandvich Maker              //
//                                                                  //
///// PLEASE DO NOT REDISTRIBUTE WITHOUT CREDITS /////////////////////



///// INCLUDE ////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

#include "Include/Helper.fxh"
#include "Include/ReforgedGlobals.fxh"
#include "Include/ReforgedUI.fxh"
#include "Setup.ini"

///// GUI ////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

UI_WHITESPACE(1)
UI_MESSAGE(w1, MERGE(               "R E A C T O R   E N B   ",             VERSION_NUMBER))

UI_WHITESPACE(2)
UI_WHITESPACE(3)

#define UI_CATEGORY Skin
UI_SEPARATOR_CUSTOM                 ("L I G H T R O O M")
UI_LINESPACE(1)
UI_BOOL(UI_Enable_LR,               "|- LR: Enable",                        true)
UI_LINESPACE(2)
UI_FLOAT_DNI(UI_highlightStrength,  "|- LR: Highlights",                    0.00, 1.00, 1.00)
UI_LINESPACE(3)
UI_FLOAT_DNI(UI_shadowStrength,     "|- LR: Shadows",                       0.00, 1.00, 0.25)
UI_LINESPACE(4)
UI_FLOAT_DNI(UI_highlightCutoff,    "|- LR: Threshold",                     0.01, 0.99, 0.05)
UI_LINESPACE(5)
UI_BOOL(UI_showDepth,               "|- LR: Visualize Depth",               false)
UI_FLOAT_FINE(UI_depthNear,         "|- LR: Depth Near",                    0.00, 1.00, 0.05, 0.001)
UI_FLOAT_FINE(UI_depthFar,          "|- LR: Depth Far",                     0.00, 1.00, 0.50, 0.001)

UI_WHITESPACE(4)

#define UI_CATEGORY Fog
UI_SEPARATOR_CUSTOM                 ("W E A T H E R   F O G")
UI_LINESPACE(10)
UI_BOOL(UI_Enable_Fog,              "|- FOG: Enable",                       false)
UI_BOOL(UI_Show_FogMask,            "|- FOG: Show Mask",                    false)
UI_LINESPACE(11)
UI_MESSAGE(5,                       "|------------ PLEASANT Weather Fog -------")
UI_FLOAT(w1fogDensity,              "|- PLEASANT: Fog Density",             0.0, 100.0, 0.0)
UI_FLOAT(w1l1nearFog,               "|- PLEASANT: near Fog Distance",       0.0, 10.0, 0.2)
UI_FLOAT(w1l1farFog,                "|- PLEASANT: near Fog Closeup",        0.0, 10.0, 0.0)
UI_FLOAT3(w1l1fogCol,               "|- PLEASANT: near Fog Color",          0.3, 0.3, 0.3)
UI_FLOAT(w1l2nearFog,               "|- PLEASANT: far Fog Distance",        0.0, 10.0, 1.0)
UI_FLOAT(w1l2farFog,                "|- PLEASANT: far Fog Closeup",         0.0, 10.0, 0.3)
UI_FLOAT3(w1l2fogCol,               "|- PLEASANT: far Fog Color",           0.3, 0.3, 0.3)
UI_LINESPACE(12)
UI_MESSAGE(6,                       "|------------ OVERCAST Weather Fog -------")
UI_FLOAT(w2fogDensity,              "|- OVERCAST: Fog Density",             0.0, 100.0, 0.0)
UI_FLOAT(w2l1nearFog,               "|- OVERCAST: near Fog Distance",       0.0, 10.0, 0.2)
UI_FLOAT(w2l1farFog,                "|- OVERCAST: near Fog Closeup",        0.0, 10.0, 0.0)
UI_FLOAT3(w2l1fogCol,               "|- OVERCAST: near Fog Color",          0.3, 0.3, 0.3)
UI_FLOAT(w2l2nearFog,               "|- OVERCAST: far Fog Distance",        0.0, 10.0, 1.0)
UI_FLOAT(w2l2farFog,                "|- OVERCAST: far Fog Closeup",         0.0, 10.0, 0.3)
UI_FLOAT3(w2l2fogCol,               "|- OVERCAST: far Fog Color",           0.3, 0.3, 0.3)
UI_LINESPACE(13)
UI_MESSAGE(7,                       "|------------ FOG Weather Fog ------------")
UI_FLOAT(w3fogDensity,              "|- FOG: Fog Density",                  0.0, 100.0, 0.0)
UI_FLOAT(w3l1nearFog,               "|- FOG: near Fog Distance",            0.0, 10.0, 0.2)
UI_FLOAT(w3l1farFog,                "|- FOG: near Fog Closeup",             0.0, 10.0, 0.0)
UI_FLOAT3(w3l1fogCol,               "|- FOG: near Fog Color",               0.3, 0.3, 0.3)
UI_FLOAT(w3l2nearFog,               "|- FOG: far Fog Distance",             0.0, 10.0, 1.0)
UI_FLOAT(w3l2farFog,                "|- FOG: far Fog Closeup",              0.0, 10.0, 0.3)
UI_FLOAT3(w3l2fogCol,               "|- FOG: far Fog Color",                0.3, 0.3, 0.3)
UI_LINESPACE(14)
UI_MESSAGE(8,                       "|------------ SNOW Weather Fog -----------")
UI_FLOAT(w4fogDensity,              "|- SNOW: Fog Density",                 0.0, 100.0, 0.0)
UI_FLOAT(w4l1nearFog,               "|- SNOW: near Fog Distance",           0.0, 10.0, 0.2)
UI_FLOAT(w4l1farFog,                "|- SNOW: near Fog Closeup",            0.0, 10.0, 0.0)
UI_FLOAT3(w4l1fogCol,               "|- SNOW: near Fog Color",              0.3, 0.3, 0.3)
UI_FLOAT(w4l2nearFog,               "|- SNOW: far Fog Distance",            0.0, 10.0, 1.0)
UI_FLOAT(w4l2farFog,                "|- SNOW: far Fog Closeup",             0.0, 10.0, 0.3)
UI_FLOAT3(w4l2fogCol,               "|- SNOW: far Fog Color",               0.3, 0.3, 0.3)
UI_LINESPACE(15)
UI_MESSAGE(9,                       "|------------ RAD Weather Fog ------------")
UI_FLOAT(w5fogDensity,              "|- RAD: Fog Density",                  0.0, 100.0, 0.0)
UI_FLOAT(w5l1nearFog,               "|- RAD: near Fog Distance",            0.0, 10.0, 0.2)
UI_FLOAT(w5l1farFog,                "|- RAD: near Fog Closeup",             0.0, 10.0, 0.0)
UI_FLOAT3(w5l1fogCol,               "|- RAD: near Fog Color",               0.3, 0.3, 0.3)
UI_FLOAT(w5l2nearFog,               "|- RAD: far Fog Distance",             0.0, 10.0, 1.0)
UI_FLOAT(w5l2farFog,                "|- RAD: far Fog Closeup",              0.0, 10.0, 0.3)
UI_FLOAT3(w5l2fogCol,               "|- RAD: far Fog Color",                0.3, 0.3, 0.3)
UI_LINESPACE(16)
UI_MESSAGE(10,                      "|------------ RAIN Weather Fog -----------")
UI_FLOAT(w6fogDensity,              "|- RAIN: Fog Density",                 0.0, 100.0, 0.0)
UI_FLOAT(w6l1nearFog,               "|- RAIN: near Fog Distance",           0.0, 10.0, 0.2)
UI_FLOAT(w6l1farFog,                "|- RAIN: near Fog Closeup",            0.0, 10.0, 0.0)
UI_FLOAT3(w6l1fogCol,               "|- RAIN: near Fog Color",              0.3, 0.3, 0.3)
UI_FLOAT(w6l2nearFog,               "|- RAIN: far Fog Distance",            0.0, 10.0, 1.0)
UI_FLOAT(w6l2farFog,                "|- RAIN: far Fog Closeup",             0.0, 10.0, 0.3)
UI_FLOAT3(w6l2fogCol,               "|- RAIN: far Fog Color",               0.3, 0.3, 0.3)
UI_LINESPACE(17)
UI_MESSAGE(11,                      "|------------ STORM Weather Fog ----------")
UI_FLOAT(w7fogDensity,              "|- STORM: Fog Density",                0.0, 100.0, 0.0)
UI_FLOAT(w7l1nearFog,               "|- STORM: near Fog Distance",          0.0, 10.0, 0.2)
UI_FLOAT(w7l1farFog,                "|- STORM: near Fog Closeup",           0.0, 10.0, 0.0)
UI_FLOAT3(w7l1fogCol,               "|- STORM: near Fog Color",             0.3, 0.3, 0.3)
UI_FLOAT(w7l2nearFog,               "|- STORM: far Fog Distance",           0.0, 10.0, 1.0)
UI_FLOAT(w7l2farFog,                "|- STORM: far Fog Closeup",            0.0, 10.0, 0.3)
UI_FLOAT3(w7l2fogCol,               "|- STORM: far Fog Color",              0.3, 0.3, 0.3)
UI_LINESPACE(18)
UI_MESSAGE(12,                      "|------------ Interior Fog ---------------")
UI_FLOAT(w8fogDensity,              "|- Interior: Fog Density",             0.0, 100.0, 0.0)
UI_FLOAT(w8l1nearFog,               "|- Interior: near Fog Distance",       0.0, 10.0, 0.2)
UI_FLOAT(w8l1farFog,                "|- Interior: near Fog Closeup",        0.0, 10.0, 0.0)
UI_FLOAT3(w8l1fogCol,               "|- Interior: near Fog Color",          0.3, 0.3, 0.3)
UI_FLOAT(w8l2nearFog,               "|- Interior: far Fog Distance",        0.0, 10.0, 1.0)
UI_FLOAT(w8l2farFog,                "|- Interior: far Fog Closeup",         0.0, 10.0, 0.3)
UI_FLOAT3(w8l2fogCol,               "|- Interior: far Fog Color",           0.3, 0.3, 0.3)

UI_WHITESPACE(20)

///// FUNCTIONS //////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

#include "Include/WeatherSeperation.fxh"
#include "Include/fogData.fxh"

float3 ShadowAndHighlight(float3 x)
{
    float  Luma   = calculateLuma(x);
	float3 Chroma = x / Luma;
    
    float a = UI_highlightStrength;
    float b = UI_highlightCutoff;
    float c = 1.0 + (UI_shadowStrength * 10.0);
    
    // Highlight adjustment
    Luma = Luma > b ? a * Luma - (a - 1.0) * b : Luma;
    
    // Shadow adjustment
    Luma = ((c - 1.0) * exp(-Luma / b) + 1.0) * Luma;
   
    return Luma * Chroma;
}

///// PIXEL SHADERS //////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

float4 PS_ClearBuffer(VS_OUTPUT IN) : SV_Target
{
    return 0.0;
}

float3 PS_Color(VS_OUTPUT IN) : SV_Target
{
    float2 coord    = IN.txcoord.xy;
    float3 color    = TextureColor.Sample(PointSampler, coord);

    // Sample fog and Blend
    int currWeather = findCurrentWeather();
    int prevWeather = findPrevWeather(); 

    float3 fog      = RenderTargetRGBA64.Sample(LinearSampler, coord); // Sample combined fog planes
    float3 fogColor = weatherLerp(fogData, colorFogLayer1, currWeather, prevWeather) * weatherLerp(fogData, colorFogLayer2, currWeather, prevWeather);

    if(UI_Show_FogMask) return fog;

            // Dither a bit. Since we can have two fog colors there can be banding
           fog     += chromaTriDither(fog, coord, Timer.x, 10);

           if(UI_Enable_Fog)
           color    = lerp(color + (fogColor / (1 + color)), color, exp(-weatherLerp(fogData, fogDensity, currWeather, prevWeather)  * fog));

    return color;
}

float3 PS_DrawFog(VS_OUTPUT IN, uniform int layerNum) : SV_Target
{
    if(!UI_Enable_Fog) return 0.0;

    // Setup
    float2 coord    = IN.txcoord.xy;
    float3 color    = TextureColor.Sample(LinearSampler, coord); // In this case prev fog
    float  nearPlane, farPlane;
    float3 fogColor;

    // Find weathers
    int currWeather = findCurrentWeather();
    int prevWeather = findPrevWeather();

    if(layerNum == 1)
    {
        nearPlane   = weatherLerp(fogData, nearFogLayer1,  currWeather, prevWeather);
        farPlane    = weatherLerp(fogData, farFogLayer1,   currWeather, prevWeather);
        fogColor    = weatherLerp(fogData, colorFogLayer1, currWeather, prevWeather);
    }
    else
    {
        nearPlane   = weatherLerp(fogData, nearFogLayer2,  currWeather, prevWeather);
        farPlane    = weatherLerp(fogData, farFogLayer2,   currWeather, prevWeather);
        fogColor    = weatherLerp(fogData, colorFogLayer2, currWeather, prevWeather);
    }

    // Calc Fog. Remove prev plane for spereration
    float  fogPlane = (1 - saturate((getLinearizedDepth(coord) - nearPlane) / (farPlane - nearPlane))) - color;

    // Add to prev Fog
    return color + (fogColor * fogPlane);
}

float4 PS_LIGHTROOM(VS_OUTPUT_POST IN) : SV_Target
{
    float3 color = TextureOriginal.Sample(PointSampler, IN.txcoord.xy);
    
    if(UI_Enable_LR)
    {
        float  depth = 1.0 - linearDepth(TextureDepth.Sample(PointSampler, IN.txcoord.xy), UI_depthNear, UI_depthFar);
               color = lerp(color, ShadowAndHighlight(color), depth);
    
        if(UI_showDepth) color = depth;
    }
   
    return float4(zerolim(color), 1.0);
}

///// TECHNIQUES /////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

technique11 pre <string UIName="REACTOR";>
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_ClearBuffer())); } }

technique11 pre1
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_DrawFog(1))); } }

technique11 pre2 <string RenderTarget="RenderTargetRGBA64";>
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_DrawFog(2))); } }

technique11 pre3
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_LIGHTROOM())); } }

technique11 pre4
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_Color())); } }