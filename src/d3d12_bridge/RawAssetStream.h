#pragma once
// RawAssetStream.h: Wave 85 Priority Async VRAM Residency & Horizon Streaming Budgeter
// for RAW (THE PLAYGROUND).
// Upgrades NVMe async asset prefetching with:
//   - Distance, screen-coverage, and camera-velocity lookahead priority queue
//   - Epoch-based stale request cancellation on rapid camera travel
//   - Hard 80% VRAM LRU residency budgeter (O(1) touch & eviction, fail-closed cap)
//   - Fixed-capacity lock-guarded async upload ring & zero-leak residency telemetry
//     feeding raw_heuristics_v2.py (Metric #23).
// Original work, MIT. MSVC /W4 /WX clean.
#include "RawAssetBridge.h"
#include "RawTextureBridge.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <list>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace raw::bridge
{

struct StreamRequest
{
    uint64_t    assetHash         = 0u;
    std::string path;
    bool        isMesh            = true;
    float       worldPos[3]       = {0.0f, 0.0f, 0.0f};
    float       boundRadius       = 1.0f;
    float       screenCoverage    = 0.25f;
    float       priorityScore     = 0.0f;
    uint32_t    cancellationEpoch = 0u;
    uint64_t    estimatedBytes    = 0u;
    uint8_t     mipTarget         = 0u;
    uint64_t    sequenceId        = 0u;
};

struct StreamRequestPriorityLess
{
    bool operator()(const StreamRequest& a, const StreamRequest& b) const noexcept
    {
        if (a.priorityScore != b.priorityScore) {
            return a.priorityScore < b.priorityScore; // max-heap: higher score first
        }
        return a.sequenceId > b.sequenceId; // deterministic FIFO tie-break
    }
};

struct VramResidencyTelemetry
{
    uint64_t totalBudgetBytes      = 0u;
    uint64_t hardCapBytes          = 0u;
    uint64_t residentBytes         = 0u;
    uint64_t peakResidentBytes     = 0u;
    uint64_t totalUploadedBytes    = 0u;
    uint64_t totalEvictedBytes     = 0u;
    uint32_t residentAssetCount    = 0u;
    uint32_t evictionCount         = 0u;
    uint32_t cancelledRequests     = 0u;
    uint32_t activePendingRequests = 0u;
    uint32_t uploadRingUsedSlots   = 0u;
    uint32_t uploadRingCapacity    = 0u;
    double   vramResidencyRatio    = 0.0;
    bool     withinHardCap         = true;
};

struct AsyncUploadRingSlot
{
    uint64_t assetHash = 0u;
    uint64_t bytes     = 0u;
    uint32_t epoch     = 0u;
};

// Hard-budgeted O(1) LRU VRAM Residency Governor (enforces <= 80% of VRAM budget).
class VramResidencyBudgeter
{
public:
    explicit VramResidencyBudgeter(
        uint64_t totalBudgetBytes = 512ull * 1024ull * 1024ull,
        float    capFraction      = 0.80f) noexcept;

    void ConfigureBudget(uint64_t totalBudgetBytes, float capFraction = 0.80f) noexcept;

    // Admits a new resident asset or touches an existing resident asset to MRU front.
    // If allowLruEviction is true, evicts LRU tail entries until bytes fit under hardCapBytes.
    // If allowLruEviction is false (or single asset > hardCapBytes), fails closed (returns false).
    bool AcquireOrTouch(
        uint64_t               assetHash,
        uint64_t               bytes,
        uint64_t               frameTick,
        bool                   allowLruEviction = true,
        std::vector<uint64_t>* outEvicted       = nullptr);

    bool Release(uint64_t assetHash);
    void ClearAll();

    [[nodiscard]] bool IsResident(uint64_t assetHash) const noexcept;
    [[nodiscard]] VramResidencyTelemetry GetTelemetry() const noexcept;

private:
    struct ResidentEntry
    {
        uint64_t assetHash     = 0u;
        uint64_t bytes         = 0u;
        uint64_t lastFrameTick = 0u;
    };

    uint64_t m_totalBudgetBytes   = 0u;
    uint64_t m_hardCapBytes       = 0u;
    uint64_t m_residentBytes      = 0u;
    uint64_t m_peakResidentBytes  = 0u;
    uint64_t m_totalUploadedBytes = 0u;
    uint64_t m_totalEvictedBytes  = 0u;
    uint32_t m_evictionCount      = 0u;

    std::list<ResidentEntry>                                        m_lruList; // front=MRU, back=LRU
    std::unordered_map<uint64_t, std::list<ResidentEntry>::iterator> m_lookup;
};

class RawAssetStream {
public:
    static constexpr uint32_t kAsyncUploadRingCapacity = 256u;

    RawAssetStream() = default;
    ~RawAssetStream() { Shutdown(); }

    void Start();
    void StartWorkers(uint32_t workerCount);
    void Shutdown();

    // Legacy FIFO-compatible entry points (assigned high default priority).
    void EnqueueMesh(const std::string& path);
    void EnqueueTexture(const std::string& path);

    // Wave 85 Priority Async Streaming & VRAM Residency API.
    void ConfigureVramBudget(uint64_t totalVramBytes, float capFraction = 0.80f);
    void SetCameraState(const float camPos[3], const float camVel[3], uint32_t epoch);
    void CancelStaleRequests(uint32_t newEpoch);

    static float ComputePriorityScore(
        const float assetPos[3],
        float       boundRadius,
        float       screenCoverage,
        const float camPos[3],
        const float camVel[3]) noexcept;

    void EnqueuePriorityRequest(StreamRequest req);

    bool AdmitResidentAsset(
        uint64_t               assetHash,
        uint64_t               bytes,
        uint64_t               frameTick,
        bool                   allowLruEviction = true,
        std::vector<uint64_t>* outEvicted       = nullptr);

    bool ReleaseResidentAsset(uint64_t assetHash);
    void ClearResidentAssets();

    bool PushAsyncUploadRing(uint64_t assetHash, uint64_t bytes, uint32_t epoch);
    bool PopAsyncUploadRing(AsyncUploadRingSlot* outSlot);

    // Deterministic priority-order dequeue for offline verification without disk IO.
    bool PopNextPriorityRequestForTest(StreamRequest* outReq);

    // Non-blocking drain on the render thread: moves completed assets ready for GPU upload.
    void DrainReady(std::vector<RawMeshAsset>* outMeshes, std::vector<RawTextureAsset>* outTextures);
    uint32_t CompletedCount() const { return m_completed.load(std::memory_order_relaxed); }

    VramResidencyTelemetry GetResidencyTelemetry() const;

private:
    void WorkerLoop();
    bool TryPopActiveRequestLocked(StreamRequest* outReq);

    mutable std::mutex                                                                m_mu;
    std::condition_variable                                                           m_cv;
    bool                                                                              m_running = false;
    std::vector<std::thread>                                                          m_workers;
    std::priority_queue<StreamRequest, std::vector<StreamRequest>, StreamRequestPriorityLess> m_priorityQueue;
    std::vector<RawMeshAsset>                                                         m_readyMeshes;
    std::vector<RawTextureAsset>                                                      m_readyTextures;
    std::atomic<uint32_t>                                                             m_completed{0};

    float                 m_camPos[3]         = {0.0f, 0.0f, 0.0f};
    float                 m_camVel[3]         = {0.0f, 0.0f, 0.0f};
    uint32_t              m_currentEpoch      = 0u;
    uint32_t              m_cancelledRequests = 0u;
    uint64_t              m_nextSeq           = 1u;
    VramResidencyBudgeter m_budgeter;

    std::array<AsyncUploadRingSlot, kAsyncUploadRingCapacity> m_uploadRing{};
    uint32_t                                                  m_ringHead  = 0u;
    uint32_t                                                  m_ringTail  = 0u;
    uint32_t                                                  m_ringCount = 0u;
};

} // namespace raw::bridge
