//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
// 				///////////////		/////      ////    ////////////
// 				///////////////   //////   	 ////    ////      ////
// 				////							///////    ////    ////      ////
// 				////////// 				////////   ////    ////////////
// 				//////////				//// ////  ////    ////////////
// 				////              ////  //// ////    ////      ////
// 				///////////////   ////    ///////		 ////      ////
// 				///////////////   ////     //////    ////////////
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
// TREYM'S FILM WORKSHOP
//
// CREDITS:
//
// Boris Vorontsov: For ENBSeries
//
// JawZ:  Author and developer of the MSL code
//
// CeeJay.dk-Crosire-GemFx-Marty McFly-Lucifer Hawk: Original Authors of sweeFX
// & Reshade code
//
// martinsh: Original author of film grain shader
//
// roxahris: Port of film martinsh's film grain to ENB
//
// TreyM: Developer of this ENB
//
#include "ENB_Settings.txt"
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// INTERACTIVE USER ADJUSTMENTS FOR IN-GAME MENU
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

bool empty3 <
    string UIName = "+++++++++++++++++++++++++++++++++++";
> = {false};

bool SHOW_HELPER <
    string UIName = "                         CLICK HERE FOR HELP ->";
> = {false};

bool SHOW_VERSION <
    string UIName = "FILM WORKSHOP VERSION:                   1.6";
> = {false};

#if(CUSTOM_MODE == 1)
#include "Custom/Look Packs/Config.fxh"
#endif

#if(CUSTOM_MODE == 2)
bool SHOW_CUSTOM <
    string UIName = "   USER MODE ENABLED";
> = {false};
#endif
#if(CUSTOM_MODE == 1)
bool SHOW_CUSTOM <
    string UIName = "   CUSTOM LOOK PACK INSTALLED";
> = {false};
bool SHOW_LOOKPACK <
    string UIName = PACK_SELECTION;
> = {false};
#endif

#if(CUSTOM_MODE == 1)
bool empty72 <
    string UIName = "++++++++++++++++++++ LOOK PACK  ";
> = {false};
int   LutLookPack_set <   string UIName="Look Pack LUT Selection";   float UIMin=1;   float UIMax=5;> = {0};
#define LutLookPack (LutLookPack_set - 1)
#endif

#if(ACTIVATE_CHROMA == 1)
bool empty35 <
    string UIName = "++++++++++++++++++++++++ LENS ";
> = {false};

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// LENS EMULATION
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
bool ENABLE_LENS <
    string UIName = "Enable Lens Emulation";
> = {true};

int LCAStrengthAmt <
    string UIName="   Chromatic Aberration";
    string UIWidget="spinner";
    float UIMin=0;
    float UIMax=50;
    float UIStep=1;
> = {15};

bool ENABLE_DISTORTION <
    string UIName = "   Enable Lens Distortion";
> = {false};

int LCADistAmt <
    string UIName="      Lens Distortion Amount";
    string UIWidget="spinner";
    float UIMin=-150;
    float UIMax=150;
    float UIStep=1;
> = {50};

#if(ACTIVATE_VIGNETTE == 1)
#if(ACTIVATE_CHROMA == 0)
bool empty35 <
    string UIName = "++++++++++++++++++++++++ LENS ";
> = {false};

bool ENABLE_LENS <
    string UIName = "   Enable Vignette";
> = {true};
#endif

float VAMT <
        string UIName="   Vignette Amount";
		string UIWidget="Spinner";
		float UIMin=0.00;
        float UIMax=3.75;
		float UIStep=0.01;
> = {0.75};
#define VignetteAmount (VAMT * -1.0)

float VignetteRatioSet <
        string UIName="   Vignette Ratio (ignored with letterbox)";
		string UIWidget="Spinner";
		float UIMin=0.10;
        float UIMax=10.0;
		float UIStep=0.01;
> = {1.78};

#define VignetteRatio lerp(VignetteRatioSet, border_ratio, USE_BORDER)
#define VignetteType       1
#define VignetteRadius  2.50
#define VignetteSlope      2
#define VignetteCenter float2(0.500, 0.500)
#endif

#if(CUSTOM_MODE == 0)
bool ENABLE_FILTER <
    string UIName = "   Enable Lens Filter";
> = {false};
float3 FogColor3        <string UIName="      Filter Color"; string UIWidget="color";> = {1.27, 1.27, 1.27};
int DefogAmt3_set <
    string UIName="      Filter Density";
    string UIWidget="spinner";
    float UIMin=0;
    float UIMax=100;
	float UIStep=1;
> = {0};
#define DefogAmt3 DefogAmt3_set
#define Defog3_set DefogAmt3 * 0.001
#define Defog3 Defog3_set * -1.0
#define Exposure2 1.0 - (DefogAmt3_set * 0.005)
#define Gamma2 1.0
#endif

#define LCAStrength LCAStrengthAmt * 0.01
#define fFisheyeDistortionCubic 0.0
//#define fFisheyeDistortion lerp((LCAStrength * 0.1), lerp((LCADistAmt * 0.003), ((LCADistAmt * -1) * 0.0015), (LCADistMode - 1.0)), ENABLE_DISTORTION)
//#define fFisheyeZoom lerp(0.5 - (fFisheyeDistortionCubic * 0.75), lerp(0.5 + (LCADistAmt * 0.00076), 0.5 - (LCADistAmt * 0.00019), (LCADistMode - 1.0)), ENABLE_DISTORTION)

#define fFisheyeDistortion lerp((LCAStrength * 0.1), LCADistAmt * (LCADistAmt > 0.0? 0.003: 0.0015), ENABLE_DISTORTION)
#define fFisheyeZoom lerp(0.5 - (fFisheyeDistortionCubic * 0.75), 0.5 + LCADistAmt * (LCADistAmt > 0.0? 0.00076 : 0.00019), ENABLE_DISTORTION)

#define fFisheyeColorshift 0.0
#else
#define LCAStrength 0.0
#define fFisheyeDistortionCubic 0.0
#define fFisheyeZoom 0.5 - (fFisheyeDistortionCubic * 0.75)
#define fFisheyeDistortion (LCAStrength * 0.1)
#define fFisheyeColorshift 0.0
#endif

#if(CUSTOM_MODE == 0)
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// CAMERA ADJUSTMENTS
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
bool empty72 <
    string UIName = "+++++++++++++++++++++ CAMERA  ";
> = {false};
/*bool ENABLE_TONEMAP <
    string UIName = "Enable Camera Adjustments";
> = {true};*/
//// TONEMAP
float CamExposure <
    string UIName="   Exposure";
    string UIWidget="spinner";
    float UIMin=0.0;
    float UIMax=2.0;
	float UIStep=0.01;
> = {1.0};
#define Exposure (CamExposure + ((Defog + Defog2) * 5))

float GammaSet <
    string UIName="   Gamma";
    string UIWidget="spinner";
    float UIMin=0.1;
    float UIMax=2.0;
	float UIStep=0.01;
> = {1.0};
#define Gamma lerp((GammaSet + 0.05), (GammaSet + 0.22), ENABLE_FOGFIX)

#define Saturation 0

int DefogAmt <
    string UIName="   Color Temperature";
    string UIWidget="spinner";
    float UIMin=-30;
    float UIMax=30;
	float UIStep=1;
> = {0};
#define Defog DefogAmt * 0.001

int DefogAmt2 <
    string UIName="   Tint";
    string UIWidget="spinner";
    float UIMin=-30;
    float UIMax=30;
	float UIStep=1;
> = {0};
#define Defog2 DefogAmt2 * 0.001
#define Bleach 0.0
#define FogColor float3(0.85,1.70,2.55)
#define FogColor2 float3(2.55,0.85,1.70)

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// NEGATIVE FILM
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
bool empty4 <
    string UIName = "++++++++++++ NEGATIVE FILM STOCK  ";
> = {false};

int   lut_n_set <   string UIName="Negative Film Stock Selection";   float UIMin=1;   float UIMax=42;> = {34};
#define lut_n (lut_n_set - 1)

// Negative STock Grain
#if(ACTIVATE_GRAIN == 1)
int grainamtnegative <
    string UIName="   Negative Grain Amount (Coarse)";
	  string UIWidget="Spinner";
	  float UIMin=0;
	  float UIMax=100;
	  float UIStep=1;
> = {15};

#define grainamountnegative (grainamtnegative * 0.0005)
#define coloramountnegative 0.50
#define grainsizenegative 2.40
#define lumamountnegative 0.25
#endif

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// PRINT FILM
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
bool empty2 <
    string UIName = "++++++++++++++ PRINT FILM  STOCK ";
> = {false};

#if(CUSTOM_MODE == 0)
int   lut_p_set <   string UIName="Print Film Stock Selection";   float UIMin=1;   float UIMax=15;> = {10};
#define lut_p (lut_p_set - 1)
#endif
#endif

#if(ACTIVATE_GRAIN == 1)
//// PRINT STOCK GRAIN
int grainamtprint
<
      string UIName="   Print Grain Amount (Fine)";
	  string UIWidget="Spinner";
	  float UIMin=0;
	  float UIMax=100;
	  float UIStep=1;
> = {30};

#define grainamountprint (grainamtprint * 0.0005)
#define coloramountprint 0.40
#define grainsizeprint 1.40
#define lumamountprint 0.80
#endif

#if(CUSTOM_MODE == 0)
bool ENABLE_AGE <
    string UIName = "   Enable Film Age Filter";
> = {false};

float AGE_MODE
<
  string UIName="   Age Mode";
  string UIWidget="spinner";
  float UIMin=1.0;
  float UIMax=2.0;
  float UIStep=1.0;
> = {1.0};

float grade_age <
    string UIName="   Age Amount";
    string UIWidget="spinner";
    float UIMin=0.0;
    float UIMax=100.00;
    float UIStep=1.0;
> = {0.0};
#define SepiaPower (grade_age * 0.0255)
#define Monochrome_color_saturation 0.0
#define Monochrome_conversion_values float3(0.21, 0.72, 0.07)
#define ColorToneSepia float3(1.000, 0.945, 0.900)
#define ColorToneMagenta float3(1.000, 0.920, 0.945)
#define GreyPower ((SepiaPower * 0.09) * -1)

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// COLOR GRADING
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
#if(ACTIVATE_GRADING == 1)
bool empty5 <
    string UIName = "++++++++++++++++ COLOR GRADING ";
> = {false};

bool ENABLE_GRADE <
    string UIName = "Enable Color Grading";
> = {true};

bool BW_MODE <
    string UIName = "   Monochrome Mode (B&W)";
> = {false};

int Curves_contrast_luma_amt <
    string UIName="   Contrast";
    string UIWidget="spinner";
    float UIMin=-100;
    float UIMax=100;
	float UIStep=1;
> = {0};
#define Curves_contrast_luma Curves_contrast_luma_amt * 0.01

int Curves_contrast_chroma_amt <
    string UIName="   Saturation";
    string UIWidget="spinner";
    float UIMin=0;
    float UIMax=200;
	float UIStep=1;
> = {100};
#define Curves_contrast_chroma (Curves_contrast_chroma_amt -100) * 0.01

int VibranceAmt <
    string UIName="   Vibrance";
    string UIWidget="spinner";
    float UIMin=0;
    float UIMax=100;
	float UIStep=1;
> = {0};
#define Vibrance VibranceAmt * 0.01
#define Vibrance_RGB_balance float3(1.00,1.00,1.00)

bool ENABLE_CW <
    string UIName = "   Enable Color Wheels";
> = {true};
bool   Enable_CWwidget <string UIName="      Show Widget (Shift + C)";   string UIWidgetEX="CWcolorE";> = {false};

bool ENABLE_HSL <
    string UIName = "   Enable Advanced Hue Shifting";
> = {true};
bool   Enable_HSLwidget <string UIName="      Show Widget (Shift + H)";  > = {false};




bool empty82 <
    string UIName = "++++++++++++++ GRADIENT OVERLAY ";
> = {false};

bool ENABLE_GRADIENT <
    string UIName = "Enable Gradient Overlay";
> = {false};

float GradientOpacity <
    string UIName="   Gradient Opacity";
    string UIWidget="spinner";
    float UIMin=0.0;
    float UIMax=1.0;
    float UIStep=0.01;
> = {0.5};

float3 GradientTop <
    string UIName = "   Gradient Color 1";  string UIWidget="Color";
> = {0.500, 0.500, 0.500};

float3 GradientBottom <
    string UIName = "   Gradient Color 2";  string UIWidget="Color";
> = {0.500, 0.500, 0.500};

float GradientRotation <
    string UIName="   Gradient Rotation (\xB0)";
    string UIWidget="spinner";
    float UIMin=-360.0;
    float UIMax=360.0;
    float UIStep=1.0;
> = {-90.0};

float GradientShift <
    string UIName="   Gradient Shift";
    string UIWidget="spinner";
    float UIMin=0.0;
    float UIMax=1.0;
    float UIStep=0.01;
> = {0.5};

#define GradientBlendPower 1.0
#define GradientBlendMode BlendOverlayf

bool empty81 <
    string UIName = "++++++++++++ LEVELS ADJUSTMENTS ";
> = {false};

int black_pnt <
    string UIName="   Lift Shadows";
	  string UIWidget="Spinner";
	  float UIMin=0;
	  float UIMax=100;
	  float UIStep=1;
> = {0};

int white_pnt <
    string UIName="   Drop Highlights";
	  string UIWidget="Spinner";
	  float UIMin=185;
	  float UIMax=255;
	  float UIStep=1;
> = {255};

#define Levels_black_point (black_pnt + 12) * -1
#define Levels_white_point 235 + (255 - white_pnt)

int cal_black <
    string UIName="   Crush Shadows (CLIPPING)";
	  string UIWidget="Spinner";
	  float UIMin=0;
	  float UIMax=100;
	  float UIStep=1;
> = {0};

int cal_white_set <
    string UIName="   Expand Highlights (CLIPPING)";
	  string UIWidget="Spinner";
	  float UIMin=0;
	  float UIMax=60;
	  float UIStep=1;
> = {0};
#define cal_white 255 - cal_white_set

#endif
#endif

#if(ACTIVATE_GRADING == 0)
#define Levels_black_point 0
#define Levels_white_point 255
#define cal_white 255
#define cal_black 0
#endif

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// SHARPENING
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

bool empty74 <
    string UIName = "+++++++++++++++++++ SHARPENING ";
> = {false};

//// LUMASHARPEN
int sharp_strength_amt
<
        string UIName="   Sharpening Amount";
		string UIWidget="Spinner";
		float UIMin=0;
		float UIMax=30;
		float UIStep=1;
> = {6};
#if(ACTIVATE_CHROMA == 1)
#define sharp_strength lerp((sharp_strength_amt * 0.1), ((sharp_strength_amt * 0.1) + (abs(LCADistAmt) * 0.01)), ENABLE_DISTORTION)
#elif(ACTIVATE_CHROMA == 0)
#define sharp_strength sharp_strength_amt * 0.1
#endif

#define pattern 2
#define sharp_clamp 0.028
#define offset_bias 1.0
#define CoefLuma float3(0.2126, 0.7152, 0.0722)

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// LETTERBOX
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

bool empty71 <
    string UIName = "+++++++++++++++++ IMAGE FORMAT ";
> = {false};

//// ASPECT RATIO
bool USE_BORDER <
    string UIName = "   Enable Letterbox / Pillarbox";
> = {true};
#define border_width float2(0,0)
#define border_color float3(0, 0, 0)

float border_ratio
<
        string UIName="   Aspect Ratio";
		string UIWidget="Spinner";
		float UIMin=0.10;
		float UIMax=10.00;
		float UIStep=0.01;
> = {2.35};

#define DITHER_METHOD 3
#define VISUALIZE_PATTERN false
#define screen_size float2(ScreenSize.x,ScreenSize.x*ScreenSize.w)
#define screen_ratio (screen_size.x / screen_size.y)
#define pixel float2(ScreenSize.y,ScreenSize.y*ScreenSize.z)

#if(CUSTOM_MODE == 0)
#if(ACTIVATE_GRADING == 1)
bool empty77 <
    string UIName = "++++++++++++++++++++++ CUSTOM ";
> = {false};

bool SHOW_LUT <
    string UIName = "   Enable LUT Overlay";
> = {false};
#endif

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// SCOPES
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
#if(ACTIVATE_SCOPES == 1 && CUSTOM_MODE == 0 && ACTIVATE_GRADING == 1)
bool empty64 <
    string UIName = "++++++++++++++++++++++ SCOPES ";
> = {false};

bool   Scope_Enable     <string UIName="Enable Scopes";          > = {false};
bool   Scope_Vector     <string UIName="   Enable VectorScope (FPS hit)";    > = {false};
bool   Scope_Parade     <string UIName="   Wavefrom Parade";       > = {false};
int    Scope_mode       <string UIName="   Histogram mode";        float UIMin=0; float UIMax=4;> = {0};
int    Scope_Bar0       <string UIName="    ";   float UIMin=0; float UIMax=0;> = {0};
#define Scope_Cursor float4(0, 0, 0, 0)
#define Scope_TapWeight 1.5
#define Scope1_TapWeight 1.5
#define Scope3_TapWeight 0.3
#define Scope_LocalData false
float  Scope_ZoomScale  <string UIName="   Image Zoom Scale";  float UIMin=1;          > = {1.0};
float2 Scope_ZoomCenter <string UIName="   Image Zoom Offset"; float UIMin=-0.5;float UIMax=0.5;float UIStep=0.001;> = {0.0, 0.0};
#endif
#if(ACTIVATE_SCOPES == 0)
#define Scope_Enable false
#endif
#endif
bool empty63 <
    string UIName = "+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++";
> = {false};

#if(CUSTOM_MODE != 0)
#define Defog2 0.0
#define Defog 0.0
#define Bleach 0.0
#define FogColor float3(0.85,1.70,2.55)
#define FogColor2 float3(2.55,0.85,1.70)
#define Exposure 1.0
#define Gamma 1.0
#define Saturation 0.0
#endif

bool ENABLE_FOGFIX <
    string UIName = "   TFC Gamma Compensation";
> = {false};

bool empty103 <
    string UIName = "++++++++++++ DO NOT MODIFY BELOW ";
> = {false};

#define CW_Weight 1.0
#define CW_lowrange 0.3333
#define CW_highrange 0.5555
#define CW_overlap 0.25

/* float CW_Weight    <string UIName="CW weight";    float UIMin = 0.0; float UIMax = 1.0; > = 1.0;
float CW_lowrange  <string UIName="CW lowrange";  float UIMin = 0.0; float UIMax = 1.0; > = 0.3333;
float CW_highrange <string UIName="CW highrange"; float UIMin = 0.0; float UIMax = 1.0; > = 0.5555;
float CW_overlap   <string UIName="CW overlap";   float UIMin = 0.0; float UIMax = 1.0; > = 0.25; */
//internals.
float3 CW_Lift         <string UIName="CW Lift";         string UIWidgetEX="CWcolor0"; string UIWidget="vector";> = {0.0, 0.0, 0.0};
float3 CW_Gamma        <string UIName="CW Gamma";        string UIWidgetEX="CWcolor1"; string UIWidget="vector";> = {0.0, 0.0, 0.0};
float3 CW_Gain         <string UIName="CW Gain";         string UIWidgetEX="CWcolor2"; string UIWidget="vector";> = {0.0, 0.0, 0.0};
float3 CW_Offset       <string UIName="CW Offset";       string UIWidgetEX="CWcolor3"; string UIWidget="vector";> = {0.0, 0.0, 0.0};
float3 CW_Highlight    <string UIName="CW Highlight";    string UIWidgetEX="CWcolor4"; string UIWidget="vector";> = {0.0, 0.0, 0.0};
float3 CW_Midtone      <string UIName="CW Midtone";      string UIWidgetEX="CWcolor5"; string UIWidget="vector";> = {0.0, 0.0, 0.0};
float3 CW_Shadow       <string UIName="CW Shadow";       string UIWidgetEX="CWcolor6"; string UIWidget="vector";> = {0.0, 0.0, 0.0};
float3 HUEcolor0        <string UIName="HueColorRed";     string UIWidget="color"; string UIWidgetEX="HSLcolor0";> = {0.75, 0.25, 0.25};
float3 HUEcolor1        <string UIName="HueColorOrange";  string UIWidget="color"; string UIWidgetEX="HSLcolor1";> = {0.75, 0.50, 0.25};
float3 HUEcolor2        <string UIName="HueColorYellow";  string UIWidget="color"; string UIWidgetEX="HSLcolor2";> = {0.75, 0.75, 0.25};
float3 HUEcolor3        <string UIName="HueColorGreen";   string UIWidget="color"; string UIWidgetEX="HSLcolor3";> = {0.25, 0.75, 0.25};
float3 HUEcolor4        <string UIName="HueColorCyan";    string UIWidget="color"; string UIWidgetEX="HSLcolor4";> = {0.25, 0.75, 0.75};
float3 HUEcolor5        <string UIName="HueColorBlue";    string UIWidget="color"; string UIWidgetEX="HSLcolor5";> = {0.25, 0.25, 0.75};
float3 HUEcolor6        <string UIName="HueColorPurple";  string UIWidget="color"; string UIWidgetEX="HSLcolor6";> = {0.50, 0.25, 0.75};
float3 HUEcolor7        <string UIName="HueColorMagenta"; string UIWidget="color"; string UIWidgetEX="HSLcolor7";> = {0.75, 0.25, 0.75};

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// EXTERNAL ENB PARAMETERS, DO NOT MODIFY
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

// x = generic timer in range 0..1, period of 16777216 ms (4.6 hours),
// y = average fps, w = frame time elapsed (in seconds)
float4	Timer;

//x = Width, y = 1/Width, z = aspect, w = 1/aspect, aspect is Width/Height
float4	ScreenSize;

// changes in range 0..1, 0 means full quality, 1 lowest dynamic quality
// (0.33, 0.66 are limits for quality levels)
float	AdaptiveQuality;

// x = current weather index, y = outgoing weather index, z = weather
// transition, w = time of the day in 24 standart hours. Weather index is value
// from weather ini file, for example WEATHER002 means index==2, but index==0
// means that weather not captured.
float4	Weather;

//x = dawn, y = sunrise, z = day, w = sunset. Interpolators range from 0..1
float4	TimeOfDay1;

//x = dusk, y = night. Interpolators range from 0..1
float4	TimeOfDay2;

//changes in range 0..1, 0 means that night time, 1 - day time
float	ENightDayFactor;

//changes 0 or 1. 0 means that exterior, 1 - interior
float	EInteriorFactor;

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// EXTERNAL ENB DEBUGGING PARAMETERS
// FOR SHADER PROGRAMERS, DO NOT MODIFY
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// keyboard controlled temporary variables. Press and hold key 1,2,3...8
// together with PageUp or PageDown to modify. By default all set to 1.0
float4	tempF1; //0,1,2,3
float4	tempF2; //5,6,7,8
float4	tempF3; //9,0

// xy = cursor position in range 0..1 of screen;
// z = is shader editor window active;
// w = mouse buttons with values 0..7 as follows:
//    0 = none
//    1 = left
//    2 = right
//    3 = left+right
//    4 = middle
//    5 = left+middle
//    6 = right+middle
//    7 = left+right+middle (or rather cat is sitting on your mouse)
float4	tempInfo1;

// xy = cursor position of previous left mouse button click
// zw = cursor position of previous right mouse button click
float4	tempInfo2;

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// MOD PARAMETERS, DO NOT MODIFY
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
Texture2D			TextureOriginal; //color R10B10G10A2 32 bit ldr format
Texture2D			TextureColor; // color which is output of previous technique
														// (except when drawed to temporary render target),
														// R10B10G10A2 32 bit ldr format
Texture2D			TextureDepth; //scene depth R32F 32 bit hdr format

// temporary textures which can be set as render target for techniques via
// annotations like <string RenderTarget="RenderTargetRGBA32";>
Texture2D			RenderTargetRGBA32; //R8G8B8A8 32 bit ldr format
Texture2D			RenderTargetRGBA64; //R16B16G16A16 64 bit ldr format
Texture2D			RenderTargetRGBA64F; //R16B16G16A16F 64 bit hdr format
Texture2D			RenderTargetR16F; //R16F 16 bit hdr format with red channel only
Texture2D			RenderTargetR32F; //R32F 32 bit hdr format with red channel only
Texture2D			RenderTargetRGB32F; //32 bit hdr format without alpha

SamplerState		Sampler0
{
	Filter = MIN_MAG_MIP_POINT;//MIN_MAG_MIP_LINEAR;
	AddressU = Clamp;
	AddressV = Clamp;
};
SamplerState		Sampler1
{
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = Clamp;
	AddressV = Clamp;
};

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// DATA STRUCTURE
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
struct VS_INPUT_POST
{
	float3 pos		: POSITION;
	float2 txcoord	: TEXCOORD0;
};
struct VS_OUTPUT_POST
{
	float4 pos		: SV_POSITION;
	float2 txcoord0	: TEXCOORD0;
};

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// SHADER CODE INCLUDES
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

#include "Shaders/enbeffectpostpass/LumaSharpen.fxh"
#include "Shaders/enbeffectpostpass/LensCA.fxh"

#if(ACTIVATE_VIGNETTE == 1)
#include "Shaders/enbeffectpostpass/Vignette.fxh"
#endif

#include "Shaders/enbeffectpostpass/ToneMap.fxh"

#if(CUSTOM_MODE == 0 && ACTIVATE_GRADING == 1)
#include "Shaders/enbeffectpostpass/Curves.fxh"
#include "Shaders/enbeffectpostpass/Vibrance.fxh"
#include "Shaders/enbeffectpostpass/ColorWheel.fxh"
#include "Shaders/enbeffectpostpass/Monochrome.fxh"
#include "Shaders/enbeffectpostpass/LUTOverlay.fxh"
#include "Shaders/enbeffectpostpass/Gradient.fxh"
#endif

#if(CUSTOM_MODE == 1 || CUSTOM_MODE == 2 || ACTIVATE_GRADING == 0)
#define ENABLE_GRADE false
#define Enable_HSLwidget false
#define HUEcolor0 float3(0.75, 0.25, 0.25)
#define HUEcolor1 float3(0.75, 0.50, 0.25)
#define HUEcolor2 float3(0.75, 0.75, 0.25)
#define HUEcolor3 float3(0.25, 0.75, 0.25)
#define HUEcolor4 float3(0.25, 0.75, 0.75)
#define HUEcolor5 float3(0.25, 0.25, 0.75)
#define HUEcolor6 float3(0.50, 0.25, 0.75)
#define HUEcolor7 float3(0.75, 0.25, 0.75)
#endif
#include "Shaders/enbeffectpostpass/HSL.fxh"
#if(ACTIVATE_SCOPES == 1 && CUSTOM_MODE == 0 && ACTIVATE_GRADING == 1)
#include "Shaders/enbeffectpostpass/Scopes.fxh"
#endif


#if(ACTIVATE_GRAIN == 1)
#include "Shaders/enbeffectpostpass/FilmGrain.fxh"
#endif

#include "Shaders/enbeffectpostpass/3DLUT.fxh"

#if(CUSTOM_MODE == 0)
#include "Shaders/enbeffectpostpass/Sepia.fxh"
#include "Shaders/enbeffectpostpass/Levels.fxh"
#endif

#include "Shaders/enbeffectpostpass/Aspect.fxh"

#include "Shaders/enbeffectpostpass/Dither.fxh"

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// HELPER IMAGE
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
Texture2D  HelperTex  <string ResourceName = "Textures/Helper/help.jpg";>;
#define help_transparency 0.75


//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// VERTEX SHADER
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

VS_OUTPUT_POST	VS_PostProcess(VS_INPUT_POST IN)
{
	VS_OUTPUT_POST	OUT;
	float4	pos;
	pos.xyz=IN.pos.xyz;
	pos.w=1.0;
	OUT.pos=pos;
	OUT.txcoord0.xy=IN.txcoord.xy;
	return OUT;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// PIXEL SHADERS
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

////// LENS PASS
  #if(ACTIVATE_CHROMA == 1)
float4 PS_CA(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
  float4 res = TextureColor.Sample(Sampler0, IN.txcoord0.xy);
  if(ENABLE_LENS == false) return res;
  float2 FishEyeRcoord, FishEyeGcoord, FishEyeBcoord;
  FishEyePass(IN.txcoord0.xy, FishEyeRcoord, FishEyeGcoord, FishEyeBcoord);
  return float4( LensCA(FishEyeRcoord).r, LensCA(FishEyeGcoord).g, LensCA(FishEyeBcoord).b, 1.0); //24 taps
}
#endif



#if(ACTIVATE_GRADING == 1)
static ColorWheelStruct ColorWheelData = {
    ENABLE_CW, CW_Weight,
    CW_lowrange, CW_highrange, CW_overlap,
    CW_Shadow, CW_Midtone, CW_Highlight, CW_Lift, CW_Gamma, CW_Gain, CW_Offset
};
#endif

////// SHARED PASS
float4	PS_Shared(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
		// LUMASHARPEN
    float4 						res 		= float4(LumaSharpenPass(IN.txcoord0.xy), 1.0);


		// VIGNETTE
    #if(ACTIVATE_VIGNETTE == 1)
		if(ENABLE_LENS == 1)   res     = VignettePass(res,IN.txcoord0.xy);
    #endif

    #if(CUSTOM_MODE == 0 && ACTIVATE_GRADING == 1)
    // LUT Overlay
    if(Scope_Enable==false && SHOW_LUT==true) res.rgb = ShowLut(res.rgb, IN.txcoord0.xy);
    #endif

    #if(ACTIVATE_CHROMA == 1 && CUSTOM_MODE == 0)
    if(ENABLE_FILTER && ENABLE_LENS)res 		= LensFilterPass(res);
    #endif

		// TONEMAPPING
		res 		= TonemapPass(res);

    #if(CUSTOM_MODE == 2)
    res.rgb  = LUTFunc1k(res.rgb);
    #endif

    #if(CUSTOM_MODE == 0)
    // Negative Stock Grain Stage
    #if(ACTIVATE_GRAIN == 1)
    #if(ACTIVATE_GRADING == 1)
    if(SHOW_LUT==false && grainamtnegative >= 0)  res.xyz = GrainPassNegative(IN.txcoord0.xy, res.xyz);
    #else
    if(grainamtnegative >= 0)  res.xyz = GrainPassNegative(IN.txcoord0.xy, res.xyz);
    #endif
    #endif

    // Film Negative
    res.rgb  = LUTFuncNeg(res.rgb);
    #endif

    #if(CUSTOM_MODE == 0)
    #if(ACTIVATE_GRADING == 1)
		// MANUAL COLOR GRADING
		if(ENABLE_GRADE) { res.rgb = LUTFuncPre(res.rgb);
                       res = CurvesPassLuma(res);
                       res = CurvesPassChroma(res);
                       res = VibrancePass(res);
                       if(ENABLE_HSL && BW_MODE == 1)res.rgb = HSLShift(res.rgb);
                       if(BW_MODE)res = MonochromePass(res);
                       res.rgb = lerp(res.rgb, ColorWheel(res.rgb, ColorWheelData), ColorWheelData.Weight * ColorWheelData.State);
                       if(ENABLE_HSL && BW_MODE == 0)res.rgb = HSLShift(res.rgb);
                       res.rgb = LUTFuncPost(res.rgb);
     if(ENABLE_GRADIENT) res.rgb = GradientPass(res.rgb, IN.txcoord0.xy);}
    res  = LevelsPass(res);
    #endif
    #endif

    // Print Stock Grain Stage
    #if(ACTIVATE_GRAIN == 1)
    #if(CUSTOM_MODE == 0)
    #if(ACTIVATE_GRADING == 1)
		if(SHOW_LUT==false && grainamtprint >= 0)  res.xyz = GrainPassPrint(IN.txcoord0.xy, res.xyz);
    #else
    if(grainamtprint >= 0)  res.xyz = GrainPassPrint(IN.txcoord0.xy, res.xyz);
    #endif
    #else
    if(grainamtprint >= 0)  res.xyz = GrainPassPrint(IN.txcoord0.xy, res.xyz);
    #endif
    #endif

    #if(CUSTOM_MODE == 0)
    // Film Print
	  res.rgb  = LUTFuncPrint(res.rgb);

    #if(ACTIVATE_GRADING == 1)
    // Levels Pass 2
    res  = LevelsPass2(res);
    #endif

    if(ENABLE_AGE)    {if(AGE_MODE == 1) res = SepiaPass(res);
                      if(AGE_MODE == 2) res = MagentaPass(res);}
    #endif

    #if(CUSTOM_MODE == 1)
    res.rgb  = LUTFuncLookPack(res.rgb);
    #endif

    #if(CUSTOM_MODE == 0 && ACTIVATE_GRADING == 1)
		// DITHERING
											if(SHOW_LUT==false)res.rgb = msDither(res.rgb,  IN.txcoord0.xy);
    #else
                      res.rgb = msDither(res.rgb,  IN.txcoord0.xy);
    #endif

		// LETTERBOX/PILLARBOX
    #if(CUSTOM_MODE == 0 && ACTIVATE_GRADING == 1)
    if(SHOW_LUT==false && Scope_Enable==false && USE_BORDER==true)   	res     = BorderPass(res, IN.txcoord0.xy);
    #else
    if(USE_BORDER)   	res     = BorderPass(res, IN.txcoord0.xy);
    #endif

    if(SHOW_HELPER) res.rgb = lerp(res, HelperTex.Sample(Sampler1, IN.txcoord0).rgb, help_transparency);


	return res;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// TECHNIQUES
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

#if(ACTIVATE_CHROMA == 1)
///// LENS PASS
technique11 KITCHEN <string UIName=ENB_NAME;>
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_CA()));
  }
}

///// SHARED PASS
technique11 KITCHEN1
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Shared()));
  }
  #if(ACTIVATE_SCOPES == 0 && ACTIVATE_GRADING == 1)/*
  pass p1 {
    SetVertexShader(CompileShader(vs_5_0, VS_Widget()));
    SetPixelShader(CompileShader(ps_5_0, PS_Widget()));
  }*/
  #endif

}

#if(ACTIVATE_SCOPES == 1 && CUSTOM_MODE == 0 && ACTIVATE_GRADING == 1)
//alpha channel required
    technique11 KITCHEN2 <string RenderTarget="RenderTargetRGBA32";>
{
    pass p0 {
        SetVertexShader(CompileShader(vs_5_0, VS_Waveform()));
        SetPixelShader(CompileShader(ps_5_0, PS_Waveform()));
    }
    pass p1 {
        SetVertexShader(CompileShader(vs_5_0, VS_VectorScope()));
        SetPixelShader(CompileShader(ps_5_0, PS_VectorScope()));
    }
}

//alpha channel required
technique11 KITCHEN3 <string RenderTarget="RenderTargetRGBA64";>
{
    pass p0 {
        SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
        SetPixelShader(CompileShader(ps_5_0, PS_Passthrough()));
    }
    pass p1  {
        SetVertexShader(CompileShader(vs_5_0, VS_Histogram()));
        SetPixelShader(CompileShader(ps_5_0, PS_Histogram()));
    }
    pass p2  {
        SetVertexShader(CompileShader(vs_5_0, VS_VectorScopeTex()));
        SetPixelShader(CompileShader(ps_5_0, PS_VectorScopeTex()));
    }
}

technique11 KITCHEN4
{
    pass p0 {
        SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
        SetPixelShader(CompileShader(ps_5_0, PS_Blank()));
    }
    pass p1 {
        SetVertexShader(CompileShader(vs_5_0, VS_Scene()));
        SetPixelShader(CompileShader(ps_5_0, PS_Scene()));
    }
    pass p2 {
        SetVertexShader(CompileShader(vs_5_0, VS_Scope_Histogram()));
        SetPixelShader(CompileShader(ps_5_0, PS_Scope_Histogram()));
    }
    pass p3 {
        SetVertexShader(CompileShader(vs_5_0, VS_Scope_Waveform()));
        SetPixelShader(CompileShader(ps_5_0, PS_Scope_Waveform()));
    }
    pass p4 {
        SetVertexShader(CompileShader(vs_5_0, VS_Scope_Vector()));
        SetPixelShader(CompileShader(ps_5_0, PS_Scope_Vector()));
    }
    pass p5 {
        SetVertexShader(CompileShader(vs_5_0, VS_Scope_MemoryColor()));
        SetPixelShader(CompileShader(ps_5_0, PS_Scope_MemoryColor()));
    }
    pass p6 {
        SetVertexShader(CompileShader(vs_5_0, VS_Scope_Minimap()));
        SetPixelShader(CompileShader(ps_5_0, PS_Scope_Minimap()));
    }/*
      pass p7 {
        SetVertexShader(CompileShader(vs_5_0, VS_Widget()));
        SetPixelShader(CompileShader(ps_5_0, PS_Widget()));
      }*/
}
#endif
#else
technique11 KITCHEN <string UIName=ENB_NAME;>
{
  pass p0  {
    SetVertexShader(CompileShader(vs_5_0, VS_PostProcess()));
    SetPixelShader(CompileShader(ps_5_0, PS_Shared()));
  }
  #if(ACTIVATE_SCOPES == 0 && ACTIVATE_GRADING == 1)/*
  pass p1 {
    SetVertexShader(CompileShader(vs_5_0, VS_Widget()));
    SetPixelShader(CompileShader(ps_5_0, PS_Widget()));
  }*/
  #endif
}

#if(ACTIVATE_SCOPES == 1 && CUSTOM_MODE == 0 && ACTIVATE_GRADING == 1)
//alpha channel required
    technique11 KITCHEN1 <string RenderTarget="RenderTargetRGBA32";>
{
    pass p0 {
        SetVertexShader(CompileShader(vs_5_0, VS_Waveform()));
        SetPixelShader(CompileShader(ps_5_0, PS_Waveform()));
    }
    pass p1 {
        SetVertexShader(CompileShader(vs_5_0, VS_VectorScope()));
        SetPixelShader(CompileShader(ps_5_0, PS_VectorScope()));
    }
}

//alpha channel required
technique11 KITCHEN2 <string RenderTarget="RenderTargetRGBA64";>
{
    pass p0 {
        SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
        SetPixelShader(CompileShader(ps_5_0, PS_Passthrough()));
    }
    pass p1  {
        SetVertexShader(CompileShader(vs_5_0, VS_Histogram()));
        SetPixelShader(CompileShader(ps_5_0, PS_Histogram()));
    }
    pass p2  {
        SetVertexShader(CompileShader(vs_5_0, VS_VectorScopeTex()));
        SetPixelShader(CompileShader(ps_5_0, PS_VectorScopeTex()));
    }
}

technique11 KITCHEN3
{
    pass p0 {
        SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
        SetPixelShader(CompileShader(ps_5_0, PS_Blank()));
    }
    pass p1 {
        SetVertexShader(CompileShader(vs_5_0, VS_Scene()));
        SetPixelShader(CompileShader(ps_5_0, PS_Scene()));
    }
    pass p2 {
        SetVertexShader(CompileShader(vs_5_0, VS_Scope_Histogram()));
        SetPixelShader(CompileShader(ps_5_0, PS_Scope_Histogram()));
    }
    pass p3 {
        SetVertexShader(CompileShader(vs_5_0, VS_Scope_Waveform()));
        SetPixelShader(CompileShader(ps_5_0, PS_Scope_Waveform()));
    }
    pass p4 {
        SetVertexShader(CompileShader(vs_5_0, VS_Scope_Vector()));
        SetPixelShader(CompileShader(ps_5_0, PS_Scope_Vector()));
    }
    pass p5 {
        SetVertexShader(CompileShader(vs_5_0, VS_Scope_MemoryColor()));
        SetPixelShader(CompileShader(ps_5_0, PS_Scope_MemoryColor()));
    }
    pass p6 {
        SetVertexShader(CompileShader(vs_5_0, VS_Scope_Minimap()));
        SetPixelShader(CompileShader(ps_5_0, PS_Scope_Minimap()));
    }/*
    pass p7 {
      SetVertexShader(CompileShader(vs_5_0, VS_Widget()));
      SetPixelShader(CompileShader(ps_5_0, PS_Widget()));
    }*/
}
#endif
#endif
