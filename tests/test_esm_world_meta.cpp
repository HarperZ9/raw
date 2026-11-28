// test_esm_world_meta.cpp: Verification Suite for `raw::worldgen::RawEsmNpcWeatherLoader`.
// Exports `int raw::worldgen::RunEsmWorldMetaTests(void (*checkFn)(bool, const char*) = nullptr)`
// and supports `#ifdef RAW_ESM_META_TEST_STANDALONE` standalone execution.
// Original work, MIT.

#include "RawEsmNpcWeatherLoader.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace raw::worldgen {
namespace {

bool IsFiniteVec3(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool AreWeatherParamsFinite(const EsmPhysicalWeatherParams& p) {
    return std::isfinite(p.beaufortWindScale) && std::isfinite(p.windSpeedMps) &&
           std::isfinite(p.windDirectionRad) && std::isfinite(p.cloudCover01) &&
           std::isfinite(p.rainRate01) && std::isfinite(p.snowRate01) &&
           std::isfinite(p.fogDensity) && std::isfinite(p.thunderIntensity01) &&
           IsFiniteVec3(p.skyTintRgb) && IsFiniteVec3(p.horizonTintRgb) &&
           IsFiniteVec3(p.sunTintRgb) && IsFiniteVec3(p.ambientTintRgb);
}

}  // namespace

int RunEsmWorldMetaTests(void (*checkFn)(bool, const char*)) {
    int failures = 0;
    auto check = [&](bool cond, const char* name) {
        if (!cond) {
            ++failures;
        }
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s %s\n", cond ? "PASS" : "FAIL", name);
        }
    };

    // =========================================================================
    // Test 1: Planted-failure controls (missing file, corrupt header, unknown lookups)
    // =========================================================================
    {
        EsmWorldMetaIndex idx{};
        std::string err;
        const bool missingFailed = !LoadEsmWorldMeta("Z:/definitely_does_not_exist/NonExistent.esm", &idx, &err) &&
                                   !err.empty();

        const uint8_t badMagic[32] = {'N', 'O', 'P', 'E', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        err.clear();
        const bool badMagicFailed = !LoadEsmWorldMetaFromMemory(badMagic, sizeof(badMagic), "bad.esm", &idx, &err) &&
                                    !err.empty();

        const uint8_t truncated[12] = {'T', 'E', 'S', '4'};
        err.clear();
        const bool truncFailed = !LoadEsmWorldMetaFromMemory(truncated, sizeof(truncated), "trunc.esm", &idx, &err);

        const bool unknownNull = (idx.FindWeatherByFormId(0xDEADBEEFu) == nullptr) &&
                                 (idx.FindWeatherByEdid("NotARealWeather") == nullptr) &&
                                 (idx.FindNpcByEdid("NotARealNpc") == nullptr) &&
                                 (idx.FindQuestByEdid("NotARealQuest") == nullptr) &&
                                 (idx.FindClimateByEdid("NotARealClimate") == nullptr) &&
                                 (idx.FindRegionByEdid("NotARealRegion") == nullptr);

        check(missingFailed && badMagicFailed && truncFailed && unknownNull,
              "esm_world_meta_planted_failure_controls");
    }

    // =========================================================================
    // Test 2: Live `Skyrim.esm` Parsing, Iconic Lookups & Monotonic `RawWeatherSim` Bridge
    // =========================================================================
    const std::string skyrimEsm = "H:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data/Skyrim.esm";
    if (std::filesystem::exists(skyrimEsm)) {
        EsmWorldMetaIndex meta{};
        std::string err;
        const bool loaded = LoadEsmWorldMeta(skyrimEsm, &meta, &err);

        std::printf(
            "  [Skyrim.esm Meta] loaded=%d time=%.2fms WTHR=%zu CLMT=%zu NPC_=%zu REGN=%zu QUST=%zu compressed=%u\n",
            loaded ? 1 : 0, meta.loadTimeMs, meta.weathers.size(), meta.climates.size(),
            meta.npcs.size(), meta.regions.size(), meta.quests.size(), meta.compressedSeen);

        const bool countsOk = loaded &&
                              meta.weathers.size() >= 50u &&
                              meta.climates.size() >= 5u &&
                              meta.npcs.size() >= 2000u &&
                              meta.regions.size() >= 20u &&
                              meta.quests.size() >= 200u &&
                              meta.loadTimeMs < 2500.0; // Typically < 100 ms; generous CI ceiling

        check(countsOk, "esm_world_meta_live_skyrim_record_counts");

        // Iconic record lookups:
        const EsmWeatherRecord* wClear     = meta.FindWeatherByEdid("SkyrimClear");
        const EsmWeatherRecord* wCloudy    = meta.FindWeatherByEdid("SkyrimCloudy");
        const EsmWeatherRecord* wStormRain = meta.FindWeatherByEdid("SkyrimStormRain");
        const EsmWeatherRecord* wStormSnow = meta.FindWeatherByEdid("SkyrimStormSnow");
        const EsmWeatherRecord* wFog       = meta.FindWeatherByEdid("SkyrimFog");
        const EsmClimateRecord* cDefault   = meta.FindClimateByEdid("DefaultClimate");
        const EsmNpcRecord*     npcPlayer  = meta.FindNpcByEdid("Player");
        const EsmQuestRecord*   qMq101     = meta.FindQuestByEdid("MQ101");

        const bool iconicFound = (wClear != nullptr) && (wCloudy != nullptr) &&
                                 (wStormRain != nullptr) && (wStormSnow != nullptr) &&
                                 (wFog != nullptr) && (cDefault != nullptr) &&
                                 (npcPlayer != nullptr) && (qMq101 != nullptr);

        bool iconicDataOk = iconicFound;
        if (iconicFound) {
            std::printf("  [Iconic] Clear(flags=0x%02X nam0=%d) Cloudy(flags=0x%02X alpha=%.2f) Rain(flags=0x%02X) Snow(flags=0x%02X) Fog(flags=0x%02X) Clmt(masser=%d secunda=%d wlst=%zu) Player(id=0x%08X race=0x%08X) MQ101(pri=%u flags=0x%04X)\n",
                        wClear->precipFlags, wClear->hasNam0 ? 1 : 0,
                        wCloudy->precipFlags, wCloudy->cloudAlphaDayMean01,
                        wStormRain->precipFlags, wStormSnow->precipFlags, wFog->precipFlags,
                        cDefault->hasMasser ? 1 : 0, cDefault->hasSecunda ? 1 : 0, cDefault->weatherList.size(),
                        npcPlayer->formId, npcPlayer->raceFormId, qMq101->priority, qMq101->flags);
            iconicDataOk = wClear->hasData && wClear->hasNam0 && wClear->IsPleasant() &&
                           wCloudy->hasData && wCloudy->hasNam0 &&
                           wStormRain->hasData && wStormRain->IsRainy() &&
                           wStormSnow->hasData && wStormSnow->IsSnowy() &&
                           wFog->hasData && wFog->IsCloudy() &&
                           (cDefault->hasMasser && cDefault->hasSecunda) &&
                           !cDefault->weatherList.empty() &&
                           (npcPlayer->formId == 0x00000007u) &&
                           (npcPlayer->raceFormId == 0x00013746u) &&
                           (qMq101->priority > 0u);
        }
        check(iconicDataOk, "esm_world_meta_live_skyrim_iconic_lookups");

        // Monotonic physical RawWeatherSim parameter mapping:
        bool mappingOk = false;
        if (iconicFound) {
            const EsmPhysicalWeatherParams pClear  = MapEsmWeatherToPhysicalParams(*wClear, 13.0f);
            const EsmPhysicalWeatherParams pCloudy = MapEsmWeatherToPhysicalParams(*wCloudy, 13.0f);
            const EsmPhysicalWeatherParams pRain   = MapEsmWeatherToPhysicalParams(*wStormRain, 13.0f);
            const EsmPhysicalWeatherParams pSnow   = MapEsmWeatherToPhysicalParams(*wStormSnow, 13.0f);
            const EsmPhysicalWeatherParams pFog    = MapEsmWeatherToPhysicalParams(*wFog, 13.0f);

            std::printf(
                "  [WTHR -> RawWeatherSim] Clear(cloud=%.2f fog=%.2f wind=%.1fm/s) Cloudy(cloud=%.2f) "
                "Rain(rain=%.2f cloud=%.2f) Snow(snow=%.2f cloud=%.2f) Fog(fog=%.2f fogDayFar=%.0fu)\n",
                pClear.cloudCover01, pClear.fogDensity, pClear.windSpeedMps,
                pCloudy.cloudCover01,
                pRain.rainRate01, pRain.cloudCover01,
                pSnow.snowRate01, pSnow.cloudCover01,
                pFog.fogDensity, wFog->fogDayFar);

            // Also verify direct ApplyEsmWeatherToSim integration with live RawWeatherSim:
            RawWeatherSim sim;
            ApplyEsmWeatherToSim(*wStormSnow, sim, 13.0f, true);
            sim.Step(0.5f, 20.0f, -1.0f, {0.0f, 0.0f, 1500.0f});
            const WeatherGpuUniforms& gpuU = sim.GetGpuUniforms();

            mappingOk = AreWeatherParamsFinite(pClear) && AreWeatherParamsFinite(pCloudy) &&
                        AreWeatherParamsFinite(pRain) && AreWeatherParamsFinite(pSnow) &&
                        AreWeatherParamsFinite(pFog) &&
                        (pClear.cloudCover01 < pCloudy.cloudCover01) &&
                        (pCloudy.cloudCover01 <= pRain.cloudCover01) &&
                        (pRain.rainRate01 > 0.5f && pRain.snowRate01 == 0.0f) &&
                        (pSnow.snowRate01 > 0.5f && pSnow.rainRate01 == 0.0f) &&
                        (pFog.fogDensity > pClear.fogDensity) &&
                        (pClear.rainRate01 == 0.0f && pClear.snowRate01 == 0.0f) &&
                        std::isfinite(gpuU.cloudWind.z) && std::isfinite(gpuU.weather.z);
        }
        check(mappingOk, "esm_world_meta_weather_sim_monotonic_mapping");
    } else {
        std::printf("SKIP esm_world_meta_live_skyrim (Skyrim.esm not present)\n");
    }

    return failures;
}

}  // namespace raw::worldgen

#ifdef RAW_ESM_META_TEST_STANDALONE
int main() {
    const int failures = raw::worldgen::RunEsmWorldMetaTests(nullptr);
    std::printf("RawEsmNpcWeatherLoader standalone test summary: %s (%d failures)\n",
                failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
#endif
