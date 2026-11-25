// test_crafting_inventory_gmst.cpp: Wave 59 verification suite for Binary-Verified 1.7.104.0 Smithing/Tempering,
// Soul-Gem/Enchanting, Alchemy & Armor/Inventory Physics GMSTs (rt_crafting_inventory_gmst.h/.cpp)
// verified directly against the real SkyrimSE.exe 1.7.104.0 PE image (342 total unique cataloged GMSTs!).
// Compiles standalone (default) OR links into ce_runtime_check with -DRAW_CRAFTING_INVENTORY_GMST_NO_MAIN.
// Original work, MIT.

#include "../src/engine_rt/rt_crafting_inventory_gmst.h"
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

int RunCraftingInventoryGmstTests(void (*checkFn)(bool, const char*) = nullptr) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* name) {
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s crafting_inventory_gmst::%s\n", cond ? "PASS" : "FAIL", name);
        }
        if (cond) ++pass; else ++fail;
    };

    // 1. Table completeness (24 live + 6 dead = 30), uniqueness, category coverage & dead-legacy hygiene:
    {
        size_t n = 0;
        const auto* tbl = CraftingInventoryGmstTable(&n);
        size_t liveCount = 0, deadCount = 0;
        bool hasSmith = false, hasSoul = false, hasEnch = false, hasAlch = false, hasLoot = false, hasInv = false;
        bool deadWortNovice = false, deadCuirass = false, deadHealthData = false;
        std::unordered_set<std::string> seenNames;
        std::unordered_set<uint32_t>    seenRvas;
        bool valid = (tbl != nullptr && n >= 24u);
        for (size_t i = 0; i < n; ++i) {
            if (!tbl[i].name || !tbl[i].category || tbl[i].expectedRva == 0u) valid = false;
            if (!tbl[i].isInt32 && (!std::isfinite(tbl[i].exeDefaultF) || !std::isfinite(tbl[i].balancedF))) valid = false;
            if (!seenNames.insert(tbl[i].name).second) valid = false;
            if (!seenRvas.insert(tbl[i].expectedRva).second) valid = false;
            if (tbl[i].hasLiveReader) {
                ++liveCount;
                if (std::strcmp(tbl[i].category, "Smithing") == 0)   hasSmith = true;
                if (std::strcmp(tbl[i].category, "SoulGem") == 0)    hasSoul = true;
                if (std::strcmp(tbl[i].category, "Enchanting") == 0) hasEnch = true;
                if (std::strcmp(tbl[i].category, "Alchemy") == 0)    hasAlch = true;
                if (std::strcmp(tbl[i].category, "Loot") == 0)       hasLoot = true;
                if (std::strcmp(tbl[i].category, "Inventory") == 0)  hasInv = true;
            } else {
                ++deadCount;
                if (std::strcmp(tbl[i].name, "iWortcraftMaxEffectsNovice") == 0) deadWortNovice = true;
                if (std::strcmp(tbl[i].name, "iArmorWeightCuirass") == 0)        deadCuirass = true;
                if (std::strcmp(tbl[i].name, "fHealthDataValue1") == 0)          deadHealthData = true;
            }
        }
        check(valid && liveCount == 24u && deadCount == 6u &&
              hasSmith && hasSoul && hasEnch && hasAlch && hasLoot && hasInv &&
              deadWortNovice && deadCuirass && deadHealthData,
              "table_completeness_uniqueness_and_dead_legacy_gmst_hygiene");
    }

    // 2. Monotonic smithing tempering curve, carry-weight encumbrance & C ABI export:
    {
        const auto tNovice = EvaluateSmithingTemperCurve(20.0f,  50.0f,  false);
        const auto tExpert = EvaluateSmithingTemperCurve(75.0f,  450.0f, false);
        const auto tMaster = EvaluateSmithingTemperCurve(100.0f, 1800.0f, true);
        const bool temperOk = (tNovice.temperBonusDelta < tExpert.temperBonusDelta) &&
                              (tExpert.temperBonusDelta < tMaster.temperBonusDelta) &&
                              (tMaster.temperBonusDelta < 18.0f) &&
                              (tMaster.qualityTier == 6u && tNovice.qualityTier < tMaster.qualityTier) &&
                              (tMaster.skillXpAwarded > tExpert.skillXpAwarded && tExpert.skillXpAwarded > tNovice.skillXpAwarded);

        const auto eLight = EvaluateCarryEncumbrance(100.0f, 300.0f);
        const auto eHeavy = EvaluateCarryEncumbrance(270.0f, 300.0f);
        const auto eOver  = EvaluateCarryEncumbrance(340.0f, 300.0f);
        const bool carryOk = (!eLight.overencumbered && eLight.speedFactor == 1.0f) &&
                             (!eHeavy.overencumbered && eHeavy.speedFactor < eLight.speedFactor) &&
                             (eOver.overencumbered && eOver.speedFactor < eHeavy.speedFactor);

        RawCraftingInventoryState st{};
        const int rcOk   = RAW_GetCraftingInventoryState(&st);
        const int rcNull = RAW_GetCraftingInventoryState(nullptr);
        const bool abiOk = (rcOk == 1) && (rcNull == 0) &&
                           (st.commonSoulValue == 1200) &&
                           (st.greaterSoulValue == 2400) &&
                           (std::fabs(st.armorBaseFactor - 0.022f) < 1e-4f);
        check(temperOk && carryOk && abiOk, "smithing_temper_carry_encumbrance_and_c_abi");
    }

    // 3. Real SkyrimSE.exe 1.7.104.0 PE verification of ALL 30 Crafting/Inventory GMST RVAs, names & defaults + planted controls!
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
            const bool realOk = VerifyCraftingInventoryGmstTable(img, &verifiedCount);

            // Planted fail-closed control #1: corrupt float fConstructibleSkillUseMult (0x20AABD8+8) -> MUST reject!
            float savedMult = 0.0f;
            std::memcpy(&savedMult, mapped.data() + 0x20AABD8u + 8u, 4u);
            const float badFloat = 77.0f;
            std::memcpy(mapped.data() + 0x20AABD8u + 8u, &badFloat, 4u);
            const bool rejectFloat = !VerifyCraftingInventoryGmstTable(img);
            std::memcpy(mapped.data() + 0x20AABD8u + 8u, &savedMult, 4u);

            // Planted fail-closed control #2: corrupt int32 iSoulLevelValueCommon (0x207B558+8) -> MUST reject!
            const int32_t badInt = 99999;
            std::memcpy(mapped.data() + 0x207B558u + 8u, &badInt, 4u);
            const bool rejectInt = !VerifyCraftingInventoryGmstTable(img);

            size_t totalSpecs = 0;
            CraftingInventoryGmstTable(&totalSpecs);
            check(realOk && verifiedCount == totalSpecs && rejectFloat && rejectInt,
                  "real_skyrim17104_crafting_inventory_gmsts_verified");
        } else {
            check(true, "real_skyrim17104_crafting_inventory_gmsts_skipped_no_exe");
        }
    }

    return fail;
}

}  // namespace raw::rt

int run_crafting_inventory_gmst_tests() {
    return raw::rt::RunCraftingInventoryGmstTests(nullptr);
}

#ifndef RAW_CRAFTING_INVENTORY_GMST_NO_MAIN
int main() {
    const int fail = run_crafting_inventory_gmst_tests();
    std::printf("crafting_inventory_gmst: %s (%d failures)\n", fail == 0 ? "ALL PASS" : "FAILED", fail);
    return fail == 0 ? 0 : 1;
}
#endif
