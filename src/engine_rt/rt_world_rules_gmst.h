#pragma once
// rt_world_rules_gmst.h: Wave 50 Binary-Verified 1.7.104.0 Dragon Combat, Stagger Poise,
// Swim/Water Physics & Crime/Witness Realism GMSTs (Zero SKSE).
// 100% binary-verified RVAs, .rdata name pointers & on-disk defaults (float + int32);
// fail-closed, SEH-guarded, cleanly reversible.
// Original work, MIT.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace raw::rt {
struct Image;
struct RefTarget;

struct WorldRulesGmstSpec {
    const char* name;
    uint32_t    expectedRva;     // Setting object RVA in SkyrimSE.exe 1.7.104.0 (value at +0x08)
    bool        isInt32;         // true = i* int32 GMST, false = f* float GMST
    float       exeDefaultF;     // Exact on-disk float default (when !isInt32)
    float       balancedF;       // 2027 balanced float target (when !isInt32)
    int32_t     exeDefaultI;     // Exact on-disk int32 default (when isInt32)
    int32_t     balancedI;       // 2027 balanced int32 target (when isInt32)
    bool        hasLiveReader;   // Proven by .text RIP-relative disassembly scan (never writes dead 0-reader GMSTs!)
    const char* category;        // "Crime" | "Stagger" | "Water" | "Dragon"
};

const WorldRulesGmstSpec* WorldRulesGmstTable(size_t* outCount = nullptr);

// Direct PE-image verification of every GMST RVA, NUL-terminated name pointer (+0x10), and on-disk default (+0x08).
// Fail-closed: returns false if ANY entry mismatches!
bool VerifyWorldRulesGmstTable(const Image& img, uint32_t* outVerifiedCount = nullptr);

// =================================================================================================
// First-Principles Crime Witness & Cold-Water Survival Breath Evaluators (pure math, deterministic)
// =================================================================================================
struct CrimeEnvInput {
    float baseAlarmRadiusU = 2400.0f; // grounded 1.7.104 alarm radius (Skyrim units)
    float rain01           = 0.0f;    // 0..1 precipitation intensity
    float snow01           = 0.0f;    // 0..1 blizzard / snowfall density
    float fog01            = 0.0f;    // 0..1 volumetric fog density
    float nightDarkness01  = 0.0f;    // 0 = clear noon, 1 = moonless midnight
    float windBeaufort     = 2.0f;    // 0..12 Beaufort wind scale (masks acoustic propagation)
    bool  inInterior       = false;   // sheltered from outdoor precipitation/wind
    bool  isStealthCrime   = false;   // non-violent stealth crime uses quieter propagation envelope
};

struct CrimeWitnessResult {
    float effectiveSightRadiusU = 2400.0f;
    float effectiveSoundRadiusU = 2400.0f;
    float effectiveAlarmRadiusU = 2400.0f;
    float attenuationFactor01   = 1.0f;   // 0..1 fraction of base radius retained
};

CrimeWitnessResult EvaluateCrimeWitnessRadius(const CrimeEnvInput& in);

struct WaterBreathResult {
    float breathDurationSec = 22.5f;  // usable underwater breath duration (seconds)
    float coldShockFactor01 = 0.0f;   // 0 = temperate water, 1 = 0 C Sea of Ghosts glacial shock
    float drowningDpsFrac   = 0.25f;  // health fraction drained per second once breath expires
};

WaterBreathResult EvaluateWaterSurvivalBreath(float waterTempC, float stamina);

// =================================================================================================
// Live Runtime State & C ABI Export (RAW_GetWorldRulesState)
// =================================================================================================
struct RawWorldRulesState {
    uint8_t gmstsVerified          = 0u;
    uint8_t gmstsAppliedCount      = 0u;
    uint8_t deadLegacySkipped      = 0u;
    uint8_t worldRulesActive       = 1u;
    float   crimeAlarmRadiusU      = 2400.0f;
    float   effectiveWitnessRadius = 2400.0f;
    float   waterBreathSec         = 22.5f;
    float   coldShockFactor01      = 0.0f;
    float   dragonLandingRadiusU   = 200.0f;
    int32_t murderBountyGold       = 2500;
};

void WorldRulesUpdateEnvironment(const CrimeEnvInput& crimeEnv, float waterTempC, float stamina);

#ifndef RAW_SIM_PURE_TEST
std::string InitWorldRulesGmst(const Image& img, const std::vector<RefTarget>& refs);
void        ShutdownWorldRulesGmst();
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetWorldRulesState(raw::rt::RawWorldRulesState* out);
