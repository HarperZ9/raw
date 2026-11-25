// test_world_rules_gmst.cpp: Wave 50 verification suite for Binary-Verified 1.7.104.0 Dragon Combat,
// Stagger Poise, Swim/Water Physics & Crime/Witness Realism GMSTs (rt_world_rules_gmst.h/.cpp) verified
// directly against the real SkyrimSE.exe 1.7.104.0 PE image!
// Compiles standalone (default) OR links into ce_runtime_check with -DRAW_WORLD_RULES_GMST_NO_MAIN.
// Original work, MIT.

#include "../src/engine_rt/rt_image.h"
#include "../src/engine_rt/rt_world_rules_gmst.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#include <windows.h>

namespace raw::rt {

int RunWorldRulesGmstTests(void (*checkFn)(bool, const char*) = nullptr) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* name) {
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s world_rules_gmst::%s\n", cond ? "PASS" : "FAIL", name);
        }
        if (cond) ++pass; else ++fail;
    };

    // 1. Table completeness, category coverage & dead 0-reader legacy GMST hygiene:
    {
        size_t n = 0;
        const auto* tbl = WorldRulesGmstTable(&n);
        size_t liveCount = 0, deadCount = 0;
        bool hasCrime = false, hasStagger = false, hasWater = false, hasDragon = false;
        bool deadSoundSkipped = false, deadStaggerSkipped = false, deadShoutSkipped = false;
        bool valid = (tbl != nullptr && n >= 20u);
        for (size_t i = 0; i < n; ++i) {
            if (!tbl[i].name || !tbl[i].category || tbl[i].expectedRva == 0u) valid = false;
            if (!tbl[i].isInt32 && (!std::isfinite(tbl[i].exeDefaultF) || !std::isfinite(tbl[i].balancedF))) valid = false;
            if (tbl[i].hasLiveReader) {
                ++liveCount;
                if (std::strcmp(tbl[i].category, "Crime") == 0)   hasCrime = true;
                if (std::strcmp(tbl[i].category, "Stagger") == 0) hasStagger = true;
                if (std::strcmp(tbl[i].category, "Water") == 0)   hasWater = true;
                if (std::strcmp(tbl[i].category, "Dragon") == 0)  hasDragon = true;
            } else {
                ++deadCount;
                if (std::strcmp(tbl[i].name, "fCrimeSoundBase") == 0)               deadSoundSkipped = true;
                if (std::strcmp(tbl[i].name, "fStaggerBlockAttackBase") == 0)       deadStaggerSkipped = true;
                if (std::strcmp(tbl[i].name, "fCombatShoutShortRecoveryTime") == 0) deadShoutSkipped = true;
            }
        }
        check(valid && liveCount == 20u && deadCount == 5u &&
              hasCrime && hasStagger && hasWater && hasDragon &&
              deadSoundSkipped && deadStaggerSkipped && deadShoutSkipped,
              "table_completeness_and_dead_legacy_gmst_hygiene");
    }

    // 2. Monotonic crime witness weather attenuation & cold-water survival breath + C ABI export:
    {
        CrimeEnvInput clearNoon{};
        clearNoon.windBeaufort = 1.0f;

        CrimeEnvInput rainyNight{};
        rainyNight.rain01          = 0.8f;
        rainyNight.fog01           = 0.4f;
        rainyNight.nightDarkness01 = 0.7f;
        rainyNight.windBeaufort    = 6.0f;

        CrimeEnvInput blizzardMidnight{};
        blizzardMidnight.snow01          = 1.0f;
        blizzardMidnight.fog01           = 0.85f;
        blizzardMidnight.nightDarkness01 = 1.0f;
        blizzardMidnight.windBeaufort    = 10.0f;

        CrimeEnvInput stealthRain = rainyNight;
        stealthRain.isStealthCrime = true;

        const auto wNoon     = EvaluateCrimeWitnessRadius(clearNoon);
        const auto wRain     = EvaluateCrimeWitnessRadius(rainyNight);
        const auto wBlizzard = EvaluateCrimeWitnessRadius(blizzardMidnight);
        const auto wStealth  = EvaluateCrimeWitnessRadius(stealthRain);

        const bool crimeMono = (wNoon.effectiveAlarmRadiusU > wRain.effectiveAlarmRadiusU) &&
                               (wRain.effectiveAlarmRadiusU > wBlizzard.effectiveAlarmRadiusU) &&
                               (wStealth.effectiveAlarmRadiusU < wRain.effectiveAlarmRadiusU) &&
                               (wBlizzard.effectiveAlarmRadiusU >= 320.0f);

        const auto bWarm    = EvaluateWaterSurvivalBreath(18.0f, 100.0f);
        const auto bCool    = EvaluateWaterSurvivalBreath(8.0f,  100.0f);
        const auto bGlacial = EvaluateWaterSurvivalBreath(0.0f,  100.0f);
        const auto bAthlete = EvaluateWaterSurvivalBreath(18.0f, 300.0f);

        const bool waterMono = (bGlacial.breathDurationSec < bCool.breathDurationSec) &&
                               (bCool.breathDurationSec < bWarm.breathDurationSec) &&
                               (bWarm.breathDurationSec < bAthlete.breathDurationSec) &&
                               (bGlacial.coldShockFactor01 > 0.99f) &&
                               (bWarm.coldShockFactor01 == 0.0f) &&
                               (bGlacial.drowningDpsFrac > bWarm.drowningDpsFrac);

        WorldRulesUpdateEnvironment(blizzardMidnight, 0.0f, 100.0f);
        RawWorldRulesState st{};
        const int rcOk   = RAW_GetWorldRulesState(&st);
        const int rcNull = RAW_GetWorldRulesState(nullptr);
        const bool abiOk = (rcOk == 1) && (rcNull == 0) &&
                           (std::fabs(st.effectiveWitnessRadius - wBlizzard.effectiveAlarmRadiusU) < 1e-3f) &&
                           (std::fabs(st.waterBreathSec - bGlacial.breathDurationSec) < 1e-3f) &&
                           (st.murderBountyGold == 2500);
        check(crimeMono && waterMono && abiOk, "crime_witness_cold_water_breath_and_c_abi");
    }

    // 3. Real SkyrimSE.exe 1.7.104.0 PE verification of ALL 25 World-Rules GMST RVAs, names & defaults + planted controls!
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
            const bool realOk = VerifyWorldRulesGmstTable(img, &verifiedCount);

            // Planted fail-closed control #1: corrupt float fDragonLandingZoneClearRadius (0x207BDF0+8) -> MUST reject!
            float savedDragonF = 0.0f;
            std::memcpy(&savedDragonF, mapped.data() + 0x207BDF0u + 8u, 4u);
            const float badFloat = 999.0f;
            std::memcpy(mapped.data() + 0x207BDF0u + 8u, &badFloat, 4u);
            const bool rejectFloat = !VerifyWorldRulesGmstTable(img);
            std::memcpy(mapped.data() + 0x207BDF0u + 8u, &savedDragonF, 4u);

            // Planted fail-closed control #2: corrupt int32 iCrimeGoldMurder (0x20A8D30+8) -> MUST reject!
            const int32_t badInt = 777;
            std::memcpy(mapped.data() + 0x20A8D30u + 8u, &badInt, 4u);
            const bool rejectInt = !VerifyWorldRulesGmstTable(img);

            size_t totalSpecs = 0;
            WorldRulesGmstTable(&totalSpecs);
            check(realOk && verifiedCount == totalSpecs && rejectFloat && rejectInt,
                  "real_skyrim17104_world_rules_gmsts_verified");
        } else {
            check(true, "real_skyrim17104_world_rules_gmsts_skipped_no_exe");
        }
    }

    return fail;
}

}  // namespace raw::rt

#ifndef RAW_WORLD_RULES_GMST_NO_MAIN
int main() {
    const int fail = raw::rt::RunWorldRulesGmstTests();
    std::printf("world_rules_gmst: %s (%d failures)\n", fail == 0 ? "ALL PASS" : "FAILED", fail);
    return fail == 0 ? 0 : 1;
}
#endif
