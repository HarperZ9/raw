// test_profile_presets.cpp: Wave 57 Verification Suite for Live Performance Budget / VRAM Watchdog,
// 4-Tier Quality Preset Profiles (`rt_profile_presets`), and `preset <name>` Console Dispatcher.
// Exposes both `int run_profile_presets_tests()` and `int raw::rt::RunProfilePresetsTests(checkFn)`.
// Original work, MIT.

#include "rt_profile_presets.h"
#include "rt_console_diag.h"
#include "rt_live_config.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace raw::rt {

int RunProfilePresetsTests(void (*checkFn)(bool, const char*) = nullptr) {
    int localFail = 0;
    auto Check = [&](bool cond, const char* msg) {
        if (checkFn) {
            checkFn(cond, msg);
        } else {
            std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", msg);
        }
        if (!cond) ++localFail;
    };

    // -------------------------------------------------------------------------
    // Check 1: 4 Validated Presets & Case-Insensitive Parser + Planted Failure Rejection
    // -------------------------------------------------------------------------
    {
        QualityPreset pUltra{}, pBal{}, pComp{}, pSafe{}, pAlias{}, pBad{};
        const bool okUltra = ParseQualityPresetName("ULTRA", pUltra) && pUltra == QualityPreset::Ultra;
        const bool okBal   = ParseQualityPresetName("  balanced \t", pBal) && pBal == QualityPreset::Balanced;
        const bool okComp  = ParseQualityPresetName("Competitive", pComp) && pComp == QualityPreset::Competitive;
        const bool okSafe  = ParseQualityPresetName("safemode", pSafe) && pSafe == QualityPreset::SafeMode;
        const bool okAlias = ParseQualityPresetName("safe", pAlias) && pAlias == QualityPreset::SafeMode;
        const bool rejNull = !ParseQualityPresetName(nullptr, pBad);
        const bool rejEmp  = !ParseQualityPresetName("   ", pBad);
        const bool rejUnk  = !ParseQualityPresetName("extreme_9000", pBad);

        const auto& sU = GetPresetSpec(QualityPreset::Ultra);
        const auto& sB = GetPresetSpec(QualityPreset::Balanced);
        const auto& sC = GetPresetSpec(QualityPreset::Competitive);
        const auto& sS = GetPresetSpec(QualityPreset::SafeMode);

        // Strict monotonic fidelity ordering (Ultra >= Balanced >= Competitive >= SafeMode)
        const bool monotonicQuality =
            sU.volumetricQuality >= sB.volumetricQuality &&
            sB.volumetricQuality >= sC.volumetricQuality &&
            sC.volumetricQuality >= sS.volumetricQuality &&
            sU.giRayBudget >= sB.giRayBudget &&
            sB.giRayBudget >= sC.giRayBudget &&
            sC.giRayBudget >= sS.giRayBudget &&
            sU.renderScaleFloor >= sB.renderScaleFloor &&
            sB.renderScaleFloor >= sS.renderScaleFloor &&
            std::fabs(sC.targetFps - 144.0f) < 1.0e-3f &&
            !sS.dynamicGovernorEnabled && !sS.skeletonXpbdEnabled &&
            sS.crashForensicsEnabled && sS.flightRecorderEnabled && sS.engineHealthReport;

        std::printf("[ProfilePresets] Check1 Specs: Ultra(vol=%.2f gi=%.2f) Bal(%.2f/%.2f) Comp(fps=%.0f vol=%.2f) Safe(gov=%u diag=%u)\n",
                    double(sU.volumetricQuality), double(sU.giRayBudget),
                    double(sB.volumetricQuality), double(sB.giRayBudget),
                    double(sC.targetFps), double(sC.volumetricQuality),
                    sS.dynamicGovernorEnabled ? 1u : 0u, sS.crashForensicsEnabled ? 1u : 0u);

        Check(okUltra && okBal && okComp && okSafe && okAlias &&
              rejNull && rejEmp && rejUnk && monotonicQuality,
              "ProfilePresets 4 validated tiers: monotonic fidelity ordering & fail-closed name parser");
    }

    // -------------------------------------------------------------------------
    // Check 2: Live Win32 GlobalMemoryStatusEx + RAM/VRAM/Frame Watchdog Pressure & Fallback Ladder
    // -------------------------------------------------------------------------
    {
        const SystemMemorySnapshot hostMem = QueryHostSystemMemory();

        // 2a. Nominal budget: no fallback recommended
        PerformanceBudgetInput nom{};
        nom.activePreset      = QualityPreset::Ultra;
        nom.ramUtilizationPct = 45.0f;
        nom.vramUsedMB        = 4096.0f;
        nom.vramBudgetMB      = 12288.0f;
        nom.frameP95Ms        = 15.8f;
        nom.targetFrameMs     = 16.67f;
        const auto vNom = EvaluatePerformanceBudget(nom);

        // 2b. High VRAM pressure (91%): Ultra -> Balanced single-step safe fallback
        PerformanceBudgetInput highVram = nom;
        highVram.vramUsedMB = 11200.0f;
        const auto vHighVram = EvaluatePerformanceBudget(highVram);

        // 2c. High frame-time overrun (1.50x): Balanced -> Competitive single-step fallback
        PerformanceBudgetInput highFrame = nom;
        highFrame.activePreset = QualityPreset::Balanced;
        highFrame.frameP95Ms   = 25.0f;
        const auto vHighFrame = EvaluatePerformanceBudget(highFrame);

        // 2d. Critical VRAM/frame overrun (98% VRAM): Ultra -> immediate SafeMode fallback
        PerformanceBudgetInput crit = nom;
        crit.vramUsedMB = 12100.0f;
        const auto vCrit = EvaluatePerformanceBudget(crit);

        // 2e. NaN/Inf resilience: must produce finite bounded output without crashing
        PerformanceBudgetInput nanIn{};
        nanIn.activePreset      = QualityPreset::Balanced;
        nanIn.ramUtilizationPct = std::nanf("");
        nanIn.vramUsedMB        = std::nanf("");
        nanIn.vramBudgetMB      = 0.0f;
        nanIn.frameP95Ms        = std::nanf("");
        nanIn.targetFrameMs     = 0.0f;
        const auto vNan = EvaluatePerformanceBudget(nanIn);

        std::printf("[ProfilePresets] Check2 Watchdog: hostRAM=%lluMB(%.1f%% ok=%u) | nom=%s highVram->%s highFrame->%s crit->%s\n",
                    static_cast<unsigned long long>(hostMem.totalPhysMB),
                    double(hostMem.ramUtilizationPct), hostMem.querySucceeded ? 1u : 0u,
                    BudgetPressureName(vNom.overallPressure),
                    QualityPresetName(vHighVram.recommendedPreset),
                    QualityPresetName(vHighFrame.recommendedPreset),
                    QualityPresetName(vCrit.recommendedPreset));

        Check(hostMem.querySucceeded && hostMem.totalPhysMB > 512ull &&
              hostMem.ramUtilizationPct >= 0.0f && hostMem.ramUtilizationPct <= 100.0f &&
              vNom.overallPressure == BudgetPressureLevel::Nominal && !vNom.fallbackRecommended &&
              vHighVram.vramPressure == BudgetPressureLevel::High &&
              vHighVram.fallbackRecommended && vHighVram.recommendedPreset == QualityPreset::Balanced &&
              vHighFrame.framePressure == BudgetPressureLevel::High &&
              vHighFrame.fallbackRecommended && vHighFrame.recommendedPreset == QualityPreset::Competitive &&
              vCrit.overallPressure == BudgetPressureLevel::Critical &&
              vCrit.fallbackRecommended && vCrit.recommendedPreset == QualityPreset::SafeMode &&
              std::isfinite(vNan.ramUtilizationPct) && std::isfinite(vNan.vramUtilizationPct) &&
              std::isfinite(vNan.frameOverrunRatio),
              "ProfilePresets watchdog: GlobalMemoryStatusEx valid & deterministic High/Critical fallback ladder");
    }

    // -------------------------------------------------------------------------
    // Check 3: Deterministic raw_engine.ini Preset Round-Trip + Live C ABI + ConsoleDiag `preset <name>`
    // -------------------------------------------------------------------------
    {
        // 3a. INI round-trip for Competitive (144 FPS, FovBreathEnabled=0) & SafeMode (DynamicGovernor=0)
        const std::string compIni = FormatPresetIni(QualityPreset::Competitive);
        LiveEngineConfig parsedComp{};
        const bool parseCompOk = ParseLiveConfigIni(compIni, parsedComp);

        const std::string safeIni = FormatPresetIni(QualityPreset::SafeMode);
        LiveEngineConfig parsedSafe{};
        const bool parseSafeOk = ParseLiveConfigIni(safeIni, parsedSafe);

        // 3b. Live runtime init + GPU VRAM injection + ConsoleDiag `preset` dispatch
        const LiveEngineConfig savedCfg = GetLiveConfig();
        const std::string initMsg = ProfilePresetsInit(QualityPreset::Balanced);

        // Planted failure: invalid preset via console dispatcher must fail closed
        std::string respBad, respQuery, respComp, respSafe, respHelp;
        const bool okBad   = DispatchDiagnosticCommand("preset hyper_bogus", respBad);
        const bool okQuery = DispatchDiagnosticCommand("preset", respQuery);
        const bool okComp  = DispatchDiagnosticCommand("preset competitive", respComp);
        const bool okHelp  = DispatchDiagnosticCommand("help", respHelp);

        const LiveEngineConfig afterCompCfg = GetLiveConfig();
        ProfilePresetsUpdateGpuVram(7900.0f, 8192.0f); // 96.4% VRAM -> Critical -> SafeMode recommendation

        RawProfilePresetState stCrit{};
        const int rcCrit = RAW_GetProfilePresetState(&stCrit);
        const int rcNull = RAW_GetProfilePresetState(nullptr);

        const bool okSafe = DispatchDiagnosticCommand("  PRESET  SafeMode ", respSafe);
        RawProfilePresetState stSafe{};
        const int rcSafe = RAW_GetProfilePresetState(&stSafe);
        const LiveEngineConfig afterSafeCfg = GetLiveConfig();

        // Restore clean defaults
        SetLiveConfigSnapshot(savedCfg);
        ProfilePresetsShutdown();

        std::printf("[ProfilePresets] Check3 LiveConsoleABI: init='%s' critRec=%s(p=%s) safeActive=%s switches=%u\n",
                    initMsg.c_str(),
                    QualityPresetName(static_cast<QualityPreset>(stCrit.recommendedPreset)),
                    BudgetPressureName(static_cast<BudgetPressureLevel>(stCrit.overallPressure)),
                    QualityPresetName(static_cast<QualityPreset>(stSafe.activePreset)),
                    stSafe.presetSwitchCount);

        Check(parseCompOk && std::fabs(parsedComp.targetFps - 144.0f) < 1.0e-3f && !parsedComp.fovBreathEnabled &&
              parseSafeOk && !parsedSafe.dynamicGovernorEnabled && !parsedSafe.skeletonXpbdEnabled &&
              !okBad && respBad.find("safemode") != std::string::npos &&
              okQuery && respQuery.find("active=Balanced") != std::string::npos &&
              okComp && respComp.find("applied=Competitive") != std::string::npos &&
              std::fabs(afterCompCfg.targetFps - 144.0f) < 1.0e-3f &&
              okHelp && respHelp.find("preset") != std::string::npos &&
              rcNull == 0 && rcCrit == 1 &&
              stCrit.overallPressure == static_cast<uint8_t>(BudgetPressureLevel::Critical) &&
              stCrit.recommendedPreset == static_cast<uint8_t>(QualityPreset::SafeMode) &&
              stCrit.fallbackRecommended == 1u &&
              okSafe && rcSafe == 1 &&
              stSafe.activePreset == static_cast<uint8_t>(QualityPreset::SafeMode) &&
              !afterSafeCfg.dynamicGovernorEnabled &&
              stSafe.presetSwitchCount >= 2u,
              "ProfilePresets INI round-trip, C ABI GPU VRAM watchdog & ConsoleDiag `preset <name>` live dispatch");
    }

    return localFail;
}

}  // namespace raw::rt

int run_profile_presets_tests() {
    return raw::rt::RunProfilePresetsTests(nullptr);
}

#ifdef RAW_PROFILE_PRESETS_STANDALONE_MAIN
int main() {
    std::printf("=== RAW Wave 57: Performance Budget / VRAM Watchdog & 4-Tier Presets Suite ===\n");
    const int fail = run_profile_presets_tests();
    std::printf("=== RESULT: %s (failures=%d) ===\n", fail == 0 ? "ALL PASS" : "FAIL", fail);
    return fail == 0 ? 0 : 1;
}
#endif
