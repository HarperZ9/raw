//----------------------------------------------------------------------------------------------//
//																								//
//						      Main ENB Depth of Field UI file									//
//						      by LonelyKitsuune aka Skratzer									//
//																								//
//----------------------------------------------------------------------------------------------//

//This file requires my UI Primer header to work!
#ifndef _UI_PRIMER_
#error UI_Primer couldnt be found!
#endif

//----------------------------------------------------------------------------------------------//
//								  User Interface Parameters										//
//																								//
//----------------------------------------------------------------------------------------------//


#if SHADERGROUP == 0

UI_FileHeaderLong(">>>          Avanced Depth of Field          <<<",
				  ">>>                Kitsuune Edition               <<<")
UI_WHITESPACE(1)

#ifndef _DOF_ADDON_UI_
#define _DOF_ADDON_UI_


//================================================================================
// COMPILE-TIME FEATURE TOGGLES
//================================================================================

// Enable adaptive sharpening of the in-focus region
#define ADDON_ENABLE_FOCUS_SHARPENING       1   //[0-1]

// Enable tilt-shift miniature effect (linear gradient DOF mask)
#define ADDON_ENABLE_TILT_SHIFT             1   //[0-1]

// Enable anamorphic light streaks from bright bokeh highlights
#define ADDON_ENABLE_ANAMORPHIC_STREAKS     1   //[0-1]

// Enable focus peaking overlay for in-focus edge visualization
#define ADDON_ENABLE_FOCUS_PEAKING          1   //[0-1]

// Enable film halation (warm glow bleeding from bright areas)
#define ADDON_ENABLE_HALATION               1   //[0-1]

// Enable longitudinal chromatic aberration fringing on bokeh edges
#define ADDON_ENABLE_BOKEH_FRINGING         1   //[0-1]

// Enable soap bubble / ring bokeh edge brightening
#define ADDON_ENABLE_RING_BOKEH             1   //[0-1]

// Enable starburst diffraction spikes from bright point sources
#define ADDON_ENABLE_STARBURST              1   //[0-1]

// Enable blue noise dithered DOF focus transition zone
#define ADDON_ENABLE_FOCUS_DITHER           1   //[0-1]

// Enable barrel/pincushion distortion, blur, and CA at screen edges
#define ADDON_ENABLE_EDGE_DISTORTION        1   //[0-1]

// Enable focus breathing simulation (FOV shift with focus distance)
#define ADDON_ENABLE_FOCUS_BREATHING        1   //[0-1]


//================================================================================
// UI — IN-FOCUS SHARPENING
//================================================================================

#if ADDON_ENABLE_FOCUS_SHARPENING

UI_WHITESPACE(80)
UI_WHITESPACE(81)
UI_ELEMENT(ADDON_Sharp_Header,     "    >>>>>>IN-FOCUS SHARPENING<<<<<<")
UI_WHITESPACE(82)

bool  UIFS_Enable       <string UIName="|---- Enable Focus Sharpening";                         > = {true};
float UIFS_Amount       <string UIName="|- Sharp - Strength";                float UIMin=0.0; float UIMax= 3.0; > = {0.7};
float UIFS_Radius       <string UIName="|- Sharp - Radius (px)";            float UIMin=0.5; float UIMax= 4.0; > = {1.2};
float UIFS_Threshold    <string UIName="|- Sharp - Detail Threshold";        float UIMin=0.0; float UIMax= 0.5; > = {0.02};
float UIFS_DepthFalloff <string UIName="|- Sharp - Depth Falloff (CoC)";    float UIMin=0.0; float UIMax= 1.0; > = {0.15};
float UIFS_DepthCurve   <string UIName="|- Sharp - Depth Falloff Curve";    float UIMin=0.5; float UIMax= 4.0; > = {2.0};

#endif


//================================================================================
// UI — TILT-SHIFT
//================================================================================

#if ADDON_ENABLE_TILT_SHIFT

UI_WHITESPACE(83)
UI_WHITESPACE(84)
UI_ELEMENT(ADDON_TS_Header,        "        >>>>>>TILT-SHIFT<<<<<<")
UI_WHITESPACE(85)

bool  UITS_Enable       <string UIName="|---- Enable Tilt-Shift";                               > = {false};
float UITS_Position     <string UIName="|- TS - Focus Band Center (Y)";     float UIMin=0.0; float UIMax=1.0;  > = {0.5};
float UITS_Width        <string UIName="|- TS - Focus Band Width";          float UIMin=0.0; float UIMax=0.5;  > = {0.1};
float UITS_GradientSize <string UIName="|- TS - Gradient Softness";         float UIMin=0.01;float UIMax=0.5;  > = {0.15};
float UITS_Angle        <string UIName="|- TS - Angle (\xB0)"; float UIStep=1; float UIMin=-90; float UIMax=90; > = {0.0};
float UITS_BlurMult     <string UIName="|- TS - Blur Multiplier";           float UIMin=0.5; float UIMax=5.0;  > = {2.0};
float UITS_BlurSigma    <string UIName="|- TS - Blur Sigma";               float UIMin=1.0; float UIMax=20.0; > = {6.0};
int   UITS_Quality      <string UIName="|- TS - Blur Quality"; string UIWidget="Quality"; int UIMin=0; int UIMax=2; > = {1};

#endif


//================================================================================
// UI — ANAMORPHIC STREAKS
//================================================================================

#if ADDON_ENABLE_ANAMORPHIC_STREAKS

UI_WHITESPACE(86)
UI_WHITESPACE(87)
UI_ELEMENT(ADDON_Streak_Header,    "    >>>>>>ANAMORPHIC STREAKS<<<<<<")
UI_WHITESPACE(88)

bool  UIAS_Enable       <string UIName="|---- Enable Anamorphic Streaks";                       > = {true};
float UIAS_Threshold    <string UIName="|- Streak - Brightness Threshold";  float UIMin=0.0; float UIMax=5.0;  > = {0.8};
float UIAS_Intensity    <string UIName="|- Streak - Intensity";             float UIMin=0.0; float UIMax=3.0;  > = {0.6};
float UIAS_Length       <string UIName="|- Streak - Spread (Sigma)";        float UIMin=1.0; float UIMax=20.0; > = {8.0};
float UIAS_Falloff      <string UIName="|- Streak - Falloff Curve";        float UIMin=0.5; float UIMax=4.0;  > = {1.5};
float UIAS_CoCMin       <string UIName="|- Streak - Min CoC for Streaks";  float UIMin=0.0; float UIMax=1.0;  > = {0.1};
float UIAS_ChromaShift  <string UIName="|- Streak - Chromatic Shift";      float UIMin=0.0; float UIMax=1.0;  > = {0.15};
float3 UIAS_Tint        <string UIName="|- Streak - Color Tint";           string UIWidget="Color";            > = {1.0, 0.95, 0.85};

#endif


//================================================================================
// UI — FOCUS PEAKING
//================================================================================

#if ADDON_ENABLE_FOCUS_PEAKING

UI_WHITESPACE(89)
UI_WHITESPACE(90)
UI_ELEMENT(ADDON_FP_Header,        "       >>>>>>FOCUS PEAKING<<<<<<")
UI_WHITESPACE(91)

bool   UIFP_Enable      <string UIName="|---- Enable Focus Peaking";                            > = {false};
float  UIFP_Threshold   <string UIName="|- Peak - Edge Threshold";         float UIMin=0.0; float UIMax=1.0;  > = {0.08};
float  UIFP_CoCLimit    <string UIName="|- Peak - Max CoC for Peaking";    float UIMin=0.0; float UIMax=0.5;  > = {0.05};
float  UIFP_Intensity   <string UIName="|- Peak - Overlay Intensity";      float UIMin=0.0; float UIMax=2.0;  > = {1.0};
float3 UIFP_Color       <string UIName="|- Peak - Overlay Color";          string UIWidget="Color";            > = {1.0, 0.2, 0.2};
int    UIFP_Mode        <string UIName="|- Peak - Mode (1-Color, 2-White)"; int UIMin=1; int UIMax=2;          > = {1};

#endif


//================================================================================
// UI — FILM HALATION
//================================================================================

#if ADDON_ENABLE_HALATION

UI_WHITESPACE(92)
UI_WHITESPACE(93)
UI_ELEMENT(ADDON_Hal_Header,       "       >>>>>>FILM HALATION<<<<<<")
UI_WHITESPACE(94)

bool  UIH_Enable        <string UIName="|---- Enable Film Halation";                            > = {true};
float UIH_Threshold     <string UIName="|- Halation - Brightness Threshold"; float UIMin=0.0; float UIMax=3.0; > = {0.5};
float UIH_Intensity     <string UIName="|- Halation - Intensity";           float UIMin=0.0; float UIMax=3.0;  > = {0.5};
float UIH_Radius        <string UIName="|- Halation - Glow Radius";        float UIMin=1.0; float UIMax=30.0; > = {10.0};
float3 UIH_Color        <string UIName="|- Halation - Glow Tint";          string UIWidget="Color";            > = {1.0, 0.55, 0.35};
float UIH_CoCWeight     <string UIName="|- Halation - CoC Enhancement";    float UIMin=0.0; float UIMax=2.0;  > = {0.5};
int   UIH_Quality       <string UIName="|- Halation - Blur Quality"; string UIWidget="Quality"; int UIMin=0; int UIMax=2; > = {1};

#endif


//================================================================================
// UI — BOKEH FRINGING (Longitudinal CA)
//================================================================================

#if ADDON_ENABLE_BOKEH_FRINGING

UI_WHITESPACE(95)
UI_WHITESPACE(96)
UI_ELEMENT(ADDON_BF_Header,        "     >>>>>>BOKEH FRINGING<<<<<<")
UI_WHITESPACE(97)

bool  UIBF_Enable       <string UIName="|---- Enable Bokeh Fringing";                           > = {false};
float UIBF_Amount       <string UIName="|- Fringe - Amount";               float UIMin=0.0; float UIMax=2.0;  > = {0.5};
float UIBF_Spread       <string UIName="|- Fringe - Spread (px)";         float UIMin=0.5; float UIMax=6.0;  > = {2.0};
float UIBF_CoCMin       <string UIName="|- Fringe - Min CoC Activation";  float UIMin=0.0; float UIMax=0.5;  > = {0.05};
float3 UIBF_ForeColor   <string UIName="|- Fringe - Foreground Tint";     string UIWidget="Color";            > = {0.55, 0.3, 0.9};
float3 UIBF_BackColor   <string UIName="|- Fringe - Background Tint";     string UIWidget="Color";            > = {0.3, 0.9, 0.4};

#endif


//================================================================================
// UI — SOAP BUBBLE / RING BOKEH
//================================================================================

#if ADDON_ENABLE_RING_BOKEH

UI_WHITESPACE(100)
UI_WHITESPACE(101)
UI_ELEMENT(ADDON_RB_Header,        "    >>>>>>RING BOKEH (SOAP BUBBLE)<<<<<<")
UI_WHITESPACE(102)

bool   UIRB_Enable      <string UIName="|---- Enable Ring Bokeh";                               > = {false};
float  UIRB_Intensity   <string UIName="|- Ring - Edge Intensity";          float UIMin=0.0; float UIMax=3.0;  > = {0.8};
float  UIRB_Width       <string UIName="|- Ring - Edge Width";             float UIMin=0.5; float UIMax=5.0;  > = {1.5};
float  UIRB_CoCMin      <string UIName="|- Ring - Min CoC Activation";    float UIMin=0.0; float UIMax=0.5;  > = {0.08};
float  UIRB_LumBoost    <string UIName="|- Ring - Luminance Bias";        float UIMin=0.0; float UIMax=2.0;  > = {0.3};
float3 UIRB_Tint        <string UIName="|- Ring - Edge Color Tint";       string UIWidget="Color";            > = {0.95, 1.0, 0.9};

#endif


//================================================================================
// UI — STARBURST DIFFRACTION SPIKES
//================================================================================

#if ADDON_ENABLE_STARBURST

UI_WHITESPACE(103)
UI_WHITESPACE(104)
UI_ELEMENT(ADDON_SB_Header,        "   >>>>>>STARBURST DIFFRACTION<<<<<<")
UI_WHITESPACE(105)

bool   UISB_Enable      <string UIName="|---- Enable Starburst Spikes";                         > = {false};
float  UISB_Threshold   <string UIName="|- Starburst - Brightness Threshold"; float UIMin=0.0; float UIMax=5.0;  > = {0.9};
float  UISB_Intensity   <string UIName="|- Starburst - Intensity";         float UIMin=0.0; float UIMax=3.0;  > = {0.5};
float  UISB_Length       <string UIName="|- Starburst - Spike Length";     float UIMin=1.0; float UIMax=20.0; > = {6.0};
float  UISB_Rotation    <string UIName="|- Starburst - Rotation (\xB0)";  float UIStep=1; float UIMin=0; float UIMax=180; > = {15.0};
int    UISB_SpikeCount  <string UIName="|- Starburst - Spike Count (2-8)"; int UIMin=2; int UIMax=8;          > = {6};
float  UISB_Falloff     <string UIName="|- Starburst - Falloff Curve";    float UIMin=0.5; float UIMax=4.0;  > = {2.0};

#endif


//================================================================================
// UI — BLUE NOISE FOCUS DITHER
//================================================================================

#if ADDON_ENABLE_FOCUS_DITHER

UI_WHITESPACE(106)
UI_WHITESPACE(107)
UI_ELEMENT(ADDON_FD_Header,        "   >>>>>>FOCUS TRANSITION DITHER<<<<<<")
UI_WHITESPACE(108)

bool   UIFD_Enable      <string UIName="|---- Enable Focus Dither";                             > = {true};
float  UIFD_Amount      <string UIName="|- Dither - Noise Amount";         float UIMin=0.0; float UIMax=1.0;  > = {0.4};
float  UIFD_Width       <string UIName="|- Dither - Transition Width (CoC)"; float UIMin=0.01;float UIMax=0.5; > = {0.12};

#endif


//================================================================================
// UI — SCREEN EDGE DISTORTION
//================================================================================

#if ADDON_ENABLE_EDGE_DISTORTION

UI_WHITESPACE(109)
UI_WHITESPACE(110)
UI_ELEMENT(ADDON_ED_Header,        "  >>>>>>SCREEN EDGE DISTORTION<<<<<<")
UI_WHITESPACE(111)

bool   UIED_Enable      <string UIName="|---- Enable Edge Distortion";                          > = {false};
float  UIED_Barrel      <string UIName="|- Edge - Barrel Distortion";      float UIMin=-1.0; float UIMax=1.0; > = {0.15};
float  UIED_BarrelCurve <string UIName="|- Edge - Distortion Curve";      float UIMin=1.0; float UIMax=4.0;  > = {2.0};
float  UIED_BlurAmount  <string UIName="|- Edge - Blur Amount";           float UIMin=0.0; float UIMax=3.0;  > = {0.5};
float  UIED_CAAmount    <string UIName="|- Edge - Chromatic Aberration";  float UIMin=0.0; float UIMax=2.0;  > = {0.3};
float  UIED_CACurve     <string UIName="|- Edge - CA Curve";             float UIMin=1.0; float UIMax=4.0;  > = {2.0};

#endif


//================================================================================
// UI — FOCUS BREATHING
//================================================================================

#if ADDON_ENABLE_FOCUS_BREATHING

UI_WHITESPACE(112)
UI_WHITESPACE(113)
UI_ELEMENT(ADDON_FB_Header,        "    >>>>>>FOCUS BREATHING<<<<<<")
UI_WHITESPACE(114)

bool   UIFB_Enable      <string UIName="|---- Enable Focus Breathing";                          > = {false};
float  UIFB_Amount      <string UIName="|- Breath - Amount (% FOV shift)"; float UIMin=0.0; float UIMax=5.0;  > = {1.5};
float  UIFB_Direction   <string UIName="|- Breath - Dir (-1 tele, +1 wide)"; float UIMin=-1.0; float UIMax=1.0; > = {1.0};

#endif


#endif //_DOF_ADDON_UI_


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 1

UI_ELEMENT(_Focus_Header,"               >>>>>>FOCUS<<<<<<")
UI_WHITESPACE(2)
int   UI_FocusType				<string UIName="|- Focus Type (1-Auto, 2-Mouse, 3-Manual)";				 int   UIMin=1;    int   UIMax=3;   > = {1};
#if ENABLE_FOCUSING_TOOL
bool  UI_VisualizeFocus			<string UIName="|- Visualize Focus";																	    > = {false};
#endif
UI_SPECIAL_WHITESPACE(1)
UI_ELEMENT(_DOF_FocusInfo1,				       "|- 1 - Autofocus")
float2 UI_AutofocusCenter		<string UIName="|- Autofocus Sample Center";							 float UIMin=0.00; float UIMax=1.0; > = {0.5,0.5};
float  UI_AutofocusRadius		<string UIName="|- Autofocus Sample Radius";							 float UIMin=0.01; float UIMax=1.0; > = {0.05};
UI_SPECIAL_WHITESPACE(2)
UI_ELEMENT(_DOF_FocusInfo2,				       "|- 2 - Mousefocus (right click to focus)")
float  UI_MousefocusRadius		<string UIName="|- Mousefocus Sample Radius";							 float UIMin=0.01; float UIMax=1.0; > = {0.05};
UI_SPECIAL_WHITESPACE(3)
UI_ELEMENT(_DOF_FocusInfo3,				       "|- 3 - Manual Focus")
float  UI_ManualfocusDepth		<string UIName="|- Manual Focus Depth";			    float UIStep=0.0001; float UIMin=0.0;  float UIMax=1.0; > = {0.05};
UI_WHITESPACE(3)
UI_WHITESPACE(4)
UI_ELEMENT(_DOF_Header,"        >>>>>>DEPTH OF FIELD<<<<<<")
UI_WHITESPACE(5)
UI_ELEMENT(_DOF_DoFInfo,				       "|------- DOF")
float  UI_NearBlurCurve			<string UIName="|- DOF - Near Blur Curve";								 float UIMin=0.01;  float UIMax=20.0;  > = {1.0};
float  UI_NearBlurBleed			<string UIName="|- DOF - Near Blur Bleeding";							 float UIMin=0.0;   float UIMax= 2.0;  > = {1.0};
float  UI_FarBlurCurve			<string UIName="|- DOF - Far Blur Curve";								 float UIMin=0.01;  float UIMax=20.0;  > = {1.4};
#if ENABLE_FAR_BOKEH_FREEZING
bool   UI_FreezeFarBokeh		<string UIName="|- DOF - Freeze Far Bokeh";																       > = {false};
#endif
float  UI_HyperFocus			<string UIName="|- DOF - Hyperfocal Depth Distance"; float UIStep=0.001; float UIMin=0.0;   float UIMax=1.0;   > = {0.015};
bool   UI_RemoveFPSObjects      <string UIName="|- DOF - Remove FPS Objects";															       > = {true};
float  UI_RenderResMult			<string UIName="|- DOF - Blur Render Res mult";							 float UIMin=0.5;   float UIMax=1.0;   > = {0.5};
float  UI_SmootheningAmount		<string UIName="|- DOF - Gaussian PostBlur Width";						 float UIMin=0.0;   float UIMax=20.0;  > = {4.0};
int    UI_GaussQuality			<string UIName="|- DOF - Gaussian Quality";   string UIWidget="Quality"; int   UIMin=0;     int   UIMax=2;     > = {  1};
#if ENABLE_COC_DEBUGGING
int    UI_CoCDebug				<string UIName="|- DOF - CoC Debug";									 int   UIMin=0;     int   UIMax=4;     > = {  0};
#endif
UI_SPECIAL_WHITESPACE(4)
UI_ELEMENT(_DOF_BokehInfo,				       "|------- BOKEH")
float  UI_BokehIntensity		<string UIName="|- Bokeh - Intensity";									 float UIMin=0.0;   float UIMax=1.0;   > = {0.5};
float  UI_BokehCurve			<string UIName="|- Bokeh - Curve";										 float UIMin=0.0;   float UIMax=20.0;  > = {1.0};
float  UI_ShapeRadius			<string UIName="|- Bokeh - Shape Max Size";		     float UIStep=0.1;	 float UIMin=0.0;   float UIMax=100.0; > = {15.0};
int    UI_ShapeVertices			<string UIName="|- Bokeh - Shape Vertices";								 int   UIMin=3;     int   UIMax=9;     > = {6};
int    UI_ShapeQuality			<string UIName="|- Bokeh - Shape Quality";								 int   UIMin=2;     int   UIMax=50;    > = {5};
float  UI_ShapeCurvatureAmount	<string UIName="|- Bokeh - Shape Roundness";							 float UIMin=-1.0;  float UIMax=1.0;   > = {1.0};
float  UI_ShapeRotation			<string UIName="|- Bokeh - Shape Rotation (\xB0)";   float UIStep=1;	 float UIMin=0;     float UIMax=360;   > = {15};
float  UI_ShapeAnamorphRatio	<string UIName="|- Bokeh - Shape Aspect Ratio";							 float UIMin=0.0;   float UIMax=2.0;   > = {1.0};
bool   UI_ShapeCut				<string UIName="|- Bokeh - Cut Shape in Half";															       > = {false};

#if ENABLE_STYLIZED_BOKEHSHAPES
UI_SPECIAL_WHITESPACE(5)
UI_ELEMENT(_DOF_StyleInfo,				       "|------- STYLIZED BOKEH SHAPES")
bool   UI_EnableStyle			<string UIName="|- Style - Override Bokeh Shape";														    > = {false};
float  UI_StyleTex				<string UIName="|- Style - Choose Shape Texture";   float UIStep=1;		float UIMin=1.0; float UIMax=9.0;   > = {1.0};
float  UI_StyleIntensity		<string UIName="|- Style - Intensity";									float UIMin=0.0; float UIMax=1.0;   > = {1.0};
float  UI_StyleRadius			<string UIName="|- Style - Shape Max Size";		    float UIStep=0.1;	float UIMin=0.0; float UIMax=100.0; > = {15.0};
float  UI_StyleQualityLevel		<string UIName="|- Style - Shape Quality Level";    float UIStep=1;		float UIMin=1.0; float UIMax=4.0;   > = {2.0};
float  UI_StyleRotation			<string UIName="|- Style - Shape Rotation (\xB0)";  float UIStep=1;		float UIMin=0.0; float UIMax=359.0; > = {0.0};
float  UI_StyleSmoothening		<string UIName="|- Style - Gaussian Blur Override";						float UIMin=0.0; float UIMax=20.0;  > = {2.0};
#endif //ENABLE_STYLIZED_BOKEHSHAPES

#if ENABLE_SPHERICAL_ABERRATION
UI_SPECIAL_WHITESPACE(6)
UI_ELEMENT(_DOF_SAInfo,						       "|------- SPHERICAL ABERRATION")
float  UI_SphericalAmount		<string UIName="|- Bokeh - Shape SA Amount"; float UIMin=-1.0; float UIMax=1.0; > = {0.1};
float  UI_SphericalCurve		<string UIName="|- Bokeh - Shape SA Curve";  float UIMin= 0.0; float UIMax=3.0; > = {0.25};
#endif //ENABLE_SPHERICAL_ABERRATION

#if ENABLE_DIFFRACTION
UI_SPECIAL_WHITESPACE(7)
UI_ELEMENT(_DOF_DiInfo,						       "|------- DIFFRACTION")
float  UI_DiffractionAmount		<string UIName="|- Bokeh - Diffraction Amount";    float UIMin=0.0;  float UIMax= 1.0; > = {0.25};
float  UI_DiffractionFreq		<string UIName="|- Bokeh - Diffraction Frequency"; float UIMin=0.01; float UIMax=20.0; > = {8.0};
#endif //ENABLE_DIFFRACTION

#if ENABLE_OPTICAL_VIGNETTE
UI_SPECIAL_WHITESPACE(9)
UI_ELEMENT(_DOF_OVInfo,						       "|------- OPTICAL VIGNETTE")
float  UI_ShapeVignetteCurve		<string UIName="|- Bokeh - Shape Vignette Curve";  float UIMin=0.5; float UIMax=2.5; > = {0.75};
float  UI_ShapeVignetteAmount		<string UIName="|- Bokeh - Shape Vignette Amount"; float UIMin=0.0; float UIMax=2.0; > = {1.0};
#endif //ENABLE_OPTICAL_VIGNETTE

#if ENABLE_CHROMATIC_ABERRATION
UI_SPECIAL_WHITESPACE(10)
UI_ELEMENT(_DOF_CAInfo,						       "|------- CHROMATIC ABERRATION")
float  UI_LongitudChromaAmount		<string UIName="|- Bokeh - Longitudinal CA Amount"; float UIMin=-1.0; float UIMax=1.0; > = {0.3};
float  UI_LateralChromaAmount		<string UIName="|- Bokeh - Lateral CA Amount";      float UIMin=-1.0; float UIMax=1.0; > = {0.2};
float  UI_LateralChromaCurve		<string UIName="|- Bokeh - Lateral CA Curve";       float UIMin= 0.0; float UIMax=8.0; > = {1.3};
int    UI_ChromaMode				<string UIName="|- Bokeh - CA Type";                int   UIMin=   0; int   UIMax=  2; > = {2};
#endif

#if ENABLE_GRAINING == 1
UI_SPECIAL_WHITESPACE(8)
UI_ELEMENT(_DOF_GInfo,						       "|------- GRAINING")
float UI_GrainAmount			<string UIName="|- Bokeh - Grain Amount"; float UIMin=0.0; float UIMax= 1.0; > = {0.1};
float UI_GrainSeed				<string UIName="|- Bokeh - Grain Seed";   float UIMin=0.0; float UIMax=10.0; > = {0.23};
#elif ENABLE_GRAINING == 2
UI_SPECIAL_WHITESPACE(8)
UI_ELEMENT(_DOF_GInfo,						       "|------- GRAINING")
float UI_GrainInt				<string UIName="|- DOF - Grain Intensity";  float UIMin= 0.0; float UIMax=0.5; > = {0.05};
float UI_GrainSaturation		<string UIName="|- DOF - Grain Saturation"; float UIMin= 0.0; float UIMax=1.0; > = {0.5};
float UI_GrainMotion			<string UIName="|- DOF - Grain Motion";     float UIMin=-2.0; float UIMax=2.0; > = {0.1};
#endif //ENABLE_GRAINING


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 2

#ifndef NOTFIRSTTIME
#define NOTFIRSTTIME
UI_ELEMENT(_DOFCC_Header,"   >>>>>>DOF COLOR GRADING<<<<<<")
UI_WHITESPACE(9)
bool  UICG_Enable		<string UIName="|---- Enable DOF Color Grading";										> = {true};
int   UICG_Mask			<string UIName="|- DOFCG - CC Mask (1-Bokeh, 2-CoC)"; int   UIMin=1;   int   UIMax=2;   > = {1};
float UICG_ConGrayLvl	<string UIName="|- DOFCG - Contrast Gray Level";      float UIMin=0.0; float UIMax=1.0; > = {0.5};
UI_SPECIAL_WHITESPACE(11)
#endif //NOTFIRSTTIME

float SEPARATE_VAR(UICG_Saturation) <string UIName=TO_STRING(|- DOFCG - TODIE - Saturation); float UIMin=0.0; float UIMax=10.0; > = {1.0};
float SEPARATE_VAR(UICG_Brightness) <string UIName=TO_STRING(|- DOFCG - TODIE - Brightness); float UIMin=0.0; float UIMax=10.0; > = {1.0};
float SEPARATE_VAR(UICG_Contrast)   <string UIName=TO_STRING(|- DOFCG - TODIE - Contrast);   float UIMin=0.0; float UIMax=10.0; > = {1.0};


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 3

UI_ELEMENT(_DOFK_Header,"           >>>>>>DOF KEYING<<<<<<")
UI_WHITESPACE(10)
bool   UIK_Enable <string UIName="|---- Enable DOF Keying";										  > = {false};
int    UIK_Mask   <string UIName="|- DOFK - Key Mask (1-Bokeh, 2-DOF)"; int UIMin=1; int UIMax=2; > = {2};
float3 UIK_Color  <string UIName="|- DOFK - Keycolor";						string UIWidget="Color";  > = {0,1,0};



//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 99

//----------------------------------------------------------------------------------------------//
//                     SKYRIMBRIDGE DOF ENHANCEMENTS                                            //
//----------------------------------------------------------------------------------------------//
// Requires SkyrimBridge SKSE plugin + SkyrimBridge.fxh in Helper/                              //
// When SB is not installed, all controls are hidden. Helpers use fallback values.               //
//----------------------------------------------------------------------------------------------//

#ifdef SKYRIMBRIDGE_FXH

UI_ELEMENT(SB_DOF_Header,      "    >>>>>>SKYRIMBRIDGE DOF<<<<<<")

//--- Core ---
float UISB_AccurateFarPlane   <string UIName="|- SB - Accurate Far Plane";         float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UISB_MenuBypass         <string UIName="|- SB - Skip DOF In Menus";           float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};

//--- Crosshair Autofocus ---
int SB_DOF_Spacer0   <string UIName="|--------- Crosshair Focus"; int UIMin=0; int UIMax=0;> = {0};
float UISB_CrosshairFocus     <string UIName="|- SB - Crosshair Autofocus";         float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UISB_CrosshairPriority  <string UIName="|- SB - Crosshair Blend Strength";    float UIMin= 0.0; float UIMax= 1.0; > = { 0.8};

//--- Dialogue ---
int SB_DOF_Spacer1   <string UIName="|--------- Dialogue DOF"; int UIMin=0; int UIMax=0;> = {0};
float UISB_DialogueDOF        <string UIName="|- SB - Dialogue Shallow DOF";        float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UISB_DialogueStrength   <string UIName="|- SB - Dialogue Blur Strength";      float UIMin= 0.0; float UIMax= 2.0; > = { 1.2};

//--- Combat ---
int SB_DOF_Spacer2   <string UIName="|--------- Combat & Killcam"; int UIMin=0; int UIMax=0;> = {0};
float UISB_CombatClarity      <string UIName="|- SB - Combat DOF Reduction";        float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UISB_CombatReduce       <string UIName="|- SB - Combat Blur Multiplier";      float UIMin= 0.0; float UIMax= 1.0; > = { 0.3};
float UISB_KillcamDOF         <string UIName="|- SB - Killcam Cinematic DOF";       float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UISB_KillcamStrength    <string UIName="|- SB - Killcam Blur Strength";       float UIMin= 0.5; float UIMax= 3.0; > = { 1.5};

//--- Physical Lens ---
int SB_DOF_Spacer3   <string UIName="|--------- Physical Lens"; int UIMin=0; int UIMax=0;> = {0};
float UISB_PhysicalCoC        <string UIName="|- SB - Physical Circle of Confusion"; float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UISB_SensorSize         <string UIName="|- SB - Sensor Size (mm)";            float UIMin= 18.0; float UIMax= 65.0; > = { 36.0};
float UISB_FStop              <string UIName="|- SB - F-Stop";                       float UIMin= 0.5; float UIMax= 16.0; > = { 2.8};
float UISB_SmartAutofocus     <string UIName="|- SB - Smart Autofocus";              float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};

//--- State-Specific ---
int SB_DOF_Spacer4   <string UIName="|--------- State-Specific"; int UIMin=0; int UIMax=0;> = {0};
float UISB_BleedoutDOF        <string UIName="|- SB - Bleedout DOF";                float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UISB_BleedoutStrength   <string UIName="|- SB - Bleedout Blur Strength";      float UIMin= 0.5; float UIMax= 3.0; > = { 1.8};
float UISB_MountedDOF         <string UIName="|- SB - Mounted DOF Range";           float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UISB_MountedRangeMult   <string UIName="|- SB - Mounted Range Multiplier";    float UIMin= 0.5; float UIMax= 3.0; > = { 1.5};
float UISB_SlowTimeDOF        <string UIName="|- SB - Slow-Time DOF";               float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UISB_SlowTimeMult       <string UIName="|- SB - Slow-Time Blur Mult";         float UIMin= 0.5; float UIMax= 3.0; > = { 1.8};
float UISB_NightEyeClarity    <string UIName="|- SB - Night Eye Clarity";           float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};
float UISB_NightEyeReduceAmt  <string UIName="|- SB - Night Eye Reduce Amount";     float UIMin= 0.0; float UIMax= 1.0; > = { 0.5};
float UISB_UnderwaterSkip     <string UIName="|- SB - Skip DOF Underwater";         float UIMin= 0.0; float UIMax= 1.0; > = { 1.0};

#endif //SKYRIMBRIDGE_FXH

#endif //SHADERGROUP

#undef TODIE




