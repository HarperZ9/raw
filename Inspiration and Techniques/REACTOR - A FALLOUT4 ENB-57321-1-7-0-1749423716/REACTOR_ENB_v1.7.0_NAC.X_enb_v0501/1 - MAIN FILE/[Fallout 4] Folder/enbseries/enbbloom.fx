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
//  - Ady Bloom Ultimate v1.1:      Adyss                           //
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
UI_MESSAGE(w2,                      "Bloom mixing is controlled")
UI_MESSAGE(w3,                      "by Image Space in enbeffect.fx")

UI_WHITESPACE(3)
UI_WHITESPACE(4)

#define UI_CATEGORY PREPASS
UI_SEPARATOR_CUSTOM                 ("P R E   P A S S")
UI_LINESPACE(1)
UI_FLOAT(UI_Lights_Threshold,       "|- Prepass: Highlights",               0.00, 1.00, 1.00)      
UI_FLOAT(UI_Shadows_Threshold,      "|- Prepass: Shadows",                  0.00, 1.00, 0.50)
UI_FLOAT(UI_removeSky,              "|- Prepass: Mask out Sky",             0.00, 1.00, 0.00)

UI_WHITESPACE(5)
UI_WHITESPACE(6)

#define UI_CATEGORY BLOOM
UI_SEPARATOR_CUSTOM                 ("B L O O M")
UI_LINESPACE(2)
UI_FLOAT_DNI(UI_Bloom_Intensity,    "|- Bloom: Intensity",                  0.00, 5.00, 1.00)
UI_LINESPACE(3)
UI_FLOAT_DNI(UI_Bloom_Radius,       "|- Bloom: Radius",                    -2.00, 2.00, 0.00)
UI_LINESPACE(4)
UI_FLOAT_DNI(UI_Bloom_Saturation,   "|- Bloom: Saturation",                 0.00, 2.00, 1.00)

UI_WHITESPACE(20)

///// CONSTANTS //////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

static const float  thresSoftness = 0.1;    // [0.1 - 1.0]  Transition softness of the prepass threshold
static const float  sigma         = 2.0;    // [2|3|4|5|6]  Sigma value used to calculate the Gaussian kernel
static const int    samples       = 11;     // [5|7|9|11 ]  Samples count / Bloom quality (performance: 5 = low / 7 = medium / 9 = high / 11 = ultra)

///// FUNCTIONS //////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

float Sigmoid(float x)
{
    return 1.0 / (1.0 + exp(-x));
}

float2 getPixelSize(float texsize)
{
    return (1.0 / texsize) * float2(1, ScreenSize.z);
}

float getWeight(int x)
{
    return exp(-(x * x) / (2 * sigma * sigma));
}

float4 simpleBlur(Texture2D inputTex, float2 coord, float2 pixelSize)
{
    float4 Blur = 0.0;

    static const float2 Offsets[4]=
    {
        float2( 0.5,  0.5),
        float2( 0.5, -0.5),
        float2(-0.5,  0.5),
        float2(-0.5, -0.5)
    };

    for (int i = 0; i < 4; i++)
    {
        Blur += inputTex.Sample(LinearSampler, coord + Offsets[i] * pixelSize);
    }

    return Blur * 0.25;
}

///// PIXEL SHADERS //////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

float3  PS_Prepass(VS_OUTPUT IN, uniform Texture2D InputTex) : SV_Target
{
    float3  color  = InputTex.Sample(LinearSampler, IN.txcoord.xy);
            color  = lerp(color, color / (1.0 + max3(color)), UI_Lights_Threshold);
            color *= Sigmoid((max3(color) - UI_Shadows_Threshold) / thresSoftness);
            color  = lerp(calculateLuma(color), color, UI_Bloom_Saturation);
            color  = lerp(color, color * (1.0 - floor(getLinearizedDepth(IN.txcoord.xy))), UI_removeSky);
    
    return  hdrlim(color);
}

float3  PS_Preblur(VS_OUTPUT IN, uniform Texture2D InputTex, uniform float texsize) : SV_Target
{
    return  simpleBlur(InputTex, IN.txcoord.xy, getPixelSize(texsize));
}
   
float3  PS_BlurH(VS_OUTPUT IN, uniform Texture2D InputTex, uniform float texsize) : SV_Target
{
    float2  pixelSize = getPixelSize(texsize);
    float3  color     = 0.0;
    float   kernelSum = 0.0;
    int     upper     = samples * 0.5;
    int     lower     = -upper;

    for (int i = lower; i <= upper; i++)
    {
        float weight  = getWeight(i);
        color        += InputTex.SampleLevel(LinearSampler, IN.txcoord.xy + float2(pixelSize.x * i, 0.0), 0) * weight;
        kernelSum    += weight;
    }
   
    return  color / kernelSum;
}

float3  PS_BlurV(VS_OUTPUT IN, uniform Texture2D InputTex, uniform float texsize) : SV_Target
{
    float2  pixelSize = getPixelSize(texsize);
    float3  color     = 0.0;    
    float   kernelSum = 0.0;
    int     upper     = samples * 0.5;
    int     lower     = -upper;
            
    for (int i = lower; i <= upper; i++)
    {
        float weight  = getWeight(i);
        color        += InputTex.SampleLevel(LinearSampler, IN.txcoord.xy + float2(0.0, pixelSize.y * i), 0) * weight;
        kernelSum    += weight;
    }
    
    return  color / kernelSum;
}

float3  PS_BloomMix(VS_OUTPUT IN) : SV_Target
{
    float2  coord = IN.txcoord.xy;
    int     maxlevel = 6;
    float   weight[7];
    float   weightSum = 0.0;

    [unroll]
    for (int i = 0; i <= maxlevel; i++) 
    {
        weight[i]  = pow(i + 1, UI_Bloom_Radius);
        weightSum += weight[i];
    }

    float3  bloom  = 0.0;
            bloom += RenderTarget1024.Sample    (LinearSampler, coord) * weight[0];
            bloom += RenderTarget512.Sample     (LinearSampler, coord) * weight[1];
            bloom += RenderTarget256.Sample     (LinearSampler, coord) * weight[2];
            bloom += RenderTarget128.Sample     (LinearSampler, coord) * weight[3];
            bloom += RenderTarget64.Sample      (LinearSampler, coord) * weight[4];
            bloom += RenderTarget32.Sample      (LinearSampler, coord) * weight[5];
            bloom += RenderTarget16.Sample      (LinearSampler, coord) * weight[6];
            bloom += RenderTargetRGBA64F.Sample (LinearSampler, coord);
            
            bloom *= UI_Bloom_Intensity / weightSum;
   
    return  hdrlim(bloom * 5.0);
}

///// TECHNIQUES /////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

technique11 normal <string UIName="REACTOR"; string RenderTarget="RenderTarget1024";>
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_Prepass(TextureDownsampled))); } }

technique11 normal1 <string RenderTarget="RenderTargetRGBA64F";>
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_Preblur(RenderTarget1024, 1024.0))); } }            

technique11 normal2
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_BlurH(RenderTarget1024, 1024.0))); } }

technique11 normal3 <string RenderTarget="RenderTarget1024";>
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_BlurV(TextureColor, 1024.0))); } }

technique11 normal4
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_BlurH(RenderTarget1024, 512.0))); } }

technique11 normal5 <string RenderTarget="RenderTarget512";>
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_BlurV(TextureColor, 512.0))); } }

technique11 normal6
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_BlurH(RenderTarget512, 256.0))); } }

technique11 normal7 <string RenderTarget="RenderTarget256";>
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_BlurV(TextureColor, 256.0))); } }

technique11 normal8
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_BlurH(RenderTarget256, 128.0))); } }

technique11 normal9 <string RenderTarget="RenderTarget128";>
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_BlurV(TextureColor, 128.0))); } }

technique11 normal10
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_BlurH(RenderTarget128, 64.0))); } }

technique11 normal11 <string RenderTarget="RenderTarget64";>
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_BlurV(TextureColor, 64.0))); } }

technique11 normal12
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_BlurH(RenderTarget64, 32.0))); } }

technique11 normal13 <string RenderTarget="RenderTarget32";>
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_BlurV(TextureColor, 32.0))); } }

technique11 normal14
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_BlurH(RenderTarget32, 16.0))); } }

technique11 normal15 <string RenderTarget="RenderTarget16";>
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_BlurV(TextureColor, 16.0))); } }

technique11 normal16
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_BloomMix())); } }