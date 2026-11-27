// RawAssetStream.cpp: Wave 85 Priority Async VRAM Residency & Horizon Streaming Budgeter
// for RAW. Original work, MIT.
#include "RawAssetStream.h"

#include <algorithm>
#include <cmath>

namespace raw::bridge
{
namespace
{

uint64_t HashStringFnv1a(const std::string& s) noexcept
{
    uint64_t h = 14695981039346656037ull;
    for (unsigned char c : s) {
        h ^= static_cast<uint64_t>(c);
        h *= 1099511628211ull;
    }
    return h;
}

} // namespace

VramResidencyBudgeter::VramResidencyBudgeter(uint64_t totalBudgetBytes, float capFraction) noexcept
{
    ConfigureBudget(totalBudgetBytes, capFraction);
}

void VramResidencyBudgeter::ConfigureBudget(uint64_t totalBudgetBytes, float capFraction) noexcept
{
    const float clampedCap = (std::clamp)(capFraction, 0.05f, 0.80f);
    m_totalBudgetBytes     = totalBudgetBytes;
    m_hardCapBytes         = static_cast<uint64_t>(static_cast<double>(totalBudgetBytes) * static_cast<double>(clampedCap));
}

bool VramResidencyBudgeter::AcquireOrTouch(
    uint64_t               assetHash,
    uint64_t               bytes,
    uint64_t               frameTick,
    bool                   allowLruEviction,
    std::vector<uint64_t>* outEvicted)
{
    if (bytes == 0u) {
        return true;
    }

    auto it = m_lookup.find(assetHash);
    if (it != m_lookup.end()) {
        // Already resident: update size delta if changed and promote to MRU front in O(1).
        ResidentEntry entry = *(it->second);
        const uint64_t oldBytes = entry.bytes;
        if (bytes > oldBytes) {
            const uint64_t delta = bytes - oldBytes;
            if (delta > m_hardCapBytes) {
                return false;
            }
            while ((m_residentBytes + delta) > m_hardCapBytes) {
                if (!allowLruEviction || m_lruList.size() <= 1u) {
                    return false;
                }
                auto victimIt = std::prev(m_lruList.end());
                if (victimIt == it->second) {
                    if (m_lruList.size() <= 1u) {
                        return false;
                    }
                    victimIt = std::prev(victimIt);
                }
                const uint64_t vHash  = victimIt->assetHash;
                const uint64_t vBytes = victimIt->bytes;
                m_residentBytes      -= vBytes;
                m_totalEvictedBytes  += vBytes;
                ++m_evictionCount;
                if (outEvicted) outEvicted->push_back(vHash);
                m_lookup.erase(vHash);
                m_lruList.erase(victimIt);
            }
            m_residentBytes      += delta;
            m_totalUploadedBytes += delta;
        } else if (bytes < oldBytes) {
            m_residentBytes -= (oldBytes - bytes);
        }
        it->second->bytes         = bytes;
        it->second->lastFrameTick = frameTick;
        m_lruList.splice(m_lruList.begin(), m_lruList, it->second);
        m_peakResidentBytes = (std::max)(m_peakResidentBytes, m_residentBytes);
        return true;
    }

    // Fail closed if a single asset exceeds the hard VRAM residency cap.
    if (bytes > m_hardCapBytes) {
        return false;
    }

    while ((m_residentBytes + bytes) > m_hardCapBytes) {
        if (!allowLruEviction || m_lruList.empty()) {
            return false;
        }
        const ResidentEntry victim = m_lruList.back();
        m_lruList.pop_back();
        m_lookup.erase(victim.assetHash);
        m_residentBytes     -= victim.bytes;
        m_totalEvictedBytes += victim.bytes;
        ++m_evictionCount;
        if (outEvicted) {
            outEvicted->push_back(victim.assetHash);
        }
    }

    m_lruList.push_front(ResidentEntry{assetHash, bytes, frameTick});
    m_lookup[assetHash]   = m_lruList.begin();
    m_residentBytes      += bytes;
    m_totalUploadedBytes += bytes;
    m_peakResidentBytes   = (std::max)(m_peakResidentBytes, m_residentBytes);
    return true;
}

bool VramResidencyBudgeter::Release(uint64_t assetHash)
{
    auto it = m_lookup.find(assetHash);
    if (it == m_lookup.end()) {
        return false;
    }
    const uint64_t b = it->second->bytes;
    m_residentBytes  = (b <= m_residentBytes) ? (m_residentBytes - b) : 0u;
    m_lruList.erase(it->second);
    m_lookup.erase(it);
    return true;
}

void VramResidencyBudgeter::ClearAll()
{
    m_lruList.clear();
    m_lookup.clear();
    m_residentBytes = 0u;
}

bool VramResidencyBudgeter::IsResident(uint64_t assetHash) const noexcept
{
    return m_lookup.find(assetHash) != m_lookup.end();
}

VramResidencyTelemetry VramResidencyBudgeter::GetTelemetry() const noexcept
{
    VramResidencyTelemetry t{};
    t.totalBudgetBytes   = m_totalBudgetBytes;
    t.hardCapBytes       = m_hardCapBytes;
    t.residentBytes      = m_residentBytes;
    t.peakResidentBytes  = m_peakResidentBytes;
    t.totalUploadedBytes = m_totalUploadedBytes;
    t.totalEvictedBytes  = m_totalEvictedBytes;
    t.residentAssetCount = static_cast<uint32_t>(m_lookup.size());
    t.evictionCount      = m_evictionCount;
    t.vramResidencyRatio = (m_totalBudgetBytes > 0u)
        ? (static_cast<double>(m_residentBytes) / static_cast<double>(m_totalBudgetBytes))
        : 0.0;
    t.withinHardCap      = (m_residentBytes <= m_hardCapBytes) && (m_peakResidentBytes <= m_hardCapBytes);
    return t;
}

void RawAssetStream::Start()
{
    StartWorkers(2u);
}

void RawAssetStream::StartWorkers(uint32_t workerCount)
{
    std::lock_guard<std::mutex> lk(m_mu);
    if (m_running) return;
    m_running = true;
    const uint32_t n = (std::clamp)(workerCount, 1u, 4u);
    m_workers.reserve(n);
    for (uint32_t i = 0u; i < n; ++i) {
        m_workers.emplace_back(&RawAssetStream::WorkerLoop, this);
    }
}

void RawAssetStream::Shutdown()
{
    {
        std::lock_guard<std::mutex> lk(m_mu);
        if (!m_running) return;
        m_running = false;
    }
    m_cv.notify_all();
    for (auto& w : m_workers) {
        if (w.joinable()) w.join();
    }
    m_workers.clear();
}

void RawAssetStream::ConfigureVramBudget(uint64_t totalVramBytes, float capFraction)
{
    std::lock_guard<std::mutex> lk(m_mu);
    m_budgeter.ConfigureBudget(totalVramBytes, capFraction);
}

void RawAssetStream::SetCameraState(const float camPos[3], const float camVel[3], uint32_t epoch)
{
    std::lock_guard<std::mutex> lk(m_mu);
    if (camPos) {
        m_camPos[0] = camPos[0];
        m_camPos[1] = camPos[1];
        m_camPos[2] = camPos[2];
    }
    if (camVel) {
        m_camVel[0] = camVel[0];
        m_camVel[1] = camVel[1];
        m_camVel[2] = camVel[2];
    }
    if (epoch > m_currentEpoch) {
        m_currentEpoch = epoch;
    }
}

void RawAssetStream::CancelStaleRequests(uint32_t newEpoch)
{
    std::lock_guard<std::mutex> lk(m_mu);
    if (newEpoch > m_currentEpoch) {
        m_currentEpoch = newEpoch;
    }
}

float RawAssetStream::ComputePriorityScore(
    const float assetPos[3],
    float       boundRadius,
    float       screenCoverage,
    const float camPos[3],
    const float camVel[3]) noexcept
{
    if (!assetPos || !camPos) {
        return 0.0f;
    }
    const float dx = assetPos[0] - camPos[0];
    const float dy = assetPos[1] - camPos[1];
    const float dz = assetPos[2] - camPos[2];
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    const float effDist = (std::max)(dist - (std::max)(boundRadius, 0.0f), 0.5f);

    float velBoost = 1.0f;
    if (camVel) {
        const float vLen = std::sqrt(camVel[0] * camVel[0] + camVel[1] * camVel[1] + camVel[2] * camVel[2]);
        if (vLen > 0.25f && dist > 1.0e-3f) {
            const float invD = 1.0f / dist;
            const float invV = 1.0f / vLen;
            const float align = (dx * camVel[0] + dy * camVel[1] + dz * camVel[2]) * invD * invV;
            if (align > 0.0f) {
                velBoost += 0.75f * align * (std::min)(vLen / 10.0f, 2.0f);
            }
        }
    }

    const float covWeight = 1.0f + 4.0f * (std::clamp)(screenCoverage, 0.0f, 1.0f);
    const float score     = (100.0f / effDist) * covWeight * velBoost;
    return std::isfinite(score) ? score : 0.0f;
}

void RawAssetStream::EnqueuePriorityRequest(StreamRequest req)
{
    {
        std::lock_guard<std::mutex> lk(m_mu);
        if (req.cancellationEpoch < m_currentEpoch) {
            ++m_cancelledRequests;
            return;
        }
        if (req.assetHash == 0u && !req.path.empty()) {
            req.assetHash = HashStringFnv1a(req.path);
        }
        if (req.priorityScore <= 0.0f) {
            req.priorityScore = ComputePriorityScore(
                req.worldPos, req.boundRadius, req.screenCoverage, m_camPos, m_camVel);
        }
        req.sequenceId = m_nextSeq++;
        m_priorityQueue.push(std::move(req));
    }
    m_cv.notify_one();
}

void RawAssetStream::EnqueueMesh(const std::string& path)
{
    StreamRequest req{};
    req.path              = path;
    req.assetHash         = HashStringFnv1a(path);
    req.isMesh            = true;
    req.priorityScore     = 1000.0f;
    req.cancellationEpoch = m_currentEpoch;
    EnqueuePriorityRequest(std::move(req));
}

void RawAssetStream::EnqueueTexture(const std::string& path)
{
    StreamRequest req{};
    req.path              = path;
    req.assetHash         = HashStringFnv1a(path);
    req.isMesh            = false;
    req.priorityScore     = 900.0f;
    req.cancellationEpoch = m_currentEpoch;
    EnqueuePriorityRequest(std::move(req));
}

bool RawAssetStream::AdmitResidentAsset(
    uint64_t               assetHash,
    uint64_t               bytes,
    uint64_t               frameTick,
    bool                   allowLruEviction,
    std::vector<uint64_t>* outEvicted)
{
    std::lock_guard<std::mutex> lk(m_mu);
    return m_budgeter.AcquireOrTouch(assetHash, bytes, frameTick, allowLruEviction, outEvicted);
}

bool RawAssetStream::ReleaseResidentAsset(uint64_t assetHash)
{
    std::lock_guard<std::mutex> lk(m_mu);
    return m_budgeter.Release(assetHash);
}

void RawAssetStream::ClearResidentAssets()
{
    std::lock_guard<std::mutex> lk(m_mu);
    m_budgeter.ClearAll();
}

bool RawAssetStream::PushAsyncUploadRing(uint64_t assetHash, uint64_t bytes, uint32_t epoch)
{
    std::lock_guard<std::mutex> lk(m_mu);
    if (m_ringCount >= kAsyncUploadRingCapacity) {
        return false;
    }
    m_uploadRing[m_ringTail] = AsyncUploadRingSlot{assetHash, bytes, epoch};
    m_ringTail               = (m_ringTail + 1u) % kAsyncUploadRingCapacity;
    ++m_ringCount;
    return true;
}

bool RawAssetStream::PopAsyncUploadRing(AsyncUploadRingSlot* outSlot)
{
    std::lock_guard<std::mutex> lk(m_mu);
    if (m_ringCount == 0u) {
        return false;
    }
    if (outSlot) {
        *outSlot = m_uploadRing[m_ringHead];
    }
    m_ringHead = (m_ringHead + 1u) % kAsyncUploadRingCapacity;
    --m_ringCount;
    return true;
}

bool RawAssetStream::TryPopActiveRequestLocked(StreamRequest* outReq)
{
    while (!m_priorityQueue.empty()) {
        StreamRequest top = m_priorityQueue.top();
        m_priorityQueue.pop();
        if (top.cancellationEpoch < m_currentEpoch) {
            ++m_cancelledRequests;
            continue;
        }
        if (outReq) {
            *outReq = std::move(top);
        }
        return true;
    }
    return false;
}

bool RawAssetStream::PopNextPriorityRequestForTest(StreamRequest* outReq)
{
    std::lock_guard<std::mutex> lk(m_mu);
    return TryPopActiveRequestLocked(outReq);
}

void RawAssetStream::DrainReady(std::vector<RawMeshAsset>* outMeshes, std::vector<RawTextureAsset>* outTextures)
{
    std::lock_guard<std::mutex> lk(m_mu);
    if (outMeshes && !m_readyMeshes.empty()) {
        outMeshes->insert(outMeshes->end(), std::make_move_iterator(m_readyMeshes.begin()),
                          std::make_move_iterator(m_readyMeshes.end()));
        m_readyMeshes.clear();
    }
    if (outTextures && !m_readyTextures.empty()) {
        outTextures->insert(outTextures->end(), std::make_move_iterator(m_readyTextures.begin()),
                            std::make_move_iterator(m_readyTextures.end()));
        m_readyTextures.clear();
    }
}

VramResidencyTelemetry RawAssetStream::GetResidencyTelemetry() const
{
    std::lock_guard<std::mutex> lk(m_mu);
    VramResidencyTelemetry t = m_budgeter.GetTelemetry();
    t.cancelledRequests      = m_cancelledRequests;
    t.activePendingRequests  = static_cast<uint32_t>(m_priorityQueue.size());
    t.uploadRingUsedSlots    = m_ringCount;
    t.uploadRingCapacity     = kAsyncUploadRingCapacity;
    return t;
}

void RawAssetStream::WorkerLoop()
{
    while (true) {
        StreamRequest req{};
        {
            std::unique_lock<std::mutex> lk(m_mu);
            m_cv.wait(lk, [&] { return !m_running || !m_priorityQueue.empty(); });
            if (!m_running) break;
            if (!TryPopActiveRequestLocked(&req)) continue;
        }
        if (req.path.empty()) continue;

        if (req.isMesh) {
            RawMeshAsset m;
            if (LoadModernMeshAsset(req.path, &m)) {
                const uint64_t bytes = (req.estimatedBytes > 0u)
                    ? req.estimatedBytes
                    : (static_cast<uint64_t>(m.vertices.size()) * sizeof(RawMeshVertex) +
                       static_cast<uint64_t>(m.indices.size()) * sizeof(uint32_t));
                std::lock_guard<std::mutex> lk(m_mu);
                if (req.cancellationEpoch >= m_currentEpoch &&
                    m_budgeter.AcquireOrTouch(req.assetHash, bytes, m_nextSeq, true, nullptr)) {
                    if (m_ringCount < kAsyncUploadRingCapacity) {
                        m_uploadRing[m_ringTail] = AsyncUploadRingSlot{req.assetHash, bytes, req.cancellationEpoch};
                        m_ringTail               = (m_ringTail + 1u) % kAsyncUploadRingCapacity;
                        ++m_ringCount;
                    }
                    m_readyMeshes.push_back(std::move(m));
                    m_completed.fetch_add(1u, std::memory_order_relaxed);
                }
            }
        } else {
            RawTextureAsset t;
            if (LoadModernTextureAsset(req.path, &t)) {
                const uint64_t bytes = (req.estimatedBytes > 0u)
                    ? req.estimatedBytes
                    : static_cast<uint64_t>(t.rgba8.size());
                std::lock_guard<std::mutex> lk(m_mu);
                if (req.cancellationEpoch >= m_currentEpoch &&
                    m_budgeter.AcquireOrTouch(req.assetHash, bytes, m_nextSeq, true, nullptr)) {
                    if (m_ringCount < kAsyncUploadRingCapacity) {
                        m_uploadRing[m_ringTail] = AsyncUploadRingSlot{req.assetHash, bytes, req.cancellationEpoch};
                        m_ringTail               = (m_ringTail + 1u) % kAsyncUploadRingCapacity;
                        ++m_ringCount;
                    }
                    m_readyTextures.push_back(std::move(t));
                    m_completed.fetch_add(1u, std::memory_order_relaxed);
                }
            }
        }
    }
}

} // namespace raw::bridge
