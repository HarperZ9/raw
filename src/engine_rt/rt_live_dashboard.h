#pragma once
// rt_live_dashboard.h: Wave 59 Non-Blocking Live JSON/HTML Telemetry Snapshot Dashboard (`raw::rt`).
// Zero-dependency atomic exporter writing `raw_engine_out\raw_live_dashboard.json` and self-contained
// `raw_engine_out\raw_live_dashboard.html` (zero external CDN links, zero em dashes) uniting live
// telemetry from rt_engine_health, rt_frame_governor, rt_profile_presets, rt_shader_hotreload,
// rt_telemetry_recorder, rt_adaptive_audio_director, and rt_sdk_bridge.
// Auto-exports every 300 frames or on-demand via `dashboard` in rt_console_diag.
// Original work, MIT.

#include <cstdint>
#include <string>

namespace raw::rt {

constexpr uint32_t kLiveDashboardCadenceFrames = 300u;

struct LiveDashboardSnapshot {
    uint32_t exportNumber         = 0u;
    uint32_t sdkVersion           = 0x00010000u;
    uint32_t activeSubsystemMask  = 0u;
    uint32_t verifiedGmstModules  = 0u;

    // rt_engine_health
    uint8_t  healthScore          = 100u;
    uint8_t  healthGrade          = 0u;
    uint8_t  pillarBinary         = 15u;
    uint8_t  pillarHandles        = 20u;
    uint8_t  pillarSaves          = 20u;
    uint8_t  pillarMods           = 20u;
    uint8_t  pillarPacing         = 17u;
    uint8_t  pillarDiag           = 8u;
    uint8_t  advisoryCount        = 0u;
    char     advisories[4][96]{};

    // rt_telemetry_recorder
    uint64_t totalRecordedFrames  = 0ull;
    uint32_t ringFillCount        = 0u;
    uint32_t hitchFramesOver50ms  = 0u;
    float    meanFrameMs          = 16.67f;
    float    p50FrameMs           = 16.67f;
    float    p95FrameMs           = 16.67f;
    float    p99FrameMs           = 16.67f;
    float    maxFrameMs           = 16.67f;

    // rt_frame_governor
    uint8_t  governorEnabled      = 1u;
    uint8_t  governorRegime       = 0u;
    float    targetFps            = 60.0f;
    float    emaFrameMs           = 16.67f;
    float    renderScale          = 1.0f;
    float    volumetricQuality01  = 1.0f;
    float    shadowDistanceScale  = 1.0f;
    float    floraLodScale        = 1.0f;
    float    giRayBudget01        = 1.0f;

    // rt_profile_presets
    uint8_t  activePreset         = 1u;
    uint8_t  recommendedPreset    = 1u;
    uint8_t  overallPressure      = 0u;
    uint8_t  fallbackRecommended  = 0u;
    float    ramUtilizationPct    = 0.0f;
    float    vramUtilizationPct   = 0.0f;
    float    vramUsedMB           = 0.0f;
    float    vramBudgetMB         = 6144.0f;

    // rt_shader_hotreload
    uint8_t  shaderWatcherActive  = 0u;
    uint32_t shaderReloadGen      = 0u;
    uint32_t shaderTrackedCount   = 0u;
    uint32_t shaderDirtyCount     = 0u;
    uint32_t shaderAcceptedCount  = 0u;
    uint32_t shaderRejectedCount  = 0u;
    char     lastDirtyShader[96]{};

    // rt_adaptive_audio_director
    uint8_t  audioMood            = 0u;
    float    audioLpfCutoffHz     = 18000.0f;
    float    audioReverbWetMult   = 0.85f;
    float    audioDuckGain        = 1.0f;
    uint32_t audioTransitions     = 0u;

    // rt_sdk_bridge world/saves/mods summary
    float    surfaceTempC         = 12.0f;
    float    snowpackM            = 0.0f;
    float    windBeaufort         = 2.0f;
    uint32_t activeHandles        = 0u;
    uint32_t saveMaxStrings       = 0u;
    uint32_t modPluginCount       = 0u;
    uint32_t modIssueCount        = 0u;
};

// Exported C ABI telemetry struct (RAW_GetLiveDashboardState).
struct RawLiveDashboardState {
    uint8_t  dashboardActive        = 0u;
    uint8_t  lastExportOk           = 0u;
    uint8_t  healthScore            = 100u;
    uint8_t  overallPressure        = 0u;
    uint32_t exportCount            = 0u;
    uint32_t autoCadenceFrames      = kLiveDashboardCadenceFrames;
    uint32_t lastJsonBytes          = 0u;
    uint32_t lastHtmlBytes          = 0u;
    uint32_t sdkVersion             = 0x00010000u;
    uint32_t activeSubsystemMask    = 0u;
    uint32_t shaderReloadGeneration = 0u;
    uint32_t reserved0              = 0u;
    char     lastJsonPath[160]{};
    char     lastHtmlPath[160]{};
};

// Pure snapshot capture & deterministic JSON/HTML formatters.
LiveDashboardSnapshot CaptureLiveDashboardSnapshot(uint32_t exportNumber = 0u) noexcept;
std::string           FormatLiveDashboardJson(const LiveDashboardSnapshot& snap);
std::string           FormatLiveDashboardHtml(const LiveDashboardSnapshot& snap);

// Runtime lifecycle & on-demand export (safe in BOTH raw_engine.dll and RAW_SIM_PURE_TEST=1).
std::string LiveDashboardInit(const std::string& outDir);
void        LiveDashboardOnPresent();
bool        LiveDashboardExportNow(const char* overrideOutDir = nullptr, std::string* outSummary = nullptr);
void        LiveDashboardShutdown();

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetLiveDashboardState(raw::rt::RawLiveDashboardState* out);
