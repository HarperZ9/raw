#pragma once
// rt_hotkey_router.h: Wave 61 Foreground-Safe Debounced Live Hotkey Router (`raw::rt`).
// Edge-triggered, 250ms-debounced, foreground-window-gated chord hotkey router for live Skyrim SE:
//   Ctrl+F8  - Emergency SafeMode fallback preset
//   Ctrl+F9  - Toggle RAW HUD visibility
//   Ctrl+F10 - Cycle Quality Preset (Ultra -> Balanced -> Competitive -> SafeMode -> Ultra)
//   Ctrl+F11 - Trigger atomic Shader Hot-Reload generation
//   Ctrl+F12 - Export Live Telemetry Dashboard (JSON+HTML) & run 12-point Boot Self-Check
// Supports pure deterministic key-state injection for unit tests without polling desktop keys,
// exports C ABI RAW_GetHotkeyRouterState, and wires `hotkeys` (12th command) in rt_console_diag.
// Original work, MIT.

#include <cstdint>
#include <string>

namespace raw::rt {

constexpr double kHotkeyDebounceMs = 250.0;

enum class HotkeyAction : uint8_t {
    None                     = 0u,
    EmergencySafeMode        = 1u, // Ctrl+F8
    ToggleHud                = 2u, // Ctrl+F9
    CyclePreset              = 3u, // Ctrl+F10
    ReloadShaders            = 4u, // Ctrl+F11
    ExportDashboardSelfCheck = 5u, // Ctrl+F12
    Count                    = 6u,
};

const char* HotkeyActionName(HotkeyAction action) noexcept;
const char* HotkeyChordLabel(HotkeyAction action) noexcept;

struct HotkeySampleInput {
    bool   foregroundOwned = false;
    bool   ctrlDown        = false;
    bool   f8Down          = false;
    bool   f9Down          = false;
    bool   f10Down         = false;
    bool   f11Down         = false;
    bool   f12Down         = false;
    double timestampMs     = 0.0;
};

struct HotkeyStepResult {
    HotkeyAction triggeredAction     = HotkeyAction::None;
    bool         blockedByBackground = false;
    bool         blockedByDebounce   = false;
    bool         blockedByHold       = false;
};

// Exported C ABI telemetry struct (RAW_GetHotkeyRouterState).
struct RawHotkeyRouterState {
    uint8_t  routerActive         = 0u;
    uint8_t  hudVisible           = 1u;
    uint8_t  foregroundOwned      = 0u;
    uint8_t  lastTriggeredAction  = 0u; // HotkeyAction 0..5
    uint32_t totalTriggers        = 0u;
    uint32_t suppressedBackground = 0u;
    uint32_t suppressedDebounce   = 0u;
    uint32_t actionCounts[6]{};
    double   debounceMs           = kHotkeyDebounceMs;
    double   lastTriggerTimeMs    = 0.0;
    char     lastSummary[128]{};
};

// Pure deterministic edge-triggered + 250ms-debounced + foreground-safe chord evaluator.
class HotkeyRouterCore {
public:
    void             Reset() noexcept;
    HotkeyStepResult Evaluate(const HotkeySampleInput& in) noexcept;

    [[nodiscard]] uint32_t SuppressedBackground() const noexcept { return m_suppressedBackground; }
    [[nodiscard]] uint32_t SuppressedDebounce() const noexcept { return m_suppressedDebounce; }

private:
    bool     m_prevChordDown[6]{};
    double   m_lastFireTimeMs[6]{-1e9, -1e9, -1e9, -1e9, -1e9, -1e9};
    uint32_t m_suppressedBackground = 0u;
    uint32_t m_suppressedDebounce   = 0u;
};

// Runtime lifecycle & deterministic injection API (safe in BOTH raw_engine.dll and RAW_SIM_PURE_TEST=1).
std::string      HotkeyRouterInit();
HotkeyStepResult HotkeyRouterInjectInput(const HotkeySampleInput& in, std::string* outActionLog = nullptr);
void             HotkeyRouterOnPresent();
std::string      HotkeyRouterFormatBindings();
void             HotkeyRouterShutdown();

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetHotkeyRouterState(raw::rt::RawHotkeyRouterState* out);
