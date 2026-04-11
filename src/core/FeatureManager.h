#pragma once
//=============================================================================
//  FeatureManager — Central feature registry with lifecycle management
//
//  Registers features with ID-based dependency tracking, resolves init order
//  via topological sort (Kahn's algorithm), and manages runtime enable/disable
//  with proper dependency-aware cascading.
//
//  Usage:
//    auto& fm = FeatureManager::Get();
//    uint32_t compute = fm.Register("ComputeManager", FeatureStage::Infrastructure,
//        []() { return ComputeManager::Get().Initialize(); },
//        []() { ComputeManager::Get().Shutdown(); });
//    uint32_t histo = fm.Register("LuminanceHistogram", FeatureStage::Compute,
//        []() { return LuminanceHistogram::Get().Initialize(); },
//        nullptr,
//        [](float dt) { LuminanceHistogram::Get().Update(dt); });
//    fm.AddDependency(histo, compute);
//    fm.InitializeAll();
//=============================================================================

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace SB
{

// ─── Feature classification ─────────────────────────────────────────────

enum class FeatureStage : uint8_t
{
    Infrastructure,  // Core systems (D3D hooks, compute manager)
    Compute,         // GPU compute passes (histogram, HiZ, TAA)
    Rendering,       // Full rendering systems (tone map, atmosphere, SSGI)
    PostProcess,     // Post-processing (frame gen, TSR)
    Debug            // Debug overlays (DebugRenderer, NavMesh, Skeleton)
};

enum class FeatureState : uint8_t
{
    Unregistered,
    Registered,
    Initializing,
    Ready,
    Failed,
    Disabled
};


// ─── Feature descriptor ─────────────────────────────────────────────────

struct FeatureInfo
{
    const char*   name  = nullptr;
    FeatureStage  stage = FeatureStage::Rendering;
    FeatureState  state = FeatureState::Unregistered;
    uint32_t      id    = 0;

    // Dependencies: list of feature IDs this feature requires
    std::vector<uint32_t> dependencies;

    // Lifecycle callbacks
    std::function<bool()>       initCallback;      // returns true on success
    std::function<void()>       shutdownCallback;
    std::function<void(float)>  updateCallback;    // dt in seconds

    // Runtime stats
    float    lastUpdateMs = 0.0f;
    uint32_t errorCount   = 0;
};


// ─── Manager ────────────────────────────────────────────────────────────

class FeatureManager
{
public:
    static FeatureManager& Get();

    // ── Registration ─────────────────────────────────────────────────
    // Register a feature, returns its unique ID (1-based)
    uint32_t Register(const char* name, FeatureStage stage,
                      std::function<bool()> init,
                      std::function<void()> shutdown = nullptr,
                      std::function<void(float)> update = nullptr);

    // Add dependency: featureId depends on dependsOnId
    void AddDependency(uint32_t featureId, uint32_t dependsOnId);

    // ── Lifecycle ────────────────────────────────────────────────────
    void InitializeAll();   // Topo-sort by deps, init in order (SEH-wrapped)
    void ShutdownAll();     // Reverse of init order
    void UpdateAll(float dt);  // Update all Ready features

    // ── Runtime control (cascading) ──────────────────────────────────
    void Enable(uint32_t id);    // Enables deps first, then inits
    void Disable(uint32_t id);   // Disables dependents first

    // ── Query ────────────────────────────────────────────────────────
    FeatureState                  GetState(uint32_t id) const;
    const char*                   GetStateName(FeatureState state) const;
    const std::vector<FeatureInfo>& GetFeatures() const;
    uint32_t                      GetReadyCount() const;
    uint32_t                      GetFailedCount() const;

    // ── INI persistence ──────────────────────────────────────────────
    void LoadConfig(const std::filesystem::path& iniPath);
    void SaveConfig(const std::filesystem::path& iniPath) const;

private:
    FeatureManager() = default;

    // Topological sort using Kahn's algorithm
    std::vector<uint32_t> TopologicalSort() const;

    std::vector<FeatureInfo> m_features;
    bool m_initialized = false;
};

} // namespace SB
