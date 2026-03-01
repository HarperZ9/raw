//----------------------------------------------------------------------------------------------//
//								    SHADER WEATHER INDEX FILE									//
//----------------------------------------------------------------------------------------------//
//								==================================								//
//								//		Silent Horizons ENB     //								//
//								//								//								//
//								//		 by LonelyKitsuune      //								//
//								==================================								//
//----------------------------------------------------------------------------------------------//
//Weather index ranges from weatherlist.ini - required for weather specific shader effects


//NAT
#define ENABLE_WEATHERFX_SHADERS
#define ENABLE_SUNRAY_WEATHERSEPARATION
#define DISABLE_AURORA_WEATHERSEPARATION


//----------------------------------------- WEATHER FX -----------------------------------------//

#define WFX_RAINY_WEATHERS_START 11
#define WFX_RAINSTORMS_START     14
#define WFX_RAINY_WEATHERS_END   14

#define WFX_SNOWY_WEATHERS_START 15
#define WFX_SNOWSTORM            17
#define WFX_SNOWY_WEATHERS_END   17


//------------------------------------------ SUN RAYS ------------------------------------------//

#define SR_CLEAR_WEATHERS_START  1
#define SR_CLOUDY_WEATHERS_START 7
#define SR_RAINY_WEATHERS_START  11
#define SR_SNOWY_WEATHERS_START  15
#define SR_FOGGY_WEATHERS_START  18
#define SR_ASH_WEATHERS_START    21
#define SR_WEATHERS_END          22
