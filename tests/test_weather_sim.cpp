// test_weather_sim.cpp: Comprehensive verification suite for RawWeatherSim:
// mesoscale weather regimes, orbital seasons & timescale, surface thermal energy balance,
// snowpack metamorphism & fire melt rings, freezing-degree-day ice growth/thaw, inversion burn-off,
// turbulent dispersion, and seasonal botanical phenology.
// Compiles standalone (default) OR links into ce_runtime_check with -DRAW_WEATHER_SIM_NO_MAIN.
// Original work, MIT.

#include "../src/worldgen/RawWeatherSim.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace raw::worldgen {
namespace {

bool IsFiniteVec2(Vec2 v) { return std::isfinite(v.x) && std::isfinite(v.y); }
bool IsFiniteVec3(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool IsFiniteVec4(Vec4 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::isfinite(v.w); }

bool IsFiniteGpu(const WeatherGpuUniforms& g) {
    return IsFiniteVec4(g.weather) && IsFiniteVec4(g.timeEnv) && IsFiniteVec4(g.cloudWind)
        && IsFiniteVec2(g.seaLevelAndTemp) && IsFiniteVec4(g.seasonThermal0) && IsFiniteVec4(g.seasonThermal1)
        && IsFiniteVec4(g.phenology0) && IsFiniteVec4(g.phenology1) && IsFiniteVec3(g.fireSmokeDeflectVec)
        && std::isfinite(g.beaufortScale) && std::isfinite(g.snowAccumRateMps)
        && std::isfinite(g.snowpackDepthM) && std::isfinite(g.lightningCharge01)
        && std::isfinite(g.lightningFlash01) && std::isfinite(g.zLclU)
        && std::isfinite(g.valleyInversionDepthU) && std::isfinite(g.barometricPressureHpa)
        && std::isfinite(g.auroraIntensity01) && std::isfinite(g.relativeHumidity01)
        && std::isfinite(g.dewPointC);
}

bool IsFiniteSample(const LocalAtmosphereSample& s) {
    return std::isfinite(s.altitudeMeters) && std::isfinite(s.pressureHpa)
        && std::isfinite(s.temperatureC) && std::isfinite(s.dewPointC)
        && std::isfinite(s.relativeHumidity01) && std::isfinite(s.lclAltitudeMeters)
        && std::isfinite(s.zLclU) && std::isfinite(s.orographicFog01)
        && std::isfinite(s.valleyMist01) && std::isfinite(s.coastalSeaFog01)
        && std::isfinite(s.precipIntensity01) && IsFiniteVec3(s.localWindMps);
}

} // namespace

int RunWeatherSimTests() {
    int pass = 0;
    int fail = 0;
    auto check = [&](bool cond, const char* name) {
        if (cond) {
            ++pass;
            std::printf("PASS weather_sim::%s\n", name);
        } else {
            ++fail;
            std::printf("FAIL weather_sim::%s\n", name);
        }
    };

    // 1. All 9 WeatherRegime presets produce finite, physically bounded uniforms and distinct names:
    {
        bool allOk = true;
        for (uint8_t r = 0; r < static_cast<uint8_t>(WeatherRegime::Count); ++r) {
            WeatherSimConfig cfg{};
            cfg.initialRegime  = static_cast<WeatherRegime>(r);
            cfg.timeOfDayHours = (r == static_cast<uint8_t>(WeatherRegime::ArcticClearAurora)) ? 23.0f : 7.0f;
            cfg.initialDayOfYear = (r == static_cast<uint8_t>(WeatherRegime::ArcticClearAurora)) ? 15.0f : 172.0f;
            RawWeatherSim sim(cfg);
            for (int i = 0; i < 30; ++i) {
                sim.Step(0.5f, Vec3{1000.0f, 2000.0f, cfg.seaLevelZ + 7000.0f});
            }
            const auto& g = sim.GetGpuUniforms();
            const auto  s = sim.SampleAt(Vec3{1000.0f, 2000.0f, cfg.seaLevelZ + 7000.0f});
            if (!IsFiniteGpu(g) || !IsFiniteSample(s) || std::strcmp(RawWeatherSim::GetRegimeName(cfg.initialRegime), "Unknown") == 0) {
                allOk = false;
            }
            if (g.barometricPressureHpa < 950.0f || g.barometricPressureHpa > 1050.0f) { allOk = false; }
            if (g.beaufortScale < 0.0f || g.beaufortScale > 12.0f) { allOk = false; }
        }
        check(allOk, "all_9_regimes_valid_and_bounded");
    }

    // 2. Adiabatic lapse rate & barometric pressure strict monotonicity across elevation (0m -> 4000m):
    {
        WeatherSimConfig cfg{};
        cfg.initialRegime = WeatherRegime::ClearAlpine;
        cfg.baseSeaTempC  = 12.0f;
        RawWeatherSim sim(cfg);
        const float altsM[5] = {0.0f, 500.0f, 1200.0f, 2500.0f, 4000.0f};
        bool tempMono = true;
        bool presMono = true;
        float prevT = 1.0e9f;
        float prevP = 1.0e9f;
        for (float am : altsM) {
            const auto s = sim.SampleAt(Vec3{0.0f, 0.0f, cfg.seaLevelZ + am * cfg.unitsPerMeter});
            if (!(s.temperatureC < prevT)) { tempMono = false; }
            if (!(s.pressureHpa < prevP))  { presMono = false; }
            prevT = s.temperatureC;
            prevP = s.pressureHpa;
        }
        check(tempMono, "adiabatic_lapse_temperature_strictly_decreasing");
        check(presMono, "hypsometric_pressure_strictly_decreasing");
    }

    // 3. LCL condensation physics (Magnus dew point & Espy LCL & orographic fog band):
    {
        const float tdDry = RawWeatherSim::ComputeDewPointC(15.0f, 0.35f);
        const float tdHum = RawWeatherSim::ComputeDewPointC(15.0f, 0.90f);
        const float tdSat = RawWeatherSim::ComputeDewPointC(15.0f, 1.00f);
        const float lclDry = RawWeatherSim::ComputeLclMeters(15.0f, tdDry);
        const float lclHum = RawWeatherSim::ComputeLclMeters(15.0f, tdHum);
        const float lclSat = RawWeatherSim::ComputeLclMeters(15.0f, tdSat);
        check(tdDry < tdHum && tdHum < tdSat + 1.0e-4f, "magnus_dew_point_monotonic_with_rh");
        check(lclDry > lclHum && lclHum > lclSat && lclSat <= 1.0f, "espy_lcl_decreases_with_humidity");

        WeatherSimConfig cfg{};
        cfg.initialRegime = WeatherRegime::MountainFogCloudCap;
        RawWeatherSim sim(cfg);
        const auto sBase  = sim.SampleAt(Vec3{0.0f, 0.0f, cfg.seaLevelZ});
        const auto sAtLcl = sim.SampleAt(Vec3{0.0f, 0.0f, sBase.zLclU + 140.0f * cfg.unitsPerMeter});
        check(sAtLcl.orographicFog01 > sBase.orographicFog01 + 0.15f && sAtLcl.orographicFog01 > 0.35f,
              "orographic_mountain_fog_peaks_at_lcl_band");
    }

    // 4. Snow/Rain precipitation phase transition with altitude:
    {
        WeatherSimConfig cfg{};
        cfg.initialRegime  = WeatherRegime::Rainstorm;
        cfg.baseSeaTempC   = 9.0f;
        cfg.timeOfDayHours = 14.0f;
        cfg.initialDayOfYear = 172.0f;
        RawWeatherSim sim(cfg);
        const auto sSea  = sim.SampleAt(Vec3{0.0f, 0.0f, cfg.seaLevelZ + 20.0f * cfg.unitsPerMeter});
        const auto sPeak = sim.SampleAt(Vec3{0.0f, 0.0f, cfg.seaLevelZ + 3200.0f * cfg.unitsPerMeter});
        check(sSea.precipPhase == PrecipPhase::Rain, "warm_sea_level_precip_is_rain");
        check(sPeak.precipPhase == PrecipPhase::DryPowderBlizzard || sPeak.precipPhase == PrecipPhase::WetSnow,
              "high_alpine_peak_precip_transitions_to_snow");
    }

    // 5. Dawn valley inversion mist & coastal sea-fog stratification:
    {
        WeatherSimConfig cfg{};
        cfg.initialRegime  = WeatherRegime::DawnValleyMist;
        cfg.timeOfDayHours = 6.2f;
        cfg.initialDayOfYear = 95.0f;
        RawWeatherSim sim(cfg);
        const auto sValley = sim.SampleAt(Vec3{0.0f, 0.0f, cfg.seaLevelZ + 20.0f * cfg.unitsPerMeter});
        const auto sRidge  = sim.SampleAt(Vec3{0.0f, 0.0f, cfg.seaLevelZ + 1200.0f * cfg.unitsPerMeter});
        check(sValley.valleyMist01 > sRidge.valleyMist01 + 0.30f && sValley.valleyMist01 > 0.40f,
              "valley_inversion_mist_pools_in_low_valleys");

        sim.SetRegime(WeatherRegime::CoastalSeaFog, true);
        const auto sCoast = sim.SampleAt(Vec3{0.0f, 0.0f, cfg.seaLevelZ + 15.0f * cfg.unitsPerMeter});
        const auto sHigh  = sim.SampleAt(Vec3{0.0f, 0.0f, cfg.seaLevelZ + 900.0f * cfg.unitsPerMeter});
        check(sCoast.coastalSeaFog01 > sHigh.coastalSeaFog01 + 0.40f && sCoast.coastalSeaFog01 > 0.50f,
              "coastal_sea_fog_hugs_low_marine_datum");
    }

    // 6. Beaufort sea state, fire/smoke wind deflection, thunderstorm lightning & arctic aurora:
    {
        WeatherSimConfig cfg{};
        cfg.initialRegime = WeatherRegime::ClearAlpine;
        RawWeatherSim calmSim(cfg);
        calmSim.Step(0.5f, Vec3{0.0f, 0.0f, cfg.seaLevelZ + 100.0f});

        cfg.initialRegime = WeatherRegime::Thunderstorm;
        RawWeatherSim stormSim(cfg);
        float maxFlash = 0.0f;
        for (int i = 0; i < 60; ++i) {
            stormSim.Step(0.25f, Vec3{0.0f, 0.0f, cfg.seaLevelZ + 100.0f});
            if (stormSim.GetGpuUniforms().lightningFlash01 > maxFlash) {
                maxFlash = stormSim.GetGpuUniforms().lightningFlash01;
            }
        }
        check(stormSim.GetGpuUniforms().beaufortScale > calmSim.GetGpuUniforms().beaufortScale + 3.0f,
              "storm_drives_higher_beaufort_sea_state");
        check(Length(stormSim.GetGpuUniforms().fireSmokeDeflectVec) > Length(calmSim.GetGpuUniforms().fireSmokeDeflectVec) + 20.0f,
              "wind_couples_to_fire_smoke_deflection");
        check(maxFlash > 0.5f && calmSim.GetGpuUniforms().lightningFlash01 == 0.0f,
              "thunderstorm_triggers_electrostatic_lightning_flashes");

        WeatherSimConfig aurCfg{};
        aurCfg.initialRegime    = WeatherRegime::ArcticClearAurora;
        aurCfg.initialDayOfYear = 10.0f; // Deep winter dark night at 60N
        aurCfg.timeOfDayHours   = 23.5f;
        RawWeatherSim aurSim(aurCfg);
        aurSim.Step(0.25f, Vec3{0.0f, 0.0f, aurCfg.seaLevelZ + 100.0f});
        check(aurSim.GetGpuUniforms().auroraIntensity01 > 0.70f, "arctic_clear_night_activates_aurora");
    }

    // 7. Orbital Seasons & Timescale (axial tilt declination, Nordic day length, timescale stepping):
    {
        const float decSum = RawWeatherSim::ComputeSolarDeclinationRad(172.0f, 23.44f);
        const float decEq  = RawWeatherSim::ComputeSolarDeclinationRad(80.0f,  23.44f);
        const float decWin = RawWeatherSim::ComputeSolarDeclinationRad(355.0f, 23.44f);
        const float daySum = RawWeatherSim::ComputeDayLengthHours(60.0f, decSum);
        const float dayEq  = RawWeatherSim::ComputeDayLengthHours(60.0f, decEq);
        const float dayWin = RawWeatherSim::ComputeDayLengthHours(60.0f, decWin);
        check(decSum > 0.35f && std::fabs(decEq) < 0.03f && decWin < -0.35f,
              "orbital_solar_declination_solstices_and_equinox");
        check(daySum > 18.0f && std::fabs(dayEq - 12.0f) < 0.4f && dayWin < 6.5f,
              "nordic_60n_seasonal_day_length_variation");

        WeatherSimConfig cfg{};
        cfg.initialDayOfYear = 172.0f;
        cfg.timeOfDayHours   = 6.0f;
        RawWeatherSim slowSim(cfg), fastSim(cfg), pausedSim(cfg);
        slowSim.Step(2.0f, 10.0f, -1.0f, Vec3{0.0f, 0.0f, cfg.seaLevelZ + 100.0f});
        fastSim.Step(2.0f, 360.0f, -1.0f, Vec3{0.0f, 0.0f, cfg.seaLevelZ + 100.0f});
        pausedSim.Step(2.0f, 0.0f, -1.0f, Vec3{0.0f, 0.0f, cfg.seaLevelZ + 100.0f});
        check(fastSim.GetCalendarState().gameDaysPassed > slowSim.GetCalendarState().gameDaysPassed
              && std::fabs(pausedSim.GetCalendarState().timeOfDayHours - 6.0f) < 1.0e-5f,
              "timescale_controls_orbital_calendar_advance");
    }

    // 8. Directional aspect solar flux + snowpack compaction/albedo aging + fire melt rings:
    {
        WeatherSimConfig cfg{};
        cfg.initialRegime    = WeatherRegime::Blizzard;
        cfg.initialDayOfYear = 15.0f; // Winter
        cfg.timeOfDayHours   = 12.5f;
        RawWeatherSim sim(cfg);
        const Vec3 camPos{0.0f, 0.0f, cfg.seaLevelZ + 200.0f * cfg.unitsPerMeter};
        for (int i = 0; i < 40; ++i) {
            sim.Step(0.5f, 20.0f, -1.0f, camPos);
        }
        const float freshDepth  = sim.GetSurfaceThermalState().snow.depthM;
        const float freshAlbedo = sim.GetSurfaceThermalState().snow.albedo;
        const float freshDens   = sim.GetSurfaceThermalState().snow.densityKgM3;
        const float canopySnow  = sim.GetPhenologyState().canopySnowLoadKgM2;
        check(freshDepth > 0.35f && freshAlbedo > 0.84f && canopySnow > 5.0f,
              "blizzard_accumulates_fresh_powder_and_canopy_snow_load");

        // Fire/forge heat-radius melt ring carves snow cleanly inside radius vs outside:
        ThermalSource hearth{camPos, 240.0f, 2200.0f};
        sim.SetThermalSources(&hearth, 1u);
        const float depthAtFire = sim.EvaluateLocalSnowDepthAt(camPos, Vec3{0.0f, 0.0f, 1.0f});
        const float depthFar    = sim.EvaluateLocalSnowDepthAt(camPos + Vec3{500.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 1.0f});
        check(depthAtFire < 0.01f && depthFar > 0.30f, "fire_thermal_source_carves_snow_melt_ring");
        sim.SetThermalSources(nullptr, 0u);

        // Age snowpack over ~18 game hours in early spring sun -> compaction rises, albedo ages down while snow remains:
        sim.SetRegime(WeatherRegime::ClearAlpine, true);
        sim.SetDayOfYear(95.0f, false); // Early spring sun
        sim.SetTimeOfDay(12.5f);
        // Check directional aspect solar flux: south-facing slope (-Y) > north-facing slope (+Y) at 60N noon!
        const float qSouth = sim.EvaluateDirectionalSolarWm2(camPos, Normalize(Vec3{0.0f, -0.55f, 0.83f}));
        const float qNorth = sim.EvaluateDirectionalSolarWm2(camPos, Normalize(Vec3{0.0f,  0.55f, 0.83f}));
        check(qSouth > qNorth * 1.25f, "south_aspect_slope_receives_more_solar_insolation");

        for (int i = 0; i < 24; ++i) {
            sim.Step(1.5f, 1200.0f, -1.0f, camPos);
        }
        const float agedAlbedo  = sim.GetSurfaceThermalState().snow.albedo;
        const float agedDens    = sim.GetSurfaceThermalState().snow.densityKgM3;
        const float meltedDepth = sim.GetSurfaceThermalState().snow.depthM;
        check(meltedDepth > 0.02f && agedAlbedo < freshAlbedo - 0.04f && agedDens > freshDens + 30.0f && meltedDepth < freshDepth,
              "snowpack_compacts_ages_albedo_and_melts_in_spring_sun");
    }

    // 9. Freezing-degree-day lake/river ice growth, black ice / frost rime, and spring fracture thaw:
    {
        WeatherSimConfig cfg{};
        cfg.initialRegime    = WeatherRegime::ArcticClearAurora;
        cfg.initialDayOfYear = 10.0f;
        cfg.timeOfDayHours   = 2.0f;
        RawWeatherSim sim(cfg);
        const Vec3 shorePos{0.0f, 0.0f, cfg.seaLevelZ + 20.0f * cfg.unitsPerMeter};
        const float initIce = sim.GetSurfaceThermalState().ice.iceThicknessM;
        for (int i = 0; i < 80; ++i) {
            sim.Step(2.0f, 1200.0f, -1.0f, shorePos);
        }
        const float winterIce  = sim.GetSurfaceThermalState().ice.iceThicknessM;
        const float winterRime = sim.GetSurfaceThermalState().ice.frostRime01;
        check(winterIce > initIce + 0.05f && winterRime > 0.25f, "freezing_degree_days_grow_ice_and_frost_rime");

        // Warm spring sun fractures and melts ice:
        sim.SetRegime(WeatherRegime::ClearAlpine, true);
        sim.SetDayOfYear(160.0f, false);
        sim.SetTimeOfDay(13.0f);
        for (int i = 0; i < 80; ++i) {
            sim.Step(2.0f, 1200.0f, -1.0f, shorePos);
        }
        check(sim.GetSurfaceThermalState().ice.iceThicknessM < winterIce, "spring_warming_thaws_ice_thickness");
    }

    // 10. Sunrise solar burn-off breaks nocturnal valley inversion & high wind raises eddy diffusivity K_h:
    {
        WeatherSimConfig cfg{};
        cfg.initialRegime    = WeatherRegime::DawnValleyMist;
        cfg.initialDayOfYear = 140.0f;
        cfg.timeOfDayHours   = 4.5f;
        RawWeatherSim sim(cfg);
        const Vec3 valleyPos{0.0f, 0.0f, cfg.seaLevelZ + 20.0f * cfg.unitsPerMeter};
        for (int i = 0; i < 20; ++i) {
            sim.Step(1.0f, 120.0f, -1.0f, valleyPos);
        }
        const float dawnInv  = sim.GetSurfaceThermalState().dispersion.inversionStrength01;
        const float calmKh   = sim.GetSurfaceThermalState().dispersion.eddyDiffusivityM2S;
        sim.SetRegime(WeatherRegime::ClearAlpine, true);
        sim.SetTimeOfDay(13.5f);
        for (int i = 0; i < 30; ++i) {
            sim.Step(1.0f, 300.0f, -1.0f, valleyPos);
        }
        const float noonInv  = sim.GetSurfaceThermalState().dispersion.inversionStrength01;
        sim.SetRegime(WeatherRegime::Thunderstorm, true);
        sim.Step(1.0f, 20.0f, -1.0f, valleyPos);
        const float stormKh  = sim.GetSurfaceThermalState().dispersion.eddyDiffusivityM2S;
        check(noonInv < dawnInv - 0.20f, "sunrise_solar_burnoff_breaks_valley_inversion");
        check(stormKh > calmKh + 10.0f, "wind_and_buoyancy_increase_eddy_diffusivity_kh");
    }

    // 11. Seasonal botanical phenology cycle (Spring bud-break -> Summer chlorophyll -> Autumn gold/crimson -> Winter abscission):
    {
        WeatherSimConfig cfg{};
        cfg.initialDayOfYear = 105.0f; // Spring
        RawWeatherSim sim(cfg);
        const auto phSpring = sim.GetPhenologyState();
        sim.SetDayOfYear(190.0f, true); // Summer
        const auto phSummer = sim.GetPhenologyState();
        sim.SetDayOfYear(295.0f, true); // Mid-late autumn
        const auto phAutumn = sim.GetPhenologyState();
        sim.SetDayOfYear(15.0f, true);  // Winter
        const auto phWinter = sim.GetPhenologyState();

        check(phSpring.budBreak01 > 0.35f && phSummer.chlorophyll01 > 0.95f
              && phAutumn.autumnCarotenoid01 > 0.50f && phAutumn.autumnAnthocyanin01 > 0.35f
              && phWinter.leafAbscission01 > 0.95f && phWinter.chlorophyll01 < 0.05f,
              "seasonal_botanical_phenology_four_season_cycle");
    }

    // 12. Deterministic stepping & zero NaN/Inf under extreme/adversarial inputs:
    {
        WeatherSimConfig cfg{};
        cfg.seed               = 424242u;
        cfg.initialRegime      = WeatherRegime::Thunderstorm;
        cfg.autoFrontEvolution = true;
        RawWeatherSim a(cfg), b(cfg);
        for (int i = 0; i < 100; ++i) {
            const Vec3 pos{static_cast<float>(i) * 120.0f, -static_cast<float>(i) * 85.0f, cfg.seaLevelZ + 3000.0f};
            a.Step(0.33f, 20.0f, -1.0f, pos);
            b.Step(0.33f, 20.0f, -1.0f, pos);
        }
        const auto& ga = a.GetGpuUniforms();
        const auto& gb = b.GetGpuUniforms();
        const bool bitExact = (std::memcmp(&ga, &gb, sizeof(WeatherGpuUniforms)) == 0);
        check(bitExact, "deterministic_seed_stepping_bit_exact");

        const float qnan = std::numeric_limits<float>::quiet_NaN();
        const float inf  = std::numeric_limits<float>::infinity();
        a.Step(0.0f, 0.0f, -1.0f, Vec3{0.0f, 0.0f, 0.0f});
        a.Step(qnan, inf, qnan, Vec3{qnan, inf, -inf});
        a.Step(1000.0f, 5000.0f, -1.0f, Vec3{1.0e7f, -1.0e7f, 500000.0f});
        const auto sExt = a.SampleAt(Vec3{qnan, inf, -500000.0f});
        check(IsFiniteGpu(a.GetGpuUniforms()) && IsFiniteSample(sExt), "zero_nan_inf_under_extreme_inputs");
    }

    std::printf("weather_sim summary: %d passed, %d failed\n", pass, fail);
    return fail;
}

} // namespace raw::worldgen

#ifndef RAW_WEATHER_SIM_NO_MAIN
int main() {
    return raw::worldgen::RunWeatherSimTests() == 0 ? 0 : 1;
}
#endif
