#pragma once
// rt_magic_physics.h: Wave 47 Binary-Verified 1.7.104.0 Elemental Magic Physics, Dual-Cast Scaling
// & Weather/Water Synergy + Aerodynamic Spell Ballistics (Zero SKSE).
// 100% binary-verified GMST RVAs & on-disk defaults, fail-closed, SEH-guarded, cleanly reversible.
// Original work, MIT.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace raw::rt {
struct Image;
struct RefTarget;

struct MagicBalanceGmstSpec {
    const char* name;
    uint32_t    expectedRva;     // Setting object RVA in SkyrimSE.exe 1.7.104.0 (float value at +0x08)
    float       exeDefault;      // Exact on-disk default float verified in SkyrimSE.exe 1.7.104.0 .data
    float       balancedValue;   // 2027 balanced target
    bool        hasLiveReader;   // Proven by .text RIP-relative disassembly scan (never writes dead 0-reader GMSTs!)
    const char* category;        // "DualCast" | "Range" | "Regen" | "Physics"
};

const MagicBalanceGmstSpec* MagicBalanceTable(size_t* outCount = nullptr);

// Direct PE-image verification of every magic GMST RVA, NUL-terminated name pointer (+0x10), and on-disk default (+0x08).
// Fail-closed: returns false if ANY entry mismatches!
bool VerifyMagicBalanceGmstTable(const Image& img, uint32_t* outVerifiedCount = nullptr);

// =================================================================================================
// Elemental Weather & Water Synergy Solver
// =================================================================================================
struct MagicEnvInput {
    float surfaceTempC    = 10.0f;  // air/surface temp in C (sub-zero < 0)
    float rainIntensity01 = 0.0f;   // 0..1 active rainfall
    float snowIntensity01 = 0.0f;   // 0..1 active snowfall/blizzard
    float wetness01       = 0.0f;   // 0..1 target/environment wetness
    bool  submerged       = false;  // standing/swimming in water
    float thunderstorm01  = 0.0f;   // 0..1 electrical storm activity
    float windBeaufort    = 2.0f;   // 0..12 physical Beaufort scale
};

struct ElementalSynergyState {
    float   fireDamageMult      = 1.0f;  // [0.78 .. 1.18] dry/arid boost vs rain/submerged damp
    float   fireRadiusMult      = 1.0f;  // [0.85 .. 1.12]
    float   snowMeltImpulse     = 0.0f;  // [0.00 .. 1.00] thermal snowpack melt burst
    float   frostDamageMult     = 1.0f;  // [1.00 .. 1.45] wet flash-freeze brittle amp (+35% wet, +10% blizzard)
    float   frostStaggerMult    = 1.0f;  // [1.00 .. 1.25] wet brittle stagger amp
    float   frostReachMult      = 1.0f;  // [1.00 .. 1.12] sub-zero blizzard reach boost
    float   shockDamageMult     = 1.0f;  // [1.00 .. 1.55] wet water-arc (+40%) + thunderstorm (+15%)
    float   shockArcRangeMult   = 1.0f;  // [1.00 .. 1.45] conductive water-arc (+35%) + storm boost
    uint8_t steamBurstFlag      = 0u;    // 1 when fire meets rain/water
    uint8_t waterArcConductFlag = 0u;    // 1 when wet/submerged amplifies shock arc
};

ElementalSynergyState EvaluateElementalSynergy(const MagicEnvInput& in);

// =================================================================================================
// Aerodynamic Spell Projectile Ballistics & Crosswind Drift
// =================================================================================================
enum class SpellProjectileKind : uint8_t {
    Firebolt      = 0,
    IceSpike      = 1,
    LightningBolt = 2,
};

struct SpellBallisticsResult {
    float speedUnitsPerSec = 3600.0f;
    float gravityDropU     = 0.0f;     // downward vertical drop after flightTimeSec (>= 0; 0 for LightningBolt)
    float windDriftXY[2]   = {};       // crosswind lateral deflection in world units (0 for LightningBolt)
    float displacement[3]  = {};       // 3D world displacement from launch origin after flightTimeSec
};

SpellBallisticsResult EvaluateSpellBallistics(SpellProjectileKind kind,
                                              float flightTimeSec,
                                              const float launchDir[3],
                                              const float windVecXY[2]);

// =================================================================================================
// Live Runtime State & C ABI Export (RAW_GetMagicPhysicsState)
// =================================================================================================
struct RawMagicPhysicsState {
    uint8_t               gmstsVerified     = 0u;
    uint8_t               gmstsAppliedCount = 0u;
    uint8_t               steamBurstFlag    = 0u;
    uint8_t               waterArcFlag      = 0u;
    ElementalSynergyState synergy{};
};

void MagicPhysicsUpdateEnvironment(const MagicEnvInput& env);

#ifndef RAW_SIM_PURE_TEST
std::string InitMagicPhysics(const Image& img, const std::vector<RefTarget>& refs);
void        ShutdownMagicPhysics();
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetMagicPhysicsState(raw::rt::RawMagicPhysicsState* out);
