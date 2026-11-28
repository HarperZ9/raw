// test_locomotion_melee_gmst.cpp: Wave 53 verification suite for Binary-Verified 1.7.104.0 Locomotion,
// Armor-Weight Fatigue, Power-Attack & Melee Combat GMSTs (rt_locomotion_melee_gmst.h/.cpp) verified directly
// against the real SkyrimSE.exe 1.7.104.0 PE image!
// Compiles standalone (default) OR links into ce_runtime_check with -DRAW_LOCOMOTION_MELEE_GMST_NO_MAIN.
// Original work, MIT.

#include "../src/engine_rt/rt_image.h"
#include "../src/engine_rt/rt_locomotion_melee_gmst.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#include <windows.h>

namespace raw::rt {

int RunLocomotionMeleeGmstTests(void (*checkFn)(bool, const char*) = nullptr) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* name) {
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s locomotion_melee_gmst::%s\n", cond ? "PASS" : "FAIL", name);
        }
        if (cond) ++pass; else ++fail;
    };

    // 1. Table completeness, category coverage & dead 0-reader legacy GMST hygiene:
    {
        size_t n = 0;
        const auto* tbl = LocomotionMeleeGmstTable(&n);
        size_t liveCount = 0, deadCount = 0;
        bool hasLoco = false, hasFatigue = false, hasPower = false, hasMelee = false;
        bool deadBonusSkipped = false, deadSprintEncumSkipped = false, deadMeleeRestSkipped = false;
        bool valid = (tbl != nullptr && n >= 20u);
        for (size_t i = 0; i < n; ++i) {
            if (!tbl[i].name || !tbl[i].category || tbl[i].expectedRva == 0u ||
                !std::isfinite(tbl[i].exeDefault) || !std::isfinite(tbl[i].balancedValue)) {
                valid = false;
            }
            if (tbl[i].hasLiveReader) {
                ++liveCount;
                if (std::strcmp(tbl[i].category, "Locomotion") == 0)  hasLoco = true;
                if (std::strcmp(tbl[i].category, "Fatigue") == 0)     hasFatigue = true;
                if (std::strcmp(tbl[i].category, "PowerAttack") == 0) hasPower = true;
                if (std::strcmp(tbl[i].category, "Melee") == 0)       hasMelee = true;
            } else {
                ++deadCount;
                if (std::strcmp(tbl[i].name, "fPowerAttackDefaultBonus") == 0)             deadBonusSkipped = true;
                if (std::strcmp(tbl[i].name, "fSprintEncumbranceMult") == 0)               deadSprintEncumSkipped = true;
                if (std::strcmp(tbl[i].name, "fMeleeMovementRestrictionsUpdateTime") == 0) deadMeleeRestSkipped = true;
            }
        }
        check(valid && liveCount == 20u && deadCount == 5u &&
              hasLoco && hasFatigue && hasPower && hasMelee &&
              deadBonusSkipped && deadSprintEncumSkipped && deadMeleeRestSkipped,
              "table_completeness_and_dead_legacy_gmst_hygiene");
    }

    // 2. Monotonic armor-weight locomotion penalty, power-attack stamina scaling & C ABI export:
    {
        LocomotionInput scout{0.0f, true, true, false};
        LocomotionInput ranger{28.0f, true, true, false};
        LocomotionInput knight{80.0f, true, true, false};
        LocomotionInput sheathedKnight{80.0f, false, true, false};

        const auto pScout    = EvaluateLocomotionProfile(scout);
        const auto pRanger   = EvaluateLocomotionProfile(ranger);
        const auto pKnight   = EvaluateLocomotionProfile(knight);
        const auto pSheathed = EvaluateLocomotionProfile(sheathedKnight);

        const bool locoMono = (pScout.effectiveSpeedUnitsSec > pRanger.effectiveSpeedUnitsSec) &&
                              (pRanger.effectiveSpeedUnitsSec > pKnight.effectiveSpeedUnitsSec) &&
                              (pSheathed.effectiveSpeedUnitsSec > pKnight.effectiveSpeedUnitsSec) &&
                              (pKnight.sprintStaminaDrainPerS > pRanger.sprintStaminaDrainPerS) &&
                              (pRanger.sprintStaminaDrainPerS > pScout.sprintStaminaDrainPerS) &&
                              (pKnight.speedMultiplier >= 0.65f);

        PowerAttackInput dagger{3.0f,  40.0f, false, false};
        PowerAttackInput sword{12.0f,  40.0f, false, false};
        PowerAttackInput hammer{28.0f, 40.0f, true,  false};
        PowerAttackInput dualSwords{12.0f, 40.0f, false, true};

        const auto cDagger = EvaluatePowerAttackCost(dagger);
        const auto cSword  = EvaluatePowerAttackCost(sword);
        const auto cHammer = EvaluatePowerAttackCost(hammer);
        const auto cDual   = EvaluatePowerAttackCost(dualSwords);

        const bool paMono = (cDagger.staminaCost < cSword.staminaCost) &&
                            (cSword.staminaCost < cHammer.staminaCost) &&
                            (cDual.staminaCost > cSword.staminaCost) &&
                            (cHammer.effectiveReachU > cSword.effectiveReachU) &&
                            (cSword.effectiveReachU > cDagger.effectiveReachU);

        RawLocomotionMeleeState st{};
        const int rcOk   = RAW_GetLocomotionMeleeState(&st);
        const int rcNull = RAW_GetLocomotionMeleeState(nullptr);
        const bool abiOk = (rcOk == 1) && (rcNull == 0) &&
                           (std::fabs(st.moveCharRunBase - 385.0f) < 1e-3f) &&
                           (std::fabs(st.sprintWeightMult - 0.008f) < 1e-5f) &&
                           (std::fabs(st.combatHitConeAngleDeg - 24.0f) < 1e-3f);
        check(locoMono && paMono && abiOk, "locomotion_weight_power_attack_and_c_abi");
    }

    // 3. Real SkyrimSE.exe 1.7.104.0 PE verification of ALL 25 Locomotion/Melee GMST RVAs, names & defaults + planted control!
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
            const bool realOk = VerifyLocomotionMeleeGmstTable(img, &verifiedCount);

            // Planted fail-closed control: corrupt fMoveCharRunBase (0x20AA4A0+8) -> MUST reject!
            const float badVal = 9999.0f;
            std::memcpy(mapped.data() + 0x20AA4A0u + 8u, &badVal, 4u);
            const bool plantedReject = !VerifyLocomotionMeleeGmstTable(img);

            size_t totalSpecs = 0;
            LocomotionMeleeGmstTable(&totalSpecs);
            check(realOk && verifiedCount == totalSpecs && plantedReject,
                  "real_skyrim17104_locomotion_melee_gmsts_verified");
        } else {
            check(true, "real_skyrim17104_locomotion_melee_gmsts_skipped_no_exe");
        }
    }

    return fail;
}

}  // namespace raw::rt

#ifndef RAW_LOCOMOTION_MELEE_GMST_NO_MAIN
int main() {
    const int fail = raw::rt::RunLocomotionMeleeGmstTests();
    std::printf("locomotion_melee_gmst: %s (%d failures)\n", fail == 0 ? "ALL PASS" : "FAILED", fail);
    return fail == 0 ? 0 : 1;
}
#endif
