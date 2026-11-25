// test_crime_mount_gmst.cpp: Wave 58 verification suite for Binary-Verified 1.7.104.0 Crime/Jail,
// Mounted Camera, Speech/Bribe/Intimidate & Lockpick/Pickpocket GMSTs (rt_crime_mount_gmst.h/.cpp)
// verified directly against the real SkyrimSE.exe 1.7.104.0 PE image (312 total cataloged GMSTs!).
// Compiles standalone (default) OR links into ce_runtime_check with -DRAW_CRIME_MOUNT_GMST_NO_MAIN.
// Original work, MIT.

#include "../src/engine_rt/rt_crime_mount_gmst.h"
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

int RunCrimeMountGmstTests(void (*checkFn)(bool, const char*) = nullptr) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* name) {
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s crime_mount_gmst::%s\n", cond ? "PASS" : "FAIL", name);
        }
        if (cond) ++pass; else ++fail;
    };

    // 1. Table completeness (24 live + 6 dead = 30), uniqueness, category coverage & dead-legacy hygiene:
    {
        size_t n = 0;
        const auto* tbl = CrimeMountGmstTable(&n);
        size_t liveCount = 0, deadCount = 0;
        bool hasCrime = false, hasMount = false, hasResist = false, hasSpeech = false, hasPp = false, hasLock = false;
        bool deadSpeechAvg = false, deadPpWarn = false, deadBarterFact = false;
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
                if (std::strcmp(tbl[i].category, "Crime") == 0)      hasCrime = true;
                if (std::strcmp(tbl[i].category, "Mount") == 0)      hasMount = true;
                if (std::strcmp(tbl[i].category, "Resist") == 0)     hasResist = true;
                if (std::strcmp(tbl[i].category, "Speech") == 0)     hasSpeech = true;
                if (std::strcmp(tbl[i].category, "Pickpocket") == 0) hasPp = true;
                if (std::strcmp(tbl[i].category, "Lockpick") == 0)   hasLock = true;
            } else {
                ++deadCount;
                if (std::strcmp(tbl[i].name, "iSpeechChallengeDifficultyAverage") == 0) deadSpeechAvg = true;
                if (std::strcmp(tbl[i].name, "iPickPocketWarnings") == 0)               deadPpWarn = true;
                if (std::strcmp(tbl[i].name, "fMagicDefaultCEBarterFactor") == 0)       deadBarterFact = true;
            }
        }
        check(valid && liveCount == 24u && deadCount == 6u &&
              hasCrime && hasMount && hasResist && hasSpeech && hasPp && hasLock &&
              deadSpeechAvg && deadPpWarn && deadBarterFact,
              "table_completeness_uniqueness_and_dead_legacy_gmst_hygiene");
    }

    // 2. Monotonic jail sentence & arrest thresholds, bribe/intimidate leverage & C ABI export:
    {
        const auto jZero   = EvaluateJailSentenceDays(0u,    false);
        const auto jPetty  = EvaluateJailSentenceDays(100u,  false);
        const auto jMurder = EvaluateJailSentenceDays(2500u, true);
        const bool jailOk = (jZero.sentenceDays == 0 && !jZero.arrestOnSight) &&
                            (jPetty.sentenceDays >= 1 && !jPetty.arrestOnSight) &&
                            (jMurder.sentenceDays == 14 && jMurder.arrestOnSight && jMurder.guardSquadSize == 5);

        const auto sLowSpeech  = EvaluateBribeAndIntimidate(20.0f, 15.0f, 0.8f, 1.5f);
        const auto sHighSpeech = EvaluateBribeAndIntimidate(20.0f, 90.0f, 0.8f, 1.5f);
        const auto sCoward     = EvaluateBribeAndIntimidate(20.0f, 50.0f, 0.2f, 0.5f);
        const auto sBrave      = EvaluateBribeAndIntimidate(20.0f, 50.0f, 0.8f, 1.65f);
        const bool socialOk = (sHighSpeech.bribeGoldCost < sLowSpeech.bribeGoldCost) &&
                              (sHighSpeech.intimidateSuccess01 > sLowSpeech.intimidateSuccess01) &&
                              (sCoward.bribeGoldCost < sBrave.bribeGoldCost) &&
                              (sCoward.intimidateSuccess01 > sBrave.intimidateSuccess01);

        RawCrimeMountState st{};
        const int rcOk   = RAW_GetCrimeMountState(&st);
        const int rcNull = RAW_GetCrimeMountState(nullptr);
        const bool abiOk = (rcOk == 1) && (rcNull == 0) &&
                           (st.guardResponseCount == 5) &&
                           (st.maxJailDays == 14) &&
                           (std::fabs(st.playerMaxResistancePct - 85.0f) < 1e-3f);
        check(jailOk && socialOk && abiOk, "jail_sentence_social_leverage_and_c_abi");
    }

    // 3. Real SkyrimSE.exe 1.7.104.0 PE verification of ALL 30 Crime/Mount GMST RVAs, names & defaults + planted controls!
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
            const bool realOk = VerifyCrimeMountGmstTable(img, &verifiedCount);

            // Planted fail-closed control #1: corrupt float fPlayerMaxResistance (0x20A9AE0+8) -> MUST reject!
            float savedRes = 0.0f;
            std::memcpy(&savedRes, mapped.data() + 0x20A9AE0u + 8u, 4u);
            const float badFloat = 100.0f;
            std::memcpy(mapped.data() + 0x20A9AE0u + 8u, &badFloat, 4u);
            const bool rejectFloat = !VerifyCrimeMountGmstTable(img);
            std::memcpy(mapped.data() + 0x20A9AE0u + 8u, &savedRes, 4u);

            // Planted fail-closed control #2: corrupt int32 iNumberGuardsCrimeResponse (0x209FD98+8) -> MUST reject!
            const int32_t badInt = 99;
            std::memcpy(mapped.data() + 0x209FD98u + 8u, &badInt, 4u);
            const bool rejectInt = !VerifyCrimeMountGmstTable(img);

            size_t totalSpecs = 0;
            CrimeMountGmstTable(&totalSpecs);
            check(realOk && verifiedCount == totalSpecs && rejectFloat && rejectInt,
                  "real_skyrim17104_crime_mount_gmsts_verified");
        } else {
            check(true, "real_skyrim17104_crime_mount_gmsts_skipped_no_exe");
        }
    }

    return fail;
}

}  // namespace raw::rt

int run_crime_mount_gmst_tests() {
    return raw::rt::RunCrimeMountGmstTests(nullptr);
}

#ifndef RAW_CRIME_MOUNT_GMST_NO_MAIN
int main() {
    const int fail = run_crime_mount_gmst_tests();
    std::printf("crime_mount_gmst: %s (%d failures)\n", fail == 0 ? "ALL PASS" : "FAILED", fail);
    return fail == 0 ? 0 : 1;
}
#endif
