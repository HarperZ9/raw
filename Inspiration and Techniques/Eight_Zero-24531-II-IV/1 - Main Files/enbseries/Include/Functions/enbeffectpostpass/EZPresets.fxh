TEXTURE(LutEZDay, "Include/Textures/LUTs/Presets/EZDay.png")
TEXTURE(LutEZNight, "Include/Textures/LUTs/Presets/EZDay.png")
TEXTURE(LutEZInterior, "Include/Textures/LUTs/Presets/EZDay.png")

TEXTURE(LutFilter, "Include/Textures/LUTs/Presets/night_filter.png")

    float3 EZPresetPass(float3 color)
    {
        int2 tex_size;
        float3 CLut_UV;

        // Find the texture width and height
        LutEZDay.GetDimensions(tex_size.x, tex_size.y);

        // Define the correct LUT size
        float2 CLut_pSize = {1.0 / tex_size.x, 1.0 / sqrt(tex_size.x)};
        color.rgb       = saturate(color.rgb) * (sqrt(tex_size.x) - 1.0);
        CLut_UV.z = floor(color.z);
        color.z  -= CLut_UV.z;
        color.xy  = (color.xy + 0.5) * CLut_pSize;
        color.x  += CLut_UV.z * CLut_pSize.y;
        color.y  *= (sqrt(tex_size.x) / tex_size.y);
        CLut_UV.x = color.x;
        CLut_UV.z = CLut_UV.x + CLut_pSize.y;


        // Day
        float3 lutcolor_D;
        CLut_UV.y = color.y + (EZ_PRESET - 1) * (sqrt(tex_size.x) / tex_size.y);
        lutcolor_D = lerp(LutEZDay.SampleLevel(SamplerLinear, CLut_UV.xy, 0).rgb, LutEZDay.SampleLevel(SamplerLinear, CLut_UV.zy, 0).rgb, color.z);

        // Night
        float3 lutcolor_N;
        CLut_UV.y = color.y + (EZ_PRESET - 1) * (sqrt(tex_size.x) / tex_size.y);

        lutcolor_N = lerp(LutEZNight.SampleLevel(SamplerLinear, CLut_UV.xy, 0).rgb, LutEZNight.SampleLevel(SamplerLinear, CLut_UV.zy, 0).rgb, color.z);

        // Interior
        float3 lutcolor_I;
        CLut_UV.y = color.y + (EZ_PRESET - 1) * (sqrt(tex_size.x) / tex_size.y);

        lutcolor_I = lerp(LutEZInterior.SampleLevel(SamplerLinear, CLut_UV.xy, 0).rgb, LutEZInterior.SampleLevel(SamplerLinear, CLut_UV.zy, 0).rgb, color.z);

        // Apply the LUT
        color.rgb = lerp(lerp( lutcolor_N, lutcolor_D, ENightDayFactor), lutcolor_I, EInteriorFactor);

        return saturate(color.rgb);
    }

    float3 NightFilterPass(float3 color)
    {
        float3 orig = color;
        int2 tex_size;
        float3 CLut_UV;

        // Find the texture width and height
        LutFilter.GetDimensions(tex_size.x, tex_size.y);

        // Define the correct LUT size
        float2 CLut_pSize = {1.0 / tex_size.x, 1.0 / sqrt(tex_size.x)};
        color.rgb = saturate(color.rgb) * (sqrt(tex_size.x) - 1.0);
        CLut_UV.z = floor(color.z);
        color.z  -= CLut_UV.z;
        color.xy  = (color.xy + 0.5) * CLut_pSize;
        color.x  += CLut_UV.z * CLut_pSize.y;
        color.y  *= (sqrt(tex_size.x) / tex_size.y);
        CLut_UV.x = color.x;
        CLut_UV.z = CLut_UV.x + CLut_pSize.y;

        // Day
        float3 lutcolor;
        CLut_UV.y = color.y * (sqrt(tex_size.x) / tex_size.y);
        lutcolor = lerp(LutFilter.SampleLevel(SamplerLinear, CLut_UV.xy, 0).rgb, LutFilter.SampleLevel(SamplerLinear, CLut_UV.zy, 0).rgb, color.z);

        // Apply the LUT
        color.rgb = lerp(lerp(lutcolor, orig, ENightDayFactor), orig, EInteriorFactor);

        return saturate(color.rgb);
    }
