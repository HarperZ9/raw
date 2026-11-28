#pragma once
// raw_job_system.h: Priority Work-Stealing Job System (`raw::core`).
// Clean-room replacement for Creation Engine's serialized BSTaskManager / IOManager / TaskQueueInterface.
// Features:
//   - Bounded worker pool: std::clamp(hwThreads / 2, 2, 6) respecting workstation CPU/RAM budgets
//   - 3 priority lanes (High, Normal, Low) with per-worker LIFO local pop + FIFO cross-worker work stealing
//   - Wait-free JobCounter / JobHandle completion barriers with deadlock-free work-helping Wait()
//   - Dependency DAG chaining (SubmitAfter) and grain-batched ParallelFor(count, grain, fn)
//   - Clean RAII shutdown guaranteeing zero hung worker threads on DLL unload.
// Original work, MIT.

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace raw::core {

enum class JobPriority : uint8_t {
    High   = 0,
    Normal = 1,
    Low    = 2,
    Count  = 3
};

class JobCounter;
class RawJobSystem;

// Wave 83: Exact 64-byte single-cache-line inline job slot (zero std::function heap allocation).
using InlineJobFn = void (*)(void* ctx, uint32_t workerIdx);

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
struct alignas(64) RawJobSlot {
    InlineJobFn fn          = nullptr;
    JobCounter* counter     = nullptr;
    uint8_t     payload[48] = {};
};
static_assert(sizeof(RawJobSlot) == 64u, "RawJobSlot must fit in one 64-byte cache line");

// Wave 83: Lock-free Chase-Lev work-stealing deque (owner LIFO push/pop, multi-thief FIFO steal).
template <size_t CapacityPow2 = 4096u>
class ChaseLevDeque {
    static_assert((CapacityPow2 & (CapacityPow2 - 1u)) == 0u && CapacityPow2 >= 16u,
                  "ChaseLevDeque capacity must be a power of two >= 16");
public:
    static constexpr size_t kMask = CapacityPow2 - 1u;

    bool PushBottom(const RawJobSlot& slot) noexcept {
        const int64_t b = m_bottom.load(std::memory_order_relaxed);
        const int64_t t = m_top.load(std::memory_order_acquire);
        if (b - t >= static_cast<int64_t>(CapacityPow2)) {
            return false;
        }
        m_ring[static_cast<size_t>(b) & kMask] = slot;
        std::atomic_thread_fence(std::memory_order_release);
        m_bottom.store(b + 1, std::memory_order_relaxed);
        return true;
    }

    bool PopBottom(RawJobSlot& out) noexcept {
        const int64_t b = m_bottom.load(std::memory_order_relaxed) - 1;
        m_bottom.store(b, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        int64_t t = m_top.load(std::memory_order_relaxed);
        if (t <= b) {
            out = m_ring[static_cast<size_t>(b) & kMask];
            if (t == b) {
                if (!m_top.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed)) {
                    m_bottom.store(b + 1, std::memory_order_relaxed);
                    return false;
                }
                m_bottom.store(b + 1, std::memory_order_relaxed);
            }
            return true;
        }
        m_bottom.store(b + 1, std::memory_order_relaxed);
        return false;
    }

    bool StealTop(RawJobSlot& out) noexcept {
        int64_t t = m_top.load(std::memory_order_acquire);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        const int64_t b = m_bottom.load(std::memory_order_acquire);
        if (t < b) {
            const RawJobSlot candidate = m_ring[static_cast<size_t>(t) & kMask];
            if (m_top.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed)) {
                out = candidate;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] int64_t ApproxSize() const noexcept {
        const int64_t b = m_bottom.load(std::memory_order_relaxed);
        const int64_t t = m_top.load(std::memory_order_relaxed);
        return b > t ? (b - t) : 0;
    }

private:
    alignas(64) std::atomic<int64_t>             m_top{0};
    alignas(64) std::atomic<int64_t>             m_bottom{0};
    std::array<RawJobSlot, CapacityPow2>         m_ring{};
};

// Wave 83: Lock-free bounded MPMC inline slot ring (Vyukov sequence protocol) for concurrent multi-thread SubmitInline.
template <size_t CapacityPow2 = 4096u>
class LockFreeInlineRing {
    static_assert((CapacityPow2 & (CapacityPow2 - 1u)) == 0u && CapacityPow2 >= 16u,
                  "LockFreeInlineRing capacity must be a power of two >= 16");
public:
    static constexpr size_t kMask = CapacityPow2 - 1u;

    LockFreeInlineRing() noexcept {
        for (size_t i = 0u; i < CapacityPow2; ++i) {
            m_cells[i].seq.store(i, std::memory_order_relaxed);
        }
    }

    bool TryPush(const RawJobSlot& slot) noexcept {
        size_t pos = m_enqueuePos.load(std::memory_order_relaxed);
        for (;;) {
            Cell& cell = m_cells[pos & kMask];
            const size_t seq = cell.seq.load(std::memory_order_acquire);
            const intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            if (dif == 0) {
                if (m_enqueuePos.compare_exchange_weak(pos, pos + 1u, std::memory_order_relaxed)) {
                    cell.data = slot;
                    cell.seq.store(pos + 1u, std::memory_order_release);
                    return true;
                }
            } else if (dif < 0) {
                return false;
            } else {
                pos = m_enqueuePos.load(std::memory_order_relaxed);
            }
        }
    }

    bool TryPop(RawJobSlot& out) noexcept {
        size_t pos = m_dequeuePos.load(std::memory_order_relaxed);
        for (;;) {
            Cell& cell = m_cells[pos & kMask];
            const size_t seq = cell.seq.load(std::memory_order_acquire);
            const intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1u);
            if (dif == 0) {
                if (m_dequeuePos.compare_exchange_weak(pos, pos + 1u, std::memory_order_relaxed)) {
                    out = cell.data;
                    cell.seq.store(pos + CapacityPow2, std::memory_order_release);
                    return true;
                }
            } else if (dif < 0) {
                return false;
            } else {
                pos = m_dequeuePos.load(std::memory_order_relaxed);
            }
        }
    }

private:
    struct alignas(64) Cell {
        std::atomic<size_t> seq{0u};
        RawJobSlot          data{};
    };
    alignas(64) std::atomic<size_t>         m_enqueuePos{0u};
    alignas(64) std::atomic<size_t>         m_dequeuePos{0u};
    std::array<Cell, CapacityPow2>          m_cells{};
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif

// Shared completion barrier counter for one or more jobs.
class JobCounter {
public:
    explicit JobCounter(int32_t initialCount = 0) noexcept : m_remaining(initialCount) {}

    void Add(int32_t delta = 1) noexcept {
        m_remaining.fetch_add(delta, std::memory_order_acq_rel);
    }

    void SignalDone() noexcept {
        if (m_remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            std::vector<std::function<void()>> conts;
            {
                std::lock_guard<std::mutex> lk(m_waitMtx);
                m_waitCv.notify_all();
                conts.swap(m_continuations);
            }
            for (auto& c : conts) {
                if (c) c();
            }
        }
    }

    // Registers a callback invoked immediately if already complete, or right when remaining hits 0.
    void AddContinuation(std::function<void()> cont) {
        if (!cont) return;
        bool runNow = false;
        {
            std::lock_guard<std::mutex> lk(m_waitMtx);
            if (IsComplete()) {
                runNow = true;
            } else {
                m_continuations.push_back(std::move(cont));
            }
        }
        if (runNow) cont();
    }

    [[nodiscard]] bool IsComplete() const noexcept {
        return m_remaining.load(std::memory_order_acquire) <= 0;
    }

    [[nodiscard]] int32_t Remaining() const noexcept {
        return m_remaining.load(std::memory_order_acquire);
    }

    // Blocks until remaining == 0. If `helpSystem` is provided, helps run queued jobs while waiting (zero deadlock!).
    void Wait(RawJobSystem* helpSystem = nullptr);

private:
    std::atomic<int32_t>               m_remaining{0};
    std::mutex                         m_waitMtx;
    std::condition_variable            m_waitCv;
    std::vector<std::function<void()>> m_continuations;
};

struct JobHandle {
    std::shared_ptr<JobCounter> counter;
    RawJobSystem*               owner = nullptr;

    [[nodiscard]] bool Valid() const noexcept { return counter != nullptr; }
    [[nodiscard]] bool IsComplete() const noexcept { return !counter || counter->IsComplete(); }
    void Wait() const {
        if (counter) counter->Wait(owner);
    }
};

struct JobSystemStats {
    uint32_t workerCount    = 0u;
    uint64_t jobsSubmitted  = 0ull;
    uint64_t jobsCompleted  = 0ull;
    uint64_t jobsStolen     = 0ull;
    uint64_t pendingJobs    = 0ull;
    bool     running        = false;
};

class RawJobSystem {
public:
    // Computes the workstation-safe default worker count: std::clamp(hwThreads / 2, 2, 6).
    [[nodiscard]] static uint32_t RecommendedWorkerCount() noexcept;

    RawJobSystem() = default;
    ~RawJobSystem();

    RawJobSystem(const RawJobSystem&) = delete;
    RawJobSystem& operator=(const RawJobSystem&) = delete;

    // Starts the bounded worker pool (workerCount == 0 uses RecommendedWorkerCount()).
    bool Init(uint32_t workerCount = 0u);
    void Shutdown();

    [[nodiscard]] bool IsRunning() const noexcept { return m_running.load(std::memory_order_acquire); }
    [[nodiscard]] uint32_t WorkerCount() const noexcept { return static_cast<uint32_t>(m_workers.size()); }

    // Submits a single job at `priority`; returns a JobHandle for completion polling or waiting.
    JobHandle Submit(JobPriority priority, std::function<void()> fn);

    // Submits a job that increments/signals an external shared JobCounter.
    void SubmitWithCounter(JobPriority priority, std::function<void()> fn, const std::shared_ptr<JobCounter>& counter);

    // Submits a dependent job that executes strictly after `dependency` completes (DAG edge).
    JobHandle SubmitAfter(const JobHandle& dependency, JobPriority priority, std::function<void()> fn);

    // Wave 83: Deterministic multi-input fan-in DAG barrier (fires strictly after ALL dependencies complete).
    JobHandle SubmitAfterAll(const std::vector<JobHandle>& dependencies, JobPriority priority, std::function<void()> fn);

    // Wave 83: Zero-allocation 64B inline job dispatch into lock-free worker ring (0 mutexes in hot path).
    bool SubmitInline(JobPriority priority, InlineJobFn fn, const void* ctxBytes, size_t ctxSize, JobCounter* counter);

    // Dispatches [0, count) in chunks of `grainSize` across workers and blocks (while helping!) until done.
    void ParallelFor(size_t count, size_t grainSize, JobPriority priority,
                     const std::function<void(size_t begin, size_t end)>& fn);

    // Runs at most one pending job on the calling thread (used by JobCounter::Wait work-helping).
    bool TryRunOnePendingJob();

    [[nodiscard]] JobSystemStats GetStats() const noexcept;

private:
    struct JobItem {
        std::function<void()>       fn;
        std::shared_ptr<JobCounter> counter;
    };

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324) // Intentional cache-line padding for per-worker lane isolation
#endif
    struct alignas(64) WorkerLane {
        LockFreeInlineRing<4096u> inlineRing; // Wave 83: zero-mutex, zero-alloc 64B slot fast path
        std::mutex                mtx;
        std::deque<JobItem>       queues[static_cast<size_t>(JobPriority::Count)];
    };
#ifdef _MSC_VER
#pragma warning(pop)
#endif

    void WorkerLoop(uint32_t workerIdx);
    bool PopLocalInline(uint32_t workerIdx, RawJobSlot* outSlot);
    bool StealInlineFromOthers(uint32_t thiefIdx, RawJobSlot* outSlot);
    bool PopLocal(uint32_t workerIdx, JobItem* outItem);
    bool StealFromOthers(uint32_t thiefIdx, JobItem* outItem);
    void EnqueueInternal(JobPriority priority, JobItem&& item);

    std::atomic<bool>                         m_running{false};
    std::atomic<bool>                         m_stopping{false};
    std::vector<std::unique_ptr<WorkerLane>>  m_lanes;
    std::vector<std::thread>                  m_workers;
    std::atomic<uint32_t>                     m_rrDispatch{0u};

    std::mutex                                m_wakeMtx;
    std::condition_variable                   m_wakeCv;

    std::atomic<uint64_t>                     m_submitted{0ull};
    std::atomic<uint64_t>                     m_completed{0ull};
    std::atomic<uint64_t>                     m_stolen{0ull};
    std::atomic<uint64_t>                     m_pending{0ull};
};

}  // namespace raw::core
