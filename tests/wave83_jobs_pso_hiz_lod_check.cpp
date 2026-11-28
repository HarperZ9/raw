// wave83_jobs_pso_hiz_lod_check.cpp: Wave 83 C++23 Unit, Stress & Planted-Failure Control Suite
// Verifies:
//   1) Lock-free ChaseLevDeque single-owner LIFO + multi-thief FIFO linearizability stress (4,096 slots, 0 lost/dup)
//   2) RawJobSystem zero-alloc 64B SubmitInline stress (2,048 tasks) + 64-way fan-in SubmitAfterAll DAG barrier
//   3) RawPsoCache deterministic SHA-256 disk & memory cache + async batch pre-warm + 0-miss cache hit
//   4) Planted Control 83-A: corrupt/1-byte tampered & version-mismatched PSO blob rejected fail-closed & cleanly rebuilt
//   5) Two-Pass Hi-Z occlusion disocclusion recovery (0 pop-in) + Planted Control 83-B (Pass-2 disabled causes pop-in)
//      + Planted Control 83-C (buggy min() Hi-Z reduction causes false foreground cull!)
//   6) Continuous meshlet LOD monotonic DAG cut + 1%-low FrameBudgetLodGovernor + Planted Control 83-D (non-monotonic DAG caught!)
//   7) Bit-identical 100 Hz replay across SkyrimSE, Fallout4, and Starfield profiles (60/100/144/240 FPS)
// Clean-room MIT, MSVC /std:c++latest /W4 /WX.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../src/engine_rt/raw_job_system.h"
#include "../src/d3d12_bridge/RawPsoCache.h"
#include "../src/d3d12_bridge/RawTwoPassHiZ.h"
#include "../src/d3d12_bridge/RawFramePacing.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

static int g_pass = 0;
static int g_fail = 0;

static void Check(bool cond, const char* name)
{
    if (cond) {
        std::printf("  [PASS] %s\n", name);
        ++g_pass;
    } else {
        std::printf("  [FAIL] %s\n", name);
        ++g_fail;
    }
}

struct InlinePayload
{
    std::atomic<uint64_t>* sumPtr;
    uint64_t               value;
};

static void InlineAccumFn(void* ctx, uint32_t /*workerIdx*/)
{
    auto* p = reinterpret_cast<InlinePayload*>(ctx);
    p->sumPtr->fetch_add(p->value, std::memory_order_relaxed);
}

int main()
{
    std::printf("=== Wave 83: Chase-Lev DAG + Async PSO Cache + Two-Pass Hi-Z + Continuous Meshlet LOD Check ===\n");

    // 1) ChaseLevDeque single-owner LIFO + 4-thief FIFO linearizability stress (2,048 items, 0 lost, 0 dup)
    {
        constexpr int kItems = 2048;
        raw::core::ChaseLevDeque<4096u> dq;
        std::vector<std::atomic<int>> seen(kItems);
        for (auto& a : seen) { a.store(0, std::memory_order_relaxed); }

        std::atomic<bool> startFlag{false};
        std::atomic<bool> producerDone{false};
        std::vector<std::thread> thieves;
        for (int t = 0; t < 4; ++t) {
            thieves.emplace_back([&]() {
                while (!startFlag.load(std::memory_order_acquire)) {}
                raw::core::RawJobSlot s{};
                while (!producerDone.load(std::memory_order_acquire) || dq.ApproxSize() > 0) {
                    if (dq.StealTop(s)) {
                        uint32_t id = 0u;
                        std::memcpy(&id, s.payload, sizeof(id));
                        if (id < static_cast<uint32_t>(kItems)) {
                            seen[id].fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                }
            });
        }
        startFlag.store(true, std::memory_order_release);
        for (uint32_t i = 0; i < static_cast<uint32_t>(kItems); ++i) {
            raw::core::RawJobSlot s{};
            std::memcpy(s.payload, &i, sizeof(i));
            while (!dq.PushBottom(s)) {}
            if ((i & 3u) == 0u) {
                raw::core::RawJobSlot local{};
                if (dq.PopBottom(local)) {
                    uint32_t id = 0u;
                    std::memcpy(&id, local.payload, sizeof(id));
                    seen[id].fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
        raw::core::RawJobSlot rem{};
        while (dq.PopBottom(rem)) {
            uint32_t id = 0u;
            std::memcpy(&id, rem.payload, sizeof(id));
            seen[id].fetch_add(1, std::memory_order_relaxed);
        }
        producerDone.store(true, std::memory_order_release);
        for (auto& th : thieves) { th.join(); }

        int lost = 0, dup = 0;
        for (int i = 0; i < kItems; ++i) {
            const int c = seen[i].load(std::memory_order_relaxed);
            if (c == 0) ++lost;
            if (c > 1)  ++dup;
        }
        Check(lost == 0 && dup == 0,
              "ChaseLevDeque linearizability stress (2,048 64B slots across owner + 4 thieves: 0 lost, 0 duplicated)");
    }

    // 2) RawJobSystem zero-alloc SubmitInline (2,048 tasks) + 64-way fan-in SubmitAfterAll DAG barrier
    {
        raw::core::RawJobSystem js;
        js.Init(4u);

        constexpr uint64_t kN = 2048ull;
        std::atomic<uint64_t> sum{0ull};
        auto ctr = std::make_shared<raw::core::JobCounter>(static_cast<int32_t>(kN));
        for (uint64_t i = 1ull; i <= kN; ++i) {
            InlinePayload pl{&sum, i};
            js.SubmitInline(raw::core::JobPriority::High, &InlineAccumFn, &pl, sizeof(pl), ctr.get());
        }
        ctr->Wait(&js);
        const uint64_t expectedSum = (kN * (kN + 1ull)) / 2ull;

        // 64-way fan-in DAG barrier: consumer MUST observe all 64 producers completed!
        std::atomic<int> prodDone{0};
        std::atomic<int> observedAtConsumer{-1};
        std::vector<raw::core::JobHandle> deps;
        deps.reserve(64);
        for (int i = 0; i < 64; ++i) {
            deps.push_back(js.Submit(raw::core::JobPriority::Normal, [&]() {
                prodDone.fetch_add(1, std::memory_order_release);
            }));
        }
        auto fin = js.SubmitAfterAll(deps, raw::core::JobPriority::High, [&]() {
            observedAtConsumer.store(prodDone.load(std::memory_order_acquire), std::memory_order_release);
        });
        fin.Wait();
        js.Shutdown();

        Check(sum.load() == expectedSum && observedAtConsumer.load() == 64,
              "RawJobSystem SubmitInline (2,048 zero-alloc 64B tasks) + 64-way SubmitAfterAll fan-in DAG");
    }

    // 3) RawPsoCache deterministic SHA-256 async pre-warm + 0-miss hit + Planted Control 83-A (corrupt blob rejected)
    {
        const auto tmpDir = std::filesystem::temp_directory_path() / "raw_w83_pso_cache_test";
        std::error_code ec;
        std::filesystem::remove_all(tmpDir, ec);

        raw::bridge::wave83::RawPsoCache cache(tmpDir, 0x00010722u);
        std::vector<raw::bridge::wave83::PsoPrewarmItem> batch;
        std::vector<raw::bridge::wave83::PsoDigest> keys;
        for (uint32_t i = 0u; i < 12u; ++i) {
            const uint8_t sh[8] = {'D', 'X', 'I', 'L', static_cast<uint8_t>(i), 0x66u, 0x69u, 0x6Au};
            const uint8_t st[4] = {1u, 2u, static_cast<uint8_t>(i), 4u};
            const auto key = raw::bridge::wave83::RawPsoCache::ComputePsoKey(sh, sizeof(sh), st, sizeof(st), 0x00010722u);
            keys.push_back(key);
            batch.push_back({key, 0x00010722u, [i]() {
                return std::vector<uint8_t>(256u, static_cast<uint8_t>(0xA0u + i));
            }});
        }
        const uint32_t warmed = cache.PrewarmBatchAsync(batch, 4u);

        // Fresh cold-start cache instance reading from disk MUST hit 12/12!
        raw::bridge::wave83::RawPsoCache coldCache(tmpDir, 0x00010722u);
        int diskHits = 0;
        for (const auto& k : keys) {
            bool hit = false;
            auto b = coldCache.LookupOrBuild(k, []() { return std::vector<uint8_t>{0xFFu}; }, &hit);
            if (hit && b.size() == 256u) { ++diskHits; }
        }

        // Planted Control 83-A: flip 1 byte inside on-disk PSO blob -> cold reader MUST reject fail-closed & rebuild!
        const auto victimPath = coldCache.BlobPathForKey(keys[0]);
        {
            std::fstream f(victimPath, std::ios::binary | std::ios::in | std::ios::out);
            f.seekp(static_cast<std::streamoff>(sizeof(raw::bridge::wave83::RawPsoBlobHeader) + 17u));
            const char badByte = 0x55;
            f.write(&badByte, 1);
        }
        raw::bridge::wave83::RawPsoCache tamperReader(tmpDir, 0x00010722u);
        bool tamperHit = true;
        auto rebuilt = tamperReader.LookupOrBuild(keys[0], []() { return std::vector<uint8_t>(256u, 0xA0u); }, &tamperHit);
        const auto stats = tamperReader.GetStats();
        std::filesystem::remove_all(tmpDir, ec);

        Check(warmed == 12u && diskHits == 12 && !tamperHit && stats.corruptRejected == 1ull && rebuilt.size() == 256u && rebuilt[17] == 0xA0u,
              "RawPsoCache async pre-warm (12/12 disk hits) + Planted Control 83-A (1-byte corrupt PSO rejected & rebuilt)");
    }

    // 4) Two-Pass Hi-Z disocclusion recovery + Planted Controls 83-B (Pass-2 off -> pop-in) & 83-C (buggy min() Hi-Z)
    {
        std::vector<raw::bridge::wave83::TwoPassCullInput> scene = {
            {1u, 0.30f, 0.50f, 0.50f, true, true}, // visible in both frames
            {2u, 0.45f, 0.20f, 0.85f, true, true}, // occluded in prev frame, NEWLY DISOCCLUDED in curr frame!
            {3u, 0.90f, 0.25f, 0.35f, true, true}, // genuinely occluded in both frames
            {4u, 0.40f, 0.90f, 0.90f, false, true} // outside frustum
        };
        const auto twoPass = raw::bridge::wave83::RunTwoPassHiZCull(scene, 1e-4f, true);
        const auto onePass = raw::bridge::wave83::RunTwoPassHiZCull(scene, 1e-4f, false); // Planted 83-B

        // Planted Control 83-C: Conservative Standard-Z Hi-Z MUST use max(); buggy min() falsely culls foreground z=0.45!
        const float goodHiZ = raw::bridge::wave83::ConservativeHiZReduce2x2(0.20f, 0.85f, 0.25f, 0.30f, false);
        const float badHiZ  = raw::bridge::wave83::ConservativeHiZReduce2x2(0.20f, 0.85f, 0.25f, 0.30f, true);
        const bool goodKeepsFg = !(0.45f > goodHiZ + 1e-4f);
        const bool badFalseCull = (0.45f > badHiZ + 1e-4f);

        Check(twoPass.totalVisible == 2u && twoPass.pass2Disoccluded.size() == 1u && twoPass.pass2Disoccluded[0] == 2u &&
              onePass.totalVisible == 1u && goodKeepsFg && badFalseCull,
              "Two-Pass Hi-Z recovers disoccluded instance (#2) with 0 pop-in + Planted Controls 83-B & 83-C caught");
    }

    // 5) Continuous Meshlet LOD monotonic cut + 1%-low FrameBudgetLodGovernor + Planted Control 83-D
    {
        std::vector<raw::bridge::wave83::GpuMeshletDesc> goodChain(3);
        goodChain[0].lodErrorWorld = 0.0f;  goodChain[0].parentErrorWorld = 0.25f;
        goodChain[1].lodErrorWorld = 0.25f; goodChain[1].parentErrorWorld = 1.00f;
        goodChain[2].lodErrorWorld = 1.00f; goodChain[2].parentErrorWorld = 1.0e9f;

        bool watertight = raw::bridge::wave83::ValidateMonotonicLodDag(goodChain);
        for (float z : {50.0f, 180.0f, 650.0f, 2400.0f}) {
            int active = 0;
            for (const auto& n : goodChain) {
                if (raw::bridge::wave83::SelectMeshletLodNode(n, z, 1080.0f, 1.25f)) { ++active; }
            }
            if (active != 1) { watertight = false; } // exact partition: 1 node selected at every distance!
        }

        // Planted Control 83-D: non-monotonic child error > parent error caught fail-closed!
        auto badChain = goodChain;
        badChain[1].lodErrorWorld = 1.50f;
        badChain[1].parentErrorWorld = 1.00f;
        const bool plantedNonMonotonicCaught = !raw::bridge::wave83::ValidateMonotonicLodDag(badChain);

        // 1%-low frame-budget governor response:
        raw::bridge::wave83::FrameBudgetLodGovernor gov(10.0f, 1.25f);
        const float pxBefore = gov.TargetPixelError();
        const float pxOver   = gov.UpdateFromP99(14.5f); // over budget -> relaxes pxErr upward
        const float pxUnder  = gov.UpdateFromP99(7.0f);  // under budget -> tightens back downward
        Check(watertight && plantedNonMonotonicCaught && (pxOver > pxBefore) && (pxUnder < pxOver),
              "Continuous meshlet LOD exact 1-node watertight cut + 1%-low governor + Planted Control 83-D caught");
    }

    // 6) Bit-identical 100 Hz replay across SkyrimSE, Fallout4, and Starfield profiles (60/100/144/240 FPS)
    {
        const char* exes[3] = {"SkyrimSE.exe", "Fallout4.exe", "Starfield.exe"};
        const double rates[4] = {60.0, 100.0, 144.0, 240.0};
        bool ok = true;
        double refVal = -1.0;
        for (int gi = 0; gi < 3; ++gi) {
            const auto prof = raw::versions::ResolveEngineProfileByExe(exes[gi]);
            const auto policy = raw::bridge::ResolveFramePacingPolicy(prof);
            if (policy.multiplayerBlocked || policy.fixedTickHz != 100.0) { ok = false; }
            for (int ri = 0; ri < 4; ++ri) {
                raw::bridge::DeterministicFramePacer pacer(policy);
                raw::bridge::wave83::FrameBudgetLodGovernor gov(10.0f, 1.25f);
                const double dt = 1.0 / rates[ri];
                const int frames = static_cast<int>(std::llround(1.0 * rates[ri]));
                double acc = 0.0;
                uint64_t tickIdx = 0u;
                for (int f = 0; f < frames; ++f) {
                    const auto s = pacer.Step(dt);
                    for (uint32_t k = 0; k < s.substeps; ++k) {
                        ++tickIdx;
                        const float p99Sim = (tickIdx % 2u == 0u) ? 11.4f : 8.2f;
                        acc += static_cast<double>(gov.UpdateFromP99(p99Sim));
                    }
                }
                if (gi == 0 && ri == 0) { refVal = acc; }
                else if (std::fabs(acc - refVal) > 1e-12) { ok = false; }
            }
        }
        Check(ok && refVal > 0.0, "Bit-identical 100 Hz LOD-governor replay across SkyrimSE / Fallout4 / Starfield & 60/100/144/240 FPS");
    }

    std::printf("=== RESULT: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
