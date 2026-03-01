//////////////////////////////////////////////////////////////////////////
//			ENBEFFECT.FX BY PROD80				//
//			EDITED FOR SNAPDRAGON ENB			//
//			LUT CODE BY KINGERIC1992			//			
//////////////////////////////////////////////////////////////////////////

// Keyboard controlled temporary variables (in some versions exists in the config file).
// Press and hold key 1,2,3...8 together with PageUp or PageDown to modify. By default all set to 1.0
	float4	tempF1; // 0,1,2,3
	float4	tempF2; // 5,6,7,8
	float4	tempF3; // 9,0
// x=generic timer in range 0..1, period of 16777216 ms (4.6 hours), w=frame time elapsed (in seconds)
	float4	Timer;
// x=Width, y=1/Width, z=ScreenScaleY, w=1/ScreenScaleY
	float4	ScreenSize;
// changes in range 0..1, 0 means that night time, 1 - day time
	float	ENightDayFactor;
//changes 0 or 1. 0 means that exterior, 1 - interior
	float	EInteriorFactor;
// enb version of bloom applied, ignored if original post processing used
	float	EBloomAmount;	

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//internal parameters, can be modified, through GUI.
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


//GLOBAL +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
	bool   Section_SkyrimIS <
		string UIName =  "------Skyrim IS Limits------";
	> = {false};
	float SKIS_SatE <
		string UIName="Skyrim IS: Max Saturation Ext";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=3.0;
		float UIStep=0.001;
	> = {1.25};
	float SKIS_SatI <
		string UIName="Skyrim IS: Max Saturation Int";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=3.0;
		float UIStep=0.001;
	> = {1.25};
	float SKIS_Con <
		string UIName="Skyrim IS: Max Contrast";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=3.0;
		float UIStep=0.001;
	> = {0.9};
	float SKIS_Brightness <
		string UIName="Skyrim IS: Max Brightness";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=3.0;
		float UIStep=0.001;
	> = {1.00};
	float SKIS_TintD <
		string UIName="Skyrim IS: Max Tint Amount Day";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.63};
	float SKIS_TintN <
		string UIName="Skyrim IS: Max Tint Amount Night";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.63};
	float SKIS_TintI <
		string UIName="Skyrim IS: Max Tint Amount Interior";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {1.00};
	bool   Section_Palette <
		string UIName =  "------ENB Palette Mix-------";
	> = {false};
	float palettemix <
		string UIName="Palette Mix";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.01;
	> = {0.5};
	bool   Section_Adaptation <
		string UIName =  "------ENB Adaptation--------";
	> = {false};
	float adapt_low <
		string UIName="Adaptation Low Clamp";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=2.0;
		float UIStep=0.001;
	> = {0.8};
	float adapt_high <
		string UIName="Adaptation High Clamp";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=2.0;
		float UIStep=0.001;
	> = {1.0};
	bool   Section_Bloom <
		string UIName =  "------ENB Bloom Tweaks------";
	> = {false};
	bool showbtex <
		string UIName="Show texture before process";
	> = {false};
	float3 nsatD <
        string UIName="Bloom RGB Balance Day";
        string UIWidget="Color";
    > = {0.318, 0.4, 0.463};
	float3 nsatN <
        string UIName="Bloom RGB Balance Night";
        string UIWidget="Color";
    > = {0.318, 0.4, 0.463};
	float3 nsatI <
        string UIName="Bloom RGB Balance Interior";
        string UIWidget="Color";
    > = {0.318, 0.4, 0.463};
	float bconD <
		string UIName="Bloom Curve Day";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.3};
	float bconN <
		string UIName="Bloom Curve Night";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.24};
	float bconI <
		string UIName="Bloom Curve Interior";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.24};
	float bloom_L1T_D <
		string UIName="Bloom L1 Threshold Day";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.0};
	float bloom_L1T_N <
		string UIName="Bloom L1 Threshold Night";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.0};
	float bloom_L1T_I <
		string UIName="Bloom L1 Threshold Interior";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.0};
	float bloom_L2T_D <
		string UIName="Bloom L2 Threshold Day";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.125};
	float bloom_L2T_N <
		string UIName="Bloom L2 Threshold Night";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.06};
	float bloom_L2T_I <
		string UIName="Bloom L2 Threshold Interior";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.06};
	float bloom_L3T_D <
		string UIName="Bloom L3 Threshold Day";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.0};
	float bloom_L3T_N <
		string UIName="Bloom L3 Threshold Night";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.0};
	float bloom_L3T_I <
		string UIName="Bloom L3 Threshold Interior";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.0};
	float bloom_L1I_D <
		string UIName="Bloom L1 Intensity Day";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=2.0;
		float UIStep=0.001;
	> = {1.0};
	float bloom_L1I_N <
		string UIName="Bloom L1 Intensity Night";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=2.0;
		float UIStep=0.001;
	> = {1.0};
	float bloom_L1I_I <
		string UIName="Bloom L1 Intensity Interior";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=2.0;
		float UIStep=0.001;
	> = {1.0};
	float bloom_L2I_D <
		string UIName="Bloom L2 Intensity Day";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=2.0;
		float UIStep=0.001;
	> = {0.7};
	float bloom_L2I_N <
		string UIName="Bloom L2 Intensity Night";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=2.0;
		float UIStep=0.001;
	> = {1.0};
	float bloom_L2I_I <
		string UIName="Bloom L2 Intensity Interior";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=2.0;
		float UIStep=0.001;
	> = {1.0};	
	float bloom_L3I_D <
		string UIName="Bloom L3 Intensity Day";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=2.0;
		float UIStep=0.001;
	> = {0.33};
	float bloom_L3I_N <
		string UIName="Bloom L3 Intensity Night";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=2.0;
		float UIStep=0.001;
	> = {0.7};
	float bloom_L3I_I <
		string UIName="Bloom L3 Intensity Interior";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=2.0;
		float UIStep=0.001;
	> = {0.7};
	bool show_screenout <
		string UIName="Show Bloom Output";
	> = {false};
	bool   Section_Sepia <
		string UIName =  "------Color Tinting---------";
	> = {false};
	bool use_tinting <
		string UIName = "Enable Tinting";
	> = {false};
	float sepia_luma <
		string UIName="Luma Source";
		string UIWidget="Spinner";
		float UIMin=1;
		float UIMax=5;
		float UIStep=1;
	> = {1};
	float3 LightColorD <
		string UIName="Light Color Day";
		string UIWidget="Color";
	> = {1, 1, 1};
	float3 MidColorD <
		string UIName="Mid Color Day";
		string UIWidget="Color";
	> = {1, 0.5, 0};
	float3 DarkColorD <
		string UIName="Dark Color Day";
		string UIWidget="Color";
	> = {0, 0.0549, 0.196};
	float3 LightColorN <
		string UIName="Light Color Night";
		string UIWidget="Color";
	> = {1, 1, 1};
	float3 MidColorN <
		string UIName="Mid Color Night";
		string UIWidget="Color";
	> = {1, 0.5, 0};
	float3 DarkColorN <
		string UIName="Dark Color Night";
		string UIWidget="Color";
	> = {0.0196, 0.0745, 0.176};
	float3 LightColorI <
		string UIName="Light Color Interior";
		string UIWidget="Color";
	> = {1, 1, 1};
	float3 MidColorI <
		string UIName="Mid Color Interior";
		string UIWidget="Color";
	> = {1, 0.5, 0};
	float3 DarkColorI <
		string UIName="Dark Color Interior";
		string UIWidget="Color";
	> = {0, 0.051, 0.173};
	float DesatD <
		string UIName="Desaturation Day";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.2};
	float DesatN <
		string UIName="Desaturation Night";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.2};
	float DesatI <
		string UIName="Desaturation Interior";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.2};
	float3 TonedD <
		string UIName="Tone Strength Day";
		string UIWidget="Color";
	> = {0.161, 0.184, 0.353};
	float3 TonedN <
		string UIName="Tone Strength Night";
		string UIWidget="Color";
	> = {0.106, 0.125, 0.18};
	float3 TonedI <
		string UIName="Tone Strength Interior";
		string UIWidget="Color";
	> = {0.106, 0.125, 0.235};
	bool   Section_Split <
		string UIName =  "------Split Coloring--------";
	> = {false};
	bool use_colortinting <
		string UIName = "Enable Splitcolor";
	> = {false};
	float3 rgbLightD <
		string UIName="RGB Balance Highlights Day";
		string UIWidget="Color";
	> = {1, 0.925, 0.224};
	float3 rgbDarkD <
		string UIName="RGB Balance Shadows Day";
		string UIWidget="Color";
	> = {0.176, 0.51, 0.941};
	float3 rgbLightN <
		string UIName="RGB Balance Highlights Night";
		string UIWidget="Color";
	> = {1, 0.925, 0.224};
	float3 rgbDarkN <
		string UIName="RGB Balance Shadows Night";
		string UIWidget="Color";
	> = {0.176, 0.51, 0.941};
	float3 rgbLightI <
		string UIName="RGB Balance Highlights Interior";
		string UIWidget="Color";
	> = {1, 0.925, 0.224};
	float3 rgbDarkI <
		string UIName="RGB Balance Shadows Interior";
		string UIWidget="Color";
	> = {0.176, 0.51, 0.941};
	float ctDesatD <
		string UIName="SC Desaturation Day";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.84};
	float ctDesatN <
		string UIName="SC Desaturation Night";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.825};
	float ctDesatI <
		string UIName="SC Desaturation Interior";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.84};
	float ctMixD <
		string UIName="Split Color Mix Day";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.345};
	float ctMixN <
		string UIName="Split Color Mix Night";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.36};
	float ctMixI <
		string UIName="Split Color Mix Interior";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.245};
	bool   Section_BW <
		string UIName =  "------B&W Color Filter------";
	> = {false};
	bool use_bwfilter <
		string UIName="Enable B&W filter";
	> = {false};
	float bw_red <
		string UIName="B&W R Filter (RGB total 100)";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=100.0;
		float UIStep=0.1;
	> = {4.0};
	float bw_green <
		string UIName="B&W G Filter (RGB total 100)";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=100.0;
		float UIStep=0.1;
	> = {92.0};
	float bw_blue <
		string UIName="B&W B Filter (RGB total 100)";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=100.0;
		float UIStep=0.1;
	> = {4.0};
	float bw_strength <
		string UIName="B&W Strength";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {1.000};

//DAY ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
	bool   Section_Day <
		string UIName =  "------Corrections Day-------";
	> = {false};
	float3 rgbday <
        string UIName="Day: RGB Balance";
        string UIWidget="Color";
    > = {0.478, 0.471, 0.467};
	float ShoulderStrengthD <
		string UIName="Tonemap Day: Shoulder Strength";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=2.0;
		float UIStep=0.001;
	> = {0.205};
	float LinearStrengthD <
		string UIName="Tonemap Day: Linear Strength";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=5.0;
		float UIStep=0.001;
	> = {0.4};
	float LinearAngleD <
		string UIName="Tonemap Day: Linear Angle";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.5};
	float ToeStrengthD <
		string UIName="Tonemap Day: Toe Strength";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=2.0;
		float UIStep=0.001;
	> = {0.235};
	float ToeNumeratorD <
		string UIName="Tonemap Day: Toe Numerator";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=0.5;
		float UIStep=0.001;
	> = {0.07};
	float ToeDenominatorD <
		string UIName="Tonemap Day: Toe Denominator";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=2.0;
		float UIStep=0.001;
	> = {0.6};
	float LinearWhiteD <
		string UIName="Tonemap Day: White Scale";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=100.0;
		float UIStep=0.001;
	> = {5.65};
	float	cc_pre_brtD <
		string UIName="Day CC: Pre-Brightness";
		string UIWidget="Spinner";
		float UIMin=1.0;
		float UIMax=10.0;
		float UIStep=0.001;
	> = {1.3};
	float	cc_satD <
		string UIName="Day CC: Saturation";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.82};
	float	inBlackD <
		string UIName="Day CC: Black Level";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.0};
	float	inGammaD <
		string UIName="Day CC: Gamma Curve";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=5.0;
		float UIStep=0.001;
	> = {2.2};
	float	inWhiteD <
		string UIName="Day CC: White Level";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {1.0};
	float outBlackD <
		string UIName="Day CC: Black Cutoff";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.0};
	float outWhiteD <
		string UIName="Day CC: White Cutoff";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {1.0};
	
//NIGHT ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++	
	bool   Section_Night <
		string UIName =  "------Corrections Night-----";
	> = {false};
	float3 rgbnight <
        string UIName="Night: RGB Balance";
        string UIWidget="Color";
    > = {0.502, 0.455, 0.439};
	float ShoulderStrengthN <
		string UIName="Tonemap Night: Shoulder Strength";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=2.0;
		float UIStep=0.001;
	> = {0.205};
	float LinearStrengthN <
		string UIName="Tonemap Night: Linear Strength";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=5.0;
		float UIStep=0.001;
	> = {0.4};
	float LinearAngleN <
		string UIName="Tonemap Night: Linear Angle";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.5};
	float ToeStrengthN <
		string UIName="Tonemap Night: Toe Strength";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=2.0;
		float UIStep=0.001;
	> = {0.235};
	float ToeNumeratorN <
		string UIName="Tonemap Night: Toe Numerator";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=0.5;
		float UIStep=0.001;
	> = {0.07};
	float ToeDenominatorN <
		string UIName="Tonemap Night: Toe Denominator";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=2.0;
		float UIStep=0.001;
	> = {0.495};
	float LinearWhiteN <
		string UIName="Tonemap Night: White Scale";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=100.0;
		float UIStep=0.001;
	> = {5.65};
	float	cc_pre_brtN <
		string UIName="Night CC: Pre-Brightness";
		string UIWidget="Spinner";
		float UIMin=1.0;
		float UIMax=10.0;
		float UIStep=0.001;
	> = {1.300};
	float	cc_satN <
		string UIName="Night CC: Saturation";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.82};
	float	inBlackN <
		string UIName="Night CC: Black Level";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.0};
	float	inGammaN <
		string UIName="Night CC: Gamma Curve";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=5.0;
		float UIStep=0.001;
	> = {2.2};
	float	inWhiteN <
		string UIName="Night CC: White Level";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {1.0};
	float outBlackN <
		string UIName="Night CC: Black Cutoff";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.0};
	float outWhiteN <
		string UIName="Night CC: White Cutoff";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {1.0};
	
//INTERIOR +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
	bool   Section_Interior <
		string UIName =  "------Corrections Interior--";
	> = {false};
	float3 rgbinterior <
        string UIName="Interior: RGB Balance";
        string UIWidget="Color";
    > = {0.49, 0.471, 0.455};
	float ShoulderStrengthI <
		string UIName="Tonemap Interior: Shoulder Strength";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=2.0;
		float UIStep=0.001;
	> = {0.25};
	float LinearStrengthI <
		string UIName="Tonemap Interior: Linear Strength";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=5.0;
		float UIStep=0.001;
	> = {0.4};
	float LinearAngleI <
		string UIName="Tonemap Interior: Linear Angle";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.54};
	float ToeStrengthI <
		string UIName="Tonemap Interior: Toe Strength";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=2.0;
		float UIStep=0.001;
	> = {0.18};
	float ToeNumeratorI <
		string UIName="Tonemap Interior: Toe Numerator";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=0.5;
		float UIStep=0.001;
	> = {0.07};
	float ToeDenominatorI <
		string UIName="Tonemap Interior: Toe Denominator";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=2.0;
		float UIStep=0.001;
	> = {0.42};
	float LinearWhiteI <
		string UIName="Tonemap Interior: White Scale";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=100.0;
		float UIStep=0.001;
	> = {5.65};
	float	cc_pre_brtI <
		string UIName="Interior CC: Pre-Brightness";
		string UIWidget="Spinner";
		float UIMin=1.0;
		float UIMax=10.0;
		float UIStep=0.001;
	> = {1.49};
	float	cc_satI <
		string UIName="Interior CC: Saturation";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.92};
	float	inBlackI <
		string UIName="Interior CC: Black Level";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.0};
	float	inGammaI <
		string UIName="Interior CC: Gamma Curve";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=5.0;
		float UIStep=0.001;
	> = {2.2};
	float	inWhiteI <
		string UIName="Interior CC: White Level";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {1.0};
	float outBlackI <
		string UIName="Interior CC: Black Cutoff";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.0};
	float outWhiteI <
		string UIName="Interior CC: White Cutoff";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {1.0};
	
	//End	
	
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//external parameters, do not modify
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
	
	texture2D texs0; // color
	texture2D texs1; // bloom skyrim
	texture2D texs2; // adaptation skyrim
	texture2D texs3; // bloom enb
	texture2D texs4; // adaptation enb
	texture2D texs7; // palette enb	

sampler2D _s0 = sampler_state {
	Texture   = <texs0>;
	MinFilter = POINT;
	MagFilter = POINT;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

sampler2D _s1 = sampler_state {
	Texture   = <texs1>;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

sampler2D _s2 = sampler_state {
	Texture   = <texs2>;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

sampler2D _s3 = sampler_state {
	Texture   = <texs3>;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

sampler2D _s4 = sampler_state {
	Texture   = <texs4>;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

sampler2D _s7 = sampler_state {
	Texture   = <texs7>;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

struct VS_OUTPUT_POST {
	float4 vpos  	: POSITION;
	float2 txcoord0 : TEXCOORD0;
};

struct VS_INPUT_POST {
	float3 pos 		: POSITION;
	float2 txcoord0 : TEXCOORD0;
};

VS_OUTPUT_POST VS_Quad(VS_INPUT_POST IN){
	VS_OUTPUT_POST OUT;
	OUT.vpos=float4(IN.pos.x,IN.pos.y,IN.pos.z,1.0);
	OUT.txcoord0.xy=IN.txcoord0.xy;
	return OUT;
};

//skyrim shader specific externals, do not modify
	float4 _c1 : register(c1);
	float4 _c2 : register(c2);
	float4 _c3 : register(c3);
	float4 _c4 : register(c4);
	float4 _c5 : register(c5);

	
	//
	// HELPER FUNCTIONS ++++++++++++++++++++++++++++++
	
	float grayValue(float3 gv)
	{
	return dot( gv, float3(0.2125, 0.7154, 0.0721) );
	}

	float3 softlight(float3 a, float3 b, float s)
	{
	float3 ret;
	float3 b_x2 = 2.0 * b;
	float3 a_b_x2 = a * b_x2;
	float3 c1 = a_b_x2 + a * a - a * a_b_x2;
	float3 c2 = sqrt(a) * ( b_x2 - 1.0 ) + 2.0 * a - a_b_x2;
	ret = ( b > 0.5 ) ? c1 : c2;
	return lerp( a, ret, s );
	}
	
	float3 overlay(float3 a, float3 b, float s)
	{
	float3 ret;
	float3 c1 = 1.0 - ( 2.0 * ( 1.0 - a ) * ( 1.0 - b ) );
	float3 c2 = a * b * 2.0;
	ret = ( a > 0.5 ) ? c1 : c2;
	return lerp( a, ret, s );
	}
	
	float3 screen(float3 a, float3 b)
	{
	return (1.0f - (1.0f - a) * (1.0f - b));
	}
	
	static const float2 poison[12] =
	{
	float2(-0.326212f, -0.40581f),
	float2(-0.840144f, -0.07358f),
	float2(-0.695914f, 0.457137f),
	float2(-0.203345f, 0.620716f),
	float2(0.96234f, -0.194983f),
	float2(0.473434f, -0.480026f),
	float2(0.519456f, 0.767022f),
	float2(0.185461f, -0.893124f),
	float2(0.507431f, 0.064425f),
	float2(0.89642f, 0.412458f),
	float2(-0.32194f, -0.932615f),
	float2(-0.791559f, -0.59771f)
	};
	
	
	// +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
	// PIXEL SHADER ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
	// +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
	
float4 PS_D6EC7DD1(VS_OUTPUT_POST IN, float2 vPos : VPOS, uniform sampler2D luttex) : COLOR {

		float4 _oC0 		= 0.0; // output
		float4 _c6 			= float4(0, 0, 0, 0);
		float4 _c7 			= float4(0.2125, 0.7154, 0.0721, 1.0);
		float3 _c8			= float3(1.0, 1.0, 1.0);
		float4 _v0			= 0.0;
		_v0.xy 				= IN.txcoord0.xy;
		//
		// Set color
		float4 color		= tex2D(_s0, _v0.xy);

//
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// EYE ADAPTATION
		
		float4 Adaptation	= tex2D( _s4, 0.5 );
		float4 Adapt_pal	= tex2D( _s4, 0.5 ); // ENB Adaptation only used on palette, unclamped
		
		//Turn off adaptation in interiors, it's just wrong.
		if ( EInteriorFactor ) {
		Adaptation.xyz		= float3( 0.5, 0.5, 0.5 );
		};
		float adapt_max		= max( max( Adaptation.x, Adaptation.y ), Adaptation.z );
		adapt_max			= clamp( adapt_max, adapt_low, adapt_high );

//
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// ADD BLOOM by PROD80

		//Bloom
		float4 xcolorbloom	= tex2D(_s3, _v0.xy);
		if (showbtex==true)
			{
			float4 bloom	= tex2D(_s3, _v0.xy) * EBloomAmount;
			bloom.w 		= 1.0f;
			return bloom;
			}
		float3 oldcol		= xcolorbloom.xyz;
		
		//Bloom tinting
		float3 nsat			= lerp( lerp( nsatN, nsatD, ENightDayFactor ), nsatI, EInteriorFactor );
		float bcalc			= dot( nsat, float3( 0.33333333, 0.33333333, 0.33333333 ));
		nsat.xyz			= nsat.xyz / bcalc.xxx;
		xcolorbloom.xyz		*= nsat;

		//DNI stuff
		float bloom_L1T		= lerp( lerp( bloom_L1T_N, bloom_L1T_D, ENightDayFactor ), bloom_L1T_I, EInteriorFactor );
		float bloom_L1I		= lerp( lerp( bloom_L1I_N, bloom_L1I_D, ENightDayFactor ), bloom_L1I_I, EInteriorFactor ) * EBloomAmount;
		float bloom_L2T		= lerp( lerp( bloom_L2T_N, bloom_L2T_D, ENightDayFactor ), bloom_L2T_I, EInteriorFactor );
		float bloom_L2I		= lerp( lerp( bloom_L2I_N, bloom_L2I_D, ENightDayFactor ), bloom_L2I_I, EInteriorFactor ) * EBloomAmount;
		float bloom_L3T		= lerp( lerp( bloom_L3T_N, bloom_L3T_D, ENightDayFactor ), bloom_L3T_I, EInteriorFactor );
		float bloom_L3I		= lerp( lerp( bloom_L3I_N, bloom_L3I_D, ENightDayFactor ), bloom_L3I_I, EInteriorFactor ) * EBloomAmount;
		float bcontrast		= lerp( lerp( bconN, bconD, ENightDayFactor ), bconI, EInteriorFactor );
		
		//Layers
		float3 bloom_L1		= max( xcolorbloom.xyz - bloom_L1T, 0.0f ) * bloom_L1I;
		float3 bloom_L2		= max( xcolorbloom.xyz - bloom_L2T, 0.0f ) * bloom_L2I;
		float3 bloom_L3		= max( xcolorbloom.xyz - bloom_L3T, 0.0f ) * bloom_L3I;
		
		//Adding bloom to image
		xcolorbloom.xyz		= bloom_L1.xyz + bloom_L2.xyz + bloom_L3.xyz;
		xcolorbloom.xyz		= lerp( xcolorbloom.xyz, xcolorbloom.xyz * xcolorbloom.xyz, bcontrast );
		if (show_screenout==true)
			{
				float4 screenout;
				screenout.w			= 1.0;
				screenout.xyz		= xcolorbloom.xyz;
				return screenout;
			}
		color.xyz 			+= xcolorbloom.xyz;

//
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++			
// GAME COLOR CORRECTIONS

float4 r0;
float4 r1;
float4 r2;
float4 r3;
float4 r4;

// _c1.x = ? 
// _c1.y = ? Some generic intensity, maybe Adaptation related
// _c1.z = ?
// _c1.w = ?
// 
// _c2.x = ? Receive Bloom Threshold
// _c2.y = White
// _c2.z = ?
// _c2.w = ?
// 
// _c3.x = Saturation
// _c3.y = ?
// _c3.z = Contrast
// _c3.w = Brightness
//
// _c4.x = Tint Red
// _c4.y = Tint Green
// _c4.z = Tint Blue
// _c4.w = Tint Amount

	r0.x=1.0/_c2.y;						//White
//	  r1=tex2D(_s2, _v0);				//replace Skyrim adaptation by fixed values
	r1=float4(0.4, 0.4, 0.4, 1.0);		//...yes, fu wonky adaptation
	float cc_pre_brt = lerp( lerp( cc_pre_brtN, cc_pre_brtD, ENightDayFactor ), cc_pre_brtI, EInteriorFactor );
	r0.yz=r1.xy * cc_pre_brt;			//CC-Pre-brightness
    r0.w=1.0/r0.y;
    r0.z=r0.w * r0.z;
    r1=color;							//set color here
//    r1.xyz=r1 * _c1.y;
	r1.xyz=r1 * cc_pre_brt;				//CC-Pre-brightness
    r0.w=dot(_c7.xyz, r1.xyz);
    r1.w=r0.w * r0.z;
    r0.z=r0.z * r0.w + _c7.w;
    r0.z=1.0/r0.z;
    r0.x=r1.w * r0.x + _c7.w;
    r0.x=r0.x * r1.w;
    r0.x=r0.z * r0.x;
    if (r0.w<0) r0.x=_c6.x;
    r0.z=1.0/r0.w;
    r0.z=r0.z * r0.x;
//    r0.x=saturate(-r0.x + _c2.x);     //... bloom related, not needed
//    r2=tex2D(_s3, _v0);				//enb bloom, shouldnt happen here
//    r2=tex2D(_s1, _v0);				//skyrim bloom, also gone
//    r2.xyz=r2 * _c1.y;				//disable all blooming math here
//    r2.xyz=r0.x * r2;					//...
    r1.xyz=r1 * r0.z;					//... + r2 removed, since no bloom anyway
    r0.x=dot(r1.xyz, _c7.xyz);
    r1.w=_c7.w;
	float SKIS_Sat = lerp( SKIS_SatE, SKIS_SatI, EInteriorFactor );
    r2=lerp(r0.x, r1, min(_c3.x, SKIS_Sat));	//max IS saturation allowed
    r1=r0.x * _c4 - r2;
	float SKIS_Tint = lerp( lerp( SKIS_TintN, SKIS_TintD, ENightDayFactor), SKIS_TintI, EInteriorFactor );
    r1=min(_c4.w, SKIS_Tint) * r1 + r2;			//max IS tint amount allowed DNI
	
	//r1=_c3.w * r1 - r0.y; 					//khajiit night vision _c3.w, IS brightness
    r3=_c3.w * r1 - r0.y;						//...
	r4=min(_c3.w, SKIS_Brightness) * r1 - r0.y;	//...
	r1=(_c3.w>1.51) ? r3 : r4;					//give control without affecting NV/PV
    
	r0=min(_c3.z, SKIS_Con) * r1 + r0.y;		//max IS contrast allowed
    r1=-r0 + _c5;
    color=_c5.w * r1 + r0;
		
//
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//Prod80 COLOR CORRECTIONS
	
		//[prod80] Color balance
		//
		//Day RGB balance
		float calcd			= dot( rgbday.xyz, float3( 0.33333333, 0.33333333, 0.33333333 ));
		float3 rgbd			= rgbday.xyz / calcd.xxx;
		//
		//Night RGB balance
		float calcn			= dot( rgbnight.xyz, float3( 0.33333333, 0.33333333, 0.33333333 ));
		float3 rgbn			= rgbnight.xyz / calcn.xxx;
		//
		//Interior RGB balance
		float calci			= dot( rgbinterior.xyz, float3( 0.33333333, 0.33333333, 0.33333333 ));
		float3 rgbi			= rgbinterior.xyz / calci.xxx;
	
		//Saturation and Levels DNI
		//
		float inBlack		= lerp(	lerp(	inBlackN,	inBlackD,	ENightDayFactor	),	inBlackI,	EInteriorFactor ); 
		float inGamma		= lerp(	lerp(	inGammaN,	inGammaD,	ENightDayFactor	),	inGammaI,	EInteriorFactor ); 
		float inWhite		= lerp(	lerp(	inWhiteN,	inWhiteD,	ENightDayFactor	),	inWhiteI,	EInteriorFactor );
		float outBlack		= lerp( lerp(	outBlackN,	outBlackD,	ENightDayFactor ),	outBlackI,	EInteriorFactor );
		float outWhite		= lerp( lerp(	outWhiteN,	outWhiteD,	ENightDayFactor ),	outWhiteI,	EInteriorFactor );
		float cc_sat		= lerp(	lerp(	cc_satN,	cc_satD,	ENightDayFactor	),	cc_satI,	EInteriorFactor );
		//
		//
		//Prod80: Apply Brightness, Saturation, Intensity
		//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

		//Saturation
		float cGray			= grayValue( color.xyz );
		color.xyz			= lerp( cGray, color.xyz, cc_sat );
		
		//Black Level, White Level and Gamma Curve
		color.xyz 			= pow( max( color.xyz - inBlack , 0.0f ) / max( inWhite - inBlack, 0.0001f ), inGamma ) * max( outWhite - outBlack, 0.0001f ) + outBlack;
		
		//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		//Apply RGB balance
		float3 cbalance 	= lerp( lerp( rgbn, rgbd, ENightDayFactor ), rgbi, EInteriorFactor );
		color.xyz			= cbalance.xyz * color.xyz;
		
//
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// TONE CURVE (FILMIC - UNCHARTED 2) DNI. FULLY TWEAKABLE FROM GUI - BE CAREFULL, ITS TRICKY
// HOOKED DIRECTLY INTO ADAPTATION (Adaptation modifies Linear White)
		
		float A				= lerp(	lerp(	ShoulderStrengthN, 	ShoulderStrengthD, 	ENightDayFactor	), 	ShoulderStrengthI, 	EInteriorFactor	);
		float B				= lerp(	lerp(	LinearStrengthN, 	LinearStrengthD, 	ENightDayFactor	), 	LinearStrengthI, 	EInteriorFactor	);
		float C				= lerp(	lerp(	LinearAngleN,		LinearAngleD,		ENightDayFactor	),	LinearAngleI,		EInteriorFactor	);
		float D				= lerp(	lerp(	ToeStrengthN,		ToeStrengthD,		ENightDayFactor	),	ToeStrengthI,		EInteriorFactor	);
		float E				= lerp(	lerp(	ToeNumeratorN,		ToeNumeratorD,		ENightDayFactor	),	ToeNumeratorI,		EInteriorFactor	);
		float F				= lerp(	lerp(	ToeDenominatorN,	ToeDenominatorD,	ENightDayFactor	),	ToeDenominatorI,	EInteriorFactor	);
		float WS			= lerp(	lerp(	LinearWhiteN,		LinearWhiteD,		ENightDayFactor	),	LinearWhiteI,		EInteriorFactor	);
		
		float3 Q			= color.xyz;
		float W				= WS * ( 1.0f + adapt_max );
		float3 numerator	= ((Q*(A*Q+C*B)+D*E)/(Q*(A*Q+B)+D*F)) - E/F;     
		float3 denominator	= ((W*(A*W+C*B)+D*E)/(W*(A*W+B)+D*F)) - E/F;
		color.xyz			= numerator / denominator;
		color.xyz			= max( color.xyz, _c6.xyz );
	
//
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// ENB PALETTE


	float2 CLut_pSize = float2(0.00390625, 0.0625);// 1 / float2(256, 16);
	color.rgb  = saturate(color.rgb);
	color.b   *= 15;
	float4 CLut_UV = 0;
	CLut_UV.w  = floor(color.b);
	CLut_UV.xy = color.rg * 15 * CLut_pSize + 0.5 * CLut_pSize ;
	CLut_UV.x += CLut_UV.w * CLut_pSize.y;
	color.rgb  = lerp( tex2Dlod(luttex, CLut_UV.xyzz).rgb, tex2Dlod(luttex, CLut_UV.xyzz + float4(CLut_pSize.y, 0, 0, 0)).rgb, color.b - CLut_UV.w);


//E_CC_PALETTE


//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// COLOR TINTING (SPLIT TONE, ETC) BY PROD80

		if (use_colortinting==true)
		{
		float3 rgbLight			= lerp( lerp( rgbLightN,	rgbLightD,		ENightDayFactor	),	rgbLightI,		EInteriorFactor );
		float3 rgbDark			= lerp( lerp( rgbDarkN,		rgbDarkD,		ENightDayFactor	),	rgbDarkI,		EInteriorFactor	);
		float ctDesat			= lerp( lerp( ctDesatN,		ctDesatD,		ENightDayFactor	),	ctDesatI,		EInteriorFactor );
		float ctMix				= lerp( lerp( ctMixN,		ctMixD,			ENightDayFactor	),	ctMixI,			EInteriorFactor );
		
		color.xyz				= saturate( color.xyz );
		float ctLuma			= grayValue( color.xyz );
		float rgbDarkAdj		= dot( rgbDark.xyz, float3( 0.33333333, 0.33333333, 0.33333333 ));
		float rgbLightAdj		= dot( rgbLight.xyz, float3( 0.33333333, 0.33333333, 0.33333333 ));
		float3 ctDarkcolor		= color.xyz * ( rgbDark.xyz / rgbDarkAdj.xxx );
		float3 ctLightcolor		= color.xyz * ( rgbLight.xyz / rgbLightAdj.xxx );
		float3 ctDesatcolor		= lerp( color.xyz, ctLuma, ctDesat );
		float3 ctColor			= lerp( ctDarkcolor.xyz, ctLightcolor.xyz, ctLuma );
		color.xyz				= lerp( ctDesatcolor, ctColor, ctMix );
		};
		
//
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// COLOR TINTING (SEPIA, ETC) BY NVIDIA, HEAVILY MODIFIED BY PROD80 (Tri-color, Luma source, etc.)

		if (use_tinting==true)
		{
		float3 	LightColor 		= lerp( lerp( LightColorN,	LightColorD,	ENightDayFactor ),	LightColorI,	EInteriorFactor );
		float3	MidColor		= lerp( lerp( MidColorN,	MidColorD,		ENightDayFactor ),	MidColorI,		EInteriorFactor );
		float3 	DarkColor 		= lerp( lerp( DarkColorN,	DarkColorD,		ENightDayFactor ),	DarkColorI,		EInteriorFactor );
		float 	Desat 			= lerp( lerp( DesatN,		DesatD,			ENightDayFactor ),	DesatI,			EInteriorFactor );
		float3 	Toned 			= lerp( lerp( TonedN,		TonedD,			ENightDayFactor ),	TonedI,			EInteriorFactor );
		
		color.xyz				= saturate( color.xyz );
		float gray				= 0.0f;
		float3 bkColor			= float3( 0.0, 		0.0, 		0.0		);
		float3 sepia_luma1		= float3( 0.2125,	0.7154,		0.0721	);
		float3 sepia_luma2		= float3( 0.299,	0.587,		0.114	);
		float3 sepia_luma3		= float3( 0.3333,	0.3333,		0.3333	);
		float3 sepia_luma4		= float3( 0.7154,	0.2125,		0.0721	);
		float3 sepia_luma5		= float3( 0.114,	0.299,		0.587	);
		
		// Luma selection
		if (sepia_luma==1) gray = dot( color.xyz, sepia_luma1.xyz );
		if (sepia_luma==2) gray = dot( color.xyz, sepia_luma2.xyz );
		if (sepia_luma==3) gray = dot( color.xyz, sepia_luma3.xyz );
		if (sepia_luma==4) gray = dot( color.xyz, sepia_luma4.xyz );
		if (sepia_luma==5) gray = dot( color.xyz, sepia_luma5.xyz );
		
		float MidColorMult		= dot( MidColor.xyz, float3( 0.33333333, 0.33333333, 0.33333333 ));
		float3 MidColorComb		= color.xyz * ( MidColor.xyz / MidColorMult.xxx );
		float3 md1				= lerp( bkColor.xyz, MidColorComb.xyz, gray ) * 2;
		float3 md2				= lerp( MidColorComb.xyz, bkColor.xyz, gray ) * 2;
		float3 mdColor			= ( gray <= 0.5 ) ? md1 : md2;
		float3 shColor			= lerp( DarkColor.xyz, mdColor.xyz, gray );
		float3 hlColor			= lerp( mdColor.xyz, color.xyz * LightColor.xyz, gray );
		float3 triColor			= lerp( shColor.xyz, hlColor.xyz, gray );
		float3 deColor			= lerp( color.xyz, gray.xxx, Desat );
		color.xyz				= lerp( deColor.xyz, triColor.xyz, Toned.xyz );
		};
		
//
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// ADVANCED B&W FILTER (prod80)

		if (use_bwfilter==true)
		{
		float3 bw_filter	= float3( 1.0f * ( bw_red * 0.01f ), 1.0f * ( bw_green * 0.01f ), 1.0f * ( bw_blue * 0.01f ) );
		float bw			= dot( color.xyz, bw_filter.xyz );
		color.xyz			= lerp( color.xyz, bw, bw_strength );
		};
		
//
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// SET COLOR	

		_oC0.w=1.0;
		_oC0.xyz=color.xyz;
			
	return _oC0;
}


texture2D lutnametex0 <string ResourceName= "defaultlut.png" ; >; 
sampler2D lutname0 = sampler_state {
	Texture   = < lutnametex0 >;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

technique Shader_D6EC7DD1
{
    pass p0
    {
        VertexShader  = compile vs_3_0 VS_Quad();
        PixelShader  = compile ps_3_0 PS_D6EC7DD1( lutname0 );
        
        ColorWriteEnable=ALPHA|RED|GREEN|BLUE;
        ZEnable=FALSE;
        ZWriteEnable=FALSE;
        CullMode=NONE;
        AlphaTestEnable=FALSE;
        AlphaBlendEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
        
    }
}


// additional lut1

texture2D lutnametex1 <string ResourceName= "LUT_1.bmp" ; >; 
sampler2D lutname1 = sampler_state {
	Texture   = < lutnametex1 >;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

technique PASSNAME <string UIName= "Snapdragon" ;> 
{
    pass p0
    {
        VertexShader  = compile vs_3_0 VS_Quad();
        PixelShader  = compile ps_3_0 PS_D6EC7DD1( lutname1 );
        
        ColorWriteEnable=ALPHA|RED|GREEN|BLUE;
        ZEnable=FALSE;
        ZWriteEnable=FALSE;
        CullMode=NONE;
        AlphaTestEnable=FALSE;
        AlphaBlendEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
        
    }
}

// additional lut2

texture2D lutnametex2 <string ResourceName= "LUT_2.bmp" ; >; 
sampler2D lutname2 = sampler_state {
	Texture   = < lutnametex2 >;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

technique PASSNAME2 <string UIName= "All-Is-Vain" ;> 
{
    pass p0
    {
        VertexShader  = compile vs_3_0 VS_Quad();
        PixelShader  = compile ps_3_0 PS_D6EC7DD1( lutname2 );
        
        ColorWriteEnable=ALPHA|RED|GREEN|BLUE;
        ZEnable=FALSE;
        ZWriteEnable=FALSE;
        CullMode=NONE;
        AlphaTestEnable=FALSE;
        AlphaBlendEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
        
    }
}

// additional lut3

texture2D lutnametex3 <string ResourceName= "LUT_3.bmp" ; >; 
sampler2D lutname3 = sampler_state {
	Texture   = < lutnametex3 >;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

technique PASSNAME3 <string UIName= "Creamy" ;> 
{
    pass p0
    {
        VertexShader  = compile vs_3_0 VS_Quad();
        PixelShader  = compile ps_3_0 PS_D6EC7DD1( lutname3 );
        
        ColorWriteEnable=ALPHA|RED|GREEN|BLUE;
        ZEnable=FALSE;
        ZWriteEnable=FALSE;
        CullMode=NONE;
        AlphaTestEnable=FALSE;
        AlphaBlendEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
        
    }
}


// additional lut4

texture2D lutnametex4 <string ResourceName= "LUT_4.bmp" ; >; 
sampler2D lutname4 = sampler_state {
	Texture   = < lutnametex4 >;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

technique PASSNAME4 <string UIName= "Toon" ;> 
{
    pass p0
    {
        VertexShader  = compile vs_3_0 VS_Quad();
        PixelShader  = compile ps_3_0 PS_D6EC7DD1( lutname4 );
        
        ColorWriteEnable=ALPHA|RED|GREEN|BLUE;
        ZEnable=FALSE;
        ZWriteEnable=FALSE;
        CullMode=NONE;
        AlphaTestEnable=FALSE;
        AlphaBlendEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
        
    }
}


// additional lut5

texture2D lutnametex5 <string ResourceName= "LUT_5.bmp" ; >; 
sampler2D lutname5 = sampler_state {
	Texture   = < lutnametex5 >;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

technique PASSNAME5 <string UIName= "Lost in Time" ;> 
{
    pass p0
    {
        VertexShader  = compile vs_3_0 VS_Quad();
        PixelShader  = compile ps_3_0 PS_D6EC7DD1( lutname5 );
        
        ColorWriteEnable=ALPHA|RED|GREEN|BLUE;
        ZEnable=FALSE;
        ZWriteEnable=FALSE;
        CullMode=NONE;
        AlphaTestEnable=FALSE;
        AlphaBlendEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
        
    }
}


// additional lut6

texture2D lutnametex6 <string ResourceName= "LUT_6.bmp" ; >; 
sampler2D lutname6 = sampler_state {
	Texture   = < lutnametex6 >;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

technique PASSNAME6 <string UIName= "Lomo" ;> 
{
    pass p0
    {
        VertexShader  = compile vs_3_0 VS_Quad();
        PixelShader  = compile ps_3_0 PS_D6EC7DD1( lutname6 );
        
        ColorWriteEnable=ALPHA|RED|GREEN|BLUE;
        ZEnable=FALSE;
        ZWriteEnable=FALSE;
        CullMode=NONE;
        AlphaTestEnable=FALSE;
        AlphaBlendEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
        
    }
}


// additional lut7

texture2D lutnametex7 <string ResourceName= "LUT_7.bmp" ; >; 
sampler2D lutname7 = sampler_state {
	Texture   = < lutnametex7 >;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

technique PASSNAME7 <string UIName= "Drama" ;> 
{
    pass p0
    {
        VertexShader  = compile vs_3_0 VS_Quad();
        PixelShader  = compile ps_3_0 PS_D6EC7DD1( lutname7 );
        
        ColorWriteEnable=ALPHA|RED|GREEN|BLUE;
        ZEnable=FALSE;
        ZWriteEnable=FALSE;
        CullMode=NONE;
        AlphaTestEnable=FALSE;
        AlphaBlendEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
        
    }
}

// additional lut8

texture2D lutnametex8 <string ResourceName= "LUT_8.bmp" ; >; 
sampler2D lutname8 = sampler_state {
	Texture   = < lutnametex8 >;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

technique PASSNAME8 <string UIName= "Silence" ;> 
{
    pass p0
    {
        VertexShader  = compile vs_3_0 VS_Quad();
        PixelShader  = compile ps_3_0 PS_D6EC7DD1( lutname8 );
        
        ColorWriteEnable=ALPHA|RED|GREEN|BLUE;
        ZEnable=FALSE;
        ZWriteEnable=FALSE;
        CullMode=NONE;
        AlphaTestEnable=FALSE;
        AlphaBlendEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
        
    }
}

// additional lut9

texture2D lutnametex9 <string ResourceName= "LUT_9.bmp" ; >; 
sampler2D lutname9 = sampler_state {
	Texture   = < lutnametex9 >;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

technique PASSNAME9 <string UIName= "Sparta" ;> 
{
    pass p0
    {
        VertexShader  = compile vs_3_0 VS_Quad();
        PixelShader  = compile ps_3_0 PS_D6EC7DD1( lutname9 );
        
        ColorWriteEnable=ALPHA|RED|GREEN|BLUE;
        ZEnable=FALSE;
        ZWriteEnable=FALSE;
        CullMode=NONE;
        AlphaTestEnable=FALSE;
        AlphaBlendEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
        
    }
}

// additional lut10

texture2D lutnametex10 <string ResourceName= "LUT_10.bmp" ; >; 
sampler2D lutname10 = sampler_state {
	Texture   = < lutnametex10 >;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

technique PASSNAME10 <string UIName= "Somber" ;> 
{
    pass p0
    {
        VertexShader  = compile vs_3_0 VS_Quad();
        PixelShader  = compile ps_3_0 PS_D6EC7DD1( lutname10 );
        
        ColorWriteEnable=ALPHA|RED|GREEN|BLUE;
        ZEnable=FALSE;
        ZWriteEnable=FALSE;
        CullMode=NONE;
        AlphaTestEnable=FALSE;
        AlphaBlendEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
        
    }
}


// additional lut11

texture2D lutnametex11 <string ResourceName= "LUT_11.bmp" ; >; 
sampler2D lutname11 = sampler_state {
	Texture   = < lutnametex11 >;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

technique PASSNAME11 <string UIName= "Old World" ;> 
{
    pass p0
    {
        VertexShader  = compile vs_3_0 VS_Quad();
        PixelShader  = compile ps_3_0 PS_D6EC7DD1( lutname11 );
        
        ColorWriteEnable=ALPHA|RED|GREEN|BLUE;
        ZEnable=FALSE;
        ZWriteEnable=FALSE;
        CullMode=NONE;
        AlphaTestEnable=FALSE;
        AlphaBlendEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
        
    }
}

// additional lut12

texture2D lutnametex12 <string ResourceName= "LUT_12.bmp" ; >; 
sampler2D lutname12 = sampler_state {
	Texture   = < lutnametex12 >;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

technique PASSNAME12 <string UIName= "Eccentric" ;> 
{
    pass p0
    {
        VertexShader  = compile vs_3_0 VS_Quad();
        PixelShader  = compile ps_3_0 PS_D6EC7DD1( lutname12 );
        
        ColorWriteEnable=ALPHA|RED|GREEN|BLUE;
        ZEnable=FALSE;
        ZWriteEnable=FALSE;
        CullMode=NONE;
        AlphaTestEnable=FALSE;
        AlphaBlendEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
        
    }
}

// additional lut13

texture2D lutnametex13 <string ResourceName= "LUT_13.bmp" ; >; 
sampler2D lutname13 = sampler_state {
	Texture   = < lutnametex13 >;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

technique PASSNAME13 <string UIName= "Knox" ;> 
{
    pass p0
    {
        VertexShader  = compile vs_3_0 VS_Quad();
        PixelShader  = compile ps_3_0 PS_D6EC7DD1( lutname13 );
        
        ColorWriteEnable=ALPHA|RED|GREEN|BLUE;
        ZEnable=FALSE;
        ZWriteEnable=FALSE;
        CullMode=NONE;
        AlphaTestEnable=FALSE;
        AlphaBlendEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
        
    }
}


// additional lut14

texture2D lutnametex14 <string ResourceName= "LUT_14.bmp" ; >; 
sampler2D lutname14 = sampler_state {
	Texture   = < lutnametex14 >;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

technique PASSNAME14 <string UIName= "Senpai" ;> 
{
    pass p0
    {
        VertexShader  = compile vs_3_0 VS_Quad();
        PixelShader  = compile ps_3_0 PS_D6EC7DD1( lutname14 );
        
        ColorWriteEnable=ALPHA|RED|GREEN|BLUE;
        ZEnable=FALSE;
        ZWriteEnable=FALSE;
        CullMode=NONE;
        AlphaTestEnable=FALSE;
        AlphaBlendEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
        
    }
}

// additional lut15

texture2D lutnametex15 <string ResourceName= "LUT_15.bmp" ; >; 
sampler2D lutname15 = sampler_state {
	Texture   = < lutnametex15 >;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

technique PASSNAME15 <string UIName= "Overseer" ;> 
{
    pass p0
    {
        VertexShader  = compile vs_3_0 VS_Quad();
        PixelShader  = compile ps_3_0 PS_D6EC7DD1( lutname15 );
        
        ColorWriteEnable=ALPHA|RED|GREEN|BLUE;
        ZEnable=FALSE;
        ZWriteEnable=FALSE;
        CullMode=NONE;
        AlphaTestEnable=FALSE;
        AlphaBlendEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
        
    }
}

// additional lut16

texture2D lutnametex16 <string ResourceName= "LUT_16.bmp" ; >; 
sampler2D lutname16 = sampler_state {
	Texture   = < lutnametex16 >;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

technique PASSNAME16 <string UIName= "Oz" ;> 
{
    pass p0
    {
        VertexShader  = compile vs_3_0 VS_Quad();
        PixelShader  = compile ps_3_0 PS_D6EC7DD1( lutname16 );
        
        ColorWriteEnable=ALPHA|RED|GREEN|BLUE;
        ZEnable=FALSE;
        ZWriteEnable=FALSE;
        CullMode=NONE;
        AlphaTestEnable=FALSE;
        AlphaBlendEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
        
    }
}

// additional lut17

texture2D lutnametex17 <string ResourceName= "LUT_17.bmp" ; >; 
sampler2D lutname17 = sampler_state {
	Texture   = < lutnametex17 >;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

technique PASSNAME17 <string UIName= "Penintenziagite" ;> 
{
    pass p0
    {
        VertexShader  = compile vs_3_0 VS_Quad();
        PixelShader  = compile ps_3_0 PS_D6EC7DD1( lutname17 );
        
        ColorWriteEnable=ALPHA|RED|GREEN|BLUE;
        ZEnable=FALSE;
        ZWriteEnable=FALSE;
        CullMode=NONE;
        AlphaTestEnable=FALSE;
        AlphaBlendEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
        
    }
}

// additional lut18

texture2D lutnametex18 <string ResourceName= "LUT_18.bmp" ; >; 
sampler2D lutname18 = sampler_state {
	Texture   = < lutnametex18 >;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

technique PASSNAME18 <string UIName= "All is Vainer" ;> 
{
    pass p0
    {
        VertexShader  = compile vs_3_0 VS_Quad();
        PixelShader  = compile ps_3_0 PS_D6EC7DD1( lutname18 );
        
        ColorWriteEnable=ALPHA|RED|GREEN|BLUE;
        ZEnable=FALSE;
        ZWriteEnable=FALSE;
        CullMode=NONE;
        AlphaTestEnable=FALSE;
        AlphaBlendEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
        
    }
}

// additional lut19

texture2D lutnametex19 <string ResourceName= "LUT_19.bmp" ; >; 
sampler2D lutname19 = sampler_state {
	Texture   = < lutnametex19 >;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

technique PASSNAME19 <string UIName= "Clockwork" ;> 
{
    pass p0
    {
        VertexShader  = compile vs_3_0 VS_Quad();
        PixelShader  = compile ps_3_0 PS_D6EC7DD1( lutname19 );
        
        ColorWriteEnable=ALPHA|RED|GREEN|BLUE;
        ZEnable=FALSE;
        ZWriteEnable=FALSE;
        CullMode=NONE;
        AlphaTestEnable=FALSE;
        AlphaBlendEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
        
    }
}

// additional lut20

texture2D lutnametex20 <string ResourceName= "LUT_20.bmp" ; >; 
sampler2D lutname20 = sampler_state {
	Texture   = < lutnametex20 >;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

technique PASSNAME20 <string UIName= "Aqua" ;> 
{
    pass p0
    {
        VertexShader  = compile vs_3_0 VS_Quad();
        PixelShader  = compile ps_3_0 PS_D6EC7DD1( lutname20 );
        
        ColorWriteEnable=ALPHA|RED|GREEN|BLUE;
        ZEnable=FALSE;
        ZWriteEnable=FALSE;
        CullMode=NONE;
        AlphaTestEnable=FALSE;
        AlphaBlendEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
        
    }
}

// additional lut21

texture2D lutnametex21 <string ResourceName= "LUT_21.bmp" ; >; 
sampler2D lutname21 = sampler_state {
	Texture   = < lutnametex21 >;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

technique PASSNAME21 <string UIName= "Ultra Contrast" ;> 
{
    pass p0
    {
        VertexShader  = compile vs_3_0 VS_Quad();
        PixelShader  = compile ps_3_0 PS_D6EC7DD1( lutname21 );
        
        ColorWriteEnable=ALPHA|RED|GREEN|BLUE;
        ZEnable=FALSE;
        ZWriteEnable=FALSE;
        CullMode=NONE;
        AlphaTestEnable=FALSE;
        AlphaBlendEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
        
    }
}

// additional lut22

texture2D lutnametex22 <string ResourceName= "LUT_22.bmp" ; >; 
sampler2D lutname22 = sampler_state {
	Texture   = < lutnametex22 >;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

technique PASSNAME22 <string UIName= "Vogue" ;> 
{
    pass p0
    {
        VertexShader  = compile vs_3_0 VS_Quad();
        PixelShader  = compile ps_3_0 PS_D6EC7DD1( lutname22 );
        
        ColorWriteEnable=ALPHA|RED|GREEN|BLUE;
        ZEnable=FALSE;
        ZWriteEnable=FALSE;
        CullMode=NONE;
        AlphaTestEnable=FALSE;
        AlphaBlendEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
        
    }
}

// additional lut23

texture2D lutnametex23 <string ResourceName= "LUT_23.bmp" ; >; 
sampler2D lutname23 = sampler_state {
	Texture   = < lutnametex23 >;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

technique PASSNAME23 <string UIName= "Vintage BW" ;> 
{
    pass p0
    {
        VertexShader  = compile vs_3_0 VS_Quad();
        PixelShader  = compile ps_3_0 PS_D6EC7DD1( lutname23 );
        
        ColorWriteEnable=ALPHA|RED|GREEN|BLUE;
        ZEnable=FALSE;
        ZWriteEnable=FALSE;
        CullMode=NONE;
        AlphaTestEnable=FALSE;
        AlphaBlendEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
        
    }
}

// additional lut24

texture2D lutnametex24 <string ResourceName= "LUT_24.bmp" ; >; 
sampler2D lutname24 = sampler_state {
	Texture   = < lutnametex24 >;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

technique PASSNAME24 <string UIName= "Simple BW" ;> 
{
    pass p0
    {
        VertexShader  = compile vs_3_0 VS_Quad();
        PixelShader  = compile ps_3_0 PS_D6EC7DD1( lutname24 );
        
        ColorWriteEnable=ALPHA|RED|GREEN|BLUE;
        ZEnable=FALSE;
        ZWriteEnable=FALSE;
        CullMode=NONE;
        AlphaTestEnable=FALSE;
        AlphaBlendEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
        
    }
}


// additional lut25

texture2D lutnametex25 <string ResourceName= "LUT_25.bmp" ; >; 
sampler2D lutname25 = sampler_state {
	Texture   = < lutnametex25 >;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

technique PASSNAME25 <string UIName= "Creeper" ;> 
{
    pass p0
    {
        VertexShader  = compile vs_3_0 VS_Quad();
        PixelShader  = compile ps_3_0 PS_D6EC7DD1( lutname25 );
        
        ColorWriteEnable=ALPHA|RED|GREEN|BLUE;
        ZEnable=FALSE;
        ZWriteEnable=FALSE;
        CullMode=NONE;
        AlphaTestEnable=FALSE;
        AlphaBlendEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
        
    }
}


// additional lut26

texture2D lutnametex26 <string ResourceName= "LUT_26.bmp" ; >; 
sampler2D lutname26 = sampler_state {
	Texture   = < lutnametex26 >;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

technique PASSNAME26 <string UIName= "Surfin Bird" ;> 
{
    pass p0
    {
        VertexShader  = compile vs_3_0 VS_Quad();
        PixelShader  = compile ps_3_0 PS_D6EC7DD1( lutname26 );
        
        ColorWriteEnable=ALPHA|RED|GREEN|BLUE;
        ZEnable=FALSE;
        ZWriteEnable=FALSE;
        CullMode=NONE;
        AlphaTestEnable=FALSE;
        AlphaBlendEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
        
    }
}


// additional lut27

texture2D lutnametex27 <string ResourceName= "LUT_27.bmp" ; >; 
sampler2D lutname27 = sampler_state {
	Texture   = < lutnametex27 >;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

technique PASSNAME27 <string UIName= "Cucumber" ;> 
{
    pass p0
    {
        VertexShader  = compile vs_3_0 VS_Quad();
        PixelShader  = compile ps_3_0 PS_D6EC7DD1( lutname27 );
        
        ColorWriteEnable=ALPHA|RED|GREEN|BLUE;
        ZEnable=FALSE;
        ZWriteEnable=FALSE;
        CullMode=NONE;
        AlphaTestEnable=FALSE;
        AlphaBlendEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
        
    }
}


// additional lut28

texture2D lutnametex28 <string ResourceName= "LUT_28.bmp" ; >; 
sampler2D lutname28 = sampler_state {
	Texture   = < lutnametex28 >;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

technique PASSNAME28 <string UIName= "Radioactive" ;> 
{
    pass p0
    {
        VertexShader  = compile vs_3_0 VS_Quad();
        PixelShader  = compile ps_3_0 PS_D6EC7DD1( lutname28 );
        
        ColorWriteEnable=ALPHA|RED|GREEN|BLUE;
        ZEnable=FALSE;
        ZWriteEnable=FALSE;
        CullMode=NONE;
        AlphaTestEnable=FALSE;
        AlphaBlendEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
        
    }
}

// additional lut29

texture2D lutnametex29 <string ResourceName= "Custom_LUT1.bmp" ; >; 
sampler2D lutname29 = sampler_state {
	Texture   = < lutnametex29 >;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

technique PASSNAME29 <string UIName= "Custom 1" ;> 
{
    pass p0
    {
        VertexShader  = compile vs_3_0 VS_Quad();
        PixelShader  = compile ps_3_0 PS_D6EC7DD1( lutname29 );
        
        ColorWriteEnable=ALPHA|RED|GREEN|BLUE;
        ZEnable=FALSE;
        ZWriteEnable=FALSE;
        CullMode=NONE;
        AlphaTestEnable=FALSE;
        AlphaBlendEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
        
    }
}

// additional lut30

texture2D lutnametex30 <string ResourceName= "Custom_LUT2.bmp" ; >; 
sampler2D lutname30 = sampler_state {
	Texture   = < lutnametex30 >;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

technique PASSNAME30 <string UIName= "Custom 2" ;> 
{
    pass p0
    {
        VertexShader  = compile vs_3_0 VS_Quad();
        PixelShader  = compile ps_3_0 PS_D6EC7DD1( lutname30 );
        
        ColorWriteEnable=ALPHA|RED|GREEN|BLUE;
        ZEnable=FALSE;
        ZWriteEnable=FALSE;
        CullMode=NONE;
        AlphaTestEnable=FALSE;
        AlphaBlendEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
        
    }
}

// additional lut31

texture2D lutnametex31 <string ResourceName= "Custom_LUT3.bmp" ; >; 
sampler2D lutname31 = sampler_state {
	Texture   = < lutnametex31 >;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

technique PASSNAME31 <string UIName= "Custom 3" ;> 
{
    pass p0
    {
        VertexShader  = compile vs_3_0 VS_Quad();
        PixelShader  = compile ps_3_0 PS_D6EC7DD1( lutname31 );
        
        ColorWriteEnable=ALPHA|RED|GREEN|BLUE;
        ZEnable=FALSE;
        ZWriteEnable=FALSE;
        CullMode=NONE;
        AlphaTestEnable=FALSE;
        AlphaBlendEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
        
    }
}

// additional lut32

texture2D lutnametex32 <string ResourceName= "tetro.bmp" ; >; 
sampler2D lutname32 = sampler_state {
	Texture   = < lutnametex32 >;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE; // LINEAR;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

technique PASSNAME32 <string UIName= "Tetro's Choice" ;> 
{
    pass p0
    {
        VertexShader  = compile vs_3_0 VS_Quad();
        PixelShader  = compile ps_3_0 PS_D6EC7DD1( lutname32 );
        
        ColorWriteEnable=ALPHA|RED|GREEN|BLUE;
        ZEnable=FALSE;
        ZWriteEnable=FALSE;
        CullMode=NONE;
        AlphaTestEnable=FALSE;
        AlphaBlendEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
        
    }
}

