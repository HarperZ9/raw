// GAUSSIAN BLUR BY IOXA ///////////////////////////

float4 PS_HazeHor(VS_OUTPUT_POST IN, uniform Texture2D GaussianHaze, float4 v0 : SV_Position0) : SV_Target {
    float4 color = TextureColor.Sample(SamplerLinear, IN.txcoord0.xy);
    if(! ENABLE_HAZE || ! ENABLE_LENS) return color;

    float offset[11] = { 0.0, 1.4895848401, 3.4757135714, 5.4618796741, 7.4481042327, 9.4344079746, 11.420811147, 13.4073334, 15.3939936778, 17.3808101174, 19.3677999584 };
	float weight[11] = { 0.06649, 0.1284697563, 0.111918249, 0.0873132676, 0.0610011113, 0.0381655709, 0.0213835661, 0.0107290241, 0.0048206869, 0.0019396469, 0.0006988718 };

    color *= weight[0];

    [unroll]
	for(int i = 1; i < 11; ++i) {
		color += float4(GaussianHaze.Sample(SamplerLinear, IN.txcoord0.xy + float2(offset[i] * ScreenSize.y, 0.0) * 2.0).rgb * weight[i], 1.0);
		color += float4(GaussianHaze.Sample(SamplerLinear, IN.txcoord0.xy - float2(offset[i] * ScreenSize.y, 0.0) * 2.0).rgb * weight[i], 1.0);
	}

	return saturate(color);
}

float4 PS_HazeVert(VS_OUTPUT_POST IN, uniform Texture2D GaussianHaze, float4 v0 : SV_Position0) : SV_Target {
    float4 color = GaussianHaze.Sample(SamplerLinear, IN.txcoord0.xy);
    float4 orig = TextureColor.Sample(SamplerLinear, IN.txcoord0.xy);
    if(! ENABLE_HAZE || ! ENABLE_LENS) 	return orig;

    float offset[11] = { 0.0, 1.4895848401, 3.4757135714, 5.4618796741, 7.4481042327, 9.4344079746, 11.420811147, 13.4073334, 15.3939936778, 17.3808101174, 19.3677999584 };
	float weight[11] = { 0.06649, 0.1284697563, 0.111918249, 0.0873132676, 0.0610011113, 0.0381655709, 0.0213835661, 0.0107290241, 0.0048206869, 0.0019396469, 0.0006988718 };

	color *= weight[0];

    [unroll]
	for(int i = 1; i < 11; ++i) {
		color += float4(GaussianHaze.Sample(SamplerLinear, IN.txcoord0.xy + float2(0.0, offset[i] * (ScreenSize.y * ScreenSize.z)) * 2.0).rgb * weight[i], 1.0);
		color += float4(GaussianHaze.Sample(SamplerLinear, IN.txcoord0.xy - float2(0.0, offset[i] * (ScreenSize.y * ScreenSize.z)) * 2.0).rgb * weight[i], 1.0);
	}

    float mask = 1.0 - dot(orig, 0.3333);
	color = lerp(orig, color, mask);
	orig = lerp(orig, color, lerp(0.25, 0.75, (HAZE_AMOUNT - 10) * 0.025));

	return saturate(orig);
}
