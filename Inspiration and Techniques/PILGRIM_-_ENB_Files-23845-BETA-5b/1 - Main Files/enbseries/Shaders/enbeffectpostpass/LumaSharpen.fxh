//-------------------LUMASHARPENING--------------------//
// by Christian Cann Schuldt Jensen ~ CeeJay.dk        //
//+++++++++++++++++++++++++++++++++++++++++++++++++++++//

#define screen_size float2(ScreenSize.x,ScreenSize.x*ScreenSize.w)
#define screen_ratio (screen_size.x / screen_size.y)
#define pixel float2(ScreenSize.y,ScreenSize.y*ScreenSize.z)

   float3 LumaSharpenPass( float2 tex )
{
#define px ScreenSize.y
#define py ScreenSize.y * ScreenSize.z

  float3 ori = TextureColor.Sample(Sampler1, tex).rgb;
  #if(PilgrimQuality != 2)
  if(ENABLE_LENS == false) return ori;
  #else
  return ori;
  #endif

  float3 sharp_strength_luma = (CoefLuma * sharp_strength);

	float3 blur_ori = TextureColor.Sample(Sampler1, tex + float2(px,-py) * 0.5 * offset_bias).rgb;
	blur_ori += TextureColor.Sample(Sampler1, tex + float2(-px,-py) * 0.5 * offset_bias).rgb;
	blur_ori += TextureColor.Sample(Sampler1, tex + float2(px,py) * 0.5 * offset_bias).rgb;
	blur_ori += TextureColor.Sample(Sampler1, tex + float2(-px,py) * 0.5 * offset_bias).rgb;

	blur_ori *= 0.25;

  float3 sharp = ori - blur_ori;

	float4 sharp_strength_luma_clamp = float4(sharp_strength_luma * (0.5 / sharp_clamp),0.5);

	float sharp_luma = saturate(dot(float4(sharp,1.0), sharp_strength_luma_clamp));
	sharp_luma = (sharp_clamp * 2.0) * sharp_luma - sharp_clamp;

  float3 outputcolor = ori + sharp_luma;

    #undef px
    #undef py

  return saturate(outputcolor);

}

float3 LumaSharpenPass2( float2 tex )
{
#define px ScreenSize.y
#define py ScreenSize.y * ScreenSize.z

float3 ori = TextureColor.Sample(Sampler1, tex).rgb;
if(USER_SHARPEN == 0) return ori;


float3 sharp_strength_luma = (CoefLuma * sharp_strength2);

float3 blur_ori = TextureColor.Sample(Sampler1, tex + float2(px,-py) * 0.5 * offset_bias).rgb;
blur_ori += TextureColor.Sample(Sampler1, tex + float2(-px,-py) * 0.5 * offset_bias).rgb;
blur_ori += TextureColor.Sample(Sampler1, tex + float2(px,py) * 0.5 * offset_bias).rgb;
blur_ori += TextureColor.Sample(Sampler1, tex + float2(-px,py) * 0.5 * offset_bias).rgb;

blur_ori *= 0.25;

float3 sharp = ori - blur_ori;

float4 sharp_strength_luma_clamp = float4(sharp_strength_luma * (0.5 / sharp_clamp),0.5);

float sharp_luma = saturate(dot(float4(sharp,1.0), sharp_strength_luma_clamp));
sharp_luma = (sharp_clamp * 2.0) * sharp_luma - sharp_clamp;

float3 outputcolor = ori + sharp_luma;

 #undef px
 #undef py

return saturate(outputcolor);

}
