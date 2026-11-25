#pragma once
// rt_world_sim.h: Live Zero-SKSE World Simulation Engine inside raw_engine.dll.
// Runs every frame in OnPresent (without SKSE or external mods) uniting:
//   - Background clean-room RFC 1950/1951 zlib Skyrim.esm NAVM extraction into live RawNavGraph
//   - Live tactical squad flanking + Mean-Payoff Stochastic-Game combat policy (rt_combat_ai.h)
//   - Live living-world weather/time NPC ecosystem directives (shelter, hearth, dusk torch)
//   - Live 2-bone terrain foot-planting IK + volumetric snow-sink stride calibration (rt_skeleton_ik.h)
// Original work, MIT.

#include <cstdint>
#include <string>

namespace raw::rt {
struct Image;

struct RawWorldSimTelemetry {
    uint32_t simTicks          = 0;
    uint32_t actorsTracked     = 0;
    uint32_t navMeshesLoaded   = 0;
    uint32_t navVertsLoaded    = 0;
    uint32_t navTrisLoaded     = 0;
    uint8_t  activeDirective   = 0;   // WorldNpcDirective
    uint8_t  tacticalAction    = 1;   // TacticalAction (0=Attack,1=Flank,2=Parry,3=Retreat)
    uint8_t  attackTokensUsed  = 0;
    uint8_t  navReady          = 0;
    float    expectedPayoff    = 0.0f;
    float    strideDampMult    = 1.0f;
    float    snowDepthU        = 0.0f;
    float    windSpeed         = 0.0f;
    float    ambientTempC      = 12.0f;
    float    coreWarmth        = 85.0f;
    float    shiverIntensity   = 0.0f;
    float    staminaRegenMult  = 1.0f;
    float    physicsSubstepMs  = 8.33f;
    float    acousticRt60Sec   = 0.25f;
    float    acousticLowPassHz = 18000.0f;
    uint8_t  physicsSubsteps   = 2;
    uint8_t  acousticEnvClass  = 0;
    uint8_t  freezingRisk      = 0;
    uint8_t  marketTier        = 1;
    float    economyBuyMult    = 1.0f;
    float    lockOnFovDeg      = 80.0f;
    float    largestSaveMb     = 0.0f;
    uint32_t totalSavesFound   = 0;
    uint8_t  camLockedOn       = 0;
    uint8_t  saveBloatWarn     = 0;
    uint8_t  squadMstComps     = 0;
    uint8_t  squadMstRounds    = 0;
    float    squadSpanLengthU  = 0.0f;
    float    synopticHpa       = 1013.25f;
    float    adaptiveQuality   = 1.0f;
    float    synopticCloud     = 0.25f;
    float    synopticRain      = 0.0f;
    float    synopticSnow      = 0.0f;
    float    synopticLightning = 0.0f;
    // Wave 34 Unified Planetary Gravity, Dual-Moon Harmonic Tides & Sea State:
    float    gravityMps2       = 9.80665f;
    float    tideHeightU       = 0.0f;
    float    tideVelocityMs    = 0.0f;
    float    springTide01      = 0.5f;
    float    beaufortScale     = 2.0f;
    float    swellDirX         = 0.866f;
    float    swellDirY         = 0.500f;
    float    simClockSec       = 0.0f;
};

void WorldSimInit(const Image& img, const std::string& gameDir);
void WorldSimOnPresent();
void WorldSimShutdown();
float WorldSimGetShiverIntensity();

// Wave 35: Pure deterministic guard for live Creation Engine TESObjectCELL::waterHeight tidal buoyancy coupling.
float ComputeBoundedTidalCellHeight(float baseWaterHeight, float tideHeightU, bool isInterior);

// Deterministic single-step helper (also directly unit-testable outside SkyrimSE.exe).
RawWorldSimTelemetry StepWorldSimSnapshot(const float playerPos[3], float gameHour,
                                          float rain01, float snow01, float windSpeed,
                                          bool inInterior, const float* actorXyzw, uint32_t actorCount,
                                          uint32_t navMeshes, uint32_t navVerts, uint32_t navTris,
                                          float frameDtSec = 1.0f / 60.0f, float prevWarmth = 85.0f);

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetWorldSimTelemetry(raw::rt::RawWorldSimTelemetry* out);
