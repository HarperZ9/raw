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

UI_FileHeaderLong(">>>         PICHO ENB by HaeVakSa         <<<",
				  " ")

int SharpBlur <string UIName=">>>                SHARP & BLUR                <<<";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
  bool ENABLE_BLURRING <
    string UIName = "Enable Blurring";
  > = {false};
  bool ENABLE_SHARPENING <
    string UIName = "Enable Sharpening";
  > = {false};
  bool ENABLE_DEPTHSHARP <
    string UIName = "Enable Depth Sharpening";
  > = {false};
  bool ENABLE_LUMA <
    string UIName = "Enable Luma Sharpening";
  > = {false};
  bool VISUALIZE_SHARP <
  
    string UIName = "Visualize Sharpening";
  > = {false};
   
int Line1 <string UIName="_________________________________________";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
  float EBlurAmount <
    string UIName="Blur: amount";         string UIWidget="spinner";  float UIMin=0.0;  float UIMax=1.0;
  > = {1.0};
  float EBlurRange <
    string UIName="Blur: range";          string UIWidget="spinner";  float UIMin=0.0;  float UIMax=2.0;
  > = {1.0};
  float ESharpAmount <
    string UIName="Sharp: amount";        string UIWidget="spinner";  float UIMin=0.0;  float UIMax=4.0;
  > = {1.0};
  float ESharpRange <
    string UIName="Sharp: range";         string UIWidget="spinner";  float UIMin=0.0;  float UIMax=2.0;
  > = {1.0};
  int fSharpFDepth <
    string UIName="Sharp: From Depth";    string UIWidget="Spinner";  int UIMin=0.0;  int UIMax=100000.0;
  > = {300.0};
  
UI_WHITESPACE(42)  
int LineC <string UIName="____________________________________________-___";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_CAS <
        string UIName = "Enable CAS Sharp by rhellct ";
    > = {false};
float ECASSharpAmount <
  string UIName="CAS Sharp: amount";        string UIWidget="spinner";  float UIMin=0.0;  float UIMax=1.0;
> = {0.0};
bool EnableClamp < string UIName = "Enable Clamping"; > = {false};
float CASSharpClamp <
  string UIName="CAS Sharp: clamp amount";        string UIWidget="spinner";  float UIMin=0.0;  float UIMax=1.0;
> = {0.07};

UI_WHITESPACE(5)  
int Line2 <string UIName="_______________________________________________";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
int Letterbox <string UIName=">>>                       LETTERBOX             <<<";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
	bool ENABLE_LETTERBOX <
        string UIName = "Enable Letterbox Bars";
    > = {false};
    float fLetterboxBarHeight <
        string UIName="Letterbox: Height in % Height";   string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=0.5;  float UIStep=0.001;
    > = {0.12};
	 float fLetterboxBarWidth <
        string UIName="Letterbox: Height in % Width";   string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=0.5;  float UIStep=0.001;
    > = {0.12};
	
float4  Border_Color		<string UIName="Lettebox Color";				string UIWidget="Color";	 > = {0,0,0,1};
	
//UI_WHITESPACE(44)  
int LineF <string UIName="________________________________________________";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
int FishEye <string UIName=">>>                     Fish Eye Camera           <<<";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};	
bool ENABLE_Fish < string UIName = "Enable Fish Eye Camera"; > = {false};
float LCAStrength <   string UIName="Lens Aberration";    string UIWidget="spinner";    float UIMin=0.0;    float UIMax=1.0;    float UIStep=0.1;> = {0.0};
float fFisheyeZoom <   string UIName="Lens Zoom";    string UIWidget="spinner";    float UIMin=0.5;    float UIMax=0.55;    float UIStep=0.001;> = {0.5};
float fFisheyeDistortion <   string UIName="Lens Distortion";    string UIWidget="spinner";    float UIMin=-1.0;    float UIMax=0.2;    float UIStep=0.01;> = {0.02};
	
UI_WHITESPACE(2)  

//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 1
int Line3 <string UIName="___________________________________________";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
int DeFog <string UIName=">>>                    Simple Tonemap           <<<";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};			    
bool ENABLE_SimpleTone <string UIName = "Enable Simple Tonemap Adjust";> = {false};   
//UI_WHITESPACE(9)  

int Brightness <string UIName="BRIGHTNESS";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
float GuiBlack		<string UIName="Brightness Day";string UIWidget="spinner";float UIMin=-10.0;float UIMax=7.0;float UIStep=0.001;> = {0.0}; 
float GuiBlackNightA		<string UIName="Brightness Night";string UIWidget="spinner";float UIMin=-10.0;float UIMax=7.0;float UIStep=0.001;> = {0.0}; 
float GuiBlackInteriorA		<string UIName="Brightness Interior";string UIWidget="spinner";float UIMin=-10.0;float UIMax=7.0;float UIStep=0.001;> = {0.0}; 
UI_WHITESPACE(16)
int Expose <string UIName="EXPOSURE";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};	
float TonemapExposureDawn <    string UIName="Exposure - Dawn ";    string UIWidget="spinner";    float UIMin=-2.0;    float UIMax=3.0;	float UIStep=0.001;> = {0.0};
float TonemapExposureSunrise <    string UIName="Exposure - Sunrise";    string UIWidget="spinner";    float UIMin=-2.0;    float UIMax=1.0;	float UIStep=0.001;> = {0.0};
float TonemapExposureDay <   string UIName="Exposure - Day";    string UIWidget="spinner";    float UIMin=-2.0;    float UIMax=1.0;	float UIStep=0.001;> = {0.0};
float TonemapExposureSunset <    string UIName="Exposure - Sunset";    string UIWidget="spinner";    float UIMin=-2.0;    float UIMax=1.0;	float UIStep=0.001;> = {0.0};
float TonemapExposureDusk <    string UIName="Exposure - Dusk";    string UIWidget="spinner";    float UIMin=-2.0;    float UIMax=3.0;	float UIStep=0.001;> = {0.0};
float TonemapExposureNight <string UIName="Exposure - Night";    string UIWidget="spinner";    float UIMin=-2.0;    float UIMax=2.0;	float UIStep=0.001;> = {0.0};
float TonemapExposure <    string UIName="Exposure - Interior Day";    string UIWidget="spinner";    float UIMin=-2.0;    float UIMax=1.0;	float UIStep=0.001;> = {0.0};
float TonemapExposureINight <    string UIName="Exposure - Interior Night";    string UIWidget="spinner";    float UIMin=-2.0;    float UIMax=1.0;	float UIStep=0.001;> = {0.0};
UI_WHITESPACE(11)
int GGAM <string UIName="GAMMA";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};

float TonemapGammaDawn <string UIName="Gamma - Dawn";string UIWidget="spinner";float UIMin=0.1;float UIMax=2.0;float UIStep=0.001;> = {1.0};
float TonemapGammaSunrise <string UIName="Gamma - Sunrise";string UIWidget="spinner";float UIMin=0.1;float UIMax=2.0;float UIStep=0.001;> = {1.0};

float TonemapGammaD <string UIName="Gamma - Day";string UIWidget="spinner";float UIMin=0.1;float UIMax=2.0;float UIStep=0.001;> = {1.0};

float TonemapGammaSunset <string UIName="Gamma - Sunset";string UIWidget="spinner";float UIMin=0.1;float UIMax=2.0;float UIStep=0.001;> = {1.0};
float TonemapGammaDusk <string UIName="Gamma - Dusk";string UIWidget="spinner";float UIMin=0.1;float UIMax=2.0;float UIStep=0.001;> = {1.0};

float TonemapGammaN <string UIName="Gamma - Night";string UIWidget="spinner";float UIMin=0.1;float UIMax=2.0;float UIStep=0.001;> = {1.0};

float TonemapGammaI <string UIName="Gamma - Interior Day";string UIWidget="spinner";float UIMin=0.1;float UIMax=2.0;float UIStep=0.001;> = {1.0};
float TonemapGammaINight <string UIName="Gamma - Interior Night";string UIWidget="spinner";float UIMin=0.1;float UIMax=2.0;float UIStep=0.001;> = {1.0};

UI_WHITESPACE(10)
int BLAKCPPPOIN <string UIName="BLACK POINT";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
float GuiBlackDay		<string UIName="Black point - Day";string UIWidget="spinner";float UIMin=-48.0;float UIMax=48.0;float UIStep=0.001;> = {0.0}; 
float GuiBlackNight		<string UIName="Black point - Night";string UIWidget="spinner";float UIMin=-48.0;float UIMax=48.0;float UIStep=0.001;> = {0.0}; 
float GuiBlackInterior		<string UIName="Black point -  Interior Day";string UIWidget="spinner";float UIMin=-50.0;float UIMax=50.0;float UIStep=0.001;> = {0.0}; 
float GuiBlackInteriorN		<string UIName="Black point -  Interior Night";string UIWidget="spinner";float UIMin=-50.0;float UIMax=50.0;float UIStep=0.001;> = {0.0}; 


UI_WHITESPACE(18)
int Saue <string UIName="SATURATION";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
float SaturationDay < string UIName="Saturation - Day";    string UIWidget="spinner";    float UIMin=-1.0;    float UIMax=2.0;	float UIStep=0.001;> = {0.0};
float SaturationNight <    string UIName="Saturation - Night";    string UIWidget="spinner";    float UIMin=-1.0;    float UIMax=2.0;	float UIStep=0.001;> = {0.0};
float Saturation <    string UIName="Saturation - Interior Day";    string UIWidget="spinner";    float UIMin=-1.0;    float UIMax=2.0;	float UIStep=0.001;> = {0.0};
float SaturationIN <    string UIName="Saturation - Interior Night";    string UIWidget="spinner";    float UIMin=-1.0;    float UIMax=2.0;	float UIStep=0.001;> = {0.0};

UI_WHITESPACE(8)
int TEMPERA <string UIName="TEMPERATURE";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
float TonemapDefog <string UIName="Temperature  Exterior Day";    string UIWidget="spinner";    float UIMin=-0.03;    float UIMax=0.03;	float UIStep=0.005;> = {0.0};
float TonemapDefogN <string UIName="Temperature  Exterior Night";    string UIWidget="spinner";    float UIMin=-0.03;    float UIMax=0.03;	float UIStep=0.005;> = {0.0};
float TonemapDefogI <string UIName="Temperature Interior Day";    string UIWidget="spinner";    float UIMin=-0.03;    float UIMax=0.03;	float UIStep=0.005;> = {0.0};
float TonemapDefogIN <string UIName="Temperature Interior Night";    string UIWidget="spinner";    float UIMin=-0.03;    float UIMax=0.03;	float UIStep=0.005;> = {0.0};
UI_WHITESPACE(7)
int Line31 <string UIName="___________________________________________-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
int DeFogg <string UIName=">>>                    SHADOW CONTROL          <<<";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};		
bool ENABLE_ENHANCER <string UIName = "Enable SHADOW CONTROL";> = {false};   
int THIUI <string UIName="HIGHLIGHT";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
float	LightnessD <		string UIName="Highlights Day";							string UIWidget="Spinner";	float UIMin=-1.0;	float UIMax=1.0;	float UIStep=0.001;	> = {0.0};
float	LightnessN <		string UIName="Highlights Night";							string UIWidget="Spinner";	float UIMin=-1.0;	float UIMax=1.0;	float UIStep=0.001;	> = {0.0};
float	LightnessI <		string UIName="Highlights Interior";						string UIWidget="Spinner";	float UIMin=-1.0;	float UIMax=1.0;	float UIStep=0.001;	> = {0.0};
//UI_WHITESPACE(55)

//float	ECCOutBlack_Day <	string UIName="Out black Day";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=1.0;> = {0.0};
//float	ECCOutWhite_Day <	string UIName="Out white Day";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=1.0;> = {1.0};
//float	ECCOutBlack_Night<	string UIName="Out black Night";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=1.0;> = {0.0};
//float	ECCOutWhite_Night<	string UIName="Out white Night";		float UIMax=1.0;> = {1.0};
//float	ECCOutBlack_Interior <	string UIName="Out black Interior";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=1.0;> = {0.0};
//float	ECCOutWhite_Interior <	string UIName="Out white Interior";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=1.0;> = {1.0};
UI_WHITESPACE(52)
int TENHANCER <string UIName="ENHANCER";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};

//float	ECCInBlack_Day <	string UIName="In black Day"; string UIWidget="Spinner"; float UIMin=0.0; float UIMax=1.0; > = {0.0};
float	ECCInWhite_Day <	string UIName="ENHANCER Day";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=1.0; float UIStep=0.001;> = {0.0};
//float	ECCInBlack_Night<	string UIName="In black Night";		float UIMin=0.0;	float UIMax=1.0;> = {0.0};
float	ECCInWhite_Night<	string UIName="ENHANCER Night";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=1.0; float UIStep=0.001;> = {0.0};

//float	ECCInBlack_Interior <	string UIName="In black Interior";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=1.0;> = {0.0};
float	ECCInWhite_Interior <	string UIName="ENHNACER Interior";		float UIMin=0.0;	float UIMax=1.0; float UIStep=0.001;> = {0.0};
UI_WHITESPACE(56)
int TSHADOWUI <string UIName="SHADOW";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
float	ShadowsD <			string UIName="Shadows Day";								string UIWidget="Spinner";	float UIMin=-1.0;	float UIMax=1.0;	float UIStep=0.001;	> = {0.0};
float	ShadowsN <			string UIName="Shadows Night";								string UIWidget="Spinner";	float UIMin=-1.0;	float UIMax=1.0;	float UIStep=0.001;	> = {0.0};
float	ShadowsI <			string UIName="Shadows Interior";							string UIWidget="Spinner";	float UIMin=-1.0;	float UIMax=1.0;	float UIStep=0.001;	> = {0.0};
UI_WHITESPACE(51)
float	ECCDesaturateShadows_Day <	string UIName="Desaturate shadows Day";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=1.0;> = {0.0};
float	ECCDesaturateShadows_Night<	string UIName="Desaturate shadows Night";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=1.0;> = {0.0};
float	ECCDesaturateShadows_Interior<	string UIName="Desaturate shadows Interior";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=1.0;> = {0.0};

//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 2
//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
//UI ADDON
UI_WHITESPACE(40)
int Line41 <string UIName="___________________________________________5";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_SP <string UIName = ">>>               Sepia Tone               <<<";> = {false};	
float3 ToneSepia   <string UIName="Sepia Color Tone Day";  string UIWidget="color"; > = (1.00, 1.00, 1.1);
float3 ToneSepiaN   <string UIName="Sepia Color Tone Night";  string UIWidget="color"; > = (1.00, 1.00, 1.1);
float3 ToneSepiaID   <string UIName="Sepia Color Tone Interior Day";  string UIWidget="color"; > = (1.00, 1.00, 1.1);
float3 ToneSepiaIN   <string UIName="Sepia Color Tone Interior Night";  string UIWidget="color"; > = (1.00, 1.00, 1.1);
float  BaseCurveE  <string UIName="Base Curve Exterior";string UIWidget="spinner";float UIMin=0.1;float UIMax=7.0;float UIStep=0.1;> = {0.5}; 
float  LinearizationE  <string UIName="Linearization Exterior";string UIWidget="spinner";float UIMin=0.25;float UIMax=0.6;float UIStep=0.01;> = {0.5}; 
float  BaseCurveI  <string UIName="Base Curve Interior";string UIWidget="spinner";float UIMin=0.1;float UIMax=7.0;float UIStep=0.1;> = {0.5}; 
float  LinearizationI  <string UIName="Linearization Interior";string UIWidget="spinner";float UIMin=0.25;float UIMax=0.6;float UIStep=0.01;> = {0.5}; 
UI_WHITESPACE(33)
int Line45 <string UIName="Sepia Tone - Vibrance";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};

//bool ENABLE_VV <string UIName = "";> = {false};	
float VibranceD <    string UIName="Vibrance - Day";    string UIWidget="spinner";    float UIMin=-1.0;    float UIMax=1.0;	float UIStep=0.1;> = {0.1};
float3 VToneD   <string UIName="Vibrance Tone Day";  string UIWidget="color"; > = (0.00, 0.00, 1.1);
float VibranceN <    string UIName="Vibrance - Night";    string UIWidget="spinner";    float UIMin=-1.0;    float UIMax=1.0;	float UIStep=0.1;> = {0.1};
float3 VToneN   <string UIName="Vibrance Tone Night";  string UIWidget="color"; > = (0.00, 0.00, 1.1);
float VibranceI <    string UIName="Vibrance - Interior Day";    string UIWidget="spinner";    float UIMin=-1.0;    float UIMax=1.0;	float UIStep=0.1;> = {0.1};
float3 VToneID   <string UIName="Vibrance Tone Interior Day";  string UIWidget="color"; > = (0.00, 0.00, 1.1);
float VibranceIN <    string UIName="Vibrance - Interior Night";    string UIWidget="spinner";    float UIMin=-1.0;    float UIMax=1.0;	float UIStep=0.1;> = {0.1};
float3 VToneIN   <string UIName="Vibrance Tone Interior Night";  string UIWidget="color"; > = (0.00, 0.00, 1.1);

UI_WHITESPACE(13)
int Line5 <string UIName="___________________________________________5";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
int UIADDON <string UIName=">>>                    UI ADDON Funtion         <<<";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
int		UINumber 		< string UIName="UIAddon Selection"; string UIWidget="spinner"; int UIMin=0; int UIMax=10;> = {0};

UI_WHITESPACE(17)
int Line6 <string UIName="_________________________________________6";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
///HDR
int HDR <string UIName=">>>                            H D R                     <<<";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_HDR <string UIName = "Enable HDR";> = {false};	
float radius2 		< string UIName="radius"; string UIWidget="Spinner";float UIMin=0.0;float UIMax=8.00; float UIStep=0.01;> = {0.85};   
UI_WHITESPACE(23)
float HDRPower 		< string UIName="HDRPower"; string UIWidget="Spinner";float UIMin=0.0;float UIMax=8.00; float UIStep=0.01;> = {1.20};  
float HDRPowerNight 		< string UIName="HDRPower Night"; string UIWidget="Spinner";float UIMin=0.0;float UIMax=8.00; float UIStep=0.01;> = {1.20};    
float HDRPowerI 		< string UIName="HDRPower Interior"; string UIWidget="Spinner";float UIMin=0.0;float UIMax=8.00; float UIStep=0.01;> = {1.20};   

UI_WHITESPACE(19)
///CURVES
int Line7 <string UIName="_________________________________________7";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
int Curves <string UIName=">>>                      CURVES                       <<<";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_CURVES <string UIName = "Enable Curves";> = {false};
UI_WHITESPACE(28)
float Curves_contrastDay 	< string UIName="Curves_contrast Day"; string UIWidget="Spinner";float UIMin=-1.0;float UIMax=1.00; float UIStep=0.01;> = {0.30};   
float Curves_contrastNight 	< string UIName="Curves_contrast Night"; string UIWidget="Spinner";float UIMin=-1.0;float UIMax=1.00; float UIStep=0.01;> = {0.30};   
float Curves_contrastInterior 	< string UIName="Curves_contrast Interior"; string UIWidget="Spinner";float UIMin=-1.0;float UIMax=1.00; float UIStep=0.01;> = {0.30};   

	UI_WHITESPACE(20)
///REINHARD
int Line8 <string UIName="_________________________________________8";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
int Reinhard <string UIName=">>>                       REINHARD                 <<<";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_REINHARD <string UIName = "Enable Reinhard";> = {false};
float ReinhardLinearSlope 			< string UIName="ReinhardLinearSlope"; string UIWidget="Spinner";float UIMin=1.0;float UIMax=5.00; float UIStep=0.01;> = {1.05};   
float ReinhardLinearWhitepoint 		< string UIName="ReinhardLinearWhitepoint"; string UIWidget="Spinner";float UIMin=-100.0;float UIMax=100.00; float UIStep=0.01;> = {1.10};   
float ReinhardLinearPoint 			< string UIName="ReinhardLinearPoint"; string UIWidget="Spinner";float UIMin=-100.0;float UIMax=100.00; float UIStep=0.001;> = {0.001};   
UI_WHITESPACE(21)
///TECHNICOLOR
int Line9 <string UIName="_________________________________________9";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
int Technicolor <string UIName=">>>                 TECHNICOLOR                <<<";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_TECHNI <string UIName = "Enable Technicolor";> = {false};	
float Technicolor2_Red_Strength 		< string UIName="Technicolor2_Red_Strength"; string UIWidget="Spinner";float UIMin=0.05;float UIMax=1.00; float UIStep=0.01;> = {0.30};   
float Technicolor2_Green_Strength 		< string UIName="Technicolor2_Green_Strength"; string UIWidget="Spinner";float UIMin=0.05;float UIMax=1.00; float UIStep=0.01;> = {0.44};   
float Technicolor2_Blue_Strength 		< string UIName="Technicolor2_Blue_Strength"; string UIWidget="Spinner";float UIMin=0.05;float UIMax=1.00; float UIStep=0.01;> = {0.39};   
float Technicolor2_Brightness 			< string UIName="Technicolor2_Brightness"; string UIWidget="Spinner";float UIMin=0.5;float UIMax=1.5; float UIStep=0.01;> = {1.50};   
float Technicolor2_Saturation 			< string UIName="Technicolor2_Saturation"; string UIWidget="Spinner";float UIMin=0.0;float UIMax=1.50; float UIStep=0.01;> = {0.67};   
float Technicolor2_StrengthDawn 			< string UIName="Technicolor2_Strength Dawn"; string UIWidget="Spinner";float UIMin=0.0;float UIMax=1.00; float UIStep=0.01;> = {0.75};  
float Technicolor2_StrengthSunrise 			< string UIName="Technicolor2_Strength Sunrise"; string UIWidget="Spinner";float UIMin=0.0;float UIMax=1.00; float UIStep=0.01;> = {0.75};  
float Technicolor2_StrengthDay 			< string UIName="Technicolor2_Strength Day"; string UIWidget="Spinner";float UIMin=0.0;float UIMax=1.00; float UIStep=0.01;> = {0.75};  
float Technicolor2_StrengthSunset 			< string UIName="Technicolor2_Strength Sunset"; string UIWidget="Spinner";float UIMin=0.0;float UIMax=1.00; float UIStep=0.01;> = {0.75};  
float Technicolor2_StrengthDusk 			< string UIName="Technicolor2_Strength Dusk"; string UIWidget="Spinner";float UIMin=0.0;float UIMax=1.00; float UIStep=0.01;> = {0.75};  
float Technicolor2_StrengthNight 			< string UIName="Technicolor2_Strength Night"; string UIWidget="Spinner";float UIMin=0.0;float UIMax=1.00; float UIStep=0.01;> = {0.75};  
float Technicolor2_Strength 			< string UIName="Technicolor2_Strength Interior Day"; string UIWidget="Spinner";float UIMin=0.0;float UIMax=1.00; float UIStep=0.01;> = {0.75};   
float Technicolor2_StrengthINight 			< string UIName="Technicolor2_Strength Interior Night"; string UIWidget="Spinner";float UIMin=0.0;float UIMax=1.00; float UIStep=0.01;> = {0.75};   
UI_WHITESPACE(22)
int Line10 <string UIName="_________________________________________10";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
int DPX <string UIName=">>>                     DPX CINEON               <<<";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};	
bool ENABLE_DPX <string UIName = "Enable DPX";> = {false};	
float Red 				< string UIName="Red"; string UIWidget="Spinner";float UIMin=1.0;float UIMax=15.00; float UIStep=0.01;> = {10.0};   
float Green 			< string UIName="Green"; string UIWidget="Spinner";float UIMin=1.0;float UIMax=15.00; float UIStep=0.01;> = {10.0};   
float Blue 				< string UIName="Blue"; string UIWidget="Spinner";float UIMin=1.0;float UIMax=15.00; float UIStep=0.01;> = {10.0};   
float ColorGamma 		< string UIName="ColorGamma"; string UIWidget="Spinner";float UIMin=0.1;float UIMax=2.5; float UIStep=0.01;> = {2.5};   
float DPXSaturation 	< string UIName="DPXSaturation"; string UIWidget="Spinner";float UIMin=0.0;float UIMax=8.00; float UIStep=0.01;> = {2.60};   
float RedC 				< string UIName="RedC"; string UIWidget="Spinner";float UIMin=0.20;float UIMax=0.60; float UIStep=0.01;> = {0.55};   
float GreenC 			< string UIName="GreenC"; string UIWidget="Spinner";float UIMin=0.20;float UIMax=0.60; float UIStep=0.01;> = {0.52};   
float BlueC 			< string UIName="BlueC"; string UIWidget="Spinner";float UIMin=0.20;float UIMax=0.60; float UIStep=0.01;> = {0.55};   
float BlendDawn 			< string UIName="Blend Dawn"; string UIWidget="Spinner";float UIMin=0.0;float UIMax=1.0; float UIStep=0.01;> = {0.15}; 
float BlendSunrise 			< string UIName="Blend Sunrise"; string UIWidget="Spinner";float UIMin=0.0;float UIMax=1.0; float UIStep=0.01;> = {0.15}; 
float BlendDay 			    < string UIName="Blend Day"; string UIWidget="Spinner";float UIMin=0.0;float UIMax=1.0; float UIStep=0.01;> = {0.15}; 
float BlendSunset 			< string UIName="Blend Sunset"; string UIWidget="Spinner";float UIMin=0.0;float UIMax=1.0; float UIStep=0.01;> = {0.15}; 
float BlendDusk 			< string UIName="Blend Dusk"; string UIWidget="Spinner";float UIMin=0.0;float UIMax=1.0; float UIStep=0.01;> = {0.15};  
float BlendNight 		    < string UIName="Blend Night"; string UIWidget="Spinner";float UIMin=0.0;float UIMax=1.0; float UIStep=0.01;> = {0.15};
float Blend 			< string UIName="Blend Interior Day"; string UIWidget="Spinner";float UIMin=0.0;float UIMax=1.0; float UIStep=0.01;> = {0.15};   
float BlendINight 			< string UIName="Blend Interior Night"; string UIWidget="Spinner";float UIMin=0.0;float UIMax=1.0; float UIStep=0.01;> = {0.15};   

UI_WHITESPACE(26)
///LIFTGAMMAGAIN
int Line11 <string UIName="_________________________________________11";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
int LiftGammaGain <string UIName=">>>               LIFTGAMMAGAIN           <<<";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_LFG <string UIName = "Enable Lift Gamma Gain";> = {false};	
float3 RGB_Lift 		< string UIName="RGB_Lift"; string UIWidget="Spinner";float UIMin=0.0;float UIMax=2.0; float UIStep=0.001;> = {1.010, 1.005, 1.000};   
float3 RGB_Gamma 		< string UIName="RGB_Gamma"; string UIWidget="Spinner";float UIMin=0.0;float UIMax=2.0; float UIStep=0.001;> = {1.030, 1.025, 1.020};   
float3 RGB_Gain 		< string UIName="RGB_Gain"; string UIWidget="Spinner";float UIMin=0.0;float UIMax=2.0; float UIStep=0.001;> = {1.020, 1.015, 1.010};   
float GainIntensityDay 			< string UIName="Gain Multiplier Day"; string UIWidget="Spinner";float UIMin=0.80;float UIMax=1.20; float UIStep=0.01;> = {1.0};   
float GainIntensityNight 			< string UIName="Gain Multiplier Night"; string UIWidget="Spinner";float UIMin=0.80;float UIMax=1.20; float UIStep=0.01;> = {1.0};   
float GainIntensityInterior 			< string UIName="Gain Multiplier Interior Day"; string UIWidget="Spinner";float UIMin=0.80;float UIMax=1.20; float UIStep=0.01;> = {1.0};   
float GainIntensityInteriorNight 			< string UIName="Gain Multiplier Interior Night"; string UIWidget="Spinner";float UIMin=0.80;float UIMax=1.20; float UIStep=0.01;> = {1.0};   

UI_WHITESPACE(25)

///CA

int Line12 <string UIName="_________________________________________12";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
int ChromaticAberration <string UIName=">>>         CHROMATIC ABERRATION      <<<";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_CA <string UIName = "Enable C.A";> = {false};	
//float CAOffsetR 			< string UIName="C.A: Offset R"; string UIWidget="Spinner";float UIMin=-5.0;float UIMax=5.00; float UIStep=0.05;> = {0.5};   
//float CAOffsetG 			< string UIName="C.A: Offset G"; string UIWidget="Spinner";float UIMin=-5.0;float UIMax=5.00; float UIStep=0.05;> = {0.5};   
//float CAOffsetB 			< string UIName="C.A: Offset B"; string UIWidget="Spinner";float UIMin=-5.0;float UIMax=5.00; float UIStep=0.05;> = {0.5};

float  UICA_HighPass	<string UIName="HighPass";		      float UIMin= 0.0; float UIMax=2.0;  > = {0.0};
float  UICA_Amount   	<string UIName="Amount";		      float UIMin=-8.0; float UIMax=8.0;  > = {1.0};
float  UICA_MaxRadius	<string UIName="Max Radius";		      float UIMin= 1.0; float UIMax=3.0;  > = {1.0};
float  UICA_Curve   	<string UIName="Curve";			      float UIMin= 1.0; float UIMax=10.0; > = {1.5};
bool   UICA_Visualize	<string UIName="Show CA Area";							  > = {false};

///GRAIN	
UI_WHITESPACE(27)
int Line13 <string UIName="_________________________________________13";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
int Grain <string UIName=">>>                         GRAIN                     <<<";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};		
    bool ENABLE_GRAIN <
        string UIName = "Enable Grain";
    > = {false};
    bool VISUALIZE_GRAIN <
        string UIName = "Visualize Grain";
    > = {false};

    float fGrainIntensity <
        string UIName="Grain: Intensity";   string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=0.5;  float UIStep=0.001; /// Uistep defines the incremental values of the spinner
    > = {0.035};
    float fGrainSaturation <
        string UIName="Grain: Saturation";  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=0.5;  float UIStep=0.001;
    > = {0.0};
    float fGrainMotion <
        string UIName="Grain: Motion";      string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=0.2;  float UIStep=0.001;
    > = {0.2};

///VIGNETTE  
UI_WHITESPACE(29)  
int Line14 <string UIName="_________________________________________14";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
int Vignette <string UIName=">>>                      VIGNETTE                   <<<";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
    bool ENABLE_VIGNETTE <
        string UIName = "Enable Vignette";
    > = {false};
    float EVignetteCurve <
        string UIName="Vignette: Curve";        string UIWidget="Spinner";    float UIMin=0.0;
    > = {4.0};
    float EVignetteRadius <
        string UIName="Vignette: Radius";       string UIWidget="Spinner";    float UIMin=0.0;
    > = {1.4};
    float3 EVignetteColor <
        string UIName="Vignette: RGB Color";    string UIWidget="Color"; /// Uiwidget color is the one that give you the rectangular color preview
    > = {0.0, 0.0, 0.0};
	float EVignetteAmount <
        string UIName="Vignette: Amount Exterior";       string UIWidget="Spinner";    float UIMin=0.0;
    > = {1.0};
	 float EVignetteAmountINTER <
        string UIName="Vignette: Amount Interior";       string UIWidget="Spinner";    float UIMin=0.0;
    > = {1.0};
UI_WHITESPACE(30)  
///DITHER
int Line15 <string UIName="_________________________________________15";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
int Dither <string UIName=">>>                       DITHER                   <<<";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};	
bool ENABLE_DITHER <string UIName = "Enable Dither";> = {false};

    bool VISUALIZE_PATTERN <
        string UIName = "Visualize Dither Pattern";
    > = {false};
    int DITHER_METHOD <
        string UIName="Dither: Choose Method";   string UIWidget="Spinner";  int UIMin=1;  int UIMax=3;
    > = {3};	
	
UI_WHITESPACE(31)  	
//////  RESHADE EFFECTS GUI START HERE  ////////////// I use the original float names from ReShade 1.1 as seen in you files

int Line16 <string UIName="_________________________________________16";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
///HSLSHIFT
int Hslshift <string UIName=">>>                        HSLSHIFT                  <<<";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_HSL <string UIName = "Enable HSL SHIFT";> = {false};	

int HSLDAWN <string UIName="-----------DAWN";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};	
float3 HUEcolor0_Dawn      <string UIName="Dawn: Red";     string UIWidget="color"; > = {0.75, 0.25, 0.25};
float3 HUEcolor1_Dawn   <string UIName="Dawn: Orange";  string UIWidget="color"; > = {0.75, 0.50, 0.25};
float3 HUEcolor2_Dawn   <string UIName="Dawn: Yellow";  string UIWidget="color"; > = {0.75, 0.75, 0.25};
float3 HUEcolor3_Dawn    <string UIName="Dawn: Green";   string UIWidget="color"; > = {0.25, 0.75, 0.25};
float3 HUEcolor4_Dawn     <string UIName="Dawn: Cyan";    string UIWidget="color"; > = {0.25, 0.75, 0.75};
float3 HUEcolor5_Dawn     <string UIName="Dawn: Blue";    string UIWidget="color"; > = {0.25, 0.25, 0.75};
float3 HUEcolor6_Dawn   <string UIName="Dawn: Purple";  string UIWidget="color"; > = {0.50, 0.25, 0.75};
float3 HUEcolor7_Dawn  <string UIName="Dawn: Magenta"; string UIWidget="color"; > = {0.75, 0.25, 0.75};

int HSLSUNRISE <string UIName="-----------SUNRISE";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};	
float3 HUEcolor0_Sunrise      <string UIName="Sunrise: Red";     string UIWidget="color"; > = {0.75, 0.25, 0.25};
float3 HUEcolor1_Sunrise   <string UIName="Sunrise: Orange";  string UIWidget="color"; > = {0.75, 0.50, 0.25};
float3 HUEcolor2_Sunrise   <string UIName="Sunrise: Yellow";  string UIWidget="color"; > = {0.75, 0.75, 0.25};
float3 HUEcolor3_Sunrise    <string UIName="Sunrise: Green";   string UIWidget="color"; > = {0.25, 0.75, 0.25};
float3 HUEcolor4_Sunrise     <string UIName="Sunrise: Cyan";    string UIWidget="color"; > = {0.25, 0.75, 0.75};
float3 HUEcolor5_Sunrise     <string UIName="Sunrise: Blue";    string UIWidget="color"; > = {0.25, 0.25, 0.75};
float3 HUEcolor6_Sunrise   <string UIName="Sunrise: Purple";  string UIWidget="color"; > = {0.50, 0.25, 0.75};
float3 HUEcolor7_Sunrise  <string UIName="Sunrise: Magenta"; string UIWidget="color"; > = {0.75, 0.25, 0.75};

int HSLDAY <string UIName="-----------DAY";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};	
float3 HUEcolor0_Day      <string UIName="Day: Red";     string UIWidget="color"; > = {0.75, 0.25, 0.25};
float3 HUEcolor1_Day   <string UIName="Day: Orange";  string UIWidget="color"; > = {0.75, 0.50, 0.25};
float3 HUEcolor2_Day   <string UIName="Day: Yellow";  string UIWidget="color"; > = {0.75, 0.75, 0.25};
float3 HUEcolor3_Day    <string UIName="Day: Green";   string UIWidget="color"; > = {0.25, 0.75, 0.25};
float3 HUEcolor4_Day     <string UIName="Day: Cyan";    string UIWidget="color"; > = {0.25, 0.75, 0.75};
float3 HUEcolor5_Day     <string UIName="Day: Blue";    string UIWidget="color"; > = {0.25, 0.25, 0.75};
float3 HUEcolor6_Day   <string UIName="Day: Purple";  string UIWidget="color"; > = {0.50, 0.25, 0.75};
float3 HUEcolor7_Day  <string UIName="Day: Magenta"; string UIWidget="color"; > = {0.75, 0.25, 0.75};

int HSLSUNSET <string UIName="-----------SUNSET";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};	
float3 HUEcolor0_Sunset      <string UIName="Sunset: Red";     string UIWidget="color"; > = {0.75, 0.25, 0.25};
float3 HUEcolor1_Sunset   <string UIName="Sunset: Orange";  string UIWidget="color"; > = {0.75, 0.50, 0.25};
float3 HUEcolor2_Sunset   <string UIName="Sunset: Yellow";  string UIWidget="color"; > = {0.75, 0.75, 0.25};
float3 HUEcolor3_Sunset    <string UIName="Sunset: Green";   string UIWidget="color"; > = {0.25, 0.75, 0.25};
float3 HUEcolor4_Sunset     <string UIName="Sunset: Cyan";    string UIWidget="color"; > = {0.25, 0.75, 0.75};
float3 HUEcolor5_Sunset     <string UIName="Sunset: Blue";    string UIWidget="color"; > = {0.25, 0.25, 0.75};
float3 HUEcolor6_Sunset   <string UIName="Sunset: Purple";  string UIWidget="color"; > = {0.50, 0.25, 0.75};
float3 HUEcolor7_Sunset  <string UIName="Sunset: Magenta"; string UIWidget="color"; > = {0.75, 0.25, 0.75};

int HSLDUSK <string UIName="-----------DUSK";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};	
float3 HUEcolor0_Dusk      <string UIName="Dusk: Red";     string UIWidget="color"; > = {0.75, 0.25, 0.25};
float3 HUEcolor1_Dusk   <string UIName="Dusk: Orange";  string UIWidget="color"; > = {0.75, 0.50, 0.25};
float3 HUEcolor2_Dusk   <string UIName="Dusk: Yellow";  string UIWidget="color"; > = {0.75, 0.75, 0.25};
float3 HUEcolor3_Dusk    <string UIName="Dusk: Green";   string UIWidget="color"; > = {0.25, 0.75, 0.25};
float3 HUEcolor4_Dusk     <string UIName="Dusk: Cyan";    string UIWidget="color"; > = {0.25, 0.75, 0.75};
float3 HUEcolor5_Dusk     <string UIName="Dusk: Blue";    string UIWidget="color"; > = {0.25, 0.25, 0.75};
float3 HUEcolor6_Dusk   <string UIName="Dusk: Purple";  string UIWidget="color"; > = {0.50, 0.25, 0.75};
float3 HUEcolor7_Dusk  <string UIName="Dusk: Magenta"; string UIWidget="color"; > = {0.75, 0.25, 0.75};

int HSLNIGHT <string UIName="-----------NIGHT";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};	
float3 HUEcolor0_Night      <string UIName="Night: Red";     string UIWidget="color"; > = {0.75, 0.25, 0.25};
float3 HUEcolor1_Night   <string UIName="Night: Orange";  string UIWidget="color"; > = {0.75, 0.50, 0.25};
float3 HUEcolor2_Night   <string UIName="Night: Yellow";  string UIWidget="color"; > = {0.75, 0.75, 0.25};
float3 HUEcolor3_Night    <string UIName="Night: Green";   string UIWidget="color"; > = {0.25, 0.75, 0.25};
float3 HUEcolor4_Night     <string UIName="Night: Cyan";    string UIWidget="color"; > = {0.25, 0.75, 0.75};
float3 HUEcolor5_Night     <string UIName="Night: Blue";    string UIWidget="color"; > = {0.25, 0.25, 0.75};
float3 HUEcolor6_Night   <string UIName="Night: Purple";  string UIWidget="color"; > = {0.50, 0.25, 0.75};
float3 HUEcolor7_Night  <string UIName="Night: Magenta"; string UIWidget="color"; > = {0.75, 0.25, 0.75};

int HSLINTERIOR <string UIName="-----------INTERIOR";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};	
float3 HUEcolor0_Interior      <string UIName="Interior: Red";     string UIWidget="color"; > = {0.75, 0.25, 0.25};
float3 HUEcolor1_Interior   <string UIName="Interior: Orange";  string UIWidget="color"; > = {0.75, 0.50, 0.25};
float3 HUEcolor2_Interior   <string UIName="Interior: Yellow";  string UIWidget="color"; > = {0.75, 0.75, 0.25};
float3 HUEcolor3_Interior    <string UIName="Interior: Green";   string UIWidget="color"; > = {0.25, 0.75, 0.25};
float3 HUEcolor4_Interior     <string UIName="Interior: Cyan";    string UIWidget="color"; > = {0.25, 0.75, 0.75};
float3 HUEcolor5_Interior     <string UIName="Interior: Blue";    string UIWidget="color"; > = {0.25, 0.25, 0.75};
float3 HUEcolor6_Interior   <string UIName="Interior: Purple";  string UIWidget="color"; > = {0.50, 0.25, 0.75};
float3 HUEcolor7_Interior  <string UIName="Interior: Magenta"; string UIWidget="color"; > = {0.75, 0.25, 0.75};

int Line18 <string UIName="_________________________________________18";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
int SEPERATORCARTOON
<
	string UIName = ">>>                CARTOON RENDERING            <<<";
	string UIWidget = "spinner";
	int UIMin = 0;
	int UIMax = 0;
>  = { 0 };


int SEPERATORKERNEL
<
	string UIName = ">                  KERNEL              <";
	string UIWidget = "spinner";
	int UIMin = 0;
	int UIMax = 0;
>  = { 0 };


bool ENABLE_OUTLINEPASSKERNERL <string UIName = "Enable KERNEL";> = {false};   

float EOutlineAmount // custom variables
<
	string UIName = "Outline Kernel:: Cleanup Amount";
	string UIWidget = "spinner";
	float UIMin = 0.0;
	float UIMax = 1.0;
> = { 0.2 };

int KernelSize 
<
	string UIName = "Outline Kernel:: Kernel Size";
	string UIWidget = "spinner";
	int UIMin = 1;
	int UIMax = 100;
> = { 1 };

float3 KernelLineCol 
<
	string UIName = "Outline Kernel:: Line Color";
	string UIWidget = "color";
> = {0.0, 0.0, 0.0};

int SeeCalculatedOutlineKernel
<
	string UIName = "Outline Kernel:: View calculated result (before color)";
	string UIWidget = "spinner";
	int UIMin = 0;
	int UIMax = 1;
>  = { 0 };

///////////////////////////////////////////////
/////////////////// OUTLINE [DEPTH] VARIABLES//
///////////////////////////////////////////////
UI_WHITESPACE(45)  	
int SEPERATORDEPTH
<
	string UIName = ">                   OUTLINE DEPTH               <";
	string UIWidget = "spinner";
	int UIMin = 0;
	int UIMax = 0;
>  = { 0 };
bool ENABLE_OUTLINEPASS <string UIName = "Enable OUTLINE";> = {false};   
int LineThickness
<
	string UIName = "Outline Depth:: Line Thickness";
	string UIWidget = "spinner";
	int UIMin = 0;
	int UIMax = 10.;
> = {1.0 };

float DepthLineMult 
<
	string UIName = "Outline Depth:: Depth Line Mult";
	string UIWidget = "spinner";
	float UIMin = 0.1;
	float UIMax = 5.0;
> = { 1.0 };

float DepthLinePower 
<
	string UIName = "Outline Depth:: Depth Line Power";
	string UIWidget = "spinner";
	float UIMin = 0.1;
	float UIMax = 5.0;	
> = { 1.0 };

float3 LineColor 
<
	string UIName = "Outline Depth:: Line Color";
	string UIWidget = "color";
> = { 0.0, 0.0, 0.0 };

int SeeCalculatedOutlineDepth
<
	string UIName = "Outline Depth:: View calculated result (before color)";
	string UIWidget = "spinner";
	int UIMin = 0;
	int UIMax = 1;
>  = { 0 };

///////////////////////////////////////////////
/////////////////// CEL SHADE [BIN] VARIABLES//
///////////////////////////////////////////////
UI_WHITESPACE(44)  	
int SEPERATORCELBINARY
<
	string UIName = ">               CEL SHADE                <";
	string UIWidget = "spinner";
	int UIMin = 0;
	int UIMax = 0;
>  = { 0 };
bool ENABLE_CELSHADE <string UIName = "Enable CEL SHADE";> = {false};  
float ToleranceParam 
<
	string UIName = "Cel Bin:: Tolerance";
	string UIWidget = "spinner";
	float UIMin = 0.1;
	float UIMax = 1.0;
> = { 0.5 };

float3 lightCol
<
	string UIName = "Cel Bin:: Lit Color";
	string UIWidget = "color";
> = {1.0, 1.0, 1.0};

float3 darkCol
<
	string UIName = "Cel Bin:: Unlit Color";
	string UIWidget = "color";
> = {0.5, 0.5, 0.5};

int SeeCalculatedCelShadeBinary
<
	string UIName = "Cel Bin:: View calculated result (before color)";
	string UIWidget = "spinner";
	int UIMin = 0;
	int UIMax = 1;
>  = { 0 };

///////////////////////////////////////////////
/////////////////// CEL SHADE [TEN] VARIABLES//
///////////////////////////////////////////////
/*
int SEPERATORCELTEN
<
	string UIName = "== CEL SHADE [TEN] VARIABLES";
	string UIWidget = "spinner";
	int UIMin = 0;
	int UIMax = 0;
>  = { 0 };
*/
float LightingAccuracy 
<
	string UIName = "Cel Ten:: Lighting Accuracy";
	string UIWidget = "spinner";
	float UIMin = 0.1;
	float UIMax = 5.0;	
> = { 0.333 };

int ShadeCount 
<
	string UIName = "Cel Ten:: Shade Count";
	string UIWidget = "spinner";
	int UIMin = 3;
	int UIMax = 12;
> = { 4 };

float ValueOffset 
<
	string UIName = "Cel Ten:: Value Offset";
	string UIWidget = "spinner";
	float UIMin = -10.0;
	float UIMax = 10.0;	
> = { 0.0 };
	
float DesatResultMult
<
	string UIName = "Cel Ten:: Desat Result Multiplier";
	string UIWidget = "spinner";
	float UIMin = 0.0;
	float UIMax = 10.0;
> = { 1.0 };

int CelShadeMixMode // <-- calculated result is already in here! its mode 1
<
	string UIName = "Cel Ten:: Cel Shading Mode";
	string UIWidget = "spinner";
	int UIMin = 0;
	int UIMax = 3;
> = { 0 };

float ResultDivisor 
<
	string UIName = "Cel Ten:: Result Divisor (Mode 2)";
	string UIWidget = "spinner";
	float UIMin = 0.1;
	float UIMax = 5.0;
> = { 2.0 };

float BalanceColorDesat
<
	string UIName = "Cel Ten:: Balance Color and Desat (Mode 3) (doesn't work as intended)";
	string UIWidget = "spinner";
	float UIMin = 0.1;
	float UIMax = 1.0;
> = {0.5};
///////////////////////////////////////////////
/////////////////// CEL SHADE [BIN] VARIABLES//
///////////////////////////////////////////////
UI_WHITESPACE(46)  	
int SEPERATORCELBINARYI
<
	string UIName = ">       CEL SHADE INTERIOR            <";
	string UIWidget = "spinner";
	int UIMin = 0;
	int UIMax = 0;
>  = { 0 };
float ToleranceParamI 
<
	string UIName = "Interior - Cel Bin:: Tolerance";
	string UIWidget = "spinner";
	float UIMin = 0.1;
	float UIMax = 1.0;
> = { 0.5 };

float3 lightColI
<
	string UIName = "Interior - Cel Bin:: Lit Color";
	string UIWidget = "color";
> = {1.0, 1.0, 1.0};

float3 darkColI
<
	string UIName = "Interior - Cel Bin:: Unlit Color";
	string UIWidget = "color";
> = {0.5, 0.5, 0.5};

int SeeCalculatedCelShadeBinaryI
<
	string UIName = "Interior - Cel Bin:: View calculated result (before color)";
	string UIWidget = "spinner";
	int UIMin = 0;
	int UIMax = 1;
>  = { 0 };

///////////////////////////////////////////////
/////////////////// CEL SHADE [TEN] VARIABLES//
///////////////////////////////////////////////
/*
int SEPERATORCELTEN
<
	string UIName = "== CEL SHADE [TEN] VARIABLES";
	string UIWidget = "spinner";
	int UIMin = 0;
	int UIMax = 0;
>  = { 0 };
*/
float LightingAccuracyI 
<
	string UIName = "Interior - Cel Ten:: Lighting Accuracy";
	string UIWidget = "spinner";
	float UIMin = 0.1;
	float UIMax = 5.0;	
> = { 0.333 };

int ShadeCountI 
<
	string UIName = "Interior - Cel Ten:: Shade Count";
	string UIWidget = "spinner";
	int UIMin = 3;
	int UIMax = 12;
> = { 4 };

float ValueOffsetI
<
	string UIName = "Interior - Cel Ten:: Value Offset";
	string UIWidget = "spinner";
	float UIMin = -10.0;
	float UIMax = 10.0;	
> = { 0.0 };
	
float DesatResultMultI
<
	string UIName = "Interior - Cel Ten:: Desat Result Multiplier";
	string UIWidget = "spinner";
	float UIMin = 0.0;
	float UIMax = 10.0;
> = { 1.0 };

int CelShadeMixModeI // <-- calculated result is already in here! its mode 1
<
	string UIName = "Interior - Cel Ten:: Cel Shading Mode";
	string UIWidget = "spinner";
	int UIMin = 0;
	int UIMax = 3;
> = { 0 };

float ResultDivisorI
<
	string UIName = "Interior - Cel Ten:: Result Divisor (Mode 2)";
	string UIWidget = "spinner";
	float UIMin = 0.1;
	float UIMax = 5.0;
> = { 2.0 };

float BalanceColorDesatI
<
	string UIName = "Interior - Cel Ten:: Balance Color and Desat (Mode 3) (doesn't work as intended)";
	string UIWidget = "spinner";
	float UIMin = 0.1;
	float UIMax = 1.0;
> = {0.5};
int Line17 <string UIName="_________________________________________17";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
int SMAAGUI <string UIName=">>>                  SMAA SETTINGS            <<<";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
#undef SHADERGROUP

#endif //SHADERGROUP

#undef TODIE
