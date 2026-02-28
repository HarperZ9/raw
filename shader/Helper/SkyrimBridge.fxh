#ifndef SKYRIMBRIDGE_FXH
#define SKYRIMBRIDGE_FXH
//=============================================================================
//  SkyrimBridge.fxh — HLSL declarations for SkyrimBridge external parameters
//
//  CANONICAL COPY — this is the single source of truth.
//  Location: SkyrimBridge_v3/shader/Helper/SkyrimBridge.fxh
//  Do NOT edit copies in Package Files for Analysis/ or other directories.
//
//  Include this file in any ENB shader (.fx) that needs SkyrimBridge data.
//  Place the #include AFTER the ENB built-in parameter block (Timer, ScreenSize,
//  etc.) and BEFORE your shader code.
//
//  These float4 declarations are populated every frame by the SkyrimBridge
//  SKSE plugin via ENBSetParameter(). Without this header, pushed data is
//  silently discarded by ENB.
//
//  Author: Zain Dana Harper
//  Version: 3.0.0
//
//  Changelog (v3.0.0):
//    [NEW] Added Section 20: Moon rendering helpers (SB_MoonScreenUV, SB_MoonGodRayParams)
//    [NEW] Added Section 21: Weather transition helpers (SB_GetWeatherBlend, SB_SmoothWeatherTransition)
//    [NEW] Added Section 22: Health/vitals feedback (SB_GetHealthVignette, SB_GetDamageFlash)
//    [NEW] Added Section 23: Point light bloom helpers (SB_GetPointLightBloomParams)
//    [NEW] Added Section 24: Scene surface state (SB_SceneWetness, SB_SnowCoverage, SB_PuddleDepth)
//    [NEW] Added Section 25: Combat state helpers (SB_CombatIntensity, SB_IsInKillcam)
//    [NEW] Added Section 26: Time-of-day interpolation (SB_DNI, SB_TOD7)
//    [NEW] Added Section 27: Interior lighting helpers (SB_InteriorLightDir, SB_InteriorAmbient)
//    [NEW] Added Section 28: Spell school detection (SB_GetSpellSchool, SB_GetActiveSpellColor)
//    [IMP] Updated version to 3.0.0
//    [IMP] Added comprehensive usage documentation
//    [IMP] Added graceful fallback pattern documentation
//
//  Changelog (v2.0.0):
//    - Added Section 18: Volumetric scattering parameters (SB_Vol_Scatter, SB_Vol_Color)
//    - Added Section 19: G-Buffer material classification system
//        - 14 material ID constants (SB_MAT_SKIN, SB_MAT_METAL, etc.)
//        - GBuffer reader functions (SB_ReadMaterialID, SB_ReadNormal, SB_ReadMotion,
//          SB_ReadRoughness, SB_ReadMetalness)
//        - SB_PBRSurface struct and SB_GetSurface() constructor
//    - Added compatibility aliases for addon shader naming conventions
//        (SB_Camera_Planes, SB_Camera_Position, SB_Shadow_SunDir, SB_LinearDepth)
//
//  Usage Pattern (Graceful Fallback):
//    // Always check SB_IsActive() before using SkyrimBridge data
//    float3 sunDir = SB_IsActive() ? SB_Sun_Direction.xyz : normalize(float3(0.5, 0.5, 1.0));
//
//    // Or use the convenience fallback helpers:
//    float3 sunDir = SB_GetSunDirection(normalize(float3(0.5, 0.5, 1.0)));
//=============================================================================


//─────────────────────────────────────────────────────────────────────────────
//  COMMON CONSTANTS
//─────────────────────────────────────────────────────────────────────────────

#ifndef DELTA
    #define DELTA 1e-6
#endif

#ifndef K_LUM
    static const float3 K_LUM = float3(0.2126, 0.7152, 0.0722);  // Rec.709 luminance
#endif


//─────────────────────────────────────────────────────────────────────────────
//  1. CELESTIAL
//─────────────────────────────────────────────────────────────────────────────

float4 SB_Sun_NDC;           // .xy = NDC [-1,1], .z = onScreen (0/1), .w = elevation (rad)
float4 SB_Sun_Direction;     // .xyz = normalized world dir, .w = elevation (rad)
float4 SB_Sun_Color;         // .rgb = weather sunlight color, .a = sun glare factor

float4 SB_Masser_NDC;        // .xy = NDC pos, .z = onScreen (0/1), .w = phase brightness [0,1]
float4 SB_Masser_Direction;  // .xyz = normalized world dir, .w = elevation (rad)

float4 SB_Secunda_NDC;       // .xy = NDC pos, .z = onScreen (0/1), .w = phase brightness [0,1]
float4 SB_Secunda_Direction; // .xyz = normalized world dir, .w = elevation (rad)

// Convenience aliases (Masser is the dominant moon in Skyrim)
float4 SB_Celestial_Sun;     // Alias for SB_Sun_NDC: .xy = NDC, .z = onScreen, .w = elevation
float4 SB_Celestial_Moon;    // Alias for SB_Masser_NDC: .xy = NDC, .z = onScreen, .w = phase [0=new,1=full]

float4 SB_Time;              // .x = gameHour [0,24), .y = sunriseHour, .z = sunsetHour, .w = dayProgress [0,1]


//─────────────────────────────────────────────────────────────────────────────
//  2. ATMOSPHERE
//─────────────────────────────────────────────────────────────────────────────

float4 SB_Atmos_SkyUpper;    // .rgb = upper sky gradient
float4 SB_Atmos_SkyLower;    // .rgb = lower sky gradient
float4 SB_Atmos_Horizon;     // .rgb = horizon band color
float4 SB_Atmos_Ambient;     // .rgb = ambient light color, .a = intensity
float4 SB_Atmos_Sunlight;    // .rgb = directional sunlight, .a = sunlight scale
float4 SB_Atmos_CloudDiffuse;// .rgb = cloud LOD diffuse tint
float4 SB_Atmos_CloudAmbient;// .rgb = cloud LOD ambient tint
float4 SB_Atmos_EffectLight; // .rgb = magic/effect lighting color


//─────────────────────────────────────────────────────────────────────────────
//  3. FOG
//─────────────────────────────────────────────────────────────────────────────

float4 SB_Fog_NearColor;     // .rgb = near fog color, .a = near distance
float4 SB_Fog_FarColor;      // .rgb = far fog color,  .a = far distance
float4 SB_Fog_Density;       // .x = power curve, .y = maxOpacity, .z = isInterior (0/1)
float4 SB_Fog_Height;        // .x = waterSurfaceZ, .y = playerAltitude, .z = seaLevelDensity, .w = falloffRate


//─────────────────────────────────────────────────────────────────────────────
//  4. WEATHER
//─────────────────────────────────────────────────────────────────────────────

float4 SB_Wind;              // .x = speed [0,1], .y = direction (rad)
float4 SB_Precipitation;     // .x = type (0=none,1=rain,2=snow), .y = intensity [0,1]
float4 SB_Lightning;         // .x = frequency, .y = isFlashing (0/1), .z = flashIntensity, .w = timeSinceFlash
float4 SB_Weather_Flags;     // .x = isPleasant, .y = isCloudy, .z = isRainy, .w = isSnowy
float4 SB_Weather_Transition;// .x = transition% [0,1], .y = outgoingID, .z = currentID
float4 SB_Precip_Surface;    // .x = surface wetness [0,1], .y = puddle depth, .z = snow accumulation


//─────────────────────────────────────────────────────────────────────────────
//  5. PLAYER
//─────────────────────────────────────────────────────────────────────────────

float4 SB_Player_Position;   // .xyz = world pos, .w = altitude above water
float4 SB_Player_Vitals;     // .x = health%, .y = stamina%, .z = magicka%, .w = level
float4 SB_Player_Movement;   // .x = speed (units/s), .y = sprinting, .z = swimming, .w = mounted
float4 SB_Player_Combat;     // .x = inCombat, .y = bleedout, .z = killcam, .w = weaponDrawn
float4 SB_Player_Water;      // .x = underwater, .y = waterSurfaceZ, .z = submersionDepth, .w = wading


//─────────────────────────────────────────────────────────────────────────────
//  6. CAMERA
//─────────────────────────────────────────────────────────────────────────────

float4 SB_Camera_Info;       // .x = FOV (deg), .y = nearClip, .z = farClip, .w = aspectRatio
float4 SB_Camera_Angles;     // .x = pitch (rad), .y = yaw (rad), .z = cameraStateEnum
float4 SB_Camera_WorldPos;   // .xyz = camera world position

// View matrix (world → camera), row-major
float4 SB_View_Row0;
float4 SB_View_Row1;
float4 SB_View_Row2;
float4 SB_View_Row3;

// Projection matrix
float4 SB_Proj_Row0;
float4 SB_Proj_Row1;
float4 SB_Proj_Row2;
float4 SB_Proj_Row3;

// Combined View*Projection
float4 SB_ViewProj_Row0;
float4 SB_ViewProj_Row1;
float4 SB_ViewProj_Row2;
float4 SB_ViewProj_Row3;

// Previous frame View*Projection (for motion vectors / temporal reprojection)
float4 SB_PrevVP_Row0;
float4 SB_PrevVP_Row1;
float4 SB_PrevVP_Row2;
float4 SB_PrevVP_Row3;

// Inverse View*Projection (for world position reconstruction from depth)
float4 SB_InvVP_Row0;
float4 SB_InvVP_Row1;
float4 SB_InvVP_Row2;
float4 SB_InvVP_Row3;


//─────────────────────────────────────────────────────────────────────────────
//  7. INTERIOR LIGHTING
//─────────────────────────────────────────────────────────────────────────────

float4 SB_Interior_Flags;    // .x = isInterior (0/1), .y = hasLightingTemplate
float4 SB_Interior_Ambient;  // .rgb = ambient, .a = intensity
float4 SB_Interior_DirColor; // .rgb = directional light, .a = fade
float4 SB_Interior_DirDir;   // .xyz = light direction
float4 SB_Interior_FogColor; // .rgb = interior fog color
float4 SB_Interior_FogDist;  // .x = near, .y = far, .z = power, .w = clipDist


//─────────────────────────────────────────────────────────────────────────────
//  8. SHADOW / DIRECTIONAL LIGHT
//─────────────────────────────────────────────────────────────────────────────

float4 SB_Shadow_Direction;  // .xyz = shadow caster dir (world), .w = shadow intensity
float4 SB_Shadow_Diffuse;    // .rgb = shadow caster diffuse color
float4 SB_Shadow_Ambient;    // .rgb = shadow caster ambient color


//─────────────────────────────────────────────────────────────────────────────
//  9. ACTIVE MAGIC EFFECTS
//─────────────────────────────────────────────────────────────────────────────

float4 SB_FX_Vision;         // .x = nightEye, .y = detectLife, .z = detectDead, .w = ethereal
float4 SB_FX_Time;           // .x = slowTimeFactor, .y = timeStopped
float4 SB_FX_Damage;         // .x = fire, .y = frost, .z = shock, .w = poison
float4 SB_FX_Misc;           // .x = invisible, .y = paralyzed, .z = drunk


//─────────────────────────────────────────────────────────────────────────────
//  10. RENDER STATE
//─────────────────────────────────────────────────────────────────────────────

float4 SB_Render_Frame;      // .x = frameCount (wraps at 2^20), .y = deltaTime (sec), .z = screenW, .w = screenH
float4 SB_Render_Jitter;     // .xy = TAA jitter (NDC), .z = frameIndex%16


//=============================================================================
//  11. IMAGE SPACE — Game's own post-processing state
//
//  The game applies its own imagespace modifiers (IMODs) for visual effects:
//  hit blur, underwater distortion, drunk/skooma, vampire lord, bed wakeup.
//  Knowing these lets shaders coordinate instead of fighting ENB processing.
//─────────────────────────────────────────────────────────────────────────────

float4 SB_IS_HDR;            // .x = eyeAdaptSpeed, .y = bloomScale, .z = bloomThreshold, .w = sunlightScale
float4 SB_IS_Cinematic;      // .x = saturation, .y = brightness, .z = contrast, .w = tintAlpha
float4 SB_IS_CineTint;       // .rgb = cinematic tint color
float4 SB_IS_DOF;            // .x = strength, .y = distance, .z = range, .w = vignetteRadius
float4 SB_IS_IMOD;           // .x = hasActiveIMOD(0/1), .y = imodStrength, .z = imodFadeIn, .w = imodElapsed
float4 SB_IS_IMODTint;       // .rgb = IMOD tint color, .a = blur amount


//=============================================================================
//  12. NEARBY LIGHTS — 3 nearest point/spot lights
//
//  Actual in-world light sources: torches, campfires, magelight, lanterns.
//  Enables multi-light screen-space effects, light-aware fog, point flares.
//─────────────────────────────────────────────────────────────────────────────

float4 SB_Light0_PosRad;     // .xyz = world position, .w = radius
float4 SB_Light0_Color;      // .rgb = color, .a = intensity (fade × dimmer)
float4 SB_Light1_PosRad;     // .xyz = world position, .w = radius
float4 SB_Light1_Color;      // .rgb = color, .a = intensity
float4 SB_Light2_PosRad;     // .xyz = world position, .w = radius
float4 SB_Light2_Color;      // .rgb = color, .a = intensity
float4 SB_Light_Summary;     // .x = total nearby count, .y = nearest distance,
                              // .z = total luminous flux, .w = dominant hue [0,1]


//=============================================================================
//  13. EXTENDED ACTOR VALUES — Full player stats
//
//  All 160+ actor values are available; these are the most useful.
//  Skills are raw levels (0-100+), resistances are percentages.
//─────────────────────────────────────────────────────────────────────────────

float4 SB_AV_Resist;         // .x = fireResist%, .y = frostResist%, .z = shockResist%, .w = magicResist%
float4 SB_AV_Resist2;        // .x = poisonResist%, .y = diseaseResist%, .z = damageResist (armor)
float4 SB_AV_Combat;         // .x = attackDamageMult, .y = weaponSpeedMult, .z = critChance, .w = unarmedDmg
float4 SB_AV_Movement;       // .x = speedMult, .y = carryWeight, .z = inventoryWeight, .w = encumbranceRatio
float4 SB_AV_SkillCombat;    // .x = oneHanded, .y = twoHanded, .z = archery, .w = block
float4 SB_AV_SkillMagic;     // .x = alteration, .y = conjuration, .z = destruction, .w = illusion
float4 SB_AV_SkillMagic2;    // .x = restoration, .y = enchanting, .z = alchemy
float4 SB_AV_SkillStealth;   // .x = lightArmor, .y = sneak, .z = lockpicking, .w = pickpocket


//=============================================================================
//  14. CROSSHAIR / LOOK-AT TARGET — What the player is aiming at
//
//  Enables material-aware highlighting, contextual HUD, auto-focus DOF,
//  enemy health display, interaction range visualization.
//─────────────────────────────────────────────────────────────────────────────

float4 SB_XHair_Info;         // .x = hasTarget(0/1), .y = distance, .z = formType (enum), .w = isActor(0/1)
float4 SB_XHair_Pos;          // .xyz = target world position, .w = boundingRadius
float4 SB_XHair_Actor;        // .x = healthPct, .y = level, .z = isHostile(0/1), .w = isEssential(0/1)
                               // (only valid when SB_XHair_Info.w == 1)


//=============================================================================
//  15. EQUIPMENT — Player's equipped weapons, armor, torch state
//
//  Weapon type: 0=none, 1=fist, 2=sword, 3=dagger, 4=warAxe, 5=mace,
//  6=greatsword, 7=battleaxe, 8=bow, 9=staff, 10=crossbow, 11=spell,
//  20=shield, 21=torch
//─────────────────────────────────────────────────────────────────────────────

float4 SB_Equip_Right;        // .x = weaponType, .y = baseDamage, .z = isEnchanted(0/1), .w = enchantCharge [0,1]
float4 SB_Equip_Left;         // .x = itemType (same enum, 20=shield, 21=torch), .y = damage/armorRating,
                               // .z = isEnchanted(0/1), .w = isSpell(0/1)
float4 SB_Equip_Armor;        // .x = totalArmorRating, .y = isWearingHeavy(0/1), .z = isWearingLight(0/1),
                               // .w = isWearingRobes(0/1)
float4 SB_Equip_Flags;        // .x = weaponDrawn(0/1), .y = hasBow(0/1), .z = hasTorch(0/1), .w = isTwoHanding(0/1)


//=============================================================================
//  16. QUEST STATE — Active quests, progression, tracked objective
//─────────────────────────────────────────────────────────────────────────────

float4 SB_Quest_Progress;      // .x = mainQuestStage, .y = totalQuestsCompleted,
                               // .z = activeQuestCount, .w = activeObjectiveCount
float4 SB_Quest_Tracked;       // .x = trackedQuestStage, .y = questType (0=misc,1=main,2=guild,3=daedric),
                               // .z = questFormID (lower 16 bits), .w = hasObjectiveMarker(0/1)


//=============================================================================
//  17. UI / MENU STATE — Which menus are open, HUD visibility
//─────────────────────────────────────────────────────────────────────────────

float4 SB_UI_Menus;            // .x = isInMenu(0/1), .y = isInDialogue(0/1), .z = isInInventory(0/1), .w = isInMap(0/1)
float4 SB_UI_HUD;              // .x = isHUDVisible(0/1), .y = isCrosshairVisible(0/1),
                               // .z = isInCinematicMode(0/1), .w = isLoading(0/1)
float4 SB_UI_Detail;           // .x = isInCrafting(0/1), .y = isInBook(0/1), .z = isInLockpick(0/1),
                               // .w = isInConsole(0/1)


//─────────────────────────────────────────────────────────────────────────────
//  18. VOLUMETRIC SCATTERING — Fog/haze medium parameters
//─────────────────────────────────────────────────────────────────────────────

float4 SB_Vol_Scatter;       // .x = inscatter coeff, .y = extinction coeff, .z = phase asymmetry (g), .w = density scale
float4 SB_Vol_Color;         // .rgb = volumetric fog/haze color, .a = ambient contribution


//─────────────────────────────────────────────────────────────────────────────
//  18b. WEATHER PARAMETERS — Per-weather effect multipliers
//
//  These are pushed by WeatherParameterComputer when SB_WEATHER_PARAMS is defined.
//  Values interpolate smoothly during weather transitions.
//  Use SB_GetWP() to read with fallback defaults.
//─────────────────────────────────────────────────────────────────────────────

#ifdef SB_WEATHER_PARAMS

// Weather parameter float4 declarations (pushed by DLL)
float4 SB_WP_BloomInt;       // .x = bloom intensity multiplier
float4 SB_WP_BloomRad;       // .x = bloom radius multiplier
float4 SB_WP_AdaptSpd;       // .x = adaptation speed multiplier
float4 SB_WP_ExpBias;        // .x = exposure bias (EV)
float4 SB_WP_Exposure;       // .x = exposure multiplier (alternative to bias)
float4 SB_WP_Saturation;     // .x = saturation multiplier
float4 SB_WP_Contrast;       // .x = contrast multiplier
float4 SB_WP_ColorTemp;      // .x = color temperature shift
float4 SB_WP_Sharpen;        // .x = sharpening intensity
float4 SB_WP_Grain;          // .x = film grain intensity
float4 SB_WP_AO;             // .x = ambient occlusion multiplier
float4 SB_WP_SSR;            // .x = screen-space reflections multiplier
float4 SB_WP_GodRay;         // .x = god ray intensity
float4 SB_WP_DOF;            // .x = depth of field strength
float4 SB_WP_LensDirt;       // .x = lens dirt intensity
float4 SB_WP_RainLens;       // .x = rain on lens intensity
float4 SB_WP_FrostLens;      // .x = frost on lens intensity
float4 SB_WP_State;          // .x = transitionProgress, .y = currentWeatherID, .z = previousWeatherID

// Helper to read weather parameter with fallback
// Uses SB_WP_State.w as the active sentinel (set to 1.0 by DLL when weather
// params are being pushed). This avoids the false-negative when a weather
// parameter legitimately equals 0.0.
float SB_GetWP(float4 wpParam, float fallback)
{
    if (!SB_IsActive()) return fallback;
    // SB_WP_State.w is set to 1.0 by WeatherParameterComputer when active
    if (SB_WP_State.w < 0.5) return fallback;
    return wpParam.x;
}

#else

// Stub when SB_WEATHER_PARAMS not defined - always return fallback
float SB_GetWP(float4 wpParam, float fallback) { return fallback; }

#endif // SB_WEATHER_PARAMS


//─────────────────────────────────────────────────────────────────────────────
//  19. G-BUFFER MATERIAL CLASSIFICATION
//
//  The SkyrimBridge SKSE plugin classifies mesh materials into categories
//  and writes a material ID per-pixel to SB_GBuffer_Material (register t10).
//  GBuffer normals are in SB_GBuffer_Normal (t11), motion in t12,
//  depth derivatives in t13, pre-tonemap HDR in t14.
//
//  Shader addons declare these textures locally at the required registers.
//  This section provides the constants, reader functions, and PBR surface
//  struct so all addons share a single, consistent API.
//─────────────────────────────────────────────────────────────────────────────

// --- Material ID Constants ---
// Values match the SKSE plugin's TESObjectMaterial classification output.
// Stored in the Red channel of SB_GBuffer_Material as normalized [0,1];
// multiply by 255 and round to get the integer ID.

static const int SB_MAT_DEFAULT  = 0;   // Unclassified geometry
static const int SB_MAT_SKIN     = 1;   // NPC/player skin
static const int SB_MAT_HAIR     = 2;   // Hair meshes
static const int SB_MAT_EYE      = 3;   // Eye meshes (sclera, iris, cornea)
static const int SB_MAT_METAL    = 4;   // Metallic surfaces (armor, weapons, dwemer)
static const int SB_MAT_STONE    = 5;   // Stone, masonry, dungeon walls
static const int SB_MAT_WOOD     = 6;   // Wood surfaces
static const int SB_MAT_CLOTH    = 7;   // Fabric, clothing, banners
static const int SB_MAT_FOLIAGE  = 8;   // Leaves, grass, flora
static const int SB_MAT_DIRT     = 9;   // Dirt, sand, gravel terrain
static const int SB_MAT_SNOW     = 10;  // Snow-covered surfaces
static const int SB_MAT_WATER    = 11;  // Water surfaces
static const int SB_MAT_EFFECT   = 12;  // Magic effects, particles, emissives
static const int SB_MAT_SKY      = 13;  // Sky, clouds, distant LOD

// --- GBuffer Reader Functions ---

// Read integer material ID from the GBuffer material texture.
// Expects the material texture to encode the ID in the R channel as ID/255.
int SB_ReadMaterialID(Texture2D matTex, int2 px)
{
    float raw = matTex.Load(int3(px, 0)).r;
    return (int)(raw * 255.0 + 0.5);
}

// Read world-space normal from the GBuffer normal texture.
// Normals are stored as (N*0.5+0.5) in RGB.
float3 SB_ReadNormal(Texture2D normTex, int2 px)
{
    float3 packed = normTex.Load(int3(px, 0)).rgb;
    return normalize(packed * 2.0 - 1.0);
}

// Read screen-space motion vector from the GBuffer motion texture.
// Motion is stored as NDC-space displacement in RG channels, centered at 0.5.
float2 SB_ReadMotion(Texture2D motionTex, int2 px)
{
    float2 packed = motionTex.Load(int3(px, 0)).rg;
    return packed * 2.0 - 1.0;
}

// Read roughness from the GBuffer material texture (Green channel).
float SB_ReadRoughness(Texture2D matTex, int2 px)
{
    return matTex.Load(int3(px, 0)).g;
}

// Read metalness from the GBuffer material texture (Blue channel).
float SB_ReadMetalness(Texture2D matTex, int2 px)
{
    return matTex.Load(int3(px, 0)).b;
}

// --- PBR Surface Struct ---

struct SB_PBRSurface
{
    int    materialID;
    float3 normal;
    float  roughness;
    float  metalness;
};

// Construct a PBR surface description from the GBuffer textures.
SB_PBRSurface SB_GetSurface(Texture2D matTex, Texture2D normTex, int2 px)
{
    SB_PBRSurface surf;
    float4 matData    = matTex.Load(int3(px, 0));
    surf.materialID   = (int)(matData.r * 255.0 + 0.5);
    surf.roughness    = matData.g;
    surf.metalness    = matData.b;
    surf.normal       = normalize(normTex.Load(int3(px, 0)).rgb * 2.0 - 1.0);
    return surf;
}


//=============================================================================
//  COMPATIBILITY ALIASES
//
//  The addon shaders (SB_GTAO, SB_ContactShadows, SB_VolumetricFog, etc.)
//  were developed using shorthand names. These aliases ensure everything
//  compiles without touching the addon source files.
//=============================================================================

// Camera plane distances: addons use SB_Camera_Planes.x/.y for near/far
#define SB_Camera_Planes   float2(SB_Camera_Info.y, SB_Camera_Info.z)

// Camera world position: addons use SB_Camera_Position
#define SB_Camera_Position SB_Camera_WorldPos

// Shadow sun direction: addons use SB_Shadow_SunDir
#define SB_Shadow_SunDir   SB_Shadow_Direction

// Depth linearization: addons call SB_LinearDepth(depth, near, far)
// whereas the original helper was SB_LinearizeDepth(rawDepth) using globals.
float SB_LinearDepth(float rawDepth, float nearClip, float farClip)
{
    return nearClip * farClip / (farClip - rawDepth * (farClip - nearClip));
}


//=============================================================================
//  HELPER: Reconstruct matrices from row vectors
//=============================================================================

float4x4 SB_GetViewMatrix()
{
    return float4x4(SB_View_Row0, SB_View_Row1, SB_View_Row2, SB_View_Row3);
}

float4x4 SB_GetProjMatrix()
{
    return float4x4(SB_Proj_Row0, SB_Proj_Row1, SB_Proj_Row2, SB_Proj_Row3);
}

float4x4 SB_GetViewProjMatrix()
{
    return float4x4(SB_ViewProj_Row0, SB_ViewProj_Row1, SB_ViewProj_Row2, SB_ViewProj_Row3);
}

float4x4 SB_GetPrevViewProjMatrix()
{
    return float4x4(SB_PrevVP_Row0, SB_PrevVP_Row1, SB_PrevVP_Row2, SB_PrevVP_Row3);
}

float4x4 SB_GetInvViewProjMatrix()
{
    return float4x4(SB_InvVP_Row0, SB_InvVP_Row1, SB_InvVP_Row2, SB_InvVP_Row3);
}


//=============================================================================
//  HELPER: World position from depth buffer
//=============================================================================

float3 SB_WorldPosFromDepth(float2 uv, float depth)
{
    // UV [0,1] → NDC [-1,1] (Y flipped for D3D)
    float4 ndc = float4(uv * 2.0 - 1.0, depth, 1.0);
    ndc.y = -ndc.y;

    float4 world = mul(ndc, SB_GetInvViewProjMatrix());
    return world.xyz / world.w;
}


//=============================================================================
//  HELPER: Motion vectors from current/previous ViewProj
//=============================================================================

float2 SB_MotionVector(float3 worldPos, float2 currentUV)
{
    float4 prevClip = mul(float4(worldPos, 1.0), SB_GetPrevViewProjMatrix());
    float2 prevUV = prevClip.xy / prevClip.w * float2(0.5, -0.5) + 0.5;
    return currentUV - prevUV;
}


//=============================================================================
//  HELPER: Sun screen-space UV (compatible with ENB's LightParameters pattern)
//=============================================================================

float2 SB_SunScreenUV()
{
    return SB_Sun_NDC.xy * float2(0.5, -0.5) + 0.5;
}

bool SB_IsSunOnScreen()
{
    return SB_Sun_NDC.z > 0.5;
}


//=============================================================================
//  HELPER: Reconstruct view-space direction from UV
//=============================================================================

float3 SB_ViewDirFromUV(float2 uv)
{
    float tanHalfFov = tan(SB_Camera_Info.x * 0.5 * 3.14159265 / 180.0);
    float aspect = SB_Camera_Info.w;
    float2 ndc = uv * 2.0 - 1.0;
    return normalize(float3(ndc.x * tanHalfFov * aspect, -ndc.y * tanHalfFov, -1.0));
}


//=============================================================================
//  HELPER: Linearize depth using SkyrimBridge camera data
//=============================================================================

float SB_LinearizeDepth(float rawDepth)
{
    float n = SB_Camera_Info.y;
    float f = SB_Camera_Info.z;
    return n * f / (f - rawDepth * (f - n));
}


//=============================================================================
//  HELPER: ImageSpace — Is an IMOD actively modifying the image?
//=============================================================================

bool SB_HasActiveIMOD()
{
    return SB_IS_IMOD.x > 0.5;
}

float SB_GetIMODStrength()
{
    return SB_IS_IMOD.y;
}

// Get the game's cinematic saturation (1.0 = normal)
// Values < 1 indicate the game is desaturating (e.g., werewolf, death)
// Values > 1 indicate the game is boosting saturation
float SB_GetGameSaturation()
{
    return SB_IS_Cinematic.x;
}


//=============================================================================
//  HELPER: Nearby Lights — Evaluate light contribution at a world position
//=============================================================================

// Compute attenuation from a single light to a world-space point.
// Returns [0,1] where 0 = outside radius, 1 = at light center.
float SB_LightAttenuation(float3 worldPos, float4 lightPosRad, float4 lightColor)
{
    float3 toLight = lightPosRad.xyz - worldPos;
    float dist = length(toLight);
    float radius = lightPosRad.w;
    if (radius < 1.0 || lightColor.a < 0.001) return 0.0;
    float atten = saturate(1.0 - dist / radius);
    return atten * atten * lightColor.a; // Inverse-square falloff approximation
}

// Compute total light color contribution from all 3 nearest lights at a point
float3 SB_EvaluateNearbyLights(float3 worldPos)
{
    float3 result = 0.0;
    result += SB_Light0_Color.rgb * SB_LightAttenuation(worldPos, SB_Light0_PosRad, SB_Light0_Color);
    result += SB_Light1_Color.rgb * SB_LightAttenuation(worldPos, SB_Light1_PosRad, SB_Light1_Color);
    result += SB_Light2_Color.rgb * SB_LightAttenuation(worldPos, SB_Light2_PosRad, SB_Light2_Color);
    return result;
}

// Project a light to screen-space NDC (for lens flare / bloom placement)
float4 SB_LightToScreen(float4 lightPosRad)
{
    float4x4 vp = SB_GetViewProjMatrix();
    float4 clip = mul(float4(lightPosRad.xyz, 1.0), vp);
    float3 ndc = clip.xyz / clip.w;
    float2 uv = ndc.xy * 0.5 + 0.5;
    uv.y = 1.0 - uv.y;
    return float4(uv, clip.w > 0.0 ? 1.0 : 0.0, clip.w); // .xy=screenUV, .z=onScreen, .w=depth
}


//=============================================================================
//  HELPER: Actor Values — Convenience accessors
//=============================================================================

// Get player's encumbrance ratio (>1.0 = over-encumbered)
float SB_GetEncumbrance()
{
    return SB_AV_Movement.w;
}

bool SB_IsOverEncumbered()
{
    return SB_AV_Movement.w > 1.0;
}

// Get a specific resistance by index (0=fire, 1=frost, 2=shock, 3=magic)
float SB_GetResistance(int index)
{
    if (index == 0) return SB_AV_Resist.x;
    if (index == 1) return SB_AV_Resist.y;
    if (index == 2) return SB_AV_Resist.z;
    return SB_AV_Resist.w;
}


//=============================================================================
//  HELPER: Crosshair — Target queries
//=============================================================================

bool SB_HasCrosshairTarget()
{
    return SB_XHair_Info.x > 0.5;
}

float SB_GetTargetDistance()
{
    return SB_XHair_Info.y;
}

bool SB_IsTargetActor()
{
    return SB_XHair_Info.w > 0.5;
}

bool SB_IsTargetHostile()
{
    return SB_XHair_Actor.z > 0.5;
}

float SB_GetTargetHealthPct()
{
    return SB_XHair_Actor.x;
}

// Auto-focus DOF helper: returns focus distance from crosshair target
// Falls back to a default distance if no target is available
float SB_GetAutoFocusDistance(float defaultDist)
{
    return SB_HasCrosshairTarget() ? SB_GetTargetDistance() : defaultDist;
}


//=============================================================================
//  HELPER: Equipment — Weapon/armor queries
//=============================================================================

// Weapon type check helpers
bool SB_HasWeaponDrawn()      { return SB_Equip_Flags.x > 0.5; }
bool SB_HasBowEquipped()      { return SB_Equip_Flags.y > 0.5; }
bool SB_HasTorchEquipped()    { return SB_Equip_Flags.z > 0.5; }
bool SB_IsTwoHanding()        { return SB_Equip_Flags.w > 0.5; }
bool SB_IsRightEnchanted()    { return SB_Equip_Right.z > 0.5; }
float SB_GetEnchantCharge()   { return SB_Equip_Right.w; }

// Armor class: returns dominant armor type
// 0 = unarmored, 1 = heavy, 2 = light, 3 = robes
float SB_GetArmorClass()
{
    if (SB_Equip_Armor.y > 0.5) return 1.0; // Heavy
    if (SB_Equip_Armor.z > 0.5) return 2.0; // Light
    if (SB_Equip_Armor.w > 0.5) return 3.0; // Robes
    return 0.0;
}

// Is the player holding a melee weapon? (types 1-7)
bool SB_IsMeleeWeapon()
{
    float t = SB_Equip_Right.x;
    return (t >= 1.0 && t <= 7.0);
}

// Is the player holding a ranged weapon? (bow/crossbow)
bool SB_IsRangedWeapon()
{
    float t = SB_Equip_Right.x;
    return (t == 8.0 || t == 10.0);
}

// Is the player casting a spell? (either hand)
bool SB_IsCasting()
{
    return (SB_Equip_Right.x == 11.0 || SB_Equip_Left.w > 0.5);
}


//=============================================================================
//  HELPER: UI State — Menu awareness
//=============================================================================

bool SB_IsInMenu()           { return SB_UI_Menus.x > 0.5; }
bool SB_IsInDialogue()       { return SB_UI_Menus.y > 0.5; }
bool SB_IsLoading()          { return SB_UI_HUD.w > 0.5; }
bool SB_IsCinematicMode()    { return SB_UI_HUD.z > 0.5; }

// Should post-processing be reduced? (menus, loading, console)
bool SB_ShouldReducePostFX()
{
    return SB_IsInMenu() || SB_IsLoading() || SB_UI_Detail.w > 0.5;
}

// Get menu-aware blur amount: 0 = no blur, 1 = full blur
// Useful for progressive background blur when menus open
float SB_GetMenuBlurAmount()
{
    if (SB_IsLoading()) return 1.0;
    if (SB_IsInMenu()) return 0.7;
    if (SB_IsInDialogue()) return 0.3;
    return 0.0;
}


//=============================================================================
//  HELPER: Check if SkyrimBridge data is valid (non-zero frame count)
//=============================================================================

bool SB_IsActive()
{
    return SB_Render_Frame.x > 0.0;
}


//=============================================================================
//  [NEW v3.0] SECTION 20: MOON RENDERING HELPERS
//
//  Masser and Secunda tracking enables moon god rays, dual moonlight,
//  and moon-phase-aware night lighting. These helpers simplify usage.
//=============================================================================

// Get Masser (larger moon) screen UV position
float2 SB_MasserScreenUV()
{
    return SB_Masser_NDC.xy * float2(0.5, -0.5) + 0.5;
}

// Get Secunda (smaller moon) screen UV position
float2 SB_SecundaScreenUV()
{
    return SB_Secunda_NDC.xy * float2(0.5, -0.5) + 0.5;
}

// Is Masser visible on screen?
bool SB_IsMasserOnScreen()
{
    return SB_Masser_NDC.z > 0.5 && SB_Masser_NDC.w > 0.0; // On screen AND above horizon
}

// Is Secunda visible on screen?
bool SB_IsSecundaOnScreen()
{
    return SB_Secunda_NDC.z > 0.5 && SB_Secunda_NDC.w > 0.0;
}

// Get combined moon brightness (0 = new moon, 1 = both full)
float SB_GetMoonBrightness()
{
    return max(SB_Masser_NDC.w, SB_Secunda_NDC.w);
}

// Get moon god ray parameters for a specific moon
// Returns: .xy = screen UV, .z = intensity (phase × elevation), .w = on-screen flag
float4 SB_GetMoonGodRayParams(bool useMasser)
{
    if (useMasser)
    {
        float intensity = SB_Masser_NDC.w * saturate(SB_Masser_Direction.w * 2.0); // Phase × elevation factor
        return float4(SB_MasserScreenUV(), intensity, SB_IsMasserOnScreen() ? 1.0 : 0.0);
    }
    else
    {
        float intensity = SB_Secunda_NDC.w * saturate(SB_Secunda_Direction.w * 2.0);
        return float4(SB_SecundaScreenUV(), intensity, SB_IsSecundaOnScreen() ? 1.0 : 0.0);
    }
}

// Get moonlight color based on moon phases (blue-white tint)
// Masser contributes red/orange, Secunda contributes white/blue
float3 SB_GetMoonlightColor()
{
    float3 masserColor  = float3(1.0, 0.9, 0.8) * SB_Masser_NDC.w;  // Warm (larger moon)
    float3 secundaColor = float3(0.8, 0.9, 1.0) * SB_Secunda_NDC.w; // Cool (smaller moon)
    return normalize(masserColor + secundaColor + 0.001) * SB_GetMoonBrightness();
}

// Check if it's nighttime (sun below horizon)
bool SB_IsNight()
{
    return SB_Sun_Direction.w < 0.0; // Elevation < 0 = below horizon
}


//=============================================================================
//  [NEW v3.0] SECTION 21: WEATHER TRANSITION HELPERS
//
//  Smooth interpolation functions for weather changes, enabling gradual
//  parameter morphing instead of hard profile snaps.
//=============================================================================

// Get weather transition progress [0,1]
// 0 = fully in previous weather, 1 = fully in current weather
float SB_GetWeatherBlend()
{
    return SB_Weather_Transition.x;
}

// Smoothstep version of weather blend for smoother transitions
float SB_SmoothWeatherTransition()
{
    float t = SB_Weather_Transition.x;
    return t * t * (3.0 - 2.0 * t); // Hermite smoothstep
}

// Smootherstep version (6th order, even smoother)
float SB_SmootherWeatherTransition()
{
    float t = SB_Weather_Transition.x;
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); // Perlin's smootherstep
}

// Lerp a value based on weather transition
float SB_WeatherLerp(float prevValue, float currValue)
{
    return lerp(prevValue, currValue, SB_SmoothWeatherTransition());
}

float3 SB_WeatherLerp3(float3 prevValue, float3 currValue)
{
    return lerp(prevValue, currValue, SB_SmoothWeatherTransition());
}

// Is a weather transition currently in progress?
bool SB_IsWeatherTransitioning()
{
    float t = SB_Weather_Transition.x;
    return t > 0.01 && t < 0.99;
}

// Get lightning flash factor (for bloom/exposure spikes)
// Returns 0-1 where 1 = flash is happening now
float SB_GetLightningFlash()
{
    if (SB_Lightning.y < 0.5) return 0.0; // No flash
    return SB_Lightning.z * saturate(1.0 - SB_Lightning.w * 2.0); // Intensity × time decay
}


//=============================================================================
//  [NEW v3.0] SECTION 22: HEALTH/VITALS FEEDBACK
//
//  Visual feedback functions for player state: health vignette, damage flash,
//  stamina pulse, magicka glow. Enables gameplay-reactive rendering.
//=============================================================================

// Get health-based vignette intensity
// Returns 0 at full health, increases as health drops
float SB_GetHealthVignette()
{
    float health = SB_Player_Vitals.x;
    float vignette = saturate(1.0 - health); // Inverse of health
    return vignette * vignette; // Quadratic falloff (more intense at low health)
}

// Get damage flash color based on active damage types
// SB_FX_Damage: .x = fire, .y = frost, .z = shock, .w = poison
float3 SB_GetDamageFlashColor()
{
    float3 color = 0.0;
    color += float3(1.0, 0.3, 0.0) * SB_FX_Damage.x;  // Fire = orange
    color += float3(0.4, 0.7, 1.0) * SB_FX_Damage.y;  // Frost = cyan
    color += float3(0.8, 0.8, 1.0) * SB_FX_Damage.z;  // Shock = white-blue
    color += float3(0.4, 1.0, 0.2) * SB_FX_Damage.w;  // Poison = green
    return color;
}

// Get total damage flash intensity
float SB_GetDamageFlashIntensity()
{
    return saturate(SB_FX_Damage.x + SB_FX_Damage.y + SB_FX_Damage.z + SB_FX_Damage.w);
}

// Is player in critical health? (<25%)
bool SB_IsCriticalHealth()
{
    return SB_Player_Vitals.x < 0.25;
}

// Is player in bleedout state?
bool SB_IsBleedout()
{
    return SB_Player_Combat.y > 0.5;
}

// Get heartbeat pulse for low-health effects
// Returns oscillating 0-1 value, faster at lower health
float SB_GetHeartbeatPulse()
{
    float health = SB_Player_Vitals.x;
    float speed = lerp(1.0, 4.0, saturate(1.0 - health)); // Faster at low health
    float time = SB_Render_Frame.x * 0.1; // Frame-based time
    return (sin(time * speed * 6.28318) * 0.5 + 0.5) * saturate(1.0 - health);
}

// Get stamina depletion effect (for sprinting visual feedback)
float SB_GetStaminaDepletion()
{
    return saturate(1.0 - SB_Player_Vitals.y);
}

// Get magicka depletion effect
float SB_GetMagickaDepletion()
{
    return saturate(1.0 - SB_Player_Vitals.z);
}


//=============================================================================
//  [NEW v3.0] SECTION 23: POINT LIGHT BLOOM HELPERS
//
//  Project individual point lights to screen space for per-light bloom,
//  lens flares, and light-aware volumetric effects.
//=============================================================================

// Structure for point light bloom parameters
struct SB_PointLightBloom
{
    float2 screenUV;      // Screen position [0,1]
    float3 color;         // Light color
    float  intensity;     // Combined intensity (color × attenuation)
    float  radius;        // World-space radius
    float  screenRadius;  // Approximate screen-space radius
    bool   onScreen;      // Is light visible?
};

// Get bloom parameters for a specific light (0, 1, or 2)
SB_PointLightBloom SB_GetPointLightBloom(int lightIndex)
{
    SB_PointLightBloom bloom;
    bloom.screenUV = 0.5;
    bloom.color = 0.0;
    bloom.intensity = 0.0;
    bloom.radius = 0.0;
    bloom.screenRadius = 0.0;
    bloom.onScreen = false;

    float4 posRad, color;
    if (lightIndex == 0) { posRad = SB_Light0_PosRad; color = SB_Light0_Color; }
    else if (lightIndex == 1) { posRad = SB_Light1_PosRad; color = SB_Light1_Color; }
    else { posRad = SB_Light2_PosRad; color = SB_Light2_Color; }

    // Skip if no light or zero intensity
    if (posRad.w < 1.0 || color.a < 0.001) return bloom;

    // Project to screen
    float4 proj = SB_LightToScreen(posRad);
    bloom.screenUV = proj.xy;
    bloom.onScreen = proj.z > 0.5 && proj.w > 0.0;
    bloom.color = color.rgb;
    bloom.intensity = color.a;
    bloom.radius = posRad.w;

    // Estimate screen-space radius (approximation based on depth)
    if (proj.w > 0.0)
    {
        bloom.screenRadius = posRad.w / proj.w * 0.1; // Rough estimate
    }

    return bloom;
}

// Get all point light bloom contributions at a screen position
float3 SB_EvaluatePointLightBloom(float2 uv, float bloomRadius)
{
    float3 result = 0.0;

    for (int i = 0; i < 3; i++)
    {
        SB_PointLightBloom light = SB_GetPointLightBloom(i);
        if (!light.onScreen) continue;

        float dist = length(uv - light.screenUV);
        float falloff = saturate(1.0 - dist / bloomRadius);
        falloff = falloff * falloff; // Quadratic falloff

        result += light.color * light.intensity * falloff;
    }

    return result;
}


//=============================================================================
//  [NEW v3.0] SECTION 24: SCENE SURFACE STATE
//
//  Surface wetness, snow coverage, and puddle depth for weather-reactive
//  material rendering. Enables dynamic wet surface darkening, snow tint,
//  and puddle reflections.
//=============================================================================

// Get scene wetness [0,1] from precipitation and recent rain
float SB_SceneWetness()
{
    // Wetness from current rain + lingering moisture
    float rainWetness = (SB_Precipitation.x == 1.0) ? SB_Precipitation.y : 0.0;
    float surfaceWetness = SB_Precip_Surface.x;
    return max(rainWetness, surfaceWetness);
}

// Get snow coverage [0,1] from snow accumulation
float SB_SnowCoverage()
{
    // Snow from current snowfall + accumulation
    float snowfall = (SB_Precipitation.x == 2.0) ? SB_Precipitation.y : 0.0;
    float accumulation = SB_Precip_Surface.z;
    return max(snowfall * 0.5, accumulation);
}

// Get puddle depth [0,1] for puddle reflections
float SB_PuddleDepth()
{
    return SB_Precip_Surface.y;
}

// Is it currently raining?
bool SB_IsRaining()
{
    return SB_Precipitation.x == 1.0 && SB_Precipitation.y > 0.0;
}

// Is it currently snowing?
bool SB_IsSnowing()
{
    return SB_Precipitation.x == 2.0 && SB_Precipitation.y > 0.0;
}

// Get wet surface darkening factor (for PBR roughness/albedo adjustment)
float SB_GetWetDarkening()
{
    return SB_SceneWetness() * 0.3; // 30% darker when fully wet
}

// Get snow tint color (white with slight blue)
float3 SB_GetSnowTint()
{
    return lerp(1.0, float3(0.95, 0.97, 1.0), SB_SnowCoverage());
}


//=============================================================================
//  [NEW v3.0] SECTION 25: COMBAT STATE HELPERS
//
//  Combat awareness for DOF, motion blur, and visual intensity adjustments.
//=============================================================================

// Is player currently in combat?
bool SB_IsInCombat()
{
    return SB_Player_Combat.x > 0.5;
}

// Is player in killcam?
bool SB_IsInKillcam()
{
    return SB_Player_Combat.z > 0.5;
}

// Get combat intensity [0,1] for visual intensity scaling
// Combines combat state, weapon drawn, and target hostility
float SB_CombatIntensity()
{
    float intensity = 0.0;
    if (SB_IsInCombat()) intensity += 0.5;
    if (SB_HasWeaponDrawn()) intensity += 0.2;
    if (SB_HasCrosshairTarget() && SB_IsTargetHostile()) intensity += 0.3;
    return saturate(intensity);
}

// Get combat DOF focus distance (snaps to hostile targets)
float SB_GetCombatFocusDistance(float defaultDist)
{
    if (SB_IsInCombat() && SB_HasCrosshairTarget() && SB_IsTargetActor())
    {
        return SB_GetTargetDistance();
    }
    return defaultDist;
}

// Get combat motion blur intensity (reduced during combat for clarity)
float SB_GetCombatMotionBlurScale()
{
    return SB_IsInCombat() ? 0.5 : 1.0;
}


//=============================================================================
//  [NEW v3.0] SECTION 26: TIME-OF-DAY INTERPOLATION (DNI System)
//
//  Day/Night/Interior blending weights for per-TOD parameter separation.
//  Compatible with Silent Horizons' DNI and 7-TOD systems.
//=============================================================================

// Basic Day/Night/Interior weights
// Returns: .x = day weight, .y = night weight, .z = interior weight
float3 SB_GetDNI()
{
    float3 dni = float3(0.0, 0.0, 0.0);

    // Interior check
    if (SB_Interior_Flags.x > 0.5)
    {
        dni.z = 1.0;
        return dni;
    }

    // Day/night based on sun elevation
    float sunElev = SB_Sun_Direction.w;
    float dayFactor = saturate(sunElev * 5.0 + 0.5); // Smooth transition

    dni.x = dayFactor;
    dni.y = 1.0 - dayFactor;

    return dni;
}

// 7 time-of-day weights (Dawn, Sunrise, Day, Sunset, Dusk, Night, Interior)
// More granular than DNI for precise per-time adjustments
struct SB_TOD7
{
    float dawn;     // Pre-sunrise glow
    float sunrise;  // Sun breaking horizon
    float day;      // Full daylight
    float sunset;   // Sun setting
    float dusk;     // Post-sunset glow
    float night;    // Full night
    float interior; // Indoor
};

SB_TOD7 SB_GetTOD7()
{
    SB_TOD7 tod;
    tod.dawn = 0.0;
    tod.sunrise = 0.0;
    tod.day = 0.0;
    tod.sunset = 0.0;
    tod.dusk = 0.0;
    tod.night = 0.0;
    tod.interior = 0.0;

    // Interior override
    if (SB_Interior_Flags.x > 0.5)
    {
        tod.interior = 1.0;
        return tod;
    }

    float hour = SB_Time.x;
    float sunrise = SB_Time.y;  // ~6.0
    float sunset = SB_Time.z;   // ~19.0

    // Calculate time periods
    float dawnStart = sunrise - 1.5;
    float dawnEnd = sunrise;
    float sunriseEnd = sunrise + 1.0;
    float dayEnd = sunset - 1.0;
    float sunsetEnd = sunset;
    float duskEnd = sunset + 1.5;

    if (hour < dawnStart || hour >= duskEnd)
    {
        tod.night = 1.0;
    }
    else if (hour < dawnEnd)
    {
        float t = (hour - dawnStart) / (dawnEnd - dawnStart);
        tod.night = 1.0 - t;
        tod.dawn = t;
    }
    else if (hour < sunriseEnd)
    {
        float t = (hour - dawnEnd) / (sunriseEnd - dawnEnd);
        tod.dawn = 1.0 - t;
        tod.sunrise = t;
    }
    else if (hour < dayEnd)
    {
        float t = (hour - sunriseEnd) / (dayEnd - sunriseEnd);
        tod.sunrise = 1.0 - saturate(t * 4.0); // Quick fade from sunrise
        tod.day = saturate(t * 4.0);
    }
    else if (hour < sunsetEnd)
    {
        float t = (hour - dayEnd) / (sunsetEnd - dayEnd);
        tod.day = 1.0 - t;
        tod.sunset = t;
    }
    else
    {
        float t = (hour - sunsetEnd) / (duskEnd - sunsetEnd);
        tod.sunset = 1.0 - t;
        tod.dusk = t;
    }

    return tod;
}

// Interpolate a value using 7-TOD weights
float SB_TOD7Lerp(float dawn, float sunrise, float day, float sunset, float dusk, float night, float interior)
{
    SB_TOD7 tod = SB_GetTOD7();
    return dawn * tod.dawn + sunrise * tod.sunrise + day * tod.day +
           sunset * tod.sunset + dusk * tod.dusk + night * tod.night +
           interior * tod.interior;
}


//=============================================================================
//  [NEW v3.0] SECTION 27: INTERIOR LIGHTING HELPERS
//
//  Access interior lighting template data for interior-aware post-processing.
//=============================================================================

// Is player currently indoors?
bool SB_IsInterior()
{
    return SB_Interior_Flags.x > 0.5;
}

// Does current cell have a lighting template?
bool SB_HasLightingTemplate()
{
    return SB_Interior_Flags.y > 0.5;
}

// Get interior directional light direction (for contact shadows, rim lighting)
float3 SB_InteriorLightDir()
{
    if (!SB_IsInterior() || !SB_HasLightingTemplate())
    {
        return normalize(float3(0.5, 0.5, 1.0)); // Default fallback
    }
    return normalize(SB_Interior_DirDir.xyz);
}

// Get interior ambient color
float3 SB_InteriorAmbient()
{
    if (!SB_IsInterior()) return SB_Atmos_Ambient.rgb;
    return SB_Interior_Ambient.rgb * SB_Interior_Ambient.a;
}

// Get interior directional light color
float3 SB_InteriorDirectional()
{
    if (!SB_IsInterior()) return SB_Atmos_Sunlight.rgb;
    return SB_Interior_DirColor.rgb * SB_Interior_DirColor.a;
}

// Get interior fog color (for fog color matching)
float3 SB_InteriorFogColor()
{
    if (!SB_IsInterior()) return SB_Fog_FarColor.rgb;
    return SB_Interior_FogColor.rgb;
}


//=============================================================================
//  [NEW v3.0] SECTION 28: SPELL SCHOOL DETECTION
//
//  Detect active spell school for spell-reactive visual effects.
//  Magic school colors: Destruction=orange/fire, Conjuration=purple,
//  Alteration=green, Illusion=blue, Restoration=gold
//=============================================================================

// Spell school enum (matches game's MagicSchool enum)
static const int SB_SCHOOL_NONE        = 0;
static const int SB_SCHOOL_ALTERATION  = 1;
static const int SB_SCHOOL_CONJURATION = 2;
static const int SB_SCHOOL_DESTRUCTION = 3;
static const int SB_SCHOOL_ILLUSION    = 4;
static const int SB_SCHOOL_RESTORATION = 5;

// Get dominant spell school from equipment
// Uses highest skill level among equipped spells
int SB_GetActiveSpellSchool()
{
    // Check if casting (spell in either hand)
    if (!SB_IsCasting()) return SB_SCHOOL_NONE;

    // Use skill levels to determine dominant school
    // Higher skill = more likely that school is being cast
    float alteration = SB_AV_SkillMagic.x;
    float conjuration = SB_AV_SkillMagic.y;
    float destruction = SB_AV_SkillMagic.z;
    float illusion = SB_AV_SkillMagic.w;
    float restoration = SB_AV_SkillMagic2.x;

    // Find highest (crude heuristic - proper implementation would track equipped spell)
    float maxSkill = max(max(max(alteration, conjuration), max(destruction, illusion)), restoration);

    if (maxSkill == destruction) return SB_SCHOOL_DESTRUCTION;
    if (maxSkill == conjuration) return SB_SCHOOL_CONJURATION;
    if (maxSkill == restoration) return SB_SCHOOL_RESTORATION;
    if (maxSkill == illusion) return SB_SCHOOL_ILLUSION;
    if (maxSkill == alteration) return SB_SCHOOL_ALTERATION;

    return SB_SCHOOL_NONE;
}

// Get spell school color for visual effects
float3 SB_GetSpellSchoolColor(int school)
{
    if (school == SB_SCHOOL_DESTRUCTION) return float3(1.0, 0.4, 0.1);  // Fire orange
    if (school == SB_SCHOOL_CONJURATION) return float3(0.6, 0.2, 0.8);  // Purple
    if (school == SB_SCHOOL_RESTORATION) return float3(1.0, 0.9, 0.4);  // Gold
    if (school == SB_SCHOOL_ILLUSION)    return float3(0.3, 0.5, 1.0);  // Blue
    if (school == SB_SCHOOL_ALTERATION)  return float3(0.3, 0.9, 0.4);  // Green
    return float3(1.0, 1.0, 1.0); // White default
}

// Get active spell color (combines school color with casting state)
float3 SB_GetActiveSpellColor()
{
    int school = SB_GetActiveSpellSchool();
    return SB_GetSpellSchoolColor(school);
}


//=============================================================================
//  GRACEFUL FALLBACK HELPERS
//
//  These functions provide fallback values when SkyrimBridge is not active,
//  ensuring shaders work standalone without the SKSE plugin.
//=============================================================================

// Get sun direction with fallback
float3 SB_GetSunDirection(float3 fallback)
{
    return SB_IsActive() ? SB_Sun_Direction.xyz : fallback;
}

// Get camera position with fallback
float3 SB_GetCameraPos(float3 fallback)
{
    return SB_IsActive() ? SB_Camera_WorldPos.xyz : fallback;
}

// Get game hour with fallback (default noon)
float SB_GetGameHour(float fallback)
{
    return SB_IsActive() ? SB_Time.x : fallback;
}

// Get fog density with fallback
float SB_GetFogDensity(float fallback)
{
    return SB_IsActive() ? SB_Fog_Density.x : fallback;
}


#endif // SKYRIMBRIDGE_FXH
