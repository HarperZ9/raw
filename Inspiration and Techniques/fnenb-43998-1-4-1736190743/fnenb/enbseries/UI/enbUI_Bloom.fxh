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
int SharpBlur <string UIName=">>>EXTENDED Bloom  modified by Haevaksa<<<";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
UI_WHITESPACE(31) 
float KawaseBloomCorrection <		string UIName=" Exterior Kawase Bloom Correction";		string UIWidget="Spinner";		float UIMin=1.0;		float UIMax=10.0;		float UIStep=0.01;	> = {1.0};
float KawaseBloomCorrectionI <		string UIName=" Interior Kawase Bloom Correction";		string UIWidget="Spinner";		float UIMin=1.0;		float UIMax=10.0;		float UIStep=0.01;	> = {1.0};	
bool ENABLE_KawaseMIXR <string UIName = " Remove Mixing from kawase bloom";> = {false};
UI_WHITESPACE(30)  
int IntensitySEPER <string UIName="-Intensity-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
float IntensityDawn <		string UIName="Dawn Intensity";		string UIWidget="Spinner";		float UIMin=0.0;		float UIMax=10.0;		float UIStep=0.001;	> = {1.0};
float IntensitySunrise <		string UIName="Sunrise Intensity";		string UIWidget="Spinner";		float UIMin=0.0;		float UIMax=10.0;		float UIStep=0.001;	> = {1.0};
float IntensityDay <		string UIName="Day Intensity";		string UIWidget="Spinner";		float UIMin=0.0;		float UIMax=10.0;		float UIStep=0.001;	> = {1.0};
float IntensitySunset <		string UIName="Sunset Intensity";		string UIWidget="Spinner";		float UIMin=0.0;		float UIMax=10.0;		float UIStep=0.001;	> = {1.0};
float IntensityDusk <		string UIName="Dusk Intensity";		string UIWidget="Spinner";		float UIMin=0.0;		float UIMax=10.0;		float UIStep=0.001;	> = {1.0};
float IntensityNight <		string UIName="Night Intensity";		string UIWidget="Spinner";		float UIMin=0.0;		float UIMax=10.0;		float UIStep=0.001;	> = {1.0};
float IntensityI <		string UIName="Interior Intensity";		string UIWidget="Spinner";		float UIMin=0.0;		float UIMax=10.0;		float UIStep=0.001;	> = {1.0};
//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 1
int LineDayBloom <string UIName="_______________________________________________";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
//int Daybloom <string UIName=">>>         Exterior       <<<";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_NightBloom <string UIName = "Enable Time Bloom - day default";> = {false};
UI_WHITESPACE(14)  
int kAWASEEX3 <string UIName="The value below Not used in Kawase Bloom.   ";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0; > = {0};
UI_WHITESPACE(24)  


//int ContrastSEPER <string UIName="Dawn";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
float	fContrastDawn <	string UIName="Dawn - Contrast";	string UIWidget="spinner";	float UIMin=0.0;	float UIMax=1000.0; float UIStep=0.01;> = {1.0};
float	ECCInBlackDawn <	string UIName="Dawn - CC: In black";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=5.0; float UIStep=0.01;> = {0.0};
float	ECCInWhiteDawn <	string UIName="Dawn - CC: In white";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=500.0; float UIStep=0.01;> = {1.0};
float	ECCOutBlackDawn <	string UIName="Dawn - CC: Out black";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=1.0; float UIStep=0.01;> = {0.0};
float	ECCOutWhiteDawn <	string UIName="Dawn - CC: Out white";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=1.0; float UIStep=0.01;> = {1.0};
float4	fSaturationDawn <	string UIName="Dawn - CCCC: Saturation";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=5.0;> = {1.0, 1.0, 1.0, 1.0};
float post_mixer_bloomShapeDawn <  string UIName="Dawn - Gaussian: Bloom Shape";  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=32.0;  float UIStep=0.01;> = {1.0};
UI_WHITESPACE(12)  
//int InblackSEPER <string UIName="Sunrise";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0; float UIStep=0.01;> = {0};

float	fContrastSunrise <	string UIName="Sunrise - Contrast";	string UIWidget="spinner";	float UIMin=0.0;	float UIMax=1000.0; float UIStep=0.01;> = {1.0};
float	ECCInBlackSunrise <	string UIName="Sunrise - CC: In black";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=5.0; float UIStep=0.01;> = {0.0};
float	ECCInWhiteSunrise <	string UIName="Sunrise - CC: In white";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=500.0; float UIStep=0.01;> = {1.0};
float	ECCOutBlackSunrise <	string UIName="Sunrise - CC: Out black";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=1.0; float UIStep=0.01;> = {0.0};
float	ECCOutWhiteSunrise <	string UIName="Sunrise - CC: Out white";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=1.0; float UIStep=0.01;> = {1.0};
float4	fSaturationSunrise <	string UIName="Sunrise - CCCC: Saturation";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=5.0;> = {1.0, 1.0, 1.0, 1.0};
float post_mixer_bloomShapeSunrise <  string UIName="Sunrise - Gaussian: Bloom Shape";  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=32.0;  float UIStep=0.01;> = {1.0};
UI_WHITESPACE(20)  
//int SaturationSEPER <string UIName="-Saturation-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};

float	fContrastD <	string UIName="Day - Contrast";	string UIWidget="spinner";	float UIMin=0.0;	float UIMax=1000.0; float UIStep=0.01;> = {1.0};
float	ECCInBlackD <	string UIName="Day - CC: In black";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=5.0; float UIStep=0.01;> = {0.0};
float	ECCInWhiteD <	string UIName="Day - CC: In white";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=500.0; float UIStep=0.01;> = {1.0};
float	ECCOutBlackD <	string UIName="Day - CC: Out black";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=1.0; float UIStep=0.01;> = {0.0};
float	ECCOutWhiteD <	string UIName="Day - CC: Out white";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=1.0; float UIStep=0.01;> = {1.0};
float4	fSaturationD <	string UIName="Day - CCCC: Saturation";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=5.0;> = {1.0, 1.0, 1.0, 1.0};
float post_mixer_bloomShapeD <  string UIName="Day - Gaussian: Bloom Shape";  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=32.0;  float UIStep=0.01;> = {1.0};
UI_WHITESPACE(8)  
//int outblackSEPER <string UIName="-Out black-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};

float	fContrastSunset <	string UIName="Sunset - Contrast";	string UIWidget="spinner";	float UIMin=0.0;	float UIMax=1000.0; float UIStep=0.01;> = {1.0};
float	ECCInBlackSunset <	string UIName="Sunset - CC: In black";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=5.0; float UIStep=0.01;> = {0.0};
float	ECCInWhiteSunset <	string UIName="Sunset - CC: In white";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=500.0; float UIStep=0.01;> = {1.0};
float	ECCOutBlackSunset <	string UIName="Sunset - CC: Out black";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=1.0; float UIStep=0.01;> = {0.0};
float	ECCOutWhiteSunset <	string UIName="Sunset - CC: Out white";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=1.0; float UIStep=0.01;> = {1.0};
float4	fSaturationSunset <	string UIName="Sunset - CCCC: Saturation";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=5.0;> = {1.0, 1.0, 1.0, 1.0};
float post_mixer_bloomShapeSunset <  string UIName="Sunset - Gaussian: Bloom Shape";  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=32.0;  float UIStep=0.01;> = {1.0};
UI_WHITESPACE(9)  
//int InWhiteSEPER <string UIName="-In white-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0; float UIStep=0.01;> = {0};
float	fContrastDusk <	string UIName="Dusk - Contrast";	string UIWidget="spinner";	float UIMin=0.0;	float UIMax=1000.0; float UIStep=0.01;> = {1.0};
float	ECCInBlackDusk <	string UIName="Dusk - CC: In black";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=5.0; float UIStep=0.01;> = {0.0};
float	ECCInWhiteDusk <	string UIName="Dusk - CC: In white";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=500.0; float UIStep=0.01;> = {1.0};
float	ECCOutBlackDusk <	string UIName="Dusk - CC: Out black";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=1.0; float UIStep=0.01;> = {0.0};
float	ECCOutWhiteDusk <	string UIName="Dusk - CC: Out white";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=1.0; float UIStep=0.01;> = {1.0};
float4	fSaturationDusk <	string UIName="Dusk - CCCC: Saturation";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=5.0;> = {1.0, 1.0, 1.0, 1.0};
float post_mixer_bloomShapeDusk <  string UIName="Dusk - Gaussian: Bloom Shape";  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=32.0;  float UIStep=0.01;> = {1.0};
UI_WHITESPACE(13)  
//int outwhiteSEPER <string UIName="-Out white-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
float	fContrastN <	string UIName="Night - Contrast";	string UIWidget="spinner";	float UIMin=0.0;	float UIMax=1000.0; float UIStep=0.01;> = {1.0};
float	ECCInBlackN <	string UIName="Night - CC: In black";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=5.0; float UIStep=0.01;> = {0.0};
float	ECCInWhiteN <	string UIName="Night - CC: In white ";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=500.0; float UIStep=0.01;> = {1.0};
float	ECCOutBlackN <	string UIName="Night - CC: Out black ";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=1.0; float UIStep=0.01;> = {0.0};
float	ECCOutWhiteN <	string UIName="Night - CC: Out white ";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=1.0; float UIStep=0.01;> = {1.0};
float4	fSaturationN <	string UIName="Night - CC: Saturation ";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=5.0;> = {1.0, 1.0, 1.0, 1.0};
float post_mixer_bloomShapeN <   string UIName="Night - Gaussian: Bloom Shape ";  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=32.0;  float UIStep=0.01;> = {1.0};
UI_WHITESPACE(10)  

UI_WHITESPACE(3)  
int LinetInteriorBloom <string UIName="_______________________________________________3";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
int Interiorbloom <string UIName=">>>                    Interior                      <<<";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_InteriorBloom <string UIName = "Enable Interior Bloom Parameters";> = {false};   

float	fContrastI <	string UIName="Contrast Interior";	string UIWidget="spinner";	float UIMin=0.0;	float UIMax=1000.0; float UIStep=0.01;> = {1.0};
float	ECCInBlackI <	string UIName="CC: In black Interior";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=5.0; float UIStep=0.01;> = {0.0};
float	ECCInWhiteI <	string UIName="CC: In white Interior";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=500.0; float UIStep=0.01;> = {1.0};
float	ECCOutBlackI <	string UIName="CC: Out black Interior";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=1.0; float UIStep=0.01;> = {0.0};
float	ECCOutWhiteI <	string UIName="CC: Out white Interior";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=1.0; float UIStep=0.01;> = {1.0};
float4	fSaturationI <	string UIName="CC: Saturation Interior";	string UIWidget="Spinner";	float UIMin=0.0;	float UIMax=5.0;> = {1.0, 1.0, 1.0, 1.0};
float post_mixer_bloomShapeI <  string  UIName="Interior - Gaussian: Bloom Shape";  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=32.0;  float UIStep=0.01;> = {1.0};
UI_WHITESPACE(5) 


//int Postmixerbloomshapeseper <string UIName="-Bloom Shape-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};







UI_WHITESPACE(18)  

#if(GaussianBloomColorEffect != 0) 
int PostmixerbloomColor <string UIName="-Bloom Color-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
float3 post_mixer_bloomColorDawn <  string UIName="Dawn - Gaussian: Bloom Color Tint Amount";  string UIWidget="Color";> = {1.0, 1.0, 1.0};
float3 post_mixer_bloomColorSunrise <  string UIName="Sunrise - Gaussian: Bloom Color Tint Amount";  string UIWidget="Color";> = {1.0, 1.0, 1.0};
float3 post_mixer_bloomColorD <  string UIName="Day - Gaussian: Bloom Color Tint Amount";  string UIWidget="Color";> = {1.0, 1.0, 1.0};
float3 post_mixer_bloomColorSunset <  string UIName="Sunset - Gaussian: Bloom Color Tint Amount";  string UIWidget="Color";> = {1.0, 1.0, 1.0};
float3 post_mixer_bloomColorDusk <  string UIName="Dusk - Gaussian: Bloom Color Tint Amount";  string UIWidget="Color";> = {1.0, 1.0, 1.0};
float3 post_mixer_bloomColorN <  string UIName="Night - Gaussian: Bloom Color Tint Amount ";  string UIWidget="Color";> = {1.0, 1.0, 1.0};
float3 post_mixer_bloomColorI <  string UIName="Interior - Gaussian: Bloom Color Tint Amount";  string UIWidget="Color";> = {1.0, 1.0, 1.0};

#endif

UI_WHITESPACE(7)  
UI_WHITESPACE(11)  
UI_WHITESPACE(6)  
UI_WHITESPACE(15)  
UI_WHITESPACE(16)  
UI_WHITESPACE(17)  

//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 2

int BloommixSeperator <string UIName=">>>>>>>Bloom Mix<<<<<<<";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0; float UIStep=0.01;> = {0};
//UI_WHITESPACE(23)  	
bool ENABLE_MIXER <string UIName = " Remove Mixing";> = {false};
//bool Pass_select1 <string UIName="Enable Mix RenderTarget1024";> = {true};
//bool Pass_select2 <string UIName="Enable Mix RenderTarget516";> = {true};
//bool Pass_select3 <string UIName="Enable Mix RenderTarget256";> = {true};
//bool Pass_select4 <string UIName="Enable Mix RenderTarget128";> = {true};
//bool Pass_select5 <string UIName="Enable Mix RenderTarget64";> = {true};
//bool Pass_select6 <string UIName="Enable Mix RenderTarget32";> = {true};
UI_WHITESPACE(28)  
int ExteriorMix <string UIName="-Exterior - Bloom Mix-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
UI_WHITESPACE(19)  	
float Pass_select1_intDawn <string UIName="Dawn-RenderTarget1024 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {0.4};
float Pass_select2_intDawn <string UIName="Dawn-RenderTarget516 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {1.0};
float Pass_select3_intDawn <string UIName="Dawn-RenderTarget256 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {1.0};
float Pass_select4_intDawn <string UIName="Dawn-RenderTarget128 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {0.85};
float Pass_select5_intDawn <string UIName="Dawn-RenderTarget64 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {0.65};
float Pass_select6_intDawn <string UIName="Dawn-RenderTarget32 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {0.45};
float Pass_select7_intDawn <string UIName="Dawn-RenderTarget16 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {0.45};
UI_WHITESPACE(21)  	
float Pass_select1_intSunrise <string UIName="Sunrise-RenderTarget1024 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {0.4};
float Pass_select2_intSunrise <string UIName="Sunrise-RenderTarget516 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {1.0};
float Pass_select3_intSunrise <string UIName="Sunrise-RenderTarget256 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {1.0};
float Pass_select4_intSunrise <string UIName="Sunrise-RenderTarget128 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {0.85};
float Pass_select5_intSunrise <string UIName="Sunrise-RenderTarget64 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {0.65};
float Pass_select6_intSunrise <string UIName="Sunrise-RenderTarget32 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {0.45};
float Pass_select7_intSunrise <string UIName="Sunrise-RenderTarget16 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {0.45};
UI_WHITESPACE(2)  	
float Pass_select1E_int <string UIName="Day-RenderTarget1024 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {0.4};
float Pass_select2_intE <string UIName="Day-RenderTarget516 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {1.0};
float Pass_select3_intE <string UIName="Day-RenderTarget256 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {1.0};
float Pass_select4_intE <string UIName="Day-RenderTarget128 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {0.85};
float Pass_select5_intE <string UIName="Day-RenderTarget64 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {0.65};
float Pass_select6_intE <string UIName="Day-RenderTarget32 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {0.45};
float Pass_select7_intE <string UIName="Day-RenderTarget16 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {0.45};
UI_WHITESPACE(25)	
float Pass_select1_intSunset <string UIName="Sunset-RenderTarget1024 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {0.4};
float Pass_select2_intSunset <string UIName="Sunset-RenderTarget516 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {1.0};
float Pass_select3_intSunset <string UIName="Sunset-RenderTarget256 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {1.0};
float Pass_select4_intSunset <string UIName="Sunset-RenderTarget128 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {0.85};
float Pass_select5_intSunset <string UIName="Sunset-RenderTarget64 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {0.65};
float Pass_select6_intSunset <string UIName="Sunset-RenderTarget32 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {0.45};
float Pass_select7_intSunset <string UIName="Sunset-RenderTarget16 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {0.45};
UI_WHITESPACE(26)  	
float Pass_select1_intDusk <string UIName="Dusk-RenderTarget1024 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {0.4};
float Pass_select2_intDusk <string UIName="Dusk-RenderTarget516 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {1.0};
float Pass_select3_intDusk <string UIName="Dusk-RenderTarget256 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {1.0};
float Pass_select4_intDusk <string UIName="Dusk-RenderTarget128 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {0.85};
float Pass_select5_intDusk <string UIName="Dusk-RenderTarget64 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {0.65};
float Pass_select6_intDusk <string UIName="Dusk-RenderTarget32 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {0.45};
float Pass_select7_intDusk <string UIName="Dusk-RenderTarget16 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {0.45};
UI_WHITESPACE(27)  	
float Pass_select1_intNight <string UIName="Night-RenderTarget1024 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {0.4};
float Pass_select2_intNight <string UIName="Night-RenderTarget516 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {1.0};
float Pass_select3_intNight <string UIName="Night-RenderTarget256 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {1.0};
float Pass_select4_intNight <string UIName="Night-RenderTarget128 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {0.85};
float Pass_select5_intNight <string UIName="Night-RenderTarget64 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {0.65};
float Pass_select6_intNight <string UIName="Night-RenderTarget32 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {0.45};
float Pass_select7_intNight <string UIName="Night-RenderTarget16 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {0.45};
UI_WHITESPACE(22)  	
int InteriorMix <string UIName="-Interior - Bloom Mix-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
float IPass_select1_int <string UIName="Interior-RenderTarget1024 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {0.4};
float IPass_select2_int <string UIName="Interior-RenderTarget516 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {1.0};
float IPass_select3_int <string UIName="Interior-RenderTarget256 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {1.0};
float IPass_select4_int <string UIName="Interior-RenderTarget128 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {0.85};
float IPass_select5_int <string UIName="Interior-RenderTarget64 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {0.65};
float IPass_select6_int <string UIName="Interior-RenderTarget32 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {0.45};
float IPass_select7_int <string UIName="Interior-RenderTarget16 Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=5.0;float UIStep=0.001;> = {0.45};	
UI_WHITESPACE(4)  
//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#undef SHADERGROUP

#endif //SHADERGROUP

#undef TODIE
