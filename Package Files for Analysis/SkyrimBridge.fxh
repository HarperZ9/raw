#ifndef SKYRIMBRIDGE_FXH
#define SKYRIMBRIDGE_FXH
//=============================================================================
//  SkyrimBridge.fxh — HLSL declarations for SkyrimBridge external parameters
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
//  Version: 2.0.0
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
//=============================================================================


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


#endif // SKYRIMBRIDGE_FXH
