/////////////////////////////////////////////////////////
//                                                     //
//          888888888               000000000          //
//        88:::::::::88           00:::::::::00        //
//      88:::::::::::::88       00:::::::::::::00      //
//     8::::::88888::::::8     0:::::::000:::::::0     //
//     8:::::8     8:::::8     0::::::0   0::::::0     //
//     8:::::8     8:::::8     0:::::0     0:::::0     //
//      8:::::88888:::::8      0:::::0     0:::::0     //
//       8:::::::::::::8       0:::::0 000 0:::::0     //
//      8:::::88888:::::8      0:::::0 000 0:::::0     //
//     8:::::8     8:::::8     0:::::0     0:::::0     //
//     8:::::8     8:::::8     0:::::0     0:::::0     //
//     8:::::8     8:::::8     0::::::0   0::::::0     //
//     8::::::88888::::::8     0:::::::000:::::::0     //
//      88:::::::::::::88       00:::::::::::::00      //
//        88:::::::::88           00:::::::::00        //
//          888888888               000000000          //
//                                                     //
//                     EIGHT ZERO                      //
//                  An ENB by TreyM                    //
//                                                     //
/////////////////////////////////////////////////////////
//                                                     //
// Boris Vorontsov: For ENBSeries                      //
//                                                     //
// JawZ:  Author and developer of the MSL code         //
//                                                     //
// CeeJay.dk-Crosire-GemFx-Marty McFly-Lucifer Hawk:   //
//        Original Authors of sweeFX & Reshade code    //
//                                                     //
// martinsh:                                           //
//        Original author of film grain shader         //
//                                                     //
// roxahris:                                           //
//        Port of film martinsh's film grain to ENB    //
//                                                     //
// kingeric1992:                                       //
//        Port of VHS shaders from shadertoy to ENB    //
//                                                     //
// TreyM :  Shader Setup, and Settings,                //
//        Port and Modification of ReShade Shaders     //
//                                                     //
/////////////////////////////////////////////////////////

// EIGHT ZERO CONFIGURATION //////////////////////
    #include "Include/Configuration/ez_config.cfg"


// ENB MACROS ////////////////////////////////////
    #include "Include/Internals/Macros.fxh"


// POST PASS INTERNALS ///////////////////////////
    #include "Include/Internals/enbeffectpostpass.fxh"


// UI ////////////////////////////////////////////
    #include "Include/UI/UI_PostPass.fxh"


// FUNCTIONS /////////////////////////////////////
    #include "Include/Functions/enbeffectpostpass/SMAA.fxh"
    #include "Include/Functions/enbeffectpostpass/LumaSharpen.fxh"
    #include "Include/Functions/enbeffectpostpass/Vignette.fxh"
    #include "Include/Functions/enbeffectpostpass/EZPresets.fxh"
    #include "Include/Functions/enbeffectpostpass/VHSLevels.fxh"
    #include "Include/Functions/enbeffectpostpass/VHS.fxh"
    #include "Include/Functions/enbeffectpostpass/CRT.fxh"
    #include "Include/Functions/enbeffectpostpass/VCR.fxh"
    #include "Include/Functions/enbeffectpostpass/Aspect.fxh"
    #include "Include/Functions/enbeffectpostpass/TVOverlay.fxh"

    #if (ENB_QUALITY == 2)
        #include "Include/Functions/enbeffectpostpass/LensDistortion.fxh"
        #include "Include/Functions/enbeffectpostpass/DirectionalBlur.fxh"
        #include "Include/Functions/enbeffectpostpass/LensHaze.fxh"
        #include "Include/Functions/enbeffectpostpass/FilmGrain.fxh"
        #include "Include/Functions/enbeffectpostpass/Dither.fxh"
    #endif


// SHADERS ///////////////////////////////////////
    float4	PS_Shared_1(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
    {
        float4 color = TextureColor.Sample(SamplerLinear, IN.txcoord0.xy);

        if(ENABLE_LENS && VIGNETTE_AMOUNT != 0) color = VignettePass(color, IN.txcoord0.xy);

        if(CAMERA) color = pow(color, CAM_GAMMA), color *= pow(2.0f, CAM_EXPOSURE);

        #if(ENB_QUALITY == 2)
            if(ENABLE_GRAIN) color.xyz = GrainPass35mm(IN.txcoord0.xy, color.xyz);
        #endif

        color.rgb = NightFilterPass(color.rgb);
        color.rgb = EZPresetPass(color.rgb);

        if(ENABLE_BORDER && !VHS_RATIO && ENABLE_CRT && ENABLE_CRT ? TV_OVERLAY ? !TV_LETTERBOX : ENABLE_BORDER : ENABLE_BORDER) color = BorderPass(color, IN.txcoord0.xy);

        #if(ENB_QUALITY == 2)
            if(ENABLE_VHS && ENABLE_TAPE_DIST) color.xyz = GrainPassVHS(IN.txcoord0.xy, color.xyz);
        #endif

        if(ENABLE_VHS && VHS_EMU && TAPE_SELECT == 5) color = VHSLevelsPass(color);
        if(ENABLE_VHS && VHS_EMU && TAPE_SELECT != 5) color.rgb = VHSLutPass(color.rgb);
        if(VHS_bUseTapeNoise && ENABLE_TAPE_DIST)  color = VHS_TapeNoise(color, IN.txcoord0);
        if(VHS_bUseLayerNoise && ENABLE_TAPE_DIST) color = VHS_LayerNoise(color, IN.txcoord0);

        return color;
    }

    #if(ENB_QUALITY == 2)
        float4	PS_Warp(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
        {
            float4 color = TextureColor.Sample(SamplerPoint, IN.txcoord0);
            if(ENABLE_BORDER ? ENABLE_CRT ? !TV_OVERLAY ? BORDER_RATIO > 1.33 : !TV_OVERLAY : BORDER_RATIO > 1.33 : (!ENABLE_CRT || !TV_OVERLAY)) return color;

            color = float4(VHS_VCR(TextureColor, IN.txcoord0), 1.0);

            return color;
        }
    #endif

    float4	PS_Shared_2(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
    {
        float4 color = TextureColor.Sample(SamplerPoint, IN.txcoord0.xy);
        if(VCR_OSD && ENABLE_VHS && !ENABLE_TUTORIAL)
        {
            color.rgb = VCROSDPass(color.rgb, IN.txcoord0.xy);
            if(VCR_SELECT > 5)
            {
                if(ENABLE_VHS && VHS_EMU && TAPE_SELECT == 5) color = VHSLevelsPass(color);
                if(ENABLE_VHS && VHS_EMU && TAPE_SELECT != 5) color.rgb = VHSLutPass(color.rgb);
            }
        }
        if(ENABLE_TUTORIAL) color.rgb = TutorialPass(color.rgb, IN.txcoord0.xy);
        if(ENABLE_BORDER && !VHS_RATIO && !ENABLE_CRT) color = BorderPass(color, IN.txcoord0.xy);

        return color;
    }

    float4	PS_VHS(VHS_struct IN) : SV_Target
    {
      float4 color = TextureColor.Sample(SamplerPoint, IN.coord);
      if(ENABLE_VHS && ENABLE_TAPE_DIST)color = VHS_VHS(TextureColor, IN.coord);
    	return color;
    }

    #if(ENB_QUALITY == 2)
        float4	PS_NTSC(VHS_struct IN) : SV_Target
        {
          float4 color = TextureColor.Sample(SamplerPoint, IN.coord);
          if(!ENABLE_NTSC || !ENABLE_CRT) return color;
              color = float4(VHS_NTSC_decoder(TextureColor, IN.NTSCuv), 1.0);
        	return color;
        }
    #endif

    float4	PS_Shared_3(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
    {
        float4 color = TextureColor.Sample(SamplerPoint, IN.txcoord0.xy);
        float3 grey = dot(float3(0.21, 0.72, 0.07), color.rgb);

        if(ENABLE_CRT) color = saturate(pow(color, 1.0) * lerp(0.5, 1.5, (TV_CONTRAST * 0.01)) + lerp(-0.150, 0.150, (TV_BRIGHTNESS * 0.01)));
        #if(ENB_QUALITY == 2)
            if(ENABLE_CRT) color.rgb = saturate(lerp(grey, color.rgb, lerp(lerp(0.0, 2.0, (TV_COLOR * 0.01)), lerp(0.0, 2.75, (TV_COLOR * 0.01)), ENABLE_NTSC)));
        #else
            if(ENABLE_CRT) color.rgb = saturate(lerp(grey, color.rgb, lerp(0.0, 2.0, (TV_COLOR * 0.01))));
        #endif

        return color;
    }

    float4	PS_TV(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
    {
        float4 color = TextureColor.Sample(SamplerPoint, IN.txcoord0.xy);

        if(TV_OVERLAY && ENABLE_CRT) color.rgb = TVPass(color.rgb, IN.txcoord0.xy);

        return color;
    }

    float4	PS_Final(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
    {
        float4 color = TextureColor.Sample(SamplerPoint, IN.txcoord0.xy);

        #if(ENB_QUALITY == 2)
            color.xyz += triDither(color.xyz, IN.txcoord0.xy, Timer.x);
        #endif

        if(ENABLE_BORDER && ENABLE_CRT ? !TV_OVERLAY : ENABLE_BORDER) color = BorderPass(color, IN.txcoord0.xy);

        return color;
    }

    // No SMAA ////////////////////////////////////
    #if(ENB_QUALITY == 2)
        TECHNIQUE_UI(EZ, "EIGHT ZERO",
            PASS(p0, VS_PostProcess, PS_Dist)
        )

        TECHNIQUE_RT(EZ1, "RenderTargetRGBA32",
            PASS_ARGS_PS(p0, VS_PostProcess, PS_HazeHor, (TextureColor))
        )

        TECHNIQUE(EZ2,
            PASS_ARGS_PS(p0, VS_PostProcess, PS_HazeVert, (RenderTargetRGBA32))
        )

        TECHNIQUE(EZ3,
            PASS(p0, VS_PostProcess, PS_CA)
        )

        TECHNIQUE(EZ4,
            PASS(p0, VS_PostProcess, PS_Sharpen)
        )

        TECHNIQUE(EZ5,
            PASS(p0, VS_PostProcess, PS_Shared_1)
        )

        TECHNIQUE(EZ6,
            PASS(p0, VS_PostProcess, PS_Warp)
        )

        TECHNIQUE(EZ7,
            PASS(p0, VS_PostProcess, PS_Shared_2)
        )

        TECHNIQUE(EZ8,
            PASS(p0, VS_VHSnPost, PS_VHS)
        )

        TECHNIQUE(EZ9,
            PASS(p0, VS_PostProcess, PS_Blur)
        )

        TECHNIQUE(EZ10,
            PASS(p0, VS_PostProcess, PS_TVDist)
        )

        TECHNIQUE(EZ11,
            PASS(p0, VS_VHSnPost, PS_NTSC)
        )
        // Second Sharpen Pass
        TECHNIQUE(EZ12,
            PASS(p0, VS_PostProcess, PS_Sharpen)
        )

        TECHNIQUE(EZ13,
            PASS(p0, VS_PostProcess, PS_Shared_3)
        )

        TECHNIQUE(EZ14,
            PASS(p0, VS_PostProcess, PS_CRT)
        )

        TECHNIQUE(EZ15,
            PASS(p0, VS_PostProcess, PS_TV)
        )

        TECHNIQUE_RT(EZ16, "RenderTargetRGBA32",
            PASS_ARGS_PS(p0, VS_PostProcess, PS_TVGlowH, (TextureColor))
        )

        TECHNIQUE(EZ17,
            PASS_ARGS_PS(p0, VS_PostProcess, PS_TVGlowV, (RenderTargetRGBA32))
        )

        TECHNIQUE(EZ18,
            PASS(p0, VS_PostProcess, PS_Final)
        )
    #else
        TECHNIQUE_UI(EZ, "EIGHT ZERO",
            PASS(p0, VS_PostProcess, PS_Sharpen)
        )

        TECHNIQUE(EZ1,
            PASS(p0, VS_PostProcess, PS_Shared_1)
        )

        TECHNIQUE(EZ2,
            PASS(p0, VS_PostProcess, PS_Shared_2)
        )

        TECHNIQUE(EZ3,
            PASS(p0, VS_VHSnPost, PS_VHS)
        )

        TECHNIQUE(EZ4,
            PASS(p0, VS_PostProcess, PS_Shared_3)
        )

        TECHNIQUE(EZ5,
            PASS(p0, VS_PostProcess, PS_CRT)
        )

        TECHNIQUE(EZ6,
            PASS(p0, VS_PostProcess, PS_Final)
        )
    #endif

    #if(ENB_QUALITY == 2)
        // With SMAA //////////////////////////////////
        TECHNIQUE_UI_RT(EZAA, "EIGHT ZERO - SMAA", SMAA_STRING(SMAA_EDGE_TEX),
            PASS(Clear, VS_SMAAClear, PS_SMAAClear)
            PASS(EdgeDetection, VS_SMAAEdgeDetection, PS_SMAAEdgeDetection)
        )

        TECHNIQUE_RT(EZAA1, SMAA_STRING(SMAA_BLEND_TEX),
            PASS(Clear, VS_SMAAClear, PS_SMAAClear)
            PASS(BlendingWeightCalculation, VS_SMAABlendingWeightCalculation, PS_SMAABlendingWeightCalculation)
        )

        TECHNIQUE(EZAA2,
            PASS(NeighborhoodBlending, VS_SMAANeighborhoodBlending, PS_SMAANeighborhoodBlending)
        )

        TECHNIQUE(EZAA3,
            PASS(p0, VS_PostProcess, PS_Dist)
        )

        TECHNIQUE_RT(EZAA4, "RenderTargetRGBA32",
            PASS_ARGS_PS(p0, VS_PostProcess, PS_HazeHor, (TextureColor))
        )

        TECHNIQUE(EZAA5,
            PASS_ARGS_PS(p0, VS_PostProcess, PS_HazeVert, (RenderTargetRGBA32))
        )

        TECHNIQUE(EZAA6,
            PASS(p0, VS_PostProcess, PS_CA)
        )

        TECHNIQUE(EZAA7,
            PASS(p0, VS_PostProcess, PS_Sharpen)
        )

        TECHNIQUE(EZAA8,
            PASS(p0, VS_PostProcess, PS_Shared_1)
        )

        TECHNIQUE(EZAA9,
            PASS(p0, VS_PostProcess, PS_Warp)
        )

        TECHNIQUE(EZAA10,
            PASS(p0, VS_PostProcess, PS_Shared_2)
        )

        TECHNIQUE(EZAA11,
            PASS(p0, VS_VHSnPost, PS_VHS)
        )

        TECHNIQUE(EZAA12,
            PASS(p0, VS_PostProcess, PS_TVDist)
        )

        TECHNIQUE(EZAA13,
            PASS(p0, VS_VHSnPost, PS_NTSC)
        )

        TECHNIQUE(EZAA14,
            PASS(p0, VS_PostProcess, PS_Blur)
        )

        // Second Sharpen Pass
        TECHNIQUE(EZAA15,
            PASS(p0, VS_PostProcess, PS_Sharpen)
        )

        TECHNIQUE(EZAA16,
            PASS(p0, VS_PostProcess, PS_Shared_3)
        )

        TECHNIQUE(EZAA17,
            PASS(p0, VS_PostProcess, PS_CRT)
        )

        TECHNIQUE(EZAA18,
            PASS(p0, VS_PostProcess, PS_TV)
        )

        TECHNIQUE_RT(EZAA19, "RenderTargetRGBA32",
            PASS_ARGS_PS(p0, VS_PostProcess, PS_TVGlowH, (TextureColor))
        )

        TECHNIQUE(EZAA20,
            PASS_ARGS_PS(p0, VS_PostProcess, PS_TVGlowV, (RenderTargetRGBA32))
        )

        TECHNIQUE(EZAA21,
            PASS(p0, VS_PostProcess, PS_Final)
        )
    #else
        TECHNIQUE_UI_RT(EZAA, "EIGHT ZERO - SMAA", SMAA_STRING(SMAA_EDGE_TEX),
            PASS(Clear, VS_SMAAClear, PS_SMAAClear)
            PASS(EdgeDetection, VS_SMAAEdgeDetection, PS_SMAAEdgeDetection)
        )

        TECHNIQUE_RT(EZAA1, SMAA_STRING(SMAA_BLEND_TEX),
            PASS(Clear, VS_SMAAClear, PS_SMAAClear)
            PASS(BlendingWeightCalculation, VS_SMAABlendingWeightCalculation, PS_SMAABlendingWeightCalculation)
        )

        TECHNIQUE(EZAA2,
            PASS(NeighborhoodBlending, VS_SMAANeighborhoodBlending, PS_SMAANeighborhoodBlending)
        )

        TECHNIQUE(EZAA3,
            PASS(p0, VS_PostProcess, PS_Sharpen)
        )

        TECHNIQUE(EZAA4,
            PASS(p0, VS_PostProcess, PS_Shared_1)
        )

        TECHNIQUE(EZAA5,
            PASS(p0, VS_PostProcess, PS_Shared_2)
        )

        TECHNIQUE(EZAA6,
            PASS(p0, VS_VHSnPost, PS_VHS)
        )

        TECHNIQUE(EZAA7,
            PASS(p0, VS_PostProcess, PS_Shared_3)
        )

        TECHNIQUE(EZAA8,
            PASS(p0, VS_PostProcess, PS_CRT)
        )

        TECHNIQUE(EZAA9,
            PASS(p0, VS_PostProcess, PS_Final)
        )
    #endif
