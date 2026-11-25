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

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
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

class RawJobSystem;

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
        std::mutex          mtx;
        std::deque<JobItem> queues[static_cast<size_t>(JobPriority::Count)];
    };
#ifdef _MSC_VER
#pragma warning(pop)
#endif

    void WorkerLoop(uint32_t workerIdx);
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
