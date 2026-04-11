#pragma once
//=============================================================================
//  SystemHealth — Per-system heartbeat monitoring
//
//  Tracks initialization state, runtime errors, stale data, and null-source
//  failures across ALL SkyrimBridge systems.  Provides a single red/yellow/green
//  status for each system, surfaced in a DebugGUI tab.
//
//  Three severity levels:
//    Green  — Initialized + running + no recent errors
//    Yellow — Initialized but degraded (stale data, occasional errors, disabled)
//    Red    — Failed to initialize, or persistent errors, or null critical source
//=============================================================================

#include <cstdint>
#include <array>
#include <atomic>

namespace SB
{

enum class HealthStatus : uint8_t
{
    Green,    // Healthy — running normally
    Yellow,   // Degraded — warnings, stale, or disabled
    Red,      // Failed — init failed, persistent errors, or null critical source
    Unknown   // Not yet evaluated
};

// System categories for grouping in the DebugGUI
enum class SystemCategory : uint8_t
{
    Tracker,        // 22 game-state trackers
    Backend,        // ShaderCache, FeedbackProcessor, WriteBack, etc.
    Compute,        // LuminanceHistogram, HiZPyramid, LUT, TAA, etc.
    Rendering,      // ToneMap, Atmosphere, GTAO, SSR, SSGI, Clouds, etc.
    Pipeline,       // ComputeManager, SRVInjector, RenderPipeline, etc.
    Proxy,          // d3d11 proxy subsystems (CB, state cache, phases, etc.)
    Debug,          // DebugRenderer, NavMesh, Skeleton, ShaderDebug
    Integration,    // ENBInterface, SharedMemory, PapyrusBridge, CompatDetect
    Count
};

struct SystemEntry
{
    const char*     name        = nullptr;
    SystemCategory  category    = SystemCategory::Backend;
    HealthStatus    status      = HealthStatus::Unknown;

    // State tracking
    bool            initialized = false;
    bool            enabled     = true;
    bool            hasError    = false;

    // Error counters
    uint32_t        errorCount      = 0;
    uint32_t        warningCount    = 0;
    uint32_t        framesSinceUpdate = 0;   // for stale detection

    // Diagnostic message (last error or status note)
    char            message[128]    = {};
};

class SystemHealth
{
public:
    static SystemHealth& Get()
    {
        static SystemHealth inst;
        return inst;
    }

    // Maximum number of tracked systems
    static constexpr uint32_t kMaxSystems = 80;

    // Register a system — returns its index (call once at init)
    uint32_t Register(const char* name, SystemCategory cat);

    // Update system state (call per-frame or on state change)
    void SetInitialized(uint32_t id, bool ok);
    void SetEnabled(uint32_t id, bool enabled);
    void ReportError(uint32_t id, const char* msg);
    void ReportWarning(uint32_t id, const char* msg);
    void Heartbeat(uint32_t id);  // call each frame to prove system is alive
    void ClearErrors(uint32_t id);

    // Query
    const SystemEntry& GetSystem(uint32_t id) const;
    uint32_t GetSystemCount() const { return m_count; }

    // Summary counts
    uint32_t GetGreenCount() const;
    uint32_t GetYellowCount() const;
    uint32_t GetRedCount() const;

    // Per-frame evaluation (recomputes status for all systems)
    void EvaluateAll();

    // Stale threshold: systems not heartbeating for this many frames → Yellow
    static constexpr uint32_t kStaleThreshold = 120;  // ~2 sec at 60fps

    // Error threshold: systems with this many errors → Red
    static constexpr uint32_t kErrorThreshold = 10;

private:
    SystemHealth() = default;

    void EvaluateSystem(SystemEntry& sys);

    std::array<SystemEntry, kMaxSystems> m_systems{};
    uint32_t m_count = 0;
};

// Helper: category name
inline const char* GetCategoryName(SystemCategory cat)
{
    switch (cat) {
    case SystemCategory::Tracker:     return "Tracker";
    case SystemCategory::Backend:     return "Backend";
    case SystemCategory::Compute:     return "Compute";
    case SystemCategory::Rendering:   return "Rendering";
    case SystemCategory::Pipeline:    return "Pipeline";
    case SystemCategory::Proxy:       return "Proxy";
    case SystemCategory::Debug:       return "Debug";
    case SystemCategory::Integration: return "Integration";
    default:                          return "Unknown";
    }
}

// Helper: status color for ImGui
inline void GetStatusColor(HealthStatus s, float& r, float& g, float& b)
{
    switch (s) {
    case HealthStatus::Green:   r = 0.3f; g = 0.85f; b = 0.4f; break;
    case HealthStatus::Yellow:  r = 0.95f; g = 0.8f; b = 0.2f; break;
    case HealthStatus::Red:     r = 0.95f; g = 0.25f; b = 0.2f; break;
    default:                    r = 0.5f; g = 0.5f; b = 0.5f; break;
    }
}

} // namespace SB
