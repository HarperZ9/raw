#pragma once
// rt_encounter_balance.h: Wave 46 Binary-Verified 2027 Archery Ballistics, De-Exploited Armor Curve
// & Combat Magicka/Stamina Resource Balance verified directly against SkyrimSE.exe 1.7.104.0 GMSTs.
// 100% binary-verified RVAs & on-disk defaults, fail-closed, SEH-guarded, cleanly reversible (Zero SKSE).
// Original work, MIT.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace raw::rt {
struct Image;
struct RefTarget;

struct EncounterBalanceGmstSpec {
    const char* name;
    uint32_t    expectedRva;      // Setting object RVA in SkyrimSE.exe 1.7.104.0 (float value sits at +0x08)
    float       exeDefault;       // Exact on-disk default float verified in SkyrimSE.exe 1.7.104.0 .data
    float       balancedValue;    // 2027 balanced target applied when NonSpongyDifficulty=1
    bool        hasLiveReader;    // Proven by .text RIP-relative disassembly scan (never writes dead 0-reader GMSTs!)
    const char* category;         // "Archery" | "Armor" | "Resources"
};

// Returns the complete table of binary-verified 1.7.104.0 archery, armor & combat resource GMSTs.
const EncounterBalanceGmstSpec* EncounterBalanceTable(size_t* outCount = nullptr);

// Pure deterministic physical evaluators matching exact SkyrimSE 1.7.104.0 x86-64 formulas:
struct ArrowBallisticsResult {
    float drawPower01     = 0.0f;  // [fArrowMinPower .. 1.0]
    float velocityMult    = 1.0f;
    float gravityFactor   = 0.0f;  // strictly positive physical drop (fixes old 0.85 anti-gravity bug!)
};
ArrowBallisticsResult EvaluateArrowBallistics(float drawTimeSec, float bowSpeed, float projBaseSpeed,
                                              float arrowBowMinTime, float bowDrawTime,
                                              float arrowGravityBase, float arrowGravityMult,
                                              float arrowWeakGravity, float arrowFastMult);

float EvaluateArmorMitigationPct(float effectiveArmor, float scalingFactor, float maxArmorRating);
float EvaluatePowerAttackStaminaCost(float weaponWeight, float baseCost, float weightMult, float penaltyMult);

// Direct PE-image verification of every GMST RVA, NUL-terminated name pointer (+0x10), and on-disk default (+0x08).
// Fail-closed: returns false if ANY entry mismatches!
bool VerifyEncounterBalanceGmstTable(const Image& img, uint32_t* outVerifiedCount = nullptr);

#ifndef RAW_SIM_PURE_TEST
// Applies all live-read encounter balance GMSTs (saving pre-override values first, gated on GetLiveConfig().nonSpongyDifficulty).
std::string ApplyEncounterBalanceGmsts(const Image& img, const std::vector<RefTarget>& refs);
// Cleanly restores every modified GMST back to its saved pre-override value.
void        RestoreEncounterBalanceGmsts();
#endif

}  // namespace raw::rt
