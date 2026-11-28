// test_progression_economy.cpp: Wave 48 verification suite for Binary-Verified 1.7.104.0 Economy Spread,
// Crafting Anti-Exploit Asymptotic Loop Convergence & Physical Fall/Progression GMSTs (rt_progression_economy.h/.cpp)
// verified directly against the real SkyrimSE.exe 1.7.104.0 PE image!
// Compiles standalone (default) OR links into ce_runtime_check with -DRAW_PROGRESSION_ECONOMY_NO_MAIN.
// Original work, MIT.

#include "../src/engine_rt/rt_image.h"
#include "../src/engine_rt/rt_progression_economy.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#include <windows.h>

namespace raw::rt {

int RunProgressionEconomyTests(void (*checkFn)(bool, const char*) = nullptr) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* name) {
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s progression_economy::%s\n", cond ? "PASS" : "FAIL", name);
        }
        if (cond) ++pass; else ++fail;
    };

    // 1. Table completeness & dead 0-reader legacy GMST hygiene:
    {
        size_t n = 0;
        const auto* tbl = ProgressionEconomyTable(&n);
        size_t liveCount = 0, deadCount = 0;
        bool valid = (tbl != nullptr && n >= 16u);
        for (size_t i = 0; i < n; ++i) {
            if (!tbl[i].name || tbl[i].expectedRva == 0u ||
                !std::isfinite(tbl[i].exeDefault) || !std::isfinite(tbl[i].balancedValue)) {
                valid = false;
            }
            if (tbl[i].hasLiveReader) ++liveCount; else ++deadCount;
        }
        check(valid && liveCount >= 16u && deadCount >= 2u,
              "table_completeness_and_dead_legacy_gmst_hygiene");
    }

    // 2. Anti-exploit crafting loop convergence proof (10-iteration Fortify-Alchemy <-> Fortify-Enchanting feedback loop):
    {
        // Monotonic skill progression from novice (15) -> master (100) with zero fortify gear:
        const auto cNovice = EvaluateCraftingCaps(15.0f,  0.0f);
        const auto cAdept  = EvaluateCraftingCaps(50.0f,  0.0f);
        const auto cMaster = EvaluateCraftingCaps(100.0f, 0.0f);
        const bool monoSkill = (cNovice.effectiveMult > 1.0f) &&
                               (cAdept.effectiveMult > cNovice.effectiveMult) &&
                               (cMaster.effectiveMult > cAdept.effectiveMult) &&
                               (cMaster.effectiveMult < kCraftingHardAsymptote);

        // 10-round iterative Fortify Alchemy <-> Enchanting exploit attempt MUST strictly converge < 2.25x!
        float enchBonus = 0.25f;
        float alchBonus = 0.0f;
        float prevDelta = 100.0f;
        bool strictlyBounded = true;
        for (int iter = 0; iter < 10; ++iter) {
            const auto alch = EvaluateCraftingCaps(100.0f, enchBonus);
            const float nextAlchBonus = (alch.effectiveMult - 1.0f) * 1.15f;
            const auto ench = EvaluateCraftingCaps(100.0f, nextAlchBonus);
            const float nextEnchBonus = (ench.effectiveMult - 1.0f) * 1.15f;
            if (!std::isfinite(alch.effectiveMult) || !std::isfinite(ench.effectiveMult) ||
                alch.effectiveMult >= kCraftingHardAsymptote || ench.effectiveMult >= kCraftingHardAsymptote) {
                strictlyBounded = false;
            }
            prevDelta = std::fabs(nextEnchBonus - enchBonus);
            alchBonus = nextAlchBonus;
            enchBonus = nextEnchBonus;
        }
        // Extreme 1000x fortify injection must STILL stay strictly <= 2.25x!
        const auto cExtreme = EvaluateCraftingCaps(100.0f, 1000.0f);
        const bool converged = (prevDelta < 0.01f) && (alchBonus < 2.0f) && (cExtreme.effectiveMult <= kCraftingHardAsymptote);
        check(monoSkill && strictlyBounded && converged,
              "anti_exploit_crafting_10iter_loop_convergence");
    }

    // 3. Monotonic barter spread & realistic fall-damage energy curve + C ABI export:
    {
        const auto bLow  = EvaluateBarterSpread(15.0f,  1.35f, 3.40f, 1.05f, 0.95f);
        const auto bHigh = EvaluateBarterSpread(100.0f, 1.35f, 3.40f, 1.05f, 0.95f);
        const bool barterOk = (bLow.buyFactor > bHigh.buyFactor + 1.4f) &&
                              (bHigh.sellFactor > bLow.sellFactor + 0.30f) &&
                              (bHigh.buyFactor >= 1.34f && bHigh.sellFactor <= 0.95f);

        const float dSmallRock = EvaluateFallDamage(350.0f,  420.0f, 0.085f, 1.55f); // 5m hop -> 0 dmg
        const float dRoof      = EvaluateFallDamage(650.0f,  420.0f, 0.085f, 1.55f); // 9.3m -> painful (~100 dmg)
        const float dCliff     = EvaluateFallDamage(1100.0f, 420.0f, 0.085f, 1.55f); // 15.7m -> lethal (>400 dmg)
        const bool fallOk = (dSmallRock == 0.0f) && (dRoof > 70.0f && dRoof < 150.0f) && (dCliff > 400.0f);

        RawProgressionEconomyState st{};
        const int rc = RAW_GetProgressionEconomyState(&st);
        const bool abiOk = (rc == 1) && (st.antiExploitActive == 1u) &&
                           (std::fabs(st.craftingAsymptoteCap - kCraftingHardAsymptote) < 1e-4f);
        check(barterOk && fallOk && abiOk, "barter_spread_fall_energy_and_c_abi");
    }

    // 4. Real SkyrimSE.exe 1.7.104.0 PE verification of ALL 19 GMST RVAs, name pointers & defaults + planted control!
    {
        std::ifstream f(R"(H:\SteamLibrary\steamapps\common\Skyrim Special Edition\SkyrimSE.exe)", std::ios::binary);
        if (f) {
            std::vector<uint8_t> pe((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(pe.data());
            const auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS64*>(pe.data() + dos->e_lfanew);
            std::vector<uint8_t> mapped(nt->OptionalHeader.SizeOfImage, 0);
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
            const bool realOk = VerifyProgressionEconomyGmstTable(img, &verifiedCount);

            // Planted fail-closed control: corrupt fSmithingConditionFactor (0x20AAB48+8) -> MUST reject!
            const float badVal = 42.0f;
            std::memcpy(mapped.data() + 0x20AAB48u + 8u, &badVal, 4u);
            uint32_t corruptCount = 0u;
            const bool plantedReject = !VerifyProgressionEconomyGmstTable(img, &corruptCount);

            size_t totalSpecs = 0;
            ProgressionEconomyTable(&totalSpecs);
            check(realOk && verifiedCount == totalSpecs && plantedReject,
                  "real_skyrim17104_progression_economy_gmsts_verified");
        } else {
            check(true, "real_skyrim17104_progression_economy_gmsts_skipped_no_exe");
        }
    }

    return fail;
}

}  // namespace raw::rt

#ifndef RAW_PROGRESSION_ECONOMY_NO_MAIN
int main() {
    const int fail = raw::rt::RunProgressionEconomyTests();
    std::printf("progression_economy: %s (%d failures)\n", fail == 0 ? "ALL PASS" : "FAILED", fail);
    return fail == 0 ? 0 : 1;
}
#endif
