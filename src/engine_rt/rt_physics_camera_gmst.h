#pragma once
// rt_physics_camera_gmst.h: Wave 51 Binary-Verified 1.7.104.0 Havok Ragdoll Impulse, Shield Block,
// Combat Regen, Bash & Mounted Camera GMSTs (Zero SKSE).
// 100% binary-verified RVAs, .rdata name pointers & on-disk defaults; fail-closed, SEH-guarded, cleanly reversible.
// Original work, MIT.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace raw::rt {
struct Image;
struct RefTarget;

struct PhysicsCameraGmstSpec {
    const char* name;
    uint32_t    expectedRva;     // Setting object RVA in SkyrimSE.exe 1.7.104.0 (float value at +0x08)
    float       exeDefault;      // Exact on-disk float default verified in SkyrimSE.exe 1.7.104.0 .data
    float       balancedValue;   // 2027 physically-grounded balanced target
    bool        hasLiveReader;   // Proven by exhaustive .text ModRM scan (never writes dead 0-reader GMSTs!)
    const char* category;        // "Ragdoll" | "Physics" | "Block" | "Bash" | "Regen" | "Camera"
};

const PhysicsCameraGmstSpec* PhysicsCameraGmstTable(size_t* outCount = nullptr);

// Direct PE-image verification of every GMST RVA, NUL-terminated name pointer (+0x10), and on-disk default (+0x08).
// Fail-closed: returns false if ANY entry mismatches!
bool VerifyPhysicsCameraGmstTable(const Image& img, uint32_t* outVerifiedCount = nullptr);

// =================================================================================================
// First-Principles Grounded Ragdoll Momentum Clamp & Directional Shield Block Mitigation Evaluators
// =================================================================================================
struct RagdollImpulseInput {
    float projectileMassKg = 0.04f;  // e.g. arrow 0.04 kg, sword 1.4 kg, warhammer 3.5 kg
    float impactSpeedMps   = 65.0f;  // impact velocity in meters/second
    float damageAmount     = 45.0f;  // hit damage feeding Creation Engine death-force curve
    float targetMassKg     = 75.0f;  // struck actor body mass (kg)
    bool  isRanged         = false;  // arrows/bolts use tighter kinetic pin ceiling
};

struct RagdollImpulseResult {
    float rawMomentumNs     = 0.0f;  // p = m * v (N*s)
    float cappedImpulseN    = 0.0f;  // smoothly bounded Havok death impulse (strictly <= ceiling!)
    float launchVelocityMps = 0.0f;  // resulting body velocity (m/s), strictly finite & non-catapulting
    bool  wasClamped        = false; // true when soft kinetic ceiling actively damped excess force
};

RagdollImpulseResult EvaluateRagdollDeathImpulse(const RagdollImpulseInput& in);

enum class BlockGearType : uint8_t {
    BareHand = 0u,
    Weapon   = 1u,
    Shield   = 2u,
};

struct BlockMitigationInput {
    BlockGearType gearType          = BlockGearType::Shield;
    float         blockSkill0_100   = 50.0f;
    float         shieldArmorRating = 32.0f;  // relevant when gearType == Shield
    bool          isTimedParry      = false;  // precision guard window bonus
    bool          isPowerAttack     = false;  // heavy power attack guard-penetration factor
    float         maxCapFrac        = 0.85f;  // hard mitigation cap (matches fPlayerMaxResistance=85%)
};

struct BlockMitigationResult {
    float mitigationFrac    = 0.0f;  // [0.0 .. maxCapFrac] fraction of incoming hit absorbed
    float damageTakenMult   = 1.0f;  // 1.0 - mitigationFrac
    float staminaCostMult   = 1.0f;  // reduced on timed parry, elevated on heavy power attacks
};

BlockMitigationResult EvaluateBlockMitigation(const BlockMitigationInput& in);

// =================================================================================================
// Live Runtime State & C ABI Export (RAW_GetPhysicsCameraState)
// =================================================================================================
struct RawPhysicsCameraState {
    uint8_t gmstsVerified          = 0u;
    uint8_t gmstsAppliedCount      = 0u;
    uint8_t deadLegacySkipped      = 0u;
    uint8_t physicsCameraActive    = 1u;
    float   meleeDeathForceCap     = 48.0f;
    float   rangedDeathForceCap    = 22.0f;
    float   physicsDamageSpeedMin  = 650.0f;
    float   blockWeaponBase        = 0.32f;
    float   combatStaminaRegenMult = 0.82f;
    float   mountedMaxLookDownDeg  = 58.0f;
};

#ifndef RAW_SIM_PURE_TEST
std::string InitPhysicsCameraGmst(const Image& img, const std::vector<RefTarget>& refs);
void        ShutdownPhysicsCameraGmst();
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetPhysicsCameraState(raw::rt::RawPhysicsCameraState* out);
