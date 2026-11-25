// test_stealth_social_gmst.cpp: Wave 52 verification suite for Binary-Verified 1.7.104.0 Stealth Sound/Light,
// Lockpicking, Pickpocket & Speechcraft/Bribery GMSTs (rt_stealth_social_gmst.h/.cpp) verified directly
// against the real SkyrimSE.exe 1.7.104.0 PE image!
// Compiles standalone (default) OR links into ce_runtime_check with -DRAW_STEALTH_SOCIAL_GMST_NO_MAIN.
// Original work, MIT.

#include "../src/engine_rt/rt_image.h"
#include "../src/engine_rt/rt_stealth_social_gmst.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#include <windows.h>

namespace raw::rt {

int RunStealthSocialGmstTests(void (*checkFn)(bool, const char*) = nullptr) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* name) {
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s stealth_social_gmst::%s\n", cond ? "PASS" : "FAIL", name);
        }
        if (cond) ++pass; else ++fail;
    };

    // 1. Table completeness, category coverage & dead 0-reader legacy GMST hygiene:
    {
        size_t n = 0;
        const auto* tbl = StealthSocialGmstTable(&n);
        size_t liveCount = 0, deadCount = 0;
        bool hasStealth = false, hasLockpick = false, hasPickpocket = false, hasSocial = false;
        bool deadStealthBoy = false, deadLockMult = false, deadBribeBase = false;
        bool valid = (tbl != nullptr && n >= 20u);
        for (size_t i = 0; i < n; ++i) {
            if (!tbl[i].name || !tbl[i].category || tbl[i].expectedRva == 0u ||
                !std::isfinite(tbl[i].exeDefault) || !std::isfinite(tbl[i].balancedValue)) {
                valid = false;
            }
            if (tbl[i].hasLiveReader) {
                ++liveCount;
                if (std::strcmp(tbl[i].category, "Stealth") == 0)    hasStealth = true;
                if (std::strcmp(tbl[i].category, "Lockpick") == 0)   hasLockpick = true;
                if (std::strcmp(tbl[i].category, "Pickpocket") == 0) hasPickpocket = true;
                if (std::strcmp(tbl[i].category, "Social") == 0)     hasSocial = true;
            } else {
                ++deadCount;
                if (std::strcmp(tbl[i].name, "fSneakStealthBoyMult") == 0) deadStealthBoy = true;
                if (std::strcmp(tbl[i].name, "fLockSkillMult") == 0)       deadLockMult = true;
                if (std::strcmp(tbl[i].name, "fBribeBase") == 0)           deadBribeBase = true;
            }
        }
        check(valid && liveCount == 20u && deadCount == 6u &&
              hasStealth && hasLockpick && hasPickpocket && hasSocial &&
              deadStealthBoy && deadLockMult && deadBribeBase,
              "table_completeness_and_dead_legacy_gmst_hygiene");
    }

    // 2. Monotonic stealth sound/light physics + pickpocket probability clamping + C ABI export:
    {
        StealthEnvInput plateRunningSun{};
        plateRunningSun.armorBootWeight = 22.0f;
        plateRunningSun.lightLevel01    = 0.95f;
        plateRunningSun.sneakSkill0_100 = 25.0f;
        plateRunningSun.isRunning       = true;
        plateRunningSun.isMoving        = true;

        StealthEnvInput leatherCrouchDry{};
        leatherCrouchDry.armorBootWeight = 2.0f;
        leatherCrouchDry.lightLevel01    = 0.10f;
        leatherCrouchDry.sneakSkill0_100 = 60.0f;
        leatherCrouchDry.rain01          = 0.0f;
        leatherCrouchDry.isRunning       = false;
        leatherCrouchDry.isMoving        = true;

        StealthEnvInput leatherCrouchRain = leatherCrouchDry;
        leatherCrouchRain.rain01          = 0.90f;
        leatherCrouchRain.windBeaufort    = 7.0f;

        const auto sPlate = EvaluateStealthNoiseAndVisibility(plateRunningSun);
        const auto sDry   = EvaluateStealthNoiseAndVisibility(leatherCrouchDry);
        const auto sRain  = EvaluateStealthNoiseAndVisibility(leatherCrouchRain);

        const bool stealthMono = (sPlate.audibleNoiseScore > sDry.audibleNoiseScore * 3.0f) &&
                                 (sPlate.visualExposureScore > sDry.visualExposureScore * 3.0f) &&
                                 (sPlate.compositeDetectRisk01 > sDry.compositeDetectRisk01 + 0.50f) &&
                                 (sRain.audibleRadiusU < sDry.audibleRadiusU) &&
                                 (sRain.audibleRadiusU >= 80.0f);

        PickpocketInput pMasterCoin{100.0f, 15.0f, 0.1f, 50.0f, true, false, 5.0f, 92.0f};
        PickpocketInput pNoviceHeavy{15.0f, 60.0f, 18.0f, 900.0f, false, true, 5.0f, 92.0f};
        PickpocketInput pMidRing{55.0f, 30.0f, 0.3f, 250.0f, false, false, 5.0f, 92.0f};

        const auto rMaster = EvaluatePickpocketChance(pMasterCoin);
        const auto rNovice = EvaluatePickpocketChance(pNoviceHeavy);
        const auto rMid    = EvaluatePickpocketChance(pMidRing);

        const bool ppOk = (rMaster.clampedChancePct <= 92.0f + 1e-4f && rMaster.wasClamped) &&
                          (rNovice.clampedChancePct >= 5.0f - 1e-4f && rNovice.wasClamped) &&
                          (rMid.clampedChancePct > rNovice.clampedChancePct) &&
                          (rMaster.clampedChancePct > rMid.clampedChancePct);

        RawStealthSocialState st{};
        const int rcOk   = RAW_GetStealthSocialState(&st);
        const int rcNull = RAW_GetStealthSocialState(nullptr);
        const bool abiOk = (rcOk == 1) && (rcNull == 0) &&
                           (std::fabs(st.sneakMaxDistanceU - 1850.0f) < 1e-3f) &&
                           (std::fabs(st.pickpocketMaxChancePct - 92.0f) < 1e-3f) &&
                           (std::fabs(st.bribeScale - 1.35f) < 1e-3f);
        check(stealthMono && ppOk && abiOk, "stealth_sound_light_pickpocket_and_c_abi");
    }

    // 3. Real SkyrimSE.exe 1.7.104.0 PE verification of ALL 26 Stealth/Social GMST RVAs, names & defaults + planted control!
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
            const bool realOk = VerifyStealthSocialGmstTable(img, &verifiedCount);

            // Planted fail-closed control: corrupt fPickPocketMaxChance (0x20A1280+8) -> MUST reject!
            const float badVal = 199.0f;
            std::memcpy(mapped.data() + 0x20A1280u + 8u, &badVal, 4u);
            const bool plantedReject = !VerifyStealthSocialGmstTable(img);

            size_t totalSpecs = 0;
            StealthSocialGmstTable(&totalSpecs);
            check(realOk && verifiedCount == totalSpecs && plantedReject,
                  "real_skyrim17104_stealth_social_gmsts_verified");
        } else {
            check(true, "real_skyrim17104_stealth_social_gmsts_skipped_no_exe");
        }
    }

    return fail;
}

}  // namespace raw::rt

#ifndef RAW_STEALTH_SOCIAL_GMST_NO_MAIN
int main() {
    const int fail = raw::rt::RunStealthSocialGmstTests();
    std::printf("stealth_social_gmst: %s (%d failures)\n", fail == 0 ? "ALL PASS" : "FAILED", fail);
    return fail == 0 ? 0 : 1;
}
#endif
