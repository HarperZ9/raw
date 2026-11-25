// test_living_world_schedule.cpp: Verification Suite for `raw::worldgen::RawLivingWorldSchedule`.
// Exports BOTH `int run_living_world_schedule_tests()` and
// `int raw::worldgen::RunLivingWorldScheduleTests(void (*checkFn)(bool, const char*) = nullptr)`
// plus `#ifdef RAW_LIVING_WORLD_SCHEDULE_TEST_STANDALONE` standalone `main()`.
// Original work, MIT.

#include "RawLivingWorldSchedule.h"

#include <array>
#include <cmath>
#include <cstdio>

namespace raw::worldgen {

int RunLivingWorldScheduleTests(void (*checkFn)(bool, const char*)) {
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

    // -------------------------------------------------------------------------
    // Test 1: Planted-Failure Controls, 32+ Roster Coverage & Determinism
    // -------------------------------------------------------------------------
    {
        RawLivingWorldSchedule a(202661u);
        RawLivingWorldSchedule b(202661u);

        const uint32_t badHoldCnt = a.PopulateDefaultHold(HoldId::Count, 36u);
        CitizenDesc badArch{};
        badArch.archetype = CitizenArchetype::Count;
        const bool badArchAdded = a.AddCitizen(badArch);

        CitizenDesc nanCitizen{};
        nanCitizen.archetype    = CitizenArchetype::Farmer;
        nanCitizen.resilience01 = std::nanf("");
        const bool nanAdded = a.AddCitizen(nanCitizen);

        const uint32_t countA = a.PopulateDefaultHold(HoldId::Whiterun, 36u);
        const uint32_t countB = b.PopulateDefaultHold(HoldId::Whiterun, 36u);

        // Verify all 8 archetypes represented in the 36-citizen roster:
        std::array<uint32_t, kCitizenArchetypeCount> archCounts{};
        for (const auto& c : a.Citizens()) {
            ++archCounts[static_cast<size_t>(c.archetype)];
        }
        bool allEightPresent = true;
        for (uint32_t cnt : archCounts) {
            if (cnt == 0u) allEightPresent = false;
        }

        // Zero/negative/NaN dtHours must be exact no-ops:
        LivingWorldContext baseCtx{};
        baseCtx.timeOfDayHours = 10.0f;
        a.StepHours(0.0f, baseCtx);
        a.StepHours(-3.0f, baseCtx);
        a.StepHours(std::nanf(""), baseCtx);
        const bool noOpOk = (a.ComputeTelemetry().simHoursElapsed == 0.0f);

        // Identical 6-hour run on two fresh engines must produce bit-identical telemetry:
        a.StepHours(6.0f, baseCtx);
        b.StepHours(6.0f, baseCtx);
        const LivingWorldTelemetry ta = a.ComputeTelemetry();
        const LivingWorldTelemetry tb = b.ComputeTelemetry();
        const bool detOk = (ta.totalWorkOutput == tb.totalWorkOutput) &&
                           (ta.meanEnergy01 == tb.meanEnergy01) &&
                           (ta.meanMorale01 == tb.meanMorale01) &&
                           (ta.workingCount == tb.workingCount);

        const bool ok = (badHoldCnt == 0u) && !badArchAdded && !nanAdded &&
                        (countA == 36u) && (countB == 36u) && allEightPresent && noOpOk && detOk;
        check(ok, "living_world_planted_controls_roster_and_determinism");
    }

    // -------------------------------------------------------------------------
    // Test 2: 24-Hour Seasonal Clock, Work Output, Morale Recovery & Nocturnal Wildlife Shift
    // -------------------------------------------------------------------------
    {
        RawLivingWorldSchedule sched(7711u);
        sched.PopulateDefaultHold(HoldId::Whiterun, 36u);

        // Inject low initial morale & mid energy to observe tavern + sleep recovery:
        LivingWorldContext noonCtx{};
        noonCtx.hold                 = HoldId::Whiterun;
        noonCtx.timeOfDayHours       = 11.0f;
        noonCtx.dayLengthHours       = 16.0f; // Summer long daylight
        noonCtx.solarElevation       = 0.72f;
        noonCtx.ambientTempC         = 16.0f;
        noonCtx.windSpeedMps         = 2.5f;
        noonCtx.settlementMarketOpen = true;

        sched.StepHours(2.0f, noonCtx);
        const LivingWorldTelemetry tNoon = sched.ComputeTelemetry();

        // Evening tavern social hour (19:30 -> 21:30): morale should rise strongly via TavernRest!
        LivingWorldContext eveCtx = noonCtx;
        eveCtx.timeOfDayHours     = 19.5f;
        eveCtx.solarElevation     = -0.05f;
        eveCtx.ambientTempC       = 11.0f;
        sched.StepHours(2.0f, eveCtx);
        const LivingWorldTelemetry tEve = sched.ComputeTelemetry();

        // Deep midnight (23:30 -> 02:30, well before 04:00 summer sunrise): sleeping recovers energy & nocturnal predators dominate!
        LivingWorldContext nightCtx = noonCtx;
        nightCtx.timeOfDayHours     = 23.5f;
        nightCtx.solarElevation     = -0.55f;
        nightCtx.ambientTempC       = 5.0f;
        sched.StepHours(3.0f, nightCtx);
        const LivingWorldTelemetry tNight = sched.ComputeTelemetry();

        const WorkShiftLedger& led = sched.WorkLedger();
        const bool workOk = tNoon.marketOpen && (tNoon.workingCount >= 18u) &&
                            (led.totalWorkOutput > 25.0f) && (led.grainWork > 0.0f) &&
                            (led.smithingWork > 0.0f) && (led.tradeWork > 0.0f) &&
                            (led.huntingWork > 0.0f);
        const bool tavernAndSleepOk = (tEve.tavernCount >= 10u) && (tEve.meanMorale01 > tNoon.meanMorale01) &&
                                      (tNight.sleepingCount >= 20u) && (tNight.meanEnergy01 > tEve.meanEnergy01) &&
                                      tNight.braziersLit && !tNoon.braziersLit;
        const bool wildlifeShiftOk = (tNoon.diurnalPreyActivity01 > 0.70f) &&
                                     (tNoon.nocturnalPredatorAct01 < 0.25f) &&
                                     (tNight.nocturnalPredatorAct01 > 0.75f) &&
                                     (tNight.diurnalPreyActivity01 < 0.25f);

        std::printf("  [24h Cycle] noonWork=%u workOut=%.1f eveTavern=%u morale %.2f->%.2f nightSleep=%u "
                    "energy %.2f->%.2f prey %.2f->%.2f pred %.2f->%.2f\n",
                    tNoon.workingCount, led.totalWorkOutput, tEve.tavernCount,
                    tNoon.meanMorale01, tEve.meanMorale01, tNight.sleepingCount,
                    tEve.meanEnergy01, tNight.meanEnergy01,
                    tNoon.diurnalPreyActivity01, tNight.diurnalPreyActivity01,
                    tNoon.nocturnalPredatorAct01, tNight.nocturnalPredatorAct01);

        check(workOk && tavernAndSleepOk && wildlifeShiftOk,
              "living_world_24h_cycle_work_recovery_and_nocturnal_wildlife");
    }

    // -------------------------------------------------------------------------
    // Test 3: Live 4-System Coupling (Weather Blizzard, Wartime Curfew & Wildfire Evacuation)
    // -------------------------------------------------------------------------
    {
        RawSettlementSim    settlement(6101u);
        RawWeatherSim       weather{};
        RawFactionDiplomacy diplomacy{};
        RawWorldEcosystem   eco{};

        // 3A. Live RawWeatherSim Blizzard -> drives outdoor villagers indoors, ignites braziers, cancels patrols!
        WeatherSimConfig wcfg{};
        wcfg.initialDayOfYear = 15.0f; // Deep Nordic winter
        wcfg.timeOfDayHours   = 11.0f;
        wcfg.initialRegime    = WeatherRegime::Blizzard;
        weather.Reset(wcfg);
        weather.SetRegime(WeatherRegime::Blizzard, true);
        weather.Step(1.0f, Vec3{0.0f, 0.0f, 0.0f});

        RawLivingWorldSchedule blizSched(6102u);
        blizSched.PopulateDefaultHold(HoldId::Winterhold, 36u);
        blizSched.StepFromSystems(1.0f, HoldId::Winterhold, &settlement, &weather, &diplomacy, &eco);
        const LivingWorldTelemetry tBliz = blizSched.ComputeTelemetry();

        const bool blizzardOk = tBliz.braziersLit && tBliz.outdoorPatrolsCancelled &&
                                (tBliz.shelteringWeatherCount >= 20u) &&
                                (tBliz.indoorsCount == tBliz.totalCitizens) &&
                                (tBliz.patrollingCount == 0u) && !tBliz.marketOpen;

        // 3B. Live RawFactionDiplomacy Wartime Curfew (clear dusk 20:00) -> extra guard patrols + curfew lockdown!
        WeatherSimConfig clearCfg{};
        clearCfg.initialDayOfYear = 172.0f;
        clearCfg.timeOfDayHours   = 20.0f;
        clearCfg.initialRegime    = WeatherRegime::ClearAlpine;
        weather.Reset(clearCfg);
        weather.SetRegime(WeatherRegime::ClearAlpine, true);
        weather.Step(0.5f, Vec3{0.0f, 0.0f, 0.0f});

        // Baseline peacetime dusk patrol count:
        RawLivingWorldSchedule peaceSched(6103u);
        peaceSched.PopulateDefaultHold(HoldId::Whiterun, 36u);
        peaceSched.StepFromSystems(1.0f, HoldId::Whiterun, &settlement, &weather, &diplomacy, &eco);
        const LivingWorldTelemetry tPeaceDusk = peaceSched.ComputeTelemetry();

        // Trigger civil-war tension & hostile posture in RawFactionDiplomacy:
        diplomacy.AdjustFactionReputation(FactionId::ImperialLegion, 75.0f);
        diplomacy.RecordCrime(HoldId::Whiterun, CrimeSeverity::Murder, true);

        RawLivingWorldSchedule warSched(6103u);
        warSched.PopulateDefaultHold(HoldId::Whiterun, 36u);
        warSched.StepFromSystems(1.0f, HoldId::Whiterun, &settlement, &weather, &diplomacy, &eco);
        const LivingWorldTelemetry tWarDusk = warSched.ComputeTelemetry();

        const bool warOk = tWarDusk.wartimeCurfewActive &&
                           (tWarDusk.patrollingCount > tPeaceDusk.patrollingCount) &&
                           (tWarDusk.curfewCount >= 20u);

        // 3C. Live RawWorldEcosystem Wildfire -> immediate town evacuation + wildlife flight!
        EcoGridConfig ecfg{};
        ecfg.gridW     = 32u;
        ecfg.gridH     = 32u;
        ecfg.cellSizeM = 2.0f;
        eco.Reset(ecfg);
        const uint32_t ignited = eco.IgniteAt(32.0f, 32.0f, 8.0f, 520.0f);
        EcoWeatherInput ewx{};
        ewx.ambientTempC = 24.0f;
        ewx.windBeaufort = 5.0f;
        eco.Step(2.0f, ewx);

        RawLivingWorldSchedule fireSched(6104u);
        fireSched.PopulateDefaultHold(HoldId::Falkreath, 36u);
        fireSched.StepFromSystems(1.0f, HoldId::Falkreath, &settlement, &weather, &diplomacy, &eco);
        const LivingWorldTelemetry tFire = fireSched.ComputeTelemetry();

        // Also test ApplyToHoldState sync back into RawSettlementSim:
        HoldState* falkHold = settlement.GetHoldMutable(HoldId::Falkreath);
        if (falkHold) {
            fireSched.ApplyToHoldState(*falkHold);
        }

        const bool fireOk = (ignited >= 4u) && tFire.wildfireEvacuationActive &&
                            (tFire.evacuatingCount >= 28u) && tFire.wildlifeFleeingFire &&
                            !tFire.marketOpen && (falkHold != nullptr && !falkHold->marketOpen);

        std::printf("  [4-Sys Coupling] blizIndoors=%u/%u braziers=%d patrols=%u | "
                    "warPatrols %u->%u curfew=%u | fireIgnited=%u evac=%u wildFlee=%d\n",
                    tBliz.indoorsCount, tBliz.totalCitizens, tBliz.braziersLit ? 1 : 0, tBliz.patrollingCount,
                    tPeaceDusk.patrollingCount, tWarDusk.patrollingCount, tWarDusk.curfewCount,
                    ignited, tFire.evacuatingCount, tFire.wildlifeFleeingFire ? 1 : 0);

        check(blizzardOk && warOk && fireOk,
              "living_world_coupled_blizzard_wartime_curfew_and_wildfire_evacuation");
    }

    return failures;
}

}  // namespace raw::worldgen

int run_living_world_schedule_tests() {
    return raw::worldgen::RunLivingWorldScheduleTests(nullptr);
}

#ifdef RAW_LIVING_WORLD_SCHEDULE_TEST_STANDALONE
int main() {
    const int failures = run_living_world_schedule_tests();
    std::printf("RawLivingWorldSchedule standalone test summary: %s (%d failures)\n",
                failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
#endif
