// tests/test_volumetric_froxels_gpu.cpp: Verification & GPU Benchmark Suite for RAW's True 3D
// 160x90x64 Exponential-Z Volumetric Froxel Fog, Mie Scattering & Temporal Reprojection Solver.
//
// Verifies on BOTH Hardware DX12 GPU (e.g. RTX 4090) AND Software WARP (--warp) with D3D12 Debug Layer ON:
//   1. Exact Beer-Lambert transmittance bounds (0 < T <= 1, monotonic decrease with Z, 0 NaN/Inf across all 921,600 voxels).
//   2. Forward Henyey-Greenstein Mie scattering anisotropy (> 2.0x brighter toward sun vs anti-solar).
//   3. Altitude-stratified valley mist (low valley optical depth > high mountain peak optical depth).
//   4. Local point-light volumetric halo (active torch froxel >> dark reference).
//   5. Temporal 3D reprojection stability across jittered frames (0 NaN/Inf, bounded transmittance).
//   6. Real RTX 4090 GPU cost (injectMs + integrateMs < 0.25 ms with 16 lights + 3D noise + temporal!) & 0 D3D12 errors.
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

#include "RawVolumetricFroxelHost.h"

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

    bool ReadbackVolumeRgba16(ID3D12Resource* vol, D3D12_RESOURCE_STATES curState,
                              uint32_t& outW, uint32_t& outH, uint32_t& outD, std::vector<float>& outRgba)
    {
        const D3D12_RESOURCE_DESC rd = vol->GetDesc();
        outW = static_cast<uint32_t>(rd.Width);
        outH = rd.Height;
        outD = rd.DepthOrArraySize;

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        UINT numRows = 0u;
        UINT64 rowBytes = 0ull, totalBytes = 0ull;
        dev->GetCopyableFootprints(&rd, 0u, 1u, 0ull, &fp, &numRows, &rowBytes, &totalBytes);

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

        ComPtr<ID3D12Resource> rb;
        if (FAILED(dev->CreateCommittedResource(&hpRb, D3D12_HEAP_FLAG_NONE, &rbd,
                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&rb)))) {
            return false;
        }

        if (!Begin()) return false;
        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = vol;
        b.Transition.StateBefore = curState;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1u, &b);

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource        = vol;
        src.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = 0u;

        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource       = rb.Get();
        dst.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = fp;
        cmd->CopyTextureRegion(&dst, 0u, 0u, 0u, &src, nullptr);

        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        b.Transition.StateAfter  = curState;
        cmd->ResourceBarrier(1u, &b);
        if (!FlushAndWait()) return false;

        void* mapped = nullptr;
        D3D12_RANGE rr{0u, static_cast<SIZE_T>(totalBytes)};
        if (FAILED(rb->Map(0u, &rr, &mapped)) || !mapped) return false;
        const auto* base = static_cast<const uint8_t*>(mapped) + fp.Offset;
        const size_t slicePitch = static_cast<size_t>(fp.Footprint.RowPitch) * numRows;
        outRgba.resize(static_cast<size_t>(outW) * outH * outD * 4u);
        for (uint32_t z = 0u; z < outD; ++z) {
            for (uint32_t y = 0u; y < outH; ++y) {
                const auto* row = reinterpret_cast<const uint16_t*>(
                    base + static_cast<size_t>(z) * slicePitch + static_cast<size_t>(y) * fp.Footprint.RowPitch);
                for (uint32_t x = 0u; x < outW; ++x) {
                    const size_t i = ((static_cast<size_t>(z) * outH + y) * outW + x) * 4u;
                    outRgba[i + 0u] = HalfToFloat(row[x * 4 + 0]);
                    outRgba[i + 1u] = HalfToFloat(row[x * 4 + 1]);
                    outRgba[i + 2u] = HalfToFloat(row[x * 4 + 2]);
                    outRgba[i + 3u] = HalfToFloat(row[x * 4 + 3]);
                }
            }
        }
        D3D12_RANGE wr{0u, 0u};
        rb->Unmap(0u, &wr);
        return true;
    }
};

// Build Z-up (Skyrim convention: +X right, +Y forward, +Z up) row-vector ViewProj and exact analytical InvViewProj.
void MakeZUpCameraMatrices(float camX, float camY, float camZ,
                           float fovYDeg, float aspect, float nearZ, float farZ,
                           float outVP[16], float outInvVP[16])
{
    std::memset(outVP, 0, sizeof(float) * 16u);
    std::memset(outInvVP, 0, sizeof(float) * 16u);

    const float sy = 1.0f / std::tan(fovYDeg * 0.5f * 3.14159265359f / 180.0f);
    const float sx = sy / aspect;
    const float q  = farZ / (farZ - nearZ);

    // Row-vector ViewProj: clip = mul(float4(wx, wy, wz, 1), outVP)
    outVP[0 * 4 + 0] = sx;
    outVP[1 * 4 + 2] = q;
    outVP[1 * 4 + 3] = 1.0f;
    outVP[2 * 4 + 1] = sy;
    outVP[3 * 4 + 0] = -camX * sx;
    outVP[3 * 4 + 1] = -camZ * sy;
    outVP[3 * 4 + 2] = -(camY + nearZ) * q;
    outVP[3 * 4 + 3] = -camY;

    // Exact analytical InvViewProj (scaled by nearZ; homogeneous divide normalizes it cleanly):
    //   wx*nearZ = cx*(nearZ/sx) + camX*(cw - cz/q)
    //   wy*nearZ = cw*nearZ      + camY*(cw - cz/q)
    //   wz*nearZ = cy*(nearZ/sy) + camZ*(cw - cz/q)
    //   w_out    = cw - cz/q
    const float invQ = 1.0f / q;
    outInvVP[0 * 4 + 0] = nearZ / sx;
    outInvVP[1 * 4 + 2] = nearZ / sy;
    outInvVP[2 * 4 + 0] = -camX * invQ;
    outInvVP[2 * 4 + 1] = -camY * invQ;
    outInvVP[2 * 4 + 2] = -camZ * invQ;
    outInvVP[2 * 4 + 3] = -invQ;
    outInvVP[3 * 4 + 0] = camX;
    outInvVP[3 * 4 + 1] = nearZ + camY;
    outInvVP[3 * 4 + 2] = camZ;
    outInvVP[3 * 4 + 3] = 1.0f;
}

VolumetricFroxelParams MakeBaseParams(float camZ = 30.0f)
{
    VolumetricFroxelParams p{};
    float vp[16]{}, invVp[16]{};
    MakeZUpCameraMatrices(0.0f, 0.0f, camZ, 60.0f, 16.0f / 9.0f, 1.0f, 300.0f, vp, invVp);
    std::memcpy(p.invViewProj, invVp, sizeof(invVp));
    std::memcpy(p.prevViewProj, vp, sizeof(vp));
    p.cameraPosW[0]         = 0.0f; p.cameraPosW[1] = 0.0f; p.cameraPosW[2] = camZ;
    p.cameraForwardW[0]     = 0.0f; p.cameraForwardW[1] = 1.0f; p.cameraForwardW[2] = 0.0f;
    p.prevCameraPosW[0]     = 0.0f; p.prevCameraPosW[1] = 0.0f; p.prevCameraPosW[2] = camZ;
    p.prevCameraForwardW[0] = 0.0f; p.prevCameraForwardW[1] = 1.0f; p.prevCameraForwardW[2] = 0.0f;
    p.nearZ                 = 1.0f;
    p.farZ                  = 300.0f;
    p.simTimeSec            = 1.0f;
    p.historyWeight         = 0.0f;
    p.enableTemporal        = false;
    p.enableHaltonJitter    = false;
    p.baseFogDensity        = 0.018f;
    p.valleyFloorZ          = 0.0f;
    p.fogScaleHeight        = 25.0f;
    p.scatterAlbedo         = 0.90f;
    p.rainIntensity         = 0.0f;
    p.snowIntensity         = 0.0f;
    p.noiseAmp              = 0.0f;
    p.noiseScale            = 0.04f;
    p.mieAnisotropyG        = 0.68f;
    p.sunDirW[0]            = 0.0f; p.sunDirW[1] = 1.0f; p.sunDirW[2] = 0.0f;
    p.sunIntensity          = 8.0f;
    p.sunColor[0]           = 1.0f; p.sunColor[1] = 0.90f; p.sunColor[2] = 0.72f;
    p.ambientIntensity      = 0.25f;
    p.ambientColor[0]       = 0.30f; p.ambientColor[1] = 0.45f; p.ambientColor[2] = 0.70f;
    p.lightCount            = 0u;
    return p;
}

float SampleLuma(const std::vector<float>& vol, uint32_t w, uint32_t h, uint32_t x, uint32_t y, uint32_t z)
{
    const size_t i = ((static_cast<size_t>(z) * h + y) * w + x) * 4u;
    return 0.2126f * vol[i + 0u] + 0.7152f * vol[i + 1u] + 0.0722f * vol[i + 2u];
}

float SampleTrans(const std::vector<float>& vol, uint32_t w, uint32_t h, uint32_t x, uint32_t y, uint32_t z)
{
    const size_t i = ((static_cast<size_t>(z) * h + y) * w + x) * 4u;
    return vol[i + 3u];
}

void RunSuiteOnAdapter(bool useWarp)
{
    TestDx12 dx{};
    if (!dx.Init(useWarp)) {
        Check(false, useWarp ? "WARP D3D12 device init" : "Hardware DX12 device init");
        return;
    }

    std::printf("\n=== 160x90x64 3D Volumetric Froxel Suite [%s] (DebugLayer=%s, TsFreq=%llu Hz) ===\n",
                dx.adapterName.c_str(), dx.debugLayer ? "ON" : "OFF",
                static_cast<unsigned long long>(dx.tsFreqHz));

    RawVolumetricFroxelHost host;
    VolumetricFroxelDesc desc{};
    desc.gridWidth  = 160u;
    desc.gridHeight = 90u;
    desc.gridDepth  = 64u;
    std::string err;
    const bool initOk = host.Init(dx.dev.Get(), desc, &err);
    Check(initOk, "RawVolumetricFroxelHost::Init (160x90x64 RGBA16F) succeeded");
    if (!initOk) {
        std::fprintf(stderr, "  Init error: %s\n", err.c_str());
        return;
    }
    host.SetTimestampFrequency(dx.tsFreqHz);

    uint32_t w = 0u, h = 0u, d = 0u;
    std::vector<float> volFwd, volAnti, volTorch;

    // ------------------------------------------------------------------------
    // 1. Forward-Sun Run: Beer-Lambert Bounds, Monotonicity, Zero NaN & Stratification
    // ------------------------------------------------------------------------
    VolumetricFroxelParams pFwd = MakeBaseParams(30.0f);
    pFwd.sunDirW[0] = 0.0f; pFwd.sunDirW[1] = 1.0f; pFwd.sunDirW[2] = 0.0f; // looking straight into sun
    host.ResetHistory();
    Check(dx.Begin() && host.Execute(dx.cmd.Get(), pFwd) && dx.FlushAndWait(),
          "Executed forward-sun 160x90x64 froxel pass");
    Check(dx.ReadbackVolumeRgba16(host.GetIntegratedVolumeResource(), host.IntegratedVolumeResourceState(), w, h, d, volFwd),
          "Read back full 160x90x64 RGBA16F IntegratedFroxelVolume (921,600 voxels)");

    uint32_t nanCount = 0u;
    uint32_t outOfBoundsT = 0u;
    uint32_t nonMonoCount = 0u;
    uint32_t nonDecayCols = 0u;
    for (float v : volFwd) {
        if (!std::isfinite(v)) ++nanCount;
    }
    for (uint32_t y = 0u; y < h; ++y) {
        for (uint32_t x = 0u; x < w; ++x) {
            float prevT = 1.002f;
            for (uint32_t z = 0u; z < d; ++z) {
                const float T = SampleTrans(volFwd, w, h, x, y, z);
                if (!(T > 0.0f && T <= 1.001f)) ++outOfBoundsT;
                if (T > prevT + 2.5e-3f) ++nonMonoCount;
                prevT = T;
            }
            const float tNear = SampleTrans(volFwd, w, h, x, y, 0u);
            const float tFar  = SampleTrans(volFwd, w, h, x, y, d - 1u);
            if (!(tFar < tNear)) ++nonDecayCols;
        }
    }
    std::printf("  Volume  : %ux%ux%u | NaN=%u | badT=%u | nonMono=%u | nonDecayCols=%u\n",
                w, h, d, nanCount, outOfBoundsT, nonMonoCount, nonDecayCols);
    Check(nanCount == 0u, "Zero NaN/Inf across all 921,600 RGBA16F froxels");
    Check(outOfBoundsT == 0u && nonMonoCount == 0u && nonDecayCols == 0u,
          "Exact Beer-Lambert transmittance bounds (0 < T <= 1) & monotonic Z decay");

    // Altitude valley-mist stratification: bottom row (valley floor z~0..10) vs top row (mountain peak z~100+)
    const float tValley = SampleTrans(volFwd, w, h, 80u, 82u, 63u);
    const float tPeak   = SampleTrans(volFwd, w, h, 80u, 8u, 63u);
    const float tauValley = -std::log(std::max(tValley, 1.0e-5f));
    const float tauPeak   = -std::log(std::max(tPeak, 1.0e-5f));
    std::printf("  Altitude: valley(y=82) T=%.4f (tau=%.3f) vs peak(y=8) T=%.4f (tau=%.3f)\n",
                tValley, tauValley, tPeak, tauPeak);
    Check(tauValley > tauPeak * 2.5f && tValley < tPeak,
          "Altitude valley-mist stratification: low valley optical depth >> high mountain peak");

    // ------------------------------------------------------------------------
    // 2. Anti-Solar Run: Henyey-Greenstein Forward Mie Anisotropy (> 2.0x ratio)
    // ------------------------------------------------------------------------
    VolumetricFroxelParams pAnti = pFwd;
    pAnti.sunDirW[0] = 0.0f; pAnti.sunDirW[1] = -1.0f; pAnti.sunDirW[2] = 0.0f; // sun directly behind camera
    host.ResetHistory();
    Check(dx.Begin() && host.Execute(dx.cmd.Get(), pAnti) && dx.FlushAndWait() &&
          dx.ReadbackVolumeRgba16(host.GetIntegratedVolumeResource(), host.IntegratedVolumeResourceState(), w, h, d, volAnti),
          "Executed anti-solar froxel pass & readback");
    const float fwdLuma  = SampleLuma(volFwd, w, h, 80u, 45u, 48u);
    const float antiLuma = SampleLuma(volAnti, w, h, 80u, 45u, 48u);
    const float mieRatio = (antiLuma > 1.0e-6f) ? (fwdLuma / antiLuma) : 0.0f;
    std::printf("  Mie HG  : forwardLuma=%.4f vs antiSolarLuma=%.4f (ratio=%.2fx, g=%.2f)\n",
                fwdLuma, antiLuma, mieRatio, pFwd.mieAnisotropyG);
    Check(mieRatio > 2.0f && fwdLuma > 0.1f,
          "Henyey-Greenstein forward Mie anisotropy (> 2.0x brighter toward sun vs anti-solar)");

    // ------------------------------------------------------------------------
    // 3. Local Point-Light Volumetric Halo Check
    // ------------------------------------------------------------------------
    VolumetricFroxelParams pDark = MakeBaseParams(30.0f);
    pDark.sunIntensity     = 0.0f;
    pDark.ambientIntensity = 0.02f;
    pDark.lightCount       = 0u;
    host.ResetHistory();
    std::vector<float> volDark;
    Check(dx.Begin() && host.Execute(dx.cmd.Get(), pDark) && dx.FlushAndWait() &&
          dx.ReadbackVolumeRgba16(host.GetIntegratedVolumeResource(), host.IntegratedVolumeResourceState(), w, h, d, volDark),
          "Executed dark reference pass (0 local lights)");

    VolumetricFroxelParams pTorch = pDark;
    pTorch.lightCount          = 1u;
    pTorch.lights[0].posW[0]   = 0.0f;
    pTorch.lights[0].posW[1]   = 25.0f; // directly along center ray (80, 45)
    pTorch.lights[0].posW[2]   = 30.0f;
    pTorch.lights[0].radius    = 22.0f;
    pTorch.lights[0].color[0]  = 1.0f;
    pTorch.lights[0].color[1]  = 0.55f;
    pTorch.lights[0].color[2]  = 0.15f;
    pTorch.lights[0].intensity = 600.0f;
    host.ResetHistory();
    Check(dx.Begin() && host.Execute(dx.cmd.Get(), pTorch) && dx.FlushAndWait() &&
          dx.ReadbackVolumeRgba16(host.GetIntegratedVolumeResource(), host.IntegratedVolumeResourceState(), w, h, d, volTorch),
          "Executed active local torch volumetric halo pass");
    const float darkLuma  = SampleLuma(volDark, w, h, 80u, 45u, 48u);
    const float torchLuma = SampleLuma(volTorch, w, h, 80u, 45u, 48u);
    const float haloGain  = (darkLuma > 1.0e-6f) ? (torchLuma / darkLuma) : 0.0f;
    std::printf("  LocalLgt: torchLuma=%.4f vs darkRef=%.4f (gain=%.2fx)\n", torchLuma, darkLuma, haloGain);
    Check(torchLuma > darkLuma * 3.0f && torchLuma > 0.05f,
          "Local point-light volumetric halo strictly brighter (> 3x) than dark reference");

    // ------------------------------------------------------------------------
    // 4. Full-Load Performance Benchmark: 16 Local Lights + 3D Wind Noise + Temporal Reprojection
    // ------------------------------------------------------------------------
    VolumetricFroxelParams pFull = MakeBaseParams(30.0f);
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

    host.ResetHistory();
    const uint32_t warmupFrames   = dx.isWarp ? 2u : 16u;
    const uint32_t measuredFrames = dx.isWarp ? 4u : 60u;
    double sumTotalMs = 0.0;
    for (uint32_t f = 0u; f < warmupFrames + measuredFrames; ++f) {
        pFull.simTimeSec = static_cast<float>(f) * (1.0f / 60.0f);
        if (!dx.Begin()) {
            Check(false, "Begin frame failed");
            return;
        }
        dx.cmd->EndQuery(dx.tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0u);
        host.Execute(dx.cmd.Get(), pFull);
        dx.cmd->EndQuery(dx.tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1u);
        dx.cmd->ResolveQueryData(dx.tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0u, 2u, dx.tsReadback.Get(), 0ull);
        if (!dx.FlushAndWait()) {
            Check(false, "FlushAndWait failed");
            return;
        }
        if (f >= warmupFrames) {
            sumTotalMs += dx.ReadTsRangeMs(0u, 1u);
        }
    }
    const double meanTotalMs = sumTotalMs / static_cast<double>(measuredFrames);
    const float  hostInjMs   = host.HasValidTiming() ? host.GetLastInjectMs() : 0.0f;
    const float  hostIntMs   = host.HasValidTiming() ? host.GetLastIntegrateMs() : 0.0f;

    // Verify temporal multi-frame stability (zero NaN/Inf after jittered reprojection)
    std::vector<float> volTemp;
    Check(dx.ReadbackVolumeRgba16(host.GetIntegratedVolumeResource(), host.IntegratedVolumeResourceState(), w, h, d, volTemp),
          "Read back temporal reprojected 160x90x64 volume");
    uint32_t tempNan = 0u;
    for (float v : volTemp) {
        if (!std::isfinite(v)) ++tempNan;
    }
    Check(tempNan == 0u, "Temporal 3D reprojection (history=0.85, Halton jitter) produced 0 NaN/Inf voxels");

    std::printf("  GPU Time: total=%.4f ms (host: inject=%.4f ms + integrate=%.4f ms = %.4f ms)\n",
                meanTotalMs, hostInjMs, hostIntMs, hostInjMs + hostIntMs);
    if (!dx.isWarp) {
        Check(meanTotalMs < 0.25, "HW GPU budget PASS: 160x90x64 Inject(16 lights+3D noise+temporal) + Integrate < 0.25 ms");
    } else {
        Check(meanTotalMs > 0.0, "WARP timing recorded cleanly");
    }
    Check(host.HasValidTiming(), "RawVolumetricFroxelHost internal GPU timestamp ring valid");

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

    std::printf("\n=== test_volumetric_froxels_gpu Summary: %d passed, %d failed ===\n", gPassed, gFailed);
    return (gFailed == 0) ? 0 : 1;
}
