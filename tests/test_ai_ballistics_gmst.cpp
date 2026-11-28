// test_ai_ballistics_gmst.cpp: Milestone Wave 62 verification suite for Binary-Verified 1.7.104.0 AI Detection,
// Acoustic Sound, Ragdoll/Knockback Impulse, Projectile Ballistics & AI Melee GMSTs (rt_ai_ballistics_gmst.h/.cpp)
// verified directly against the real SkyrimSE.exe 1.7.104.0 PE image (432 total unique cataloged GMSTs!).
// Compiles standalone (default) OR links into ce_runtime_check with -DRAW_AI_BALLISTICS_GMST_NO_MAIN.
// Original work, MIT.

#include "../src/engine_rt/rt_image.h"
#include "../src/engine_rt/rt_ai_ballistics_gmst.h"

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

int RunAiBallisticsGmstTests(void (*checkFn)(bool, const char*) = nullptr) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* name) {
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s ai_ballistics_gmst::%s\n", cond ? "PASS" : "FAIL", name);
        }
        if (cond) ++pass; else ++fail;
    };

    // 1. Table completeness (24 live + 6 dead = 30), uniqueness, category coverage & dead-legacy hygiene:
    {
        size_t n = 0;
        const auto* tbl = AiBallisticsGmstTable(&n);
        size_t liveCount = 0, deadCount = 0;
        bool hasDet = false, hasSnd = false, hasRag = false, hasBal = false, hasMel = false, hasStg = false;
        bool deadProjDist = false, deadCollScale = false, deadDetPicks = false;
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
                if (std::strcmp(tbl[i].category, "Detection") == 0)  hasDet = true;
                if (std::strcmp(tbl[i].category, "Sound") == 0)      hasSnd = true;
                if (std::strcmp(tbl[i].category, "Ragdoll") == 0)    hasRag = true;
                if (std::strcmp(tbl[i].category, "Ballistics") == 0) hasBal = true;
                if (std::strcmp(tbl[i].category, "AIMelee") == 0)    hasMel = true;
                if (std::strcmp(tbl[i].category, "Stagger") == 0)    hasStg = true;
            } else {
                ++deadCount;
                if (std::strcmp(tbl[i].name, "fProjectileMaxDistance") == 0)           deadProjDist = true;
                if (std::strcmp(tbl[i].name, "fProjectileCollisionImpulseScale") == 0) deadCollScale = true;
                if (std::strcmp(tbl[i].name, "iDetectionHighNumPicks") == 0)           deadDetPicks = true;
            }
        }
        check(valid && liveCount == 24u && deadCount == 6u &&
              hasDet && hasSnd && hasRag && hasBal && hasMel && hasStg &&
              deadProjDist && deadCollScale && deadDetPicks,
              "table_completeness_uniqueness_and_dead_legacy_gmst_hygiene");
    }

    // 2. Acoustic detection cone, ballistic ragdoll impulse & C ABI export:
    {
        const auto dLoudClose = EvaluateAcousticDetectionScore(210.0f, 400.0f, 10.0f, true);
        const auto dQuietBack = EvaluateAcousticDetectionScore(50.0f, 1200.0f, 120.0f, true);
        const auto dIntSight  = EvaluateAcousticDetectionScore(0.0f, 900.0f, 5.0f, false);
        const auto dExtSight  = EvaluateAcousticDetectionScore(0.0f, 900.0f, 5.0f, true);
        const bool detOk = dLoudClose.detected && !dQuietBack.detected &&
                           (dLoudClose.soundScore01 > dQuietBack.soundScore01) &&
                           (dIntSight.losVisibility01 > dExtSight.losVisibility01);

        const auto iFastBiped   = EvaluateBallisticImpactImpulse(2400.0f, 0.25f, 70.0f, true);
        const auto iFastClutter = EvaluateBallisticImpactImpulse(2400.0f, 0.25f, 8.0f, false);
        const auto iSlow        = EvaluateBallisticImpactImpulse(600.0f, 0.10f, 70.0f, true);
        const bool impOk = iFastBiped.trailActive && !iSlow.trailActive &&
                           (iFastClutter.appliedImpulse > iFastBiped.appliedImpulse) &&
                           (iFastClutter.appliedImpulse <= 480.0f);

        RawAiBallisticsState st{};
        const int rcOk   = RAW_GetAiBallisticsState(&st);
        const int rcNull = RAW_GetAiBallisticsState(nullptr);
        const bool abiOk = (rcOk == 1) && (rcNull == 0) &&
                           (std::fabs(st.detectionLosAngleDeg - 56.0f) < 1e-4f) &&
                           (std::fabs(st.explosionMaxImpulse - 6400.0f) < 1e-4f) &&
                           (st.sneakDetectThreshold == 65) &&
                           (st.ballisticPickSegments == 6);
        check(detOk && impOk && abiOk, "acoustic_detection_ballistic_impulse_and_c_abi");
    }

    // 3. Real SkyrimSE.exe 1.7.104.0 PE verification of ALL 30 AI/Ballistics GMST RVAs, names & defaults + planted controls!
    {
        std::ifstream f(R"(H:\SteamLibrary\steamapps\common\Skyrim Special Edition\SkyrimSE.exe)", std::ios::binary);
        if (f) {
            std::vector<uint8_t> pe((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(pe.data());
            const auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS64*>(pe.data() + dos->e_lfanew);
            std::vector<uint8_t> mapped(nt->OptionalHeader.SizeOfImage, uint8_t{0});
            std::memcpy(mapped.data(), pe.data(), (std::min)(pe.size(), static_cast<size_t>(nt->OptionalHeader.SizeOfHeaders)));
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
            const bool realOk = VerifyAiBallisticsGmstTable(img, &verifiedCount);

            // Planted fail-closed control #1: corrupt float fExplosionMaxImpulse (0x20A9D08+8) -> MUST reject!
            float savedImp = 0.0f;
            std::memcpy(&savedImp, mapped.data() + 0x20A9D08u + 8u, 4u);
            const float badFloat = 99999.0f;
            std::memcpy(mapped.data() + 0x20A9D08u + 8u, &badFloat, 4u);
            const bool rejectFloat = !VerifyAiBallisticsGmstTable(img);
            std::memcpy(mapped.data() + 0x20A9D08u + 8u, &savedImp, 4u);

            // Planted fail-closed control #2: corrupt int32 iSoundLevelVeryLoud (0x209EF28+8) -> MUST reject!
            const int32_t badInt = 999;
            std::memcpy(mapped.data() + 0x209EF28u + 8u, &badInt, 4u);
            const bool rejectInt = !VerifyAiBallisticsGmstTable(img);

            size_t totalSpecs = 0;
            AiBallisticsGmstTable(&totalSpecs);
            check(realOk && verifiedCount == totalSpecs && rejectFloat && rejectInt,
                  "real_skyrim17104_ai_ballistics_gmsts_verified");
        } else {
            check(true, "real_skyrim17104_ai_ballistics_gmsts_skipped_no_exe");
        }
    }

    return fail;
}

}  // namespace raw::rt

int run_ai_ballistics_gmst_tests() {
    return raw::rt::RunAiBallisticsGmstTests(nullptr);
}

#ifndef RAW_AI_BALLISTICS_GMST_NO_MAIN
int main() {
    const int fail = run_ai_ballistics_gmst_tests();
    std::printf("ai_ballistics_gmst: %s (%d failures)\n", fail == 0 ? "ALL PASS" : "FAILED", fail);
    return fail == 0 ? 0 : 1;
}
#endif
