#pragma once
// rt_alchemy_enchant_gmst.h: Wave 54 Binary-Verified 1.7.104.0 Enchantment, Skill Training,
// Soul-Gem, Alchemy & Leveled-Encounter GMSTs (Zero SKSE, Zero overlap with prior GMST modules).
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

struct AlchemyEnchantGmstSpec {
    const char* name;
    uint32_t    expectedRva;     // Setting object RVA in SkyrimSE.exe 1.7.104.0 (value at +0x08)
    bool        isInt32;         // true = i* int32 GMST, false = f* float GMST
    float       exeDefaultF;     // Exact on-disk float default (when !isInt32)
    float       balancedF;       // 2027 balanced float target (when !isInt32)
    int32_t     exeDefaultI;     // Exact on-disk int32 default (when isInt32)
    int32_t     balancedI;       // 2027 balanced int32 target (when isInt32)
    bool        hasLiveReader;   // Proven by overlap-safe .text RIP-relative scan (never writes 0-reader GMSTs!)
    const char* category;        // "Enchanting" | "SoulGem" | "Training" | "Alchemy" | "Tempering" | "Leveled"
};

const AlchemyEnchantGmstSpec* AlchemyEnchantGmstTable(size_t* outCount = nullptr);

// Direct PE-image verification of every GMST RVA, NUL-terminated name pointer (+0x10), and on-disk default (+0x08).
// Fail-closed: returns false if ANY entry mismatches!
bool VerifyAlchemyEnchantGmstTable(const Image& img, uint32_t* outVerifiedCount = nullptr);

// =================================================================================================
// First-Principles Enchantment Charge Economy & Leveled-Encounter Scaling Evaluators
// =================================================================================================
enum class SoulGemTier : uint8_t {
    Petty   = 0u,
    Lesser  = 1u,
    Common  = 2u,
    Greater = 3u,
    Grand   = 4u,
};

struct EnchantChargeResult {
    float soulChargeYield   = 300.0f;  // total recharge energy yielded by the soul gem
    float costPerStrike     = 12.0f;   // weapon charge drained per strike (using balanced drain=0.18)
    float strikesPerCharge  = 25.0f;   // strikes before recharge needed (smooth, no 10-swing spam!)
};

EnchantChargeResult EvaluateEnchantmentChargeEconomy(SoulGemTier tier, float effectBaseCost, float skillLevel0_100);

enum class EncounterTier : uint8_t {
    Easy     = 0u,
    Medium   = 1u,
    Hard     = 2u,
    VeryHard = 3u,
};

struct EncounterTierResult {
    float tierMultiplier = 0.5f;
    float targetLevel    = 10.0f;
    float minBandLevel   = 8.0f;
    float maxBandLevel   = 12.0f;
};

EncounterTierResult EvaluateEncounterTierLevel(float playerLevel, EncounterTier tier);

// =================================================================================================
// Live Runtime State & C ABI Export (RAW_GetAlchemyEnchantState)
// =================================================================================================
struct RawAlchemyEnchantState {
    uint8_t gmstsVerified           = 0u;
    uint8_t gmstsAppliedCount       = 0u;
    uint8_t deadLegacySkipped       = 0u;
    uint8_t alchemyEnchantActive    = 1u;
    float   enchantChargeMult       = 1.25f;
    float   enchantDrainMult        = 0.18f;
    float   trainingBaseCost        = 60.0f;
    float   trainingMultCost        = 2.25f;
    int32_t grandSoulValue          = 3600;
    int32_t trainingAllowedPerLevel = 5;
};

#ifndef RAW_SIM_PURE_TEST
std::string InitAlchemyEnchantGmst(const Image& img, const std::vector<RefTarget>& refs);
void        ShutdownAlchemyEnchantGmst();
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetAlchemyEnchantState(raw::rt::RawAlchemyEnchantState* out);
