TEXTURE(ArriColor, "Include/Textures/LUTs/Technical/ArriColor.png")

float3 LUTArri(float3 color) {
    float2 CLut_pSize =  1 / float2(4096, 64);
    float4 CLut_UV    = 0.0;

    color      = saturate(color) * 63.0;
    CLut_UV.w  = floor(color.b);
    CLut_UV.xy = (color.rg + 0.5) * CLut_pSize;
    CLut_UV.x += CLut_UV.w * CLut_pSize.y;
    CLut_UV.z  = CLut_UV.x + CLut_pSize.y;

    float3 lut = lerp(ArriColor.SampleLevel(Sampler1, CLut_UV.xy, 0).rgb, ArriColor.SampleLevel(Sampler1, CLut_UV.zy, 0).rgb, color.b - CLut_UV.w);

    return lut;
}
