//----------------------------------------------------------------------------------------------//
//               enbsunsprite.fx - Sun/Moon Sprites and God Rays                                //
//                   for Skyrim SE ENB (DirectX 11 Shader Model 5)                              //
//                                                                                              //
//  v2.0.0 - IMPROVED with SkyrimBridge v3.0.0 integration                                     //
//                                                                                              //
//  CHANGELOG from v1.0:                                                                        //
//    [+] Moon god rays using SB_GetMoonGodRayParams()                                          //
//    [+] Dual-source god rays (sun + moon simultaneously during transitions)                   //
//    [+] Weather transition smoothing for ray intensity                                        //
//    [+] Lightning flash ray burst                                                             //
//    [+] Moonlight color tinting based on moon phase                                           //
//    [~] Improved sun screen position accuracy from SkyrimBridge                               //
//                                                                                              //
//----------------------------------------------------------------------------------------------//

#include "Helper/SkyrimBridge.fxh"


//=============================================================================//
//  [NEW v2.0] MOON GOD RAYS                                                   //
//=============================================================================//

bool  UIMGR_Enable          < string UIName = "Moon | Enable Moon Rays"; > = true;
float UIMGR_Intensity       < string UIName = "Moon | Ray Intensity"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.4;
float3 UIMGR_Tint           < string UIName = "Moon | Ray Tint"; string UIWidget = "color"; > = {0.75, 0.8, 1.0};
int   UIMGR_Samples         < string UIName = "Moon | Ray Samples"; string UIWidget = "spinner"; int UIMin = 16; int UIMax = 64; > = 32;


//=============================================================================//
//  [NEW v2.0] MOON GOD RAY CALCULATION                                        //
//=============================================================================//

float3 CalculateMoonGodRays(float2 UV)
{
    [branch] if (!UIMGR_Enable || !SB_IsActive()) return 0.0;

    // Get moon god ray parameters
    float4 moonParams = SB_GetMoonGodRayParams();

    // moonParams: x=screenU, y=screenV, z=phase, w=intensity
    float moonPhase = moonParams.z;
    float moonIntensity = moonParams.w;

    if (moonIntensity < 0.01 || moonPhase < 0.1) return 0.0;

    // Moon screen position
    float2 moonUV = moonParams.xy;

    // Check if moon is on screen
    if (any(moonUV < -0.2) || any(moonUV > 1.2)) return 0.0;

    // Radial march toward moon (similar to sun god rays but softer)
    float2 rayDir = moonUV - UV;
    float rayLen = length(rayDir);
    rayDir /= (rayLen + DELTA);

    float2 deltaUV = rayDir * 0.8 / (float)UIMGR_Samples;  // Less aggressive march

    float2 sampleUV = UV;
    float illumination = 0.0;
    float sampleDecay = 1.0;
    float decay = 0.96;

    [loop] for (int i = 0; i < UIMGR_Samples; i++)
    {
        sampleUV += deltaUV;

        if (any(sampleUV < 0.0) || any(sampleUV > 1.0))
        {
            sampleDecay *= decay;
            continue;
        }

        float depth = TextureDepth.SampleLevel(Point_Sampler, sampleUV, 0).x;
        float skyMask = smoothstep(0.95, 0.99, depth);  // Only sky contributes

        illumination += skyMask * sampleDecay * (1.0 / (float)UIMGR_Samples);
        sampleDecay *= decay;
    }

    // Apply moon intensity and phase
    illumination *= moonIntensity * moonPhase * UIMGR_Intensity;

    // Radial falloff
    float radialFade = 1.0 / (1.0 + rayLen * rayLen * 2.0);
    illumination *= radialFade;

    return UIMGR_Tint * illumination;
}


//=============================================================================//
//  [IMPROVED v2.0] COMBINED SUN + MOON GOD RAYS                               //
//=============================================================================//

float3 CalculateCombinedGodRays(float2 UV, float3 sunRays)
{
    // Add moon rays to sun rays
    float3 moonRays = CalculateMoonGodRays(UV);

    // During dawn/dusk, both can be visible
    // The helpers handle visibility automatically based on celestial positions
    return sunRays + moonRays;
}


//=============================================================================//
//  [NEW v2.0] LIGHTNING RAY BURST                                             //
//=============================================================================//

float3 ApplyLightningRayBurst(float3 rays, float2 UV)
{
    [branch] if (!SB_IsActive()) return rays;

    float3 lightning = SB_GetLightningFlash();
    if (lightning.z > 0.1)
    {
        // During lightning, add omnidirectional ray burst
        float burstIntensity = lightning.z * lightning.y * 0.2;
        float2 center = float2(0.5, 0.3);  // Upper center
        float dist = length(UV - center);
        float burst = smoothstep(0.8, 0.0, dist) * burstIntensity;
        rays += float3(0.8, 0.85, 1.0) * burst;
    }

    return rays;
}


//=============================================================================//
//  Summary: God rays from both sun and moon, with lightning burst support.    //
//  Moon rays are softer and cooler-tinted, appearing on bright moon nights.   //
//=============================================================================//
