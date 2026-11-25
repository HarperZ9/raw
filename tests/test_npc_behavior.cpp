// test_npc_behavior.cpp: Wave 49 Weather, Season & Economy-Driven NPC Daily Behavior &
// Crowd Ecology Verification Suite.
// Linkable into ce_runtime_check via raw::rt::RunNpcBehaviorTests(checkFn),
// or buildable standalone with -DRAW_NPC_BEHAVIOR_STANDALONE_MAIN.
// Original work, MIT.

#include "../src/engine_rt/rt_live_config.h"
#include "../src/engine_rt/rt_npc_behavior.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace raw::rt {

int RunNpcBehaviorTests(void (*checkFn)(bool, const char*)) {
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        if (!ok) ++failures;
        if (checkFn) {
            checkFn(ok, name);
        } else {
            std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
        }
    };

    // 1. Summer sunny noon: Villager WorkOutdoor, Merchant MarketStall, torchRatio == 0, shelterPressure < 0.1
    {
        NpcBehaviorEnvInput env{};
        env.gameHour     = 12.0f;
        env.season       = WorldSeason::Summer;
        env.surfaceTempC = 21.0f;
        env.windBeaufort = 2.0f;
        env.rain01       = 0.0f;
        env.snow01       = 0.0f;
        env.fog01        = 0.0f;
        env.thunder01    = 0.0f;
        env.scarcity01   = 0.05f;

        const NpcBehaviorEvaluation ev = EvaluateNpcBehavior(env);
        const auto& vill = ev.archetypes[static_cast<size_t>(NpcArchetype::Villager)];
        const auto& mkt  = ev.archetypes[static_cast<size_t>(NpcArchetype::Merchant)];
        const auto& grd  = ev.archetypes[static_cast<size_t>(NpcArchetype::Guard)];

        const bool summerOk =
            vill.directive == RoutineDirective::WorkOutdoor &&
            mkt.directive  == RoutineDirective::MarketStall &&
            grd.directive  == RoutineDirective::Patrol &&
            !grd.carryTorch &&
            ev.summary.torchDeploymentRatio == 0.0f &&
            ev.summary.shelterPressure01 < 0.10f &&
            ev.summary.marketActivity01 > 0.90f;
        check(summerOk, "npc_behavior_summer_sunny_noon_work_market_zero_torches");
    }

    // 2. Winter midnight blizzard (-22 C, heavy snow): Villager/Merchant SeekShelter, Guard torch + high alertness,
    //    shelterPressure > 0.85, Hunter campfire + expanded winter forage range!
    {
        NpcBehaviorEnvInput env{};
        env.gameHour     = 0.5f;
        env.season       = WorldSeason::Winter;
        env.surfaceTempC = -22.0f;
        env.windBeaufort = 8.0f;
        env.snow01       = 0.85f;
        env.scarcity01   = 0.60f;

        const NpcBehaviorEvaluation ev = EvaluateNpcBehavior(env);
        const auto& vill = ev.archetypes[static_cast<size_t>(NpcArchetype::Villager)];
        const auto& mkt  = ev.archetypes[static_cast<size_t>(NpcArchetype::Merchant)];
        const auto& grd  = ev.archetypes[static_cast<size_t>(NpcArchetype::Guard)];
        const auto& hnt  = ev.archetypes[static_cast<size_t>(NpcArchetype::Hunter)];

        if (!checkFn) {
            std::printf("[Blizzard] windChill=%.1fC shelter=%.2f guardAlert=%.2fx guardPatrol=%.2f torchRatio=%.2f hunterRange=%.2f\n",
                        double(ev.summary.effectiveWindChillC), double(ev.summary.shelterPressure01),
                        double(ev.summary.guardAlertnessMult), double(grd.patrolRadiusMult),
                        double(ev.summary.torchDeploymentRatio), double(ev.summary.hunterForageRangeMult));
        }

        const bool blizzardOk =
            vill.directive == RoutineDirective::SeekShelter &&
            mkt.directive  == RoutineDirective::SeekShelter &&
            grd.directive  == RoutineDirective::Patrol &&
            grd.carryTorch &&
            grd.alertnessMult > 1.35f &&
            grd.patrolRadiusMult < 0.80f &&
            ev.summary.shelterPressure01 > 0.85f &&
            hnt.lightCampfire &&
            ev.summary.hunterForageRangeMult > 1.40f;
        check(blizzardOk, "npc_behavior_winter_midnight_blizzard_shelter_torch_and_alertness");
    }

    // 3. Rainstorm / thunderstorm: Mage & Bandit shift preferred element to Shock/Frost (matching rt_magic_physics!),
    //    marketActivity01 drops cleanly vs fair weather.
    {
        NpcBehaviorEnvInput fair{};
        fair.gameHour     = 13.0f;
        fair.season       = WorldSeason::Autumn;
        fair.surfaceTempC = 14.0f;
        fair.windBeaufort = 2.0f;
        const NpcBehaviorEvaluation evFair = EvaluateNpcBehavior(fair);

        NpcBehaviorEnvInput storm = fair;
        storm.rain01       = 0.80f;
        storm.thunder01    = 0.75f;
        storm.windBeaufort = 7.0f;
        const NpcBehaviorEvaluation evStorm = EvaluateNpcBehavior(storm);

        const auto& mage   = evStorm.archetypes[static_cast<size_t>(NpcArchetype::Mage)];
        const auto& bandit = evStorm.archetypes[static_cast<size_t>(NpcArchetype::Bandit)];

        const bool mageShockOrFrost   = (mage.preferredElement == PreferredElement::Shock ||
                                         mage.preferredElement == PreferredElement::Frost);
        const bool banditShockOrFrost = (bandit.preferredElement == PreferredElement::Shock ||
                                         bandit.preferredElement == PreferredElement::Frost);
        const bool marketDropped      = evStorm.summary.marketActivity01 < evFair.summary.marketActivity01 * 0.5f;

        if (!checkFn) {
            std::printf("[Storm] mageElem=%s banditElem=%s marketFair=%.2f -> marketStorm=%.2f\n",
                        PreferredElementName(mage.preferredElement),
                        PreferredElementName(bandit.preferredElement),
                        double(evFair.summary.marketActivity01),
                        double(evStorm.summary.marketActivity01));
        }

        check(mageShockOrFrost && banditShockOrFrost,
              "npc_behavior_rainstorm_shifts_mage_and_bandit_to_shock_or_frost");
        check(marketDropped, "npc_behavior_rainstorm_drops_market_activity");
    }

    // 4. Dry freezing weather -> Mage/Bandit prefer Fire (matching rt_magic_physics dry-cold combustion boost!)
    {
        NpcBehaviorEnvInput dryCold{};
        dryCold.gameHour     = 11.0f;
        dryCold.season       = WorldSeason::Winter;
        dryCold.surfaceTempC = -10.0f;
        dryCold.windBeaufort = 2.0f;
        dryCold.rain01       = 0.0f;
        dryCold.snow01       = 0.0f;
        const NpcBehaviorEvaluation evDryCold = EvaluateNpcBehavior(dryCold);
        const bool firePreferred =
            evDryCold.summary.preferredMageElement   == static_cast<uint8_t>(PreferredElement::Fire) &&
            evDryCold.summary.preferredBanditElement == static_cast<uint8_t>(PreferredElement::Fire);
        check(firePreferred, "npc_behavior_dry_cold_prefers_fire_element");
    }

    // 5. Seasonal market-hour shift: Summer 19:30 still MarketStall vs Winter 19:30 closed HearthRest!
    {
        NpcBehaviorEnvInput summerEve{};
        summerEve.gameHour     = 19.5f;
        summerEve.season       = WorldSeason::Summer;
        summerEve.surfaceTempC = 17.0f;
        summerEve.windBeaufort = 2.0f;

        NpcBehaviorEnvInput winterEve{};
        winterEve.gameHour     = 19.5f;
        winterEve.season       = WorldSeason::Winter;
        winterEve.surfaceTempC = -4.0f;
        winterEve.windBeaufort = 2.0f;

        const NpcBehaviorEvaluation evSum = EvaluateNpcBehavior(summerEve);
        const NpcBehaviorEvaluation evWin = EvaluateNpcBehavior(winterEve);

        const auto& mSum = evSum.archetypes[static_cast<size_t>(NpcArchetype::Merchant)];
        const auto& mWin = evWin.archetypes[static_cast<size_t>(NpcArchetype::Merchant)];
        check(mSum.directive == RoutineDirective::MarketStall &&
              mWin.directive == RoutineDirective::HearthRest &&
              evSum.summary.marketActivity01 > 0.75f &&
              evWin.summary.marketActivity01 == 0.0f,
              "npc_behavior_seasonal_market_hours_summer_1930_open_vs_winter_closed");
    }

    // 6. RAW_GetNpcBehaviorState C ABI + [WorldAI] DynamicNpcBehavior INI round-trip & 0 disable gate
    {
        const LiveEngineConfig prevCfg = GetLiveConfig();
        LiveEngineConfig onCfg = DefaultLiveEngineConfig();
        onCfg.dynamicNpcBehaviorEnabled = true;
        SetLiveConfigSnapshot(onCfg);

        NpcBehaviorInit();
        NpcBehaviorEnvInput blizzard{};
        blizzard.gameHour     = 1.0f;
        blizzard.season       = WorldSeason::Winter;
        blizzard.surfaceTempC = -22.0f;
        blizzard.windBeaufort = 8.0f;
        blizzard.snow01       = 0.85f;
        NpcBehaviorUpdateEnv(blizzard);

        RawNpcBehaviorState stOn{};
        const int okOn = RAW_GetNpcBehaviorState(&stOn);
        const bool cAbiOnOk =
            okOn == 1 &&
            stOn.behaviorEnabled == 1u &&
            stOn.shelterPressure01 > 0.85f &&
            stOn.activeDirective[static_cast<size_t>(NpcArchetype::Villager)] ==
                static_cast<uint8_t>(RoutineDirective::SeekShelter);
        check(cAbiOnOk, "npc_behavior_c_abi_exports_live_blizzard_directives");

        // Verify [WorldAI] DynamicNpcBehavior INI parsing & 0 disable gate freezes updates
        const std::string defIni = FormatDefaultLiveConfigIni(DefaultLiveEngineConfig());
        LiveEngineConfig parsedDef{};
        ParseLiveConfigIni(defIni, parsedDef);
        LiveEngineConfig parsedOff{};
        ParseLiveConfigIni("[WorldAI]\nDynamicNpcBehavior=0\n", parsedOff);

        SetLiveConfigSnapshot(parsedOff);
        NpcBehaviorEnvInput sunny{};
        sunny.gameHour     = 12.0f;
        sunny.season       = WorldSeason::Summer;
        sunny.surfaceTempC = 22.0f;
        NpcBehaviorUpdateEnv(sunny); // MUST be ignored while DynamicNpcBehavior=0!

        RawNpcBehaviorState stOff{};
        RAW_GetNpcBehaviorState(&stOff);

        const bool iniGateOk =
            defIni.find("[WorldAI]") != std::string::npos &&
            defIni.find("DynamicNpcBehavior=1") != std::string::npos &&
            parsedDef.dynamicNpcBehaviorEnabled &&
            !parsedOff.dynamicNpcBehaviorEnabled &&
            stOff.behaviorEnabled == 0u &&
            stOff.shelterPressure01 > 0.85f; // Sunny update was properly gated off!
        check(iniGateOk, "npc_behavior_worldai_ini_roundtrip_and_disable_gate");

        NpcBehaviorShutdown();
        SetLiveConfigSnapshot(prevCfg);
    }

    if (!checkFn) {
        std::printf("NpcBehaviorTests completed: %s (failures=%d)\n",
                    failures == 0 ? "PASS" : "FAIL", failures);
    }
    return failures;
}

}  // namespace raw::rt

#ifdef RAW_NPC_BEHAVIOR_STANDALONE_MAIN
int main() {
    const int failures = raw::rt::RunNpcBehaviorTests(nullptr);
    return failures == 0 ? 0 : 1;
}
#endif
