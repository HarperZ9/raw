// rt_profile_presets.cpp: Wave 57 Live Performance Budget / VRAM Watchdog & 4-Tier Quality Preset Profiles (`raw::rt`).
// Original work, MIT.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "rt_profile_presets.h"

#include "rt_frame_governor.h"
#include "rt_telemetry_recorder.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

namespace raw::rt {
namespace {

constexpr uint32_t kWatchdogCadenceFrames = 60u;

constexpr PresetSpec kPresetSpecs[4] = {
    // Ultra: full fidelity, 60 FPS target, high VRAM headroom
    {
        QualityPreset::Ultra, "Ultra",
        60.0f, 0.85f, 1.00f, 1.00f, 1.00f, 1.00f, 1.20f, 10240.0f,
        true, true, true, true, true, true, true
    },
    // Balanced: recommended 60 FPS default with smooth dynamic scaling
    {
        QualityPreset::Balanced, "Balanced",
        60.0f, 0.75f, 0.85f, 0.85f, 0.85f, 0.80f, 1.00f, 6144.0f,
        true, true, true, true, true, true, true
    },
    // Competitive: 144 FPS low-latency pacing, lean volumetrics/GI, zero camera FOV sway
    {
        QualityPreset::Competitive, "Competitive",
        144.0f, 0.75f, 0.60f, 0.70f, 0.70f, 0.55f, 0.80f, 4096.0f,
        true, true, false, true, true, true, true
    },
    // SafeMode: minimal-footprint diagnostic fallback with diagnostics & crash forensics kept active
    {
        QualityPreset::SafeMode, "SafeMode",
        60.0f, 0.70f, 0.50f, 0.65f, 0.60f, 0.50f, 0.00f, 2048.0f,
        false, false, false, false, true, true, true
    },
};

inline float ClampFiniteF(float v, float lo, float hi, float fallback) noexcept {
    if (!std::isfinite(v)) return fallback;
    return v < lo ? lo : (v > hi ? hi : v);
}

inline BudgetPressureLevel MaxPressure(BudgetPressureLevel a, BudgetPressureLevel b) noexcept {
    return static_cast<uint8_t>(a) >= static_cast<uint8_t>(b) ? a : b;
}

inline QualityPreset StepDownPreset(QualityPreset current) noexcept {
    switch (current) {
        case QualityPreset::Ultra:       return QualityPreset::Balanced;
        case QualityPreset::Balanced:    return QualityPreset::Competitive;
        case QualityPreset::Competitive: return QualityPreset::SafeMode;
        case QualityPreset::SafeMode:
        default:                         return QualityPreset::SafeMode;
    }
}

struct ProfilePresetRuntime {
    std::mutex            mu;
    bool                  initialized         = false;
    QualityPreset         activePreset        = QualityPreset::Balanced;
    float                 gpuVramUsedMB       = 0.0f;
    float                 gpuVramBudgetMB     = 0.0f;
    uint32_t              presentTick         = 0u;
    uint32_t              presetSwitchCount   = 0u;
    uint32_t              watchdogEvaluations = 0u;
    RawProfilePresetState lastState{};
} g_pp;

void SyncPresetFieldsToState(const PresetSpec& spec, RawProfilePresetState& st) noexcept {
    st.activePreset        = static_cast<uint8_t>(spec.preset);
    st.targetFps           = spec.targetFps;
    st.renderScaleFloor    = spec.renderScaleFloor;
    st.volumetricQuality   = spec.volumetricQuality;
    st.shadowDistanceScale = spec.shadowDistanceScale;
    st.floraLodScale       = spec.floraLodScale;
    st.giRayBudget         = spec.giRayBudget;
}

void RunWatchdogLocked() noexcept {
    const PresetSpec& spec = GetPresetSpec(g_pp.activePreset);
    const SystemMemorySnapshot mem = QueryHostSystemMemory();

    float frameP95Ms    = 1000.0f / spec.targetFps;
    uint32_t hitches    = 0u;
    RawFlightRecorderStats fr{};
    if (RAW_GetFlightRecorderStats(&fr) && fr.ringFillCount > 0u) {
        frameP95Ms = ClampFiniteF(fr.p95FrameMs, 0.5f, 500.0f, frameP95Ms);
        hitches    = fr.hitchFramesOver50ms;
    }

    float targetMs = 1000.0f / spec.targetFps;
    RawFrameGovernorState fg{};
    if (RAW_GetFrameGovernorState(&fg) && std::isfinite(fg.targetFrameMs) && fg.targetFrameMs > 1.0f) {
        targetMs = fg.targetFrameMs;
    }

    const float effVramBudget = g_pp.gpuVramBudgetMB > 128.0f ? g_pp.gpuVramBudgetMB : spec.vramBudgetMB;
    PerformanceBudgetInput in{};
    in.activePreset      = g_pp.activePreset;
    in.ramUtilizationPct = mem.querySucceeded ? mem.ramUtilizationPct : 0.0f;
    in.vramUsedMB        = g_pp.gpuVramUsedMB;
    in.vramBudgetMB      = effVramBudget;
    in.frameP95Ms        = frameP95Ms;
    in.targetFrameMs     = targetMs;
    in.hitchesOver50ms   = hitches;

    const PerformanceBudgetVerdict v = EvaluatePerformanceBudget(in);
    SyncPresetFieldsToState(spec, g_pp.lastState);
    g_pp.lastState.recommendedPreset   = static_cast<uint8_t>(v.recommendedPreset);
    g_pp.lastState.overallPressure     = static_cast<uint8_t>(v.overallPressure);
    g_pp.lastState.fallbackRecommended = v.fallbackRecommended ? 1u : 0u;
    g_pp.lastState.ramPressure         = static_cast<uint8_t>(v.ramPressure);
    g_pp.lastState.vramPressure        = static_cast<uint8_t>(v.vramPressure);
    g_pp.lastState.framePressure       = static_cast<uint8_t>(v.framePressure);
    g_pp.lastState.watchdogActive      = 1u;
    g_pp.lastState.ramUtilizationPct   = v.ramUtilizationPct;
    g_pp.lastState.vramUtilizationPct  = v.vramUtilizationPct;
    g_pp.lastState.frameP95Ms          = frameP95Ms;
    g_pp.lastState.vramUsedMB          = g_pp.gpuVramUsedMB;
    g_pp.lastState.vramBudgetMB        = effVramBudget;
    g_pp.lastState.presetSwitchCount   = g_pp.presetSwitchCount;
    g_pp.lastState.watchdogEvaluations = ++g_pp.watchdogEvaluations;
}

}  // namespace

const char* QualityPresetName(QualityPreset preset) noexcept {
    const uint8_t idx = static_cast<uint8_t>(preset);
    if (idx < static_cast<uint8_t>(QualityPreset::Count)) {
        return kPresetSpecs[idx].name;
    }
    return "Balanced";
}

const char* BudgetPressureName(BudgetPressureLevel level) noexcept {
    switch (level) {
        case BudgetPressureLevel::Nominal:  return "Nominal";
        case BudgetPressureLevel::Elevated: return "Elevated";
        case BudgetPressureLevel::High:     return "High";
        case BudgetPressureLevel::Critical: return "Critical";
        default:                            return "Unknown";
    }
}

bool ParseQualityPresetName(const char* name, QualityPreset& outPreset) noexcept {
    if (!name) return false;
    const char* p = name;
    while (*p != '\0' && std::isspace(static_cast<unsigned char>(*p)) != 0) ++p;
    size_t len = std::strlen(p);
    while (len > 0u && std::isspace(static_cast<unsigned char>(p[len - 1u])) != 0) --len;
    if (len == 0u || len > 31u) return false;

    char lower[32];
    for (size_t i = 0u; i < len; ++i) {
        lower[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(p[i])));
    }
    lower[len] = '\0';

    if (std::strcmp(lower, "ultra") == 0) {
        outPreset = QualityPreset::Ultra;
        return true;
    }
    if (std::strcmp(lower, "balanced") == 0) {
        outPreset = QualityPreset::Balanced;
        return true;
    }
    if (std::strcmp(lower, "competitive") == 0) {
        outPreset = QualityPreset::Competitive;
        return true;
    }
    if (std::strcmp(lower, "safemode") == 0 || std::strcmp(lower, "safe") == 0) {
        outPreset = QualityPreset::SafeMode;
        return true;
    }
    return false;
}

const PresetSpec& GetPresetSpec(QualityPreset preset) noexcept {
    const uint8_t idx = static_cast<uint8_t>(preset);
    if (idx < static_cast<uint8_t>(QualityPreset::Count)) {
        return kPresetSpecs[idx];
    }
    return kPresetSpecs[static_cast<uint8_t>(QualityPreset::Balanced)];
}

SystemMemorySnapshot QueryHostSystemMemory() noexcept {
    SystemMemorySnapshot snap{};
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms) != 0 && ms.ullTotalPhys > 0ull) {
        constexpr uint64_t kBytesPerMB = 1024ull * 1024ull;
        snap.totalPhysMB = ms.ullTotalPhys / kBytesPerMB;
        snap.availPhysMB = ms.ullAvailPhys / kBytesPerMB;
        snap.usedPhysMB  = (ms.ullTotalPhys >= ms.ullAvailPhys)
                               ? ((ms.ullTotalPhys - ms.ullAvailPhys) / kBytesPerMB)
                               : 0ull;
        const double pct = 100.0 * static_cast<double>(ms.ullTotalPhys - ms.ullAvailPhys) /
                           static_cast<double>(ms.ullTotalPhys);
        snap.ramUtilizationPct = ClampFiniteF(static_cast<float>(pct), 0.0f, 100.0f, 0.0f);
        snap.querySucceeded    = true;
    }
    return snap;
}

PerformanceBudgetVerdict EvaluatePerformanceBudget(const PerformanceBudgetInput& in) noexcept {
    PerformanceBudgetVerdict v{};
    const float ramPct     = ClampFiniteF(in.ramUtilizationPct, 0.0f, 100.0f, 0.0f);
    const float vramUsed   = ClampFiniteF(in.vramUsedMB, 0.0f, 1048576.0f, 0.0f);
    const float vramBudget = ClampFiniteF(in.vramBudgetMB, 128.0f, 1048576.0f, 8192.0f);
    const float vramPct    = ClampFiniteF((vramUsed / vramBudget) * 100.0f, 0.0f, 200.0f, 0.0f);
    const float targetMs   = ClampFiniteF(in.targetFrameMs, 4.0f, 100.0f, 16.6667f);
    const float p95Ms      = ClampFiniteF(in.frameP95Ms, 0.5f, 500.0f, targetMs);
    const float ratio      = ClampFiniteF(p95Ms / targetMs, 0.05f, 30.0f, 1.0f);

    v.ramUtilizationPct  = ramPct;
    v.vramUtilizationPct = vramPct;
    v.frameOverrunRatio  = ratio;

    // 1. Host RAM pressure thresholds: <75% Nominal, 75-84% Elevated, 85-92% High, >=93% Critical
    if (ramPct >= 93.0f)      v.ramPressure = BudgetPressureLevel::Critical;
    else if (ramPct >= 85.0f) v.ramPressure = BudgetPressureLevel::High;
    else if (ramPct >= 75.0f) v.ramPressure = BudgetPressureLevel::Elevated;
    else                      v.ramPressure = BudgetPressureLevel::Nominal;

    // 2. GPU VRAM pressure thresholds: <75% Nominal, 75-87% Elevated, 88-95% High, >=96% Critical
    if (vramPct >= 96.0f)      v.vramPressure = BudgetPressureLevel::Critical;
    else if (vramPct >= 88.0f) v.vramPressure = BudgetPressureLevel::High;
    else if (vramPct >= 75.0f) v.vramPressure = BudgetPressureLevel::Elevated;
    else                       v.vramPressure = BudgetPressureLevel::Nominal;

    // 3. Frame-time budget pressure thresholds: <=1.10x Nominal, 1.10-1.35x Elevated, 1.35-1.75x High, >1.75x or >=6 hitches Critical
    if (ratio >= 1.75f || in.hitchesOver50ms >= 6u)      v.framePressure = BudgetPressureLevel::Critical;
    else if (ratio >= 1.35f || in.hitchesOver50ms >= 3u) v.framePressure = BudgetPressureLevel::High;
    else if (ratio >= 1.10f || in.hitchesOver50ms >= 1u) v.framePressure = BudgetPressureLevel::Elevated;
    else                                                 v.framePressure = BudgetPressureLevel::Nominal;

    v.overallPressure = MaxPressure(v.ramPressure, MaxPressure(v.vramPressure, v.framePressure));

    // Safe fallback recommendation policy:
    //   Critical -> immediate SafeMode recommendation
    //   High     -> step down one quality tier (Ultra -> Balanced -> Competitive -> SafeMode)
    //   Nominal/Elevated -> retain active preset (zero thrashing)
    if (v.overallPressure == BudgetPressureLevel::Critical) {
        v.recommendedPreset = QualityPreset::SafeMode;
    } else if (v.overallPressure == BudgetPressureLevel::High) {
        v.recommendedPreset = StepDownPreset(in.activePreset);
    } else {
        v.recommendedPreset = in.activePreset;
    }
    v.fallbackRecommended = (v.recommendedPreset != in.activePreset);

    if (v.overallPressure == BudgetPressureLevel::Nominal) {
        std::snprintf(v.reason, sizeof(v.reason),
                      "Within budget (RAM=%.0f%% VRAM=%.0f%% p95=%.1fms/%.1fms)",
                      double(ramPct), double(vramPct), double(p95Ms), double(targetMs));
    } else {
        std::snprintf(v.reason, sizeof(v.reason),
                      "%s pressure (RAM=%s %.0f%%, VRAM=%s %.0f%%, Frame=%s %.2fx) -> %s",
                      BudgetPressureName(v.overallPressure),
                      BudgetPressureName(v.ramPressure), double(ramPct),
                      BudgetPressureName(v.vramPressure), double(vramPct),
                      BudgetPressureName(v.framePressure), double(ratio),
                      QualityPresetName(v.recommendedPreset));
    }
    return v;
}

LiveEngineConfig ApplyPresetToLiveConfig(QualityPreset preset, const LiveEngineConfig& baseCfg) noexcept {
    const PresetSpec& spec = GetPresetSpec(preset);
    LiveEngineConfig out   = baseCfg;
    out.dynamicGovernorEnabled = spec.dynamicGovernorEnabled;
    out.targetFps              = spec.targetFps;
    out.skeletonXpbdEnabled    = spec.skeletonXpbdEnabled;
    out.xpbdWindScale          = spec.xpbdWindScale;
    out.fovBreathEnabled       = spec.fovBreathEnabled;
    out.adaptiveAudioDirector  = spec.adaptiveAudioDirector;
    out.crashForensicsEnabled  = spec.crashForensicsEnabled;
    out.flightRecorderEnabled  = spec.flightRecorderEnabled;
    out.engineHealthReport     = spec.engineHealthReport;
    ClampLiveEngineConfig(out);
    return out;
}

std::string FormatPresetIni(QualityPreset preset, const LiveEngineConfig& baseCfg) {
    return FormatDefaultLiveConfigIni(ApplyPresetToLiveConfig(preset, baseCfg));
}

bool ApplyActiveQualityPreset(QualityPreset preset, std::string* outSummary) {
    if (static_cast<uint8_t>(preset) >= static_cast<uint8_t>(QualityPreset::Count)) {
        return false;
    }
    const LiveEngineConfig updated = ApplyPresetToLiveConfig(preset, GetLiveConfig());
    SetLiveConfigSnapshot(updated);

    const PresetSpec& spec = GetPresetSpec(preset);
    RawProfilePresetState snap{};
    {
        std::lock_guard<std::mutex> lk(g_pp.mu);
        g_pp.initialized  = true;
        g_pp.activePreset = preset;
        ++g_pp.presetSwitchCount;
        RunWatchdogLocked();
        snap = g_pp.lastState;
    }

    if (outSummary) {
        char buf[288];
        std::snprintf(buf, sizeof(buf),
                      "[Preset] applied=%s (targetFps=%.0f gov=%u renderFloor=%.2f vol=%.2f shadow=%.2f flora=%.2f gi=%.2f xpbd=%u) | watchdog=%s (RAM=%.0f%% VRAM=%.0f%% rec=%s)",
                      spec.name, double(spec.targetFps), spec.dynamicGovernorEnabled ? 1u : 0u,
                      double(spec.renderScaleFloor), double(spec.volumetricQuality),
                      double(spec.shadowDistanceScale), double(spec.floraLodScale),
                      double(spec.giRayBudget), spec.skeletonXpbdEnabled ? 1u : 0u,
                      BudgetPressureName(static_cast<BudgetPressureLevel>(snap.overallPressure)),
                      double(snap.ramUtilizationPct), double(snap.vramUtilizationPct),
                      QualityPresetName(static_cast<QualityPreset>(snap.recommendedPreset)));
        *outSummary = buf;
    }
    return true;
}

std::string ProfilePresetsInit(QualityPreset initialPreset) {
    RawProfilePresetState snap{};
    {
        std::lock_guard<std::mutex> lk(g_pp.mu);
        g_pp.initialized         = true;
        g_pp.activePreset        = initialPreset;
        g_pp.gpuVramUsedMB       = 0.0f;
        g_pp.gpuVramBudgetMB     = 0.0f;
        g_pp.presentTick         = 0u;
        g_pp.presetSwitchCount   = 0u;
        g_pp.watchdogEvaluations = 0u;
        RunWatchdogLocked();
        snap = g_pp.lastState;
    }
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "[ProfilePresets] initialized (active=%s targetFps=%.0f | RAM=%.0f%% pressure=%s recommended=%s)",
                  QualityPresetName(static_cast<QualityPreset>(snap.activePreset)),
                  double(snap.targetFps),
                  double(snap.ramUtilizationPct),
                  BudgetPressureName(static_cast<BudgetPressureLevel>(snap.overallPressure)),
                  QualityPresetName(static_cast<QualityPreset>(snap.recommendedPreset)));
    return buf;
}

void ProfilePresetsUpdateGpuVram(float usedMB, float budgetMB) noexcept {
    std::lock_guard<std::mutex> lk(g_pp.mu);
    g_pp.gpuVramUsedMB   = ClampFiniteF(usedMB, 0.0f, 1048576.0f, 0.0f);
    g_pp.gpuVramBudgetMB = ClampFiniteF(budgetMB, 0.0f, 1048576.0f, 0.0f);
    if (g_pp.initialized) {
        RunWatchdogLocked();
    }
}

void ProfilePresetsOnPresent() {
    std::lock_guard<std::mutex> lk(g_pp.mu);
    if (!g_pp.initialized) return;
    ++g_pp.presentTick;
    if ((g_pp.presentTick % kWatchdogCadenceFrames) == 0u) {
        RunWatchdogLocked();
    }
}

void ProfilePresetsShutdown() {
    std::lock_guard<std::mutex> lk(g_pp.mu);
    g_pp.initialized         = false;
    g_pp.presentTick         = 0u;
    g_pp.gpuVramUsedMB       = 0.0f;
    g_pp.gpuVramBudgetMB     = 0.0f;
    g_pp.lastState.watchdogActive = 0u;
}

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetProfilePresetState(raw::rt::RawProfilePresetState* out) {
    if (!out) return 0;
    std::lock_guard<std::mutex> lk(raw::rt::g_pp.mu);
    *out = raw::rt::g_pp.lastState;
    return raw::rt::g_pp.initialized ? 1 : 0;
}
