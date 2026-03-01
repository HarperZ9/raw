//=========//
// Options //
//=========//

#define Use_LensDirt 0

//==========//
// Textures //
//==========//
Texture2D			TextureDownsampled;  //color R16B16G16A16 64 bit or R11G11B10 32 bit hdr format. 1024*1024 size
Texture2D			TextureColor;        //color which is output of previous technique (except when drawed to temporary render target), R16B16G16A16 64 bit hdr format. 1024*1024 size

Texture2D			TextureOriginal;     //color R16B16G16A16 64 bit or R11G11B10 32 bit hdr format, screen size. PLEASE AVOID USING IT BECAUSE OF ALIASING ARTIFACTS, UNLESS YOU FIX THEM
Texture2D			TextureDepth;        //scene depth R32F 32 bit hdr format, screen size. PLEASE AVOID USING IT BECAUSE OF ALIASING ARTIFACTS, UNLESS YOU FIX THEM
Texture2D			TextureAperture;     //this frame aperture 1*1 R32F hdr red channel only. computed in PS_Aperture of enbdepthoffield.fx

//temporary textures which can be set as render target for techniques via annotations like <string RenderTarget="RenderTargetRGBA32";>
Texture2D			RenderTarget1024;    //R16B16G16A16F 64 bit hdr format, 1024*1024 size
Texture2D			RenderTarget512;     //R16B16G16A16F 64 bit hdr format, 512*512 size
Texture2D			RenderTarget256;     //R16B16G16A16F 64 bit hdr format, 256*256 size
Texture2D			RenderTarget128;     //R16B16G16A16F 64 bit hdr format, 128*128 size
Texture2D			RenderTarget64;      //R16B16G16A16F 64 bit hdr format, 64*64 size
Texture2D			RenderTarget32;      //R16B16G16A16F 64 bit hdr format, 32*32 size
Texture2D			RenderTarget16;      //R16B16G16A16F 64 bit hdr format, 16*16 size
Texture2D			RenderTargetRGBA32;  //R8G8B8A8 32 bit ldr format, screen size
Texture2D			RenderTargetRGBA64F; //R16B16G16A16F 64 bit hdr format, screen size

#if Use_LensDirt
Texture2D           DirtTexLarge              <string ResourceName="Include/Textures/LensDirt Large.png"; >;
Texture2D           DirtTexFine               <string ResourceName="Include/Textures/LensDirt Fine.png"; >;
Texture2D           DirtTexDense              <string ResourceName="Include/Textures/LensDirt Dense.png"; >;
#endif

// Include Needes Values
#include "Include/Helper.fxh"
#include "Include/ReforgedUI.fxh"

//=====//
// GUI //
//=====//

UI_FLOAT(Threshold,                 "Bloom Threshold",         0.0, 1.0, 0.5)
UI_FLOAT(BloomSensitivity,          "Bloom Sensitivity",       1.0, 5.0, 1.5)
UI_FLOAT(BloomRadius,               "Bloom Radius",            0.5, 2.0, 1.0)
UI_FLOAT(BloomPower,                "Bloom Intensity",         0.0, 10.0, 1.0)
UI_FLOAT(Bloom_Saturation,          "Bloom Saturation",        0.0, 3.0, 1.0)
UI_FLOAT3(Bloom_Tint,               "Bloom Tint",              0.0, 0.0, 0.0)


#if Use_LensDirt
UI_INT(Dirt_Power,                  "Dirt Intensity",          0.0, 50.0, 1.0)
UI_FLOAT3(Dirt_Tint,                "Dirt Tint",               0.0, 0.0, 0.0)
UI_BOOL(Dirt_Recolor,               "Recolor Dirt",            false)
UI_FLOAT3(Dirt_Color,               "Dirt Color",              0.0, 0.0, 0.0)
#endif

//===========//
// Functions //
//===========//

// Lil code by The Sandvich Maker
float3 applyThreshold(float3 Color)
{
    float luma    = GetLuma(Color, Rec709);
    Color        /= max(luma, 0.001);
    luma          = max(0.0, luma - Threshold);
    return Color *= luma;
}

//===============//
// Pixel Shaders //
//===============//

float3	PS_Prepass(VS_OUTPUT IN, uniform Texture2D inputtex) : SV_Target
{
	float2 coord     = IN.txcoord.xy;
	float3 Color     = inputtex.Sample(LinearSampler, coord);
	Color            = pow(Color, BloomSensitivity);
	Color            = applyThreshold(Color);
	return Color;
}

float4  PS_GaussianBlurH(VS_OUTPUT IN, uniform Texture2D inputtex, uniform float texsize) : SV_Target
{
    float2 coord     = IN.txcoord.xy;
	float2 pixelsize = (1/texsize) * float2(1, ScreenSize.z);
	float4 Blur      = 0;
	float  WeightSum = 0;

	float Offsets[5] = {0.3, 0.5, 1.0,  2.0, 3.0};
	float Weights[5] = {3.0, 2.5, 1.25, 1.0, 0.5};

    for (int i = 1; i < 5; i++)
    {
		float Offset = Offsets[i] * BloomRadius;
		Blur += inputtex.Sample(LinearSampler, coord + float2(Offset, 0) * pixelsize) * Weights[i];
		Blur += inputtex.Sample(LinearSampler, coord - float2(Offset, 0) * pixelsize) * Weights[i];
		WeightSum += Weights[i];
	}

	Blur /= WeightSum * 2; // 2 times cuz... i guess math

	Blur = max(Blur, 0.0);
	Blur = min(Blur, 16384.0);

	return Blur;
}

float4  PS_GaussianBlurV(VS_OUTPUT IN, uniform Texture2D inputtex, uniform float texsize) : SV_Target
{
    float2 coord     = IN.txcoord.xy;
	float2 pixelsize = (1/texsize) * float2(1, ScreenSize.z);
	float4 Blur      = 0;
	float  WeightSum = 0;

	float Offsets[5] = {0.3, 0.5, 1.0,  2.0, 3.0};
	float Weights[5] = {3.0, 2.5, 1.25, 1.0, 0.5};

    for (int i = 1; i < 5; i++)
    {
		float Offset = Offsets[i] * BloomRadius;
		Blur += inputtex.Sample(LinearSampler, coord + float2(0, Offset) * pixelsize) * Weights[i];
		Blur += inputtex.Sample(LinearSampler, coord - float2(0, Offset) * pixelsize) * Weights[i];
		WeightSum += Weights[i];
	}

	Blur /= WeightSum * 2; // 2 times cuz... i guess math

	Blur = max(Blur, 0.0);
	Blur = min(Blur, 16384.0);

	return Blur;
}

float3	PS_BloomMix(VS_OUTPUT IN) : SV_Target
{
    float2 coord = IN.txcoord.xy;

    float3 Bloom = RenderTarget1024.Sample (LinearSampler, coord).rgb;
    Bloom       += RenderTarget512.Sample  (LinearSampler, coord).rgb;
    Bloom       += RenderTarget256.Sample  (LinearSampler, coord).rgb;
    Bloom       += RenderTarget128.Sample  (LinearSampler, coord).rgb;
    Bloom       += RenderTarget64.Sample   (LinearSampler, coord).rgb;
    Bloom       += RenderTarget32.Sample   (LinearSampler, coord).rgb;
    Bloom       += RenderTarget16.Sample   (LinearSampler, coord).rgb;
	Bloom       /= 7 / BloomPower; // devide by the amount of added textures(7). Lower it to get more bloom

	Bloom        = max(Bloom, 0.0);
	Bloom        = min(Bloom, 16384.0);

	return Bloom;
}

float4  PS_Postpass(VS_OUTPUT IN) : SV_Target
{
    float2 coord = IN.txcoord.xy;
    float4 Color = TextureColor.Sample(LinearSampler, coord);
    Color.rgb   *= Bloom_Tint;
    Color        = lerp(GetLuma(Color, Rec709), Color, Bloom_Saturation);

#if Use_LensDirt == 1
    float4 Dirt  = DirtTex.Sample(LinearSampler, coord) * LensDirtPower;

    if (Dirt_Recolor)
    {
        Dirt  = GetLuma(Dirt, Rec709);               // make it gray
        Dirt *= (1.0 - Color.rgb) * Dirt_Color;      // Give it back Color
    }
    Dirt  *= Dirt_Tint * 2.55;
    Color *= Dirt; // Mix lol
#endif

    return          Color;
}
//============//
// Techniques //
//============//

technique11 Blum <string UIName="Nordwind Bloom"; string RenderTarget="RenderTarget1024";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
    SetPixelShader (CompileShader(ps_5_0, PS_Prepass(TextureDownsampled)));
  }
}
technique11 Blum1
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
    SetPixelShader (CompileShader(ps_5_0, PS_GaussianBlurH(RenderTarget1024, 1536.0)));
  }
}
technique11 Blum2 <string RenderTarget="RenderTarget1024";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
    SetPixelShader (CompileShader(ps_5_0, PS_GaussianBlurV(TextureColor, 1536.0)));
  }
}
technique11 Blum3
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
    SetPixelShader (CompileShader(ps_5_0, PS_GaussianBlurH(RenderTarget1024, 512.0)));
  }
}
technique11 Blum4 <string RenderTarget="RenderTarget512";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
    SetPixelShader (CompileShader(ps_5_0, PS_GaussianBlurV(TextureColor, 512.0)));
  }
}
technique11 Blum5
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
    SetPixelShader (CompileShader(ps_5_0, PS_GaussianBlurH(RenderTarget512, 256.0)));
  }
}
technique11 Blum6 <string RenderTarget="RenderTarget256";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
    SetPixelShader (CompileShader(ps_5_0, PS_GaussianBlurV(TextureColor, 256.0)));
  }
}
technique11 Blum7
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
    SetPixelShader (CompileShader(ps_5_0, PS_GaussianBlurH(RenderTarget256, 128.0)));
  }
}
technique11 Blum8 <string RenderTarget="RenderTarget128";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
    SetPixelShader(CompileShader(ps_5_0, PS_GaussianBlurV(TextureColor, 128.0)));
  }
}
technique11 Blum9
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
    SetPixelShader (CompileShader(ps_5_0, PS_GaussianBlurH(RenderTarget128, 64.0)));
  }
}
technique11 Blum10 <string RenderTarget="RenderTarget64";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
    SetPixelShader (CompileShader(ps_5_0, PS_GaussianBlurV(TextureColor, 64.0)));
  }
}
technique11 Blum11
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
    SetPixelShader(CompileShader (ps_5_0, PS_GaussianBlurH(RenderTarget64, 32.0)));
  }
}
technique11 Blum12 <string RenderTarget="RenderTarget32";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
    SetPixelShader (CompileShader(ps_5_0, PS_GaussianBlurV(TextureColor, 32.0)));
  }
}
technique11 Blum13
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
    SetPixelShader(CompileShader (ps_5_0, PS_GaussianBlurH(RenderTarget32, 16.0)));
  }
}
technique11 Blum14 <string RenderTarget="RenderTarget16";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
    SetPixelShader (CompileShader(ps_5_0, PS_GaussianBlurV(TextureColor, 16.0)));
  }
}
technique11 Blum15
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
    SetPixelShader (CompileShader(ps_5_0, PS_BloomMix()));
  }
}

technique11 Blum16
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
    SetPixelShader (CompileShader(ps_5_0, PS_Postpass()));
  }
}
