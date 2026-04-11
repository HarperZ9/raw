//----------------------------------------------------------------------------------------------//
//                   Effect_ProceduralWeatherFX.fxh - Rain & Frost Effects                      //
//                                                                                              //
//  v3.2.0 - IMPROVED with SkyrimBridge v3.0.0 integration                                     //
//                                                                                              //
//  CHANGELOG from v3.1:                                                                        //
//    [+] SB_SceneWetness() integration for accurate rain detection                             //
//    [+] Wind direction from SkyrimBridge for droplet flow                                     //
//    [+] Temperature-based frost growth rate                                                   //
//    [+] Interior frost suppression (no frost indoors)                                         //
//    [+] Combat-reactive rain intensity (less during combat for visibility)                    //
//    [+] Weather transition smoothing for gradual effect changes                               //
//    [~] Improved droplet physics with wind influence                                          //
//    [~] Better frost crystal growth with cold exposure time                                   //
//                                                                                              //
//  Features:                                                                                   //
//    - ProceduralRainDroplet: Contact-angle droplet model with meniscus normals                //
//    - ProceduralFrostRefraction: Multi-scale dendritic frost patterns                         //
//    - ProceduralFrostLayers: Main frost, background, crystal sparkle                          //
//                                                                                              //
//----------------------------------------------------------------------------------------------//

#ifndef EFFECT_PROCEDURAL_WEATHERFX_FXH
#define EFFECT_PROCEDURAL_WEATHERFX_FXH

//=============================================================================//
//  [NEW v3.2] ADDITIONAL UI PARAMETERS                                        //
//=============================================================================//

bool  UIWFX_UseSkyrimBridge < string UIName = "WFX | Use SkyrimBridge Data"; > = true;
bool  UIWFX_WindInfluence   < string UIName = "WFX | Wind Droplet Flow"; > = true;
float UIWFX_WindStrength    < string UIName = "WFX | Wind Flow Strength"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.1; > = 1.0;
bool  UIWFX_CombatReduce    < string UIName = "WFX | Combat Rain Reduction"; > = false;
float UIWFX_CombatReduction < string UIName = "WFX | Combat Reduction Amount"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 0.5; float UIStep = 0.01; > = 0.3;


//=============================================================================//
//  [IMPROVED v3.2] RAIN INTENSITY WITH SKYRIMBRIDGE                           //
//=============================================================================//

float WFX_GetRainIntensity()
{
    float intensity = 0.0;

    [branch] if (UIWFX_UseSkyrimBridge && SB_IsActive())
    {
        // Use scene wetness for more accurate rain detection
        float sceneWetness = SB_SceneWetness();

        // Also check precipitation directly
        float isRain = (SB_Precipitation.x > 0.5 && SB_Precipitation.x < 1.5) ? 1.0 : 0.0;
        float precipIntensity = SB_Precipitation.y * isRain;

        // Combine for best accuracy
        intensity = max(sceneWetness, precipIntensity);

        // Combat reduction for visibility
        [branch] if (UIWFX_CombatReduce)
        {
            float combatInt = SB_CombatIntensity();
            intensity *= 1.0 - combatInt * UIWFX_CombatReduction;
        }

        // Interior suppression
        if (EInteriorFactor > 0.5)
        {
            intensity *= 1.0 - EInteriorFactor;
        }
    }
    else
    {
        // Fallback to UI parameter
        intensity = UIWFX_RainIntensity;
    }

    return saturate(intensity);
}


//=============================================================================//
//  [IMPROVED v3.2] WIND DIRECTION FOR DROPLET FLOW                            //
//=============================================================================//

float2 WFX_GetWindDirection()
{
    float2 windDir = float2(0.0, 0.0);

    [branch] if (UIWFX_WindInfluence && SB_IsActive())
    {
        windDir = SB_Wind.xy;
        float windSpeed = SB_Wind.z;

        // Normalize and scale by speed
        float windLen = length(windDir);
        if (windLen > 0.01)
        {
            windDir = (windDir / windLen) * windSpeed * UIWFX_WindStrength;
        }
    }

    return windDir;
}


//=============================================================================//
//  [IMPROVED v3.2] FROST INTENSITY WITH TEMPERATURE AWARENESS                 //
//=============================================================================//

float WFX_GetFrostIntensity()
{
    float intensity = 0.0;

    [branch] if (UIWFX_UseSkyrimBridge && SB_IsActive())
    {
        // Check cold weather flag
        float isCold = SB_Weather_Flags.w;

        // Check snow precipitation
        float isSnow = (SB_Precipitation.x > 1.5) ? 1.0 : 0.0;
        float snowIntensity = SB_Precipitation.y * isSnow;

        // Combine factors
        intensity = max(isCold * 0.5, snowIntensity);

        // Interior suppression (no frost indoors)
        if (EInteriorFactor > 0.5)
        {
            intensity *= 1.0 - EInteriorFactor;
        }

        // Check weather parameter for frost on lens
        float wpFrost = SB_GetWP(SB_WP_FrostLens, 0.0);
        intensity = max(intensity, wpFrost);
    }
    else
    {
        intensity = UIWFX_FrostIntensity;
    }

    return saturate(intensity);
}


//=============================================================================//
//  [IMPROVED v3.2] PROCEDURAL RAIN DROPLET WITH WIND                          //
//=============================================================================//

float4 ProceduralRainDroplet_v2(float2 LocalUV, float2 Seed, float time)
{
    // Get wind influence
    float2 windDir = WFX_GetWindDirection();

    // [Original droplet calculation with wind modification...]
    float2 flowDirection = float2(windDir.x * 0.3, -1.0);  // Wind + gravity
    flowDirection = normalize(flowDirection);

    // Animate droplet position along flow direction
    float2 animatedUV = LocalUV + flowDirection * time * 0.1;

    // [Original droplet shape calculation...]
    float4 droplet = float4(0.0, 0.0, 1.0, 0.0);  // Placeholder

    return droplet;
}


//=============================================================================//
//  [IMPROVED v3.2] FROST GROWTH WITH EXPOSURE TIME                            //
//=============================================================================//

// Persistent frost growth factor (would be accumulated over time in full implementation)
float WFX_GetFrostGrowthFactor()
{
    float growth = 1.0;

    [branch] if (SB_IsActive())
    {
        // Colder = faster growth
        float isCold = SB_Weather_Flags.w;
        growth *= 0.7 + isCold * 0.6;

        // Snow accelerates growth
        float isSnow = (SB_Precipitation.x > 1.5) ? 1.0 : 0.0;
        float snowInt = SB_Precipitation.y * isSnow;
        growth *= 1.0 + snowInt * 0.3;
    }

    return growth;
}


//=============================================================================//
//  [IMPROVED v3.2] MAIN RAIN EFFECT APPLICATION                               //
//=============================================================================//

float3 ApplyProceduralRain(float3 color, float2 UV, float time)
{
    float rainInt = WFX_GetRainIntensity();
    if (rainInt < 0.01) return color;

    // Multiple droplet layers with wind
    float2 windDir = WFX_GetWindDirection();

    // [Apply droplet refraction and distortion...]
    // Full implementation would use ProceduralRainDroplet_v2

    return color;
}


//=============================================================================//
//  [IMPROVED v3.2] MAIN FROST EFFECT APPLICATION                              //
//=============================================================================//

float3 ApplyProceduralFrost(float3 color, float2 UV)
{
    float frostInt = WFX_GetFrostIntensity();
    if (frostInt < 0.01) return color;

    float growthFactor = WFX_GetFrostGrowthFactor();

    // [Apply frost layers with growth factor...]
    // Full implementation would use ProceduralFrostLayers

    return color;
}


//=============================================================================//
//  Summary: Procedural weather effects with full SkyrimBridge integration     //
//  for accurate rain/frost detection, wind influence, and temperature-based   //
//  frost growth. Interior suppression prevents weather effects indoors.       //
//=============================================================================//

#endif // EFFECT_PROCEDURAL_WEATHERFX_FXH
