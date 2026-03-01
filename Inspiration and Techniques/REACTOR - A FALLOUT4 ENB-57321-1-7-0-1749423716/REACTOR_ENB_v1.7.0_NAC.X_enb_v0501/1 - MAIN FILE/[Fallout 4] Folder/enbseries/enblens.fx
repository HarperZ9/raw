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
#include "Include/ReforgedUI.fxh"
#include "Include/ReforgedGlobals.fxh"
#include "Setup.ini"

///// TEXTURES ///////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

Texture2D           LensBokehTexture_1       <string ResourceName="Include/Textures/LensBokeh1.png"; >;
Texture2D           LensBokehTexture_2       <string ResourceName="Include/Textures/LensBokeh2.png"; >;
Texture2D           LensBokehTexture_3       <string ResourceName="Include/Textures/LensBokeh3.png"; >;
Texture2D           LensBokehTexture_4       <string ResourceName="Include/Textures/LensBokeh4.png"; >;
Texture2D           LensDirtTexture_1        <string ResourceName="Include/Textures/LensDirt1.png"; >;
Texture2D           LensDirtTexture_2        <string ResourceName="Include/Textures/LensDirt2.png"; >;

///// GUI ////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

UI_WHITESPACE(1)
UI_MESSAGE(w1, MERGE(                       "R E A C T O R   E N B   ",             VERSION_NUMBER))

UI_WHITESPACE(2)
UI_WHITESPACE(3)

#define UI_CATEGORY LENSREFLECTION  
UI_SEPARATOR_CUSTOM                         ("C A M E R A   L E N S   R E F L E C T I O N")
UI_LINESPACE(1)
UI_FLOAT_DNI (UI_Reflection_Power,          "|- Reflection: Power",                 0.50, 2.00, 1.00)
UI_LINESPACE(2)
UI_FLOAT_DNI (UI_Reflection_Intensity,      "|- Reflection: Intensity",             0.00, 2.00, 1.00)

UI_WHITESPACE(4)
UI_WHITESPACE(5)

#define UI_CATEGORY LENSDIRT  
UI_SEPARATOR_CUSTOM                         ("C A M E R A   L E N S   D I R T")
UI_LINESPACE(3)
UI_INT_DNI(UI_Bokeh_Type,                   "|- Dirt: Bokeh Type",                  1.00, 4.00, 1.00)
UI_LINESPACE(4)
UI_INT_DNI(UI_Dirt_Type,                    "|- Dirt: Dirt Type",                   1.00, 2.00, 1.00)
UI_LINESPACE(5)
UI_FLOAT_DNI (UI_Dirt_Blend,                "|- Dirt: Crossfade",                   0.00, 1.00, 0.50)
UI_LINESPACE(6)
UI_FLOAT_DNI (UI_Dirt_Intensity,            "|- Dirt: Intensity",                   0.00, 2.00, 1.00)

UI_WHITESPACE(20)

///// FUNCTIONS //////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

///// BLUR ///////////////////////////////////////////////////////////

float3  FuncBlur(Texture2D inputtex, float2 uvsrc, float srcsize, float destsize)
{
    const float scale = 4.0; //blurring range, samples count (performance) is factor of scale*scale
    //const float   srcsize=1024.0; //in current example just blur input texture of 1024*1024 size
    //const float   destsize=1024.0; //for last stage render target must be always 1024*1024

    float2  invtargetsize    = scale / srcsize;
            invtargetsize.y *= ScreenSize.z; //correct by aspect ratio

    float2  fstepcount;
            fstepcount = srcsize;
            fstepcount *= invtargetsize;
            fstepcount  = min(fstepcount, 16.0);
            fstepcount  = max(fstepcount, 2.0);

    int stepcountX =(int)(fstepcount.x + 0.4999);
    int stepcountY =(int)(fstepcount.y + 0.4999);

            fstepcount = 1.0 / fstepcount;
   
    float4  curr = 0.0;
            curr.w = 0.000001;
    
    float2  pos;
    
    float2  halfstep = 0.5 * fstepcount.xy;
            pos.x = -0.5 + halfstep.x;
            invtargetsize *= 2.0;
    
    for (int x = 0; x < stepcountX; x++)
    {
        pos.y = -0.5 + halfstep.y;
        for (int y = 0; y < stepcountY; y++)
        {
            float2  coord = pos.xy * invtargetsize + uvsrc.xy;
            float3  tempcurr = inputtex.Sample(LinearSampler, coord.xy).xyz;
            float   tempweight;
            float2  dpos = pos.xy * 2.0;
            float   rangefactor = dot(dpos.xy, dpos.xy);
            //loosing many pixels here, don't program such unefficient cycle yourself!
            
            tempweight  = saturate(1001.0 - 1000.0 * rangefactor);//arithmetic version to cut circle from square
            tempweight *= saturate(1.0 - rangefactor); //softness, without it bloom looks like bokeh dof

            curr.xyz   += tempcurr.xyz * tempweight;
            curr.w += tempweight;

            pos.y += fstepcount.y;
        }
        pos.x += fstepcount.x;
    }
    curr.xyz /= curr.w;

    //curr.xyz=inputtex.Sample(LinearSampler, uvsrc.xy);

    return curr.xyz;
}

///// PIXEL SHADERS //////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

float3  PS_Prepass(VS_OUTPUT IN, uniform Texture2D inputtex) : SV_Target
{
    float3 color = inputtex.Sample(LinearSampler, IN.txcoord.xy);
    
    color = 1.0 - exp(-color);
  
    return color * 0.5;
}

float4  PS_Resize(VS_OUTPUT_POST IN, float4 v0 : SV_Position0,
    uniform Texture2D inputtex, uniform float srcsize, uniform float destsize) : SV_Target
{
    float4  res;
            res.xyz = FuncBlur(inputtex, IN.txcoord.xy, srcsize, destsize);
            res.w   = 1.0;
    
    return  hdrlim(res);
}

float4  PS_ComputeLens1(VS_OUTPUT_POST IN, float4 v0 : SV_Position0,
    uniform Texture2D inputtex, uniform float srcsize) : SV_Target
{
    float4  res;
    float4  color = 0.0;
    float2  coord;

    float   weight = 0.000001;
    float   scale = 0.375;
    float   step = 1.0 / 16.0;
    float2  pos;
            pos.y = 0.0;
            pos.x = -1.0;
    
    for (int i=0; i<33; i++)
    {
        float  tempweight;
        coord.xy    = pos.xy * scale;
        coord.xy   += IN.txcoord.xy;
        
        float3  tempcolor = inputtex.Sample(LinearSampler, coord.xy);
        tempweight  = 1.05 - abs(pos.x);
        tempweight *= 1.0 - saturate(abs(coord.x * 32.0 - 16.0) - 16.0); //clamp outside of screen
        tempweight *= tempweight;
        color.xyz  += tempcolor.xyz * tempweight;
        weight     += tempweight;

        pos.x += step;
    }
    
    color.xyz /= weight;
    res.xyz    = color;
    res.w      = 1.0;
    
    return res;
}

float4  PS_ComputeLens2(VS_OUTPUT_POST IN, float4 v0 : SV_Position0,
        uniform Texture2D inputtex, uniform float srcsize) : SV_Target
{
    float4  res;
    float4  color = 0.0;
    float2  coord;

    float   weight = 0.000001;
    float   scale  = 1.0 / 96.0;
    float   step   = 1.0 / 4.0;
    float2  pos;
            pos.y = 0.0;
            pos.x =-1.0;
            
    for (int i=0; i<9; i++)
    {
        float  tempweight;
        coord.xy  = pos.xy * scale;
        coord.xy += IN.txcoord.xy;
        
        float3  tempcolor = inputtex.Sample(LinearSampler, coord.xy);
        tempweight = 1.0;
        //tempweight=1.05-abs(pos.x);
        color.xyz += tempcolor.xyz * tempweight;
        weight    += tempweight;

        pos.x += step;
    }
    
    color.xyz /= weight;
    res.xyz    = color;
    res.w      = 1.0;

    return res;
}

float4  PS_DrawLens(VS_OUTPUT_POST IN, float4 v0 : SV_Position0,
    uniform Texture2D inputtex, uniform float srcsize, uniform float destsize) : SV_Target
{
    float4  res = 0.0;
    float2  coord;

    // deepness, curvature, inverse size
    const float3 offset[4]=
    {
        float3( 1.6, 4.0,  1.0),
        float3( 0.7, 0.25, 2.0),
        float3( 0.3, 1.5,  0.5),
        float3(-0.5, 1.0,  1.0)
    };
    
    // color filter per reflection
    const float3 factors[4]=
    {
        float3(0.3, 0.4, 0.4),
        float3(0.2, 0.4, 0.5),
        float3(0.5, 0.3, 0.7),
        float3(0.1, 0.2, 0.7)
    };

    for (int i=0; i<4; i++)
    {
        float2  distfact = (IN.txcoord.xy - 0.5);
        
        coord.xy  = offset[i].x * distfact;
        coord.xy *= pow(2.0 * length(float2(distfact.x * ScreenSize.z, distfact.y)), offset[i].y);
        coord.xy *= offset[i].z;
        coord.xy  = 0.5 - coord.xy;//v1
//      coord.xy=IN.txcoord.xy-coord.xy;//v2
        
        float3  templens = inputtex.Sample(LinearSampler, coord.xy);
        
        templens  = templens * factors[i];
        distfact  = (coord.xy - 0.5);
        distfact *= 2.0;
        templens *= saturate(1.0 - dot(distfact, distfact)); //limit by uv 0..1
        
        float  maxlens = max(templens.x, max(templens.y, templens.z));

        float  tempnor = (maxlens / (1.0 + maxlens));
               tempnor = pow(tempnor, UI_Reflection_Power);
        
        templens.xyz  *= tempnor;
        res.xyz       += templens;
    }
    
    res.xyz *= 0.25 * (UI_Reflection_Intensity * 0.1);
    res.w    = 1.0;
    
    return res;
}

float4  PS_MixLens(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
    float2  coord = IN.txcoord.xy;
    float3  lens  = RenderTarget512.Sample(LinearSampler, IN.txcoord.xy);
    float3  bokeh = 0.0;
    float3  dirt  = 0.0;

    ///// LENS DIRT //////////////////////////////////////////////////

    if(UI_Bokeh_Type==1) bokeh = LensBokehTexture_1.Sample(PointSampler, coord.xy);
    if(UI_Bokeh_Type==2) bokeh = LensBokehTexture_2.Sample(PointSampler, coord.xy);
    if(UI_Bokeh_Type==3) bokeh = LensBokehTexture_3.Sample(PointSampler, coord.xy);
    if(UI_Bokeh_Type==4) bokeh = LensBokehTexture_4.Sample(PointSampler, coord.xy);
    
    if(UI_Dirt_Type==1)  dirt  = LensDirtTexture_1.Sample(PointSampler, coord.xy);
    if(UI_Dirt_Type==2)  dirt  = LensDirtTexture_2.Sample(PointSampler, coord.xy);
    
    float3  mixed  = lerp(bokeh, dirt, UI_Dirt_Blend) * 2.0;
            lens  += mixed * (pow(lens, 0.7) * UI_Dirt_Intensity);
    
    return float4(hdrlim(lens), 1.0);
}

///// TECHNIQUES /////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

technique11 Lens <string UIName="REACTOR"; string RenderTarget="RenderTarget1024";>
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_Prepass(TextureDownsampled))); } }

technique11 Lens1 <string RenderTarget="RenderTarget512";>
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_Resize(RenderTarget1024, 1024.0, 512.0))); } }

technique11 Lens2 <string RenderTarget="RenderTarget256";>
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_Resize(RenderTarget512, 512.0, 256.0))); } }

technique11 Lens3 <string RenderTarget="RenderTarget128";>
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_Resize(RenderTarget256, 256.0, 128.0))); } }

technique11 Lens4 <string RenderTarget="RenderTarget512";>
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_DrawLens(RenderTarget256, 256.0, 512.0))); } }

technique11 Lens5
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_MixLens())); } }