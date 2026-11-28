#pragma once
// rt_locomotion_melee_gmst.h: Wave 53 Binary-Verified 1.7.104.0 Locomotion, Armor-Weight Fatigue,
// Power-Attack & Melee Combat GMSTs (Zero SKSE).
// 100% binary-verified RVAs, .rdata name pointers & on-disk defaults; fail-closed, SEH-guarded, cleanly reversible.
// Original work, MIT.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace raw::rt {
struct Image;
struct RefTarget;

struct LocomotionMeleeGmstSpec {
    const char* name;
    uint32_t    expectedRva;     // Setting object RVA in SkyrimSE.exe 1.7.104.0 (float value at +0x08)
    float       exeDefault;      // Exact on-disk float default verified in SkyrimSE.exe 1.7.104.0 .data
    float       balancedValue;   // 2027 balanced target
    bool        hasLiveReader;   // Proven by exhaustive .text ModRM scan (never writes dead 0-reader GMSTs!)
    const char* category;        // "Locomotion" | "Fatigue" | "PowerAttack" | "Melee"
};

const LocomotionMeleeGmstSpec* LocomotionMeleeGmstTable(size_t* outCount = nullptr);

// Direct PE-image verification of every GMST RVA, NUL-terminated name pointer (+0x10), and on-disk default (+0x08).
// Fail-closed: returns false if ANY entry mismatches!
bool VerifyLocomotionMeleeGmstTable(const Image& img, uint32_t* outVerifiedCount = nullptr);

// =================================================================================================
// First-Principles Armor-Weight Locomotion & Power-Attack Stamina Evaluators (pure math, finite)
// =================================================================================================
struct LocomotionInput {
    float equippedArmorWeight = 0.0f;   // 0 = robes/unarmored, 20 = leather/scout, 70+ = heavy plate
    bool  weaponDrawn         = false;
    bool  isSprinting         = false;
    bool  isSneaking          = false;
};

struct LocomotionProfile {
    float armorWeightFrac01       = 0.0f;   // [0..1] normalized armor encumbrance fraction
    float speedMultiplier         = 1.0f;   // [0.60..1.0] movement speed scalar
    float effectiveSpeedUnitsSec  = 385.0f; // effective ground speed (Skyrim units/s)
    float sprintStaminaDrainPerS  = 12.3f;  // stamina/sec drained while sprinting (scales with armor weight!)
};

LocomotionProfile EvaluateLocomotionProfile(const LocomotionInput& in);

struct PowerAttackInput {
    float weaponWeight      = 12.0f;  // e.g. dagger 3, 1H sword 12, 2H warhammer 28
    float combatSkill0_100  = 40.0f;  // actor 1H/2H skill level 0..100
    bool  isTwoHanded       = false;
    bool  isDualPowerAttack = false;
};

struct PowerAttackResult {
    float staminaCost      = 20.0f;  // strictly bounded finite stamina cost
    float effectiveReachU  = 96.0f;  // melee sweep reach estimate (Skyrim units)
    float hitConeAngleDeg  = 24.0f;  // precision directional hit cone
};

PowerAttackResult EvaluatePowerAttackCost(const PowerAttackInput& in);

// =================================================================================================
// Live Runtime State & C ABI Export (RAW_GetLocomotionMeleeState)
// =================================================================================================
struct RawLocomotionMeleeState {
    uint8_t gmstsVerified            = 0u;
    uint8_t gmstsAppliedCount        = 0u;
    uint8_t deadLegacySkipped        = 0u;
    uint8_t locomotionMeleeActive    = 1u;
    float   moveCharRunBase          = 385.0f;
    float   moveSprintMult           = 1.38f;
    float   sprintWeightMult         = 0.008f;
    float   staminaAttackWeaponBase  = 8.5f;
    float   staminaAttackWeaponMult  = 0.14f;
    float   combatHitConeAngleDeg    = 24.0f;
};

#ifndef RAW_SIM_PURE_TEST
std::string InitLocomotionMeleeGmst(const Image& img, const std::vector<RefTarget>& refs);
void        ShutdownLocomotionMeleeGmst();
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetLocomotionMeleeState(raw::rt::RawLocomotionMeleeState* out);
