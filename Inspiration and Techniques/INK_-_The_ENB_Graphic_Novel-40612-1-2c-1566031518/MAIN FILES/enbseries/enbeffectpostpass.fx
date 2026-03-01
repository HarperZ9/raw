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
// Internals                                    //
//////////////////////////////////////////////////
#include "Include/Internals/Macros.fxh"
#include "Include/Internals/ENBCommon.fxh"

//////////////////////////////////////////////////
// UI                                           //
//////////////////////////////////////////////////
#include "Include/Internals/UI/UIPostPass.fxh"

//////////////////////////////////////////////////
// Functions                                    //
//////////////////////////////////////////////////
#include "Include/Functions/global/Globals.fxh"
#include "Include/Functions/global/Dither.fxh"
#include "Include/Functions/global/BlendModes.fxh"
#include "Include/Functions/enbeffectpostpass/SMAA.fxh"
#include "Include/Functions/enbeffectpostpass/FXAA.fxh"
#include "Include/Functions/enbeffectpostpass/GaussianBlur.fxh"
#include "Include/Functions/enbeffectpostpass/InkGrain.fxh"
#include "Include/Functions/enbeffectpostpass/AdvancedLighting.fxh"
#include "Include/Functions/enbeffectpostpass/Gradient.fxh"
#include "Include/Functions/enbeffectpostpass/ComicTreatment.fxh"

//////////////////////////////////////////////////
// Pixel Shaders                                //
//////////////////////////////////////////////////

// Lens Diffusion (Horizontal Gaussian Pass) /////
float4	PS_ColorEdgeH(VS_OUTPUT IN, uniform Texture2D TEX_ColorEdge) : SV_Target
{
    float2 coord = IN.txcoord.xy;
    float4 color = BACKBUFFER(coord);

    if (SHADOW_DETAIL || (LINE_MODE == 5)) color = ColorEdgeH(IN, TEX_ColorEdge);

    return color;
}

// Lens Diffusion (Vertical Gaussian Pass) ///////
float4	PS_ColorEdgeV(VS_OUTPUT IN, uniform Texture2D TEX_ColorEdge) : SV_Target
{
    float2 coord = IN.txcoord.xy;
    float4 color = BACKBUFFER(coord);

    if (SHADOW_DETAIL || (LINE_MODE == 5)) color = ColorEdgeV(IN, TEX_ColorEdge);

    return color;
}

float3 PS_ComicTreatment(VS_OUTPUT IN, uniform Texture2D TEX_ColorEdge) : SV_Target
{
    float2 coord = IN.txcoord.xy;
    float3 color = BACKBUFFER(coord);

    color = ComicTreatment(IN, TEX_ColorEdge);

    return color;
}

float3 PS_Panels(VS_OUTPUT IN) : SV_Target
{
    float2 coord = IN.txcoord.xy;
	float3 color = BACKBUFFER(coord);

    color = Panels(coord);

    //color = lerp(20 / 255.0, 220 / 255.0, color);

    return color;
}

float3 PS_Overlays(VS_OUTPUT IN) : SV_Target
{
    float2 coord = IN.txcoord.xy;
	float3 color = BACKBUFFER(coord);

    if (ENABLE_OVERLAYS) color = Overlays(coord);

    return color;
}

float4	PS_PageSoftenH(VS_OUTPUT IN, uniform Texture2D TEX_Soften) : SV_Target
{
    float2 coord = IN.txcoord.xy;
    float4 color = BACKBUFFER(coord);

    if (SOFTEN_PAGE) color = PageSoftenH(IN, TEX_Soften);

    return color;
}

// Lens Diffusion (Vertical Gaussian Pass) ///////
float4	PS_PageSoftenV(VS_OUTPUT IN, uniform Texture2D TEX_Soften) : SV_Target
{
    float2 coord = IN.txcoord.xy;
    float4 color = BACKBUFFER(coord);

    if (SOFTEN_PAGE) color = PageSoftenV(IN, TEX_Soften);

    return color;
}

float3 PS_Paper(VS_OUTPUT IN) : SV_Target
{
    float2 coord = IN.txcoord.xy;
	float3 color = BACKBUFFER(coord);

    color = Paper(coord);

    return color;
}

//////////////////////////////////////////////////
// Techniques                                   //
//////////////////////////////////////////////////
#define      TECHNIQUE_NAME         pp
TECHNIQUE(  <ID(INK); RT(RGBA32);>, PASS(PS_ColorEdgeH     (TextureColor)))
TECHNIQUE(  1        <RT(RGB32F);>, PASS(PS_ColorEdgeV     (RenderTargetRGBA32)))
TECHNIQUE(  2,                      PASS(PS_ComicTreatment (RenderTargetRGB32F)))
TECHNIQUE(  3,                      PASS(PS_Panels()))
TECHNIQUE(  4,                      PASS(PS_Overlays()))
TECHNIQUE(  5        <RT(RGB32F);>, PASS_FULL(p0, VS_SMAAClear(), PS_SMAAClear())
                                    PASS_FULL(p1, VS_SMAAEdgeDetection(), PS_SMAAEdgeDetection()))
TECHNIQUE(  6        <RT(RGBA64);>, PASS_FULL(p0, VS_SMAAClear(), PS_SMAAClear())
                                    PASS_FULL(p1, VS_SMAABlendingWeightCalculation(), PS_SMAABlendingWeightCalculation()))
TECHNIQUE(  7,                      PASS_FULL(p0, VS_SMAANeighborhoodBlending(), PS_SMAANeighborhoodBlending()))
TECHNIQUE(  8,                      PASS_FULL(p0, VS_Draw(), PS_FXAA()))
TECHNIQUE(  9        <RT(RGBA32);>, PASS(PS_PageSoftenH    (TextureColor)))
TECHNIQUE( 10,                      PASS(PS_PageSoftenV    (RenderTargetRGBA32)))
TECHNIQUE( 11,                      PASS(PS_Paper()))
