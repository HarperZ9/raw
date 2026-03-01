// TV Overlay Textures by exodus123456
Texture2D TVScreen <string ResourceName = "Include/Textures/TV/screen.png";>;
Texture2D TVFrame <string ResourceName = "Include/Textures/TV/frame.png";>;
Texture2D TVBody <string ResourceName = "Include/Textures/TV/body.png";>;
Texture2D TVGlowMask <string ResourceName = "Include/Textures/TV/glow_mask.png";>;

Texture2D TVScreen219 <string ResourceName = "Include/Textures/TV/screen219.png";>;
Texture2D TVFrame219 <string ResourceName = "Include/Textures/TV/frame219.png";>;
Texture2D TVBody219 <string ResourceName = "Include/Textures/TV/body219.png";>;
Texture2D TVGlowMask219 <string ResourceName = "Include/Textures/TV/glow_mask219.png";>;

// Simple Overlay Shader by TreyM ////////////////
float3 TVPass(float3 Color, float2 txCoord)
{
    float4 orig = TextureColor.Sample(SamplerPoint, txCoord);
    float4 overlay;
    float4 blend;
    float4 sourceCoordFactor = 1;
    float2 coords = (txCoord.xy * sourceCoordFactor.xy) - (1.0-sourceCoordFactor.zw) * 0.5;

    if(TV_SCREEN_INTENSITY != 0)
    {
        orig = pow(orig, lerp(1.0, 1.25, (TV_SCREEN_INTENSITY * 0.01)));
        if(ScreenSize.z < 2.0) overlay = TVScreen.Sample(SamplerPoint, coords);
        else overlay = TVScreen219.Sample(SamplerPoint, coords);
        orig = BlendScreenf(orig, saturate(overlay * TV_SCREEN_INTENSITY * 0.01));
    }

    float4 mask;

    if(ScreenSize.z < 2.0)
    {
        mask = 1.0 - TVFrame.Sample(SamplerPoint, coords);
        overlay = 1.0 - mask;
        orig = orig * overlay;
        overlay = pow(TVBody.Sample(SamplerPoint, coords), lerp(1.33, 1.0, (TV_BODY * 0.01)));
        orig = lerp(orig, lerp(orig, overlay, (TV_BODY * 0.01)), mask);
    }
    else
    {
        mask = 1.0 - TVFrame219.Sample(SamplerPoint, coords);
        overlay = 1.0 - mask;
        orig = orig * overlay;
        overlay = TVBody219.Sample(SamplerPoint, coords);
        orig = lerp(orig, lerp(orig, overlay, (TV_BODY * 0.01)), mask);
    }

    return orig;
}

#if(ENB_QUALITY == 2)
// GAUSSIAN BLUR BY IOXA /////////////////////////
float4 PS_TVGlowH(VS_OUTPUT_POST IN, uniform Texture2D GaussianGlow, float4 v0 : SV_Position0) : SV_Target
{
    float4 color = TextureColor.Sample(SamplerLinear, IN.txcoord0.xy);

    [branch]
    if(!ENABLE_CRT || !TV_OVERLAY || !TV_GLOW) return color;

    float offset[15] = { 0.0, 1.4953705027, 3.4891992113, 5.4830312105, 7.4768683759, 9.4707125766, 11.4645656736, 13.4584295168, 15.4523059431, 17.4461967743, 19.4401038149, 21.43402885, 23.4279736431, 25.4219399344, 27.4159294386 };
	float weight[15] = { 0.0443266667, 0.0872994708, 0.0820892038, 0.0734818355, 0.0626171681, 0.0507956191, 0.0392263968, 0.0288369812, 0.0201808877, 0.0134446557, 0.0085266392, 0.0051478359, 0.0029586248, 0.0016187257, 0.0008430913 };

    color *= weight[0];

    [unroll]
	for(int i = 1; i < 15; ++i)
    {
		color += float4(GaussianGlow.Sample(SamplerLinear, IN.txcoord0.xy + float2(offset[i] * ScreenSize.y, 0.0) * 8.0).rgb * weight[i], 1.0);
		color += float4(GaussianGlow.Sample(SamplerLinear, IN.txcoord0.xy - float2(offset[i] * ScreenSize.y, 0.0) * 8.0).rgb * weight[i], 1.0);
	}

	return saturate(color);
}

float4 PS_TVGlowV(VS_OUTPUT_POST IN, uniform Texture2D GaussianGlow, float4 v0 : SV_Position0) : SV_Target
{
    float4 mix, orig;
    float4 color = GaussianGlow.Sample(SamplerLinear, IN.txcoord0.xy);
    mix = orig = TextureColor.Sample(SamplerLinear, IN.txcoord0.xy);

    [branch]
    if(!ENABLE_CRT || !TV_OVERLAY || !TV_GLOW) 	return orig;

    float offset[15] = { 0.0, 1.4953705027, 3.4891992113, 5.4830312105, 7.4768683759, 9.4707125766, 11.4645656736, 13.4584295168, 15.4523059431, 17.4461967743, 19.4401038149, 21.43402885, 23.4279736431, 25.4219399344, 27.4159294386 };
	float weight[15] = { 0.0443266667, 0.0872994708, 0.0820892038, 0.0734818355, 0.0626171681, 0.0507956191, 0.0392263968, 0.0288369812, 0.0201808877, 0.0134446557, 0.0085266392, 0.0051478359, 0.0029586248, 0.0016187257, 0.0008430913 };

	color *= weight[0];

    [unroll]
	for(int i = 1; i < 15; ++i)
    {
		color += float4(GaussianGlow.Sample(SamplerLinear, IN.txcoord0.xy + float2(0.0, offset[i] * (ScreenSize.y * ScreenSize.z)) * 8.0).rgb * weight[i], 1.0);
		color += float4(GaussianGlow.Sample(SamplerLinear, IN.txcoord0.xy - float2(0.0, offset[i] * (ScreenSize.y * ScreenSize.z)) * 8.0).rgb * weight[i], 1.0);
	}

    float4 mask;
    float4 mask2;

    if(ScreenSize.z < 2.0)
    {
        mask = TVGlowMask.Sample(SamplerPoint, IN.txcoord0.xy);
        mask2 = 1.0 - TVFrame.Sample(SamplerPoint, IN.txcoord0.xy);
    }
    else
    {
        mask = TVGlowMask219.Sample(SamplerPoint, IN.txcoord0.xy);
        mask2 = 1.0 - TVFrame219.Sample(SamplerPoint, IN.txcoord0.xy);
    }

    color = lerp(orig, TV_BODY > 0 ? BlendScreenf(orig, pow(color, 1.1)) : pow(color, 1.1), TV_BODY > 0 ? mask : mask2);
    orig = lerp(orig, color, lerp(0.4, 0.8, TV_BODY * 0.01));

	return saturate(orig);
}
#endif
