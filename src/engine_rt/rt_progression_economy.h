#pragma once
// rt_progression_economy.h: Wave 48 Binary-Verified 1.7.104.0 Economy Spread, Crafting Anti-Exploit
// & Movement/Fall/Progression GMSTs (Zero SKSE).
// 100% binary-verified RVAs & on-disk defaults, fail-closed, SEH-guarded, cleanly reversible.
// Original work, MIT.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace raw::rt {
struct Image;
struct RefTarget;

struct ProgressionEconomyGmstSpec {
    const char* name;
    uint32_t    expectedRva;     // Setting object RVA in SkyrimSE.exe 1.7.104.0 (float value at +0x08)
    float       exeDefault;      // Exact on-disk default float verified in SkyrimSE.exe 1.7.104.0 .data
    float       balancedValue;   // 2027 balanced target
    bool        hasLiveReader;   // Proven by .text RIP-relative disassembly scan (never writes dead 0-reader GMSTs!)
    const char* category;        // "Economy" | "Crafting" | "Movement" | "Progression"
};

const ProgressionEconomyGmstSpec* ProgressionEconomyTable(size_t* outCount = nullptr);

// Direct PE-image verification of every GMST RVA, NUL-terminated name pointer (+0x10), and on-disk default (+0x08).
// Fail-closed: returns false if ANY entry mismatches!
bool VerifyProgressionEconomyGmstTable(const Image& img, uint32_t* outVerifiedCount = nullptr);

// =================================================================================================
// Anti-Exploit Crafting Asymptotic Cap, Grounded Barter Spread & Physical Fall Damage Curves
// =================================================================================================
inline constexpr float kCraftingHardAsymptote = 2.25f;

struct CraftingCapResult {
    float rawUnboundedMult = 1.0f;
    float effectiveMult    = 1.0f;  // strictly bounded in [1.0 .. 2.25) -> kills infinite Alchemy/Enchanting loops!
    float diminishingFrac  = 0.0f;  // 0..1 degree of asymptotic damping applied
};
CraftingCapResult EvaluateCraftingCaps(float skillLevel0_100, float fortifyBonusFrac,
                                       float skillWeight = 0.85f, float maxCap = kCraftingHardAsymptote);

struct BarterSpreadResult {
    float buyFactor  = 2.0f;  // merchant buy multiplier (> 1.0)
    float sellFactor = 0.5f;  // merchant sell multiplier (< 1.0)
    float spreadRatio = 4.0f; // buyFactor / sellFactor
};
BarterSpreadResult EvaluateBarterSpread(float speechSkill0_100, float barterMin, float barterMax,
                                        float barterBuyMin, float barterSellMax);

// Exact SkyrimSE 1.7.104.0 fall damage formula (0x6BD57B..0x6BD5C1):
//   if (fallHeightU <= minH) return 0; else return pow((fallHeightU - minH) * mult, exponent);
float EvaluateFallDamage(float fallHeightU, float minHeightU, float heightMult, float exponent);

// =================================================================================================
// Live Runtime State & C ABI Export (RAW_GetProgressionEconomyState)
// =================================================================================================
struct RawProgressionEconomyState {
    uint8_t gmstsVerified         = 0u;
    uint8_t gmstsAppliedCount     = 0u;
    uint8_t deadLegacySkipped     = 0u;
    uint8_t antiExploitActive     = 1u;
    float   craftingAsymptoteCap  = kCraftingHardAsymptote;
    float   barterMinActive       = 1.35f;
    float   barterMaxActive       = 3.40f;
    float   fallMinHeightU        = 420.0f;
    float   fallHeightMult        = 0.085f;
    float   fallExponent          = 1.55f;
};

#ifndef RAW_SIM_PURE_TEST
std::string InitProgressionEconomy(const Image& img, const std::vector<RefTarget>& refs);
void        ShutdownProgressionEconomy();
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetProgressionEconomyState(raw::rt::RawProgressionEconomyState* out);
