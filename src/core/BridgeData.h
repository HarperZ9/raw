#pragma once
//=============================================================================
//  BridgeData.h — The data contract between SkyrimBridge and ENB shaders
//
//  Every parameter pushed to ENB is defined here as a named float4.
//  The HLSL mirror is in shader/Helper/SkyrimBridge.fxh (v2.0).
//
//  NAMING: SB_ prefix avoids collision with ENB/game parameters.
//  PACKING: One float4 per semantic group.
//  Total: 102 float4 params across 17 domains.
//=============================================================================

#include <cstdint>
#include <cstddef>

namespace SB
{
    // ── Utility ─────────────────────────────────────────────────────────
    struct Float4 {
        float x = 0.f, y = 0.f, z = 0.f, w = 0.f;
    };

    struct Float4x4 {
        Float4 row[4];
    };

    // ── 1. CELESTIAL ────────────────────────────────────────────────────

    struct CelestialData
    {
        Float4 SunNDC;          // .xy = NDC pos, .z = onScreen(0/1), .w = elevation angle (rad)
        Float4 SunDirection;    // .xyz = normalized world dir, .w = angular radius (rad)
        Float4 SunColor;        // .rgb = weather sunlight color, .a = sun glare factor

        Float4 MasserNDC;       // .xy = NDC pos, .z = onScreen(0/1), .w = phase brightness [0,1]
        Float4 MasserDirection; // .xyz = normalized world dir, .w = elevation angle (rad)

        Float4 SecundaNDC;      // .xy = NDC pos, .z = onScreen(0/1), .w = phase brightness [0,1]
        Float4 SecundaDirection;// .xyz = normalized world dir, .w = elevation angle (rad)

        Float4 TimeData;        // .x = gameHour [0,24), .y = sunriseHour, .z = sunsetHour, .w = dayProgress [0,1]
    };

    // ── 2. ATMOSPHERE & SKY COLORS ──────────────────────────────────────

    struct AtmosphereData
    {
        Float4 SkyUpper;        // .rgb = upper sky gradient, .a = 0
        Float4 SkyLower;        // .rgb = lower sky gradient, .a = 0
        Float4 Horizon;         // .rgb = horizon band color, .a = 0
        Float4 Ambient;         // .rgb = ambient light color, .a = ambient intensity (normalized)
        Float4 SunlightColor;   // .rgb = directional sunlight color, .a = sunlight scale
        Float4 CloudLODDiffuse; // .rgb = cloud LOD diffuse tint, .a = 0
        Float4 CloudLODAmbient; // .rgb = cloud LOD ambient tint, .a = 0
        Float4 EffectLighting;  // .rgb = magic/effect lighting color, .a = 0
    };

    // ── 3. FOG ──────────────────────────────────────────────────────────

    struct FogData
    {
        Float4 NearColor;       // .rgb = near fog color, .a = near distance
        Float4 FarColor;        // .rgb = far fog color,  .a = far distance
        Float4 Density;         // .x = power curve, .y = maxOpacity [0,1], .z = isInteriorFog(0/1), .w = 0
        Float4 HeightFog;       // .x = waterSurfaceZ, .y = playerAltitude, .z = seaLevelDensity, .w = falloffRate
    };

    // ── 4. WEATHER ──────────────────────────────────────────────────────

    struct WeatherData
    {
        Float4 Wind;            // .x = speed [0,1], .y = direction (radians), .z = 0, .w = 0
        Float4 Precipitation;   // .x = type (0=none,1=rain,2=snow), .y = intensity [0,1], .z = 0, .w = 0
        Float4 Lightning;       // .x = frequency, .y = isFlashing(0/1), .z = flashIntensity, .w = timeSinceFlash(sec)
        Float4 Flags;           // .x = isPleasant, .y = isCloudy, .z = isRainy, .w = isSnowy
        Float4 Transition;      // .x = transition% [0,1], .y = outgoingWeatherID, .z = currentWeatherID, .w = 0
        Float4 PrecipSurface;   // .x = surface wetness [0,1], .y = puddle depth, .z = snow accumulation, .w = 0
    };

    // ── 5. PLAYER ───────────────────────────────────────────────────────

    struct PlayerData
    {
        Float4 Position;        // .xyz = world position, .w = altitude above sea level
        Float4 Vitals;          // .x = health%, .y = stamina%, .z = magicka%, .w = level (raw)
        Float4 Movement;        // .x = speed (units/s), .y = isSprinting, .z = isSwimming, .w = isRiding
        Float4 Combat;          // .x = inCombat(0/1), .y = isBleedout, .z = isKillMove, .w = weaponDrawn(0/1)
        Float4 Water;           // .x = isUnderwater(0/1), .y = waterSurfaceZ, .z = submersionDepth, .w = isWading(0/1)
    };

    // ── 6. CAMERA ───────────────────────────────────────────────────────

    struct CameraData
    {
        Float4 Info;            // .x = FOV (degrees), .y = nearClip, .z = farClip, .w = aspectRatio
        Float4 Angles;          // .x = pitch (rad), .y = yaw (rad), .z = cameraStateEnum, .w = 0
        Float4 WorldPos;        // .xyz = camera world position, .w = 0

        Float4x4 ViewMatrix;    // Current frame View matrix (world→camera)
        Float4x4 ProjMatrix;    // Current frame Projection matrix
        Float4x4 ViewProjMatrix;// Current frame combined VP
        Float4x4 PrevViewProj;  // Previous frame VP (for motion vectors)
        Float4x4 InvViewProj;   // Inverse VP (for world position reconstruction)
    };

    // ── 7. INTERIOR LIGHTING ────────────────────────────────────────────

    struct InteriorData
    {
        Float4 IsInterior;      // .x = isInterior(0/1), .y = hasLightingTemplate, .z = 0, .w = 0
        Float4 AmbientColor;    // .rgb = interior ambient, .a = intensity
        Float4 DirectionalColor;// .rgb = interior directional light, .a = fade
        Float4 DirectionalDir;  // .xyz = interior light direction, .w = 0
        Float4 InteriorFogColor;// .rgb = interior fog color, .a = 0
        Float4 InteriorFogDist; // .x = near, .y = far, .z = power, .w = clipDist
    };

    // ── 8. DIRECTIONAL LIGHT & SHADOWS ──────────────────────────────────

    struct ShadowData
    {
        Float4 LightDirection;  // .xyz = shadow caster direction (world), .w = shadow intensity
        Float4 LightDiffuse;    // .rgb = shadow caster diffuse color, .a = 0
        Float4 LightAmbient;    // .rgb = shadow caster ambient color, .a = 0
    };

    // ── 9. ACTIVE MAGIC EFFECTS ─────────────────────────────────────────

    struct EffectsData
    {
        Float4 VisionEffects;   // .x = nightEye(0/1), .y = detectLife(0/1), .z = detectDead(0/1), .w = etherealForm(0/1)
        Float4 TimeEffects;     // .x = slowTimeFactor, .y = isTimeStopped, .z = 0, .w = 0
        Float4 DamageEffects;   // .x = isTakingFireDmg, .y = isTakingFrostDmg, .z = isTakingShockDmg, .w = isTakingPoisonDmg
        Float4 MiscEffects;     // .x = isInvisible, .y = isParalyzed, .z = isDrunk (skooma/ale), .w = 0
    };

    // ── 10. RENDER STATE ────────────────────────────────────────────────

    struct RenderData
    {
        Float4 FrameInfo;       // .x = frameCount, .y = deltaTime (sec), .z = screenWidth, .w = screenHeight
        Float4 Jitter;          // .xy = TAA jitter offset (NDC), .z = frameIndex%16 (for blue noise), .w = 0
    };

    // ── 11. IMAGE SPACE — Game's post-processing state (IMODs) ────────

    struct ImageSpaceData
    {
        Float4 HDR;             // .x = eyeAdaptSpeed, .y = bloomScale, .z = bloomThreshold, .w = sunlightScale
        Float4 Cinematic;       // .x = saturation, .y = brightness, .z = contrast, .w = tintAlpha
        Float4 CineTint;        // .rgb = cinematic tint color, .a = 0
        Float4 DOF;             // .x = strength, .y = distance, .z = range, .w = vignetteRadius
        Float4 IMOD;            // .x = hasActiveIMOD(0/1), .y = imodStrength, .z = imodFadeIn, .w = imodElapsed
        Float4 IMODTint;        // .rgb = IMOD tint color, .a = blur amount
    };

    // ── 12. NEARBY LIGHTS — 3 nearest point/spot lights ───────────────

    struct LightData
    {
        Float4 Light0PosRad;    // .xyz = world position, .w = radius
        Float4 Light0Color;     // .rgb = color, .a = intensity
        Float4 Light1PosRad;
        Float4 Light1Color;
        Float4 Light2PosRad;
        Float4 Light2Color;
        Float4 Summary;         // .x = total nearby count, .y = nearest distance, .z = total luminous flux, .w = dominant hue [0,1]
    };

    // ── 13. ACTOR VALUES — Resistances, combat stats, skills ──────────

    struct ActorValueData
    {
        Float4 Resist;          // .x = fireResist%, .y = frostResist%, .z = shockResist%, .w = magicResist%
        Float4 Resist2;         // .x = poisonResist%, .y = diseaseResist%, .z = damageResist (armor), .w = 0
        Float4 Combat;          // .x = attackDamageMult, .y = weaponSpeedMult, .z = critChance, .w = unarmedDmg
        Float4 Movement;        // .x = speedMult, .y = carryWeight, .z = inventoryWeight, .w = encumbranceRatio
        Float4 SkillCombat;     // .x = oneHanded, .y = twoHanded, .z = archery, .w = block
        Float4 SkillMagic;      // .x = alteration, .y = conjuration, .z = destruction, .w = illusion
        Float4 SkillMagic2;     // .x = restoration, .y = enchanting, .z = alchemy, .w = 0
        Float4 SkillStealth;    // .x = lightArmor, .y = sneak, .z = lockpicking, .w = pickpocket
    };

    // ── 14. CROSSHAIR / LOOK-AT TARGET ────────────────────────────────

    struct CrosshairData
    {
        Float4 Info;            // .x = hasTarget(0/1), .y = distance, .z = formType (enum), .w = isActor(0/1)
        Float4 Pos;             // .xyz = target world position, .w = boundingRadius
        Float4 Actor;           // .x = healthPct, .y = level, .z = isHostile(0/1), .w = isEssential(0/1)
    };

    // ── 15. EQUIPMENT — Weapons, armor, torch state ───────────────────

    struct EquipmentData
    {
        Float4 Right;           // .x = weaponType, .y = baseDamage, .z = isEnchanted(0/1), .w = enchantCharge [0,1]
        Float4 Left;            // .x = itemType, .y = damage/armorRating, .z = isEnchanted(0/1), .w = isSpell(0/1)
        Float4 Armor;           // .x = totalArmorRating, .y = isWearingHeavy(0/1), .z = isWearingLight(0/1), .w = isWearingRobes(0/1)
        Float4 Flags;           // .x = weaponDrawn(0/1), .y = hasBow(0/1), .z = hasTorch(0/1), .w = isTwoHanding(0/1)
    };

    // ── 16. QUEST STATE ───────────────────────────────────────────────

    struct QuestData
    {
        Float4 Progress;        // .x = mainQuestStage, .y = totalQuestsCompleted, .z = activeQuestCount, .w = activeObjectiveCount
        Float4 Tracked;         // .x = trackedQuestStage, .y = questType, .z = questFormID (low 16 bits), .w = hasObjectiveMarker(0/1)
    };

    // ── 17. UI / MENU STATE ───────────────────────────────────────────

    struct UIStateData
    {
        Float4 Menus;           // .x = isInMenu(0/1), .y = isInDialogue(0/1), .z = isInInventory(0/1), .w = isInMap(0/1)
        Float4 HUD;             // .x = isHUDVisible(0/1), .y = isCrosshairVisible(0/1), .z = isInCinematicMode(0/1), .w = isLoading(0/1)
        Float4 Detail;          // .x = isInCrafting(0/1), .y = isInBook(0/1), .z = isInLockpick(0/1), .w = isInConsole(0/1)
    };

    // ── Aggregate ───────────────────────────────────────────────────────

    struct AllData
    {
        CelestialData   celestial;
        AtmosphereData  atmosphere;
        FogData         fog;
        WeatherData     weather;
        PlayerData      player;
        CameraData      camera;
        InteriorData    interior;
        ShadowData      shadow;
        EffectsData     effects;
        RenderData      render;
        ImageSpaceData  imageSpace;
        LightData       lights;
        ActorValueData  actorValues;
        CrosshairData   crosshair;
        EquipmentData   equipment;
        QuestData       quest;
        UIStateData     uiState;
    };

    // ── Parameter name table ────────────────────────────────────────────
    // Maps each Float4 field to the ENB parameter name string.
    // Used by the push routine to iterate all parameters generically.

    struct ParamEntry {
        const char* name;       // ENB parameter name (e.g., "SB_Sun_NDC")
        std::size_t offset;     // Byte offset into AllData
    };

    // Declared in BridgeData.cpp
    extern const ParamEntry kParamTable[];
    extern const std::size_t kParamCount;

    // Target shader files — every .fx that includes SkyrimBridge.fxh
    inline constexpr const char* kTargetShaders[] = {
        "enbsunsprite.fx",
        "enbeffectprepass.fx",
        "enbeffect.fx",
        "enbeffectpostpass.fx",
        "enblens.fx",
        "enbunderwater.fx",
        "enbdepthoffield.fx",
        "enbbloom.fx",
        "enbadaptation.fx",
    };
}
