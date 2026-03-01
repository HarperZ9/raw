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
    float2 halfPixelSize = pixelSize / 1.0f;
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

	// Sample top center
    texCoordSample.x = texCoord.x;
    texCoordSample.y = texCoord.y + dUV.y;    
    cOut += tex.Sample(Sampler1, texCoordSample ).xyz;
	
	// Sample right center
    texCoordSample.x = texCoord.x + dUV.x;
    texCoordSample.y = texCoord.y;
    cOut += tex.Sample(Sampler1, texCoordSample ).xyz;
	
	// Sample bottom center
    texCoordSample.x = texCoord.x;
    texCoordSample.y = texCoord.y - dUV.y;
    cOut += tex.Sample(Sampler1, texCoordSample ).xyz;
	
	// Sample left center
    texCoordSample.x = texCoord.x - dUV.x;
    texCoordSample.y = texCoord.y;
    cOut += tex.Sample(Sampler1, texCoordSample ).xyz;

    // Average 
    cOut *= 0.125f;
    
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

    res.xyz=max(res.xyz-(ECCInBlack*0.1), 0.0) / max(ECCInWhite-(ECCInBlack*0.1), 0.0001);
	if (fContrast!=1.0) res.xyz=pow(res.xyz, fContrast);
	res.xyz=res.xyz*(ECCOutWhite-ECCOutBlack) + ECCOutBlack;
  
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


  #if 0
    res.xyz=RenderTarget1024.Sample(Sampler1, IN.txcoord0.xy);
    res.xyz+=RenderTarget512.Sample(Sampler1, IN.txcoord0.xy);
    res.xyz+=RenderTarget256.Sample(Sampler1, IN.txcoord0.xy);
    res.xyz+=RenderTarget128.Sample(Sampler1, IN.txcoord0.xy);
    res.xyz+=RenderTarget64.Sample(Sampler1, IN.txcoord0.xy);
    res.xyz+=RenderTarget32.Sample(Sampler1, IN.txcoord0.xy);
    res.xyz /= 6;
  #else 
    res.xyz=RenderTarget1024.Sample(Sampler1, IN.txcoord0.xy)*0.5;
    res.xyz+=RenderTarget512.Sample(Sampler1, IN.txcoord0.xy)*0.8*0.75;
    res.xyz+=RenderTarget256.Sample(Sampler1, IN.txcoord0.xy)*0.6;
    res.xyz+=RenderTarget128.Sample(Sampler1, IN.txcoord0.xy)*0.45;
    res.xyz+=RenderTarget64.Sample(Sampler1, IN.txcoord0.xy) *0.32;
    res.xyz+=RenderTarget32.Sample(Sampler1, IN.txcoord0.xy) *0.23;
    res.xyz /= 1.0;
  #endif

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