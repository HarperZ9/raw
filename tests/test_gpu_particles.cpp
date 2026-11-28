// test_gpu_particles.cpp: Standalone DX12 GPU Verification Suite for Wave 58 16,384-Particle 3D Physics Engine.
// Original clean-room implementation, MIT License.
//
// Runs on BOTH Hardware GPU (RTX 4090) and WARP (--warp supported) with D3D12 Debug Layer ENABLED:
//   1. Spawn & lifespan conservation: 16,384 active with respawn ON; 0 active after expiry with respawn OFF.
//   2. Upward ember buoyancy (meanY rises, meanVy > 0) vs downward snow gravity (meanY falls, meanVy < 0).
//   3. Wind drift direction: strong +X wind shifts meanX & meanVx strictly positive.
//   4. Ground bounce restitution: falling rain impacts groundY = 0.0 -> bouncedCount > 10000, minY >= -1e-3, upward rebound.
//   5. Screen-space additive splat buffer: non-zero splat pixels > 200, splattedCount > 10000, 0 NaN/Inf across all 16,384 particles.
//   6. Real RTX 4090 GPU cost < 0.08 ms for 16,384 live particles, d3d12DebugErrors == 0.

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

#include "RawGpuParticlesHost.h"

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

bool ReadbackBuffer(ID3D12Device* dev, ID3D12CommandQueue* q, ID3D12CommandAllocator* alloc,
                    ID3D12GraphicsCommandList* cmd, ID3D12Fence* fence, uint64_t& fv, HANDLE ev,
                    ID3D12Resource* srcBuf, uint64_t byteSize, std::vector<uint8_t>& outBytes)
{
    D3D12_HEAP_PROPERTIES hpRb{D3D12_HEAP_TYPE_READBACK};
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width            = byteSize;
    rd.Height           = 1u;
    rd.DepthOrArraySize = 1u;
    rd.MipLevels        = 1u;
    rd.SampleDesc.Count = 1u;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> rbBuf;
    if (FAILED(dev->CreateCommittedResource(&hpRb, D3D12_HEAP_FLAG_NONE, &rd,
                                            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&rbBuf)))) {
        return false;
    }

    alloc->Reset();
    cmd->Reset(alloc, nullptr);
    D3D12_RESOURCE_BARRIER b{};
    b.Type       = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {srcBuf, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE};
    cmd->ResourceBarrier(1u, &b);
    cmd->CopyBufferRegion(rbBuf.Get(), 0ull, srcBuf, 0ull, byteSize);
    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    cmd->ResourceBarrier(1u, &b);
    cmd->Close();

    ID3D12CommandList* lists[1] = {cmd};
    q->ExecuteCommandLists(1u, lists);
    if (!WaitForQueue(q, fence, fv, ev)) return false;

    void* ptr = nullptr;
    D3D12_RANGE rr{0u, static_cast<SIZE_T>(byteSize)};
    if (FAILED(rbBuf->Map(0u, &rr, &ptr)) || !ptr) return false;
    outBytes.resize(static_cast<size_t>(byteSize));
    std::memcpy(outBytes.data(), ptr, static_cast<size_t>(byteSize));
    D3D12_RANGE wr{0u, 0u};
    rbBuf->Unmap(0u, &wr);
    return true;
}

void RunAdapterSuite(IDXGIFactory6* factory, bool useWarp)
{
    const char* tag = useWarp ? "WARP" : "HW-GPU";
    std::printf("\n--- Running GPU 16,384-Particle 3D Physics Suite [%s] ---\n", tag);

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

    constexpr uint32_t kCount = 16384u;
    constexpr uint32_t kW     = 480u;
    constexpr uint32_t kH     = 270u;

    RawGpuParticlesHost host;
    GpuParticlesDesc desc{};
    desc.particleCount = kCount;
    desc.splatWidth    = kW;
    desc.splatHeight   = kH;
    std::string initErr;
    const bool initOk = host.Init(dev.Get(), desc, &initErr);
    if (!initOk) std::printf("  Init error: %s\n", initErr.c_str());
    Check(initOk && host.Ready(), useWarp ? "WARP: RawGpuParticlesHost::Init" : "HW: RawGpuParticlesHost::Init");
    if (!initOk) {
        CloseHandle(ev);
        return;
    }
    host.SetTimestampFrequency(tsFreq);

    const float eye[3]    = {0.0f, 2.5f, -9.0f};
    const float target[3] = {0.0f, 2.0f,  0.0f};
    const float up[3]     = {0.0f, 1.0f,  0.0f};
    float viewProj[16]{};
    BuildParticlesViewProjLH(eye, target, up, 1.04719755f, static_cast<float>(kW) / static_cast<float>(kH),
                             0.1f, 100.0f, viewProj);

    auto RunSteps = [&](const GpuParticlesParams& pBase, uint32_t steps, bool resetOnFirst,
                        float& outMeanTotalMs, float& outMinTotalMs, uint32_t& outSamples) -> bool {
        outMeanTotalMs = -1.0f; outMinTotalMs = 1e9f; outSamples = 0u;
        float sumMs = 0.0f;
        GpuParticlesParams p = pBase;
        for (uint32_t s = 0u; s < steps; ++s) {
            p.resetAll   = (resetOnFirst && s == 0u);
            p.frameIndex = s;
            p.timeSec    = static_cast<float>(s) * p.dtSec;
            alloc->Reset();
            cmd->Reset(alloc.Get(), nullptr);
            if (!host.Execute(cmd.Get(), p)) return false;
            cmd->Close();
            ID3D12CommandList* lists[1] = {cmd.Get()};
            queue->ExecuteCommandLists(1u, lists);
            if (!WaitForQueue(queue.Get(), fence.Get(), fv, ev)) return false;
            if (s >= RawGpuParticlesHost::kRingCount && host.HasValidTiming()) {
                const float ms = host.GetLastTotalMs();
                sumMs += ms;
                outMinTotalMs = std::min(outMinTotalMs, ms);
                ++outSamples;
            }
        }
        if (outSamples > 0u) outMeanTotalMs = sumMs / static_cast<float>(outSamples);
        return true;
    };

    auto ReadStats = [&](GpuParticlesStats& st) -> bool {
        std::vector<uint8_t> raw;
        if (!ReadbackBuffer(dev.Get(), queue.Get(), alloc.Get(), cmd.Get(), fence.Get(), fv, ev,
                            host.GetStatsBuffer(), 16ull, raw)) return false;
        std::memcpy(&st, raw.data(), sizeof(st));
        return true;
    };

    auto ReadParticles = [&](std::vector<GpuParticleCpuView>& outP, uint32_t& nanInfCount) -> bool {
        std::vector<uint8_t> raw;
        if (!ReadbackBuffer(dev.Get(), queue.Get(), alloc.Get(), cmd.Get(), fence.Get(), fv, ev,
                            host.GetParticleBuffer(), static_cast<uint64_t>(kCount) * sizeof(GpuParticleCpuView), raw)) {
            return false;
        }
        outP.resize(kCount);
        std::memcpy(outP.data(), raw.data(), raw.size());
        nanInfCount = 0u;
        for (const auto& p : outP) {
            if (!std::isfinite(p.pos[0]) || !std::isfinite(p.pos[1]) || !std::isfinite(p.pos[2]) ||
                !std::isfinite(p.vel[0]) || !std::isfinite(p.vel[1]) || !std::isfinite(p.vel[2]) ||
                !std::isfinite(p.age) || !std::isfinite(p.maxLife) ||
                !std::isfinite(p.color[0]) || !std::isfinite(p.color[1]) || !std::isfinite(p.color[2])) {
                ++nanInfCount;
            }
        }
        return true;
    };

    uint32_t totalNanInf = 0u;

    // ------------------------------------------------------------------------
    // Test 1 & 2A & 5: Rising Fire Embers (buoyancy + splat + perf benchmark)
    // ------------------------------------------------------------------------
    GpuParticlesParams emberP{};
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
    emberP.mode = ParticleMode::Embers;
    emberP.enableRespawn = true;
    emberP.enableGroundBounce = true;

    float emberMeanMs = -1.0f, emberMinMs = 1e9f;
    uint32_t emberSamples = 0u;
    const uint32_t emberSteps = 16u;
    const bool emberRunOk = RunSteps(emberP, emberSteps, true, emberMeanMs, emberMinMs, emberSamples);
    GpuParticlesStats emberStats{};
    std::vector<GpuParticleCpuView> emberParticles;
    uint32_t emberNan = 0u;
    ReadStats(emberStats);
    ReadParticles(emberParticles, emberNan);
    totalNanInf += emberNan;

    double emberMeanY = 0.0, emberMeanVy = 0.0;
    for (const auto& p : emberParticles) {
        emberMeanY  += p.pos[1];
        emberMeanVy += p.vel[1];
    }
    emberMeanY  /= static_cast<double>(kCount);
    emberMeanVy /= static_cast<double>(kCount);

    // Readback additive splat buffer for embers
    std::vector<uint8_t> splatRaw;
    ReadbackBuffer(dev.Get(), queue.Get(), alloc.Get(), cmd.Get(), fence.Get(), fv, ev,
                   host.GetSplatBuffer(), host.SplatBufferBytes(), splatRaw);
    const auto* splatU32 = reinterpret_cast<const uint32_t*>(splatRaw.data());
    uint32_t nonZeroSplatPx = 0u;
    uint32_t maxSplatR = 0u;
    for (uint32_t i = 0u; i < kW * kH; ++i) {
        const uint32_t r = splatU32[i * 4u + 0u];
        const uint32_t g = splatU32[i * 4u + 1u];
        const uint32_t b = splatU32[i * 4u + 2u];
        const uint32_t a = splatU32[i * 4u + 3u];
        if (a > 0u && (r > 0u || g > 0u || b > 0u)) {
            ++nonZeroSplatPx;
            maxSplatR = std::max(maxSplatR, r);
        }
    }

    // ------------------------------------------------------------------------
    // Test 1B: Lifespan Expiry Conservation (enableRespawn = false)
    // ------------------------------------------------------------------------
    GpuParticlesParams expireP = emberP;
    expireP.enableRespawn = false;
    expireP.dtSec = 0.12f; // 35 steps * 0.12s = 4.2s > maxLife (3.0s)
    float dummyMean = 0.0f, dummyMin = 0.0f;
    uint32_t dummySamples = 0u;
    RunSteps(expireP, 35u, true, dummyMean, dummyMin, dummySamples);
    GpuParticlesStats expireStats{};
    ReadStats(expireStats);

    // ------------------------------------------------------------------------
    // Test 2B: Falling Snow Gravity (downward)
    // ------------------------------------------------------------------------
    GpuParticlesParams snowP = emberP;
    snowP.mode = ParticleMode::Snow;
    snowP.emitterPos[1] = 8.0f;
    snowP.curlStrength = 0.4f;
    snowP.dtSec = 1.0f / 20.0f;
    RunSteps(snowP, 16u, true, dummyMean, dummyMin, dummySamples);
    std::vector<GpuParticleCpuView> snowParticles;
    uint32_t snowNan = 0u;
    ReadParticles(snowParticles, snowNan);
    totalNanInf += snowNan;

    double snowMeanY = 0.0, snowMeanVy = 0.0;
    for (const auto& p : snowParticles) {
        snowMeanY  += p.pos[1];
        snowMeanVy += p.vel[1];
    }
    snowMeanY  /= static_cast<double>(kCount);
    snowMeanVy /= static_cast<double>(kCount);

    // ------------------------------------------------------------------------
    // Test 3: Strong +X Wind Drift Direction
    // ------------------------------------------------------------------------
    GpuParticlesParams windP = emberP;
    windP.windVel[0] = 14.0f; windP.windVel[1] = 0.0f; windP.windVel[2] = 0.0f;
    windP.curlStrength = 0.25f;
    windP.dragCoeff = 1.8f;
    windP.dtSec = 1.0f / 20.0f;
    RunSteps(windP, 16u, true, dummyMean, dummyMin, dummySamples);
    std::vector<GpuParticleCpuView> windParticles;
    uint32_t windNan = 0u;
    ReadParticles(windParticles, windNan);
    totalNanInf += windNan;

    double windMeanX = 0.0, windMeanVx = 0.0;
    for (const auto& p : windParticles) {
        windMeanX  += p.pos[0];
        windMeanVx += p.vel[0];
    }
    windMeanX  /= static_cast<double>(kCount);
    windMeanVx /= static_cast<double>(kCount);

    // ------------------------------------------------------------------------
    // Test 4: Falling Rain Ground Elastic Bounce Restitution (groundY = 0.0)
    // ------------------------------------------------------------------------
    GpuParticlesParams bounceP = emberP;
    bounceP.mode = ParticleMode::Rain;
    bounceP.emitterPos[0] = 0.0f; bounceP.emitterPos[1] = 1.1f; bounceP.emitterPos[2] = 0.0f;
    bounceP.emitterRadius = 0.35f;
    bounceP.windVel[0] = 0.0f; bounceP.curlStrength = 0.0f;
    bounceP.groundY = 0.0f;
    bounceP.restitution = 0.78f;
    bounceP.dtSec = 1.0f / 40.0f;
    bounceP.enableRespawn = false;
    bounceP.enableGroundBounce = true;
    RunSteps(bounceP, 18u, true, dummyMean, dummyMin, dummySamples);
    GpuParticlesStats bounceStats{};
    std::vector<GpuParticleCpuView> bounceParticles;
    uint32_t bounceNan = 0u;
    ReadStats(bounceStats);
    ReadParticles(bounceParticles, bounceNan);
    totalNanInf += bounceNan;

    float minBounceY = 1e9f;
    uint32_t positiveRebound = 0u;
    for (const auto& p : bounceParticles) {
        minBounceY = std::min(minBounceY, p.pos[1]);
        if (p.bounceCount > 0u && p.vel[1] > 0.25f) ++positiveRebound;
    }

    const uint32_t dbgErrs = CountDebugErrors(dev.Get());

    std::printf("  [%s] 16384 Particles: active=%u expire=%u | ember(Y=%.2f vy=%+.2f) snow(Y=%.2f vy=%+.2f) | "
                "wind(X=%+.2f vx=%+.2f) | bounce(cnt=%u minY=%.4f rebound=%u) | splat(cnt=%u px=%u maxR=%.2f) | "
                "nan=%u totalMs=%.4f (min=%.4f) dbgErrs=%u\n",
                tag, emberStats.activeCount, expireStats.activeCount,
                emberMeanY, emberMeanVy, snowMeanY, snowMeanVy,
                windMeanX, windMeanVx,
                bounceStats.bouncedCount, static_cast<double>(minBounceY), positiveRebound,
                emberStats.splattedCount, nonZeroSplatPx, static_cast<double>(maxSplatR) / 1024.0,
                totalNanInf, static_cast<double>(emberMeanMs), static_cast<double>(emberMinMs), dbgErrs);

    Check(emberRunOk && emberStats.activeCount == kCount && expireStats.activeCount == 0u,
          useWarp ? "WARP: Spawn/lifespan conservation (16384 active w/ respawn, 0 after expiry)"
                  : "HW: Spawn/lifespan conservation (16384 active w/ respawn, 0 after expiry)");
    Check(emberMeanY > 1.8 && emberMeanVy > 1.0 && snowMeanY < 7.2 && snowMeanVy < -0.5,
          useWarp ? "WARP: Upward ember buoyancy vs downward snow gravity verified"
                  : "HW: Upward ember buoyancy vs downward snow gravity verified");
    Check(windMeanX > 2.5 && windMeanVx > 5.0,
          useWarp ? "WARP: +X wind advection shifts meanX & meanVx strictly positive"
                  : "HW: +X wind advection shifts meanX & meanVx strictly positive");
    Check(bounceStats.bouncedCount > 10000u && minBounceY >= -1.0e-3f && positiveRebound > 1000u,
          useWarp ? "WARP: Ground elastic bounce restitution (cnt > 10k, no floor tunnel, upward rebound)"
                  : "HW: Ground elastic bounce restitution (cnt > 10k, no floor tunnel, upward rebound)");
    Check(emberStats.splattedCount > 10000u && nonZeroSplatPx > 200u && maxSplatR > 0u,
          useWarp ? "WARP: Screen-space additive splat buffer populated (> 200 non-zero px)"
                  : "HW: Screen-space additive splat buffer populated (> 200 non-zero px)");
    Check(totalNanInf == 0u,
          useWarp ? "WARP: 0 NaN/Inf across all 16,384 particles"
                  : "HW: 0 NaN/Inf across all 16,384 particles");
    if (!useWarp) {
        Check(emberSamples > 0u && emberMeanMs >= 0.0f && emberMeanMs < 0.08f,
              "HW: Real RTX 4090 GPU cost < 0.08 ms for 16,384 live particles");
    } else {
        Check(emberSamples > 0u && emberMeanMs >= 0.0f,
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

    std::printf("=== Wave 58 GPU 16,384-Particle 3D Physics & Additive Splat Suite ===\n");

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
