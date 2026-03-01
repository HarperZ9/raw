// SMAA PORTED BY KINGERIC1992 /////////////////////

#ifndef _ENBSMAA_FX_
#define _ENBSMAA_FX_
#if(ENB_QUALITY == 1)
    #define SMAA_PRESET  1
#else
    #define SMAA_PRESET  4
#endif
#define SMAA_PREDICATION 1  // 0 == off, see descriptions below
#define SMAA_EDGE_MODE   0  // 0 == color(quality), 1 == luma, 2 == depth(performance)
#define SMAA_DEBUG       1  // 0 == off, enable additional options to display texture in each stage

/*============================================================================================
 *              SMAA_PRESET_4 : static custom preset
============================================================================================*/
// Under same settings, static custom preset will have better performance then SMAA_PRESET_5 UI tweak.
// detail descriptions below.

#define SMAA_THRESHOLD              0.05
#define SMAA_MAX_SEARCH_STEPS       98
#define SMAA_MAX_SEARCH_STEPS_DIAG  20
#define SMAA_CORNER_ROUNDING        25

// Predicated thresholding \\
#define SMAA_PREDICATION_THRESHOLD  0.01
#define SMAA_PREDICATION_SCALE      2.0
#define SMAA_PREDICATION_STRENGTH   0.4

//-------------------Internal resource & helpers-------------------------------------------------------------------------------
#if    SMAA_PRESET == 4
    #define SMAA_DEBUG 0
#elif SMAA_PRESET == 3
    #define SMAA_PRESET_ULTRA 1
    #define SMAA_DEBUG 0
#elif SMAA_PRESET == 2
    #define SMAA_PRESET_HIGH 1
    #define SMAA_DEBUG 0
#elif SMAA_PRESET == 1
    #define SMAA_PRESET_MEDIUM 1
    #define SMAA_DEBUG 0
#elif SMAA_PRESET == 0
    #define SMAA_PRESET_LOW 1
    #define SMAA_DEBUG 0
#endif

#define SMAA_HLSL_4_1 1 // actually using 5.0 here, SMAA header use 4.1 profile
#define SMAA_PIXEL_SIZE float2( ScreenSize.y, ScreenSize.y * ScreenSize.z)
#include "Include/Functions/enbeffectpostpass/SMAA.h"

#define SMAA_STRING(a) #a
#ifndef SMAA_EDGE_TEX
#define SMAA_EDGE_TEX   RenderTargetRGB32F
#endif
#ifndef SMAA_BLEND_TEX
#define SMAA_BLEND_TEX  RenderTargetRGBA64
#endif


Texture2D SMAA_AreaTex   < string UIName = "SMAA Area Tex";   string ResourceName = "Include/Textures/SMAA/SMAA_AreaTex.dds";   >;
Texture2D SMAA_SearchTex < string UIName = "SMAA Search Tex"; string ResourceName = "Include/Textures/SMAA/SMAA_SearchTex.dds"; >;

struct VS_INPUT_SMAA
{
	float3 pos   : POSITION;
	float2 coord : TEXCOORD0;
};

struct VS_OUTPUT_SMAA
{
    float4 svPosition : SV_POSITION;
    float2 texcoord   : TEXCOORD0;
    float4 offset[3]  : TEXCOORD1;
};

//----------------------------------------------------------------------------------------------------------------------------
//full screen quad
void VS_SMAAClear( VS_INPUT_SMAA i, out float4 svPosition : SV_POSITION, out float2 texcoord : TEXCOORD0) {
	svPosition = float4(i.pos, 1.0);
    texcoord   = i.coord;
}

//clear up buffer
float4 PS_SMAAClear( float4 position : SV_POSITION, float2 texcoord : TEXCOORD0) : SV_Target {
    return 0;
}

//original tex for bypass texture
float4 PS_SMAAOriginal( float4 position : SV_POSITION, float2 texcoord : TEXCOORD0) : SV_Target {
    return TextureColor.Sample(LinearSampler, texcoord);
}

//----------------------------------------------------------------------------------------------------------------------------
void VS_SMAAEdgeDetection( VS_INPUT_SMAA i, out VS_OUTPUT_SMAA o) {
    o.texcoord = i.coord;
    SMAAEdgeDetectionVS(float4(i.pos, 1.0), o.svPosition, o.texcoord, o.offset);
}

float4 PS_SMAAEdgeDetection( VS_OUTPUT_SMAA i) : SV_Target {
#if SMAA_EDGE_MODE == 1
    return float4(SMAALumaEdgeDetectionPS( i.texcoord, i.offset, TextureColor
    #if SMAA_PREDICATION == 1
        , TextureDepth
    #endif
        ).xyz, 1.0);
#elif SMAA_EDGE_MODE == 2
    return float4(SMAADepthEdgeDetectionPS( i.texcoord, i.offset, TextureDepth).xyz, 1.0);
#else
    return float4(SMAAColorEdgeDetectionPS( i.texcoord, i.offset, TextureColor
    #if SMAA_PREDICATION == 1
        , TextureDepth
    #endif
        ).xyz, 1.0);
#endif
}

//----------------------------------------------------------------------------------------------------------------------------
void VS_SMAABlendingWeightCalculation( VS_INPUT_SMAA i, out VS_OUTPUT_SMAA o) {
    float2 null;
    o.texcoord = i.coord;
    SMAABlendingWeightCalculationVS(float4(i.pos, 1.0), o.svPosition, o.texcoord, null, o.offset);
}

float4 PS_SMAABlendingWeightCalculation( VS_OUTPUT_SMAA i) : SV_Target {
    if(SMAA_EDGE_TEX.Sample(LinearSampler, i.texcoord).a < 0.5) discard;
    return SMAABlendingWeightCalculationPS( i.texcoord, i.svPosition.xy, i.offset, SMAA_EDGE_TEX, SMAA_AreaTex, SMAA_SearchTex, 0);
}

//----------------------------------------------------------------------------------------------------------------------------
void VS_SMAANeighborhoodBlending( VS_INPUT_SMAA i, out VS_OUTPUT_SMAA o) {
    float4 offset[2];
    o.texcoord   = i.coord;
    SMAANeighborhoodBlendingVS(float4(i.pos, 1.0), o.svPosition, o.texcoord, offset);
    o.offset[0] = offset[0];
    o.offset[1] = offset[1];
    o.offset[2] = float4(0.0, 0.0, 0.0, 0.0);
}

float4 PS_SMAANeighborhoodBlending( VS_OUTPUT_SMAA i) : SV_Target {
#if SMAA_DEBUG == 1
    if     (smaa_showstagetex < 0.5) return TextureColor.Sample(LinearSampler, i.texcoord);
    else if(smaa_showstagetex < 1.5) return SMAA_EDGE_TEX.Sample(LinearSampler, i.texcoord);
    else if(smaa_showstagetex < 2.5) return SMAA_BLEND_TEX.Sample(LinearSampler, i.texcoord);
#endif
    float4 offset[2] = {i.offset[0], i.offset[1]};
    return SMAANeighborhoodBlendingPS( i.texcoord, offset, TextureColor, SMAA_BLEND_TEX);
}
#endif  // end of header.
