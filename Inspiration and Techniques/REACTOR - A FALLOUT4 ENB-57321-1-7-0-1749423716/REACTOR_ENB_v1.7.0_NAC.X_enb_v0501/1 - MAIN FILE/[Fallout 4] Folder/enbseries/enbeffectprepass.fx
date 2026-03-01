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

UI_WHITESPACE(20)

///// FUNCTIONS //////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

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

float4 PS_LIGHTROOM(VS_OUTPUT_POST IN) : SV_Target
{
    float3 color = TextureColor.Sample(PointSampler, IN.txcoord.xy);
    
    [branch] if(UI_Enable_LR)
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
            SetPixelShader (CompileShader(ps_5_0, PS_LIGHTROOM())); } }