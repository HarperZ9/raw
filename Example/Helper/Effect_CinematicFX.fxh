//----------------------------------------------------------------------------------------------//
//                   Effect_CinematicFX.fxh - Film Emulation Effects                            //
//                                                                                              //
//  v2.0.0 - IMPROVED with SkyrimBridge v3.0.0 integration                                     //
//                                                                                              //
//  CHANGELOG from v1.0:                                                                        //
//    [+] Combat-reactive halation boost (more intense during action)                           //
//    [+] Health feedback halation (red halo when damaged)                                      //
//    [+] Gate weave intensity modulation (calmer in peaceful scenes)                           //
//    [+] Spell casting halation coloring                                                       //
//    [+] Lightning flash halation burst                                                        //
//    [+] Cinematic letterbox during killcam                                                    //
//                                                                                              //
//  Features:                                                                                   //
//    - Film Halation (light bleed around bright areas)                                         //
//    - Gate Weave (subtle frame instability)                                                   //
//    - Cinematic Letterbox (aspect ratio bars)                                                 //
//    - Film Grain (procedural noise)                                                           //
//                                                                                              //
//----------------------------------------------------------------------------------------------//

#ifndef EFFECT_CINEMATIC_FX_FXH
#define EFFECT_CINEMATIC_FX_FXH

//=============================================================================//
//  [NEW v2.0] ADDITIONAL UI PARAMETERS                                        //
//=============================================================================//

// Combat-reactive halation
bool  UIHAL_CombatBoost     < string UIName = "Halation | Combat Boost"; > = true;
float UIHAL_CombatMult      < string UIName = "Halation | Combat Multiplier"; string UIWidget = "spinner"; float UIMin = 1.0; float UIMax = 2.0; float UIStep = 0.1; > = 1.3;

// Health feedback halation
bool  UIHAL_HealthFeedback  < string UIName = "Halation | Health Feedback"; > = true;
float UIHAL_DamageRed       < string UIName = "Halation | Damage Red Shift"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.4;

// Killcam letterbox
bool  UILB_KillcamAuto      < string UIName = "Letterbox | Auto Killcam"; > = true;
float UILB_KillcamAspect    < string UIName = "Letterbox | Killcam Aspect"; string UIWidget = "spinner"; float UIMin = 1.5; float UIMax = 2.4; float UIStep = 0.05; > = 2.35;


//=============================================================================//
//  [IMPROVED v2.0] FILM HALATION WITH GAME STATE AWARENESS                    //
//=============================================================================//

float GetHalationIntensityModifier()
{
    float mod = 1.0;

    [branch] if (!SB_IsActive()) return mod;

    // Combat boost
    [branch] if (UIHAL_CombatBoost)
    {
        float combatInt = SB_CombatIntensity();
        mod *= lerp(1.0, UIHAL_CombatMult, combatInt);
    }

    // Lightning flash burst
    float3 lightning = SB_GetLightningFlash();
    if (lightning.z > 0.3)
    {
        mod *= 1.0 + lightning.z * 0.5;
    }

    // Spell casting boost
    int spellSchool = SB_GetActiveSpellSchool();
    if (spellSchool > 0)
    {
        mod *= 1.15;  // 15% boost while casting
    }

    return mod;
}

float3 GetHalationColorModifier(float3 baseHalation)
{
    [branch] if (!SB_IsActive()) return baseHalation;

    float3 halation = baseHalation;

    // Health feedback - red shift when damaged
    [branch] if (UIHAL_HealthFeedback)
    {
        float4 healthVig = SB_GetHealthVignette();
        float healthNorm = healthVig.x;

        if (healthNorm < 0.4)
        {
            float effect = 1.0 - (healthNorm / 0.4);
            // Shift halation toward red
            halation.r *= 1.0 + effect * UIHAL_DamageRed;
            halation.gb *= 1.0 - effect * UIHAL_DamageRed * 0.3;
        }
    }

    // Spell school coloring
    int spellSchool = SB_GetActiveSpellSchool();
    if (spellSchool > 0)
    {
        float3 schoolColor = SB_GetSpellSchoolColor(spellSchool);
        halation = lerp(halation, halation * schoolColor, 0.2);
    }

    return halation;
}


//=============================================================================//
//  [IMPROVED v2.0] GATE WEAVE WITH SCENE AWARENESS                            //
//=============================================================================//

float GetGateWeaveIntensityModifier()
{
    float mod = 1.0;

    [branch] if (!SB_IsActive()) return mod;

    // Reduce weave in calm scenes, increase in action
    float combatInt = SB_CombatIntensity();

    // Base calm = 0.7x, combat = 1.2x
    mod = lerp(0.7, 1.2, combatInt);

    // Menu/dialogue = very calm
    if (SB_UI_Menus.x > 0.5 || SB_UI_Menus.y > 0.5)
    {
        mod *= 0.3;
    }

    return mod;
}


//=============================================================================//
//  [NEW v2.0] AUTOMATIC KILLCAM LETTERBOX                                     //
//=============================================================================//

float GetLetterboxAmount(float baseAmount)
{
    float amount = baseAmount;

    [branch] if (!SB_IsActive() || !UILB_KillcamAuto) return amount;

    // Auto-letterbox during killcam
    float inKillcam = SB_Player_Combat.z;
    if (inKillcam > 0.5)
    {
        // Calculate letterbox for cinematic aspect ratio
        float screenAspect = ScreenSize.x / ScreenSize.y;
        float targetAspect = UILB_KillcamAspect;

        if (targetAspect > screenAspect)
        {
            float barHeight = (1.0 - screenAspect / targetAspect) * 0.5;
            amount = max(amount, barHeight * inKillcam);
        }
    }

    return amount;
}


//=============================================================================//
//  [IMPROVED v2.0] MAIN HALATION PIXEL SHADER                                 //
//=============================================================================//

float4 PS_FilmHalation(CineFXVSOutput IN) : SV_Target
{
    float3 color = TextureColor.Sample(Linear_Sampler, IN.texcoord).rgb;

    // [Original halation calculation...]
    float3 halation = 0.0;  // Placeholder

    // [NEW v2.0] Apply intensity modifier
    float intMod = GetHalationIntensityModifier();
    halation *= intMod;

    // [NEW v2.0] Apply color modifier
    halation = GetHalationColorModifier(halation);

    // Additive composite
    color += halation;

    return float4(max(color, 0.0), 1.0);
}


//=============================================================================//
//  [IMPROVED v2.0] MAIN GATE WEAVE PIXEL SHADER                               //
//=============================================================================//

float4 PS_GateWeave(WeaveVSOutput IN) : SV_Target
{
    // [Original weave calculation...]
    float2 weaveOffset = 0.0;  // Placeholder

    // [NEW v2.0] Apply intensity modifier
    float weaveMod = GetGateWeaveIntensityModifier();
    weaveOffset *= weaveMod;

    float2 sampleUV = IN.texcoord + weaveOffset;
    float3 color = TextureColor.Sample(Linear_Sampler, sampleUV).rgb;

    return float4(color, 1.0);
}


//=============================================================================//
//  [IMPROVED v2.0] MAIN LETTERBOX PIXEL SHADER                                //
//=============================================================================//

float4 PS_Letterbox(CineFXVSOutput IN) : SV_Target
{
    float3 color = TextureColor.Sample(Linear_Sampler, IN.texcoord).rgb;

    float barAmount = UILB_Amount;

    // [NEW v2.0] Killcam auto-letterbox
    barAmount = GetLetterboxAmount(barAmount);

    // Apply letterbox bars
    float2 uv = IN.texcoord;
    if (uv.y < barAmount || uv.y > (1.0 - barAmount))
    {
        color = UILB_BarColor;
    }

    return float4(color, 1.0);
}

#endif // EFFECT_CINEMATIC_FX_FXH
