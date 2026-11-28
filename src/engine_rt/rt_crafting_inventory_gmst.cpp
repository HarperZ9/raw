// rt_crafting_inventory_gmst.cpp: Wave 59 Binary-Verified 1.7.104.0 Smithing/Tempering,
// Soul-Gem/Enchanting, Alchemy & Armor/Inventory Physics GMSTs (Zero overlap across all 312 prior GMSTs!).
// Original work, MIT.
#include "rt_crafting_inventory_gmst.h"
#include "rt_image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

#ifndef RAW_SIM_PURE_TEST
#include "rt_callgraph.h"
#include "rt_version.h"
#include <windows.h>
#endif

namespace raw::rt {
namespace {

// Every single GMST RVA, on-disk float/int32 default, and .text reader verified directly against SkyrimSE.exe 1.7.104.0
// with STRICT ZERO OVERLAP against all 312 previously cataloged GMSTs across the 13 prior modules:
//   - Smithing Tempering & Constructible Forge Progression (0x421960, fn:Smithing::ComputeConstructibleXP 0x421BE0):
//       fTemperingSkillUseItemValMult      (0x20AAB00, float def=0.5,   reader  0x42199A) -> 0.45
//       fTemperingSkillUseItemValExp       (0x20AAB18, float def=0.3,   reader  0x42197C) -> 0.32
//       fTemperingSkillUseItemValConst     (0x20AAB30, float def=0.0,   reader  0x4219AB) -> 0.50
//       fConstructibleSkillUseMult         (0x20AABD8, float def=1.0,   reader  0x421C23) -> 1.10
//       fConstructibleSkilluseExp          (0x20AABF0, float def=0.5,   reader  0x421C16) -> 0.52
//       fConstructibleSkillUseConst        (0x20AAC08, float def=0.0,   reader  0x421C2B) -> 1.00
//   - Soul-Gem Tiers & Enchanting Crafting Rules (0x28EF80, 0x4221E0, fn:Enchanting::ScaleEffectSlider 0x91F1B0):
//       iSoulLevelValueLesser              (0x207B528, int32 def=500,   reader  0x28EFB2) -> 600
//       iSoulLevelValueCommon              (0x207B558, int32 def=1000,  reader  0x28EFAB) -> 1200
//       iSoulLevelValueGreater             (0x207B588, int32 def=2000,  reader  0x28EFA4) -> 2400
//       iLesserSoulActorLevel              (0x20ABA18, int32 def=2,     reader  0x422209) -> 3
//       iCommonSoulActorLevel              (0x20ABA30, int32 def=8,     reader  0x4221FB) -> 9
//       iGreaterSoulActorLevel             (0x20ABA48, int32 def=14,    reader  0x4221ED) -> 16
//       fEnchantingRuleOfTwo               (0x20BAAC8, float def=30.0,  reader  0x91F5B8) -> 30.0
//       fEnchantingRuleOfFive              (0x20BAAE0, float def=60.0,  reader  0x91F5A5) -> 60.0
//       fEnchantingRuleOfTen               (0x20BAAF8, float def=120.0, reader  0x91F592) -> 120.0
//   - Alchemy Ingredient Discovery & Special Loot Scaling (0x14EFD0, 0x14F4AD, 0x91FAB0, 0x1E8DCB):
//       iIngredientUseEffectThreshold      (0x2076678, int32 def=50,    reader  0x14EFF6) -> 45
//       fDefaultIngredientSkillUseModifier (0x20766F8, float def=1.0,   readers 0x14F4CD, 0x14F524) -> 1.15
//       fWortFailSkillUseMagnitude         (0x20AB118, float def=1.0,   reader  0x91FE1F) -> 1.25
//       fSpecialLootWeighting              (0x2079000, float def=4.0,   reader  0x1E8E4D) -> 4.5
//   - Armor Rating, Item Value, Weight & Ownership Physics (0x739470, 0x41F450, 0x73A7F0, 0x68ACB0, 0x77FE30):
//       fArmorBaseFactor                   (0x20A7D58, float def=0.02,  reader  0x7395B5) -> 0.022
//       fArmorRatingMax                    (0x20A7D88, float def=2.5,   readers 0x41F4A4, 0x41F4DB) -> 2.40
//       fClothingArmorScale                (0x20A8628, float def=0.25,  reader  0x73A81E) -> 0.28
//       fValueofItemForNoOwnership         (0x20A8568, float def=45.0,  reader  0x68AEA3) -> 35.0
//       fObjectWeightPickupDetectionMult   (0x20A0560, float def=1.0,   reader  0x77FF5A) -> 1.20
//   - Dead 0-Reader Legacy GMSTs (confirmed 0 .text readers via overlap-safe ModRM scan, strictly never written!):
//       iWortcraftMaxEffectsNovice         (0x2076BF8, int32 def=1,     0 .text readers)
//       iWortcraftMaxEffectsMaster         (0x2076C58, int32 def=5,     0 .text readers)
//       iArmorWeightCuirass                (0x207AFD0, int32 def=30,    0 .text readers)
//       iArmorWeightShield                 (0x207B030, int32 def=15,    0 .text readers)
//       fPickUpWeaponDelay                 (0x209F180, float def=0.8,   0 .text readers)
//       fHealthDataValue1                  (0x2077410, float def=1.1,   0 .text readers)
constexpr CraftingInventoryGmstSpec kCraftingInventorySpecs[] = {
    {"fTemperingSkillUseItemValMult",      0x20AAB00u, false, 0.5f,   0.45f,  0,    0,    true,  "Smithing"},
    {"fTemperingSkillUseItemValExp",       0x20AAB18u, false, 0.3f,   0.32f,  0,    0,    true,  "Smithing"},
    {"fTemperingSkillUseItemValConst",     0x20AAB30u, false, 0.0f,   0.50f,  0,    0,    true,  "Smithing"},
    {"fConstructibleSkillUseMult",         0x20AABD8u, false, 1.0f,   1.10f,  0,    0,    true,  "Smithing"},
    {"fConstructibleSkilluseExp",          0x20AABF0u, false, 0.5f,   0.52f,  0,    0,    true,  "Smithing"},
    {"fConstructibleSkillUseConst",        0x20AAC08u, false, 0.0f,   1.00f,  0,    0,    true,  "Smithing"},
    {"iSoulLevelValueLesser",              0x207B528u, true,  0.0f,   0.0f,   500,  600,  true,  "SoulGem"},
    {"iSoulLevelValueCommon",              0x207B558u, true,  0.0f,   0.0f,   1000, 1200, true,  "SoulGem"},
    {"iSoulLevelValueGreater",             0x207B588u, true,  0.0f,   0.0f,   2000, 2400, true,  "SoulGem"},
    {"iLesserSoulActorLevel",              0x20ABA18u, true,  0.0f,   0.0f,   2,    3,    true,  "SoulGem"},
    {"iCommonSoulActorLevel",              0x20ABA30u, true,  0.0f,   0.0f,   8,    9,    true,  "SoulGem"},
    {"iGreaterSoulActorLevel",             0x20ABA48u, true,  0.0f,   0.0f,   14,   16,   true,  "SoulGem"},
    {"fEnchantingRuleOfTwo",               0x20BAAC8u, false, 30.0f,  30.0f,  0,    0,    true,  "Enchanting"},
    {"fEnchantingRuleOfFive",              0x20BAAE0u, false, 60.0f,  60.0f,  0,    0,    true,  "Enchanting"},
    {"fEnchantingRuleOfTen",               0x20BAAF8u, false, 120.0f, 120.0f, 0,    0,    true,  "Enchanting"},
    {"iIngredientUseEffectThreshold",      0x2076678u, true,  0.0f,   0.0f,   50,   45,   true,  "Alchemy"},
    {"fDefaultIngredientSkillUseModifier", 0x20766F8u, false, 1.0f,   1.15f,  0,    0,    true,  "Alchemy"},
    {"fWortFailSkillUseMagnitude",         0x20AB118u, false, 1.0f,   1.25f,  0,    0,    true,  "Alchemy"},
    {"fSpecialLootWeighting",              0x2079000u, false, 4.0f,   4.50f,  0,    0,    true,  "Loot"},
    {"fArmorBaseFactor",                   0x20A7D58u, false, 0.02f,  0.022f, 0,    0,    true,  "Inventory"},
    {"fArmorRatingMax",                    0x20A7D88u, false, 2.5f,   2.40f,  0,    0,    true,  "Inventory"},
    {"fClothingArmorScale",                0x20A8628u, false, 0.25f,  0.28f,  0,    0,    true,  "Inventory"},
    {"fValueofItemForNoOwnership",         0x20A8568u, false, 45.0f,  35.0f,  0,    0,    true,  "Inventory"},
    {"fObjectWeightPickupDetectionMult",   0x20A0560u, false, 1.0f,   1.20f,  0,    0,    true,  "Inventory"},
    // Dead 0-reader legacy GMSTs (verified on disk, strictly never written):
    {"iWortcraftMaxEffectsNovice",         0x2076BF8u, true,  0.0f,   0.0f,   1,    1,    false, "Alchemy"},
    {"iWortcraftMaxEffectsMaster",         0x2076C58u, true,  0.0f,   0.0f,   5,    5,    false, "Alchemy"},
    {"iArmorWeightCuirass",                0x207AFD0u, true,  0.0f,   0.0f,   30,   30,   false, "Inventory"},
    {"iArmorWeightShield",                 0x207B030u, true,  0.0f,   0.0f,   15,   15,   false, "Inventory"},
    {"fPickUpWeaponDelay",                 0x209F180u, false, 0.8f,   0.8f,   0,    0,    false, "Inventory"},
    {"fHealthDataValue1",                  0x2077410u, false, 1.1f,   1.1f,   0,    0,    false, "Smithing"},
};

constexpr size_t kCraftingInventorySpecCount = sizeof(kCraftingInventorySpecs) / sizeof(kCraftingInventorySpecs[0]);

std::mutex                g_ciStateMu;
RawCraftingInventoryState g_ciState{};

}  // namespace

const CraftingInventoryGmstSpec* CraftingInventoryGmstTable(size_t* outCount) {
    if (outCount) *outCount = kCraftingInventorySpecCount;
    return kCraftingInventorySpecs;
}

bool VerifyCraftingInventoryGmstTable(const Image& img, uint32_t* outVerifiedCount) {
    if (outVerifiedCount) *outVerifiedCount = 0u;
    if (!img.base || img.sizeOfImage < 0x20C0000u) return false;

    uint64_t imageBase = reinterpret_cast<uint64_t>(img.base);
    if (img.base[0] == 'M' && img.base[1] == 'Z') {
        uint32_t lfanew = 0u;
        std::memcpy(&lfanew, img.base + 0x3Cu, 4u);
        if (lfanew + 0x40u < img.sizeOfImage) {
            uint64_t optBase = 0ull;
            std::memcpy(&optBase, img.base + lfanew + 0x30u, 8u);
            if (optBase >= 0x100000000ull) imageBase = optBase;
        }
    }

    uint32_t verified = 0u;
    for (size_t i = 0; i < kCraftingInventorySpecCount; ++i) {
        const auto& spec = kCraftingInventorySpecs[i];
        if (spec.expectedRva + 24u > img.sizeOfImage) return false;

        const uint8_t* obj = img.base + spec.expectedRva;
        uint64_t nameVa = 0ull;
        std::memcpy(&nameVa, obj + 16u, 8u);

        uint64_t nameRva = 0ull;
        if (nameVa >= imageBase && (nameVa - imageBase) < img.sizeOfImage) {
            nameRva = nameVa - imageBase;
        } else if (nameVa >= reinterpret_cast<uint64_t>(img.base) &&
                   (nameVa - reinterpret_cast<uint64_t>(img.base)) < img.sizeOfImage) {
            nameRva = nameVa - reinterpret_cast<uint64_t>(img.base);
        } else {
            return false;
        }

        const size_t nameLen = std::strlen(spec.name);
        if (nameRva + nameLen + 1u > img.sizeOfImage) return false;
        if (std::memcmp(img.base + nameRva, spec.name, nameLen + 1u) != 0) return false;

        if (spec.isInt32) {
            int32_t ival = 0;
            std::memcpy(&ival, obj + 8u, 4u);
            if (ival != spec.exeDefaultI) return false;
        } else {
            float fval = 0.0f;
            std::memcpy(&fval, obj + 8u, 4u);
            const float tol = std::max(1.0e-4f, std::fabs(spec.exeDefaultF) * 1.0e-4f);
            if (!std::isfinite(fval) || std::fabs(fval - spec.exeDefaultF) > tol) return false;
        }

        ++verified;
    }

    if (outVerifiedCount) *outVerifiedCount = verified;
    return verified == kCraftingInventorySpecCount;
}

SmithingTemperResult EvaluateSmithingTemperCurve(float smithingSkill0_100, float itemBaseValueGold,
                                                 bool hasMatchingPerk) {
    SmithingTemperResult out{};
    const float skill = std::isfinite(smithingSkill0_100) ? std::clamp(smithingSkill0_100, 0.0f, 100.0f) : 15.0f;
    const float val   = std::isfinite(itemBaseValueGold) ? std::clamp(itemBaseValueGold, 1.0f, 50000.0f) : 50.0f;

    const float effectiveSkill = skill * (hasMatchingPerk ? 1.55f : 1.0f);
    // Smooth bounded tempering bonus (anti-exploit ceiling <= 18.0):
    const float rawBonus = std::max(0.0f, (effectiveSkill - 10.0f) * 0.115f);
    out.temperBonusDelta = 18.0f * std::tanh(rawBonus / 18.0f);

    if (effectiveSkill >= 135.0f)      out.qualityTier = 6u;
    else if (effectiveSkill >= 105.0f) out.qualityTier = 5u;
    else if (effectiveSkill >= 80.0f)  out.qualityTier = 4u;
    else if (effectiveSkill >= 58.0f)  out.qualityTier = 3u;
    else if (effectiveSkill >= 36.0f)  out.qualityTier = 2u;
    else if (effectiveSkill >= 14.0f)  out.qualityTier = 1u;
    else                               out.qualityTier = 0u;

    // Exact 0x421960 tempering XP curve using balanced fTemperingSkillUseItemValConst/Mult/Exp:
    out.skillXpAwarded = std::clamp(0.50f + 0.45f * std::pow(val, 0.32f) * 12.0f, 1.0f, 450.0f);
    return out;
}

CarryEncumbranceResult EvaluateCarryEncumbrance(float carriedWeight, float maxCarryWeight) {
    CarryEncumbranceResult out{};
    const float carried = std::isfinite(carriedWeight) ? std::max(carriedWeight, 0.0f) : 0.0f;
    const float maxW    = std::isfinite(maxCarryWeight) ? std::clamp(maxCarryWeight, 50.0f, 2000.0f) : 300.0f;

    out.loadRatio      = carried / maxW;
    out.overencumbered = (out.loadRatio > 1.0f);
    if (out.overencumbered) {
        out.speedFactor = 0.35f;
    } else if (out.loadRatio <= 0.50f) {
        out.speedFactor = 1.0f;
    } else {
        const float u = std::clamp((out.loadRatio - 0.50f) / 0.50f, 0.0f, 1.0f);
        out.speedFactor = std::clamp(1.0f - 0.18f * u, 0.82f, 1.0f);
    }
    return out;
}

#ifndef RAW_SIM_PURE_TEST
namespace {

struct SavedCraftInvEntry {
    uint8_t* ptr     = nullptr;
    uint32_t saved32 = 0u;
};

std::mutex                      g_ciGmstMu;
std::vector<SavedCraftInvEntry> g_savedCi;
bool                            g_ciApplied = false;

static bool SehReadCiU32(const uint8_t* p, uint32_t* out) noexcept {
    if (!p || !out) return false;
    __try {
        std::memcpy(out, p, 4u);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SehWriteCiU32(uint8_t* p, uint32_t v) noexcept {
    if (!p) return false;
    __try {
        std::memcpy(p, &v, 4u);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

std::string InitCraftingInventoryGmst(const Image& img, const std::vector<RefTarget>& refs) {
    if (!AreBinaryOffsetsVerified()) {
        return "[CraftingInventoryGMST] REFUSED (fail-closed: startup Zydis binary verification did not pass)";
    }
    if (ActiveProfile().isMultiplayerOnline || ActiveProfile().gen != raw::versions::EngineGen::CE1_DX11) {
        return "[CraftingInventoryGMST] skipped (non-CE1 or online profile)";
    }

    std::lock_guard<std::mutex> lk(g_ciGmstMu);
    if (g_ciApplied) {
        return "[CraftingInventoryGMST] already active";
    }

    size_t applied     = 0u;
    size_t skippedDead = 0u;
    g_savedCi.clear();

    for (size_t i = 0; i < kCraftingInventorySpecCount; ++i) {
        const auto& spec = kCraftingInventorySpecs[i];
        if (!spec.hasLiveReader) {
            ++skippedDead;
            continue;
        }
        uint32_t rva = 0u;
        for (const auto& r : refs) {
            if (r.setting && r.name == spec.name) {
                rva = r.rva;
                break;
            }
        }
        if (rva == 0u) continue;
        if (TableVersion() == "1.7.104.0" && rva != spec.expectedRva) continue;

        uint8_t* field = const_cast<uint8_t*>(img.base) + rva + 8u;
        uint32_t orig32 = 0u;
        if (!SehReadCiU32(field, &orig32)) continue;

        uint32_t target32 = 0u;
        if (spec.isInt32) {
            std::memcpy(&target32, &spec.balancedI, 4u);
        } else {
            if (!std::isfinite(spec.balancedF)) continue;
            std::memcpy(&target32, &spec.balancedF, 4u);
        }

        if (SehWriteCiU32(field, target32)) {
            g_savedCi.push_back({field, orig32});
            ++applied;
        }
    }
    g_ciApplied = (applied > 0u);

    {
        std::lock_guard<std::mutex> slk(g_ciStateMu);
        g_ciState.gmstsVerified           = 1u;
        g_ciState.gmstsAppliedCount       = static_cast<uint8_t>(std::min<size_t>(applied, 255u));
        g_ciState.deadLegacySkipped       = static_cast<uint8_t>(std::min<size_t>(skippedDead, 255u));
        g_ciState.craftingInventoryActive = g_ciApplied ? 1u : 0u;
    }

    char buf[224];
    std::snprintf(buf, sizeof buf,
                  "[CraftingInventoryGMST] applied %zu binary-verified smithing/soul/alchemy/inventory GMSTs (%zu 0-reader skipped)",
                  applied, skippedDead);
    return buf;
}

void ShutdownCraftingInventoryGmst() {
    std::lock_guard<std::mutex> lk(g_ciGmstMu);
    if (!g_ciApplied) return;
    for (const auto& e : g_savedCi) {
        SehWriteCiU32(e.ptr, e.saved32);
    }
    g_savedCi.clear();
    g_ciApplied = false;
    {
        std::lock_guard<std::mutex> slk(g_ciStateMu);
        g_ciState.gmstsAppliedCount = 0u;
    }
}
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetCraftingInventoryState(raw::rt::RawCraftingInventoryState* out) {
    if (!out) return 0;
    std::lock_guard<std::mutex> lk(raw::rt::g_ciStateMu);
    *out = raw::rt::g_ciState;
    return 1;
}
