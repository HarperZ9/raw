// GAUSSIAN BLUR BY IOXA ///////////////////////////

float4 PS_HDRHor(VS_OUTPUT_POST IN, uniform Texture2D GaussianHDR, float4 v0 : SV_Position0) : SV_Target {
    float4 color = TextureColor.Sample(Sampler1, IN.txcoord0.xy);
    [branch]
    if(!ENABLE_HDR) return TextureColor.Sample(Sampler0, IN.txcoord0.xy);

    float offset[6] = { 0.0, 1.4584295168, 3.40398480678, 5.3518057801, 7.302940716, 9.2581597095 };
	float weight[6] = { 0.13298, 0.23227575, 0.1353261595, 0.0511557427, 0.01253922, 0.0019913644 };

    color *= weight[0];

    [unroll]
	for(int i = 1; i < 6; ++i) {
		color += float4(GaussianHDR.Sample(Sampler1, IN.txcoord0.xy + float2(offset[i] * ScreenSize.y, 0.0) * 2.0).rgb * weight[i], 1.0);
		color += float4(GaussianHDR.Sample(Sampler1, IN.txcoord0.xy - float2(offset[i] * ScreenSize.y, 0.0) * 2.0).rgb * weight[i], 1.0);
	}

	return saturate(color);
}

float4 PS_HDRVert(VS_OUTPUT_POST IN, uniform Texture2D GaussianHDR, float4 v0 : SV_Position0) : SV_Target {
    float4 color = GaussianHDR.Sample(Sampler1, IN.txcoord0.xy);
    float4 orig = TextureColor.Sample(Sampler1, IN.txcoord0.xy);
    [branch]
    if(!ENABLE_HDR) return TextureColor.Sample(Sampler0, IN.txcoord0.xy);

    float offset[6] = { 0.0, 1.4584295168, 3.40398480678, 5.3518057801, 7.302940716, 9.2581597095 };
	float weight[6] = { 0.13298, 0.23227575, 0.1353261595, 0.0511557427, 0.01253922, 0.0019913644 };

	color *= weight[0];

    [unroll]
	for(int i = 1; i < 6; ++i) {
		color += float4(GaussianHDR.Sample(Sampler1, IN.txcoord0.xy + float2(0.0, offset[i] * (ScreenSize.y * ScreenSize.z)) * 2.0).rgb * weight[i], 1.0);
		color += float4(GaussianHDR.Sample(Sampler1, IN.txcoord0.xy - float2(0.0, offset[i] * (ScreenSize.y * ScreenSize.z)) * 2.0).rgb * weight[i], 1.0);
	}

    float4 mask = dot(float3(0.21, 0.72, 0.07), pow(color, 0.625) * 0.625);
    float4 invert = 1.0 - mask;
	orig = BlendSoftLightf(pow(orig, lerp(1.0, 1.175, HDR_AMOUNT * 0.01)), lerp(0.5, invert, HDR_AMOUNT * 0.01));

	return saturate(orig);
}
