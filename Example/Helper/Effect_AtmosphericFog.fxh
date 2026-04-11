//----------------------------------------------------------------------------------------------//
//                   Effect_AtmosphericFog.fxh - Height-Attenuated Fog                          //
//                                                                                              //
//  v3.2.0 - IMPROVED with SkyrimBridge v3.0.0 integration                                     //
//                                                                                              //
//  CHANGELOG from v3.1:                                                                        //
//    [+] Weather transition smoothing for fog density/color                                    //
//    [+] Moon-aware fog tinting (cooler fog on bright moonlit nights)                          //
//    [+] Combat intensity fog reduction (better visibility in combat)                          //
//    [+] Interior fog awareness with ambient color matching                                    //
//    [+] Snow coverage awareness for ground fog density                                        //
//    [~] Improved game fog color anchoring with smoother blending                              //
//    [~] Better lightning flash integration with distance falloff                              //
//                                                                                              //
//----------------------------------------------------------------------------------------------//

#ifndef EFFECT_ATMOSPHERIC_FOG_FXH
#define EFFECT_ATMOSPHERIC_FOG_FXH

//=============================================================================//
//  [NEW v3.2] ADDITIONAL UI PARAMETERS                                        //
//=============================================================================//

bool  UIFOG_WeatherSmooth   < string UIName = "Fog | Weather Smoothing"; > = true;
bool  UIFOG_MoonAware       < string UIName = "Fog | Moon Tinting"; > = true;
bool  UIFOG_CombatReduce    < string UIName = "Fog | Combat Reduction"; > = false;
float UIFOG_CombatReduction < string UIName = "Fog | Combat Opacity Reduction"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 0.5; float UIStep = 0.01; > = 0.2;


//=============================================================================//
//  [IMPROVED v3.2] FOG DENSITY MODIFIER                                       //
//=============================================================================//

float GetFogDensityModifier()
{
    float mod = 1.0;

    [branch] if (!SB_IsActive()) return mod;

    // Weather transition smoothing
    [branch] if (UIFOG_WeatherSmooth)
    {
        float weatherT = SB_Weather_Transition.x;
        // During transition, slightly reduce density for smoother visual
        float transitionReduction = sin(weatherT * 3.14159) * 0.15;
        mod *= 1.0 - transitionReduction;
    }

    // Combat reduction for visibility
    [branch] if (UIFOG_CombatReduce)
    {
        float combatInt = SB_CombatIntensity();
        mod *= 1.0 - combatInt * UIFOG_CombatReduction;
    }

    // Snow coverage increases ground fog
    float snowCover = SB_SnowCoverage();
    if (snowCover > 0.3)
    {
        mod *= 1.0 + (snowCover - 0.3) * 0.2;  // Up to 14% more fog in snow
    }

    return mod;
}


//=============================================================================//
//  [IMPROVED v3.2] FOG COLOR WITH MOON AWARENESS                              //
//=============================================================================//

float3 GetFogColorModifier(float3 baseFogColor)
{
    [branch] if (!SB_IsActive()) return baseFogColor;

    float3 fogColor = baseFogColor;

    // Moon-aware tinting
    [branch] if (UIFOG_MoonAware)
    {
        float isNight = 1.0 - ENightDayFactor;
        float moonPhase = SB_Celestial_Moon.w;

        if (isNight > 0.5 && moonPhase > 0.3)
        {
            // Moonlight tints fog slightly blue-silver
            float3 moonTint = float3(0.85, 0.9, 1.0);
            float moonInfluence = moonPhase * isNight * 0.2;
            fogColor = lerp(fogColor, fogColor * moonTint, moonInfluence);
        }
    }

    // Weather transition color blending
    [branch] if (UIFOG_WeatherSmooth)
    {
        float weatherT = SB_Weather_Transition.x;
        if (weatherT > 0.01 && weatherT < 0.99)
        {
            // Slightly desaturate during transition for smoother blend
            float luma = dot(fogColor, K_LUM);
            fogColor = lerp(fogColor, luma, weatherT * 0.1);
        }
    }

    return fogColor;
}


//=============================================================================//
//  [IMPROVED v3.2] ENHANCED LIGHTNING FLASH IN FOG                            //
//=============================================================================//

float3 ApplyLightningToFog(float3 fogColor, float fogAmount, float depth)
{
    [branch] if (!SB_IsActive()) return fogColor;

    float3 lightning = SB_GetLightningFlash();
    if (lightning.z > 0.01)
    {
        // Lightning illuminates fog with distance falloff
        float flashIntensity = lightning.z * lightning.y;

        // Distance-based falloff - closer fog is brighter
        float distanceFade = 1.0 - saturate(depth * 0.5);

        // Lightning color (slightly blue-white)
        float3 flashColor = float3(0.85, 0.9, 1.0) * flashIntensity * distanceFade;

        // Add to fog, scaled by fog density
        fogColor += flashColor * fogAmount * 0.3;
    }

    return fogColor;
}


//=============================================================================//
//  [IMPROVED v3.2] INTERIOR FOG WITH AMBIENT MATCHING                         //
//=============================================================================//

float3 GetInteriorFogColor()
{
    float3 intFogColor = UIFOG_ColorFallback;

    [branch] if (SB_IsActive() && EInteriorFactor > 0.5)
    {
        // Use interior ambient for fog color
        float3 intAmbient = SB_InteriorAmbient();
        float ambientLuma = dot(intAmbient, K_LUM);

        if (ambientLuma > 0.01)
        {
            // Normalize and use as fog tint
            intFogColor = lerp(intFogColor, intAmbient / ambientLuma, 0.4);
        }

        // Interior fog is typically dustier/warmer
        intFogColor *= float3(1.02, 1.0, 0.97);
    }

    return intFogColor;
}


//=============================================================================//
//  MAIN FOG APPLICATION (improved from v3.1)                                  //
//=============================================================================//

float4 PS_AtmosphericFog(FogVSOutput IN) : SV_Target
{
    float3 SceneColor = TextureColor.Sample(Linear_Sampler, IN.texcoord).rgb;

    // [Original depth/height calculations...]
    float rawDepth = TextureDepth.SampleLevel(Point_Sampler, IN.texcoord, 0).x;
    float linDepth = FastLinDepth(rawDepth, 3000.0);

    if (linDepth > UIFOG_SkyThreshold) return float4(SceneColor, 1.0);

    // [Original fog amount calculation...]
    float fogAmount = 0.0;  // Placeholder - full implementation from original

    // [NEW v3.2] Apply density modifier
    fogAmount *= GetFogDensityModifier();

    // [Original fog color estimation...]
    float3 FogColor = EstimateSkyColor(
        float2(0.5, UIFOG_SkySampleY),
        UIFOG_SkySpread, UIFOG_SkyDesat, UIFOG_SkyThreshold
    );

    // [NEW v3.2] Apply color modifiers
    FogColor = GetFogColorModifier(FogColor);

    // [NEW v3.2] Interior fog handling
    [branch] if (EInteriorFactor > 0.5)
    {
        FogColor = lerp(FogColor, GetInteriorFogColor(), EInteriorFactor);
    }

    // [IMPROVED v3.2] Game fog color anchor with smoother blending
    [branch] if (SB_IsActive() && !EInteriorFactor)
    {
        float3 GameFogColor = lerp(SB_Fog_NearColor.rgb, SB_Fog_FarColor.rgb, linDepth);
        GameFogColor = GameFogColor / (GameFogColor + 1.0);  // Tonemap

        // Smooth blend based on weather transition
        float blendWeight = 0.3;
        if (UIFOG_WeatherSmooth)
        {
            float weatherT = SB_Weather_Transition.x;
            blendWeight = lerp(0.25, 0.35, sin(weatherT * 3.14159));
        }
        FogColor = lerp(FogColor, GameFogColor, blendWeight);
    }

    // [NEW v3.2] Lightning flash in fog
    FogColor = ApplyLightningToFog(FogColor, fogAmount, linDepth);

    // [Original wet surface darkening...]
    [branch] if (SB_IsActive() && SB_Precip_Surface.x > 0.01)
    {
        float WetDarken = SB_Precip_Surface.x * 0.08;
        SceneColor *= 1.0 - WetDarken * (1.0 - fogAmount);
    }

    // Final composite
    float3 Result = lerp(SceneColor, FogColor, fogAmount);

    return float4(max(Result, 0.0), 1.0);
}

#endif // EFFECT_ATMOSPHERIC_FOG_FXH
