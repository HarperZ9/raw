// tools/raw_bench_suite.cpp: Studio-Grade Automated GPU/CPU Benchmark & Visual Invariant Suite for RAW.
// Benchmarks all 7 core GPU & CPU engine subsystems on Hardware DX12 GPU (e.g. RTX 4090) or WARP:
//   1. GPU 64^3 Navier-Stokes Fire/Smoke + 256^2 SWE Water (RawFluidSimHost)
//   2. GPU 4-Cascade Radiance Cascades GI (RawRadianceHost)
//   3. Procedural PBR Texture Forge (RawTextureForge, all 8 seamless PBR sets)
//   4. 48kHz FDN-8 Hadamard + Woodworth HRTF Audio DSP (RawAudioDsp)
//   5. 3D Voronoi CSG Destruction Physics (RawDestructionEngine)
//   6. Biomechanical 3D Flora & Hydraulic Terrain Erosion (RawFloraEngine + RawHydrologyGen + RawAssetValidator)
//   7. Multi-Cell Tamriel Streaming (RawTamrielWorldStreamer, graceful skip if Skyrim.esm absent)
//
// CLI:
//   raw_bench_suite [--warp] [--quick] [--frames N] [--json <path>] [--esm <path>] [--bsa <path>] [--no-debug-layer]
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

#include "../src/d3d12_bridge/RawFluidSimHost.h"
#include "../src/d3d12_bridge/RawRadianceHost.h"
#include "../src/materials/RawTextureForge.h"
#include "../src/audio/RawAudioDsp.h"
#include "../src/worldgen/RawDestructionEngine.h"
#include "../src/worldgen/RawFloraEngine.h"
#include "../src/worldgen/RawHydrologyGen.h"
#include "../src/worldgen/RawMeshPipeline.h"
#include "../src/worldgen/RawTamrielWorldStreamer.h"
#include "../src/worldgen/RawTerrainGen.h"
#include "../src/pipeline/RawAssetValidator.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

namespace
{

using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;

double ElapsedMs(Clock::time_point t0, Clock::time_point t1)
{
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

struct Percentiles {
    double mean = 0.0;
    double p50  = 0.0;
    double p95  = 0.0;
    double p99  = 0.0;
    double minV = 0.0;
    double maxV = 0.0;
};

Percentiles ComputePercentiles(std::vector<double> samples)
{
    Percentiles r{};
    if (samples.empty()) {
        return r;
    }
    std::sort(samples.begin(), samples.end());
    double sum = 0.0;
    for (double v : samples) {
        sum += v;
    }
    const size_t n = samples.size();
    auto atPct = [&](double pct) -> double {
        if (n == 1u) return samples[0];
        const double idx = pct * static_cast<double>(n - 1u);
        const size_t lo  = static_cast<size_t>(std::floor(idx));
        const size_t hi  = std::min(lo + 1u, n - 1u);
        const double t   = idx - static_cast<double>(lo);
        return samples[lo] * (1.0 - t) + samples[hi] * t;
    };
    r.mean = sum / static_cast<double>(n);
    r.p50  = atPct(0.50);
    r.p95  = atPct(0.95);
    r.p99  = atPct(0.99);
    r.minV = samples.front();
    r.maxV = samples.back();
    return r;
}

std::string JsonEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8u);
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out.push_back(c); break;
        }
    }
    return out;
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

uint16_t FloatToHalf(float f)
{
    uint32_t bits = 0u;
    std::memcpy(&bits, &f, sizeof(bits));
    const uint32_t sign = (bits >> 16) & 0x8000u;
    const int32_t  exp  = static_cast<int32_t>((bits >> 23) & 0xFFu) - 127 + 15;
    const uint32_t mant = bits & 0x007FFFFFu;
    if (exp <= 0) return static_cast<uint16_t>(sign);
    if (exp >= 31) return static_cast<uint16_t>(sign | 0x7C00u);
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | (mant >> 13));
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

// ----------------------------------------------------------------------------
// Minimal Self-Contained DX12 Rig with Real ID3D12QueryHeap GPU Timestamps
// ----------------------------------------------------------------------------
struct BenchDx12Rig {
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
    uint64_t                          vramBytes   = 0ull;
    bool                              isWarp      = false;
    bool                              debugLayer  = false;

    static constexpr uint32_t kMaxQueries = 16u;

    ~BenchDx12Rig()
    {
        if (fenceEvent) {
            CloseHandle(fenceEvent);
            fenceEvent = nullptr;
        }
    }

    bool Init(bool preferWarp, bool enableDebugLayer)
    {
        if (enableDebugLayer) {
            ComPtr<ID3D12Debug> dbg;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) {
                dbg->EnableDebugLayer();
                debugLayer = true;
            }
        }

        ComPtr<IDXGIFactory4> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
            return false;
        }

        if (!preferWarp) {
            ComPtr<IDXGIAdapter1> bestAdapter;
            DXGI_ADAPTER_DESC1 bestDesc{};
            for (UINT idx = 0u; ; ++idx) {
                ComPtr<IDXGIAdapter1> cand;
                if (factory->EnumAdapters1(idx, &cand) == DXGI_ERROR_NOT_FOUND) break;
                DXGI_ADAPTER_DESC1 desc{};
                cand->GetDesc1(&desc);
                if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0u) continue;
                ComPtr<ID3D12Device> testDev;
                if (SUCCEEDED(D3D12CreateDevice(cand.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&testDev)))) {
                    D3D12_FEATURE_DATA_SHADER_MODEL sm{D3D_SHADER_MODEL_6_6};
                    if (SUCCEEDED(testDev->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof sm)) &&
                        sm.HighestShaderModel >= D3D_SHADER_MODEL_6_6) {
                        if (!bestAdapter || desc.DedicatedVideoMemory > bestDesc.DedicatedVideoMemory) {
                            bestAdapter = cand;
                            bestDesc    = desc;
                            dev         = testDev;
                        }
                    }
                }
            }
            if (dev) {
                adapterName = WideToUtf8(bestDesc.Description);
                vramBytes   = static_cast<uint64_t>(bestDesc.DedicatedVideoMemory);
                isWarp      = false;
            }
        }

        if (!dev) {
            ComPtr<IDXGIAdapter> warp;
            if (FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))) ||
                FAILED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&dev)))) {
                return false;
            }
            ComPtr<IDXGIAdapter1> warp1;
            if (SUCCEEDED(warp.As(&warp1))) {
                DXGI_ADAPTER_DESC1 desc{};
                warp1->GetDesc1(&desc);
                adapterName = WideToUtf8(desc.Description);
            } else {
                adapterName = "Microsoft Basic Render Driver (WARP)";
            }
            vramBytes = 0ull;
            isWarp    = true;
        }

        if (debugLayer) {
            dev.As(&infoQueue);
        }

        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue))) ||
            FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))) ||
            FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&cmd))) ||
            FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
            return false;
        }
        queue->GetTimestampFrequency(&tsFreqHz);
        cmd->Close();

        D3D12_QUERY_HEAP_DESC qhd{};
        qhd.Type     = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        qhd.Count    = kMaxQueries;
        qhd.NodeMask = 0u;
        if (FAILED(dev->CreateQueryHeap(&qhd, IID_PPV_ARGS(&tsHeap)))) {
            return false;
        }

        D3D12_HEAP_PROPERTIES hpRb{};
        hpRb.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rbd{};
        rbd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        rbd.Width            = static_cast<UINT64>(kMaxQueries) * sizeof(uint64_t);
        rbd.Height           = 1u;
        rbd.DepthOrArraySize = 1u;
        rbd.MipLevels        = 1u;
        rbd.SampleDesc.Count = 1u;
        rbd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(dev->CreateCommittedResource(&hpRb, D3D12_HEAP_FLAG_NONE, &rbd,
                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tsReadback)))) {
            return false;
        }

        fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        return fenceEvent != nullptr;
    }

    bool BeginFrame()
    {
        return SUCCEEDED(alloc->Reset()) && SUCCEEDED(cmd->Reset(alloc.Get(), nullptr));
    }

    bool FlushAndWait()
    {
        if (FAILED(cmd->Close())) return false;
        ID3D12CommandList* lists[1] = {cmd.Get()};
        queue->ExecuteCommandLists(1, lists);
        ++fenceVal;
        if (FAILED(queue->Signal(fence.Get(), fenceVal))) return false;
        if (fence->GetCompletedValue() < fenceVal) {
            if (FAILED(fence->SetEventOnCompletion(fenceVal, fenceEvent))) return false;
            WaitForSingleObject(fenceEvent, 30000);
        }
        return fence->GetCompletedValue() >= fenceVal;
    }

    void MarkTimestamp(uint32_t queryIdx)
    {
        cmd->EndQuery(tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, queryIdx);
    }

    void ResolveTimestamps(uint32_t startIdx, uint32_t count)
    {
        cmd->ResolveQueryData(tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, startIdx, count,
                              tsReadback.Get(), static_cast<UINT64>(startIdx) * sizeof(uint64_t));
    }

    bool ReadTimestampRangeMs(uint32_t startIdx, uint32_t endIdx, double& outMs)
    {
        if (tsFreqHz == 0ull) return false;
        void* mapped = nullptr;
        D3D12_RANGE rr{0, static_cast<SIZE_T>(kMaxQueries) * sizeof(uint64_t)};
        if (FAILED(tsReadback->Map(0, &rr, &mapped))) return false;
        const auto* ticks = static_cast<const uint64_t*>(mapped);
        const uint64_t t0 = ticks[startIdx];
        const uint64_t t1 = ticks[endIdx];
        D3D12_RANGE wr{0, 0};
        tsReadback->Unmap(0, &wr);
        if (t1 < t0) {
            outMs = 0.0;
            return true;
        }
        outMs = static_cast<double>(t1 - t0) * 1000.0 / static_cast<double>(tsFreqHz);
        return true;
    }

    uint32_t DrainDebugErrors()
    {
        if (!infoQueue) return 0u;
        uint32_t errCount = 0u;
        const UINT64 n = infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
        for (UINT64 i = 0u; i < n; ++i) {
            SIZE_T len = 0u;
            infoQueue->GetMessage(i, nullptr, &len);
            if (len == 0u) continue;
            std::vector<uint8_t> buf(len);
            auto* msg = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
            if (SUCCEEDED(infoQueue->GetMessage(i, msg, &len))) {
                if (msg->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION ||
                    msg->Severity == D3D12_MESSAGE_SEVERITY_ERROR) {
                    std::fprintf(stderr, "  [D3D12 ERROR] %s\n", msg->pDescription ? msg->pDescription : "(null)");
                    ++errCount;
                }
            }
        }
        infoQueue->ClearStoredMessages();
        return errCount;
    }

    bool UploadRgba16Tex2D(ComPtr<ID3D12Resource>& tex, uint32_t dim, const std::vector<float>& rgbaFloat)
    {
        D3D12_HEAP_PROPERTIES hpDef{};
        hpDef.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width            = dim;
        rd.Height           = dim;
        rd.DepthOrArraySize = 1u;
        rd.MipLevels        = 1u;
        rd.Format           = DXGI_FORMAT_R16G16B16A16_FLOAT;
        rd.SampleDesc.Count = 1u;

        if (!tex) {
            if (FAILED(dev->CreateCommittedResource(&hpDef, D3D12_HEAP_FLAG_NONE, &rd,
                                                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex)))) {
                return false;
            }
        } else {
            if (!BeginFrame()) return false;
            D3D12_RESOURCE_BARRIER b{};
            b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource   = tex.Get();
            b.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            b.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmd->ResourceBarrier(1, &b);
            if (!FlushAndWait()) return false;
        }

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        UINT numRows = 0u;
        UINT64 rowBytes = 0ull, totalBytes = 0ull;
        dev->GetCopyableFootprints(&rd, 0, 1, 0, &fp, &numRows, &rowBytes, &totalBytes);

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

        ComPtr<ID3D12Resource> upBuf;
        if (FAILED(dev->CreateCommittedResource(&hpUp, D3D12_HEAP_FLAG_NONE, &ubd,
                                                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upBuf)))) {
            return false;
        }
        void* mapped = nullptr;
        D3D12_RANGE rr{0, 0};
        if (FAILED(upBuf->Map(0, &rr, &mapped))) return false;
        auto* dstBase = static_cast<uint8_t*>(mapped) + fp.Offset;
        for (uint32_t y = 0u; y < dim; ++y) {
            auto* dstRow = reinterpret_cast<uint16_t*>(dstBase + static_cast<size_t>(y) * fp.Footprint.RowPitch);
            for (uint32_t x = 0u; x < dim; ++x) {
                const size_t src = (static_cast<size_t>(y) * dim + x) * 4u;
                dstRow[x * 4 + 0] = FloatToHalf(rgbaFloat[src + 0]);
                dstRow[x * 4 + 1] = FloatToHalf(rgbaFloat[src + 1]);
                dstRow[x * 4 + 2] = FloatToHalf(rgbaFloat[src + 2]);
                dstRow[x * 4 + 3] = FloatToHalf(rgbaFloat[src + 3]);
            }
        }
        upBuf->Unmap(0, nullptr);

        if (!BeginFrame()) return false;
        D3D12_TEXTURE_COPY_LOCATION dstLoc{};
        dstLoc.pResource        = tex.Get();
        dstLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = 0u;

        D3D12_TEXTURE_COPY_LOCATION srcLoc{};
        srcLoc.pResource       = upBuf.Get();
        srcLoc.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint = fp;
        cmd->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = tex.Get();
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &b);
        return FlushAndWait();
    }

    bool ReadbackRgba16Tex(ID3D12Resource* tex, D3D12_RESOURCE_STATES curState,
                           uint32_t& outW, uint32_t& outH, uint32_t& outD, std::vector<float>& outRgba)
    {
        const D3D12_RESOURCE_DESC rd = tex->GetDesc();
        outW = static_cast<uint32_t>(rd.Width);
        outH = rd.Height;
        outD = (rd.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) ? rd.DepthOrArraySize : 1u;

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        UINT numRows = 0u;
        UINT64 rowBytes = 0ull, totalBytes = 0ull;
        dev->GetCopyableFootprints(&rd, 0, 1, 0, &fp, &numRows, &rowBytes, &totalBytes);

        D3D12_HEAP_PROPERTIES hpRb{};
        hpRb.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rbd{};
        rbd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        rbd.Width            = totalBytes;
        rbd.Height           = 1u;
        rbd.DepthOrArraySize = 1u;
        rbd.MipLevels        = 1u;
        rbd.SampleDesc.Count = 1u;
        rbd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ComPtr<ID3D12Resource> rbBuf;
        if (FAILED(dev->CreateCommittedResource(&hpRb, D3D12_HEAP_FLAG_NONE, &rbd,
                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&rbBuf)))) {
            return false;
        }

        if (!BeginFrame()) return false;
        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = tex;
        b.Transition.StateBefore = curState;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &b);

        D3D12_TEXTURE_COPY_LOCATION srcLoc{};
        srcLoc.pResource        = tex;
        srcLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcLoc.SubresourceIndex = 0u;

        D3D12_TEXTURE_COPY_LOCATION dstLoc{};
        dstLoc.pResource       = rbBuf.Get();
        dstLoc.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dstLoc.PlacedFootprint = fp;
        cmd->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        b.Transition.StateAfter  = curState;
        cmd->ResourceBarrier(1, &b);
        if (!FlushAndWait()) return false;

        void* mapped = nullptr;
        D3D12_RANGE rr{0, static_cast<SIZE_T>(totalBytes)};
        if (FAILED(rbBuf->Map(0, &rr, &mapped))) return false;
        const auto* base = static_cast<const uint8_t*>(mapped) + fp.Offset;
        const size_t slicePitch = static_cast<size_t>(fp.Footprint.RowPitch) * numRows;
        outRgba.resize(static_cast<size_t>(outW) * outH * outD * 4u);
        for (uint32_t z = 0u; z < outD; ++z) {
            for (uint32_t y = 0u; y < outH; ++y) {
                const auto* row = reinterpret_cast<const uint16_t*>(
                    base + static_cast<size_t>(z) * slicePitch + static_cast<size_t>(y) * fp.Footprint.RowPitch);
                for (uint32_t x = 0u; x < outW; ++x) {
                    const size_t dst = ((static_cast<size_t>(z) * outH + y) * outW + x) * 4u;
                    outRgba[dst + 0] = HalfToFloat(row[x * 4 + 0]);
                    outRgba[dst + 1] = HalfToFloat(row[x * 4 + 1]);
                    outRgba[dst + 2] = HalfToFloat(row[x * 4 + 2]);
                    outRgba[dst + 3] = HalfToFloat(row[x * 4 + 3]);
                }
            }
        }
        D3D12_RANGE wr{0, 0};
        rbBuf->Unmap(0, &wr);
        return true;
    }
};

// ----------------------------------------------------------------------------
// Per-Subsystem Result Structures
// ----------------------------------------------------------------------------
struct FluidBenchResult {
    bool        passed           = false;
    Percentiles fireGpuMs{};
    Percentiles waterGpuMs{};
    Percentiles combinedGpuMs{};
    Percentiles cpuRecordMs{};
    uint32_t    fireNanCount     = 0u;
    uint32_t    waterNanCount    = 0u;
    float       peakTempK        = 0.0f;
    float       hostFireMs       = 0.0f;
    float       hostWaterMs      = 0.0f;
    uint32_t    pressureIters    = 0u;
    std::string note;
};

struct RadianceBenchResult {
    bool        passed           = false;
    Percentiles giGpuMs{};
    Percentiles cpuRecordMs{};
    uint32_t    nanCount         = 0u;
    float       openIrrad        = 0.0f;
    float       umbraIrrad       = 0.0f;
    float       linearScaleRatio = 0.0f;
    float       hostGiMs         = 0.0f;
    std::string note;
};

struct TextureBenchResult {
    bool        passed          = false;
    uint32_t    setsGenerated   = 0u;
    uint32_t    resolution      = 256u;
    double      totalMs         = 0.0;
    double      mpixelsPerSec   = 0.0;
    float       maxSeamDiff     = 0.0f;
    Percentiles perSetMs{};
    std::string note;
};

struct AudioBenchResult {
    bool        passed           = false;
    uint32_t    sampleRate       = 48000u;
    uint32_t    totalFrames      = 0u;
    double      audioDurationMs  = 0.0;
    double      totalRenderMs    = 0.0;
    double      headroomRatio    = 0.0;
    double      realtimeSpeedup  = 0.0;
    float       peakAbs          = 0.0f;
    float       rmsLevel         = 0.0f;
    uint32_t    nanCount         = 0u;
    Percentiles blockMs{};
    std::string note;
};

struct DestructionBenchResult {
    bool        passed            = false;
    uint32_t    archetypesTested  = 0u;
    uint32_t    totalShards       = 0u;
    double      maxVolErrorPct    = 0.0;
    Percentiles fractureMs{};
    Percentiles stepUs{};
    bool        allFinite         = false;
    std::string note;
};

struct FloraHydroBenchResult {
    bool        passed                = false;
    uint32_t    treesBuilt            = 0u;
    uint32_t    floraMeshlets         = 0u;
    uint32_t    floraValidationErrors = 0u;
    double      floraTotalMs          = 0.0;
    double      treesPerSec           = 0.0;
    Percentiles treeMs{};
    uint32_t    hydroCellsBuilt       = 0u;
    uint32_t    hydroMeshlets         = 0u;
    uint32_t    hydroValidationErrors = 0u;
    double      hydroTotalMs          = 0.0;
    double      cellsPerSec           = 0.0;
    Percentiles cellMs{};
    std::string note;
};

struct TamrielBenchResult {
    bool        passed               = false;
    bool        skipped              = false;
    uint32_t    indexedWorldCells    = 0u;
    uint32_t    indexedModels        = 0u;
    double      indexMs              = 0.0;
    double      riverwoodColdMs      = 0.0;
    uint32_t    riverwoodCells       = 0u;
    uint32_t    riverwoodMeshlets    = 0u;
    double      whiterunStreamMs     = 0.0;
    double      whiterunWarmHitRate  = 0.0;
    uint64_t    cacheHits            = 0ull;
    uint64_t    cacheMisses          = 0ull;
    std::string note;
};

// ----------------------------------------------------------------------------
// 1. GPU 64^3 Navier-Stokes Fire/Smoke + 256^2 SWE Water (RawFluidSimHost)
// ----------------------------------------------------------------------------
FluidBenchResult RunFluidBenchmark(BenchDx12Rig& rig, uint32_t warmupFrames, uint32_t measuredFrames)
{
    FluidBenchResult res{};
    raw::bridge::RawFluidSimHost host;
    std::string err;
    if (!host.Init(rig.dev.Get(), &err)) {
        res.note = "RawFluidSimHost::Init failed: " + err;
        return res;
    }
    host.SetTimestampFrequency(rig.tsFreqHz);
    host.SetTargetBudgetMs(rig.isWarp ? 2000.0f : 0.45f);

    raw::bridge::FluidFireParams fp{};
    fp.cellSize          = 4.0f;
    fp.dt                = 1.0f / 60.0f;
    fp.windVelW[0]       = 7.0f;
    fp.windVelW[1]       = 2.5f;
    fp.jacobiIterations  = 8u;
    fp.emitterCount      = 1u;
    fp.emitters[0].posW[0] = 128.0f;
    fp.emitters[0].posW[1] = 128.0f;
    fp.emitters[0].posW[2] = 44.0f;
    fp.emitters[0].radius        = 18.0f;
    fp.emitters[0].ignitionTempK = 2600.0f;
    fp.emitters[0].fuelRate      = 3.0f;
    fp.emitters[0].jetSpeed      = 40.0f;
    fp.emitters[0].curlStrength  = 14.0f;
    fp.emitters[0].sootSeedRate  = 0.45f;

    raw::bridge::FluidWaterParams wp{};
    wp.cellSize        = 8.0f;
    wp.dt              = 1.0f / 60.0f;
    wp.rainIntensity   = 0.35f;
    wp.trailCount      = 1u;
    wp.trails[0].posW[0]   = 1024.0f;
    wp.trails[0].posW[1]   = 1024.0f;
    wp.trails[0].strength  = 1.2f;
    wp.trails[0].velW[0]   = 45.0f;
    wp.trails[0].velW[1]   = 12.0f;
    wp.trails[0].radius    = 24.0f;

    std::vector<double> fireSamples, waterSamples, combSamples, cpuSamples;
    fireSamples.reserve(measuredFrames);
    waterSamples.reserve(measuredFrames);
    combSamples.reserve(measuredFrames);
    cpuSamples.reserve(measuredFrames);

    const uint32_t totalFrames = warmupFrames + measuredFrames;
    for (uint32_t f = 0u; f < totalFrames; ++f) {
        const float tSec = static_cast<float>(f) * fp.dt;
        fp.timeSec = tSec;
        wp.timeSec = tSec;
        wp.trails[0].posW[0] = 1024.0f + std::sin(tSec * 1.4f) * 180.0f;
        wp.trails[0].posW[1] = 1024.0f + std::cos(tSec * 1.1f) * 140.0f;

        const auto c0 = Clock::now();
        if (!rig.BeginFrame()) {
            res.note = "BeginFrame failed";
            return res;
        }
        rig.MarkTimestamp(0u);
        host.StepFire(rig.cmd.Get(), fp);
        rig.MarkTimestamp(1u);
        host.StepWater(rig.cmd.Get(), wp);
        rig.MarkTimestamp(2u);
        rig.ResolveTimestamps(0u, 3u);
        const auto c1 = Clock::now();

        if (!rig.FlushAndWait()) {
            res.note = "FlushAndWait failed";
            return res;
        }

        if (f >= warmupFrames) {
            double fMs = 0.0, wMs = 0.0, totMs = 0.0;
            rig.ReadTimestampRangeMs(0u, 1u, fMs);
            rig.ReadTimestampRangeMs(1u, 2u, wMs);
            rig.ReadTimestampRangeMs(0u, 2u, totMs);
            fireSamples.push_back(fMs);
            waterSamples.push_back(wMs);
            combSamples.push_back(totMs);
            cpuSamples.push_back(ElapsedMs(c0, c1));
        }
    }

    res.fireGpuMs     = ComputePercentiles(fireSamples);
    res.waterGpuMs    = ComputePercentiles(waterSamples);
    res.combinedGpuMs = ComputePercentiles(combSamples);
    res.cpuRecordMs   = ComputePercentiles(cpuSamples);

    const auto ht = host.GetLastTimings();
    res.hostFireMs    = ht.fireStepMs;
    res.hostWaterMs   = ht.waterStepMs;
    res.pressureIters = ht.activePressureIters;

    // Readback invariants (zero NaN/Inf across 64^3 fire thermo + 256^2 water state, real combustion temp rise)
    std::vector<float> fireData, waterData;
    uint32_t fw = 0u, fh = 0u, fd = 0u, ww = 0u, wh = 0u, wd = 0u;
    if (!rig.ReadbackRgba16Tex(host.FireThermoResource(), host.FireResourceState(), fw, fh, fd, fireData) ||
        !rig.ReadbackRgba16Tex(host.WaterStateResource(), host.WaterResourceState(), ww, wh, wd, waterData)) {
        res.note = "GPU fluid readback failed";
        return res;
    }
    for (size_t i = 0u; i < fireData.size(); i += 4u) {
        const float tK = fireData[i + 0u];
        if (!std::isfinite(tK) || !std::isfinite(fireData[i + 1u]) ||
            !std::isfinite(fireData[i + 2u]) || !std::isfinite(fireData[i + 3u])) {
            ++res.fireNanCount;
        } else if (tK > res.peakTempK) {
            res.peakTempK = tK;
        }
    }
    for (float v : waterData) {
        if (!std::isfinite(v)) ++res.waterNanCount;
    }

    const bool budgetOk = rig.isWarp ? true : (res.combinedGpuMs.mean < 0.50);
    const bool physOk   = (res.fireNanCount == 0u) && (res.waterNanCount == 0u) && (res.peakTempK > 450.0f);
    res.passed = budgetOk && physOk;
    if (!physOk) {
        res.note = "Fluid physics/NaN invariant failed";
    } else if (!budgetOk) {
        res.note = "HW GPU fluid combined mean exceeded 0.50 ms budget";
    } else {
        res.note = rig.isWarp ? "WARP correctness PASS" : "HW GPU < 0.50 ms budget PASS";
    }
    return res;
}

// ----------------------------------------------------------------------------
// 2. GPU 4-Cascade Radiance Cascades GI (RawRadianceHost)
// ----------------------------------------------------------------------------
RadianceBenchResult RunRadianceBenchmark(BenchDx12Rig& rig, uint32_t warmupFrames, uint32_t measuredFrames)
{
    RadianceBenchResult res{};
    raw::bridge::RawRadianceHost host;
    std::string err;
    if (!host.Init(rig.dev.Get(), &err)) {
        res.note = "RawRadianceHost::Init failed: " + err;
        return res;
    }
    host.SetTimestampFrequency(rig.tsFreqHz);

    constexpr uint32_t kDim = 256u;
    auto buildScene = [](float emitScale) -> std::vector<float> {
        std::vector<float> s(static_cast<size_t>(kDim) * kDim * 4u, 0.0f);
        for (uint32_t y = 0u; y < kDim; ++y) {
            for (uint32_t x = 0u; x < kDim; ++x) {
                const size_t idx = (static_cast<size_t>(y) * kDim + x) * 4u;
                const float dx = static_cast<float>(x) - 52.0f;
                const float dy = static_cast<float>(y) - 128.0f;
                const float r  = std::sqrt(dx * dx + dy * dy);
                if (r < 16.0f) {
                    const float g = std::exp(-(r * r) / 96.0f);
                    s[idx + 0u] = 6.0f * emitScale * g;
                    s[idx + 1u] = 2.4f * emitScale * g;
                    s[idx + 2u] = 0.6f * emitScale * g;
                    s[idx + 3u] = 0.0f;
                }
                if (x >= 120u && x <= 144u && y >= 96u && y <= 160u) {
                    s[idx + 0u] = 0.0f;
                    s[idx + 1u] = 0.0f;
                    s[idx + 2u] = 0.0f;
                    s[idx + 3u] = 1.0f; // solid occluder pillar
                }
            }
        }
        return s;
    };

    ComPtr<ID3D12Resource> sceneTex;
    if (!rig.UploadRgba16Tex2D(sceneTex, kDim, buildScene(1.0f))) {
        res.note = "UploadSceneTexture (1x) failed";
        return res;
    }

    raw::bridge::RadianceCascadeParams rp{};
    rp.baseIntervalTexels = 3.5f;
    rp.maxTraceTexels     = 300.0f;
    rp.opacityScale       = 2.5f;
    rp.emissiveScale      = 0.15f;
    rp.irradianceScale    = 1.0f;

    std::vector<double> giSamples, cpuSamples;
    giSamples.reserve(measuredFrames);
    cpuSamples.reserve(measuredFrames);

    const uint32_t totalFrames = warmupFrames + measuredFrames;
    for (uint32_t f = 0u; f < totalFrames; ++f) {
        const auto c0 = Clock::now();
        if (!rig.BeginFrame()) {
            res.note = "BeginFrame failed";
            return res;
        }
        rig.MarkTimestamp(0u);
        host.ComputeGI(rig.cmd.Get(), sceneTex.Get(), rp, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        rig.MarkTimestamp(1u);
        rig.ResolveTimestamps(0u, 2u);
        const auto c1 = Clock::now();
        if (!rig.FlushAndWait()) {
            res.note = "FlushAndWait failed";
            return res;
        }
        if (f >= warmupFrames) {
            double gMs = 0.0;
            rig.ReadTimestampRangeMs(0u, 1u, gMs);
            giSamples.push_back(gMs);
            cpuSamples.push_back(ElapsedMs(c0, c1));
        }
    }

    res.giGpuMs     = ComputePercentiles(giSamples);
    res.cpuRecordMs = ComputePercentiles(cpuSamples);
    res.hostGiMs    = host.GetLastTimings().giPassMs;

    // Readback 1x irradiance & verify zero NaN/Inf + pillar shadow
    std::vector<float> irr1x;
    uint32_t iw = 0u, ih = 0u, id = 0u;
    if (!rig.ReadbackRgba16Tex(host.IrradianceResource(), host.IrradianceResourceState(), iw, ih, id, irr1x)) {
        res.note = "Irradiance 1x readback failed";
        return res;
    }
    for (float v : irr1x) {
        if (!std::isfinite(v)) ++res.nanCount;
    }
    auto sampleLumaAt = [&](const std::vector<float>& buf, uint32_t x, uint32_t y) -> float {
        const size_t idx = (static_cast<size_t>(y) * kDim + x) * 4u;
        return 0.2126f * buf[idx + 0u] + 0.7152f * buf[idx + 1u] + 0.0722f * buf[idx + 2u];
    };
    res.openIrrad  = sampleLumaAt(irr1x, 88u, 128u);
    res.umbraIrrad = sampleLumaAt(irr1x, 176u, 128u);

    // Linear energy scaling invariant check (2x emitter -> 2.00x irradiance)
    if (!rig.UploadRgba16Tex2D(sceneTex, kDim, buildScene(2.0f))) {
        res.note = "UploadSceneTexture (2x) failed";
        return res;
    }
    if (!rig.BeginFrame()) {
        res.note = "BeginFrame (2x) failed";
        return res;
    }
    host.ComputeGI(rig.cmd.Get(), sceneTex.Get(), rp, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (!rig.FlushAndWait()) {
        res.note = "FlushAndWait (2x) failed";
        return res;
    }
    std::vector<float> irr2x;
    if (!rig.ReadbackRgba16Tex(host.IrradianceResource(), host.IrradianceResourceState(), iw, ih, id, irr2x)) {
        res.note = "Irradiance 2x readback failed";
        return res;
    }
    const float open2x = sampleLumaAt(irr2x, 88u, 128u);
    res.linearScaleRatio = (res.openIrrad > 1.0e-5f) ? (open2x / res.openIrrad) : 0.0f;

    const bool budgetOk = rig.isWarp ? true : (res.giGpuMs.mean < 0.35);
    const bool invOk    = (res.nanCount == 0u) && (res.openIrrad > 0.05f) &&
                          (res.umbraIrrad < res.openIrrad * 0.25f) &&
                          (std::fabs(res.linearScaleRatio - 2.0f) < 0.05f);
    res.passed = budgetOk && invOk;
    if (!invOk) {
        res.note = "Radiance GI energy/shadow/NaN invariant failed";
    } else if (!budgetOk) {
        res.note = "HW GPU GI mean exceeded 0.35 ms budget";
    } else {
        res.note = rig.isWarp ? "WARP linear energy PASS" : "HW GPU < 0.35 ms & 2.00x linearity PASS";
    }
    return res;
}

// ----------------------------------------------------------------------------
// 3. Procedural PBR Texture Forge (RawTextureForge — all 8 seamless PBR sets)
// ----------------------------------------------------------------------------
TextureBenchResult RunTextureForgeBenchmark(bool quickMode)
{
    TextureBenchResult res{};
    res.resolution = quickMode ? 128u : 256u;
    constexpr uint32_t kKindCount = static_cast<uint32_t>(raw::materials::ProceduralTextureKind::Count);

    std::vector<double> setTimes;
    setTimes.reserve(kKindCount);

    bool allOk = true;
    const auto tAll0 = Clock::now();
    for (uint32_t k = 0u; k < kKindCount; ++k) {
        const auto kind = static_cast<raw::materials::ProceduralTextureKind>(k);
        raw::materials::ProceduralPbrTextureSet set{};
        const auto t0 = Clock::now();
        const bool ok = raw::materials::GenerateProceduralPbrTextureSet(kind, res.resolution, 202647u + k * 13u, &set);
        const auto t1 = Clock::now();
        setTimes.push_back(ElapsedMs(t0, t1));
        if (!ok || set.albedo.mips.empty() || set.normal.mips.empty() || set.ormh.mips.empty()) {
            allOk = false;
            continue;
        }
        const auto seam = raw::materials::MeasureSeamlessTileability(set.albedo.mips[0]);
        res.maxSeamDiff = std::max(res.maxSeamDiff, std::max(seam.meanEdgeDiffX, seam.meanEdgeDiffY));
        ++res.setsGenerated;
    }
    const auto tAll1 = Clock::now();

    res.totalMs  = ElapsedMs(tAll0, tAll1);
    res.perSetMs = ComputePercentiles(setTimes);
    // 3 PBR maps (Albedo + Normal + ORMH) * W * H per set
    const double totalPixels = static_cast<double>(res.setsGenerated) * 3.0 *
                               static_cast<double>(res.resolution) * static_cast<double>(res.resolution);
    res.mpixelsPerSec = (res.totalMs > 1.0e-6) ? (totalPixels / (res.totalMs * 1000.0)) : 0.0;
    res.passed        = allOk && (res.setsGenerated == kKindCount) && (res.mpixelsPerSec > 0.1);
    res.note          = res.passed ? "All 8 seamless PBR sets generated" : "Texture forge generation error";
    return res;
}

// ----------------------------------------------------------------------------
// 4. 48kHz FDN-8 Hadamard + HRTF Audio DSP (RawAudioDsp)
// ----------------------------------------------------------------------------
AudioBenchResult RunAudioDspBenchmark(bool quickMode)
{
    AudioBenchResult res{};
    res.sampleRate = 48000u;
    constexpr uint32_t kBlockFrames = 512u;
    const uint32_t numBlocks = quickMode ? 94u : 188u; // ~1.0s quick, ~2.0s full
    res.totalFrames     = numBlocks * kBlockFrames;
    res.audioDurationMs = static_cast<double>(res.totalFrames) * 1000.0 / static_cast<double>(res.sampleRate);

    raw::audio::SoundscapeSynth synth(res.sampleRate, 202647u);
    raw::audio::SpatialVoice3D  voice(res.sampleRate);

    raw::audio::SoundscapeParams sp{};
    sp.environment     = raw::audio::AcousticEnvironment::NordicStoneCrypt;
    sp.windSpeedMs     = 11.5f;
    sp.windGust01      = 0.55f;
    sp.fireIntensity01 = 0.80f;
    sp.waterFlow01     = 0.40f;
    sp.masterGain      = 0.72f;
    sp.enableReverb    = true;

    raw::audio::ListenerState listener{};
    raw::audio::SourceState   source{};
    source.position = {3.5f, 4.0f, 0.2f};
    source.velocity = {-2.0f, 0.5f, 0.0f};
    source.gain     = 0.22f;

    std::vector<float> monoIn(kBlockFrames, 0.0f);
    for (uint32_t i = 0u; i < kBlockFrames; ++i) {
        monoIn[i] = 0.25f * std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / static_cast<float>(res.sampleRate));
    }
    std::vector<float> stereo(static_cast<size_t>(kBlockFrames) * 2u, 0.0f);
    std::vector<double> blockTimes;
    blockTimes.reserve(numBlocks);

    double sumSq = 0.0;
    const auto tAll0 = Clock::now();
    for (uint32_t b = 0u; b < numBlocks; ++b) {
        const float angle = static_cast<float>(b) * 0.08f;
        source.position.x = std::cos(angle) * 5.0f;
        source.position.y = std::sin(angle) * 5.0f;

        const auto t0 = Clock::now();
        synth.SynthesizeStereo(sp, stereo.data(), kBlockFrames);
        voice.ProcessMonoToStereo(monoIn.data(), kBlockFrames, listener, source, stereo.data(), true);
        const auto t1 = Clock::now();
        blockTimes.push_back(ElapsedMs(t0, t1));

        for (float v : stereo) {
            if (!std::isfinite(v)) {
                ++res.nanCount;
            } else {
                const float av = std::fabs(v);
                if (av > res.peakAbs) res.peakAbs = av;
                sumSq += static_cast<double>(v) * static_cast<double>(v);
            }
        }
    }
    const auto tAll1 = Clock::now();

    res.totalRenderMs   = ElapsedMs(tAll0, tAll1);
    res.blockMs         = ComputePercentiles(blockTimes);
    res.headroomRatio   = res.totalRenderMs / std::max(res.audioDurationMs, 1.0);
    res.realtimeSpeedup = (res.totalRenderMs > 1.0e-6) ? (res.audioDurationMs / res.totalRenderMs) : 0.0;
    res.rmsLevel        = static_cast<float>(std::sqrt(sumSq / static_cast<double>(static_cast<size_t>(res.totalFrames) * 2u)));

    const bool headroomOk = (res.headroomRatio < 0.15);
    const bool signalOk   = (res.nanCount == 0u) && (res.rmsLevel > 1.0e-4f) && (res.peakAbs <= 2.0f);
    res.passed = headroomOk && signalOk;
    if (!signalOk) {
        res.note = "Audio DSP peak/RMS/NaN check failed";
    } else if (!headroomOk) {
        res.note = "Audio DSP headroom ratio exceeded 0.15";
    } else {
        res.note = "FDN-8 + Woodworth HRTF > 6.5x real-time PASS";
    }
    return res;
}

// ----------------------------------------------------------------------------
// 5. 3D Voronoi CSG Destruction Physics (RawDestructionEngine)
// ----------------------------------------------------------------------------
DestructionBenchResult RunDestructionBenchmark(bool quickMode)
{
    DestructionBenchResult res{};
    constexpr uint32_t kArchCount = static_cast<uint32_t>(raw::worldgen::DestructibleArchetype::Count);
    std::vector<double> fracSamples, stepUsSamples;
    fracSamples.reserve(kArchCount);

    raw::worldgen::FractureResult heroFrac{};
    bool fracAllOk = true;
    for (uint32_t a = 0u; a < kArchCount; ++a) {
        const auto arch = static_cast<raw::worldgen::DestructibleArchetype>(a);
        const auto body = raw::worldgen::BuildDestructibleArchetype(arch, 1.0f, {0.0f, 0.0f, 0.0f});
        raw::worldgen::FractureConfig fcfg{};
        fcfg.seed       = 202647u + a * 31u;
        fcfg.shardCount = 18u;
        fcfg.impactPoint = {0.2f, 0.0f, 0.8f};

        const auto t0 = Clock::now();
        const auto fres = raw::worldgen::FractureConvexBody(body, fcfg);
        const auto t1 = Clock::now();
        fracSamples.push_back(ElapsedMs(t0, t1));

        if (!fres.ok || fres.shards.size() < 4u) {
            fracAllOk = false;
            continue;
        }
        const double volErrPct = static_cast<double>(fres.volumeErrorRel) * 100.0;
        res.maxVolErrorPct = std::max(res.maxVolErrorPct, volErrPct);
        res.totalShards += static_cast<uint32_t>(fres.shards.size());
        ++res.archetypesTested;
        if (a == 0u) heroFrac = fres;
    }

    // 60Hz rigid-body debris step benchmark
    raw::worldgen::DestructionSimulation sim{};
    raw::worldgen::DestructionImpact imp{};
    imp.point         = {0.2f, 0.0f, 0.8f};
    imp.direction     = {1.0f, 0.15f, 0.25f};
    imp.impulseNs     = 220.0f;
    imp.radialBurstNs = 110.0f;
    const bool initSimOk = sim.InitializeFromFracture(heroFrac, &imp);

    const uint32_t stepCount = quickMode ? 60u : 180u;
    stepUsSamples.reserve(stepCount);
    for (uint32_t s = 0u; s < stepCount; ++s) {
        const auto t0 = Clock::now();
        sim.Step(1.0f / 60.0f);
        const auto t1 = Clock::now();
        stepUsSamples.push_back(ElapsedMs(t0, t1) * 1000.0);
    }

    const auto stats = sim.GetStats();
    res.fractureMs = ComputePercentiles(fracSamples);
    res.stepUs     = ComputePercentiles(stepUsSamples);
    res.allFinite  = stats.allFinite;

    const bool volOk = (res.maxVolErrorPct < 1.0e-4); // < 1e-4% exact Voronoi CSG volume conservation
    res.passed = fracAllOk && initSimOk && res.allFinite && volOk && (res.archetypesTested == kArchCount);
    res.note   = res.passed ? "Exact Voronoi CSG volume conservation & 60Hz sim PASS" : "Destruction benchmark/invariant failed";
    return res;
}

// ----------------------------------------------------------------------------
// 6. Biomechanical 3D Flora & Hydraulic Terrain Erosion (RawFloraEngine + RawHydrologyGen)
// ----------------------------------------------------------------------------
FloraHydroBenchResult RunFloraAndHydrologyBenchmark(bool quickMode)
{
    FloraHydroBenchResult res{};
    constexpr uint32_t kSpecCount = static_cast<uint32_t>(raw::worldgen::FloraSpecies::Count);
    const uint32_t floraReps = quickMode ? 1u : 2u;

    std::vector<double> treeSamples;
    treeSamples.reserve(static_cast<size_t>(kSpecCount) * floraReps);

    const auto tFlora0 = Clock::now();
    for (uint32_t rep = 0u; rep < floraReps; ++rep) {
        for (uint32_t s = 0u; s < kSpecCount; ++s) {
            const auto spec = static_cast<raw::worldgen::FloraSpecies>(s);
            auto params = raw::worldgen::MakeDefaultFloraParams(spec, 202647u + rep * 97u + s * 17u, 0u);
            raw::worldgen::ProceduralMesh mesh{};
            raw::worldgen::FloraBuildStats fstats{};
            const auto t0 = Clock::now();
            const bool ok = raw::worldgen::GenerateFloraMesh(params, &mesh, &fstats);
            const auto t1 = Clock::now();
            treeSamples.push_back(ElapsedMs(t0, t1));
            if (ok) {
                ++res.treesBuilt;
                res.floraMeshlets += static_cast<uint32_t>(mesh.meshlets.size());
                raw::pipeline::ValidationReport repV{};
                if (!raw::pipeline::ValidateProceduralMeshStrict(mesh, &repV)) {
                    res.floraValidationErrors += std::max(repV.TotalErrors(), 1u);
                }
            } else {
                ++res.floraValidationErrors;
            }
        }
    }
    const auto tFlora1 = Clock::now();
    res.floraTotalMs = ElapsedMs(tFlora0, tFlora1);
    res.treeMs       = ComputePercentiles(treeSamples);
    res.treesPerSec  = (res.floraTotalMs > 1.0e-6) ? (static_cast<double>(res.treesBuilt) * 1000.0 / res.floraTotalMs) : 0.0;

    // Hydraulic + Thermal + D8 Watershed Terrain Erosion + Meshlet Validation
    const uint32_t cellCount = quickMode ? 2u : 4u;
    std::vector<double> cellSamples;
    cellSamples.reserve(cellCount);

    const auto tHydro0 = Clock::now();
    for (uint32_t c = 0u; c < cellCount; ++c) {
        raw::worldgen::TerrainConfig tcfg{};
        tcfg.seed                   = 202647u + c * 53u;
        tcfg.chunkSizeU             = 4096.0f;
        tcfg.enableHydraulicErosion = false; // Hydrology pass below runs full hydraulic+thermal+D8

        raw::worldgen::HydrologyChunkConfig hcfg{};
        hcfg.enableHydraulic        = true;
        hcfg.enableThermal          = true;
        hcfg.enableWatershed        = true;
        hcfg.hydraulic.dropletCount = quickMode ? 2048u : 4096u;
        hcfg.thermal.iterations     = quickMode ? 12u : 24u;

        const auto t0 = Clock::now();
        raw::worldgen::TerrainChunkData chunk{};
        const bool gOk = raw::worldgen::GenerateTerrainChunk(static_cast<int32_t>(c), 0, 0u, tcfg, 0u, &chunk);
        raw::worldgen::WatershedResult ws{};
        const bool hOk = gOk && raw::worldgen::ApplyHydrologyToTerrainChunk(chunk, hcfg, &ws);
        raw::worldgen::ProceduralMesh tMesh{};
        const bool mOk = hOk && raw::worldgen::BuildTerrainChunkMesh(chunk, 126u, &tMesh);
        const auto t1 = Clock::now();
        cellSamples.push_back(ElapsedMs(t0, t1));

        if (mOk && ws.ok) {
            ++res.hydroCellsBuilt;
            res.hydroMeshlets += static_cast<uint32_t>(tMesh.meshlets.size());
            raw::pipeline::ValidationReport repV{};
            if (!raw::pipeline::ValidateProceduralMeshStrict(tMesh, &repV)) {
                res.hydroValidationErrors += std::max(repV.TotalErrors(), 1u);
            }
        } else {
            ++res.hydroValidationErrors;
        }
    }
    const auto tHydro1 = Clock::now();
    res.hydroTotalMs = ElapsedMs(tHydro0, tHydro1);
    res.cellMs       = ComputePercentiles(cellSamples);
    res.cellsPerSec  = (res.hydroTotalMs > 1.0e-6) ? (static_cast<double>(res.hydroCellsBuilt) * 1000.0 / res.hydroTotalMs) : 0.0;

    res.passed = (res.treesBuilt == kSpecCount * floraReps) && (res.floraValidationErrors == 0u) &&
                 (res.hydroCellsBuilt == cellCount) && (res.hydroValidationErrors == 0u);
    res.note   = res.passed ? "All 5 flora species + hydrology terrain meshlets strictly validated"
                            : "Flora or hydrology validation failed";
    return res;
}

// ----------------------------------------------------------------------------
// 7. Multi-Cell Tamriel Streaming (RawTamrielWorldStreamer)
// ----------------------------------------------------------------------------
TamrielBenchResult RunTamrielStreamerBenchmark(const std::string& esmPath, const std::string& bsaPath)
{
    TamrielBenchResult res{};
    std::error_code ec;
    if (!std::filesystem::exists(esmPath, ec) || !std::filesystem::exists(bsaPath, ec)) {
        res.skipped = true;
        res.passed  = true;
        res.note    = "Skyrim.esm or Meshes0.bsa not present; gracefully skipped";
        return res;
    }

    raw::worldgen::RawTamrielWorldStreamer streamer;
    raw::worldgen::TamrielStreamConfig cfg{};
    cfg.radiusCells            = 1; // 3x3 ring (9 cells)
    cfg.maxCellsPerUpdate      = 9u;
    cfg.maxUniqueModelsPerCell = 24u;
    cfg.bakeCellProps          = true;

    std::string err;
    const auto tInit0 = Clock::now();
    if (!streamer.Init(esmPath, bsaPath, cfg, &err)) {
        res.note = "RawTamrielWorldStreamer::Init failed: " + err;
        return res;
    }
    const auto tInit1 = Clock::now();
    res.indexMs = ElapsedMs(tInit0, tInit1);

    // Riverwood (4, -12) 3x3 cold load
    const raw::worldgen::Vec3 riverwoodPos{(4.0f + 0.5f) * 4096.0f, (-12.0f + 0.5f) * 4096.0f, 0.0f};
    const auto tRw0 = Clock::now();
    const auto stRw = streamer.StreamRingComplete(riverwoodPos);
    const auto tRw1 = Clock::now();
    res.riverwoodColdMs   = ElapsedMs(tRw0, tRw1);
    res.riverwoodCells    = stRw.activeCells;
    res.riverwoodMeshlets = stRw.totalTerrainMeshlets + stRw.totalPropMeshlets;
    res.indexedWorldCells = stRw.indexedWorldCells;
    res.indexedModels     = stRw.indexedModels;

    // Whiterun (5, -3) 3x3 stream + warm LRU cache hit measurement
    const raw::worldgen::Vec3 whiterunPos{(5.0f + 0.5f) * 4096.0f, (-3.0f + 0.5f) * 4096.0f, 0.0f};
    const auto tWr0 = Clock::now();
    const auto stWr = streamer.StreamRingComplete(whiterunPos);
    const auto tWr1 = Clock::now();
    res.whiterunStreamMs = ElapsedMs(tWr0, tWr1);
    res.cacheHits        = stWr.cacheHits;
    res.cacheMisses      = stWr.cacheMisses;
    const uint64_t totalQueries = stWr.cacheHits + stWr.cacheMisses;
    res.whiterunWarmHitRate = (totalQueries > 0ull)
        ? (static_cast<double>(stWr.cacheHits) * 100.0 / static_cast<double>(totalQueries))
        : 0.0;

    streamer.Shutdown();

    res.passed = (res.indexedWorldCells >= 10000u) && (res.riverwoodCells == 9u) && (res.riverwoodMeshlets > 0u);
    res.note   = res.passed ? "Live Skyrim.esm Tamriel 3x3 ring & LRU cache PASS"
                            : "Tamriel streaming invariant failed";
    return res;
}

// ----------------------------------------------------------------------------
// JSON Serializer & Console Scorecard Printer
// ----------------------------------------------------------------------------
void WriteBenchJson(const std::string& path,
                    const BenchDx12Rig& rig,
                    bool quickMode,
                    uint32_t warmupFrames,
                    uint32_t measuredFrames,
                    uint32_t d3d12DebugErrors,
                    bool allPassed,
                    const FluidBenchResult& fluid,
                    const RadianceBenchResult& rc,
                    const TextureBenchResult& tex,
                    const AudioBenchResult& audio,
                    const DestructionBenchResult& destr,
                    const FloraHydroBenchResult& flora,
                    const TamrielBenchResult& tamriel)
{
    std::ostringstream os;
    os << std::fixed << std::setprecision(4);
    auto pctObj = [&](const Percentiles& p) {
        os << "{\"mean\":" << p.mean << ",\"p50\":" << p.p50
           << ",\"p95\":" << p.p95 << ",\"p99\":" << p.p99
           << ",\"min\":" << p.minV << ",\"max\":" << p.maxV << "}";
    };

    os << "{\n";
    os << "  \"suite\": \"RAW Engine Studio Benchmark Suite\",\n";
    os << "  \"mode\": \"" << (quickMode ? "quick" : "full") << "\",\n";
    os << "  \"adapter\": \"" << JsonEscape(rig.adapterName) << "\",\n";
    os << "  \"isWarp\": " << (rig.isWarp ? "true" : "false") << ",\n";
    os << "  \"vramMB\": " << (rig.vramBytes / (1024ull * 1024ull)) << ",\n";
    os << "  \"debugLayer\": " << (rig.debugLayer ? "true" : "false") << ",\n";
    os << "  \"d3d12DebugErrors\": " << d3d12DebugErrors << ",\n";
    os << "  \"timestampFreqHz\": " << rig.tsFreqHz << ",\n";
    os << "  \"gpuWarmupFrames\": " << warmupFrames << ",\n";
    os << "  \"gpuMeasuredFrames\": " << measuredFrames << ",\n";
    os << "  \"allPassed\": " << (allPassed ? "true" : "false") << ",\n";
    os << "  \"subsystems\": {\n";

    // 1. Fluid
    os << "    \"fluidSim\": {\n";
    os << "      \"passed\": " << (fluid.passed ? "true" : "false") << ",\n";
    os << "      \"fireGpuMs\": "; pctObj(fluid.fireGpuMs); os << ",\n";
    os << "      \"waterGpuMs\": "; pctObj(fluid.waterGpuMs); os << ",\n";
    os << "      \"combinedGpuMs\": "; pctObj(fluid.combinedGpuMs); os << ",\n";
    os << "      \"cpuRecordMs\": "; pctObj(fluid.cpuRecordMs); os << ",\n";
    os << "      \"hostFireMs\": " << fluid.hostFireMs << ",\n";
    os << "      \"hostWaterMs\": " << fluid.hostWaterMs << ",\n";
    os << "      \"pressureIters\": " << fluid.pressureIters << ",\n";
    os << "      \"peakTempK\": " << fluid.peakTempK << ",\n";
    os << "      \"fireNanCount\": " << fluid.fireNanCount << ",\n";
    os << "      \"waterNanCount\": " << fluid.waterNanCount << ",\n";
    os << "      \"note\": \"" << JsonEscape(fluid.note) << "\"\n";
    os << "    },\n";

    // 2. Radiance Cascades
    os << "    \"radianceCascades\": {\n";
    os << "      \"passed\": " << (rc.passed ? "true" : "false") << ",\n";
    os << "      \"giPassGpuMs\": "; pctObj(rc.giGpuMs); os << ",\n";
    os << "      \"cpuRecordMs\": "; pctObj(rc.cpuRecordMs); os << ",\n";
    os << "      \"hostGiMs\": " << rc.hostGiMs << ",\n";
    os << "      \"openIrrad\": " << rc.openIrrad << ",\n";
    os << "      \"umbraIrrad\": " << rc.umbraIrrad << ",\n";
    os << "      \"linearScaleRatio\": " << rc.linearScaleRatio << ",\n";
    os << "      \"nanCount\": " << rc.nanCount << ",\n";
    os << "      \"note\": \"" << JsonEscape(rc.note) << "\"\n";
    os << "    },\n";

    // 3. Texture Forge
    os << "    \"textureForge\": {\n";
    os << "      \"passed\": " << (tex.passed ? "true" : "false") << ",\n";
    os << "      \"setsGenerated\": " << tex.setsGenerated << ",\n";
    os << "      \"resolution\": " << tex.resolution << ",\n";
    os << "      \"totalMs\": " << tex.totalMs << ",\n";
    os << "      \"mpixelsPerSec\": " << tex.mpixelsPerSec << ",\n";
    os << "      \"maxMeanSeamDiff\": " << tex.maxSeamDiff << ",\n";
    os << "      \"perSetMs\": "; pctObj(tex.perSetMs); os << ",\n";
    os << "      \"note\": \"" << JsonEscape(tex.note) << "\"\n";
    os << "    },\n";

    // 4. Audio DSP
    os << "    \"audioDsp\": {\n";
    os << "      \"passed\": " << (audio.passed ? "true" : "false") << ",\n";
    os << "      \"sampleRate\": " << audio.sampleRate << ",\n";
    os << "      \"totalFrames\": " << audio.totalFrames << ",\n";
    os << "      \"audioDurationMs\": " << audio.audioDurationMs << ",\n";
    os << "      \"totalRenderMs\": " << audio.totalRenderMs << ",\n";
    os << "      \"headroomRatio\": " << audio.headroomRatio << ",\n";
    os << "      \"realtimeSpeedup\": " << audio.realtimeSpeedup << ",\n";
    os << "      \"peakAbs\": " << audio.peakAbs << ",\n";
    os << "      \"rmsLevel\": " << audio.rmsLevel << ",\n";
    os << "      \"nanCount\": " << audio.nanCount << ",\n";
    os << "      \"blockMs\": "; pctObj(audio.blockMs); os << ",\n";
    os << "      \"note\": \"" << JsonEscape(audio.note) << "\"\n";
    os << "    },\n";

    // 5. Destruction
    os << "    \"destruction\": {\n";
    os << "      \"passed\": " << (destr.passed ? "true" : "false") << ",\n";
    os << "      \"archetypesTested\": " << destr.archetypesTested << ",\n";
    os << "      \"totalShards\": " << destr.totalShards << ",\n";
    os << "      \"maxVolumeErrorPct\": " << std::setprecision(7) << destr.maxVolErrorPct << std::setprecision(4) << ",\n";
    os << "      \"fractureMs\": "; pctObj(destr.fractureMs); os << ",\n";
    os << "      \"stepUs\": "; pctObj(destr.stepUs); os << ",\n";
    os << "      \"allFinite\": " << (destr.allFinite ? "true" : "false") << ",\n";
    os << "      \"note\": \"" << JsonEscape(destr.note) << "\"\n";
    os << "    },\n";

    // 6. Flora + Hydrology
    os << "    \"floraAndHydrology\": {\n";
    os << "      \"passed\": " << (flora.passed ? "true" : "false") << ",\n";
    os << "      \"treesBuilt\": " << flora.treesBuilt << ",\n";
    os << "      \"floraMeshlets\": " << flora.floraMeshlets << ",\n";
    os << "      \"floraValidationErrors\": " << flora.floraValidationErrors << ",\n";
    os << "      \"treesPerSec\": " << flora.treesPerSec << ",\n";
    os << "      \"treeMs\": "; pctObj(flora.treeMs); os << ",\n";
    os << "      \"hydroCellsBuilt\": " << flora.hydroCellsBuilt << ",\n";
    os << "      \"hydroMeshlets\": " << flora.hydroMeshlets << ",\n";
    os << "      \"hydroValidationErrors\": " << flora.hydroValidationErrors << ",\n";
    os << "      \"cellsPerSec\": " << flora.cellsPerSec << ",\n";
    os << "      \"cellMs\": "; pctObj(flora.cellMs); os << ",\n";
    os << "      \"note\": \"" << JsonEscape(flora.note) << "\"\n";
    os << "    },\n";

    // 7. Tamriel Streamer
    os << "    \"tamrielStreamer\": {\n";
    os << "      \"passed\": " << (tamriel.passed ? "true" : "false") << ",\n";
    os << "      \"skipped\": " << (tamriel.skipped ? "true" : "false") << ",\n";
    os << "      \"indexedWorldCells\": " << tamriel.indexedWorldCells << ",\n";
    os << "      \"indexedModels\": " << tamriel.indexedModels << ",\n";
    os << "      \"indexMs\": " << tamriel.indexMs << ",\n";
    os << "      \"riverwoodColdMs\": " << tamriel.riverwoodColdMs << ",\n";
    os << "      \"riverwoodCells\": " << tamriel.riverwoodCells << ",\n";
    os << "      \"riverwoodMeshlets\": " << tamriel.riverwoodMeshlets << ",\n";
    os << "      \"whiterunStreamMs\": " << tamriel.whiterunStreamMs << ",\n";
    os << "      \"whiterunWarmHitRatePct\": " << tamriel.whiterunWarmHitRate << ",\n";
    os << "      \"cacheHits\": " << tamriel.cacheHits << ",\n";
    os << "      \"cacheMisses\": " << tamriel.cacheMisses << ",\n";
    os << "      \"note\": \"" << JsonEscape(tamriel.note) << "\"\n";
    os << "    }\n";

    os << "  }\n";
    os << "}\n";

    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (ofs) {
        const std::string s = os.str();
        ofs.write(s.data(), static_cast<std::streamsize>(s.size()));
    }
}

void PrintScorecard(const BenchDx12Rig& rig,
                    bool quickMode,
                    uint32_t warmupFrames,
                    uint32_t measuredFrames,
                    uint32_t d3d12DebugErrors,
                    const FluidBenchResult& fluid,
                    const RadianceBenchResult& rc,
                    const TextureBenchResult& tex,
                    const AudioBenchResult& audio,
                    const DestructionBenchResult& destr,
                    const FloraHydroBenchResult& flora,
                    const TamrielBenchResult& tamriel)
{
    std::printf("\n");
    std::printf("=====================================================================================================\n");
    std::printf("  RAW ENGINE STUDIO BENCHMARK & INVARIANT SCORECARD (%s)\n", quickMode ? "QUICK CI" : "FULL");
    std::printf("  Adapter : %s (%s, %llu MB VRAM) | DebugLayer: %s | D3D12 Errors: %u\n",
                rig.adapterName.c_str(),
                rig.isWarp ? "WARP" : "HW DX12",
                static_cast<unsigned long long>(rig.vramBytes / (1024ull * 1024ull)),
                rig.debugLayer ? "ON" : "OFF",
                d3d12DebugErrors);
    std::printf("  GPU Run : %u warmup + %u measured frames | Timestamp Freq: %llu Hz\n",
                warmupFrames, measuredFrames, static_cast<unsigned long long>(rig.tsFreqHz));
    std::printf("-----------------------------------------------------------------------------------------------------\n");
    std::printf("  %-30s | %-6s | %-28s | %s\n", "Subsystem", "Status", "Primary Metric", "Details / Invariants");
    std::printf("-----------------------------------------------------------------------------------------------------\n");

    char metric[64];
    char detail[128];

    std::snprintf(metric, sizeof(metric), "Fire %.3f + Water %.3f ms", fluid.fireGpuMs.mean, fluid.waterGpuMs.mean);
    std::snprintf(detail, sizeof(detail), "Comb p50=%.3f p95=%.3f p99=%.3f ms | Tmax=%.0fK | NaN=%u",
                  fluid.combinedGpuMs.p50, fluid.combinedGpuMs.p95, fluid.combinedGpuMs.p99,
                  fluid.peakTempK, fluid.fireNanCount + fluid.waterNanCount);
    std::printf("  %-30s | %-6s | %-28s | %s\n", "1. GPU Fluid (64^3 + 256^2)", fluid.passed ? "PASS" : "FAIL", metric, detail);

    std::snprintf(metric, sizeof(metric), "GI %.3f ms (p95 %.3f ms)", rc.giGpuMs.mean, rc.giGpuMs.p95);
    std::snprintf(detail, sizeof(detail), "p50=%.3f p99=%.3f ms | 2xRatio=%.3f | Umbra=%.1f%%",
                  rc.giGpuMs.p50, rc.giGpuMs.p99, rc.linearScaleRatio,
                  rc.openIrrad > 1e-5f ? (rc.umbraIrrad / rc.openIrrad * 100.0f) : 0.0f);
    std::printf("  %-30s | %-6s | %-28s | %s\n", "2. GPU Radiance Cascades GI", rc.passed ? "PASS" : "FAIL", metric, detail);

    std::snprintf(metric, sizeof(metric), "%.2f MPix/s (%.1f ms)", tex.mpixelsPerSec, tex.totalMs);
    std::snprintf(detail, sizeof(detail), "%u sets @ %ux%u | p50=%.2f p95=%.2f ms | seam=%.2f",
                  tex.setsGenerated, tex.resolution, tex.resolution, tex.perSetMs.p50, tex.perSetMs.p95, tex.maxSeamDiff);
    std::printf("  %-30s | %-6s | %-28s | %s\n", "3. Procedural PBR Forge", tex.passed ? "PASS" : "FAIL", metric, detail);

    std::snprintf(metric, sizeof(metric), "Ratio %.4f (%.1fx RT)", audio.headroomRatio, audio.realtimeSpeedup);
    std::snprintf(detail, sizeof(detail), "Render %.2f ms / %.0f ms | p95=%.3f ms | RMS=%.3f Peak=%.3f",
                  audio.totalRenderMs, audio.audioDurationMs, audio.blockMs.p95, audio.rmsLevel, audio.peakAbs);
    std::printf("  %-30s | %-6s | %-28s | %s\n", "4. 48kHz FDN-8 + HRTF Audio", audio.passed ? "PASS" : "FAIL", metric, detail);

    std::snprintf(metric, sizeof(metric), "Frac %.2f ms | %.1f us/st", destr.fractureMs.mean, destr.stepUs.mean);
    std::snprintf(detail, sizeof(detail), "%u arch / %u shards | volErr=%.6f%% | step p95=%.1f us",
                  destr.archetypesTested, destr.totalShards, destr.maxVolErrorPct, destr.stepUs.p95);
    std::printf("  %-30s | %-6s | %-28s | %s\n", "5. 3D Voronoi Destruction", destr.passed ? "PASS" : "FAIL", metric, detail);

    std::snprintf(metric, sizeof(metric), "%.1f trees/s | %.1f cells/s", flora.treesPerSec, flora.cellsPerSec);
    std::snprintf(detail, sizeof(detail), "Flora=%u msh (%u err) | Hydro=%u msh (%u err)",
                  flora.floraMeshlets, flora.floraValidationErrors,
                  flora.hydroMeshlets, flora.hydroValidationErrors);
    std::printf("  %-30s | %-6s | %-28s | %s\n", "6. Flora & Hydrology Gen", flora.passed ? "PASS" : "FAIL", metric, detail);

    if (tamriel.skipped) {
        std::printf("  %-30s | %-6s | %-28s | %s\n", "7. Tamriel ESM Streamer", "SKIP", "Skyrim.esm absent", tamriel.note.c_str());
    } else {
        std::snprintf(metric, sizeof(metric), "RW %.1f ms | Hit %.1f%%", tamriel.riverwoodColdMs, tamriel.whiterunWarmHitRate);
        std::snprintf(detail, sizeof(detail), "%u cells idx | WR %.1f ms | hits=%llu miss=%llu",
                      tamriel.indexedWorldCells, tamriel.whiterunStreamMs,
                      static_cast<unsigned long long>(tamriel.cacheHits),
                      static_cast<unsigned long long>(tamriel.cacheMisses));
        std::printf("  %-30s | %-6s | %-28s | %s\n", "7. Tamriel ESM Streamer", tamriel.passed ? "PASS" : "FAIL", metric, detail);
    }

    std::printf("=====================================================================================================\n");
}

} // namespace

int main(int argc, char** argv)
{
    bool        preferWarp       = false;
    bool        quickMode        = false;
    bool        enableDebugLayer = true;
    int         framesOverride   = -1;
    std::string jsonPath;
    std::string esmPath = "H:\\SteamLibrary\\steamapps\\common\\Skyrim Special Edition\\Data\\Skyrim.esm";
    std::string bsaPath = "H:\\SteamLibrary\\steamapps\\common\\Skyrim Special Edition\\Data\\Skyrim - Meshes0.bsa";

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--warp") {
            preferWarp = true;
        } else if (arg == "--quick") {
            quickMode = true;
        } else if (arg == "--no-debug-layer") {
            enableDebugLayer = false;
        } else if (arg == "--frames" && i + 1 < argc) {
            framesOverride = std::atoi(argv[++i]);
        } else if (arg == "--json" && i + 1 < argc) {
            jsonPath = argv[++i];
        } else if (arg == "--esm" && i + 1 < argc) {
            esmPath = argv[++i];
        } else if (arg == "--bsa" && i + 1 < argc) {
            bsaPath = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::printf("Usage: raw_bench_suite [--warp] [--quick] [--frames N] [--json path] [--esm path] [--bsa path] [--no-debug-layer]\n");
            return 0;
        }
    }

    BenchDx12Rig rig{};
    if (!rig.Init(preferWarp, enableDebugLayer)) {
        std::fprintf(stderr, "FATAL: Failed to initialize DX12 benchmark device.\n");
        return 2;
    }

    uint32_t warmupFrames   = rig.isWarp ? (quickMode ? 2u : 4u) : (quickMode ? 6u : 30u);
    uint32_t measuredFrames = rig.isWarp ? (quickMode ? 4u : 8u) : (quickMode ? 10u : 60u);
    if (framesOverride > 0) {
        measuredFrames = static_cast<uint32_t>(framesOverride);
        if (warmupFrames > measuredFrames) warmupFrames = std::max(2u, measuredFrames / 2u);
    }

    const FluidBenchResult       fluid   = RunFluidBenchmark(rig, warmupFrames, measuredFrames);
    const RadianceBenchResult    rc      = RunRadianceBenchmark(rig, warmupFrames, measuredFrames);
    const TextureBenchResult     tex     = RunTextureForgeBenchmark(quickMode);
    const AudioBenchResult       audio   = RunAudioDspBenchmark(quickMode);
    const DestructionBenchResult destr   = RunDestructionBenchmark(quickMode);
    const FloraHydroBenchResult  flora   = RunFloraAndHydrologyBenchmark(quickMode);
    const TamrielBenchResult     tamriel = RunTamrielStreamerBenchmark(esmPath, bsaPath);

    const uint32_t debugErrors = rig.DrainDebugErrors();
    const bool allPassed = fluid.passed && rc.passed && tex.passed && audio.passed &&
                           destr.passed && flora.passed && tamriel.passed && (debugErrors == 0u);

    PrintScorecard(rig, quickMode, warmupFrames, measuredFrames, debugErrors,
                   fluid, rc, tex, audio, destr, flora, tamriel);

    if (!jsonPath.empty()) {
        WriteBenchJson(jsonPath, rig, quickMode, warmupFrames, measuredFrames, debugErrors, allPassed,
                       fluid, rc, tex, audio, destr, flora, tamriel);
        std::printf("  JSON report written: %s\n", jsonPath.c_str());
    }

    std::printf("  OVERALL SUITE RESULT: %s (d3d12DebugErrors=%u)\n\n",
                allPassed ? "PASS" : "FAIL", debugErrors);
    return allPassed ? 0 : 1;
}
