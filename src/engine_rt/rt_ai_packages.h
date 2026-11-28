#pragma once
// rt_ai_packages.h: Wave 49 Binary-Verified 1.7.104.0 Flanking, Ranged, Companion & Stealth-Detection AI GMSTs
// (Zero SKSE).
// 100% binary-verified RVAs, .rdata name pointers & on-disk defaults; fail-closed, SEH-guarded, cleanly reversible.
// Original work, MIT.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace raw::rt {
struct Image;
struct RefTarget;

struct AiPackageGmstSpec {
    const char* name;
    uint32_t    expectedRva;     // Setting object RVA in SkyrimSE.exe 1.7.104.0 (float value at +0x08)
    float       exeDefault;      // Exact on-disk default float verified in SkyrimSE.exe 1.7.104.0 .data
    float       balancedValue;   // 2027 tactical AI balanced target
    bool        hasLiveReader;   // Proven by .text RIP-relative disassembly scan (never writes dead 0-reader GMSTs!)
    const char* category;        // "Flanking" | "Ranged" | "Search" | "Detection" | "Companion"
};

const AiPackageGmstSpec* AiPackagesTable(size_t* outCount = nullptr);

// Direct PE-image verification of every GMST RVA, NUL-terminated name pointer (+0x10), and on-disk default (+0x08).
// Fail-closed: returns false if ANY entry mismatches!
bool VerifyAiPackagesGmstTable(const Image& img, uint32_t* outVerifiedCount = nullptr);

// =================================================================================================
// Weather / Darkness / Visibility-Aware Tactical AI Evaluation (pure math, deterministic, bounded)
// =================================================================================================
struct TacticalAiEnv {
    float rain01          = 0.0f;   // 0..1 precipitation intensity
    float snow01          = 0.0f;   // 0..1 snowfall / blizzard intensity
    float fog01           = 0.0f;   // 0..1 volumetric fog density
    float nightDarkness01 = 0.0f;   // 0 = bright noon, 1 = moonless midnight
    float windBeaufort    = 2.0f;   // 0..12 Beaufort wind scale
    bool  inInterior      = false;  // sheltered from rain/snow/wind
};

struct TacticalAiProfile {
    float effectiveViewConeDeg    = 168.0f;  // [104 .. 168] degrees
    float effectiveSightRadiusU   = 1920.0f; // [640 .. 1920] Skyrim units
    float rangedAimDispersionMult = 1.0f;    // [1.0 .. 2.25] crosswind + visibility aim dispersion multiplier
    float flankingAggressionMult  = 1.0f;    // [0.80 .. 1.35] squad flanking aggression scalar
    float searchPersistenceMult   = 1.0f;    // [0.75 .. 1.40] search sweep persistence scalar
    float visibilityFactor01      = 1.0f;    // [0.25 .. 1.0] composite optical visibility
};

TacticalAiProfile EvaluateTacticalAiProfile(const TacticalAiEnv& env);

// =================================================================================================
// Live Runtime State & C ABI Export (RAW_GetAiPackagesState)
// =================================================================================================
struct RawAiPackagesState {
    uint8_t gmstsVerified            = 0u;
    uint8_t gmstsAppliedCount        = 0u;
    uint8_t deadLegacySkipped        = 0u;
    uint8_t tacticalAiActive         = 1u;
    float   effectiveViewConeDeg     = 168.0f;
    float   effectiveSightRadiusU    = 1920.0f;
    float   rangedAimDispersionMult  = 1.0f;
    float   followerDoorSpacingU     = 280.0f;
    float   rangedMinRangeU          = 340.0f;
    float   flankingAngleOffsetMax   = 42.0f;
};

void AiPackagesUpdateEnvironment(const TacticalAiEnv& env);

#ifndef RAW_SIM_PURE_TEST
std::string InitAiPackages(const Image& img, const std::vector<RefTarget>& refs);
void        ShutdownAiPackages();
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetAiPackagesState(raw::rt::RawAiPackagesState* out);
