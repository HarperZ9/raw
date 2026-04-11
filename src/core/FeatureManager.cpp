//=============================================================================
//  FeatureManager — Central feature registry with lifecycle management
//=============================================================================

#include "FeatureManager.h"

#include <SKSE/SKSE.h>

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <queue>
#include <unordered_set>

namespace SB
{

// ─── SEH wrappers (must be in functions with no C++ destructors) ─────────

namespace
{
    // Returns true if the callback succeeded, false on SEH exception or failure
    bool SEH_CallInit(const std::function<bool()>& fn) noexcept
    {
        __try
        {
            return fn();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void SEH_CallVoid(const std::function<void()>& fn) noexcept
    {
        __try
        {
            fn();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }
} // anonymous namespace


// ─── Singleton ──────────────────────────────────────────────────────────

FeatureManager& FeatureManager::Get()
{
    static FeatureManager instance;
    return instance;
}


// ─── Registration ───────────────────────────────────────────────────────

uint32_t FeatureManager::Register(const char* name, FeatureStage stage,
                                   std::function<bool()> init,
                                   std::function<void()> shutdown,
                                   std::function<void(float)> update)
{
    if (!name)
    {
        SKSE::log::error("FeatureManager: attempted to register feature with null name");
        return 0;
    }

    // Check for duplicate name
    for (const auto& f : m_features)
    {
        if (f.name && std::strcmp(f.name, name) == 0)
        {
            SKSE::log::warn("FeatureManager: feature '{}' already registered as id={}", name, f.id);
            return f.id;
        }
    }

    FeatureInfo info;
    info.name             = name;
    info.stage            = stage;
    info.state            = FeatureState::Registered;
    info.id               = static_cast<uint32_t>(m_features.size()) + 1;  // 1-based IDs
    info.initCallback     = std::move(init);
    info.shutdownCallback = std::move(shutdown);
    info.updateCallback   = std::move(update);

    m_features.push_back(std::move(info));

    SKSE::log::info("FeatureManager: registered '{}' id={} stage={}", name, info.id,
                    static_cast<int>(stage));
    return m_features.back().id;
}

void FeatureManager::AddDependency(uint32_t featureId, uint32_t dependsOnId)
{
    if (featureId == 0 || dependsOnId == 0)
    {
        SKSE::log::error("FeatureManager::AddDependency: invalid id (feature={}, dep={})",
                         featureId, dependsOnId);
        return;
    }

    if (featureId > m_features.size() || dependsOnId > m_features.size())
    {
        SKSE::log::error("FeatureManager::AddDependency: id out of range (feature={}, dep={}, count={})",
                         featureId, dependsOnId, m_features.size());
        return;
    }

    if (featureId == dependsOnId)
    {
        SKSE::log::warn("FeatureManager::AddDependency: feature {} cannot depend on itself",
                        featureId);
        return;
    }

    auto& deps = m_features[featureId - 1].dependencies;

    // Avoid duplicate dependency
    if (std::find(deps.begin(), deps.end(), dependsOnId) != deps.end())
        return;

    deps.push_back(dependsOnId);

    SKSE::log::info("FeatureManager: '{}' (id={}) now depends on '{}' (id={})",
                    m_features[featureId - 1].name ? m_features[featureId - 1].name : "?",
                    featureId,
                    m_features[dependsOnId - 1].name ? m_features[dependsOnId - 1].name : "?",
                    dependsOnId);
}


// ─── Topological sort (Kahn's algorithm) ────────────────────────────────

std::vector<uint32_t> FeatureManager::TopologicalSort() const
{
    const size_t n = m_features.size();

    // Build in-degree counts and adjacency (dependents) list
    // Index by (id - 1) since IDs are 1-based
    std::vector<int> inDegree(n, 0);
    std::vector<std::vector<uint32_t>> dependents(n);

    for (size_t i = 0; i < n; ++i)
    {
        for (uint32_t depId : m_features[i].dependencies)
        {
            if (depId == 0 || depId > n)
            {
                SKSE::log::warn("FeatureManager: '{}' has invalid dependency id={}",
                                m_features[i].name ? m_features[i].name : "?", depId);
                continue;
            }
            dependents[depId - 1].push_back(m_features[i].id);
            inDegree[i]++;
        }
    }

    // Collect zero-indegree seeds, sorted by (stage, name) for determinism
    std::vector<uint32_t> seeds;
    for (size_t i = 0; i < n; ++i)
    {
        if (inDegree[i] == 0)
            seeds.push_back(static_cast<uint32_t>(i + 1));
    }

    auto cmpByStageAndName = [this](uint32_t a, uint32_t b)
    {
        const auto& fa = m_features[a - 1];
        const auto& fb = m_features[b - 1];
        if (fa.stage != fb.stage)
            return fa.stage < fb.stage;
        // Compare names for deterministic ordering
        const char* na = fa.name ? fa.name : "";
        const char* nb = fb.name ? fb.name : "";
        return std::strcmp(na, nb) < 0;
    };

    std::sort(seeds.begin(), seeds.end(), cmpByStageAndName);

    std::queue<uint32_t> q;
    for (uint32_t s : seeds)
        q.push(s);

    std::vector<uint32_t> sorted;
    sorted.reserve(n);

    while (!q.empty())
    {
        uint32_t current = q.front();
        q.pop();
        sorted.push_back(current);

        // Collect and sort neighbors for deterministic order
        auto& neighbors = dependents[current - 1];
        std::sort(neighbors.begin(), neighbors.end(), cmpByStageAndName);

        for (uint32_t neighbor : neighbors)
        {
            inDegree[neighbor - 1]--;
            if (inDegree[neighbor - 1] == 0)
                q.push(neighbor);
        }
    }

    // Detect cycles
    if (sorted.size() != n)
    {
        SKSE::log::error("FeatureManager: dependency cycle detected! Sorted {}/{} features",
                         sorted.size(), n);

        std::unordered_set<uint32_t> inSorted(sorted.begin(), sorted.end());
        for (size_t i = 0; i < n; ++i)
        {
            uint32_t id = static_cast<uint32_t>(i + 1);
            if (!inSorted.contains(id))
            {
                SKSE::log::error("FeatureManager: '{}' (id={}) is part of a dependency cycle",
                                 m_features[i].name ? m_features[i].name : "?", id);
                sorted.push_back(id);
            }
        }
    }

    return sorted;
}


// ─── Lifecycle ──────────────────────────────────────────────────────────

void FeatureManager::InitializeAll()
{
    SKSE::log::info("FeatureManager: InitializeAll — {} features registered", m_features.size());

    auto order = TopologicalSort();

    SKSE::log::info("FeatureManager: initialization order ({} features):", order.size());
    for (size_t i = 0; i < order.size(); ++i)
    {
        const auto& f = m_features[order[i] - 1];
        SKSE::log::info("  [{}] {} (id={}, stage={})", i,
                        f.name ? f.name : "?", f.id, static_cast<int>(f.stage));
    }

    uint32_t readyCount = 0;
    uint32_t failCount  = 0;
    uint32_t skipCount  = 0;

    for (uint32_t id : order)
    {
        auto& info = m_features[id - 1];

        if (info.state == FeatureState::Disabled)
        {
            skipCount++;
            SKSE::log::info("FeatureManager: '{}' (id={}) disabled, skipping",
                            info.name ? info.name : "?", id);
            continue;
        }

        // Check that all dependencies are Ready
        bool depsOk = true;
        for (uint32_t depId : info.dependencies)
        {
            if (depId == 0 || depId > m_features.size())
            {
                depsOk = false;
                break;
            }
            if (m_features[depId - 1].state != FeatureState::Ready)
            {
                SKSE::log::warn("FeatureManager: '{}' (id={}) skipped — dep '{}' (id={}) is {}",
                                info.name ? info.name : "?", id,
                                m_features[depId - 1].name ? m_features[depId - 1].name : "?",
                                depId, GetStateName(m_features[depId - 1].state));
                depsOk = false;
                break;
            }
        }

        if (!depsOk)
        {
            info.state = FeatureState::Failed;
            info.errorCount++;
            failCount++;
            continue;
        }

        // No init callback — trivially ready
        if (!info.initCallback)
        {
            info.state = FeatureState::Ready;
            readyCount++;
            SKSE::log::info("FeatureManager: '{}' (id={}) ready (no init callback)",
                            info.name ? info.name : "?", id);
            continue;
        }

        // Run the initializer (SEH-wrapped)
        info.state = FeatureState::Initializing;
        SKSE::log::info("FeatureManager: initializing '{}'...", info.name ? info.name : "?");

        auto t0 = std::chrono::high_resolution_clock::now();
        bool ok = SEH_CallInit(info.initCallback);
        auto t1 = std::chrono::high_resolution_clock::now();
        float ms = std::chrono::duration<float, std::milli>(t1 - t0).count();

        if (ok)
        {
            info.state = FeatureState::Ready;
            info.lastUpdateMs = ms;
            readyCount++;
            SKSE::log::info("FeatureManager: '{}' (id={}) ready ({:.2f} ms)",
                            info.name ? info.name : "?", id, ms);
        }
        else
        {
            info.state = FeatureState::Failed;
            info.errorCount++;
            failCount++;
            SKSE::log::error("FeatureManager: '{}' (id={}) FAILED ({:.2f} ms)",
                             info.name ? info.name : "?", id, ms);
        }
    }

    m_initialized = true;

    SKSE::log::info("FeatureManager: InitializeAll complete — {} ready, {} failed, {} disabled",
                    readyCount, failCount, skipCount);
}

void FeatureManager::ShutdownAll()
{
    SKSE::log::info("FeatureManager: ShutdownAll");

    // Shutdown in reverse topological order
    auto order = TopologicalSort();

    for (auto it = order.rbegin(); it != order.rend(); ++it)
    {
        uint32_t id = *it;
        if (id == 0 || id > m_features.size())
            continue;

        auto& info = m_features[id - 1];

        if (info.state == FeatureState::Ready && info.shutdownCallback)
        {
            SKSE::log::info("FeatureManager: shutting down '{}' (id={})",
                            info.name ? info.name : "?", id);
            SEH_CallVoid(info.shutdownCallback);
        }

        if (info.state != FeatureState::Disabled)
            info.state = FeatureState::Registered;
    }

    m_initialized = false;
    SKSE::log::info("FeatureManager: ShutdownAll complete");
}

void FeatureManager::UpdateAll(float dt)
{
    for (auto& info : m_features)
    {
        if (info.state != FeatureState::Ready || !info.updateCallback)
            continue;

        auto t0 = std::chrono::high_resolution_clock::now();
        SEH_CallVoid([&]() { info.updateCallback(dt); });
        auto t1 = std::chrono::high_resolution_clock::now();
        info.lastUpdateMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
    }
}


// ─── Runtime control (cascading) ────────────────────────────────────────

void FeatureManager::Enable(uint32_t id)
{
    if (id == 0 || id > m_features.size())
    {
        SKSE::log::warn("FeatureManager::Enable: invalid id={}", id);
        return;
    }

    auto& info = m_features[id - 1];

    if (info.state == FeatureState::Ready)
        return;  // already enabled

    // Recursively enable dependencies first
    for (uint32_t depId : info.dependencies)
    {
        if (depId == 0 || depId > m_features.size())
            continue;

        if (m_features[depId - 1].state != FeatureState::Ready)
        {
            SKSE::log::info("FeatureManager::Enable: enabling dep '{}' (id={}) for '{}'",
                            m_features[depId - 1].name ? m_features[depId - 1].name : "?",
                            depId, info.name ? info.name : "?");
            Enable(depId);
        }
    }

    // Mark as registered so init will proceed
    info.state = FeatureState::Registered;

    // Attempt initialization
    if (info.initCallback)
    {
        info.state = FeatureState::Initializing;
        bool ok = SEH_CallInit(info.initCallback);
        if (ok)
        {
            info.state = FeatureState::Ready;
            SKSE::log::info("FeatureManager: '{}' (id={}) enabled", info.name ? info.name : "?", id);
        }
        else
        {
            info.state = FeatureState::Failed;
            info.errorCount++;
            SKSE::log::error("FeatureManager: '{}' (id={}) enable failed",
                             info.name ? info.name : "?", id);
        }
    }
    else
    {
        info.state = FeatureState::Ready;
        SKSE::log::info("FeatureManager: '{}' (id={}) enabled (no init callback)",
                        info.name ? info.name : "?", id);
    }
}

void FeatureManager::Disable(uint32_t id)
{
    if (id == 0 || id > m_features.size())
    {
        SKSE::log::warn("FeatureManager::Disable: invalid id={}", id);
        return;
    }

    auto& info = m_features[id - 1];

    if (info.state == FeatureState::Disabled)
        return;  // already disabled

    // First, cascade: disable all features that depend on this one
    for (auto& other : m_features)
    {
        if (other.state == FeatureState::Disabled || other.id == id)
            continue;

        for (uint32_t depId : other.dependencies)
        {
            if (depId == id)
            {
                SKSE::log::info("FeatureManager: disabling dependent '{}' (id={}) before '{}' (id={})",
                                other.name ? other.name : "?", other.id,
                                info.name ? info.name : "?", id);
                Disable(other.id);
                break;
            }
        }
    }

    // Shutdown this feature
    if (info.state == FeatureState::Ready && info.shutdownCallback)
    {
        SKSE::log::info("FeatureManager: shutting down '{}' (id={}) for disable",
                        info.name ? info.name : "?", id);
        SEH_CallVoid(info.shutdownCallback);
    }

    info.state = FeatureState::Disabled;
    SKSE::log::info("FeatureManager: '{}' (id={}) disabled", info.name ? info.name : "?", id);
}


// ─── Query ──────────────────────────────────────────────────────────────

FeatureState FeatureManager::GetState(uint32_t id) const
{
    if (id == 0 || id > m_features.size())
        return FeatureState::Unregistered;
    return m_features[id - 1].state;
}

const char* FeatureManager::GetStateName(FeatureState state) const
{
    switch (state)
    {
    case FeatureState::Unregistered:  return "Unregistered";
    case FeatureState::Registered:    return "Registered";
    case FeatureState::Initializing:  return "Initializing";
    case FeatureState::Ready:         return "Ready";
    case FeatureState::Failed:        return "Failed";
    case FeatureState::Disabled:      return "Disabled";
    default:                          return "Unknown";
    }
}

const std::vector<FeatureInfo>& FeatureManager::GetFeatures() const
{
    return m_features;
}

uint32_t FeatureManager::GetReadyCount() const
{
    uint32_t count = 0;
    for (const auto& f : m_features)
    {
        if (f.state == FeatureState::Ready)
            count++;
    }
    return count;
}

uint32_t FeatureManager::GetFailedCount() const
{
    uint32_t count = 0;
    for (const auto& f : m_features)
    {
        if (f.state == FeatureState::Failed)
            count++;
    }
    return count;
}


// ─── INI persistence ────────────────────────────────────────────────────

void FeatureManager::LoadConfig(const std::filesystem::path& iniPath)
{
    if (iniPath.empty())
    {
        SKSE::log::warn("FeatureManager::LoadConfig: empty INI path");
        return;
    }

    std::string pathStr = iniPath.string();
    SKSE::log::info("FeatureManager: loading config from '{}'", pathStr);

    for (auto& info : m_features)
    {
        if (!info.name)
            continue;

        int val = GetPrivateProfileIntA("Features", info.name, -1, pathStr.c_str());

        if (val == -1)
            continue;  // key not present, keep default state

        if (val == 0)
        {
            info.state = FeatureState::Disabled;
            SKSE::log::info("FeatureManager: '{}' (id={}) disabled by INI", info.name, info.id);
        }
        else
        {
            // Re-enable if it was disabled
            if (info.state == FeatureState::Disabled)
                info.state = FeatureState::Registered;
            SKSE::log::info("FeatureManager: '{}' (id={}) enabled by INI", info.name, info.id);
        }
    }
}

void FeatureManager::SaveConfig(const std::filesystem::path& iniPath) const
{
    if (iniPath.empty())
    {
        SKSE::log::warn("FeatureManager::SaveConfig: empty INI path");
        return;
    }

    std::string pathStr = iniPath.string();
    SKSE::log::info("FeatureManager: saving config to '{}'", pathStr);

    for (const auto& info : m_features)
    {
        if (!info.name)
            continue;

        const char* val = (info.state != FeatureState::Disabled) ? "1" : "0";
        WritePrivateProfileStringA("Features", info.name, val, pathStr.c_str());
    }
}

} // namespace SB
