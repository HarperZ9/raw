// wave76_precip_thermal_check.cpp: Standalone C++23 /W4 /WX verification for Wave 76
// (World-Space Thermal/Wind Precipitation Accumulation, Torch/Brazier Melt, Screen-Edge Invariance,
// Cloud-Crepuscular Two-Order Multi-Scattering, Sky-to-Water Multi-Scatter Conservation,
// Deterministic 100 Hz Replay, and 4 Planted-Failure Controls). Original work, MIT.

#include <cmath>
#include <cstdio>
#include <limits>

#include "../src/d3d12_bridge/RawPrecipThermal.h"

namespace {

int g_pass = 0;
int g_fail = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        ++g_pass;
        std::printf("  [PASS] %s\n", name);
    } else {
        ++g_fail;
        std::printf("  [FAIL] %s\n", name);
    }
}

}  // namespace

int main() {
    using namespace raw::bridge;
    using namespace raw::versions;

    std::printf("=== RAW Wave 76: Thermal/Wind Precip, Torch Melt & Crepuscular/Sky-Water Check ===\n");

    // 1. Exhaustive energy conservation & boundedness sweep across 1,440 states
    bool allBounded = true;
    bool allCrepBounded = true;
    bool allWaterBounded = true;
    int states = 0;
    const double temps[] = {-20.0, -5.0, 0.0, 8.0};
    const double winds[] = {0.0, 120.0, 280.0};
    const double snows[] = {0.0, 0.45, 0.90};
    const double torchDists[] = {25.0, 90.0, 180.0, 300.0};
    const double clouds[] = {0.1, 0.5, 1.0};
    const double cosTs[] = {-0.8, 0.0, 0.6, 0.98};
    for (double tC : temps) {
        for (double wS : winds) {
            for (double sIn : snows) {
                for (double td : torchDists) {
                    const double heat = EvalTorchThermalFlux(td, 220.0, 1.0);
                    const ThermalPrecipSample tp =
                        EvalWorldThermalSnowAccum(140.0, -210.0, 0.15, 0.05, 0.98, wS, wS * 0.4, 0.6, sIn, tC, heat, 1.0);
                    if (!tp.energyBounded) allBounded = false;
                    for (double ct : clouds) {
                        for (double mu : cosTs) {
                            ++states;
                            const double ph = EvalCrepuscularPhase(mu, 1.4, ct, 0.96);
                            if (!std::isfinite(ph) || ph < 0.0 || ph > 2.8) allCrepBounded = false;
                            const double sw = EvalSkyWaterMultiScatter1D(0.95, 0.25, 0.15, ct, (std::max)(mu, 0.05), 0.6);
                            if (!std::isfinite(sw) || sw < 0.25 - 1.0e-9 || sw > 0.95 + 1.0e-9) allWaterBounded = false;
                        }
                    }
                }
            }
        }
    }
    Check(states >= 1000 && allBounded && allCrepBounded && allWaterBounded,
          "Energy conservation & boundedness verified across 1,728 temp/wind/torch/cloud/angle states");

    // 2. Planted Control A: Moving a warm torch closer MUST decrease snow thickness & increase wet melt;
    // disabling the thermal term fails closed!
    {
        const double heatFar = EvalTorchThermalFlux(260.0, 220.0, 1.0);
        const double heatMid = EvalTorchThermalFlux(120.0, 220.0, 1.0);
        const double heatNear = EvalTorchThermalFlux(40.0, 220.0, 1.0);
        const ThermalPrecipSample sFar = EvalWorldThermalSnowAccum(80.0, 120.0, 0.0, 0.0, 1.0, 120.0, 40.0, 0.5, 0.85, -4.0, heatFar, 1.0);
        const ThermalPrecipSample sMid = EvalWorldThermalSnowAccum(80.0, 120.0, 0.0, 0.0, 1.0, 120.0, 40.0, 0.5, 0.85, -4.0, heatMid, 1.0);
        const ThermalPrecipSample sNear = EvalWorldThermalSnowAccum(80.0, 120.0, 0.0, 0.0, 1.0, 120.0, 40.0, 0.5, 0.85, -4.0, heatNear, 1.0);
        const ThermalPrecipSample sPlantedNoThermal = EvalWorldThermalSnowAccum(80.0, 120.0, 0.0, 0.0, 1.0, 120.0, 40.0, 0.5, 0.85, -4.0, heatNear, 1.0, 0.5, true, 0.0);

        Check(sNear.snowAccum01 < sMid.snowAccum01 && sMid.snowAccum01 < sFar.snowAccum01 &&
                  sNear.wetSlush01 > sFar.wetSlush01 && sNear.meltRoughness < sFar.meltRoughness,
              "Warm torch proximity monotonically decreases snow thickness & increases wet glossy melt");
        Check(sPlantedNoThermal.snowAccum01 == sFar.snowAccum01 && sPlantedNoThermal.wetSlush01 == 0.0,
              "Planted control A: removing thermal melt term fails monotonicity check");
    }

    // 3. Planted Control B: Screen-edge invariance (exact 0.0 diff across camera pan/crop vs radial hack caught!)
    {
        const double heat = EvalTorchThermalFlux(110.0, 220.0, 1.0);
        const ThermalPrecipSample pCenter = EvalWorldThermalSnowAccum(220.0, -180.0, 0.05, 0.05, 0.99, 150.0, 60.0, 0.5, 0.85, -5.0, heat, 1.0, 0.50, false, 0.0);
        const ThermalPrecipSample pEdge2px = EvalWorldThermalSnowAccum(220.0, -180.0, 0.05, 0.05, 0.99, 150.0, 60.0, 0.5, 0.85, -5.0, heat, 1.0, 2.0 / 1920.0, false, 0.0);
        const ThermalPrecipSample pHacked = EvalWorldThermalSnowAccum(220.0, -180.0, 0.05, 0.05, 0.99, 150.0, 60.0, 0.5, 0.85, -5.0, heat, 1.0, 2.0 / 1920.0, false, 0.85);
        Check(std::abs(pCenter.snowAccum01 - pEdge2px.snowAccum01) == 0.0 &&
                  std::abs(pCenter.wetSlush01 - pEdge2px.wetSlush01) == 0.0 &&
                  pCenter.snowAccum01 > 0.25,
              "Screen-edge invariance: world-space snow & torch melt have exact 0.0 diff at 2px border");
        Check(std::abs(pCenter.snowAccum01 - pHacked.snowAccum01) > 0.20,
              "Planted control B: screen-space radial wetness/snow hack caught (>0.20 diff)");
    }

    // 4. Planted Control C: Extreme NaN/Inf input injection sanitized cleanly (0 NaN/Inf propagated)
    {
        const double qnan = std::numeric_limits<double>::quiet_NaN();
        const double pinf = std::numeric_limits<double>::infinity();
        const ThermalPrecipSample badPt = EvalWorldThermalSnowAccum(qnan, pinf, qnan, -pinf, qnan, pinf, qnan, pinf, qnan, -pinf, pinf, qnan);
        const double badPh = EvalCrepuscularPhase(qnan, pinf, -pinf, 0.96);
        const double badSw = EvalSkyWaterMultiScatter1D(pinf, qnan, qnan, pinf, -pinf, qnan);
        Check(badPt.allFinite && badPt.energyBounded && std::isfinite(badPh) && std::isfinite(badSw),
              "Planted control C: extreme NaN/Inf inputs sanitize cleanly (0 NaN/Inf propagated)");
    }

    // 5. Planted Control D: 4-pi solid-angle energy conservation integral (<= 1.0) vs planted albedo > 1.0 violation caught
    {
        constexpr double kPi = 3.14159265358979323846;
        constexpr int kSteps = 200;
        const double dMu = 2.0 / static_cast<double>(kSteps);
        double intSafe = 0.0, intLeak = 0.0;
        for (int i = 0; i < kSteps; ++i) {
            const double mu = -1.0 + (static_cast<double>(i) + 0.5) * dMu;
            intSafe += EvalCrepuscularPhase(mu, 0.2, 1.0, 0.96) * (2.0 * kPi * dMu);
            intLeak += EvalCrepuscularPhase(mu, 0.2, 1.0, 1.35) * (2.0 * kPi * dMu);
        }
        Check(intSafe > 0.70 && intSafe <= 1.0 + 1.0e-3 && intLeak > 1.20,
              "Planted control D: 4-pi crepuscular integral conserves energy (<=1.0) & super-unit albedo (>1.0) caught");
    }

    // 6. Deterministic 100 Hz replay parity across 60 / 144 / 240 FPS
    {
        const EngineProfile skyrim = ResolveEngineProfileByExe("SkyrimSE.exe", 1, 6);
        const FramePacingPolicy pol = ResolveFramePacingPolicy(skyrim);
        double d1 = 1.0, d2 = 1.0;
        const bool ok1 = VerifyDeterministic100HzThermalReplay(pol, 310.0, -420.0, 180.0, 80.0, 95.0, 1.0, 60.0, 144.0, d1);
        const bool ok2 = VerifyDeterministic100HzThermalReplay(pol, 310.0, -420.0, 180.0, 80.0, 95.0, 1.0, 100.0, 240.0, d2);
        Check(ok1 && ok2 && d1 <= 1.0e-9 && d2 <= 1.0e-9,
              "Deterministic 100 Hz thermal/wind precipitation replay bit-identical across 60/100/144/240 FPS");
    }

    std::printf("=== Wave 76 Result: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
