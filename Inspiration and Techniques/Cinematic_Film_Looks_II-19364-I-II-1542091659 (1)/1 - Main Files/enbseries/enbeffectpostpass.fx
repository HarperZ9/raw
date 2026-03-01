////////////////////////////////////////////////////
//                                                //
//         ::::::::       ::::::::::       :::    //
//       :+:    :+:      :+:              :+:     //
//      +:+             +:+              +:+      //
//     +#+             :#::+::#         +#+       //
//    +#+             +#+              +#+        //
//   #+#    #+#      #+#              #+#         //
//   ########       ###              ##########   //
//                                                //
//            CINEMATIC FILM LOOKS II             //
//        CREATED BY TREYM FOR SKYRIM SE          //
//                                                //
////////////////////////////////////////////////////


// CONFIG FILE /////////////////////////////////////
    #include "CFL_Settings.txt"


// MACROS //////////////////////////////////////////
    #include "Include/Internals/Macros.fxh"


// ENB SETUP ///////////////////////////////////////
    #include "Include/Internals/Quality.fxh"
    #include "Include/Internals/PostPass.fxh"


// UI //////////////////////////////////////////////
    #include "Include/UI/UI_PostPass.fxh"


// FUNCTIONS ///////////////////////////////////////
    #include "Include/Functions/generic/LOG.fxh"
    #include "Include/Functions/generic/BlendingModes.fxh"
    #include "Include/Functions/generic/RGBHSV.fxh"
    #include "Include/Functions/enbeffectpostpass/3DLUT.fxh"
    #include "Include/Functions/enbeffectpostpass/Age.fxh"

    #if(COLORBLIND_ASSIST == 1)
        #include "Include/Functions/enbeffectpostpass/ColorBlind.fxh"
    #endif

    #if(CFL_QUALITY == 2)
        #include "Include/Functions/enbeffectpostpass/HDR.fxh"
        #include "Include/Functions/enbeffectpostpass/LensHaze.fxh"
        #include "Include/Functions/enbeffectpostpass/Dither.fxh"
        #include "Include/Functions/enbeffectpostpass/LensCA.fxh"
    #endif

    #include "Include/Functions/enbeffectpostpass/Arri.fxh"
    #include "Include/Functions/enbeffectpostpass/ToneMap.fxh"
    #include "Include/Functions/enbeffectpostpass/Vibrance.fxh"
    #include "Include/Functions/enbeffectpostpass/LumaSharpen.fxh"
    #include "Include/Functions/enbeffectpostpass/LiftGammaGain.fxh"

    #if(ACTIVATE_HSL)
        #include "Include/Functions/enbeffectpostpass/HSL.fxh"
    #endif

    #include "Include/Functions/enbeffectpostpass/Gradient.fxh"
    #include "Include/Functions/enbeffectpostpass/Vignette.fxh"
    #include "Include/Functions/enbeffectpostpass/Aspect.fxh"
    #include "Include/Functions/enbeffectpostpass/Curves.fxh"
    #include "Include/Functions/enbeffectpostpass/Levels.fxh"
    #include "Include/Functions/enbeffectpostpass/FilmGrain.fxh"
    #include "Include/Functions/enbeffectpostpass/FXAA.fxh"
    #include "Include/Functions/enbeffectpostpass/SMAA.fxh"
    #include "Include/Functions/enbeffectpostpass/Grid.fxh"
    #include "Include/Functions/enbeffectpostpass/PhotoSuite.fxh"
    #include "Include/Functions/ui/UIBox.fxh"


// PIXEL SHADERS ///////////////////////////////////

    float4	PS_Sharpen(VS_OUTPUT_POST IN) : SV_Target
    {
    	float4 color = TextureColor.Sample(Sampler0, IN.txcoord0.xy);
        if(SHARP_AMOUNT != 0) color = float4(LumaSharpenPass(IN.txcoord0.xy), 1.0);

    	return color;
    }

    float4	PS_Shared_1(VS_OUTPUT_POST IN) : SV_Target
    {
    	float4 color = TextureColor.Sample(Sampler0, IN.txcoord0.xy);

        color = VignettePass(color, IN.txcoord0.xy);
        color = TonemapPass(color);
        color = VibrancePass(color);
        color = CurvesPassLuma(color);
        color = CurvesPassChroma(color);
        color.rgb = LUTArri(color.rgb);

        #if(USER_LUT != 1)
            color.rgb = LUTFunc(color.rgb);
        #else
            if(!CUSTOM_LUT) color.rgb = LUTFunc(color.rgb);
            if(CUSTOM_LUT) color.rgb  = LUTCustom(color.rgb);
        #endif

        #if(ACTIVATE_HSL)
            if(HSL_SHIFT && ENABLE_GRADE) color = float4(HSLShift(color.rgb), 1.0);
        #endif

        if(PHOTO_SUITE && PHOTO_LEAKS) color.rgb = LeakPass(color.rgb, IN.txcoord0.xy);
        if(GRAIN_AMOUNT != 0) color.xyz = GrainPass(IN.txcoord0.xy, color.xyz);
        if(ENABLE_LGG && ENABLE_GRADE) color = LiftGammaGainPass(color);
        if(ENABLE_GRADIENT && ENABLE_GRADE) color.rgb = GradientPass(color.rgb, IN.txcoord0.xy);
        if(ENABLE_GRADE) color = LevelsPass(color);

    	return color;
    }

    float4	PS_Overlays(VS_OUTPUT_POST IN) : SV_Target
    {
    	float4 color = TextureColor.Sample(Sampler0, IN.txcoord0.xy);

        color = LevelsPass2(color);

        if(ENABLE_AGE && !AGE_OVERLAYS) color = AgePass(color);

        // Photo Frame Function //////////
        if(PHOTO_SUITE && PHOTO_FRAME) color.rgb  = FramePass(color.rgb, IN.txcoord0.xy);

        // Photo Damage Function /////////
        if(PHOTO_SUITE && PHOTO_DAMAGE) color.rgb = DamagePass(color.rgb, IN.txcoord0.xy);

        if(ENABLE_AGE && AGE_OVERLAYS) color = AgePass(color);

        // Photo Dirt Function ///////////
        if(PHOTO_SUITE && PHOTO_DIRT) color.rgb   = DirtPass(color.rgb, IN.txcoord0.xy);

    	return color;
    }

    float4	PS_Final(VS_OUTPUT_POST IN) : SV_Target
    {
        float4 color = TextureColor.Sample(Sampler0, IN.txcoord0.xy);

        #if(COLORBLIND_ASSIST == 1)
            if(CB_ENABLE && LUT_PACK != 5) color.rgb = ColorBlindPass(color.rgb);
        #endif

        #if(CFL_QUALITY == 2)
            if(!ENABLE_TUTORIAL) color.xyz += triDither(color.xyz, IN.txcoord0.xy, Timer.x);
        #endif

        if(ENABLE_GRID) color.rgb = GridPass(color.rgb, IN.txcoord0.xy);
        if(ENABLE_BORDER) color = BorderPass(color, IN.txcoord0.xy);

        return color;
    }


// TECHNIQUES //////////////////////////////////////

    // No AA //////////////////////////////////////
    #if(CFL_QUALITY == 2)
        TECHNIQUE_UI(CFL, "CFL II",
            PASS(p0, VS_PostProcess, PS_Sharpen)
        )

        TECHNIQUE(CFL1,
                PASS(p0, VS_PostProcess, PS_Dist)
            )

        TECHNIQUE(CFL2,
            PASS(p0, VS_PostProcess, PS_CA)
        )

        TECHNIQUE_RT(CFL3, "RenderTargetRGBA32",
            PASS_ARGS_PS(p0, VS_PostProcess, PS_HDRHor, (TextureColor))
        )

        TECHNIQUE(CFL4,
            PASS_ARGS_PS(p0, VS_PostProcess, PS_HDRVert, (RenderTargetRGBA32))
        )

        TECHNIQUE_RT(CFL5, "RenderTargetRGBA32",
            PASS_ARGS_PS(p0, VS_PostProcess, PS_HazeHor, (TextureColor))
        )

        TECHNIQUE(CFL6,
            PASS_ARGS_PS(p0, VS_PostProcess, PS_HazeVert, (RenderTargetRGBA32))
        )

        TECHNIQUE(CFL7,
            PASS(p0, VS_PostProcess, PS_Shared_1)
        )

        TECHNIQUE(CFL8,
            PASS(p0, VS_PostProcess, PS_Overlays)
        )

        TECHNIQUE(CFL9,
            PASS(p0, VS_PostProcess, PS_Final)
        )

        TECHNIQUE_RT(CFL10, "RenderTargetRGBA32",
            PASS_ARGS_PS(p0, VS_PostProcess, PS_UIBlurH, (TextureColor))
        )

        TECHNIQUE(CFL11,
            PASS_ARGS_PS(p0, VS_PostProcess, PS_UIBlurV, (RenderTargetRGBA32))
        )
    #else
        TECHNIQUE_UI(CFL, "CFL II",
            PASS(p0, VS_PostProcess, PS_Sharpen)
        )

        TECHNIQUE(CFL1,
            PASS(p0, VS_PostProcess, PS_Shared_1)
        )

        TECHNIQUE(CFL2,
            PASS(p0, VS_PostProcess, PS_Overlays)
        )

        TECHNIQUE(CFL3,
            PASS(p0, VS_PostProcess, PS_Final)
        )

        TECHNIQUE_RT(CFL4, "RenderTargetRGBA32",
            PASS_ARGS_PS(p0, VS_PostProcess, PS_UIBlurH, (TextureColor))
        )

        TECHNIQUE(CFL5,
            PASS_ARGS_PS(p0, VS_PostProcess, PS_UIBlurV, (RenderTargetRGBA32))
        )
    #endif


    // With AA ////////////////////////////////////
    #if(CFL_QUALITY == 2)
        TECHNIQUE_UI_RT(CFLAA,  "CFL II - AA", SMAA_STRING(SMAA_EDGE_TEX),
            PASS(Clear, VS_SMAAClear, PS_SMAAClear)
            PASS(EdgeDetection, VS_SMAAEdgeDetection, PS_SMAAEdgeDetection)
        )

        TECHNIQUE_RT(CFLAA1, SMAA_STRING(SMAA_BLEND_TEX),
            PASS(Clear, VS_SMAAClear, PS_SMAAClear)
            PASS(BlendingWeightCalculation, VS_SMAABlendingWeightCalculation, PS_SMAABlendingWeightCalculation)
        )

        TECHNIQUE(CFLAA2,
            PASS(NeighborhoodBlending, VS_SMAANeighborhoodBlending, PS_SMAANeighborhoodBlending)
        )

        TECHNIQUE(CFLAA3,
            PASS(p0, VS_PostProcess, PS_FXAA)
            )

        TECHNIQUE(CFLAA4,
            PASS(p0, VS_PostProcess, PS_Sharpen)
        )

        TECHNIQUE(CFLAA5,
            PASS(p0, VS_PostProcess, PS_Dist)
        )

        TECHNIQUE(CFLAA6,
            PASS(p0, VS_PostProcess, PS_CA)
        )

        TECHNIQUE_RT(CFLAA7, "RenderTargetRGBA32",
            PASS_ARGS_PS(p0, VS_PostProcess, PS_HDRHor, (TextureColor))
        )

        TECHNIQUE(CFLAA8,
            PASS_ARGS_PS(p0, VS_PostProcess, PS_HDRVert, (RenderTargetRGBA32))
        )

        TECHNIQUE_RT(CFLAA9, "RenderTargetRGBA32",
            PASS_ARGS_PS(p0, VS_PostProcess, PS_HazeHor, (TextureColor))
        )

        TECHNIQUE(CFLAA10,
            PASS_ARGS_PS(p0, VS_PostProcess, PS_HazeVert, (RenderTargetRGBA32))
        )

        TECHNIQUE(CFLAA11,
            PASS(p0, VS_PostProcess, PS_Shared_1)
        )

        TECHNIQUE(CFLAA12,
            PASS(p0, VS_PostProcess, PS_Overlays)
        )

        TECHNIQUE(CFLAA13,
            PASS(p0, VS_PostProcess, PS_Final)
        )

        TECHNIQUE_RT(CFLAA14, "RenderTargetRGBA32",
            PASS_ARGS_PS(p0, VS_PostProcess, PS_UIBlurH, (TextureColor))
        )

        TECHNIQUE(CFLAA15,
            PASS_ARGS_PS(p0, VS_PostProcess, PS_UIBlurV, (RenderTargetRGBA32))
        )
    #else
        TECHNIQUE_UI_RT(CFLAA,  "CFL II - AA", SMAA_STRING(SMAA_EDGE_TEX),
            PASS(Clear, VS_SMAAClear, PS_SMAAClear)
            PASS(EdgeDetection, VS_SMAAEdgeDetection, PS_SMAAEdgeDetection)
        )

        TECHNIQUE_RT(CFLAA1, SMAA_STRING(SMAA_BLEND_TEX),
            PASS(Clear, VS_SMAAClear, PS_SMAAClear)
            PASS(BlendingWeightCalculation, VS_SMAABlendingWeightCalculation, PS_SMAABlendingWeightCalculation)
        )

        TECHNIQUE(CFLAA2,
            PASS(NeighborhoodBlending, VS_SMAANeighborhoodBlending, PS_SMAANeighborhoodBlending)
        )

        TECHNIQUE(CFLAA3,
            PASS(p0, VS_PostProcess, PS_FXAA)
            )

        TECHNIQUE(CFLAA4,
            PASS(p0, VS_PostProcess, PS_Sharpen)
        )

        TECHNIQUE(CFLAA5,
            PASS(p0, VS_PostProcess, PS_Shared_1)
        )

        TECHNIQUE(CFLAA6,
            PASS(p0, VS_PostProcess, PS_Overlays)
        )

        TECHNIQUE(CFLAA7,
            PASS(p0, VS_PostProcess, PS_Final)
        )

        TECHNIQUE_RT(CFLAA8, "RenderTargetRGBA32",
            PASS_ARGS_PS(p0, VS_PostProcess, PS_UIBlurH, (TextureColor))
        )

        TECHNIQUE(CFLAA9,
            PASS_ARGS_PS(p0, VS_PostProcess, PS_UIBlurV, (RenderTargetRGBA32))
        )
    #endif
