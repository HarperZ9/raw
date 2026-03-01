// LUMASHARPEN SHADER //////////////////////////////
////////////////////////////////////////////////////
// By Christian Cann Schuldt Jensen ~ CeeJay.dk   //
// Depth limiter added by TreyM                   //
////////////////////////////////////////////////////

// FUNCTION //////////////////////////////////////
    float3 LumaSharpenPass( float2 txcoord )
    {
        #define sharp_clamp 0.035

        // Grab the framebuffer //////////////////
        float3 ori = TextureColor.Sample(Sampler1, txcoord).rgb;

        // Grab the depth buffer /////////////////
        float3 depth = TextureDepth.Sample(Sampler0, txcoord);

        // Linearize the depth buffer ////////////
        depth *= rcp(mad(depth,-2999.0,3000.0));

        // Invert and desaturate the depth buffer
        depth = 1.0 - depth;
        depth = pow(depth, 6.0);

        float3 sharp_strength_luma = (float3(0.2126, 0.7152, 0.0722) * (SHARP_AMOUNT * 0.033));

        float3 blur_ori;
        blur_ori  = TextureColor.Sample(Sampler1, txcoord + float2(ScreenSize.y, (ScreenSize.y * ScreenSize.z) * -1.0) * 0.5 * 1.0).rgb;
    	blur_ori += TextureColor.Sample(Sampler1, txcoord - float2(ScreenSize.y, (ScreenSize.y * ScreenSize.z)) * 0.5 * 1.0).rgb;
    	blur_ori += TextureColor.Sample(Sampler1, txcoord + float2(ScreenSize.y, (ScreenSize.y * ScreenSize.z)) * 0.5 * 1.0).rgb;
    	blur_ori += TextureColor.Sample(Sampler1, txcoord - float2(ScreenSize.y,(ScreenSize.y * ScreenSize.z) * -1.0) * 0.5 * 1.0).rgb;

        blur_ori *= 0.25;

        float3 sharp = ori - blur_ori;

        float4 sharp_strength_luma_clamp = float4(sharp_strength_luma * (0.5 / sharp_clamp),0.5);

        float sharp_luma = saturate(dot(float4(sharp,1.0), sharp_strength_luma_clamp));
        sharp_luma = (sharp_clamp * 2.0) * sharp_luma - sharp_clamp;

        float3 outputcolor = DEPTH_SHARPEN ? lerp(ori, ori + sharp_luma, depth) : ori + sharp_luma;

        return saturate(outputcolor);
    }
