// test_archery_mount_gmst.cpp: Wave 56 verification suite for Binary-Verified 1.7.104.0 Bow Draw,
// Archery Kinematics, Mounted Combat & HUD Compass GMSTs (rt_archery_mount_gmst.h/.cpp) verified
// directly against the real SkyrimSE.exe 1.7.104.0 PE image!
// Compiles standalone (default) OR links into ce_runtime_check with -DRAW_ARCHERY_MOUNT_GMST_NO_MAIN.
// Original work, MIT.

#include "../src/engine_rt/rt_archery_mount_gmst.h"
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

int RunArcheryMountGmstTests(void (*checkFn)(bool, const char*) = nullptr) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* name) {
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s archery_mount_gmst::%s\n", cond ? "PASS" : "FAIL", name);
        }
        if (cond) ++pass; else ++fail;
    };

    // 1. Table completeness, uniqueness, category coverage & 0-reader legacy GMST hygiene:
    {
        size_t n = 0;
        const auto* tbl = ArcheryMountGmstTable(&n);
        size_t liveCount = 0, deadCount = 0;
        bool hasArchery = false, hasRanged = false, hasMount = false, hasHud = false;
        bool deadOptSkipped = false, deadHoldSkipped = false, deadTurnSkipped = false;
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
                if (std::strcmp(tbl[i].category, "Archery") == 0)  hasArchery = true;
                if (std::strcmp(tbl[i].category, "RangedAI") == 0) hasRanged = true;
                if (std::strcmp(tbl[i].category, "Mount") == 0)    hasMount = true;
                if (std::strcmp(tbl[i].category, "HUD") == 0)      hasHud = true;
            } else {
                ++deadCount;
                if (std::strcmp(tbl[i].name, "fArrowOptimalDistance") == 0)      deadOptSkipped = true;
                if (std::strcmp(tbl[i].name, "fBowHoldTimer") == 0)              deadHoldSkipped = true;
                if (std::strcmp(tbl[i].name, "iHorseTurnDegreesPerSecond") == 0) deadTurnSkipped = true;
            }
        }
        check(valid && liveCount == 20u && deadCount == 6u &&
              hasArchery && hasRanged && hasMount && hasHud &&
              deadOptSkipped && deadHoldSkipped && deadTurnSkipped,
              "table_completeness_uniqueness_and_dead_legacy_gmst_hygiene");
    }

    // 2. Monotonic bow draw power curve, over-hold stamina drain, weather-attenuated compass scale & C ABI:
    {
        const auto dTap  = EvaluateBowDrawPower(0.15f, 1.65f, false);
        const auto dHalf = EvaluateBowDrawPower(0.80f, 1.65f, false);
        const auto dFull = EvaluateBowDrawPower(1.65f, 1.65f, false);
        const auto dHold = EvaluateBowDrawPower(3.50f, 1.65f, false);
        const auto dZoom = EvaluateBowDrawPower(1.65f, 1.65f, true);

        const bool bowOk = (dTap.releasePower >= 0.28f && dTap.releasePower < dHalf.releasePower) &&
                           (dHalf.releasePower < dFull.releasePower) &&
                           (std::fabs(dFull.releasePower - 1.0f) < 1e-4f) &&
                           (dFull.staminaDrainPerSec == 0.0f) &&
                           (dHold.isOverHeld && dHold.staminaDrainPerSec > 0.0f) &&
                           (dZoom.staminaDrainPerSec >= 12.5f);

        const auto cNearClear = EvaluateCompassMarkerScale(1200.0f, 9600.0f, 0.0f, 0.0f);
        const auto cFarClear  = EvaluateCompassMarkerScale(7000.0f, 9600.0f, 0.0f, 0.0f);
        const auto cFarBliz   = EvaluateCompassMarkerScale(7000.0f, 9600.0f, 0.6f, 0.9f);

        const bool compOk = cNearClear.isVisible && cFarClear.isVisible &&
                            (cNearClear.iconScale > cFarClear.iconScale) &&
                            (cFarClear.iconScale >= 0.55f && cNearClear.iconScale <= 1.0f) &&
                            (!cFarBliz.isVisible && cFarBliz.effectiveMaxDistU < cFarClear.effectiveMaxDistU);

        RawArcheryMountState st{};
        const int rcOk   = RAW_GetArcheryMountState(&st);
        const int rcNull = RAW_GetArcheryMountState(nullptr);
        const bool abiOk = (rcOk == 1) && (rcNull == 0) &&
                           (std::fabs(st.arrowMinPower - 0.28f) < 1e-4f) &&
                           (std::fabs(st.horseMaxUpwardPitchDeg - 42.0f) < 1e-3f) &&
                           (st.maxAttachedArrows == 6);
        check(bowOk && compOk && abiOk, "bow_draw_power_compass_scale_and_c_abi");
    }

    // 3. Real SkyrimSE.exe 1.7.104.0 PE verification of ALL 26 Archery/Mount GMST RVAs, names & defaults + planted controls!
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
            const bool realOk = VerifyArcheryMountGmstTable(img, &verifiedCount);

            // Planted fail-closed control #1: corrupt float fHUDCompassLocationMaxDist (0x20ADD88+8) -> MUST reject!
            float savedHud = 0.0f;
            std::memcpy(&savedHud, mapped.data() + 0x20ADD88u + 8u, 4u);
            const float badFloat = 123.0f;
            std::memcpy(mapped.data() + 0x20ADD88u + 8u, &badFloat, 4u);
            const bool rejectFloat = !VerifyArcheryMountGmstTable(img);
            std::memcpy(mapped.data() + 0x20ADD88u + 8u, &savedHud, 4u);

            // Planted fail-closed control #2: corrupt int32 iMaxAttachedArrows (0x2077088+8) -> MUST reject!
            const int32_t badInt = 999;
            std::memcpy(mapped.data() + 0x2077088u + 8u, &badInt, 4u);
            const bool rejectInt = !VerifyArcheryMountGmstTable(img);

            size_t totalSpecs = 0;
            ArcheryMountGmstTable(&totalSpecs);
            check(realOk && verifiedCount == totalSpecs && rejectFloat && rejectInt,
                  "real_skyrim17104_archery_mount_gmsts_verified");
        } else {
            check(true, "real_skyrim17104_archery_mount_gmsts_skipped_no_exe");
        }
    }

    return fail;
}

}  // namespace raw::rt

#ifndef RAW_ARCHERY_MOUNT_GMST_NO_MAIN
int main() {
    const int fail = raw::rt::RunArcheryMountGmstTests();
    std::printf("archery_mount_gmst: %s (%d failures)\n", fail == 0 ? "ALL PASS" : "FAILED", fail);
    return fail == 0 ? 0 : 1;
}
#endif
