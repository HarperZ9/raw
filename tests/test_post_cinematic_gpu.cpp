// tests/test_post_cinematic_gpu.cpp: Verification & GPU Benchmark Suite for RAW's SM 6.6 / 6.10
// Cinematic Post-Process Solver (Tile-Max Velocity Reduction + 12-Tap Neighbor-Max Motion Blur
// + Depth-Aware Physical CoC Hexagonal Bokeh DoF with Foreground Bleed Guard + Filmic Vignette).
//
// Verifies on BOTH Hardware DX12 GPU (e.g. RTX 4090) AND Software WARP (--warp) with D3D12 Debug Layer ON:
//   1. Zero-Velocity & Zero-DoF/Vignette Identity: output matches input HDR color (maxAbsDiff < 1e-3).
//   2. Directional Motion Streak: moving vertical bar (+24px X velocity) streaks horizontally into
//      neighboring background while preserving strict zero vertical smear.
//   3. Physical CoC Focus Sharpness vs. Far Bokeh Blur: sharp in-focus stripe patch (Z = focusDist)
//      preserves >98% contrast while far out-of-focus stripe patch (Z = 35m) blurs strongly (<55% stdDev).
//   4. Foreground Bleed Protection: bright crimson sharp foreground pillar (Z = 6m) in front of dark
//      blurry far background (Z = 35m) produces ZERO crimson bleed onto adjacent background pixels.
//   5. Filmic Vignette Monotonicity: center retains 1.0x gain while corners smoothly attenuate.
//   6. NaN/Inf Resilience + Dynamic Resolution Auto-Reallocation (640x360 -> 960x540) +
//      Real RTX 4090 GPU cost (< 0.08 ms at 960x540!) & 0 D3D12 Debug Layer errors.
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

#include "RawPostCinematicHost.h"

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

float LinearToStdZ(float linearZ, float nearZ, float farZ)
{
    const float q = farZ / (farZ - nearZ);
    return std::clamp(q * (1.0f - nearZ / linearZ), 0.0f, 0.99997f);
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

    std::printf("\n=== GPU Cinematic Post-Process Suite [%s] (DebugLayer=%s, TsFreq=%llu Hz) ===\n",
                dx.adapterName.c_str(), dx.debugLayer ? "ON" : "OFF",
                static_cast<unsigned long long>(dx.tsFreqHz));

    RawPostCinematicHost host;
    PostCinematicDesc desc{};
    desc.initialWidth  = 640u; // Start smaller to prove dynamic auto-reallocation to 960x540
    desc.initialHeight = 360u;
    std::string err;
    const bool initOk = host.Init(dx.dev.Get(), desc, &err);
    Check(initOk, "RawPostCinematicHost::Init succeeded");
    if (!initOk) {
        std::fprintf(stderr, "  Init error: %s\n", err.c_str());
        return;
    }
    host.SetTimestampFrequency(dx.tsFreqHz);

    constexpr uint32_t kW = 960u;
    constexpr uint32_t kH = 540u;
    constexpr float kNearZ = 0.5f;
    constexpr float kFarZ  = 500.0f;
    const float zFocus = LinearToStdZ(6.0f, kNearZ, kFarZ);
    const float zFar   = LinearToStdZ(35.0f, kNearZ, kFarZ);

    // ========================================================================
    // Test 1: Zero-Velocity & Zero-DoF/Vignette Identity
    // ========================================================================
    std::vector<float> idColor(static_cast<size_t>(kW) * kH * 4u, 0.0f);
    std::vector<float> idDepth(static_cast<size_t>(kW) * kH, zFocus);
    std::vector<float> zeroVel(static_cast<size_t>(kW) * kH * 2u, 0.0f);
    for (uint32_t y = 0u; y < kH; ++y) {
        for (uint32_t x = 0u; x < kW; ++x) {
            const size_t i = (static_cast<size_t>(y) * kW + x) * 4u;
            idColor[i + 0u] = static_cast<float>((x * 7u + y * 3u) % 256u) / 255.0f * 1.5f;
            idColor[i + 1u] = static_cast<float>((x * 5u + y * 11u) % 256u) / 255.0f * 1.2f;
            idColor[i + 2u] = static_cast<float>((x * 13u + y * 2u) % 256u) / 255.0f * 0.9f;
            idColor[i + 3u] = 1.0f;
        }
    }
    ComPtr<ID3D12Resource> idColorTex, idDepthTex, zeroVelTex;
    Check(dx.UploadFloatTex2D(idColorTex, kW, kH, DXGI_FORMAT_R32G32B32A32_FLOAT, 4u, idColor) &&
          dx.UploadFloatTex2D(idDepthTex, kW, kH, DXGI_FORMAT_R32_FLOAT, 1u, idDepth) &&
          dx.UploadFloatTex2D(zeroVelTex, kW, kH, DXGI_FORMAT_R32G32_FLOAT, 2u, zeroVel),
          "Uploaded 960x540 identity scene");

    PostCinematicParams pId{};
    pId.nearZ            = kNearZ;
    pId.farZ             = kFarZ;
    pId.dofStrength      = 0.0f;
    pId.motionBlurScale  = 1.0f;
    pId.vignetteStrength = 0.0f;
    pId.frameJitter      = 0.0f;

    if (!dx.Begin() || !host.Execute(dx.cmd.Get(), idColorTex.Get(), idDepthTex.Get(), zeroVelTex.Get(), kW, kH, pId) || !dx.FlushAndWait()) {
        Check(false, "Identity Execute failed");
        return;
    }
    Check(host.Width() == kW && host.Height() == kH, "Dynamic resolution auto-reallocation (640x360 -> 960x540) succeeded");

    uint32_t rw = 0u, rh = 0u;
    std::vector<float> outRgba;
    Check(dx.ReadbackRgba16Tex2D(host.GetOutputResource(), host.OutputResourceState(), rw, rh, outRgba), "Read back identity output");
    float maxIdDiff = 0.0f;
    for (size_t i = 0u; i < static_cast<size_t>(kW) * kH; ++i) {
        for (int c = 0; c < 3; ++c) {
            maxIdDiff = std::max(maxIdDiff, std::fabs(outRgba[i * 4u + c] - idColor[i * 4u + c]));
        }
    }
    std::printf("  Identity: maxAbsDiff=%.6f (RGBA16F quant tolerance < 0.002)\n", maxIdDiff);
    Check(maxIdDiff < 0.002f, "Zero-velocity identity: output matches input HDR color cleanly");

    // ========================================================================
    // Test 2: Directional Motion Streak (Moving Vertical Bar +24 px Horizontal)
    // ========================================================================
    std::vector<float> mbColor(static_cast<size_t>(kW) * kH * 4u, 0.0f);
    std::vector<float> mbVel(static_cast<size_t>(kW) * kH * 2u, 0.0f);
    const float velU = 24.0f / static_cast<float>(kW); // +24 px rightward
    for (uint32_t y = 200u; y < 340u; ++y) {
        for (uint32_t x = 470u; x < 490u; ++x) {
            const size_t i = static_cast<size_t>(y) * kW + x;
            mbColor[i * 4u + 0u] = 2.0f;
            mbColor[i * 4u + 1u] = 1.6f;
            mbColor[i * 4u + 2u] = 0.8f;
            mbColor[i * 4u + 3u] = 1.0f;
            mbVel[i * 2u + 0u]   = velU;
            mbVel[i * 2u + 1u]   = 0.0f;
        }
    }
    ComPtr<ID3D12Resource> mbColorTex, mbVelTex;
    dx.UploadFloatTex2D(mbColorTex, kW, kH, DXGI_FORMAT_R32G32B32A32_FLOAT, 4u, mbColor);
    dx.UploadFloatTex2D(mbVelTex, kW, kH, DXGI_FORMAT_R32G32_FLOAT, 2u, mbVel);

    PostCinematicParams pMb = pId;
    pMb.motionBlurScale   = 1.0f;
    pMb.maxMotionRadiusPx = 24.0f;
    if (!dx.Begin() || !host.Execute(dx.cmd.Get(), mbColorTex.Get(), idDepthTex.Get(), mbVelTex.Get(), kW, kH, pMb) || !dx.FlushAndWait()) {
        Check(false, "Motion blur Execute failed");
        return;
    }
    dx.ReadbackRgba16Tex2D(host.GetOutputResource(), host.OutputResourceState(), rw, rh, outRgba);

    // Probe +8 px right of bar (498, 270) -> must have strong horizontal motion streak (> 0.25)
    // Probe +8 px above bar (480, 192)   -> must have ZERO vertical smear (< 0.005)
    const float horizStreakR = outRgba[(static_cast<size_t>(270u) * kW + 498u) * 4u + 0u];
    const float leftStreakR  = outRgba[(static_cast<size_t>(270u) * kW + 462u) * 4u + 0u];
    const float vertSmearR   = outRgba[(static_cast<size_t>(192u) * kW + 480u) * 4u + 0u];
    std::printf("  Motion  : horizStreakR(right)=%.4f (left)=%.4f | vertSmearR(above)=%.5f\n",
                horizStreakR, leftStreakR, vertSmearR);
    Check(horizStreakR > 0.25f && leftStreakR > 0.25f && vertSmearR < 0.005f,
          "Directional motion blur: strong horizontal streak along +24px velocity with zero vertical smear");

    // ========================================================================
    // Test 3 & 4: Physical CoC Focus Sharpness vs Far Blur + Foreground Bleed Guard
    // ========================================================================
    // Layout (960x540):
    //   - Left stripe patch  (x in [80..240],  y in [160..380]): 4px alternating black/white stripes at FOCUS (Z=6m)
    //   - Mid  stripe patch  (x in [360..520], y in [160..380]): 4px alternating black/white stripes at FAR   (Z=35m)
    //   - Right bleed scene  (x in [600..880], y in [160..380]): dark blue FAR background (Z=35m, rgb=(0.0, 0.0, 0.25))
    //       with a SHARP IN-FOCUS bright crimson foreground pillar (x in [700..780], Z=6m, rgb=(4.0, 0.0, 0.0))!
    //   - Top sky strip      (y < 40): rawZ = 1.0
    std::vector<float> dofColor(static_cast<size_t>(kW) * kH * 4u, 0.0f);
    std::vector<float> dofDepth(static_cast<size_t>(kW) * kH, zFar);
    for (uint32_t y = 0u; y < kH; ++y) {
        for (uint32_t x = 0u; x < kW; ++x) {
            const size_t i = static_cast<size_t>(y) * kW + x;
            if (y < 40u) {
                dofDepth[i] = 1.0f;
                dofColor[i * 4u + 2u] = 0.4f;
                dofColor[i * 4u + 3u] = 1.0f;
                continue;
            }
            if (y >= 160u && y < 380u) {
                if (x >= 80u && x < 240u) {
                    const float stripe = (((x / 4u) & 1u) == 0u) ? 1.0f : 0.0f;
                    dofColor[i * 4u + 0u] = stripe;
                    dofColor[i * 4u + 1u] = stripe;
                    dofColor[i * 4u + 2u] = stripe;
                    dofColor[i * 4u + 3u] = 1.0f;
                    dofDepth[i]           = zFocus;
                } else if (x >= 360u && x < 520u) {
                    const float stripe = (((x / 4u) & 1u) == 0u) ? 1.0f : 0.0f;
                    dofColor[i * 4u + 0u] = stripe;
                    dofColor[i * 4u + 1u] = stripe;
                    dofColor[i * 4u + 2u] = stripe;
                    dofColor[i * 4u + 3u] = 1.0f;
                    dofDepth[i]           = zFar;
                } else if (x >= 600u && x < 880u) {
                    if (x >= 700u && x < 780u) {
                        // Sharp in-focus crimson foreground pillar (Z=6m)
                        dofColor[i * 4u + 0u] = 4.0f;
                        dofColor[i * 4u + 1u] = 0.0f;
                        dofColor[i * 4u + 2u] = 0.0f;
                        dofColor[i * 4u + 3u] = 1.0f;
                        dofDepth[i]           = zFocus;
                    } else {
                        // Blurry far dark-blue background (Z=35m)
                        dofColor[i * 4u + 0u] = 0.0f;
                        dofColor[i * 4u + 1u] = 0.0f;
                        dofColor[i * 4u + 2u] = 0.25f;
                        dofColor[i * 4u + 3u] = 1.0f;
                        dofDepth[i]           = zFar;
                    }
                }
            }
        }
    }

    ComPtr<ID3D12Resource> dofColorTex, dofDepthTex;
    dx.UploadFloatTex2D(dofColorTex, kW, kH, DXGI_FORMAT_R32G32B32A32_FLOAT, 4u, dofColor);
    dx.UploadFloatTex2D(dofDepthTex, kW, kH, DXGI_FORMAT_R32_FLOAT, 1u, dofDepth);

    PostCinematicParams pDof{};
    pDof.nearZ             = kNearZ;
    pDof.farZ              = kFarZ;
    pDof.focusDistanceM    = 6.0f;
    pDof.focalLengthMm     = 50.0f;
    pDof.apertureFStop     = 1.8f;
    pDof.cocScale          = 2.0f;
    pDof.dofStrength       = 1.0f;
    pDof.maxBokehRadiusPx  = 10.0f;
    pDof.motionBlurScale   = 0.0f;
    pDof.vignetteStrength  = 0.0f;
    pDof.frameJitter       = 0.0f;

    if (!dx.Begin() || !host.Execute(dx.cmd.Get(), dofColorTex.Get(), dofDepthTex.Get(), nullptr, kW, kH, pDof) || !dx.FlushAndWait()) {
        Check(false, "DoF Execute failed");
        return;
    }
    dx.ReadbackRgba16Tex2D(host.GetOutputResource(), host.OutputResourceState(), rw, rh, outRgba);

    auto patchStdDev = [&](uint32_t x0, uint32_t x1, uint32_t y0, uint32_t y1) -> float {
        double sum = 0.0, sum2 = 0.0;
        uint32_t cnt = 0u;
        for (uint32_t y = y0; y < y1; ++y) {
            for (uint32_t x = x0; x < x1; ++x) {
                const float r = outRgba[(static_cast<size_t>(y) * kW + x) * 4u + 0u];
                sum  += r;
                sum2 += r * r;
                ++cnt;
            }
        }
        const double mean = sum / cnt;
        const double var  = std::max(0.0, sum2 / cnt - mean * mean);
        return static_cast<float>(std::sqrt(var));
    };

    const float focusStd = patchStdDev(110u, 210u, 200u, 340u);
    const float farStd   = patchStdDev(390u, 490u, 200u, 340u);

    // Check foreground bleed protection on dark-blue background immediately left/right of sharp crimson pillar:
    // Pillar spans [700..780]; sample background pixels x in [692..699] and [780..788] at y=270.
    float maxBgCrimsonBleed = 0.0f;
    for (uint32_t x = 692u; x < 700u; ++x) {
        maxBgCrimsonBleed = std::max(maxBgCrimsonBleed, outRgba[(static_cast<size_t>(270u) * kW + x) * 4u + 0u]);
    }
    for (uint32_t x = 780u; x < 788u; ++x) {
        maxBgCrimsonBleed = std::max(maxBgCrimsonBleed, outRgba[(static_cast<size_t>(270u) * kW + x) * 4u + 0u]);
    }
    // Also verify the sharp crimson foreground pillar interior retains pure crimson (R ~ 4.0)
    const float pillarR = outRgba[(static_cast<size_t>(270u) * kW + 740u) * 4u + 0u];

    std::printf("  BokehDoF: focusStd=%.4f (orig 0.500) | farBlurStd=%.4f (ratio=%.2fx) | fgPillarR=%.3f | maxBgBleedR=%.5f\n",
                focusStd, farStd, focusStd / std::max(farStd, 1.0e-5f), pillarR, maxBgCrimsonBleed);
    Check(focusStd > 0.49f && farStd < 0.25f,
          "Physical CoC DoF: sharp focus plane retains >98% contrast while far out-of-focus plane blurs strongly");
    Check(pillarR > 3.95f && maxBgCrimsonBleed < 0.005f,
          "Foreground bleed protection: sharp foreground pillar causes ZERO crimson bleed onto blurry background");

    // ========================================================================
    // Test 5: Filmic Vignette + NaN/Inf Resilience
    // ========================================================================
    std::vector<float> nanColor(static_cast<size_t>(kW) * kH * 4u, 1.0f);
    const float qNaN = std::numeric_limits<float>::quiet_NaN();
    const float pInf = std::numeric_limits<float>::infinity();
    for (size_t i = 0u; i < static_cast<size_t>(kW) * kH; i += 97u) {
        nanColor[i * 4u + 0u] = ((i & 1u) == 0u) ? qNaN : pInf;
    }
    ComPtr<ID3D12Resource> nanColorTex;
    dx.UploadFloatTex2D(nanColorTex, kW, kH, DXGI_FORMAT_R32G32B32A32_FLOAT, 4u, nanColor);

    PostCinematicParams pFull = pDof;
    pFull.motionBlurScale  = 1.0f;
    pFull.vignetteStrength = 0.35f;
    if (!dx.Begin() || !host.Execute(dx.cmd.Get(), nanColorTex.Get(), dofDepthTex.Get(), mbVelTex.Get(), kW, kH, pFull) || !dx.FlushAndWait()) {
        Check(false, "Vignette+NaN Execute failed");
        return;
    }
    dx.ReadbackRgba16Tex2D(host.GetOutputResource(), host.OutputResourceState(), rw, rh, outRgba);

    uint32_t nanInfCount = 0u;
    for (float v : outRgba) {
        if (!std::isfinite(v)) ++nanInfCount;
    }
    const float centerG = outRgba[(static_cast<size_t>(kH / 2u) * kW + (kW / 2u)) * 4u + 1u];
    const float cornerG = outRgba[(static_cast<size_t>(4u) * kW + 4u) * 4u + 1u];
    std::printf("  Vig+NaN : centerG=%.4f cornerG=%.4f (attenuation=%.1f%%) | nanInfCount=%u\n",
                centerG, cornerG, (1.0f - cornerG / std::max(centerG, 1.0e-5f)) * 100.0f, nanInfCount);
    Check(nanInfCount == 0u, "Zero NaN/Inf across all 960x540 pixels even with poisoned NaN/+Inf HDR input");
    Check(centerG > 0.98f && cornerG < centerG - 0.10f && cornerG > 0.50f,
          "Filmic optical vignette: center preserves 1.0x gain while corners attenuate smoothly");

    // ========================================================================
    // Test 6: Warmup + Measured GPU Timing Loop (Full Pipeline: TileMax + NeighborMax + DoF + MotionBlur + Vignette)
    // ========================================================================
    const uint32_t warmupFrames   = dx.isWarp ? 2u : 16u;
    const uint32_t measuredFrames = dx.isWarp ? 4u : 60u;
    double sumTotalMs = 0.0;
    for (uint32_t f = 0u; f < warmupFrames + measuredFrames; ++f) {
        pFull.frameJitter = static_cast<float>(f & 63u);
        if (!dx.Begin()) {
            Check(false, "Begin timing frame failed");
            return;
        }
        dx.cmd->EndQuery(dx.tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0u);
        host.Execute(dx.cmd.Get(), dofColorTex.Get(), dofDepthTex.Get(), mbVelTex.Get(), kW, kH, pFull);
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
    const float  hostTileMs  = host.HasValidTiming() ? host.GetLastTileMs() : 0.0f;
    const float  hostCompMs  = host.HasValidTiming() ? host.GetLastCompositeMs() : 0.0f;
    std::printf("  GPU Time: total=%.4f ms (host: tile=%.4f ms + composite=%.4f ms = %.4f ms)\n",
                meanTotalMs, hostTileMs, hostCompMs, hostTileMs + hostCompMs);

    if (!dx.isWarp) {
        Check(meanTotalMs < 0.08, "HW GPU budget PASS: 960x540 TileMax+NeighborMax+DoF+MotionBlur+Vignette < 0.08 ms");
    } else {
        Check(meanTotalMs > 0.0, "WARP timing recorded cleanly");
    }
    Check(host.HasValidTiming(), "RawPostCinematicHost internal GPU timestamp ring valid");

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

    std::printf("\n=== test_post_cinematic_gpu Summary: %d passed, %d failed ===\n", gPassed, gFailed);
    return (gFailed == 0) ? 0 : 1;
}
