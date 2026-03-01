// Subsurface Scattering by Jorge Jimenez
// Souce: http://www.iryoku.com/screen-space-subsurface-scattering
// Note after testing: Seems not to work that well or i am doing it wrong

UI_FLOAT(correction,             "Skin correction",                  0.5, 300.0, 0.5)
UI_FLOAT(sssStrength,            "SSS Power",                  0.5, 30.0, 0.5)


float4 PS_Blur(VS_OUTPUT IN, uniform float2 dir) : SV_TARGET
{
    float2 coord = IN.txcoord.xy;
    // Gaussian weights for the six samples around the current pixel:
    //   -3 -2 -1 +1 +2 +3
    float w[6] = { 0.006,   0.061,   0.242,  0.242,  0.061, 0.006 };
    float o[6] = {  -1.0, -0.6667, -0.3333, 0.3333, 0.6667,   1.0 };

    // Fetch color and linear depth for current pixel:
    float4 colorM = TextureColor.Sample(PointSampler, coord);
    float depthM = GetLinearizedDepth(coord);

    // Accumulate center sample, multiplying it with its gaussian weight:
    float4 colorBlurred = colorM;
    colorBlurred.rgb *= 0.382;

    // Calculate the step that we will use to fetch the surrounding pixels,
    // where "step" is:
    //     step = sssStrength * gaussianWidth * pixelSize * dir
    // The closer the pixel, the stronger the effect needs to be, hence
    // the factor 1.0 / depthM.
    float2 step = sssStrength * w[0] * PixelSize * dir;
    float2 finalStep = step / depthM;

    // Accumulate the other samples:
    [unroll]
    for (int i = 0; i < 6; i++) {
        // Fetch color and depth for current sample:
        float2 offset = IN.txcoord.xy + o[i] * finalStep;
        float3 color = TextureColor.SampleLevel(LinearSampler, offset, 0).rgb;
        float depth = GetLinearizedDepth(offset);

        // If the difference in depth is huge, we lerp color back to "colorM":
        float s = min(0.0125 * correction * abs(depthM - depth), 1.0);
        color = lerp(color, colorM.rgb, s);

        // Accumulate:
        colorBlurred.rgb += w[i] * color;
    }


    // Ady: Apply mask so only skin gets affected
    float4 Original = TextureColor.Sample  (LinearSampler, coord);
    float4 Mask     = TextureMask.Sample   (LinearSampler, coord);
    float4 Output   = lerp(Original, colorBlurred, Mask);

    return Output;
}

technique11 pre1
{
    pass p
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
        SetPixelShader (CompileShader(ps_5_0, PS_Blur(float2(0.2,0.05))));
    }
}

technique11 pre2
{
    pass p
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
        SetPixelShader (CompileShader(ps_5_0, PS_Blur(float2(0.05,0.2))));
    }
}

technique11 pre3
{
    pass p
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
        SetPixelShader (CompileShader(ps_5_0, PS_Blur(float2(0.2,0.05))));
    }
}

technique11 pre4
{
    pass p
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
        SetPixelShader (CompileShader(ps_5_0, PS_Blur(float2(0.05,0.1))));
    }
}