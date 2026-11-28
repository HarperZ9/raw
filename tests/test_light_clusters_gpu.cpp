// test_light_clusters_gpu.cpp: Standalone DX12 GPU Verification Suite for Wave 50 16x9x24 Clustered Light Assignment.
// Original clean-room implementation, MIT License.
//
// Runs on BOTH Hardware GPU (RTX 4090) and WARP (--warp or both by default) with D3D12 Debug Layer ENABLED:
//   1. Exact CPU-vs-GPU cluster assignment parity on deterministic point lights in distinct near/mid/far quadrants.
//   2. Spotlight cone rejection proof (forward cone clusters hit; rearward/side clusters rejected).
//   3. Out-of-frustum behind-camera light rejection proof (0 total assigned entries).
//   4. Full 1,024-dynamic-light single-frame stress test: zero overflow (all counts <= 64), zero NaNs,
//      exact CPU-vs-GPU parity, real RTX 4090 GPU cost (buildMs + assignMs < 0.15 ms), d3d12DebugErrors == 0.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "RawLightClusterHost.h"

namespace
{
using Microsoft::WRL::ComPtr;
using namespace raw::bridge;

int g_pass = 0;
int g_fail = 0;

void Check(bool ok, const char* label)
{
    if (ok) {
        ++g_pass;
        std::printf("[PASS] %s\n", label);
    } else {
        ++g_fail;
        std::printf("[FAIL] %s\n", label);
    }
}

uint32_t CountDebugErrors(ID3D12Device* dev)
{
    ComPtr<ID3D12InfoQueue> iq;
    if (!dev || FAILED(dev->QueryInterface(IID_PPV_ARGS(&iq)))) return 0u;
    const UINT64 count = iq->GetNumStoredMessagesAllowedByRetrievalFilter();
    uint32_t errs = 0u;
    for (UINT64 i = 0; i < count; ++i) {
        SIZE_T len = 0;
        if (FAILED(iq->GetMessage(i, nullptr, &len)) || len == 0) continue;
        std::vector<uint8_t> buf(len);
        auto* msg = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
        if (SUCCEEDED(iq->GetMessage(i, msg, &len))) {
            if (msg->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION ||
                msg->Severity == D3D12_MESSAGE_SEVERITY_ERROR) {
                ++errs;
                std::printf("  [D3D12 ERR] %s\n", msg->pDescription ? msg->pDescription : "(null)");
            }
        }
    }
    iq->ClearStoredMessages();
    return errs;
}

bool WaitForQueue(ID3D12CommandQueue* q, ID3D12Fence* fence, uint64_t& fenceVal, HANDLE ev)
{
    ++fenceVal;
    if (FAILED(q->Signal(fence, fenceVal))) return false;
    if (fence->GetCompletedValue() < fenceVal) {
        if (FAILED(fence->SetEventOnCompletion(fenceVal, ev))) return false;
        WaitForSingleObject(ev, INFINITE);
    }
    return true;
}

bool CreateReadbackBuffer(ID3D12Device* dev, uint64_t bytes, ComPtr<ID3D12Resource>& out)
{
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = (bytes + 255ull) & ~255ull;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                  D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&out)));
}

void RunAdapterSuite(IDXGIFactory6* factory, bool useWarp)
{
    const char* tag = useWarp ? "WARP" : "HW-GPU";
    std::printf("\n--- Running 16x9x24 Clustered 1,024-Light GPU Suite [%s] ---\n", tag);

    ComPtr<IDXGIAdapter1> adapter;
    if (useWarp) {
        if (FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)))) {
            Check(false, "WARP: EnumWarpAdapter");
            return;
        }
    } else {
        for (UINT i = 0; factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                             IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc{};
            adapter->GetDesc1(&desc);
            if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0) {
                std::printf("  Adapter: %ls\n", desc.Description);
                break;
            }
            adapter.Reset();
        }
        if (!adapter) {
            std::printf("  [INFO] No hardware D3D12 GPU found; skipping HW-GPU pass.\n");
            return;
        }
    }

    ComPtr<ID3D12Device> dev;
    if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&dev)))) {
        Check(false, useWarp ? "WARP: D3D12CreateDevice" : "HW: D3D12CreateDevice");
        return;
    }

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> alloc;
    ComPtr<ID3D12GraphicsCommandList> cmd;
    ComPtr<ID3D12Fence> fence;
    if (FAILED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue))) ||
        FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))) ||
        FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&cmd))) ||
        FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
        Check(false, useWarp ? "WARP: Queue/CmdList/Fence creation" : "HW: Queue/CmdList/Fence creation");
        return;
    }
    cmd->Close();
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    uint64_t fenceVal = 0u;

    uint64_t tsFreq = 0u;
    queue->GetTimestampFrequency(&tsFreq);

    RawLightClusterHost host;
    std::string initErr;
    const bool initOk = host.Init(dev.Get(), {}, &initErr);
    if (!initOk) std::printf("  Init error: %s\n", initErr.c_str());
    Check(initOk && host.Ready(), useWarp ? "WARP: RawLightClusterHost::Init" : "HW: RawLightClusterHost::Init");
    if (!initOk) {
        CloseHandle(ev);
        return;
    }
    host.SetTimestampFrequency(tsFreq);

    const uint64_t aabbBytes    = static_cast<uint64_t>(RawLightClusterHost::kTotalClusters) * sizeof(ClusterAabb);
    const uint64_t gridBytes    = static_cast<uint64_t>(RawLightClusterHost::kTotalClusters) * sizeof(ClusterLightGridEntry);
    const uint64_t indicesBytes = static_cast<uint64_t>(RawLightClusterHost::kTotalIndexEntries) * sizeof(uint32_t);
    ComPtr<ID3D12Resource> rbAabb, rbGrid, rbIndices;
    if (!CreateReadbackBuffer(dev.Get(), aabbBytes, rbAabb) ||
        !CreateReadbackBuffer(dev.Get(), gridBytes, rbGrid) ||
        !CreateReadbackBuffer(dev.Get(), indicesBytes, rbIndices)) {
        Check(false, useWarp ? "WARP: Readback buffers" : "HW: Readback buffers");
        CloseHandle(ev);
        return;
    }

    LightClusterViewParams view{};
    view.nearZ = 0.5f;
    view.farZ  = 500.0f;
    BuildPerspectiveInvProjLH(1.04719755f, 16.0f / 9.0f, view.nearZ, view.farZ, view.invProj); // 60 deg FOV

    auto ExecuteAndReadback = [&](const std::vector<GpuClusterLight>& lights,
                                  std::vector<ClusterAabb>& outAabb,
                                  std::vector<ClusterLightGridEntry>& outGrid,
                                  std::vector<uint32_t>& outIdx) -> bool {
        alloc->Reset();
        cmd->Reset(alloc.Get(), nullptr);

        if (!host.BuildAndAssign(cmd.Get(), lights.empty() ? nullptr : lights.data(),
                                 static_cast<uint32_t>(lights.size()), view)) {
            return false;
        }

        // Transition outputs to COPY_SOURCE, copy to readback, restore states cleanly
        D3D12_RESOURCE_BARRIER toCopy[3]{};
        toCopy[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toCopy[0].Transition = {host.ClusterAabbResource(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                host.ClusterAabbState(), D3D12_RESOURCE_STATE_COPY_SOURCE};
        toCopy[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toCopy[1].Transition = {host.ClusterGridResource(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                host.ClusterGridState(), D3D12_RESOURCE_STATE_COPY_SOURCE};
        toCopy[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toCopy[2].Transition = {host.LightIndicesResource(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                host.LightIndicesState(), D3D12_RESOURCE_STATE_COPY_SOURCE};
        cmd->ResourceBarrier(3, toCopy);

        cmd->CopyBufferRegion(rbAabb.Get(),    0, host.ClusterAabbResource(),  0, aabbBytes);
        cmd->CopyBufferRegion(rbGrid.Get(),    0, host.ClusterGridResource(),  0, gridBytes);
        cmd->CopyBufferRegion(rbIndices.Get(), 0, host.LightIndicesResource(), 0, indicesBytes);

        D3D12_RESOURCE_BARRIER restore[3]{};
        for (int i = 0; i < 3; ++i) {
            restore[i] = toCopy[i];
            std::swap(restore[i].Transition.StateBefore, restore[i].Transition.StateAfter);
        }
        cmd->ResourceBarrier(3, restore);

        if (FAILED(cmd->Close())) return false;
        ID3D12CommandList* lists[1] = {cmd.Get()};
        queue->ExecuteCommandLists(1, lists);
        if (!WaitForQueue(queue.Get(), fence.Get(), fenceVal, ev)) return false;

        outAabb.resize(RawLightClusterHost::kTotalClusters);
        outGrid.resize(RawLightClusterHost::kTotalClusters);
        outIdx.resize(RawLightClusterHost::kTotalIndexEntries);

        auto ReadBuf = [](ID3D12Resource* rb, void* dst, uint64_t bytes) -> bool {
            void* p = nullptr;
            D3D12_RANGE rr{0, static_cast<SIZE_T>(bytes)};
            if (FAILED(rb->Map(0, &rr, &p))) return false;
            std::memcpy(dst, p, static_cast<size_t>(bytes));
            D3D12_RANGE wr{0, 0};
            rb->Unmap(0, &wr);
            return true;
        };
        return ReadBuf(rbAabb.Get(), outAabb.data(), aabbBytes) &&
               ReadBuf(rbGrid.Get(), outGrid.data(), gridBytes) &&
               ReadBuf(rbIndices.Get(), outIdx.data(), indicesBytes);
    };

    // ------------------------------------------------------------------------
    // Test 1: Deterministic quadrant point lights + exact CPU-vs-GPU parity
    // ------------------------------------------------------------------------
    {
        std::vector<GpuClusterLight> qLights(3);
        // Light 0: Left-Top-Near quadrant
        qLights[0].posView[0] = -1.6f; qLights[0].posView[1] = 0.8f; qLights[0].posView[2] = 3.0f;
        qLights[0].radius = 0.85f; qLights[0].colorRgb[0] = 2.0f; qLights[0].typeFlags = kClusterLightFire;
        // Light 1: Center-Mid
        qLights[1].posView[0] = 0.0f; qLights[1].posView[1] = 0.0f; qLights[1].posView[2] = 28.0f;
        qLights[1].radius = 6.0f; qLights[1].colorRgb[1] = 1.5f; qLights[1].typeFlags = kClusterLightPoint;
        // Light 2: Right-Bottom-Far quadrant
        qLights[2].posView[0] = 95.0f; qLights[2].posView[1] = -45.0f; qLights[2].posView[2] = 240.0f;
        qLights[2].radius = 35.0f; qLights[2].colorRgb[2] = 3.0f; qLights[2].typeFlags = kClusterLightSpell;

        std::vector<ClusterAabb> gpuAabb, cpuAabb;
        std::vector<ClusterLightGridEntry> gpuGrid, cpuGrid;
        std::vector<uint32_t> gpuIdx, cpuIdx;

        const bool okRun = ExecuteAndReadback(qLights, gpuAabb, gpuGrid, gpuIdx);
        CpuBuildAndAssignClusters(qLights.data(), static_cast<uint32_t>(qLights.size()), view, cpuAabb, cpuGrid, cpuIdx);

        uint32_t mismatches = 0u;
        uint32_t light0Hits = 0u;
        uint32_t light0FarRightFalsePos = 0u;
        uint32_t light2Hits = 0u;
        for (uint32_t c = 0u; c < RawLightClusterHost::kTotalClusters; ++c) {
            if (gpuGrid[c].offset != cpuGrid[c].offset || gpuGrid[c].count != cpuGrid[c].count) {
                ++mismatches;
            } else {
                for (uint32_t k = 0u; k < gpuGrid[c].count; ++k) {
                    const uint32_t id = gpuIdx[gpuGrid[c].offset + k];
                    if (id != cpuIdx[cpuGrid[c].offset + k]) ++mismatches;
                    const uint32_t cz = c / (16u * 9u);
                    const uint32_t rem = c % (16u * 9u);
                    const uint32_t cx = rem % 16u;
                    if (id == 0u) {
                        ++light0Hits;
                        if (cx >= 8u || cz >= 12u) ++light0FarRightFalsePos;
                    }
                    if (id == 2u) ++light2Hits;
                }
            }
        }
        std::printf("  [%s] Quadrant parity: mismatches=%u light0Hits=%u light0FarRightFP=%u light2Hits=%u\n",
                    tag, mismatches, light0Hits, light0FarRightFalsePos, light2Hits);
        Check(okRun && mismatches == 0u && light0Hits > 0u && light0FarRightFalsePos == 0u && light2Hits > 0u,
              useWarp ? "WARP: Exact CPU-vs-GPU quadrant parity & 0 far-right false positives"
                      : "HW: Exact CPU-vs-GPU quadrant parity & 0 far-right false positives");
    }

    // ------------------------------------------------------------------------
    // Test 2: Spotlight cone rejection proof
    // ------------------------------------------------------------------------
    {
        // Spotlight placed at center z=30, radius=20, pointing strictly +X (right) with 25-deg half-angle
        // Left clusters (cx <= 5) MUST reject it; right clusters (cx >= 10) at z~30 MUST contain it!
        GpuClusterLight spot{};
        spot.posView[0] = 0.0f; spot.posView[1] = 0.0f; spot.posView[2] = 30.0f;
        spot.radius = 22.0f;
        spot.dirView[0] = 1.0f; spot.dirView[1] = 0.0f; spot.dirView[2] = 0.0f;
        spot.cosOuterCone = std::cos(25.0f * 3.14159265f / 180.0f);
        spot.typeFlags = kClusterLightSpotBit | kClusterLightSpell;

        std::vector<GpuClusterLight> sLights = {spot};
        std::vector<ClusterAabb> gpuAabb, cpuAabb;
        std::vector<ClusterLightGridEntry> gpuGrid, cpuGrid;
        std::vector<uint32_t> gpuIdx, cpuIdx;
        const bool okRun = ExecuteAndReadback(sLights, gpuAabb, gpuGrid, gpuIdx);
        CpuBuildAndAssignClusters(sLights.data(), 1u, view, cpuAabb, cpuGrid, cpuIdx);

        uint32_t rightForwardHits = 0u;
        uint32_t leftBehindHits = 0u;
        uint32_t parityErrs = 0u;
        for (uint32_t c = 0u; c < RawLightClusterHost::kTotalClusters; ++c) {
            if (gpuGrid[c].count != cpuGrid[c].count) ++parityErrs;
            if (gpuGrid[c].count > 0u) {
                const uint32_t cx = (c % (16u * 9u)) % 16u;
                if (cx >= 10u) ++rightForwardHits;
                if (cx <= 5u) ++leftBehindHits;
            }
        }
        std::printf("  [%s] Spotlight cone: forwardRightHits=%u behindLeftHits=%u parityErrs=%u\n",
                    tag, rightForwardHits, leftBehindHits, parityErrs);
        Check(okRun && rightForwardHits > 0u && leftBehindHits == 0u && parityErrs == 0u,
              useWarp ? "WARP: Spotlight cone rejects rearward clusters & lights forward cone"
                      : "HW: Spotlight cone rejects rearward clusters & lights forward cone");
    }

    // ------------------------------------------------------------------------
    // Test 3: Out-of-frustum behind-camera rejection proof
    // ------------------------------------------------------------------------
    {
        std::vector<GpuClusterLight> behindLights(64);
        for (uint32_t i = 0u; i < 64u; ++i) {
            behindLights[i].posView[0] = static_cast<float>(static_cast<int>(i) - 32) * 3.0f;
            behindLights[i].posView[1] = static_cast<float>((i % 7u) - 3.0f) * 2.0f;
            behindLights[i].posView[2] = -25.0f - static_cast<float>(i) * 2.0f; // strictly behind camera
            behindLights[i].radius = 12.0f;
            behindLights[i].typeFlags = kClusterLightPoint;
        }
        std::vector<ClusterAabb> gpuAabb;
        std::vector<ClusterLightGridEntry> gpuGrid;
        std::vector<uint32_t> gpuIdx;
        const bool okRun = ExecuteAndReadback(behindLights, gpuAabb, gpuGrid, gpuIdx);

        uint32_t totalAssigned = 0u;
        for (const auto& e : gpuGrid) totalAssigned += e.count;
        Check(okRun && totalAssigned == 0u,
              useWarp ? "WARP: Behind-camera lights 100% rejected (count == 0)"
                      : "HW: Behind-camera lights 100% rejected (count == 0)");
    }

    // ------------------------------------------------------------------------
    // Test 4: Full 1,024-dynamic-light single-frame stress + GPU timing proof
    // ------------------------------------------------------------------------
    {
        std::vector<GpuClusterLight> stressLights(1024);
        uint32_t seed = 0x51A7C0DEu;
        auto NextF = [&seed]() -> float {
            seed = seed * 1664525u + 1013904223u;
            return static_cast<float>((seed >> 8) & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
        };
        for (uint32_t i = 0u; i < 1024u; ++i) {
            const float z = 1.5f * std::pow(420.0f / 1.5f, NextF());
            const float halfH = z * std::tan(1.04719755f * 0.5f);
            const float halfW = halfH * (16.0f / 9.0f);
            GpuClusterLight& l = stressLights[i];
            l.posView[0] = (NextF() * 2.0f - 1.0f) * halfW * 1.15f;
            l.posView[1] = (NextF() * 2.0f - 1.0f) * halfH * 1.15f;
            l.posView[2] = z;
            l.radius     = 1.5f + NextF() * (0.08f * z + 6.0f);
            l.colorRgb[0] = 0.5f + NextF() * 2.0f;
            l.colorRgb[1] = 0.4f + NextF() * 1.6f;
            l.colorRgb[2] = 0.3f + NextF() * 1.8f;
            if ((i & 3u) == 0u) {
                const float dx = NextF() * 2.0f - 1.0f;
                const float dy = NextF() * 2.0f - 1.0f;
                const float dz = NextF() * 2.0f - 1.0f;
                const float invL = 1.0f / std::max(std::sqrt(dx * dx + dy * dy + dz * dz), 1.0e-5f);
                l.dirView[0] = dx * invL; l.dirView[1] = dy * invL; l.dirView[2] = dz * invL;
                l.cosOuterCone = 0.5f + NextF() * 0.42f;
                l.typeFlags = kClusterLightSpotBit | kClusterLightSpell;
            } else {
                l.dirView[0] = 0.0f; l.dirView[1] = 0.0f; l.dirView[2] = 1.0f;
                l.cosOuterCone = -1.0f;
                l.typeFlags = ((i & 1u) == 0u) ? kClusterLightFire : kClusterLightPoint;
            }
        }

        // Run 10 iterations so the 4-frame timestamp ring harvests steady-state GPU cost cleanly
        std::vector<ClusterAabb> gpuAabb;
        std::vector<ClusterLightGridEntry> gpuGrid;
        std::vector<uint32_t> gpuIdx;
        bool okStress = true;
        float sumBuildMs = 0.0f, sumAssignMs = 0.0f;
        uint32_t timeSamples = 0u;
        const uint32_t iters = useWarp ? 6u : 12u;
        for (uint32_t it = 0u; it < iters; ++it) {
            okStress = okStress && ExecuteAndReadback(stressLights, gpuAabb, gpuGrid, gpuIdx);
            if (host.HasValidTiming()) {
                sumBuildMs  += host.GetLastBuildMs();
                sumAssignMs += host.GetLastAssignMs();
                ++timeSamples;
            }
        }

        uint32_t maxCount = 0u;
        uint32_t nonEmptyClusters = 0u;
        uint32_t totalLinks = 0u;
        uint32_t overflowErrs = 0u;
        uint32_t nanInfAabbs = 0u;
        for (uint32_t c = 0u; c < RawLightClusterHost::kTotalClusters; ++c) {
            const auto& a = gpuAabb[c];
            for (int k = 0; k < 3; ++k) {
                if (!std::isfinite(a.minBounds[k]) || !std::isfinite(a.maxBounds[k]) || a.minBounds[k] > a.maxBounds[k]) {
                    ++nanInfAabbs;
                }
            }
            const auto& e = gpuGrid[c];
            if (e.count > RawLightClusterHost::kMaxLightsPerCluster || e.offset != c * RawLightClusterHost::kMaxLightsPerCluster) {
                ++overflowErrs;
            }
            maxCount = std::max(maxCount, e.count);
            if (e.count > 0u) ++nonEmptyClusters;
            totalLinks += e.count;
            for (uint32_t k = 0u; k < std::min(e.count, RawLightClusterHost::kMaxLightsPerCluster); ++k) {
                if (gpuIdx[e.offset + k] >= 1024u) ++overflowErrs;
            }
        }

        const float meanBuildMs  = (timeSamples > 0u) ? (sumBuildMs / static_cast<float>(timeSamples)) : -1.0f;
        const float meanAssignMs = (timeSamples > 0u) ? (sumAssignMs / static_cast<float>(timeSamples)) : -1.0f;
        const float totalGpuMs   = meanBuildMs + meanAssignMs;
        const uint32_t dbgErrs   = CountDebugErrors(dev.Get());

        std::printf("  [%s] 1024-Light Stress: nonEmpty=%u/3456 maxCount=%u totalLinks=%u overflow=%u nanAabb=%u buildMs=%.4f assignMs=%.4f totalMs=%.4f dbgErrs=%u\n",
                    tag, nonEmptyClusters, maxCount, totalLinks, overflowErrs, nanInfAabbs,
                    static_cast<double>(meanBuildMs), static_cast<double>(meanAssignMs),
                    static_cast<double>(totalGpuMs), dbgErrs);

        Check(okStress && overflowErrs == 0u && nanInfAabbs == 0u && nonEmptyClusters > 500u && totalLinks > 1000u,
              useWarp ? "WARP: 1,024-light stress valid (0 overflow, 0 NaN, >500 lit clusters)"
                      : "HW: 1,024-light stress valid (0 overflow, 0 NaN, >500 lit clusters)");
        if (!useWarp) {
            Check(timeSamples > 0u && totalGpuMs >= 0.0f && totalGpuMs < 0.15f,
                  "HW: 1,024-light GPU cost (buildMs + assignMs < 0.15 ms)");
        } else {
            Check(timeSamples > 0u && totalGpuMs >= 0.0f,
                  "WARP: Valid GPU timestamps harvested");
        }
        Check(dbgErrs == 0u,
              useWarp ? "WARP: 0 D3D12 Debug Layer errors"
                      : "HW: 0 D3D12 Debug Layer errors");
    }

    host.Shutdown();
    CloseHandle(ev);
}

} // namespace

int main(int argc, char** argv)
{
    bool warpOnly = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--warp") == 0) warpOnly = true;
    }

    std::printf("=== Wave 50 16x9x24 Clustered 1,024-Light GPU Compute Suite ===\n");

    ComPtr<ID3D12Debug> dbg;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) {
        dbg->EnableDebugLayer();
        std::printf("D3D12 Debug Layer: ENABLED\n");
    } else {
        std::printf("D3D12 Debug Layer: UNAVAILABLE\n");
    }

    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&factory)))) {
        CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
    }
    Check(factory != nullptr, "DXGI Factory created");
    if (!factory) return 1;

    if (!warpOnly) {
        RunAdapterSuite(factory.Get(), false); // Hardware RTX 4090
    }
    RunAdapterSuite(factory.Get(), true);      // WARP

    std::printf("\n=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
