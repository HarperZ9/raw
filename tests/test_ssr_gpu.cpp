// test_ssr_gpu.cpp: Standalone DX12 GPU Verification Suite for Wave 54 GGX Stochastic Screen-Space Reflections.
// Original clean-room implementation, MIT License.
//
// Runs on BOTH Hardware GPU (RTX 4090) and WARP (--warp supported) with D3D12 Debug Layer ENABLED:
//   Synthetic 960x540 ray-traced 3D scene: reflective flat wet floor + bright crimson-gold emissive pillar + dark sky.
//   1. Mirror floor pixels reflecting the crimson-gold pillar hit accurately (hitConfidence > 0.80, R > 0.70, R > 3.0 * B).
//   2. Sky pixels (depth >= 0.99998) and floor rays reflecting open sky return hitConfidence == 0.0.
//   3. Increasing roughness spreads reflection confidence smoothly with 0 NaN/Inf across all 518,400 pixels.
//   4. Real RTX 4090 GPU cost < 0.15 ms @ 960x540, d3d12DebugErrors == 0.

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

#include "RawSsrHost.h"

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
    const uint32_t sign = (static_cast<uint32_t>(h & 0x8000u)) << 16;
    const uint32_t exp  = (h >> 10) & 0x1Fu;
    const uint32_t mant = h & 0x03FFu;
    uint32_t fBits = 0u;
    if (exp == 0u) {
        if (mant == 0u) {
            fBits = sign;
        } else {
            float v = std::ldexp(static_cast<float>(mant), -24);
            return (sign != 0u) ? -v : v;
        }
    } else if (exp == 31u) {
        fBits = sign | 0x7F800000u | (mant << 13);
    } else {
        fBits = sign | ((exp + 112u) << 23) | (mant << 13);
    }
    float f = 0.0f;
    std::memcpy(&f, &fBits, sizeof(f));
    return f;
}

bool UploadTex2D(ID3D12Device* dev, ID3D12CommandQueue* q, ID3D12CommandAllocator* alloc,
                 ID3D12GraphicsCommandList* cmd, ID3D12Fence* fence, uint64_t& fv, HANDLE ev,
                 uint32_t w, uint32_t h, DXGI_FORMAT fmt, uint32_t bytesPerPx,
                 const void* data, ComPtr<ID3D12Resource>& outTex)
{
    D3D12_HEAP_PROPERTIES hpDef{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC td{};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = w; td.Height = h; td.DepthOrArraySize = 1; td.MipLevels = 1;
    td.Format = fmt; td.SampleDesc.Count = 1;
    if (FAILED(dev->CreateCommittedResource(&hpDef, D3D12_HEAP_FLAG_NONE, &td,
                                            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&outTex)))) {
        return false;
    }
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT numRows = 0; UINT64 rowB = 0, totB = 0;
    dev->GetCopyableFootprints(&td, 0, 1, 0, &fp, &numRows, &rowB, &totB);

    D3D12_HEAP_PROPERTIES hpUp{D3D12_HEAP_TYPE_UPLOAD};
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = totB; bd.Height = 1;
    bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> upBuf;
    if (FAILED(dev->CreateCommittedResource(&hpUp, D3D12_HEAP_FLAG_NONE, &bd,
                                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upBuf)))) {
        return false;
    }
    void* p = nullptr;
    D3D12_RANGE rr{0, 0};
    upBuf->Map(0, &rr, &p);
    auto* dst = static_cast<uint8_t*>(p) + fp.Offset;
    const auto* src = static_cast<const uint8_t*>(data);
    for (uint32_t y = 0; y < h; ++y) {
        std::memcpy(dst + static_cast<size_t>(y) * fp.Footprint.RowPitch,
                    src + static_cast<size_t>(y) * w * bytesPerPx,
                    static_cast<size_t>(w) * bytesPerPx);
    }
    upBuf->Unmap(0, nullptr);

    alloc->Reset();
    cmd->Reset(alloc, nullptr);
    D3D12_TEXTURE_COPY_LOCATION dLoc{outTex.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
    dLoc.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION sLoc{upBuf.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}};
    sLoc.PlacedFootprint = fp;
    cmd->CopyTextureRegion(&dLoc, 0, 0, 0, &sLoc, nullptr);

    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {outTex.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                    D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
    cmd->ResourceBarrier(1, &b);
    cmd->Close();
    ID3D12CommandList* lists[1] = {cmd};
    q->ExecuteCommandLists(1, lists);
    return WaitForQueue(q, fence, fv, ev);
}

void RunAdapterSuite(IDXGIFactory6* factory, bool useWarp)
{
    const char* tag = useWarp ? "WARP" : "HW-GPU";
    std::printf("\n--- Running GPU GGX Stochastic SSR Suite [%s] ---\n", tag);

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

    D3D12_COMMAND_QUEUE_DESC qd{D3D12_COMMAND_LIST_TYPE_DIRECT};
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
    uint64_t fv = 0u;
    uint64_t tsFreq = 0u;
    queue->GetTimestampFrequency(&tsFreq);

    constexpr uint32_t kW = 960u;
    constexpr uint32_t kH = 540u;

    RawSsrHost host;
    SsrDesc ssrDesc{};
    ssrDesc.initialWidth  = kW;
    ssrDesc.initialHeight = kH;
    std::string initErr;
    const bool initOk = host.Init(dev.Get(), ssrDesc, &initErr);
    if (!initOk) std::printf("  Init error: %s\n", initErr.c_str());
    Check(initOk && host.Ready(), useWarp ? "WARP: RawSsrHost::Init" : "HW: RawSsrHost::Init");
    if (!initOk) {
        CloseHandle(ev);
        return;
    }
    host.SetTimestampFrequency(tsFreq);

    // ------------------------------------------------------------------------
    // Ray-traced synthetic 960x540 3D scene (LH view space: camera at origin looking +Z):
    //   - Wet flat reflective floor at y = -1.8m (extending to z = 120m)
    //   - Tall bright crimson-gold emissive pillar: x in [-1.8, +1.8], y in [-1.8, +5.2], z in [11.0, 14.0]
    //     Color = (2.10, 0.52, 0.14) -> R > 0.70 and R > 3.0 * B
    //   - Dark sky above horizon (depth = 1.0f)
    // ------------------------------------------------------------------------
    const float fovY   = 1.04719755f; // 60 deg
    const float aspect = static_cast<float>(kW) / static_cast<float>(kH);
    const float nearZ  = 0.25f;
    const float farZ   = 150.0f;

    SsrParams params{};
    BuildSsrPerspectiveLH(fovY, aspect, nearZ, farZ, params.proj, params.invProj);
    params.maxRayDist        = 45.0f;
    params.thicknessM        = 2.2f;
    params.nearZ             = nearZ;
    params.startBias         = 0.03f;
    params.fallbackRoughness = 0.01f; // Pure mirror first
    params.maxRoughness      = 0.72f;
    params.edgeFadeWidth     = 0.06f;
    params.maxSteps          = 40u;

    const float tanHalfY = std::tan(fovY * 0.5f);
    const float tanHalfX = tanHalfY * aspect;
    const float q        = farZ / (farZ - nearZ);

    std::vector<float>    sceneDepth(static_cast<size_t>(kW) * kH, 1.0f);
    std::vector<uint16_t> sceneColor(static_cast<size_t>(kW) * kH * 4u, 0u);
    std::vector<uint8_t>  isMirrorTarget(static_cast<size_t>(kW) * kH, 0u);
    std::vector<uint8_t>  isOpenSkyFloor(static_cast<size_t>(kW) * kH, 0u);

    auto WriteColor = [&](uint32_t x, uint32_t y, float r, float g, float b) {
        const size_t off = (static_cast<size_t>(y) * kW + x) * 4u;
        sceneColor[off + 0] = FloatToHalf(r);
        sceneColor[off + 1] = FloatToHalf(g);
        sceneColor[off + 2] = FloatToHalf(b);
        sceneColor[off + 3] = FloatToHalf(1.0f);
    };

    for (uint32_t y = 0u; y < kH; ++y) {
        const float ndcY = 1.0f - (static_cast<float>(y) + 0.5f) / kH * 2.0f;
        const float dirY = ndcY * tanHalfY;
        for (uint32_t x = 0u; x < kW; ++x) {
            const float ndcX = (static_cast<float>(x) + 0.5f) / kW * 2.0f - 1.0f;
            const float dirX = ndcX * tanHalfX;
            const float dirZ = 1.0f;

            float bestZ = 1e9f;
            float cr = 0.01f, cg = 0.02f, cb = 0.04f;
            bool hitFloor = false;

            // 1) Floor plane y = -1.8
            if (dirY < -1.0e-5f) {
                const float t = -1.8f / dirY;
                if (t > nearZ && t < 120.0f) {
                    bestZ = t;
                    cr = 0.04f; cg = 0.05f; cb = 0.07f;
                    hitFloor = true;
                }
            }

            // 2) Ray-AABB intersection against crimson-gold pillar x[-1.8,1.8], y[-1.8,5.2], z[11.0,14.0]
            auto slab = [](float d, float bmin, float bmax, float& tMin, float& tMax) -> bool {
                if (std::fabs(d) < 1.0e-7f) return (0.0f >= bmin && 0.0f <= bmax);
                float t0 = bmin / d;
                float t1 = bmax / d;
                if (t0 > t1) std::swap(t0, t1);
                tMin = std::max(tMin, t0);
                tMax = std::min(tMax, t1);
                return tMin <= tMax;
            };
            float tMin = nearZ, tMax = farZ;
            if (slab(dirX, -1.8f, 1.8f, tMin, tMax) &&
                slab(dirY, -1.8f, 5.2f, tMin, tMax) &&
                slab(dirZ, 11.0f, 14.0f, tMin, tMax) &&
                tMin < bestZ) {
                bestZ = tMin;
                cr = 2.10f; cg = 0.52f; cb = 0.14f;
                hitFloor = false;
            }

            if (bestZ < 1e8f) {
                const float ndcZ = (bestZ * q - nearZ * q) / bestZ;
                sceneDepth[static_cast<size_t>(y) * kW + x] = std::clamp(ndcZ, 0.0f, 0.99995f);

                if (hitFloor) {
                    const float hitX = dirX * bestZ;
                    // Analytical check: mirror reflection of (dirX, dirY, 1) across floor normal (0,1,0)
                    // has direction (dirX, -dirY, 1); intersect with pillar front face z = 11.0!
                    if (bestZ > 4.2f && bestZ < 10.2f) {
                        const float dt = 11.0f - bestZ;
                        const float rx = hitX + dirX * dt;
                        const float ry = -1.8f + (-dirY) * dt;
                        if (std::fabs(rx) < 1.2f && ry > -1.0f && ry < 4.2f) {
                            isMirrorTarget[static_cast<size_t>(y) * kW + x] = 1u;
                        } else if (std::fabs(hitX) > 4.2f && std::fabs(rx) > 5.0f) {
                            isOpenSkyFloor[static_cast<size_t>(y) * kW + x] = 1u;
                        }
                    }
                }
            }
            WriteColor(x, y, cr, cg, cb);
        }
    }

    ComPtr<ID3D12Resource> colorTex, depthTex, rbBuf;
    if (!UploadTex2D(dev.Get(), queue.Get(), alloc.Get(), cmd.Get(), fence.Get(), fv, ev,
                     kW, kH, DXGI_FORMAT_R16G16B16A16_FLOAT, 8u, sceneColor.data(), colorTex) ||
        !UploadTex2D(dev.Get(), queue.Get(), alloc.Get(), cmd.Get(), fence.Get(), fv, ev,
                     kW, kH, DXGI_FORMAT_R32_FLOAT, 4u, sceneDepth.data(), depthTex)) {
        Check(false, useWarp ? "WARP: Upload synthetic color/depth" : "HW: Upload synthetic color/depth");
        CloseHandle(ev);
        return;
    }

    D3D12_RESOURCE_DESC outDesc = host.GetOutputResource()->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT outFp{};
    UINT outRows = 0; UINT64 outRowB = 0, outTotB = 0;
    dev->GetCopyableFootprints(&outDesc, 0, 1, 0, &outFp, &outRows, &outRowB, &outTotB);
    D3D12_HEAP_PROPERTIES hpRb{D3D12_HEAP_TYPE_READBACK};
    D3D12_RESOURCE_DESC rbDesc{};
    rbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rbDesc.Width = outTotB; rbDesc.Height = 1;
    rbDesc.DepthOrArraySize = 1; rbDesc.MipLevels = 1; rbDesc.SampleDesc.Count = 1;
    rbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(dev->CreateCommittedResource(&hpRb, D3D12_HEAP_FLAG_NONE, &rbDesc,
                                            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&rbBuf)))) {
        Check(false, useWarp ? "WARP: Create SSR readback buffer" : "HW: Create SSR readback buffer");
        CloseHandle(ev);
        return;
    }

    auto RunAndReadback = [&](const SsrParams& pRun, uint32_t iters, std::vector<float>& outRgba,
                              float& outMeanMs, float& outMinMs, uint32_t& outSamples, uint32_t& outNanInf) -> bool {
        outMeanMs = -1.0f; outMinMs = 1e9f; outSamples = 0u; outNanInf = 0u;
        float sumMs = 0.0f;
        SsrParams pLocal = pRun;
        for (uint32_t it = 0u; it < iters; ++it) {
            pLocal.frameJitter = static_cast<float>(it & 63u);
            alloc->Reset();
            cmd->Reset(alloc.Get(), nullptr);
            if (!host.Execute(cmd.Get(), colorTex.Get(), depthTex.Get(), kW, kH, pLocal)) {
                return false;
            }
            if (it + 1u == iters) {
                D3D12_RESOURCE_BARRIER toCopy{};
                toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                toCopy.Transition = {host.GetOutputResource(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                     host.OutputResourceState(), D3D12_RESOURCE_STATE_COPY_SOURCE};
                cmd->ResourceBarrier(1, &toCopy);
                D3D12_TEXTURE_COPY_LOCATION dLoc{rbBuf.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}};
                dLoc.PlacedFootprint = outFp;
                D3D12_TEXTURE_COPY_LOCATION sLoc{host.GetOutputResource(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
                sLoc.SubresourceIndex = 0;
                cmd->CopyTextureRegion(&dLoc, 0, 0, 0, &sLoc, nullptr);
                D3D12_RESOURCE_BARRIER back = toCopy;
                std::swap(back.Transition.StateBefore, back.Transition.StateAfter);
                cmd->ResourceBarrier(1, &back);
            }
            cmd->Close();
            ID3D12CommandList* lists[1] = {cmd.Get()};
            queue->ExecuteCommandLists(1, lists);
            if (!WaitForQueue(queue.Get(), fence.Get(), fv, ev)) return false;
            if (host.HasValidTiming()) {
                const float ms = host.GetLastSsrMs();
                sumMs += ms;
                outMinMs = std::min(outMinMs, ms);
                ++outSamples;
            }
        }
        if (outSamples > 0u) outMeanMs = sumMs / static_cast<float>(outSamples);

        outRgba.resize(static_cast<size_t>(kW) * kH * 4u);
        void* ptr = nullptr;
        D3D12_RANGE rr{0, static_cast<SIZE_T>(outTotB)};
        if (FAILED(rbBuf->Map(0, &rr, &ptr))) return false;
        const auto* base = static_cast<const uint8_t*>(ptr) + outFp.Offset;
        for (uint32_t y = 0u; y < kH; ++y) {
            const auto* row = reinterpret_cast<const uint16_t*>(base + static_cast<size_t>(y) * outFp.Footprint.RowPitch);
            for (uint32_t x = 0u; x < kW * 4u; ++x) {
                if ((row[x] & 0x7C00u) == 0x7C00u) ++outNanInf;
                outRgba[static_cast<size_t>(y) * kW * 4u + x] = HalfToFloat(row[x]);
            }
        }
        D3D12_RANGE wr{0, 0};
        rbBuf->Unmap(0, &wr);
        return true;
    };

    // Pass A: Mirror SSR (roughness = 0.01) + GPU timestamp benchmark
    std::vector<float> mirrorRgba;
    float meanMs = -1.0f, minMs = 1e9f;
    uint32_t timeSamples = 0u, mirrorNanInf = 0u;
    const uint32_t iters = useWarp ? 6u : 14u;
    const bool mirrorOk = RunAndReadback(params, iters, mirrorRgba, meanMs, minMs, timeSamples, mirrorNanInf);

    double sumConf = 0.0, sumR = 0.0, sumB = 0.0;
    uint32_t targetCount = 0u;
    float maxSkyConf = 0.0f;
    float maxOpenFloorConf = 0.0f;
    uint32_t mirrorSpanXMin = kW, mirrorSpanXMax = 0u;

    for (uint32_t y = 0u; y < kH; ++y) {
        for (uint32_t x = 0u; x < kW; ++x) {
            const size_t idx = static_cast<size_t>(y) * kW + x;
            const float r = mirrorRgba[idx * 4u + 0u];
            const float b = mirrorRgba[idx * 4u + 2u];
            const float a = mirrorRgba[idx * 4u + 3u];
            if (sceneDepth[idx] >= 0.99998f) {
                maxSkyConf = std::max(maxSkyConf, a);
            }
            if (isMirrorTarget[idx]) {
                sumConf += a; sumR += r; sumB += b;
                ++targetCount;
            }
            if (isOpenSkyFloor[idx]) {
                maxOpenFloorConf = std::max(maxOpenFloorConf, a);
            }
            if (y == 380u && a > 0.25f && r > 0.6f) {
                mirrorSpanXMin = std::min(mirrorSpanXMin, x);
                mirrorSpanXMax = std::max(mirrorSpanXMax, x);
            }
        }
    }
    const float meanConf = (targetCount > 0u) ? static_cast<float>(sumConf / targetCount) : 0.0f;
    const float meanR    = (targetCount > 0u) ? static_cast<float>(sumR / targetCount) : 0.0f;
    const float meanB    = (targetCount > 0u) ? static_cast<float>(sumB / targetCount) : 0.0f;
    const uint32_t mirrorSpanW = (mirrorSpanXMax >= mirrorSpanXMin) ? (mirrorSpanXMax - mirrorSpanXMin) : 0u;

    // Pass B: GGX Stochastic Roughness (roughness = 0.28) spread & NaN check
    SsrParams roughParams = params;
    roughParams.fallbackRoughness = 0.28f;
    std::vector<float> roughRgba;
    float roughMs = 0.0f, roughMinMs = 0.0f;
    uint32_t roughSamples = 0u, roughNanInf = 0u;
    const bool roughOk = RunAndReadback(roughParams, 2u, roughRgba, roughMs, roughMinMs, roughSamples, roughNanInf);

    uint32_t roughSpanXMin = kW, roughSpanXMax = 0u;
    uint32_t roughHitPx = 0u;
    for (uint32_t y = 340u; y < 440u; ++y) {
        for (uint32_t x = 0u; x < kW; ++x) {
            const size_t idx = static_cast<size_t>(y) * kW + x;
            const float r = roughRgba[idx * 4u + 0u];
            const float a = roughRgba[idx * 4u + 3u];
            if (a > 0.15f && r > 0.45f) {
                ++roughHitPx;
                if (y == 380u) {
                    roughSpanXMin = std::min(roughSpanXMin, x);
                    roughSpanXMax = std::max(roughSpanXMax, x);
                }
            }
        }
    }
    const uint32_t roughSpanW = (roughSpanXMax >= roughSpanXMin) ? (roughSpanXMax - roughSpanXMin) : 0u;

    const uint32_t dbgErrs = CountDebugErrors(dev.Get());
    std::printf("  [%s] 960x540 SSR: targetPx=%u conf=%.3f R=%.3f B=%.3f skyConf=%.3f openFloorConf=%.3f span(mir=%u rough=%u hits=%u) nanInf=%u ssrMs=%.4f (min=%.4f) dbgErrs=%u\n",
                tag, targetCount, static_cast<double>(meanConf), static_cast<double>(meanR), static_cast<double>(meanB),
                static_cast<double>(maxSkyConf), static_cast<double>(maxOpenFloorConf),
                mirrorSpanW, roughSpanW, roughHitPx, mirrorNanInf + roughNanInf,
                static_cast<double>(meanMs), static_cast<double>(minMs), dbgErrs);

    Check(mirrorOk && targetCount > 500u && meanConf > 0.80f && meanR > 0.70f && meanR > 3.0f * meanB,
          useWarp ? "WARP: Mirror floor reflects crimson-gold pillar (conf > 0.80, R > 0.70, R > 3*B)"
                  : "HW: Mirror floor reflects crimson-gold pillar (conf > 0.80, R > 0.70, R > 3*B)");
    Check(maxSkyConf == 0.0f && maxOpenFloorConf == 0.0f,
          useWarp ? "WARP: Sky pixels & open-sky floor rays return hitConfidence == 0.0"
                  : "HW: Sky pixels & open-sky floor rays return hitConfidence == 0.0");
    Check(roughOk && roughHitPx > 500u && roughSpanW > mirrorSpanW && (mirrorNanInf + roughNanInf) == 0u,
          useWarp ? "WARP: GGX roughness spreads reflection lobe with 0 NaN/Inf"
                  : "HW: GGX roughness spreads reflection lobe with 0 NaN/Inf");
    if (!useWarp) {
        Check(timeSamples > 0u && meanMs >= 0.0f && meanMs < 0.15f,
              "HW: Real RTX 4090 GPU SSR cost < 0.15 ms @ 960x540");
    } else {
        Check(timeSamples > 0u && meanMs >= 0.0f,
              "WARP: Valid GPU timestamp harvested");
    }
    Check(dbgErrs == 0u,
          useWarp ? "WARP: 0 D3D12 Debug Layer errors"
                  : "HW: 0 D3D12 Debug Layer errors");

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

    std::printf("=== Wave 54 GPU GGX Stochastic Screen-Space Reflections (SSR) Suite ===\n");

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
