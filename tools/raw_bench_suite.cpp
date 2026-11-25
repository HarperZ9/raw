// tools/raw_bench_suite.cpp: Studio-Grade Automated GPU/CPU Benchmark & Visual Invariant Suite for RAW.
// Benchmarks all 18 core GPU & CPU engine subsystems on Hardware DX12 GPU (e.g. RTX 4090) or WARP:
//   1. GPU 64^3 Navier-Stokes Fire/Smoke + 256^2 SWE Water (RawFluidSimHost)
//   2. GPU 4-Cascade Radiance Cascades GI (RawRadianceHost)
//   3. Procedural PBR Texture Forge (RawTextureForge, all 8 seamless PBR sets)
//   4. 48kHz FDN-8 Hadamard + Woodworth HRTF Audio DSP (RawAudioDsp)
//   5. 3D Voronoi CSG Destruction Physics (RawDestructionEngine)
//   6. Biomechanical 3D Flora & Hydraulic Terrain Erosion (RawFloraEngine + RawHydrologyGen + RawAssetValidator)
//   7. Multi-Cell Tamriel Streaming (RawTamrielWorldStreamer, graceful skip if Skyrim.esm absent)
//   8. GPU XPBD Soft-Body / Hair / Cloth / Bush-Bone Solver (RawXpbdSimHost, 256 chains / 4,096 particles)
//   9. GPU Conservative Hi-Z Pyramid + 16,384-Instance Frustum/Occlusion Culler (RawGpuCullHost)
//  10. GPU 16x9x24 3D Clustered 1,024-Light Grid Assignment (RawLightClusterHost)
//  11. GPU 160x90x64 3D Volumetric Froxel Fog, Mie Scattering & Temporal Reprojection (RawVolumetricFroxelHost)
//  12. GPU Variable-Penumbra PCSS Contact-Hardening Shadows & Texel-Snapped Cascade (RawShadowPcssHost)
//  13. GPU Jimenez GTAO + Unoccluded Bent Normals & Bilateral Denoise (RawGtaoHost)
//  14. GPU GGX Stochastic Screen-Space Reflections (RawSsrHost)
//  15. GPU 256-Bin Log-Luminance Histogram & Asymmetric Human Eye-Adaptation Auto-Exposure (RawAutoExposureHost)
//  16. GPU 3D Volumetric Cloud Raymarching + Beer-Powder Silver-Lining & Temporal Reprojection (RawCloudRaymarchHost)
//  17. GPU Cinematic Post-Process: Tile-Max Motion Blur + Depth-Aware Hexagonal Bokeh DoF (RawPostCinematicHost)
//  18. GPU 16,384-Particle 3D Physics & Screen-Space Additive Splat Engine (RawGpuParticlesHost)
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
#include "../src/d3d12_bridge/RawXpbdSimHost.h"
#include "../src/d3d12_bridge/RawGpuCullHost.h"
#include "../src/d3d12_bridge/RawLightClusterHost.h"
#include "../src/d3d12_bridge/RawVolumetricFroxelHost.h"
#include "../src/d3d12_bridge/RawShadowPcssHost.h"
#include "../src/d3d12_bridge/RawGtaoHost.h"
#include "../src/d3d12_bridge/RawSsrHost.h"
#include "../src/d3d12_bridge/RawAutoExposureHost.h"
#include "../src/d3d12_bridge/RawCloudRaymarchHost.h"
#include "../src/d3d12_bridge/RawPostCinematicHost.h"
#include "../src/d3d12_bridge/RawGpuParticlesHost.h"
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
#include <limits>
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

    bool ReadbackBuffer(ID3D12Resource* buf, D3D12_RESOURCE_STATES curState,
                        uint64_t byteSize, std::vector<uint8_t>& outBytes)
    {
        if (!buf || byteSize == 0ull) return false;
        D3D12_HEAP_PROPERTIES hpRb{};
        hpRb.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rbd{};
        rbd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        rbd.Width            = byteSize;
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
        if (curState != D3D12_RESOURCE_STATE_COPY_SOURCE) {
            D3D12_RESOURCE_BARRIER b{};
            b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource   = buf;
            b.Transition.StateBefore = curState;
            b.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmd->ResourceBarrier(1, &b);
        }

        cmd->CopyBufferRegion(rbBuf.Get(), 0ull, buf, 0ull, byteSize);

        if (curState != D3D12_RESOURCE_STATE_COPY_SOURCE) {
            D3D12_RESOURCE_BARRIER b{};
            b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource   = buf;
            b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
            b.Transition.StateAfter  = curState;
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmd->ResourceBarrier(1, &b);
        }
        if (!FlushAndWait()) return false;

        void* mapped = nullptr;
        D3D12_RANGE rr{0, static_cast<SIZE_T>(byteSize)};
        if (FAILED(rbBuf->Map(0, &rr, &mapped))) return false;
        outBytes.resize(static_cast<size_t>(byteSize));
        std::memcpy(outBytes.data(), mapped, static_cast<size_t>(byteSize));
        D3D12_RANGE wr{0, 0};
        rbBuf->Unmap(0, &wr);
        return true;
    }

    bool UploadRgba16Tex2DWH(ComPtr<ID3D12Resource>& tex, uint32_t w, uint32_t h, const std::vector<float>& rgbaFloat)
    {
        D3D12_HEAP_PROPERTIES hpDef{};
        hpDef.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width            = w;
        rd.Height           = h;
        rd.DepthOrArraySize = 1u;
        rd.MipLevels        = 1u;
        rd.Format           = DXGI_FORMAT_R16G16B16A16_FLOAT;
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
        ComPtr<ID3D12Resource> upBuf;
        if (FAILED(dev->CreateCommittedResource(&hpUp, D3D12_HEAP_FLAG_NONE, &ubd,
                                                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upBuf)))) {
            return false;
        }
        void* mapped = nullptr;
        D3D12_RANGE rr{0u, 0u};
        if (FAILED(upBuf->Map(0u, &rr, &mapped)) || !mapped) return false;
        auto* dstBase = static_cast<uint8_t*>(mapped) + fp.Offset;
        for (uint32_t y = 0u; y < h; ++y) {
            auto* dstRow = reinterpret_cast<uint16_t*>(dstBase + static_cast<size_t>(y) * fp.Footprint.RowPitch);
            for (uint32_t x = 0u; x < w; ++x) {
                const size_t src = (static_cast<size_t>(y) * w + x) * 4u;
                dstRow[x * 4u + 0u] = FloatToHalf(rgbaFloat[src + 0u]);
                dstRow[x * 4u + 1u] = FloatToHalf(rgbaFloat[src + 1u]);
                dstRow[x * 4u + 2u] = FloatToHalf(rgbaFloat[src + 2u]);
                dstRow[x * 4u + 3u] = FloatToHalf(rgbaFloat[src + 3u]);
            }
        }
        upBuf->Unmap(0u, nullptr);

        if (!BeginFrame()) return false;
        D3D12_TEXTURE_COPY_LOCATION dstLoc{};
        dstLoc.pResource        = tex.Get();
        dstLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = 0u;
        D3D12_TEXTURE_COPY_LOCATION srcLoc{};
        srcLoc.pResource       = upBuf.Get();
        srcLoc.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint = fp;
        cmd->CopyTextureRegion(&dstLoc, 0u, 0u, 0u, &srcLoc, nullptr);

        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = tex.Get();
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1u, &b);
        return FlushAndWait();
    }

    bool UploadR32Tex2D(ComPtr<ID3D12Resource>& tex, uint32_t w, uint32_t h, const std::vector<float>& data)
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
        ComPtr<ID3D12Resource> upBuf;
        if (FAILED(dev->CreateCommittedResource(&hpUp, D3D12_HEAP_FLAG_NONE, &ubd,
                                                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upBuf)))) {
            return false;
        }
        void* mapped = nullptr;
        D3D12_RANGE rr{0u, 0u};
        if (FAILED(upBuf->Map(0u, &rr, &mapped)) || !mapped) return false;
        auto* base = static_cast<uint8_t*>(mapped) + fp.Offset;
        for (uint32_t y = 0u; y < h; ++y) {
            std::memcpy(base + static_cast<size_t>(y) * fp.Footprint.RowPitch,
                        data.data() + static_cast<size_t>(y) * w,
                        static_cast<size_t>(w) * sizeof(float));
        }
        upBuf->Unmap(0u, nullptr);

        if (!BeginFrame()) return false;
        D3D12_TEXTURE_COPY_LOCATION dstLoc{};
        dstLoc.pResource        = tex.Get();
        dstLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = 0u;
        D3D12_TEXTURE_COPY_LOCATION srcLoc{};
        srcLoc.pResource       = upBuf.Get();
        srcLoc.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint = fp;
        cmd->CopyTextureRegion(&dstLoc, 0u, 0u, 0u, &srcLoc, nullptr);

        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = tex.Get();
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1u, &b);
        return FlushAndWait();
    }

    bool UploadDefaultBuffer(ComPtr<ID3D12Resource>& dstBuf, const void* data, uint64_t byteSize)
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
        ComPtr<ID3D12Resource> upBuf;
        if (FAILED(dev->CreateCommittedResource(&hpUp, D3D12_HEAP_FLAG_NONE, &rd,
                                                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upBuf)))) {
            return false;
        }
        void* mapped = nullptr;
        D3D12_RANGE rr{0u, 0u};
        if (FAILED(upBuf->Map(0u, &rr, &mapped)) || !mapped) return false;
        std::memcpy(mapped, data, static_cast<size_t>(byteSize));
        upBuf->Unmap(0u, nullptr);

        if (!BeginFrame()) return false;
        cmd->CopyBufferRegion(dstBuf.Get(), 0ull, upBuf.Get(), 0ull, byteSize);
        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = dstBuf.Get();
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1u, &b);
        return FlushAndWait();
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

struct XpbdBenchResult {
    bool        passed          = false;
    Percentiles stepGpuMs{};
    Percentiles cpuRecordMs{};
    uint32_t    chainCount      = 0u;
    uint32_t    particleCount   = 0u;
    uint32_t    substeps        = 4u;
    uint32_t    nanCount        = 0u;
    float       maxRootDrift    = 0.0f;
    float       maxStretchRatio = 0.0f;
    float       minColliderSD   = 0.0f;
    float       hostStepMs      = 0.0f;
    std::string note;
};

struct GpuCullBenchResult {
    bool        passed              = false;
    Percentiles hizGpuMs{};
    Percentiles cullGpuMs{};
    Percentiles combinedGpuMs{};
    Percentiles cpuRecordMs{};
    uint32_t    depthWidth          = 512u;
    uint32_t    depthHeight         = 512u;
    uint32_t    instanceCount       = 16384u;
    uint32_t    visibleCount        = 0u;
    uint32_t    visibleMeshlets     = 0u;
    uint32_t    frustumCulled       = 0u;
    uint32_t    distanceCulled      = 0u;
    uint32_t    hizCulled           = 0u;
    uint32_t    falseOcclusionCount = 0u;
    float       hostHiZMs           = 0.0f;
    float       hostCullMs          = 0.0f;
    std::string note;
};

struct LightClusterBenchResult {
    bool        passed           = false;
    Percentiles buildGpuMs{};
    Percentiles assignGpuMs{};
    Percentiles combinedGpuMs{};
    Percentiles cpuRecordMs{};
    uint32_t    clusterCount     = raw::bridge::RawLightClusterHost::kTotalClusters;
    uint32_t    lightCount       = 1024u;
    uint32_t    nonEmptyClusters = 0u;
    uint32_t    maxLightsCluster = 0u;
    uint32_t    totalLightLinks  = 0u;
    uint32_t    overflowErrors   = 0u;
    uint32_t    nanAabbCount     = 0u;
    float       hostBuildMs      = 0.0f;
    float       hostAssignMs     = 0.0f;
    std::string note;
};

struct VolumetricFroxelsBenchResult {
    bool        passed          = false;
    Percentiles totalGpuMs{};
    Percentiles cpuRecordMs{};
    uint32_t    gridWidth       = 160u;
    uint32_t    gridHeight      = 90u;
    uint32_t    gridDepth       = 64u;
    uint32_t    lightCount      = 16u;
    uint32_t    nanCount        = 0u;
    uint32_t    nonMonoCount    = 0u;
    float       mieForwardRatio = 0.0f;
    float       valleyTau       = 0.0f;
    float       peakTau         = 0.0f;
    float       hostInjectMs    = 0.0f;
    float       hostIntegrateMs = 0.0f;
    std::string note;
};

struct ShadowPcssBenchResult {
    bool        passed          = false;
    Percentiles pcssGpuMs{};
    Percentiles cpuRecordMs{};
    uint32_t    outWidth        = 960u;
    uint32_t    outHeight       = 540u;
    uint32_t    shadowMapSize   = 1024u;
    float       openVis         = 0.0f;
    float       nearUmbraVis    = 1.0f;
    float       farUmbraVis     = 1.0f;
    float       skyMinVis       = 0.0f;
    float       nearEdgePen     = 0.0f;
    float       farEdgePen      = 0.0f;
    float       penumbraRatio   = 0.0f;
    uint32_t    softBandPx      = 0u;
    uint32_t    nanInfCount     = 0u;
    float       hostPcssMs      = 0.0f;
    std::string note;
};

struct GtaoBenchResult {
    bool        passed          = false;
    Percentiles totalGpuMs{};
    Percentiles cpuRecordMs{};
    uint32_t    width           = 960u;
    uint32_t    height          = 540u;
    float       hostMainMs      = 0.0f;
    float       hostDenoiseMs   = 0.0f;
    float       openFloorVis    = 0.0f;
    float       openFloorBentY  = 0.0f;
    float       creviceVis      = 1.0f;
    float       creviceBentX    = 0.0f;
    float       pitVis          = 1.0f;
    uint32_t    nanCount        = 0u;
    uint32_t    skyBadCount     = 0u;
    std::string note;
};

struct SsrBenchResult {
    bool        passed           = false;
    Percentiles ssrGpuMs{};
    Percentiles cpuRecordMs{};
    uint32_t    width            = 960u;
    uint32_t    height           = 540u;
    float       hostSsrMs        = 0.0f;
    uint32_t    targetPx         = 0u;
    float       meanConf         = 0.0f;
    float       meanR            = 0.0f;
    float       meanB            = 0.0f;
    float       rbRatio          = 0.0f;
    float       maxSkyConf       = 1.0f;
    float       maxOpenFloorConf = 1.0f;
    uint32_t    mirrorSpanW      = 0u;
    uint32_t    roughSpanW       = 0u;
    uint32_t    nanInfCount      = 0u;
    std::string note;
};

struct AutoExposureBenchResult {
    bool        passed           = false;
    Percentiles totalGpuMs{};
    Percentiles cpuRecordMs{};
    uint32_t    width            = 960u;
    uint32_t    height           = 540u;
    float       hostHistMs       = 0.0f;
    float       hostAdaptMs      = 0.0f;
    float       darkExposure     = 0.0f;
    float       glacierExposure  = 0.0f;
    float       fireflyTargetLum = 0.0f;
    float       fireflyErrPct    = 100.0f;
    float       asymBrightFrac   = 0.0f;
    float       asymDarkFrac     = 0.0f;
    float       asymRatio        = 0.0f;
    bool        nanFinite        = false;
    std::string note;
};

struct CloudRaymarchBenchResult {
    bool        passed          = false;
    Percentiles cloudGpuMs{};
    Percentiles cpuRecordMs{};
    uint32_t    width           = 480u;
    uint32_t    height          = 270u;
    float       hostCloudMs     = 0.0f;
    float       clearMaxRad     = 1.0f;
    uint32_t    belowHorizonBad = 0u;
    float       minTrans        = 1.0f;
    uint32_t    cloudyPixels    = 0u;
    float       forwardLuma     = 0.0f;
    float       antiSolarLuma   = 0.0f;
    float       silverRatio     = 0.0f;
    uint32_t    nanInfCount     = 0u;
    std::string note;
};

struct PostCinematicBenchResult {
    bool        passed             = false;
    Percentiles totalGpuMs{};
    Percentiles cpuRecordMs{};
    uint32_t    width              = 960u;
    uint32_t    height             = 540u;
    float       hostTileMs         = 0.0f;
    float       hostCompMs         = 0.0f;
    float       identityMaxDiff    = 1.0f;
    float       horizStreakR       = 0.0f;
    float       vertSmearR         = 1.0f;
    float       focusStd           = 0.0f;
    float       farBlurStd         = 1.0f;
    float       maxBgBleedR        = 1.0f;
    float       vignetteFalloffPct = 0.0f;
    uint32_t    nanInfCount        = 0u;
    std::string note;
};

struct GpuParticlesBenchResult {
    bool        passed         = false;
    Percentiles totalGpuMs{};
    Percentiles cpuRecordMs{};
    uint32_t    particleCount  = 16384u;
    uint32_t    splatWidth     = 480u;
    uint32_t    splatHeight    = 270u;
    float       hostSimMs      = 0.0f;
    float       hostSplatMs    = 0.0f;
    uint32_t    activeCount    = 0u;
    uint32_t    expiredActive  = 1u;
    float       emberMeanY     = 0.0f;
    float       snowMeanY      = 0.0f;
    float       windMeanX      = 0.0f;
    uint32_t    bouncedCount   = 0u;
    float       minBounceY     = -1.0f;
    uint32_t    splattedCount  = 0u;
    uint32_t    nonZeroSplatPx = 0u;
    uint32_t    nanInfCount    = 0u;
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
// 8. GPU XPBD Soft-Body / Hair / Cloth / Bush-Bone Solver (RawXpbdSimHost)
// ----------------------------------------------------------------------------
XpbdBenchResult RunXpbdBenchmark(BenchDx12Rig& rig, uint32_t warmupFrames, uint32_t measuredFrames)
{
    XpbdBenchResult res{};
    raw::bridge::RawXpbdSimHost host;
    raw::bridge::XpbdSimDesc desc{};
    desc.maxChains    = 256u;
    desc.maxParticles = 4096u;
    std::string err;
    if (!host.Init(rig.dev.Get(), desc, &err)) {
        res.note = "RawXpbdSimHost::Init failed: " + err;
        return res;
    }
    host.SetTimestampFrequency(rig.tsFreqHz);

    std::vector<raw::bridge::XpbdParticle>  initParticles;
    std::vector<raw::bridge::XpbdChainMeta> initChains;
    raw::bridge::BuildHairAndFoliageDemoChains(initParticles, initChains, 256u);

    if (!rig.BeginFrame()) {
        res.note = "BeginFrame (upload) failed";
        return res;
    }
    if (!host.UploadChains(rig.cmd.Get(), initParticles, initChains)) {
        res.note = "RawXpbdSimHost::UploadChains failed";
        return res;
    }
    if (!rig.FlushAndWait()) {
        res.note = "FlushAndWait (upload) failed";
        return res;
    }

    res.chainCount    = host.ChainCount();
    res.particleCount = host.ParticleCount();
    res.substeps      = 4u;

    raw::bridge::XpbdStepParams xp{};
    xp.dt              = 1.0f / 60.0f;
    xp.substeps        = res.substeps;
    xp.gravityZ        = -9.80665f;
    xp.windDir[0]      = 0.92f;
    xp.windDir[1]      = 0.39f;
    xp.windDir[2]      = 0.0f;
    xp.windSpeedMps    = 8.5f;
    xp.snowLoad01      = 0.25f;
    xp.maxSpeed        = 35.0f;
    xp.collisionMargin = 0.005f;

    // 3 analytic colliders: character head sphere, torso capsule, bush push sphere
    xp.colliders[0].p0[0]  = 0.0f; xp.colliders[0].p0[1] = 0.0f; xp.colliders[0].p0[2] = 1.62f;
    xp.colliders[0].radius = 0.14f;
    xp.colliders[0].flags  = 0.0f;

    xp.colliders[1].p0[0]  = 0.0f; xp.colliders[1].p0[1] = 0.0f; xp.colliders[1].p0[2] = 1.48f;
    xp.colliders[1].p1[0]  = 0.0f; xp.colliders[1].p1[1] = 0.0f; xp.colliders[1].p1[2] = 0.88f;
    xp.colliders[1].radius = 0.22f;
    xp.colliders[1].flags  = 1.0f;

    xp.colliders[2].p0[0]  = 3.0f; xp.colliders[2].p0[1] = 0.0f; xp.colliders[2].p0[2] = 0.65f;
    xp.colliders[2].radius = 0.32f;
    xp.colliders[2].flags  = 0.0f;
    xp.colliderCount       = 3u;

    std::vector<double> gpuSamples, cpuSamples;
    gpuSamples.reserve(measuredFrames);
    cpuSamples.reserve(measuredFrames);

    const uint32_t totalFrames = warmupFrames + measuredFrames;
    for (uint32_t f = 0u; f < totalFrames; ++f) {
        const float tSec = static_cast<float>(f) * xp.dt;
        xp.timeSec   = tSec;
        xp.gustPhase = tSec * 1.3f;

        const auto c0 = Clock::now();
        if (!rig.BeginFrame()) {
            res.note = "BeginFrame failed";
            return res;
        }
        rig.MarkTimestamp(0u);
        host.Step(rig.cmd.Get(), xp);
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
            gpuSamples.push_back(gMs);
            cpuSamples.push_back(ElapsedMs(c0, c1));
        }
    }

    res.stepGpuMs   = ComputePercentiles(gpuSamples);
    res.cpuRecordMs = ComputePercentiles(cpuSamples);
    res.hostStepMs  = host.HasValidTiming() ? host.GetLastStepMs() : 0.0f;

    // GPU readback physics invariant verification (zero NaN, pinned roots, stretch ratio, collider clearance)
    std::vector<uint8_t> rawBytes;
    const uint64_t byteSize = static_cast<uint64_t>(initParticles.size()) * sizeof(raw::bridge::XpbdParticle);
    if (!rig.ReadbackBuffer(host.ParticleResource(), host.ParticleResourceState(), byteSize, rawBytes) ||
        rawBytes.size() < byteSize) {
        res.note = "GPU XPBD particle readback failed";
        return res;
    }

    std::vector<raw::bridge::XpbdParticle> outParticles(initParticles.size());
    std::memcpy(outParticles.data(), rawBytes.data(), static_cast<size_t>(byteSize));

    auto evalColliderSD = [](const float p[3], const raw::bridge::XpbdCollider& c) -> float {
        if (c.flags > 0.5f) {
            const float abx = c.p1[0] - c.p0[0];
            const float aby = c.p1[1] - c.p0[1];
            const float abz = c.p1[2] - c.p0[2];
            const float abLenSq = abx * abx + aby * aby + abz * abz;
            float t = 0.0f;
            if (abLenSq > 1.0e-12f) {
                t = ((p[0] - c.p0[0]) * abx + (p[1] - c.p0[1]) * aby + (p[2] - c.p0[2]) * abz) / abLenSq;
                t = std::clamp(t, 0.0f, 1.0f);
            }
            const float dx = p[0] - (c.p0[0] + abx * t);
            const float dy = p[1] - (c.p0[1] + aby * t);
            const float dz = p[2] - (c.p0[2] + abz * t);
            return std::sqrt(dx * dx + dy * dy + dz * dz) - c.radius;
        }
        const float dx = p[0] - c.p0[0];
        const float dy = p[1] - c.p0[1];
        const float dz = p[2] - c.p0[2];
        return std::sqrt(dx * dx + dy * dy + dz * dz) - c.radius;
    };

    res.minColliderSD   = 1.0e9f;
    res.maxRootDrift    = 0.0f;
    res.maxStretchRatio = 1.0f;

    for (size_t i = 0u; i < outParticles.size(); ++i) {
        const auto& pt = outParticles[i];
        const bool finite = std::isfinite(pt.pos[0]) && std::isfinite(pt.pos[1]) && std::isfinite(pt.pos[2]) &&
                            std::isfinite(pt.prevPos[0]) && std::isfinite(pt.prevPos[1]) && std::isfinite(pt.prevPos[2]) &&
                            std::isfinite(pt.vel[0]) && std::isfinite(pt.vel[1]) && std::isfinite(pt.vel[2]);
        if (!finite) {
            ++res.nanCount;
            continue;
        }
        if (initParticles[i].invMass == 0.0f) {
            const float dx = pt.pos[0] - initParticles[i].pos[0];
            const float dy = pt.pos[1] - initParticles[i].pos[1];
            const float dz = pt.pos[2] - initParticles[i].pos[2];
            res.maxRootDrift = std::max(res.maxRootDrift, std::sqrt(dx * dx + dy * dy + dz * dz));
        } else {
            for (uint32_t k = 0u; k < xp.colliderCount; ++k) {
                const float sd = evalColliderSD(pt.pos, xp.colliders[k]);
                if (sd < res.minColliderSD) res.minColliderSD = sd;
            }
        }
    }

    for (const auto& ch : initChains) {
        for (uint32_t n = 1u; n < ch.nodeCount; ++n) {
            const uint32_t i0 = ch.startParticle + n - 1u;
            const uint32_t i1 = ch.startParticle + n;
            const auto& a = outParticles[i0];
            const auto& b = outParticles[i1];
            const float dx = b.pos[0] - a.pos[0];
            const float dy = b.pos[1] - a.pos[1];
            const float dz = b.pos[2] - a.pos[2];
            const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (b.restLen > 1.0e-6f && std::isfinite(len)) {
                res.maxStretchRatio = std::max(res.maxStretchRatio, len / b.restLen);
            }
        }
    }

    const bool budgetOk = rig.isWarp ? true : (res.stepGpuMs.mean < 0.25);
    const bool physOk   = (res.nanCount == 0u) && (res.maxRootDrift < 1.0e-5f) &&
                          (res.maxStretchRatio < 1.03f) && (res.minColliderSD >= -1.0e-2f);
    res.passed = budgetOk && physOk;
    if (!physOk) {
        res.note = "XPBD constraint/collider/NaN invariant failed";
    } else if (!budgetOk) {
        res.note = "HW GPU XPBD step mean exceeded 0.25 ms budget";
    } else {
        res.note = rig.isWarp ? "WARP 4096p XPBD constraints & colliders PASS"
                              : "HW GPU < 0.25 ms & 4096p XPBD invariants PASS";
    }
    return res;
}

// ----------------------------------------------------------------------------
// 9. GPU Conservative Hi-Z Pyramid + 16,384-Instance Culler (RawGpuCullHost)
// ----------------------------------------------------------------------------
GpuCullBenchResult RunGpuCullBenchmark(BenchDx12Rig& rig, uint32_t warmupFrames, uint32_t measuredFrames)
{
    GpuCullBenchResult res{};
    raw::bridge::RawGpuCullHost host;
    raw::bridge::GpuCullDesc desc{};
    desc.maxInstances     = 65536u;
    desc.initialHiZWidth  = res.depthWidth;
    desc.initialHiZHeight = res.depthHeight;
    std::string err;
    if (!host.Init(rig.dev.Get(), desc, &err)) {
        res.note = "RawGpuCullHost::Init failed: " + err;
        return res;
    }
    host.SetTimestampFrequency(rig.tsFreqHz);

    // Build 512x512 depth buffer with giant z=0.25 occluder wall covering UV [0.15, 0.85]^2
    std::vector<float> depthImg(static_cast<size_t>(res.depthWidth) * res.depthHeight, 1.0f);
    const uint32_t wallMinPx = static_cast<uint32_t>(0.15f * res.depthWidth);
    const uint32_t wallMaxPx = static_cast<uint32_t>(0.85f * res.depthWidth);
    for (uint32_t y = wallMinPx; y <= wallMaxPx; ++y) {
        for (uint32_t x = wallMinPx; x <= wallMaxPx; ++x) {
            depthImg[static_cast<size_t>(y) * res.depthWidth + x] = 0.25f;
        }
    }
    ComPtr<ID3D12Resource> depthTex;
    if (!rig.UploadR32Tex2D(depthTex, res.depthWidth, res.depthHeight, depthImg)) {
        res.note = "Upload 512x512 Hi-Z occluder depth failed";
        return res;
    }

    constexpr uint32_t kFrustumOut = 4096u;
    constexpr uint32_t kDistCull   = 1024u;
    constexpr uint32_t kBehindWall = 6144u;
    constexpr uint32_t kInFront    = 2560u;
    constexpr uint32_t kPeeking    = 1536u;
    constexpr uint32_t kNearStrad  = 1024u;
    constexpr uint32_t kTotalInst  = kFrustumOut + kDistCull + kBehindWall + kInFront + kPeeking + kNearStrad;
    constexpr uint32_t kExpVis     = kInFront + kPeeking + kNearStrad; // 5,120

    std::vector<raw::bridge::GpuCullInstance> instances;
    instances.reserve(kTotalInst);
    std::vector<uint8_t> expectedVis(kTotalInst, 0u);
    uint32_t expectedMeshlets = 0u;

    auto pushBox = [&](float x0, float y0, float z0, float x1, float y1, float z1, float maxDist, bool vis) {
        const uint32_t id = static_cast<uint32_t>(instances.size());
        const uint32_t mc = (id % 7u) + 1u;
        raw::bridge::GpuCullInstance g{};
        g.aabbMin[0] = x0; g.aabbMin[1] = y0; g.aabbMin[2] = z0;
        g.aabbMax[0] = x1; g.aabbMax[1] = y1; g.aabbMax[2] = z1;
        g.center[0]  = 0.5f * (x0 + x1);
        g.center[1]  = 0.5f * (y0 + y1);
        g.center[2]  = 0.5f * (z0 + z1);
        const float hx = 0.5f * (x1 - x0), hy = 0.5f * (y1 - y0), hz = 0.5f * (z1 - z0);
        g.radius        = std::sqrt(hx * hx + hy * hy + hz * hz);
        g.maxDrawDist   = maxDist;
        g.meshletOffset = id * 8u;
        g.meshletCount  = mc;
        g.instanceId    = id;
        expectedVis[id] = vis ? 1u : 0u;
        if (vis) expectedMeshlets += mc;
        instances.push_back(g);
    };

    for (uint32_t i = 0u; i < kFrustumOut; ++i) {
        const float t = static_cast<float>(i % 64u) / 64.0f;
        const uint32_t side = i % 6u;
        float cx = 0.0f, cy = 0.0f, cz = 10.0f;
        if (side == 0u)      cz = -10.0f - t * 20.0f;
        else if (side == 1u) cx = -25.0f - t * 10.0f;
        else if (side == 2u) cx =  25.0f + t * 10.0f;
        else if (side == 3u) cy =  25.0f + t * 10.0f;
        else if (side == 4u) cy = -25.0f - t * 10.0f;
        else                 cz = 1150.0f + t * 200.0f;
        pushBox(cx - 0.5f, cy - 0.5f, cz - 0.5f, cx + 0.5f, cy + 0.5f, cz + 0.5f, 5000.0f, false);
    }
    for (uint32_t i = 0u; i < kDistCull; ++i) {
        const float cz = 50.0f + static_cast<float>(i % 32u);
        const float cx = 0.85f * cz;
        const float cy = 0.85f * cz;
        pushBox(cx - 0.5f, cy - 0.5f, cz - 0.5f, cx + 0.5f, cy + 0.5f, cz + 0.5f, 15.0f, false);
    }
    for (uint32_t i = 0u; i < kBehindWall; ++i) {
        const float u  = (static_cast<float>(i % 64u) / 63.0f) * 2.0f - 1.0f;
        const float v  = (static_cast<float>((i / 64u) % 64u) / 63.0f) * 2.0f - 1.0f;
        const float cz = 3.0f + static_cast<float>((i / 4096u) % 8u) * 4.0f + static_cast<float>(i % 13u) * 0.2f;
        const float cx = u * 0.50f * cz;
        const float cy = v * 0.50f * cz;
        const float h  = 0.04f * cz;
        pushBox(cx - h, cy - h, cz - 0.1f, cx + h, cy + h, cz + 0.1f, 5000.0f, false);
    }
    for (uint32_t i = 0u; i < kInFront; ++i) {
        const float u  = (static_cast<float>(i % 64u) / 63.0f) * 2.0f - 1.0f;
        const float v  = (static_cast<float>((i / 64u) % 40u) / 39.0f) * 2.0f - 1.0f;
        const float cz = 1.10f + static_cast<float>(i % 10u) * 0.01f;
        const float cx = u * 0.50f * cz;
        const float cy = v * 0.50f * cz;
        pushBox(cx - 0.02f, cy - 0.02f, cz - 0.02f, cx + 0.02f, cy + 0.02f, cz + 0.02f, 5000.0f, true);
    }
    for (uint32_t i = 0u; i < kPeeking; ++i) {
        const float cz = 4.0f + static_cast<float>(i % 32u) * 0.5f;
        const float cy = ((static_cast<float>(i % 48u) / 47.0f) * 2.0f - 1.0f) * 0.60f * cz;
        const float sx = ((i & 1u) != 0u) ? 1.0f : -1.0f;
        const float cx = sx * 0.72f * cz;
        const float hx = 0.12f * cz;
        pushBox(cx - hx, cy - 0.1f, cz - 0.1f, cx + hx, cy + 0.1f, cz + 0.1f, 5000.0f, true);
    }
    for (uint32_t i = 0u; i < kNearStrad; ++i) {
        const float u  = (static_cast<float>(i % 32u) / 31.0f) * 2.0f - 1.0f;
        const float v  = (static_cast<float>((i / 32u) % 32u) / 31.0f) * 2.0f - 1.0f;
        const float cx = u * 0.45f;
        const float cy = v * 0.45f;
        pushBox(cx - 0.05f, cy - 0.05f, 0.70f, cx + 0.05f, cy + 0.05f, 1.30f, 5000.0f, true);
    }

    ComPtr<ID3D12Resource> instBuf;
    if (!rig.UploadDefaultBuffer(instBuf, instances.data(), instances.size() * sizeof(raw::bridge::GpuCullInstance))) {
        res.note = "Upload 16,384 GpuCullInstance buffer failed";
        return res;
    }

    raw::bridge::GpuCullViewParams vp{};
    const float nearZ = 1.0f, farZ = 1000.0f, q = farZ / (farZ - nearZ);
    vp.viewProj[0 * 4 + 0] = 1.0f;
    vp.viewProj[1 * 4 + 1] = 1.0f;
    vp.viewProj[2 * 4 + 2] = q;
    vp.viewProj[2 * 4 + 3] = 1.0f;
    vp.viewProj[3 * 4 + 2] = -nearZ * q;
    vp.autoExtractPlanes        = true;
    vp.depthBias                = 1.0e-4f;
    vp.defaultIndexCountPerInst = 36u;
    vp.enableFrustumCull        = true;
    vp.enableDistanceCull       = true;
    vp.enableHiZCull            = true;

    std::vector<double> hizSamples, cullSamples, combSamples, cpuSamples;
    hizSamples.reserve(measuredFrames);
    cullSamples.reserve(measuredFrames);
    combSamples.reserve(measuredFrames);
    cpuSamples.reserve(measuredFrames);

    const uint32_t totalFrames = warmupFrames + measuredFrames;
    for (uint32_t f = 0u; f < totalFrames; ++f) {
        const auto c0 = Clock::now();
        if (!rig.BeginFrame()) {
            res.note = "BeginFrame failed";
            return res;
        }
        rig.MarkTimestamp(0u);
        host.BuildHiZPyramid(rig.cmd.Get(), depthTex.Get(), res.depthWidth, res.depthHeight);
        rig.MarkTimestamp(1u);
        host.CullInstances(rig.cmd.Get(), instBuf.Get(), kTotalInst, vp);
        rig.MarkTimestamp(2u);
        rig.ResolveTimestamps(0u, 3u);
        const auto c1 = Clock::now();
        if (!rig.FlushAndWait()) {
            res.note = "FlushAndWait failed";
            return res;
        }
        if (f >= warmupFrames) {
            double hMs = 0.0, cMs = 0.0, totMs = 0.0;
            rig.ReadTimestampRangeMs(0u, 1u, hMs);
            rig.ReadTimestampRangeMs(1u, 2u, cMs);
            rig.ReadTimestampRangeMs(0u, 2u, totMs);
            hizSamples.push_back(hMs);
            cullSamples.push_back(cMs);
            combSamples.push_back(totMs);
            cpuSamples.push_back(ElapsedMs(c0, c1));
        }
    }

    res.hizGpuMs      = ComputePercentiles(hizSamples);
    res.cullGpuMs     = ComputePercentiles(cullSamples);
    res.combinedGpuMs = ComputePercentiles(combSamples);
    res.cpuRecordMs   = ComputePercentiles(cpuSamples);
    res.hostHiZMs     = host.HasValidHiZTiming() ? host.GetLastHiZMs() : 0.0f;
    res.hostCullMs    = host.HasValidCullTiming() ? host.GetLastCullMs() : 0.0f;

    std::vector<uint8_t> cntBytes, argBytes, visBytes;
    if (!rig.ReadbackBuffer(host.GetVisibleCountBuffer(), host.VisibleCountResourceState(), sizeof(raw::bridge::GpuCullStats), cntBytes) ||
        !rig.ReadbackBuffer(host.GetIndirectArgsBuffer(), host.IndirectArgsResourceState(), sizeof(raw::bridge::GpuCullIndirectArgsLayout), argBytes) ||
        !rig.ReadbackBuffer(host.GetVisibleInstancesBuffer(), host.VisibleInstancesResourceState(),
                            static_cast<uint64_t>(kTotalInst) * sizeof(uint32_t), visBytes)) {
        res.note = "GPU Cull readback failed";
        return res;
    }

    raw::bridge::GpuCullStats stats{};
    raw::bridge::GpuCullIndirectArgsLayout args{};
    std::memcpy(&stats, cntBytes.data(), sizeof(stats));
    std::memcpy(&args, argBytes.data(), sizeof(args));

    res.visibleCount    = stats.visibleInstances;
    res.visibleMeshlets = stats.visibleMeshlets;
    res.frustumCulled   = stats.frustumCulled;
    res.distanceCulled  = stats.distanceCulled;
    res.hizCulled       = stats.hizCulled;

    std::vector<uint8_t> seen(kTotalInst, 0u);
    uint32_t falseSurv = 0u, dupCount = 0u;
    const uint32_t checkN = std::min(stats.visibleInstances, kTotalInst);
    const auto* visIds = reinterpret_cast<const uint32_t*>(visBytes.data());
    for (uint32_t i = 0u; i < checkN; ++i) {
        const uint32_t id = visIds[i];
        if (id >= kTotalInst) { ++falseSurv; continue; }
        if (seen[id]) ++dupCount;
        seen[id] = 1u;
        if (!expectedVis[id]) ++falseSurv;
    }
    uint32_t missed = 0u;
    for (uint32_t i = 0u; i < kTotalInst; ++i) {
        if (expectedVis[i] && !seen[i]) ++missed;
    }
    res.falseOcclusionCount = missed;

    const bool countsOk = (stats.visibleInstances == kExpVis) &&
                          (stats.frustumCulled == kFrustumOut) &&
                          (stats.distanceCulled == kDistCull) &&
                          (stats.hizCulled == kBehindWall) &&
                          (args.drawIndexed.InstanceCount == kExpVis) &&
                          (args.dispatchMeshX == expectedMeshlets) &&
                          (falseSurv == 0u) && (dupCount == 0u) && (missed == 0u);
    const bool budgetOk = rig.isWarp ? true : (res.combinedGpuMs.mean < 0.15);
    res.passed = countsOk && budgetOk;
    if (!countsOk) {
        res.note = "Hi-Z/Frustum cull count or zero-false-occlusion invariant failed";
    } else if (!budgetOk) {
        res.note = "HW GPU Hi-Z + Cull combined mean exceeded 0.15 ms budget";
    } else {
        res.note = rig.isWarp ? "WARP exact 5120/4096/1024/6144 & 0% false occlusion PASS"
                              : "HW GPU < 0.15 ms & 0% false occlusion PASS";
    }
    return res;
}

// ----------------------------------------------------------------------------
// 10. GPU 16x9x24 3D Clustered 1,024-Light Grid (RawLightClusterHost)
// ----------------------------------------------------------------------------
LightClusterBenchResult RunLightClusterBenchmark(BenchDx12Rig& rig, uint32_t warmupFrames, uint32_t measuredFrames)
{
    LightClusterBenchResult res{};
    raw::bridge::RawLightClusterHost host;
    raw::bridge::LightClusterDesc desc{};
    desc.maxLights = 1024u;
    std::string err;
    if (!host.Init(rig.dev.Get(), desc, &err)) {
        res.note = "RawLightClusterHost::Init failed: " + err;
        return res;
    }
    host.SetTimestampFrequency(rig.tsFreqHz);

    raw::bridge::LightClusterViewParams view{};
    view.nearZ = 0.5f;
    view.farZ  = 500.0f;
    raw::bridge::BuildPerspectiveInvProjLH(1.04719755f, 16.0f / 9.0f, view.nearZ, view.farZ, view.invProj);

    // Deterministic 1,024 dynamic point + spotlight mix spanning full 16x9x24 logarithmic frustum
    std::vector<raw::bridge::GpuClusterLight> lights(res.lightCount);
    uint32_t seed = 0x51A7C0DEu;
    auto NextF = [&seed]() -> float {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>((seed >> 8) & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
    };
    for (uint32_t i = 0u; i < res.lightCount; ++i) {
        const float z = 1.5f * std::pow(420.0f / 1.5f, NextF());
        const float halfH = z * std::tan(1.04719755f * 0.5f);
        const float halfW = halfH * (16.0f / 9.0f);
        auto& l = lights[i];
        l.posView[0]  = (NextF() * 2.0f - 1.0f) * halfW * 1.15f;
        l.posView[1]  = (NextF() * 2.0f - 1.0f) * halfH * 1.15f;
        l.posView[2]  = z;
        l.radius      = 1.5f + NextF() * (0.08f * z + 6.0f);
        l.colorRgb[0] = 0.5f + NextF() * 2.0f;
        l.colorRgb[1] = 0.4f + NextF() * 1.6f;
        l.colorRgb[2] = 0.3f + NextF() * 1.8f;
        if ((i & 3u) == 0u) {
            const float dx = NextF() * 2.0f - 1.0f;
            const float dy = NextF() * 2.0f - 1.0f;
            const float dz = NextF() * 2.0f - 1.0f;
            const float invL = 1.0f / std::max(std::sqrt(dx * dx + dy * dy + dz * dz), 1.0e-5f);
            l.dirView[0]   = dx * invL;
            l.dirView[1]   = dy * invL;
            l.dirView[2]   = dz * invL;
            l.cosOuterCone = 0.5f + NextF() * 0.42f;
            l.typeFlags    = raw::bridge::kClusterLightSpotBit | raw::bridge::kClusterLightSpell;
        } else {
            l.dirView[0]   = 0.0f;
            l.dirView[1]   = 0.0f;
            l.dirView[2]   = 1.0f;
            l.cosOuterCone = -1.0f;
            l.typeFlags    = ((i & 1u) == 0u) ? raw::bridge::kClusterLightFire : raw::bridge::kClusterLightPoint;
        }
    }

    std::vector<double> buildSamples, assignSamples, combSamples, cpuSamples;
    buildSamples.reserve(measuredFrames);
    assignSamples.reserve(measuredFrames);
    combSamples.reserve(measuredFrames);
    cpuSamples.reserve(measuredFrames);

    const uint32_t totalFrames = warmupFrames + measuredFrames;
    for (uint32_t f = 0u; f < totalFrames; ++f) {
        const auto c0 = Clock::now();
        if (!rig.BeginFrame()) {
            res.note = "BeginFrame failed";
            return res;
        }
        rig.MarkTimestamp(0u);
        host.BuildAndAssign(rig.cmd.Get(), lights.data(), res.lightCount, view);
        rig.MarkTimestamp(1u);
        rig.ResolveTimestamps(0u, 2u);
        const auto c1 = Clock::now();
        if (!rig.FlushAndWait()) {
            res.note = "FlushAndWait failed";
            return res;
        }
        if (f >= warmupFrames) {
            double totMs = 0.0;
            rig.ReadTimestampRangeMs(0u, 1u, totMs);
            combSamples.push_back(totMs);
            if (host.HasValidTiming()) {
                buildSamples.push_back(static_cast<double>(host.GetLastBuildMs()));
                assignSamples.push_back(static_cast<double>(host.GetLastAssignMs()));
            }
            cpuSamples.push_back(ElapsedMs(c0, c1));
        }
    }

    res.combinedGpuMs = ComputePercentiles(combSamples);
    res.buildGpuMs    = buildSamples.empty() ? Percentiles{} : ComputePercentiles(buildSamples);
    res.assignGpuMs   = assignSamples.empty() ? Percentiles{} : ComputePercentiles(assignSamples);
    res.cpuRecordMs   = ComputePercentiles(cpuSamples);
    res.hostBuildMs   = host.HasValidTiming() ? host.GetLastBuildMs() : 0.0f;
    res.hostAssignMs  = host.HasValidTiming() ? host.GetLastAssignMs() : 0.0f;

    const uint64_t aabbBytes = static_cast<uint64_t>(res.clusterCount) * sizeof(raw::bridge::ClusterAabb);
    const uint64_t gridBytes = static_cast<uint64_t>(res.clusterCount) * sizeof(raw::bridge::ClusterLightGridEntry);
    const uint64_t idxBytes  = static_cast<uint64_t>(raw::bridge::RawLightClusterHost::kTotalIndexEntries) * sizeof(uint32_t);
    std::vector<uint8_t> aabbRaw, gridRaw, idxRaw;
    if (!rig.ReadbackBuffer(host.ClusterAabbResource(), host.ClusterAabbState(), aabbBytes, aabbRaw) ||
        !rig.ReadbackBuffer(host.ClusterGridResource(), host.ClusterGridState(), gridBytes, gridRaw) ||
        !rig.ReadbackBuffer(host.LightIndicesResource(), host.LightIndicesState(), idxBytes, idxRaw)) {
        res.note = "GPU LightClusters readback failed";
        return res;
    }

    const auto* aabbs = reinterpret_cast<const raw::bridge::ClusterAabb*>(aabbRaw.data());
    const auto* grid  = reinterpret_cast<const raw::bridge::ClusterLightGridEntry*>(gridRaw.data());
    const auto* idxs  = reinterpret_cast<const uint32_t*>(idxRaw.data());

    for (uint32_t c = 0u; c < res.clusterCount; ++c) {
        for (int k = 0; k < 3; ++k) {
            if (!std::isfinite(aabbs[c].minBounds[k]) || !std::isfinite(aabbs[c].maxBounds[k]) ||
                aabbs[c].minBounds[k] > aabbs[c].maxBounds[k]) {
                ++res.nanAabbCount;
            }
        }
        const auto& e = grid[c];
        if (e.count > raw::bridge::RawLightClusterHost::kMaxLightsPerCluster ||
            e.offset != c * raw::bridge::RawLightClusterHost::kMaxLightsPerCluster) {
            ++res.overflowErrors;
        }
        if (e.count > 0u) ++res.nonEmptyClusters;
        res.maxLightsCluster = std::max(res.maxLightsCluster, e.count);
        res.totalLightLinks += e.count;
        for (uint32_t k = 0u; k < std::min(e.count, raw::bridge::RawLightClusterHost::kMaxLightsPerCluster); ++k) {
            if (idxs[e.offset + k] >= res.lightCount) ++res.overflowErrors;
        }
    }

    const bool validOk  = (res.overflowErrors == 0u) && (res.nanAabbCount == 0u) &&
                          (res.nonEmptyClusters > 500u) && (res.totalLightLinks > 1000u);
    const bool budgetOk = rig.isWarp ? true : (res.combinedGpuMs.mean < 0.15);
    res.passed = validOk && budgetOk;
    if (!validOk) {
        res.note = "3D clustered light overflow/NaN/coverage invariant failed";
    } else if (!budgetOk) {
        res.note = "HW GPU 1,024-light cluster grid mean exceeded 0.15 ms budget";
    } else {
        res.note = rig.isWarp ? "WARP 3456-cluster 1024-light assignment & 0 overflow PASS"
                              : "HW GPU < 0.15 ms & 0 overflow/NaN PASS";
    }
    return res;
}

// ----------------------------------------------------------------------------
// 11. GPU 160x90x64 3D Volumetric Froxel Fog & Mie Scattering (RawVolumetricFroxelHost)
// ----------------------------------------------------------------------------
VolumetricFroxelsBenchResult RunVolumetricFroxelsBenchmark(BenchDx12Rig& rig, uint32_t warmupFrames, uint32_t measuredFrames)
{
    VolumetricFroxelsBenchResult res{};
    raw::bridge::RawVolumetricFroxelHost host;
    raw::bridge::VolumetricFroxelDesc desc{};
    desc.gridWidth  = res.gridWidth;
    desc.gridHeight = res.gridHeight;
    desc.gridDepth  = res.gridDepth;
    std::string err;
    if (!host.Init(rig.dev.Get(), desc, &err)) {
        res.note = "RawVolumetricFroxelHost::Init failed: " + err;
        return res;
    }
    host.SetTimestampFrequency(rig.tsFreqHz);

    auto makeZUpParams = [](float camZ) -> raw::bridge::VolumetricFroxelParams {
        raw::bridge::VolumetricFroxelParams p{};
        const float fovYDeg = 60.0f;
        const float aspect  = 16.0f / 9.0f;
        const float nearZ   = 1.0f;
        const float farZ    = 300.0f;
        const float sy      = 1.0f / std::tan(fovYDeg * 0.5f * 3.14159265359f / 180.0f);
        const float sx      = sy / aspect;
        const float q       = farZ / (farZ - nearZ);
        const float invQ    = 1.0f / q;

        p.prevViewProj[0 * 4 + 0] = sx;
        p.prevViewProj[1 * 4 + 2] = q;
        p.prevViewProj[1 * 4 + 3] = 1.0f;
        p.prevViewProj[2 * 4 + 1] = sy;
        p.prevViewProj[3 * 4 + 1] = -camZ * sy;
        p.prevViewProj[3 * 4 + 2] = -nearZ * q;

        p.invViewProj[0 * 4 + 0] = nearZ / sx;
        p.invViewProj[1 * 4 + 2] = nearZ / sy;
        p.invViewProj[2 * 4 + 2] = -camZ * invQ;
        p.invViewProj[2 * 4 + 3] = -invQ;
        p.invViewProj[3 * 4 + 1] = nearZ;
        p.invViewProj[3 * 4 + 2] = camZ;
        p.invViewProj[3 * 4 + 3] = 1.0f;

        p.cameraPosW[2]         = camZ;
        p.cameraForwardW[1]     = 1.0f;
        p.prevCameraPosW[2]     = camZ;
        p.prevCameraForwardW[1] = 1.0f;
        p.nearZ                 = nearZ;
        p.farZ                  = farZ;
        p.baseFogDensity        = 0.018f;
        p.valleyFloorZ          = 0.0f;
        p.fogScaleHeight        = 25.0f;
        p.scatterAlbedo         = 0.90f;
        p.mieAnisotropyG        = 0.68f;
        p.sunDirW[1]            = 1.0f;
        p.sunIntensity          = 8.0f;
        p.sunColor[0]           = 1.0f; p.sunColor[1] = 0.90f; p.sunColor[2] = 0.72f;
        p.ambientIntensity      = 0.25f;
        p.ambientColor[0]       = 0.30f; p.ambientColor[1] = 0.45f; p.ambientColor[2] = 0.70f;
        return p;
    };

    // 1. Timed full-load pass: 16 local lights + 3D wind noise + rain + 0.85 temporal reprojection
    auto pFull = makeZUpParams(30.0f);
    pFull.enableTemporal     = true;
    pFull.enableHaltonJitter = true;
    pFull.historyWeight      = 0.85f;
    pFull.noiseAmp           = 0.25f;
    pFull.rainIntensity      = 0.35f;
    pFull.lightCount         = 16u;
    for (uint32_t i = 0u; i < 16u; ++i) {
        const float a = static_cast<float>(i) * 0.392699f;
        pFull.lights[i].posW[0]   = std::sin(a) * 35.0f;
        pFull.lights[i].posW[1]   = 15.0f + static_cast<float>(i) * 10.0f;
        pFull.lights[i].posW[2]   = 20.0f + std::cos(a) * 10.0f;
        pFull.lights[i].radius    = 28.0f;
        pFull.lights[i].color[0]  = 1.0f;
        pFull.lights[i].color[1]  = 0.60f;
        pFull.lights[i].color[2]  = 0.22f;
        pFull.lights[i].intensity = 240.0f;
    }

    std::vector<double> gpuSamples, cpuSamples;
    gpuSamples.reserve(measuredFrames);
    cpuSamples.reserve(measuredFrames);

    const uint32_t totalFrames = warmupFrames + measuredFrames;
    for (uint32_t f = 0u; f < totalFrames; ++f) {
        pFull.simTimeSec = static_cast<float>(f) * (1.0f / 60.0f);
        const auto c0 = Clock::now();
        if (!rig.BeginFrame()) {
            res.note = "BeginFrame failed";
            return res;
        }
        rig.MarkTimestamp(0u);
        host.Execute(rig.cmd.Get(), pFull);
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
            gpuSamples.push_back(gMs);
            cpuSamples.push_back(ElapsedMs(c0, c1));
        }
    }

    res.totalGpuMs      = ComputePercentiles(gpuSamples);
    res.cpuRecordMs     = ComputePercentiles(cpuSamples);
    res.hostInjectMs    = host.HasValidTiming() ? host.GetLastInjectMs() : 0.0f;
    res.hostIntegrateMs = host.HasValidTiming() ? host.GetLastIntegrateMs() : 0.0f;

    // 2. Optical & Beer-Lambert readback invariants (forward-sun vs anti-solar + altitude stratification)
    auto pFwd = makeZUpParams(30.0f);
    pFwd.enableTemporal     = false;
    pFwd.enableHaltonJitter = false;
    pFwd.historyWeight      = 0.0f;
    pFwd.noiseAmp           = 0.0f;
    host.ResetHistory();
    if (!rig.BeginFrame() || !host.Execute(rig.cmd.Get(), pFwd) || !rig.FlushAndWait()) {
        res.note = "Forward-sun invariant pass failed";
        return res;
    }
    std::vector<float> volFwd, volAnti;
    uint32_t w = 0u, h = 0u, d = 0u;
    if (!rig.ReadbackRgba16Tex(host.GetIntegratedVolumeResource(), host.IntegratedVolumeResourceState(), w, h, d, volFwd)) {
        res.note = "Forward-sun 3D froxel readback failed";
        return res;
    }

    auto sampleAt = [&](const std::vector<float>& v, uint32_t x, uint32_t y, uint32_t z, uint32_t ch) -> float {
        return v[((static_cast<size_t>(z) * h + y) * w + x) * 4u + ch];
    };
    auto lumaAt = [&](const std::vector<float>& v, uint32_t x, uint32_t y, uint32_t z) -> float {
        return 0.2126f * sampleAt(v, x, y, z, 0u) +
               0.7152f * sampleAt(v, x, y, z, 1u) +
               0.0722f * sampleAt(v, x, y, z, 2u);
    };

    uint32_t badT = 0u;
    for (float v : volFwd) {
        if (!std::isfinite(v)) ++res.nanCount;
    }
    for (uint32_t y = 0u; y < h; ++y) {
        for (uint32_t x = 0u; x < w; ++x) {
            float prevT = 1.002f;
            for (uint32_t z = 0u; z < d; ++z) {
                const float T = sampleAt(volFwd, x, y, z, 3u);
                if (!(T > 0.0f && T <= 1.001f)) ++badT;
                if (T > prevT + 2.5e-3f) ++res.nonMonoCount;
                prevT = T;
            }
        }
    }

    const float tValley = sampleAt(volFwd, 80u, 82u, d - 1u, 3u);
    const float tPeak   = sampleAt(volFwd, 80u, 8u, d - 1u, 3u);
    res.valleyTau = -std::log(std::max(tValley, 1.0e-5f));
    res.peakTau   = -std::log(std::max(tPeak, 1.0e-5f));

    auto pAnti = pFwd;
    pAnti.sunDirW[1] = -1.0f;
    host.ResetHistory();
    if (!rig.BeginFrame() || !host.Execute(rig.cmd.Get(), pAnti) || !rig.FlushAndWait() ||
        !rig.ReadbackRgba16Tex(host.GetIntegratedVolumeResource(), host.IntegratedVolumeResourceState(), w, h, d, volAnti)) {
        res.note = "Anti-solar 3D froxel pass/readback failed";
        return res;
    }
    const float fwdLuma  = lumaAt(volFwd, 80u, 45u, 48u);
    const float antiLuma = lumaAt(volAnti, 80u, 45u, 48u);
    res.mieForwardRatio  = (antiLuma > 1.0e-6f) ? (fwdLuma / antiLuma) : 0.0f;

    const bool optOk    = (res.nanCount == 0u) && (badT == 0u) && (res.nonMonoCount == 0u) &&
                          (res.mieForwardRatio > 2.0f) && (res.valleyTau > res.peakTau * 2.5f);
    const bool budgetOk = rig.isWarp ? true : (res.totalGpuMs.mean < 0.25);
    res.passed = optOk && budgetOk;
    if (!optOk) {
        res.note = "Volumetric froxel Beer-Lambert/Mie/stratification invariant failed";
    } else if (!budgetOk) {
        res.note = "HW GPU 160x90x64 volumetric froxels mean exceeded 0.25 ms budget";
    } else {
        res.note = rig.isWarp ? "WARP 921.6k froxels Beer-Lambert & Mie PASS"
                              : "HW GPU < 0.25 ms & 921.6k froxels Beer-Lambert/Mie PASS";
    }
    return res;
}

// ----------------------------------------------------------------------------
// 12. GPU Variable-Penumbra PCSS Contact-Hardening Shadows (RawShadowPcssHost)
// ----------------------------------------------------------------------------
ShadowPcssBenchResult RunShadowPcssBenchmark(BenchDx12Rig& rig, uint32_t warmupFrames, uint32_t measuredFrames)
{
    ShadowPcssBenchResult res{};
    raw::bridge::RawShadowPcssHost host;
    std::string err;
    if (!host.Init(rig.dev.Get(), {}, &err)) {
        res.note = "RawShadowPcssHost::Init failed: " + err;
        return res;
    }
    host.SetTimestampFrequency(rig.tsFreqHz);

    const uint32_t kW = res.outWidth;
    const uint32_t kH = res.outHeight;
    const uint32_t kSmRes = res.shadowMapSize;

    const float targetW[3] = {0.0f, 0.0f, 0.0f};
    const float sunDir[3]  = {0.0f, 0.0f, 1.0f};
    const raw::bridge::TexelSnappedCascade cascade =
        raw::bridge::BuildTexelSnappedCascade(targetW, sunDir, 32.0f, 64.0f, kSmRes);

    auto mulRow4 = [](const float v[4], const float m[16], float out[4]) {
        for (int c = 0; c < 4; ++c) {
            out[c] = v[0] * m[0 * 4 + c] + v[1] * m[1 * 4 + c] + v[2] * m[2 * 4 + c] + v[3] * m[3 * 4 + c];
        }
    };

    raw::bridge::ShadowPcssParams params{};
    std::memset(params.invViewProj, 0, sizeof(params.invViewProj));
    params.invViewProj[0 * 4 + 0] = 24.0f;
    params.invViewProj[1 * 4 + 1] = 13.5f;
    params.invViewProj[2 * 4 + 2] = -40.0f;
    params.invViewProj[3 * 4 + 2] = 20.0f;
    params.invViewProj[3 * 4 + 3] = 1.0f;
    std::memcpy(params.lightViewProj, cascade.lightViewProj, sizeof(params.lightViewProj));
    params.sunDirW[0]          = sunDir[0];
    params.sunDirW[1]          = sunDir[1];
    params.sunDirW[2]          = sunDir[2];
    params.baseDepthBias       = 5.0e-4f;
    params.slopeDepthBias      = 5.0e-4f;
    params.normalOffsetWorld   = 0.0f;
    params.lightAngularSize    = 14.0f;
    params.blockerSearchTexels = 16.0f;
    params.minFilterTexels     = 0.75f;
    params.maxFilterTexels     = 24.0f;
    params.shadowMapSize       = kSmRes;

    std::vector<float> depthImg(static_cast<size_t>(kW) * kH, 0.5f);
    for (uint32_t y = 0u; y < 40u; ++y) {
        for (uint32_t x = 0u; x < kW; ++x) {
            depthImg[static_cast<size_t>(y) * kW + x] = 1.0f;
        }
    }

    std::vector<float> smImg(static_cast<size_t>(kSmRes) * kSmRes, 0.50f);
    auto rasterBox = [&](float minX, float maxX, float minY, float maxY, float worldHeightZ) {
        const float pt[4] = {0.5f * (minX + maxX), 0.5f * (minY + maxY), worldHeightZ, 1.0f};
        float lc[4]{};
        mulRow4(pt, cascade.lightViewProj, lc);
        const float casterZ = lc[2];
        float uMin = 1.0f, uMax = 0.0f, vMin = 1.0f, vMax = 0.0f;
        const float xs[2] = {minX, maxX};
        const float ys[2] = {minY, maxY};
        for (int ix = 0; ix < 2; ++ix) {
            for (int iy = 0; iy < 2; ++iy) {
                const float cPt[4] = {xs[ix], ys[iy], worldHeightZ, 1.0f};
                float cLc[4]{};
                mulRow4(cPt, cascade.lightViewProj, cLc);
                const float u = cLc[0] * 0.5f + 0.5f;
                const float v = 0.5f - cLc[1] * 0.5f;
                uMin = std::min(uMin, u); uMax = std::max(uMax, u);
                vMin = std::min(vMin, v); vMax = std::max(vMax, v);
            }
        }
        const uint32_t x0 = static_cast<uint32_t>(std::clamp(std::floor(uMin * kSmRes), 0.0f, static_cast<float>(kSmRes - 1u)));
        const uint32_t x1 = static_cast<uint32_t>(std::clamp(std::ceil(uMax * kSmRes), 0.0f, static_cast<float>(kSmRes - 1u)));
        const uint32_t y0 = static_cast<uint32_t>(std::clamp(std::floor(vMin * kSmRes), 0.0f, static_cast<float>(kSmRes - 1u)));
        const uint32_t y1 = static_cast<uint32_t>(std::clamp(std::ceil(vMax * kSmRes), 0.0f, static_cast<float>(kSmRes - 1u)));
        for (uint32_t y = y0; y <= y1; ++y) {
            for (uint32_t x = x0; x <= x1; ++x) {
                float& dst = smImg[static_cast<size_t>(y) * kSmRes + x];
                dst = std::min(dst, casterZ);
            }
        }
    };
    rasterBox(-16.0f, -6.0f, -5.0f, 5.0f, 0.8f);
    rasterBox(  6.0f, 16.0f, -5.0f, 5.0f, 22.0f);

    ComPtr<ID3D12Resource> depthTex, smTex, outTex, rbBuf;
    if (!rig.UploadR32Tex2D(depthTex, kW, kH, depthImg) ||
        !rig.UploadR32Tex2D(smTex, kSmRes, kSmRes, smImg)) {
        res.note = "Upload PCSS depth/shadow textures failed";
        return res;
    }

    D3D12_HEAP_PROPERTIES hpDef{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC outRd{};
    outRd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    outRd.Width = kW; outRd.Height = kH; outRd.DepthOrArraySize = 1u; outRd.MipLevels = 1u;
    outRd.Format = DXGI_FORMAT_R16G16_FLOAT; outRd.SampleDesc.Count = 1u;
    outRd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(rig.dev->CreateCommittedResource(&hpDef, D3D12_HEAP_FLAG_NONE, &outRd,
                                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&outTex)))) {
        res.note = "Create RG16F PCSS output texture failed";
        return res;
    }

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT outFp{};
    UINT outRows = 0u; UINT64 outRowB = 0ull, outTotB = 0ull;
    rig.dev->GetCopyableFootprints(&outRd, 0u, 1u, 0ull, &outFp, &outRows, &outRowB, &outTotB);
    D3D12_HEAP_PROPERTIES hpRb{D3D12_HEAP_TYPE_READBACK};
    D3D12_RESOURCE_DESC rbRd{};
    rbRd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rbRd.Width = outTotB; rbRd.Height = 1u;
    rbRd.DepthOrArraySize = 1u; rbRd.MipLevels = 1u; rbRd.SampleDesc.Count = 1u;
    rbRd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(rig.dev->CreateCommittedResource(&hpRb, D3D12_HEAP_FLAG_NONE, &rbRd,
                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&rbBuf)))) {
        res.note = "Create PCSS readback buffer failed";
        return res;
    }

    std::vector<double> gpuSamples, cpuSamples;
    gpuSamples.reserve(measuredFrames);
    cpuSamples.reserve(measuredFrames);
    const uint32_t totalFrames = warmupFrames + measuredFrames;
    for (uint32_t f = 0u; f < totalFrames; ++f) {
        params.frameJitter = static_cast<float>(f & 63u);
        const auto c0 = Clock::now();
        if (!rig.BeginFrame()) {
            res.note = "BeginFrame failed";
            return res;
        }
        rig.MarkTimestamp(0u);
        host.Execute(rig.cmd.Get(), depthTex.Get(), smTex.Get(), outTex.Get(), kW, kH, params);
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
            gpuSamples.push_back(gMs);
            cpuSamples.push_back(ElapsedMs(c0, c1));
        }
    }

    res.pcssGpuMs   = ComputePercentiles(gpuSamples);
    res.cpuRecordMs = ComputePercentiles(cpuSamples);
    res.hostPcssMs  = host.HasValidTiming() ? host.GetLastPcssMs() : 0.0f;

    // Readback RG16F ShadowOut
    if (!rig.BeginFrame()) {
        res.note = "BeginFrame (readback) failed";
        return res;
    }
    D3D12_RESOURCE_BARRIER toSrc{};
    toSrc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toSrc.Transition = {outTex.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE};
    rig.cmd->ResourceBarrier(1u, &toSrc);
    D3D12_TEXTURE_COPY_LOCATION dLoc{rbBuf.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}};
    dLoc.PlacedFootprint = outFp;
    D3D12_TEXTURE_COPY_LOCATION sLoc{outTex.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
    sLoc.SubresourceIndex = 0u;
    rig.cmd->CopyTextureRegion(&dLoc, 0u, 0u, 0u, &sLoc, nullptr);
    D3D12_RESOURCE_BARRIER toUav = toSrc;
    std::swap(toUav.Transition.StateBefore, toUav.Transition.StateAfter);
    rig.cmd->ResourceBarrier(1u, &toUav);
    if (!rig.FlushAndWait()) {
        res.note = "FlushAndWait (readback) failed";
        return res;
    }

    std::vector<float> visImg(static_cast<size_t>(kW) * kH, 0.0f);
    std::vector<float> penImg(static_cast<size_t>(kW) * kH, 0.0f);
    {
        void* p = nullptr;
        D3D12_RANGE rr{0u, static_cast<SIZE_T>(outTotB)};
        if (FAILED(rbBuf->Map(0u, &rr, &p)) || !p) {
            res.note = "Map PCSS readback failed";
            return res;
        }
        const auto* base = static_cast<const uint8_t*>(p) + outFp.Offset;
        for (uint32_t y = 0u; y < kH; ++y) {
            const auto* row = reinterpret_cast<const uint16_t*>(base + static_cast<size_t>(y) * outFp.Footprint.RowPitch);
            for (uint32_t x = 0u; x < kW; ++x) {
                const uint16_t hv = row[x * 2u + 0u];
                const uint16_t hp = row[x * 2u + 1u];
                if ((hv & 0x7C00u) == 0x7C00u || (hp & 0x7C00u) == 0x7C00u) ++res.nanInfCount;
                visImg[static_cast<size_t>(y) * kW + x] = HalfToFloat(hv);
                penImg[static_cast<size_t>(y) * kW + x] = HalfToFloat(hp);
            }
        }
        D3D12_RANGE wr{0u, 0u};
        rbBuf->Unmap(0u, &wr);
    }

    auto sampleW = [&](float wx, float wy, float& vOut, float& pOut) {
        const float u = (wx / 24.0f) * 0.5f + 0.5f;
        const float v = 0.5f - (wy / 13.5f) * 0.5f;
        const uint32_t px = static_cast<uint32_t>(std::clamp(std::floor(u * kW), 0.0f, static_cast<float>(kW - 1u)));
        const uint32_t py = static_cast<uint32_t>(std::clamp(std::floor(v * kH), 0.0f, static_cast<float>(kH - 1u)));
        vOut = visImg[static_cast<size_t>(py) * kW + px];
        pOut = penImg[static_cast<size_t>(py) * kW + px];
    };

    res.skyMinVis = 1.0f;
    for (uint32_t x = 0u; x < kW; ++x) res.skyMinVis = std::min(res.skyMinVis, visImg[static_cast<size_t>(10u) * kW + x]);
    float openPen = 0.0f, nearUmbraPen = 0.0f, farUmbraPen = 0.0f;
    sampleW(  0.0f, 0.0f, res.openVis,      openPen);
    sampleW(-11.0f, 0.0f, res.nearUmbraVis, nearUmbraPen);
    sampleW( 11.0f, 0.0f, res.farUmbraVis,  farUmbraPen);

    float nearSum = 0.0f, farSum = 0.0f;
    uint32_t nearCnt = 0u, farCnt = 0u;
    for (uint32_t y = 220u; y < 320u; ++y) {
        for (uint32_t x = 0u; x < kW; ++x) {
            const float wx = ((static_cast<float>(x) + 0.5f) / kW * 2.0f - 1.0f) * 24.0f;
            const float v  = visImg[static_cast<size_t>(y) * kW + x];
            const float pn = penImg[static_cast<size_t>(y) * kW + x];
            if (std::fabs(wx - (-6.0f)) < 0.6f && pn > 1.0e-4f) { nearSum += pn; ++nearCnt; }
            if (std::fabs(wx - 6.0f) < 1.2f && pn > 1.0e-4f) {
                farSum += pn; ++farCnt;
                if (v > 0.05f && v < 0.95f) ++res.softBandPx;
            }
        }
    }
    res.nearEdgePen   = (nearCnt > 0u) ? (nearSum / static_cast<float>(nearCnt)) : nearUmbraPen;
    res.farEdgePen    = (farCnt > 0u)  ? (farSum / static_cast<float>(farCnt))   : farUmbraPen;
    res.penumbraRatio = (res.nearEdgePen > 1.0e-5f) ? (res.farEdgePen / res.nearEdgePen) : 0.0f;

    const bool optOk    = (res.nanInfCount == 0u) && (res.openVis >= 0.999f) &&
                          (res.nearUmbraVis <= 0.001f) && (res.farUmbraVis <= 0.001f) &&
                          (res.skyMinVis >= 0.999f) && (res.penumbraRatio > 2.0f) && (res.softBandPx > 20u);
    const bool budgetOk = rig.isWarp ? true : (res.pcssGpuMs.mean < 0.15);
    res.passed = optOk && budgetOk;
    if (!optOk) {
        res.note = "PCSS contact-hardening/umbra/sky/NaN invariant failed";
    } else if (!budgetOk) {
        res.note = "HW GPU 960x540 PCSS mean exceeded 0.15 ms budget";
    } else {
        res.note = rig.isWarp ? "WARP 960x540 PCSS 32x contact hardening PASS"
                              : "HW GPU < 0.15 ms & 32x contact hardening PASS";
    }
    return res;
}

// ----------------------------------------------------------------------------
// 13. GPU Jimenez GTAO + Unoccluded Bent Normals & Bilateral Denoise (RawGtaoHost)
// ----------------------------------------------------------------------------
GtaoBenchResult RunGtaoBenchmark(BenchDx12Rig& rig, uint32_t warmupFrames, uint32_t measuredFrames)
{
    GtaoBenchResult res{};
    constexpr uint32_t kW = 960u;
    constexpr uint32_t kH = 540u;
    res.width  = kW;
    res.height = kH;

    raw::bridge::RawGtaoHost host;
    raw::bridge::GtaoDesc desc{};
    desc.initialWidth  = kW;
    desc.initialHeight = kH;
    std::string err;
    if (!host.Init(rig.dev.Get(), desc, &err)) {
        res.note = "RawGtaoHost::Init failed: " + err;
        return res;
    }
    host.SetTimestampFrequency(rig.tsFreqHz);

    raw::bridge::GtaoParams gp{};
    gp.fovYDeg           = 60.0f;
    gp.nearZ             = 0.5f;
    gp.farZ              = 500.0f;
    gp.effectRadius      = 1.6f;
    gp.falloffStart      = 0.65f;
    gp.aoPower           = 1.0f;
    gp.maxScreenRadiusPx = 128.0f;
    gp.depthEdgeScale    = 0.025f;
    gp.numSlices         = 2u;
    gp.numSteps          = 6u;
    gp.temporalJitter    = 0.0f;

    // Build ray-traced exact 3D view-space synthetic depth buffer (Standard-Z 0=near, 1=far):
    // open floor Y=-2.0, 90-deg +X wall X>=1.0, recessed pit at (-1.2, 4.7) r=0.90 bottom Y=-2.55 with 3-sided stone rim, sky top 64 rows.
    std::vector<float> depthImg(static_cast<size_t>(kW) * kH, 1.0f);
    const float aspect = static_cast<float>(kW) / static_cast<float>(kH);
    const float tanY   = std::tan(0.5f * gp.fovYDeg * 3.14159265359f / 180.0f);
    const float tanX   = tanY * aspect;
    const float q      = gp.farZ / (gp.farZ - gp.nearZ);
    auto linearToStdZ = [&](float vz) -> float {
        return std::clamp(q * (1.0f - gp.nearZ / vz), 0.0f, 0.99997f);
    };
    auto slabHit = [](float dir, float bmin, float bmax, float& tMin, float& tMax) -> bool {
        if (std::fabs(dir) < 1.0e-7f) return (0.0f >= bmin && 0.0f <= bmax);
        float t0 = bmin / dir;
        float t1 = bmax / dir;
        if (t0 > t1) std::swap(t0, t1);
        tMin = std::max(tMin, t0);
        tMax = std::min(tMax, t1);
        return tMin <= tMax;
    };

    constexpr float kFloorY  = -2.0f;
    constexpr float kPitX    = -1.2f;
    constexpr float kPitZ    =  4.7f;
    constexpr float kPitR    =  0.90f;
    constexpr float kPitBotY = -2.55f;

    for (uint32_t y = 0u; y < kH; ++y) {
        for (uint32_t x = 0u; x < kW; ++x) {
            const float uvX  = (static_cast<float>(x) + 0.5f) / static_cast<float>(kW);
            const float uvY  = (static_cast<float>(y) + 0.5f) / static_cast<float>(kH);
            const float rx   = (uvX * 2.0f - 1.0f) * tanX;
            const float ry   = (1.0f - uvY * 2.0f) * tanY;
            const float rz   = 1.0f;
            float bestT = 1.0e9f;

            if (ry < -1.0e-5f) {
                const float tFloor = kFloorY / ry;
                if (tFloor > gp.nearZ && tFloor < 45.0f) {
                    const float fx = rx * tFloor;
                    const float fz = rz * tFloor;
                    const float dx = fx - kPitX;
                    const float dz = fz - kPitZ;
                    if ((dx * dx + dz * dz) >= (kPitR * kPitR)) {
                        bestT = std::min(bestT, tFloor);
                    } else {
                        const float tBot = kPitBotY / ry;
                        if (tBot > tFloor) {
                            const float bx = rx * tBot - kPitX;
                            const float bz = rz * tBot - kPitZ;
                            if (bx * bx + bz * bz <= kPitR * kPitR) bestT = std::min(bestT, tBot);
                        }
                        const float a = rx * rx + 1.0f;
                        const float b = -2.0f * (rx * kPitX + kPitZ);
                        const float c = kPitX * kPitX + kPitZ * kPitZ - kPitR * kPitR;
                        const float disc = b * b - 4.0f * a * c;
                        if (disc >= 0.0f) {
                            const float tCyl = (-b + std::sqrt(disc)) / (2.0f * a);
                            if (tCyl > tFloor) {
                                const float cy = ry * tCyl;
                                if (cy <= kFloorY && cy >= kPitBotY) bestT = std::min(bestT, tCyl);
                            }
                        }
                    }
                }
            }

            {
                float t0 = gp.nearZ, t1 = 1.0e6f;
                if (slabHit(rx, 1.0f, 5.0f, t0, t1) && slabHit(ry, kFloorY, 1.2f, t0, t1) &&
                    slabHit(rz, 3.0f, 14.0f, t0, t1) && t0 > gp.nearZ) {
                    bestT = std::min(bestT, t0);
                }
            }
            {
                float t0 = gp.nearZ, t1 = 1.0e6f;
                if (slabHit(rx, -2.2f, -0.2f, t0, t1) && slabHit(ry, kPitBotY, -1.35f, t0, t1) &&
                    slabHit(rz, 5.55f, 5.95f, t0, t1) && t0 > gp.nearZ) {
                    bestT = std::min(bestT, t0);
                }
                t0 = gp.nearZ; t1 = 1.0e6f;
                if (slabHit(rx, -2.2f, -1.75f, t0, t1) && slabHit(ry, kPitBotY, -1.35f, t0, t1) &&
                    slabHit(rz, 4.2f, 5.95f, t0, t1) && t0 > gp.nearZ) {
                    bestT = std::min(bestT, t0);
                }
                t0 = gp.nearZ; t1 = 1.0e6f;
                if (slabHit(rx, -0.65f, -0.2f, t0, t1) && slabHit(ry, kPitBotY, -1.35f, t0, t1) &&
                    slabHit(rz, 4.2f, 5.95f, t0, t1) && t0 > gp.nearZ) {
                    bestT = std::min(bestT, t0);
                }
            }

            if (y < 64u) bestT = 1.0e9f;
            depthImg[static_cast<size_t>(y) * kW + x] = (bestT < 1.0e8f) ? linearToStdZ(bestT) : 1.0f;
        }
    }

    ComPtr<ID3D12Resource> depthTex;
    if (!rig.UploadR32Tex2D(depthTex, kW, kH, depthImg)) {
        res.note = "Upload 960x540 GTAO synthetic depth failed";
        return res;
    }

    std::vector<double> gpuSamples, cpuSamples;
    gpuSamples.reserve(measuredFrames);
    cpuSamples.reserve(measuredFrames);
    const uint32_t totalFrames = warmupFrames + measuredFrames;
    for (uint32_t f = 0u; f < totalFrames; ++f) {
        gp.temporalJitter = 0.0f;
        const auto c0 = Clock::now();
        if (!rig.BeginFrame()) {
            res.note = "BeginFrame failed";
            return res;
        }
        rig.MarkTimestamp(0u);
        host.Execute(rig.cmd.Get(), depthTex.Get(), kW, kH, gp);
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
            gpuSamples.push_back(gMs);
            cpuSamples.push_back(ElapsedMs(c0, c1));
        }
    }

    res.totalGpuMs    = ComputePercentiles(gpuSamples);
    res.cpuRecordMs   = ComputePercentiles(cpuSamples);
    res.hostMainMs    = host.HasValidTiming() ? host.GetLastMainMs() : 0.0f;
    res.hostDenoiseMs = host.HasValidTiming() ? host.GetLastDenoiseMs() : 0.0f;

    // Readback RGBA16F GtaoFilteredOut
    ID3D12Resource* outTex = host.GetFilteredOutputResource();
    if (!outTex) {
        res.note = "RawGtaoHost filtered output resource null";
        return res;
    }
    const D3D12_RESOURCE_DESC outRd = outTex->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT outFp{};
    UINT outRows = 0u;
    UINT64 outRowB = 0ull, outTotB = 0ull;
    rig.dev->GetCopyableFootprints(&outRd, 0u, 1u, 0ull, &outFp, &outRows, &outRowB, &outTotB);
    D3D12_HEAP_PROPERTIES hpRb{D3D12_HEAP_TYPE_READBACK};
    D3D12_RESOURCE_DESC rbRd{};
    rbRd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rbRd.Width            = outTotB;
    rbRd.Height           = 1u;
    rbRd.DepthOrArraySize = 1u;
    rbRd.MipLevels        = 1u;
    rbRd.SampleDesc.Count = 1u;
    rbRd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> rbBuf;
    if (FAILED(rig.dev->CreateCommittedResource(&hpRb, D3D12_HEAP_FLAG_NONE, &rbRd,
                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&rbBuf)))) {
        res.note = "Create GTAO readback buffer failed";
        return res;
    }
    if (!rig.BeginFrame()) {
        res.note = "BeginFrame (readback) failed";
        return res;
    }
    D3D12_RESOURCE_BARRIER toSrc{};
    toSrc.Type       = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toSrc.Transition = {outTex, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                        host.FilteredOutputResourceState(), D3D12_RESOURCE_STATE_COPY_SOURCE};
    rig.cmd->ResourceBarrier(1u, &toSrc);
    D3D12_TEXTURE_COPY_LOCATION dLoc{rbBuf.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}};
    dLoc.PlacedFootprint = outFp;
    D3D12_TEXTURE_COPY_LOCATION sLoc{outTex, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
    sLoc.SubresourceIndex = 0u;
    rig.cmd->CopyTextureRegion(&dLoc, 0u, 0u, 0u, &sLoc, nullptr);
    D3D12_RESOURCE_BARRIER toBack = toSrc;
    std::swap(toBack.Transition.StateBefore, toBack.Transition.StateAfter);
    rig.cmd->ResourceBarrier(1u, &toBack);
    if (!rig.FlushAndWait()) {
        res.note = "FlushAndWait (readback) failed";
        return res;
    }

    std::vector<float> outRgba(static_cast<size_t>(kW) * kH * 4u, 0.0f);
    {
        void* p = nullptr;
        D3D12_RANGE rr{0u, static_cast<SIZE_T>(outTotB)};
        if (FAILED(rbBuf->Map(0u, &rr, &p)) || !p) {
            res.note = "Map GTAO readback failed";
            return res;
        }
        const auto* base = static_cast<const uint8_t*>(p) + outFp.Offset;
        for (uint32_t y = 0u; y < kH; ++y) {
            const auto* row = reinterpret_cast<const uint16_t*>(base + static_cast<size_t>(y) * outFp.Footprint.RowPitch);
            for (uint32_t x = 0u; x < kW; ++x) {
                const size_t i = (static_cast<size_t>(y) * kW + x) * 4u;
                const float bx = HalfToFloat(row[x * 4u + 0u]);
                const float by = HalfToFloat(row[x * 4u + 1u]);
                const float bz = HalfToFloat(row[x * 4u + 2u]);
                const float vis = HalfToFloat(row[x * 4u + 3u]);
                outRgba[i + 0u] = bx;
                outRgba[i + 1u] = by;
                outRgba[i + 2u] = bz;
                outRgba[i + 3u] = vis;
                if (!std::isfinite(bx) || !std::isfinite(by) || !std::isfinite(bz) || !std::isfinite(vis)) {
                    ++res.nanCount;
                    continue;
                }
                if (depthImg[static_cast<size_t>(y) * kW + x] >= 0.99998f) {
                    if (std::fabs(vis - 1.0f) > 1.0e-3f || std::fabs(bz + 1.0f) > 2.0e-3f) {
                        ++res.skyBadCount;
                    }
                }
            }
        }
        D3D12_RANGE wr{0u, 0u};
        rbBuf->Unmap(0u, &wr);
    }

    auto sampleAvg5x5 = [&](float vx, float vy, float vz, float out4[4]) {
        const float ndcX = vx / (vz * tanX);
        const float ndcY = vy / (vz * tanY);
        const float u    = std::clamp(ndcX * 0.5f + 0.5f, 0.0f, 0.9999f);
        const float v    = std::clamp(0.5f - ndcY * 0.5f, 0.0f, 0.9999f);
        const int cx     = static_cast<int>(u * static_cast<float>(kW));
        const int cy     = static_cast<int>(v * static_cast<float>(kH));
        double s0 = 0.0, s1 = 0.0, s2 = 0.0, s3 = 0.0;
        int cnt = 0;
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dxp = -2; dxp <= 2; ++dxp) {
                const int px = std::clamp(cx + dxp, 0, static_cast<int>(kW) - 1);
                const int py = std::clamp(cy + dy, 0, static_cast<int>(kH) - 1);
                const size_t idx = (static_cast<size_t>(py) * kW + static_cast<size_t>(px)) * 4u;
                s0 += outRgba[idx + 0u];
                s1 += outRgba[idx + 1u];
                s2 += outRgba[idx + 2u];
                s3 += outRgba[idx + 3u];
                ++cnt;
            }
        }
        out4[0] = static_cast<float>(s0 / cnt);
        out4[1] = static_cast<float>(s1 / cnt);
        out4[2] = static_cast<float>(s2 / cnt);
        out4[3] = static_cast<float>(s3 / cnt);
    };

    float openProbe[4]{}, creviceProbe[4]{}, pitProbe[4]{};
    sampleAvg5x5(-3.2f, -2.0f, 6.5f, openProbe);
    sampleAvg5x5( 0.82f, -2.0f, 6.0f, creviceProbe);
    sampleAvg5x5(-1.2f, -2.55f, 5.15f, pitProbe);

    res.openFloorVis   = openProbe[3];
    res.openFloorBentY = openProbe[1];
    res.creviceVis     = creviceProbe[3];
    res.creviceBentX   = creviceProbe[0];
    res.pitVis         = pitProbe[3];

    const bool optOk = (res.nanCount == 0u) && (res.skyBadCount == 0u) &&
                       (res.openFloorVis > 0.90f) && (res.openFloorBentY > 0.92f) &&
                       (res.creviceVis < 0.65f) && (res.creviceBentX < -0.15f) &&
                       (res.pitVis < res.openFloorVis - 0.25f);
    const bool budgetOk = rig.isWarp ? true : (res.totalGpuMs.mean < 0.15);
    res.passed = optOk && budgetOk;
    if (!optOk) {
        res.note = "GTAO floor/crevice/bent-normal/pit/sky/NaN invariant failed";
    } else if (!budgetOk) {
        res.note = "HW GPU 960x540 GTAO total mean exceeded 0.15 ms budget";
    } else {
        res.note = rig.isWarp ? "WARP 960x540 Jimenez GTAO + bent normals PASS"
                              : "HW GPU < 0.15 ms & Jimenez GTAO + bent normals PASS";
    }
    return res;
}

// ----------------------------------------------------------------------------
// 14. GPU GGX Stochastic Screen-Space Reflections (RawSsrHost)
// ----------------------------------------------------------------------------
SsrBenchResult RunSsrBenchmark(BenchDx12Rig& rig, uint32_t warmupFrames, uint32_t measuredFrames)
{
    SsrBenchResult res{};
    constexpr uint32_t kW = 960u;
    constexpr uint32_t kH = 540u;
    res.width  = kW;
    res.height = kH;

    raw::bridge::RawSsrHost host;
    raw::bridge::SsrDesc desc{};
    desc.initialWidth  = kW;
    desc.initialHeight = kH;
    std::string err;
    if (!host.Init(rig.dev.Get(), desc, &err)) {
        res.note = "RawSsrHost::Init failed: " + err;
        return res;
    }
    host.SetTimestampFrequency(rig.tsFreqHz);

    // Ray-traced synthetic 960x540 3D scene (LH view space: camera at origin looking +Z):
    //   - Wet flat reflective floor at y = -1.8 m (extending to z = 120 m)
    //   - Bright crimson-gold emissive pillar: x in [-1.8, +1.8], y in [-1.8, +5.2], z in [11.0, 14.0]
    //     Color = (2.10, 0.52, 0.14) -> R > 0.70 and R/B = 15.0x > 3.0x
    //   - Dark sky above horizon (depth = 1.0)
    const float fovY   = 1.04719755f; // 60 deg
    const float aspect = static_cast<float>(kW) / static_cast<float>(kH);
    const float nearZ  = 0.25f;
    const float farZ   = 150.0f;

    raw::bridge::SsrParams sp{};
    raw::bridge::BuildSsrPerspectiveLH(fovY, aspect, nearZ, farZ, sp.proj, sp.invProj);
    sp.maxRayDist        = 45.0f;
    sp.thicknessM        = 2.2f;
    sp.nearZ             = nearZ;
    sp.startBias         = 0.03f;
    sp.fallbackRoughness = 0.01f;
    sp.maxRoughness      = 0.72f;
    sp.edgeFadeWidth     = 0.06f;
    sp.maxSteps          = 40u;

    const float tanHalfY = std::tan(fovY * 0.5f);
    const float tanHalfX = tanHalfY * aspect;
    const float q        = farZ / (farZ - nearZ);

    std::vector<float>   sceneDepth(static_cast<size_t>(kW) * kH, 1.0f);
    std::vector<float>   sceneColor(static_cast<size_t>(kW) * kH * 4u, 0.0f);
    std::vector<uint8_t> isMirrorTarget(static_cast<size_t>(kW) * kH, 0u);
    std::vector<uint8_t> isOpenSkyFloor(static_cast<size_t>(kW) * kH, 0u);

    auto slabHit = [](float d, float bmin, float bmax, float& tMin, float& tMax) -> bool {
        if (std::fabs(d) < 1.0e-7f) return (0.0f >= bmin && 0.0f <= bmax);
        float t0 = bmin / d;
        float t1 = bmax / d;
        if (t0 > t1) std::swap(t0, t1);
        tMin = std::max(tMin, t0);
        tMax = std::min(tMax, t1);
        return tMin <= tMax;
    };

    for (uint32_t y = 0u; y < kH; ++y) {
        const float ndcY = 1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(kH) * 2.0f;
        const float dirY = ndcY * tanHalfY;
        for (uint32_t x = 0u; x < kW; ++x) {
            const float ndcX = (static_cast<float>(x) + 0.5f) / static_cast<float>(kW) * 2.0f - 1.0f;
            const float dirX = ndcX * tanHalfX;
            const float dirZ = 1.0f;

            float bestZ = 1.0e9f;
            float cr = 0.01f, cg = 0.02f, cb = 0.04f;
            bool hitFloor = false;

            if (dirY < -1.0e-5f) {
                const float t = -1.8f / dirY;
                if (t > nearZ && t < 120.0f) {
                    bestZ = t;
                    cr = 0.04f; cg = 0.05f; cb = 0.07f;
                    hitFloor = true;
                }
            }

            float tMin = nearZ, tMax = farZ;
            if (slabHit(dirX, -1.8f, 1.8f, tMin, tMax) &&
                slabHit(dirY, -1.8f, 5.2f, tMin, tMax) &&
                slabHit(dirZ, 11.0f, 14.0f, tMin, tMax) &&
                tMin < bestZ) {
                bestZ = tMin;
                cr = 2.10f; cg = 0.52f; cb = 0.14f;
                hitFloor = false;
            }

            const size_t pxIdx = static_cast<size_t>(y) * kW + x;
            if (bestZ < 1.0e8f) {
                const float ndcZ = (bestZ * q - nearZ * q) / bestZ;
                sceneDepth[pxIdx] = std::clamp(ndcZ, 0.0f, 0.99995f);

                if (hitFloor && bestZ > 4.2f && bestZ < 10.2f) {
                    const float hitX = dirX * bestZ;
                    const float dt   = 11.0f - bestZ;
                    const float rx   = hitX + dirX * dt;
                    const float ry   = -1.8f + (-dirY) * dt;
                    if (std::fabs(rx) < 1.2f && ry > -1.0f && ry < 4.2f) {
                        isMirrorTarget[pxIdx] = 1u;
                    } else if (std::fabs(hitX) > 4.2f && std::fabs(rx) > 5.0f) {
                        isOpenSkyFloor[pxIdx] = 1u;
                    }
                }
            }
            const size_t cOff = pxIdx * 4u;
            sceneColor[cOff + 0u] = cr;
            sceneColor[cOff + 1u] = cg;
            sceneColor[cOff + 2u] = cb;
            sceneColor[cOff + 3u] = 1.0f;
        }
    }

    ComPtr<ID3D12Resource> colorTex, depthTex;
    if (!rig.UploadRgba16Tex2DWH(colorTex, kW, kH, sceneColor) ||
        !rig.UploadR32Tex2D(depthTex, kW, kH, sceneDepth)) {
        res.note = "Upload 960x540 SSR synthetic color/depth failed";
        return res;
    }

    std::vector<double> gpuSamples, cpuSamples;
    gpuSamples.reserve(measuredFrames);
    cpuSamples.reserve(measuredFrames);
    const uint32_t totalFrames = warmupFrames + measuredFrames;
    for (uint32_t f = 0u; f < totalFrames; ++f) {
        sp.frameJitter = static_cast<float>(f & 63u);
        const auto c0 = Clock::now();
        if (!rig.BeginFrame()) {
            res.note = "BeginFrame failed";
            return res;
        }
        rig.MarkTimestamp(0u);
        host.Execute(rig.cmd.Get(), colorTex.Get(), depthTex.Get(), kW, kH, sp);
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
            gpuSamples.push_back(gMs);
            cpuSamples.push_back(ElapsedMs(c0, c1));
        }
    }

    res.ssrGpuMs    = ComputePercentiles(gpuSamples);
    res.cpuRecordMs = ComputePercentiles(cpuSamples);
    res.hostSsrMs   = host.HasValidTiming() ? host.GetLastSsrMs() : 0.0f;

    // Readback mirror SSR pass and validate optical invariants
    uint32_t rbW = 0u, rbH = 0u, rbD = 0u;
    std::vector<float> mirrorRgba;
    if (!rig.ReadbackRgba16Tex(host.GetOutputResource(), host.OutputResourceState(), rbW, rbH, rbD, mirrorRgba) ||
        rbW != kW || rbH != kH) {
        res.note = "Readback SSR mirror output failed";
        return res;
    }

    double sumConf = 0.0, sumR = 0.0, sumB = 0.0;
    uint32_t targetCount = 0u;
    float maxSkyConf = 0.0f;
    float maxOpenFloorConf = 0.0f;
    uint32_t mirrorSpanMin = kW, mirrorSpanMax = 0u;

    for (uint32_t y = 0u; y < kH; ++y) {
        for (uint32_t x = 0u; x < kW; ++x) {
            const size_t idx = static_cast<size_t>(y) * kW + x;
            const float r = mirrorRgba[idx * 4u + 0u];
            const float g = mirrorRgba[idx * 4u + 1u];
            const float b = mirrorRgba[idx * 4u + 2u];
            const float a = mirrorRgba[idx * 4u + 3u];
            if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b) || !std::isfinite(a)) {
                ++res.nanInfCount;
                continue;
            }
            if (sceneDepth[idx] >= 0.99998f) {
                maxSkyConf = std::max(maxSkyConf, a);
            }
            if (isMirrorTarget[idx]) {
                sumConf += a;
                sumR += r;
                sumB += b;
                ++targetCount;
            }
            if (isOpenSkyFloor[idx]) {
                maxOpenFloorConf = std::max(maxOpenFloorConf, a);
            }
            if (y == 380u && a > 0.25f && r > 0.6f) {
                mirrorSpanMin = std::min(mirrorSpanMin, x);
                mirrorSpanMax = std::max(mirrorSpanMax, x);
            }
        }
    }

    res.targetPx         = targetCount;
    res.meanConf         = (targetCount > 0u) ? static_cast<float>(sumConf / targetCount) : 0.0f;
    res.meanR            = (targetCount > 0u) ? static_cast<float>(sumR / targetCount) : 0.0f;
    res.meanB            = (targetCount > 0u) ? static_cast<float>(sumB / targetCount) : 0.0f;
    res.rbRatio          = res.meanR / std::max(res.meanB, 1.0e-4f);
    res.maxSkyConf       = maxSkyConf;
    res.maxOpenFloorConf = maxOpenFloorConf;
    res.mirrorSpanW      = (mirrorSpanMax >= mirrorSpanMin) ? (mirrorSpanMax - mirrorSpanMin) : 0u;

    // GGX stochastic roughness lobe spread validation (outside steady-state timing loop)
    raw::bridge::SsrParams roughSp = sp;
    roughSp.fallbackRoughness = 0.28f;
    roughSp.frameJitter       = 1.0f;
    if (!rig.BeginFrame()) {
        res.note = "BeginFrame (roughness probe) failed";
        return res;
    }
    host.Execute(rig.cmd.Get(), colorTex.Get(), depthTex.Get(), kW, kH, roughSp);
    if (!rig.FlushAndWait()) {
        res.note = "FlushAndWait (roughness probe) failed";
        return res;
    }
    std::vector<float> roughRgba;
    if (!rig.ReadbackRgba16Tex(host.GetOutputResource(), host.OutputResourceState(), rbW, rbH, rbD, roughRgba) ||
        rbW != kW || rbH != kH) {
        res.note = "Readback SSR roughness probe failed";
        return res;
    }
    uint32_t roughSpanMin = kW, roughSpanMax = 0u;
    for (uint32_t y = 0u; y < kH; ++y) {
        for (uint32_t x = 0u; x < kW; ++x) {
            const size_t idx = static_cast<size_t>(y) * kW + x;
            const float r = roughRgba[idx * 4u + 0u];
            const float g = roughRgba[idx * 4u + 1u];
            const float b = roughRgba[idx * 4u + 2u];
            const float a = roughRgba[idx * 4u + 3u];
            if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b) || !std::isfinite(a)) {
                ++res.nanInfCount;
                continue;
            }
            if (y == 380u && a > 0.15f && r > 0.45f) {
                roughSpanMin = std::min(roughSpanMin, x);
                roughSpanMax = std::max(roughSpanMax, x);
            }
        }
    }
    res.roughSpanW = (roughSpanMax >= roughSpanMin) ? (roughSpanMax - roughSpanMin) : 0u;

    const bool optOk = (res.nanInfCount == 0u) && (res.targetPx > 500u) &&
                       (res.meanConf > 0.80f) && (res.meanR > 0.70f) && (res.rbRatio > 3.0f) &&
                       (res.maxSkyConf == 0.0f) && (res.maxOpenFloorConf == 0.0f) &&
                       (res.roughSpanW > res.mirrorSpanW);
    const bool budgetOk = rig.isWarp ? true : (res.ssrGpuMs.mean < 0.15);
    res.passed = optOk && budgetOk;
    if (!optOk) {
        res.note = "SSR reflection/sky/roughness/NaN invariant failed";
    } else if (!budgetOk) {
        res.note = "HW GPU 960x540 SSR mean exceeded 0.15 ms budget";
    } else {
        res.note = rig.isWarp ? "WARP 960x540 GGX stochastic SSR PASS"
                              : "HW GPU < 0.15 ms & GGX stochastic SSR PASS";
    }
    return res;
}

// ----------------------------------------------------------------------------
// 15. GPU 256-Bin Log-Luminance Histogram & Eye-Adaptation Auto-Exposure (RawAutoExposureHost)
// ----------------------------------------------------------------------------
AutoExposureBenchResult RunAutoExposureBenchmark(BenchDx12Rig& rig, uint32_t warmupFrames, uint32_t measuredFrames)
{
    AutoExposureBenchResult res{};
    constexpr uint32_t kW = 960u;
    constexpr uint32_t kH = 540u;
    res.width  = kW;
    res.height = kH;

    raw::bridge::RawAutoExposureHost host;
    std::string err;
    if (!host.Init(rig.dev.Get(), {}, &err)) {
        res.note = "RawAutoExposureHost::Init failed: " + err;
        return res;
    }
    host.SetTimestampFrequency(rig.tsFreqHz);

    auto uploadRgba32FTex = [&](ComPtr<ID3D12Resource>& tex, const std::vector<float>& rgba) -> bool {
        D3D12_HEAP_PROPERTIES hpDef{D3D12_HEAP_TYPE_DEFAULT};
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width            = kW;
        rd.Height           = kH;
        rd.DepthOrArraySize = 1u;
        rd.MipLevels        = 1u;
        rd.Format           = DXGI_FORMAT_R32G32B32A32_FLOAT;
        rd.SampleDesc.Count = 1u;
        if (FAILED(rig.dev->CreateCommittedResource(&hpDef, D3D12_HEAP_FLAG_NONE, &rd,
                                                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex)))) {
            return false;
        }
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        UINT numRows = 0u;
        UINT64 rowBytes = 0ull, totalBytes = 0ull;
        rig.dev->GetCopyableFootprints(&rd, 0u, 1u, 0ull, &fp, &numRows, &rowBytes, &totalBytes);

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
        if (FAILED(rig.dev->CreateCommittedResource(&hpUp, D3D12_HEAP_FLAG_NONE, &ubd,
                                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&up)))) {
            return false;
        }
        void* mapped = nullptr;
        const D3D12_RANGE rr{0u, 0u};
        if (FAILED(up->Map(0u, &rr, &mapped)) || !mapped) return false;
        auto* base = static_cast<uint8_t*>(mapped) + fp.Offset;
        for (uint32_t y = 0u; y < kH; ++y) {
            std::memcpy(base + static_cast<size_t>(y) * fp.Footprint.RowPitch,
                        rgba.data() + static_cast<size_t>(y) * kW * 4u,
                        static_cast<size_t>(kW) * 4u * sizeof(float));
        }
        up->Unmap(0u, nullptr);

        if (!rig.BeginFrame()) return false;
        D3D12_TEXTURE_COPY_LOCATION dst{tex.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
        dst.SubresourceIndex = 0u;
        D3D12_TEXTURE_COPY_LOCATION src{up.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}};
        src.PlacedFootprint = fp;
        rig.cmd->CopyTextureRegion(&dst, 0u, 0u, 0u, &src, nullptr);

        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = tex.Get();
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        rig.cmd->ResourceBarrier(1u, &b);
        return rig.FlushAndWait();
    };

    auto makeUniformHdr = [&](float lum) -> std::vector<float> {
        std::vector<float> img(static_cast<size_t>(kW) * kH * 4u);
        for (size_t i = 0u; i < static_cast<size_t>(kW) * kH; ++i) {
            img[i * 4u + 0u] = lum;
            img[i * 4u + 1u] = lum;
            img[i * 4u + 2u] = lum;
            img[i * 4u + 3u] = 1.0f;
        }
        return img;
    };

    const size_t totalPx = static_cast<size_t>(kW) * kH;
    auto darkImg    = makeUniformHdr(0.02f);
    auto glacierImg = makeUniformHdr(8.0f);
    auto fireflyImg = makeUniformHdr(0.50f);
    auto nanImg     = makeUniformHdr(0.50f);
    for (size_t i = 0u; i < totalPx; i += 50u) {
        fireflyImg[i * 4u + 0u] = 10000.0f;
        fireflyImg[i * 4u + 1u] = 10000.0f;
        fireflyImg[i * 4u + 2u] = 10000.0f;
    }
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

    ComPtr<ID3D12Resource> darkTex, glacierTex, fireflyTex, nanTex;
    if (!uploadRgba32FTex(darkTex, darkImg) ||
        !uploadRgba32FTex(glacierTex, glacierImg) ||
        !uploadRgba32FTex(fireflyTex, fireflyImg) ||
        !uploadRgba32FTex(nanTex, nanImg)) {
        res.note = "Upload 960x540 Auto-Exposure HDR test textures failed";
        return res;
    }

    auto readState = [&](raw::bridge::AutoExposureGpuState& st) -> bool {
        std::vector<uint8_t> rawBytes;
        if (!rig.ReadbackBuffer(host.GetExposureBufferResource(), host.ExposureBufferResourceState(),
                                sizeof(raw::bridge::AutoExposureGpuState), rawBytes) ||
            rawBytes.size() < sizeof(raw::bridge::AutoExposureGpuState)) {
            return false;
        }
        std::memcpy(&st, rawBytes.data(), sizeof(st));
        return true;
    };

    auto runStep = [&](ID3D12Resource* tex, const raw::bridge::AutoExposureParams& p,
                       raw::bridge::AutoExposureGpuState& st) -> bool {
        if (!rig.BeginFrame()) return false;
        if (!host.Execute(rig.cmd.Get(), tex, kW, kH, p)) return false;
        if (!rig.FlushAndWait()) return false;
        return readState(st);
    };

    // 1. Dark dungeon (lum=0.02)
    raw::bridge::AutoExposureParams pReset{};
    pReset.resetAdapt = true;
    raw::bridge::AutoExposureGpuState stDark{}, stGlacier{}, stFirefly{}, stNan{};
    if (!runStep(darkTex.Get(), pReset, stDark) ||
        !runStep(glacierTex.Get(), pReset, stGlacier) ||
        !runStep(fireflyTex.Get(), pReset, stFirefly) ||
        !runStep(nanTex.Get(), pReset, stNan)) {
        res.note = "Auto-Exposure scene invariant probe execution failed";
        return res;
    }

    res.darkExposure     = stDark.adaptedExposure;
    res.glacierExposure  = stGlacier.adaptedExposure;
    res.fireflyTargetLum = stFirefly.targetLum;
    res.fireflyErrPct    = std::fabs(stFirefly.targetLum - 0.50f) / 0.50f * 100.0f;
    res.nanFinite        = std::isfinite(stNan.adaptedLum) && std::isfinite(stNan.adaptedExposure) &&
                           std::isfinite(stNan.targetLum) && std::isfinite(stNan.targetEv100) &&
                           (std::fabs(stNan.targetLum - 0.50f) / 0.50f < 0.10f);

    // 2. Asymmetric eye-adaptation proof (0.25s dark->bright vs bright->dark)
    raw::bridge::AutoExposureParams pStep{};
    pStep.dtSec        = 0.25f;
    pStep.tauBrightSec = 0.35f;
    pStep.tauDarkSec   = 1.40f;
    pStep.resetAdapt   = false;

    raw::bridge::AutoExposureGpuState stBaseDark{}, stAfterBright{}, stBaseBright{}, stAfterDark{};
    if (!rig.BeginFrame()) { res.note = "BeginFrame reset1 failed"; return res; }
    host.ResetAdaptation(rig.cmd.Get());
    if (!rig.FlushAndWait()) { res.note = "Flush reset1 failed"; return res; }
    if (!runStep(darkTex.Get(), pStep, stBaseDark) || !runStep(glacierTex.Get(), pStep, stAfterBright)) {
        res.note = "Auto-Exposure dark->bright step failed";
        return res;
    }

    if (!rig.BeginFrame()) { res.note = "BeginFrame reset2 failed"; return res; }
    host.ResetAdaptation(rig.cmd.Get());
    if (!rig.FlushAndWait()) { res.note = "Flush reset2 failed"; return res; }
    if (!runStep(glacierTex.Get(), pStep, stBaseBright) || !runStep(darkTex.Get(), pStep, stAfterDark)) {
        res.note = "Auto-Exposure bright->dark step failed";
        return res;
    }

    const float logDark   = std::log2(std::max(stBaseDark.adaptedLum, 1.0e-6f));
    const float logBright = std::log2(std::max(stBaseBright.adaptedLum, 1.0e-6f));
    const float logSpan   = std::max(std::fabs(logBright - logDark), 1.0e-5f);
    res.asymBrightFrac    = (std::log2(std::max(stAfterBright.adaptedLum, 1.0e-6f)) - logDark) / logSpan;
    res.asymDarkFrac      = (logBright - std::log2(std::max(stAfterDark.adaptedLum, 1.0e-6f))) / logSpan;
    res.asymRatio         = (res.asymDarkFrac > 1.0e-5f) ? (res.asymBrightFrac / res.asymDarkFrac) : 0.0f;

    // 3. Steady-state GPU timing benchmark
    raw::bridge::AutoExposureParams pBench{};
    pBench.dtSec      = 1.0f / 60.0f;
    pBench.resetAdapt = false;
    std::vector<double> gpuSamples, cpuSamples;
    gpuSamples.reserve(measuredFrames);
    cpuSamples.reserve(measuredFrames);
    const uint32_t totalFrames = warmupFrames + measuredFrames;
    for (uint32_t f = 0u; f < totalFrames; ++f) {
        const auto c0 = Clock::now();
        if (!rig.BeginFrame()) {
            res.note = "BeginFrame (timing) failed";
            return res;
        }
        rig.MarkTimestamp(0u);
        host.Execute(rig.cmd.Get(), fireflyTex.Get(), kW, kH, pBench);
        rig.MarkTimestamp(1u);
        rig.ResolveTimestamps(0u, 2u);
        const auto c1 = Clock::now();
        if (!rig.FlushAndWait()) {
            res.note = "FlushAndWait (timing) failed";
            return res;
        }
        if (f >= warmupFrames) {
            double gMs = 0.0;
            rig.ReadTimestampRangeMs(0u, 1u, gMs);
            gpuSamples.push_back(gMs);
            cpuSamples.push_back(ElapsedMs(c0, c1));
        }
    }

    res.totalGpuMs  = ComputePercentiles(gpuSamples);
    res.cpuRecordMs = ComputePercentiles(cpuSamples);
    res.hostHistMs  = host.HasValidTiming() ? host.GetLastHistMs() : 0.0f;
    res.hostAdaptMs = host.HasValidTiming() ? host.GetLastAdaptMs() : 0.0f;

    const bool optOk = (res.darkExposure > 4.0f) && (res.glacierExposure < 0.15f) &&
                       (res.fireflyErrPct < 10.0f) && (res.asymRatio > 1.8f) && res.nanFinite;
    const bool budgetOk = rig.isWarp ? true : (res.totalGpuMs.mean < 0.08);
    res.passed = optOk && budgetOk;
    if (!optOk) {
        res.note = "Auto-Exposure dark/glacier/firefly/asymmetric/NaN invariant failed";
    } else if (!budgetOk) {
        res.note = "HW GPU 960x540 Auto-Exposure mean exceeded 0.08 ms budget";
    } else {
        res.note = rig.isWarp ? "WARP 960x540 256-bin histogram & asymmetric eye-adapt PASS"
                              : "HW GPU < 0.08 ms & 256-bin histogram/firefly/eye-adapt PASS";
    }
    return res;
}

// ----------------------------------------------------------------------------
// 16. GPU 3D Volumetric Cloud Raymarcher + Beer-Powder Silver-Lining (RawCloudRaymarchHost)
// ----------------------------------------------------------------------------
CloudRaymarchBenchResult RunCloudRaymarchBenchmark(BenchDx12Rig& rig, uint32_t warmupFrames, uint32_t measuredFrames)
{
    CloudRaymarchBenchResult res{};
    constexpr uint32_t kW = 480u;
    constexpr uint32_t kH = 270u;
    res.width  = kW;
    res.height = kH;

    raw::bridge::RawCloudRaymarchHost host;
    raw::bridge::CloudRaymarchDesc desc{};
    desc.initialWidth  = kW;
    desc.initialHeight = kH;
    std::string err;
    if (!host.Init(rig.dev.Get(), desc, &err)) {
        res.note = "RawCloudRaymarchHost::Init failed: " + err;
        return res;
    }
    host.SetTimestampFrequency(rig.tsFreqHz);

    raw::bridge::CloudRaymarchParams baseP{};
    baseP.camPos[0] = 0.0f; baseP.camPos[1] = 80.0f; baseP.camPos[2] = 0.0f;
    baseP.camRight[0] = 1.0f; baseP.camRight[1] = 0.0f; baseP.camRight[2] = 0.0f;
    baseP.camUp[0]    = 0.0f; baseP.camUp[1]    = 1.0f; baseP.camUp[2]    = 0.0f;
    baseP.camForward[0] = 0.0f; baseP.camForward[1] = 0.0f; baseP.camForward[2] = 1.0f;
    baseP.fovYDeg       = 65.0f;
    baseP.timeSec       = 12.0f;
    baseP.planetRadiusM = 6371000.0f;
    baseP.cloudBaseM    = 1500.0f;
    baseP.cloudTopM     = 5500.0f;
    baseP.densityScale  = 0.0024f;
    baseP.anvilFactor   = 0.35f;

    // 1) Clear-sky exact (0,0,0,1.0) check
    raw::bridge::CloudRaymarchParams clearP = baseP;
    clearP.cloudCoverage = 0.0f;
    clearP.historyWeight = 0.0f;
    host.ResetHistory();
    if (!rig.BeginFrame()) {
        res.note = "BeginFrame (clear sky) failed";
        return res;
    }
    host.Execute(rig.cmd.Get(), kW, kH, clearP);
    if (!rig.FlushAndWait()) {
        res.note = "FlushAndWait (clear sky) failed";
        return res;
    }
    uint32_t rbW = 0u, rbH = 0u, rbD = 0u;
    std::vector<float> clearRgba;
    if (!rig.ReadbackRgba16Tex(host.GetOutputResource(), host.OutputResourceState(), rbW, rbH, rbD, clearRgba) ||
        rbW != kW || rbH != kH) {
        res.note = "Readback clear-sky cloud output failed";
        return res;
    }
    float clearMaxRad = 0.0f;
    float clearMinT   = 1.0f;
    for (size_t i = 0u; i < static_cast<size_t>(kW) * kH; ++i) {
        const float r = clearRgba[i * 4u + 0u];
        const float g = clearRgba[i * 4u + 1u];
        const float b = clearRgba[i * 4u + 2u];
        const float a = clearRgba[i * 4u + 3u];
        if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b) || !std::isfinite(a)) {
            ++res.nanInfCount;
            continue;
        }
        clearMaxRad = std::max({clearMaxRad, std::fabs(r), std::fabs(g), std::fabs(b)});
        clearMinT   = std::min(clearMinT, a);
    }
    res.clearMaxRad = clearMaxRad;

    // 2) Dense cumulus (coverage = 0.85) with temporal history + forward sun silver-lining timed loop
    raw::bridge::CloudRaymarchParams fwdP = baseP;
    fwdP.cloudCoverage = 0.85f;
    fwdP.historyWeight = 0.75f;
    fwdP.sunDir[0] = 0.0f; fwdP.sunDir[1] = 0.3090f; fwdP.sunDir[2] = 0.9511f;
    host.ResetHistory();

    std::vector<double> gpuSamples, cpuSamples;
    gpuSamples.reserve(measuredFrames);
    cpuSamples.reserve(measuredFrames);
    const uint32_t totalFrames = warmupFrames + measuredFrames;
    for (uint32_t f = 0u; f < totalFrames; ++f) {
        fwdP.frameIndex = f;
        const auto c0 = Clock::now();
        if (!rig.BeginFrame()) {
            res.note = "BeginFrame (dense clouds) failed";
            return res;
        }
        rig.MarkTimestamp(0u);
        host.Execute(rig.cmd.Get(), kW, kH, fwdP);
        rig.MarkTimestamp(1u);
        rig.ResolveTimestamps(0u, 2u);
        const auto c1 = Clock::now();
        if (!rig.FlushAndWait()) {
            res.note = "FlushAndWait (dense clouds) failed";
            return res;
        }
        if (f >= warmupFrames) {
            double gMs = 0.0;
            rig.ReadTimestampRangeMs(0u, 1u, gMs);
            gpuSamples.push_back(gMs);
            cpuSamples.push_back(ElapsedMs(c0, c1));
        }
    }

    res.cloudGpuMs  = ComputePercentiles(gpuSamples);
    res.cpuRecordMs = ComputePercentiles(cpuSamples);
    res.hostCloudMs = host.HasValidTiming() ? host.GetLastCloudMs() : 0.0f;

    std::vector<float> fwdRgba;
    if (!rig.ReadbackRgba16Tex(host.GetOutputResource(), host.OutputResourceState(), rbW, rbH, rbD, fwdRgba) ||
        rbW != kW || rbH != kH) {
        res.note = "Readback dense cloud output failed";
        return res;
    }

    const float tanHalfY = std::tan(0.5f * fwdP.fovYDeg * (3.14159265359f / 180.0f));
    const float aspect   = static_cast<float>(kW) / static_cast<float>(kH);
    const float tanHalfX = tanHalfY * aspect;

    uint32_t belowHorBad = 0u;
    uint32_t cloudyPx    = 0u;
    float    minT        = 1.0f;
    double   fwdLumaSum  = 0.0;
    uint32_t fwdSunPx    = 0u;

    for (uint32_t y = 0u; y < kH; ++y) {
        const float ndcY = 1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(kH) * 2.0f;
        const float dirY = ndcY * tanHalfY;
        for (uint32_t x = 0u; x < kW; ++x) {
            const float ndcX = (static_cast<float>(x) + 0.5f) / static_cast<float>(kW) * 2.0f - 1.0f;
            const float dirX = ndcX * tanHalfX;
            const float invL = 1.0f / std::sqrt(dirX * dirX + dirY * dirY + 1.0f);
            const float rx   = dirX * invL;
            const float ry   = dirY * invL;
            const float rz   = invL;

            const size_t i = static_cast<size_t>(y) * kW + x;
            const float r = fwdRgba[i * 4u + 0u];
            const float g = fwdRgba[i * 4u + 1u];
            const float b = fwdRgba[i * 4u + 2u];
            const float a = fwdRgba[i * 4u + 3u];
            if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b) || !std::isfinite(a)) {
                ++res.nanInfCount;
                continue;
            }
            if (ry <= 0.0f) {
                if (r != 0.0f || g != 0.0f || b != 0.0f || a != 1.0f) ++belowHorBad;
            } else {
                minT = std::min(minT, a);
                if (a < 0.85f) ++cloudyPx;
                const float cosSun = rx * fwdP.sunDir[0] + ry * fwdP.sunDir[1] + rz * fwdP.sunDir[2];
                if (cosSun > 0.927f && a < 0.90f) {
                    fwdLumaSum += (0.2126f * r + 0.7152f * g + 0.0722f * b);
                    ++fwdSunPx;
                }
            }
        }
    }
    res.belowHorizonBad = belowHorBad;
    res.minTrans        = minT;
    res.cloudyPixels    = cloudyPx;
    res.forwardLuma     = (fwdSunPx > 0u) ? static_cast<float>(fwdLumaSum / fwdSunPx) : 0.0f;

    // 3) Anti-solar probe on the exact same cloud field (silver-lining ratio proof)
    raw::bridge::CloudRaymarchParams antiP = fwdP;
    antiP.sunDir[0] = 0.0f; antiP.sunDir[1] = 0.3090f; antiP.sunDir[2] = -0.9511f;
    antiP.historyWeight = 0.0f;
    antiP.frameIndex    = 0u;
    host.ResetHistory();
    if (!rig.BeginFrame()) {
        res.note = "BeginFrame (anti-solar probe) failed";
        return res;
    }
    host.Execute(rig.cmd.Get(), kW, kH, antiP);
    if (!rig.FlushAndWait()) {
        res.note = "FlushAndWait (anti-solar probe) failed";
        return res;
    }
    std::vector<float> antiRgba;
    if (!rig.ReadbackRgba16Tex(host.GetOutputResource(), host.OutputResourceState(), rbW, rbH, rbD, antiRgba) ||
        rbW != kW || rbH != kH) {
        res.note = "Readback anti-solar cloud output failed";
        return res;
    }
    double   antiLumaSum = 0.0;
    uint32_t antiPx      = 0u;
    for (uint32_t y = 0u; y < kH; ++y) {
        const float ndcY = 1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(kH) * 2.0f;
        const float dirY = ndcY * tanHalfY;
        if (dirY <= 0.0f) continue;
        for (uint32_t x = 0u; x < kW; ++x) {
            const float ndcX = (static_cast<float>(x) + 0.5f) / static_cast<float>(kW) * 2.0f - 1.0f;
            const float dirX = ndcX * tanHalfX;
            const float invL = 1.0f / std::sqrt(dirX * dirX + dirY * dirY + 1.0f);
            const float rx   = dirX * invL;
            const float ry   = dirY * invL;
            const float rz   = invL;
            const float cosFwd = rx * fwdP.sunDir[0] + ry * fwdP.sunDir[1] + rz * fwdP.sunDir[2];
            const size_t i = static_cast<size_t>(y) * kW + x;
            const float r = antiRgba[i * 4u + 0u];
            const float g = antiRgba[i * 4u + 1u];
            const float b = antiRgba[i * 4u + 2u];
            const float a = antiRgba[i * 4u + 3u];
            if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b) || !std::isfinite(a)) {
                ++res.nanInfCount;
                continue;
            }
            if (cosFwd > 0.927f && a < 0.90f) {
                antiLumaSum += (0.2126f * r + 0.7152f * g + 0.0722f * b);
                ++antiPx;
            }
        }
    }
    res.antiSolarLuma = (antiPx > 0u) ? static_cast<float>(antiLumaSum / antiPx) : 0.0f;
    res.silverRatio   = res.forwardLuma / std::max(res.antiSolarLuma, 1.0e-5f);

    const bool optOk = (res.nanInfCount == 0u) && (res.clearMaxRad == 0.0f) && (clearMinT == 1.0f) &&
                       (res.belowHorizonBad == 0u) && (res.minTrans < 0.35f) && (res.cloudyPixels > 10000u) &&
                       (fwdSunPx > 500u) && (antiPx > 500u) && (res.silverRatio > 2.0f);
    const bool budgetOk = rig.isWarp ? true : (res.cloudGpuMs.mean < 0.15);
    res.passed = optOk && budgetOk;
    if (!optOk) {
        res.note = "Volumetric cloud clear/horizon/occlusion/silver-lining/NaN invariant failed";
    } else if (!budgetOk) {
        res.note = "HW GPU 480x270 Volumetric Cloud mean exceeded 0.15 ms budget";
    } else {
        res.note = rig.isWarp ? "WARP 480x270 Beer-Powder volumetric clouds PASS"
                              : "HW GPU < 0.15 ms & Beer-Powder silver-lining clouds PASS";
    }
    return res;
}

// ----------------------------------------------------------------------------
// 17. GPU Cinematic Post-Process: Tile-Max Motion Blur & Bokeh DoF (RawPostCinematicHost)
// ----------------------------------------------------------------------------
PostCinematicBenchResult RunPostCinematicBenchmark(BenchDx12Rig& rig, uint32_t warmupFrames, uint32_t measuredFrames)
{
    PostCinematicBenchResult res{};
    constexpr uint32_t kW = 960u;
    constexpr uint32_t kH = 540u;
    constexpr float kNearZ = 0.5f;
    constexpr float kFarZ  = 500.0f;
    res.width  = kW;
    res.height = kH;

    raw::bridge::RawPostCinematicHost host;
    raw::bridge::PostCinematicDesc desc{};
    desc.initialWidth  = kW;
    desc.initialHeight = kH;
    std::string err;
    if (!host.Init(rig.dev.Get(), desc, &err)) {
        res.note = "RawPostCinematicHost::Init failed: " + err;
        return res;
    }
    host.SetTimestampFrequency(rig.tsFreqHz);

    auto linearToStdZ = [](float vz) -> float {
        const float q = kFarZ / (kFarZ - kNearZ);
        return std::clamp(q * (1.0f - kNearZ / vz), 0.0f, 0.99997f);
    };
    const float zFocus = linearToStdZ(6.0f);
    const float zFar   = linearToStdZ(35.0f);

    auto uploadFloatTex = [&](ComPtr<ID3D12Resource>& tex, DXGI_FORMAT fmt,
                              uint32_t floatsPerPx, const std::vector<float>& data) -> bool {
        D3D12_HEAP_PROPERTIES hpDef{D3D12_HEAP_TYPE_DEFAULT};
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width            = kW;
        rd.Height           = kH;
        rd.DepthOrArraySize = 1u;
        rd.MipLevels        = 1u;
        rd.Format           = fmt;
        rd.SampleDesc.Count = 1u;
        if (FAILED(rig.dev->CreateCommittedResource(&hpDef, D3D12_HEAP_FLAG_NONE, &rd,
                                                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex)))) {
            return false;
        }
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        UINT numRows = 0u;
        UINT64 rowBytes = 0ull, totalBytes = 0ull;
        rig.dev->GetCopyableFootprints(&rd, 0u, 1u, 0ull, &fp, &numRows, &rowBytes, &totalBytes);

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
        if (FAILED(rig.dev->CreateCommittedResource(&hpUp, D3D12_HEAP_FLAG_NONE, &ubd,
                                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&up)))) {
            return false;
        }
        void* mapped = nullptr;
        const D3D12_RANGE rr{0u, 0u};
        if (FAILED(up->Map(0u, &rr, &mapped)) || !mapped) return false;
        auto* base = static_cast<uint8_t*>(mapped) + fp.Offset;
        const size_t srcRowBytes = static_cast<size_t>(kW) * floatsPerPx * sizeof(float);
        for (uint32_t y = 0u; y < kH; ++y) {
            std::memcpy(base + static_cast<size_t>(y) * fp.Footprint.RowPitch,
                        data.data() + static_cast<size_t>(y) * kW * floatsPerPx,
                        srcRowBytes);
        }
        up->Unmap(0u, nullptr);

        if (!rig.BeginFrame()) return false;
        D3D12_TEXTURE_COPY_LOCATION dst{tex.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
        dst.SubresourceIndex = 0u;
        D3D12_TEXTURE_COPY_LOCATION src{up.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}};
        src.PlacedFootprint = fp;
        rig.cmd->CopyTextureRegion(&dst, 0u, 0u, 0u, &src, nullptr);

        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = tex.Get();
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        rig.cmd->ResourceBarrier(1u, &b);
        return rig.FlushAndWait();
    };

    // Build test scenes: identity, directional +24px motion bar, DoF/foreground-bleed, and NaN/vignette
    const size_t nPx = static_cast<size_t>(kW) * kH;
    std::vector<float> idColor(nPx * 4u, 0.0f);
    std::vector<float> idDepth(nPx, zFocus);
    std::vector<float> zeroVel(nPx * 2u, 0.0f);
    std::vector<float> mbColor(nPx * 4u, 0.0f);
    std::vector<float> mbVel(nPx * 2u, 0.0f);
    std::vector<float> dofColor(nPx * 4u, 0.0f);
    std::vector<float> dofDepth(nPx, zFar);
    std::vector<float> nanColor(nPx * 4u, 1.0f);

    const float velU = 24.0f / static_cast<float>(kW);
    for (uint32_t y = 0u; y < kH; ++y) {
        for (uint32_t x = 0u; x < kW; ++x) {
            const size_t i = static_cast<size_t>(y) * kW + x;
            idColor[i * 4u + 0u] = static_cast<float>((x * 7u + y * 3u) % 256u) / 255.0f * 1.5f;
            idColor[i * 4u + 1u] = static_cast<float>((x * 5u + y * 11u) % 256u) / 255.0f * 1.2f;
            idColor[i * 4u + 2u] = static_cast<float>((x * 13u + y * 2u) % 256u) / 255.0f * 0.9f;
            idColor[i * 4u + 3u] = 1.0f;

            if (y >= 200u && y < 340u && x >= 470u && x < 490u) {
                mbColor[i * 4u + 0u] = 2.0f;
                mbColor[i * 4u + 1u] = 1.6f;
                mbColor[i * 4u + 2u] = 0.8f;
                mbColor[i * 4u + 3u] = 1.0f;
                mbVel[i * 2u + 0u]   = velU;
            }

            if (y < 40u) {
                dofDepth[i] = 1.0f;
                dofColor[i * 4u + 2u] = 0.4f;
                dofColor[i * 4u + 3u] = 1.0f;
            } else if (y >= 160u && y < 380u) {
                if (x >= 80u && x < 240u) {
                    const float s = (((x / 4u) & 1u) == 0u) ? 1.0f : 0.0f;
                    dofColor[i * 4u + 0u] = s; dofColor[i * 4u + 1u] = s; dofColor[i * 4u + 2u] = s; dofColor[i * 4u + 3u] = 1.0f;
                    dofDepth[i] = zFocus;
                } else if (x >= 360u && x < 520u) {
                    const float s = (((x / 4u) & 1u) == 0u) ? 1.0f : 0.0f;
                    dofColor[i * 4u + 0u] = s; dofColor[i * 4u + 1u] = s; dofColor[i * 4u + 2u] = s; dofColor[i * 4u + 3u] = 1.0f;
                    dofDepth[i] = zFar;
                } else if (x >= 600u && x < 880u) {
                    if (x >= 700u && x < 780u) {
                        dofColor[i * 4u + 0u] = 4.0f; dofColor[i * 4u + 3u] = 1.0f;
                        dofDepth[i] = zFocus;
                    } else {
                        dofColor[i * 4u + 2u] = 0.25f; dofColor[i * 4u + 3u] = 1.0f;
                        dofDepth[i] = zFar;
                    }
                }
            }
        }
    }
    const float qNaN = std::numeric_limits<float>::quiet_NaN();
    const float pInf = std::numeric_limits<float>::infinity();
    for (size_t i = 0u; i < nPx; i += 97u) {
        nanColor[i * 4u + 0u] = ((i & 1u) == 0u) ? qNaN : pInf;
    }

    ComPtr<ID3D12Resource> idColorTex, idDepthTex, zeroVelTex, mbColorTex, mbVelTex, dofColorTex, dofDepthTex, nanColorTex;
    if (!uploadFloatTex(idColorTex,  DXGI_FORMAT_R32G32B32A32_FLOAT, 4u, idColor) ||
        !uploadFloatTex(idDepthTex,  DXGI_FORMAT_R32_FLOAT,          1u, idDepth) ||
        !uploadFloatTex(zeroVelTex,  DXGI_FORMAT_R32G32_FLOAT,       2u, zeroVel) ||
        !uploadFloatTex(mbColorTex,  DXGI_FORMAT_R32G32B32A32_FLOAT, 4u, mbColor) ||
        !uploadFloatTex(mbVelTex,    DXGI_FORMAT_R32G32_FLOAT,       2u, mbVel)   ||
        !uploadFloatTex(dofColorTex, DXGI_FORMAT_R32G32B32A32_FLOAT, 4u, dofColor) ||
        !uploadFloatTex(dofDepthTex, DXGI_FORMAT_R32_FLOAT,          1u, dofDepth) ||
        !uploadFloatTex(nanColorTex, DXGI_FORMAT_R32G32B32A32_FLOAT, 4u, nanColor)) {
        res.note = "Upload 960x540 PostCinematic test textures failed";
        return res;
    }

    uint32_t rbW = 0u, rbH = 0u, rbD = 0u;
    std::vector<float> outRgba;

    // 1. Zero-velocity & zero-DoF/vignette identity
    raw::bridge::PostCinematicParams pId{};
    pId.nearZ = kNearZ; pId.farZ = kFarZ;
    pId.dofStrength = 0.0f; pId.motionBlurScale = 1.0f; pId.vignetteStrength = 0.0f; pId.frameJitter = 0.0f;
    if (!rig.BeginFrame() || !host.Execute(rig.cmd.Get(), idColorTex.Get(), idDepthTex.Get(), zeroVelTex.Get(), kW, kH, pId) ||
        !rig.FlushAndWait() || !rig.ReadbackRgba16Tex(host.GetOutputResource(), host.OutputResourceState(), rbW, rbH, rbD, outRgba)) {
        res.note = "PostCinematic identity pass failed";
        return res;
    }
    res.identityMaxDiff = 0.0f;
    for (size_t i = 0u; i < nPx; ++i) {
        for (int c = 0; c < 3; ++c) {
            res.identityMaxDiff = std::max(res.identityMaxDiff, std::fabs(outRgba[i * 4u + c] - idColor[i * 4u + c]));
        }
    }

    // 2. Directional motion streak (+24px X)
    raw::bridge::PostCinematicParams pMb = pId;
    pMb.motionBlurScale = 1.0f; pMb.maxMotionRadiusPx = 24.0f;
    if (!rig.BeginFrame() || !host.Execute(rig.cmd.Get(), mbColorTex.Get(), idDepthTex.Get(), mbVelTex.Get(), kW, kH, pMb) ||
        !rig.FlushAndWait() || !rig.ReadbackRgba16Tex(host.GetOutputResource(), host.OutputResourceState(), rbW, rbH, rbD, outRgba)) {
        res.note = "PostCinematic motion blur pass failed";
        return res;
    }
    res.horizStreakR = outRgba[(static_cast<size_t>(270u) * kW + 498u) * 4u + 0u];
    res.vertSmearR   = outRgba[(static_cast<size_t>(192u) * kW + 480u) * 4u + 0u];

    // 3. Physical CoC focus sharpness vs far bokeh blur + foreground bleed guard
    raw::bridge::PostCinematicParams pDof{};
    pDof.nearZ = kNearZ; pDof.farZ = kFarZ;
    pDof.focusDistanceM = 6.0f; pDof.focalLengthMm = 50.0f; pDof.apertureFStop = 1.8f; pDof.cocScale = 2.0f;
    pDof.dofStrength = 1.0f; pDof.maxBokehRadiusPx = 10.0f;
    pDof.motionBlurScale = 0.0f; pDof.vignetteStrength = 0.0f; pDof.frameJitter = 0.0f;
    if (!rig.BeginFrame() || !host.Execute(rig.cmd.Get(), dofColorTex.Get(), dofDepthTex.Get(), nullptr, kW, kH, pDof) ||
        !rig.FlushAndWait() || !rig.ReadbackRgba16Tex(host.GetOutputResource(), host.OutputResourceState(), rbW, rbH, rbD, outRgba)) {
        res.note = "PostCinematic DoF pass failed";
        return res;
    }
    auto patchStd = [&](uint32_t x0, uint32_t x1, uint32_t y0, uint32_t y1) -> float {
        double s = 0.0, s2 = 0.0;
        uint32_t c = 0u;
        for (uint32_t y = y0; y < y1; ++y) {
            for (uint32_t x = x0; x < x1; ++x) {
                const float v = outRgba[(static_cast<size_t>(y) * kW + x) * 4u + 0u];
                s += v; s2 += v * v; ++c;
            }
        }
        const double m = s / c;
        return static_cast<float>(std::sqrt(std::max(0.0, s2 / c - m * m)));
    };
    res.focusStd   = patchStd(110u, 210u, 200u, 340u);
    res.farBlurStd = patchStd(390u, 490u, 200u, 340u);
    res.maxBgBleedR = 0.0f;
    for (uint32_t x = 692u; x < 700u; ++x) res.maxBgBleedR = std::max(res.maxBgBleedR, outRgba[(static_cast<size_t>(270u) * kW + x) * 4u + 0u]);
    for (uint32_t x = 780u; x < 788u; ++x) res.maxBgBleedR = std::max(res.maxBgBleedR, outRgba[(static_cast<size_t>(270u) * kW + x) * 4u + 0u]);

    // 4. Filmic vignette + NaN/Inf resilience
    raw::bridge::PostCinematicParams pFull = pDof;
    pFull.motionBlurScale = 1.0f; pFull.vignetteStrength = 0.35f;
    if (!rig.BeginFrame() || !host.Execute(rig.cmd.Get(), nanColorTex.Get(), dofDepthTex.Get(), mbVelTex.Get(), kW, kH, pFull) ||
        !rig.FlushAndWait() || !rig.ReadbackRgba16Tex(host.GetOutputResource(), host.OutputResourceState(), rbW, rbH, rbD, outRgba)) {
        res.note = "PostCinematic vignette+NaN pass failed";
        return res;
    }
    for (float v : outRgba) {
        if (!std::isfinite(v)) ++res.nanInfCount;
    }
    const float centerG = outRgba[(static_cast<size_t>(kH / 2u) * kW + (kW / 2u)) * 4u + 1u];
    const float cornerG = outRgba[(static_cast<size_t>(4u) * kW + 4u) * 4u + 1u];
    res.vignetteFalloffPct = (1.0f - cornerG / std::max(centerG, 1.0e-5f)) * 100.0f;

    // 5. Steady-state GPU timing benchmark (full 3-pass TileMax + NeighborMax + DoF + MotionBlur + Vignette)
    std::vector<double> gpuSamples, cpuSamples;
    gpuSamples.reserve(measuredFrames);
    cpuSamples.reserve(measuredFrames);
    const uint32_t totalFrames = warmupFrames + measuredFrames;
    for (uint32_t f = 0u; f < totalFrames; ++f) {
        pFull.frameJitter = static_cast<float>(f & 63u);
        const auto c0 = Clock::now();
        if (!rig.BeginFrame()) { res.note = "BeginFrame (timing) failed"; return res; }
        rig.MarkTimestamp(0u);
        host.Execute(rig.cmd.Get(), dofColorTex.Get(), dofDepthTex.Get(), mbVelTex.Get(), kW, kH, pFull);
        rig.MarkTimestamp(1u);
        rig.ResolveTimestamps(0u, 2u);
        const auto c1 = Clock::now();
        if (!rig.FlushAndWait()) { res.note = "FlushAndWait (timing) failed"; return res; }
        if (f >= warmupFrames) {
            double gMs = 0.0;
            rig.ReadTimestampRangeMs(0u, 1u, gMs);
            gpuSamples.push_back(gMs);
            cpuSamples.push_back(ElapsedMs(c0, c1));
        }
    }

    res.totalGpuMs  = ComputePercentiles(gpuSamples);
    res.cpuRecordMs = ComputePercentiles(cpuSamples);
    res.hostTileMs  = host.HasValidTiming() ? host.GetLastTileMs() : 0.0f;
    res.hostCompMs  = host.HasValidTiming() ? host.GetLastCompositeMs() : 0.0f;

    const bool optOk = (res.nanInfCount == 0u) && (res.identityMaxDiff < 0.002f) &&
                       (res.horizStreakR > 0.25f) && (res.vertSmearR < 0.005f) &&
                       (res.focusStd > 0.49f) && (res.farBlurStd < 0.25f) &&
                       (res.maxBgBleedR < 0.005f) && (res.vignetteFalloffPct > 10.0f);
    const bool budgetOk = rig.isWarp ? true : (res.totalGpuMs.mean < 0.08);
    res.passed = optOk && budgetOk;
    if (!optOk) {
        res.note = "PostCinematic identity/motion/DoF/bleed-guard/vignette/NaN invariant failed";
    } else if (!budgetOk) {
        res.note = "HW GPU 960x540 PostCinematic mean exceeded 0.08 ms budget";
    } else {
        res.note = rig.isWarp ? "WARP 960x540 Tile-Max MotionBlur & Bokeh DoF PASS"
                              : "HW GPU < 0.08 ms & MotionBlur/Bokeh/Zero-Bleed PASS";
    }
    return res;
}

// ----------------------------------------------------------------------------
// 18. GPU 16,384-Particle 3D Physics & Screen-Space Additive Splat (RawGpuParticlesHost)
// ----------------------------------------------------------------------------
GpuParticlesBenchResult RunGpuParticlesBenchmark(BenchDx12Rig& rig, uint32_t warmupFrames, uint32_t measuredFrames)
{
    GpuParticlesBenchResult res{};
    constexpr uint32_t kCount = 16384u;
    constexpr uint32_t kW     = 480u;
    constexpr uint32_t kH     = 270u;
    res.particleCount = kCount;
    res.splatWidth    = kW;
    res.splatHeight   = kH;

    raw::bridge::RawGpuParticlesHost host;
    raw::bridge::GpuParticlesDesc desc{};
    desc.particleCount = kCount;
    desc.splatWidth    = kW;
    desc.splatHeight   = kH;
    std::string err;
    if (!host.Init(rig.dev.Get(), desc, &err)) {
        res.note = "RawGpuParticlesHost::Init failed: " + err;
        return res;
    }
    host.SetTimestampFrequency(rig.tsFreqHz);

    const float eye[3]    = {0.0f, 2.5f, -9.0f};
    const float target[3] = {0.0f, 2.0f,  0.0f};
    const float up[3]     = {0.0f, 1.0f,  0.0f};
    float viewProj[16]{};
    raw::bridge::BuildParticlesViewProjLH(eye, target, up, 1.04719755f,
                                          static_cast<float>(kW) / static_cast<float>(kH),
                                          0.1f, 100.0f, viewProj);

    auto runFixedSteps = [&](const raw::bridge::GpuParticlesParams& pBase, uint32_t steps) -> bool {
        raw::bridge::GpuParticlesParams p = pBase;
        for (uint32_t s = 0u; s < steps; ++s) {
            p.resetAll   = (s == 0u);
            p.frameIndex = s;
            p.timeSec    = static_cast<float>(s) * p.dtSec;
            if (!rig.BeginFrame()) return false;
            host.Execute(rig.cmd.Get(), p);
            if (!rig.FlushAndWait()) return false;
        }
        return true;
    };

    auto readParticles = [&](std::vector<raw::bridge::GpuParticleCpuView>& outP) -> bool {
        std::vector<uint8_t> raw;
        if (!rig.ReadbackBuffer(host.GetParticleBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                static_cast<uint64_t>(kCount) * sizeof(raw::bridge::GpuParticleCpuView), raw)) {
            return false;
        }
        outP.resize(kCount);
        std::memcpy(outP.data(), raw.data(), raw.size());
        for (const auto& p : outP) {
            if (!std::isfinite(p.pos[0]) || !std::isfinite(p.pos[1]) || !std::isfinite(p.pos[2]) ||
                !std::isfinite(p.vel[0]) || !std::isfinite(p.vel[1]) || !std::isfinite(p.vel[2]) ||
                !std::isfinite(p.age) || !std::isfinite(p.maxLife)) {
                ++res.nanInfCount;
            }
        }
        return true;
    };

    auto readStats = [&](raw::bridge::GpuParticlesStats& st) -> bool {
        std::vector<uint8_t> raw;
        if (!rig.ReadbackBuffer(host.GetStatsBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 16ull, raw)) {
            return false;
        }
        std::memcpy(&st, raw.data(), sizeof(st));
        return true;
    };

    // 1) Upward rising fire embers + steady-state GPU benchmark
    raw::bridge::GpuParticlesParams emberP{};
    std::memcpy(emberP.viewProj, viewProj, sizeof(viewProj));
    emberP.emitterPos[0] = 0.0f; emberP.emitterPos[1] = 1.2f; emberP.emitterPos[2] = 0.0f;
    emberP.emitterRadius = 0.75f;
    emberP.windVel[0] = 0.0f; emberP.windVel[1] = 0.0f; emberP.windVel[2] = 0.0f;
    emberP.curlStrength = 1.6f;
    emberP.dtSec = 1.0f / 30.0f;
    emberP.groundY = 0.0f;
    emberP.restitution = 0.6f;
    emberP.buoyancyAccel = 4.6f;
    emberP.dragCoeff = 0.5f;
    emberP.mode = raw::bridge::ParticleMode::Embers;
    emberP.enableRespawn = true;
    emberP.enableGroundBounce = true;

    std::vector<double> gpuSamples, cpuSamples;
    gpuSamples.reserve(measuredFrames);
    cpuSamples.reserve(measuredFrames);
    const uint32_t totalFrames = warmupFrames + measuredFrames;
    for (uint32_t f = 0u; f < totalFrames; ++f) {
        emberP.resetAll   = (f == 0u);
        emberP.frameIndex = f;
        emberP.timeSec    = static_cast<float>(f) * emberP.dtSec;
        const auto c0 = Clock::now();
        if (!rig.BeginFrame()) {
            res.note = "BeginFrame (particles) failed";
            return res;
        }
        rig.MarkTimestamp(0u);
        host.Execute(rig.cmd.Get(), emberP);
        rig.MarkTimestamp(1u);
        rig.ResolveTimestamps(0u, 2u);
        const auto c1 = Clock::now();
        if (!rig.FlushAndWait()) {
            res.note = "FlushAndWait (particles) failed";
            return res;
        }
        if (f >= warmupFrames) {
            double gMs = 0.0;
            rig.ReadTimestampRangeMs(0u, 1u, gMs);
            gpuSamples.push_back(gMs);
            cpuSamples.push_back(ElapsedMs(c0, c1));
        }
    }

    res.totalGpuMs  = ComputePercentiles(gpuSamples);
    res.cpuRecordMs = ComputePercentiles(cpuSamples);
    res.hostSimMs   = host.HasValidTiming() ? host.GetLastSimMs() : 0.0f;
    res.hostSplatMs = host.HasValidTiming() ? host.GetLastSplatMs() : 0.0f;

    // 2) Deterministic physics & splat invariant probes (identical 16-step trajectory on HW & WARP)
    if (!runFixedSteps(emberP, 16u)) {
        res.note = "Ember physics probe failed";
        return res;
    }
    raw::bridge::GpuParticlesStats emberStats{};
    std::vector<raw::bridge::GpuParticleCpuView> particles;
    if (!readStats(emberStats) || !readParticles(particles)) {
        res.note = "Readback ember stats/particles failed";
        return res;
    }
    res.activeCount   = emberStats.activeCount;
    res.splattedCount = emberStats.splattedCount;
    double sumEmberY = 0.0, sumEmberVy = 0.0;
    for (const auto& p : particles) {
        sumEmberY  += p.pos[1];
        sumEmberVy += p.vel[1];
    }
    res.emberMeanY = static_cast<float>(sumEmberY / kCount);
    const float emberMeanVy = static_cast<float>(sumEmberVy / kCount);

    std::vector<uint8_t> splatRaw;
    if (!rig.ReadbackBuffer(host.GetSplatBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                            host.SplatBufferBytes(), splatRaw)) {
        res.note = "Readback particle splat buffer failed";
        return res;
    }
    const auto* splatU32 = reinterpret_cast<const uint32_t*>(splatRaw.data());
    uint32_t nonZeroPx = 0u;
    for (uint32_t i = 0u; i < kW * kH; ++i) {
        if (splatU32[i * 4u + 3u] > 0u &&
            (splatU32[i * 4u + 0u] > 0u || splatU32[i * 4u + 1u] > 0u || splatU32[i * 4u + 2u] > 0u)) {
            ++nonZeroPx;
        }
    }
    res.nonZeroSplatPx = nonZeroPx;

    // 3) Lifespan expiry conservation (enableRespawn = false)
    raw::bridge::GpuParticlesParams expireP = emberP;
    expireP.enableRespawn = false;
    expireP.dtSec = 0.12f;
    if (!runFixedSteps(expireP, 35u)) {
        res.note = "Expiry probe failed";
        return res;
    }
    raw::bridge::GpuParticlesStats expireStats{};
    readStats(expireStats);
    res.expiredActive = expireStats.activeCount;

    // 4) Downward falling snow gravity
    raw::bridge::GpuParticlesParams snowP = emberP;
    snowP.mode = raw::bridge::ParticleMode::Snow;
    snowP.emitterPos[1] = 8.0f;
    snowP.curlStrength = 0.4f;
    snowP.dtSec = 1.0f / 20.0f;
    if (!runFixedSteps(snowP, 16u) || !readParticles(particles)) {
        res.note = "Snow gravity probe failed";
        return res;
    }
    double sumSnowY = 0.0, sumSnowVy = 0.0;
    for (const auto& p : particles) {
        sumSnowY  += p.pos[1];
        sumSnowVy += p.vel[1];
    }
    res.snowMeanY = static_cast<float>(sumSnowY / kCount);
    const float snowMeanVy = static_cast<float>(sumSnowVy / kCount);

    // 5) +X wind drift advection
    raw::bridge::GpuParticlesParams windP = emberP;
    windP.windVel[0] = 14.0f;
    windP.curlStrength = 0.25f;
    windP.dragCoeff = 1.8f;
    windP.dtSec = 1.0f / 20.0f;
    if (!runFixedSteps(windP, 16u) || !readParticles(particles)) {
        res.note = "Wind advection probe failed";
        return res;
    }
    double sumWindX = 0.0;
    for (const auto& p : particles) sumWindX += p.pos[0];
    res.windMeanX = static_cast<float>(sumWindX / kCount);

    // 6) Rain ground elastic bounce restitution
    raw::bridge::GpuParticlesParams bounceP = emberP;
    bounceP.mode = raw::bridge::ParticleMode::Rain;
    bounceP.emitterPos[1] = 1.1f;
    bounceP.emitterRadius = 0.35f;
    bounceP.curlStrength = 0.0f;
    bounceP.groundY = 0.0f;
    bounceP.restitution = 0.78f;
    bounceP.dtSec = 1.0f / 40.0f;
    bounceP.enableRespawn = false;
    bounceP.enableGroundBounce = true;
    if (!runFixedSteps(bounceP, 18u) || !readParticles(particles)) {
        res.note = "Ground bounce probe failed";
        return res;
    }
    raw::bridge::GpuParticlesStats bounceStats{};
    readStats(bounceStats);
    res.bouncedCount = bounceStats.bouncedCount;
    float minY = 1.0e9f;
    uint32_t posRebound = 0u;
    for (const auto& p : particles) {
        minY = std::min(minY, p.pos[1]);
        if (p.bounceCount > 0u && p.vel[1] > 0.25f) ++posRebound;
    }
    res.minBounceY = minY;

    const bool optOk = (res.nanInfCount == 0u) && (res.activeCount == kCount) && (res.expiredActive == 0u) &&
                       (res.emberMeanY > 1.8f) && (emberMeanVy > 1.0f) &&
                       (res.snowMeanY < 7.2f) && (snowMeanVy < -0.5f) &&
                       (res.windMeanX > 2.5f) &&
                       (res.bouncedCount > 10000u) && (res.minBounceY >= -1.0e-3f) && (posRebound > 1000u) &&
                       (res.splattedCount > 10000u) && (res.nonZeroSplatPx > 200u);
    const bool budgetOk = rig.isWarp ? true : (res.totalGpuMs.mean < 0.08);
    res.passed = optOk && budgetOk;
    if (!optOk) {
        res.note = "GpuParticles conservation/buoyancy/wind/bounce/splat/NaN invariant failed";
    } else if (!budgetOk) {
        res.note = "HW GPU 16K GpuParticles mean exceeded 0.08 ms budget";
    } else {
        res.note = rig.isWarp ? "WARP 16K 3D particles physics & additive splat PASS"
                              : "HW GPU < 0.08 ms & 16K 3D physics/bounce/splat PASS";
    }
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
                    const TamrielBenchResult& tamriel,
                    const XpbdBenchResult& xpbd,
                    const GpuCullBenchResult& gpuCull,
                    const LightClusterBenchResult& lightClusters,
                    const VolumetricFroxelsBenchResult& volFroxels,
                    const ShadowPcssBenchResult& shadowPcss,
                    const GtaoBenchResult& gtao,
                    const SsrBenchResult& ssr,
                    const AutoExposureBenchResult& autoExposure,
                    const CloudRaymarchBenchResult& cloudRaymarch,
                    const PostCinematicBenchResult& postCinematic,
                    const GpuParticlesBenchResult& gpuParticles)
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
    os << "    },\n";

    // 8. GPU XPBD Soft-Body / Hair / Cloth / Bush-Bone Solver
    os << "    \"xpbdSim\": {\n";
    os << "      \"passed\": " << (xpbd.passed ? "true" : "false") << ",\n";
    os << "      \"chainCount\": " << xpbd.chainCount << ",\n";
    os << "      \"particleCount\": " << xpbd.particleCount << ",\n";
    os << "      \"substeps\": " << xpbd.substeps << ",\n";
    os << "      \"stepGpuMs\": "; pctObj(xpbd.stepGpuMs); os << ",\n";
    os << "      \"cpuRecordMs\": "; pctObj(xpbd.cpuRecordMs); os << ",\n";
    os << "      \"hostStepMs\": " << xpbd.hostStepMs << ",\n";
    os << "      \"maxRootDrift\": " << std::setprecision(7) << xpbd.maxRootDrift << std::setprecision(4) << ",\n";
    os << "      \"maxStretchRatio\": " << xpbd.maxStretchRatio << ",\n";
    os << "      \"minColliderSD\": " << xpbd.minColliderSD << ",\n";
    os << "      \"nanCount\": " << xpbd.nanCount << ",\n";
    os << "      \"note\": \"" << JsonEscape(xpbd.note) << "\"\n";
    os << "    },\n";

    // 9. GPU Conservative Hi-Z Pyramid + 16,384-Instance Culler
    os << "    \"gpuCull\": {\n";
    os << "      \"passed\": " << (gpuCull.passed ? "true" : "false") << ",\n";
    os << "      \"depthWidth\": " << gpuCull.depthWidth << ",\n";
    os << "      \"depthHeight\": " << gpuCull.depthHeight << ",\n";
    os << "      \"instanceCount\": " << gpuCull.instanceCount << ",\n";
    os << "      \"hizGpuMs\": "; pctObj(gpuCull.hizGpuMs); os << ",\n";
    os << "      \"cullGpuMs\": "; pctObj(gpuCull.cullGpuMs); os << ",\n";
    os << "      \"combinedGpuMs\": "; pctObj(gpuCull.combinedGpuMs); os << ",\n";
    os << "      \"cpuRecordMs\": "; pctObj(gpuCull.cpuRecordMs); os << ",\n";
    os << "      \"hostHiZMs\": " << gpuCull.hostHiZMs << ",\n";
    os << "      \"hostCullMs\": " << gpuCull.hostCullMs << ",\n";
    os << "      \"visibleCount\": " << gpuCull.visibleCount << ",\n";
    os << "      \"visibleMeshlets\": " << gpuCull.visibleMeshlets << ",\n";
    os << "      \"frustumCulled\": " << gpuCull.frustumCulled << ",\n";
    os << "      \"distanceCulled\": " << gpuCull.distanceCulled << ",\n";
    os << "      \"hizCulled\": " << gpuCull.hizCulled << ",\n";
    os << "      \"falseOcclusionCount\": " << gpuCull.falseOcclusionCount << ",\n";
    os << "      \"note\": \"" << JsonEscape(gpuCull.note) << "\"\n";
    os << "    },\n";

    // 10. GPU 16x9x24 3D Clustered 1,024-Light Grid
    os << "    \"lightClusters\": {\n";
    os << "      \"passed\": " << (lightClusters.passed ? "true" : "false") << ",\n";
    os << "      \"clusterCount\": " << lightClusters.clusterCount << ",\n";
    os << "      \"lightCount\": " << lightClusters.lightCount << ",\n";
    os << "      \"buildGpuMs\": "; pctObj(lightClusters.buildGpuMs); os << ",\n";
    os << "      \"assignGpuMs\": "; pctObj(lightClusters.assignGpuMs); os << ",\n";
    os << "      \"combinedGpuMs\": "; pctObj(lightClusters.combinedGpuMs); os << ",\n";
    os << "      \"cpuRecordMs\": "; pctObj(lightClusters.cpuRecordMs); os << ",\n";
    os << "      \"hostBuildMs\": " << lightClusters.hostBuildMs << ",\n";
    os << "      \"hostAssignMs\": " << lightClusters.hostAssignMs << ",\n";
    os << "      \"nonEmptyClusters\": " << lightClusters.nonEmptyClusters << ",\n";
    os << "      \"maxLightsCluster\": " << lightClusters.maxLightsCluster << ",\n";
    os << "      \"totalLightLinks\": " << lightClusters.totalLightLinks << ",\n";
    os << "      \"overflowErrors\": " << lightClusters.overflowErrors << ",\n";
    os << "      \"nanAabbCount\": " << lightClusters.nanAabbCount << ",\n";
    os << "      \"note\": \"" << JsonEscape(lightClusters.note) << "\"\n";
    os << "    },\n";

    // 11. GPU 160x90x64 3D Volumetric Froxels
    os << "    \"volumetricFroxels\": {\n";
    os << "      \"passed\": " << (volFroxels.passed ? "true" : "false") << ",\n";
    os << "      \"gridWidth\": " << volFroxels.gridWidth << ",\n";
    os << "      \"gridHeight\": " << volFroxels.gridHeight << ",\n";
    os << "      \"gridDepth\": " << volFroxels.gridDepth << ",\n";
    os << "      \"lightCount\": " << volFroxels.lightCount << ",\n";
    os << "      \"totalGpuMs\": "; pctObj(volFroxels.totalGpuMs); os << ",\n";
    os << "      \"cpuRecordMs\": "; pctObj(volFroxels.cpuRecordMs); os << ",\n";
    os << "      \"hostInjectMs\": " << volFroxels.hostInjectMs << ",\n";
    os << "      \"hostIntegrateMs\": " << volFroxels.hostIntegrateMs << ",\n";
    os << "      \"mieForwardRatio\": " << volFroxels.mieForwardRatio << ",\n";
    os << "      \"valleyTau\": " << volFroxels.valleyTau << ",\n";
    os << "      \"peakTau\": " << volFroxels.peakTau << ",\n";
    os << "      \"nanCount\": " << volFroxels.nanCount << ",\n";
    os << "      \"nonMonoCount\": " << volFroxels.nonMonoCount << ",\n";
    os << "      \"note\": \"" << JsonEscape(volFroxels.note) << "\"\n";
    os << "    },\n";

    // 12. GPU Variable-Penumbra PCSS Contact-Hardening Shadows
    os << "    \"shadowPcss\": {\n";
    os << "      \"passed\": " << (shadowPcss.passed ? "true" : "false") << ",\n";
    os << "      \"outWidth\": " << shadowPcss.outWidth << ",\n";
    os << "      \"outHeight\": " << shadowPcss.outHeight << ",\n";
    os << "      \"shadowMapSize\": " << shadowPcss.shadowMapSize << ",\n";
    os << "      \"pcssGpuMs\": "; pctObj(shadowPcss.pcssGpuMs); os << ",\n";
    os << "      \"cpuRecordMs\": "; pctObj(shadowPcss.cpuRecordMs); os << ",\n";
    os << "      \"hostPcssMs\": " << shadowPcss.hostPcssMs << ",\n";
    os << "      \"openVis\": " << shadowPcss.openVis << ",\n";
    os << "      \"nearUmbraVis\": " << shadowPcss.nearUmbraVis << ",\n";
    os << "      \"farUmbraVis\": " << shadowPcss.farUmbraVis << ",\n";
    os << "      \"skyMinVis\": " << shadowPcss.skyMinVis << ",\n";
    os << "      \"nearEdgePen\": " << shadowPcss.nearEdgePen << ",\n";
    os << "      \"farEdgePen\": " << shadowPcss.farEdgePen << ",\n";
    os << "      \"penumbraRatio\": " << shadowPcss.penumbraRatio << ",\n";
    os << "      \"softBandPx\": " << shadowPcss.softBandPx << ",\n";
    os << "      \"nanInfCount\": " << shadowPcss.nanInfCount << ",\n";
    os << "      \"note\": \"" << JsonEscape(shadowPcss.note) << "\"\n";
    os << "    },\n";

    // 13. GPU Jimenez GTAO + Unoccluded Bent Normals
    os << "    \"gtao\": {\n";
    os << "      \"passed\": " << (gtao.passed ? "true" : "false") << ",\n";
    os << "      \"width\": " << gtao.width << ",\n";
    os << "      \"height\": " << gtao.height << ",\n";
    os << "      \"totalGpuMs\": "; pctObj(gtao.totalGpuMs); os << ",\n";
    os << "      \"cpuRecordMs\": "; pctObj(gtao.cpuRecordMs); os << ",\n";
    os << "      \"hostMainMs\": " << gtao.hostMainMs << ",\n";
    os << "      \"hostDenoiseMs\": " << gtao.hostDenoiseMs << ",\n";
    os << "      \"openFloorVis\": " << gtao.openFloorVis << ",\n";
    os << "      \"openFloorBentY\": " << gtao.openFloorBentY << ",\n";
    os << "      \"creviceVis\": " << gtao.creviceVis << ",\n";
    os << "      \"creviceBentX\": " << gtao.creviceBentX << ",\n";
    os << "      \"pitVis\": " << gtao.pitVis << ",\n";
    os << "      \"nanCount\": " << gtao.nanCount << ",\n";
    os << "      \"skyBadCount\": " << gtao.skyBadCount << ",\n";
    os << "      \"note\": \"" << JsonEscape(gtao.note) << "\"\n";
    os << "    },\n";

    // 14. GPU GGX Stochastic Screen-Space Reflections (SSR)
    os << "    \"ssr\": {\n";
    os << "      \"passed\": " << (ssr.passed ? "true" : "false") << ",\n";
    os << "      \"width\": " << ssr.width << ",\n";
    os << "      \"height\": " << ssr.height << ",\n";
    os << "      \"ssrGpuMs\": "; pctObj(ssr.ssrGpuMs); os << ",\n";
    os << "      \"cpuRecordMs\": "; pctObj(ssr.cpuRecordMs); os << ",\n";
    os << "      \"hostSsrMs\": " << ssr.hostSsrMs << ",\n";
    os << "      \"targetPx\": " << ssr.targetPx << ",\n";
    os << "      \"meanConf\": " << ssr.meanConf << ",\n";
    os << "      \"meanR\": " << ssr.meanR << ",\n";
    os << "      \"meanB\": " << ssr.meanB << ",\n";
    os << "      \"rbRatio\": " << ssr.rbRatio << ",\n";
    os << "      \"maxSkyConf\": " << ssr.maxSkyConf << ",\n";
    os << "      \"maxOpenFloorConf\": " << ssr.maxOpenFloorConf << ",\n";
    os << "      \"mirrorSpanW\": " << ssr.mirrorSpanW << ",\n";
    os << "      \"roughSpanW\": " << ssr.roughSpanW << ",\n";
    os << "      \"nanInfCount\": " << ssr.nanInfCount << ",\n";
    os << "      \"note\": \"" << JsonEscape(ssr.note) << "\"\n";
    os << "    },\n";

    // 15. GPU 256-Bin Log-Luminance Auto-Exposure
    os << "    \"autoExposure\": {\n";
    os << "      \"passed\": " << (autoExposure.passed ? "true" : "false") << ",\n";
    os << "      \"width\": " << autoExposure.width << ",\n";
    os << "      \"height\": " << autoExposure.height << ",\n";
    os << "      \"totalGpuMs\": "; pctObj(autoExposure.totalGpuMs); os << ",\n";
    os << "      \"cpuRecordMs\": "; pctObj(autoExposure.cpuRecordMs); os << ",\n";
    os << "      \"hostHistMs\": " << autoExposure.hostHistMs << ",\n";
    os << "      \"hostAdaptMs\": " << autoExposure.hostAdaptMs << ",\n";
    os << "      \"darkExposure\": " << autoExposure.darkExposure << ",\n";
    os << "      \"glacierExposure\": " << autoExposure.glacierExposure << ",\n";
    os << "      \"fireflyTargetLum\": " << autoExposure.fireflyTargetLum << ",\n";
    os << "      \"fireflyErrPct\": " << autoExposure.fireflyErrPct << ",\n";
    os << "      \"asymBrightFrac\": " << autoExposure.asymBrightFrac << ",\n";
    os << "      \"asymDarkFrac\": " << autoExposure.asymDarkFrac << ",\n";
    os << "      \"asymRatio\": " << autoExposure.asymRatio << ",\n";
    os << "      \"nanFinite\": " << (autoExposure.nanFinite ? "true" : "false") << ",\n";
    os << "      \"note\": \"" << JsonEscape(autoExposure.note) << "\"\n";
    os << "    },\n";

    // 16. GPU 3D Volumetric Cloud Raymarcher
    os << "    \"cloudRaymarch\": {\n";
    os << "      \"passed\": " << (cloudRaymarch.passed ? "true" : "false") << ",\n";
    os << "      \"width\": " << cloudRaymarch.width << ",\n";
    os << "      \"height\": " << cloudRaymarch.height << ",\n";
    os << "      \"cloudGpuMs\": "; pctObj(cloudRaymarch.cloudGpuMs); os << ",\n";
    os << "      \"cpuRecordMs\": "; pctObj(cloudRaymarch.cpuRecordMs); os << ",\n";
    os << "      \"hostCloudMs\": " << cloudRaymarch.hostCloudMs << ",\n";
    os << "      \"clearMaxRad\": " << cloudRaymarch.clearMaxRad << ",\n";
    os << "      \"belowHorizonBad\": " << cloudRaymarch.belowHorizonBad << ",\n";
    os << "      \"minTrans\": " << cloudRaymarch.minTrans << ",\n";
    os << "      \"cloudyPixels\": " << cloudRaymarch.cloudyPixels << ",\n";
    os << "      \"forwardLuma\": " << cloudRaymarch.forwardLuma << ",\n";
    os << "      \"antiSolarLuma\": " << cloudRaymarch.antiSolarLuma << ",\n";
    os << "      \"silverRatio\": " << cloudRaymarch.silverRatio << ",\n";
    os << "      \"nanInfCount\": " << cloudRaymarch.nanInfCount << ",\n";
    os << "      \"note\": \"" << JsonEscape(cloudRaymarch.note) << "\"\n";
    os << "    },\n";

    // 17. GPU Cinematic Post-Process (Tile-Max Motion Blur & Bokeh DoF)
    os << "    \"postCinematic\": {\n";
    os << "      \"passed\": " << (postCinematic.passed ? "true" : "false") << ",\n";
    os << "      \"width\": " << postCinematic.width << ",\n";
    os << "      \"height\": " << postCinematic.height << ",\n";
    os << "      \"totalGpuMs\": "; pctObj(postCinematic.totalGpuMs); os << ",\n";
    os << "      \"cpuRecordMs\": "; pctObj(postCinematic.cpuRecordMs); os << ",\n";
    os << "      \"hostTileMs\": " << postCinematic.hostTileMs << ",\n";
    os << "      \"hostCompMs\": " << postCinematic.hostCompMs << ",\n";
    os << "      \"identityMaxDiff\": " << postCinematic.identityMaxDiff << ",\n";
    os << "      \"horizStreakR\": " << postCinematic.horizStreakR << ",\n";
    os << "      \"vertSmearR\": " << postCinematic.vertSmearR << ",\n";
    os << "      \"focusStd\": " << postCinematic.focusStd << ",\n";
    os << "      \"farBlurStd\": " << postCinematic.farBlurStd << ",\n";
    os << "      \"maxBgBleedR\": " << postCinematic.maxBgBleedR << ",\n";
    os << "      \"vignetteFalloffPct\": " << postCinematic.vignetteFalloffPct << ",\n";
    os << "      \"nanInfCount\": " << postCinematic.nanInfCount << ",\n";
    os << "      \"note\": \"" << JsonEscape(postCinematic.note) << "\"\n";
    os << "    },\n";

    // 18. GPU 16,384-Particle 3D Physics & Screen-Space Additive Splat
    os << "    \"gpuParticles\": {\n";
    os << "      \"passed\": " << (gpuParticles.passed ? "true" : "false") << ",\n";
    os << "      \"particleCount\": " << gpuParticles.particleCount << ",\n";
    os << "      \"splatWidth\": " << gpuParticles.splatWidth << ",\n";
    os << "      \"splatHeight\": " << gpuParticles.splatHeight << ",\n";
    os << "      \"totalGpuMs\": "; pctObj(gpuParticles.totalGpuMs); os << ",\n";
    os << "      \"cpuRecordMs\": "; pctObj(gpuParticles.cpuRecordMs); os << ",\n";
    os << "      \"hostSimMs\": " << gpuParticles.hostSimMs << ",\n";
    os << "      \"hostSplatMs\": " << gpuParticles.hostSplatMs << ",\n";
    os << "      \"activeCount\": " << gpuParticles.activeCount << ",\n";
    os << "      \"expiredActive\": " << gpuParticles.expiredActive << ",\n";
    os << "      \"emberMeanY\": " << gpuParticles.emberMeanY << ",\n";
    os << "      \"snowMeanY\": " << gpuParticles.snowMeanY << ",\n";
    os << "      \"windMeanX\": " << gpuParticles.windMeanX << ",\n";
    os << "      \"bouncedCount\": " << gpuParticles.bouncedCount << ",\n";
    os << "      \"minBounceY\": " << gpuParticles.minBounceY << ",\n";
    os << "      \"splattedCount\": " << gpuParticles.splattedCount << ",\n";
    os << "      \"nonZeroSplatPx\": " << gpuParticles.nonZeroSplatPx << ",\n";
    os << "      \"nanInfCount\": " << gpuParticles.nanInfCount << ",\n";
    os << "      \"note\": \"" << JsonEscape(gpuParticles.note) << "\"\n";
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
                    const TamrielBenchResult& tamriel,
                    const XpbdBenchResult& xpbd,
                    const GpuCullBenchResult& gpuCull,
                    const LightClusterBenchResult& lightClusters,
                    const VolumetricFroxelsBenchResult& volFroxels,
                    const ShadowPcssBenchResult& shadowPcss,
                    const GtaoBenchResult& gtao,
                    const SsrBenchResult& ssr,
                    const AutoExposureBenchResult& autoExposure,
                    const CloudRaymarchBenchResult& cloudRaymarch,
                    const PostCinematicBenchResult& postCinematic,
                    const GpuParticlesBenchResult& gpuParticles)
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

    std::snprintf(metric, sizeof(metric), "XPBD %.4f ms (p95 %.4f)", xpbd.stepGpuMs.mean, xpbd.stepGpuMs.p95);
    std::snprintf(detail, sizeof(detail), "%uc/%up | stretch=%.4f | minSD=%+.4f | drift=%.1e | NaN=%u",
                  xpbd.chainCount, xpbd.particleCount, xpbd.maxStretchRatio,
                  xpbd.minColliderSD, xpbd.maxRootDrift, xpbd.nanCount);
    std::printf("  %-30s | %-6s | %-28s | %s\n", "8. GPU XPBD Soft-Body (4096p)", xpbd.passed ? "PASS" : "FAIL", metric, detail);

    std::snprintf(metric, sizeof(metric), "HiZ %.4f + Cull %.4f ms", gpuCull.hizGpuMs.mean, gpuCull.cullGpuMs.mean);
    std::snprintf(detail, sizeof(detail), "Comb p50=%.4f p95=%.4f ms | vis=%u hiz=%u fr=%u | fOcc=%u",
                  gpuCull.combinedGpuMs.p50, gpuCull.combinedGpuMs.p95,
                  gpuCull.visibleCount, gpuCull.hizCulled, gpuCull.frustumCulled, gpuCull.falseOcclusionCount);
    std::printf("  %-30s | %-6s | %-28s | %s\n", "9. GPU Hi-Z + 16k Cull", gpuCull.passed ? "PASS" : "FAIL", metric, detail);

    std::snprintf(metric, sizeof(metric), "Grid %.4f ms (p95 %.4f)", lightClusters.combinedGpuMs.mean, lightClusters.combinedGpuMs.p95);
    std::snprintf(detail, sizeof(detail), "Bld=%.4f Asn=%.4f ms | lit=%u/%u links=%u | ovf=%u",
                  lightClusters.buildGpuMs.mean, lightClusters.assignGpuMs.mean,
                  lightClusters.nonEmptyClusters, lightClusters.clusterCount,
                  lightClusters.totalLightLinks, lightClusters.overflowErrors);
    std::printf("  %-30s | %-6s | %-28s | %s\n", "10. 3D Clustered 1024-Lights", lightClusters.passed ? "PASS" : "FAIL", metric, detail);

    std::snprintf(metric, sizeof(metric), "Froxel %.4f ms (p95 %.4f)", volFroxels.totalGpuMs.mean, volFroxels.totalGpuMs.p95);
    std::snprintf(detail, sizeof(detail), "Inj=%.4f Int=%.4f ms | Mie=%.1fx | tau=%.2f/%.2f | NaN=%u",
                  volFroxels.hostInjectMs, volFroxels.hostIntegrateMs,
                  volFroxels.mieForwardRatio, volFroxels.valleyTau, volFroxels.peakTau, volFroxels.nanCount);
    std::printf("  %-30s | %-6s | %-28s | %s\n", "11. 160x90x64 3D Vol Froxels", volFroxels.passed ? "PASS" : "FAIL", metric, detail);

    std::snprintf(metric, sizeof(metric), "PCSS %.4f ms (p95 %.4f)", shadowPcss.pcssGpuMs.mean, shadowPcss.pcssGpuMs.p95);
    std::snprintf(detail, sizeof(detail), "p50=%.4f ms | ratio=%.1fx | open=%.1f umb=%.1f sky=%.1f | NaN=%u",
                  shadowPcss.pcssGpuMs.p50, shadowPcss.penumbraRatio,
                  shadowPcss.openVis, shadowPcss.nearUmbraVis, shadowPcss.skyMinVis, shadowPcss.nanInfCount);
    std::printf("  %-30s | %-6s | %-28s | %s\n", "12. GPU Variable-Pen PCSS", shadowPcss.passed ? "PASS" : "FAIL", metric, detail);

    std::snprintf(metric, sizeof(metric), "GTAO %.4f ms (p95 %.4f)", gtao.totalGpuMs.mean, gtao.totalGpuMs.p95);
    std::snprintf(detail, sizeof(detail), "Main=%.4f Den=%.4f | flr=%.3f crv=%.3f bx=%+.3f pit=%.3f | NaN=%u",
                  gtao.hostMainMs, gtao.hostDenoiseMs,
                  gtao.openFloorVis, gtao.creviceVis, gtao.creviceBentX, gtao.pitVis, gtao.nanCount);
    std::printf("  %-30s | %-6s | %-28s | %s\n", "13. GPU Jimenez GTAO (960x540)", gtao.passed ? "PASS" : "FAIL", metric, detail);

    std::snprintf(metric, sizeof(metric), "SSR %.4f ms (p95 %.4f)", ssr.ssrGpuMs.mean, ssr.ssrGpuMs.p95);
    std::snprintf(detail, sizeof(detail), "host=%.4f | conf=%.3f R/B=%.1fx sky=%.1f span=%u->%u | NaN=%u",
                  ssr.hostSsrMs, ssr.meanConf, ssr.rbRatio, ssr.maxSkyConf,
                  ssr.mirrorSpanW, ssr.roughSpanW, ssr.nanInfCount);
    std::printf("  %-30s | %-6s | %-28s | %s\n", "14. GPU GGX Stochastic SSR", ssr.passed ? "PASS" : "FAIL", metric, detail);

    std::snprintf(metric, sizeof(metric), "AutoExp %.4f ms (p95 %.4f)", autoExposure.totalGpuMs.mean, autoExposure.totalGpuMs.p95);
    std::snprintf(detail, sizeof(detail), "H=%.4f A=%.4f | drk=%.2fx glc=%.3fx ffErr=%.2f%% asym=%.2fx",
                  autoExposure.hostHistMs, autoExposure.hostAdaptMs,
                  autoExposure.darkExposure, autoExposure.glacierExposure,
                  autoExposure.fireflyErrPct, autoExposure.asymRatio);
    std::printf("  %-30s | %-6s | %-28s | %s\n", "15. GPU 256-Bin Auto-Exposure", autoExposure.passed ? "PASS" : "FAIL", metric, detail);

    std::snprintf(metric, sizeof(metric), "Cloud %.4f ms (p95 %.4f)", cloudRaymarch.cloudGpuMs.mean, cloudRaymarch.cloudGpuMs.p95);
    std::snprintf(detail, sizeof(detail), "host=%.4f | minT=%.3f px=%u silver=%.2fx horBad=%u | NaN=%u",
                  cloudRaymarch.hostCloudMs, cloudRaymarch.minTrans, cloudRaymarch.cloudyPixels,
                  cloudRaymarch.silverRatio, cloudRaymarch.belowHorizonBad, cloudRaymarch.nanInfCount);
    std::printf("  %-30s | %-6s | %-28s | %s\n", "16. GPU 3D Volumetric Clouds", cloudRaymarch.passed ? "PASS" : "FAIL", metric, detail);

    std::snprintf(metric, sizeof(metric), "Post %.4f ms (p95 %.4f)", postCinematic.totalGpuMs.mean, postCinematic.totalGpuMs.p95);
    std::snprintf(detail, sizeof(detail), "T=%.4f C=%.4f | strk=%.2f dof=%.2f->%.2f bld=%.4f | NaN=%u",
                  postCinematic.hostTileMs, postCinematic.hostCompMs,
                  postCinematic.horizStreakR, postCinematic.focusStd, postCinematic.farBlurStd,
                  postCinematic.maxBgBleedR, postCinematic.nanInfCount);
    std::printf("  %-30s | %-6s | %-28s | %s\n", "17. GPU Cinematic MB + DoF", postCinematic.passed ? "PASS" : "FAIL", metric, detail);

    std::snprintf(metric, sizeof(metric), "Prt %.4f ms (p95 %.4f)", gpuParticles.totalGpuMs.mean, gpuParticles.totalGpuMs.p95);
    std::snprintf(detail, sizeof(detail), "S=%.4f P=%.4f | act=%u bnc=%u px=%u wX=%+.1f | NaN=%u",
                  gpuParticles.hostSimMs, gpuParticles.hostSplatMs,
                  gpuParticles.activeCount, gpuParticles.bouncedCount,
                  gpuParticles.nonZeroSplatPx, gpuParticles.windMeanX, gpuParticles.nanInfCount);
    std::printf("  %-30s | %-6s | %-28s | %s\n", "18. GPU 16K 3D Particles", gpuParticles.passed ? "PASS" : "FAIL", metric, detail);

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
    const XpbdBenchResult              xpbd          = RunXpbdBenchmark(rig, warmupFrames, measuredFrames);
    const GpuCullBenchResult           gpuCull       = RunGpuCullBenchmark(rig, warmupFrames, measuredFrames);
    const LightClusterBenchResult      lightClusters = RunLightClusterBenchmark(rig, warmupFrames, measuredFrames);
    const VolumetricFroxelsBenchResult volFroxels    = RunVolumetricFroxelsBenchmark(rig, warmupFrames, measuredFrames);
    const ShadowPcssBenchResult        shadowPcss    = RunShadowPcssBenchmark(rig, warmupFrames, measuredFrames);
    const GtaoBenchResult              gtao          = RunGtaoBenchmark(rig, warmupFrames, measuredFrames);
    const SsrBenchResult               ssr           = RunSsrBenchmark(rig, warmupFrames, measuredFrames);
    const AutoExposureBenchResult      autoExposure  = RunAutoExposureBenchmark(rig, warmupFrames, measuredFrames);
    const CloudRaymarchBenchResult     cloudRaymarch = RunCloudRaymarchBenchmark(rig, warmupFrames, measuredFrames);
    const PostCinematicBenchResult     postCinematic = RunPostCinematicBenchmark(rig, warmupFrames, measuredFrames);
    const GpuParticlesBenchResult      gpuParticles  = RunGpuParticlesBenchmark(rig, warmupFrames, measuredFrames);

    const uint32_t debugErrors = rig.DrainDebugErrors();
    const bool allPassed = fluid.passed && rc.passed && tex.passed && audio.passed &&
                           destr.passed && flora.passed && tamriel.passed && xpbd.passed &&
                           gpuCull.passed && lightClusters.passed && volFroxels.passed &&
                           shadowPcss.passed && gtao.passed && ssr.passed &&
                           autoExposure.passed && cloudRaymarch.passed &&
                           postCinematic.passed && gpuParticles.passed && (debugErrors == 0u);

    PrintScorecard(rig, quickMode, warmupFrames, measuredFrames, debugErrors,
                   fluid, rc, tex, audio, destr, flora, tamriel, xpbd, gpuCull, lightClusters, volFroxels, shadowPcss, gtao, ssr, autoExposure, cloudRaymarch, postCinematic, gpuParticles);

    if (!jsonPath.empty()) {
        WriteBenchJson(jsonPath, rig, quickMode, warmupFrames, measuredFrames, debugErrors, allPassed,
                       fluid, rc, tex, audio, destr, flora, tamriel, xpbd, gpuCull, lightClusters, volFroxels, shadowPcss, gtao, ssr, autoExposure, cloudRaymarch, postCinematic, gpuParticles);
        std::printf("  JSON report written: %s\n", jsonPath.c_str());
    }

    std::printf("  OVERALL SUITE RESULT: %s (d3d12DebugErrors=%u)\n\n",
                allPassed ? "PASS" : "FAIL", debugErrors);
    return allPassed ? 0 : 1;
}
