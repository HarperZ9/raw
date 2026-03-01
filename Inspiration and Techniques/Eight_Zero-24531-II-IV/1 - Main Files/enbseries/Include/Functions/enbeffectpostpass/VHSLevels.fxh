// VHS Levels is adapted from various SweetFX shaders
// written by Christian Cann Schuldt Jensen ~ CeeJay.dk
// Tonemap, Curves, Monochrome, Levels
// Modified by TreyM

// Original Eight Zero VHS Levels without curves enabled
float4 VHSLevelsPass(float4 color)
{
    // Gamma Adjustment
    color = pow(color, 0.8);

    // Contrast Curve
    float3 lumCoeff = float3(0.2126, 0.7152, 0.0722);
    float Curves_contrast_blend = 0.9;
    float luma = dot(lumCoeff, color.rgb);
    float3 chroma = color.rgb - luma;
    float3 x = color.rgb;

    x = x - 0.5;
    x = ( x / (0.5 + abs(x)) ) + 0.5;

    float3 final = x;
    color.rgb = lerp(color.rgb, final, Curves_contrast_blend);

    // De-Saturate
    float3 grey = dot(float3(0.21, 0.72, 0.07), color.rgb);
    color.rgb = lerp(grey, color.rgb, 0.9);


    // Adjust Levels
    color.rgb = color.rgb * (255.0 / ( 350 - (-16))) - ((-16 / 255.0) *  (255.0 / ( 350 - (-16))));

    return color;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// 3D LUT SHADER WITH MULTI-LUT SUPPORT VIA TEXTURE ATLAS
// DNI SUPPORT LEFT IN FOR POTENTIAL FUTURE USE
// CODE BY KINGERIC1992
// MODIFIED BY TREYM TO AUTO-ADJUST TO TEXTURE SIZE
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

    TEXTURE(LutTapes, "Include/Textures/LUTs/Tapes/Tapes.png")

    float3 VHSLutPass(float3 color) {
        // Pre Gamma Boost
        color = pow(color, 0.75);

        // Pre Saturate
        //float3 grey = dot(float3(0.21, 0.72, 0.07), color.rgb);
        //color.rgb = lerp(grey, color.rgb, 1.2);

        // Find the texture width and height
        int2 tex_size;
        LutTapes.GetDimensions(tex_size.x, tex_size.y);

        // Define the correct LUT size
        float2 CLut_pSize = {1.0 / tex_size.x, 1.0 / sqrt(tex_size.x)};
        color.rgb = saturate(color.rgb) * (sqrt(tex_size.x) - 1.0);
        float3 CLut_UV;
        CLut_UV.z = floor(color.z);
        color.z  -= CLut_UV.z;
        color.xy  = (color.xy + 0.5) * CLut_pSize;
        color.x  += CLut_UV.z * CLut_pSize.y;
        color.y  *= (sqrt(tex_size.x) / tex_size.y);
        CLut_UV.x = color.x;
        CLut_UV.z = CLut_UV.x + CLut_pSize.y;

        float3 lut;
        CLut_UV.y = color.y + (TAPE_SELECT - 1) * (sqrt(tex_size.x) / tex_size.y);
        lut = lerp(LutTapes.SampleLevel(SamplerLinear, CLut_UV.xy, 0).rgb, LutTapes.SampleLevel(SamplerLinear, CLut_UV.zy, 0).rgb, color.z);

        // Apply the LUT
        color.rgb = lut;//pow(lut, 1.0) + 0.0392156862745098;

        return saturate(color.rgb);
    }
