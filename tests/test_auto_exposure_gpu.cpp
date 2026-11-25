// tests/test_auto_exposure_gpu.cpp: Verification & GPU Benchmark Suite for RAW's SM 6.6 / 6.10
// 256-Bin Center-Weighted Log-Luminance Histogram & Percentile-Trimmed Asymmetric Eye-Adaptation Solver.
//
// Verifies on BOTH Hardware DX12 GPU (e.g. RTX 4090) AND Software WARP (--warp) with D3D12 Debug Layer ON:
//   1. Dark dungeon HDR scene (lum = 0.02) converges cleanly to high exposure gain (exposure > 4.0x).
//   2. Bright sunlit glacier HDR scene (lum = 8.0) converges cleanly to low exposure gain (exposure < 0.15x).
//   3. 95th-Percentile Firefly Rejection: injecting 2% extreme 10,000.0 firefly pixels into a 0.50 midtone
//      scene keeps targetLum within 10% of 0.50 (never collapses exposure!).
//   4. Asymmetric Human Eye-Adaptation Proof: stepping 0.25s dark -> bright moves strictly faster
//      fractionally than stepping 0.25s bright -> dark (brightFrac > 1.8 * darkFrac).
//   5. NaN/Inf resilience (injected NaN/+Inf/-Inf pixels safely ignored -> finite exposure!).
//   6. Real RTX 4090 GPU cost (histMs + adaptMs < 0.08 ms at 960x540!) & 0 D3D12 Debug Layer errors.
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

#include "RawAutoExposureHost.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
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

        D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_READBACK};
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
        const D3D12_RANGE rr{0u, static_cast<SIZE_T>(kMaxQueries) * sizeof(uint64_t)};
        if (FAILED(tsReadback->Map(0u, &rr, &mapped)) || !mapped) return 0.0;
        const auto* ticks = static_cast<const uint64_t*>(mapped);
        const uint64_t t0 = ticks[a];
        const uint64_t t1 = ticks[b];
        const D3D12_RANGE wr{0u, 0u};
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

    bool UploadRgba32FTex2D(ComPtr<ID3D12Resource>& tex, uint32_t w, uint32_t h, const std::vector<float>& rgba)
    {
        D3D12_HEAP_PROPERTIES hpDef{D3D12_HEAP_TYPE_DEFAULT};
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width            = w;
        rd.Height           = h;
        rd.DepthOrArraySize = 1u;
        rd.MipLevels        = 1u;
        rd.Format           = DXGI_FORMAT_R32G32B32A32_FLOAT;
        rd.SampleDesc.Count = 1u;
        if (FAILED(dev->CreateCommittedResource(&hpDef, D3D12_HEAP_FLAG_NONE, &rd,
                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex)))) {
            return false;
        }
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        UINT numRows = 0u;
        UINT64 rowBytes = 0ull, totalBytes = 0ull;
        dev->GetCopyableFootprints(&rd, 0u, 1u, 0ull, &fp, &numRows, &rowBytes, &totalBytes);

        D3D12_HEAP_PROPERTIES hpUp{D3D12_HEAP_TYPE_UPLOAD};
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
        const D3D12_RANGE rr{0u, 0u};
        if (FAILED(up->Map(0u, &rr, &mapped)) || !mapped) return false;
        auto* base = static_cast<uint8_t*>(mapped) + fp.Offset;
        for (uint32_t y = 0u; y < h; ++y) {
            std::memcpy(base + static_cast<size_t>(y) * fp.Footprint.RowPitch,
                        rgba.data() + static_cast<size_t>(y) * w * 4u,
                        static_cast<size_t>(w) * 4u * sizeof(float));
        }
        up->Unmap(0u, nullptr);

        if (!Begin()) return false;
        D3D12_TEXTURE_COPY_LOCATION dst{tex.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
        dst.SubresourceIndex = 0u;
        D3D12_TEXTURE_COPY_LOCATION src{up.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}};
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

    bool ReadbackExposureState(RawAutoExposureHost& host, AutoExposureGpuState& outState)
    {
        ID3D12Resource* res = host.GetExposureBufferResource();
        if (!res) return false;

        D3D12_HEAP_PROPERTIES hpRb{D3D12_HEAP_TYPE_READBACK};
        D3D12_RESOURCE_DESC rbd{};
        rbd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        rbd.Width            = 256ull;
        rbd.Height           = 1u;
        rbd.DepthOrArraySize = 1u;
        rbd.MipLevels        = 1u;
        rbd.SampleDesc.Count = 1u;
        rbd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> rb;
        if (FAILED(dev->CreateCommittedResource(&hpRb, D3D12_HEAP_FLAG_NONE, &rbd,
                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&rb)))) {
            return false;
        }

        if (!Begin()) return false;
        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = res;
        b.Transition.StateBefore = host.ExposureBufferResourceState();
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1u, &b);

        cmd->CopyBufferRegion(rb.Get(), 0ull, res, 0ull, sizeof(AutoExposureGpuState));

        std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
        cmd->ResourceBarrier(1u, &b);
        if (!FlushAndWait()) return false;

        void* mapped = nullptr;
        const D3D12_RANGE rr{0u, sizeof(AutoExposureGpuState)};
        if (FAILED(rb->Map(0u, &rr, &mapped)) || !mapped) return false;
        std::memcpy(&outState, mapped, sizeof(AutoExposureGpuState));
        const D3D12_RANGE wr{0u, 0u};
        rb->Unmap(0u, &wr);
        return true;
    }
};

std::vector<float> MakeUniformHdr(uint32_t w, uint32_t h, float lum)
{
    std::vector<float> img(static_cast<size_t>(w) * h * 4u);
    for (size_t i = 0u; i < static_cast<size_t>(w) * h; ++i) {
        img[i * 4u + 0u] = lum;
        img[i * 4u + 1u] = lum;
        img[i * 4u + 2u] = lum;
        img[i * 4u + 3u] = 1.0f;
    }
    return img;
}

void RunSuiteOnAdapter(bool useWarp)
{
    TestDx12 dx{};
    if (!dx.Init(useWarp)) {
        Check(false, useWarp ? "WARP D3D12 device init" : "Hardware DX12 device init");
        return;
    }

    std::printf("\n=== GPU 256-Bin Log-Luminance Auto-Exposure Suite [%s] (DebugLayer=%s, TsFreq=%llu Hz) ===\n",
                dx.adapterName.c_str(), dx.debugLayer ? "ON" : "OFF",
                static_cast<unsigned long long>(dx.tsFreqHz));

    RawAutoExposureHost host;
    std::string err;
    const bool initOk = host.Init(dx.dev.Get(), {}, &err);
    Check(initOk, "RawAutoExposureHost::Init succeeded");
    if (!initOk) {
        std::fprintf(stderr, "  Init error: %s\n", err.c_str());
        return;
    }
    host.SetTimestampFrequency(dx.tsFreqHz);

    constexpr uint32_t kW = 960u;
    constexpr uint32_t kH = 540u;

    // Upload 4 test HDR textures at 960x540 (RGBA32F so 10,000.0 & NaN/Inf are exact):
    //   1. Dark dungeon (lum = 0.02)
    //   2. Bright sunlit glacier (lum = 8.0)
    //   3. Midtone (lum = 0.50) + 2% extreme 10,000.0 firefly pixels
    //   4. Midtone (lum = 0.50) + 5% NaN / +Inf / -Inf poisoned pixels
    ComPtr<ID3D12Resource> darkTex, brightTex, fireflyTex, nanTex;
    {
        auto darkImg   = MakeUniformHdr(kW, kH, 0.02f);
        auto brightImg = MakeUniformHdr(kW, kH, 8.0f);
        auto ffImg     = MakeUniformHdr(kW, kH, 0.50f);
        auto nanImg    = MakeUniformHdr(kW, kH, 0.50f);

        // Inject 2% extreme 10,000.0 firefly pixels uniformly distributed across image (every 50th pixel)
        const size_t totalPx = static_cast<size_t>(kW) * kH;
        for (size_t i = 0u; i < totalPx; i += 50u) {
            ffImg[i * 4u + 0u] = 10000.0f;
            ffImg[i * 4u + 1u] = 10000.0f;
            ffImg[i * 4u + 2u] = 10000.0f;
        }

        // Inject 5% NaN / +Inf / -Inf pixels across image
        const float qNaN = std::numeric_limits<float>::quiet_NaN();
        const float pInf = std::numeric_limits<float>::infinity();
        const float nInf = -std::numeric_limits<float>::infinity();
        for (size_t i = 0u; i < totalPx; i += 20u) {
            const uint32_t kind = static_cast<uint32_t>((i / 20u) % 3u);
            const float bad = (kind == 0u) ? qNaN : ((kind == 1u) ? pInf : nInf);
            nanImg[i * 4u + 0u] = bad;
            nanImg[i * 4u + 1u] = bad;
            nanImg[i * 4u + 2u] = bad;
        }

        const bool upOk = dx.UploadRgba32FTex2D(darkTex, kW, kH, darkImg) &&
                          dx.UploadRgba32FTex2D(brightTex, kW, kH, brightImg) &&
                          dx.UploadRgba32FTex2D(fireflyTex, kW, kH, ffImg) &&
                          dx.UploadRgba32FTex2D(nanTex, kW, kH, nanImg);
        Check(upOk, "Uploaded 4x 960x540 RGBA32F HDR test scenes (dark, glacier, 2% firefly, 5% NaN/Inf)");
        if (!upOk) return;
    }

    auto runSingleStep = [&](ID3D12Resource* tex, const AutoExposureParams& p, AutoExposureGpuState& st) -> bool {
        if (!dx.Begin()) return false;
        if (!host.Execute(dx.cmd.Get(), tex, kW, kH, p)) return false;
        if (!dx.FlushAndWait()) return false;
        return dx.ReadbackExposureState(host, st);
    };

    // --- Test 1: Dark dungeon HDR scene (lum = 0.02 -> exposure > 4.0x) ---
    AutoExposureParams pDark{};
    pDark.resetAdapt = true;
    AutoExposureGpuState stDark{};
    Check(runSingleStep(darkTex.Get(), pDark, stDark), "Executed dark dungeon scene (lum=0.02)");
    std::printf("  Dark    : targetLum=%.5f adaptedLum=%.5f exposure=%.3fx ev100=%.2f weight=%u\n",
                stDark.targetLum, stDark.adaptedLum, stDark.adaptedExposure, stDark.targetEv100, stDark.totalWeight);
    Check(std::isfinite(stDark.adaptedExposure) && stDark.adaptedExposure > 4.0f &&
          std::fabs(stDark.targetLum - 0.02f) < 0.003f,
          "Dark dungeon (lum=0.02) converges cleanly to high exposure gain (> 4.0x)");

    // --- Test 2: Bright sunlit glacier HDR scene (lum = 8.0 -> exposure < 0.15x) ---
    AutoExposureParams pBright{};
    pBright.resetAdapt = true;
    AutoExposureGpuState stBright{};
    Check(runSingleStep(brightTex.Get(), pBright, stBright), "Executed sunlit glacier scene (lum=8.0)");
    std::printf("  Glacier : targetLum=%.5f adaptedLum=%.5f exposure=%.4fx ev100=%.2f weight=%u\n",
                stBright.targetLum, stBright.adaptedLum, stBright.adaptedExposure, stBright.targetEv100, stBright.totalWeight);
    Check(std::isfinite(stBright.adaptedExposure) && stBright.adaptedExposure < 0.15f &&
          std::fabs(stBright.targetLum - 8.0f) < 0.5f,
          "Bright sunlit glacier (lum=8.0) converges cleanly to low exposure gain (< 0.15x)");

    // --- Test 3: 95th-Percentile Firefly Rejection Proof (2% 10,000.0 fireflies in 0.50 scene) ---
    AutoExposureParams pFirefly{};
    pFirefly.resetAdapt = true;
    AutoExposureGpuState stFirefly{};
    Check(runSingleStep(fireflyTex.Get(), pFirefly, stFirefly), "Executed 2% 10,000.0 firefly rejection scene");
    const float ffRelErr = std::fabs(stFirefly.targetLum - 0.50f) / 0.50f;
    std::printf("  Firefly : targetLum=%.5f (err=%.2f%% vs 0.50) exposure=%.4fx\n",
                stFirefly.targetLum, ffRelErr * 100.0f, stFirefly.adaptedExposure);
    Check(std::isfinite(stFirefly.targetLum) && ffRelErr < 0.10f,
          "95th-percentile firefly rejection: 2% 10,000.0 pixels keep targetLum within 10% of 0.50");

    // --- Test 4: Asymmetric Human Eye-Adaptation Proof (0.25s dark->bright vs bright->dark) ---
    AutoExposureParams pStep{};
    pStep.dtSec        = 0.25f;
    pStep.tauBrightSec = 0.35f;
    pStep.tauDarkSec   = 1.40f;
    pStep.resetAdapt   = false;

    // 4a. Start at dark (0.02), step 0.25s toward bright (8.0)
    if (!dx.Begin()) { Check(false, "Begin ResetAdaptation dark->bright"); return; }
    host.ResetAdaptation(dx.cmd.Get());
    if (!dx.FlushAndWait()) { Check(false, "Flush ResetAdaptation dark->bright"); return; }

    AutoExposureGpuState stBaseDark{}, stAfterBrightStep{};
    runSingleStep(darkTex.Get(), pStep, stBaseDark);          // snaps to dark (0.02) via ResetAdaptation
    runSingleStep(brightTex.Get(), pStep, stAfterBrightStep); // 0.25s brightening step (tauBright=0.35s)

    // 4b. Start at bright (8.0), step 0.25s toward dark (0.02)
    if (!dx.Begin()) { Check(false, "Begin ResetAdaptation bright->dark"); return; }
    host.ResetAdaptation(dx.cmd.Get());
    if (!dx.FlushAndWait()) { Check(false, "Flush ResetAdaptation bright->dark"); return; }

    AutoExposureGpuState stBaseBright{}, stAfterDarkStep{};
    runSingleStep(brightTex.Get(), pStep, stBaseBright);    // snaps to bright (8.0) via ResetAdaptation
    runSingleStep(darkTex.Get(), pStep, stAfterDarkStep);   // 0.25s darkening step (tauDark=1.40s)

    const float logDark   = std::log2(stBaseDark.adaptedLum);
    const float logBright = std::log2(stBaseBright.adaptedLum);
    const float brightFrac = (std::log2(stAfterBrightStep.adaptedLum) - logDark) / (logBright - logDark);
    const float darkFrac   = (std::log2(stAfterDarkStep.adaptedLum) - logBright) / (logDark - logBright);
    const float asymRatio  = (darkFrac > 1.0e-5f) ? (brightFrac / darkFrac) : 0.0f;
    std::printf("  AsymEMA : dark->bright frac=%.4f (lum %.4f->%.4f) | bright->dark frac=%.4f (lum %.4f->%.4f) | ratio=%.2fx\n",
                brightFrac, stBaseDark.adaptedLum, stAfterBrightStep.adaptedLum,
                darkFrac, stBaseBright.adaptedLum, stAfterDarkStep.adaptedLum, asymRatio);
    Check(brightFrac > 0.35f && darkFrac > 0.08f && brightFrac > 1.8f * darkFrac,
          "Asymmetric eye adaptation: 0.25s dark->bright strictly faster than bright->dark (brightFrac > 1.8 * darkFrac)");

    // --- Test 5: NaN / +Inf / -Inf Resilience ---
    AutoExposureParams pNan{};
    pNan.resetAdapt = true;
    AutoExposureGpuState stNan{};
    Check(runSingleStep(nanTex.Get(), pNan, stNan), "Executed 5% NaN/+Inf/-Inf poisoned HDR scene");
    const float nanRelErr = std::fabs(stNan.targetLum - 0.50f) / 0.50f;
    std::printf("  NaNTest : targetLum=%.5f (err=%.2f%% vs 0.50) adaptedExposure=%.4fx finite=%s\n",
                stNan.targetLum, nanRelErr * 100.0f, stNan.adaptedExposure,
                (std::isfinite(stNan.adaptedLum) && std::isfinite(stNan.adaptedExposure) &&
                 std::isfinite(stNan.targetLum) && std::isfinite(stNan.targetEv100)) ? "YES" : "NO");
    Check(std::isfinite(stNan.adaptedLum) && std::isfinite(stNan.adaptedExposure) &&
          std::isfinite(stNan.targetLum) && std::isfinite(stNan.targetEv100) && nanRelErr < 0.10f,
          "NaN/Inf resilience: poisoned pixels ignored cleanly with exact finite exposure");

    // --- Test 6: Warmup + Measured GPU Timing Loop ---
    AutoExposureParams pBench{};
    pBench.dtSec      = 1.0f / 60.0f;
    pBench.resetAdapt = false;
    const uint32_t warmupFrames   = dx.isWarp ? 2u : 16u;
    const uint32_t measuredFrames = dx.isWarp ? 4u : 60u;
    double sumTotalMs = 0.0;
    for (uint32_t f = 0u; f < warmupFrames + measuredFrames; ++f) {
        if (!dx.Begin()) {
            Check(false, "Begin timing frame failed");
            return;
        }
        dx.cmd->EndQuery(dx.tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0u);
        host.Execute(dx.cmd.Get(), fireflyTex.Get(), kW, kH, pBench);
        dx.cmd->EndQuery(dx.tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1u);
        dx.cmd->ResolveQueryData(dx.tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0u, 2u, dx.tsReadback.Get(), 0ull);
        if (!dx.FlushAndWait()) {
            Check(false, "FlushAndWait timing frame failed");
            return;
        }
        if (f >= warmupFrames) {
            sumTotalMs += dx.ReadTsRangeMs(0u, 1u);
        }
    }
    const double meanTotalMs = sumTotalMs / static_cast<double>(measuredFrames);
    const float  hostHistMs  = host.HasValidTiming() ? host.GetLastHistMs() : 0.0f;
    const float  hostAdaptMs = host.HasValidTiming() ? host.GetLastAdaptMs() : 0.0f;
    std::printf("  GPU Time: total=%.4f ms (host: hist=%.4f ms + adapt=%.4f ms = %.4f ms)\n",
                meanTotalMs, hostHistMs, hostAdaptMs, hostHistMs + hostAdaptMs);

    if (!dx.isWarp) {
        Check(meanTotalMs < 0.08, "HW GPU budget PASS: 960x540 Histogram + Eye-Adapt < 0.08 ms");
    } else {
        Check(meanTotalMs > 0.0, "WARP timing recorded cleanly");
    }
    Check(host.HasValidTiming(), "RawAutoExposureHost internal GPU timestamp ring valid");

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

    std::printf("\n=== test_auto_exposure_gpu Summary: %d passed, %d failed ===\n", gPassed, gFailed);
    return (gFailed == 0) ? 0 : 1;
}
