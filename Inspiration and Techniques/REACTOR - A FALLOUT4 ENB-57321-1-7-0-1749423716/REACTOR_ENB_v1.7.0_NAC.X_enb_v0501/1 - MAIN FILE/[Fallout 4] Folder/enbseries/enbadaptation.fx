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
//  - Histogram based adaptation:   kingeric1992                    //
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

#if   E_ADAPTATION==1
    #define ADAPT_STATE ENB
#elif E_ADAPTATION==2
    #define ADAPT_STATE VANILLA
#elif E_ADAPTATION==3
    #define ADAPT_STATE VANILLA AUTO EXPOSURE
#endif

UI_WHITESPACE(1)

UI_MESSAGE(w1, MERGE(                       "R E A C T O R   E N B   ",             VERSION_NUMBER))

#if E_ADAPTATION==3
    UI_WHITESPACE(2)
    UI_MESSAGE(w2,                          "Adaptation is controlled")
    UI_MESSAGE(w3,                          "by Image Space in enbeffect.fx")
#endif

UI_WHITESPACE(3)
UI_WHITESPACE(4)

#define UI_CATEGORY HBADAPTATION
UI_SEPARATOR_CUSTOM                         ("A D A P T A T I O N")
UI_LINESPACE(1)

#if E_ADAPTATION!=3

    UI_MESSAGE(w4, MERGE(                   "|  Mode: ",                            TO_STRING(ADAPT_STATE)))
    UI_LINESPACE(2)
    UI_FLOAT_DNI(UI_Bias,                   "|- Auto Exposure: Bias",              -10.0, 10.0,  0.00)
    UI_FLOAT_DNI(UI_MaxBrightness,          "|- Adapt: Max Brightness",            -9.00, 3.00,  1.00)
    UI_FLOAT_DNI(UI_MinBrightness,          "|- Adapt: Min Brightness",            -9.00, 3.00, -4.0)
    UI_FLOAT_DNI(UI_LowPercent,             "|- Adapt: Low Percent",                0.50, 0.99,  0.80)
    UI_FLOAT_DNI(UI_HighPercent,            "|- Adapt: High Percent",               0.50, 0.99,  0.95)

    UI_WHITESPACE(5)
    UI_WHITESPACE(6)

    #define UI_CATEGORY MANUALOVERWRITE
    UI_SEPARATOR_CUSTOM                     ("M A N U A L   O V E R W R I T E")
    UI_LINESPACE(3)
    UI_BOOL(UI_EnableManualOverwride,       "|- MO: Disable Adaptation",            false)
    UI_FLOAT_DNI(UI_ManualOverwrite,        "|- MO: Manual value",                  0.00, 1.00, 0.50)

#elif E_ADAPTATION==3

    UI_MESSAGE(w4, MERGE(                   "|  Mode: ",                            TO_STRING(ADAPT_STATE)))
    
    #define UI_Bias                         -1.00
    #define UI_MaxBrightness                 0.00
    #define UI_MinBrightness                -8.00
    #define UI_LowPercent                    0.80
    #define UI_HighPercent                   0.95
    #define UI_EnableManualOverwride         false
    #define UI_ManualOverwrite               0.50

#endif

UI_WHITESPACE(20)


///// PIXEL SHADERS //////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

// output size is 16*16
// TextureCurrent size is 256*256, it's internally downscaled from full screen
// input texture is R16G16B16A16 or R11G11B10 float format (alpha ignored)
// output texture is R32 float format (red channel only)

float4  AdaptationParameters; // x = AdaptationMin, y = AdaptationMax, z = AdaptationSensitivity, w = AdaptationTime multiplied by time elapsed

float4  PS_Downsample(float4 pos : SV_POSITION, float2 txcoord0 : TEXCOORD0) : SV_Target
{
    float  res = 0.0;
    float4 coord = { txcoord0.xyy, 1.0 / 128.0 };

    for (int x=0; x<8; x++)
    {
        coord.y = coord.z;
        for (int y=0; y<8; y++)
        {
            float4 color    = TextureCurrent.Sample(Sampler1, coord.xy);
                   res     += dot(color.xyz, float3(0.0, 0.5, 0.5));
                 //res     += max3(color);
                   coord.y += coord.w;
        }
        coord.x += coord.w;
    }
    
  //return log2(res) - 6.0; //default
    return log2( res / 64.0);
}

// output size is 1*1
// TexturePrevious size is 1*1
// TextureCurrent size is 16*16
// output and input textures are R32 float format (red channel only)

float4  PS_Histogram() : SV_Target
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
            float color = TextureCurrent.SampleLevel(Sampler0, coord.xy, 0.0).r;
            float level = saturate(( color + 9.0 ) / 12) * 63; // [-9, 3] Fallout 4
          //float level = saturate(( color + 5.0 ) / 7) * 63;  // [-5, 2] Skyrim 
            bin[ level * 0.25 ] += float4(0.0, 1.0, 2.0, 3.0) == float4(trunc(level % 4).xxxx); //bitwise ?
            coord.y  += coord.w;
        }
        coord.x += coord.w;
    }

    float2 adaptAnchor = 0.5; //.x = high, .y = low
    float2 accumulate  = float2( UI_HighPercent - 1.0, UI_LowPercent - 1.0) * 256.0;

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

    float adapt = (adaptAnchor.x + adaptAnchor.y) * 0.5 / 63.0 * 12.0 - 9.0; // Fallout 4
//  float adapt = (adaptAnchor.x + adaptAnchor.y) * 0.5 / 63.0 * 7.0 - 5.0;  // Skyrim
          adapt =  pow(2.0, clamp( adapt, UI_MinBrightness, UI_MaxBrightness) + UI_Bias);  // min max on log2 scale

    if(UI_EnableManualOverwride) return pow(0.01, UI_ManualOverwrite); 
    else return deltalim(lerp(TexturePrevious.Sample(Sampler0, 0.5).x, adapt, AdaptationParameters.w));
    
}

///// TECHNIQUES /////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

technique11 Downsample
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
            SetPixelShader (CompileShader(ps_5_0, PS_Downsample())); } }

technique11 Draw  <string UIName="REACTOR";>
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
            SetPixelShader (CompileShader(ps_5_0, PS_Histogram())); } }