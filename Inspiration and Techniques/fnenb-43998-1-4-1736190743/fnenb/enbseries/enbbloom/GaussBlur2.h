//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Fast seperated gaussian bloom
// Potential future improvements: 
//    Moving offsets/weights to vertex shader?
//    Not using a switch in the final pass... done?
// Source: https://www.shadertoy.com/view/lstSRS
// Original author: SonicEther
// Additional credits are below near the relevant code. 
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++



// SHADERS ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

float3 ColorFetch(Texture2D inputtex, float2 coord)
{
 	return inputtex.Sample(Sampler1, coord).rgb;   
}

//Horizontal gaussian blur leveraging hardware filtering for fewer texture lookups.
float3  FuncHoriBlur(Texture2D inputtex, float2 uvsrc, float2 pxSize)
{    
float weights[5];
float offsets[5];
    
    weights[0] = 0.19638062;
    weights[1] = 0.29675293;
    weights[2] = 0.09442139;
    weights[3] = 0.01037598;
    weights[4] = 0.00025940;
    
    offsets[0] = 0.00000000;
    offsets[1] = 1.41176471;
    offsets[2] = 3.29411765;
    offsets[3] = 5.17647059;
    offsets[4] = 7.05882353;
    
    float2 uv = uvsrc; //fragCoord.xy / iResolution.xy;
    
    float3 color = 0.0;
    float weightSum = 0.0;
    
    //if (uv.x < 0.52)
    {
        color += ColorFetch(inputtex, uv) * weights[0];
        weightSum += weights[0];

        for(int i = 1; i < 5; i++)
        {
            float2 offset = offsets[i] * pxSize;
            color += ColorFetch(inputtex, uv + offset * float2(0.5, 0.0)) * weights[i];
            color += ColorFetch(inputtex, uv - offset * float2(0.5, 0.0)) * weights[i];
            weightSum += weights[i] * 2.0;
        }

        color /= weightSum;
    }

    return color;
}

//Vertical gaussian blur leveraging hardware filtering for fewer texture lookups.
float3  FuncVertBlur(Texture2D inputtex, float2 uvsrc, float2 pxSize)
{    
float weights[5];
float offsets[5];
    
    weights[0] = 0.19638062;
    weights[1] = 0.29675293;
    weights[2] = 0.09442139;
    weights[3] = 0.01037598;
    weights[4] = 0.00025940;
    
    offsets[0] = 0.00000000;
    offsets[1] = 1.41176471;
    offsets[2] = 3.29411765;
    offsets[3] = 5.17647059;
    offsets[4] = 7.05882353;
    
    float2 uv = uvsrc; //fragCoord.xy / iResolution.xy;
    
    float3 color = 0.0;
    float weightSum = 0.0;
    
    //if (uv.x < 0.52)
    {
        color += ColorFetch(inputtex, uv) * weights[0];
        weightSum += weights[0];

        for(int i = 1; i < 5; i++)
        {
            float2 offset = offsets[i] * pxSize;
            color += ColorFetch(inputtex, uv + offset * float2(0.0, 0.5)) * weights[i];
            color += ColorFetch(inputtex, uv - offset * float2(0.0, 0.5)) * weights[i];
            weightSum += weights[i] * 2.0;
        }

        color /= weightSum;
    }

    return color;
}

float4  PS_GaussHResize(VS_OUTPUT_POST IN, float4 v0 : SV_Position0,
  uniform Texture2D inputtex, uniform float texsize) : SV_Target
{
  float4  res;

  float2 pxSize = (1/(texsize))*float2(1, ScreenSize.z);
  res.xyz=FuncHoriBlur(inputtex, IN.txcoord0.xy, pxSize*2);

  res=max(res, 0.0);
  res=min(res, 16384.0);

  res.w=10.0;
  return res;
}

float4  PS_GaussVResize(VS_OUTPUT_POST IN, float4 v0 : SV_Position0,
  uniform Texture2D inputtex, uniform float texsize) : SV_Target
{
  float4  res;

  float2 pxSize = (1/(texsize))*float2(1, ScreenSize.z);
  res.xyz=FuncVertBlur(inputtex, IN.txcoord0.xy, pxSize*2);

  res=max(res, 0.0);
  res=min(res, 16384.0);

  res.w=10.0;
  return res;
}

float4  PS_GaussHResizeFirst(VS_OUTPUT_POST IN, float4 v0 : SV_Position0,
  uniform Texture2D inputtex, uniform float texsize) : SV_Target
{
  float4  res;

  float2 pxSize = (1/(texsize))*float2(1, ScreenSize.z);
  res.xyz=FuncHoriBlur(inputtex, IN.txcoord0.xy, pxSize*2);
  
  float RECCInBlack,RECCInWhite,RfContrast,RECCOutWhite,RECCOutBlack;
  RECCInBlack = CAV(ECCInBlackDawn/10,ECCInBlackSunrise/10,ECCInBlackD/10,ECCInBlackSunset/10,ECCInBlackDusk/10,ECCInBlackN/10,ECCInBlackI/10);
  RECCInWhite = CAV(ECCInWhiteDawn,ECCInWhiteSunrise,ECCInWhiteD,ECCInWhiteSunset,ECCInWhiteDusk,ECCInWhiteN,ECCInWhiteI);
  RfContrast = CAV (fContrastDawn,fContrastSunrise,fContrastD,fContrastSunset,fContrastDusk,fContrastN,fContrastI);
  RECCOutWhite =CAV (ECCOutWhiteDawn,ECCOutWhiteSunrise,ECCOutWhiteD,ECCOutWhiteSunset,ECCOutWhiteDusk,ECCOutWhiteN,ECCOutWhiteI);
  RECCOutBlack =CAV (ECCOutBlackDawn,ECCOutBlackSunrise,ECCOutBlackD,ECCOutBlackSunset,ECCOutBlackDusk,ECCOutBlackN,ECCOutBlackI);
  

  	res.xyz=max(res.xyz-RECCInBlack, 0.0) / max(RECCInWhite-RECCInBlack, 0.0001);
	if (RfContrast!=1.0) res.xyz=pow(res.xyz, RfContrast);
	res.xyz=res.xyz*(RECCOutWhite-RECCOutBlack) + RECCOutBlack;
  
  res=max(res, 0.0);
  res=min(res, 16384.0);

  res.w=10.0;
  return res;
}

float4  PS_GaussVResizeFirst(VS_OUTPUT_POST IN, float4 v0 : SV_Position0,
  uniform Texture2D inputtex, uniform float texsize) : SV_Target
{
  float4  res;
  
	float RECCInBlack,RECCInWhite,RfContrast,RECCOutWhite,RECCOutBlack;
	  RECCInBlack = CAV(ECCInBlackDawn/10,ECCInBlackSunrise/10,ECCInBlackD/10,ECCInBlackSunset/10,ECCInBlackDusk/10,ECCInBlackN/10,ECCInBlackI/10);
	  RECCInWhite = CAV(ECCInWhiteDawn,ECCInWhiteSunrise,ECCInWhiteD,ECCInWhiteSunset,ECCInWhiteDusk,ECCInWhiteN,ECCInWhiteI);
	  RfContrast = CAV (fContrastDawn,fContrastSunrise,fContrastD,fContrastSunset,fContrastDusk,fContrastN,fContrastI);
	  RECCOutWhite =CAV (ECCOutWhiteDawn,ECCOutWhiteSunrise,ECCOutWhiteD,ECCOutWhiteSunset,ECCOutWhiteDusk,ECCOutWhiteN,ECCOutWhiteI);
	  RECCOutBlack =CAV (ECCOutBlackDawn,ECCOutBlackSunrise,ECCOutBlackD,ECCOutBlackSunset,ECCOutBlackDusk,ECCOutBlackN,ECCOutBlackI);
	  

  float2 pxSize = (1/(texsize))*float2(1, ScreenSize.z);
  res.xyz=FuncVertBlur(inputtex, IN.txcoord0.xy, pxSize*2);

  	res.xyz=max(res.xyz-RECCInBlack, 0.0) / max(RECCInWhite-RECCInBlack, 0.0001);
	if (RfContrast!=1.0) res.xyz=pow(res.xyz, RfContrast);
	res.xyz=res.xyz*(RECCOutWhite-RECCOutBlack) + RECCOutBlack;
  
  res=max(res, 0.0);
  res=min(res, 16384.0);

  res.w=10.0;
  return res;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

float3 MixColorFetch(float i, float2 coord)
{   // Shader gods, have mercy.
    // This actually isn't optimised out by the compiler like I thought. Don't use this.
    switch (i) {
        case 0: //return RenderTarget1024; 
        return RenderTarget1024.Sample(Sampler1, coord);
        case 1: //return RenderTarget512; 
        return RenderTarget512.Sample(Sampler1, coord);
        case 2: //return RenderTarget256; 
        return RenderTarget256.Sample(Sampler1, coord);
        case 3: //return RenderTarget128; 
        return RenderTarget128.Sample(Sampler1, coord);
        case 4: //return RenderTarget64; 
        return RenderTarget64.Sample(Sampler1, coord);
        case 5: //return RenderTarget32; 
        return RenderTarget32.Sample(Sampler1, coord);
    }
    return 0; //TextureColor; //?!
}




float4  PS_GaussMix(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
  float4  res = 0.0;

  // Mercury bloom blending code
  // Source: https://imgur.com/a/MZD3l
  // This is kind of messy... sorry! 
  float weightSum = 0;
  int maxlevel = 6;
  #define TAU 6.28318
  /* UNROLL THIS
  for (int i = 0; i <= maxlevel; i++) {
    float weight = pow(i+1,post_mixer_bloomShape);
    weightSum += weight;
    float x = i*2;
    #ifdef GaussianBloomColorEffect
    res.xyz += MixColorFetch(i, IN.txcoord0.xy)*weight * (1 + post_mixer_bloomColor*float3(sin(x), sin(x+TAU/3), sin(x-TAU/3)));
    #else
    res.xyz += MixColorFetch(i, IN.txcoord0.xy)*weight; //
    #endif
  }*/

    // This should get optimised by the compiler. 
    float weight[7];
    float x[7];
	////////////
	

	float Rpost_mixer_bloomShape= CAV(post_mixer_bloomShapeDawn,post_mixer_bloomShapeSunrise,post_mixer_bloomShapeD,post_mixer_bloomShapeSunset,post_mixer_bloomShapeDusk,post_mixer_bloomShapeN,post_mixer_bloomShapeI);
	float4 RfSaturation=CAVF(fSaturationDawn,fSaturationSunrise,fSaturationD,fSaturationSunset,fSaturationDusk,fSaturationN,fSaturationI);	
	float3 Rpost_mixer_bloomColor;
	#if(GaussianBloomColorEffect != 0) 
	 Rpost_mixer_bloomColor=CAVFT(post_mixer_bloomColorDawn,post_mixer_bloomColorSunrise,post_mixer_bloomColorD,post_mixer_bloomColorSunset,post_mixer_bloomColorDusk,post_mixer_bloomColorN,post_mixer_bloomColorI);
	#endif
 
    /////////////
	

	float DSMULTI		= 1.0f;
	float Pass_select1_int =CAV(Pass_select1_intDawn,Pass_select1_intSunrise,Pass_select1E_int,Pass_select1_intSunset,Pass_select1_intDusk,Pass_select1_intNight,IPass_select1_int);
	float Pass_select2_int =CAV(Pass_select2_intDawn,Pass_select2_intSunrise,Pass_select2_intE,Pass_select2_intSunset,Pass_select2_intDusk,Pass_select2_intNight,IPass_select2_int);
	float Pass_select3_int =CAV(Pass_select3_intDawn,Pass_select3_intSunrise,Pass_select3_intE,Pass_select3_intSunset,Pass_select3_intDusk,Pass_select3_intNight,IPass_select3_int);
	float Pass_select4_int =CAV(Pass_select4_intDawn,Pass_select4_intSunrise,Pass_select4_intE,Pass_select4_intSunset,Pass_select4_intDusk,Pass_select4_intNight,IPass_select4_int);
	float Pass_select5_int =CAV(Pass_select5_intDawn,Pass_select5_intSunrise,Pass_select5_intE,Pass_select5_intSunset,Pass_select5_intDusk,Pass_select5_intNight,IPass_select5_int);
	float Pass_select6_int =CAV(Pass_select6_intDawn,Pass_select6_intSunrise,Pass_select6_intE,Pass_select6_intSunset,Pass_select6_intDusk,Pass_select6_intNight,IPass_select6_int);
	float Pass_select7_int =CAV(Pass_select7_intDawn,Pass_select7_intSunrise,Pass_select7_intE,Pass_select7_intSunset,Pass_select7_intDusk,Pass_select7_intNight,IPass_select7_int);
	
	if(ENABLE_MIXER)
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
	
	
	
	
    [unroll]
    for (int i=0; i <= maxlevel; i++) {
        weight[i] = pow(i+1, Rpost_mixer_bloomShape);
        weightSum += weight[i];
        x[i] = i*2;
    }
    
    if (GaussianBloomColorEffect!=0) {
    res.xyz += Pass_select1_int*lerp(0,ColorFetch(RenderTarget1024, IN.txcoord0.xy) * weight[0] * (1 + Rpost_mixer_bloomColor*float3(sin(x[0]), sin(x[0]+TAU/3), sin(x[0]-TAU/3))),Pass_select1);
    res.xyz += Pass_select2_int*lerp(0,ColorFetch(RenderTarget512, IN.txcoord0.xy)  * weight[1] * (1 + Rpost_mixer_bloomColor*float3(sin(x[1]), sin(x[1]+TAU/3), sin(x[1]-TAU/3))),Pass_select2);
    res.xyz += Pass_select3_int*lerp(0,ColorFetch(RenderTarget256, IN.txcoord0.xy)  * weight[2] * (1 + Rpost_mixer_bloomColor*float3(sin(x[2]), sin(x[2]+TAU/3), sin(x[2]-TAU/3))),Pass_select3);
    res.xyz += Pass_select4_int*lerp(0,ColorFetch(RenderTarget128, IN.txcoord0.xy)  * weight[3] * (1 + Rpost_mixer_bloomColor*float3(sin(x[3]), sin(x[3]+TAU/3), sin(x[3]-TAU/3))),Pass_select4);
    res.xyz += Pass_select5_int*lerp(0,ColorFetch(RenderTarget64, IN.txcoord0.xy)   * weight[4] * (1 + Rpost_mixer_bloomColor*float3(sin(x[4]), sin(x[4]+TAU/3), sin(x[4]-TAU/3))),Pass_select5);
    res.xyz += Pass_select6_int*lerp(0,ColorFetch(RenderTarget32, IN.txcoord0.xy)   * weight[5] * (1 + Rpost_mixer_bloomColor*float3(sin(x[5]), sin(x[5]+TAU/3), sin(x[5]-TAU/3))),Pass_select6);
    res.xyz += Pass_select7_int*lerp(0,ColorFetch(RenderTarget32, IN.txcoord0.xy)   * weight[6] * (1 + Rpost_mixer_bloomColor*float3(sin(x[6]), sin(x[6]+TAU/3), sin(x[6]-TAU/3))),Pass_select7);
   
	} else { 
    res.xyz += Pass_select1_int*lerp(0,ColorFetch(RenderTarget1024, IN.txcoord0.xy) * weight[0],Pass_select1);
    res.xyz += Pass_select2_int*lerp(0,ColorFetch(RenderTarget512, IN.txcoord0.xy)  * weight[1] ,Pass_select2);
    res.xyz += Pass_select3_int*lerp(0,ColorFetch(RenderTarget256, IN.txcoord0.xy)  * weight[2] ,Pass_select3);
    res.xyz += Pass_select4_int*lerp(0,ColorFetch(RenderTarget128, IN.txcoord0.xy)  * weight[3] ,Pass_select4);
    res.xyz += Pass_select5_int*lerp(0,ColorFetch(RenderTarget64, IN.txcoord0.xy)   * weight[4],Pass_select5);
    res.xyz += Pass_select6_int*lerp(0,ColorFetch(RenderTarget32, IN.txcoord0.xy)   * weight[5],Pass_select6);
	 res.xyz += Pass_select7_int*lerp(0,ColorFetch(RenderTarget16, IN.txcoord0.xy)   * weight[6],Pass_select7);
    };
	//
	float divider		= Pass_select1 + Pass_select2 + Pass_select3 + Pass_select4 + Pass_select5 + Pass_select6 + Pass_select7;
	float int_sum		= ( Pass_select1 * Pass_select1_int ) + ( Pass_select2 * Pass_select2_int ) + ( Pass_select3 * Pass_select3_int ) + ( Pass_select4 * Pass_select4_int ) + ( Pass_select5 * Pass_select5_int ) + ( Pass_select6 * Pass_select6_int ) +  ( Pass_select7 * Pass_select7_int );
	float multiplier	= divider / int_sum;
	//res.xyz			*= ( 1.0f / divider );
	res.xyz			*= multiplier;

    res /= weightSum;
	
	res.xyz 			*= vIntensity;
	
	float3 Temp = AvgLuma(res.xyz).w;
    res.xyz = lerp(Temp.xyz, res.xyz, RfSaturation);
  
  res=max(res, 0.0);
  res=min(res, 16384.0);

  res.w=1.0;
  return res;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
technique11 GaussPass2Bloom <string UIName="Gaussian bloom (Single pass)"; string RenderTarget="RenderTarget1024";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_GaussHResize(TextureDownsampled, 1024.0)));
  }
}
technique11 GaussPass2Bloom1 
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_GaussVResize(RenderTarget1024, 1024.0)));
  }
}


//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
technique11 GaussPassMBloom <string UIName="Gaussian bloom (Multiple pass)";>
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Quad()));SetPixelShader(CompileShader(ps_5_0, PS_GaussHResizeFirst(TextureDownsampled, 1024.0)));}}

technique11 GaussPassMBloom1 <string RenderTarget="RenderTarget1024";>
{ pass p0 {SetVertexShader(CompileShader(vs_5_0, VS_Quad())); SetPixelShader(CompileShader(ps_5_0, PS_GaussVResizeFirst(TextureColor, 1024.0)));}}

technique11 GaussPassMBloom2{  pass p0  {    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));    SetPixelShader(CompileShader(ps_5_0, PS_GaussHResizeFirst(RenderTarget1024, 512.0)));}}

technique11 GaussPassMBloom3 <string RenderTarget="RenderTarget512";>{  pass p0  {    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));    SetPixelShader(CompileShader(ps_5_0, PS_GaussVResizeFirst(TextureColor, 512.0)));}}

technique11 GaussPassMBloom4 {  pass p0  {    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));    SetPixelShader(CompileShader(ps_5_0, PS_GaussHResize(RenderTarget512, 256.0)));  }}

technique11 GaussPassMBloom5 <string RenderTarget="RenderTarget256";>{  pass p0  {    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));    SetPixelShader(CompileShader(ps_5_0, PS_GaussVResize(TextureColor, 256.0)));}}

technique11 GaussPassMBloom6 {  pass p0  {    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));    SetPixelShader(CompileShader(ps_5_0, PS_GaussHResize(RenderTarget256, 128.0)));  }}

technique11 GaussPassMBloom7 <string RenderTarget="RenderTarget128";>{  pass p0  {    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));    SetPixelShader(CompileShader(ps_5_0, PS_GaussVResize(TextureColor, 128.0))); }}

technique11 GaussPassMBloom8{  pass p0  {    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));    SetPixelShader(CompileShader(ps_5_0, PS_GaussHResize(RenderTarget128, 64.0)));  }}

technique11 GaussPassMBloom9 <string RenderTarget="RenderTarget64";>{pass p0  {    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));    SetPixelShader(CompileShader(ps_5_0, PS_GaussVResize(TextureColor, 64.0)));}}

technique11 GaussPassMBloom10 { pass p0  {    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));    SetPixelShader(CompileShader(ps_5_0, PS_GaussHResize(RenderTarget64, 32.0))); }}

technique11 GaussPassMBloom11 <string RenderTarget="RenderTarget32";>{  pass p0  {    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));    SetPixelShader(CompileShader(ps_5_0, PS_GaussVResize(TextureColor, 32.0)));}}

technique11 GaussPassMBloom12 { pass p0  {    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));    SetPixelShader(CompileShader(ps_5_0, PS_GaussHResize(RenderTarget32, 16.0))); }}

technique11 GaussPassMBloom13 <string RenderTarget="RenderTarget16";>{  pass p0  {    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));    SetPixelShader(CompileShader(ps_5_0, PS_GaussVResize(TextureColor, 16.0)));}}


// last pass output to bloom texture
technique11 GaussPassMBloom14
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_GaussMix()));
  }
}