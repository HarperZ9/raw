#pragma once
// rt_spellcraft_gmst.h: Milestone Wave 60 Binary-Verified 1.7.104.0 Dual-Cast, Spell Ballistics,
// Conjuration/Illusion/Ward AI, Explosion Clutter & Light-Sensor GMSTs (372 Total Unique Verified GMSTs!).
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

struct SpellcraftGmstSpec {
    const char* name;
    uint32_t    expectedRva;     // Setting object RVA in SkyrimSE.exe 1.7.104.0 (value at +0x08)
    bool        isInt32;         // true = i* int32 GMST, false = f* float GMST
    float       exeDefaultF;     // Exact on-disk float default (when !isInt32)
    float       balancedF;       // 2027 balanced float target (when !isInt32)
    int32_t     exeDefaultI;     // Exact on-disk int32 default (when isInt32)
    int32_t     balancedI;       // 2027 balanced int32 target (when isInt32)
    bool        hasLiveReader;   // Proven by overlap-safe .text RIP-relative scan (never writes 0-reader GMSTs!)
    const char* category;        // "DualCast" | "Spellcraft" | "Ward" | "Conjuration" | "Illusion" | "Alteration" | "Explosion" | "Light"
};

const SpellcraftGmstSpec* SpellcraftGmstTable(size_t* outCount = nullptr);

// Direct PE-image verification of every GMST RVA, NUL-terminated name pointer (+0x10), and on-disk default (+0x08).
// Fail-closed: returns false if ANY entry mismatches!
bool VerifySpellcraftGmstTable(const Image& img, uint32_t* outVerifiedCount = nullptr);

// =================================================================================================
// First-Principles Dual-Cast Magicka/Magnitude & Directional Ward Absorption Evaluators
// =================================================================================================
struct DualCastResult {
    float magickaCost      = 25.0f;
    float finalMagnitude   = 25.0f;
    float efficiencyRatio  = 1.0f;   // magnitude per magicka spent
};

DualCastResult EvaluateDualCastEfficiency(float baseSpellCost, float baseMagnitude,
                                          float schoolSkill0_100, bool isDualCast);

struct WardAbsorbResult {
    float absorbedDamage   = 0.0f;
    float bleedThroughDmg  = 0.0f;
    bool  wardBroken       = false;
    bool  insideCone       = true;
};

WardAbsorbResult EvaluateWardAbsorption(float wardMaxStrength, float incomingSpellDmg,
                                        float impactAngleDeg, float wardConeHalfDeg = 52.5f);

// =================================================================================================
// Live Runtime State & C ABI Export (RAW_GetSpellcraftState)
// =================================================================================================
struct RawSpellcraftState {
    uint8_t gmstsVerified            = 0u;
    uint8_t gmstsAppliedCount        = 0u;
    uint8_t deadLegacySkipped        = 0u;
    uint8_t spellcraftActive         = 1u;
    float   dualCastAiChance         = 0.48f;
    float   magicBallOptimalDistU    = 1250.0f;
    float   magicExplosionClutterMul = 4.0f;
    float   lightRecalcPlayerSec     = 0.35f;
    int32_t interiorLightMod         = 40;
    int32_t startSpellSkillLevel     = 30;
};

#ifndef RAW_SIM_PURE_TEST
std::string InitSpellcraftGmst(const Image& img, const std::vector<RefTarget>& refs);
void        ShutdownSpellcraftGmst();
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetSpellcraftState(raw::rt::RawSpellcraftState* out);
