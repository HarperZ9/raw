// Skintoner by Adyss

float4 ApplyToSkin(float4 SkinColor, float2 coord)
{
    return lerp(TextureColor.Sample(LinearSampler, coord), SkinColor, TextureMask.Sample(LinearSampler, coord));
}

float3 Reinhard(float3 x)
{
	return (x*(1+(x/pow(2, 2))))/(1+x);
}

float4 SkinColorEdit(float4 Color)
{
    Color      = pow(Color, SkinGamma);
    Color     *= pow(2.0f, SkinExposure);
    Color.rgb  = lerp(Color, Color * SkinTint * 2.55, SkinTintStrength);
    float3 hsv = rgb_to_hsv(Color.rgb);
    hsv.x     += SkinHue;
    Color.rgb  = lerp(Color, hsv_to_rgb(hsv), HueOpacity);
    Color.rgb  = Reinhard(Color); // Acts as white limiter so users dont have crushing or clipping issues
    return       saturate(Color);
}

float4	PS_Skin(VS_OUTPUT IN) : SV_Target
{
    return ApplyToSkin(SkinColorEdit(TextureColor.Sample(LinearSampler, IN.txcoord.xy)), IN.txcoord.xy);
}