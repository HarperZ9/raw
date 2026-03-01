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

UI_BLANK(1)
#if(ENB_QUALITY == 2)
    UI_MSG(1, "EIGHT ZERO II")
#else
    UI_MSG(1, "EIGHT ZERO II -  PERFORMANCE MODE")
#endif
UI_MSG(2, "BACK TO THE EIGHTIES")
UI_BLANK(2)
UI_BOOL(ENABLE_TUTORIAL, "   Enable Tutorial", true)
UI_INT(TUTORIAL_PAGE, "      Tutorial Page", 1, 8, 8)
UI_DIVIDER(1)

UI_INT(EZ_PRESET, "PRESET SELECTION", 1, 3, 1)
UI_BLANK(34)
//(NIGHT_FILTER, "   Night Filter (Enabled at Night)", true)
//UI_BLANK(36)
#if(ENB_QUALITY == 2)
    UI_BOOL(ENABLE_GRAIN, "   Film Grain", true)
    UI_INT(GRAIN_AMOUNT, "      Grain Amount", 0, 50, 30)
    UI_BLANK(20)
#endif
UI_INT(SHARP_AMOUNT, "   Sharpening", 0, 50, 6)
UI_DIVIDER(3)

UI_BOOL(CAMERA, "CAMERA", true)
UI_FLOAT_FINE(CAM_EXPOSURE, "   Exposure", -3.0, 3.0, 0.01, 0.0)
UI_FLOAT_FINE(CAM_GAMMA, "   Gamma", 0.01, 4.0, 0.01, 1.0)
UI_DIVIDER(16)

UI_BOOL(ENABLE_LENS, "ENABLE LENS EMULATION", true)
#if(ENB_QUALITY == 2)
    UI_BOOL(ENABLE_DISTORTION, "   Enable Distortion", true)
    UI_INT(LENS_DIST, "   Distortion Amount", -150, 150, 35)
    UI_INT(LENS_CA, "   Aberration Amount", 0, 30, 10)
    UI_BLANK(3)
    UI_BOOL(ENABLE_HAZE, "   Lens Haze", true)
    UI_INT(HAZE_AMOUNT, "   Haze Strength", 10, 50, 25)
    UI_BLANK(4)
#endif
UI_INT(VIGNETTE_AMOUNT, "   Vignette Intensity", 0, 100, 30)
UI_INT(VIGNETTE_WIDTH, "   Vignette Width", 1, 25, 10)
UI_DIVIDER(4)

UI_BOOL(ENABLE_VHS, "ENABLE VCR EFFECTS", true)
UI_BOOL(VHS_EMU, "   Enable Tape Image Range", true)
UI_INT(TAPE_SELECT, "      Tape Type", 1, 5, 1)
UI_BLANK(5)
UI_BOOL(ENABLE_TAPE_DIST, "   Tape Distortion ", true)
UI_INT(TAPE_DIST, "      Distortion Strength", 30, 100, 50)
UI_INT(TAPE_DIST_FLIP, "      Distortion Type", 1, 2, 1)
#if(ENB_QUALITY == 2)
    UI_BOOL(TAPE_WARP, "      Warping", false)
        UI_MSG(19, "      ^ Effect Needs 4:3 Crop or TV Overlay")
    UI_BOOL(TAPE_JITTER, "      VCR Head Jitter", true)
    UI_QUALITY(JITTER_STRENGTH, "         Jitter Strength", 0, 2, 1)
#endif
UI_BOOL(VHS_bUseLayerNoiseBool, "      VCR Dirty Head Noise", false)
UI_BOOL(VHS_bUseTapeNoiseBool, "      VCR Tracking Noise", false)
UI_FLOAT_FINE(noise_brightness, "         VCR Noise Brightness", 50.0, 100.0, 1.0, 60.0)
UI_BLANK(13)
UI_BOOL(VCR_OSD, "   Enable VCR OSD", false)
UI_INT(VCR_SELECT, "      OSD Mode", 1, 6, 1)
UI_DIVIDER(5)

UI_BOOL(ENABLE_CRT, "ENABLE TV EFFECTS", true)
#if(ENB_QUALITY == 2)
    UI_INT(CRT_MODE, "   CRT Mode (Disable NTSC)", 0, 8, 0)
#else
    UI_INT(CRT_MODE, "   CRT Mode", 0, 8, 0)
#endif
UI_BLANK(42)
#if(ENB_QUALITY == 2)
    UI_BOOL(ENABLE_NTSC, "   NTSC Processing", false)
#endif
UI_BLANK(11)
UI_INT(TV_BRIGHTNESS, "   TV Brightness", 0, 100, 50)
UI_INT(TV_CONTRAST, "   TV Contrast", 0, 100, 50)
UI_INT(TV_COLOR, "   TV Color", 0, 100, 50)
UI_BLANK(15)
UI_BOOL(TV_OVERLAY, "   TV Overlay", false)
#if(ENB_QUALITY == 2)
    UI_BOOL(TV_OVERLAY_DIST, "      Conform Image to Screen", true)
    UI_BOOL(TV_GLOW, "      TV Glow (May Affect FPS)", true)
    UI_BOOL(TV_LETTERBOX, "      Ignore Letterbox / Pillarbox", true)
    UI_INT(TV_BODY, "      TV Body Visibility", 0, 100, 50)
    UI_INT(TV_SCREEN_INTENSITY, "      Screen Texture", 0, 100, 100)
#else
    UI_INT(TV_BODY, "      TV Body Visibility", 0, 100, 50)
    UI_INT(TV_SCREEN_INTENSITY, "      Screen Texture", 0, 100, 100)
#endif
UI_MSG(15, "------------------------------------------------------------------------------------------------")

UI_MSG(9, "IMAGE FORMAT")
UI_BOOL(ENABLE_BORDER, "   Enable Letterbox / Pillarbox (Black Bars)", false)
UI_BOOL(VHS_RATIO, "   4:3 Ratio (Eighties TV) ", true)
UI_FLOAT_FINE(BORDER_RATIO_SET, "   Aspect Ratio (Disable 4:3)", 0.1, 10.0, 0.01, 2.35)
#define BORDER_RATIO lerp(TV_OVERLAY && ENABLE_CRT ? BORDER_RATIO_SET > 1.33 ? BORDER_RATIO_SET * (ScreenSize.z / 1.33) : ScreenSize.z > 2.0 ? BORDER_RATIO_SET * (ScreenSize.z * 0.4389) : BORDER_RATIO_SET : BORDER_RATIO_SET, 1.33, VHS_RATIO)
