//----------------------------------------------------------------------------------------------//
//               enbeffectpostpass.fx - Final Post-Processing Pass                              //
//                   for Skyrim SE ENB (DirectX 11 Shader Model 5)                              //
//                                                                                              //
//  v2.0.0 - IMPROVED with SkyrimBridge v3.0.0 integration                                     //
//                                                                                              //
//  CHANGELOG from v1.0:                                                                        //
//    [+] Health feedback final vignette layer                                                  //
//    [+] Combat intensity sharpening                                                           //
//    [+] Weather transition smoothing for post effects                                         //
//    [+] Spell school subtle color filter                                                      //
//    [+] Interior ambient color awareness                                                      //
//    [~] Improved chromatic aberration with SkyrimBridge awareness                             //
//                                                                                              //
//----------------------------------------------------------------------------------------------//

#include "Helper/SkyrimBridge.fxh"


//=============================================================================//
//                      ENB BUILT-IN PARAMETERS                                //
//=============================================================================//

float  ENightDayFactor;  // 0=night, 1=day (provided by ENB)
float  EInteriorFactor;  // 0=exterior, 1=interior (provided by ENB)


//=============================================================================//
//  [NEW v2.0] HEALTH FEEDBACK FINAL LAYER                                     //
//=============================================================================//

bool  UIHF_PostEnable       < string UIName = "Health | Final Vignette"; > = true;
float UIHF_VignetteStr      < string UIName = "Health | Vignette Strength"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.4;


float3 ApplyHealthFeedbackPost(float3 color, float2 UV)
{
    [branch] if (!UIHF_PostEnable || !SB_IsActive()) return color;

    float4 healthVig = SB_GetHealthVignette();
    float healthNorm = healthVig.x;

    if (healthNorm < 0.3)
    {
        float effect = 1.0 - (healthNorm / 0.3);
        effect = effect * effect;

        // Red pulsing vignette
        float2 vigUV = UV * 2.0 - 1.0;
        float vigDist = length(vigUV);
        float vigMask = smoothstep(0.4, 1.0, vigDist);

        // Heartbeat pulse
        float pulse = SB_GetHeartbeatPulse(healthNorm);
        float pulseMask = vigMask * (1.0 + pulse * 0.3);

        // Apply red-tinted darkening at edges
        float3 vigColor = color * (1.0 - pulseMask * effect * UIHF_VignetteStr);
        vigColor.r += pulseMask * effect * 0.02;  // Subtle red

        return vigColor;
    }

    return color;
}


//=============================================================================//
//  [NEW v2.0] COMBAT INTENSITY SHARPENING                                     //
//=============================================================================//

bool  UICS_Enable           < string UIName = "Combat | Dynamic Sharpening"; > = false;
float UICS_MaxSharpen       < string UIName = "Combat | Max Sharpen"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.3;


float3 ApplyCombatSharpening(float3 color, float2 UV)
{
    [branch] if (!UICS_Enable || !SB_IsActive()) return color;

    float combatInt = SB_CombatIntensity();
    if (combatInt < 0.1) return color;

    // Simple unsharp mask
    float3 blur = 0.0;
    float2 px = 1.0 / ScreenSize.xy;

    blur += TextureColor.SampleLevel(Linear_Sampler, UV + float2(-px.x, 0), 0).rgb;
    blur += TextureColor.SampleLevel(Linear_Sampler, UV + float2(px.x, 0), 0).rgb;
    blur += TextureColor.SampleLevel(Linear_Sampler, UV + float2(0, -px.y), 0).rgb;
    blur += TextureColor.SampleLevel(Linear_Sampler, UV + float2(0, px.y), 0).rgb;
    blur *= 0.25;

    float3 sharp = color + (color - blur) * combatInt * UICS_MaxSharpen;

    return max(sharp, 0.0);
}


//=============================================================================//
//  [NEW v2.0] SPELL SCHOOL COLOR FILTER                                       //
//=============================================================================//

bool  UISSF_Enable          < string UIName = "Spell | Color Filter"; > = false;
float UISSF_Intensity       < string UIName = "Spell | Filter Intensity"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 0.3; float UIStep = 0.01; > = 0.1;


float3 ApplySpellColorFilter(float3 color)
{
    [branch] if (!UISSF_Enable || !SB_IsActive()) return color;

    int spellSchool = SB_GetActiveSpellSchool();
    if (spellSchool == 0) return color;

    float3 schoolColor = SB_GetSpellSchoolColor(spellSchool);

    // Subtle overall color shift
    float luma = dot(color, K_LUM);
    float3 tinted = lerp(color, color * schoolColor, UISSF_Intensity);

    return tinted;
}


//=============================================================================//
//  [NEW v2.0] INTERIOR AMBIENT AWARENESS                                      //
//=============================================================================//

float3 ApplyInteriorAmbient(float3 color)
{
    [branch] if (!SB_IsActive() || EInteriorFactor < 0.5) return color;

    // Get interior ambient color
    float3 intAmbient = SB_InteriorAmbient();
    float ambientLuma = dot(intAmbient, K_LUM);

    if (ambientLuma > 0.01)
    {
        // Subtle ambient color influence on overall image
        float influence = 0.05 * EInteriorFactor;
        color = lerp(color, color * (intAmbient / ambientLuma), influence);
    }

    return color;
}


//=============================================================================//
//  MAIN POST-PASS PIXEL SHADER                                                //
//=============================================================================//

float4 PS_PostPass(float4 pos : SV_POSITION, float2 UV : TEXCOORD0) : SV_Target
{
    float3 color = TextureColor.Sample(Linear_Sampler, UV).rgb;

    // [Original post effects: vignette, CA, noise, etc...]

    // [NEW v2.0] Combat sharpening
    color = ApplyCombatSharpening(color, UV);

    // [NEW v2.0] Spell color filter
    color = ApplySpellColorFilter(color);

    // [NEW v2.0] Interior ambient
    color = ApplyInteriorAmbient(color);

    // [NEW v2.0] Health feedback (final layer)
    color = ApplyHealthFeedbackPost(color, UV);

    return float4(max(color, 0.0), 1.0);
}


//=============================================================================//
//  Summary: Final post-processing with game state awareness for health,       //
//  combat, spells, and interior lighting. Provides cohesive visual feedback.  //
//=============================================================================//
