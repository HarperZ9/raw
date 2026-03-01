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
// Skin shader suite created by Adyss             //
////////////////////////////////////////////////////


// MACROS //////////////////////////////////////////
    #include "Include/Internals/Macros.fxh"


// ENB SETUP ///////////////////////////////////////
    #include "Include/Internals/PrePass.fxh"


// UI //////////////////////////////////////////////
    UI_BOOL(SkinSmoothing, "Skin Smoothing", false)
    UI_BLANK(2)
    UI_MSG(2, "    Skin Shader by Adyss")
    UI_MSG(3, "\xA9 2018")
    #define SkinGamma 1.1
    #define SkinExposure 0.0
    #define SkinTint float3(0.15, 0.15, 0.15)
    #define SkinTintStrength 1.0
    #define SkinHue 0.0
    #define HueOpacity 0.0
    #define SkinSmoothingPower 0.5
    #define HighlightMasking false
    #define Threshold 0.2
    #define ThresholdRange 0.2


// FUNCTIONS ///////////////////////////////////////
    #include "Include/Functions/enbeffectprepass/SkinColor.fxh"
    #include "Include/Functions/enbeffectprepass/SkinBlur.fxh"


// TECHNIQUE ///////////////////////////////////////
    TECHNIQUE_UI(skin, "CFL II - Skin",
        PASS(p0, VS_PreProcess, PS_Skin)
    )

    TECHNIQUE(skin1,
        PASS(p0, VS_PreProcess, PS_Hblur)
    )

    TECHNIQUE(skin2,
        PASS(p0, VS_PreProcess, PS_Vblur)
    )
