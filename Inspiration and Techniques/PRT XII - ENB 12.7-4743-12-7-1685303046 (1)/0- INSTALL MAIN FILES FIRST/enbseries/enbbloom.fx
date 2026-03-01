/*/////////////////////////////////////////////////////////
//                ENBSeries effect file                //
//         visit http://enbdev.com for updates         //
//       Copyright (c) 2007-2018 Boris Vorontsov       //
//----------------------ENB PRESET---------------------//
			MM"""""""`YM MM"""""""`MM M""""""""M 
			MM  mmmmm  M MM  mmmm,  M Mmmm  mmmM 
			M'        .M M'        .M MMMM  MMMM 
			MM  MMMMMMMM MM  MMMb. "M MMMM  MMMM 
			MM  MMMMMMMM MM  MMMMM  M MMMM  MMMM 
			MM  MMMMMMMM MM  MMMMM  M MMMM  MMMM 
			MMMMMMMMMMMM MMMMMMMMMMMM MMMMMMMMMM 
https://www.nexusmods.com/skyrimspecialedition/mods/4743
//-----------------------CREDITS-----------------------//
// Boris: For ENBSeries and his knowledge and codes    //
// SonicEther&MaxG3D: Original authors of Bloom code   //
// L00 :  Shader Setup, Presets and Settings,          //
//        Port, Modification and author of this file   //
//     Please do not redistribute without credits      //
/////////////////////////////////////////////////////////
//            PHOTOREALISTIC TAMRIEL 12.7              //

/////////////////////////////////////////////////////////
//               	  INTERFACE                        //
///////////////////////////////////////////////////////*/

float	BloomSlider				< string UIName="Bloom Intensity                                          %";string UIWidget="spinner";float UIMin=0.0f;float UIMax=150.0f;float UIStep=1.0f;> = {75.0};
float	BloomSlider_n			< string UIName="Bloom Intensity night                                         %";string UIWidget="spinner";float UIMin=0.0f;float UIMax=150.0f;float UIStep=1.0f;> = {80.0}; 
float	BloomSlider_i			< string UIName="Bloom Intensity interiors                              %";string UIWidget="spinner";float UIMin=0.0f;float UIMax=150.0f;float UIStep=1.0f;> = {75.0}; 


/////////////////////////////////////////////////////////
//               	INTERNAL SETTINGS                  //
/////////////////////////////////////////////////////////

#define GaussianBloomColorEffect 0
#define fContrast 				lerp(lerp(1.0,1.02,ENightDayFactor),1.0,EInteriorFactor)
#define ECCInBlack 				lerp(0.0,0.05,lerp(lerp(BloomSlider_n*0.01,BloomSlider*0.01,ENightDayFactor), BloomSlider_i*0.01, EInteriorFactor))
#define ECCInWhite 				1.0
#define ECCOutBlack 			0.0
#define ECCOutWhite				lerp(0.0,1.0,lerp(lerp(BloomSlider_n*0.01,BloomSlider*0.01,ENightDayFactor), BloomSlider_i*0.01, EInteriorFactor))
#define fSaturation				float4(0.65,0.65,0.65,0.5)
#define post_mixer_bloomShape 	5.0
#define post_mixer_bloomColor float3(0.0431, 0.251, 0)
#define fSaturation float4(1.0,1.0,1.0,1.0)

#define iLensLeakMixmode			1
#define fLensLeakSaturation			1.0
#define fLensLeakIntensityDawn		5.0
#define fLensLeakIntensitySunrise	2.0
#define fLensLeakIntensityDay		4.0
#define fLensLeakIntensitySunset	3.0
#define fLensLeakIntensityDusk		2.0
#define fLensLeakIntensityNight		10.0
#define fLensLeakIntensityInterior	0.0

float4	Timer;				//x = generic timer in range 0..1, period of 16777216 ms (4.6 hours), y = average fps, w = frame time elapsed (in seconds)
float4	ScreenSize; 		//x = Width, y = 1/Width, z = aspect, w = 1/aspect, aspect is Width/Height
float	AdaptiveQuality; 	//changes in range 0..1, 0 means full quality, 1 lowest dynamic quality (0.33, 0.66 are limits for quality levels)
float4	Weather; 			//x = current weather index, y = outgoing weather index, z = weather transition, w = time of the day in 24 standart hours. Weather index is value from weather ini file, for example WEATHER002 means index==2, but index==0 means that weather not captured.
float4	TimeOfDay1; 		//x = dawn, y = sunrise, z = day, w = sunset. Interpolators range from 0..1
float4	TimeOfDay2; 		//x = dusk, y = night. Interpolators range from 0..1
float	ENightDayFactor; 	//changes in range 0..1, 0 means that night time, 1 - day time
float	EInteriorFactor; 	//changes 0 or 1. 0 means that exterior, 1 - interior

//keyboard controlled temporary variables. Press and hold key 1,2,3...8 together with PageUp or PageDown to modify. By default all set to 1.0
float4	tempF1; //0,1,2,3
float4	tempF2; //5,6,7,8
float4	tempF3; //9,0
// xy = cursor position in range 0..1 of screen;
// z = is shader editor window active;
// w = mouse buttons with values 0..7 as follows:
//    0 = none
//    1 = left
//    2 = right
//    3 = left+right
//    4 = middle
//    5 = left+middle
//    6 = right+middle
//    7 = left+right+middle (or rather cat is sitting on your mouse)
float4	tempInfo1;
// xy = cursor position of previous left mouse button click
// zw = cursor position of previous right mouse button click
float4	tempInfo2;

/////////////////////////////////////////////////////////
//               		TEXTURES                       //
/////////////////////////////////////////////////////////
Texture2D			TextureDownsampled; //color R16B16G16A16 64 bit or R11G11B10 32 bit hdr format. 1024*1024 size
Texture2D			TextureColor; //color which is output of previous technique (except when drawed to temporary render target), R16B16G16A16 64 bit hdr format. 1024*1024 size

Texture2D			TextureOriginal; //color R16B16G16A16 64 bit or R11G11B10 32 bit hdr format, screen size. PLEASE AVOID USING IT BECAUSE OF ALIASING ARTIFACTS, UNLESS YOU FIX THEM
Texture2D			TextureDepth; //scene depth R32F 32 bit hdr format, screen size. PLEASE AVOID USING IT BECAUSE OF ALIASING ARTIFACTS, UNLESS YOU FIX THEM
Texture2D			TextureAperture; //this frame aperture 1*1 R32F hdr red channel only. computed in PS_Aperture of enbdepthoffield.fx

//temporary textures which can be set as render target for techniques via annotations like <string RenderTarget="RenderTargetRGBA32";>
Texture2D			RenderTarget1024; //R16B16G16A16F 64 bit hdr format, 1024*1024 size
Texture2D			RenderTarget512; //R16B16G16A16F 64 bit hdr format, 512*512 size
Texture2D			RenderTarget256; //R16B16G16A16F 64 bit hdr format, 256*256 size
Texture2D			RenderTarget128; //R16B16G16A16F 64 bit hdr format, 128*128 size
Texture2D			RenderTarget64; //R16B16G16A16F 64 bit hdr format, 64*64 size
Texture2D			RenderTarget32; //R16B16G16A16F 64 bit hdr format, 32*32 size
Texture2D			RenderTarget16; //R16B16G16A16F 64 bit hdr format, 16*16 size
Texture2D			RenderTargetRGBA32; //R8G8B8A8 32 bit ldr format, screen size
Texture2D			RenderTargetRGBA64F; //R16B16G16A16F 64 bit hdr format, screen size

Texture2D TextureLeak 			<string ResourceName = "Textures/Lenses/_BloomDirt.jpg";>;

/////////////////////////////////////////////////////////
//               	    SAMPLERS                       //
/////////////////////////////////////////////////////////

SamplerState		Sampler0
{
	Filter = MIN_MAG_MIP_POINT;
	AddressU = Clamp;
	AddressV = Clamp;
};
SamplerState		Sampler1
{
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = Clamp;
	AddressV = Clamp;
};
SamplerState 		SamplerLeak
{
   Filter = MIN_MAG_MIP_LINEAR;  
   AddressU=Clamp;  
   AddressV=Clamp;
};

/////////////////////////////////////////////////////////
//               	   FUNCTIONS                 	   //
/////////////////////////////////////////////////////////

struct VS_INPUT_POST
{
	float3 pos		: POSITION;
	float2 txcoord	: TEXCOORD0;
};
struct VS_OUTPUT_POST
{
	float4 pos		: SV_POSITION;
	float2 txcoord0	: TEXCOORD0;
};

float4 AvgLuma(float3 inColor)
{
	return float4(dot(inColor, float3(0.2125f, 0.7154f, 0.0721f)),                 /// Perform a weighted average
                max(inColor.r, max(inColor.g, inColor.b)),                       /// Take the maximum value of the incoming value
                max(max(inColor.x, inColor.y), inColor.z),                       /// Compute the luminance component as per the HSL colour space
                sqrt((inColor.x*inColor.x*0.2125f)+(inColor.y*inColor.y*0.7154f)+(inColor.z*inColor.z*0.0721f)));
}

float linearDepth(float d, float n, float f)
{
  return (2.0 * n)/(f + n - d * (f - n));
}

float3 ColorFetch(Texture2D inputtex, float2 coord)
{
 	return inputtex.Sample(Sampler1, coord).rgb;   
}

//Horizontal gaussian blur leveraging hardware filtering for fewer texture lookups.
float3  FuncHoriBlur(Texture2D inputtex, float2 uvsrc, float2 pxSize, float Iteration)
{    
float weights[5];
float offsets[5];
    
    weights[0] = 2.0;
    weights[1] = 1.67;
    weights[2] = 0.8;
    weights[3] = 0.23;
    weights[4] = 0.09;
    
    offsets[0] = 0.0;
    offsets[1] = 1.0;
    offsets[2] = 2.0;
    offsets[3] = 3.0;
    offsets[4] = 4.0;
    
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
            color += ColorFetch(inputtex, uv + offset * float2(0.6, 0.0)) * weights[i];
            color += ColorFetch(inputtex, uv - offset * float2(0.6, 0.0)) * weights[i];
            weightSum += weights[i] * 2.0;
        }

        color /= weightSum;
    }

    return color;
}

//Vertical gaussian blur leveraging hardware filtering for fewer texture lookups.
float3  FuncVertBlur(Texture2D inputtex, float2 uvsrc, float2 pxSize, float Iteration)
{    
float weights[5];
float offsets[5];
    
    weights[0] = 2.0;
    weights[1] = 1.67;
    weights[2] = 0.8;
    weights[3] = 0.23;
    weights[4] = 0.09;
    
    offsets[0] = 0.0;
    offsets[1] = 1.0;
    offsets[2] = 2.0;
    offsets[3] = 3.0;
    offsets[4] = 4.0;
    
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
            color += ColorFetch(inputtex, uv + offset * float2(0.0, 0.6)) * weights[i];
            color += ColorFetch(inputtex, uv - offset * float2(0.0, 0.6)) * weights[i];
            weightSum += weights[i] * 2.0;
        }

        color /= weightSum;
    }

    return color;
}

/////////////////////////////////////////////////////////
//               	  PIXEL SHADER                     //
/////////////////////////////////////////////////////////

VS_OUTPUT_POST	VS_Quad(VS_INPUT_POST IN)
{
	VS_OUTPUT_POST	OUT;
	float4	pos;
	pos.xyz=IN.pos.xyz;
	pos.w=1.0;
	OUT.pos=pos;
	OUT.txcoord0.xy=IN.txcoord.xy;
	return OUT;
}

float4  PS_GaussHResize(VS_OUTPUT_POST IN, float4 v0 : SV_Position0,
  uniform Texture2D inputtex, uniform float texsize, uniform float Iteration) : SV_Target
{
  float4  res;

  float2 pxSize = (1/(texsize))*float2(1, ScreenSize.z);
  res.xyz=FuncHoriBlur(inputtex, IN.txcoord0.xy, pxSize*2.0, Iteration);

  #if 1 // Double blur. This removes artifacing in the raw bloom texture.
  // However, it costs more than ENB's bloom and does like 60 passes. 
  // The aliasing is only noticeable at 100% bloom anyway. 
  res.xyz+=FuncHoriBlur(inputtex, IN.txcoord0.xy, pxSize*2.0, Iteration+1);
  res.xyz/=2;
  #endif
  
  res=max(res, 0.0);
  res=min(res, 16384.0);

  res.w=1.0;
  return res;
}

float4  PS_GaussVResize(VS_OUTPUT_POST IN, float4 v0 : SV_Position0,
  uniform Texture2D inputtex, uniform float texsize, uniform float Iteration) : SV_Target
{
  float4  res;

  float2 pxSize = (1/(texsize))*float2(1, ScreenSize.z);
  res.xyz=FuncVertBlur(inputtex, IN.txcoord0.xy, pxSize*2.0, Iteration);

  #if 1 // Double blur. This removes artifacing in the raw bloom texture.
  // However, it costs more than ENB's bloom and does like 60 passes. 
  // The aliasing is only noticeable at 100% bloom anyway. 
  res.xyz+=FuncVertBlur(inputtex, IN.txcoord0.xy, pxSize*2.0, Iteration+1);
  res.xyz/=2;
  #endif
  
  res=max(res, 0.0);
  res=min(res, 16384.0);

  res.w=1.0;
  return res;
}

float4  PS_GaussHResizeFirst(VS_OUTPUT_POST IN, float4 v0 : SV_Position0,
  uniform Texture2D inputtex, uniform float texsize, uniform float Iteration) : SV_Target
{
  float4  res;

  float2 pxSize = (1/(texsize))*float2(1, ScreenSize.z);
  res.xyz=FuncHoriBlur(inputtex, IN.txcoord0.xy, pxSize*2.0, Iteration);
  
  
  #if 1 // Double blur. This removes artifacing in the raw bloom texture.
  // However, it costs more than ENB's bloom and does like 60 passes. 
  // The aliasing is only noticeable at 100% bloom anyway. 
  res.xyz+=FuncHoriBlur(inputtex, IN.txcoord0.xy, pxSize*2.0, Iteration+1);
  res.xyz/=2;
  #endif

  	res.xyz=max(res.xyz-(ECCInBlack*0.1), 0.0) / max(ECCInWhite-(ECCInBlack*0.1), 0.00001);
	if (fContrast!=1.0) res.xyz=pow(res.xyz, fContrast);
	res.xyz=res.xyz*(ECCOutWhite-ECCOutBlack) + ECCOutBlack;
  
  res=max(res, 0.0);
  res=min(res, 16384.0);

  res.w=1.0;
  return res;
}

float4  PS_GaussVResizeFirst(VS_OUTPUT_POST IN, float4 v0 : SV_Position0,
  uniform Texture2D inputtex, uniform float texsize, uniform float Iteration) : SV_Target
{
 float4  res;

        float2 pxSize = (1/(texsize))*float2(1, ScreenSize.z);
        res.xyz=FuncVertBlur(inputtex, IN.txcoord0.xy, pxSize*2.0, Iteration);

        #if 1 // Double blur. This removes artifacing in the raw bloom texture.
            // However, it costs more than ENB's bloom and does like 60 passes.
            // The aliasing is only noticeable at 100% bloom anyway.
            res.xyz+=FuncVertBlur(inputtex, IN.txcoord0.xy, pxSize*2.0, Iteration+1);
            res.xyz/=2;
        #endif

        res.xyz=max(res.xyz-(ECCInBlack*0.1), 0.0) / max(ECCInWhite-(ECCInBlack*0.1), 0.00001);
        if (fContrast!=1.0) res.xyz=pow(res.xyz, fContrast);
        res.xyz=res.xyz*(ECCOutWhite-ECCOutBlack) + ECCOutBlack;

        res=max(res, 0.0);
        res=min(res, 16384.0);

        res.w=1.0;
        return res;
}

/* float post_mixer_bloomShape <
  string UIName="Gaussian: Bloom Shape";
  string UIWidget="Spinner";
  float UIMin=0.0;
  float UIMax=32.0;
  float UIStep=0.01;
> = {1.0}; */

#ifdef GaussianBloomColorEffect
#define post_mixer_bloomColor (0.647, 0.765, 1)
/* float3 post_mixer_bloomColor <
  string UIName="Gaussian: Bloom Color Tint Amount";
  string UIWidget="Color";
> = {1.0, 1.0, 1.0}; */
#endif

float4  PS_GaussMix(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
  float4  res = 0.0;

	float   timeweight=0.000001;
	float   timevalue=0.0;
  
  //float4 fSaturation	= (1.0,1.0,1.0,0.65);
  
  // Mercury bloom blending code
  // Source: https://imgur.com/a/MZD3l
  // This is kind of messy... sorry! 
  float weightSum = 0;
  int maxlevel = 5;
  #define TAU 6.28318
 
    // This should get optimised by the compiler. 
    float weight[6];
    float x[6];

    [unroll]
    for (int i=0; i <= maxlevel; i++) {
        weight[i] = pow(i+1, post_mixer_bloomShape);
        weightSum += weight[i];
        x[i] = i*2;
    }

    if (GaussianBloomColorEffect) {
    res.xyz += ColorFetch(RenderTarget1024, IN.txcoord0.xy) * weight[0] * (1 + post_mixer_bloomColor*float3(sin(x[0]), sin(x[0]+TAU/3), sin(x[0]-TAU/3)));
    res.xyz += ColorFetch(RenderTarget512, IN.txcoord0.xy)  * weight[1] * (1 + post_mixer_bloomColor*float3(sin(x[1]), sin(x[1]+TAU/3), sin(x[1]-TAU/3)));
    res.xyz += ColorFetch(RenderTarget256, IN.txcoord0.xy)  * weight[2] * (1 + post_mixer_bloomColor*float3(sin(x[2]), sin(x[2]+TAU/3), sin(x[2]-TAU/3)));
    res.xyz += ColorFetch(RenderTarget128, IN.txcoord0.xy)  * weight[3] * (1 + post_mixer_bloomColor*float3(sin(x[3]), sin(x[3]+TAU/3), sin(x[3]-TAU/3)));
    res.xyz += ColorFetch(RenderTarget64, IN.txcoord0.xy)   * weight[4] * (1 + post_mixer_bloomColor*float3(sin(x[4]), sin(x[4]+TAU/3), sin(x[4]-TAU/3)));
    res.xyz += ColorFetch(RenderTarget32, IN.txcoord0.xy)   * weight[5] * (1 + post_mixer_bloomColor*float3(sin(x[5]), sin(x[5]+TAU/3), sin(x[5]-TAU/3)));
    } else { 
    res.xyz += ColorFetch(RenderTarget1024, IN.txcoord0.xy) * weight[0] * (1 + (post_mixer_bloomColor * 4.0)); 
    res.xyz += ColorFetch(RenderTarget512, IN.txcoord0.xy)  * weight[1] * (1 + (post_mixer_bloomColor * 2.7889)); 
    res.xyz += ColorFetch(RenderTarget256, IN.txcoord0.xy)  * weight[2] * (1 + (post_mixer_bloomColor * 0.064)); 
    res.xyz += ColorFetch(RenderTarget128, IN.txcoord0.xy)  * weight[3] * (1 + (post_mixer_bloomColor * 0.0529)); 
    res.xyz += ColorFetch(RenderTarget64, IN.txcoord0.xy)   * weight[4] * (1 + (post_mixer_bloomColor * 0.0081)); 
    res.xyz += ColorFetch(RenderTarget32, IN.txcoord0.xy)   * weight[5] * (1 + (post_mixer_bloomColor * 0.0)); 
    };

  res /= weightSum;

	float3 Temp = AvgLuma(res.xyz).w;
    res.xyz = lerp(Temp.xyz, res.xyz, fSaturation);
  
  //Intensity TOD
	
	timevalue+=TimeOfDay1.x * fLensLeakIntensityDawn;
	timevalue+=TimeOfDay1.y * fLensLeakIntensitySunrise;
	timevalue+=TimeOfDay1.z * fLensLeakIntensityDay;
	timevalue+=TimeOfDay1.w * fLensLeakIntensitySunset;
	timevalue+=TimeOfDay2.x * fLensLeakIntensityDusk;
	timevalue+=TimeOfDay2.y * fLensLeakIntensityNight;

	timeweight+=TimeOfDay1.x;
	timeweight+=TimeOfDay1.y;
	timeweight+=TimeOfDay1.z;
	timeweight+=TimeOfDay1.w;
	timeweight+=TimeOfDay2.x;
	timeweight+=TimeOfDay2.y;
	
	float	todfLensLeakIntensity=lerp( (timevalue / timeweight), fLensLeakIntensityInterior, EInteriorFactor );
  //float	todfLensLeakIntensity=lerp(lerp(fLensLeakIntensityNight, fLensLeakIntensityDay, ENightDayFactor), fLensLeakIntensityInterior, EInteriorFactor); 

						float3 Leaktex = TextureLeak.Sample(SamplerLeak, IN.txcoord0.xy);
						float3 lensLeak = Leaktex.xyz*res*todfLensLeakIntensity;
						
						res.xyz=saturate(res.xyz);
						
						lensLeak = lerp(dot(lensLeak.xyz,0.333), lensLeak.xyz, fLensLeakSaturation);

						if(iLensLeakMixmode == 1) res.xyz = res.xyz + lensLeak.xyz;
						if(iLensLeakMixmode == 2) res.xyz = 1-(1-res.xyz)*(1-lensLeak.xyz);
						if(iLensLeakMixmode == 3) res.xyz = max(0.0f,max(res.xyz,lerp(res.xyz,(1.0f - (1.0f - saturate(lensLeak.xyz)) *(1.0f - saturate(lensLeak.xyz * 1.0))),1.0)));
						if(iLensLeakMixmode == 4) res.xyz = max(res.xyz, lensLeak.xyz);
  
  res=max(res, 0.0);
  res=min(res, 16384.0);

  
  res.w=1.0;
  return res;
}

/////////////////////////////////////////////////////////
//               	    TECHNIQUES                     //
/////////////////////////////////////////////////////////

technique11 BalancedPassMBloom <string UIName="PRT.X: BLOOM";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_GaussHResizeFirst(TextureDownsampled, 1536.0, 2)));
  }
}
technique11 BalancedPassMBloom1 <string RenderTarget="RenderTarget1024";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_GaussVResizeFirst(TextureColor, 1536.0, 2)));
  }
}
technique11 BalancedPassMBloom2
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_GaussHResizeFirst(RenderTarget1024, 512.0, 4)));
  }
}
technique11 BalancedPassMBloom3 <string RenderTarget="RenderTarget512";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_GaussVResizeFirst(TextureColor, 512.0, 4)));
  }
}
technique11 BalancedPassMBloom4
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_GaussHResize(RenderTarget512, 256.0, 5)));
  }
}
technique11 BalancedPassMBloom5 <string RenderTarget="RenderTarget256";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_GaussVResize(TextureColor, 256.0, 5)));
  }
}
technique11 BalancedPassMBloom6
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_GaussHResize(RenderTarget256, 128.0, 6)));
  }
}
technique11 BalancedPassMBloom7 <string RenderTarget="RenderTarget128";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_GaussVResize(TextureColor, 128.0, 6)));
  }
}
technique11 BalancedPassMBloom8
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_GaussHResize(RenderTarget128, 64.0, 7)));
  }
}
technique11 BalancedPassMBloom9 <string RenderTarget="RenderTarget64";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_GaussVResize(TextureColor, 64.0, 7)));
  }
}
technique11 BalancedPassMBloom10 
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_GaussHResize(RenderTarget64, 32.0, 8)));
  }
}
technique11 BalancedPassMBloom11 <string RenderTarget="RenderTarget32";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_GaussVResize(TextureColor, 32.0, 8)));
  }
}
// last pass output to bloom texture
technique11 BalancedPassMBloom12
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
    SetPixelShader(CompileShader(ps_5_0, PS_GaussMix()));
  }
}

