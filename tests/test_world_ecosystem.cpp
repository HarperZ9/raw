// test_world_ecosystem.cpp: Verification Suite for `raw::worldgen::RawWorldEcosystem`.
// Exports `int raw::worldgen::RunWorldEcosystemTests(void (*checkFn)(bool, const char*) = nullptr)`
// and supports `#ifdef RAW_WORLD_ECOSYSTEM_TEST_STANDALONE` standalone execution.
// Original work, MIT.

#include "RawWorldEcosystem.h"

#include <cmath>
#include <cstdio>

namespace raw::worldgen {
namespace {

bool IsCellFinite(const EcoCell& c) {
    return std::isfinite(c.elevationM) && std::isfinite(c.fuelLoad01) &&
           std::isfinite(c.moisture01) && std::isfinite(c.snowDepthM) &&
           std::isfinite(c.meltWaterM) && std::isfinite(c.blackIce01) &&
           std::isfinite(c.fireIntensity01) && std::isfinite(c.surfaceTempC) &&
           std::isfinite(c.char01);
}

bool IsGridFinite(const RawWorldEcosystem& eco) {
    for (uint32_t y = 0u; y < eco.Height(); ++y) {
        for (uint32_t x = 0u; x < eco.Width(); ++x) {
            if (!IsCellFinite(*eco.CellAt(x, y))) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

int RunWorldEcosystemTests(void (*checkFn)(bool, const char*)) {
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
    // Test 1: Planted-failure controls (zero dt no-op, river water & soaked cells refuse ignition)
    // =========================================================================
    {
        EcoGridConfig cfg{};
        cfg.gridW     = 32u;
        cfg.gridH     = 32u;
        cfg.cellSizeM = 2.0f;
        RawWorldEcosystem eco(cfg);

        // River strip on left half (x in [4..10]):
        for (uint32_t y = 0u; y < 32u; ++y) {
            for (uint32_t x = 4u; x <= 10u; ++x) {
                EcoCell* c = eco.CellAt(x, y);
                c->isRiverWater = true;
                c->fuelLoad01   = 0.8f;
                c->moisture01   = 0.1f;
            }
        }
        // Saturated soaked patch on right half (x in [20..26]):
        for (uint32_t y = 0u; y < 32u; ++y) {
            for (uint32_t x = 20u; x <= 26u; ++x) {
                EcoCell* c = eco.CellAt(x, y);
                c->moisture01 = 0.90f;
                c->fuelLoad01 = 0.8f;
            }
        }

        const Vec2 riverCenter  = eco.CellCenterWorldM(7u, 16u);
        const Vec2 soakedCenter = eco.CellCenterWorldM(23u, 16u);
        const uint32_t riverIgn  = eco.IgniteAt(riverCenter.x, riverCenter.y, 4.0f, 600.0f);
        const uint32_t soakedIgn = eco.IgniteAt(soakedCenter.x, soakedCenter.y, 4.0f, 600.0f);

        // Zero-dt no-op check on a dry ignited cell:
        const Vec2 dryCenter = eco.CellCenterWorldM(15u, 16u);
        const uint32_t dryIgn = eco.IgniteAt(dryCenter.x, dryCenter.y, 2.5f, 500.0f);
        const EcoCell before = *eco.CellAt(15u, 16u);
        EcoWeatherInput wx{};
        wx.rainRate01 = 0.9f;
        eco.Step(0.0f, wx);
        eco.Step(-1.0f, wx);
        const EcoCell after = *eco.CellAt(15u, 16u);
        const bool zeroDtNoOp = (before.fuelLoad01 == after.fuelLoad01) &&
                                (before.fireIntensity01 == after.fireIntensity01) &&
                                (before.surfaceTempC == after.surfaceTempC);

        check(riverIgn == 0u && soakedIgn == 0u && dryIgn >= 1u && zeroDtNoOp,
              "world_ecosystem_planted_failure_controls");
    }

    // =========================================================================
    // Test 2: Downwind & Upslope Wildfire Spread Proof
    // =========================================================================
    {
        EcoGridConfig cfg{};
        cfg.gridW     = 64u;
        cfg.gridH     = 64u;
        cfg.cellSizeM = 2.0f;
        RawWorldEcosystem eco(cfg);

        // Gentle upslope rising toward +X (east) + dry grassy pine needle fuel bed:
        for (uint32_t y = 0u; y < 64u; ++y) {
            for (uint32_t x = 0u; x < 64u; ++x) {
                EcoCell* c = eco.CellAt(x, y);
                c->elevationM = static_cast<float>(x) * 0.45f; // ~12.7 deg upslope east
                c->fuelLoad01 = 0.85f;
                c->moisture01 = 0.12f;
            }
        }

        const uint32_t originGx = 22u;
        const uint32_t originGy = 32u;
        const Vec2 ignPos = eco.CellCenterWorldM(originGx, originGy);
        const uint32_t ignited = eco.IgniteAt(ignPos.x, ignPos.y, 3.0f, 550.0f);

        EcoWeatherInput eastGale{};
        eastGale.ambientTempC      = 24.0f;
        eastGale.windDirX          = 1.0f; // Strong east wind (+X)
        eastGale.windDirY          = 0.0f;
        eastGale.windBeaufort      = 7.0f; // Near gale (~15.5 m/s)
        eastGale.rainRate01        = 0.0f;
        eastGale.solarInsolation01 = 0.85f;

        for (int s = 0; s < 60; ++s) { // 15 seconds simulated
            eco.Step(0.25f, eastGale);
        }

        // Measure farthest east (+X) vs farthest west (-X) burned/burning reach from originGx:
        int32_t maxEastReach = 0;
        int32_t maxWestReach = 0;
        for (uint32_t y = 0u; y < 64u; ++y) {
            for (uint32_t x = 0u; x < 64u; ++x) {
                const EcoCell* c = eco.CellAt(x, y);
                if (c->char01 > 0.10f || c->fireIntensity01 > 0.10f) {
                    const int32_t dx = static_cast<int32_t>(x) - static_cast<int32_t>(originGx);
                    if (dx > maxEastReach) maxEastReach = dx;
                    if (-dx > maxWestReach) maxWestReach = -dx;
                }
            }
        }
        const EcoFieldTelemetry tel = eco.ComputeTelemetry();
        const float originFuelLeft  = eco.CellAt(originGx, originGy)->fuelLoad01;

        std::printf(
            "  [Wildfire East Wind+Upslope] eastReach=%d cells vs westReach=%d cells | "
            "burnedArea=%.1fm2 activeFire=%u peakTemp=%.1fC originFuel=%.2f\n",
            maxEastReach, maxWestReach, tel.burnedAreaM2, tel.activeFireCells, tel.peakTempC, originFuelLeft);

        check(ignited >= 1u && maxEastReach >= maxWestReach + 4 && maxEastReach >= 6 &&
                  tel.burnedAreaM2 > 60.0f && originFuelLeft < 0.55f &&
                  tel.peakTempC > 400.0f && IsGridFinite(eco),
              "world_ecosystem_downwind_upslope_wildfire_spread");
    }

    // =========================================================================
    // Test 3: Heavy Rain Suppression Proof
    // =========================================================================
    {
        EcoGridConfig cfg{};
        cfg.gridW     = 32u;
        cfg.gridH     = 32u;
        cfg.cellSizeM = 2.0f;
        RawWorldEcosystem eco(cfg);

        const Vec2 center = eco.CellCenterWorldM(16u, 16u);
        eco.IgniteAt(center.x, center.y, 5.0f, 600.0f);

        // Let fire establish briefly under dry conditions:
        EcoWeatherInput dry{};
        dry.ambientTempC = 20.0f;
        dry.windBeaufort = 3.0f;
        for (int s = 0; s < 10; ++s) {
            eco.Step(0.25f, dry);
        }
        const uint32_t activeBeforeRain = eco.ComputeTelemetry().activeFireCells;

        // Torrential rainstorm (rainRate01 = 0.90) extinguishes wildfire cleanly:
        EcoWeatherInput storm{};
        storm.ambientTempC      = 9.0f;
        storm.windBeaufort      = 4.0f;
        storm.rainRate01        = 0.90f;
        storm.solarInsolation01 = 0.1f;

        float peakSteam = 0.0f;
        for (int s = 0; s < 40; ++s) { // 10 seconds of torrential rain
            eco.Step(0.25f, storm);
            peakSteam = std::max(peakSteam, eco.ComputeTelemetry().steamFlux);
        }
        const EcoFieldTelemetry afterRain = eco.ComputeTelemetry();

        std::printf("  [Rain Suppression] activeFire %u -> %u | peakSteamFlux=%.1f\n",
                    activeBeforeRain, afterRain.activeFireCells, peakSteam);

        check(activeBeforeRain >= 4u && afterRain.activeFireCells == 0u &&
                  peakSteam > 1.0f && IsGridFinite(eco),
              "world_ecosystem_heavy_rain_suppression");
    }

    // =========================================================================
    // Test 4: Fire-Melts-Snow + Mass-Conserving D8 Downhill Runoff -> Night Black-Ice Refreeze
    // =========================================================================
    {
        EcoGridConfig cfg{};
        cfg.gridW     = 32u;
        cfg.gridH     = 32u;
        cfg.cellSizeM = 2.0f;
        RawWorldEcosystem eco(cfg);

        // Sloped hillside descending from X=28 (high, elev=28m) down to X=4 basin (low, elev=4m):
        for (uint32_t y = 0u; y < 32u; ++y) {
            for (uint32_t x = 0u; x < 32u; ++x) {
                EcoCell* c = eco.CellAt(x, y);
                c->elevationM   = static_cast<float>(x) * 0.9f;
                c->surfaceTempC = -1.0f;
                c->fuelLoad01   = 0.70f;
                c->moisture01   = 0.18f;
                // Uphill snowbank on X in [18..26], dry brush strip right beside it at X in [27..29]:
                if (x >= 18u && x <= 26u && y >= 10u && y <= 22u) {
                    c->snowDepthM = 0.35f;
                }
            }
        }

        const float initialWaterMassM3 = eco.ComputeTelemetry().totalWaterMassM3;
        const float initialSnowVolM3   = eco.ComputeTelemetry().totalSnowVolumeM3;

        // Ignite dry brush at top of snowbank + warm spring afternoon (+9 C) melting snow into downhill runoff:
        const Vec2 brushPos = eco.CellCenterWorldM(28u, 16u);
        eco.IgniteAt(brushPos.x, brushPos.y, 4.0f, 600.0f);

        EcoWeatherInput springMelt{};
        springMelt.ambientTempC      = 9.0f;
        springMelt.windDirX          = -1.0f; // Blow fire heat across snowbank!
        springMelt.windDirY          = 0.0f;
        springMelt.windBeaufort      = 4.0f;
        springMelt.rainRate01        = 0.0f;
        springMelt.snowRate01        = 0.0f;
        springMelt.solarInsolation01 = 0.8f;

        for (int s = 0; s < 60; ++s) { // 15 seconds warm melt + D8 downhill flow
            eco.Step(0.25f, springMelt);
        }

        const EcoFieldTelemetry afterMelt = eco.ComputeTelemetry();
        const float massErrRatio = std::abs(afterMelt.totalWaterMassM3 - initialWaterMassM3) /
                                   std::max(1.0e-4f, initialWaterMassM3);

        // Verify meltwater actually flowed downhill into the lower slope (X < 18):
        float downhillWaterM3 = 0.0f;
        for (uint32_t y = 8u; y <= 24u; ++y) {
            for (uint32_t x = 0u; x < 18u; ++x) {
                downhillWaterM3 += eco.CellAt(x, y)->meltWaterM * (cfg.cellSizeM * cfg.cellSizeM);
            }
        }

        // Nighttime sub-zero plunge (-8 C): standing downhill meltwater refreezes into slick blackIce01 > 0.5!
        EcoWeatherInput freezeNight{};
        freezeNight.ambientTempC      = -8.0f;
        freezeNight.windBeaufort      = 2.0f;
        freezeNight.rainRate01        = 0.0f;
        freezeNight.snowRate01        = 0.0f;
        freezeNight.solarInsolation01 = 0.0f;

        for (int s = 0; s < 120; ++s) { // 30 seconds deep night freeze
            eco.Step(0.25f, freezeNight);
        }

        float peakDownhillBlackIce = 0.0f;
        for (uint32_t y = 8u; y <= 24u; ++y) {
            for (uint32_t x = 0u; x < 18u; ++x) {
                peakDownhillBlackIce = std::max(peakDownhillBlackIce, eco.CellAt(x, y)->blackIce01);
            }
        }
        const EcoFieldTelemetry afterFreeze = eco.ComputeTelemetry();

        std::printf(
            "  [Snowmelt->Runoff->BlackIce] snowVol %.1f->%.1fm3 downhillWater=%.2fm3 "
            "massErr=%.2e peakDownhillBlackIce=%.2f blackIceCells=%u\n",
            initialSnowVolM3, afterMelt.totalSnowVolumeM3, downhillWaterM3,
            massErrRatio, peakDownhillBlackIce, afterFreeze.blackIceCells);

        check(afterMelt.totalSnowVolumeM3 < initialSnowVolM3 * 0.90f &&
                  downhillWaterM3 > 0.5f &&
                  massErrRatio < 1.0e-4f &&
                  peakDownhillBlackIce > 0.50f &&
                  afterFreeze.blackIceCells >= 10u &&
                  IsGridFinite(eco),
              "world_ecosystem_snowmelt_d8_runoff_and_black_ice");
    }

    // =========================================================================
    // Test 5: Seasonal Biomass Recovery (`AdvanceSeasonDays(90)`)
    // =========================================================================
    {
        EcoGridConfig cfg{};
        cfg.gridW     = 16u;
        cfg.gridH     = 16u;
        cfg.cellSizeM = 2.0f;
        RawWorldEcosystem eco(cfg);

        // Severely burned post-fire patch:
        for (uint32_t y = 0u; y < 16u; ++y) {
            for (uint32_t x = 0u; x < 16u; ++x) {
                EcoCell* c = eco.CellAt(x, y);
                c->fuelLoad01 = 0.05f;
                c->char01     = 0.95f;
                c->moisture01 = 0.05f;
            }
        }

        const float fuelBefore = eco.CellAt(8u, 8u)->fuelLoad01;
        const float charBefore = eco.CellAt(8u, 8u)->char01;

        eco.AdvanceSeasonDays(90.0f);

        const float fuelAfter = eco.CellAt(8u, 8u)->fuelLoad01;
        const float charAfter = eco.CellAt(8u, 8u)->char01;

        std::printf("  [Seasonal Recovery 90d] fuel %.2f->%.2f char %.2f->%.2f\n",
                    fuelBefore, fuelAfter, charBefore, charAfter);

        check(fuelAfter > fuelBefore + 0.45f &&
                  charAfter < charBefore * 0.25f &&
                  IsGridFinite(eco),
              "world_ecosystem_seasonal_recovery");
    }

    return failures;
}

}  // namespace raw::worldgen

#ifdef RAW_WORLD_ECOSYSTEM_TEST_STANDALONE
int main() {
    const int failures = raw::worldgen::RunWorldEcosystemTests(nullptr);
    std::printf("RawWorldEcosystem standalone test summary: %s (%d failures)\n",
                failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
#endif
