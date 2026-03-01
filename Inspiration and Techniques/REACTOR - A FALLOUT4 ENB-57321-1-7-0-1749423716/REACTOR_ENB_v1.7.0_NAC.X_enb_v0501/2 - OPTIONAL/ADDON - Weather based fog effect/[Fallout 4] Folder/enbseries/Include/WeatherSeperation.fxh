//////////////////////////////////////////////////////////////////////
//                                                                  //
//    ██████╗ ███████╗ █████╗  ██████╗████████╗ ██████╗ ██████╗     //
//    ██╔══██╗██╔════╝██╔══██╗██╔════╝╚══██╔══╝██╔═══██╗██╔══██╗    //
//    ██████╔╝█████╗  ███████║██║        ██║   ██║   ██║██████╔╝    //
//    ██╔══██╗██╔══╝  ██╔══██║██║        ██║   ██║   ██║██╔══██╗    //
//    ██║  ██║███████╗██║  ██║╚██████╗   ██║   ╚██████╔╝██║  ██║    //
//    ╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝ ╚═════╝   ╚═╝    ╚═════╝ ╚═╝  ╚═╝    //
//                                                                  //
//                         A FALLOUT4 ENB                           //
//                                                                  //
///// MOD PAGE ///////////////////////////////////////////////////////
//                                                                  //
//    https://www.nexusmods.com/fallout4/mods/57321                 //
//                                                                  //
//////////////////////////////////////////////////////////////////////
//                                                                  //
//    Weather Seperation file for NAC.X                             //
//                                                                  //
///// CREDITS ////////////////////////////////////////////////////////
//                                                                  //
//    Author of this file: Adyss                                    //
//                                                                  //
///// PLEASE DO NOT REDISTRIBUTE WITHOUT CREDITS /////////////////////



// Set Number of indexed Weathers. Starts at 1
#define NUM_WEATHERS                7

// PLEASANT
// Weather 1
#define PLEASANT_WEATHERS_START     1
#define PLEASANT_WEATHERS_END       6
#define PLEASANT_NUM                1

// OVERCAST
// Weather 2
#define OVERCAST_WEATHERS_START     7
#define OVERCAST_WEATHERS_END       15
#define OVERCAST_NUM                2

// FOG
// Weather 3
#define FOG_WEATHERS_START          16
#define FOG_WEATHERS_END            20
#define FOG_NUM                     3

// SNOW
// Weather 4
#define SNOW_WEATHERS_START         21
#define SNOW_WEATHERS_END           24
#define SNOW_NUM                    4

// RAD
// Weather 5
#define RAD_WEATHERS_START          25
#define RAD_WEATHERS_END            31
#define RAD_NUM                     5

// RAIN
// Weather 6
#define RAIN_WEATHERS_START         32
#define RAIN_WEATHERS_END           37
#define RAIN_NUM                    6

// STORM
// Weather 7
#define STORM_WEATHERS_START        38
#define STORM_WEATHERS_END          42
#define STORM_NUM                   7

// Interior
#define INTERIOR_NUM                8


// Returns Number of current weather group
int findCurrentWeather()
{
    int weatherNum = 0;

    // PLEASANT
    if(Weather.x >= PLEASANT_WEATHERS_START && Weather.x <= PLEASANT_WEATHERS_END)
        weatherNum = 1;

    // OVERCAST
    if(Weather.x >= OVERCAST_WEATHERS_START && Weather.x <= OVERCAST_WEATHERS_END)
        weatherNum = 2;

    // FOG
    if(Weather.x >= FOG_WEATHERS_START && Weather.x <= FOG_WEATHERS_END)
        weatherNum = 3;

    // SNOW
    if(Weather.x >= SNOW_WEATHERS_START && Weather.x <= SNOW_WEATHERS_END)
        weatherNum = 4;

    // RAD
    if(Weather.x >= RAD_WEATHERS_START && Weather.x <= RAD_WEATHERS_END)
        weatherNum = 5;

    // RAIN
    if(Weather.x >= RAIN_WEATHERS_START && Weather.x <= RAIN_WEATHERS_END)
        weatherNum = 6;

    // STORM
    if(Weather.x >= STORM_WEATHERS_START && Weather.x <= STORM_WEATHERS_END)
        weatherNum = 7;

    // Interior
    if(EInteriorFactor)
        weatherNum = 8;

    return weatherNum;
}

// Returns Number of Pervious weather group
int findPrevWeather()
{
    int weatherNum = 0;

    // PLEASANT
    if(Weather.y >= PLEASANT_WEATHERS_START && Weather.y <= PLEASANT_WEATHERS_END)
        weatherNum = 1;

    // OVERCAST
    if(Weather.y >= OVERCAST_WEATHERS_START && Weather.y <= OVERCAST_WEATHERS_END)
        weatherNum = 2;

    // FOG
    if(Weather.y >= FOG_WEATHERS_START && Weather.y <= FOG_WEATHERS_END)
        weatherNum = 3;

    // SNOW
    if(Weather.y >= SNOW_WEATHERS_START && Weather.y <= SNOW_WEATHERS_END)
        weatherNum = 4;

    // RAD
    if(Weather.y >= RAD_WEATHERS_START && Weather.y <= RAD_WEATHERS_END)
        weatherNum = 5;

    // RAIN
    if(Weather.y >= RAIN_WEATHERS_START && Weather.y <= RAIN_WEATHERS_END)
        weatherNum = 6;

    // STORM
    if(Weather.y >= STORM_WEATHERS_START && Weather.y <= STORM_WEATHERS_END)
        weatherNum = 7;

    // Interior
    if(EInteriorFactor)
        weatherNum = 8;

    return weatherNum;
}

// ty Trey <3
#define weatherLerp(array, val, current, next) \
    lerp(array##[next].##val, \
         array##[current].##val, \
         Weather.z)

// Less performant but fine if you only need one value
#define weatherLerpAuto(array, val) \
    lerp(array##[findNextWeather()].##val, \
         array##[findCurrentWeather()].##val, \
         Weather.z)