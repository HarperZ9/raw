// raw_job_system.cpp: Priority Work-Stealing Job System (`raw::core`).
// Original work, MIT.

#include "raw_job_system.h"

#include <algorithm>

namespace raw::core {

void JobCounter::Wait(RawJobSystem* helpSystem) {
    while (!IsComplete()) {
        if (helpSystem && helpSystem->TryRunOnePendingJob()) {
            continue;
        }
        std::unique_lock<std::mutex> lk(m_waitMtx);
        if (IsComplete()) break;
        if (helpSystem) {
            m_waitCv.wait_for(lk, std::chrono::microseconds(250), [this]() { return IsComplete(); });
        } else {
            m_waitCv.wait(lk, [this]() { return IsComplete(); });
        }
    }
}

uint32_t RawJobSystem::RecommendedWorkerCount() noexcept {
    const uint32_t hw = std::thread::hardware_concurrency();
    const uint32_t half = (hw > 0u) ? (hw / 2u) : 2u;
    return std::clamp<uint32_t>(half, 2u, 6u);
}

RawJobSystem::~RawJobSystem() {
    Shutdown();
}

bool RawJobSystem::Init(uint32_t workerCount) {
    if (m_running.load(std::memory_order_acquire)) return true;

    const uint32_t n = (workerCount == 0u) ? RecommendedWorkerCount() : std::clamp<uint32_t>(workerCount, 1u, 16u);
    m_stopping.store(false, std::memory_order_release);
    m_lanes.clear();
    m_lanes.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        m_lanes.push_back(std::make_unique<WorkerLane>());
    }

    m_running.store(true, std::memory_order_release);
    m_workers.clear();
    m_workers.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        m_workers.emplace_back(&RawJobSystem::WorkerLoop, this, i);
    }
    return true;
}

void RawJobSystem::Shutdown() {
    if (!m_running.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    m_stopping.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lk(m_wakeMtx);
        m_wakeCv.notify_all();
    }
    for (auto& t : m_workers) {
        if (t.joinable()) t.join();
    }
    m_workers.clear();

    // Signal any leftover unexecuted jobs so no external waiter can ever hang during teardown.
    for (auto& lane : m_lanes) {
        std::lock_guard<std::mutex> lk(lane->mtx);
        for (size_t p = 0; p < static_cast<size_t>(JobPriority::Count); ++p) {
            while (!lane->queues[p].empty()) {
                JobItem it = std::move(lane->queues[p].front());
                lane->queues[p].pop_front();
                m_pending.fetch_sub(1ull, std::memory_order_relaxed);
                if (it.counter) it.counter->SignalDone();
            }
        }
    }
    m_lanes.clear();
    m_pending.store(0ull, std::memory_order_release);
    m_stopping.store(false, std::memory_order_release);
}

void RawJobSystem::EnqueueInternal(JobPriority priority, JobItem&& item) {
    if (!m_running.load(std::memory_order_acquire) || m_lanes.empty()) {
        // Fallback: execute inline when pool is not running.
        if (item.fn) item.fn();
        if (item.counter) item.counter->SignalDone();
        return;
    }

    const size_t pIdx = std::min<size_t>(static_cast<size_t>(priority), static_cast<size_t>(JobPriority::Count) - 1u);
    const uint32_t laneIdx = m_rrDispatch.fetch_add(1u, std::memory_order_relaxed) % static_cast<uint32_t>(m_lanes.size());
    {
        std::lock_guard<std::mutex> lk(m_lanes[laneIdx]->mtx);
        // Increment pending while holding lane mutex so a stealing worker can never decrement before increment.
        m_pending.fetch_add(1ull, std::memory_order_release);
        m_lanes[laneIdx]->queues[pIdx].push_back(std::move(item));
    }
    m_submitted.fetch_add(1ull, std::memory_order_relaxed);
    m_wakeCv.notify_one();
}

JobHandle RawJobSystem::Submit(JobPriority priority, std::function<void()> fn) {
    auto counter = std::make_shared<JobCounter>(1);
    EnqueueInternal(priority, JobItem{std::move(fn), counter});
    return JobHandle{std::move(counter), this};
}

void RawJobSystem::SubmitWithCounter(JobPriority priority, std::function<void()> fn,
                                     const std::shared_ptr<JobCounter>& counter) {
    if (counter) counter->Add(1);
    EnqueueInternal(priority, JobItem{std::move(fn), counter});
}

JobHandle RawJobSystem::SubmitAfter(const JobHandle& dependency, JobPriority priority, std::function<void()> fn) {
    auto outCounter = std::make_shared<JobCounter>(1);
    if (!dependency.Valid() || dependency.IsComplete()) {
        EnqueueInternal(priority, JobItem{std::move(fn), outCounter});
        return JobHandle{std::move(outCounter), this};
    }

    // Continuation-based DAG dispatch: never blocks a worker thread and is immune to work-stealing stack inversion!
    RawJobSystem* self = this;
    dependency.counter->AddContinuation(
        [self, priority, fn = std::move(fn), outCounter]() mutable {
            self->EnqueueInternal(priority, JobItem{std::move(fn), std::move(outCounter)});
        });
    return JobHandle{std::move(outCounter), this};
}

void RawJobSystem::ParallelFor(size_t count, size_t grainSize, JobPriority priority,
                               const std::function<void(size_t begin, size_t end)>& fn) {
    if (count == 0u || !fn) return;
    const size_t grain = std::max<size_t>(grainSize, 1u);
    if (!m_running.load(std::memory_order_acquire) || count <= grain) {
        fn(0u, count);
        return;
    }

    const size_t numChunks = (count + grain - 1u) / grain;
    auto counter = std::make_shared<JobCounter>(static_cast<int32_t>(numChunks));
    for (size_t c = 0; c < numChunks; ++c) {
        const size_t begin = c * grain;
        const size_t end   = std::min<size_t>(begin + grain, count);
        EnqueueInternal(priority, JobItem{
            [&fn, begin, end]() { fn(begin, end); },
            counter
        });
    }
    counter->Wait(this);
}

bool RawJobSystem::PopLocal(uint32_t workerIdx, JobItem* outItem) {
    if (workerIdx >= m_lanes.size() || !outItem) return false;
    WorkerLane& lane = *m_lanes[workerIdx];
    std::lock_guard<std::mutex> lk(lane.mtx);
    // Strict priority order: High -> Normal -> Low; local pop LIFO for warm cache locality.
    for (size_t p = 0; p < static_cast<size_t>(JobPriority::Count); ++p) {
        if (!lane.queues[p].empty()) {
            *outItem = std::move(lane.queues[p].back());
            lane.queues[p].pop_back();
            m_pending.fetch_sub(1ull, std::memory_order_relaxed);
            return true;
        }
    }
    return false;
}

bool RawJobSystem::StealFromOthers(uint32_t thiefIdx, JobItem* outItem) {
    const uint32_t n = static_cast<uint32_t>(m_lanes.size());
    if (n == 0u || !outItem) return false;
    // Scan priority-first across sibling lanes, stealing FIFO (oldest) to minimize contention.
    for (size_t p = 0; p < static_cast<size_t>(JobPriority::Count); ++p) {
        for (uint32_t offset = 1u; offset <= n; ++offset) {
            const uint32_t victimIdx = (thiefIdx + offset) % n;
            WorkerLane& lane = *m_lanes[victimIdx];
            if (lane.mtx.try_lock()) {
                std::lock_guard<std::mutex> lk(lane.mtx, std::adopt_lock);
                if (!lane.queues[p].empty()) {
                    *outItem = std::move(lane.queues[p].front());
                    lane.queues[p].pop_front();
                    m_pending.fetch_sub(1ull, std::memory_order_relaxed);
                    if (victimIdx != thiefIdx) {
                        m_stolen.fetch_add(1ull, std::memory_order_relaxed);
                    }
                    return true;
                }
            }
        }
    }
    return false;
}

bool RawJobSystem::TryRunOnePendingJob() {
    if (!m_running.load(std::memory_order_acquire) || m_pending.load(std::memory_order_acquire) == 0ull) {
        return false;
    }
    JobItem item{};
    if (!StealFromOthers(0u, &item)) return false;
    if (item.fn) item.fn();
    m_completed.fetch_add(1ull, std::memory_order_relaxed);
    if (item.counter) item.counter->SignalDone();
    return true;
}

void RawJobSystem::WorkerLoop(uint32_t workerIdx) {
    while (!m_stopping.load(std::memory_order_acquire)) {
        JobItem item{};
        if (PopLocal(workerIdx, &item) || StealFromOthers(workerIdx, &item)) {
            if (item.fn) item.fn();
            m_completed.fetch_add(1ull, std::memory_order_relaxed);
            if (item.counter) item.counter->SignalDone();
            continue;
        }
        std::unique_lock<std::mutex> lk(m_wakeMtx);
        m_wakeCv.wait_for(lk, std::chrono::milliseconds(2), [this]() {
            return m_stopping.load(std::memory_order_acquire) ||
                   m_pending.load(std::memory_order_acquire) > 0ull;
        });
    }
}

JobSystemStats RawJobSystem::GetStats() const noexcept {
    JobSystemStats st{};
    st.workerCount   = static_cast<uint32_t>(m_workers.size());
    st.jobsSubmitted = m_submitted.load(std::memory_order_relaxed);
    st.jobsCompleted = m_completed.load(std::memory_order_relaxed);
    st.jobsStolen    = m_stolen.load(std::memory_order_relaxed);
    st.pendingJobs   = m_pending.load(std::memory_order_relaxed);
    st.running       = m_running.load(std::memory_order_acquire);
    return st;
}

}  // namespace raw::core
