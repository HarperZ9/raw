// YES i am 100% aware of the fact that this is NOT how it should work(or is it?)... but it looks decent... so fug u. And yes i am shameless
float3 MicroBloom(inout float3 color, in float2 uv)
{
	float3 bloom = 0.0;
	float allWeights = 0.0f;

	for (int i = 0; i < 4; i++) 
	{
		for (int j = 0; j < 4; j++) 
		{
			float weight = 1.0f - distance(float2(i, j), 2.5f) / 2.5;
				  weight = clamp(weight, 0.0f, 1.0f);
				  weight = 1.0f - cos(weight * 3.1415 / 2.0f);
				  weight = pow(weight, 2.0f);
			float2 coord = (i - 2.5, j - 2.5);
				 coord.x /= ScreenSize.x;
				 coord.y /= ScreenSize.x * ScreenSize.w;
                 
			float2 finalCoord = (uv.xy + coord.xy * 1.0);

			if (weight > 0.0f)
			{
				bloom += pow(clamp(TextureColor.Sample(LinearSampler, finalCoord, 0).rgb, 0.0f, 1.0f), 2.2f) * weight;
				allWeights += 1.0f * weight;
			}
		}
	}
	bloom /= allWeights;

	return lerp(color, bloom, 0.35); // mix = lerp?
}
