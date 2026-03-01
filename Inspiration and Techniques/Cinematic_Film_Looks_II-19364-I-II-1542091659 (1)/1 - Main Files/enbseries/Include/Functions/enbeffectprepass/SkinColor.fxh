// Skintoner by Adyss

float3 rgb_to_hsv(float3 RGB)
{
    float r = RGB.x;
    float g = RGB.y;
    float b = RGB.z;

    float minChannel = min(r, min(g, b));
    float maxChannel = max(r, max(g, b));

    float h = 0;
    float s = 0;
    float v = maxChannel;

    float delta = maxChannel - minChannel;

    if (delta != 0)
    {
        s = delta / v;

        if (r == v) h = (g - b) / delta;
        else if (g == v) h = 2 + (b - r) / delta;
        else if (b == v) h = 4 + (r - g) / delta;
    }

    return float3(h, s, v);
}

float3 hsv_to_rgb(float3 HSV)
{
    float3 RGB = HSV.z;

    float h = HSV.x;
    float s = HSV.y;
    float v = HSV.z;

    float i = floor(h);
    float f = h - i;

    float p = (1.0 - s);
    float q = (1.0 - s * f);
    float t = (1.0 - s * (1 - f));

    if (i == 0) { RGB = float3(1, t, p); }
    else if (i == 1) { RGB = float3(q, 1, p); }
    else if (i == 2) { RGB = float3(p, 1, t); }
    else if (i == 3) { RGB = float3(p, q, 1); }
    else if (i == 4) { RGB = float3(t, p, 1); }
    else /* i == -1 */ { RGB = float3(1, p, q); }

    RGB *= v;

    return RGB;
}

float4 ApplyToSkin(float4 SkinColor, float2 coord)
{
    return lerp(TextureColor.Sample(Sampler1, coord), SkinColor, TextureMask.Sample(Sampler1, coord));
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

float4	PS_Skin(VS_OUTPUT_POST IN) : SV_Target
{
    return ApplyToSkin(SkinColorEdit(TextureColor.Sample(Sampler1, IN.txcoord0.xy)), IN.txcoord0.xy);
}
