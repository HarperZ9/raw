#pragma once
// rt_boot_selfcheck.h: Wave 60 12-Point Live Boot & Runtime Self-Diagnostic Verifier (`raw::rt`).
// Clean-room atomic integrity auditor for raw_engine.dll verifying 12 critical runtime invariants:
//   0. PeBinaryChecksum       (PE64 header, TimeDateStamp, SizeOfImage, CheckSum)
//   1. TsvOffsetTable         (skyrimse-1.7.104.tsv presence & hex RVA row count)
//   2. RttiCatalogIntegrity   (skyrimse-1.7.104-rtti.tsv presence & vtable row count)
//   3. ShaderHotReloadHealth  (rt_shader_hotreload watcher & 0 rejected syntax edits)
//   4. LiveConfigBounds       (rt_live_config finite clamped parameter invariants)
//   5. PresetVramHeadroom     (rt_profile_presets RAM/VRAM watchdog pressure tier)
//   6. SaveGameHealth         (rt_save_papyrus_guard .ESS health & string-table headroom)
//   7. ModLoadOrderHygiene    (rt_mod_load_audit TES4 master/ESL integrity)
//   8. SdkAbiReadiness        (rt_sdk_bridge version 0x00010000 & unified snapshot ABI)
//   9. DashboardReadiness     (rt_live_dashboard exporter state & cadence)
//  10. EngineHealthScore      (rt_engine_health 0-100 composite score & grade)
//  11. FrameGovernorStability (rt_frame_governor finite bounds across 5 quality scales)
// Exports C ABI RAW_GetBootSelfCheckState, writes `raw_engine_out\boot_selfcheck.txt`, and wires
// `selfcheck` (11th command) into rt_console_diag.
// Original work, MIT.

#include <cstdint>
#include <string>

namespace raw::rt {

constexpr uint32_t kBootSelfCheckCount = 12u;

enum class BootCheckStatus : uint8_t {
    Pass = 0u,
    Warn = 1u,
    Fail = 2u,
};

const char* BootCheckStatusName(BootCheckStatus st) noexcept;

struct BootCheckItem {
    uint8_t  id        = 0u; // 0..11
    uint8_t  status    = 0u; // BootCheckStatus
    uint16_t reserved  = 0u;
    char     name[32]{};
    char     detail[128]{};
};

struct BootSelfCheckOptions {
    std::string gameDir;
    std::string outDir;
    std::string hostExePath;      // Optional override; defaults to current process exe
    std::string tsvPathOverride;  // Optional explicit skyrimse-1.7.104.tsv path
    std::string rttiPathOverride; // Optional explicit skyrimse-1.7.104-rtti.tsv path
};

struct BootSelfCheckReport {
    uint32_t      runNumber       = 0u;
    uint32_t      passCount       = 0u;
    uint32_t      warnCount       = 0u;
    uint32_t      failCount       = 0u;
    uint32_t      passBitmask     = 0u;
    uint32_t      failBitmask     = 0u;
    uint32_t      tsvRowCount     = 0u;
    uint32_t      rttiRowCount    = 0u;
    uint32_t      peTimeDateStamp = 0u;
    uint32_t      peSizeOfImage   = 0u;
    uint32_t      peCheckSum      = 0u;
    BootCheckItem items[kBootSelfCheckCount]{};
};

// Exported C ABI telemetry struct (RAW_GetBootSelfCheckState).
struct RawBootSelfCheckState {
    uint8_t  initialized       = 0u;
    uint8_t  overallStatus     = 0u; // BootCheckStatus (0=Pass, 1=Warn, 2=Fail)
    uint8_t  lastReportWritten = 0u; // 0/1
    uint8_t  reserved0         = 0u;
    uint32_t runCount          = 0u;
    uint32_t totalChecks       = kBootSelfCheckCount;
    uint32_t passCount         = 0u;
    uint32_t warnCount         = 0u;
    uint32_t failCount         = 0u;
    uint32_t passBitmask       = 0u;
    uint32_t failBitmask       = 0u;
    uint32_t tsvRowCount       = 0u;
    uint32_t rttiRowCount      = 0u;
    char     lastReportPath[160]{};
};

// Pure deterministic 12-point evaluator & report formatter.
BootSelfCheckReport RunBootSelfCheck(const BootSelfCheckOptions& opts);
std::string         FormatBootSelfCheckReport(const BootSelfCheckReport& rep);

// Runtime lifecycle hooks (safe in BOTH raw_engine.dll and RAW_SIM_PURE_TEST=1).
std::string BootSelfCheckInit(const std::string& gameDir, const std::string& outDir);
bool        BootSelfCheckRunAndWrite(const char* overrideOutDir = nullptr, std::string* outSummary = nullptr);
void        BootSelfCheckShutdown();

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetBootSelfCheckState(raw::rt::RawBootSelfCheckState* out);
