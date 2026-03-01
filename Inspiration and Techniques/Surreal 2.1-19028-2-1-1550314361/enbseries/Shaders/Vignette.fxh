// Just a super simple Vignette

float4 Vignette(float4 Color, float2 coord)
{
    return Color *= pow(16.0*coord.x*coord.y*(1.0-coord.x)*(1.0-coord.y), VignetteAmount );
}