#pragma once
// rt_console_diag.h: Wave 56-62 Live File-Triggered Engine Diagnostic Command Dispatcher (`raw::rt`).
// Clean-room, zero-dependency synchronous command router (13 live runtime commands: help, health,
// sdk, flight, mods, saves, status, preset <name>, reload_shaders, dashboard, selfcheck, hotkeys,
// recovery [status|reset]) + non-blocking 60-frame file-trigger poll (`raw_cmd.txt` -> atomic
// `raw_cmd_result.txt`, 1 KB cap).
// Original work, MIT.

#include <cstdint>
#include <string>

namespace raw::rt {

// Pure synchronous command dispatcher (case-insensitive, whitespace-trimmed).
// Supports: help, health, sdk, flight, mods, saves, status, preset <name>, reload_shaders, dashboard, selfcheck, hotkeys, recovery [status|reset].
// Unknown, empty, or malformed inputs fail closed returning false with a clean usage hint (never throws).
bool DispatchDiagnosticCommand(const char* cmdLine, std::string& outResponse) noexcept;

// Runtime lifecycle hooks (safe in BOTH raw_engine.dll and RAW_SIM_PURE_TEST=1).
std::string ConsoleDiagInit(const std::string& outDir);
void        ConsoleDiagOnPresent();
void        ConsoleDiagShutdown();

// Telemetry counters for diagnostics & tests.
uint32_t    ConsoleDiagCommandsExecuted() noexcept;
uint32_t    ConsoleDiagBatchesProcessed() noexcept;

}  // namespace raw::rt
