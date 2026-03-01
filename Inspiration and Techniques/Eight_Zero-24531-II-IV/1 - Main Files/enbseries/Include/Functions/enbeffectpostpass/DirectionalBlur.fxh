//////////////////////////////////////////////////
// Blur Shader modified from ENB Blur shader by
// Boris Vorontsov

float4    PS_Blur(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
    float4    res;
    float4    color;
    float4    centercolor;
    float2    pixeloffset=ScreenSize.y;
    pixeloffset.y*=ScreenSize.z;

    centercolor=TextureColor.Sample(SamplerPoint, IN.txcoord0.xy);
		if( ! ENABLE_TAPE_DIST || ! ENABLE_VHS ) return centercolor;
    color=0.0;
    float2    offsets[4]=
    {
        float2(lerp(0.0,-6.0, TAPE_DIST * 0.01),lerp(0.0, 0.15, TAPE_DIST * 0.01)),
        float2(lerp(0.0,-6.0, TAPE_DIST * 0.01),0.0),
        float2(lerp(0.0,6.0, TAPE_DIST * 0.01),lerp(0.0, -0.15, TAPE_DIST * 0.01)),
        float2(lerp(0.0,6.0, TAPE_DIST * 0.01),0.0),
    };
    for (int i=0; i<4; i++)
    {
        float2    coord=offsets[i].xy * pixeloffset.xy * 0.35 + IN.txcoord0.xy;
        color.xyz+=TextureColor.Sample(SamplerLinear, coord.xy);
    }
    color.xyz+=centercolor.xyz;
    color.xyz *= 0.2;
    res.xyz=color.xyz;

    res.w=1.0;
    return res;
}
