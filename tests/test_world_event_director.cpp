// test_world_event_director.cpp: Verification Suite for `raw::worldgen::RawWorldEventDirector`.
// Exports `int raw::worldgen::RunWorldEventDirectorTests(void (*checkFn)(bool, const char*) = nullptr)`
// and supports `#ifdef RAW_WORLD_EVENT_DIRECTOR_TEST_STANDALONE` standalone execution.
// Original work, MIT.

#include "RawWorldEventDirector.h"

#include <cmath>
#include <cstdio>

namespace raw::worldgen {

int RunWorldEventDirectorTests(void (*checkFn)(bool, const char*)) {
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
    // Test 1: Planted-failure controls (zero/neg hours no-op, unknown resolve false, cap enforced)
    // =========================================================================
    {
        EventDirectorConfig cfg{};
        cfg.maxConcurrentEvents = 2u; // Strict tight cap!
        RawWorldEventDirector dir(cfg);

        WorldEventInputs in{};
        in.timeOfDayHours = 23.0f;
        in.auroraKp       = 7.5f;
        in.cloudCover01   = 0.1f;

        // Zero / negative hours must be an exact no-op:
        dir.StepHours(0.0f, in);
        dir.StepHours(-5.0f, in);
        const bool zeroNoOp = (dir.ComputeTelemetry().totalSpawned == 0u);

        // Resolving unknown ID returns false:
        const bool unknownResolveFalse = !dir.ResolveEvent(0u) && !dir.ResolveEvent(9999u);

        // Trigger 3+ conditions simultaneously and verify cap <= 2 is strictly enforced:
        RawSettlementSim settle(101u);
        SettlementWeatherMap blizWx{};
        blizWx.byHold[static_cast<size_t>(HoldId::Windhelm)].blizzard01   = 0.92f;
        blizWx.byHold[static_cast<size_t>(HoldId::Winterhold)].blizzard01 = 0.95f;
        settle.StepHours(10.0f, blizWx);

        RawWorldEcosystem eco{};
        const Vec2 c = eco.CellCenterWorldM(32u, 32u);
        eco.IgniteAt(c.x, c.y, 12.0f, 650.0f);

        in.settlement = &settle;
        in.ecosystem  = &eco;
        in.season     = SeasonId::Winter;
        in.blizzard01 = 0.85f;
        dir.StepHours(1.0f, in);
        const EventDirectorTelemetry capTel = dir.ComputeTelemetry();
        const bool capOk = (capTel.activeCount <= 2u) &&
                           (capTel.totalSpawned == capTel.activeCount + capTel.totalResolved + capTel.totalExpired);

        check(zeroNoOp && unknownResolveFalse && capOk,
              "world_event_director_planted_failure_controls");
    }

    // =========================================================================
    // Test 2: Blizzard Caravan Rescue + Cooldown Anti-Spam Proof
    // =========================================================================
    {
        RawSettlementSim settle(202u);
        SettlementWeatherMap blizWx{};
        blizWx.byHold[static_cast<size_t>(HoldId::Windhelm)].blizzard01   = 0.92f;
        blizWx.byHold[static_cast<size_t>(HoldId::Winterhold)].blizzard01 = 0.95f;
        blizWx.byHold[static_cast<size_t>(HoldId::Dawnstar)].blizzard01   = 0.90f;
        settle.StepHours(12.0f, blizWx);

        RawWorldEventDirector dir{};
        WorldEventInputs in{};
        in.settlement     = &settle;
        in.timeOfDayHours = 12.0f;
        in.blizzard01     = 0.90f;

        dir.StepHours(1.0f, in);
        const WorldEvent* rescueEv = dir.FindActiveByKind(WorldEventKind::BlizzardCaravanRescue);
        const bool spawnedRescue   = (rescueEv != nullptr) && (rescueEv->rewardGold > 100.0f);
        const uint32_t rescueId    = rescueEv ? rescueEv->id : 0u;

        // Repeatedly stepping while active must NOT spawn duplicates:
        for (int h = 0; h < 6; ++h) {
            settle.StepHours(1.0f, blizWx);
            dir.StepHours(1.0f, in);
        }
        const uint32_t spawnsWhileActive =
            dir.ComputeTelemetry().spawnedByKind[static_cast<size_t>(WorldEventKind::BlizzardCaravanRescue)];

        // Resolve event and step inside cooldown window (cooldown = 16h) -> must NOT immediately respawn!
        const bool resolved = dir.ResolveEvent(rescueId);
        for (int h = 0; h < 6; ++h) {
            dir.StepHours(1.0f, in);
        }
        const uint32_t spawnsDuringCooldown =
            dir.ComputeTelemetry().spawnedByKind[static_cast<size_t>(WorldEventKind::BlizzardCaravanRescue)];

        std::printf("  [Blizzard Rescue Anti-Spam] spawned=%u resolved=%d duringCooldown=%u cooldownLeft=%.1fh\n",
                    spawnsWhileActive, resolved ? 1 : 0, spawnsDuringCooldown,
                    dir.CooldownRemainingHours(WorldEventKind::BlizzardCaravanRescue));

        check(spawnedRescue && resolved && spawnsWhileActive == 1u && spawnsDuringCooldown == 1u,
              "world_event_director_blizzard_rescue_and_cooldown_anti_spam");
    }

    // =========================================================================
    // Test 3: Wildfire Spawn & Rain Auto-Resolution Proof
    // =========================================================================
    {
        EcoGridConfig ecfg{};
        ecfg.gridW     = 32u;
        ecfg.gridH     = 32u;
        ecfg.cellSizeM = 2.0f;
        RawWorldEcosystem eco(ecfg);

        const Vec2 center = eco.CellCenterWorldM(16u, 16u);
        eco.IgniteAt(center.x, center.y, 7.0f, 650.0f);

        RawWorldEventDirector dir{};
        WorldEventInputs in{};
        in.ecosystem       = &eco;
        in.ecosystemHoldId = HoldId::Falkreath;
        in.timeOfDayHours  = 14.0f;

        dir.StepHours(1.0f, in);
        const WorldEvent* fireEv = dir.FindActiveByKind(WorldEventKind::WildfireContainment);
        const bool spawnedFire   = (fireEv != nullptr) && (fireEv->holdId == HoldId::Falkreath);

        // Extinguish wildfire with heavy rain in RawWorldEcosystem:
        EcoWeatherInput storm{};
        storm.ambientTempC = 8.0f;
        storm.rainRate01   = 0.95f;
        for (int s = 0; s < 50; ++s) {
            eco.Step(0.25f, storm);
        }

        // Next director step auto-resolves WildfireContainment cleanly!
        dir.StepHours(1.0f, in);
        const bool autoResolved = (eco.ComputeTelemetry().activeFireCells == 0u) &&
                                  (dir.FindActiveByKind(WorldEventKind::WildfireContainment) == nullptr) &&
                                  (dir.ComputeTelemetry().totalResolved >= 1u);

        std::printf("  [Wildfire Auto-Resolve] spawned=%d fireAfterRain=%u totalResolved=%u\n",
                    spawnedFire ? 1 : 0, eco.ComputeTelemetry().activeFireCells,
                    dir.ComputeTelemetry().totalResolved);

        check(spawnedFire && autoResolved,
              "world_event_director_wildfire_spawn_and_auto_resolution");
    }

    // =========================================================================
    // Test 4: Aurora Night + 30-Day (720h) Coupled 9-Hold World Accounting Proof
    // =========================================================================
    {
        RawSettlementSim settle(404u);
        RawWildlifeBoids boids;
        boids.SpawnGuild(FaunaGuild::PineCrowFlock, 24u, {0.0f, 0.0f, 300.0f}, 120.0f, 55u);
        boids.EmitStimulus({0.0f, 0.0f, 260.0f}, 1200.0f, StimulusKind::LoudNoise, 1.0f, 3.0f);
        WildlifeEnvInput wEnv{};
        boids.Step(0.05f, wEnv); // Produces startledCount = 24 (> 15)

        RawWorldEventDirector dir{};
        SettlementWeatherMap wx{};
        bool accountingAlwaysExact = true;
        bool auroraTriggered       = false;
        bool harvestTriggered      = false;
        bool allFinite             = true;

        for (int h = 0; h < 720; ++h) { // 30 simulated days
            const int day = h / 24;
            const float tod = static_cast<float>(h % 24);
            // Periodic winter pass blizzards & clear geomagnetic aurora nights:
            const bool stormDay = (day % 6 == 2);
            wx.byHold[static_cast<size_t>(HoldId::Windhelm)].blizzard01   = stormDay ? 0.88f : 0.05f;
            wx.byHold[static_cast<size_t>(HoldId::Winterhold)].blizzard01 = stormDay ? 0.92f : 0.05f;
            settle.StepHours(1.0f, wx);

            WorldEventInputs in{};
            in.settlement     = &settle;
            in.wildlife       = (day == 0) ? &boids : nullptr;
            in.timeOfDayHours = tod;
            in.season         = (day < 15) ? SeasonId::Autumn : SeasonId::Winter;
            in.auroraKp       = (!stormDay && (day % 4 == 0)) ? 7.2f : 2.0f;
            in.cloudCover01   = stormDay ? 0.90f : 0.12f;
            in.blizzard01     = stormDay ? 0.85f : 0.05f;

            dir.StepHours(1.0f, in);

            if (dir.FindActiveByKind(WorldEventKind::AuroraCelestialGathering) != nullptr) {
                auroraTriggered = true;
            }
            if (dir.FindActiveByKind(WorldEventKind::SeasonalHarvestMarket) != nullptr) {
                harvestTriggered = true;
            }
            const EventDirectorTelemetry tel = dir.ComputeTelemetry();
            if (tel.totalSpawned != tel.activeCount + tel.totalResolved + tel.totalExpired ||
                tel.activeCount > dir.Config().maxConcurrentEvents ||
                !std::isfinite(tel.highestUrgency)) {
                accountingAlwaysExact = false;
            }
            for (const auto& e : dir.ActiveEvents()) {
                if (!std::isfinite(e.urgency01) || !std::isfinite(e.rewardGold) || !std::isfinite(e.ageHours)) {
                    allFinite = false;
                }
            }
        }

        const EventDirectorTelemetry finalTel = dir.ComputeTelemetry();
        uint32_t distinctKinds = 0u;
        for (size_t k = 0u; k < kWorldEventKindCount; ++k) {
            if (finalTel.spawnedByKind[k] > 0u) ++distinctKinds;
        }

        std::printf(
            "  [30-Day Radiant World] spawned=%u active=%u resolved=%u expired=%u distinctKinds=%u/6 "
            "(aurora=%d harvest=%d)\n",
            finalTel.totalSpawned, finalTel.activeCount, finalTel.totalResolved, finalTel.totalExpired,
            distinctKinds, auroraTriggered ? 1 : 0, harvestTriggered ? 1 : 0);

        check(accountingAlwaysExact && allFinite && auroraTriggered && harvestTriggered &&
                  distinctKinds >= 4u && finalTel.totalSpawned >= 15u,
              "world_event_director_aurora_and_30_day_coupled_world");
    }

    return failures;
}

}  // namespace raw::worldgen

#ifdef RAW_WORLD_EVENT_DIRECTOR_TEST_STANDALONE
int main() {
    const int failures = raw::worldgen::RunWorldEventDirectorTests(nullptr);
    std::printf("RawWorldEventDirector standalone test summary: %s (%d failures)\n",
                failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
#endif
