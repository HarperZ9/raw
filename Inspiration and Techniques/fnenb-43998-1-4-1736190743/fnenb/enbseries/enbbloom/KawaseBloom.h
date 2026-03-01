//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Gaussian-like "Kawase blur" bloom. v1.1
// Ported by roxahris. Original algorithms by Filip S. and Masaki Kawase.
// Additional credits are below near the code. 
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// This is supposedly a very fast approximation of a gaussian blur. 
// Source: https://software.intel.com/en-us/blogs/2014/07/15/an-investigation-of-fast-real-time-gpu-based-image-blur-algorithms
float3 KawaseBlurFilter( Texture2D tex, float2 texCoord, float2 pixelSize, float iteration )
{
    float2 texCoordSample;
    float2 halfPixelSize = pixelSize / 2.0f;
    float2 dUV = ( pixelSize.xy * float2( iteration, iteration ) ) + halfPixelSize.xy;

    float3 cOut;

    // Sample top left pixel
    texCoordSample.x = texCoord.x - dUV.x;
    texCoordSample.y = texCoord.y + dUV.y;    
    cOut = tex.Sample(Sampler1, texCoordSample ).xyz;

    // Sample top right pixel
    texCoordSample.x = texCoord.x + dUV.x;
    texCoordSample.y = texCoord.y + dUV.y;
    cOut += tex.Sample(Sampler1, texCoordSample ).xyz;

    // Sample bottom right pixel
    texCoordSample.x = texCoord.x + dUV.x;
    texCoordSample.y = texCoord.y - dUV.y;
    cOut += tex.Sample(Sampler1, texCoordSample ).xyz;

    // Sample bottom left pixel
    texCoordSample.x = texCoord.x - dUV.x;
    texCoordSample.y = texCoord.y - dUV.y;
    cOut += tex.Sample(Sampler1, texCoordSample ).xyz;

    // Average 
    cOut *= 0.25f;
    
    return cOut;
}

float4  PS_KawaseBloom(VS_OUTPUT_POST IN, float4 v0 : SV_Position0,
  uniform Texture2D inputtex, uniform float destsize, uniform float iteration) : SV_Target
{
  float4  res;

  //float2 pxSize = (1/destsize)*float2(1, ScreenSize.z);  // Visibly blocky
  float2 pxSize = (1/(destsize*2))*float2(1, ScreenSize.z);

  res.xyz=KawaseBlurFilter(inputtex, IN.txcoord0.xy, pxSize, iteration);

  #if 0 // Double blur. This removes artifacing in the raw bloom texture.
  // However, it costs more than ENB's bloom and does like 60 passes. 
  // The aliasing is only noticeable at 100% bloom anyway. 
  res.xyz+=KawaseBlurFilter(inputtex, IN.txcoord0.xy, pxSize, iteration+1);
  res.xyz/=2;
  #endif
  
  res=max(res, 0.0);
  res=min(res, 16384.0);

  res.w=1.0;
  return res;
}

float4  PS_KawaseBloomFirst(VS_OUTPUT_POST IN, float4 v0 : SV_Position0,
  uniform Texture2D inputtex, uniform float destsize, uniform float iteration) : SV_Target
{
  float4  res;

  //float2 pxSize = (1/destsize)*float2(1, ScreenSize.z);  // Visibly blocky
  float2 pxSize = (1/(destsize*2))*float2(1, ScreenSize.z);

  res.xyz=KawaseBlurFilter(inputtex, IN.txcoord0.xy, pxSize, iteration);

  #if 0 // Double blur. This removes artifacing in the raw bloom texture.
  // However, it costs more than ENB's bloom and does like 60 passes. 
  // The aliasing is only noticeable at 100% bloom anyway. 
  res.xyz+=KawaseBlurFilter(inputtex, IN.txcoord0.xy, pxSize, iteration+1);
  res.xyz/=2;
  #endif
   float KRECCInBlack,KRECCInWhite,KRfContrast,KRECCOutWhite,KRECCOutBlack;
	  KRECCInBlack = CAV(ECCInBlackDawn/10,ECCInBlackSunrise/10,ECCInBlackD/10,ECCInBlackSunset/10,ECCInBlackDusk/10,ECCInBlackN/10,ECCInBlackI/10);
	  KRECCInWhite = CAV(ECCInWhiteDawn,ECCInWhiteSunrise,ECCInWhiteD,ECCInWhiteSunset,ECCInWhiteDusk,ECCInWhiteN,ECCInWhiteI);
	  KRfContrast = CAV (fContrastDawn,fContrastSunrise,fContrastD,fContrastSunset,fContrastDusk,fContrastN,fContrastI);
	  KRECCOutWhite =CAV (ECCOutWhiteDawn,ECCOutWhiteSunrise,ECCOutWhiteD,ECCOutWhiteSunset,ECCOutWhiteDusk,ECCOutWhiteN,ECCOutWhiteI);
	  KRECCOutBlack =CAV (ECCOutBlackDawn,ECCOutBlackSunrise,ECCOutBlackD,ECCOutBlackSunset,ECCOutBlackDusk,ECCOutBlackN,ECCOutBlackI);
  
    res.xyz=max(res.xyz-KRECCInBlack, 0.0) / max(KRECCInWhite-KRECCInBlack, 0.0001);
	if (KRfContrast!=1.0) res.xyz=pow(res.xyz, KRfContrast);
	res.xyz=res.xyz*(KRECCOutWhite-KRECCOutBlack) + KRECCOutBlack;
  
  res=max(res, 0.0);
  res=min(res, 16384.0);

  res.w=1.0;
  return res;
}

float4  PS_KawaseMix(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
  float4  res;

    res.xyz=RenderTarget1024.Sample(Sampler1, IN.txcoord0.xy);

  res=max(res, 0.0);
  res=min(res, 16384.0);

  res.w=1.0;
  return res;
}

float4  PS_KawaseMix2(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
  float4  res=0;
  
    float Pass_select1_int =CAV(Pass_select1_intDawn,Pass_select1_intSunrise,Pass_select1E_int,Pass_select1_intSunset,Pass_select1_intDusk,Pass_select1_intNight,IPass_select1_int);
	float Pass_select2_int =CAV(Pass_select2_intDawn,Pass_select2_intSunrise,Pass_select2_intE,Pass_select2_intSunset,Pass_select2_intDusk,Pass_select2_intNight,IPass_select2_int);
	float Pass_select3_int =CAV(Pass_select3_intDawn,Pass_select3_intSunrise,Pass_select3_intE,Pass_select3_intSunset,Pass_select3_intDusk,Pass_select3_intNight,IPass_select3_int);
	float Pass_select4_int =CAV(Pass_select4_intDawn,Pass_select4_intSunrise,Pass_select4_intE,Pass_select4_intSunset,Pass_select4_intDusk,Pass_select4_intNight,IPass_select4_int);
	float Pass_select5_int =CAV(Pass_select5_intDawn,Pass_select5_intSunrise,Pass_select5_intE,Pass_select5_intSunset,Pass_select5_intDusk,Pass_select5_intNight,IPass_select5_int);
	float Pass_select6_int =CAV(Pass_select6_intDawn,Pass_select6_intSunrise,Pass_select6_intE,Pass_select6_intSunset,Pass_select6_intDusk,Pass_select6_intNight,IPass_select6_int);
	float Pass_select7_int =CAV(Pass_select7_intDawn,Pass_select7_intSunrise,Pass_select7_intE,Pass_select7_intSunset,Pass_select7_intDusk,Pass_select7_intNight,IPass_select7_int);
	
	if(ENABLE_KawaseMIXR)
	{
		Pass_select1_int =1;
		Pass_select2_int =1;
		Pass_select3_int =1;
		Pass_select4_int =1;
		Pass_select5_int =1;
		Pass_select6_int =1;
		Pass_select7_int =1;
		
	}
  float vIntensity=CAV(IntensityDawn,IntensitySunrise,IntensityDay,IntensitySunset,IntensityDusk,IntensityNight,IntensityI);
	
	
  #if 0
    res.xyz= Pass_select1_int*lerp(0,RenderTarget1024.Sample(Sampler1, IN.txcoord0.xy),Pass_select1);
    res.xyz+=Pass_select2_int*lerp(0,RenderTarget256.Sample(Sampler1, IN.txcoord0.xy),Pass_select2);
    res.xyz+=Pass_select3_int*lerp(0,RenderTarget512.Sample(Sampler1, IN.txcoord0.xy),Pass_select3);
    res.xyz+=Pass_select4_int*lerp(0,RenderTarget128.Sample(Sampler1, IN.txcoord0.xy),Pass_select4);
    res.xyz+=Pass_select5_int*lerp(0,RenderTarget64.Sample(Sampler1, IN.txcoord0.xy), Pass_select5);
    res.xyz+=Pass_select6_int*lerp(0,RenderTarget32.Sample(Sampler1, IN.txcoord0.xy), Pass_select6);
	 res.xyz+=Pass_select7_int*lerp(0,RenderTarget16.Sample(Sampler1, IN.txcoord0.xy), Pass_select7);
    res.xyz /= 6;
  #else 
    res.xyz= Pass_select1_int*lerp(0,RenderTarget1024.Sample(Sampler1, IN.txcoord0.xy)*0.5    ,Pass_select1);
    res.xyz+=Pass_select2_int*lerp(0,RenderTarget512.Sample(Sampler1, IN.txcoord0.xy)*0.8*0.75,Pass_select2);
    res.xyz+=Pass_select3_int*lerp(0,RenderTarget256.Sample(Sampler1, IN.txcoord0.xy)*0.6     ,Pass_select3);
    res.xyz+=Pass_select4_int*lerp(0,RenderTarget128.Sample(Sampler1, IN.txcoord0.xy)*0.45    ,Pass_select4);
    res.xyz+=Pass_select5_int*lerp(0,RenderTarget64.Sample(Sampler1, IN.txcoord0.xy) *0.32    ,Pass_select5);
    res.xyz+=Pass_select6_int*lerp(0,RenderTarget32.Sample(Sampler1, IN.txcoord0.xy) *0.23    ,Pass_select6);
	 res.xyz+=Pass_select7_int*lerp(0,RenderTarget16.Sample(Sampler1, IN.txcoord0.xy) *0.23    ,Pass_select7);
    res.xyz /= 2.2;
  #endif
  	float divider		= Pass_select1 + Pass_select2 + Pass_select3 + Pass_select4 + Pass_select5 + Pass_select6 + Pass_select7;
	float int_sum		= ( Pass_select1 * Pass_select1_int ) + ( Pass_select2 * Pass_select2_int ) + ( Pass_select3 * Pass_select3_int ) + ( Pass_select4 * Pass_select4_int ) + ( Pass_select5 * Pass_select5_int ) + ( Pass_select6 * Pass_select6_int ) + ( Pass_select7 * Pass_select7_int );
	float multiplier	= divider / int_sum;
	//res.xyz			*= ( 1.0f / divider );
	res.xyz			*= multiplier;
	res.xyz 			*= vIntensity/CAI(KawaseBloomCorrection,KawaseBloomCorrectionI);
  res=max(res, 0.0);
  res=min(res, 16384.0);

  res.w=1.0;
  return res;
}


//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
technique11 KawaseBloomPass <string UIName="Kawase blur bloom (5 passes)"; string RenderTarget="RenderTarget1024";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_KawaseBloom(TextureDownsampled, 1024.0, 0)));
  }
}
technique11 KawaseBloomPass1 
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_KawaseBloom(RenderTarget1024, 1024.0, 1)));
  }
}
technique11 KawaseBloomPass2
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_KawaseBloom(TextureColor, 1024.0, 2)));
  }
}
technique11 KawaseBloomPass3
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_KawaseBloom(TextureColor, 1024.0, 2)));
  }
}
technique11 KawaseBloomPass4 <string RenderTarget="RenderTarget1024";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_KawaseBloom(TextureColor, 1024.0, 3)));
  }
}
technique11 KawaseBloomPass5
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_KawaseMix()));
  }
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
technique11 MKawaseBloomPass <string UIName="Kawase blur bloom (25 passes)"; string RenderTarget="RenderTarget1024";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_KawaseBloomFirst(TextureDownsampled, 1024.0, 0)));
  }
}
technique11 MKawaseBloomPass1 <string RenderTarget="RenderTarget512";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_KawaseBloomFirst(RenderTarget1024, 512.0, 1)));
  }
}
technique11 MKawaseBloomPass2 <string RenderTarget="RenderTarget256";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_KawaseBloom(RenderTarget512, 256.0, 1)));
  }
}
technique11 MKawaseBloomPass3 <string RenderTarget="RenderTarget128";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_KawaseBloom(RenderTarget256, 128.0, 1)));
  }
}
technique11 MKawaseBloomPass4 <string RenderTarget="RenderTarget64";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_KawaseBloom(RenderTarget128, 64.0, 1)));
  }
}
technique11 MKawaseBloomPass5 <string RenderTarget="RenderTarget32";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_KawaseBloom(RenderTarget64, 32.0, 1)));
  }
}
// Passes 1024
technique11 MKawaseBloomPass6
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_KawaseBloomFirst(RenderTarget1024, 1024.0, 1)));
  }
}
technique11 MKawaseBloomPass7
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_KawaseBloomFirst(TextureColor, 1024.0, 2)));
  }
}
technique11 MKawaseBloomPass8
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_KawaseBloomFirst(TextureColor, 1024.0, 2)));
  }
}
technique11 MKawaseBloomPass9 <string RenderTarget="RenderTarget1024";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_KawaseBloomFirst(TextureColor, 1024.0, 3)));
  }
}
// Passes 512
technique11 MKawaseBloomPass10
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_KawaseBloomFirst(TextureColor, 512.0, 2)));
  }
}
technique11 MKawaseBloomPass11
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_KawaseBloomFirst(TextureColor, 512.0, 2)));
  }
}
technique11 MKawaseBloomPass12 <string RenderTarget="RenderTarget512";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_KawaseBloomFirst(TextureColor, 512.0, 3)));
  }
}

// Passes 256
technique11 MKawaseBloomPass13
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_KawaseBloom(TextureColor, 256.0, 2)));
  }
}
technique11 MKawaseBloomPass14
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_KawaseBloom(TextureColor, 256.0, 2)));
  }
}
technique11 MKawaseBloomPass15 <string RenderTarget="RenderTarget256";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_KawaseBloom(TextureColor, 256.0, 3)));
  }
}

// Passes 128
technique11 MKawaseBloomPass16
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_KawaseBloom(TextureColor, 128.0, 2)));
  }
}
technique11 MKawaseBloomPass17
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_KawaseBloom(TextureColor, 128.0, 2)));
  }
}
technique11 MKawaseBloomPass18 <string RenderTarget="RenderTarget128";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_KawaseBloom(TextureColor, 128.0, 3)));
  }
}

// Passes 64
technique11 MKawaseBloomPass19
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_KawaseBloom(TextureColor, 64.0, 2)));
  }
}
technique11 MKawaseBloomPass20
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_KawaseBloom(TextureColor, 64.0, 2)));
  }
}
technique11 MKawaseBloomPass21 <string RenderTarget="RenderTarget64";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_KawaseBloom(TextureColor, 64.0, 3)));
  }
}

// Passes 32
technique11 MKawaseBloomPass22
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_KawaseBloom(TextureColor, 32.0, 2)));
  }
}
technique11 MKawaseBloomPass23
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_KawaseBloom(TextureColor, 32.0, 2)));
  }
}
technique11 MKawaseBloomPass24 <string RenderTarget="RenderTarget32";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_KawaseBloom(TextureColor, 32.0, 3)));
  }
}

// End
technique11 MKawaseBloomPass25
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_KawaseMix2()));
  }
}