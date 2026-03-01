/*-----------------------------------------------------------.
/                         Gradient                           /
/            Gradient overlay effect by smack0007            /
/              (ported and modified by IDDQD)\n              /
/							(modified further by roxahris)   /
'-----------------------------------------------------------*/

float3 GradientPass(float3 color, float2 texcoord : TEXCOORD) : SV_Target
{
    float4 Maincolor = float4(0,0,0,0);

    float2 othogonal = normalize(float2(tan((clamp(0.00001, 179.99999, (GRADIENT_ROTATION - 90))) * 0.0174533), - ScreenSize.z));
    float TS_Dist = abs(dot(texcoord.xy - 0.5 - othogonal * ((GRADIENT_SHIFT) * 0.01), othogonal));

    if(GRADIENT_COLORS == 3)
    {
        Maincolor.rgb = lerp( GRADIENT_TOP, GRADIENT_BOTTOM, smoothstep(0.0 + (0.5 - (GRADIENT_RANGE * 0.005)), 1.0 - (0.5 - (GRADIENT_RANGE * 0.005)), TS_Dist));
    }
    else if(GRADIENT_COLORS == 1)
    {
        Maincolor.rgb = lerp( GRADIENT_TOP, 0.5, smoothstep(0.0 + (0.5 - (GRADIENT_RANGE * 0.005)), 1.0 - (0.5 - (GRADIENT_RANGE * 0.005)), TS_Dist));
    }
    else if(GRADIENT_COLORS == 2)
    {
        Maincolor.rgb = lerp( 0.5, GRADIENT_BOTTOM, smoothstep(0.0 + (0.5 - (GRADIENT_RANGE * 0.005)), 1.0 - (0.5 - (GRADIENT_RANGE * 0.005)), TS_Dist));
    }

    Maincolor.rgb = BlendLinearLightf(color.rgb,Maincolor.rgb);


    color.rgb = lerp(color.rgb,Maincolor.rgb,(GRADIENT_OPACITY * 0.01));

    return color;
}
