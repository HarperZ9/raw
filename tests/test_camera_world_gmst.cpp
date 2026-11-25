// test_camera_world_gmst.cpp: Milestone Wave 61 verification suite for Binary-Verified 1.7.104.0 Camera Shake,
// KillCam, Dragon Flight/Orbit/Landing, Weather, Celestial & Water Physics GMSTs (rt_camera_world_gmst.h/.cpp)
// verified directly against the real SkyrimSE.exe 1.7.104.0 PE image (402 total unique cataloged GMSTs!).
// Compiles standalone (default) OR links into ce_runtime_check with -DRAW_CAMERA_WORLD_GMST_NO_MAIN.
// Original work, MIT.

#include "../src/engine_rt/rt_image.h"
#include "../src/engine_rt/rt_camera_world_gmst.h"

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

int RunCameraWorldGmstTests(void (*checkFn)(bool, const char*) = nullptr) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* name) {
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s camera_world_gmst::%s\n", cond ? "PASS" : "FAIL", name);
        }
        if (cond) ++pass; else ++fail;
    };

    // 1. Table completeness (24 live + 6 dead = 30), uniqueness, category coverage & dead-legacy hygiene:
    {
        size_t n = 0;
        const auto* tbl = CameraWorldGmstTable(&n);
        size_t liveCount = 0, deadCount = 0;
        bool hasCam = false, hasKill = false, hasDragon = false, hasWeath = false, hasCel = false, hasWater = false;
        bool deadGunShell = false, deadVatsZoom = false, deadSnowNoise = false;
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
                if (std::strcmp(tbl[i].category, "Camera") == 0)       hasCam = true;
                if (std::strcmp(tbl[i].category, "KillCam") == 0)      hasKill = true;
                if (std::strcmp(tbl[i].category, "DragonFlight") == 0) hasDragon = true;
                if (std::strcmp(tbl[i].category, "Weather") == 0)      hasWeath = true;
                if (std::strcmp(tbl[i].category, "Celestial") == 0)    hasCel = true;
                if (std::strcmp(tbl[i].category, "Water") == 0)        hasWater = true;
            } else {
                ++deadCount;
                if (std::strcmp(tbl[i].name, "fGunShellCameraDistance") == 0) deadGunShell = true;
                if (std::strcmp(tbl[i].name, "fVATSCamZoomInTime") == 0)      deadVatsZoom = true;
                if (std::strcmp(tbl[i].name, "iSnowNoiseTextureSize") == 0)   deadSnowNoise = true;
            }
        }
        check(valid && liveCount == 24u && deadCount == 6u &&
              hasCam && hasKill && hasDragon && hasWeath && hasCel && hasWater &&
              deadGunShell && deadVatsZoom && deadSnowNoise,
              "table_completeness_uniqueness_and_dead_legacy_gmst_hygiene");
    }

    // 2. Camera spring-zoom, weather celestial visibility & C ABI export:
    {
        const auto zNear = EvaluateCameraSpringZoom(150.0f, 300.0f, 0.05f, 800.0f);
        const auto zMid  = EvaluateCameraSpringZoom(150.0f, 300.0f, 0.05f, 2600.0f);
        const auto zFar  = EvaluateCameraSpringZoom(150.0f, 300.0f, 0.05f, 4000.0f);
        const bool camOk = (zNear.newDistU > 150.0f && zNear.newDistU < 300.0f) &&
                           (zNear.velocityU > 0.0f) &&
                           (std::fabs(zNear.shakeAttenuation - 1.0f) < 1e-4f) &&
                           (std::fabs(zMid.shakeAttenuation - 0.5f) < 1e-4f) &&
                           (zFar.shakeAttenuation == 0.0f);

        const auto wClear = EvaluateWeatherCelestialVisibility(0.0f, 0.0f, 0.1f, 45.0f);
        const auto wStorm = EvaluateWeatherCelestialVisibility(0.9f, 0.8f, 0.9f, 45.0f);
        const auto wNight = EvaluateWeatherCelestialVisibility(0.0f, 0.0f, 0.0f, -20.0f);
        const bool wOk = (wClear.skyVisibility01 > wStorm.skyVisibility01) &&
                         (wClear.sunGlareScale > wStorm.sunGlareScale) &&
                         (wStorm.precipWindSlant > wClear.precipWindSlant) &&
                         (std::fabs(wNight.sunGlareScale - 0.20f) < 1e-4f);

        RawCameraWorldState st{};
        const int rcOk   = RAW_GetCameraWorldState(&st);
        const int rcNull = RAW_GetCameraWorldState(nullptr);
        const bool abiOk = (rcOk == 1) && (rcNull == 0) &&
                           (std::fabs(st.cameraShakeTimeSec - 0.95f) < 1e-4f) &&
                           (std::fabs(st.dragonOrbitDistU - 4800.0f) < 1e-4f) &&
                           (st.killCamLevelOffset == 4) &&
                           (st.masserAngularSize == 94);
        check(camOk && wOk && abiOk, "camera_spring_weather_visibility_and_c_abi");
    }

    // 3. Real SkyrimSE.exe 1.7.104.0 PE verification of ALL 30 Camera/World GMST RVAs, names & defaults + planted controls!
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
            const bool realOk = VerifyCameraWorldGmstTable(img, &verifiedCount);

            // Planted fail-closed control #1: corrupt float fCombatOrbitDistance (0x20A2768+8) -> MUST reject!
            float savedOrbit = 0.0f;
            std::memcpy(&savedOrbit, mapped.data() + 0x20A2768u + 8u, 4u);
            const float badFloat = 12345.0f;
            std::memcpy(mapped.data() + 0x20A2768u + 8u, &badFloat, 4u);
            const bool rejectFloat = !VerifyCameraWorldGmstTable(img);
            std::memcpy(mapped.data() + 0x20A2768u + 8u, &savedOrbit, 4u);

            // Planted fail-closed control #2: corrupt int32 iMasserSize (0x209E658+8) -> MUST reject!
            const int32_t badInt = 777;
            std::memcpy(mapped.data() + 0x209E658u + 8u, &badInt, 4u);
            const bool rejectInt = !VerifyCameraWorldGmstTable(img);

            size_t totalSpecs = 0;
            CameraWorldGmstTable(&totalSpecs);
            check(realOk && verifiedCount == totalSpecs && rejectFloat && rejectInt,
                  "real_skyrim17104_camera_world_gmsts_verified");
        } else {
            check(true, "real_skyrim17104_camera_world_gmsts_skipped_no_exe");
        }
    }

    return fail;
}

}  // namespace raw::rt

int run_camera_world_gmst_tests() {
    return raw::rt::RunCameraWorldGmstTests(nullptr);
}

#ifndef RAW_CAMERA_WORLD_GMST_NO_MAIN
int main() {
    const int fail = run_camera_world_gmst_tests();
    std::printf("camera_world_gmst: %s (%d failures)\n", fail == 0 ? "ALL PASS" : "FAILED", fail);
    return fail == 0 ? 0 : 1;
}
#endif
