#pragma once
// rt_profile_presets.h: Wave 57 Live Performance Budget / VRAM Watchdog & 4-Tier Quality Preset Profiles (`raw::rt`).
// Provides 4 validated engine presets (Ultra, Balanced, Competitive, SafeMode), Win32 physical RAM
// pressure monitoring (GlobalMemoryStatusEx) + caller GPU VRAM telemetry classification, deterministic
// fallback recommendation on frame-time/VRAM budget overrun, raw_engine.ini preset formatting/application,
// and exported C ABI state (RAW_GetProfilePresetState).
// Zero per-frame heap allocations; zero external dependencies.
// Original work, MIT.

#include "rt_live_config.h"

#include <cstdint>
#include <string>

namespace raw::rt {

enum class QualityPreset : uint8_t {
    Ultra       = 0u,
    Balanced    = 1u,
    Competitive = 2u,
    SafeMode    = 3u,
    Count       = 4u,
};

enum class BudgetPressureLevel : uint8_t {
    Nominal  = 0u,
    Elevated = 1u,
    High     = 2u,
    Critical = 3u,
};

const char* QualityPresetName(QualityPreset preset) noexcept;
const char* BudgetPressureName(BudgetPressureLevel level) noexcept;
bool        ParseQualityPresetName(const char* name, QualityPreset& outPreset) noexcept;

struct PresetSpec {
    QualityPreset preset                 = QualityPreset::Balanced;
    const char*   name                   = "Balanced";
    float         targetFps              = 60.0f;
    float         renderScaleFloor       = 0.75f;
    float         volumetricQuality      = 0.85f;
    float         shadowDistanceScale    = 0.85f;
    float         floraLodScale          = 0.85f;
    float         giRayBudget            = 0.80f;
    float         xpbdWindScale          = 1.00f;
    float         vramBudgetMB           = 6144.0f;
    bool          dynamicGovernorEnabled = true;
    bool          skeletonXpbdEnabled    = true;
    bool          fovBreathEnabled       = true;
    bool          adaptiveAudioDirector  = true;
    bool          crashForensicsEnabled  = true;
    bool          flightRecorderEnabled  = true;
    bool          engineHealthReport     = true;
};

const PresetSpec& GetPresetSpec(QualityPreset preset) noexcept;

struct SystemMemorySnapshot {
    uint64_t totalPhysMB       = 0ull;
    uint64_t availPhysMB       = 0ull;
    uint64_t usedPhysMB        = 0ull;
    float    ramUtilizationPct = 0.0f;
    bool     querySucceeded    = false;
};

// Queries live host RAM utilization via Win32 GlobalMemoryStatusEx (fail-safe, noexcept).
SystemMemorySnapshot QueryHostSystemMemory() noexcept;

struct PerformanceBudgetInput {
    QualityPreset activePreset      = QualityPreset::Balanced;
    float         ramUtilizationPct = 0.0f;    // [0 .. 100]
    float         vramUsedMB        = 0.0f;    // Caller-supplied GPU VRAM usage in MB
    float         vramBudgetMB      = 8192.0f; // Caller-supplied GPU VRAM budget in MB
    float         frameP95Ms        = 16.67f;  // Rolling p95 frame time in ms
    float         targetFrameMs     = 16.67f;  // 1000 / targetFps
    uint32_t      hitchesOver50ms   = 0u;
};

struct PerformanceBudgetVerdict {
    BudgetPressureLevel ramPressure         = BudgetPressureLevel::Nominal;
    BudgetPressureLevel vramPressure        = BudgetPressureLevel::Nominal;
    BudgetPressureLevel framePressure       = BudgetPressureLevel::Nominal;
    BudgetPressureLevel overallPressure     = BudgetPressureLevel::Nominal;
    float               ramUtilizationPct   = 0.0f;
    float               vramUtilizationPct  = 0.0f;
    float               frameOverrunRatio   = 1.0f;
    QualityPreset       recommendedPreset   = QualityPreset::Balanced;
    bool                fallbackRecommended = false;
    char                reason[128]{};
};

// Pure deterministic RAM/VRAM/frame-time pressure classifier & safe fallback recommender.
PerformanceBudgetVerdict EvaluatePerformanceBudget(const PerformanceBudgetInput& in) noexcept;

// Deterministic raw_engine.ini preset application & serialization.
LiveEngineConfig ApplyPresetToLiveConfig(QualityPreset preset, const LiveEngineConfig& baseCfg) noexcept;
std::string      FormatPresetIni(QualityPreset preset, const LiveEngineConfig& baseCfg = DefaultLiveEngineConfig());

// Applies a preset live to active runtime state + LiveEngineConfig snapshot.
bool ApplyActiveQualityPreset(QualityPreset preset, std::string* outSummary = nullptr);

// Exported C ABI telemetry struct (RAW_GetProfilePresetState).
struct RawProfilePresetState {
    uint8_t  activePreset        = 1u; // QualityPreset 0..3
    uint8_t  recommendedPreset   = 1u; // QualityPreset 0..3
    uint8_t  overallPressure     = 0u; // BudgetPressureLevel 0..3
    uint8_t  fallbackRecommended = 0u; // 0/1
    uint8_t  ramPressure         = 0u; // BudgetPressureLevel 0..3
    uint8_t  vramPressure        = 0u; // BudgetPressureLevel 0..3
    uint8_t  framePressure       = 0u; // BudgetPressureLevel 0..3
    uint8_t  watchdogActive      = 1u; // 0/1
    float    targetFps           = 60.0f;
    float    renderScaleFloor    = 0.75f;
    float    volumetricQuality   = 0.85f;
    float    shadowDistanceScale = 0.85f;
    float    floraLodScale       = 0.85f;
    float    giRayBudget         = 0.80f;
    float    ramUtilizationPct   = 0.0f;
    float    vramUtilizationPct  = 0.0f;
    float    frameP95Ms          = 16.67f;
    float    vramUsedMB          = 0.0f;
    float    vramBudgetMB        = 6144.0f;
    uint32_t presetSwitchCount   = 0u;
    uint32_t watchdogEvaluations = 0u;
};

// Runtime lifecycle hooks (safe in BOTH raw_engine.dll and RAW_SIM_PURE_TEST=1).
std::string ProfilePresetsInit(QualityPreset initialPreset = QualityPreset::Balanced);
void        ProfilePresetsUpdateGpuVram(float usedMB, float budgetMB) noexcept;
void        ProfilePresetsOnPresent();
void        ProfilePresetsShutdown();

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetProfilePresetState(raw::rt::RawProfilePresetState* out);
