// test_economy_ledger.cpp: Wave 42 verification suite for the Persistent 9-Hold Living Skyrim Economy Ledger
// (rt_economy_ledger.h/.cpp) & Dynamic NPC Weather/Night Shelter & Torch Heuristics.
// Compiles standalone (default) OR links into ce_runtime_check with -DRAW_ECONOMY_LEDGER_NO_MAIN.
// Original work, MIT.

#include "../src/engine_rt/rt_economy_ledger.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace raw::rt {

int RunEconomyLedgerTests(void (*checkFn)(bool, const char*) = nullptr) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* name) {
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s economy_ledger::%s\n", cond ? "PASS" : "FAIL", name);
        }
        if (cond) ++pass; else ++fail;
    };

    // 1. All 9 Skyrim Hold coordinate mappings (canonical capital & landmark Tamriel (x, y) coordinates):
    {
        struct HoldProbe { float x; float y; SkyrimHold expected; };
        const HoldProbe kProbes[] = {
            {  20480.0f,  -8192.0f, SkyrimHold::Whiterun   },  // Whiterun
            {  20480.0f, -45056.0f, SkyrimHold::Whiterun   },  // Riverwood
            { -69632.0f, 102400.0f, SkyrimHold::Haafingar  },  // Solitude
            {-102400.0f,  65536.0f, SkyrimHold::Haafingar  },  // Dragon Bridge
            { 135168.0f,  32768.0f, SkyrimHold::Eastmarch  },  // Windhelm
            { 172032.0f, -94208.0f, SkyrimHold::TheRift    },  // Riften
            {-176128.0f,      0.0f, SkyrimHold::TheReach   },  // Markarth
            { -32768.0f, -90112.0f, SkyrimHold::Falkreath  },  // Falkreath
            { 114688.0f, 102400.0f, SkyrimHold::Winterhold },  // Winterhold
            {  24576.0f, 106496.0f, SkyrimHold::ThePale    },  // Dawnstar
            { -61440.0f,  45056.0f, SkyrimHold::Hjaalmarch },  // Morthal
        };
        bool allMapped = true;
        for (const auto& pr : kProbes) {
            if (ClassifySkyrimHold(pr.x, pr.y) != pr.expected) {
                allMapped = false;
                break;
            }
        }
        // Verify distinct non-empty names across all 9 holds
        bool namesValid = true;
        for (size_t i = 0; i < kSkyrimHoldCount; ++i) {
            const auto h = static_cast<SkyrimHold>(i);
            if (!SkyrimHoldName(h) || std::strlen(SkyrimHoldName(h)) < 4u ||
                !SkyrimHoldCapital(h) || std::strlen(SkyrimHoldCapital(h)) < 4u) {
                namesValid = false;
            }
        }
        check(allMapped && namesValid, "all_9_hold_coordinate_mappings");
    }

    // 2. Hold production specialties (Whiterun grain, Markarth ore, Falkreath timber, Dawnstar furs, Riften alchemy):
    {
        EconomyLedger ledger;
        const auto& wh = ledger.GetHold(SkyrimHold::Whiterun);
        const auto& mk = ledger.GetHold(SkyrimHold::TheReach);
        const auto& fk = ledger.GetHold(SkyrimHold::Falkreath);
        const auto& ds = ledger.GetHold(SkyrimHold::ThePale);
        const auto& rf = ledger.GetHold(SkyrimHold::TheRift);
        const auto& wi = ledger.GetHold(SkyrimHold::Winterhold);

        const bool ok =
            (wh.markets[static_cast<size_t>(CommodityKind::FoodGrain)].baseProduction >
             mk.markets[static_cast<size_t>(CommodityKind::FoodGrain)].baseProduction * 2.0f) &&
            (mk.markets[static_cast<size_t>(CommodityKind::OreMetal)].baseProduction >
             wh.markets[static_cast<size_t>(CommodityKind::OreMetal)].baseProduction * 1.8f) &&
            (fk.markets[static_cast<size_t>(CommodityKind::TimberFuel)].baseProduction >
             wi.markets[static_cast<size_t>(CommodityKind::TimberFuel)].baseProduction * 2.5f) &&
            (ds.markets[static_cast<size_t>(CommodityKind::FursClothing)].baseProduction >
             mk.markets[static_cast<size_t>(CommodityKind::FursClothing)].baseProduction * 1.4f) &&
            (rf.markets[static_cast<size_t>(CommodityKind::AlchemySupplies)].baseProduction >
             wh.markets[static_cast<size_t>(CommodityKind::AlchemySupplies)].baseProduction * 1.5f);
        check(ok, "hold_regional_production_specialties");
    }

    // 3. Winter northern scarcity (Winterhold/Dawnstar) vs autumn Whiterun harvest abundance:
    {
        EconomyLedger winterLedger;
        const uint32_t winterDoy = ComputeTamrielDayOfYear(11.0f /*Evening Star*/, 18.0f);
        for (int d = 0; d < 10; ++d) {
            EconomyDayStepInput st{};
            st.dayOfYear  = (winterDoy + static_cast<uint32_t>(d)) % 365u;
            st.storm01    = 0.20f;
            st.snowDepthU = 18.0f;
            st.focusHold  = SkyrimHold::Winterhold;
            winterLedger.StepDay(st);
        }

        EconomyLedger harvestLedger;
        const uint32_t harvestDoy = ComputeTamrielDayOfYear(7.0f /*Last Seed*/, 25.0f);
        for (int d = 0; d < 10; ++d) {
            EconomyDayStepInput st{};
            st.dayOfYear  = (harvestDoy + static_cast<uint32_t>(d)) % 365u;
            st.storm01    = 0.0f;
            st.snowDepthU = 0.0f;
            st.focusHold  = SkyrimHold::Whiterun;
            harvestLedger.StepDay(st);
        }

        const auto wiWinter  = winterLedger.QueryHoldBarterMultipliers(SkyrimHold::Winterhold, 0.20f, 18.0f, winterDoy);
        const auto dsWinter  = winterLedger.QueryHoldBarterMultipliers(SkyrimHold::ThePale,    0.20f, 18.0f, winterDoy);
        const auto whHarvest = harvestLedger.QueryHoldBarterMultipliers(SkyrimHold::Whiterun,  0.0f,  0.0f,  harvestDoy);

        const float whGrainHarvestPrice =
            harvestLedger.GetHold(SkyrimHold::Whiterun).markets[static_cast<size_t>(CommodityKind::FoodGrain)].priceIndex;
        const float wiGrainWinterPrice =
            winterLedger.GetHold(SkyrimHold::Winterhold).markets[static_cast<size_t>(CommodityKind::FoodGrain)].priceIndex;
        const float wiFuelWinterPrice =
            winterLedger.GetHold(SkyrimHold::Winterhold).markets[static_cast<size_t>(CommodityKind::TimberFuel)].priceIndex;

        const bool ok =
            (whGrainHarvestPrice < 0.85f) &&
            (wiGrainWinterPrice > 1.35f) &&
            (wiFuelWinterPrice  > 1.35f) &&
            (wiWinter.buyPriceMult > whHarvest.buyPriceMult * 1.25f) &&
            (dsWinter.buyPriceMult > whHarvest.buyPriceMult * 1.15f) &&
            (wiWinter.barterMin    > whHarvest.barterMin + 0.25f);
        check(ok, "winter_northern_scarcity_vs_autumn_whiterun_harvest");
    }

    // 4. Storm & blizzard caravan disruption spikes arctic hold prices cleanly above calm winter:
    {
        const uint32_t winterDoy = ComputeTamrielDayOfYear(0.0f /*Morning Star*/, 12.0f);
        EconomyLedger calmLedger;
        EconomyLedger blizzardLedger;

        for (int d = 0; d < 7; ++d) {
            EconomyDayStepInput calmIn{};
            calmIn.dayOfYear  = (winterDoy + static_cast<uint32_t>(d)) % 365u;
            calmIn.storm01    = 0.0f;
            calmIn.snowDepthU = 6.0f;
            calmIn.focusHold  = SkyrimHold::Winterhold;
            calmLedger.StepDay(calmIn);

            EconomyDayStepInput blizIn{};
            blizIn.dayOfYear  = (winterDoy + static_cast<uint32_t>(d)) % 365u;
            blizIn.storm01    = 0.95f;
            blizIn.snowDepthU = 34.0f;
            blizIn.focusHold  = SkyrimHold::Winterhold;
            blizzardLedger.StepDay(blizIn);
        }

        const auto qCalm = calmLedger.QueryHoldBarterMultipliers(SkyrimHold::Winterhold, 0.0f, 6.0f, winterDoy);
        const auto qBliz = blizzardLedger.QueryHoldBarterMultipliers(SkyrimHold::Winterhold, 0.95f, 34.0f, winterDoy);

        // Sum caravan flow across all 14 routes: blizzard must choke total inter-hold cargo significantly!
        float calmFlow = 0.0f, blizFlow = 0.0f;
        for (size_t ri = 0; ri < kCaravanRouteCount; ++ri) {
            calmFlow += calmLedger.GetRoute(ri).lastFlowUnits;
            blizFlow += blizzardLedger.GetRoute(ri).lastFlowUnits;
        }

        const bool ok =
            (qBliz.caravanDisruption01 > 0.65f) &&
            (qBliz.caravanDisruption01 > qCalm.caravanDisruption01 + 0.45f) &&
            (blizFlow < calmFlow * 0.55f) &&
            (qBliz.buyPriceMult > qCalm.buyPriceMult + 0.15f) &&
            (qBliz.barterMin    > qCalm.barterMin + 0.15f);
        check(ok, "storm_caravan_disruption_price_spike");
    }

    // 5. Player oversupply depression (iron dagger dump) & multi-day caravan equilibrium recovery:
    {
        EconomyLedger ledger;
        const uint32_t springDoy = ComputeTamrielDayOfYear(4.0f /*Second Seed*/, 10.0f);
        const auto qBefore = ledger.QueryHoldBarterMultipliers(SkyrimHold::Whiterun, 0.0f, 0.0f, springDoy);

        // Player floods Whiterun's OreMetal market with 90 units of crafted iron daggers!
        ledger.RecordPlayerTrade(SkyrimHold::Whiterun, CommodityKind::OreMetal, 90.0f, true /*playerSoldToMerchant*/);
        const auto qFlooded = ledger.QueryHoldBarterMultipliers(SkyrimHold::Whiterun, 0.0f, 0.0f, springDoy);
        const float floodedSurplus =
            ledger.GetHold(SkyrimHold::Whiterun).markets[static_cast<size_t>(CommodityKind::OreMetal)].playerSurplus;

        // Simulate 14 calm trade days: caravans export Whiterun's metal surplus & local market clears.
        for (int d = 0; d < 14; ++d) {
            EconomyDayStepInput st{};
            st.dayOfYear = (springDoy + static_cast<uint32_t>(d)) % 365u;
            st.storm01   = 0.0f;
            st.focusHold = SkyrimHold::Whiterun;
            ledger.StepDay(st);
        }
        const auto qRecovered = ledger.QueryHoldBarterMultipliers(SkyrimHold::Whiterun, 0.0f, 0.0f, springDoy + 14u);
        const float recoveredSurplus =
            ledger.GetHold(SkyrimHold::Whiterun).markets[static_cast<size_t>(CommodityKind::OreMetal)].playerSurplus;

        const bool ok =
            (floodedSurplus >= 85.0f) &&
            (qFlooded.sellPriceMult < qBefore.sellPriceMult - 0.15f) &&
            (qFlooded.barterSellMax < qBefore.barterSellMax - 0.10f) &&
            (recoveredSurplus < floodedSurplus * 0.15f) &&
            (qRecovered.sellPriceMult > qFlooded.sellPriceMult + 0.12f) &&
            (qRecovered.barterSellMax > qFlooded.barterSellMax + 0.08f);
        check(ok, "player_oversupply_depression_and_caravan_recovery");
    }

    // 6. Full atomic JSON save / reload round-trip + corrupt/missing JSON fail-closed!
    {
        EconomyLedger orig;
        const uint32_t doy = ComputeTamrielDayOfYear(10.0f /*Sun's Dusk*/, 5.0f);
        orig.RecordPlayerTrade(SkyrimHold::TheReach, CommodityKind::OreMetal, 48.0f, true);
        orig.RecordPlayerTrade(SkyrimHold::Winterhold, CommodityKind::FoodGrain, 22.0f, false);
        for (int d = 0; d < 5; ++d) {
            EconomyDayStepInput st{};
            st.dayOfYear  = (doy + static_cast<uint32_t>(d)) % 365u;
            st.storm01    = 0.60f;
            st.snowDepthU = 20.0f;
            st.focusHold  = SkyrimHold::Winterhold;
            orig.StepDay(st);
        }
        orig.SetLastCalendarDaysPassed(42.75f);

        const std::filesystem::path tmpDir = std::filesystem::temp_directory_path();
        const std::filesystem::path savePath = tmpDir / "raw_w42_economy_roundtrip.json";
        const std::filesystem::path corruptPath = tmpDir / "raw_w42_economy_corrupt.json";
        const std::filesystem::path missingPath = tmpDir / "raw_w42_economy_does_not_exist.json";

        std::error_code ec;
        std::filesystem::remove(savePath, ec);
        std::filesystem::remove(corruptPath, ec);
        std::filesystem::remove(missingPath, ec);

        const bool saved = orig.SaveToJson(savePath.string());
        EconomyLedger reloaded;
        const bool loaded = reloaded.LoadFromJson(savePath.string());

        bool exactParity = saved && loaded &&
                           reloaded.TotalDaysSimulated() == orig.TotalDaysSimulated() &&
                           reloaded.TotalPlayerTrades() == orig.TotalPlayerTrades() &&
                           reloaded.LastDayOfYear() == orig.LastDayOfYear() &&
                           std::fabs(reloaded.LastCalendarDaysPassed() - orig.LastCalendarDaysPassed()) < 1e-3f;
        for (size_t hi = 0; hi < kSkyrimHoldCount && exactParity; ++hi) {
            const auto h = static_cast<SkyrimHold>(hi);
            const auto& ha = orig.GetHold(h);
            const auto& hb = reloaded.GetHold(h);
            if (ha.playerTrades != hb.playerTrades || std::fabs(ha.disruption01 - hb.disruption01) > 1e-3f) {
                exactParity = false;
                break;
            }
            for (size_t ci = 0; ci < kCommodityCount; ++ci) {
                if (std::fabs(ha.markets[ci].stock - hb.markets[ci].stock) > 1e-2f ||
                    std::fabs(ha.markets[ci].playerSurplus - hb.markets[ci].playerSurplus) > 1e-2f ||
                    std::fabs(ha.markets[ci].priceIndex - hb.markets[ci].priceIndex) > 1e-2f) {
                    exactParity = false;
                    break;
                }
            }
        }
        for (size_t ri = 0; ri < kCaravanRouteCount && exactParity; ++ri) {
            if (std::fabs(orig.GetRoute(ri).disruption01 - reloaded.GetRoute(ri).disruption01) > 1e-3f ||
                std::fabs(orig.GetRoute(ri).lastFlowUnits - reloaded.GetRoute(ri).lastFlowUnits) > 1e-2f) {
                exactParity = false;
                break;
            }
        }

        // Corrupt JSON, truncated JSON, wrong schemaVersion, NaN injection, and missing file MUST fail closed!
        EconomyLedger guardLedger = reloaded;
        const float preStock = guardLedger.GetHold(SkyrimHold::TheReach).markets[2].stock;

        const bool missRejected = !guardLedger.LoadFromJson(missingPath.string());
        const bool emptyRejected = !guardLedger.DeserializeFromJson("");
        const bool truncRejected = !guardLedger.DeserializeFromJson("{\"schemaVersion\": 1, \"holds\": [");
        const bool badVerRejected = !guardLedger.DeserializeFromJson("{\"schemaVersion\": 99, \"holds\": [], \"routes\": []}");
        {
            std::ofstream cf(corruptPath, std::ios::binary | std::ios::trunc);
            cf << "{\"schemaVersion\": 1, \"holds\": [{\"id\": 0, \"markets\": [{\"stock\": NaN}]}], \"routes\": []}";
        }
        const bool corruptFileRejected = !guardLedger.LoadFromJson(corruptPath.string());
        const bool statePreserved =
            std::fabs(guardLedger.GetHold(SkyrimHold::TheReach).markets[2].stock - preStock) < 1e-4f &&
            guardLedger.TotalDaysSimulated() == reloaded.TotalDaysSimulated();

        std::filesystem::remove(savePath, ec);
        std::filesystem::remove(corruptPath, ec);

        check(exactParity && missRejected && emptyRejected && truncRejected &&
              badVerRejected && corruptFileRejected && statePreserved,
              "atomic_json_roundtrip_and_corrupt_fail_closed");
    }

    // 7. Dynamic NPC weather/night shelter, hearth & torch heuristics:
    {
        // Midday clear in Whiterun -> NormalRoutine
        NpcEcosystemInput dayClear{};
        dayClear.gameHour = 13.0f;
        dayClear.ambientTempC = 14.0f;
        dayClear.hold = SkyrimHold::Whiterun;
        const auto dDay = EvaluateNpcShelterTorchHeuristic(dayClear);

        // Midnight clear outdoors -> EquipTorch
        NpcEcosystemInput nightClear = dayClear;
        nightClear.gameHour = 23.0f;
        nightClear.ambientTempC = 6.0f;
        const auto dNight = EvaluateNpcShelterTorchHeuristic(nightClear);

        // Severe blizzard outdoors in Winterhold -> SeekShelter
        NpcEcosystemInput blizzard{};
        blizzard.gameHour = 16.0f;
        blizzard.snow01 = 0.90f;
        blizzard.storm01 = 0.92f;
        blizzard.snowDepthU = 28.0f;
        blizzard.windSpeedMs = 22.0f;
        blizzard.ambientTempC = -14.0f;
        blizzard.hold = SkyrimHold::Winterhold;
        blizzard.caravanDisruption01 = 0.85f;
        const auto dBliz = EvaluateNpcShelterTorchHeuristic(blizzard);

        // Blizzard right next to campfire/brazier -> WarmAtBrazier
        NpcEcosystemInput hearth = blizzard;
        hearth.nearFire = true;
        const auto dHearth = EvaluateNpcShelterTorchHeuristic(hearth);

        // Active combat overrides to AlertPatrol with alertness=1.0
        NpcEcosystemInput combat = blizzard;
        combat.inCombat = true;
        const auto dCombat = EvaluateNpcShelterTorchHeuristic(combat);

        const bool ok =
            (dDay.directive == WorldNpcDirective::NormalRoutine && !dDay.shouldCarryTorch && !dDay.shouldSeekShelter) &&
            (dNight.directive == WorldNpcDirective::EquipTorch && dNight.shouldCarryTorch && dNight.torchNeed01 > 0.75f) &&
            (dBliz.directive == WorldNpcDirective::SeekShelter && dBliz.shouldSeekShelter && dBliz.shelterUrgency01 > 0.75f && dBliz.alertness01 > 0.50f) &&
            (dHearth.directive == WorldNpcDirective::WarmAtBrazier && dHearth.shouldWarmAtHearth) &&
            (dCombat.directive == WorldNpcDirective::AlertPatrol && dCombat.alertness01 >= 0.99f);
        check(ok, "dynamic_npc_weather_night_shelter_and_torch_heuristics");
    }

    return fail;
}

}  // namespace raw::rt

#ifndef RAW_ECONOMY_LEDGER_NO_MAIN
int main() {
    const int fail = raw::rt::RunEconomyLedgerTests();
    std::printf("economy_ledger: %s (%d failures)\n", fail == 0 ? "ALL PASS" : "FAILED", fail);
    return fail == 0 ? 0 : 1;
}
#endif
