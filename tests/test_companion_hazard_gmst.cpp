// test_companion_hazard_gmst.cpp: Milestone Wave 55 verification suite for Binary-Verified 1.7.104.0
// Companion, Summon, Dialogue, Gear-AI & Environmental Hazard GMSTs (rt_companion_hazard_gmst.h/.cpp)
// verified directly against the real SkyrimSE.exe 1.7.104.0 PE image!
// Compiles standalone (default) OR links into ce_runtime_check with -DRAW_COMPANION_HAZARD_GMST_NO_MAIN.
// Original work, MIT.

#include "../src/engine_rt/rt_companion_hazard_gmst.h"
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

int RunCompanionHazardGmstTests(void (*checkFn)(bool, const char*) = nullptr) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* name) {
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s companion_hazard_gmst::%s\n", cond ? "PASS" : "FAIL", name);
        }
        if (cond) ++pass; else ++fail;
    };

    // 1. Table completeness, uniqueness, category coverage & 0-reader legacy GMST hygiene (zero overlap!):
    {
        size_t n = 0;
        const auto* tbl = CompanionHazardGmstTable(&n);
        size_t liveCount = 0, deadCount = 0;
        bool hasComp = false, hasSummon = false, hasDlg = false, hasGear = false, hasBark = false, hasHaz = false, hasTorch = false;
        bool deadWeapDesire = false, deadDlgDist = false, deadTauntChance = false;
        std::unordered_set<std::string> seenNames;
        std::unordered_set<uint32_t>    seenRvas;
        bool valid = (tbl != nullptr && n >= 20u);
        for (size_t i = 0; i < n; ++i) {
            if (!tbl[i].name || !tbl[i].category || tbl[i].expectedRva == 0u) valid = false;
            if (!tbl[i].isInt32 && (!std::isfinite(tbl[i].exeDefaultF) || !std::isfinite(tbl[i].balancedF))) valid = false;
            if (!seenNames.insert(tbl[i].name).second) valid = false;
            if (!seenRvas.insert(tbl[i].expectedRva).second) valid = false;
            if (tbl[i].hasLiveReader) {
                ++liveCount;
                if (std::strcmp(tbl[i].category, "Companion") == 0)  hasComp = true;
                if (std::strcmp(tbl[i].category, "Summon") == 0)     hasSummon = true;
                if (std::strcmp(tbl[i].category, "Dialogue") == 0)   hasDlg = true;
                if (std::strcmp(tbl[i].category, "GearAI") == 0)     hasGear = true;
                if (std::strcmp(tbl[i].category, "CombatBark") == 0) hasBark = true;
                if (std::strcmp(tbl[i].category, "Hazard") == 0)     hasHaz = true;
                if (std::strcmp(tbl[i].category, "Torch") == 0)      hasTorch = true;
            } else {
                ++deadCount;
                if (std::strcmp(tbl[i].name, "fActorWeaponDesirabilityDamageMult") == 0)  deadWeapDesire = true;
                if (std::strcmp(tbl[i].name, "fAIInDialogueModeWithPlayerDistance") == 0) deadDlgDist = true;
                if (std::strcmp(tbl[i].name, "fCombatSpeakTauntChance") == 0)             deadTauntChance = true;
            }
        }
        check(valid && liveCount == 20u && deadCount == 6u &&
              hasComp && hasSummon && hasDlg && hasGear && hasBark && hasHaz && hasTorch &&
              deadWeapDesire && deadDlgDist && deadTauntChance,
              "table_completeness_uniqueness_and_dead_legacy_gmst_hygiene");
    }

    // 2. Monotonic NPC gear desirability, environmental hazard mitigation & C ABI export:
    {
        NpcGearInput ironSword{10.0f, 45.0f, 0.0f, 9.0f, true, false, false};
        NpcGearInput steelSword{16.0f, 45.0f, 0.0f, 10.0f, false, false, false};
        NpcGearInput enchantedEbony{28.0f, 45.0f, 15.0f, 14.0f, false, false, false};

        const auto sIron  = EvaluateNpcGearDesirability(ironSword);
        const auto sSteel = EvaluateNpcGearDesirability(steelSword, sIron.finalScore);
        const auto sEbony = EvaluateNpcGearDesirability(enchantedEbony, sSteel.finalScore);
        const bool gearOk = (sSteel.finalScore > sIron.finalScore) &&
                            (sEbony.finalScore > sSteel.finalScore) &&
                            sSteel.isUpgradeOverRef && sEbony.isUpgradeOverRef;

        HazardInput fireUnresisted{40.0f, 0.0f, 0.0f, 3.0f, false};
        HazardInput fireResisted{40.0f, 0.0f, 60.0f, 3.0f, false};
        HazardInput fireCapped{40.0f, 0.0f, 150.0f, 3.0f, false};
        HazardInput trapUnarmored{50.0f, 0.0f, 0.0f, 1.0f, true};
        HazardInput trapArmored{50.0f, 450.0f, 0.0f, 1.0f, true};

        const auto hFire0   = EvaluateHazardExposure(fireUnresisted);
        const auto hFire60  = EvaluateHazardExposure(fireResisted);
        const auto hFireCap = EvaluateHazardExposure(fireCapped);
        const auto hTrap0   = EvaluateHazardExposure(trapUnarmored);
        const auto hTrapArm = EvaluateHazardExposure(trapArmored);

        const bool hazOk = (hFire60.totalDamage < hFire0.totalDamage) &&
                           (hFireCap.mitigationFrac <= 0.85f + 1e-5f) &&
                           (hFireCap.effectiveDps > 0.0f) &&
                           (hTrapArm.totalDamage < hTrap0.totalDamage);

        RawCompanionHazardState st{};
        const int rcOk   = RAW_GetCompanionHazardState(&st);
        const int rcNull = RAW_GetCompanionHazardState(nullptr);
        const bool abiOk = (rcOk == 1) && (rcNull == 0) &&
                           (st.maxAllowedFollowers == 8) &&
                           (std::fabs(st.summonDistanceThresholdU - 950.0f) < 1e-3f) &&
                           (std::fabs(st.dialogueDistanceU - 200.0f) < 1e-3f);
        check(gearOk && hazOk && abiOk, "npc_gear_desirability_hazard_mitigation_and_c_abi");
    }

    // 3. Real SkyrimSE.exe 1.7.104.0 PE verification of ALL 26 Companion/Hazard GMST RVAs, names & defaults + planted controls!
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
            const bool realOk = VerifyCompanionHazardGmstTable(img, &verifiedCount);

            // Planted fail-closed control #1: corrupt float fSummonDistanceCheckThreshold (0x20ABA00+8) -> MUST reject!
            float savedSummon = 0.0f;
            std::memcpy(&savedSummon, mapped.data() + 0x20ABA00u + 8u, 4u);
            const float badFloat = 99999.0f;
            std::memcpy(mapped.data() + 0x20ABA00u + 8u, &badFloat, 4u);
            const bool rejectFloat = !VerifyCompanionHazardGmstTable(img);
            std::memcpy(mapped.data() + 0x20ABA00u + 8u, &savedSummon, 4u);

            // Planted fail-closed control #2: corrupt int32 iNumberActorsAllowedToFollowPlayer (0x209FDC8+8) -> MUST reject!
            const int32_t badInt = 999;
            std::memcpy(mapped.data() + 0x209FDC8u + 8u, &badInt, 4u);
            const bool rejectInt = !VerifyCompanionHazardGmstTable(img);

            size_t totalSpecs = 0;
            CompanionHazardGmstTable(&totalSpecs);
            check(realOk && verifiedCount == totalSpecs && rejectFloat && rejectInt,
                  "real_skyrim17104_companion_hazard_gmsts_verified");
        } else {
            check(true, "real_skyrim17104_companion_hazard_gmsts_skipped_no_exe");
        }
    }

    return fail;
}

}  // namespace raw::rt

#ifndef RAW_COMPANION_HAZARD_GMST_NO_MAIN
int main() {
    const int fail = raw::rt::RunCompanionHazardGmstTests();
    std::printf("companion_hazard_gmst: %s (%d failures)\n", fail == 0 ? "ALL PASS" : "FAILED", fail);
    return fail == 0 ? 0 : 1;
}
#endif
