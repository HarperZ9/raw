// test_core_runtime.cpp: Multi-threaded stress, canary safety, job-DAG & async-vs-sync streaming tests
// for RawSlabAllocator, RawJobSystem, and RawWorldShowcase async streaming.
// Export: int raw::core::RunCoreRuntimeTests(void (*checkFn)(bool, const char*) = nullptr)
// Original work, MIT.

#include "../src/engine_rt/raw_job_system.h"
#include "../src/engine_rt/raw_slab_allocator.h"
#include "../src/worldgen/RawWorldShowcase.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace raw::core {

int RunCoreRuntimeTests(void (*checkFn)(bool, const char*)) {
    int localFail = 0;
    auto check = [&](bool cond, const char* name) {
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s %s\n", cond ? "PASS" : "FAIL", name);
        }
        if (!cond) ++localFail;
    };

    // 1. Multi-threaded 8 threads x 25,000 random-size slab alloc/free stress test (200,000 total ops!)
    {
        RawSlabAllocator alloc(512u * 1024u);
        std::atomic<uint32_t> patternErrors{0u};
        std::atomic<uint32_t> nullErrors{0u};

        constexpr uint32_t kThreads = 8u;
        constexpr uint32_t kOpsPerThread = 25000u;
        constexpr size_t kTestSizes[] = {
            8u, 16u, 24u, 32u, 48u, 64u, 112u, 128u, 256u, 512u, 1024u, 2048u, 4096u, 8192u, 16384u, 24576u
        };

        std::vector<std::thread> threads;
        threads.reserve(kThreads);
        for (uint32_t t = 0; t < kThreads; ++t) {
            threads.emplace_back([&, t]() {
                uint64_t rng = 0x9E3779B97F4A7C15ull ^ (uint64_t(t + 1u) * 0xBF58476D1CE4E5B9ull);
                auto nextRand = [&]() -> uint64_t {
                    rng ^= rng >> 12;
                    rng ^= rng << 25;
                    rng ^= rng >> 27;
                    return rng * 0x2545F4914F6CDD1Dull;
                };

                struct LiveRec { void* ptr = nullptr; size_t sz = 0; uint8_t pat = 0; };
                std::vector<LiveRec> live;
                live.reserve(64u);

                for (uint32_t i = 0; i < kOpsPerThread; ++i) {
                    const bool doFree = (!live.empty() && ((nextRand() & 1u) == 0u || live.size() >= 48u));
                    if (doFree) {
                        const size_t idx = size_t(nextRand() % live.size());
                        const LiveRec r = live[idx];
                        const auto* bytes = static_cast<const uint8_t*>(r.ptr);
                        if (bytes[0] != r.pat || bytes[r.sz - 1] != r.pat) {
                            patternErrors.fetch_add(1u, std::memory_order_relaxed);
                        }
                        if (alloc.FreeChecked(r.ptr) != FreeResult::Ok) {
                            patternErrors.fetch_add(1u, std::memory_order_relaxed);
                        }
                        live[idx] = live.back();
                        live.pop_back();
                    } else {
                        const size_t sz = kTestSizes[nextRand() % (sizeof(kTestSizes) / sizeof(kTestSizes[0]))];
                        void* p = alloc.Alloc(sz, 16u, raw::ce_rebuild::AllocDomain::Simulation);
                        if (!p) {
                            nullErrors.fetch_add(1u, std::memory_order_relaxed);
                            continue;
                        }
                        const uint8_t pat = static_cast<uint8_t>((t * 31u + i) & 0xFFu);
                        auto* bytes = static_cast<uint8_t*>(p);
                        bytes[0] = pat;
                        bytes[sz - 1] = pat;
                        live.push_back({p, sz, pat});
                    }
                }
                for (const LiveRec& r : live) {
                    const auto* bytes = static_cast<const uint8_t*>(r.ptr);
                    if (bytes[0] != r.pat || bytes[r.sz - 1] != r.pat) {
                        patternErrors.fetch_add(1u, std::memory_order_relaxed);
                    }
                    if (alloc.FreeChecked(r.ptr) != FreeResult::Ok) {
                        patternErrors.fetch_add(1u, std::memory_order_relaxed);
                    }
                }
            });
        }
        for (auto& th : threads) th.join();

        const SlabAllocatorMetrics m = alloc.GetMetrics();
        std::printf("[SlabStress] 8x25k ops: totalAlloc=%llu slabHits=%llu large=%llu casRetries=%llu highWater=%llu B live=%llu\n",
                    static_cast<unsigned long long>(m.totalAllocations),
                    static_cast<unsigned long long>(m.slabHits),
                    static_cast<unsigned long long>(m.largeFallbacks),
                    static_cast<unsigned long long>(m.casRetries),
                    static_cast<unsigned long long>(m.highWaterBytes),
                    static_cast<unsigned long long>(m.liveAllocations));

        check(nullErrors.load() == 0u && patternErrors.load() == 0u &&
              m.liveAllocations == 0ull && m.liveBytes == 0ull &&
              m.canaryCorruptions == 0ull && m.doubleFreeRejects == 0ull &&
              m.totalAllocations == m.totalDeallocations && m.slabHits > 50000ull && m.largeFallbacks > 1000ull,
              "core_slab_8thread_200k_lockfree_stress_zero_leaks");
    }

    // 2. Canary corruption, double-free, foreign-pointer & IAllocatorBackend FrameLinear arena tests
    {
        RawSlabAllocator alloc(256u * 1024u);
        raw::ce_rebuild::IAllocatorBackend* iface = &alloc;

        // Canary corruption test (1-byte buffer overrun)
        auto* pOver = static_cast<uint8_t*>(iface->Allocate(raw::ce_rebuild::AllocDomain::TerrainChunk, 64u, 16u));
        check(pOver != nullptr && alloc.ValidatePointer(pOver), "core_slab_valid_pointer_check");
        pOver[64] ^= 0x5Au; // Deliberate 1-byte overflow into footer canary!
        check(!alloc.ValidatePointer(pOver), "core_slab_validate_detects_overrun");
        check(alloc.FreeChecked(pOver) == FreeResult::CanaryCorrupted, "core_slab_free_detects_canary_corruption");

        // Double-free detection test
        void* pDbl = iface->Allocate(raw::ce_rebuild::AllocDomain::AssetStreaming, 128u, 16u);
        check(alloc.FreeChecked(pDbl) == FreeResult::Ok, "core_slab_first_free_ok");
        check(alloc.FreeChecked(pDbl) == FreeResult::DoubleFree, "core_slab_double_free_rejected");

        // Foreign pointer detection test
        alignas(32) uint8_t stackBuf[128]{};
        check(alloc.FreeChecked(stackBuf + 32) == FreeResult::ForeignPointer, "core_slab_foreign_ptr_rejected");

        // Triple-buffered FrameLinear bump arena test
        iface->ResetFrameArena(0u);
        void* f0 = iface->Allocate(raw::ce_rebuild::AllocDomain::FrameLinear, 1024u, 64u);
        void* f1 = iface->Allocate(raw::ce_rebuild::AllocDomain::FrameLinear, 2048u, 64u);
        const auto fStats = iface->QueryStats(raw::ce_rebuild::AllocDomain::FrameLinear);
        iface->ResetFrameArena(0u);
        const auto fStatsAfter = iface->QueryStats(raw::ce_rebuild::AllocDomain::FrameLinear);
        check(f0 != nullptr && f1 != nullptr && (reinterpret_cast<uintptr_t>(f0) % 64u) == 0u &&
              fStats.bytesInUse >= 3072u && fStatsAfter.bytesInUse == 0u && fStatsAfter.frameResets >= 2u,
              "core_slab_frame_linear_triple_buffer_reset");

        const SlabAllocatorMetrics m = alloc.GetMetrics();
        check(m.canaryCorruptions == 1ull && m.doubleFreeRejects == 1ull && m.foreignPtrRejects == 1ull,
              "core_slab_safety_counters_exact");
    }

    // 3. RawJobSystem: ParallelFor, Priority Lanes, Dependency DAG (SubmitAfter), & Repeated Init/Shutdown
    {
        RawJobSystem js;
        const uint32_t rec = RawJobSystem::RecommendedWorkerCount();
        check(rec >= 2u && rec <= 6u, "core_jobs_recommended_workers_bounded_2_to_6");
        check(js.Init(4u) && js.IsRunning() && js.WorkerCount() == 4u, "core_jobs_init_4_workers");

        // ParallelFor deterministic sum over 100,000 items
        constexpr size_t kN = 100000u;
        std::vector<uint64_t> values(kN, 0ull);
        js.ParallelFor(kN, 1024u, JobPriority::High, [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) values[i] = uint64_t(i + 1u);
        });
        uint64_t sum = 0ull;
        for (uint64_t v : values) sum += v;
        const uint64_t expectedSum = uint64_t(kN) * uint64_t(kN + 1u) / 2ull;
        check(sum == expectedSum, "core_jobs_parallel_for_100k_exact");

        // Dependency DAG: A -> B -> C strict ordering
        std::atomic<int> stage{0};
        std::atomic<bool> orderOk{true};
        JobHandle hA = js.Submit(JobPriority::Normal, [&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            if (stage.exchange(1) != 0) orderOk.store(false);
        });
        JobHandle hB = js.SubmitAfter(hA, JobPriority::High, [&]() {
            if (stage.exchange(2) != 1) orderOk.store(false);
        });
        JobHandle hC = js.SubmitAfter(hB, JobPriority::Low, [&]() {
            if (stage.exchange(3) != 2) orderOk.store(false);
        });
        hC.Wait();
        check(orderOk.load() && stage.load() == 3 && hC.IsComplete(), "core_jobs_dependency_dag_strict_order");

        // Repeated shutdown/re-init stress (verifies zero hung threads on DLL unload!)
        bool cleanCycles = true;
        for (int cyc = 0; cyc < 5; ++cyc) {
            std::atomic<int> doneCount{0};
            for (int j = 0; j < 32; ++j) {
                js.Submit(JobPriority::Normal, [&]() { doneCount.fetch_add(1, std::memory_order_relaxed); });
            }
            js.Shutdown();
            if (js.IsRunning()) cleanCycles = false;
            if (cyc < 4 && !js.Init(2u)) cleanCycles = false;
        }
        check(cleanCycles, "core_jobs_repeated_shutdown_zero_hang");
    }

    // 4. RawWorldShowcase Async-vs-Sync Bit-Exact Meshlet Determinism & Low Main-Thread Dispatch Cost
    {
        raw::worldgen::StreamConfig syncCfg{};
        syncCfg.showcaseMode     = true;
        syncCfg.lod0RadiusChunks = 0;
        syncCfg.lod1RadiusChunks = 1;
        syncCfg.lod2RadiusChunks = 1;
        syncCfg.lod3RadiusChunks = 1;
        syncCfg.maxLoadsPerFrame = 9u;
        syncCfg.maxMeshletTris   = 64u;
        syncCfg.asyncStreaming   = false;

        raw::worldgen::StreamConfig asyncCfg = syncCfg;
        asyncCfg.asyncStreaming   = true;
        asyncCfg.asyncWorkerCount = 2u;

        const raw::worldgen::Vec3 camPos{225000.0f, 18000.0f, 1200.0f};

        raw::worldgen::RawWorldShowcase syncMgr;
        raw::worldgen::RawWorldShowcase asyncMgr;
        check(syncMgr.Init(syncCfg) && asyncMgr.Init(asyncCfg), "core_showcase_sync_async_init");

        // Sync finishes all 9 chunks immediately on Update 1
        const auto syncSt = syncMgr.Update(camPos, nullptr);

        // Async dispatches non-blockingly (measure main-thread dispatch time), then drains completed chunks
        const auto t0 = std::chrono::steady_clock::now();
        const auto asyncDispatchSt = asyncMgr.Update(camPos, nullptr);
        const double dispatchMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();
        (void)asyncDispatchSt;

        asyncMgr.WaitForAsyncIdle();
        raw::worldgen::StreamUpdateStats asyncFinalSt{};
        for (int f = 0; f < 4; ++f) {
            asyncFinalSt = asyncMgr.Update(camPos, nullptr);
            if (asyncFinalSt.activeChunkCount == syncSt.activeChunkCount) break;
            asyncMgr.WaitForAsyncIdle();
        }

        std::printf("[AsyncShowcase] dispatch=%.3f ms, syncChunks=%u (meshlets=%u), asyncChunks=%u (meshlets=%u)\n",
                    dispatchMs, syncSt.activeChunkCount, syncSt.totalMeshlets,
                    asyncFinalSt.activeChunkCount, asyncFinalSt.totalMeshlets);

        bool bitExact = (syncMgr.ActiveChunkCount() == asyncMgr.ActiveChunkCount()) && (syncMgr.ActiveChunkCount() == 9u);
        for (const auto& kv : syncMgr.ResidentChunks()) {
            const auto* aEntry = asyncMgr.FindChunk(kv.first);
            if (!aEntry) { bitExact = false; break; }
            const auto& sm = kv.second.mesh;
            const auto& am = aEntry->mesh;
            if (sm.vertices.size() != am.vertices.size() ||
                sm.indices.size()  != am.indices.size()  ||
                sm.meshlets.size() != am.meshlets.size() ||
                kv.second.props.size() != aEntry->props.size()) {
                bitExact = false;
                break;
            }
            if (!sm.vertices.empty() &&
                std::memcmp(sm.vertices.data(), am.vertices.data(), sm.vertices.size() * sizeof(raw::worldgen::TerrainVertex)) != 0) {
                bitExact = false;
                break;
            }
            if (!sm.indices.empty() &&
                std::memcmp(sm.indices.data(), am.indices.data(), sm.indices.size() * sizeof(uint32_t)) != 0) {
                bitExact = false;
                break;
            }
        }
        check(bitExact, "core_showcase_async_vs_sync_bit_exact_meshlets");
        check(dispatchMs < 25.0, "core_showcase_async_dispatch_non_blocking");
    }

    return localFail;
}

}  // namespace raw::core

#ifdef RAW_CORE_RUNTIME_STANDALONE_MAIN
int main() {
    const int fails = raw::core::RunCoreRuntimeTests(nullptr);
    std::printf("CoreRuntimeTests completed: %s (failures=%d)\n", fails == 0 ? "PASS" : "FAIL", fails);
    return fails == 0 ? 0 : 1;
}
#endif
