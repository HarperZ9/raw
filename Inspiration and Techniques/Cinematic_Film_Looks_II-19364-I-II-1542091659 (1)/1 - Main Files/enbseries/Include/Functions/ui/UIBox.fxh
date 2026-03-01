// GAUSSIAN BLUR BY IOXA ///////////////////////////

TEXTURE(UIMask12,     "Include/Textures/UI/UIMask.png")

TEXTURE(Tutorial,          "Include/Textures/UI/UITutorial.png")

TEXTURE(Highlight123,   "Include/Textures/UI/Presets/Highlight/h123.png")
TEXTURE(Highlight456,   "Include/Textures/UI/Presets/Highlight/h456.png")
TEXTURE(Highlight789,   "Include/Textures/UI/Presets/Highlight/h789.png")
TEXTURE(Highlight10,  "Include/Textures/UI/Presets/Highlight/h10.png")

TEXTURE(PresetList123,      "Include/Textures/UI/Presets/p123.png")
TEXTURE(PresetList456,      "Include/Textures/UI/Presets/p456.png")


float4 PS_UIBlurH(VS_OUTPUT_POST IN, uniform Texture2D GaussianUI, float4 v0 : SV_Position0) : SV_Target
{
    float4 color = TextureColor.Sample(Sampler1, IN.txcoord0.xy);
    [branch]
    if(!ENABLE_TUTORIAL && !PRESET_LIST || FANCY_MSGBOX == 0 || CFL_QUALITY != 2) return color;

	float offset[18] = { 0.0, 1.4953705027, 3.4891992113, 5.4830312105, 7.4768683759, 9.4707125766, 11.4645656736, 13.4584295168, 15.4523059431, 17.4461967743, 19.4661974725, 21.4627427973, 23.4592916956, 25.455844494, 27.4524015179, 29.4489630909, 31.445529535, 33.4421011704 };
	float weight[18] = { 0.033245, 0.0659162217, 0.0636705814, 0.0598194658, 0.0546642566, 0.0485871646, 0.0420045997, 0.0353207015, 0.0288880982, 0.0229808311, 0.0177815511, 0.013382297, 0.0097960001, 0.0069746748, 0.0048301008, 0.0032534598, 0.0021315311, 0.0013582974 };

	color *= weight[0];

    [unroll]
	for(int i = 1; i < 18; ++i) {
		color += float4(GaussianUI.Sample(Sampler1, IN.txcoord0.xy + float2(offset[i] * ScreenSize.y, 0.0) * 2.0).rgb * weight[i], 1.0);
		color += float4(GaussianUI.Sample(Sampler1, IN.txcoord0.xy - float2(offset[i] * ScreenSize.y, 0.0) * 2.0).rgb * weight[i], 1.0);
	}

	return saturate(color);
}

float4 PS_UIBlurV(VS_OUTPUT_POST IN, uniform Texture2D GaussianUI, float4 v0 : SV_Position0) : SV_Target
{
    float4 color = GaussianUI.Sample(Sampler1, IN.txcoord0.xy);
    float4 orig = TextureColor.Sample(Sampler1, IN.txcoord0.xy);

    [branch]
    if(!ENABLE_TUTORIAL && !PRESET_LIST) return orig;

    float mask;
    float msg;
    float hlt;
    float3 UIMask = UIMask12.Sample(Sampler1, IN.txcoord0.xy);
    float3 UITut = Tutorial.Sample(Sampler1, IN.txcoord0.xy);
    float3 hl123 = Highlight123.Sample(Sampler1, IN.txcoord0.xy);
    float3 hl456 = Highlight456.Sample(Sampler1, IN.txcoord0.xy);
    float3 hl789 = Highlight789.Sample(Sampler1, IN.txcoord0.xy);
    float3 list123 = PresetList123.Sample(Sampler1, IN.txcoord0.xy);
    float3 list456 = PresetList456.Sample(Sampler1, IN.txcoord0.xy);

    #if(FANCY_MSGBOX == 0 || CFL_QUALITY != 2)
        if(ENABLE_TUTORIAL)
        {
            mask = UIMask.g;
            if(TUTORIAL_PAGE == 1)      msg = UITut.r;
            else if(TUTORIAL_PAGE == 2) msg = UITut.g;
            else if(TUTORIAL_PAGE == 3) msg = UITut.b;
        }

        if(!ENABLE_TUTORIAL && PRESET_LIST)
        {
            mask = UIMask.r;
            if(LUT_SELECT == 1)       hlt = hl123.r;
            else if(LUT_SELECT == 2)  hlt = hl123.g;
            else if(LUT_SELECT == 3)  hlt = hl123.b;
            else if(LUT_SELECT == 4)  hlt = hl456.r;
            else if(LUT_SELECT == 5)  hlt = hl456.g;
            else if(LUT_SELECT == 6)  hlt = hl456.b;
            else if(LUT_SELECT == 7)  hlt = hl789.r;
            else if(LUT_SELECT == 8)  hlt = hl789.g;
            else if(LUT_SELECT == 9)  hlt = hl789.b;
            else if(LUT_SELECT == 10) hlt = Highlight10.Sample(Sampler1, IN.txcoord0.xy);

            if(LUT_PACK == 1)      msg = list123.r;
            else if(LUT_PACK == 2) msg = list123.g;
            else if(LUT_PACK == 3) msg = list123.b;
            else if(LUT_PACK == 4) msg = list456.r;
            else if(LUT_PACK == 5) msg = list456.g;
            else if(LUT_PACK == 6) msg = list456.b;
        }

    	color = lerp(orig, pow(color, 0.5) * 0.5 + 0.1, mask);
        if(!ENABLE_TUTORIAL && PRESET_LIST) color = BlendScreenf(color, hlt);
        color = BlendScreenf(color, msg);

    #else

    	float offset[18] = { 0.0, 1.4953705027, 3.4891992113, 5.4830312105, 7.4768683759, 9.4707125766, 11.4645656736, 13.4584295168, 15.4523059431, 17.4461967743, 19.4661974725, 21.4627427973, 23.4592916956, 25.455844494, 27.4524015179, 29.4489630909, 31.445529535, 33.4421011704 };
    	float weight[18] = { 0.033245, 0.0659162217, 0.0636705814, 0.0598194658, 0.0546642566, 0.0485871646, 0.0420045997, 0.0353207015, 0.0288880982, 0.0229808311, 0.0177815511, 0.013382297, 0.0097960001, 0.0069746748, 0.0048301008, 0.0032534598, 0.0021315311, 0.0013582974 };

    	color *= weight[0];

        [unroll]
    	for(int i = 1; i < 18; ++i)
        {
    		color += float4(GaussianUI.Sample(Sampler1, IN.txcoord0.xy + float2(0.0, offset[i] * (ScreenSize.y * ScreenSize.z)) * 2.0).rgb * weight[i], 1.0);
    		color += float4(GaussianUI.Sample(Sampler1, IN.txcoord0.xy - float2(0.0, offset[i] * (ScreenSize.y * ScreenSize.z)) * 2.0).rgb * weight[i], 1.0);
    	}

        if(ENABLE_TUTORIAL)
        {
            mask = UIMask.g;
            if(TUTORIAL_PAGE == 1)      msg = UITut.r;
            else if(TUTORIAL_PAGE == 2) msg = UITut.g;
            else if(TUTORIAL_PAGE == 3) msg = UITut.b;
        }

        if(!ENABLE_TUTORIAL && PRESET_LIST)
        {
            mask = UIMask.r;
            if(LUT_SELECT == 1)       hlt = hl123.r;
            else if(LUT_SELECT == 2)  hlt = hl123.g;
            else if(LUT_SELECT == 3)  hlt = hl123.b;
            else if(LUT_SELECT == 4)  hlt = hl456.r;
            else if(LUT_SELECT == 5)  hlt = hl456.g;
            else if(LUT_SELECT == 6)  hlt = hl456.b;
            else if(LUT_SELECT == 7)  hlt = hl789.r;
            else if(LUT_SELECT == 8)  hlt = hl789.g;
            else if(LUT_SELECT == 9)  hlt = hl789.b;
            else if(LUT_SELECT == 10) hlt = Highlight10.Sample(Sampler1, IN.txcoord0.xy);

            if(LUT_PACK == 1)      msg = list123.r;
            else if(LUT_PACK == 2) msg = list123.g;
            else if(LUT_PACK == 3) msg = list123.b;
            else if(LUT_PACK == 4) msg = list456.r;
            else if(LUT_PACK == 5) msg = list456.g;
            else if(LUT_PACK == 6) msg = list456.b;
        }

    	color = lerp(orig, pow(color, 0.8) * 0.75 + 0.05, pow(mask, 1.0));
        if(!ENABLE_TUTORIAL && PRESET_LIST) color = BlendScreenf(color, lerp(0.0, hlt, 0.75));
        color = BlendScreenf(color, lerp(0.0, msg, 0.75));

    #endif

	return saturate(color);
}
