//----------------------------------------------------------------------------------------------//
//																								//
//								 Main ENB PostPass UI file										//
//						       by LonelyKitsuune aka Skratzer									//
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

UI_FileHeaderLong(">>>         COLOR & POST FX SUITE         <<<",
				  ">>>              by LonelyKitsuune              <<<")

#if ENABLE_TOOLS

UI_WHITESPACE(1)
UI_ELEMENT(_Tools_Header,"               >>>>>>TOOLS<<<<<<")
UI_WHITESPACE(2)

#if AA_SUPPORT_PRESET > 2
bool UI_CREAAShowEdgeTex <string UIGroup = "Developer Tools"; string UIName="Show CREAA EdgeTex";              > = {false};
#endif
#if !DIRECT_COLOR_GRADING
bool UI_ShowInternalLUT  <string UIGroup = "CG.KiSuite"; string UIName="|- Show Internal LUT"; > = {false};
#endif
UI_WHITESPACE(3)

#endif //ENABLE_TOOLS

#undef SHADERGROUP


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 1

#ifndef NOTFIRSTTIME
#define NOTFIRSTTIME
UI_WHITESPACE(4)
UI_ELEMENT(_KCGS_Header,"        >>>>>>COLOR GRADING<<<<<<")
UI_WHITESPACE(5)
float UICG_LumaZonesOverlap		 <string UIGroup = "CG.KiSuite"; string UIName="|- Luma Zones Overlap";			float UIMin=0.0; float UIMax=1.0; > = {1.0};
float Day_UICG_ExtLUTWeight		 <string UIGroup = "CG.KiSuite"; string UIName="|- External LUT Weight Day";		float UIMin=0.0; float UIMax=1.0; > = {1.0};
float Night_UICG_ExtLUTWeight	 <string UIGroup = "CG.KiSuite"; string UIName="|- External LUT Weight Night";	float UIMin=0.0; float UIMax=1.0; > = {1.0};
float Interior_UICG_ExtLUTWeight <string UIGroup = "CG.KiSuite"; string UIName="|- External LUT Weight Interior";	float UIMin=0.0; float UIMax=1.0; > = {1.0};
#if !DIRECT_COLOR_GRADING
float UICG_LUTDither			 <string UIGroup = "CG.KiSuite"; string UIName="|- LUT Dithering Amount";			float UIMin=0.0; float UIMax=1.0; > = {0.0};
#endif
UI_SPECIAL_WHITESPACE(1)
#endif //NOTFIRSTTIME

UI_ELEMENT(_KCGS_TODIE_Header,					               TO_STRING(|------- TODIE -------------------------------------))
float  SEPARATE_VAR(UICG_Brightness)			<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Brightness);							float UIMin= -1.0; float UIMax=  3.0; > = {1.0};
float  SEPARATE_VAR(UICG_Contrast)				<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Contrast);								float UIMin= -1.0; float UIMax=  3.0; > = {1.0};
float  SEPARATE_VAR(UICG_GammaCurve)			<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Gamma Curve);							float UIMin= -1.0; float UIMax=  3.0; > = {1.0};
float  SEPARATE_VAR(UICG_Saturation)			<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Saturation);							float UIMin= -1.0; float UIMax=  3.0; > = {1.0};
float  SEPARATE_VAR(UICG_Vibrance)				<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Vibrance);								float UIMin= -1.0; float UIMax=  3.0; > = {1.0};
float3 SEPARATE_VAR(UICG_Tint)					<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Tint);					string UIWidget="Color";	      > = {1.0, 1.0, 1.0};
float  SEPARATE_VAR(UICG_BlackLevelIn)			<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Black Level In);     float UIStep=1.0; float UIMin=  0.0; float UIMax=511.0; > = {0.0};
float  SEPARATE_VAR(UICG_WhiteLevelIn)			<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - White Level In);     float UIStep=1.0; float UIMin=  0.0; float UIMax=511.0; > = {255.0};
float  SEPARATE_VAR(UICG_BlackLevelOut)			<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Black Level Out);    float UIStep=1.0; float UIMin=  0.0; float UIMax=511.0; > = {0.0};
float  SEPARATE_VAR(UICG_WhiteLevelOut)			<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - White Level Out);    float UIStep=1.0; float UIMin=  0.0; float UIMax=511.0; > = {255.0};
float3 SEPARATE_VAR(UICG_ChannelMixRed)			<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Channel Mix Red);					string UIWidget="Color";		      > = {1.0, 0.0, 0.0};
float3 SEPARATE_VAR(UICG_ChannelMixGreen)		<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Channel Mix Green);					string UIWidget="Color";		      > = {0.0, 1.0, 0.0};
float3 SEPARATE_VAR(UICG_ChannelMixBlue)		<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Channel Mix Blue);					string UIWidget="Color";		      > = {0.0, 0.0, 1.0};
int SEPARATE_VAR(_SWS1) < string UIName = SPECIAL_WHITESPACE_60; int UIMin = 0; int UIMax = 0; > = {0};
float  SEPARATE_VAR(UICG_HighlSat)				<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Highlight Saturation);					float UIMin= -1.0; float UIMax=  3.0; > = {1.0};
float  SEPARATE_VAR(UICG_HighlBright)			<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Highlight Brightness);					float UIMin= -1.0; float UIMax=  3.0; > = {1.0};
float3 SEPARATE_VAR(UICG_HighlTint)				<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Highlight Tint);						string UIWidget="Color";		      > = {1.0, 1.0, 1.0};
float  SEPARATE_VAR(UICG_MidtoSat)				<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Midtone Saturation);					float UIMin= -1.0; float UIMax=  3.0; > = {1.0};
float  SEPARATE_VAR(UICG_MidtoBright)			<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Midtone Brightness);					float UIMin= -1.0; float UIMax=  3.0; > = {1.0};
float3 SEPARATE_VAR(UICG_MidtoTint)				<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Midtone Tint);							string UIWidget="Color";		      > = {1.0, 1.0, 1.0};
float  SEPARATE_VAR(UICG_ShadoSat)				<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Shadow Saturation);					float UIMin= -1.0; float UIMax=  3.0; > = {1.0};
float  SEPARATE_VAR(UICG_ShadoBright)			<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Shadow Brightness);					float UIMin= -1.0; float UIMax=  3.0; > = {1.0};
float3 SEPARATE_VAR(UICG_ShadoTint)				<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Shadow Tint);							string UIWidget="Color";		      > = {1.0, 1.0, 1.0};
int SEPARATE_VAR(_SWS2) < string UIName = SPECIAL_WHITESPACE_61; int UIMin = 0; int UIMax = 0; > = {0};
float  SEPARATE_VAR(UICG_HueShiftRed)			<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Hue Shift Red);						float UIMin=-10.0; float UIMax= 10.0; > = {0.0};
float  SEPARATE_VAR(UICG_HueShiftOrange)		<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Hue Shift Orange);						float UIMin=-10.0; float UIMax= 10.0; > = {0.0};
float  SEPARATE_VAR(UICG_HueShiftYellow)		<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Hue Shift Yellow);						float UIMin=-10.0; float UIMax= 10.0; > = {0.0};
float  SEPARATE_VAR(UICG_HueShiftGreen)			<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Hue Shift Green);						float UIMin=-10.0; float UIMax= 10.0; > = {0.0};
float  SEPARATE_VAR(UICG_HueShiftCyan)			<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Hue Shift Cyan);						float UIMin=-10.0; float UIMax= 10.0; > = {0.0};
float  SEPARATE_VAR(UICG_HueShiftBlue)			<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Hue Shift Blue);						float UIMin=-10.0; float UIMax= 10.0; > = {0.0};
float  SEPARATE_VAR(UICG_HueShiftMagenta)		<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Hue Shift Magenta);					float UIMin=-10.0; float UIMax= 10.0; > = {0.0};
int SEPARATE_VAR(_SWS3) < string UIName = SPECIAL_WHITESPACE_62; int UIMin = 0; int UIMax = 0; > = {0};
float  SEPARATE_VAR(UICG_HueSatRed)				<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Saturation Red);						float UIMin= -1.0; float UIMax=  2.0; > = {1.0};
float  SEPARATE_VAR(UICG_HueSatOrange)			<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Saturation Orange);					float UIMin= -1.0; float UIMax=  2.0; > = {1.0};
float  SEPARATE_VAR(UICG_HueSatYellow)			<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Saturation Yellow);					float UIMin= -1.0; float UIMax=  2.0; > = {1.0};
float  SEPARATE_VAR(UICG_HueSatGreen)			<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Saturation Green);						float UIMin= -1.0; float UIMax=  2.0; > = {1.0};
float  SEPARATE_VAR(UICG_HueSatCyan)			<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Saturation Cyan);						float UIMin= -1.0; float UIMax=  2.0; > = {1.0};
float  SEPARATE_VAR(UICG_HueSatBlue)			<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Saturation Blue);						float UIMin= -1.0; float UIMax=  2.0; > = {1.0};
float  SEPARATE_VAR(UICG_HueSatMagenta)			<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Saturation Magenta);					float UIMin= -1.0; float UIMax=  2.0; > = {1.0};
int SEPARATE_VAR(_SWS4) < string UIName = SPECIAL_WHITESPACE_63; int UIMin = 0; int UIMax = 0; > = {0};
float  SEPARATE_VAR(UICG_HueBrightRed)			<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Brightness Red);						float UIMin= -1.0; float UIMax=  1.0; > = {1.0};
float  SEPARATE_VAR(UICG_HueBrightOrange)		<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Brightness Orange);					float UIMin= -1.0; float UIMax=  1.0; > = {1.0};
float  SEPARATE_VAR(UICG_HueBrightYellow)		<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Brightness Yellow);					float UIMin= -1.0; float UIMax=  1.0; > = {1.0};
float  SEPARATE_VAR(UICG_HueBrightGreen)		<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Brightness Green);						float UIMin= -1.0; float UIMax=  1.0; > = {1.0};
float  SEPARATE_VAR(UICG_HueBrightCyan)			<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Brightness Cyan);						float UIMin= -1.0; float UIMax=  1.0; > = {1.0};
float  SEPARATE_VAR(UICG_HueBrightBlue)			<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Brightness Blue);						float UIMin= -1.0; float UIMax=  1.0; > = {1.0};
float  SEPARATE_VAR(UICG_HueBrightMagenta)		<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Brightness Magenta);					float UIMin= -1.0; float UIMax=  1.0; > = {1.0};
int SEPARATE_VAR(_SWS5) < string UIName = SPECIAL_WHITESPACE_64; int UIMin = 0; int UIMax = 0; > = {0};
float  SEPARATE_VAR(UICG_HueConRed)			<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Contrast Red);						float UIMin= -1.0; float UIMax=  3.0; > = {1.0};
float  SEPARATE_VAR(UICG_HueConOrange)			<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Contrast Orange);					float UIMin= -1.0; float UIMax=  3.0; > = {1.0};
float  SEPARATE_VAR(UICG_HueConYellow)			<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Contrast Yellow);					float UIMin= -1.0; float UIMax=  3.0; > = {1.0};
float  SEPARATE_VAR(UICG_HueConGreen)			<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Contrast Green);						float UIMin= -1.0; float UIMax=  3.0; > = {1.0};
float  SEPARATE_VAR(UICG_HueConCyan)			<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Contrast Cyan);						float UIMin= -1.0; float UIMax=  3.0; > = {1.0};
float  SEPARATE_VAR(UICG_HueConBlue)			<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Contrast Blue);						float UIMin= -1.0; float UIMax=  3.0; > = {1.0};
float  SEPARATE_VAR(UICG_HueConMagenta)		<string UIGroup = "CG.KiSuite"; string UIName=TO_STRING(|- TODIE - Contrast Magenta);					float UIMin= -1.0; float UIMax=  3.0; > = {1.0};



//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 2

#if AA_SUPPORT_PRESET > 0
UI_WHITESPACE(6)
UI_WHITESPACE(7)
UI_ELEMENT(_AA_Header,"         >>>>>>ANTIALIASING<<<<<<")
UI_WHITESPACE(8)

#if AA_SUPPORT_PRESET == 1 || AA_SUPPORT_PRESET > 2
bool UISMAA_Enable		   <string UIGroup = "AA"; string UIName="|- SMAA Enable"; > = {true};
#endif

#if AA_SUPPORT_PRESET > 1 && AA_SUPPORT_PRESET != 4
bool UIFXAA_Enable		   <string UIGroup = "AA"; string UIName="|- FXAA Enable"; > = {false};
#endif

#if AA_SUPPORT_PRESET > 3
bool UICREAA_Enable		   <string UIGroup = "AA"; string UIName="|- CREAA Enable"; > = {false};
#endif

#if AA_SUPPORT_PRESET > 1 && AA_SUPPORT_PRESET != 4
UI_SPECIAL_WHITESPACE(2)
float UIFXAA_SubPix		   <string UIGroup = "AA"; string UIName="|- FXAA Sub-pixel Amount";	      float UIMin=0.0; float UIMax=1.0;  > = {0.75};
float UIFXAA_EdgeThresh    <string UIGroup = "AA"; string UIName="|- FXAA Edge Threshold";       float UIMin=0.0; float UIMax=1.0;  > = {0.166};
float UIFXAA_EdgeThreshMin <string UIGroup = "AA"; string UIName="|- FXAA Min Edge Threshold";		      float UIMin=0.0; float UIMax=1.0;  > = {0.0833};
#endif

#if AA_SUPPORT_PRESET > 3
UI_SPECIAL_WHITESPACE(3)
float UIAA_Sensitivity	   <string UIGroup = "AA"; string UIName="|- CREAA Sensitivity"; float UIStep=1.0; float UIMin=1.0; float UIMax=10.0; > = {3.0};
float UIAA_Intensity	   <string UIGroup = "AA"; string UIName="|- CREAA Intensity";				      float UIMin=0.0; float UIMax=0.66; > = {0.4};
#endif

#endif //AA_SUPPORT_PRESET


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//

#if ENABLE_BLUR_SUITE
UI_WHITESPACE(9)
UI_WHITESPACE(10)
UI_ELEMENT(_Blur_Header,"           >>>>>>BLUR SUITE<<<<<<")
UI_WHITESPACE(11)

bool  UIB_EnableBlur	 <string UIGroup = "Blur Suite"; string UIName="|- Enable";																       > = {false};
float UIB_MaxAmount		 <string UIGroup = "Blur Suite"; string UIName="|- Amount";								    float UIMin=0.0; float UIMax=50.0; > = {2.0};
int   UIB_BlurType       <string UIGroup = "Blur Suite"; string UIName="|- Type";								    int   UIMin=  1; int   UIMax=   4; > = {  1}; //1 = 2D blur, 2-4 = 1D blur
int   UIB_Quality		 <string UIGroup = "Blur Suite"; string UIName="|- Quality"; string UIWidget="Quality";     int   UIMin=  0; int   UIMax=   2; > = {  1};
UI_SPECIAL_WHITESPACE(4)
bool  UIB_EnableRadial   <string UIGroup = "Blur Suite"; string UIName="|- Use radial direction";									       > = {true};
int   UIB_BlurDir        <string UIGroup = "Blur Suite"; string UIName="|- Blur - Type 2-4 - Non-Radial Direction (\xB0)"; int   UIMin=  0; int   UIMax= 360; > = {  0};
UI_SPECIAL_WHITESPACE(5)
bool  UIB_EnableVig		 <string UIGroup = "Blur Suite.Vignetting"; string UIName="|- Enable";													       > = {false};
float UIB_VigCurve		 <string UIGroup = "Blur Suite.Vignetting"; string UIName="|- Curve";					    float UIMin=0.0; float UIMax=10.0; > = {1.0};
float UIB_VigInner		 <string UIGroup = "Blur Suite.Vignetting"; string UIName="|- Inner Radius";			    float UIMin=0.0; float UIMax= 2.0; > = {0.2};
float UIB_VigOuter  	 <string UIGroup = "Blur Suite.Vignetting"; string UIName="|- Outer Radius";			    float UIMin=0.0; float UIMax= 2.0; > = {1.0};
UI_SPECIAL_WHITESPACE(6)
bool  UIB_EnableDepth	 <string UIGroup = "Blur Suite.Depth"; string UIName="|- Enable";														       > = {false};
bool  UIB_DepthInvert	 <string UIGroup = "Blur Suite.Depth"; string UIName="|- Invert";														       > = {false};
float UIB_DepthCurve     <string UIGroup = "Blur Suite.Depth"; string UIName="|- Curve";						    float UIMin=0.0; float UIMax=10.0; > = {1.0};
float UIB_DepthStart     <string UIGroup = "Blur Suite.Depth"; string UIName="|- Fade Start"; float UIStep=0.001; float UIMin=0.0; float UIMax= 1.0; > = {0.0};
float UIB_DepthEnd		 <string UIGroup = "Blur Suite.Depth"; string UIName="|- Fade End";   float UIStep=0.001; float UIMin=0.0; float UIMax= 1.0; > = {1.0};
float UIB_EdgeBleedReduc <string UIGroup = "Blur Suite.Depth"; string UIName="|- Edge Bleeding Reduction";	    float UIMin=0.0; float UIMax= 1.0; > = {0.3};

#endif //ENABLE_BLUR_SUITE


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//

UI_WHITESPACE(12)
UI_WHITESPACE(13)
UI_ELEMENT(_Sharp_Header,"             >>>>>>SHARPEN<<<<<<")
UI_WHITESPACE(14)

bool  UIUM_Enable		<string UIGroup = "KiSharp"; string UIName="|- Enable";										      > = {true};
#if ENABLE_HIGH_QUALITY_SHARPENING
float UIUM_Steps		<string UIGroup = "KiSharp"; string UIName="|- Steps";		float UIStep=1.0; float UIMin=1.0;  float UIMax=15.0; > = {1.0};
#else
float UIUM_Steps		<string UIGroup = "KiSharp"; string UIName="|- Steps";		float UIStep=1.0; float UIMin=1.0;  float UIMax=SHARP_MAX_KERNELSIZE - 1.0; > = {1.0};
#endif
float UIUM_StepSize		<string UIGroup = "KiSharp"; string UIName="|- Step Size";					  float UIMin=0.01; float UIMax=1.25; > = {1.0};
float UIUM_Amount		<string UIGroup = "KiSharp"; string UIName="|- Amount";						  float UIMin=0.0;  float UIMax=10.0; > = {0.7};
float UIUM_LumaClamp	<string UIGroup = "KiSharp"; string UIName="|- Luma Clamping";				  float UIMin=0.0;  float UIMax= 2.0; > = {0.1};
UI_SPECIAL_WHITESPACE(7)
float UIUM_ConWeight	<string UIGroup = "KiSharp"; string UIName="|- Contrast Awareness";			  float UIMin=0.0;  float UIMax= 1.0; > = {0.6};
float UIUM_EdgeMask		<string UIGroup = "KiSharp"; string UIName="|- Object Edge Removal";		  float UIMin=0.0;  float UIMax= 1.0; > = {0.5};
float UIUM_Depthfade	<string UIGroup = "KiSharp"; string UIName="|- Depth Fade";					  float UIMin=0.01; float UIMax= 1.0; > = {1.0};
float UIUM_DepthBlur	<string UIGroup = "KiSharp"; string UIName="|- Depth Blur";					  float UIMin=0.0;  float UIMax= 1.0; > = {1.0};
bool  UIUM_Visualize	<string UIGroup = "KiSharp"; string UIName="|- Show Mask";													      > = {false};


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//

UI_WHITESPACE(15)
UI_WHITESPACE(16)
UI_ELEMENT(_MC_Header1," >>>>MONOCHROMA - COLORFILTER<<<<")
UI_WHITESPACE(17)

bool   UIMC_Colorfilter_Enable	<string UIGroup = "CG.Monochroma"; string UIName="|- Enable";																	> = {false};
float3 UIMC_HueSelect			<string UIGroup = "CG.Monochroma"; string UIName="|- Color Selection";				string UIWidget="Color";								> = {1,0,0};
float  UIMC_FilterRange			<string UIGroup = "CG.Monochroma"; string UIName="|- Filter Range";					float UIMin=0.0;  float UIMax=1.0; float UIStep=0.0001; > = {0.08};
float  UIMC_Hardness			<string UIGroup = "CG.Monochroma"; string UIName="|- Filter Hardness";			float UIMin=0.0;  float UIMax=1.0; float UIStep=0.01;	> = {0.00};
float  UIMC_Sat					<string UIGroup = "CG.Monochroma"; string UIName="|- Filtered Saturation";			float UIMin=0.0;  float UIMax=5.0; float UIStep=0.01;	> = {1.00};
float  UIMC_NF_Sat				<string UIGroup = "CG.Monochroma"; string UIName="|- Non-filtered Saturation";		float UIMin=0.0;  float UIMax=1.0; float UIStep=0.01;	> = {0.00};

UI_WHITESPACE(18)
UI_WHITESPACE(19)
UI_ELEMENT(_MC_Header2,"   >>>>>>MONOCHROMA - B&W<<<<<<")
UI_WHITESPACE(20)
bool   UIBW_Enable				<string UIGroup = "CG.Monochroma.Black and White"; string UIName="|- Enable";																> = {false};
float  UIBW_RedWeight			<string UIGroup = "CG.Monochroma.Black and White"; string UIName="|- Red Weight";						float UIMin=0.0;  float UIMax=1.0; float UIStep=0.001;  > = {0.213};//~Rec.709
float  UIBW_GreenWeight			<string UIGroup = "CG.Monochroma.Black and White"; string UIName="|- Green Weight";						float UIMin=0.0;  float UIMax=1.0; float UIStep=0.001;  > = {0.715};
float  UIBW_BlueWeight			<string UIGroup = "CG.Monochroma.Black and White"; string UIName="|- Blue Weight";						float UIMin=0.0;  float UIMax=1.0; float UIStep=0.001;  > = {0.072};
float4 UIBW_Tint				<string UIGroup = "CG.Monochroma.Black and White"; string UIName="|- Tint";								string UIWidget="Color";								> = {0.439,0.259,0.0784,0.0}; //Sepia


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//

UI_WHITESPACE(21)
UI_WHITESPACE(22)
UI_ELEMENT(_Vignette_Header,"             >>>>>>VIGNETTE<<<<<<")
UI_WHITESPACE(23)

bool   UI_VigEnable				<string UIGroup = "Vignette"; string UIName="|- Enable";													    > = {false};
int    UI_VigBlendMode    		<string UIGroup = "Vignette"; string UIName="|- Blend Mode";				int   UIMin=1;	  int   UIMax=4;    > = {1};
float  UI_VigRadiusInner		<string UIGroup = "Vignette"; string UIName="|- Inner Radius";			float UIMin=0.00; float UIMax=2.0;  > = {0.5};
float  UI_VigRadiusOuter  		<string UIGroup = "Vignette"; string UIName="|- Outer Radius";			float UIMin=0.00; float UIMax=2.0;  > = {1.2};
float  UI_VigPositionH   			<string UIGroup = "Vignette"; string UIName="|- Position Horizontal";	float UIMin=-1.0; float UIMax=1.0;  > = {0.0};
float  UI_VigPositionV   			<string UIGroup = "Vignette"; string UIName="|- Position Vertical";		float UIMin=-1.0; float UIMax=1.0;  > = {0.0};
float  UI_VigRoundness				<string UIGroup = "Vignette"; string UIName="|- Roundness";				float UIMin=0.01; float UIMax=1.0;  > = {1.0};
float  UI_VigAspectRatio			<string UIGroup = "Vignette"; string UIName="|- Aspect Ratio";			float UIMin=0.25; float UIMax=4.0;  > = {1.0};
float4 UI_VigColor   			<string UIGroup = "Vignette"; string UIName="|- Color"; 					string UIWidget="Color";		    > = {0.0,0.0,0.0,0.5};
float  UI_VigCurve   			<string UIGroup = "Vignette"; string UIName="|- Curve";					float UIMin=0.01; float UIMax=10.0; > = {1.5};
bool   UI_VigShowRadii			<string UIGroup = "Vignette"; string UIName="|- Show Radii";												    > = {false};


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//

UI_WHITESPACE(24)
UI_WHITESPACE(25)
UI_ELEMENT(_CA_Header,">>>>>>CHROMATIC ABERRATION<<<<<<")
UI_WHITESPACE(26)

bool   UICA_Enable		<string UIGroup = "LensDist"; string UIName="|- Enable";									  > = {false};
float  UICA_HighPass	<string UIGroup = "LensDist"; string UIName="|- HighPass";				      float UIMin= 0.0; float UIMax=2.0;  > = {0.0};
float  UICA_Amount  	<string UIGroup = "LensDist"; string UIName="|- Amount";				      float UIMin=-8.0; float UIMax=8.0;  > = {1.0};
float  UICA_MaxRadius	<string UIGroup = "LensDist"; string UIName="|- Max Radius";			      float UIMin= 1.0; float UIMax=3.0;  > = {1.0};
float  UICA_Curve   	<string UIGroup = "LensDist"; string UIName="|- Curve";					      float UIMin= 1.0; float UIMax=10.0; > = {1.5};
bool   UICA_Visualize	<string UIGroup = "LensDist"; string UIName="|- Show CA Area";													  > = {false};


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//

UI_WHITESPACE(40)
UI_WHITESPACE(41)
UI_ELEMENT(_LUT_Header,"          >>>>>>LUT WEIGHTS<<<<<<")
UI_WHITESPACE(42)

// Voyager Collection
float UILUT_Algol               <string UIGroup = "CG.LUTs.Voyager"; string UIName="|- Algol";             float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_Arakon              <string UIGroup = "CG.LUTs.Voyager"; string UIName="|- Arakon";            float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_Arcturus            <string UIGroup = "CG.LUTs.Voyager"; string UIName="|- Arcturus";          float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_Argus               <string UIGroup = "CG.LUTs.Voyager"; string UIName="|- Argus";             float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_Cambra              <string UIGroup = "CG.LUTs.Voyager"; string UIName="|- Cambra";            float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_Cerberus            <string UIGroup = "CG.LUTs.Voyager"; string UIName="|- Cerberus";          float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_Cruces              <string UIGroup = "CG.LUTs.Voyager"; string UIName="|- Cruces";            float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_Elora               <string UIGroup = "CG.LUTs.Voyager"; string UIName="|- Elora";             float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_Halka               <string UIGroup = "CG.LUTs.Voyager"; string UIName="|- Halka";             float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_Hypatia             <string UIGroup = "CG.LUTs.Voyager"; string UIName="|- Hypatia";           float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_Mutara              <string UIGroup = "CG.LUTs.Voyager"; string UIName="|- Mutara";            float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_Nelvana             <string UIGroup = "CG.LUTs.Voyager"; string UIName="|- Nelvana";           float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_Omega               <string UIGroup = "CG.LUTs.Voyager"; string UIName="|- Omega";             float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_Organia             <string UIGroup = "CG.LUTs.Voyager"; string UIName="|- Organia";           float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_Persephone          <string UIGroup = "CG.LUTs.Voyager"; string UIName="|- Persephone";        float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_Scorpii             <string UIGroup = "CG.LUTs.Voyager"; string UIName="|- Scorpii";           float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_Valo                <string UIGroup = "CG.LUTs.Voyager"; string UIName="|- Valo";              float UIMin=0.0; float UIMax=1.0; > = {0.0};

// Custom / Film Stock Collection
float UILUT_AgfaRSXII100        <string UIGroup = "CG.LUTs.Custom"; string UIName="|- Agfa RSX II 100";    float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_AgfaChroma1000RS    <string UIGroup = "CG.LUTs.Custom"; string UIName="|- Agfa Chroma 1000RS"; float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_AgfacolorUltra50    <string UIGroup = "CG.LUTs.Custom"; string UIName="|- Agfacolor Ultra 50"; float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_Cinestill800T       <string UIGroup = "CG.LUTs.Custom"; string UIName="|- Cinestill 800T";     float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_Cinestill800TAlt    <string UIGroup = "CG.LUTs.Custom"; string UIName="|- Cinestill 800T Alt"; float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_CinestillD50        <string UIGroup = "CG.LUTs.Custom"; string UIName="|- Cinestill D50";      float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_FantasticoFiltro    <string UIGroup = "CG.LUTs.Custom"; string UIName="|- Fantastico Filtro";  float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_FujiVelvia50        <string UIGroup = "CG.LUTs.Custom"; string UIName="|- Fuji Velvia 50";     float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_Fujifilm3513        <string UIGroup = "CG.LUTs.Custom"; string UIName="|- Fujifilm 3513";      float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_Kodak200T           <string UIGroup = "CG.LUTs.Custom"; string UIName="|- Kodak 200T";         float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_Kodak2383D55        <string UIGroup = "CG.LUTs.Custom"; string UIName="|- Kodak 2383 D55";     float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_Kodak2383D60        <string UIGroup = "CG.LUTs.Custom"; string UIName="|- Kodak 2383 D60";     float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_Kodak2383D65        <string UIGroup = "CG.LUTs.Custom"; string UIName="|- Kodak 2383 D65";     float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_Kodak250D           <string UIGroup = "CG.LUTs.Custom"; string UIName="|- Kodak 250D";         float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_Kodak500T           <string UIGroup = "CG.LUTs.Custom"; string UIName="|- Kodak 500T";         float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_Kodachrome25        <string UIGroup = "CG.LUTs.Custom"; string UIName="|- Kodachrome 25";      float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_Kodachrome64        <string UIGroup = "CG.LUTs.Custom"; string UIName="|- Kodachrome 64";      float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_KodakUltraMax       <string UIGroup = "CG.LUTs.Custom"; string UIName="|- Kodak Ultra Max";    float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_KonicaCenturia200   <string UIGroup = "CG.LUTs.Custom"; string UIName="|- Konica Centuria 200";float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_TealOrange          <string UIGroup = "CG.LUTs.Custom"; string UIName="|- Teal Orange";        float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_Technicolor16       <string UIGroup = "CG.LUTs.Custom"; string UIName="|- Technicolor 16";     float UIMin=0.0; float UIMax=1.0; > = {0.0};
float UILUT_KonicaColorCenturia <string UIGroup = "CG.LUTs.Custom"; string UIName="|- Konica Color Centuria"; float UIMin=0.0; float UIMax=1.0; > = {0.0};


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//

UI_WHITESPACE(27)
UI_WHITESPACE(28)
UI_ELEMENT(_Grain_Header,"                 >>>>>>GRAIN<<<<<<")
UI_WHITESPACE(29)

bool  UIGrain_Enable			<string UIGroup = "KiGrain"; string UIName="|- Enable";										   > = {false};
float UIGrain_Int				<string UIGroup = "KiGrain"; string UIName="|- Intensity";	float UIMin= 0.0; float UIMax=1.0; > = {0.04};
float UIGrain_Saturation		<string UIGroup = "KiGrain"; string UIName="|- Saturation"; float UIMin= 0.0; float UIMax=1.0; > = {0.0};
float UIGrain_Motion			<string UIGroup = "KiGrain"; string UIName="|- Motion"; 	float UIMin=-2.0; float UIMax=2.0; > = {0.2};


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//

UI_WHITESPACE(43)
UI_WHITESPACE(44)
UI_ELEMENT(_Dither_Header,"               >>>>>>DITHER<<<<<<")
UI_WHITESPACE(45)

float  UID_Intensity			<string UIGroup = "Dither"; string UIName="|- Intensity";	float UIMin= 0.0; float UIMax=20.0; > = {10.0};
float  UID_Motion				<string UIGroup = "Dither"; string UIName="|- Motion"; 		float UIMin= 0.0; float UIMax=2.0;  > = {1.0};
bool   UID_Visualize			<string UIGroup = "Dither"; string UIName="|- Visualize";								         > = {false};


//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//

UI_WHITESPACE(30)
UI_WHITESPACE(31)
UI_ELEMENT(_Border_Header," >>>>>>LETTERBOX / PILLARBOX<<<<<<")
UI_WHITESPACE(32)

bool   UIBorder_Enable			<string UIGroup = "CinBorders"; string UIName="|- Enable";											 > = {false};
float4 UIBorder_Color			<string UIGroup = "CinBorders"; string UIName="|- Color & Amount";								string UIWidget="Color";		 > = {0,0,0,1};
float  UIBorder_Ratio			<string UIGroup = "CinBorders"; string UIName="|- Ratio";								float UIMin=0; float UIMax=30;   > = {2.3};
bool   UIBorder_Use_PxlOs		<string UIGroup = "CinBorders"; string UIName="|- Use Pixel Offsets instead";											 > = {false};
float  UIBorder_Width			<string UIGroup = "CinBorders"; string UIName="|- Width"; float UIStep=1.0; float UIMin=0; float UIMax=4000; > = {0.0};
float  UIBorder_Height			<string UIGroup = "CinBorders"; string UIName="|- Height"; float UIStep=1.0; float UIMin=0; float UIMax=4000; > = {0.0};

#undef SHADERGROUP

#endif //SHADERGROUP

#undef TODIE
