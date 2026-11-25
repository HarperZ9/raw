// rt_alchemy_enchant_gmst.cpp: Wave 54 Binary-Verified 1.7.104.0 Enchantment, Skill Training,
// Soul-Gem, Alchemy & Leveled-Encounter GMSTs (Zero overlap with prior GMST modules).
// Original work, MIT.
#include "rt_alchemy_enchant_gmst.h"
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
// with ZERO overlap against rt_encounter_balance, rt_magic_physics, rt_progression_economy, rt_ai_packages,
// rt_world_rules_gmst, rt_physics_camera_gmst, rt_stealth_social_gmst, and rt_locomotion_melee_gmst:
//   - Enchanting Cost, Charge & Soul-Gem Yield (0x420C40, 0x4218B0, 0x421E60, 0x422020, 0x28EF80, 0x921DE0):
//       fEnchantmentPointsMult        (0x20A9348, float def=0.4,   reader 0x420C4E) -> 0.45
//       fEnchantmentEffectPointsMult  (0x20A9360, float def=0.4,   reader 0x420C5A) -> 0.45
//       fEnchantingSkillCostBase      (0x20AAA58, float def=0.005, reader 0x4218C4) -> 0.005
//       fEnchantingSkillCostMult      (0x20AAA70, float def=3.0,   reader 0x4218F9) -> 2.40
//       fEnchantingCostExponent       (0x20AAAA0, float def=1.1,   reader 0x4218E5) -> 1.08
//       fMagicEnchantmentChargeMult   (0x20AAD58, float def=1.0,   reader 0x421E63) -> 1.25
//       fMagicEnchantmentDrainMult    (0x20AAD88, float def=0.25,  reader 0x422023) -> 0.18
//       fDisenchantSkillUse           (0x20A9378, float def=6.0,   reader 0x921E6B) -> 8.0
//       iSoulLevelValuePetty          (0x207B4F8, int32 def=250,   reader 0x28EFB9) -> 300
//       iSoulLevelValueGrand          (0x207B5B8, int32 def=3000,  reader 0x28EF9A) -> 3600
//   - Skill Training Rules & Cost Curves (fn:Training::ComputeSessionCost 0x421C40, 0x985A90):
//       iTrainingNumAllowedPerLevel   (0x20A8730, int32 def=5,     readers 0x985AD5, 0x985E47) -> 5
//       iTrainingJourneymanSkill      (0x20A8748, int32 def=50,    readers 0x421C41, 0x985D99) -> 50
//       iTrainingExpertSkill          (0x20A8760, int32 def=75,    readers 0x421C56, 0x985DB1) -> 75
//       iTrainingMasterSkill          (0x20A8778, int32 def=90,    reader  0x421C66)           -> 90
//       fTrainingBaseCost             (0x20AA350, float def=50.0,  reader  0x421C93)           -> 60.0
//       fTrainingMultCost             (0x20AA368, float def=2.0,   reader  0x421C87)           -> 2.25
//   - Alchemy, Tempering & Leveled World Scaling (0x421920, 0x421960, 0x180C10, 0x4221E0):
//       fAlchemyIngredientInitMult    (0x20AAAB8, float def=4.0,   reader 0x421947) -> 3.5
//       fTemperingSkillUseMult        (0x20AAAE8, float def=15.0,  reader 0x4219B3) -> 12.0
//       fLeveledLockMult              (0x20772D8, float def=1.5,   readers 0x180C4D, 0x180D0A) -> 1.35
//       iGrandSoulActorLevel          (0x20ABA60, int32 def=19,    reader 0x4221E5) -> 22
//   - 0-Direct-Reader Legacy GMSTs (verified on disk via overlap-safe scan, strictly never written!):
//       fLeveledActorMultEasy         (0x20775C0, float def=0.25,  0 direct .text value readers)
//       fLeveledActorMultMedium       (0x20775D8, float def=0.5,   0 direct .text value readers)
//       fLeveledActorMultHard         (0x20775F0, float def=0.75,  0 direct .text value readers)
//       fLeveledActorMultVeryHard     (0x2077608, float def=1.0,   0 direct .text value readers)
//       fEnchantmentGoldMult          (0x20AAC38, float def=1.0,   0 direct .text value readers)
//       iAllowAlchemyDuringCombat     (0x20AF678, int32 def=0,     0 direct .text value readers)
constexpr AlchemyEnchantGmstSpec kAlchemyEnchantSpecs[] = {
    {"fEnchantmentPointsMult",       0x20A9348u, false, 0.4f,   0.45f,  0,    0,    true,  "Enchanting"},
    {"fEnchantmentEffectPointsMult", 0x20A9360u, false, 0.4f,   0.45f,  0,    0,    true,  "Enchanting"},
    {"fEnchantingSkillCostBase",     0x20AAA58u, false, 0.005f, 0.005f, 0,    0,    true,  "Enchanting"},
    {"fEnchantingSkillCostMult",     0x20AAA70u, false, 3.0f,   2.40f,  0,    0,    true,  "Enchanting"},
    {"fEnchantingCostExponent",      0x20AAAA0u, false, 1.1f,   1.08f,  0,    0,    true,  "Enchanting"},
    {"fMagicEnchantmentChargeMult",  0x20AAD58u, false, 1.0f,   1.25f,  0,    0,    true,  "Enchanting"},
    {"fMagicEnchantmentDrainMult",   0x20AAD88u, false, 0.25f,  0.18f,  0,    0,    true,  "Enchanting"},
    {"fDisenchantSkillUse",          0x20A9378u, false, 6.0f,   8.0f,   0,    0,    true,  "Enchanting"},
    {"iSoulLevelValuePetty",         0x207B4F8u, true,  0.0f,   0.0f,   250,  300,  true,  "SoulGem"},
    {"iSoulLevelValueGrand",         0x207B5B8u, true,  0.0f,   0.0f,   3000, 3600, true,  "SoulGem"},
    {"iTrainingNumAllowedPerLevel",  0x20A8730u, true,  0.0f,   0.0f,   5,    5,    true,  "Training"},
    {"iTrainingJourneymanSkill",     0x20A8748u, true,  0.0f,   0.0f,   50,   50,   true,  "Training"},
    {"iTrainingExpertSkill",         0x20A8760u, true,  0.0f,   0.0f,   75,   75,   true,  "Training"},
    {"iTrainingMasterSkill",         0x20A8778u, true,  0.0f,   0.0f,   90,   90,   true,  "Training"},
    {"fTrainingBaseCost",            0x20AA350u, false, 50.0f,  60.0f,  0,    0,    true,  "Training"},
    {"fTrainingMultCost",            0x20AA368u, false, 2.0f,   2.25f,  0,    0,    true,  "Training"},
    {"fAlchemyIngredientInitMult",   0x20AAAB8u, false, 4.0f,   3.5f,   0,    0,    true,  "Alchemy"},
    {"fTemperingSkillUseMult",       0x20AAAE8u, false, 15.0f,  12.0f,  0,    0,    true,  "Tempering"},
    {"fLeveledLockMult",             0x20772D8u, false, 1.5f,   1.35f,  0,    0,    true,  "Leveled"},
    {"iGrandSoulActorLevel",         0x20ABA60u, true,  0.0f,   0.0f,   19,   22,   true,  "SoulGem"},
    // 0-direct-reader legacy GMSTs (verified on disk, strictly never written):
    {"fLeveledActorMultEasy",        0x20775C0u, false, 0.25f,  0.25f,  0,    0,    false, "Leveled"},
    {"fLeveledActorMultMedium",      0x20775D8u, false, 0.50f,  0.50f,  0,    0,    false, "Leveled"},
    {"fLeveledActorMultHard",        0x20775F0u, false, 0.75f,  0.75f,  0,    0,    false, "Leveled"},
    {"fLeveledActorMultVeryHard",    0x2077608u, false, 1.00f,  1.00f,  0,    0,    false, "Leveled"},
    {"fEnchantmentGoldMult",         0x20AAC38u, false, 1.00f,  1.00f,  0,    0,    false, "Enchanting"},
    {"iAllowAlchemyDuringCombat",    0x20AF678u, true,  0.0f,   0.0f,   0,    0,    false, "Alchemy"},
};

constexpr size_t kAlchemyEnchantSpecCount = sizeof(kAlchemyEnchantSpecs) / sizeof(kAlchemyEnchantSpecs[0]);

std::mutex             g_aeStateMu;
RawAlchemyEnchantState g_aeState{};

}  // namespace

const AlchemyEnchantGmstSpec* AlchemyEnchantGmstTable(size_t* outCount) {
    if (outCount) *outCount = kAlchemyEnchantSpecCount;
    return kAlchemyEnchantSpecs;
}

bool VerifyAlchemyEnchantGmstTable(const Image& img, uint32_t* outVerifiedCount) {
    if (outVerifiedCount) *outVerifiedCount = 0u;
    if (!img.base || img.sizeOfImage < 0x20B0000u) return false;

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
    for (size_t i = 0; i < kAlchemyEnchantSpecCount; ++i) {
        const auto& spec = kAlchemyEnchantSpecs[i];
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
    return verified == kAlchemyEnchantSpecCount;
}

EnchantChargeResult EvaluateEnchantmentChargeEconomy(SoulGemTier tier, float effectBaseCost, float skillLevel0_100) {
    EnchantChargeResult out{};
    const float baseCost = std::isfinite(effectBaseCost) ? std::clamp(effectBaseCost, 5.0f, 500.0f) : 40.0f;
    const float skill01  = std::isfinite(skillLevel0_100) ? std::clamp(skillLevel0_100, 0.0f, 100.0f) / 100.0f : 0.25f;

    float soulEnergy = 300.0f;
    switch (tier) {
        case SoulGemTier::Petty:   soulEnergy = 300.0f;  break;
        case SoulGemTier::Lesser:  soulEnergy = 600.0f;  break;
        case SoulGemTier::Common:  soulEnergy = 1200.0f; break;
        case SoulGemTier::Greater: soulEnergy = 2400.0f; break;
        case SoulGemTier::Grand:   soulEnergy = 3600.0f; break;
    }

    // Balanced recharge capacity (fMagicEnchantmentChargeMult=1.25) & strike drain (fMagicEnchantmentDrainMult=0.18):
    out.soulChargeYield  = soulEnergy * 1.25f;
    const float skillRed = 1.0f - 0.42f * skill01;
    out.costPerStrike    = std::max(2.0f, baseCost * 0.18f * skillRed);
    out.strikesPerCharge = std::clamp(out.soulChargeYield / out.costPerStrike, 4.0f, 1500.0f);
    return out;
}

EncounterTierResult EvaluateEncounterTierLevel(float playerLevel, EncounterTier tier) {
    EncounterTierResult out{};
    const float pLvl = std::isfinite(playerLevel) ? std::clamp(playerLevel, 1.0f, 120.0f) : 10.0f;

    float mult = 0.65f;
    switch (tier) {
        case EncounterTier::Easy:     mult = 0.35f; break;
        case EncounterTier::Medium:   mult = 0.65f; break;
        case EncounterTier::Hard:     mult = 0.95f; break;
        case EncounterTier::VeryHard: mult = 1.25f; break;
    }
    out.tierMultiplier = mult;
    out.targetLevel    = std::max(1.0f, pLvl * mult);
    out.minBandLevel   = std::max(1.0f, out.targetLevel * 0.85f);
    out.maxBandLevel   = std::max(out.minBandLevel, out.targetLevel * 1.15f);
    return out;
}

#ifndef RAW_SIM_PURE_TEST
namespace {

struct SavedAlchEnchEntry {
    uint8_t* ptr     = nullptr;
    uint32_t saved32 = 0u;
};

std::mutex                      g_aeGmstMu;
std::vector<SavedAlchEnchEntry> g_savedAe;
bool                            g_aeApplied = false;

static bool SehReadAeU32(const uint8_t* p, uint32_t* out) noexcept {
    if (!p || !out) return false;
    __try {
        std::memcpy(out, p, 4u);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SehWriteAeU32(uint8_t* p, uint32_t v) noexcept {
    if (!p) return false;
    __try {
        std::memcpy(p, &v, 4u);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

std::string InitAlchemyEnchantGmst(const Image& img, const std::vector<RefTarget>& refs) {
    if (!AreBinaryOffsetsVerified()) {
        return "[AlchemyEnchantGMST] REFUSED (fail-closed: startup Zydis binary verification did not pass)";
    }
    if (ActiveProfile().isMultiplayerOnline || ActiveProfile().gen != raw::versions::EngineGen::CE1_DX11) {
        return "[AlchemyEnchantGMST] skipped (non-CE1 or online profile)";
    }

    std::lock_guard<std::mutex> lk(g_aeGmstMu);
    if (g_aeApplied) {
        return "[AlchemyEnchantGMST] already active";
    }

    size_t applied     = 0u;
    size_t skippedDead = 0u;
    g_savedAe.clear();

    for (size_t i = 0; i < kAlchemyEnchantSpecCount; ++i) {
        const auto& spec = kAlchemyEnchantSpecs[i];
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
        if (!SehReadAeU32(field, &orig32)) continue;

        uint32_t target32 = 0u;
        if (spec.isInt32) {
            std::memcpy(&target32, &spec.balancedI, 4u);
        } else {
            if (!std::isfinite(spec.balancedF)) continue;
            std::memcpy(&target32, &spec.balancedF, 4u);
        }

        if (SehWriteAeU32(field, target32)) {
            g_savedAe.push_back({field, orig32});
            ++applied;
        }
    }
    g_aeApplied = (applied > 0u);

    {
        std::lock_guard<std::mutex> slk(g_aeStateMu);
        g_aeState.gmstsVerified        = 1u;
        g_aeState.gmstsAppliedCount    = static_cast<uint8_t>(std::min<size_t>(applied, 255u));
        g_aeState.deadLegacySkipped    = static_cast<uint8_t>(std::min<size_t>(skippedDead, 255u));
        g_aeState.alchemyEnchantActive = g_aeApplied ? 1u : 0u;
    }

    char buf[224];
    std::snprintf(buf, sizeof buf,
                  "[AlchemyEnchantGMST] applied %zu binary-verified enchanting/training/soul/leveled GMSTs (%zu 0-reader skipped)",
                  applied, skippedDead);
    return buf;
}

void ShutdownAlchemyEnchantGmst() {
    std::lock_guard<std::mutex> lk(g_aeGmstMu);
    if (!g_aeApplied) return;
    for (const auto& e : g_savedAe) {
        SehWriteAeU32(e.ptr, e.saved32);
    }
    g_savedAe.clear();
    g_aeApplied = false;
    {
        std::lock_guard<std::mutex> slk(g_aeStateMu);
        g_aeState.gmstsAppliedCount = 0u;
    }
}
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetAlchemyEnchantState(raw::rt::RawAlchemyEnchantState* out) {
    if (!out) return 0;
    std::lock_guard<std::mutex> lk(raw::rt::g_aeStateMu);
    *out = raw::rt::g_aeState;
    return 1;
}
