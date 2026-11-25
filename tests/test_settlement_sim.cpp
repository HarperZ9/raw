// test_settlement_sim.cpp: Verification Suite for `raw::worldgen::RawSettlementSim`.
// Exports `int raw::worldgen::RunSettlementSimTests(void (*checkFn)(bool, const char*) = nullptr)`
// and supports `#ifdef RAW_SETTLEMENT_SIM_TEST_STANDALONE` standalone execution.
// Original work, MIT.

#include "RawSettlementSim.h"

#include <cmath>
#include <cstdio>

namespace raw::worldgen {

int RunSettlementSimTests(void (*checkFn)(bool, const char*)) {
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
    // Test 1: Planted-failure controls (zero/negative hours no-op, invalid HoldId)
    // =========================================================================
    {
        RawSettlementSim sim(1001u);
        const HoldState* badHold = sim.GetHold(HoldId::Count);
        const HoldState* badHold2 = sim.GetHold(static_cast<HoldId>(99));
        const double badCommodityTotal = sim.TotalWorldCommodityUnits(CommodityKind::Count);

        const float whiterunGrainBefore = sim.GetHold(HoldId::Whiterun)->stock[0];
        const double goldBefore         = sim.ComputeWorldTelemetry().totalGold;

        SettlementWeatherMap wx{};
        sim.StepHours(0.0f, wx);
        sim.StepHours(-12.0f, wx);

        const float whiterunGrainAfter = sim.GetHold(HoldId::Whiterun)->stock[0];
        const double goldAfter         = sim.ComputeWorldTelemetry().totalGold;

        check(badHold == nullptr && badHold2 == nullptr && badCommodityTotal == 0.0 &&
                  whiterunGrainBefore == whiterunGrainAfter && goldBefore == goldAfter &&
                  sim.Routes().size() >= 14u,
              "settlement_sim_planted_failure_controls");
    }

    // =========================================================================
    // Test 2: Exact Transit Commodity & Gold Conservation across 200 hours
    // =========================================================================
    {
        RawSettlementSim sim(2026u);
        std::array<double, kCommodityCount> initialTotals{};
        for (size_t c = 0u; c < kCommodityCount; ++c) {
            initialTotals[c] = sim.TotalWorldCommodityUnits(static_cast<CommodityKind>(c));
        }
        const double initialGold = sim.ComputeWorldTelemetry().totalGold;

        SettlementWeatherMap frozenProdWx{};
        frozenProdWx.enableProductionConsumption = false; // Pure trade conservation test!
        frozenProdWx.enableBanditAmbushes        = false;

        sim.StepHours(200.0f, frozenProdWx);

        double maxRelCommodityErr = 0.0;
        for (size_t c = 0u; c < kCommodityCount; ++c) {
            const double now = sim.TotalWorldCommodityUnits(static_cast<CommodityKind>(c));
            const double err = std::abs(now - initialTotals[c]) / std::max(1.0, initialTotals[c]);
            if (err > maxRelCommodityErr) maxRelCommodityErr = err;
        }
        const SettlementTelemetry tel = sim.ComputeWorldTelemetry();
        const double goldRelErr = std::abs(tel.totalGold - initialGold) / std::max(1.0, initialGold);

        std::printf(
            "  [Transit Conservation 200h] deliveries=%u activeCaravans=%u maxCommodityRelErr=%.2e goldRelErr=%.2e\n",
            tel.deliveriesCompleted, tel.totalCaravansActive, maxRelCommodityErr, goldRelErr);

        check(tel.deliveriesCompleted >= 5u && maxRelCommodityErr < 1.0e-4 && goldRelErr < 1.0e-4,
              "settlement_sim_exact_transit_conservation");
    }

    // =========================================================================
    // Test 3: Blizzard Blockade Price-Spike Proof (72-hour Winterhold/Windhelm blizzard)
    // =========================================================================
    {
        RawSettlementSim clearSim(3030u);
        RawSettlementSim blizzardSim(3030u);

        SettlementWeatherMap clearWx{};
        clearWx.enableProductionConsumption = true;
        clearWx.enableBanditAmbushes        = false;

        SettlementWeatherMap blizzardWx = clearWx;
        blizzardWx.byHold[static_cast<size_t>(HoldId::Windhelm)].blizzard01   = 0.92f;
        blizzardWx.byHold[static_cast<size_t>(HoldId::Winterhold)].blizzard01 = 0.95f;
        blizzardWx.byHold[static_cast<size_t>(HoldId::Dawnstar)].blizzard01   = 0.88f;

        uint32_t peakSnowbound = 0u;
        for (int h = 0; h < 72; ++h) {
            clearSim.StepHours(1.0f, clearWx);
            blizzardSim.StepHours(1.0f, blizzardWx);
            const uint32_t sb = blizzardSim.ComputeWorldTelemetry().snowboundCaravans;
            if (sb > peakSnowbound) peakSnowbound = sb;
        }

        const float clearWinterholdGrainPrice =
            clearSim.GetHold(HoldId::Winterhold)->price[static_cast<size_t>(CommodityKind::Grain)];
        const float blizzardWinterholdGrainPrice =
            blizzardSim.GetHold(HoldId::Winterhold)->price[static_cast<size_t>(CommodityKind::Grain)];
        const float clearTavernOcc = clearSim.GetHold(HoldId::Winterhold)->tavernOccupancy01;
        const float blizTavernOcc  = blizzardSim.GetHold(HoldId::Winterhold)->tavernOccupancy01;

        std::printf(
            "  [Blizzard Blockade 72h] peakSnowbound=%u WinterholdGrainPrice clear=%.2fg vs blizzard=%.2fg "
            "tavernOcc=%.2f->%.2f\n",
            peakSnowbound, clearWinterholdGrainPrice, blizzardWinterholdGrainPrice,
            clearTavernOcc, blizTavernOcc);

        check(peakSnowbound > 0u &&
                  blizzardWinterholdGrainPrice > clearWinterholdGrainPrice * 1.05f &&
                  blizTavernOcc > clearTavernOcc,
              "settlement_sim_blizzard_blockade_price_spike");
    }

    // =========================================================================
    // Test 4: 30-Day (720-Hour) Full 9-Hold Stability Proof
    // =========================================================================
    {
        RawSettlementSim sim(4040u);
        SettlementWeatherMap wx{};
        wx.seasonDaylightHours = 11.0f;

        bool allFiniteAndValid = true;
        for (int day = 0; day < 30; ++day) {
            // Mild seasonal weather variation across holds:
            wx.byHold[static_cast<size_t>(HoldId::Winterhold)].blizzard01 = (day % 7 == 0) ? 0.75f : 0.15f;
            wx.byHold[static_cast<size_t>(HoldId::Riften)].rain01         = (day % 5 == 0) ? 0.60f : 0.10f;
            sim.StepHours(24.0f, wx);

            for (size_t i = 0u; i < kHoldCount; ++i) {
                const HoldState* h = sim.GetHold(static_cast<HoldId>(i));
                if (!h || !std::isfinite(h->treasuryGold) || h->treasuryGold < 0.0f) {
                    allFiniteAndValid = false;
                }
                for (size_t c = 0u; c < kCommodityCount; ++c) {
                    if (!std::isfinite(h->stock[c]) || h->stock[c] < 0.0f ||
                        !std::isfinite(h->price[c]) || h->price[c] <= 0.0f) {
                        allFiniteAndValid = false;
                    }
                }
            }
        }

        const SettlementTelemetry tel = sim.ComputeWorldTelemetry();
        std::printf(
            "  [30-Day 9-Hold Sim] deliveries=%u activeCaravans=%u totalGold=%.0f meanPriceIdx=%.2f scarceHold=%s\n",
            tel.deliveriesCompleted, tel.totalCaravansActive, tel.totalGold,
            tel.meanPriceIndex, GetHoldName(tel.mostScarceHold));

        check(allFiniteAndValid && tel.deliveriesCompleted > 10u &&
                  std::isfinite(tel.meanPriceIndex) && tel.meanPriceIndex > 0.4f && tel.meanPriceIndex < 3.0f,
              "settlement_sim_30_day_9_hold_stability");
    }

    return failures;
}

}  // namespace raw::worldgen

#ifdef RAW_SETTLEMENT_SIM_TEST_STANDALONE
int main() {
    const int failures = raw::worldgen::RunSettlementSimTests(nullptr);
    std::printf("RawSettlementSim standalone test summary: %s (%d failures)\n",
                failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
#endif
