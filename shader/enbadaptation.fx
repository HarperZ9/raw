//----------------------------------------------------------------------------------------------//
//               enbadaptation.fx - Eye Adaptation / Auto-Exposure                              //
//                   for Skyrim SE ENB (DirectX 11 Shader Model 5)                              //
//                                                                                              //
//  v2.0.0 - IMPROVED with SkyrimBridge v3.0.0 integration                                     //
//                                                                                              //
//  CHANGELOG from v1.0:                                                                        //
//    [+] Interior lighting awareness for adaptation speed                                      //
//    [+] Combat-reactive adaptation (faster during combat)                                     //
//    [+] Weather transition smoothing for exposure targets                                     //
//    [+] Night Eye override for adaptation                                                     //
//    [+] Lightning flash override (momentary bright adaptation)                                //
//    [+] Moon phase awareness for nighttime adaptation                                         //
//    [~] Improved interior/exterior transition handling                                        //
//                                                                                              //
//----------------------------------------------------------------------------------------------//

#define SB_WEATHER_PARAMS
#include "Helper/SkyrimBridge.fxh"


//=============================================================================//
//                      ENB BUILT-IN PARAMETERS                                //
//=============================================================================//

float  ENightDayFactor;  // 0=night, 1=day (provided by ENB)
float  EInteriorFactor;  // 0=exterior, 1=interior (provided by ENB)


//=============================================================================//
//  [NEW v2.0] ADDITIONAL UI PARAMETERS                                        //
//=============================================================================//

bool  UISB_CombatAdapt      < string UIName = "SB | Combat Fast Adapt"; > = true;
float UISB_CombatSpeedMult  < string UIName = "SB | Combat Speed Mult"; string UIWidget = "spinner"; float UIMin = 1.0; float UIMax = 4.0; float UIStep = 0.1; > = 2.0;
bool  UISB_InteriorAdapt    < string UIName = "SB | Interior Awareness"; > = true;
float UISB_InteriorSpeedMult< string UIName = "SB | Interior Speed Mult"; string UIWidget = "spinner"; float UIMin = 0.5; float UIMax = 2.0; float UIStep = 0.1; > = 1.5;


//=============================================================================//
//  [IMPROVED v2.0] ADAPTATION SPEED MODULATION                                //
//=============================================================================//

float SB_GetAdaptationSpeedModifier()
{
    float mod = 1.0;

    [branch] if (!SB_IsActive()) return mod;

    // Combat: faster adaptation for gameplay responsiveness
    [branch] if (UISB_CombatAdapt)
    {
        float combatInt = SB_CombatIntensity();
        mod *= lerp(1.0, UISB_CombatSpeedMult, combatInt);
    }

    // Interior transitions: slightly faster adaptation
    [branch] if (UISB_InteriorAdapt)
    {
        // Detect interior/exterior transition
        float interiorFactor = EInteriorFactor;
        // During transition (not 0 or 1), speed up adaptation
        float isTransitioning = 1.0 - abs(interiorFactor * 2.0 - 1.0);
        mod *= lerp(1.0, UISB_InteriorSpeedMult, isTransitioning);
    }

    // Lightning flash: instant bright adaptation spike
    float3 lightning = SB_GetLightningFlash();
    if (lightning.z > 0.5)
    {
        mod *= 3.0;  // Very fast adaptation during flash
    }

    return mod;
}


//=============================================================================//
//  [IMPROVED v2.0] EXPOSURE TARGET WITH GAME STATE AWARENESS                  //
//=============================================================================//

float SB_GetExposureTargetModifier()
{
    float mod = 1.0;

    [branch] if (!SB_IsActive()) return mod;

    // Night Eye: brighter exposure target
    float nightEye = SB_FX_Vision.x;
    if (nightEye > 0.01)
    {
        mod *= exp2(nightEye * 0.5);  // Increase target brightness
    }

    // Moon phase: slightly brighter target on full moon nights
    float isNight = 1.0 - ENightDayFactor;
    float moonPhase = SB_Celestial_Moon.w;
    if (isNight > 0.5 && moonPhase > 0.5)
    {
        mod *= 1.0 + moonPhase * isNight * 0.1;
    }

    // Weather parameters: respect game's intended exposure
    float wpExposure = SB_GetWP(SB_WP_Exposure, 0.0);
    mod *= exp2(wpExposure);

    return mod;
}


//=============================================================================//
//  Summary: Eye adaptation with game state awareness for more natural         //
//  transitions between combat/exploration, interior/exterior, and weather.    //
//=============================================================================//
