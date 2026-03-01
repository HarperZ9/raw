// GAUSSIAN BLUR BY IOXA ///////////////////////////

float4 PS_HazeHor(VS_OUTPUT_POST IN, uniform Texture2D GaussianHaze, float4 v0 : SV_Position0) : SV_Target {
    float4 color = TextureColor.Sample(Sampler1, IN.txcoord0.xy);
    [branch]
    if(! ENABLE_HAZE || ! ENABLE_LENS) return color;


	float offset[18] = { 0.0, 1.4953705027, 3.4891992113, 5.4830312105, 7.4768683759, 9.4707125766, 11.4645656736, 13.4584295168, 15.4523059431, 17.4461967743, 19.4661974725, 21.4627427973, 23.4592916956, 25.455844494, 27.4524015179, 29.4489630909, 31.445529535, 33.4421011704 };
	float weight[18] = { 0.033245, 0.0659162217, 0.0636705814, 0.0598194658, 0.0546642566, 0.0485871646, 0.0420045997, 0.0353207015, 0.0288880982, 0.0229808311, 0.0177815511, 0.013382297, 0.0097960001, 0.0069746748, 0.0048301008, 0.0032534598, 0.0021315311, 0.0013582974 };

	color *= weight[0];

    [unroll]
	for(int i = 1; i < 18; ++i) {
		color += float4(GaussianHaze.Sample(Sampler1, IN.txcoord0.xy + float2(offset[i] * ScreenSize.y, 0.0) * 2.0).rgb * weight[i], 1.0);
		color += float4(GaussianHaze.Sample(Sampler1, IN.txcoord0.xy - float2(offset[i] * ScreenSize.y, 0.0) * 2.0).rgb * weight[i], 1.0);
	}

	return saturate(color);
}

float4 PS_HazeVert(VS_OUTPUT_POST IN, uniform Texture2D GaussianHaze, float4 v0 : SV_Position0) : SV_Target {
    float4 color = GaussianHaze.Sample(Sampler1, IN.txcoord0.xy);
    float4 orig = TextureColor.Sample(Sampler1, IN.txcoord0.xy);
    [branch]
    if(! ENABLE_HAZE || ! ENABLE_LENS) 	return orig;


	float offset[18] = { 0.0, 1.4953705027, 3.4891992113, 5.4830312105, 7.4768683759, 9.4707125766, 11.4645656736, 13.4584295168, 15.4523059431, 17.4461967743, 19.4661974725, 21.4627427973, 23.4592916956, 25.455844494, 27.4524015179, 29.4489630909, 31.445529535, 33.4421011704 };
	float weight[18] = { 0.033245, 0.0659162217, 0.0636705814, 0.0598194658, 0.0546642566, 0.0485871646, 0.0420045997, 0.0353207015, 0.0288880982, 0.0229808311, 0.0177815511, 0.013382297, 0.0097960001, 0.0069746748, 0.0048301008, 0.0032534598, 0.0021315311, 0.0013582974 };

	color *= weight[0];

    [unroll]
	for(int i = 1; i < 18; ++i) {
		color += float4(GaussianHaze.Sample(Sampler1, IN.txcoord0.xy + float2(0.0, offset[i] * (ScreenSize.y * ScreenSize.z)) * 2.0).rgb * weight[i], 1.0);
		color += float4(GaussianHaze.Sample(Sampler1, IN.txcoord0.xy - float2(0.0, offset[i] * (ScreenSize.y * ScreenSize.z)) * 2.0).rgb * weight[i], 1.0);
	}

	float mask = 1.0 - dot(orig, 0.3333);
    mask = pow(mask, 0.5) * 1.5 - 0.75;
	color = lerp(orig, color, pow(mask, 1.0));
    color = pow(color, 0.97);
	orig = lerp(orig, color, lerp(0.9, 0.50, HAZE_AMOUNT * 0.33));
    orig.rgb = LUTHazeCurve(orig.rgb);

	return saturate(orig);
}
