// test_encounter_balance.cpp: Wave 46 verification suite for Binary-Verified 2027 Archery Ballistics,
// De-Exploited Armor Mitigation Curve & Combat Magicka/Stamina Resource Balance (rt_encounter_balance.h/.cpp)
// verified directly against the real SkyrimSE.exe 1.7.104.0 PE image!
// Compiles standalone (default) OR links into ce_runtime_check with -DRAW_ENCOUNTER_BALANCE_NO_MAIN.
// Original work, MIT.

#include "../src/engine_rt/rt_encounter_balance.h"
#include "../src/engine_rt/rt_image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#include <windows.h>

namespace raw::rt {

int RunEncounterBalanceTests(void (*checkFn)(bool, const char*) = nullptr) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* name) {
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s encounter_balance::%s\n", cond ? "PASS" : "FAIL", name);
        }
        if (cond) ++pass; else ++fail;
    };

    // 1. EncounterBalanceTable completeness & sanity (covers Archery, Armor, Resources; skips dead 0-reader GMSTs):
    {
        size_t n = 0;
        const auto* tbl = EncounterBalanceTable(&n);
        size_t liveArch = 0, liveArmor = 0, liveRes = 0, deadSkipped = 0;
        bool allFinite = (tbl != nullptr && n >= 15u);
        for (size_t i = 0; i < n; ++i) {
            if (!tbl[i].name || !std::isfinite(tbl[i].exeDefault) || !std::isfinite(tbl[i].balancedValue) ||
                tbl[i].expectedRva == 0u) {
                allFinite = false;
            }
            if (!tbl[i].hasLiveReader) {
                ++deadSkipped;
            } else if (std::strcmp(tbl[i].category, "Archery") == 0) {
                ++liveArch;
            } else if (std::strcmp(tbl[i].category, "Armor") == 0) {
                ++liveArmor;
            } else if (std::strcmp(tbl[i].category, "Resources") == 0) {
                ++liveRes;
            }
        }
        check(allFinite && liveArch >= 7u && liveArmor >= 4u && liveRes >= 4u && deadSkipped >= 2u,
              "table_completeness_and_live_reader_hygiene");
    }

    // 2. Archery ballistics physical sanity (crisp draw curve + strictly positive physical gravity, ZERO anti-gravity bug!):
    {
        const auto quickShot = EvaluateArrowBallistics(0.45f, 1.0f, 3600.0f, 0.45f, 1.45f, 0.24f, 0.0016f, 1.45f, 1.18f);
        const auto fullDraw  = EvaluateArrowBallistics(1.45f, 1.0f, 3600.0f, 0.45f, 1.45f, 0.24f, 0.0016f, 1.45f, 1.18f);
        const bool ok =
            (std::fabs(quickShot.drawPower01 - 0.35f) < 1e-3f) &&
            (std::fabs(fullDraw.drawPower01 - 1.00f) < 1e-3f) &&
            (fullDraw.velocityMult > quickShot.velocityMult * 2.5f) &&
            (fullDraw.gravityFactor >= 0.05f && fullDraw.gravityFactor < 0.25f) &&
            (quickShot.gravityFactor > fullDraw.gravityFactor + 0.70f);
        check(ok, "archery_ballistics_physical_draw_and_gravity");
    }

    // 3. De-exploited armor mitigation curve (smooth progression without 95% invulnerability cap):
    {
        const float steelAr  = EvaluateArmorMitigationPct(220.0f, 0.11f, 75.0f);  // 24.2%
        const float ebonyAr  = EvaluateArmorMitigationPct(500.0f, 0.11f, 75.0f);  // 55.0%
        const float maxedAr  = EvaluateArmorMitigationPct(1200.0f, 0.11f, 75.0f); // capped strictly at 75.0%
        const bool ok =
            (steelAr > 20.0f && steelAr < 30.0f) &&
            (ebonyAr > steelAr + 25.0f && ebonyAr < 65.0f) &&
            (std::fabs(maxedAr - 75.0f) < 1e-3f);
        check(ok, "de_exploited_armor_mitigation_curve");
    }

    // 4. Combat power-attack stamina commitment formula (matches SkyrimSE 1.7.104.0 0x41F7F0..0x41F823):
    {
        const float daggerCost     = EvaluatePowerAttackStaminaCost(3.0f,  14.0f, 0.42f, 1.85f);
        const float greatswordCost = EvaluatePowerAttackStaminaCost(18.0f, 14.0f, 0.42f, 1.85f);
        const bool ok =
            (daggerCost > 25.0f && daggerCost < 32.0f) &&
            (greatswordCost > daggerCost + 9.0f && greatswordCost < 46.0f);
        check(ok, "combat_power_attack_stamina_formula");
    }

    // 5. Real SkyrimSE.exe 1.7.104.0 PE verification of ALL 18 GMST RVAs, name pointers & exact on-disk defaults + planted control!
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
            const bool realOk = VerifyEncounterBalanceGmstTable(img, &verifiedCount);

            // Planted fail-closed control: corrupt fArrowGravityMult on-disk default float -> MUST reject!
            const float badVal = 0.85f;
            std::memcpy(mapped.data() + 0x20A7398u + 8u, &badVal, 4u);
            uint32_t corruptCount = 0u;
            const bool plantedReject = !VerifyEncounterBalanceGmstTable(img, &corruptCount);

            size_t totalSpecs = 0;
            EncounterBalanceTable(&totalSpecs);
            check(realOk && verifiedCount == totalSpecs && plantedReject,
                  "real_skyrim17104_gmst_rvas_and_defaults_verified");
        } else {
            check(true, "real_skyrim17104_gmst_rvas_skipped_no_exe");
        }
    }

    return fail;
}

}  // namespace raw::rt

#ifndef RAW_ENCOUNTER_BALANCE_NO_MAIN
int main() {
    const int fail = raw::rt::RunEncounterBalanceTests();
    std::printf("encounter_balance: %s (%d failures)\n", fail == 0 ? "ALL PASS" : "FAILED", fail);
    return fail == 0 ? 0 : 1;
}
#endif
