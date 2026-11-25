// tests/test_snow_deform_gpu.cpp: Hardware DX12 + WARP GPU Verification Suite for
// RawSnowDeformHost & RawSnowDeformSim.hlsl (512x512 Snow Accumulation, Campfire/Solar
// Melt, Slush-to-Black-Ice Refreeze & Mass-Conserving Footprint Deformation, SM 6.6 / 6.10).
//
// Verifies on BOTH Hardware GPU (RTX 4090) and DX12 WARP with D3D12 Debug Layer ON:
//   1. Mass-conserving footprint stamp depresses central trench AND raises outer compacted rim
//      while preserving total 2D snow volume within tight conservation bounds.
//   2. Analytical 3x3 Sobel normal map aligns accurately with footprint trench wall slopes
//      (-X wall tilts +X, +X wall tilts -X, flat outer snow has exact (0, 1, 0) normal).
//   3. Circular campfire thermal heat source melts snow into wet slush, and subsequent
//      sub-zero cooling freezes wet slush into glossy black-ice glaze.
//   4. Seasonal snowfall replenishment smoothly increases depth and restores depressed tracks.
//   5. World-anchored toroidal scrolling shifts existing footprints accurately across camera motion
//      while cleanly initializing newly entered border snow.
//   6. Zero NaN/Inf across all 512x512 state and normal texels + RTX 4090 GPU timing < 0.05 ms
//      + strict 0 D3D12 Debug Layer errors.
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

#include "../src/d3d12_bridge/RawSnowDeformHost.h"

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

int gPass = 0;
int gFail = 0;

void Check(bool ok, const char* msg)
{
    if (ok) {
        ++gPass;
        std::printf("  [PASS] %s\n", msg);
    } else {
        ++gFail;
        std::printf("  [FAIL] %s\n", msg);
    }
}

float HalfToFloat(uint16_t h)
{
    const uint32_t s = (static_cast<uint32_t>(h) & 0x8000u) << 16u;
    const uint32_t e = (static_cast<uint32_t>(h) >> 10u) & 0x1Fu;
    const uint32_t m = static_cast<uint32_t>(h) & 0x03FFu;
    uint32_t bits = 0u;
    if (e == 0u) {
        if (m != 0u) {
            uint32_t shift = 0u;
            uint32_t mant  = m;
            while ((mant & 0x0400u) == 0u) {
                mant <<= 1u;
                ++shift;
            }
            mant &= 0x03FFu;
            bits = s | ((127u - 14u - shift) << 23u) | (mant << 13u);
        } else {
            bits = s;
        }
    } else if (e == 31u) {
        bits = s | 0x7F800000u | (m << 13u);
    } else {
        bits = s | ((e + 112u) << 23u) | (m << 13u);
    }
    float f = 0.0f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

std::string WideToUtf8(const wchar_t* ws)
{
    if (!ws || !*ws) return {};
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

    bool Init(bool useWarp, bool enableDebug)
    {
        if (enableDebug) {
            ComPtr<ID3D12Debug> dbg;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) {
                dbg->EnableDebugLayer();
                debugLayer = true;
            }
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
            auto* msg = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
            if (SUCCEEDED(infoQueue->GetMessage(i, msg, &len))) {
                if (msg->Severity == D3D12_MESSAGE_SEVERITY_ERROR ||
                    msg->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION) {
                    std::fprintf(stderr, "  [D3D12 ERROR] %s\n", msg->pDescription ? msg->pDescription : "(null)");
                    ++errs;
                }
            }
        }
        infoQueue->ClearStoredMessages();
        return errs;
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

        D3D12_TEXTURE_COPY_LOCATION srcLoc{tex, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {0u}};
        D3D12_TEXTURE_COPY_LOCATION dstLoc{};
        dstLoc.pResource       = rb.Get();
        dstLoc.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dstLoc.PlacedFootprint = fp;
        cmd->CopyTextureRegion(&dstLoc, 0u, 0u, 0u, &srcLoc, nullptr);

        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        b.Transition.StateAfter  = curState;
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
                const size_t dst = (static_cast<size_t>(y) * outW + x) * 4u;
                outRgba[dst + 0u] = HalfToFloat(row[x * 4u + 0u]);
                outRgba[dst + 1u] = HalfToFloat(row[x * 4u + 1u]);
                outRgba[dst + 2u] = HalfToFloat(row[x * 4u + 2u]);
                outRgba[dst + 3u] = HalfToFloat(row[x * 4u + 3u]);
            }
        }
        const D3D12_RANGE wr{0u, 0u};
        rb->Unmap(0u, &wr);
        return true;
    }
};

void RunSuiteOnAdapter(bool useWarp, bool enableDebug)
{
    TestDx12 dx{};
    if (!dx.Init(useWarp, enableDebug)) {
        Check(false, useWarp ? "WARP D3D12 device init" : "Hardware DX12 device init");
        return;
    }

    std::printf("\n=== RAW Snow Deformation, Thermal Melt & Footprint Suite [%s] (DebugLayer=%s, TsFreq=%llu Hz) ===\n",
                dx.adapterName.c_str(), dx.debugLayer ? "ON" : "OFF",
                static_cast<unsigned long long>(dx.tsFreqHz));

    RawSnowDeformHost host;
    SnowDeformDesc desc{};
    desc.gridWidth  = 512u;
    desc.gridHeight = 512u;
    desc.worldSpanM = 64.0f; // 0.125 m/texel
    std::string err;
    const bool initOk = host.Init(dx.dev.Get(), desc, &err);
    Check(initOk, "RawSnowDeformHost::Init succeeded (512x512, 64m world span)");
    if (!initOk) {
        std::fprintf(stderr, "  Init error: %s\n", err.c_str());
        return;
    }
    host.SetTimestampFrequency(dx.tsFreqHz);

    auto stepHost = [&](const SnowDeformParams& p) -> bool {
        return dx.Begin() && host.Execute(dx.cmd.Get(), p) && dx.FlushAndWait();
    };

    uint32_t rw = 0u, rh = 0u;
    std::vector<float> stateBuf;
    std::vector<float> normBuf;

    auto at = [&](const std::vector<float>& buf, uint32_t x, uint32_t y, uint32_t c) -> float {
        return buf[(static_cast<size_t>(y) * rw + x) * 4u + c];
    };

    // ========================================================================
    // Test 1: Mass-Conserving Footprint Stamp (Trench + Raised Rim + Volume Conservation)
    // ========================================================================
    SnowDeformParams pReset{};
    pReset.camWorldX       = 0.0f;
    pReset.camWorldZ       = 0.0f;
    pReset.dtSec           = 0.0f;
    pReset.ambientTempC    = -8.0f;
    pReset.baselineDepthM  = 0.40f;
    pReset.windSpeedMps    = 0.0f;
    pReset.resetField      = true;
    Check(stepHost(pReset), "Initial 0.40 m snowpack baseline reset");

    // Stamp a boot footprint at world (0.0, 0.0) with rimRatio = 1.0 (pure mass conservation probe)
    SnowStamp boot{};
    boot.worldX         = 0.0f;
    boot.worldZ         = 0.0f;
    boot.radiusM        = 1.0f;  // 8 texels radius for crisp Sobel & volume measurement
    boot.compressDepthM = 0.24f;
    boot.rimRatio       = 1.0f;
    boot.headingRad     = 0.0f;
    boot.aspectRatio    = 1.0f;

    SnowDeformParams pStamp = pReset;
    pStamp.resetField = false;
    pStamp.stamps     = &boot;
    pStamp.stampCount = 1u;
    Check(stepHost(pStamp), "Footprint stamp dispatch executed");

    dx.ReadbackRgba16Tex2D(host.GetStateResource(), host.StateResourceState(), rw, rh, stateBuf);
    dx.ReadbackRgba16Tex2D(host.GetNormalResource(), host.NormalResourceState(), rw, rh, normBuf);

    // Texel (256, 256) sits right at world (+0.0625, +0.0625) inside the footprint center
    const float trenchH    = at(stateBuf, 256u, 256u, 0u);
    const float trenchComp = at(stateBuf, 256u, 256u, 1u);
    // Rim peak is at r = 1.325 m -> ~10.6 texels from center (256 + 10 = 266)
    const float rimH       = at(stateBuf, 266u, 256u, 0u);
    const float farFlatH   = at(stateBuf, 64u, 64u, 0u);

    // Integrate total snow depth delta over a 48x48 box surrounding the footprint (center 256,256)
    double volumeBefore = 0.0;
    double volumeAfter  = 0.0;
    for (uint32_t y = 232u; y <= 280u; ++y) {
        for (uint32_t x = 232u; x <= 280u; ++x) {
            volumeBefore += 0.40;
            volumeAfter  += static_cast<double>(at(stateBuf, x, y, 0u));
        }
    }
    const double volErrPct = std::fabs(volumeAfter - volumeBefore) / volumeBefore * 100.0;
    std::printf("  Stamp   : trenchH=%.4f m (comp=%.3f) | rimH=%.4f m | flatH=%.4f m | massErr=%.3f%%\n",
                trenchH, trenchComp, rimH, farFlatH, volErrPct);
    Check(trenchH < 0.20f && trenchComp > 0.15f && rimH > 0.45f && std::fabs(farFlatH - 0.40f) < 0.003f && volErrPct < 1.0,
          "Footprint stamp depresses trench, raises compacted rim, and conserves snow mass (< 1% error)");

    // ========================================================================
    // Test 2: Analytical 3x3 Sobel Normal Alignment with Footprint Wall Slopes
    // ========================================================================
    // Left inner wall of trench (x=251, y=256): height decreases toward +X -> normal tilts +X (nx > 0)
    // Right inner wall of trench (x=261, y=256): height increases toward +X -> normal tilts -X (nx < 0)
    // Top inner wall of trench (x=256, y=251): height decreases toward +Z -> normal tilts +Z (nz > 0)
    // Bottom inner wall of trench (x=256, y=261): height increases toward +Z -> normal tilts -Z (nz < 0)
    const float leftWallNx   = at(normBuf, 251u, 256u, 0u);
    const float rightWallNx  = at(normBuf, 261u, 256u, 0u);
    const float topWallNz    = at(normBuf, 256u, 251u, 2u);
    const float bottomWallNz = at(normBuf, 256u, 261u, 2u);
    const float flatNx       = at(normBuf, 64u, 64u, 0u);
    const float flatNy       = at(normBuf, 64u, 64u, 1u);
    const float flatNz       = at(normBuf, 64u, 64u, 2u);
    std::printf("  Normals : leftNx=%+.3f rightNx=%+.3f topNz=%+.3f botNz=%+.3f | flat=(%.3f,%.3f,%.3f)\n",
                leftWallNx, rightWallNx, topWallNz, bottomWallNz, flatNx, flatNy, flatNz);
    Check(leftWallNx > 0.12f && rightWallNx < -0.12f && topWallNz > 0.12f && bottomWallNz < -0.12f &&
          std::fabs(flatNx) < 0.005f && std::fabs(flatNy - 1.0f) < 0.005f && std::fabs(flatNz) < 0.005f,
          "Sobel normal map aligns accurately with footprint trench walls and flat snow (0,1,0)");

    // ========================================================================
    // Test 3: Campfire Thermal Melt -> Wet Slush -> Sub-Zero Black-Ice Refreeze
    // ========================================================================
    SnowHeatSource campfire{};
    campfire.worldX     = 8.0f; // 64 texels +X from center -> texel (320, 256)
    campfire.worldZ     = 0.0f;
    campfire.radiusM    = 3.5f;
    campfire.heatPowerC = 65.0f;

    SnowDeformParams pMelt = pReset;
    pMelt.resetField   = false;
    pMelt.dtSec        = 0.20f;
    pMelt.ambientTempC = -4.0f;
    pMelt.meltCoeff    = 0.015f;
    pMelt.heatSources  = &campfire;
    pMelt.heatCount    = 1u;

    for (uint32_t i = 0u; i < 6u; ++i) {
        stepHost(pMelt);
    }
    dx.ReadbackRgba16Tex2D(host.GetStateResource(), host.StateResourceState(), rw, rh, stateBuf);
    const float fireCenterH     = at(stateBuf, 320u, 256u, 0u);
    const float fireRingSlush   = at(stateBuf, 336u, 256u, 2u); // 2.0 m from campfire center
    const float coldControlH    = at(stateBuf, 64u, 64u, 0u);
    const float coldControlSlush = at(stateBuf, 64u, 64u, 2u);

    // Extinguish campfire and refreeze slush under -18 C arctic night
    SnowDeformParams pFreeze = pReset;
    pFreeze.resetField   = false;
    pFreeze.dtSec        = 0.25f;
    pFreeze.ambientTempC = -18.0f;
    for (uint32_t i = 0u; i < 8u; ++i) {
        stepHost(pFreeze);
    }
    dx.ReadbackRgba16Tex2D(host.GetStateResource(), host.StateResourceState(), rw, rh, stateBuf);
    dx.ReadbackRgba16Tex2D(host.GetNormalResource(), host.NormalResourceState(), rw, rh, normBuf);
    const float frozenSlush    = at(stateBuf, 336u, 256u, 2u);
    const float frozenBlackIce = at(stateBuf, 336u, 256u, 3u);
    const float iceGlint       = at(normBuf, 336u, 256u, 3u);

    std::printf("  Thermal : fireH=%.4f m (ctrl=%.4f m) | ringSlush %.3f->%.3f | blackIce=%.3f glint=%.3f\n",
                fireCenterH, coldControlH, fireRingSlush, frozenSlush, frozenBlackIce, iceGlint);
    Check(fireCenterH < 0.05f && std::fabs(coldControlH - 0.40f) < 0.01f &&
          fireRingSlush > 0.35f && coldControlSlush < 0.01f &&
          frozenBlackIce > 0.40f && frozenSlush < fireRingSlush * 0.25f && iceGlint > 0.35f,
          "Campfire melts snow to wet slush; sub-zero night freezes slush into glossy black ice");

    // ========================================================================
    // Test 4: Seasonal Snowfall Replenishment
    // ========================================================================
    const float depthBeforeFall = at(stateBuf, 256u, 256u, 0u);
    SnowDeformParams pSnow = pReset;
    pSnow.resetField      = false;
    pSnow.dtSec           = 0.25f;
    pSnow.ambientTempC    = -10.0f;
    pSnow.snowfallRateMps = 0.08f; // 0.02 m per step x 10 steps = +0.20 m
    for (uint32_t i = 0u; i < 10u; ++i) {
        stepHost(pSnow);
    }
    dx.ReadbackRgba16Tex2D(host.GetStateResource(), host.StateResourceState(), rw, rh, stateBuf);
    const float depthAfterFall = at(stateBuf, 256u, 256u, 0u);
    const float flatAfterFall  = at(stateBuf, 64u, 64u, 0u);
    std::printf("  Snowfall: trench %.4f -> %.4f m | flat %.4f -> %.4f m\n",
                depthBeforeFall, depthAfterFall, coldControlH, flatAfterFall);
    Check(depthAfterFall > depthBeforeFall + 0.18f && flatAfterFall > 0.58f && flatAfterFall < 0.62f,
          "Seasonal snowfall smoothly replenishes snowpack depth (+0.20 m exact)");

    // ========================================================================
    // Test 5: World-Anchored Toroidal Scrolling Preserves Footprint World Position
    // ========================================================================
    // Move camera +4.0 m in X (+32 texels). Previous footprint at world (0,0) shifts from (256,256) to (224,256)!
    SnowDeformParams pScroll = pReset;
    pScroll.resetField     = false;
    pScroll.camWorldX      = 4.0f;
    pScroll.camWorldZ      = 0.0f;
    pScroll.dtSec          = 0.0f;
    pScroll.baselineDepthM = 0.40f;
    stepHost(pScroll);

    dx.ReadbackRgba16Tex2D(host.GetStateResource(), host.StateResourceState(), rw, rh, stateBuf);
    dx.ReadbackRgba16Tex2D(host.GetNormalResource(), host.NormalResourceState(), rw, rh, normBuf);
    const float shiftedTrenchH = at(stateBuf, 224u, 256u, 0u);
    const float freshBorderH   = at(stateBuf, 500u, 256u, 0u); // Newly entered +X border strip

    uint32_t nanInfCount = 0u;
    for (float f : stateBuf) if (!std::isfinite(f)) ++nanInfCount;
    for (float f : normBuf)  if (!std::isfinite(f)) ++nanInfCount;

    std::printf("  Scroll  : shiftedTrench(224,256)=%.4f m (prev=%.4f m) | freshBorder(500,256)=%.4f m | NaN=%u\n",
                shiftedTrenchH, depthAfterFall, freshBorderH, nanInfCount);
    Check(std::fabs(shiftedTrenchH - depthAfterFall) < 0.005f &&
          std::fabs(freshBorderH - 0.40f) < 0.005f && nanInfCount == 0u,
          "Toroidal scroll preserves world-anchored footprint, initializes fresh border, 0 NaN/Inf");

    // ========================================================================
    // Test 6: Steady-State GPU Performance Benchmark (512x512, 32 stamps + 16 heat sources)
    // ========================================================================
    std::vector<SnowStamp> benchStamps(32u);
    for (uint32_t i = 0u; i < 32u; ++i) {
        const float ang = static_cast<float>(i) * 0.19634954f;
        benchStamps[i].worldX         = std::cos(ang) * 14.0f;
        benchStamps[i].worldZ         = std::sin(ang) * 14.0f;
        benchStamps[i].radiusM        = 0.35f;
        benchStamps[i].compressDepthM = 0.12f;
        benchStamps[i].rimRatio       = 0.88f;
        benchStamps[i].headingRad     = ang;
        benchStamps[i].aspectRatio    = 1.4f;
    }
    std::vector<SnowHeatSource> benchHeats(16u);
    for (uint32_t i = 0u; i < 16u; ++i) {
        const float ang = static_cast<float>(i) * 0.39269908f;
        benchHeats[i].worldX     = std::cos(ang) * 20.0f;
        benchHeats[i].worldZ     = std::sin(ang) * 20.0f;
        benchHeats[i].radiusM    = 3.5f;
        benchHeats[i].heatPowerC = 45.0f;
    }

    const uint32_t warmupFrames   = dx.isWarp ? 2u : 16u;
    const uint32_t measuredFrames = dx.isWarp ? 4u : 60u;
    double sumTotalMs = 0.0;
    for (uint32_t f = 0u; f < warmupFrames + measuredFrames; ++f) {
        SnowDeformParams tp{};
        tp.camWorldX       = 0.0f;
        tp.camWorldZ       = 0.0f;
        tp.dtSec           = 1.0f / 60.0f;
        tp.snowfallRateMps = 0.002f;
        tp.ambientTempC    = -6.0f;
        tp.solarWarmthC    = 1.0f;
        tp.baselineDepthM  = 0.40f;
        tp.windSpeedMps    = 2.5f;
        tp.resetField      = (f == 0u);
        tp.stamps          = benchStamps.data();
        tp.stampCount      = 32u;
        tp.heatSources     = benchHeats.data();
        tp.heatCount       = 16u;

        if (!dx.Begin()) { Check(false, "Begin timing frame failed"); return; }
        dx.cmd->EndQuery(dx.tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0u);
        host.Execute(dx.cmd.Get(), tp);
        dx.cmd->EndQuery(dx.tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1u);
        dx.cmd->ResolveQueryData(dx.tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0u, 2u, dx.tsReadback.Get(), 0ull);
        if (!dx.FlushAndWait()) { Check(false, "FlushAndWait timing frame failed"); return; }
        if (f >= warmupFrames) {
            sumTotalMs += dx.ReadTsRangeMs(0u, 1u);
        }
    }
    const double meanMs = sumTotalMs / static_cast<double>(measuredFrames);
    std::printf("  GPU Time: total=%.4f ms (host: step=%.4f ms + normal=%.4f ms = %.4f ms) [512x512, 32 stamps, 16 heats]\n",
                meanMs, host.GetLastStepMs(), host.GetLastNormalMs(), host.GetLastTotalMs());
    if (!dx.isWarp) {
        Check(meanMs < 0.05, "HW GPU budget PASS: 512x512 SnowDeform Step + Sobel Normal < 0.05 ms");
    } else {
        Check(meanMs > 0.0, "WARP timing recorded cleanly");
    }
    Check(host.HasValidTiming() && host.GetLastTotalMs() > 0.0f, "RawSnowDeformHost internal GPU timestamp ring valid");

    host.Shutdown();
    const uint32_t dbgErrs = dx.DrainDebugErrors();
    std::printf("  D3D12 Debug Layer Errors: %u\n", dbgErrs);
    Check(dbgErrs == 0u, "D3D12 Debug Layer reported 0 errors/corruptions");
}

} // namespace

int main(int argc, char** argv)
{
    bool warpOnly    = false;
    bool enableDebug = true;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--warp") == 0) warpOnly = true;
        if (std::strcmp(argv[i], "--no-debug") == 0) enableDebug = false;
    }

    if (!warpOnly) {
        RunSuiteOnAdapter(false, enableDebug);
    }
    RunSuiteOnAdapter(true, enableDebug);

    std::printf("\n=== test_snow_deform_gpu Summary: %d passed, %d failed ===\n", gPass, gFail);
    return (gFail == 0) ? 0 : 1;
}
