#pragma once
// rt_safe_recovery.h: Wave 62 Atomic Crash-Loop Safe-Rollover Recovery Guard (`raw::rt`).
// Writes atomic `raw_engine_out\boot_sentinel.state` on startup (recording boot timestamp, active
// preset, and consecutive unclean boot count) and clears it cleanly on SafeRecoveryShutdown().
// If uncleanBootCount >= 2 (two consecutive crashes without clean shutdown), automatically forces
// QualityPreset::SafeMode via rt_profile_presets, disables experimental overrides, and writes
// `raw_engine_out\recovery_report.txt` so players are never crash-loop locked out of Skyrim SE.
// Exports C ABI RAW_GetSafeRecoveryState and wires `recovery [status|reset]` in rt_console_diag.
// Original work, MIT.

#include <cstdint>
#include <string>

namespace raw::rt {

constexpr uint32_t kSafeRecoveryCrashThreshold = 2u;

// Exported C ABI telemetry struct (RAW_GetSafeRecoveryState).
struct RawSafeRecoveryState {
    uint8_t  guardActive          = 0u;
    uint8_t  sentinelPresent      = 0u;
    uint8_t  safeRolloverActive   = 0u;
    uint8_t  experimentalDisabled = 0u;
    uint32_t uncleanBootCount     = 0u;
    uint32_t crashLoopThreshold   = kSafeRecoveryCrashThreshold;
    uint32_t totalRollovers       = 0u;
    uint32_t manualResets         = 0u;
    uint64_t bootEpochSec         = 0ull;
    char     activePresetName[32]{};
    char     sentinelPath[160]{};
    char     recoveryReportPath[160]{};
};

// Runtime lifecycle & recovery control (safe in BOTH raw_engine.dll and RAW_SIM_PURE_TEST=1).
std::string SafeRecoveryInit(const std::string& outDir);
bool        SafeRecoveryResetCounter(std::string* outSummary = nullptr);
std::string SafeRecoveryFormatStatus();
void        SafeRecoveryShutdown();

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetSafeRecoveryState(raw::rt::RawSafeRecoveryState* out);
