// test_ai_packages.cpp: Wave 49 verification suite for Binary-Verified 1.7.104.0 Flanking, Ranged,
// Companion Anti-Doorway-Blocking & Stealth-Detection AI GMSTs (rt_ai_packages.h/.cpp) verified directly
// against the real SkyrimSE.exe 1.7.104.0 PE image!
// Compiles standalone (default) OR links into ce_runtime_check with -DRAW_AI_PACKAGES_NO_MAIN.
// Original work, MIT.

#include "../src/engine_rt/rt_ai_packages.h"
#include "../src/engine_rt/rt_image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#include <windows.h>

namespace raw::rt {

int RunAiPackagesTests(void (*checkFn)(bool, const char*) = nullptr) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* name) {
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s ai_packages::%s\n", cond ? "PASS" : "FAIL", name);
        }
        if (cond) ++pass; else ++fail;
    };

    // 1. Table completeness, category coverage & dead 0-reader legacy GMST hygiene:
    {
        size_t n = 0;
        const auto* tbl = AiPackagesTable(&n);
        size_t liveCount = 0, deadCount = 0;
        bool hasFlank = false, hasRanged = false, hasSearch = false, hasDetect = false, hasComp = false;
        bool deadFollowSkipped = false, deadStandoffSkipped = false, deadInvestSkipped = false;
        bool valid = (tbl != nullptr && n >= 20u);
        for (size_t i = 0; i < n; ++i) {
            if (!tbl[i].name || !tbl[i].category || tbl[i].expectedRva == 0u ||
                !std::isfinite(tbl[i].exeDefault) || !std::isfinite(tbl[i].balancedValue)) {
                valid = false;
            }
            if (tbl[i].hasLiveReader) {
                ++liveCount;
                if (std::strcmp(tbl[i].category, "Flanking") == 0)  hasFlank = true;
                if (std::strcmp(tbl[i].category, "Ranged") == 0)    hasRanged = true;
                if (std::strcmp(tbl[i].category, "Search") == 0)    hasSearch = true;
                if (std::strcmp(tbl[i].category, "Detection") == 0) hasDetect = true;
                if (std::strcmp(tbl[i].category, "Companion") == 0) hasComp = true;
            } else {
                ++deadCount;
                if (std::strcmp(tbl[i].name, "fCombatTeammateFollowRadiusBase") == 0) deadFollowSkipped = true;
                if (std::strcmp(tbl[i].name, "fCombatRangedStandoffTimer") == 0)      deadStandoffSkipped = true;
                if (std::strcmp(tbl[i].name, "fCombatSearchInvestigateTime") == 0)    deadInvestSkipped = true;
            }
        }
        check(valid && liveCount == 20u && deadCount == 4u &&
              hasFlank && hasRanged && hasSearch && hasDetect && hasComp &&
              deadFollowSkipped && deadStandoffSkipped && deadInvestSkipped,
              "table_completeness_and_dead_legacy_gmst_hygiene");
    }

    // 2. Monotonic weather/night/interior tactical AI profile response:
    {
        TacticalAiEnv clearNoon{};
        clearNoon.windBeaufort = 1.0f;

        TacticalAiEnv rainyDusk{};
        rainyDusk.rain01          = 0.7f;
        rainyDusk.fog01           = 0.4f;
        rainyDusk.nightDarkness01 = 0.45f;
        rainyDusk.windBeaufort    = 5.0f;

        TacticalAiEnv blizzardNight{};
        blizzardNight.snow01          = 1.0f;
        blizzardNight.fog01           = 0.85f;
        blizzardNight.nightDarkness01 = 1.0f;
        blizzardNight.windBeaufort    = 10.0f;

        TacticalAiEnv darkCrypt{};
        darkCrypt.snow01          = 1.0f; // sheltered inside!
        darkCrypt.windBeaufort    = 11.0f;
        darkCrypt.nightDarkness01 = 0.85f;
        darkCrypt.inInterior      = true;

        const auto pNoon     = EvaluateTacticalAiProfile(clearNoon);
        const auto pDusk     = EvaluateTacticalAiProfile(rainyDusk);
        const auto pBlizzard = EvaluateTacticalAiProfile(blizzardNight);
        const auto pCrypt    = EvaluateTacticalAiProfile(darkCrypt);

        const bool monoCone = (pNoon.effectiveViewConeDeg > pDusk.effectiveViewConeDeg) &&
                              (pDusk.effectiveViewConeDeg > pBlizzard.effectiveViewConeDeg) &&
                              (pBlizzard.effectiveViewConeDeg >= 104.0f);
        const bool monoSight = (pNoon.effectiveSightRadiusU > pDusk.effectiveSightRadiusU) &&
                               (pDusk.effectiveSightRadiusU > pBlizzard.effectiveSightRadiusU) &&
                               (pBlizzard.effectiveSightRadiusU >= 480.0f);
        const bool monoAim = (pNoon.rangedAimDispersionMult < pDusk.rangedAimDispersionMult) &&
                             (pDusk.rangedAimDispersionMult < pBlizzard.rangedAimDispersionMult) &&
                             (pBlizzard.rangedAimDispersionMult <= 2.25f);
        // Interior crypt ignores outdoor blizzard wind for archery dispersion, but stays bounded & persistent:
        const bool interiorOk = (pCrypt.rangedAimDispersionMult < pBlizzard.rangedAimDispersionMult) &&
                                (pCrypt.searchPersistenceMult > pBlizzard.searchPersistenceMult) &&
                                (pCrypt.flankingAggressionMult >= 0.80f && pCrypt.flankingAggressionMult <= 1.35f);
        check(monoCone && monoSight && monoAim && interiorOk,
              "monotonic_weather_night_interior_tactical_ai_profile");
    }

    // 3. Environment update + RAW_GetAiPackagesState C ABI round-trip & null safety:
    {
        TacticalAiEnv storm{};
        storm.snow01          = 0.9f;
        storm.fog01           = 0.6f;
        storm.nightDarkness01 = 0.8f;
        storm.windBeaufort    = 8.0f;
        AiPackagesUpdateEnvironment(storm);

        RawAiPackagesState st{};
        const int rcOk   = RAW_GetAiPackagesState(&st);
        const int rcNull = RAW_GetAiPackagesState(nullptr);
        const auto expected = EvaluateTacticalAiProfile(storm);
        const bool abiOk = (rcOk == 1) && (rcNull == 0) &&
                           (std::fabs(st.effectiveViewConeDeg - expected.effectiveViewConeDeg) < 1e-4f) &&
                           (std::fabs(st.effectiveSightRadiusU - expected.effectiveSightRadiusU) < 1e-3f) &&
                           (std::fabs(st.rangedAimDispersionMult - expected.rangedAimDispersionMult) < 1e-4f) &&
                           (std::fabs(st.followerDoorSpacingU - 280.0f) < 1e-4f);
        check(abiOk, "environment_update_and_c_abi_export");
    }

    // 4. Real SkyrimSE.exe 1.7.104.0 PE verification of ALL 24 AI GMST RVAs, name pointers & defaults + planted control!
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
            const bool realOk = VerifyAiPackagesGmstTable(img, &verifiedCount);

            // Planted fail-closed control: corrupt fDetectionViewCone (0x209EE20+8) -> MUST reject!
            const float badVal = 360.0f;
            std::memcpy(mapped.data() + 0x209EE20u + 8u, &badVal, 4u);
            uint32_t corruptCount = 0u;
            const bool plantedReject = !VerifyAiPackagesGmstTable(img, &corruptCount);

            size_t totalSpecs = 0;
            AiPackagesTable(&totalSpecs);
            check(realOk && verifiedCount == totalSpecs && plantedReject,
                  "real_skyrim17104_ai_packages_gmsts_verified");
        } else {
            check(true, "real_skyrim17104_ai_packages_gmsts_skipped_no_exe");
        }
    }

    return fail;
}

}  // namespace raw::rt

#ifndef RAW_AI_PACKAGES_NO_MAIN
int main() {
    const int fail = raw::rt::RunAiPackagesTests();
    std::printf("ai_packages: %s (%d failures)\n", fail == 0 ? "ALL PASS" : "FAILED", fail);
    return fail == 0 ? 0 : 1;
}
#endif
