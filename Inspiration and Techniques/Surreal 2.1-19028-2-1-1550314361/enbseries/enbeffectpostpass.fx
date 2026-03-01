//================= V2.0 ===================//
//     _____                            __  //
//    / ___/__  _______________  ____ _/ /  //
//    \__ \/ / / / ___/ ___/ _ \/ __ `/ /   //
//   ___/ / /_/ / /  / /  /  __/ /_/ / /    //
//  /____/\__,_/_/  /_/   \___/\__,_/_/     //
//                                          //
//==========================================//
// Gaussian Bloom by Prod80                 //
// ENBSMAA and Lut Code by kingeric1992     //
// LumaSharp, Curves and Vibrance by Ceejay //
// Crop Assistant by Wolrajh                //
// Setup and Tweaked by Adyss               //
//==========================================//

//==========//
// Textures //
//==========//
Texture2D			TextureOriginal;     //color R10B10G10A2 32 bit ldr format
Texture2D			TextureColor;        //color which is output of previous technique (except when drawed to temporary render target), R10B10G10A2 32 bit ldr format
Texture2D			TextureDepth;        //scene depth R32F 32 bit hdr format

Texture2D			RenderTargetRGBA32;  //R8G8B8A8 32 bit ldr format
Texture2D			RenderTargetRGBA64;  //R16B16G16A16 64 bit ldr format
Texture2D			RenderTargetRGBA64F; //R16B16G16A16F 64 bit hdr format
Texture2D			RenderTargetR16F;    //R16F 16 bit hdr format with red channel only
Texture2D			RenderTargetR32F;    //R32F 32 bit hdr format with red channel only
Texture2D			RenderTargetRGB32F;  //32 bit hdr format without alpha

// Include Needes Values
#include "Shaders/ENBcommon.fxh"
#include "Shaders/Globals.fxh"
#include "Shaders/ReforgedUI.fxh"

// UI
UI_SEPARATOR_CUSTOM                     ("\xD7 Surreal Post FX V2.0 \xD7")
UI_WHITESPACE(1)
#define UI_CATEGORY Sharpening
UI_SEPARATOR
UI_FLOAT_EI(LSharpStrength,             "Image Sharpness",         0.0, 3.0, 1.0)
UI_WHITESPACE(3)
#define UI_CATEGORY Manual
UI_SEPARATOR_CUSTOM("Manual Color")
UI_BOOL(ManualColors,                   "Toggle Manual Edits",     true)
UI_FLOAT_EI(Exposure,                   "Exposure",               -1.0, 1.0, 0.0)
UI_FLOAT_EI(Contrast,                   "Contrast",                0.0, 1.0, 0.0)
UI_FLOAT_EI(PostGamma,                  "Gamma",                   0.0, 2.2, 1.0)
UI_FLOAT_EI(Vibrance,                   "Vibrance",               -1.0, 1.0, 0.0)
UI_FLOAT3_DNI(RGBBalance,               "RGB Saturation",          1.0, 1.0, 1.0)
UI_WHITESPACE(4)
#define UI_CATEGORY Cam
UI_SEPARATOR_CUSTOM                     ("Camera Effects")
#define UI_PREFIX_MODE PREFIX
UI_BOOL(ENABLE_CROPPREVIEW,             "Toggle Letterbox",        true)
UI_FLOAT(Wratio,                        "Letterbox Size",          0.0, 30.0, 0.1)
UI_FLOAT(VignetteAmount,                "Vignette Scale",          0.0, 1.0, 0.2)


//===========//
// Functions //
//===========//

#include "Shaders/SMAA/enbsmaa.fx"
#include "Shaders/Vignette.fxh"
#include "Shaders/Dither.fxh"
#include "Shaders/Vibrance.fxh"
#include "Shaders/Curves.fxh"
#include "Shaders/LumaSharp.fxh"
#include "Shaders/FXAA.fxh"
#include "Shaders/Letterbox.fxh"
#include "Shaders/ACES.fxh"
#include "Shaders/TemporalBloom.fxh"

//===============//
// Pixel Shaders //
//===============//



float4 PS_PostFX(VS_OUTPUT IN) : SV_Target
{
	float2 coord = IN.txcoord.xy;
	float4 Color = TextureColor.Sample(LinearSampler, coord);
    if (ManualColors==true) {
    Color       *= pow(2.0f, Exposure);
    Color        = pow(Color, PostGamma);
    Color        = Curves(Color);
    Color.rgb    = VibrancePass(Color);
    }

	Color.rgb    = ACESFilm(Color);
	Color        = Vignette(Color, coord);
	Color        = LumaSharp(Color, coord);
	return saturate(Color);
}


technique11 noAA <string UIName="no AA";>
{
  pass p0
    {
       SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
       SetPixelShader (CompileShader(ps_5_0, PS_PostFX()));
    }
}

technique11  noAA2
{
  pass p0
    {
       SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
       SetPixelShader (CompileShader(ps_5_0, PS_TempBloom()));
    }
}

technique11  noAA3
{
  pass p0
    {
       SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
       SetPixelShader (CompileShader(ps_5_0, PS_wolCropPreview()));
    }
}

technique11 post <string RenderTarget= SMAA_STRING(SMAA_EDGE_TEX); string UIName= "SMAA";>
{
    pass Clear
    {
        SetVertexShader(CompileShader(vs_5_0, VS_SMAAClear()));
        SetPixelShader (CompileShader(ps_5_0, PS_SMAAClear()));
    }

    pass EdgeDetection
    {
        SetVertexShader(CompileShader(vs_5_0, VS_SMAAEdgeDetection()));
        SetPixelShader (CompileShader(ps_5_0, PS_SMAAEdgeDetection()));
    }
}

technique11 post1 <string RenderTarget=SMAA_STRING(SMAA_BLEND_TEX);>
{
    pass Clear
    {
        SetVertexShader(CompileShader(vs_5_0, VS_SMAAClear()));
        SetPixelShader (CompileShader(ps_5_0, PS_SMAAClear()));
    }

    pass BlendingWeightCalculation
    {
        SetVertexShader(CompileShader(vs_5_0, VS_SMAABlendingWeightCalculation()));
        SetPixelShader (CompileShader(ps_5_0, PS_SMAABlendingWeightCalculation()));
    }
}

technique11 post2
{
    pass NeighborhoodBlending
    {
        SetVertexShader(CompileShader(vs_5_0, VS_SMAANeighborhoodBlending()));
        SetPixelShader (CompileShader(ps_5_0, PS_SMAANeighborhoodBlending()));
    }
}

technique11 post3
{
  pass p0
    {
       SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
       SetPixelShader (CompileShader(ps_5_0, PS_PostFX()));
    }
}

technique11  post4
{
  pass p0
    {
       SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
       SetPixelShader (CompileShader(ps_5_0, PS_TempBloom()));
    }
}

technique11 post5
{
  pass p0
    {
       SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
       SetPixelShader (CompileShader(ps_5_0, PS_wolCropPreview()));
    }
}

technique11 FXAAPASS <string UIName="FXAA";>
{
  pass p0
    {
       SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
       SetPixelShader (CompileShader(ps_5_0, PS_FXAA()));
    }
}

technique11  FXAAPASS1
{
  pass p0
    {
       SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
       SetPixelShader (CompileShader(ps_5_0, PS_PostFX()));
    }
}


technique11  FXAAPASS2
{
  pass p0
    {
       SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
       SetPixelShader (CompileShader(ps_5_0, PS_TempBloom()));
    }
}

technique11  FXAAPASS3
{
  pass p0
    {
       SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
       SetPixelShader (CompileShader(ps_5_0, PS_wolCropPreview()));
    }
}


technique11 SMAAFXAA <string RenderTarget= SMAA_STRING(SMAA_EDGE_TEX); string UIName= "SMAA + FXAA";>
{
    pass Clear
    {
        SetVertexShader(CompileShader(vs_5_0, VS_SMAAClear()));
        SetPixelShader (CompileShader(ps_5_0, PS_SMAAClear()));
    }

    pass EdgeDetection
    {
        SetVertexShader(CompileShader(vs_5_0, VS_SMAAEdgeDetection()));
        SetPixelShader (CompileShader(ps_5_0, PS_SMAAEdgeDetection()));
    }
}

technique11 SMAAFXAA1 <string RenderTarget=SMAA_STRING(SMAA_BLEND_TEX);>
{
    pass Clear
    {
        SetVertexShader(CompileShader(vs_5_0, VS_SMAAClear()));
        SetPixelShader (CompileShader(ps_5_0, PS_SMAAClear()));
    }

    pass BlendingWeightCalculation
    {
        SetVertexShader(CompileShader(vs_5_0, VS_SMAABlendingWeightCalculation()));
        SetPixelShader (CompileShader(ps_5_0, PS_SMAABlendingWeightCalculation()));
    }
}

technique11 SMAAFXAA2
{
    pass NeighborhoodBlending
    {
        SetVertexShader(CompileShader(vs_5_0, VS_SMAANeighborhoodBlending()));
        SetPixelShader (CompileShader(ps_5_0, PS_SMAANeighborhoodBlending()));
    }
}

technique11 SMAAFXAA3
{
  pass p0
    {
       SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
       SetPixelShader (CompileShader(ps_5_0, PS_FXAA()));
    }
}

technique11 SMAAFXAA4
{
  pass p0
    {
       SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
       SetPixelShader (CompileShader(ps_5_0, PS_PostFX()));
    }
}

technique11  SMAAFXAA5
{
  pass p0
    {
       SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
       SetPixelShader (CompileShader(ps_5_0, PS_TempBloom()));
    }
}

technique11 SMAAFXAA6
{
  pass p0
    {
       SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
       SetPixelShader (CompileShader(ps_5_0, PS_wolCropPreview()));
    }
}
