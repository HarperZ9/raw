// RawHotReload.cpp: Wave 86 Sub-100 ms Atomic Live Hot-Reload & Automatic PSO Rollback.
// Original work, MIT.
#include "RawHotReload.h"

#include <algorithm>
#include <chrono>

namespace raw::bridge::wave86
{

RawHotReloadManager::RawHotReloadManager(wave83::RawPsoCache* psoCache, uint32_t driverTag) noexcept
    : m_psoCache(psoCache), m_driverTag(driverTag)
{
}

bool RawHotReloadManager::RegisterInitialGoodAsset(
    const std::string&          assetPath,
    HotReloadAssetKind          kind,
    const std::string&          sourceText,
    const std::vector<uint8_t>& compiledBlob,
    uint64_t                    abiHash)
{
    if (assetPath.empty() || compiledBlob.empty()) {
        return false;
    }

    const wave83::PsoDigest key = wave83::RawPsoCache::ComputePsoKey(
        reinterpret_cast<const uint8_t*>(sourceText.data()), sourceText.size(),
        reinterpret_cast<const uint8_t*>(&abiHash), sizeof(abiHash),
        m_driverTag);

    if (m_psoCache) {
        m_psoCache->StoreBlob(key, compiledBlob);
    }

    std::lock_guard<std::mutex> lk(m_mtx);
    const uint64_t gen = m_presentGeneration.load(std::memory_order_relaxed) + 1ull;
    HotReloadEntry e{};
    e.assetPath          = assetPath;
    e.kind               = kind;
    e.activeGeneration   = gen;
    e.lastKnownGoodGen   = gen;
    e.expectedAbiHash    = abiHash;
    e.activePsoKey       = key;
    e.activePsoBlob      = compiledBlob;
    e.lastKnownGoodBlob  = compiledBlob;
    m_entries[assetPath] = std::move(e);
    return true;
}

HotReloadReport RawHotReloadManager::StageReload(
    const std::string&        assetPath,
    const std::string&        newSourceText,
    uint64_t                  candidateAbiHash,
    const HotReloadCompileFn& compileFn,
    const HotReloadCanaryFn&  canaryFn)
{
    const auto t0 = std::chrono::steady_clock::now();
    HotReloadReport rep{};

    auto finishElapsed = [&]() {
        const auto t1 = std::chrono::steady_clock::now();
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
    };

    uint64_t expectedAbi = 0ull;
    uint64_t curGen      = 0ull;
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        const auto it = m_entries.find(assetPath);
        if (it == m_entries.end()) {
            rep.rolledBack = true;
            rep.errorCode  = "HOTRELOAD_UNKNOWN_ASSET";
            rep.diagnostic = "Asset '" + assetPath + "' not registered for hot-reload.";
            rep.elapsedUs  = finishElapsed();
            ++m_rejectedReloads;
            return rep;
        }
        expectedAbi = it->second.expectedAbiHash;
        curGen      = it->second.activeGeneration;
    }

    // 1. Fail-closed ABI hash guard before compiling or touching GPU state.
    if (candidateAbiHash != expectedAbi) {
        std::lock_guard<std::mutex> lk(m_mtx);
        ++m_rejectedReloads;
        ++m_rollbackCount;
        rep.rolledBack = true;
        rep.generation = curGen;
        rep.elapsedUs  = finishElapsed();
        rep.errorCode  = "HOTRELOAD_ABI_MISMATCH";
        rep.diagnostic = "Rolled back '" + assetPath + "': candidate ABI hash mismatch; preserved last-known-good PSO.";
        return rep;
    }

    // 2. Compute deterministic SHA-256 PSO key and check Wave 83 RawPsoCache first.
    const wave83::PsoDigest key = wave83::RawPsoCache::ComputePsoKey(
        reinterpret_cast<const uint8_t*>(newSourceText.data()), newSourceText.size(),
        reinterpret_cast<const uint8_t*>(&candidateAbiHash), sizeof(candidateAbiHash),
        m_driverTag);

    std::vector<uint8_t> candidateBlob;
    bool fromCache = false;
    if (m_psoCache && m_psoCache->LoadBlob(key, candidateBlob) && !candidateBlob.empty()) {
        fromCache = true;
    } else {
        if (!compileFn) {
            std::lock_guard<std::mutex> lk(m_mtx);
            ++m_rejectedReloads;
            ++m_rollbackCount;
            rep.rolledBack = true;
            rep.generation = curGen;
            rep.elapsedUs  = finishElapsed();
            rep.errorCode  = "HOTRELOAD_NO_COMPILER";
            rep.diagnostic = "Rolled back '" + assetPath + "': no compiler callback configured.";
            return rep;
        }
        const HotReloadCompileResult cr = compileFn(newSourceText);
        if (!cr.ok || cr.compiledBlob.empty()) {
            std::lock_guard<std::mutex> lk(m_mtx);
            ++m_rejectedReloads;
            ++m_rollbackCount;
            rep.rolledBack = true;
            rep.generation = curGen;
            rep.elapsedUs  = finishElapsed();
            rep.errorCode  = "HOTRELOAD_BROKEN_PSO";
            rep.diagnostic = "Rolled back '" + assetPath + "': compile error (" + cr.errorMessage + "); active PSO untouched.";
            return rep;
        }
        candidateBlob = cr.compiledBlob;
    }

    // 3. Validate compiled candidate against NaN canary BEFORE caching or staging.
    if (canaryFn) {
        const HotReloadCanaryResult can = canaryFn(candidateBlob);
        if (!can.ok || can.nanCount > 0u) {
            std::lock_guard<std::mutex> lk(m_mtx);
            ++m_rejectedReloads;
            ++m_rollbackCount;
            rep.rolledBack = true;
            rep.generation = curGen;
            rep.elapsedUs  = finishElapsed();
            rep.errorCode  = "HOTRELOAD_NAN_CANARY";
            rep.diagnostic = "Rolled back '" + assetPath + "': NaN canary rejected (" + can.diagnostic + "); active PSO untouched.";
            return rep;
        }
    }

    if (!fromCache && m_psoCache) {
        m_psoCache->StoreBlob(key, candidateBlob);
    }

    const uint64_t elapsedUs = finishElapsed();
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_staged.push_back(StagedCandidate{assetPath, key, std::move(candidateBlob), elapsedUs});
        m_maxReloadUs = (std::max)(m_maxReloadUs, elapsedUs);
    }

    rep.staged     = true;
    rep.rolledBack = false;
    rep.generation = curGen;
    rep.elapsedUs  = elapsedUs;
    rep.errorCode  = "STAGED";
    rep.diagnostic = "Candidate staged cleanly for Present-boundary atomic swap.";
    return rep;
}

uint32_t RawHotReloadManager::CommitAtPresentBoundary()
{
    std::lock_guard<std::mutex> lk(m_mtx);
    if (m_staged.empty()) {
        return 0u;
    }
    const uint64_t newGen = m_presentGeneration.fetch_add(1ull, std::memory_order_acq_rel) + 1ull;
    uint32_t committed = 0u;
    for (auto& c : m_staged) {
        auto it = m_entries.find(c.assetPath);
        if (it == m_entries.end()) continue;
        it->second.lastKnownGoodBlob    = it->second.activePsoBlob;
        it->second.lastKnownGoodGen     = it->second.activeGeneration;
        it->second.activePsoKey         = c.psoKey;
        it->second.activePsoBlob        = std::move(c.blob);
        it->second.activeGeneration     = newGen;
        it->second.lastReloadDurationUs = c.elapsedUs;
        ++m_successfulSwaps;
        ++committed;
    }
    m_staged.clear();
    return committed;
}

bool RawHotReloadManager::GetActiveEntry(const std::string& assetPath, HotReloadEntry* outEntry) const
{
    std::lock_guard<std::mutex> lk(m_mtx);
    const auto it = m_entries.find(assetPath);
    if (it == m_entries.end()) return false;
    if (outEntry) *outEntry = it->second;
    return true;
}

HotReloadStats RawHotReloadManager::GetStats() const noexcept
{
    std::lock_guard<std::mutex> lk(m_mtx);
    HotReloadStats s{};
    s.presentGeneration = m_presentGeneration.load(std::memory_order_acquire);
    s.successfulSwaps   = m_successfulSwaps;
    s.rejectedReloads   = m_rejectedReloads;
    s.rollbackCount     = m_rollbackCount;
    s.maxReloadUs       = m_maxReloadUs;
    return s;
}

} // namespace raw::bridge::wave86
