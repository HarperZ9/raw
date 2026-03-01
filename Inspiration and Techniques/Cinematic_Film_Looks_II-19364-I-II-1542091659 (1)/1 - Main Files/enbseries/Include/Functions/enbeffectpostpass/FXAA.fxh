// FXAA by geeks3d
// Shadertoy Implementation by Reinder
// ENB port by Adyss
// Ref: https://www.shadertoy.com/view/ls3GWS
//      http://www.geeks3d.com/20110405/fxaa-fast-approximate-anti-aliasing-demo-glsl-opengl-test-radeon-geforce/3/
//      https://anteru.net/blog/2016/mapping-between-hlsl-and-glsl/index.html

#if ENABLE_FXAA == 1
    // Settings
    #define FXAA_SPAN_MAX      24.0
    #define FXAA_REDUCE_MUL   (1.0/FXAA_SPAN_MAX)
    #define FXAA_REDUCE_MIN   (1.0/18.0)
    #define FXAA_SUBPIX_SHIFT (1.0/9.0)
    #define Resolution        float2(ScreenSize.x, ScreenSize.x * ScreenSize.w) // Display Resolution

    float3 PS_FXAA(VS_OUTPUT_POST IN) : SV_Target
    {
        float2 rcpFrame = 1 / Resolution;
        float4 uv;
        uv.xy = IN.txcoord0.xy;
        uv.zw = IN.txcoord0.xy - (rcpFrame * (0.5 + FXAA_SUBPIX_SHIFT));

        float3 rgbNW = TextureColor.SampleLevel(Sampler1, uv.zw, 0.0).xyz;
        float3 rgbNE = TextureColor.SampleLevel(Sampler1, uv.zw + (1,0)*rcpFrame.xy, 0.0).xyz;
        float3 rgbSW = TextureColor.SampleLevel(Sampler1, uv.zw + (0,1)*rcpFrame.xy, 0.0).xyz;
        float3 rgbSE = TextureColor.SampleLevel(Sampler1, uv.zw + (1,1)*rcpFrame.xy, 0.0).xyz;
        float3 rgbM  = TextureColor.SampleLevel(Sampler1, uv.xy, 0.0).xyz;

        float3 luma  = (0.299, 0.587, 0.114);
        float lumaNW = dot(rgbNW, luma);
        float lumaNE = dot(rgbNE, luma);
        float lumaSW = dot(rgbSW, luma);
        float lumaSE = dot(rgbSE, luma);
        float lumaM  = dot(rgbM,  luma);

        float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
        float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

        float2 dir;
        dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
        dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

        float dirReduce = max(
            (lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * FXAA_REDUCE_MUL),
            FXAA_REDUCE_MIN);
        float rcpDirMin = 1.0/(min(abs(dir.x), abs(dir.y)) + dirReduce);

        dir = min(( FXAA_SPAN_MAX,  FXAA_SPAN_MAX),
              max((-FXAA_SPAN_MAX, -FXAA_SPAN_MAX),
              dir * rcpDirMin)) * rcpFrame.xy;

        float3 rgbA = (1.0/2.0) * (
            TextureColor.SampleLevel(Sampler1, uv.xy + dir * (1.0/3.0 - 0.5), 0.0).xyz +
            TextureColor.SampleLevel(Sampler1, uv.xy + dir * (2.0/3.0 - 0.5), 0.0).xyz);
        float3 rgbB = rgbA * (1.0/2.0) + (1.0/4.0) * (
            TextureColor.SampleLevel(Sampler1, uv.xy + dir * (0.0/3.0 - 0.5), 0.0).xyz +
            TextureColor.SampleLevel(Sampler1, uv.xy + dir * (3.0/3.0 - 0.5), 0.0).xyz);

        float lumaB = dot(rgbB, luma);

        if((lumaB < lumaMin) || (lumaB > lumaMax)) return rgbA;

        return rgbB;
    }
#else
    float3 PS_FXAA(VS_OUTPUT_POST IN) : SV_Target
    {
        return TextureColor.Sample(Sampler0, IN.txcoord0.xy);
    }
#endif
