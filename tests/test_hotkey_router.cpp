// test_hotkey_router.cpp: Wave 61 Verification Suite for Foreground-Safe Debounced Live Hotkey
// Router (`rt_hotkey_router`) and `hotkeys` (12th) Console Dispatcher command.
// Pure deterministic key-state injection: NEVER polls live desktop keys during unit tests!
// Exposes BOTH `int run_hotkey_router_tests()` and `int raw::rt::RunHotkeyRouterTests(checkFn)`.
// Original work, MIT.

#include "rt_hotkey_router.h"
#include "rt_boot_selfcheck.h"
#include "rt_console_diag.h"
#include "rt_live_config.h"
#include "rt_live_dashboard.h"
#include "rt_profile_presets.h"
#include "rt_shader_hotreload.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace raw::rt {

namespace {

HotkeySampleInput MakeSample(bool fg, bool ctrl, int fnKey, double tsMs) noexcept {
    HotkeySampleInput s{};
    s.foregroundOwned = fg;
    s.ctrlDown        = ctrl;
    s.f8Down          = (fnKey == 8);
    s.f9Down          = (fnKey == 9);
    s.f10Down         = (fnKey == 10);
    s.f11Down         = (fnKey == 11);
    s.f12Down         = (fnKey == 12);
    s.timestampMs     = tsMs;
    return s;
}

}  // namespace

int RunHotkeyRouterTests(void (*checkFn)(bool, const char*) = nullptr) {
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
    // Check 1: Pure Foreground Safety, Edge Triggering & 250ms Debounce
    // -------------------------------------------------------------------------
    {
        HotkeyRouterCore core{};
        core.Reset();

        // 1a. Unfocused window MUST suppress chord (Ctrl+F9 while backgrounded)
        const auto rBg = core.Evaluate(MakeSample(false, true, 9, 1000.0));

        // 1b. Focused rising edge fires strictly once
        const auto rFire1 = core.Evaluate(MakeSample(true, true, 9, 1100.0));

        // 1c. Holding chord next frame MUST NOT re-fire (edge-triggered)
        const auto rHold = core.Evaluate(MakeSample(true, true, 9, 1116.0));

        // 1d. Release chord at 1150ms, then rapid re-press at 1200ms (100ms < 250ms debounce) -> blocked!
        (void)core.Evaluate(MakeSample(true, false, 0, 1150.0));
        const auto rDebounced = core.Evaluate(MakeSample(true, true, 9, 1200.0));

        // 1e. Release at 1250ms, then re-press at 1400ms (300ms >= 250ms debounce) -> fires cleanly!
        (void)core.Evaluate(MakeSample(true, false, 0, 1250.0));
        const auto rFire2 = core.Evaluate(MakeSample(true, true, 9, 1400.0));

        std::printf("[HotkeyRouter] Check1 Core: bgBlk=%u fire1=%s holdBlk=%u debBlk=%u fire2=%s sup=(bg:%u deb:%u)\n",
                    rBg.blockedByBackground ? 1u : 0u,
                    HotkeyActionName(rFire1.triggeredAction),
                    rHold.blockedByHold ? 1u : 0u,
                    rDebounced.blockedByDebounce ? 1u : 0u,
                    HotkeyActionName(rFire2.triggeredAction),
                    core.SuppressedBackground(), core.SuppressedDebounce());

        Check(rBg.triggeredAction == HotkeyAction::None && rBg.blockedByBackground &&
              rFire1.triggeredAction == HotkeyAction::ToggleHud &&
              rHold.triggeredAction == HotkeyAction::None && rHold.blockedByHold &&
              rDebounced.triggeredAction == HotkeyAction::None && rDebounced.blockedByDebounce &&
              rFire2.triggeredAction == HotkeyAction::ToggleHud &&
              core.SuppressedBackground() == 1u && core.SuppressedDebounce() == 1u,
              "HotkeyRouter core: background gate, single-edge trigger & 250ms debounce verified");
    }

    // -------------------------------------------------------------------------
    // Check 2: All 5 Live Chords (Ctrl+F8..Ctrl+F12) + C ABI + `hotkeys` 12th Console Command
    // -------------------------------------------------------------------------
    {
        std::error_code ec;
        const std::filesystem::path tmpDir =
            std::filesystem::temp_directory_path(ec) / "raw_hotkey_router_w61";
        std::filesystem::remove_all(tmpDir, ec);
        const std::filesystem::path shDir  = tmpDir / "raw_shaders";
        const std::filesystem::path outDir = tmpDir / "out";
        std::filesystem::create_directories(shDir, ec);
        std::filesystem::create_directories(outDir, ec);
        {
            std::ofstream f(shDir / "TestPass.hlsl", std::ios::binary);
            f << "float4 main() : SV_Target { return float4(1,1,1,1); }\n";
        }

        const LiveEngineConfig savedCfg = GetLiveConfig();
        (void)ProfilePresetsInit(QualityPreset::Balanced);
        (void)ShaderHotReloadInit(shDir.string());
        (void)LiveDashboardInit(outDir.string());
        (void)BootSelfCheckInit(tmpDir.string(), outDir.string());
        const std::string initMsg = HotkeyRouterInit();

        // Ctrl+F9: Toggle HUD ON -> OFF
        std::string logF9, logF10, logF8, logF11, logF12;
        const auto sF9 = HotkeyRouterInjectInput(MakeSample(true, true, 9, 2000.0), &logF9);
        (void)HotkeyRouterInjectInput(MakeSample(true, false, 0, 2050.0));

        // Ctrl+F10: Cycle Preset Balanced -> Competitive
        const auto sF10 = HotkeyRouterInjectInput(MakeSample(true, true, 10, 2300.0), &logF10);
        RawProfilePresetState ppAfterF10{};
        RAW_GetProfilePresetState(&ppAfterF10);
        (void)HotkeyRouterInjectInput(MakeSample(true, false, 0, 2350.0));

        // Ctrl+F8: Emergency SafeMode fallback
        const auto sF8 = HotkeyRouterInjectInput(MakeSample(true, true, 8, 2600.0), &logF8);
        RawProfilePresetState ppAfterF8{};
        RAW_GetProfilePresetState(&ppAfterF8);
        (void)HotkeyRouterInjectInput(MakeSample(true, false, 0, 2650.0));

        // Ctrl+F11: Live Shader Hot-Reload generation bump
        const auto sF11 = HotkeyRouterInjectInput(MakeSample(true, true, 11, 2900.0), &logF11);
        RawShaderHotReloadState shrAfterF11{};
        RAW_GetShaderHotReloadState(&shrAfterF11);
        (void)HotkeyRouterInjectInput(MakeSample(true, false, 0, 2950.0));

        // Ctrl+F12: Export Live Dashboard + Boot Self-Check
        const auto sF12 = HotkeyRouterInjectInput(MakeSample(true, true, 12, 3200.0), &logF12);
        const bool dashWrote = std::filesystem::exists(outDir / "raw_live_dashboard.json", ec) &&
                               std::filesystem::exists(outDir / "raw_live_dashboard.html", ec);
        const bool selfWrote = std::filesystem::exists(outDir / "boot_selfcheck.txt", ec);

        // C ABI & `hotkeys` console command verification
        RawHotkeyRouterState st{};
        const int rcNull = RAW_GetHotkeyRouterState(nullptr);
        const int rcOk   = RAW_GetHotkeyRouterState(&st);

        std::string respHotkeys, respHelp;
        const bool okCmd  = DispatchDiagnosticCommand("hotkeys", respHotkeys);
        const bool okHelp = DispatchDiagnosticCommand("help", respHelp);

        // Clean shutdown & restore defaults
        HotkeyRouterShutdown();
        BootSelfCheckShutdown();
        LiveDashboardShutdown();
        ShaderHotReloadShutdown();
        ProfilePresetsShutdown();
        SetLiveConfigSnapshot(savedCfg);

        std::filesystem::remove_all(tmpDir, ec);
        const bool cleaned = !std::filesystem::exists(tmpDir, ec);

        std::printf("[HotkeyRouter] Check2+3 LiveChords: init='%s' triggers=%u hud=%u f10=%s f8=%s shGen=%u dash=%u self=%u clean=%u\n",
                    initMsg.c_str(), st.totalTriggers, static_cast<unsigned>(st.hudVisible),
                    QualityPresetName(static_cast<QualityPreset>(ppAfterF10.activePreset)),
                    QualityPresetName(static_cast<QualityPreset>(ppAfterF8.activePreset)),
                    shrAfterF11.reloadGeneration, dashWrote ? 1u : 0u, selfWrote ? 1u : 0u, cleaned ? 1u : 0u);

        Check(sF9.triggeredAction == HotkeyAction::ToggleHud && st.hudVisible == 0u &&
              sF10.triggeredAction == HotkeyAction::CyclePreset &&
              ppAfterF10.activePreset == static_cast<uint8_t>(QualityPreset::Competitive) &&
              sF8.triggeredAction == HotkeyAction::EmergencySafeMode &&
              ppAfterF8.activePreset == static_cast<uint8_t>(QualityPreset::SafeMode) &&
              sF11.triggeredAction == HotkeyAction::ReloadShaders && shrAfterF11.reloadGeneration >= 1u &&
              sF12.triggeredAction == HotkeyAction::ExportDashboardSelfCheck && dashWrote && selfWrote &&
              rcNull == 0 && rcOk == 1 && st.routerActive == 1u && st.totalTriggers == 5u &&
              st.actionCounts[1] == 1u && st.actionCounts[2] == 1u && st.actionCounts[3] == 1u &&
              st.actionCounts[4] == 1u && st.actionCounts[5] == 1u &&
              okCmd && okHelp &&
              respHotkeys.find("Ctrl+F8") != std::string::npos &&
              respHotkeys.find("Ctrl+F12") != std::string::npos &&
              respHelp.find("hotkeys") != std::string::npos &&
              cleaned,
              "HotkeyRouter 5 live chords (Ctrl+F8..F12), C ABI & `hotkeys` 12th console command verified");
    }

    return localFail;
}

}  // namespace raw::rt

int run_hotkey_router_tests() {
    return raw::rt::RunHotkeyRouterTests(nullptr);
}

#ifdef RAW_HOTKEY_ROUTER_STANDALONE_MAIN
int main() {
    std::printf("=== RAW Wave 61: Foreground-Safe Debounced Live Hotkey Router Suite ===\n");
    const int fail = run_hotkey_router_tests();
    std::printf("=== RESULT: %s (failures=%d) ===\n", fail == 0 ? "ALL PASS" : "FAIL", fail);
    return fail == 0 ? 0 : 1;
}
#endif
