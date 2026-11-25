#pragma once
// rt_stealth_social_gmst.h: Wave 52 Binary-Verified 1.7.104.0 Stealth Sound/Light, Lockpicking,
// Pickpocket & Speechcraft/Bribery GMSTs (Zero SKSE).
// 100% binary-verified RVAs, .rdata name pointers & on-disk defaults; fail-closed, SEH-guarded, cleanly reversible.
// Original work, MIT.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace raw::rt {
struct Image;
struct RefTarget;

struct StealthSocialGmstSpec {
    const char* name;
    uint32_t    expectedRva;     // Setting object RVA in SkyrimSE.exe 1.7.104.0 (float value at +0x08)
    float       exeDefault;      // Exact on-disk float default verified in SkyrimSE.exe 1.7.104.0 .data
    float       balancedValue;   // 2027 balanced target
    bool        hasLiveReader;   // Proven by exhaustive .text ModRM scan (never writes dead 0-reader GMSTs!)
    const char* category;        // "Stealth" | "Lockpick" | "Pickpocket" | "Social"
};

const StealthSocialGmstSpec* StealthSocialGmstTable(size_t* outCount = nullptr);

// Direct PE-image verification of every GMST RVA, NUL-terminated name pointer (+0x10), and on-disk default (+0x08).
// Fail-closed: returns false if ANY entry mismatches!
bool VerifyStealthSocialGmstTable(const Image& img, uint32_t* outVerifiedCount = nullptr);

// =================================================================================================
// First-Principles Stealth Acoustic/Shadow & Pickpocket Risk/Reward Evaluators (pure math, finite)
// =================================================================================================
struct StealthEnvInput {
    float armorBootWeight = 4.0f;    // Skyrim weight units (0 = bare/cloth, 2..6 = leather, 12..25 = heavy plate)
    float lightLevel01    = 0.5f;    // 0 = pitch shadow, 1 = bright sunlit/torchlit floor
    float sneakSkill0_100 = 40.0f;   // actor sneak skill 0..100
    float rain01          = 0.0f;    // 0..1 precipitation acoustic masking
    float windBeaufort    = 2.0f;    // 0..12 ambient wind noise masking
    bool  isRunning       = false;   // full jog/sprint vs crouching sneak
    bool  isMoving        = true;    // stationary vs moving
};

struct StealthEvalResult {
    float audibleNoiseScore     = 0.0f;  // composite acoustic loudness (higher = louder)
    float audibleRadiusU        = 0.0f;  // effective footstep hearing radius in Skyrim units
    float visualExposureScore   = 0.0f;  // visual visibility exposure (higher = more visible)
    float compositeDetectRisk01 = 0.0f;  // [0..1] overall detection risk
};

StealthEvalResult EvaluateStealthNoiseAndVisibility(const StealthEnvInput& in);

struct PickpocketInput {
    float actorSkill0_100   = 50.0f;
    float targetSkill0_100  = 35.0f;
    float itemWeight        = 1.0f;
    float itemValueGold     = 100.0f;
    bool  targetIsSleeping  = false;
    bool  isDetected        = false;
    float minChancePct      = 5.0f;   // matches verified fPickPocketMinChance
    float maxChancePct      = 92.0f;  // matches verified fPickPocketMaxChance
};

struct PickpocketResult {
    float rawChancePct     = 50.0f;
    float clampedChancePct = 50.0f;   // strictly bounded in [minChancePct .. maxChancePct]
    bool  wasClamped       = false;
};

PickpocketResult EvaluatePickpocketChance(const PickpocketInput& in);

// =================================================================================================
// Live Runtime State & C ABI Export (RAW_GetStealthSocialState)
// =================================================================================================
struct RawStealthSocialState {
    uint8_t gmstsVerified            = 0u;
    uint8_t gmstsAppliedCount        = 0u;
    uint8_t deadLegacySkipped        = 0u;
    uint8_t stealthSocialActive      = 1u;
    float   sneakMaxDistanceU        = 1850.0f;
    float   sneakEquippedWeightMult  = 0.68f;
    float   lockpickSweetSpotBase    = 0.72f;
    float   pickpocketMinChancePct   = 5.0f;
    float   pickpocketMaxChancePct   = 92.0f;
    float   bribeScale               = 1.35f;
};

#ifndef RAW_SIM_PURE_TEST
std::string InitStealthSocialGmst(const Image& img, const std::vector<RefTarget>& refs);
void        ShutdownStealthSocialGmst();
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetStealthSocialState(raw::rt::RawStealthSocialState* out);
