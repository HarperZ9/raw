#pragma once
// rt_crafting_inventory_gmst.h: Wave 59 Binary-Verified 1.7.104.0 Smithing/Tempering,
// Soul-Gem/Enchanting, Alchemy & Armor/Inventory Physics GMSTs (342 Total Unique Verified GMSTs!).
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

struct CraftingInventoryGmstSpec {
    const char* name;
    uint32_t    expectedRva;     // Setting object RVA in SkyrimSE.exe 1.7.104.0 (value at +0x08)
    bool        isInt32;         // true = i* int32 GMST, false = f* float GMST
    float       exeDefaultF;     // Exact on-disk float default (when !isInt32)
    float       balancedF;       // 2027 balanced float target (when !isInt32)
    int32_t     exeDefaultI;     // Exact on-disk int32 default (when isInt32)
    int32_t     balancedI;       // 2027 balanced int32 target (when isInt32)
    bool        hasLiveReader;   // Proven by overlap-safe .text RIP-relative scan (never writes 0-reader GMSTs!)
    const char* category;        // "Smithing" | "SoulGem" | "Enchanting" | "Alchemy" | "Loot" | "Inventory"
};

const CraftingInventoryGmstSpec* CraftingInventoryGmstTable(size_t* outCount = nullptr);

// Direct PE-image verification of every GMST RVA, NUL-terminated name pointer (+0x10), and on-disk default (+0x08).
// Fail-closed: returns false if ANY entry mismatches!
bool VerifyCraftingInventoryGmstTable(const Image& img, uint32_t* outVerifiedCount = nullptr);

// =================================================================================================
// First-Principles Smithing Tempering Curve & Carry-Weight Encumbrance Evaluators
// =================================================================================================
struct SmithingTemperResult {
    float temperBonusDelta = 0.0f;  // flat damage/armor improvement bounded cleanly [0 .. 18]
    float skillXpAwarded   = 1.0f;  // exact 0x421960 value-exponent tempering XP curve
    uint8_t qualityTier    = 0u;    // 0=Base, 1=Fine, 2=Superior, 3=Exquisite, 4=Flawless, 5=Epic, 6=Legendary
};

SmithingTemperResult EvaluateSmithingTemperCurve(float smithingSkill0_100, float itemBaseValueGold,
                                                 bool hasMatchingPerk);

struct CarryEncumbranceResult {
    float loadRatio       = 0.0f;  // carried / maxCarry
    float speedFactor     = 1.0f;  // [0.35 .. 1.0]
    bool  overencumbered  = false;
};

CarryEncumbranceResult EvaluateCarryEncumbrance(float carriedWeight, float maxCarryWeight);

// =================================================================================================
// Live Runtime State & C ABI Export (RAW_GetCraftingInventoryState)
// =================================================================================================
struct RawCraftingInventoryState {
    uint8_t gmstsVerified            = 0u;
    uint8_t gmstsAppliedCount        = 0u;
    uint8_t deadLegacySkipped        = 0u;
    uint8_t craftingInventoryActive  = 1u;
    float   temperValMult            = 0.45f;
    float   constructibleXpMult      = 1.10f;
    int32_t commonSoulValue          = 1200;
    int32_t greaterSoulValue         = 2400;
    float   armorBaseFactor          = 0.022f;
    float   noOwnershipValueGold     = 35.0f;
};

#ifndef RAW_SIM_PURE_TEST
std::string InitCraftingInventoryGmst(const Image& img, const std::vector<RefTarget>& refs);
void        ShutdownCraftingInventoryGmst();
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetCraftingInventoryState(raw::rt::RawCraftingInventoryState* out);
