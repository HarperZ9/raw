// tests/test_gtao_gpu.cpp: Verification & GPU Benchmark Suite for RAW's Jimenez (SIGGRAPH 2016)
// Ground-Truth Ambient Occlusion (GTAO) + Unoccluded Bent-Normal & Bilateral Denoise Solver.
//
// Verifies on BOTH Hardware DX12 GPU (e.g. RTX 4090) AND Software WARP (--warp) with D3D12 Debug Layer ON:
//   1. Open flat floor -> vis > 0.90 and bentNormal points straight up (+Y) along surface normal.
//   2. 90-degree wall-floor crevice -> creviceVis < 0.65 AND bentNormal tilts strictly AWAY from occluding +X wall (bent.x < -0.15).
//   3. Recessed pit in floor -> pitVis < openVis - 0.25 (strict deep occlusion).
//   4. Sky pixels (depth >= 0.99998) -> exact vis == 1.0, bentNormal == (0, 0, -1), 0 NaN/Inf across all 960x540 pixels.
//   5. Dynamic resolution auto-reallocation (640x360 -> 960x540) with 0 D3D12 errors.
//   6. Real RTX 4090 GPU cost (mainMs + denoiseMs < 0.15 ms at 960x540!) & 0 D3D12 Debug Layer errors.
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

#include "RawGtaoHost.h"

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
                        data.data() + static_cast<size_t>(y) * w,
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
        b.Transition.pResource   = tex;
        b.Transition.StateBefore = curState;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1u, &b);

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource        = tex;
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
        outRgba.resize(static_cast<size_t>(outW) * outH * 4u);
        for (uint32_t y = 0u; y < outH; ++y) {
            const auto* row = reinterpret_cast<const uint16_t*>(base + static_cast<size_t>(y) * fp.Footprint.RowPitch);
            for (uint32_t x = 0u; x < outW; ++x) {
                const size_t i = (static_cast<size_t>(y) * outW + x) * 4u;
                outRgba[i + 0u] = HalfToFloat(row[x * 4 + 0]);
                outRgba[i + 1u] = HalfToFloat(row[x * 4 + 1]);
                outRgba[i + 2u] = HalfToFloat(row[x * 4 + 2]);
                outRgba[i + 3u] = HalfToFloat(row[x * 4 + 3]);
            }
        }
        D3D12_RANGE wr{0u, 0u};
        rb->Unmap(0u, &wr);
        return true;
    }
};

// Ray-traced exact 3D view-space synthetic depth buffer (Standard-Z 0=near, 1=far):
//   - Sky strip along upper screen (y < 90 & rays above horizon missing the wall)
//   - Open flat horizontal floor at Y = -2.0 (Z in [1.0, 40.0])
//   - 90-degree solid vertical wall occupying X >= +1.0, Y in [-2.0, +1.2], Z in [3.0, 14.0]
//   - Recessed circular pit centered at (X = -1.2, Z = 6.0), radius = 0.50, floor at Y = -3.6
std::vector<float> BuildSynthetic3DDepth(uint32_t w, uint32_t h, float fovYDeg, float nearZ, float farZ)
{
    std::vector<float> depth(static_cast<size_t>(w) * h, 1.0f);
    const float aspect = static_cast<float>(w) / static_cast<float>(h);
    const float tanY   = std::tan(0.5f * fovYDeg * 3.14159265359f / 180.0f);
    const float tanX   = tanY * aspect;
    const float q      = farZ / (farZ - nearZ);

    auto linearToStdZ = [&](float vz) -> float {
        return std::clamp(q * (1.0f - nearZ / vz), 0.0f, 0.99997f);
    };

    constexpr float kFloorY  = -2.0f;
    constexpr float kPitX    = -1.2f;
    constexpr float kPitZ    =  4.7f;
    constexpr float kPitR    =  0.90f;
    constexpr float kPitBotY = -2.55f;

    for (uint32_t y = 0u; y < h; ++y) {
        for (uint32_t x = 0u; x < w; ++x) {
            const float uvX  = (static_cast<float>(x) + 0.5f) / static_cast<float>(w);
            const float uvY  = (static_cast<float>(y) + 0.5f) / static_cast<float>(h);
            const float ndcX = uvX * 2.0f - 1.0f;
            const float ndcY = 1.0f - uvY * 2.0f;
            const float rx   = ndcX * tanX;
            const float ry   = ndcY * tanY;
            const float rz   = 1.0f; // ray P(t) = (rx*t, ry*t, t) so t == viewZ

            float bestT = 1.0e9f;

            // 1. Floor (Y = -2.0) outside pit opening + Pit cylinder interior + Pit bottom (Y = -3.6)
            if (ry < -1.0e-5f) {
                const float tFloor = kFloorY / ry;
                if (tFloor > nearZ && tFloor < 45.0f) {
                    const float fx = rx * tFloor;
                    const float fz = rz * tFloor;
                    const float dx = fx - kPitX;
                    const float dz = fz - kPitZ;
                    const bool inPitOpening = (dx * dx + dz * dz) < (kPitR * kPitR);
                    if (!inPitOpening) {
                        bestT = std::min(bestT, tFloor);
                    } else {
                        // Inside pit opening: intersect pit bottom Y = -3.6 AND pit vertical cylinder wall
                        const float tBot = kPitBotY / ry;
                        if (tBot > tFloor) {
                            const float bx = rx * tBot - kPitX;
                            const float bz = rz * tBot - kPitZ;
                            if (bx * bx + bz * bz <= kPitR * kPitR) {
                                bestT = std::min(bestT, tBot);
                            }
                        }
                        // Cylinder (rx*t - kPitX)^2 + (t - kPitZ)^2 = kPitR^2
                        const float a = rx * rx + 1.0f;
                        const float b = -2.0f * (rx * kPitX + kPitZ);
                        const float c = kPitX * kPitX + kPitZ * kPitZ - kPitR * kPitR;
                        const float disc = b * b - 4.0f * a * c;
                        if (disc >= 0.0f) {
                            const float sd = std::sqrt(disc);
                            const float tCyl = (-b + sd) / (2.0f * a); // far interior wall of cylinder
                            if (tCyl > tFloor) {
                                const float cy = ry * tCyl;
                                if (cy <= kFloorY && cy >= kPitBotY) {
                                    bestT = std::min(bestT, tCyl);
                                }
                            }
                        }
                    }
                }
            }

            // 2. Solid 90-degree right-side occluding wall box: X in [1.0, 5.0], Y in [-2.0, 1.2], Z in [3.0, 14.0]
            {
                auto slab = [](float dir, float bmin, float bmax, float& tMin, float& tMax) -> bool {
                    if (std::fabs(dir) < 1.0e-7f) return (0.0f >= bmin && 0.0f <= bmax);
                    float t0 = bmin / dir;
                    float t1 = bmax / dir;
                    if (t0 > t1) std::swap(t0, t1);
                    tMin = std::max(tMin, t0);
                    tMax = std::min(tMax, t1);
                    return tMin <= tMax;
                };
                float t0 = nearZ, t1 = 1.0e6f;
                if (slab(rx, 1.0f, 5.0f, t0, t1) &&
                    slab(ry, kFloorY, 1.2f, t0, t1) &&
                    slab(rz, 3.0f, 14.0f, t0, t1) && t0 > nearZ) {
                    bestT = std::min(bestT, t0);
                }
            }

            // 3. Raised stone well-rim around left/right/back of the recessed pit (Y in [-2.0, -1.35],
            //    leaving front lip open so camera has unobstructed line of sight to the 1.20m-deep pit bottom)
            {
                auto slab = [](float dir, float bmin, float bmax, float& tMin, float& tMax) -> bool {
                    if (std::fabs(dir) < 1.0e-7f) return (0.0f >= bmin && 0.0f <= bmax);
                    float t0 = bmin / dir;
                    float t1 = bmax / dir;
                    if (t0 > t1) std::swap(t0, t1);
                    tMin = std::max(tMin, t0);
                    tMax = std::min(tMax, t1);
                    return tMin <= tMax;
                };
                // Back rim wall: X in [-2.2, -0.2], Y in [-2.55, -1.35], Z in [5.55, 5.95]
                float t0 = nearZ, t1 = 1.0e6f;
                if (slab(rx, -2.2f, -0.2f, t0, t1) && slab(ry, kPitBotY, -1.35f, t0, t1) && slab(rz, 5.55f, 5.95f, t0, t1) && t0 > nearZ)
                    bestT = std::min(bestT, t0);
                // Left rim wall: X in [-2.2, -1.75], Y in [-2.55, -1.35], Z in [4.2, 5.95]
                t0 = nearZ; t1 = 1.0e6f;
                if (slab(rx, -2.2f, -1.75f, t0, t1) && slab(ry, kPitBotY, -1.35f, t0, t1) && slab(rz, 4.2f, 5.95f, t0, t1) && t0 > nearZ)
                    bestT = std::min(bestT, t0);
                // Right rim wall: X in [-0.65, -0.2], Y in [-2.55, -1.35], Z in [4.2, 5.95]
                t0 = nearZ; t1 = 1.0e6f;
                if (slab(rx, -0.65f, -0.2f, t0, t1) && slab(ry, kPitBotY, -1.35f, t0, t1) && slab(rz, 4.2f, 5.95f, t0, t1) && t0 > nearZ)
                    bestT = std::min(bestT, t0);
            }

            // Keep top 64 rows guaranteed sky
            if (y < 64u) bestT = 1.0e9f;

            depth[static_cast<size_t>(y) * w + x] = (bestT < 1.0e8f) ? linearToStdZ(bestT) : 1.0f;
        }
    }
    return depth;
}

// Project a 3D view-space point (vx, vy, vz) into exact pixel coordinates (px, py)
void ProjectViewToPixel(float vx, float vy, float vz, uint32_t w, uint32_t h, float fovYDeg, uint32_t& outX, uint32_t& outY)
{
    const float aspect = static_cast<float>(w) / static_cast<float>(h);
    const float tanY   = std::tan(0.5f * fovYDeg * 3.14159265359f / 180.0f);
    const float tanX   = tanY * aspect;
    const float ndcX   = vx / (vz * tanX);
    const float ndcY   = vy / (vz * tanY);
    const float u      = std::clamp(ndcX * 0.5f + 0.5f, 0.0f, 0.9999f);
    const float v      = std::clamp(0.5f - ndcY * 0.5f, 0.0f, 0.9999f);
    outX = static_cast<uint32_t>(u * static_cast<float>(w));
    outY = static_cast<uint32_t>(v * static_cast<float>(h));
}

void RunSuiteOnAdapter(bool useWarp)
{
    TestDx12 dx{};
    if (!dx.Init(useWarp)) {
        Check(false, useWarp ? "WARP D3D12 device init" : "Hardware DX12 device init");
        return;
    }

    std::printf("\n=== Jimenez GTAO + Bent-Normal Suite [%s] (DebugLayer=%s, TsFreq=%llu Hz) ===\n",
                dx.adapterName.c_str(), dx.debugLayer ? "ON" : "OFF",
                static_cast<unsigned long long>(dx.tsFreqHz));

    RawGtaoHost host;
    GtaoDesc desc{};
    desc.initialWidth  = 640u; // start smaller to verify dynamic resolution auto-reallocation to 960x540
    desc.initialHeight = 360u;
    std::string err;
    const bool initOk = host.Init(dx.dev.Get(), desc, &err);
    Check(initOk, "RawGtaoHost::Init succeeded");
    if (!initOk) {
        std::fprintf(stderr, "  Init error: %s\n", err.c_str());
        return;
    }
    host.SetTimestampFrequency(dx.tsFreqHz);

    constexpr uint32_t kW = 960u;
    constexpr uint32_t kH = 540u;
    GtaoParams gp{};
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

    const std::vector<float> depthImg = BuildSynthetic3DDepth(kW, kH, gp.fovYDeg, gp.nearZ, gp.farZ);
    ComPtr<ID3D12Resource> depthTex;
    Check(dx.UploadR32Tex2D(depthTex, kW, kH, depthImg), "Uploaded 960x540 ray-traced 3D synthetic depth scene");

    // Warmup + Measured GPU Timing Loop
    const uint32_t warmupFrames   = dx.isWarp ? 2u : 16u;
    const uint32_t measuredFrames = dx.isWarp ? 4u : 60u;
    double sumTotalMs = 0.0;
    for (uint32_t f = 0u; f < warmupFrames + measuredFrames; ++f) {
        if (!dx.Begin()) {
            Check(false, "Begin frame failed");
            return;
        }
        dx.cmd->EndQuery(dx.tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0u);
        host.Execute(dx.cmd.Get(), depthTex.Get(), kW, kH, gp);
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
    const float  hostMainMs  = host.HasValidTiming() ? host.GetLastMainMs() : 0.0f;
    const float  hostDenMs   = host.HasValidTiming() ? host.GetLastDenoiseMs() : 0.0f;

    Check(host.Width() == kW && host.Height() == kH, "Dynamic resolution auto-reallocation (640x360 -> 960x540) succeeded");

    // Readback filtered GTAO + bent normal output
    uint32_t rw = 0u, rh = 0u;
    std::vector<float> outRgba;
    Check(dx.ReadbackRgba16Tex2D(host.GetFilteredOutputResource(), host.FilteredOutputResourceState(), rw, rh, outRgba),
          "Read back 960x540 RGBA16F GtaoFilteredOut");

    uint32_t nanCount = 0u, skyBadCount = 0u;
    for (uint32_t y = 0u; y < rh; ++y) {
        for (uint32_t x = 0u; x < rw; ++x) {
            const size_t i = (static_cast<size_t>(y) * rw + x) * 4u;
            const float bx = outRgba[i + 0u];
            const float by = outRgba[i + 1u];
            const float bz = outRgba[i + 2u];
            const float vis = outRgba[i + 3u];
            if (!std::isfinite(bx) || !std::isfinite(by) || !std::isfinite(bz) || !std::isfinite(vis)) {
                ++nanCount;
                continue;
            }
            if (depthImg[static_cast<size_t>(y) * rw + x] >= 0.99998f) {
                if (std::fabs(vis - 1.0f) > 1.0e-3f || std::fabs(bz + 1.0f) > 2.0e-3f) {
                    ++skyBadCount;
                }
            }
        }
    }

    auto sampleAvg5x5 = [&](float vx, float vy, float vz, float out4[4]) {
        uint32_t cx = 0u, cy = 0u;
        ProjectViewToPixel(vx, vy, vz, kW, kH, gp.fovYDeg, cx, cy);
        double s0 = 0.0, s1 = 0.0, s2 = 0.0, s3 = 0.0;
        int cnt = 0;
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dxp = -2; dxp <= 2; ++dxp) {
                const int px = std::clamp(static_cast<int>(cx) + dxp, 0, static_cast<int>(kW) - 1);
                const int py = std::clamp(static_cast<int>(cy) + dy, 0, static_cast<int>(kH) - 1);
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
    // Open flat floor far from wall and pit: (-3.2, -2.0, 6.5)
    sampleAvg5x5(-3.2f, -2.0f, 6.5f, openProbe);
    // 90-degree wall-floor crevice immediately left of the +X=1.0 wall: (+0.82, -2.0, 6.0)
    sampleAvg5x5(0.82f, -2.0f, 6.0f, creviceProbe);
    // Recessed pit bottom (on-screen ndcY=-0.858, unobstructed r=0.71<0.90): (-1.2, -2.55, 5.15)
    sampleAvg5x5(-1.2f, -2.55f, 5.15f, pitProbe);

    std::printf("  OpenFlr : vis=%.4f | bent=(%+.3f, %+.3f, %+.3f)\n",
                openProbe[3], openProbe[0], openProbe[1], openProbe[2]);
    std::printf("  Crevice : vis=%.4f | bent=(%+.3f, %+.3f, %+.3f) [wall on +X]\n",
                creviceProbe[3], creviceProbe[0], creviceProbe[1], creviceProbe[2]);
    std::printf("  PitBot  : vis=%.4f | bent=(%+.3f, %+.3f, %+.3f)\n",
                pitProbe[3], pitProbe[0], pitProbe[1], pitProbe[2]);
    std::printf("  Hygiene : NaN=%u | skyBad=%u\n", nanCount, skyBadCount);
    std::printf("  GPU Time: total=%.4f ms (host: main=%.4f ms + denoise=%.4f ms = %.4f ms)\n",
                meanTotalMs, hostMainMs, hostDenMs, hostMainMs + hostDenMs);

    Check(nanCount == 0u, "Zero NaN/Inf across all 960x540 GTAO + bent-normal pixels");
    Check(skyBadCount == 0u, "Sky pixels exact passthrough (vis == 1.0, bentNormal == (0, 0, -1))");
    Check(openProbe[3] > 0.90f && openProbe[1] > 0.92f && std::fabs(openProbe[0]) < 0.08f,
          "Open flat floor: vis > 0.90 and bentNormal points straight up (+Y)");
    Check(creviceProbe[3] < 0.65f && creviceProbe[0] < -0.15f,
          "90-deg wall crevice: vis < 0.65 AND bentNormal tilts strictly AWAY from +X wall (bent.x < -0.15)");
    Check(pitProbe[3] < openProbe[3] - 0.25f,
          "Recessed pit: strictly deeply occluded (pitVis < openVis - 0.25)");

    if (!dx.isWarp) {
        Check(meanTotalMs < 0.15, "HW GPU budget PASS: 960x540 GTAO Main + Bilateral Denoise < 0.15 ms");
    } else {
        Check(meanTotalMs > 0.0, "WARP timing recorded cleanly");
    }
    Check(host.HasValidTiming(), "RawGtaoHost internal GPU timestamp ring valid");

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

    std::printf("\n=== test_gtao_gpu Summary: %d passed, %d failed ===\n", gPassed, gFailed);
    return (gFailed == 0) ? 0 : 1;
}
