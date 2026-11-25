#pragma once
// rt_shouts_survival_gmst.h: Wave 57 Binary-Verified 1.7.104.0 Shouts, Disease/Bleedout,
// Survival Warmth/Hydrodynamics & Leveling/XP GMSTs (282 Total Zero-Overlap Verified GMSTs!).
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

struct ShoutsSurvivalGmstSpec {
    const char* name;
    uint32_t    expectedRva;     // Setting object RVA in SkyrimSE.exe 1.7.104.0 (value at +0x08)
    bool        isInt32;         // true = i* int32 GMST, false = f* float GMST
    float       exeDefaultF;     // Exact on-disk float default (when !isInt32)
    float       balancedF;       // 2027 balanced float target (when !isInt32)
    int32_t     exeDefaultI;     // Exact on-disk int32 default (when isInt32)
    int32_t     balancedI;       // 2027 balanced int32 target (when isInt32)
    bool        hasLiveReader;   // Proven by overlap-safe .text RIP-relative scan (never writes 0-reader GMSTs!)
    const char* category;        // "Shout" | "Disease" | "Bleedout" | "Ward" | "Survival" | "Leveling"
};

const ShoutsSurvivalGmstSpec* ShoutsSurvivalGmstTable(size_t* outCount = nullptr);

// Direct PE-image verification of every GMST RVA, NUL-terminated name pointer (+0x10), and on-disk default (+0x08).
// Fail-closed: returns false if ANY entry mismatches!
bool VerifyShoutsSurvivalGmstTable(const Image& img, uint32_t* outVerifiedCount = nullptr);

// =================================================================================================
// First-Principles Shout Recovery, Survival Warmth & Leveling XP Curve Evaluators
// =================================================================================================
struct ShoutCooldownResult {
    float holdTimeSec       = 0.16f;
    float baseCooldownSec   = 15.0f;
    float finalCooldownSec  = 15.0f;  // bounded [3.0 .. 180.0]
};

ShoutCooldownResult EvaluateShoutCooldown(uint32_t wordCount1To3, float baseWordCooldownSec,
                                          float cooldownReductionFrac = 0.0f);

struct SurvivalWarmthResult {
    float totalWarmthRating = 50.0f;  // [0 .. 300]
    float coldDrainMult     = 1.0f;   // lower = better cold resistance [0.22 .. 1.65]
};

SurvivalWarmthResult EvaluateSurvivalWarmth(float armorWarmthBase, bool hasTorch, bool isWet, float airTempC);

// Exact 0x78DC60 Skyrim level-up XP formula: fXPLevelUpBase + (currentLevel - 1) * fXPLevelUpMult
float EvaluateLevelUpXpRequired(uint32_t currentLevel, float xpBase = 180.0f, float xpMult = 32.0f);

// =================================================================================================
// Live Runtime State & C ABI Export (RAW_GetShoutsSurvivalState)
// =================================================================================================
struct RawShoutsSurvivalState {
    uint8_t gmstsVerified        = 0u;
    uint8_t gmstsAppliedCount    = 0u;
    uint8_t deadLegacySkipped    = 0u;
    uint8_t shoutsSurvivalActive = 1u;
    float   shoutTime1Sec        = 0.16f;
    float   shoutTime2Sec        = 0.72f;
    float   survArmorScalar      = 1.12f;
    float   survTorchBonus       = 60.0f;
    float   xpLevelUpBase        = 180.0f;
    float   xpLevelUpMult        = 32.0f;
};

#ifndef RAW_SIM_PURE_TEST
std::string InitShoutsSurvivalGmst(const Image& img, const std::vector<RefTarget>& refs);
void        ShutdownShoutsSurvivalGmst();
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetShoutsSurvivalState(raw::rt::RawShoutsSurvivalState* out);
