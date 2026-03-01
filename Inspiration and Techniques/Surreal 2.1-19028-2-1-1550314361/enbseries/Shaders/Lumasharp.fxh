float4	LumaSharp(float4 ori, float2 coord)
{
    #define LUM_709  float3(0.2125, 0.7154, 0.0721)
	// Combining the strength and luma multipliers
	float3 sharp_strength_luma = (LUM_709 * LSharpStrength);

	// Sampling patterns
	float4 blur_ori;
	
		blur_ori  = TextureColor.Sample(PointSampler, coord.xy + PixelSize * float2(0.4, -1.2) * 1);  // South South East
		blur_ori += TextureColor.Sample(PointSampler, coord.xy + PixelSize * float2(1.2, 0.4) * 1);   // West South West
		blur_ori += TextureColor.Sample(PointSampler, coord.xy + PixelSize * float2(1.2, 0.4) * 1);   // East North East
		blur_ori += TextureColor.Sample(PointSampler, coord.xy + PixelSize * float2(0.4, -1.2) * 1);  // North North West

		blur_ori *= 0.25;  // ( /= 4) Divide by the number of texture fetches

		sharp_strength_luma *= 0.51;

	float4 sharp = ori - blur_ori;  //Subtracting the blurred image from the original image

	float4 sharp_strength_luma_clamp = float4(sharp_strength_luma * (0.5 / 0.1), 0.5);
	sharp.w = 1.0;
	float sharp_luma = saturate( dot(sharp, sharp_strength_luma_clamp) );
	sharp_luma = (0.1 * 2.0) * sharp_luma - 0.1;

	float4 outputcolor = ori + sharp_luma;

	return saturate(outputcolor);
}
