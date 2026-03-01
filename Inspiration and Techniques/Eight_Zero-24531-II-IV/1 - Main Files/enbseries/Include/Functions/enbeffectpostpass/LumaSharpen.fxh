//-------------------LUMASHARPENING--------------------//
// by Christian Cann Schuldt Jensen ~ CeeJay.dk        //
//+++++++++++++++++++++++++++++++++++++++++++++++++++++//
    float3 LumaSharpenPass( float2 tex ) {
        #define px ScreenSize.y
        #define py ScreenSize.y * ScreenSize.z
        #define sharp_clamp 0.028
        #if(ENB_QUALITY == 2)
            #define sharp_strength (SHARP_AMOUNT * 0.05) + (LENS_DIST > 0.0 ? lerp(0.0, ENABLE_DISTORTION && !TV_OVERLAY_DIST ? 0.6 : 0.0, LENS_DIST * 0.0066) : 0.0) + (LENS_CA > 0.0 ? lerp(0.0, 0.7, LENS_CA * 0.033) : 0.0)
        #else
            #define sharp_strength (SHARP_AMOUNT * 0.1)
        #endif
        #define offset_bias 1.0
        #define CoefLuma float3(0.2126, 0.7152, 0.0722)

        float3 ori = TextureColor.Sample(SamplerLinear, tex).rgb;

        float3 sharp_strength_luma = (CoefLuma * sharp_strength);

        float3 blur_ori = TextureColor.Sample(SamplerLinear, tex + float2(px,-py) * 0.5 * offset_bias).rgb;
        blur_ori += TextureColor.Sample(SamplerLinear, tex + float2(-px,-py) * 0.5 * offset_bias).rgb;
        blur_ori += TextureColor.Sample(SamplerLinear, tex + float2(px,py) * 0.5 * offset_bias).rgb;
        blur_ori += TextureColor.Sample(SamplerLinear, tex + float2(-px,py) * 0.5 * offset_bias).rgb;

        blur_ori *= 0.25;

        float3 sharp = ori - blur_ori;

        float4 sharp_strength_luma_clamp = float4(sharp_strength_luma * (0.5 / sharp_clamp),0.5);

        float sharp_luma = saturate(dot(float4(sharp,1.0), sharp_strength_luma_clamp));
        sharp_luma = (sharp_clamp * 2.0) * sharp_luma - sharp_clamp;

        float3 outputcolor = ori + sharp_luma;

        #undef px
        #undef py

        return saturate(outputcolor);
    }

    float4	PS_Sharpen(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
    {
        float4 color = TextureColor.Sample(SamplerLinear, IN.txcoord0.xy);
        if(SHARP_AMOUNT != 0) color = float4(LumaSharpenPass(IN.txcoord0.xy), 1.0);

        return color;
    }
