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
//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#if LutUI == 1
int Line4 <string UIName="_________________________________________4";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
int LUTINT <string UIName=">>>                    Look Up Table            <<<";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
						   
bool ENABLE_LUT <string UIName = "Enable LUT ";> = {false};	 
bool ENABLE_DELU <string UIName = "Enable Defualt Lut";> = {false};
UI_WHITESPACE(38)
int ColorgradingSP <string UIName="-Pi-CHO ENB Extra Lut A-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
int		iCG 		< string UIName="Select Extra Lut A - Zero is OFF"; string UIWidget="spinner"; int UIMin=0; int UIMax=13;> = {0};
float 	CGAmountDay 	<string UIName="A intensity Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5}; 
float 	CGAmountNight 	<string UIName="A intensity Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};
float 	CGAmountInterior 	<string UIName="A intensity Interior Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	CGAmountInteriorNight 	<string UIName="A intensity Interior Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
UI_WHITESPACE(41)
int CAMERAFUNCTION <string UIName="-Pi-CHO ENB Extra Lut B-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
int		iDSLRType 	< string UIName="Select Extra Lut B - Zero is OFF"; string UIWidget="spinner"; int UIMin=0; int UIMax=5;> = {1}; 
float 	CAmountDay 	<string UIName="B intensity Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	CAmountNight 	<string UIName="B intensity Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	CAmountInterior 	<string UIName="B intensity Interior Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};  
float 	CAmountInteriorNight 	<string UIName="B intensity Interior Night "; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};    
UI_WHITESPACE(14)

int SHFilterSeperator <string UIName="-Silent Horizons LUT A Filter-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_PRC1 <string UIName = "SH - Base";> = {false};
bool ENABLE_SHKitsuneCut <string UIName = "SH Kisune Cut Default";> = {false};   
bool ENABLE_SHsummer <string UIName = "SH Summer";> = {false};
bool ENABLE_SHspring <string UIName = "SH Spring ";> = {false};
bool ENABLE_SHAuturmn <string UIName = "SH Auturmn ";> = {false};
bool ENABLE_SHCalmMoor <string UIName = "SH CalmMoor ";> = {false};
bool ENABLE_SHECReinforced <string UIName = "SH Eccentric Reinforced ";> = {false};
bool ENABLE_SHECSacrifice <string UIName = "SH Eccentric Sacrifice ";> = {false};

//bool ENABLE_TEST <string UIName = "SH Lofi Trance ";> = {false};
float 	SHAmountDay 	<string UIName="Silent Horizon Lut intensity Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	SHAmountNight 	<string UIName="Silent Horizon Lut intensity Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	SHAmountInterior 	<string UIName="Silent Horizon Lut intensity Interior Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	SHAmountInteriorNight 	<string UIName="Silent Horizon Lut intensity Interior Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
int SHFilterSeperatorB <string UIName="-Silent Horizons LUT B Filter-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_baseT <string UIName = "SH - Base B";> = {false};
bool ENABLE_SHWINTER <string UIName = "SH Winter ";> = {false};
bool ENABLE_SHECEcho <string UIName = "SH Eccentric Echo ";> = {false};
bool ENABLE_SHECburst <string UIName = "SH Eccentric Burst ";> = {false};
bool ENABLE_SHECDreamland<string UIName = "SH Eccentric Dreamland ";> = {false};
bool ENABLE_SHStroll <string UIName = "SH Stroll ";> = {false};
bool ENABLE_SHLofiFade <string UIName = "SH Lofi Fade ";> = {false};
bool ENABLE_SHLofiOLD <string UIName = "SH Lofi OLD ";> = {false};
bool ENABLE_SHLofiTrance <string UIName = "SH Lofi Trance ";> = {false};
float 	SHAmountDayB 	<string UIName="Silent Horizon Lut B intensity Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	SHAmountNightB 	<string UIName="Silent Horizon Lut B intensity Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	SHAmountInteriorB 	<string UIName="Silent Horizon Lut B intensity Interior Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	SHAmountInteriorNightB 	<string UIName="Silent Horizon Lut B intensity Interior Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
UI_WHITESPACE(4)
int SNAPDRAGONSPERATOR <string UIName="-Miiu Lut A-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
//int EXPLAIN <string UIName="----";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_SNAP <string UIName = "01 Ascension";> = {false};	
bool ENABLE_ALL <string UIName = "02 impression";> = {false};	
bool ENABLE_Creamy <string UIName = "03 Scorch Trial";> = {false};  
bool ENABLE_Toon <string UIName = "04 Bloosom";> = {false};  
bool ENABLE_Lost <string UIName = "05 Joker";> = {false};  
bool ENABLE_LOmo <string UIName = "06 Rome Cavalry ";> = {false};  
bool ENABLE_Drama <string UIName = "07 Ocean and Sky";> = {false};  
bool ENABLE_Silence <string UIName = "08 Penance";> = {false};  

bool ENABLE_Sparta <string UIName = "09 Old World";> = {false};  

bool ENABLE_Somber <string UIName = "10 Dark Age";> = {false};  
bool ENABLE_OLD <string UIName = "11 Turquoise";> = {false};  
bool ENABLE_Eccentric <string UIName = "12 Sediments";> = {false};  
bool ENABLE_Knox <string UIName = "13 Old Photo Black";> = {false};  
bool ENABLE_Senpai <string UIName = "14 Fluorite";> = {false};  
bool ENABLE_Overseer <string UIName = "15 Wake me up";> = {false};  
bool ENABLE_Beach <string UIName = "16 Vertigo";> = {false};  
bool ENABLE_Bay <string UIName = "17 Senpai";> = {false};  
bool ENABLE_Labamba <string UIName = "18 Ghost town";> = {false};   

float 	SNAPAAmountDay 	<string UIName="Miiu Lut A intensity Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	SNAPAAmountNight 	<string UIName="Miiu Lut A intensity Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	SNAPAAmountInterior 	<string UIName="Miiu Lut A intensity Interior Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	SNAPAAmountInteriorNight 	<string UIName="Miiu Lut A intensity Interior Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
UI_WHITESPACE(12)
int SNAPDRAGONSPERATORB <string UIName="-Miiu Lut B-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_Golden <string UIName = "01 Sunflower";> = {false};  
bool ENABLE_Aqua <string UIName = "02 Mythology";> = {false};  
bool ENABLE_UltraContrast <string UIName = "03 Distrust";> = {false};  
bool ENABLE_Vogue <string UIName = "04 Sing me to sleep";> = {false};  
bool ENABLE_Vintage <string UIName = "05 Dream";> = {false};  
bool ENABLE_Simple <string UIName = "06 Untruth World";> = {false};  
bool ENABLE_Creeper <string UIName = "07 Ego";> = {false};  
bool ENABLE_Surfin <string UIName = "08 Living Night";> = {false};  

float 	SNAPBAmountDay	<string UIName="Miiu Lut B intensity Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};
float 	SNAPBAmountNight 	<string UIName="Miiu Lut B intensity Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};
float 	SNAPBAmountInterior 	<string UIName="Miiu Lut B intensity Interior Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};
float 	SNAPBAmountInteriorNight 	<string UIName="Miiu Lut B intensity Interior Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
UI_WHITESPACE(15)
int MythicalSEPERA <string UIName="-TheDaedricDoll && Extra Lut-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_RUDYP1 <string UIName = "01 CRYSTAL_FRUIT";> = {false};
bool ENABLE_RUDYP2 <string UIName = "02 DEATHBELL_DREAMS";> = {false};
bool ENABLE_RUDYP3 <string UIName = "03 DIAMOND_EYES";> = {false};
bool ENABLE_RUDYP4 <string UIName = "04 ROSEBLOOD";> = {false};
bool ENABLE_RUDYP5 <string UIName = "05 DD_REALISM";> = {false}; 
bool ENABLE_Engage <string UIName = "Dark Base";> = {false};
bool ENABLE_UCON <string UIName = "Terrorism";> = {false};
float 	RudyAmountDay 	<string UIName="DD Lut intensity Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	RudyAmountNight 	<string UIName="DD Lut intensity Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	RudyAmountInterior 	<string UIName="DD Lut intensity Interior Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	RudyAmountInteriorNight 	<string UIName="DD Lut intensity Interio Nightr"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
UI_WHITESPACE(35)
int CaffeineSeparator <string UIName="-Miiu Lut C-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_Caffeine1 <string UIName = "01 Polar Regions";> = {false}; 
bool ENABLE_Caffeine2 <string UIName = "02 Blizzard";> = {false};   
bool ENABLE_Caffeine3 <string UIName = "03 Desert Strike";> = {false}; 
bool ENABLE_Caffeine4 <string UIName = "04 Salt and sun";> = {false};   
bool ENABLE_Caffeine5 <string UIName = "05 Vice";> = {false};   
bool ENABLE_Caffeine6 <string UIName = "06 Apocalypse";> = {false};   
bool ENABLE_Caffeine7 <string UIName = "07 Ohmu";> = {false};   
bool ENABLE_Caffeine8 <string UIName = "08 Amplitude";> = {false};   
bool ENABLE_Caffeine9 <string UIName = "09 Wax";> = {false};   
bool ENABLE_Caffeine10 <string UIName = "10 Tropical";> = {false};   
bool ENABLE_Caffeine11 <string UIName = "11 Papyrus";> = {false};   
bool ENABLE_Caffeine12 <string UIName = "12 Sahara";> = {false};   
bool ENABLE_Caffeine13 <string UIName = "13 Phantom";> = {false};   
bool ENABLE_Caffeine14 <string UIName = "14 Vineyard";> = {false};
float 	CaffeAmountDay 	<string UIName="Miiu Lut C intensity Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	CaffeAmountNight 	<string UIName="Miiu Lut C intensity Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	CaffeAmountInterior 	<string UIName="Miiu Lut C intensity Interior Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	CaffeAmountInteriorNight 	<string UIName="Miiu Lut C intensity Interior Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
UI_WHITESPACE(34)
int CaffeineSeparatorB <string UIName="-Miiu Lut D FIlter-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_Caffeine15 <string UIName = "01 Doze";> = {false};  
bool ENABLE_Caffeine16 <string UIName = "02 Wasteland";> = {false};   
bool ENABLE_Caffeine17 <string UIName = "03 Lake";> = {false};  
bool ENABLE_Caffeine18 <string UIName = "04 All Soft";> = {false};   
bool ENABLE_Caffeine19 <string UIName = "05 Lotus flower";> = {false};   
bool ENABLE_Caffeine20 <string UIName = "06 Deap Fog";> = {false};   
bool ENABLE_Caffeine21 <string UIName = "07 Sunny day ";> = {false};        
bool ENABLE_Caffeine22 <string UIName = "08 Dawn";> = {false};   
bool ENABLE_Caffeine23 <string UIName = "08 Parchment";> = {false};   
bool ENABLE_Caffeine24 <string UIName = "09 Concentration";> = {false};   
bool ENABLE_Caffeine25 <string UIName = "10 Dragon Snail";> = {false};   
bool ENABLE_Caffeine26 <string UIName = "11 Aqua";> = {false};   

float 	CaffeBAmountDay 	<string UIName="Miiu Lut D intensity Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	CaffeBAmountNight 	<string UIName="Miiu Lut D intensity Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	CaffeBAmountInterior 	<string UIName="Miiu Lut D intensity Interior Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
 float 	CaffeBAmountInteriorNight 	<string UIName="Miiu Lut D intensity Interior Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
UI_WHITESPACE(24)
int DiamondSeparator <string UIName="-Meonmeon_Diamond LUT A FIlter-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_MeonmeonLUT1 <string UIName = "MeonmeonLUT1 ";> = {false};    
bool ENABLE_MeonmeonLUT2 <string UIName = "MeonmeonLUT2 ";> = {false};   
bool ENABLE_MeonmeonLUT3 <string UIName = "MeonmeonLUT3 ";> = {false};   
bool ENABLE_MeonmeonLUT4 <string UIName = "MeonmeonLUT4 ";> = {false};   
bool ENABLE_MeonmeonLUT5 <string UIName = "MeonmeonLUT5 ";> = {false};   
bool ENABLE_MeonmeonLUT6 <string UIName = "MeonmeonLUT6 ";> = {false};   
bool ENABLE_MeonmeonLUT7 <string UIName = "MeonmeonLUT7 ";> = {false};   
bool ENABLE_MeonmeonLUT8 <string UIName = "MeonmeonLUT8 ";> = {false};   
bool ENABLE_MeonmeonLUT9 <string UIName = "MeonmeonLUT9 ";> = {false};   
bool ENABLE_MeonmeonLUT10 <string UIName = "MeonmeonLUT10 ";> = {false};   
bool ENABLE_MeonmeonLUT11 <string UIName = "MeonmeonLUT11 ";> = {false};   
bool ENABLE_MeonmeonLUT12 <string UIName = "MeonmeonLUT12 ";> = {false}; 
float 	MeonAAmountDay 	<string UIName="Meonmeon A Lut intensity Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	MeonAAmountNight 	<string UIName="Meonmeon A Lut intensity Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};  
float 	MeonAAmountInterior 	<string UIName="Meonmeon A Lut intensity Interior Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};  
float 	MeonAAmountInteriorNight	<string UIName="Meonmeon A Lut intensity Interior Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};  
UI_WHITESPACE(32)
int DiamondSeparatorB <string UIName="-Meonmeon_Diamond LUT B FIlter-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};  
bool ENABLE_MeonmeonLUT13 <string UIName = "MeonmeonLUT13 ";> = {false};   
bool ENABLE_MeonmeonLUT14 <string UIName = "MeonmeonLUT14 ";> = {false};   
bool ENABLE_MeonmeonLUT15 <string UIName = "MeonmeonLUT15 ";> = {false};   
bool ENABLE_MeonmeonLUT16 <string UIName = "MeonmeonLUT16 ";> = {false};   
bool ENABLE_MeonmeonLUT17 <string UIName = "MeonmeonLUT17 ";> = {false};   
bool ENABLE_MeonmeonLUT18 <string UIName = "MeonmeonLUT18 ";> = {false};   
bool ENABLE_MeonmeonLUT19 <string UIName = "MeonmeonLUT19 ";> = {false};   
bool ENABLE_MeonmeonLUT20 <string UIName = "MeonmeonLUT20 ";> = {false};   
bool ENABLE_MeonmeonLUT21 <string UIName = "MeonmeonLUT21 ";> = {false};   
bool ENABLE_MeonmeonLUT22 <string UIName = "MeonmeonLUT22 ";> = {false};   
bool ENABLE_MeonmeonLUT23 <string UIName = "MeonmeonLUT23 ";> = {false};   
bool ENABLE_MeonmeonLUT24 <string UIName = "MeonmeonLUT24 ";> = {false};   
float 	MeonBAmountDay 	<string UIName="Meonmeon B Lut intensity Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	MeonBAmountNight 	<string UIName="Meonmeon B Lut intensity Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	MeonBAmountInterior 	<string UIName="Meonmeon B Lut intensity Interior Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	MeonBAmountInteriorNight 	<string UIName="Meonmeon B Lut intensity Interior Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
#undef LutUI
//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif LutUI == 2
int Line4 <string UIName="_________________________________________4";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
int LUTINT <string UIName=">>>                    Look Up Table            <<<";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_LUT <string UIName = "Enable LUT ";> = {false};	 
bool ENABLE_DELU <string UIName = "Enable Defualt Lut";> = {false};		
				   

UI_WHITESPACE(38)
int ColorgradingSP <string UIName="-Pi-CHO ENB Extra Lut A-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
int		iCG 		< string UIName="Select Extra Lut A - Zero is OFF"; string UIWidget="spinner"; int UIMin=0; int UIMax=13;> = {0};
float 	CGAmountDay 	<string UIName="A intensity Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5}; 
float 	CGAmountNight 	<string UIName="A intensity Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};
float 	CGAmountInterior 	<string UIName="A intensity Interior Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	CGAmountInteriorNight 	<string UIName="A intensity Interior Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
UI_WHITESPACE(41)
int CAMERAFUNCTION <string UIName="-Pi-CHO ENB Extra Lut B-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
int		iDSLRType 	< string UIName="Select Extra Lut B - Zero is OFF"; string UIWidget="spinner"; int UIMin=0; int UIMax=5;> = {1}; 
float 	CAmountDay 	<string UIName="B intensity Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	CAmountNight 	<string UIName="B intensity Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	CAmountInterior 	<string UIName="B intensity Interior Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};  
float 	CAmountInteriorNight 	<string UIName="B intensity Interior Night "; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};    
UI_WHITESPACE(14)

int SHFilterSeperator <string UIName="-Silent Horizons LUT A Filter-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_PRC1 <string UIName = "SH - Base";> = {false};
bool ENABLE_SHKitsuneCut <string UIName = "SH Kisune Cut Default";> = {false};   
bool ENABLE_SHsummer <string UIName = "SH Summer";> = {false};
bool ENABLE_SHspring <string UIName = "SH Spring ";> = {false};
bool ENABLE_SHAuturmn <string UIName = "SH Auturmn ";> = {false};
bool ENABLE_SHCalmMoor <string UIName = "SH CalmMoor ";> = {false};
bool ENABLE_SHECReinforced <string UIName = "SH Eccentric Reinforced ";> = {false};
bool ENABLE_SHECSacrifice <string UIName = "SH Eccentric Sacrifice ";> = {false};

//bool ENABLE_TEST <string UIName = "SH Lofi Trance ";> = {false};
float 	SHAmountDay 	<string UIName="Silent Horizon Lut intensity Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	SHAmountNight 	<string UIName="Silent Horizon Lut intensity Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	SHAmountInterior 	<string UIName="Silent Horizon Lut intensity Interior Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	SHAmountInteriorNight 	<string UIName="Silent Horizon Lut intensity Interior Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
int SHFilterSeperatorB <string UIName="-Silent Horizons LUT B Filter-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_baseT <string UIName = "SH - Base B";> = {false};
bool ENABLE_SHWINTER <string UIName = "SH Winter ";> = {false};
bool ENABLE_SHECEcho <string UIName = "SH Eccentric Echo ";> = {false};
bool ENABLE_SHECburst <string UIName = "SH Eccentric Burst ";> = {false};
bool ENABLE_SHECDreamland<string UIName = "SH Eccentric Dreamland ";> = {false};
bool ENABLE_SHStroll <string UIName = "SH Stroll ";> = {false};
bool ENABLE_SHLofiFade <string UIName = "SH Lofi Fade ";> = {false};
bool ENABLE_SHLofiOLD <string UIName = "SH Lofi OLD ";> = {false};
bool ENABLE_SHLofiTrance <string UIName = "SH Lofi Trance ";> = {false};
float 	SHAmountDayB 	<string UIName="Silent Horizon Lut B intensity Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	SHAmountNightB 	<string UIName="Silent Horizon Lut B intensity Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	SHAmountInteriorB 	<string UIName="Silent Horizon Lut B intensity Interior Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	SHAmountInteriorNightB 	<string UIName="Silent Horizon Lut B intensity Interior Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
UI_WHITESPACE(4)
int SNAPDRAGONSPERATOR <string UIName="-Snapdragon Lut A-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
//int EXPLAIN <string UIName="----";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_SNAP <string UIName = "01_Snapdragon";> = {false};	
bool ENABLE_ALL <string UIName = "02_All-Is-Vain";> = {false};	
bool ENABLE_Creamy <string UIName = "03_Creamy";> = {false};  
bool ENABLE_Toon <string UIName = "04_Toon";> = {false};  
bool ENABLE_Lost <string UIName = "05_Lost in Time";> = {false};  
bool ENABLE_LOmo <string UIName = "06_Lomo";> = {false};  
bool ENABLE_Drama <string UIName = "07_Drama";> = {false};  
bool ENABLE_Silence <string UIName = "08_Silence";> = {false};  
bool ENABLE_Sparta <string UIName = "09_Sparta";> = {false};  
bool ENABLE_Somber <string UIName = "10_Somber";> = {false};  
bool ENABLE_OLD <string UIName = "11_Old World";> = {false};  
bool ENABLE_Eccentric <string UIName = "12_Eccentric";> = {false};  
bool ENABLE_Knox <string UIName = "13_Knox";> = {false};  
bool ENABLE_Senpai <string UIName = "14_Senpai";> = {false};  
bool ENABLE_Overseer <string UIName = "15_Overseer";> = {false};  
bool ENABLE_Beach <string UIName = "16_Beach Boy";> = {false};  
bool ENABLE_Bay <string UIName = "17_Baywatch";> = {false};  
bool ENABLE_Labamba <string UIName = "18_La Bamba";> = {false};   

float 	SNAPAAmountDay 	<string UIName="Snapdragon Lut A intensity Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	SNAPAAmountNight 	<string UIName="Snapdragon Lut A intensity Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	SNAPAAmountInterior 	<string UIName="Snapdragon Lut A intensity Interior Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	SNAPAAmountInteriorNight 	<string UIName="Snapdragon Lut A intensity Interior Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
UI_WHITESPACE(12)
int SNAPDRAGONSPERATORB <string UIName="-Snapdragon Lut B-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_Golden <string UIName = "19_Golden Hour";> = {false};  
bool ENABLE_Aqua <string UIName = "20_Aqua";> = {false};  
bool ENABLE_UltraContrast <string UIName = "21_Ultra Contrast";> = {false};  
bool ENABLE_Vogue <string UIName = "22_Vogue";> = {false};  
bool ENABLE_Vintage <string UIName = "23_Vintage BW";> = {false};  
bool ENABLE_Simple <string UIName = "24_Simple BW";> = {false};  
bool ENABLE_Creeper <string UIName = "25_Creeper";> = {false};  
bool ENABLE_Surfin <string UIName = "26_Surfin Bird";> = {false};  

float 	SNAPBAmountDay	<string UIName="Snapdragon Lut B intensity Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};
float 	SNAPBAmountNight 	<string UIName="Snapdragon Lut B intensity Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};
float 	SNAPBAmountInterior 	<string UIName="Snapdragon Lut B intensity Interior Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};
float 	SNAPBAmountInteriorNight 	<string UIName="Snapdragon Lut B intensity Interior Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
UI_WHITESPACE(15)
int MythicalSEPERA <string UIName="-TheDaedricDoll && Extra Lut-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_RUDYP2 <string UIName = "RUDY";> = {false};
bool ENABLE_RUDYP1 <string UIName = "DD_CRYSTAL_FRUIT";> = {false};
bool ENABLE_RUDYP3 <string UIName = "DD_DEATHBELL_DREAMS";> = {false};
bool ENABLE_RUDYP4 <string UIName = "DD_DIAMOND_EYES";> = {false};
bool ENABLE_RUDYP5 <string UIName = "DD_ELDER_LIGHT";> = {false}; 
bool ENABLE_Engage <string UIName = "Re - Engaged";> = {false};
bool ENABLE_UCON <string UIName = "Terrorism";> = {false};
float 	RudyAmountDay 	<string UIName="DD Lut intensity Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	RudyAmountNight 	<string UIName="DD Lut intensity Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	RudyAmountInterior 	<string UIName="DD Lut intensity Interior Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	RudyAmountInteriorNight 	<string UIName="DD Lut intensity Interio Nightr"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
UI_WHITESPACE(35)
int CaffeineSeparator <string UIName="-Caffeine Lut A-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_Caffeine1 <string UIName = "Caffeine01";> = {false}; 
bool ENABLE_Caffeine2 <string UIName = "Caffeine02";> = {false};   
bool ENABLE_Caffeine3 <string UIName = "Caffeine03";> = {false};   
bool ENABLE_Caffeine4 <string UIName = "Caffeine04";> = {false};   
bool ENABLE_Caffeine5 <string UIName = "Caffeine05";> = {false};   
bool ENABLE_Caffeine6 <string UIName = "Caffeine06";> = {false};   
bool ENABLE_Caffeine7 <string UIName = "Caffeine07";> = {false};   
bool ENABLE_Caffeine8 <string UIName = "Caffeine08";> = {false};   
bool ENABLE_Caffeine9 <string UIName = "Caffeine09";> = {false};   
bool ENABLE_Caffeine10 <string UIName = "Caffeine10";> = {false};   
bool ENABLE_Caffeine11 <string UIName = "Caffeine11";> = {false};   
bool ENABLE_Caffeine12 <string UIName = "Caffeine12";> = {false};   
bool ENABLE_Caffeine13 <string UIName = "Caffeine13";> = {false};   
bool ENABLE_Caffeine14 <string UIName = "Caffeine14";> = {false};   
float 	CaffeAmountDay 	<string UIName="Caffeine A Lut intensity Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	CaffeAmountNight 	<string UIName="Caffeine A Lut intensity Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	CaffeAmountInterior 	<string UIName="Caffeine A Lut intensity Interior Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	CaffeAmountInteriorNight 	<string UIName="Caffeine A Lut intensity Interior Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
UI_WHITESPACE(34)
int CaffeineSeparatorB <string UIName="-Caffeine B Lut FIlter-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_Caffeine15 <string UIName = "Caffeine15";> = {false};  
bool ENABLE_Caffeine16 <string UIName = "Caffeine16";> = {false};   
bool ENABLE_Caffeine17 <string UIName = "Caffeine17";> = {false};   
bool ENABLE_Caffeine18 <string UIName = "Caffeine18";> = {false};   
bool ENABLE_Caffeine19 <string UIName = "Caffeine19";> = {false};   
bool ENABLE_Caffeine20 <string UIName = "Caffeine20";> = {false};   
bool ENABLE_Caffeine21 <string UIName = "Caffeine21";> = {false};        
bool ENABLE_Caffeine22 <string UIName = "Caffeine22";> = {false};   
bool ENABLE_Caffeine23 <string UIName = "Caffeine23";> = {false};   
bool ENABLE_Caffeine24 <string UIName = "Caffeine24";> = {false};   
bool ENABLE_Caffeine25 <string UIName = "Caffeine25";> = {false};   
bool ENABLE_Caffeine26 <string UIName = "Caffeine26";> = {false};  

float 	CaffeBAmountDay 	<string UIName="Caffeine B Lut intensity Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	CaffeBAmountNight 	<string UIName="Caffeine B Lut intensity Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	CaffeBAmountInterior 	<string UIName="Caffeine B Lut intensity Interior Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
 float 	CaffeBAmountInteriorNight 	<string UIName="Caffeine B Lut intensity Interior Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
UI_WHITESPACE(24)
int DiamondSeparator <string UIName="-Meonmeon_Diamond LUT A FIlter-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_MeonmeonLUT1 <string UIName = "MeonmeonLUT1 ";> = {false};    
bool ENABLE_MeonmeonLUT2 <string UIName = "MeonmeonLUT2 ";> = {false};   
bool ENABLE_MeonmeonLUT3 <string UIName = "MeonmeonLUT3 ";> = {false};   
bool ENABLE_MeonmeonLUT4 <string UIName = "MeonmeonLUT4 ";> = {false};   
bool ENABLE_MeonmeonLUT5 <string UIName = "MeonmeonLUT5 ";> = {false};   
bool ENABLE_MeonmeonLUT6 <string UIName = "MeonmeonLUT6 ";> = {false};   
bool ENABLE_MeonmeonLUT7 <string UIName = "MeonmeonLUT7 ";> = {false};   
bool ENABLE_MeonmeonLUT8 <string UIName = "MeonmeonLUT8 ";> = {false};   
bool ENABLE_MeonmeonLUT9 <string UIName = "MeonmeonLUT9 ";> = {false};   
bool ENABLE_MeonmeonLUT10 <string UIName = "MeonmeonLUT10 ";> = {false};   
bool ENABLE_MeonmeonLUT11 <string UIName = "MeonmeonLUT11 ";> = {false};   
bool ENABLE_MeonmeonLUT12 <string UIName = "MeonmeonLUT12 ";> = {false}; 
float 	MeonAAmountDay 	<string UIName="Meonmeon A Lut intensity Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	MeonAAmountNight 	<string UIName="Meonmeon A Lut intensity Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};  
float 	MeonAAmountInterior 	<string UIName="Meonmeon A Lut intensity Interior Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};  
float 	MeonAAmountInteriorNight	<string UIName="Meonmeon A Lut intensity Interior Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};  
UI_WHITESPACE(32)
int DiamondSeparatorB <string UIName="-Meonmeon_Diamond LUT B FIlter-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};  
bool ENABLE_MeonmeonLUT13 <string UIName = "MeonmeonLUT13 ";> = {false};   
bool ENABLE_MeonmeonLUT14 <string UIName = "MeonmeonLUT14 ";> = {false};   
bool ENABLE_MeonmeonLUT15 <string UIName = "MeonmeonLUT15 ";> = {false};   
bool ENABLE_MeonmeonLUT16 <string UIName = "MeonmeonLUT16 ";> = {false};   
bool ENABLE_MeonmeonLUT17 <string UIName = "MeonmeonLUT17 ";> = {false};   
bool ENABLE_MeonmeonLUT18 <string UIName = "MeonmeonLUT18 ";> = {false};   
bool ENABLE_MeonmeonLUT19 <string UIName = "MeonmeonLUT19 ";> = {false};   
bool ENABLE_MeonmeonLUT20 <string UIName = "MeonmeonLUT20 ";> = {false};   
bool ENABLE_MeonmeonLUT21 <string UIName = "MeonmeonLUT21 ";> = {false};   
bool ENABLE_MeonmeonLUT22 <string UIName = "MeonmeonLUT22 ";> = {false};   
bool ENABLE_MeonmeonLUT23 <string UIName = "MeonmeonLUT23 ";> = {false};   
bool ENABLE_MeonmeonLUT24 <string UIName = "MeonmeonLUT24 ";> = {false};   
float 	MeonBAmountDay 	<string UIName="Meonmeon B Lut intensity Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	MeonBAmountNight 	<string UIName="Meonmeon B Lut intensity Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	MeonBAmountInterior 	<string UIName="Meonmeon B Lut intensity Interior Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	MeonBAmountInteriorNight 	<string UIName="Meonmeon B Lut intensity Interior Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
#undef LutUI

//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif LutUI == 3
int Line4 <string UIName="_________________________________________4";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
int LUTINT <string UIName=">>>                    Look Up Table            <<<";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_LUT <string UIName = "Enable LUT ";> = {false};	 
bool ENABLE_DELU <string UIName = "Enable Defualt Lut";> = {false};		
				   

UI_WHITESPACE(38)
int ColorgradingSP <string UIName="-Pi-CHO ENB Extra Lut A-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
int		iCG 		< string UIName="Select Extra Lut A - Zero is OFF"; string UIWidget="spinner"; int UIMin=0; int UIMax=13;> = {0};
float 	CGAmountDay 	<string UIName="A intensity Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5}; 
float 	CGAmountNight 	<string UIName="A intensity Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};
float 	CGAmountInterior 	<string UIName="A intensity Interior Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	CGAmountInteriorNight 	<string UIName="A intensity Interior Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
UI_WHITESPACE(41)
int CAMERAFUNCTION <string UIName="-Pi-CHO ENB Extra Lut B-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
int		iDSLRType 	< string UIName="Select Extra Lut B - Zero is OFF"; string UIWidget="spinner"; int UIMin=0; int UIMax=5;> = {1}; 
float 	CAmountDay 	<string UIName="B intensity Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	CAmountNight 	<string UIName="B intensity Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	CAmountInterior 	<string UIName="B intensity Interior Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};  
float 	CAmountInteriorNight 	<string UIName="B intensity Interior Night "; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};    
UI_WHITESPACE(14)

int SHFilterSeperator <string UIName="-Silent Horizons LUT A Filter-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_PRC1 <string UIName = "SH - Base";> = {false};
bool ENABLE_SHKitsuneCut <string UIName = "SH Kisune Cut Default";> = {false};   
bool ENABLE_SHsummer <string UIName = "SH Summer";> = {false};
bool ENABLE_SHspring <string UIName = "SH Spring ";> = {false};
bool ENABLE_SHAuturmn <string UIName = "SH Auturmn ";> = {false};
bool ENABLE_SHCalmMoor <string UIName = "SH CalmMoor ";> = {false};
bool ENABLE_SHECReinforced <string UIName = "SH Eccentric Reinforced ";> = {false};
bool ENABLE_SHECSacrifice <string UIName = "SH Eccentric Sacrifice ";> = {false};

//bool ENABLE_TEST <string UIName = "SH Lofi Trance ";> = {false};
float 	SHAmountDay 	<string UIName="Silent Horizon Lut intensity Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	SHAmountNight 	<string UIName="Silent Horizon Lut intensity Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	SHAmountInterior 	<string UIName="Silent Horizon Lut intensity Interior Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	SHAmountInteriorNight 	<string UIName="Silent Horizon Lut intensity Interior Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
int SHFilterSeperatorB <string UIName="-Silent Horizons LUT B Filter-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_baseT <string UIName = "SH - Base B";> = {false};
bool ENABLE_SHWINTER <string UIName = "SH Winter ";> = {false};
bool ENABLE_SHECEcho <string UIName = "SH Eccentric Echo ";> = {false};
bool ENABLE_SHECburst <string UIName = "SH Eccentric Burst ";> = {false};
bool ENABLE_SHECDreamland<string UIName = "SH Eccentric Dreamland ";> = {false};
bool ENABLE_SHStroll <string UIName = "SH Stroll ";> = {false};
bool ENABLE_SHLofiFade <string UIName = "SH Lofi Fade ";> = {false};
bool ENABLE_SHLofiOLD <string UIName = "SH Lofi OLD ";> = {false};
bool ENABLE_SHLofiTrance <string UIName = "SH Lofi Trance ";> = {false};
float 	SHAmountDayB 	<string UIName="Silent Horizon Lut B intensity Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	SHAmountNightB 	<string UIName="Silent Horizon Lut B intensity Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	SHAmountInteriorB 	<string UIName="Silent Horizon Lut B intensity Interior Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	SHAmountInteriorNightB 	<string UIName="Silent Horizon Lut B intensity Interior Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
UI_WHITESPACE(4)
int SNAPDRAGONSPERATOR <string UIName="-Snapdragon Lut A by tetrodoxin-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
//int EXPLAIN <string UIName="----";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_SNAP <string UIName = "01 Snapdragon";> = {false};	
bool ENABLE_ALL <string UIName = "02_All-Is-Vain";> = {false};	
bool ENABLE_Creamy <string UIName = "03_Creamy";> = {false};  
bool ENABLE_Toon <string UIName = "04_Toon";> = {false};  
bool ENABLE_Lost <string UIName = "05_Lost in Time";> = {false};  
bool ENABLE_LOmo <string UIName = "06_Lomo";> = {false};  
bool ENABLE_Drama <string UIName = "07_Drama";> = {false};  
bool ENABLE_Silence <string UIName = "08_Silence";> = {false};  
bool ENABLE_Sparta <string UIName = "09_Sparta";> = {false};  
bool ENABLE_Somber <string UIName = "10_Somber";> = {false};  
bool ENABLE_OLD <string UIName = "11_Old World";> = {false};  
bool ENABLE_Eccentric <string UIName = "12_Eccentric";> = {false};  
bool ENABLE_Knox <string UIName = "13_Knox";> = {false};  
bool ENABLE_Senpai <string UIName = "14_Senpai";> = {false};  
bool ENABLE_Overseer <string UIName = "15_Overseer";> = {false};  
bool ENABLE_Beach <string UIName = "16_Beach Boy";> = {false};  
bool ENABLE_Bay <string UIName = "17_Baywatch";> = {false};  
bool ENABLE_Labamba <string UIName = "18_La Bamba";> = {false};   

float 	SNAPAAmountDay 	<string UIName="Snapdragon Lut A intensity Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	SNAPAAmountNight 	<string UIName="Snapdragon Lut A intensity Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	SNAPAAmountInterior 	<string UIName="Snapdragon Lut A intensity Interior Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	SNAPAAmountInteriorNight 	<string UIName="Snapdragon Lut A intensity Interior Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
UI_WHITESPACE(12)
int SNAPDRAGONSPERATORB <string UIName="-Snapdragon Lut B by tetrodoxin-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_Golden <string UIName = "19_Golden Hour";> = {false};  
bool ENABLE_Aqua <string UIName = "20_Aqua";> = {false};  
bool ENABLE_UltraContrast <string UIName = "21_Ultra Contrast";> = {false};  
bool ENABLE_Vogue <string UIName = "22_Vogue";> = {false};  
bool ENABLE_Vintage <string UIName = "23_Vintage BW";> = {false};  
bool ENABLE_Simple <string UIName = "24_Simple BW";> = {false};  
bool ENABLE_Creeper <string UIName = "25_Creeper";> = {false};  
bool ENABLE_Surfin <string UIName = "26_Surfin Bird";> = {false};  

float 	SNAPBAmountDay	<string UIName="Snapdragon Lut B intensity Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};
float 	SNAPBAmountNight 	<string UIName="Snapdragon Lut B intensity Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};
float 	SNAPBAmountInterior 	<string UIName="Snapdragon Lut B intensity Interior Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};
float 	SNAPBAmountInteriorNight 	<string UIName="Snapdragon Lut B intensity Interior Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
UI_WHITESPACE(15)
int MythicalSEPERA <string UIName="-TheDaedricDoll && Extra Lut-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_RUDYP1 <string UIName = "01 CRYSTAL_FRUIT";> = {false};
bool ENABLE_RUDYP2 <string UIName = "02 DEATHBELL_DREAMS";> = {false};
bool ENABLE_RUDYP3 <string UIName = "03 DIAMOND_EYES";> = {false};
bool ENABLE_RUDYP4 <string UIName = "04 ROSEBLOOD";> = {false};
bool ENABLE_RUDYP5 <string UIName = "05 DD_REALISM";> = {false}; 
bool ENABLE_Engage <string UIName = "Dark Base";> = {false};
bool ENABLE_UCON <string UIName = "Terrorism";> = {false};
float 	RudyAmountDay 	<string UIName="DD Lut intensity Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	RudyAmountNight 	<string UIName="DD Lut intensity Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	RudyAmountInterior 	<string UIName="DD Lut intensity Interior Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	RudyAmountInteriorNight 	<string UIName="DD Lut intensity Interio Nightr"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
UI_WHITESPACE(35)
int CaffeineSeparator <string UIName="-Miiu Lut C-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_Caffeine1 <string UIName = "01 Polar Regions";> = {false}; 
bool ENABLE_Caffeine2 <string UIName = "02 Blizzard";> = {false};   
bool ENABLE_Caffeine3 <string UIName = "03 Desert Strike";> = {false}; 
bool ENABLE_Caffeine4 <string UIName = "04 Salt and sun";> = {false};   
bool ENABLE_Caffeine5 <string UIName = "05 Vice";> = {false};   
bool ENABLE_Caffeine6 <string UIName = "06 Apocalypse";> = {false};   
bool ENABLE_Caffeine7 <string UIName = "07 Ohmu";> = {false};   
bool ENABLE_Caffeine8 <string UIName = "08 Amplitude";> = {false};   
bool ENABLE_Caffeine9 <string UIName = "09 Wax";> = {false};   
bool ENABLE_Caffeine10 <string UIName = "10 Tropical";> = {false};   
bool ENABLE_Caffeine11 <string UIName = "11 Papyrus";> = {false};   
bool ENABLE_Caffeine12 <string UIName = "12 Sahara";> = {false};   
bool ENABLE_Caffeine13 <string UIName = "13 Phantom";> = {false};   
bool ENABLE_Caffeine14 <string UIName = "14 Vineyard";> = {false};
float 	CaffeAmountDay 	<string UIName="Miiu Lut C intensity Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	CaffeAmountNight 	<string UIName="Miiu Lut C intensity Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	CaffeAmountInterior 	<string UIName="Miiu Lut C intensity Interior Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	CaffeAmountInteriorNight 	<string UIName="Miiu Lut C intensity Interior Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
UI_WHITESPACE(34)
int CaffeineSeparatorB <string UIName="-Miiu Lut D FIlter-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_Caffeine15 <string UIName = "01 Doze";> = {false};  
bool ENABLE_Caffeine16 <string UIName = "02 Wasteland";> = {false};   
bool ENABLE_Caffeine17 <string UIName = "03 Lake";> = {false};  
bool ENABLE_Caffeine18 <string UIName = "04 All Soft";> = {false};   
bool ENABLE_Caffeine19 <string UIName = "05 Lotus flower";> = {false};   
bool ENABLE_Caffeine20 <string UIName = "06 Deap Fog";> = {false};   
bool ENABLE_Caffeine21 <string UIName = "07 Sunny day ";> = {false};        
bool ENABLE_Caffeine22 <string UIName = "08 Dawn";> = {false};   
bool ENABLE_Caffeine23 <string UIName = "08 Parchment";> = {false};   
bool ENABLE_Caffeine24 <string UIName = "09 Concentration";> = {false};   
bool ENABLE_Caffeine25 <string UIName = "10 Dragon Snail";> = {false};   
bool ENABLE_Caffeine26 <string UIName = "Tetro (Snapdragon)";> = {false};   

float 	CaffeBAmountDay 	<string UIName="Miiu Lut D intensity Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	CaffeBAmountNight 	<string UIName="Miiu Lut D intensity Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	CaffeBAmountInterior 	<string UIName="Miiu Lut D intensity Interior Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
 float 	CaffeBAmountInteriorNight 	<string UIName="Miiu Lut D intensity Interior Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
UI_WHITESPACE(24)
int DiamondSeparator <string UIName="-Meonmeon_Diamond LUT A FIlter-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool ENABLE_MeonmeonLUT1 <string UIName = "MeonmeonLUT1 ";> = {false};    
bool ENABLE_MeonmeonLUT2 <string UIName = "MeonmeonLUT2 ";> = {false};   
bool ENABLE_MeonmeonLUT3 <string UIName = "MeonmeonLUT3 ";> = {false};   
bool ENABLE_MeonmeonLUT4 <string UIName = "MeonmeonLUT4 ";> = {false};   
bool ENABLE_MeonmeonLUT5 <string UIName = "MeonmeonLUT5 ";> = {false};   
bool ENABLE_MeonmeonLUT6 <string UIName = "MeonmeonLUT6 ";> = {false};   
bool ENABLE_MeonmeonLUT7 <string UIName = "MeonmeonLUT7 ";> = {false};   
bool ENABLE_MeonmeonLUT8 <string UIName = "MeonmeonLUT8 ";> = {false};   
bool ENABLE_MeonmeonLUT9 <string UIName = "MeonmeonLUT9 ";> = {false};   
bool ENABLE_MeonmeonLUT10 <string UIName = "MeonmeonLUT10 ";> = {false};   
bool ENABLE_MeonmeonLUT11 <string UIName = "MeonmeonLUT11 ";> = {false};   
bool ENABLE_MeonmeonLUT12 <string UIName = "MeonmeonLUT12 ";> = {false}; 
float 	MeonAAmountDay 	<string UIName="Meonmeon A Lut intensity Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	MeonAAmountNight 	<string UIName="Meonmeon A Lut intensity Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};  
float 	MeonAAmountInterior 	<string UIName="Meonmeon A Lut intensity Interior Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};  
float 	MeonAAmountInteriorNight	<string UIName="Meonmeon A Lut intensity Interior Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};  
UI_WHITESPACE(32)
int DiamondSeparatorB <string UIName="-Meonmeon_Diamond LUT B FIlter-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};  
bool ENABLE_MeonmeonLUT13 <string UIName = "MeonmeonLUT13 ";> = {false};   
bool ENABLE_MeonmeonLUT14 <string UIName = "MeonmeonLUT14 ";> = {false};   
bool ENABLE_MeonmeonLUT15 <string UIName = "MeonmeonLUT15 ";> = {false};   
bool ENABLE_MeonmeonLUT16 <string UIName = "MeonmeonLUT16 ";> = {false};   
bool ENABLE_MeonmeonLUT17 <string UIName = "MeonmeonLUT17 ";> = {false};   
bool ENABLE_MeonmeonLUT18 <string UIName = "MeonmeonLUT18 ";> = {false};   
bool ENABLE_MeonmeonLUT19 <string UIName = "MeonmeonLUT19 ";> = {false};   
bool ENABLE_MeonmeonLUT20 <string UIName = "MeonmeonLUT20 ";> = {false};   
bool ENABLE_MeonmeonLUT21 <string UIName = "MeonmeonLUT21 ";> = {false};   
bool ENABLE_MeonmeonLUT22 <string UIName = "MeonmeonLUT22 ";> = {false};   
bool ENABLE_MeonmeonLUT23 <string UIName = "MeonmeonLUT23 ";> = {false};   
bool ENABLE_MeonmeonLUT24 <string UIName = "MeonmeonLUT24 ";> = {false};   
float 	MeonBAmountDay 	<string UIName="Meonmeon B Lut intensity Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	MeonBAmountNight 	<string UIName="Meonmeon B Lut intensity Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	MeonBAmountInterior 	<string UIName="Meonmeon B Lut intensity Interior Day"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
float 	MeonBAmountInteriorNight 	<string UIName="Meonmeon B Lut intensity Interior Night"; string UIWidget="Spinner";    float UIMin=0.0;  float UIMax=1.000; float UIStep=0.01;> = {0.5};   
#endif //SHADERGROUP

#undef TODIE