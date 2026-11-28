// test_alchemy_enchant_gmst.cpp: Wave 54 verification suite for Binary-Verified 1.7.104.0 Enchantment,
// Skill Training, Soul-Gem, Alchemy & Leveled-Encounter GMSTs (rt_alchemy_enchant_gmst.h/.cpp) verified
// directly against the real SkyrimSE.exe 1.7.104.0 PE image!
// Compiles standalone (default) OR links into ce_runtime_check with -DRAW_ALCHEMY_ENCHANT_GMST_NO_MAIN.
// Original work, MIT.

#include "../src/engine_rt/rt_alchemy_enchant_gmst.h"
#include "../src/engine_rt/rt_image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_set>
#include <vector>

#include <windows.h>

namespace raw::rt {

int RunAlchemyEnchantGmstTests(void (*checkFn)(bool, const char*) = nullptr) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* name) {
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s alchemy_enchant_gmst::%s\n", cond ? "PASS" : "FAIL", name);
        }
        if (cond) ++pass; else ++fail;
    };

    // 1. Table completeness, uniqueness, category coverage & 0-reader legacy GMST hygiene (zero overlap!):
    {
        size_t n = 0;
        const auto* tbl = AlchemyEnchantGmstTable(&n);
        size_t liveCount = 0, deadCount = 0;
        bool hasEnch = false, hasSoul = false, hasTrain = false, hasAlch = false, hasTemp = false, hasLev = false;
        bool deadEasySkipped = false, deadGoldSkipped = false, deadCombatAlchSkipped = false;
        std::unordered_set<std::string> seenNames;
        std::unordered_set<uint32_t>    seenRvas;
        bool valid = (tbl != nullptr && n >= 20u);
        for (size_t i = 0; i < n; ++i) {
            if (!tbl[i].name || !tbl[i].category || tbl[i].expectedRva == 0u) valid = false;
            if (!tbl[i].isInt32 && (!std::isfinite(tbl[i].exeDefaultF) || !std::isfinite(tbl[i].balancedF))) valid = false;
            if (!seenNames.insert(tbl[i].name).second) valid = false;
            if (!seenRvas.insert(tbl[i].expectedRva).second) valid = false;
            // Verify zero overlap with previously patched GMSTs from rt_magic_physics / rt_progression_economy:
            if (std::strcmp(tbl[i].name, "fEnchantingSkillFactor") == 0 ||
                std::strcmp(tbl[i].name, "fEnchantingSkillCostScale") == 0 ||
                std::strcmp(tbl[i].name, "fAlchemySkillFactor") == 0 ||
                std::strcmp(tbl[i].name, "fMagicDualCastingCostMult") == 0) {
                valid = false;
            }
            if (tbl[i].hasLiveReader) {
                ++liveCount;
                if (std::strcmp(tbl[i].category, "Enchanting") == 0) hasEnch = true;
                if (std::strcmp(tbl[i].category, "SoulGem") == 0)    hasSoul = true;
                if (std::strcmp(tbl[i].category, "Training") == 0)   hasTrain = true;
                if (std::strcmp(tbl[i].category, "Alchemy") == 0)    hasAlch = true;
                if (std::strcmp(tbl[i].category, "Tempering") == 0)  hasTemp = true;
                if (std::strcmp(tbl[i].category, "Leveled") == 0)    hasLev = true;
            } else {
                ++deadCount;
                if (std::strcmp(tbl[i].name, "fLeveledActorMultEasy") == 0)     deadEasySkipped = true;
                if (std::strcmp(tbl[i].name, "fEnchantmentGoldMult") == 0)      deadGoldSkipped = true;
                if (std::strcmp(tbl[i].name, "iAllowAlchemyDuringCombat") == 0) deadCombatAlchSkipped = true;
            }
        }
        check(valid && liveCount == 20u && deadCount == 6u &&
              hasEnch && hasSoul && hasTrain && hasAlch && hasTemp && hasLev &&
              deadEasySkipped && deadGoldSkipped && deadCombatAlchSkipped,
              "table_completeness_uniqueness_and_dead_legacy_gmst_hygiene");
    }

    // 2. Monotonic soul-gem charge yield (Petty < Lesser < Common < Greater < Grand), encounter tiers & C ABI:
    {
        const auto cPetty   = EvaluateEnchantmentChargeEconomy(SoulGemTier::Petty,   40.0f, 50.0f);
        const auto cLesser  = EvaluateEnchantmentChargeEconomy(SoulGemTier::Lesser,  40.0f, 50.0f);
        const auto cCommon  = EvaluateEnchantmentChargeEconomy(SoulGemTier::Common,  40.0f, 50.0f);
        const auto cGreater = EvaluateEnchantmentChargeEconomy(SoulGemTier::Greater, 40.0f, 50.0f);
        const auto cGrand   = EvaluateEnchantmentChargeEconomy(SoulGemTier::Grand,   40.0f, 50.0f);
        const auto cMaster  = EvaluateEnchantmentChargeEconomy(SoulGemTier::Grand,   40.0f, 100.0f);

        const bool soulMono = (cPetty.soulChargeYield < cLesser.soulChargeYield) &&
                              (cLesser.soulChargeYield < cCommon.soulChargeYield) &&
                              (cCommon.soulChargeYield < cGreater.soulChargeYield) &&
                              (cGreater.soulChargeYield < cGrand.soulChargeYield) &&
                              (cGrand.strikesPerCharge > cPetty.strikesPerCharge * 8.0f) &&
                              (cMaster.strikesPerCharge > cGrand.strikesPerCharge);

        const auto tEasy     = EvaluateEncounterTierLevel(30.0f, EncounterTier::Easy);
        const auto tMedium   = EvaluateEncounterTierLevel(30.0f, EncounterTier::Medium);
        const auto tHard     = EvaluateEncounterTierLevel(30.0f, EncounterTier::Hard);
        const auto tVeryHard = EvaluateEncounterTierLevel(30.0f, EncounterTier::VeryHard);

        const bool tierMono = (tEasy.targetLevel < tMedium.targetLevel) &&
                              (tMedium.targetLevel < tHard.targetLevel) &&
                              (tHard.targetLevel < tVeryHard.targetLevel) &&
                              (tEasy.minBandLevel >= 1.0f && tVeryHard.maxBandLevel > tVeryHard.targetLevel);

        RawAlchemyEnchantState st{};
        const int rcOk   = RAW_GetAlchemyEnchantState(&st);
        const int rcNull = RAW_GetAlchemyEnchantState(nullptr);
        const bool abiOk = (rcOk == 1) && (rcNull == 0) &&
                           (std::fabs(st.enchantChargeMult - 1.25f) < 1e-4f) &&
                           (std::fabs(st.enchantDrainMult - 0.18f) < 1e-4f) &&
                           (st.grandSoulValue == 3600) &&
                           (st.trainingAllowedPerLevel == 5);
        check(soulMono && tierMono && abiOk, "soul_gem_charge_encounter_tiers_and_c_abi");
    }

    // 3. Real SkyrimSE.exe 1.7.104.0 PE verification of ALL 26 Alchemy/Enchant GMST RVAs, names & defaults + planted controls!
    {
        std::ifstream f(R"(H:\SteamLibrary\steamapps\common\Skyrim Special Edition\SkyrimSE.exe)", std::ios::binary);
        if (f) {
            std::vector<uint8_t> pe((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(pe.data());
            const auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS64*>(pe.data() + dos->e_lfanew);
            std::vector<uint8_t> mapped(nt->OptionalHeader.SizeOfImage, uint8_t{0});
            std::memcpy(mapped.data(), pe.data(), std::min<size_t>(pe.size(), nt->OptionalHeader.SizeOfHeaders));
            const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
            Image img{};
            img.base        = mapped.data();
            img.sizeOfImage = nt->OptionalHeader.SizeOfImage;
            for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
                const uint32_t va    = sec->VirtualAddress;
                const uint32_t rawSz = sec->SizeOfRawData;
                if (rawSz > 0 && size_t(sec->PointerToRawData) + rawSz <= pe.size() && size_t(va) + rawSz <= mapped.size()) {
                    std::memcpy(mapped.data() + va, pe.data() + sec->PointerToRawData, rawSz);
                }
            }

            uint32_t verifiedCount = 0u;
            const bool realOk = VerifyAlchemyEnchantGmstTable(img, &verifiedCount);

            // Planted fail-closed control #1: corrupt float fMagicEnchantmentDrainMult (0x20AAD88+8) -> MUST reject!
            float savedDrain = 0.0f;
            std::memcpy(&savedDrain, mapped.data() + 0x20AAD88u + 8u, 4u);
            const float badFloat = 9.9f;
            std::memcpy(mapped.data() + 0x20AAD88u + 8u, &badFloat, 4u);
            const bool rejectFloat = !VerifyAlchemyEnchantGmstTable(img);
            std::memcpy(mapped.data() + 0x20AAD88u + 8u, &savedDrain, 4u);

            // Planted fail-closed control #2: corrupt int32 iSoulLevelValueGrand (0x207B5B8+8) -> MUST reject!
            const int32_t badInt = 12345;
            std::memcpy(mapped.data() + 0x207B5B8u + 8u, &badInt, 4u);
            const bool rejectInt = !VerifyAlchemyEnchantGmstTable(img);

            size_t totalSpecs = 0;
            AlchemyEnchantGmstTable(&totalSpecs);
            check(realOk && verifiedCount == totalSpecs && rejectFloat && rejectInt,
                  "real_skyrim17104_alchemy_enchant_gmsts_verified");
        } else {
            check(true, "real_skyrim17104_alchemy_enchant_gmsts_skipped_no_exe");
        }
    }

    return fail;
}

}  // namespace raw::rt

#ifndef RAW_ALCHEMY_ENCHANT_GMST_NO_MAIN
int main() {
    const int fail = raw::rt::RunAlchemyEnchantGmstTests();
    std::printf("alchemy_enchant_gmst: %s (%d failures)\n", fail == 0 ? "ALL PASS" : "FAILED", fail);
    return fail == 0 ? 0 : 1;
}
#endif
