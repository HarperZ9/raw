// GUASSIAN BLUR SUITE ///////////////////////////
// Original code by Ioxa, modified by TreyM     //
//////////////////////////////////////////////////
#define DETAIL_LEVEL LINE_MODE != 5 ? (DETAIL_AMOUNT * 0.01) : 0.75

float4 ColorEdgeH(VS_OUTPUT IN, uniform Texture2D TEX_ColorEdge) : SV_Target
{
    float4 color = TextureColor.Sample(LinearSampler, IN.txcoord.xy);

	float offset[4] =
    {
        0.0, 1.1824255238, 3.0293122308, 5.0040701377
    };

	float weight[4] =
    {
        0.39894, 0.2959599993, 0.0045656525, 0.00000149278686458842
    };

    color *= weight[0];

    [unroll]
	for(int i = 1; i < 4; ++i)
    {
		color += float4(TEX_ColorEdge.Sample(LinearSampler, IN.txcoord.xy + float2(offset[i] * ScreenSize.y, 0.0) * lerp(0.0, 1.0, DETAIL_LEVEL)).rgb * weight[i], 1.0);
		color += float4(TEX_ColorEdge.Sample(LinearSampler, IN.txcoord.xy - float2(offset[i] * ScreenSize.y, 0.0) * lerp(0.0, 1.0, DETAIL_LEVEL)).rgb * weight[i], 1.0);
	}

	return saturate(color);
}

float4 ColorEdgeV(VS_OUTPUT IN, uniform Texture2D TEX_ColorEdge) : SV_Target
{
    float4 color = TEX_ColorEdge.Sample(LinearSampler, IN.txcoord.xy);
    float4 orig  = TextureColor.Sample(LinearSampler, IN.txcoord.xy);

	float offset[4] =
    {
        0.0, 1.1824255238, 3.0293122308, 5.0040701377
    };

	float weight[4] =
    {
        0.39894, 0.2959599993, 0.0045656525, 0.00000149278686458842
    };

	color *= weight[0];

    [unroll]
	for(int i = 1; i < 4; ++i)
    {
		color += float4(TEX_ColorEdge.Sample(LinearSampler, IN.txcoord.xy + float2(0.0, offset[i] * (ScreenSize.y * ScreenSize.z)) * lerp(0.0, 1.0, DETAIL_LEVEL)).rgb * weight[i], 1.0);
		color += float4(TEX_ColorEdge.Sample(LinearSampler, IN.txcoord.xy - float2(0.0, offset[i] * (ScreenSize.y * ScreenSize.z)) * lerp(0.0, 1.0, DETAIL_LEVEL)).rgb * weight[i], 1.0);
	}

    color = saturate(lerp(0.0, 1.5, color)) * 4.0;
    orig  = saturate(lerp(0.0, 1.5, orig)) * 4.0;

    color = BlendDifference(orig, color);

    color = pow(color, lerp(0.75, 0.45, (DETAIL_AMOUNT * 0.01))) * 3.0;

    color = floor(color);
    color = color / 2.0;

    color.rgb = GetLuma(color.rgb, Rec709_5);

	return saturate(color);
}

float4 PageSoftenH(VS_OUTPUT IN, uniform Texture2D TEX_Soften) : SV_Target
{
    float4 color = TextureColor.Sample(LinearSampler, IN.txcoord.xy);

	float offset[4] =
    {
        0.0, 1.1824255238, 3.0293122308, 5.0040701377
    };

	float weight[4] =
    {
        0.39894, 0.2959599993, 0.0045656525, 0.00000149278686458842
    };

    color *= weight[0];

    [unroll]
	for(int i = 1; i < 4; ++i)
    {
		color += float4(TEX_Soften.Sample(LinearSampler, IN.txcoord.xy + float2(offset[i] * ScreenSize.y, 0.0) * 0.33).rgb * weight[i], 1.0);
		color += float4(TEX_Soften.Sample(LinearSampler, IN.txcoord.xy - float2(offset[i] * ScreenSize.y, 0.0) * 0.33).rgb * weight[i], 1.0);
	}

	return saturate(color);
}

float4 PageSoftenV(VS_OUTPUT IN, uniform Texture2D TEX_Soften) : SV_Target
{
    float4 color = TEX_Soften.Sample(LinearSampler, IN.txcoord.xy);

	float offset[4] =
    {
        0.0, 1.1824255238, 3.0293122308, 5.0040701377
    };

	float weight[4] =
    {
        0.39894, 0.2959599993, 0.0045656525, 0.00000149278686458842
    };

	color *= weight[0];

    [unroll]
	for(int i = 1; i < 4; ++i)
    {
		color += float4(TEX_Soften.Sample(LinearSampler, IN.txcoord.xy + float2(0.0, offset[i] * (ScreenSize.y * ScreenSize.z)) * 0.33).rgb * weight[i], 1.0);
		color += float4(TEX_Soften.Sample(LinearSampler, IN.txcoord.xy - float2(0.0, offset[i] * (ScreenSize.y * ScreenSize.z)) * 0.33).rgb * weight[i], 1.0);
	}

	return saturate(color);
}
