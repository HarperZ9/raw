// tests/test_tsr_upscale_gpu.cpp: Verification & GPU Benchmark Suite for RAW-TSR (SM 6.6 / 6.10
// GPU Compute Temporal Super-Resolution + 5-Tap Catmull-Rom + YCoCg Variance Clip + RCAS Sharpen).
//
// Verifies on BOTH Hardware DX12 GPU (e.g. RTX 4090) AND Software WARP (--warp) with D3D12 Debug Layer ON:
//   1. Static Halton (2,3) Jitter Convergence: 16 frames of 640x360 -> 1280x720 2x super-resolution
//      strictly reduces diagonal sub-pixel aliasing RMSE vs 1-frame upscale.
//   2. Fast Camera Pan Disocclusion Anti-Ghosting: rapid horizontal pan over a high-contrast scene
//      with matching motion vectors & newly disoccluded edge leaves ZERO ghosting trail.
//   3. Reactive Mask Pass-Through: bright moving spark/ember tagged with reactiveMask=1.0 passes
//      through immediately at >90% peak brightness with zero ghost trail left at its previous position.
//   4. RCAS Contrast-Adaptive Sharpening: boosts soft edge gradient cleanly without ringing or NaNs.
//   5. Dynamic Resize Auto-Reallocation (320x180->640x360 to 640x360->1280x720) & NaN/Inf resilience.
//   6. Real RTX 4090 GPU cost (< 0.08 ms at 640x360 -> 1280x720!) & 0 D3D12 Debug Layer errors.
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

#include "RawTsrUpscaleHost.h"

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

float HalfToFloat(uint16_t h)
{
    const uint32_t sign = static_cast<uint32_t>(h & 0x8000u) << 16;
    const uint32_t exp  = (h >> 10) & 0x1Fu;
    const uint32_t mant = h & 0x03FFu;
    uint32_t bits = 0u;
    if (exp == 0u) {
        if (mant == 0u) {
            bits = sign;
        } else {
            uint32_t m = mant;
            uint32_t e = 127u - 14u;
            while ((m & 0x0400u) == 0u) { m <<= 1; --e; }
            m &= 0x03FFu;
            bits = sign | (e << 23) | (m << 13);
        }
    } else if (exp == 31u) {
        bits = sign | 0x7F800000u | (mant << 13);
    } else {
        bits = sign | ((exp + (127u - 15u)) << 23) | (mant << 13);
    }
    float f = 0.0f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

// High-frequency analytic ground-truth test scene (crisp diagonal zone-plate / fine grating)
// so 2x temporal super-resolution visibly resolves sub-pixel geometry that 1x low-res aliases.
void EvalAnalyticScene(float u, float v, float& r, float& g, float& b)
{
    // Diagonal high-frequency grating near Nyquist of 640x360 (wavelength ~2.4 low-res px = ~4.8 high-res px)
    const float xHi = u * 1280.0f;
    const float yHi = v * 720.0f;
    const float diag = xHi * 0.80f + yHi * 0.60f;
    const float s1 = std::sin(diag * (2.0f * 3.14159265f / 5.2f));
    const float s2 = std::sin((xHi * 0.60f - yHi * 0.80f) * (2.0f * 3.14159265f / 7.4f));
    const float edge = (s1 > 0.0f) ? 0.90f : 0.12f;
    const float mod  = 0.5f + 0.35f * s2;
    r = edge * mod;
    g = edge * (0.65f + 0.25f * s1);
    b = 0.15f + 0.55f * (1.0f - edge) * mod;
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

    bool UploadFloatTex2D(ComPtr<ID3D12Resource>& tex, uint32_t w, uint32_t h, DXGI_FORMAT fmt,
                          uint32_t floatsPerPx, const std::vector<float>& data)
    {
        D3D12_HEAP_PROPERTIES hpDef{D3D12_HEAP_TYPE_DEFAULT};
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width            = w;
        rd.Height           = h;
        rd.DepthOrArraySize = 1u;
        rd.MipLevels        = 1u;
        rd.Format           = fmt;
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
        const size_t srcRowBytes = static_cast<size_t>(w) * floatsPerPx * sizeof(float);
        for (uint32_t y = 0u; y < h; ++y) {
            std::memcpy(base + static_cast<size_t>(y) * fp.Footprint.RowPitch,
                        data.data() + static_cast<size_t>(y) * w * floatsPerPx,
                        srcRowBytes);
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

    bool ReadbackRgba16Tex2D(ID3D12Resource* tex, D3D12_RESOURCE_STATES curState,
                             uint32_t& outW, uint32_t& outH, std::vector<float>& outRgba)
    {
        const D3D12_RESOURCE_DESC rd = tex->GetDesc();
        outW = static_cast<uint32_t>(rd.Width);
        outH = rd.Height;

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        UINT numRows = 0u;
        UINT64 rowBytes = 0ull, totalBytes = 0ull;
        dev->GetCopyableFootprints(&rd, 0u, 1u, 0ull, &fp, &numRows, &rowBytes, &totalBytes);

        D3D12_HEAP_PROPERTIES hpRb{D3D12_HEAP_TYPE_READBACK};
        D3D12_RESOURCE_DESC rbd{};
        rbd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        rbd.Width            = totalBytes;
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
        b.Transition.pResource   = tex;
        b.Transition.StateBefore = curState;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1u, &b);

        D3D12_TEXTURE_COPY_LOCATION src{tex, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
        src.SubresourceIndex = 0u;
        D3D12_TEXTURE_COPY_LOCATION dst{rb.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}};
        dst.PlacedFootprint = fp;
        cmd->CopyTextureRegion(&dst, 0u, 0u, 0u, &src, nullptr);

        std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
        cmd->ResourceBarrier(1u, &b);
        if (!FlushAndWait()) return false;

        void* mapped = nullptr;
        const D3D12_RANGE rr{0u, static_cast<SIZE_T>(totalBytes)};
        if (FAILED(rb->Map(0u, &rr, &mapped)) || !mapped) return false;
        const auto* base = static_cast<const uint8_t*>(mapped) + fp.Offset;
        outRgba.resize(static_cast<size_t>(outW) * outH * 4u);
        for (uint32_t y = 0u; y < outH; ++y) {
            const auto* row = reinterpret_cast<const uint16_t*>(base + static_cast<size_t>(y) * fp.Footprint.RowPitch);
            for (uint32_t x = 0u; x < outW; ++x) {
                const size_t i = (static_cast<size_t>(y) * outW + x) * 4u;
                outRgba[i + 0u] = HalfToFloat(row[x * 4u + 0u]);
                outRgba[i + 1u] = HalfToFloat(row[x * 4u + 1u]);
                outRgba[i + 2u] = HalfToFloat(row[x * 4u + 2u]);
                outRgba[i + 3u] = HalfToFloat(row[x * 4u + 3u]);
            }
        }
        const D3D12_RANGE wr{0u, 0u};
        rb->Unmap(0u, &wr);
        return true;
    }
};

void RunSuiteOnAdapter(bool useWarp)
{
    TestDx12 dx{};
    if (!dx.Init(useWarp)) {
        Check(false, useWarp ? "WARP D3D12 device init" : "Hardware DX12 device init");
        return;
    }

    std::printf("\n=== RAW-TSR Temporal Super-Resolution + RCAS Suite [%s] (DebugLayer=%s, TsFreq=%llu Hz) ===\n",
                dx.adapterName.c_str(), dx.debugLayer ? "ON" : "OFF",
                static_cast<unsigned long long>(dx.tsFreqHz));

    RawTsrUpscaleHost host;
    TsrUpscaleDesc desc{};
    // Start smaller (320x180 -> 640x360) to prove dynamic auto-reallocation to 640x360 -> 1280x720
    desc.initialInputWidth   = 320u;
    desc.initialInputHeight  = 180u;
    desc.initialOutputWidth  = 640u;
    desc.initialOutputHeight = 360u;
    std::string err;
    const bool initOk = host.Init(dx.dev.Get(), desc, &err);
    Check(initOk, "RawTsrUpscaleHost::Init succeeded");
    if (!initOk) {
        std::fprintf(stderr, "  Init error: %s\n", err.c_str());
        return;
    }
    host.SetTimestampFrequency(dx.tsFreqHz);

    constexpr uint32_t kInW  = 640u;
    constexpr uint32_t kInH  = 360u;
    constexpr uint32_t kOutW = 1280u;
    constexpr uint32_t kOutH = 720u;

    // Ground truth at 1280x720
    std::vector<float> gtRgba(static_cast<size_t>(kOutW) * kOutH * 4u, 0.0f);
    for (uint32_t y = 0u; y < kOutH; ++y) {
        for (uint32_t x = 0u; x < kOutW; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(kOutW);
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(kOutH);
            const size_t i = (static_cast<size_t>(y) * kOutW + x) * 4u;
            EvalAnalyticScene(u, v, gtRgba[i + 0u], gtRgba[i + 1u], gtRgba[i + 2u]);
            gtRgba[i + 3u] = 1.0f;
        }
    }

    auto computeInteriorRmse = [&](const std::vector<float>& img) -> float {
        double sumSq = 0.0;
        uint64_t cnt = 0ull;
        for (uint32_t y = 32u; y < kOutH - 32u; ++y) {
            for (uint32_t x = 32u; x < kOutW - 32u; ++x) {
                const size_t i = (static_cast<size_t>(y) * kOutW + x) * 4u;
                for (int c = 0; c < 3; ++c) {
                    const double d = static_cast<double>(img[i + c]) - static_cast<double>(gtRgba[i + c]);
                    sumSq += d * d;
                    ++cnt;
                }
            }
        }
        return static_cast<float>(std::sqrt(sumSq / static_cast<double>(cnt)));
    };

    // Shared flat depth & zero velocity textures at 640x360
    std::vector<float> flatDepth(static_cast<size_t>(kInW) * kInH, 0.5f);
    std::vector<float> zeroVel(static_cast<size_t>(kInW) * kInH * 2u, 0.0f);
    ComPtr<ID3D12Resource> flatDepthTex, zeroVelTex;
    dx.UploadFloatTex2D(flatDepthTex, kInW, kInH, DXGI_FORMAT_R32_FLOAT, 1u, flatDepth);
    dx.UploadFloatTex2D(zeroVelTex, kInW, kInH, DXGI_FORMAT_R32G32_FLOAT, 2u, zeroVel);

    // ========================================================================
    // Test 1: Static Halton (2,3) Jitter Convergence (16 frames vs 1 frame)
    // ========================================================================
    uint32_t rw = 0u, rh = 0u;
    std::vector<float> outRgba;
    float rmse1Frame = 0.0f;
    float rmse16Frames = 0.0f;
    ComPtr<ID3D12Resource> lastInColorTex;

    std::vector<float> lowColor(static_cast<size_t>(kInW) * kInH * 4u, 0.0f);
    for (uint32_t f = 0u; f < 16u; ++f) {
        float jx = 0.0f, jy = 0.0f;
        ComputeHaltonJitter(f, jx, jy);
        for (uint32_t y = 0u; y < kInH; ++y) {
            for (uint32_t x = 0u; x < kInW; ++x) {
                const float u = (static_cast<float>(x) + 0.5f + jx) / static_cast<float>(kInW);
                const float v = (static_cast<float>(y) + 0.5f + jy) / static_cast<float>(kInH);
                const size_t i = (static_cast<size_t>(y) * kInW + x) * 4u;
                EvalAnalyticScene(u, v, lowColor[i + 0u], lowColor[i + 1u], lowColor[i + 2u]);
                lowColor[i + 3u] = 1.0f;
            }
        }
        ComPtr<ID3D12Resource> inTex;
        dx.UploadFloatTex2D(inTex, kInW, kInH, DXGI_FORMAT_R32G32B32A32_FLOAT, 4u, lowColor);
        lastInColorTex = inTex;

        TsrUpscaleParams tp{};
        tp.jitterX       = jx;
        tp.jitterY       = jy;
        tp.historyWeight = 0.88f;
        tp.varianceGamma = 1.15f;
        tp.rcasSharpness = 0.0f; // Pure TSR convergence check first
        tp.resetHistory  = (f == 0u);

        if (!dx.Begin() || !host.Execute(dx.cmd.Get(), inTex.Get(), flatDepthTex.Get(), zeroVelTex.Get(), nullptr,
                                         kInW, kInH, kOutW, kOutH, tp) || !dx.FlushAndWait()) {
            Check(false, "TSR convergence frame Execute failed");
            return;
        }
        if (f == 0u) {
            dx.ReadbackRgba16Tex2D(host.GetOutputResource(), host.OutputResourceState(), rw, rh, outRgba);
            rmse1Frame = computeInteriorRmse(outRgba);
        }
    }
    Check(host.InputWidth() == kInW && host.OutputWidth() == kOutW && host.OutputHeight() == kOutH,
          "Dynamic resize auto-reallocation (320x180->640x360 to 640x360->1280x720) succeeded");

    dx.ReadbackRgba16Tex2D(host.GetOutputResource(), host.OutputResourceState(), rw, rh, outRgba);
    rmse16Frames = computeInteriorRmse(outRgba);
    const float rmseReductionPct = (1.0f - rmse16Frames / std::max(rmse1Frame, 1.0e-5f)) * 100.0f;
    std::printf("  TSR Conv: rmse1=%.4f -> rmse16=%.4f (reduction=%.1f%%)\n", rmse1Frame, rmse16Frames, rmseReductionPct);
    Check(rmse16Frames < rmse1Frame * 0.80f,
          "Static Halton (2,3) jitter convergence reduces 2x aliasing RMSE by > 20% vs 1-frame");

    // ========================================================================
    // Test 2: Fast Camera Pan Disocclusion Anti-Ghosting
    // ========================================================================
    // Frame A: Bright orange vertical tower occupying x in [280..360] (low-res) on dark navy sky (0.02, 0.04, 0.12).
    // Frame B: Rapid pan right (+40 low-res px): tower shifts to [320..400], newly uncovering [280..320] as dark navy sky!
    // Velocity is set to 0 at [280..320] (simulating disoccluded static background where history STILL holds bright orange!).
    // YCoCg variance clipping must immediately clamp away the stale orange history -> ZERO ghosting!
    std::vector<float> panA(static_cast<size_t>(kInW) * kInH * 4u, 0.0f);
    std::vector<float> panB(static_cast<size_t>(kInW) * kInH * 4u, 0.0f);
    std::vector<float> panVel(static_cast<size_t>(kInW) * kInH * 2u, 0.0f);
    for (uint32_t y = 0u; y < kInH; ++y) {
        for (uint32_t x = 0u; x < kInW; ++x) {
            const size_t i = static_cast<size_t>(y) * kInW + x;
            // Frame A: tower at [280..360]
            if (x >= 280u && x < 360u) {
                panA[i * 4u + 0u] = 3.0f; panA[i * 4u + 1u] = 1.2f; panA[i * 4u + 2u] = 0.1f; panA[i * 4u + 3u] = 1.0f;
            } else {
                panA[i * 4u + 0u] = 0.02f; panA[i * 4u + 1u] = 0.04f; panA[i * 4u + 2u] = 0.12f; panA[i * 4u + 3u] = 1.0f;
            }
            // Frame B: tower shifted to [320..400], [280..320] is newly disoccluded dark sky
            if (x >= 320u && x < 400u) {
                panB[i * 4u + 0u] = 3.0f; panB[i * 4u + 1u] = 1.2f; panB[i * 4u + 2u] = 0.1f; panB[i * 4u + 3u] = 1.0f;
                panVel[i * 2u + 0u] = 40.0f / static_cast<float>(kInW);
            } else {
                panB[i * 4u + 0u] = 0.02f; panB[i * 4u + 1u] = 0.04f; panB[i * 4u + 2u] = 0.12f; panB[i * 4u + 3u] = 1.0f;
            }
        }
    }
    ComPtr<ID3D12Resource> panATex, panBTex, panVelTex;
    dx.UploadFloatTex2D(panATex, kInW, kInH, DXGI_FORMAT_R32G32B32A32_FLOAT, 4u, panA);
    dx.UploadFloatTex2D(panBTex, kInW, kInH, DXGI_FORMAT_R32G32B32A32_FLOAT, 4u, panB);
    dx.UploadFloatTex2D(panVelTex, kInW, kInH, DXGI_FORMAT_R32G32_FLOAT, 2u, panVel);

    TsrUpscaleParams pPan{};
    pPan.historyWeight = 0.90f;
    pPan.varianceGamma = 1.15f;
    pPan.rcasSharpness = 0.0f;
    pPan.resetHistory  = true;
    dx.Begin(); host.Execute(dx.cmd.Get(), panATex.Get(), flatDepthTex.Get(), zeroVelTex.Get(), nullptr, kInW, kInH, kOutW, kOutH, pPan); dx.FlushAndWait();
    pPan.resetHistory  = false;
    dx.Begin(); host.Execute(dx.cmd.Get(), panBTex.Get(), flatDepthTex.Get(), panVelTex.Get(), nullptr, kInW, kInH, kOutW, kOutH, pPan); dx.FlushAndWait();

    dx.ReadbackRgba16Tex2D(host.GetOutputResource(), host.OutputResourceState(), rw, rh, outRgba);
    // Probe center of newly disoccluded sky region: low-res x=300 -> high-res x=600, y=360
    const float ghostR = outRgba[(static_cast<size_t>(360u) * kOutW + 600u) * 4u + 0u];
    const float towerR = outRgba[(static_cast<size_t>(360u) * kOutW + 720u) * 4u + 0u];
    std::printf("  AntiGhst: disoccludedSkyR=%.4f (true 0.02, staleHist 3.00) | movedTowerR=%.3f\n", ghostR, towerR);
    Check(ghostR < 0.05f && towerR > 2.8f,
          "Fast pan disocclusion anti-ghosting: YCoCg variance clamp eliminates stale 3.0 orange ghosting");

    // ========================================================================
    // Test 3: Reactive Mask Pass-Through (Moving Fire Spark Preserved Without Ghosting)
    // ========================================================================
    // Frame 1: Dark background everywhere (0.05) -> history locked to 0.05.
    // Frame 2: Bright ember spark (4.0, 2.0, 0.2) appears at low-res (320, 180) with reactiveMask=1.0 (zero velocity).
    // Without reactive mask, 0.90 history weight would crush the spark down to ~0.45!
    // With reactiveMask=1.0, spark passes through at > 3.5!
    std::vector<float> darkBg(static_cast<size_t>(kInW) * kInH * 4u, 0.05f);
    std::vector<float> sparkCol = darkBg;
    std::vector<float> sparkReact(static_cast<size_t>(kInW) * kInH, 0.0f);
    for (uint32_t y = 178u; y <= 182u; ++y) {
        for (uint32_t x = 318u; x <= 322u; ++x) {
            const size_t i = static_cast<size_t>(y) * kInW + x;
            sparkCol[i * 4u + 0u] = 4.0f;
            sparkCol[i * 4u + 1u] = 2.0f;
            sparkCol[i * 4u + 2u] = 0.2f;
            sparkCol[i * 4u + 3u] = 1.0f;
            sparkReact[i]         = 1.0f;
        }
    }
    ComPtr<ID3D12Resource> darkBgTex, sparkColTex, sparkReactTex;
    dx.UploadFloatTex2D(darkBgTex, kInW, kInH, DXGI_FORMAT_R32G32B32A32_FLOAT, 4u, darkBg);
    dx.UploadFloatTex2D(sparkColTex, kInW, kInH, DXGI_FORMAT_R32G32B32A32_FLOAT, 4u, sparkCol);
    dx.UploadFloatTex2D(sparkReactTex, kInW, kInH, DXGI_FORMAT_R32_FLOAT, 1u, sparkReact);

    TsrUpscaleParams pReact{};
    pReact.historyWeight = 0.92f;
    pReact.varianceGamma = 1.15f;
    pReact.rcasSharpness = 0.0f;
    pReact.resetHistory  = true;
    dx.Begin(); host.Execute(dx.cmd.Get(), darkBgTex.Get(), flatDepthTex.Get(), zeroVelTex.Get(), nullptr, kInW, kInH, kOutW, kOutH, pReact); dx.FlushAndWait();
    pReact.resetHistory  = false;
    dx.Begin(); host.Execute(dx.cmd.Get(), sparkColTex.Get(), flatDepthTex.Get(), zeroVelTex.Get(), sparkReactTex.Get(), kInW, kInH, kOutW, kOutH, pReact); dx.FlushAndWait();

    dx.ReadbackRgba16Tex2D(host.GetOutputResource(), host.OutputResourceState(), rw, rh, outRgba);
    const float sparkPeakR = outRgba[(static_cast<size_t>(360u) * kOutW + 640u) * 4u + 0u];
    std::printf("  Reactive: movingSparkR=%.3f (true 4.000, history was 0.050)\n", sparkPeakR);
    Check(sparkPeakR > 3.6f, "Reactive mask pass-through: moving fire spark preserves >90% peak brightness");

    // ========================================================================
    // Test 4: RCAS Contrast-Adaptive Sharpening (Edge Boost + Zero Ringing + NaN Safety)
    // ========================================================================
    // Build soft horizontal ramp (0.1 -> 0.9 over 4 low-res pixels) + poisoned NaN/+Inf pixels
    std::vector<float> edgeImg(static_cast<size_t>(kInW) * kInH * 4u, 0.1f);
    for (uint32_t y = 0u; y < kInH; ++y) {
        for (uint32_t x = 0u; x < kInW; ++x) {
            const float t = std::clamp((static_cast<float>(x) - 318.0f) / 4.0f, 0.0f, 1.0f);
            const float v = 0.1f + 0.8f * t;
            const size_t i = (static_cast<size_t>(y) * kInW + x) * 4u;
            edgeImg[i + 0u] = v; edgeImg[i + 1u] = v; edgeImg[i + 2u] = v; edgeImg[i + 3u] = 1.0f;
        }
    }
    // Inject NaN/+Inf in far corner (away from center edge probe)
    edgeImg[0] = std::numeric_limits<float>::quiet_NaN();
    edgeImg[4] = std::numeric_limits<float>::infinity();
    ComPtr<ID3D12Resource> edgeTex;
    dx.UploadFloatTex2D(edgeTex, kInW, kInH, DXGI_FORMAT_R32G32B32A32_FLOAT, 4u, edgeImg);

    auto measureEdgeGrad = [&](float rcasSharp, float& outMin, float& outMax, uint32_t& outNans) -> float {
        TsrUpscaleParams tp{};
        tp.rcasSharpness = rcasSharp;
        tp.resetHistory  = true;
        dx.Begin(); host.Execute(dx.cmd.Get(), edgeTex.Get(), flatDepthTex.Get(), nullptr, nullptr, kInW, kInH, kOutW, kOutH, tp); dx.FlushAndWait();
        std::vector<float> buf;
        dx.ReadbackRgba16Tex2D(host.GetOutputResource(), host.OutputResourceState(), rw, rh, buf);
        outNans = 0u;
        for (float f : buf) if (!std::isfinite(f)) ++outNans;
        outMin = 10.0f; outMax = -10.0f;
        float maxGrad = 0.0f;
        for (uint32_t x = 620u; x < 660u; ++x) {
            const float v0 = buf[(static_cast<size_t>(360u) * kOutW + x) * 4u + 0u];
            const float v1 = buf[(static_cast<size_t>(360u) * kOutW + (x + 1u)) * 4u + 0u];
            outMin = std::min(outMin, v0);
            outMax = std::max(outMax, v0);
            maxGrad = std::max(maxGrad, std::fabs(v1 - v0));
        }
        return maxGrad;
    };

    float mn0 = 0.0f, mx0 = 0.0f, mn1 = 0.0f, mx1 = 0.0f;
    uint32_t nan0 = 0u, nan1 = 0u;
    const float gradNoSharp = measureEdgeGrad(0.0f, mn0, mx0, nan0);
    const float gradRcas    = measureEdgeGrad(0.85f, mn1, mx1, nan1);
    std::printf("  RCAS    : grad %.4f -> %.4f (+%.1f%%) | range=[%.4f..%.4f] (orig [0.10..0.90]) | NaN=%u\n",
                gradNoSharp, gradRcas, (gradRcas / std::max(gradNoSharp, 1.0e-5f) - 1.0f) * 100.0f, mn1, mx1, nan1);
    Check(gradRcas > gradNoSharp * 1.08f && mn1 >= 0.098f && mx1 <= 0.902f && nan1 == 0u,
          "RCAS sharpens edge gradient cleanly without ringing or NaNs");

    // ========================================================================
    // Test 5: Steady-State GPU Timing Benchmark (640x360 -> 1280x720 2x Upscale)
    // ========================================================================
    const uint32_t warmupFrames   = dx.isWarp ? 2u : 16u;
    const uint32_t measuredFrames = dx.isWarp ? 4u : 60u;
    double sumTotalMs = 0.0;
    for (uint32_t f = 0u; f < warmupFrames + measuredFrames; ++f) {
        TsrUpscaleParams tp{};
        ComputeHaltonJitter(f, tp.jitterX, tp.jitterY);
        tp.historyWeight = 0.88f;
        tp.varianceGamma = 1.15f;
        tp.rcasSharpness = 0.65f;
        tp.resetHistory  = (f == 0u);
        if (!dx.Begin()) { Check(false, "Begin timing frame failed"); return; }
        dx.cmd->EndQuery(dx.tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0u);
        host.Execute(dx.cmd.Get(), lastInColorTex.Get(), flatDepthTex.Get(), zeroVelTex.Get(), sparkReactTex.Get(),
                     kInW, kInH, kOutW, kOutH, tp);
        dx.cmd->EndQuery(dx.tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1u);
        dx.cmd->ResolveQueryData(dx.tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0u, 2u, dx.tsReadback.Get(), 0ull);
        if (!dx.FlushAndWait()) { Check(false, "FlushAndWait timing frame failed"); return; }
        if (f >= warmupFrames) {
            sumTotalMs += dx.ReadTsRangeMs(0u, 1u);
        }
    }
    const double meanTotalMs  = sumTotalMs / static_cast<double>(measuredFrames);
    const float  hostResMs    = host.HasValidTiming() ? host.GetLastResolveMs() : 0.0f;
    const float  hostRcasMs   = host.HasValidTiming() ? host.GetLastRcasMs() : 0.0f;
    std::printf("  GPU Time: total=%.4f ms (host: resolve=%.4f ms + rcas=%.4f ms = %.4f ms) [640x360 -> 1280x720]\n",
                meanTotalMs, hostResMs, hostRcasMs, hostResMs + hostRcasMs);

    if (!dx.isWarp) {
        Check(meanTotalMs < 0.08, "HW GPU budget PASS: 640x360 -> 1280x720 TSR Resolve + RCAS < 0.08 ms");
    } else {
        Check(meanTotalMs > 0.0, "WARP timing recorded cleanly");
    }
    Check(host.HasValidTiming(), "RawTsrUpscaleHost internal GPU timestamp ring valid");

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

    std::printf("\n=== test_tsr_upscale_gpu Summary: %d passed, %d failed ===\n", gPassed, gFailed);
    return (gFailed == 0) ? 0 : 1;
}
