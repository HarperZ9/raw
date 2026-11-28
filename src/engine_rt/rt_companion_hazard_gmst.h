#pragma once
// rt_companion_hazard_gmst.h: Milestone Wave 55 Binary-Verified 1.7.104.0 Companion, Summon,
// Dialogue, Gear-AI & Environmental Hazard GMSTs (Crossing 226 Total Zero-Overlap Verified GMSTs!).
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

struct CompanionHazardGmstSpec {
    const char* name;
    uint32_t    expectedRva;     // Setting object RVA in SkyrimSE.exe 1.7.104.0 (value at +0x08)
    bool        isInt32;         // true = i* int32 GMST, false = f* float GMST
    float       exeDefaultF;     // Exact on-disk float default (when !isInt32)
    float       balancedF;       // 2027 balanced float target (when !isInt32)
    int32_t     exeDefaultI;     // Exact on-disk int32 default (when isInt32)
    int32_t     balancedI;       // 2027 balanced int32 target (when isInt32)
    bool        hasLiveReader;   // Proven by overlap-safe .text RIP-relative scan (never writes 0-reader GMSTs!)
    const char* category;        // "Companion" | "Summon" | "Dialogue" | "GearAI" | "CombatBark" | "Hazard" | "Torch"
};

const CompanionHazardGmstSpec* CompanionHazardGmstTable(size_t* outCount = nullptr);

// Direct PE-image verification of every GMST RVA, NUL-terminated name pointer (+0x10), and on-disk default (+0x08).
// Fail-closed: returns false if ANY entry mismatches!
bool VerifyCompanionHazardGmstTable(const Image& img, uint32_t* outVerifiedCount = nullptr);

// =================================================================================================
// First-Principles NPC Gear Desirability & Environmental Hazard Exposure Evaluators
// =================================================================================================
struct NpcGearInput {
    float baseDamageOrArmor   = 18.0f;
    float actorSkill0_100     = 50.0f;
    float enchantPower        = 0.0f;
    float itemWeight          = 10.0f;
    bool  isCurrentlyEquipped = false;
    bool  isRangedPreferred   = false;
    bool  isRangedWeapon      = false;
};

struct NpcGearScore {
    float rawCombatScore   = 0.0f;
    float finalScore       = 0.0f;  // includes equipped hysteresis & ranged preference multiplier
    bool  isUpgradeOverRef = false;
};

NpcGearScore EvaluateNpcGearDesirability(const NpcGearInput& in, float equippedRefScore = 0.0f);

struct HazardInput {
    float baseDps            = 30.0f;  // fire/lava/oil/blade hazard base DPS
    float armorRating        = 200.0f; // physical armor rating (for traps)
    float elementalResistPct = 0.0f;   // 0..100 elemental resist (for fire/frost/shock/poison hazards)
    float exposureSeconds    = 2.0f;   // duration inside hazard volume
    bool  isPhysicalTrap     = false;  // true = swinging blades/darts (armor), false = fire/lava (elemental resist)
};

struct HazardExposureResult {
    float mitigationFrac = 0.0f;  // bounded [0 .. 0.85]
    float effectiveDps   = 0.0f;
    float totalDamage    = 0.0f;
};

HazardExposureResult EvaluateHazardExposure(const HazardInput& in);

// =================================================================================================
// Live Runtime State & C ABI Export (RAW_GetCompanionHazardState)
// =================================================================================================
struct RawCompanionHazardState {
    uint8_t gmstsVerified            = 0u;
    uint8_t gmstsAppliedCount        = 0u;
    uint8_t deadLegacySkipped        = 0u;
    uint8_t companionHazardActive    = 1u;
    int32_t maxAllowedFollowers      = 8;
    int32_t maxSummonedCreatures     = 1;
    float   summonDistanceThresholdU = 950.0f;
    float   dialogueDistanceU        = 200.0f;
    float   hazardTickIntervalSec    = 0.25f;
    float   torchNightLightThreshold = 1.35f;
};

#ifndef RAW_SIM_PURE_TEST
std::string InitCompanionHazardGmst(const Image& img, const std::vector<RefTarget>& refs);
void        ShutdownCompanionHazardGmst();
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetCompanionHazardState(raw::rt::RawCompanionHazardState* out);
