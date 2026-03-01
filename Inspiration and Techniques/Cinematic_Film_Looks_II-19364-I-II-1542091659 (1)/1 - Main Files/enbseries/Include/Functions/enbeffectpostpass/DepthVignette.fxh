float4 PS_DepthVignette(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target {
    float4 color = TextureColor.Sample(Sampler1, IN.txcoord0.xy);
    float4 mask = TextureDepth.Sample(Sampler1, IN.txcoord0.xy);
    mask = LinearizeDepth(mask);

    // Turn pixel to grayscale.
    float grayscale = dot(mask.r, float3(0.3, 0.59, 0.11));
    mask.r = grayscale;
    mask.g = grayscale;
    mask.b = grayscale;
    mask.a = 1.0f;

    color = lerp(color, 0.0, pow(mask, MGAMMA) * MGAIN - MLIFT);
	//color = lerp(orig, color, pow(mask, 1.0));
    //color = pow(color, 0.96);
	//orig = lerp(orig, color, 0.65);

	return color;
}
