//=============================================================================
//  SkyrimBridge_CB.fxh — Constant buffer declaration for SkyrimBridge data
//
//  SkyrimBridge v3 pushes ALL game state data via a D3D11 constant buffer
//  bound to register(b7). This header declares the cbuffer with all 102
//  float4 fields in exact C++ AllData struct order (byte-for-byte match).
//
//  Usage: #include "Helper/SkyrimBridge_CB.fxh"
//  Then use SB_* variables directly (e.g., SB_Render_Frame.x for frameCount).
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#ifndef SKYRIMBRIDGE_CB_FXH
#define SKYRIMBRIDGE_CB_FXH

// Guard against the old extern-based header
#define SKYRIMBRIDGE_FXH 1

//─────────────────────────────────────────────────────────────────────────────
//  Constant buffer — 102 float4s = 1632 bytes at register(b7)
//
//  Field order MUST match C++ SB::AllData layout exactly.
//  DO NOT reorder, insert, or remove fields without updating BridgeData.h.
//─────────────────────────────────────────────────────────────────────────────

cbuffer SkyrimBridgeData : register(b7)
{
    // ── 1. Celestial (8 float4s) ────────────────────────────────────────
    float4 SB_Sun_NDC;             // .xy = NDC pos, .z = onScreen(0/1), .w = elevation angle (rad)
    float4 SB_Sun_Direction;       // .xyz = normalized world dir, .w = angular radius (rad)
    float4 SB_Sun_Color;           // .rgb = weather sunlight color, .a = sun glare factor
    float4 SB_Masser_NDC;         // .xy = NDC pos, .z = onScreen(0/1), .w = phase brightness [0,1]
    float4 SB_Masser_Direction;   // .xyz = normalized world dir, .w = elevation angle (rad)
    float4 SB_Secunda_NDC;        // .xy = NDC pos, .z = onScreen(0/1), .w = phase brightness [0,1]
    float4 SB_Secunda_Direction;  // .xyz = normalized world dir, .w = elevation angle (rad)
    float4 SB_Time;                // .x = gameHour [0,24), .y = sunriseHour, .z = sunsetHour, .w = dayProgress [0,1]

    // ── 2. Atmosphere (8 float4s) ───────────────────────────────────────
    float4 SB_Atmos_SkyUpper;     // .rgb = upper sky gradient
    float4 SB_Atmos_SkyLower;     // .rgb = lower sky gradient
    float4 SB_Atmos_Horizon;      // .rgb = horizon band color
    float4 SB_Atmos_Ambient;      // .rgb = ambient light color, .a = ambient intensity
    float4 SB_Atmos_Sunlight;     // .rgb = directional sunlight color, .a = sunlight scale
    float4 SB_Atmos_CloudDiffuse; // .rgb = cloud LOD diffuse tint
    float4 SB_Atmos_CloudAmbient; // .rgb = cloud LOD ambient tint
    float4 SB_Atmos_EffectLight;  // .rgb = magic/effect lighting color

    // ── 3. Fog (4 float4s) ──────────────────────────────────────────────
    float4 SB_Fog_NearColor;      // .rgb = near fog color, .a = near distance
    float4 SB_Fog_FarColor;       // .rgb = far fog color,  .a = far distance
    float4 SB_Fog_Density;        // .x = power curve, .y = maxOpacity [0,1], .z = isInteriorFog(0/1)
    float4 SB_Fog_Height;         // .x = waterSurfaceZ, .y = playerAltitude, .z = seaLevelDensity, .w = falloffRate

    // ── 4. Weather (6 float4s) ──────────────────────────────────────────
    float4 SB_Wind;                // .x = speed [0,1], .y = direction (radians)
    float4 SB_Precipitation;      // .x = type (0=none,1=rain,2=snow), .y = intensity [0,1]
    float4 SB_Lightning;          // .x = frequency, .y = isFlashing(0/1), .z = flashIntensity, .w = timeSinceFlash(sec)
    float4 SB_Weather_Flags;      // .x = isPleasant, .y = isCloudy, .z = isRainy, .w = isSnowy
    float4 SB_Weather_Transition; // .x = transition% [0,1], .y = outgoingWeatherID, .z = currentWeatherID
    float4 SB_Precip_Surface;     // .x = surface wetness [0,1], .y = puddle depth, .z = snow accumulation

    // ── 5. Player (5 float4s) ───────────────────────────────────────────
    float4 SB_Player_Position;    // .xyz = world position, .w = altitude above sea level
    float4 SB_Player_Vitals;      // .x = health%, .y = stamina%, .z = magicka%, .w = level
    float4 SB_Player_Movement;    // .x = speed (units/s), .y = isSprinting, .z = isSwimming, .w = isRiding
    float4 SB_Player_Combat;      // .x = inCombat(0/1), .y = isBleedout, .z = isKillMove, .w = weaponDrawn(0/1)
    float4 SB_Player_Water;       // .x = isUnderwater(0/1), .y = waterSurfaceZ, .z = submersionDepth, .w = isWading(0/1)

    // ── 6. Camera (23 float4s) ──────────────────────────────────────────
    float4 SB_Camera_Info;        // .x = FOV (degrees), .y = nearClip, .z = farClip, .w = aspectRatio
    float4 SB_Camera_Angles;      // .x = pitch (rad), .y = yaw (rad), .z = cameraStateEnum
    float4 SB_Camera_WorldPos;    // .xyz = camera world position

    // View matrix (world → camera) — 4 row vectors
    float4 SB_View_Row0;
    float4 SB_View_Row1;
    float4 SB_View_Row2;
    float4 SB_View_Row3;

    // Projection matrix — 4 row vectors
    float4 SB_Proj_Row0;
    float4 SB_Proj_Row1;
    float4 SB_Proj_Row2;
    float4 SB_Proj_Row3;

    // ViewProjection matrix — 4 row vectors
    float4 SB_ViewProj_Row0;
    float4 SB_ViewProj_Row1;
    float4 SB_ViewProj_Row2;
    float4 SB_ViewProj_Row3;

    // Previous frame ViewProjection (for motion vectors) — 4 row vectors
    float4 SB_PrevVP_Row0;
    float4 SB_PrevVP_Row1;
    float4 SB_PrevVP_Row2;
    float4 SB_PrevVP_Row3;

    // Inverse ViewProjection (for world reconstruction) — 4 row vectors
    float4 SB_InvVP_Row0;
    float4 SB_InvVP_Row1;
    float4 SB_InvVP_Row2;
    float4 SB_InvVP_Row3;

    // ── 7. Interior (6 float4s) ─────────────────────────────────────────
    float4 SB_Interior_Flags;     // .x = isInterior(0/1), .y = hasLightingTemplate
    float4 SB_Interior_Ambient;   // .rgb = interior ambient, .a = intensity
    float4 SB_Interior_DirColor;  // .rgb = interior directional light, .a = fade
    float4 SB_Interior_DirDir;    // .xyz = interior light direction
    float4 SB_Interior_FogColor;  // .rgb = interior fog color
    float4 SB_Interior_FogDist;   // .x = near, .y = far, .z = power, .w = clipDist

    // ── 8. Shadow (3 float4s) ───────────────────────────────────────────
    float4 SB_Shadow_Direction;   // .xyz = shadow caster direction (world), .w = shadow intensity
    float4 SB_Shadow_Diffuse;     // .rgb = shadow caster diffuse color
    float4 SB_Shadow_Ambient;     // .rgb = shadow caster ambient color

    // ── 9. Effects (4 float4s) ──────────────────────────────────────────
    float4 SB_FX_Vision;          // .x = nightEye(0/1), .y = detectLife(0/1), .z = detectDead(0/1), .w = etherealForm(0/1)
    float4 SB_FX_Time;            // .x = slowTimeFactor, .y = isTimeStopped
    float4 SB_FX_Damage;          // .x = isTakingFireDmg, .y = isTakingFrostDmg, .z = isTakingShockDmg, .w = isTakingPoisonDmg
    float4 SB_FX_Misc;            // .x = isInvisible, .y = isParalyzed, .z = isDrunk (skooma/ale)

    // ── 10. Render State (2 float4s) ────────────────────────────────────
    float4 SB_Render_Frame;       // .x = frameCount, .y = deltaTime (sec), .z = screenWidth, .w = screenHeight
    float4 SB_Render_Jitter;      // .xy = TAA jitter offset (NDC), .z = frameIndex%16

    // ── 11. ImageSpace (6 float4s) ──────────────────────────────────────
    float4 SB_IS_HDR;             // .x = eyeAdaptSpeed, .y = bloomScale, .z = bloomThreshold, .w = sunlightScale
    float4 SB_IS_Cinematic;       // .x = saturation, .y = brightness, .z = contrast, .w = tintAlpha
    float4 SB_IS_CineTint;        // .rgb = cinematic tint color
    float4 SB_IS_DOF;             // .x = strength, .y = distance, .z = range, .w = vignetteRadius
    float4 SB_IS_IMOD;            // .x = hasActiveIMOD(0/1), .y = imodStrength, .z = imodFadeIn, .w = imodElapsed
    float4 SB_IS_IMODTint;        // .rgb = IMOD tint color, .a = blur amount

    // ── 12. Nearby Lights (7 float4s) ───────────────────────────────────
    float4 SB_Light0_PosRad;     // .xyz = world position, .w = radius
    float4 SB_Light0_Color;      // .rgb = color, .a = intensity
    float4 SB_Light1_PosRad;
    float4 SB_Light1_Color;
    float4 SB_Light2_PosRad;
    float4 SB_Light2_Color;
    float4 SB_Light_Summary;     // .x = total nearby count, .y = nearest distance, .z = total luminous flux, .w = dominant hue [0,1]

    // ── 13. Actor Values (8 float4s) ────────────────────────────────────
    float4 SB_AV_Resist;          // .x = fireResist%, .y = frostResist%, .z = shockResist%, .w = magicResist%
    float4 SB_AV_Resist2;         // .x = poisonResist%, .y = diseaseResist%, .z = damageResist (armor)
    float4 SB_AV_Combat;          // .x = attackDamageMult, .y = weaponSpeedMult, .z = critChance, .w = unarmedDmg
    float4 SB_AV_Movement;        // .x = speedMult, .y = carryWeight, .z = inventoryWeight, .w = encumbranceRatio
    float4 SB_AV_SkillCombat;     // .x = oneHanded, .y = twoHanded, .z = archery, .w = block
    float4 SB_AV_SkillMagic;      // .x = alteration, .y = conjuration, .z = destruction, .w = illusion
    float4 SB_AV_SkillMagic2;     // .x = restoration, .y = enchanting, .z = alchemy
    float4 SB_AV_SkillStealth;    // .x = lightArmor, .y = sneak, .z = lockpicking, .w = pickpocket

    // ── 14. Crosshair (3 float4s) ──────────────────────────────────────
    float4 SB_XHair_Info;         // .x = hasTarget(0/1), .y = distance, .z = formType (enum), .w = isActor(0/1)
    float4 SB_XHair_Pos;          // .xyz = target world position, .w = boundingRadius
    float4 SB_XHair_Actor;        // .x = healthPct, .y = level, .z = isHostile(0/1), .w = isEssential(0/1)

    // ── 15. Equipment (4 float4s) ──────────────────────────────────────
    float4 SB_Equip_Right;        // .x = weaponType, .y = baseDamage, .z = isEnchanted(0/1), .w = enchantCharge [0,1]
    float4 SB_Equip_Left;         // .x = itemType, .y = damage/armorRating, .z = isEnchanted(0/1), .w = isSpell(0/1)
    float4 SB_Equip_Armor;        // .x = totalArmorRating, .y = isWearingHeavy(0/1), .z = isWearingLight(0/1), .w = isWearingRobes(0/1)
    float4 SB_Equip_Flags;        // .x = weaponDrawn(0/1), .y = hasBow(0/1), .z = hasTorch(0/1), .w = isTwoHanding(0/1)

    // ── 16. Quest (2 float4s) ───────────────────────────────────────────
    float4 SB_Quest_Progress;     // .x = mainQuestStage, .y = totalQuestsCompleted, .z = activeQuestCount, .w = activeObjectiveCount
    float4 SB_Quest_Tracked;      // .x = trackedQuestStage, .y = questType, .z = questFormID (low 16 bits), .w = hasObjectiveMarker(0/1)

    // ── 17. UI State (3 float4s) ────────────────────────────────────────
    float4 SB_UI_Menus;           // .x = isInMenu(0/1), .y = isInDialogue(0/1), .z = isInInventory(0/1), .w = isInMap(0/1)
    float4 SB_UI_HUD;             // .x = isHUDVisible(0/1), .y = isCrosshairVisible(0/1), .z = isInCinematicMode(0/1), .w = isLoading(0/1)
    float4 SB_UI_Detail;          // .x = isInCrafting(0/1), .y = isInBook(0/1), .z = isInLockpick(0/1), .w = isInConsole(0/1)
};

//─────────────────────────────────────────────────────────────────────────────
//  Helper functions — same API as the old extern-based SkyrimBridge.fxh
//─────────────────────────────────────────────────────────────────────────────

// Returns true when SkyrimBridge is actively pushing data
bool SB_IsActive() { return SB_Render_Frame.x > 0.0; }

// Linearize a hardware depth buffer value to view-space distance
float SB_LinearizeDepth(float rawDepth)
{
    float n = SB_Camera_Info.y;  // nearClip
    float f = SB_Camera_Info.z;  // farClip
    return n * f / (f - rawDepth * (f - n));
}

// Returns true between sunset and sunrise
bool SB_IsNight() { return SB_Time.x < SB_Time.y || SB_Time.x > SB_Time.z; }

// Compute sun position in [0,1] screen UV space from NDC
float2 SB_SunScreenUV()
{
    return float2(SB_Sun_NDC.x * 0.5 + 0.5, -SB_Sun_NDC.y * 0.5 + 0.5);
}

// Menu/UI state helpers (used by adaptation and DOF shaders)
bool SB_IsInMenu()         { return SB_UI_Menus.x > 0.5; }
bool SB_IsInDialogue()     { return SB_UI_Menus.y > 0.5; }
bool SB_IsLoading()        { return SB_UI_HUD.w > 0.5; }
bool SB_HasTorchEquipped() { return SB_Equip_Flags.z > 0.5; }

#endif // SKYRIMBRIDGE_CB_FXH
