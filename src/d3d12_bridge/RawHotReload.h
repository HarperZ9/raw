#pragma once
// RawHotReload.h: Wave 86 Sub-100 ms Atomic Live Hot-Reload & Automatic PSO Rollback.
// Watches HLSL shaders, BuildLang modules, and material presets; compiles candidates via
// Wave 83 RawPsoCache, validates ABI hash & NaN canary output, stages atomic swaps for
// Present-boundary commit (< 100 ms), and automatically rolls back to the last-known-good
// PSO/preset on any compile error, NaN canary, or ABI mismatch (0 crash, 0 black frame).
// Clean-room MIT, MSVC /W4 /WX clean.

#include "RawPsoCache.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace raw::bridge::wave86
{

enum class HotReloadAssetKind : uint32_t
{
    HlslShader      = 0u,
    BuildLangModule = 1u,
    MaterialPreset  = 2u,
};

struct HotReloadCompileResult
{
    bool                 ok = false;
    std::vector<uint8_t> compiledBlob;
    std::string          errorMessage;
};

struct HotReloadCanaryResult
{
    bool        ok       = false;
    uint32_t    nanCount = 0u;
    std::string diagnostic;
};

struct HotReloadEntry
{
    std::string          assetPath;
    HotReloadAssetKind   kind               = HotReloadAssetKind::HlslShader;
    uint64_t             activeGeneration   = 0ull;
    uint64_t             lastKnownGoodGen   = 0ull;
    uint64_t             expectedAbiHash    = 0ull;
    wave83::PsoDigest    activePsoKey{};
    std::vector<uint8_t> activePsoBlob;
    std::vector<uint8_t> lastKnownGoodBlob;
    uint64_t             lastReloadDurationUs = 0ull;
};

struct HotReloadReport
{
    bool        staged        = false;
    bool        rolledBack    = false;
    uint64_t    generation    = 0ull;
    uint64_t    elapsedUs     = 0ull;
    std::string errorCode;
    std::string diagnostic;
};

struct HotReloadStats
{
    uint64_t presentGeneration = 0ull;
    uint64_t successfulSwaps   = 0ull;
    uint64_t rejectedReloads   = 0ull;
    uint64_t rollbackCount     = 0ull;
    uint64_t maxReloadUs       = 0ull;
};

using HotReloadCompileFn = std::function<HotReloadCompileResult(const std::string& source)>;
using HotReloadCanaryFn  = std::function<HotReloadCanaryResult(const std::vector<uint8_t>& blob)>;

class RawHotReloadManager
{
public:
    explicit RawHotReloadManager(wave83::RawPsoCache* psoCache = nullptr, uint32_t driverTag = 0x00010722u) noexcept;

    // Registers an initial verified good asset & PSO blob into active + last-known-good slots.
    bool RegisterInitialGoodAsset(
        const std::string&          assetPath,
        HotReloadAssetKind          kind,
        const std::string&          sourceText,
        const std::vector<uint8_t>& compiledBlob,
        uint64_t                    abiHash);

    // Compiles, caches via Wave 83 RawPsoCache, validates ABI hash + NaN canary, and stages
    // candidate for Present-boundary swap. Any failure automatically rolls back and preserves
    // the last-known-good PSO untouched.
    HotReloadReport StageReload(
        const std::string&        assetPath,
        const std::string&        newSourceText,
        uint64_t                  candidateAbiHash,
        const HotReloadCompileFn& compileFn,
        const HotReloadCanaryFn&  canaryFn);

    // Atomic frame-boundary swap on Present: promotes all validated staged candidates cleanly.
    uint32_t CommitAtPresentBoundary();

    bool GetActiveEntry(const std::string& assetPath, HotReloadEntry* outEntry) const;
    HotReloadStats GetStats() const noexcept;

private:
    struct StagedCandidate
    {
        std::string          assetPath;
        wave83::PsoDigest    psoKey{};
        std::vector<uint8_t> blob;
        uint64_t             elapsedUs = 0ull;
    };

    wave83::RawPsoCache*                             m_psoCache  = nullptr;
    uint32_t                                         m_driverTag = 0x00010722u;
    mutable std::mutex                               m_mtx;
    std::unordered_map<std::string, HotReloadEntry>  m_entries;
    std::vector<StagedCandidate>                     m_staged;
    std::atomic<uint64_t>                            m_presentGeneration{0ull};
    uint64_t                                         m_successfulSwaps = 0ull;
    uint64_t                                         m_rejectedReloads = 0ull;
    uint64_t                                         m_rollbackCount   = 0ull;
    uint64_t                                         m_maxReloadUs     = 0ull;
};

} // namespace raw::bridge::wave86
