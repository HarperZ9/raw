// test_shouts_survival_gmst.cpp: Wave 57 verification suite for Binary-Verified 1.7.104.0 Shouts,
// Disease/Bleedout, Survival Warmth/Hydrodynamics & Leveling/XP GMSTs (rt_shouts_survival_gmst.h/.cpp)
// verified directly against the real SkyrimSE.exe 1.7.104.0 PE image!
// Compiles standalone (default) OR links into ce_runtime_check with -DRAW_SHOUTS_SURVIVAL_GMST_NO_MAIN.
// Original work, MIT.

#include "../src/engine_rt/rt_image.h"
#include "../src/engine_rt/rt_shouts_survival_gmst.h"

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

int RunShoutsSurvivalGmstTests(void (*checkFn)(bool, const char*) = nullptr) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* name) {
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s shouts_survival_gmst::%s\n", cond ? "PASS" : "FAIL", name);
        }
        if (cond) ++pass; else ++fail;
    };

    // 1. Table completeness (24 live + 6 dead = 30), uniqueness, category coverage & dead-legacy hygiene:
    {
        size_t n = 0;
        const auto* tbl = ShoutsSurvivalGmstTable(&n);
        size_t liveCount = 0, deadCount = 0;
        bool hasShout = false, hasDisease = false, hasBleed = false, hasWard = false, hasSurv = false, hasLvl = false;
        bool deadLongRec = false, deadWerewolf = false, deadXpKill = false;
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
                if (std::strcmp(tbl[i].category, "Shout") == 0)    hasShout = true;
                if (std::strcmp(tbl[i].category, "Disease") == 0)  hasDisease = true;
                if (std::strcmp(tbl[i].category, "Bleedout") == 0) hasBleed = true;
                if (std::strcmp(tbl[i].category, "Ward") == 0)     hasWard = true;
                if (std::strcmp(tbl[i].category, "Survival") == 0) hasSurv = true;
                if (std::strcmp(tbl[i].category, "Leveling") == 0) hasLvl = true;
            } else {
                ++deadCount;
                if (std::strcmp(tbl[i].name, "fCombatShoutLongRecoveryTime") == 0) deadLongRec = true;
                if (std::strcmp(tbl[i].name, "iCrimeGoldWerewolf") == 0)           deadWerewolf = true;
                if (std::strcmp(tbl[i].name, "iXPRewardKillOpponent") == 0)        deadXpKill = true;
            }
        }
        check(valid && liveCount == 24u && deadCount == 6u &&
              hasShout && hasDisease && hasBleed && hasWard && hasSurv && hasLvl &&
              deadLongRec && deadWerewolf && deadXpKill,
              "table_completeness_uniqueness_and_dead_legacy_gmst_hygiene");
    }

    // 2. Monotonic shout cooldown, survival warmth + torch/wet physics, leveling XP curve & C ABI:
    {
        const auto s1 = EvaluateShoutCooldown(1u, 15.0f, 0.0f);
        const auto s2 = EvaluateShoutCooldown(2u, 15.0f, 0.0f);
        const auto s3 = EvaluateShoutCooldown(3u, 15.0f, 0.0f);
        const auto s3Amulet = EvaluateShoutCooldown(3u, 15.0f, 0.20f);
        const bool shoutOk = (s1.finalCooldownSec < s2.finalCooldownSec) &&
                             (s2.finalCooldownSec < s3.finalCooldownSec) &&
                             (s3Amulet.finalCooldownSec < s3.finalCooldownSec) &&
                             (s1.holdTimeSec < s2.holdTimeSec && s2.holdTimeSec < s3.holdTimeSec);

        const auto wWarmTorch = EvaluateSurvivalWarmth(120.0f, true,  false, -15.0f);
        const auto wWarmDry   = EvaluateSurvivalWarmth(120.0f, false, false, -15.0f);
        const auto wColdWet   = EvaluateSurvivalWarmth(40.0f,  false, true,  -15.0f);
        const bool survOk = (wWarmTorch.totalWarmthRating > wWarmDry.totalWarmthRating) &&
                            (wWarmTorch.coldDrainMult < wWarmDry.coldDrainMult) &&
                            (wWarmDry.coldDrainMult < wColdWet.coldDrainMult);

        const float xp1  = EvaluateLevelUpXpRequired(1u);
        const float xp10 = EvaluateLevelUpXpRequired(10u);
        const float xp50 = EvaluateLevelUpXpRequired(50u);
        const bool xpOk = (xp1 == 180.0f) && (xp10 > xp1) && (xp50 > xp10);

        RawShoutsSurvivalState st{};
        const int rcOk   = RAW_GetShoutsSurvivalState(&st);
        const int rcNull = RAW_GetShoutsSurvivalState(nullptr);
        const bool abiOk = (rcOk == 1) && (rcNull == 0) &&
                           (std::fabs(st.shoutTime1Sec - 0.16f) < 1e-4f) &&
                           (std::fabs(st.survTorchBonus - 60.0f) < 1e-3f) &&
                           (std::fabs(st.xpLevelUpBase - 180.0f) < 1e-3f);
        check(shoutOk && survOk && xpOk && abiOk, "shout_cooldown_survival_warmth_xp_and_c_abi");
    }

    // 3. Real SkyrimSE.exe 1.7.104.0 PE verification of ALL 30 Shouts/Survival GMST RVAs, names & defaults + planted controls!
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
            const bool realOk = VerifyShoutsSurvivalGmstTable(img, &verifiedCount);

            // Planted fail-closed control #1: corrupt float fXPLevelUpBase (0x20A8CB8+8) -> MUST reject!
            float savedXp = 0.0f;
            std::memcpy(&savedXp, mapped.data() + 0x20A8CB8u + 8u, 4u);
            const float badFloat = 9999.0f;
            std::memcpy(mapped.data() + 0x20A8CB8u + 8u, &badFloat, 4u);
            const bool rejectFloat = !VerifyShoutsSurvivalGmstTable(img);
            std::memcpy(mapped.data() + 0x20A8CB8u + 8u, &savedXp, 4u);

            // Planted fail-closed control #2: corrupt int32 iVoicePointsDefault (0x20A1298+8) -> MUST reject!
            const int32_t badInt = 777;
            std::memcpy(mapped.data() + 0x20A1298u + 8u, &badInt, 4u);
            const bool rejectInt = !VerifyShoutsSurvivalGmstTable(img);

            size_t totalSpecs = 0;
            ShoutsSurvivalGmstTable(&totalSpecs);
            check(realOk && verifiedCount == totalSpecs && rejectFloat && rejectInt,
                  "real_skyrim17104_shouts_survival_gmsts_verified");
        } else {
            check(true, "real_skyrim17104_shouts_survival_gmsts_skipped_no_exe");
        }
    }

    return fail;
}

}  // namespace raw::rt

int run_shouts_survival_gmst_tests() {
    return raw::rt::RunShoutsSurvivalGmstTests(nullptr);
}

#ifndef RAW_SHOUTS_SURVIVAL_GMST_NO_MAIN
int main() {
    const int fail = run_shouts_survival_gmst_tests();
    std::printf("shouts_survival_gmst: %s (%d failures)\n", fail == 0 ? "ALL PASS" : "FAILED", fail);
    return fail == 0 ? 0 : 1;
}
#endif
