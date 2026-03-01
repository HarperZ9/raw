//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// 3D LUT SHADER WITH MULTI-LUT SUPPORT VIA TEXTURE ATLAS
// DNI SUPPORT LEFT IN FOR POTENTIAL FUTURE USE
// CODE BY KINGERIC1992
// MODIFIED BY TREYM TO AUTO-ADJUST TO TEXTURE SIZE
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

    TEXTURE(LutAtlas, "Include/Textures/LUTs/Presets/CFL2Atlas.png")
    TEXTURE(CustomLut, "Include/Textures/LUTs/CustomLut.png")
    TEXTURE(HazeCurveLut, "Include/Textures/LUTs/Technical/HazeCurve.png")


// CFL LUT Function ////////////////////////////////
    float3 LUTFunc(float3 inColor)
    {
        int2 tex_size;
        float3 CLut_UV;

        // Find the texture width and height
        LutAtlas.GetDimensions(tex_size.x, tex_size.y);

        // Find the correct LUT size
        float2 CLut_pSize = {1.0 / tex_size.x, 1.0 / sqrt(tex_size.x)};
        inColor.rgb       = saturate(inColor.rgb) * (sqrt(tex_size.x) - 1.0);
        CLut_UV.z = floor(inColor.z);
        inColor.z  -= CLut_UV.z;
        inColor.xy  = (inColor.xy + 0.5) * CLut_pSize;
        inColor.x  += CLut_UV.z * CLut_pSize.y;
        inColor.y  *= (sqrt(tex_size.x) / tex_size.y);
        CLut_UV.x = inColor.x;
        CLut_UV.z = CLut_UV.x + CLut_pSize.y;

        int INDEX_OFFSET;
        if(LUT_PACK < 2) INDEX_OFFSET = 0;
        if(LUT_PACK > 1) INDEX_OFFSET = (LUT_PACK - 1) * 10;

        float3 lutcolor;
        CLut_UV.y = inColor.y + lerp(0, (LUT_SELECT + INDEX_OFFSET), ENABLE_CFL) * (sqrt(tex_size.x) / tex_size.y);

        lutcolor = lerp(LutAtlas.SampleLevel(Sampler1, CLut_UV.xy, 0).rgb, LutAtlas.SampleLevel(Sampler1, CLut_UV.zy, 0).rgb, inColor.z);

        return saturate(lutcolor);
    }

    #if(CFL_QUALITY == 2)
        float3 LUTHazeCurve(float3 color)
        {
            float2 CLut_pSize =  1 / float2(256, 16);
            float4 CLut_UV    = 0.0;
            float3 orig = color;

            color      = saturate(color) * 15.0;
            CLut_UV.w  = floor(color.b);
            CLut_UV.xy = (color.rg + 0.5) * CLut_pSize;
            CLut_UV.x += CLut_UV.w * CLut_pSize.y;
            CLut_UV.z  = CLut_UV.x + CLut_pSize.y;

            float3 lut = lerp(HazeCurveLut.SampleLevel(Sampler1, CLut_UV.xy, 0).rgb, HazeCurveLut.SampleLevel(Sampler1, CLut_UV.zy, 0).rgb, color.b - CLut_UV.w);

            return lerp(orig, lerp(orig, lut, ENightDayFactor), lerp(1.0, 0.33, HAZE_AMOUNT * 0.33));
        }
    #endif

    #if(USER_LUT == 1)
        float3 LUTCustom(float3 color)
        {
            float2 CLut_pSize =  1 / float2(4096, 64);
            float4 CLut_UV    = 0.0;

            color      = saturate(color) * 63.0;
            CLut_UV.w  = floor(color.b);
            CLut_UV.xy = (color.rg + 0.5) * CLut_pSize;
            CLut_UV.x += CLut_UV.w * CLut_pSize.y;
            CLut_UV.z  = CLut_UV.x + CLut_pSize.y;

            float3 lut = lerp(CustomLut.SampleLevel(Sampler1, CLut_UV.xy, 0).rgb, CustomLut.SampleLevel(Sampler1, CLut_UV.zy, 0).rgb, color.b - CLut_UV.w);

            return lut;
        }
    #endif
