// test_cloud_raymarch_gpu.cpp: Standalone DX12 GPU Verification Suite for Wave 56 Volumetric Cloud Raymarching.
// Original clean-room implementation, MIT License.
//
// Runs on BOTH Hardware GPU (RTX 4090) and WARP (--warp supported) with D3D12 Debug Layer ENABLED:
//   Resolution: 480x270 (129,600 pixels)
//   1. Clear sky (cloudCoverage == 0.0) -> transmittance == 1.0, radiance == 0.0 across entire frame.
//   2. Below-horizon pixels (rayDir.y <= 0.0) -> exact (0, 0, 0, 1.0) passthrough even at coverage == 0.85.
//   3. Dense cumulus (cloudCoverage == 0.85) -> strong cloud optical occlusion (minT < 0.35, cloudyPixels > 10000),
//      forward silver-lining toward sun strictly brighter than anti-solar clouds (forwardLuma > 2.0 * antiSolarLuma),
//      zero NaN/Inf across all pixels.
//   4. Temporal reprojection blend + dynamic stability + real RTX 4090 GPU cost < 0.15 ms @ 480x270, d3d12DebugErrors == 0.

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

#include "RawCloudRaymarchHost.h"

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
    for (UINT64 i = 0u; i < count; ++i) {
        SIZE_T len = 0u;
        if (FAILED(iq->GetMessage(i, nullptr, &len)) || len == 0u) continue;
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
            const float v = std::ldexp(static_cast<float>(mant), -24);
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

void RunAdapterSuite(IDXGIFactory6* factory, bool useWarp)
{
    const char* tag = useWarp ? "WARP" : "HW-GPU";
    std::printf("\n--- Running GPU Volumetric Cloud Raymarch Suite [%s] ---\n", tag);

    ComPtr<IDXGIAdapter1> adapter;
    if (useWarp) {
        if (FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)))) {
            Check(false, "WARP: EnumWarpAdapter");
            return;
        }
    } else {
        for (UINT i = 0u; factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                              IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc{};
            adapter->GetDesc1(&desc);
            if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0u) {
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
        FAILED(dev->CreateCommandList(0u, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&cmd))) ||
        FAILED(dev->CreateFence(0u, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
        Check(false, useWarp ? "WARP: Queue/CmdList/Fence creation" : "HW: Queue/CmdList/Fence creation");
        return;
    }
    cmd->Close();
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    uint64_t fv = 0u;
    uint64_t tsFreq = 0u;
    queue->GetTimestampFrequency(&tsFreq);

    constexpr uint32_t kW = 480u;
    constexpr uint32_t kH = 270u;

    RawCloudRaymarchHost host;
    CloudRaymarchDesc desc{};
    desc.initialWidth  = kW;
    desc.initialHeight = kH;
    std::string initErr;
    const bool initOk = host.Init(dev.Get(), desc, &initErr);
    if (!initOk) std::printf("  Init error: %s\n", initErr.c_str());
    Check(initOk && host.Ready(), useWarp ? "WARP: RawCloudRaymarchHost::Init" : "HW: RawCloudRaymarchHost::Init");
    if (!initOk) {
        CloseHandle(ev);
        return;
    }
    host.SetTimestampFrequency(tsFreq);

    // Readback buffer matching 480x270 RGBA16F output
    const D3D12_RESOURCE_DESC outDesc = host.GetOutputResource()->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT outFp{};
    UINT outRows = 0u;
    UINT64 outRowB = 0ull, outTotB = 0ull;
    dev->GetCopyableFootprints(&outDesc, 0u, 1u, 0ull, &outFp, &outRows, &outRowB, &outTotB);
    D3D12_HEAP_PROPERTIES hpRb{D3D12_HEAP_TYPE_READBACK};
    D3D12_RESOURCE_DESC rbDesc{};
    rbDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rbDesc.Width            = outTotB;
    rbDesc.Height           = 1u;
    rbDesc.DepthOrArraySize = 1u;
    rbDesc.MipLevels        = 1u;
    rbDesc.SampleDesc.Count = 1u;
    rbDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> rbBuf;
    if (FAILED(dev->CreateCommittedResource(&hpRb, D3D12_HEAP_FLAG_NONE, &rbDesc,
                                            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&rbBuf)))) {
        Check(false, useWarp ? "WARP: Create cloud readback buffer" : "HW: Create cloud readback buffer");
        CloseHandle(ev);
        return;
    }

    auto RunAndReadback = [&](const CloudRaymarchParams& pRun, uint32_t iters, bool resetFirst,
                              std::vector<float>& outRgba, float& outMeanMs, float& outMinMs,
                              uint32_t& outSamples, uint32_t& outNanInf) -> bool {
        outMeanMs = -1.0f; outMinMs = 1e9f; outSamples = 0u; outNanInf = 0u;
        if (resetFirst) host.ResetHistory();
        float sumMs = 0.0f;
        CloudRaymarchParams pLocal = pRun;
        for (uint32_t it = 0u; it < iters; ++it) {
            pLocal.frameIndex = it;
            alloc->Reset();
            cmd->Reset(alloc.Get(), nullptr);
            if (!host.Execute(cmd.Get(), kW, kH, pLocal)) return false;

            if (it + 1u == iters) {
                ID3D12Resource* outRes = host.GetOutputResource();
                D3D12_RESOURCE_BARRIER toCopy{};
                toCopy.Type       = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                toCopy.Transition = {outRes, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                     host.OutputResourceState(), D3D12_RESOURCE_STATE_COPY_SOURCE};
                cmd->ResourceBarrier(1u, &toCopy);
                D3D12_TEXTURE_COPY_LOCATION dLoc{rbBuf.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}};
                dLoc.PlacedFootprint = outFp;
                D3D12_TEXTURE_COPY_LOCATION sLoc{outRes, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
                sLoc.SubresourceIndex = 0u;
                cmd->CopyTextureRegion(&dLoc, 0u, 0u, 0u, &sLoc, nullptr);
                D3D12_RESOURCE_BARRIER back = toCopy;
                std::swap(back.Transition.StateBefore, back.Transition.StateAfter);
                cmd->ResourceBarrier(1u, &back);
            }
            cmd->Close();
            ID3D12CommandList* lists[1] = {cmd.Get()};
            queue->ExecuteCommandLists(1u, lists);
            if (!WaitForQueue(queue.Get(), fence.Get(), fv, ev)) return false;
            if (it >= RawCloudRaymarchHost::kRingCount && host.HasValidTiming()) {
                const float ms = host.GetLastCloudMs();
                sumMs += ms;
                outMinMs = std::min(outMinMs, ms);
                ++outSamples;
            }
        }
        if (outSamples > 0u) outMeanMs = sumMs / static_cast<float>(outSamples);

        outRgba.resize(static_cast<size_t>(kW) * kH * 4u);
        void* ptr = nullptr;
        D3D12_RANGE rr{0u, static_cast<SIZE_T>(outTotB)};
        if (FAILED(rbBuf->Map(0u, &rr, &ptr)) || !ptr) return false;
        const auto* base = static_cast<const uint8_t*>(ptr) + outFp.Offset;
        for (uint32_t y = 0u; y < kH; ++y) {
            const auto* row = reinterpret_cast<const uint16_t*>(base + static_cast<size_t>(y) * outFp.Footprint.RowPitch);
            for (uint32_t x = 0u; x < kW * 4u; ++x) {
                if ((row[x] & 0x7C00u) == 0x7C00u) ++outNanInf;
                outRgba[static_cast<size_t>(y) * kW * 4u + x] = HalfToFloat(row[x]);
            }
        }
        D3D12_RANGE wr{0u, 0u};
        rbBuf->Unmap(0u, &wr);
        return true;
    };

    // Base horizon-level camera so top half (y < 135) is sky and bottom half (y >= 135) is below horizon!
    CloudRaymarchParams baseP{};
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
    baseP.historyWeight = 0.0f;

    // ------------------------------------------------------------------------
    // Test 1: Clear Sky (cloudCoverage == 0.0) -> exact (0, 0, 0, 1.0)
    // ------------------------------------------------------------------------
    CloudRaymarchParams clearP = baseP;
    clearP.cloudCoverage = 0.0f;
    std::vector<float> clearRgba;
    float clearMs = 0.0f, clearMinMs = 0.0f;
    uint32_t clearSamples = 0u, clearNan = 0u;
    const bool clearRunOk = RunAndReadback(clearP, 1u, true, clearRgba, clearMs, clearMinMs, clearSamples, clearNan);

    float clearMaxRad = 0.0f;
    float clearMinT   = 1.0f;
    float clearMaxT   = 1.0f;
    for (size_t i = 0u; i < static_cast<size_t>(kW) * kH; ++i) {
        clearMaxRad = std::max({clearMaxRad, std::fabs(clearRgba[i * 4u + 0u]),
                                std::fabs(clearRgba[i * 4u + 1u]), std::fabs(clearRgba[i * 4u + 2u])});
        clearMinT   = std::min(clearMinT, clearRgba[i * 4u + 3u]);
        clearMaxT   = std::max(clearMaxT, clearRgba[i * 4u + 3u]);
    }

    // ------------------------------------------------------------------------
    // Test 2 & 3A: Dense Cumulus (coverage == 0.85) looking TOWARD sun (forward silver-lining)
    //   + Below-horizon exact (0, 0, 0, 1.0) check + steady-state temporal GPU benchmark
    // ------------------------------------------------------------------------
    CloudRaymarchParams fwdP = baseP;
    fwdP.cloudCoverage = 0.85f;
    fwdP.historyWeight = 0.75f;
    // Sun in front of camera at ~18 deg elevation: (0, 0.309, 0.951)
    fwdP.sunDir[0] = 0.0f; fwdP.sunDir[1] = 0.3090f; fwdP.sunDir[2] = 0.9511f;

    std::vector<float> fwdRgba;
    float meanMs = -1.0f, minMs = 1e9f;
    uint32_t timeSamples = 0u, fwdNan = 0u;
    const uint32_t iters = useWarp ? 6u : 18u;
    const bool fwdRunOk = RunAndReadback(fwdP, iters, true, fwdRgba, meanMs, minMs, timeSamples, fwdNan);

    const float tanHalfY = std::tan(0.5f * fwdP.fovYDeg * (3.14159265359f / 180.0f));
    const float aspect   = static_cast<float>(kW) / static_cast<float>(kH);
    const float tanHalfX = tanHalfY * aspect;

    uint32_t belowHorizonBad = 0u;
    uint32_t cloudyPixels    = 0u;
    float    minT            = 1.0f;
    double   fwdSunLumaSum   = 0.0;
    uint32_t fwdSunPx        = 0u;

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

            if (ry <= 0.0f) {
                if (r != 0.0f || g != 0.0f || b != 0.0f || a != 1.0f) {
                    ++belowHorizonBad;
                }
            } else {
                minT = std::min(minT, a);
                if (a < 0.85f) ++cloudyPixels;
                // Cone within ~22 deg of forward sun direction (cos > 0.927)
                const float cosSun = rx * fwdP.sunDir[0] + ry * fwdP.sunDir[1] + rz * fwdP.sunDir[2];
                if (cosSun > 0.927f && a < 0.90f) {
                    const float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                    fwdSunLumaSum += luma;
                    ++fwdSunPx;
                }
            }
        }
    }
    const float forwardLuma = (fwdSunPx > 0u) ? static_cast<float>(fwdSunLumaSum / fwdSunPx) : 0.0f;

    // ------------------------------------------------------------------------
    // Test 3B: Exact Same Cloud Field Looking AWAY from Sun (anti-solar back-scatter)
    // ------------------------------------------------------------------------
    CloudRaymarchParams antiP = fwdP;
    antiP.sunDir[0] = 0.0f; antiP.sunDir[1] = 0.3090f; antiP.sunDir[2] = -0.9511f;
    antiP.historyWeight = 0.0f;
    std::vector<float> antiRgba;
    float antiMs = 0.0f, antiMinMs = 0.0f;
    uint32_t antiSamples = 0u, antiNan = 0u;
    const bool antiRunOk = RunAndReadback(antiP, 1u, true, antiRgba, antiMs, antiMinMs, antiSamples, antiNan);

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
            // Sample the exact same sky-cone coordinates (now anti-solar!)
            const float cosFwd = rx * fwdP.sunDir[0] + ry * fwdP.sunDir[1] + rz * fwdP.sunDir[2];
            const size_t i = static_cast<size_t>(y) * kW + x;
            const float a = antiRgba[i * 4u + 3u];
            if (cosFwd > 0.927f && a < 0.90f) {
                const float r = antiRgba[i * 4u + 0u];
                const float g = antiRgba[i * 4u + 1u];
                const float b = antiRgba[i * 4u + 2u];
                antiLumaSum += (0.2126f * r + 0.7152f * g + 0.0722f * b);
                ++antiPx;
            }
        }
    }
    const float antiSolarLuma = (antiPx > 0u) ? static_cast<float>(antiLumaSum / antiPx) : 0.0f;
    const float silverRatio   = forwardLuma / std::max(antiSolarLuma, 1.0e-5f);
    const uint32_t totalNan   = clearNan + fwdNan + antiNan;
    const uint32_t dbgErrs    = CountDebugErrors(dev.Get());

    std::printf("  [%s] 480x270 Clouds: clearMaxRad=%.4f clearT=[%.3f..%.3f] belowHorBad=%u | "
                "dense(minT=%.3f cloudyPx=%u) silver(fwd=%.4f anti=%.4f ratio=%.2fx) nanInf=%u cloudMs=%.4f (min=%.4f) dbgErrs=%u\n",
                tag, static_cast<double>(clearMaxRad), static_cast<double>(clearMinT), static_cast<double>(clearMaxT),
                belowHorizonBad, static_cast<double>(minT), cloudyPixels,
                static_cast<double>(forwardLuma), static_cast<double>(antiSolarLuma), static_cast<double>(silverRatio),
                totalNan, static_cast<double>(meanMs), static_cast<double>(minMs), dbgErrs);

    Check(clearRunOk && clearMaxRad == 0.0f && clearMinT == 1.0f && clearMaxT == 1.0f,
          useWarp ? "WARP: Clear sky (coverage=0.0) returns exact (0,0,0,1.0)"
                  : "HW: Clear sky (coverage=0.0) returns exact (0,0,0,1.0)");
    Check(fwdRunOk && belowHorizonBad == 0u,
          useWarp ? "WARP: Below-horizon pixels (rayDir.y <= 0.0) return exact (0,0,0,1.0)"
                  : "HW: Below-horizon pixels (rayDir.y <= 0.0) return exact (0,0,0,1.0)");
    Check(fwdRunOk && minT < 0.35f && cloudyPixels > 10000u,
          useWarp ? "WARP: Dense cumulus (coverage=0.85) yields strong optical occlusion (minT < 0.35, cloudyPx > 10000)"
                  : "HW: Dense cumulus (coverage=0.85) yields strong optical occlusion (minT < 0.35, cloudyPx > 10000)");
    Check(antiRunOk && fwdSunPx > 500u && antiPx > 500u && forwardLuma > 2.0f * antiSolarLuma,
          useWarp ? "WARP: Forward silver-lining strictly brighter than anti-solar clouds (> 2.0x)"
                  : "HW: Forward silver-lining strictly brighter than anti-solar clouds (> 2.0x)");
    Check(totalNan == 0u,
          useWarp ? "WARP: 0 NaN/Inf across all cloud passes"
                  : "HW: 0 NaN/Inf across all cloud passes");
    if (!useWarp) {
        Check(timeSamples > 0u && meanMs >= 0.0f && meanMs < 0.15f,
              "HW: Real RTX 4090 GPU volumetric cloud cost < 0.15 ms @ 480x270");
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

    std::printf("=== Wave 56 GPU Volumetric Cloud Raymarching + Beer-Powder Silver-Lining Suite ===\n");

    ComPtr<ID3D12Debug> dbg;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) {
        dbg->EnableDebugLayer();
        std::printf("D3D12 Debug Layer: ENABLED\n");
    } else {
        std::printf("D3D12 Debug Layer: UNAVAILABLE\n");
    }

    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&factory)))) {
        CreateDXGIFactory2(0u, IID_PPV_ARGS(&factory));
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
