// tests/test_gpu_cull.cpp: Verification & GPU Benchmark Suite for RAW's Conservative Hi-Z Pyramid
// & 65,536-Instance Frustum/Occlusion/Distance Culler (RawGpuCull.hlsl + RawGpuCullHost).
//
// Verifies on BOTH Hardware DX12 GPU (e.g. RTX 4090) AND Software WARP (--warp) with D3D12 Debug Layer ON:
//   1. Frustum-outside instances -> 100% culled.
//   2. Distance-LOD exceeded instances -> 100% culled.
//   3. Instances completely behind depth occluder wall (z = 0.25, minObjZ > 0.35) -> 100% Hi-Z occluded.
//   4. Instances in front of wall (z < 0.20), peeking past wall edge, or straddling near plane -> 0% false occlusion (100% survive).
//   5. Exact IndirectArgsBuffer (DrawIndexed.InstanceCount + DispatchMesh.ThreadGroupCountX) & unique surviving IDs.
//   6. Odd-dimension (321x181) 1-pixel right-edge gap conservative Hi-Z coverage -> 0% false occlusion.
//   7. Full 65,536-instance single-dispatch stress test.
//   8. Hardware RTX 4090 timing budget (hizMs + cullMs < 0.15 ms for 16,384 instances) & 0 D3D12 Debug Layer errors.
//
// Original clean-room implementation, MIT License.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include "RawGpuCullHost.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
using Microsoft::WRL::ComPtr;
using namespace raw::bridge;

int gPassed = 0;
int gFailed = 0;

void Check(bool cond, const char* msg)
{
    if (cond) {
        ++gPassed;
        std::printf("  [PASS] %s\n", msg);
    } else {
        ++gFailed;
        std::printf("  [FAIL] %s\n", msg);
    }
}

std::string WideToUtf8(const wchar_t* ws)
{
    if (!ws || !ws[0]) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, ws, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string out(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws, -1, out.data(), n, nullptr, nullptr);
    return out;
}

struct TestDx12 {
    ComPtr<ID3D12Device>              dev;
    ComPtr<ID3D12InfoQueue>           infoQueue;
    ComPtr<ID3D12CommandQueue>        queue;
    ComPtr<ID3D12CommandAllocator>    alloc;
    ComPtr<ID3D12GraphicsCommandList> cmd;
    ComPtr<ID3D12Fence>               fence;
    ComPtr<ID3D12QueryHeap>           tsHeap;
    ComPtr<ID3D12Resource>            tsReadback;
    HANDLE                            fenceEvent  = nullptr;
    uint64_t                          fenceVal    = 0ull;
    uint64_t                          tsFreqHz    = 0ull;
    std::string                       adapterName = "Unknown";
    bool                              isWarp      = false;
    bool                              debugLayer  = false;

    static constexpr uint32_t kMaxQueries = 8u;

    ~TestDx12()
    {
        if (fenceEvent) {
            CloseHandle(fenceEvent);
            fenceEvent = nullptr;
        }
    }

    bool Init(bool useWarp)
    {
        ComPtr<ID3D12Debug> dbg;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) {
            dbg->EnableDebugLayer();
            debugLayer = true;
        }

        ComPtr<IDXGIFactory4> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return false;

        if (!useWarp) {
            ComPtr<IDXGIAdapter1> best;
            DXGI_ADAPTER_DESC1 bestDesc{};
            for (UINT i = 0u; ; ++i) {
                ComPtr<IDXGIAdapter1> cand;
                if (factory->EnumAdapters1(i, &cand) == DXGI_ERROR_NOT_FOUND) break;
                DXGI_ADAPTER_DESC1 desc{};
                cand->GetDesc1(&desc);
                if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0u) continue;
                ComPtr<ID3D12Device> testDev;
                if (SUCCEEDED(D3D12CreateDevice(cand.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&testDev)))) {
                    D3D12_FEATURE_DATA_SHADER_MODEL sm{D3D_SHADER_MODEL_6_6};
                    if (SUCCEEDED(testDev->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm))) &&
                        sm.HighestShaderModel >= D3D_SHADER_MODEL_6_6) {
                        if (!best || desc.DedicatedVideoMemory > bestDesc.DedicatedVideoMemory) {
                            best     = cand;
                            bestDesc = desc;
                            dev      = testDev;
                        }
                    }
                }
            }
            if (!dev) return false;
            adapterName = WideToUtf8(bestDesc.Description);
            isWarp      = false;
        } else {
            ComPtr<IDXGIAdapter> warp;
            if (FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))) ||
                FAILED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&dev)))) {
                return false;
            }
            adapterName = "Microsoft Basic Render Driver (WARP)";
            isWarp      = true;
        }

        if (debugLayer) dev.As(&infoQueue);

        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue))) ||
            FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))) ||
            FAILED(dev->CreateCommandList(0u, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&cmd))) ||
            FAILED(dev->CreateFence(0ull, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
            return false;
        }
        queue->GetTimestampFrequency(&tsFreqHz);
        cmd->Close();

        D3D12_QUERY_HEAP_DESC qhd{};
        qhd.Type  = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        qhd.Count = kMaxQueries;
        if (FAILED(dev->CreateQueryHeap(&qhd, IID_PPV_ARGS(&tsHeap)))) return false;

        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rbd{};
        rbd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        rbd.Width            = static_cast<UINT64>(kMaxQueries) * sizeof(uint64_t);
        rbd.Height           = 1u;
        rbd.DepthOrArraySize = 1u;
        rbd.MipLevels        = 1u;
        rbd.SampleDesc.Count = 1u;
        rbd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rbd,
                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tsReadback)))) {
            return false;
        }

        fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        return fenceEvent != nullptr;
    }

    bool Begin()
    {
        return SUCCEEDED(alloc->Reset()) && SUCCEEDED(cmd->Reset(alloc.Get(), nullptr));
    }

    bool FlushAndWait()
    {
        if (FAILED(cmd->Close())) return false;
        ID3D12CommandList* lists[1] = {cmd.Get()};
        queue->ExecuteCommandLists(1u, lists);
        ++fenceVal;
        if (FAILED(queue->Signal(fence.Get(), fenceVal))) return false;
        if (fence->GetCompletedValue() < fenceVal) {
            if (FAILED(fence->SetEventOnCompletion(fenceVal, fenceEvent))) return false;
            WaitForSingleObject(fenceEvent, 30000);
        }
        return fence->GetCompletedValue() >= fenceVal;
    }

    double ReadTsRangeMs(uint32_t a, uint32_t b)
    {
        if (tsFreqHz == 0ull) return 0.0;
        void* mapped = nullptr;
        D3D12_RANGE rr{0u, static_cast<SIZE_T>(kMaxQueries) * sizeof(uint64_t)};
        if (FAILED(tsReadback->Map(0u, &rr, &mapped)) || !mapped) return 0.0;
        const auto* ticks = static_cast<const uint64_t*>(mapped);
        const uint64_t t0 = ticks[a];
        const uint64_t t1 = ticks[b];
        D3D12_RANGE wr{0u, 0u};
        tsReadback->Unmap(0u, &wr);
        return (t1 >= t0) ? (static_cast<double>(t1 - t0) * 1000.0 / static_cast<double>(tsFreqHz)) : 0.0;
    }

    uint32_t DrainDebugErrors()
    {
        if (!infoQueue) return 0u;
        uint32_t errs = 0u;
        const UINT64 n = infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
        for (UINT64 i = 0u; i < n; ++i) {
            SIZE_T len = 0u;
            infoQueue->GetMessage(i, nullptr, &len);
            if (len == 0u) continue;
            std::vector<uint8_t> buf(len);
            auto* m = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
            if (SUCCEEDED(infoQueue->GetMessage(i, m, &len))) {
                if (m->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION ||
                    m->Severity == D3D12_MESSAGE_SEVERITY_ERROR) {
                    std::fprintf(stderr, "  [D3D12 ERROR] %s\n", m->pDescription ? m->pDescription : "(null)");
                    ++errs;
                }
            }
        }
        infoQueue->ClearStoredMessages();
        return errs;
    }

    bool UploadR32Tex2D(ComPtr<ID3D12Resource>& tex, uint32_t w, uint32_t h, const std::vector<float>& depthData)
    {
        D3D12_HEAP_PROPERTIES hpDef{};
        hpDef.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width            = w;
        rd.Height           = h;
        rd.DepthOrArraySize = 1u;
        rd.MipLevels        = 1u;
        rd.Format           = DXGI_FORMAT_R32_FLOAT;
        rd.SampleDesc.Count = 1u;

        if (FAILED(dev->CreateCommittedResource(&hpDef, D3D12_HEAP_FLAG_NONE, &rd,
                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex)))) {
            return false;
        }

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        UINT numRows = 0u;
        UINT64 rowBytes = 0ull, totalBytes = 0ull;
        dev->GetCopyableFootprints(&rd, 0u, 1u, 0ull, &fp, &numRows, &rowBytes, &totalBytes);

        D3D12_HEAP_PROPERTIES hpUp{};
        hpUp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC ubd{};
        ubd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        ubd.Width            = totalBytes;
        ubd.Height           = 1u;
        ubd.DepthOrArraySize = 1u;
        ubd.MipLevels        = 1u;
        ubd.SampleDesc.Count = 1u;
        ubd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ComPtr<ID3D12Resource> up;
        if (FAILED(dev->CreateCommittedResource(&hpUp, D3D12_HEAP_FLAG_NONE, &ubd,
                                                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&up)))) {
            return false;
        }
        void* mapped = nullptr;
        D3D12_RANGE rr{0u, 0u};
        if (FAILED(up->Map(0u, &rr, &mapped)) || !mapped) return false;
        auto* base = static_cast<uint8_t*>(mapped) + fp.Offset;
        for (uint32_t y = 0u; y < h; ++y) {
            std::memcpy(base + static_cast<size_t>(y) * fp.Footprint.RowPitch,
                        depthData.data() + static_cast<size_t>(y) * w,
                        static_cast<size_t>(w) * sizeof(float));
        }
        up->Unmap(0u, nullptr);

        if (!Begin()) return false;
        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource        = tex.Get();
        dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0u;

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource       = up.Get();
        src.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = fp;
        cmd->CopyTextureRegion(&dst, 0u, 0u, 0u, &src, nullptr);

        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = tex.Get();
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1u, &b);
        return FlushAndWait();
    }

    bool UploadBuffer(ComPtr<ID3D12Resource>& dstBuf, const void* data, uint64_t byteSize)
    {
        const uint64_t aligned = (byteSize + 255ull) & ~255ull;
        D3D12_HEAP_PROPERTIES hpDef{};
        hpDef.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width            = aligned;
        rd.Height           = 1u;
        rd.DepthOrArraySize = 1u;
        rd.MipLevels        = 1u;
        rd.SampleDesc.Count = 1u;
        rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(dev->CreateCommittedResource(&hpDef, D3D12_HEAP_FLAG_NONE, &rd,
                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&dstBuf)))) {
            return false;
        }

        D3D12_HEAP_PROPERTIES hpUp{};
        hpUp.Type = D3D12_HEAP_TYPE_UPLOAD;
        ComPtr<ID3D12Resource> up;
        if (FAILED(dev->CreateCommittedResource(&hpUp, D3D12_HEAP_FLAG_NONE, &rd,
                                                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&up)))) {
            return false;
        }
        void* mapped = nullptr;
        D3D12_RANGE rr{0u, 0u};
        if (FAILED(up->Map(0u, &rr, &mapped)) || !mapped) return false;
        std::memcpy(mapped, data, static_cast<size_t>(byteSize));
        up->Unmap(0u, nullptr);

        if (!Begin()) return false;
        cmd->CopyBufferRegion(dstBuf.Get(), 0ull, up.Get(), 0ull, byteSize);
        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = dstBuf.Get();
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1u, &b);
        return FlushAndWait();
    }

    bool ReadbackBuffer(ID3D12Resource* buf, D3D12_RESOURCE_STATES curState, uint64_t byteSize, std::vector<uint8_t>& out)
    {
        D3D12_HEAP_PROPERTIES hpRb{};
        hpRb.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width            = (byteSize + 255ull) & ~255ull;
        rd.Height           = 1u;
        rd.DepthOrArraySize = 1u;
        rd.MipLevels        = 1u;
        rd.SampleDesc.Count = 1u;
        rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ComPtr<ID3D12Resource> rb;
        if (FAILED(dev->CreateCommittedResource(&hpRb, D3D12_HEAP_FLAG_NONE, &rd,
                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&rb)))) {
            return false;
        }
        if (!Begin()) return false;
        if (curState != D3D12_RESOURCE_STATE_COPY_SOURCE) {
            D3D12_RESOURCE_BARRIER b{};
            b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource   = buf;
            b.Transition.StateBefore = curState;
            b.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmd->ResourceBarrier(1u, &b);
        }
        cmd->CopyBufferRegion(rb.Get(), 0ull, buf, 0ull, byteSize);
        if (curState != D3D12_RESOURCE_STATE_COPY_SOURCE) {
            D3D12_RESOURCE_BARRIER b{};
            b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource   = buf;
            b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
            b.Transition.StateAfter  = curState;
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmd->ResourceBarrier(1u, &b);
        }
        if (!FlushAndWait()) return false;

        void* mapped = nullptr;
        D3D12_RANGE rr{0u, static_cast<SIZE_T>(byteSize)};
        if (FAILED(rb->Map(0u, &rr, &mapped)) || !mapped) return false;
        out.resize(static_cast<size_t>(byteSize));
        std::memcpy(out.data(), mapped, static_cast<size_t>(byteSize));
        D3D12_RANGE wr{0u, 0u};
        rb->Unmap(0u, &wr);
        return true;
    }
};

// Build left-handed DX12 perspective ViewProj (camera at origin looking +Z, fovY=90 deg, aspect=1, z in [0, 1]).
void MakeTestPerspectiveLH(float nearZ, float farZ, float outM[16])
{
    std::memset(outM, 0, sizeof(float) * 16u);
    const float q = farZ / (farZ - nearZ);
    outM[0 * 4 + 0] = 1.0f;
    outM[1 * 4 + 1] = 1.0f;
    outM[2 * 4 + 2] = q;
    outM[2 * 4 + 3] = 1.0f;
    outM[3 * 4 + 2] = -nearZ * q;
}

GpuCullInstance MakeAabbInst(float minX, float minY, float minZ,
                             float maxX, float maxY, float maxZ,
                             float maxDrawDist, uint32_t id, uint32_t meshletCount)
{
    GpuCullInstance inst{};
    inst.aabbMin[0]    = minX;
    inst.aabbMin[1]    = minY;
    inst.aabbMin[2]    = minZ;
    inst.aabbMax[0]    = maxX;
    inst.aabbMax[1]    = maxY;
    inst.aabbMax[2]    = maxZ;
    inst.center[0]     = 0.5f * (minX + maxX);
    inst.center[1]     = 0.5f * (minY + maxY);
    inst.center[2]     = 0.5f * (minZ + maxZ);
    const float hx     = 0.5f * (maxX - minX);
    const float hy     = 0.5f * (maxY - minY);
    const float hz     = 0.5f * (maxZ - minZ);
    inst.radius        = std::sqrt(hx * hx + hy * hy + hz * hz);
    inst.maxDrawDist   = maxDrawDist;
    inst.meshletOffset = id * 8u;
    inst.meshletCount  = meshletCount;
    inst.instanceId    = id;
    inst.lodBiasFlags  = 0u;
    return inst;
}

void RunSuiteOnAdapter(bool useWarp)
{
    TestDx12 dx{};
    if (!dx.Init(useWarp)) {
        Check(false, useWarp ? "WARP D3D12 device init" : "Hardware DX12 device init");
        return;
    }

    std::printf("\n=== GPU Hi-Z & 65k-Instance Cull Suite [%s] (DebugLayer=%s, TsFreq=%llu Hz) ===\n",
                dx.adapterName.c_str(), dx.debugLayer ? "ON" : "OFF",
                static_cast<unsigned long long>(dx.tsFreqHz));

    RawGpuCullHost host;
    GpuCullDesc desc{};
    desc.maxInstances     = 65536u;
    desc.initialHiZWidth  = 512u;
    desc.initialHiZHeight = 512u;
    std::string err;
    const bool initOk = host.Init(dx.dev.Get(), desc, &err);
    Check(initOk, "RawGpuCullHost::Init succeeded");
    if (!initOk) {
        std::fprintf(stderr, "  Init error: %s\n", err.c_str());
        return;
    }
    host.SetTimestampFrequency(dx.tsFreqHz);

    // ------------------------------------------------------------------------
    // 1. Deterministic Synthetic Scene: 512x512 Depth Wall (z=0.25) + 16,384 Instances
    // ------------------------------------------------------------------------
    constexpr uint32_t kDepthW = 512u;
    constexpr uint32_t kDepthH = 512u;
    std::vector<float> depthImg(static_cast<size_t>(kDepthW) * kDepthH, 1.0f);
    // Giant occluder wall covering UV [0.15, 0.85] x [0.15, 0.85] (NDC [-0.70, +0.70]) at z = 0.25
    const uint32_t wallMinPx = static_cast<uint32_t>(0.15f * kDepthW); // 76
    const uint32_t wallMaxPx = static_cast<uint32_t>(0.85f * kDepthW); // 435
    for (uint32_t y = wallMinPx; y <= wallMaxPx; ++y) {
        for (uint32_t x = wallMinPx; x <= wallMaxPx; ++x) {
            depthImg[static_cast<size_t>(y) * kDepthW + x] = 0.25f;
        }
    }

    ComPtr<ID3D12Resource> depthTex;
    Check(dx.UploadR32Tex2D(depthTex, kDepthW, kDepthH, depthImg), "Uploaded 512x512 depth buffer with z=0.25 occluder wall");

    constexpr uint32_t kFrustumOutCount = 4096u;
    constexpr uint32_t kDistCullCount   = 1024u;
    constexpr uint32_t kBehindWallCount = 6144u;
    constexpr uint32_t kInFrontCount    = 2560u;
    constexpr uint32_t kPeekingCount    = 1536u;
    constexpr uint32_t kNearStraddleCnt = 1024u;
    constexpr uint32_t kTotalInstances  = kFrustumOutCount + kDistCullCount + kBehindWallCount +
                                          kInFrontCount + kPeekingCount + kNearStraddleCnt;
    static_assert(kTotalInstances == 16384u, "Exact 16,384 instance test mix");
    constexpr uint32_t kExpectedVisible = kInFrontCount + kPeekingCount + kNearStraddleCnt; // 5,120

    std::vector<GpuCullInstance> instances;
    instances.reserve(kTotalInstances);
    std::vector<uint8_t> expectedVis(kTotalInstances, 0u);
    uint32_t expectedMeshlets = 0u;

    auto addInst = [&](const GpuCullInstance& inst, bool shouldSurvive) {
        const uint32_t idx = static_cast<uint32_t>(instances.size());
        expectedVis[idx] = shouldSurvive ? 1u : 0u;
        if (shouldSurvive) expectedMeshlets += inst.meshletCount;
        instances.push_back(inst);
    };

    // Bucket A: Frustum-outside (behind camera, left, right, top, bottom, beyond far)
    for (uint32_t i = 0u; i < kFrustumOutCount; ++i) {
        const uint32_t id = static_cast<uint32_t>(instances.size());
        const uint32_t m  = (id % 7u) + 1u;
        const float    t  = static_cast<float>(i % 64u) / 64.0f;
        const uint32_t side = i % 6u;
        float cx = 0.0f, cy = 0.0f, cz = 10.0f;
        if (side == 0u)      { cz = -10.0f - t * 20.0f; } // behind camera
        else if (side == 1u) { cx = -25.0f - t * 10.0f; cz = 10.0f; } // outside left
        else if (side == 2u) { cx =  25.0f + t * 10.0f; cz = 10.0f; } // outside right
        else if (side == 3u) { cy =  25.0f + t * 10.0f; cz = 10.0f; } // outside top
        else if (side == 4u) { cy = -25.0f - t * 10.0f; cz = 10.0f; } // outside bottom
        else                 { cz = 1150.0f + t * 200.0f; }           // beyond far plane (1000)
        addInst(MakeAabbInst(cx - 0.5f, cy - 0.5f, cz - 0.5f, cx + 0.5f, cy + 0.5f, cz + 0.5f, 5000.0f, id, m), false);
    }

    // Bucket B: Inside frustum (open sky corner outside wall) but beyond maxDrawDist
    for (uint32_t i = 0u; i < kDistCullCount; ++i) {
        const uint32_t id = static_cast<uint32_t>(instances.size());
        const uint32_t m  = (id % 7u) + 1u;
        const float    cz = 50.0f + static_cast<float>(i % 32u);
        const float    cx = 0.85f * cz; // outside wall (|x/z| > 0.70) but inside frustum (|x/z| < 1.0)
        const float    cy = 0.85f * cz;
        addInst(MakeAabbInst(cx - 0.5f, cy - 0.5f, cz - 0.5f, cx + 0.5f, cy + 0.5f, cz + 0.5f, 15.0f, id, m), false);
    }

    // Bucket C: Completely behind occluder wall (minObjZ > 0.55 > 0.35 > 0.25, projected strictly inside [-0.55, +0.55] NDC)
    for (uint32_t i = 0u; i < kBehindWallCount; ++i) {
        const uint32_t id = static_cast<uint32_t>(instances.size());
        const uint32_t m  = (id % 7u) + 1u;
        const float    u  = (static_cast<float>(i % 64u) / 63.0f) * 2.0f - 1.0f;       // [-1, 1]
        const float    v  = (static_cast<float>((i / 64u) % 64u) / 63.0f) * 2.0f - 1.0f; // [-1, 1]
        const float    cz = 3.0f + static_cast<float>((i / 4096u) % 8u) * 4.0f + (static_cast<float>(i % 13u) * 0.2f);
        const float    cx = u * 0.50f * cz;
        const float    cy = v * 0.50f * cz;
        const float    h  = 0.04f * cz; // stays strictly inside [-0.55, +0.55] NDC
        addInst(MakeAabbInst(cx - h, cy - h, cz - 0.1f, cx + h, cy + h, cz + 0.1f, 5000.0f, id, m), false);
    }

    // Bucket D: Strictly in front of wall (z in [1.08, 1.22] -> ndcZ in [0.07, 0.19] < 0.20 < 0.25)
    for (uint32_t i = 0u; i < kInFrontCount; ++i) {
        const uint32_t id = static_cast<uint32_t>(instances.size());
        const uint32_t m  = (id % 7u) + 1u;
        const float    u  = (static_cast<float>(i % 64u) / 63.0f) * 2.0f - 1.0f;
        const float    v  = (static_cast<float>((i / 64u) % 40u) / 39.0f) * 2.0f - 1.0f;
        const float    cz = 1.10f + static_cast<float>(i % 10u) * 0.01f; // [1.10, 1.19]
        const float    cx = u * 0.50f * cz;
        const float    cy = v * 0.50f * cz;
        addInst(MakeAabbInst(cx - 0.02f, cy - 0.02f, cz - 0.02f, cx + 0.02f, cy + 0.02f, cz + 0.02f, 5000.0f, id, m), true);
    }

    // Bucket E: Peeking past wall edge (behind wall in Z, but AABB extends past |x/z| = 0.70 into open z=1.0 border!)
    for (uint32_t i = 0u; i < kPeekingCount; ++i) {
        const uint32_t id = static_cast<uint32_t>(instances.size());
        const uint32_t m  = (id % 7u) + 1u;
        const float    cz = 4.0f + static_cast<float>(i % 32u) * 0.5f;
        const float    cy = ((static_cast<float>(i % 48u) / 47.0f) * 2.0f - 1.0f) * 0.60f * cz;
        const float    signX = ((i & 1u) != 0u) ? 1.0f : -1.0f;
        const float    cx = signX * 0.72f * cz;
        const float    hx = 0.12f * cz; // spans [0.60, 0.84] NDC -> overlaps wall AND open background (z=1.0)
        addInst(MakeAabbInst(cx - hx, cy - 0.1f, cz - 0.1f, cx + hx, cy + 0.1f, cz + 0.1f, 5000.0f, id, m), true);
    }

    // Bucket F: Straddling the near plane (z in [0.7, 1.3], front corners have clip.z < 0 -> MUST NOT be Hi-Z culled!)
    for (uint32_t i = 0u; i < kNearStraddleCnt; ++i) {
        const uint32_t id = static_cast<uint32_t>(instances.size());
        const uint32_t m  = (id % 7u) + 1u;
        const float    u  = (static_cast<float>(i % 32u) / 31.0f) * 2.0f - 1.0f;
        const float    v  = (static_cast<float>((i / 32u) % 32u) / 31.0f) * 2.0f - 1.0f;
        const float    cx = u * 0.45f;
        const float    cy = v * 0.45f;
        addInst(MakeAabbInst(cx - 0.05f, cy - 0.05f, 0.70f, cx + 0.05f, cy + 0.05f, 1.30f, 5000.0f, id, m), true);
    }

    ComPtr<ID3D12Resource> instBuf;
    Check(dx.UploadBuffer(instBuf, instances.data(), instances.size() * sizeof(GpuCullInstance)),
          "Uploaded 16,384 deterministic GpuCullInstance bounds");

    GpuCullViewParams vp{};
    MakeTestPerspectiveLH(1.0f, 1000.0f, vp.viewProj);
    vp.autoExtractPlanes        = true;
    vp.cameraPosW[0]            = 0.0f;
    vp.cameraPosW[1]            = 0.0f;
    vp.cameraPosW[2]            = 0.0f;
    vp.depthBias                = 1.0e-4f;
    vp.defaultIndexCountPerInst = 36u;
    vp.enableFrustumCull        = true;
    vp.enableDistanceCull       = true;
    vp.enableHiZCull            = true;

    // Warmup + Measured GPU Timing Loop
    const uint32_t warmupFrames   = dx.isWarp ? 3u : 16u;
    const uint32_t measuredFrames = dx.isWarp ? 6u : 60u;
    double sumHiZMs = 0.0, sumCullMs = 0.0;
    for (uint32_t f = 0u; f < warmupFrames + measuredFrames; ++f) {
        if (!dx.Begin()) {
            Check(false, "Begin frame failed");
            return;
        }
        dx.cmd->EndQuery(dx.tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0u);
        host.BuildHiZPyramid(dx.cmd.Get(), depthTex.Get(), kDepthW, kDepthH);
        dx.cmd->EndQuery(dx.tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1u);
        host.CullInstances(dx.cmd.Get(), instBuf.Get(), kTotalInstances, vp);
        dx.cmd->EndQuery(dx.tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 2u);
        dx.cmd->ResolveQueryData(dx.tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0u, 3u, dx.tsReadback.Get(), 0ull);
        if (!dx.FlushAndWait()) {
            Check(false, "FlushAndWait failed");
            return;
        }
        if (f >= warmupFrames) {
            sumHiZMs  += dx.ReadTsRangeMs(0u, 1u);
            sumCullMs += dx.ReadTsRangeMs(1u, 2u);
        }
    }
    const double meanHiZMs   = sumHiZMs / static_cast<double>(measuredFrames);
    const double meanCullMs  = sumCullMs / static_cast<double>(measuredFrames);
    const double meanTotalMs = meanHiZMs + meanCullMs;

    // Readback counts, indirect args, and visible instance index list
    std::vector<uint8_t> cntBytes, argBytes, visBytes;
    Check(dx.ReadbackBuffer(host.GetVisibleCountBuffer(), host.VisibleCountResourceState(), sizeof(GpuCullStats), cntBytes) &&
          dx.ReadbackBuffer(host.GetIndirectArgsBuffer(), host.IndirectArgsResourceState(), sizeof(GpuCullIndirectArgsLayout), argBytes) &&
          dx.ReadbackBuffer(host.GetVisibleInstancesBuffer(), host.VisibleInstancesResourceState(),
                            static_cast<uint64_t>(kTotalInstances) * sizeof(uint32_t), visBytes),
          "Readback VisibleCountBuffer, IndirectArgsBuffer & VisibleInstanceBuffer");

    GpuCullStats stats{};
    GpuCullIndirectArgsLayout args{};
    std::memcpy(&stats, cntBytes.data(), sizeof(stats));
    std::memcpy(&args, argBytes.data(), sizeof(args));

    std::vector<uint32_t> visIndices(stats.visibleInstances);
    if (stats.visibleInstances > 0u && stats.visibleInstances <= kTotalInstances) {
        std::memcpy(visIndices.data(), visBytes.data(), static_cast<size_t>(stats.visibleInstances) * sizeof(uint32_t));
    }

    std::printf("  Counts  : vis=%u (exp %u) | meshlets=%u (exp %u) | frustum=%u (exp %u) | dist=%u (exp %u) | hiz=%u (exp %u)\n",
                stats.visibleInstances, kExpectedVisible,
                stats.visibleMeshlets, expectedMeshlets,
                stats.frustumCulled, kFrustumOutCount,
                stats.distanceCulled, kDistCullCount,
                stats.hizCulled, kBehindWallCount);
    std::printf("  Indirect: DrawIndexed(idx=%u, inst=%u) | DispatchMesh=(%u, %u, %u)\n",
                args.drawIndexed.IndexCountPerInstance, args.drawIndexed.InstanceCount,
                args.dispatchMeshX, args.dispatchMeshY, args.dispatchMeshZ);
    std::printf("  GPU Time: Hi-Z=%.4f ms + Cull(16384)=%.4f ms = %.4f ms (host: HiZ=%.4f ms, Cull=%.4f ms)\n",
                meanHiZMs, meanCullMs, meanTotalMs,
                host.HasValidHiZTiming() ? host.GetLastHiZMs() : 0.0f,
                host.HasValidCullTiming() ? host.GetLastCullMs() : 0.0f);

    Check(stats.frustumCulled == kFrustumOutCount, "100% frustum-outside instances culled (4096/4096)");
    Check(stats.distanceCulled == kDistCullCount, "100% distance-LOD instances culled (1024/1024)");
    Check(stats.hizCulled == kBehindWallCount, "100% behind-wall instances Hi-Z occluded (6144/6144)");
    Check(stats.visibleInstances == kExpectedVisible, "0% false occlusion: 100% in-front + peeking + near-straddling survived (5120/5120)");
    Check(args.drawIndexed.IndexCountPerInstance == 36u &&
          args.drawIndexed.InstanceCount == kExpectedVisible &&
          args.dispatchMeshX == expectedMeshlets &&
          args.dispatchMeshY == 1u && args.dispatchMeshZ == 1u,
          "IndirectArgsBuffer DrawIndexed + DispatchMesh arguments exact match");

    // Verify zero duplicates, zero false survivors, and 100% coverage of expected visible IDs
    std::vector<uint8_t> seen(kTotalInstances, 0u);
    uint32_t falseSurvivors = 0u, duplicates = 0u, outOfRange = 0u;
    for (uint32_t id : visIndices) {
        if (id >= kTotalInstances) { ++outOfRange; continue; }
        if (seen[id]) ++duplicates;
        seen[id] = 1u;
        if (!expectedVis[id]) ++falseSurvivors;
    }
    uint32_t missedVisible = 0u;
    for (uint32_t i = 0u; i < kTotalInstances; ++i) {
        if (expectedVis[i] && !seen[i]) ++missedVisible;
    }
    Check(outOfRange == 0u && duplicates == 0u && falseSurvivors == 0u && missedVisible == 0u,
          "VisibleInstanceBuffer exact set equality (0 duplicates, 0 false survivors, 0 missed)");

    // ------------------------------------------------------------------------
    // 2. Odd-Dimension Conservative Coverage Test (321x181 with 1-px right-edge gap!)
    // ------------------------------------------------------------------------
    {
        constexpr uint32_t kOddW = 321u;
        constexpr uint32_t kOddH = 181u;
        std::vector<float> oddDepth(static_cast<size_t>(kOddW) * kOddH, 0.25f);
        // Carve a 1-pixel vertical slit at the extreme rightmost odd column x = 320 (z = 1.0 background)
        for (uint32_t y = 0u; y < kOddH; ++y) {
            oddDepth[static_cast<size_t>(y) * kOddW + (kOddW - 1u)] = 1.0f;
        }
        ComPtr<ID3D12Resource> oddTex;
        Check(dx.UploadR32Tex2D(oddTex, kOddW, kOddH, oddDepth), "Uploaded odd-dimension 321x181 depth buffer with 1px right-edge slit");

        std::vector<GpuCullInstance> oddInsts;
        // Instance 0: strictly inside solid wall away from slit -> MUST be occluded
        oddInsts.push_back(MakeAabbInst(-0.5f, -0.5f, 8.0f, 0.5f, 0.5f, 9.0f, 5000.0f, 0u, 3u));
        // Instance 1: behind wall at z=8..9, touching right screen edge (ndcX in [0.90, 1.00] -> hits x=320 slit!) -> MUST SURVIVE
        oddInsts.push_back(MakeAabbInst(7.2f, -0.5f, 8.0f, 8.0f, 0.5f, 9.0f, 5000.0f, 1u, 5u));

        ComPtr<ID3D12Resource> oddInstBuf;
        Check(dx.UploadBuffer(oddInstBuf, oddInsts.data(), oddInsts.size() * sizeof(GpuCullInstance)), "Uploaded odd-edge test instances");
        Check(dx.Begin() &&
              host.BuildHiZPyramid(dx.cmd.Get(), oddTex.Get(), kOddW, kOddH) &&
              host.CullInstances(dx.cmd.Get(), oddInstBuf.Get(), 2u, vp) &&
              dx.FlushAndWait(),
              "Executed odd-dimension 321x181 Hi-Z rebuild + cull");

        std::vector<uint8_t> oddCntBytes, oddVisBytes;
        dx.ReadbackBuffer(host.GetVisibleCountBuffer(), host.VisibleCountResourceState(), sizeof(GpuCullStats), oddCntBytes);
        dx.ReadbackBuffer(host.GetVisibleInstancesBuffer(), host.VisibleInstancesResourceState(), 8ull, oddVisBytes);
        GpuCullStats oddStats{};
        uint32_t oddVis0 = 999u;
        std::memcpy(&oddStats, oddCntBytes.data(), sizeof(oddStats));
        std::memcpy(&oddVis0, oddVisBytes.data(), sizeof(oddVis0));
        Check(oddStats.visibleInstances == 1u && oddStats.hizCulled == 1u && oddVis0 == 1u,
              "Odd-edge 321x181 conservative Hi-Z: slit-touching instance survived (0% false occlusion), solid-occluded culled");
    }

    // ------------------------------------------------------------------------
    // 3. Full 65,536-Instance Single-Dispatch Stress Test
    // ------------------------------------------------------------------------
    {
        constexpr uint32_t kStressN = 65536u;
        std::vector<GpuCullInstance> stress(kStressN);
        for (uint32_t i = 0u; i < kStressN; ++i) {
            if ((i & 1u) == 0u) {
                // In front of wall -> visible (32,768)
                stress[i] = MakeAabbInst(-0.1f, -0.1f, 1.12f, 0.1f, 0.1f, 1.18f, 5000.0f, i, 2u);
            } else {
                // Behind wall -> Hi-Z occluded (32,768)
                stress[i] = MakeAabbInst(-0.2f, -0.2f, 10.0f, 0.2f, 0.2f, 10.5f, 5000.0f, i, 2u);
            }
        }
        ComPtr<ID3D12Resource> stressBuf;
        Check(dx.UploadBuffer(stressBuf, stress.data(), stress.size() * sizeof(GpuCullInstance)), "Uploaded 65,536 stress instances");
        Check(dx.Begin() &&
              host.BuildHiZPyramid(dx.cmd.Get(), depthTex.Get(), kDepthW, kDepthH) &&
              host.CullInstances(dx.cmd.Get(), stressBuf.Get(), kStressN, vp) &&
              dx.FlushAndWait(),
              "Executed 65,536-instance single-dispatch Hi-Z cull");
        std::vector<uint8_t> sBytes;
        dx.ReadbackBuffer(host.GetVisibleCountBuffer(), host.VisibleCountResourceState(), sizeof(GpuCullStats), sBytes);
        GpuCullStats ss{};
        std::memcpy(&ss, sBytes.data(), sizeof(ss));
        Check(ss.visibleInstances == 32768u && ss.hizCulled == 32768u && ss.visibleMeshlets == 65536u,
              "65,536-instance stress test exact split (32768 visible / 32768 Hi-Z culled / 65536 meshlets)");
    }

    // ------------------------------------------------------------------------
    // 4. Hardware Performance Budget & Zero D3D12 Debug Errors
    // ------------------------------------------------------------------------
    if (!dx.isWarp) {
        Check(meanTotalMs < 0.15, "HW GPU budget PASS: Hi-Z(512x512) + Cull(16384) < 0.15 ms");
    } else {
        Check(meanTotalMs > 0.0, "WARP timing recorded cleanly");
    }
    Check(host.HasValidHiZTiming() && host.HasValidCullTiming(), "RawGpuCullHost internal GPU timestamp ring valid");

    host.Shutdown();
    const uint32_t dbgErrs = dx.DrainDebugErrors();
    std::printf("  D3D12 Debug Layer Errors: %u\n", dbgErrs);
    Check(dbgErrs == 0u, "D3D12 Debug Layer reported 0 errors/corruptions");
}

} // namespace

int main(int argc, char** argv)
{
    bool warpOnly = false;
    bool hwOnly   = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--warp") == 0)    warpOnly = true;
        if (std::strcmp(argv[i], "--hw-only") == 0) hwOnly   = true;
    }

    if (!warpOnly) {
        RunSuiteOnAdapter(false);
    }
    if (!hwOnly) {
        RunSuiteOnAdapter(true);
    }

    std::printf("\n=== test_gpu_cull Summary: %d passed, %d failed ===\n", gPassed, gFailed);
    return (gFailed == 0) ? 0 : 1;
}
