// rt_hotkey_router.cpp: Wave 61 Foreground-Safe Debounced Live Hotkey Router (`raw::rt`).
// Original work, MIT.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "rt_hotkey_router.h"

#include "rt_boot_selfcheck.h"
#include "rt_live_dashboard.h"
#include "rt_profile_presets.h"
#include "rt_shader_hotreload.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>

namespace raw::rt {
namespace {

struct HotkeyRouterRuntime {
    std::mutex           mu;
    bool                 initialized = false;
    HotkeyRouterCore     core{};
    RawHotkeyRouterState state{};
} g_hr;

void CopyStrTrunc(char* dst, size_t cap, const std::string& src) noexcept {
    if (!dst || cap == 0u) return;
    const size_t n = std::min(src.size(), cap - 1u);
    if (n > 0u) std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

QualityPreset NextPresetInCycle(QualityPreset cur) noexcept {
    switch (cur) {
        case QualityPreset::Ultra:       return QualityPreset::Balanced;
        case QualityPreset::Balanced:    return QualityPreset::Competitive;
        case QualityPreset::Competitive: return QualityPreset::SafeMode;
        case QualityPreset::SafeMode:
        default:                         return QualityPreset::Ultra;
    }
}

#ifndef RAW_SIM_PURE_TEST
bool IsHostProcessForeground() noexcept {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0u;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

bool IsVkDown(int vk) noexcept {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}
#endif

}  // namespace

const char* HotkeyActionName(HotkeyAction action) noexcept {
    switch (action) {
        case HotkeyAction::None:                     return "None";
        case HotkeyAction::EmergencySafeMode:        return "EmergencySafeMode";
        case HotkeyAction::ToggleHud:                return "ToggleHud";
        case HotkeyAction::CyclePreset:              return "CyclePreset";
        case HotkeyAction::ReloadShaders:            return "ReloadShaders";
        case HotkeyAction::ExportDashboardSelfCheck: return "ExportDashboardSelfCheck";
        default:                                     return "Unknown";
    }
}

const char* HotkeyChordLabel(HotkeyAction action) noexcept {
    switch (action) {
        case HotkeyAction::EmergencySafeMode:        return "Ctrl+F8";
        case HotkeyAction::ToggleHud:                return "Ctrl+F9";
        case HotkeyAction::CyclePreset:              return "Ctrl+F10";
        case HotkeyAction::ReloadShaders:            return "Ctrl+F11";
        case HotkeyAction::ExportDashboardSelfCheck: return "Ctrl+F12";
        default:                                     return "None";
    }
}

void HotkeyRouterCore::Reset() noexcept {
    for (int i = 0; i < 6; ++i) {
        m_prevChordDown[i]  = false;
        m_lastFireTimeMs[i] = -1e9;
    }
    m_suppressedBackground = 0u;
    m_suppressedDebounce   = 0u;
}

HotkeyStepResult HotkeyRouterCore::Evaluate(const HotkeySampleInput& in) noexcept {
    HotkeyStepResult res{};
    const double nowMs = std::isfinite(in.timestampMs) ? in.timestampMs : 0.0;

    const bool chordDown[6] = {
        false,
        in.ctrlDown && in.f8Down,
        in.ctrlDown && in.f9Down,
        in.ctrlDown && in.f10Down,
        in.ctrlDown && in.f11Down,
        in.ctrlDown && in.f12Down,
    };

    // Foreground safety gate: never trigger while host window is unfocused; clear held-edge history
    // so alt-tabbing back does not synthesize a phantom rising edge.
    if (!in.foregroundOwned) {
        bool anyPressed = false;
        for (int i = 1; i < 6; ++i) {
            if (chordDown[i]) anyPressed = true;
            m_prevChordDown[i] = false;
        }
        if (anyPressed) {
            res.blockedByBackground = true;
            ++m_suppressedBackground;
        }
        return res;
    }

    // Priority order: Ctrl+F8 (Emergency SafeMode) -> Ctrl+F9 -> Ctrl+F10 -> Ctrl+F11 -> Ctrl+F12.
    for (int i = 1; i < 6; ++i) {
        const bool wasDown = m_prevChordDown[i];
        const bool isDown  = chordDown[i];
        m_prevChordDown[i] = isDown;

        if (!isDown) continue;
        if (wasDown) {
            res.blockedByHold = true;
            continue;
        }

        // Rising edge detected; enforce strict 250ms debounce window.
        if ((nowMs - m_lastFireTimeMs[i]) < kHotkeyDebounceMs) {
            res.blockedByDebounce = true;
            ++m_suppressedDebounce;
            continue;
        }

        if (res.triggeredAction == HotkeyAction::None) {
            m_lastFireTimeMs[i] = nowMs;
            res.triggeredAction = static_cast<HotkeyAction>(i);
        }
    }
    return res;
}

std::string HotkeyRouterInit() {
    {
        std::lock_guard<std::mutex> lk(g_hr.mu);
        g_hr.initialized        = true;
        g_hr.core.Reset();
        g_hr.state              = {};
        g_hr.state.routerActive = 1u;
        g_hr.state.hudVisible   = 1u;
        g_hr.state.debounceMs   = kHotkeyDebounceMs;
        CopyStrTrunc(g_hr.state.lastSummary, sizeof(g_hr.state.lastSummary), "ready");
    }
    return "[HotkeyRouter] initialized (Ctrl+F8 SafeMode | Ctrl+F9 HUD | Ctrl+F10 Preset | Ctrl+F11 Shaders | Ctrl+F12 Dashboard+SelfCheck | debounce=250ms)";
}

HotkeyStepResult HotkeyRouterInjectInput(const HotkeySampleInput& in, std::string* outActionLog) {
    HotkeyStepResult step{};
    uint8_t nextHud = 1u;
    {
        std::lock_guard<std::mutex> lk(g_hr.mu);
        if (!g_hr.initialized) {
            g_hr.initialized        = true;
            g_hr.state.routerActive = 1u;
            g_hr.state.hudVisible   = 1u;
            g_hr.state.debounceMs   = kHotkeyDebounceMs;
        }
        g_hr.state.foregroundOwned      = in.foregroundOwned ? 1u : 0u;
        step                            = g_hr.core.Evaluate(in);
        g_hr.state.suppressedBackground = g_hr.core.SuppressedBackground();
        g_hr.state.suppressedDebounce   = g_hr.core.SuppressedDebounce();

        if (step.triggeredAction != HotkeyAction::None) {
            const uint8_t idx = static_cast<uint8_t>(step.triggeredAction);
            g_hr.state.lastTriggeredAction = idx;
            ++g_hr.state.totalTriggers;
            if (idx < 6u) ++g_hr.state.actionCounts[idx];
            g_hr.state.lastTriggerTimeMs = std::isfinite(in.timestampMs) ? in.timestampMs : 0.0;
            if (step.triggeredAction == HotkeyAction::ToggleHud) {
                g_hr.state.hudVisible = g_hr.state.hudVisible ? 0u : 1u;
            }
            nextHud = g_hr.state.hudVisible;
        }
    }

    // Dispatch live subsystem action outside g_hr.mu to guarantee zero lock inversion.
    std::string actionSummary;
    switch (step.triggeredAction) {
        case HotkeyAction::EmergencySafeMode: {
            (void)ApplyActiveQualityPreset(QualityPreset::SafeMode, &actionSummary);
            break;
        }
        case HotkeyAction::ToggleHud: {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "[Hotkey] Ctrl+F9 HUD=%s", nextHud ? "ON" : "OFF");
            actionSummary = buf;
            break;
        }
        case HotkeyAction::CyclePreset: {
            RawProfilePresetState pp{};
            (void)RAW_GetProfilePresetState(&pp);
            const QualityPreset nextP = NextPresetInCycle(static_cast<QualityPreset>(pp.activePreset));
            (void)ApplyActiveQualityPreset(nextP, &actionSummary);
            break;
        }
        case HotkeyAction::ReloadShaders: {
            (void)ShaderHotReloadTriggerNow(&actionSummary);
            break;
        }
        case HotkeyAction::ExportDashboardSelfCheck: {
            std::string dashSum, selfSum;
            (void)LiveDashboardExportNow(nullptr, &dashSum);
            (void)BootSelfCheckRunAndWrite(nullptr, &selfSum);
            actionSummary = dashSum + " | " + selfSum;
            break;
        }
        case HotkeyAction::None:
        default:
            break;
    }

    if (!actionSummary.empty()) {
        std::lock_guard<std::mutex> lk(g_hr.mu);
        CopyStrTrunc(g_hr.state.lastSummary, sizeof(g_hr.state.lastSummary), actionSummary);
    }
    if (outActionLog) {
        *outActionLog = actionSummary;
    }
    return step;
}

void HotkeyRouterOnPresent() {
#ifndef RAW_SIM_PURE_TEST
    HotkeySampleInput sample{};
    sample.foregroundOwned = IsHostProcessForeground();
    sample.timestampMs     = static_cast<double>(GetTickCount64());
    if (sample.foregroundOwned) {
        sample.ctrlDown = IsVkDown(VK_CONTROL);
        if (sample.ctrlDown) {
            sample.f8Down  = IsVkDown(VK_F8);
            sample.f9Down  = IsVkDown(VK_F9);
            sample.f10Down = IsVkDown(VK_F10);
            sample.f11Down = IsVkDown(VK_F11);
            sample.f12Down = IsVkDown(VK_F12);
        }
    }
    (void)HotkeyRouterInjectInput(sample, nullptr);
#endif
}

std::string HotkeyRouterFormatBindings() {
    RawHotkeyRouterState st{};
    (void)RAW_GetHotkeyRouterState(&st);
    std::ostringstream os;
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "[Hotkeys] active=%u hud=%s fg=%u debounce=%.0fms triggers=%u (bgSup=%u debSup=%u) last=%s\n",
                  static_cast<unsigned>(st.routerActive),
                  st.hudVisible ? "ON" : "OFF",
                  static_cast<unsigned>(st.foregroundOwned),
                  st.debounceMs, st.totalTriggers,
                  st.suppressedBackground, st.suppressedDebounce,
                  HotkeyActionName(static_cast<HotkeyAction>(st.lastTriggeredAction)));
    os << buf;
    os << "  Ctrl+F8  - Emergency SafeMode fallback preset (count=" << st.actionCounts[1] << ")\n"
       << "  Ctrl+F9  - Toggle RAW HUD visibility (count=" << st.actionCounts[2] << ")\n"
       << "  Ctrl+F10 - Cycle Quality Preset Ultra/Balanced/Competitive/SafeMode (count=" << st.actionCounts[3] << ")\n"
       << "  Ctrl+F11 - Trigger Live Shader Hot-Reload generation (count=" << st.actionCounts[4] << ")\n"
       << "  Ctrl+F12 - Export Live Dashboard (JSON+HTML) + Boot Self-Check (count=" << st.actionCounts[5] << ")";
    return os.str();
}

void HotkeyRouterShutdown() {
    std::lock_guard<std::mutex> lk(g_hr.mu);
    g_hr.initialized        = false;
    g_hr.state.routerActive = 0u;
    g_hr.core.Reset();
}

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetHotkeyRouterState(raw::rt::RawHotkeyRouterState* out) {
    if (!out) return 0;
    std::lock_guard<std::mutex> lk(raw::rt::g_hr.mu);
    *out = raw::rt::g_hr.state;
    return raw::rt::g_hr.initialized ? 1 : 0;
}
