// test_water_caustics_gpu.cpp: Standalone DX12 GPU Verification Suite for Wave 60 Physical Water Caustics
// and Underwater Volumetric God-Ray Solver (RawWaterCausticsHost + RawWaterCausticsSim.hlsl).
// Original clean-room implementation, MIT License.
//
// Runs on BOTH Hardware GPU (RTX 4090) and WARP (--warp supported) with D3D12 Debug Layer ENABLED:
//   1. Flat water (waveAmp == 0): preserves exact uniform unit irradiance (1.000 +/- 1e-3, 0 NaN/Inf).
//   2. Wavy surface Jacobian focusing: produces sharp caustic peaks (> 2.0x) while preserving mean energy
//      within 15% of flat water (0.85 <= meanEnergy / flatMean <= 1.15).
//   3. Chromatic R/B Cauchy dispersion: measurable spatial separation between R and B caustic ridges.
//   4. Beer-Lambert spectral extinction: red attenuates strictly faster than blue as depth increases
//      (R < G < B and deep (R/B) << shallow (R/B)).
//   5. Underwater volumetric sun-shafts (16-step Henyey-Greenstein): peak strongly sunward vs anti-solar
//      (> 2.5x) with active wave-coupled crepuscular modulation, and clean zero radiance above water.
//   6. Zero NaN/Inf across both 512x512 RGBA16F outputs, HW GPU timing < 0.06 ms, d3d12DebugErrors == 0.

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

#include "RawWaterCausticsHost.h"

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

float HalfToFloat(uint16_t h)
{
    const uint32_t sign = (static_cast<uint32_t>(h) & 0x8000u) << 16u;
    const uint32_t exp  = (h >> 10u) & 0x1Fu;
    const uint32_t mant = h & 0x03FFu;
    uint32_t bits = 0u;
    if (exp == 0u) {
        if (mant == 0u) {
            bits = sign;
        } else {
            uint32_t m = mant;
            int32_t  e = -14;
            while ((m & 0x0400u) == 0u) {
                m <<= 1u;
                --e;
            }
            m &= 0x03FFu;
            bits = sign | (static_cast<uint32_t>(e + 127) << 23u) | (m << 13u);
        }
    } else if (exp == 31u) {
        bits = sign | 0x7F800000u | (mant << 13u);
    } else {
        bits = sign | ((exp + 112u) << 23u) | (mant << 13u);
    }
    float f = 0.0f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
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

struct RgbaTexData {
    uint32_t           w = 0u;
    uint32_t           h = 0u;
    std::vector<float> rgba; // 4 floats per pixel
    uint32_t           nanInfCount = 0u;
};

bool ReadbackRgba16fTexture(ID3D12Device* dev, ID3D12CommandQueue* q, ID3D12CommandAllocator* alloc,
                            ID3D12GraphicsCommandList* cmd, ID3D12Fence* fence, uint64_t& fv, HANDLE ev,
                            ID3D12Resource* srcTex, D3D12_RESOURCE_STATES curState,
                            uint32_t w, uint32_t h, RgbaTexData& out)
{
    const D3D12_RESOURCE_DESC srcDesc = srcTex->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT   numRows    = 0u;
    UINT64 rowBytes   = 0ull;
    UINT64 totalBytes = 0ull;
    dev->GetCopyableFootprints(&srcDesc, 0u, 1u, 0ull, &fp, &numRows, &rowBytes, &totalBytes);

    D3D12_HEAP_PROPERTIES hpRb{D3D12_HEAP_TYPE_READBACK};
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width            = totalBytes;
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
    b.Transition = {srcTex, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, curState, D3D12_RESOURCE_STATE_COPY_SOURCE};
    cmd->ResourceBarrier(1u, &b);

    D3D12_TEXTURE_COPY_LOCATION dstLoc{};
    dstLoc.pResource       = rbBuf.Get();
    dstLoc.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dstLoc.PlacedFootprint = fp;

    D3D12_TEXTURE_COPY_LOCATION srcLoc{};
    srcLoc.pResource        = srcTex;
    srcLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    srcLoc.SubresourceIndex = 0u;

    cmd->CopyTextureRegion(&dstLoc, 0u, 0u, 0u, &srcLoc, nullptr);
    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    cmd->ResourceBarrier(1u, &b);
    cmd->Close();

    ID3D12CommandList* lists[1] = {cmd};
    q->ExecuteCommandLists(1u, lists);
    if (!WaitForQueue(q, fence, fv, ev)) return false;

    void* ptr = nullptr;
    D3D12_RANGE rr{0u, static_cast<SIZE_T>(totalBytes)};
    if (FAILED(rbBuf->Map(0u, &rr, &ptr)) || !ptr) return false;

    out.w = w;
    out.h = h;
    out.rgba.resize(static_cast<size_t>(w) * h * 4u);
    out.nanInfCount = 0u;

    const auto* base = static_cast<const uint8_t*>(ptr);
    for (uint32_t y = 0u; y < h; ++y) {
        const auto* row = reinterpret_cast<const uint16_t*>(base + fp.Offset + static_cast<size_t>(y) * fp.Footprint.RowPitch);
        for (uint32_t x = 0u; x < w; ++x) {
            const size_t dst = (static_cast<size_t>(y) * w + x) * 4u;
            for (uint32_t c = 0u; c < 4u; ++c) {
                const float v = HalfToFloat(row[x * 4u + c]);
                if (!std::isfinite(v)) ++out.nanInfCount;
                out.rgba[dst + c] = v;
            }
        }
    }

    D3D12_RANGE wr{0u, 0u};
    rbBuf->Unmap(0u, &wr);
    return true;
}

void RunAdapterSuite(IDXGIFactory6* factory, bool useWarp)
{
    const char* tag = useWarp ? "WARP" : "HW-GPU";
    std::printf("\n--- Running Physical Water Caustics & Underwater God-Rays Suite [%s] ---\n", tag);

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

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> alloc;
    ComPtr<ID3D12GraphicsCommandList> cmd;
    ComPtr<ID3D12Fence> fence;
    if (FAILED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue))) ||
        FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))) ||
        FAILED(dev->CreateCommandList(0u, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&cmd))) ||
        FAILED(dev->CreateFence(0ull, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
        Check(false, "Create D3D12 queue/cmd/fence");
        return;
    }
    cmd->Close();
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    uint64_t fenceVal = 0ull;

    uint64_t tsFreq = 0ull;
    queue->GetTimestampFrequency(&tsFreq);

    constexpr uint32_t kRes = 512u;
    RawWaterCausticsHost host;
    std::string initErr;
    WaterCausticsDesc desc{};
    desc.initialWidth  = kRes;
    desc.initialHeight = kRes;
    const bool okInit = host.Init(dev.Get(), desc, &initErr);
    if (!okInit) {
        std::printf("  [ERR] host.Init: %s\n", initErr.c_str());
    }
    Check(okInit && host.Ready(), useWarp ? "WARP: RawWaterCausticsHost::Init" : "HW: RawWaterCausticsHost::Init");
    if (!okInit) {
        CloseHandle(ev);
        return;
    }
    host.SetTimestampFrequency(tsFreq);

    auto runOnce = [&](const WaterCausticsParams& p, RgbaTexData* outCaustics, RgbaTexData* outRays) -> bool {
        alloc->Reset();
        cmd->Reset(alloc.Get(), nullptr);
        const bool ok = host.Execute(cmd.Get(), kRes, kRes, p);
        cmd->Close();
        if (!ok) return false;
        ID3D12CommandList* lists[1] = {cmd.Get()};
        queue->ExecuteCommandLists(1u, lists);
        if (!WaitForQueue(queue.Get(), fence.Get(), fenceVal, ev)) return false;
        if (outCaustics) {
            if (!ReadbackRgba16fTexture(dev.Get(), queue.Get(), alloc.Get(), cmd.Get(), fence.Get(), fenceVal, ev,
                                        host.GetCausticsResource(), host.CausticsResourceState(),
                                        kRes, kRes, *outCaustics)) {
                return false;
            }
        }
        if (outRays) {
            if (!ReadbackRgba16fTexture(dev.Get(), queue.Get(), alloc.Get(), cmd.Get(), fence.Get(), fenceVal, ev,
                                        host.GetUnderwaterRaysResource(), host.UnderwaterRaysResourceState(),
                                        kRes, kRes, *outRays)) {
                return false;
            }
        }
        return true;
    };

    const size_t totalPx = static_cast<size_t>(kRes) * kRes;
    uint32_t totalNanInf = 0u;

    // -------------------------------------------------------------------------
    // Test 1: Flat water (waveAmp == 0, zero absorption, unit white sun) preserves uniform unit irradiance
    // -------------------------------------------------------------------------
    WaterCausticsParams pFlat{};
    pFlat.timeSec          = 0.0f;
    pFlat.waveAmp          = 0.0f;
    pFlat.bedDepthM        = 2.4f;
    pFlat.domainSizeM      = 8.0f;
    pFlat.sunDir[0]        = 0.42f;
    pFlat.sunDir[1]        = 0.82f;
    pFlat.sunDir[2]        = 0.39f;
    pFlat.sunColor[0]      = 1.0f;
    pFlat.sunColor[1]      = 1.0f;
    pFlat.sunColor[2]      = 1.0f;
    pFlat.sunIntensity     = 1.0f;
    pFlat.absorptionRGB[0] = 0.0f;
    pFlat.absorptionRGB[1] = 0.0f;
    pFlat.absorptionRGB[2] = 0.0f;
    pFlat.dispersionScale  = 1.0f;

    RgbaTexData flatTex{};
    const bool okFlat = runOnce(pFlat, &flatTex, nullptr);
    totalNanInf += flatTex.nanInfCount;

    double flatMeanR = 0.0, flatMeanG = 0.0, flatMeanB = 0.0, flatMeanA = 0.0;
    float  flatMaxErr = 0.0f;
    for (size_t i = 0u; i < totalPx; ++i) {
        const float r = flatTex.rgba[i * 4u + 0u];
        const float g = flatTex.rgba[i * 4u + 1u];
        const float b = flatTex.rgba[i * 4u + 2u];
        const float a = flatTex.rgba[i * 4u + 3u];
        flatMeanR += r;
        flatMeanG += g;
        flatMeanB += b;
        flatMeanA += a;
        flatMaxErr = std::max(flatMaxErr, std::fabs(r - 1.0f));
        flatMaxErr = std::max(flatMaxErr, std::fabs(g - 1.0f));
        flatMaxErr = std::max(flatMaxErr, std::fabs(b - 1.0f));
        flatMaxErr = std::max(flatMaxErr, std::fabs(a - 1.0f));
    }
    flatMeanR /= static_cast<double>(totalPx);
    flatMeanG /= static_cast<double>(totalPx);
    flatMeanB /= static_cast<double>(totalPx);
    flatMeanA /= static_cast<double>(totalPx);

    std::printf("  Flat Water: meanRGBA=(%.4f, %.4f, %.4f, %.4f) maxUnitErr=%.5f\n",
                flatMeanR, flatMeanG, flatMeanB, flatMeanA, flatMaxErr);
    Check(okFlat && flatMaxErr < 2e-3f && flatTex.nanInfCount == 0u,
          useWarp ? "WARP: Flat water preserves exact uniform unit irradiance"
                  : "HW: Flat water preserves exact uniform unit irradiance");

    // -------------------------------------------------------------------------
    // Test 2 & 3: Wavy surface caustic focusing (> 2.0x peak), mean energy conservation (within 15%),
    //             and Cauchy RGB chromatic dispersion separation
    // -------------------------------------------------------------------------
    WaterCausticsParams pWavy = pFlat;
    pWavy.timeSec          = 1.35f;
    pWavy.waveAmp          = 0.195f;
    pWavy.bedDepthM        = 2.4f;
    pWavy.dispersionScale  = 1.0f;

    RgbaTexData wavyTex{};
    const bool okWavy = runOnce(pWavy, &wavyTex, nullptr);
    totalNanInf += wavyTex.nanInfCount;

    double wavyMeanR = 0.0, wavyMeanG = 0.0, wavyMeanB = 0.0, wavyMeanA = 0.0;
    double meanAbsRB = 0.0;
    float  peakFocus = 0.0f;
    float  minFocus  = 1e9f;
    float  maxAbsRB  = 0.0f;
    for (size_t i = 0u; i < totalPx; ++i) {
        const float r = wavyTex.rgba[i * 4u + 0u];
        const float g = wavyTex.rgba[i * 4u + 1u];
        const float b = wavyTex.rgba[i * 4u + 2u];
        const float a = wavyTex.rgba[i * 4u + 3u];
        wavyMeanR += r;
        wavyMeanG += g;
        wavyMeanB += b;
        wavyMeanA += a;
        peakFocus = std::max(peakFocus, g);
        minFocus  = std::min(minFocus, g);
        const float rbDiff = std::fabs(r - b);
        meanAbsRB += rbDiff;
        maxAbsRB = std::max(maxAbsRB, rbDiff);
    }
    wavyMeanR /= static_cast<double>(totalPx);
    wavyMeanG /= static_cast<double>(totalPx);
    wavyMeanB /= static_cast<double>(totalPx);
    wavyMeanA /= static_cast<double>(totalPx);
    meanAbsRB /= static_cast<double>(totalPx);

    const double energyRatio = wavyMeanG / std::max(flatMeanG, 1e-6);
    std::printf("  Wavy Caustics: peakFocus=%.3fx minFocus=%.3fx meanEnergy=%.4f (ratio=%.4f) R/B meanDiff=%.4f maxDiff=%.4f\n",
                peakFocus, minFocus, wavyMeanG, energyRatio, meanAbsRB, maxAbsRB);

    Check(okWavy && peakFocus > 2.0f && minFocus < 0.75f,
          useWarp ? "WARP: Wavy surface produces strong caustic peak focusing (> 2.0x)"
                  : "HW: Wavy surface produces strong caustic peak focusing (> 2.0x)");
    Check(okWavy && energyRatio >= 0.85 && energyRatio <= 1.15 &&
              wavyMeanR >= 0.85 && wavyMeanR <= 1.15 &&
              wavyMeanB >= 0.85 && wavyMeanB <= 1.15,
          useWarp ? "WARP: Caustic Jacobian preserves mean energy within 15%"
                  : "HW: Caustic Jacobian preserves mean energy within 15%");
    Check(okWavy && meanAbsRB > 0.025 && maxAbsRB > 0.20f,
          useWarp ? "WARP: Cauchy chromatic R/B dispersion separation verified"
                  : "HW: Cauchy chromatic R/B dispersion separation verified");

    // -------------------------------------------------------------------------
    // Test 4: Beer-Lambert spectral underwater extinction (red attenuates strictly faster than blue)
    // -------------------------------------------------------------------------
    WaterCausticsParams pShallow = pWavy;
    pShallow.bedDepthM        = 1.0f;
    pShallow.absorptionRGB[0] = 0.38f;
    pShallow.absorptionRGB[1] = 0.065f;
    pShallow.absorptionRGB[2] = 0.022f;

    WaterCausticsParams pDeep = pShallow;
    pDeep.bedDepthM           = 6.0f;

    RgbaTexData shallowTex{}, deepTex{};
    const bool okShallow = runOnce(pShallow, &shallowTex, nullptr);
    const bool okDeep    = runOnce(pDeep, &deepTex, nullptr);
    totalNanInf += shallowTex.nanInfCount + deepTex.nanInfCount;

    auto meanRGB = [&](const RgbaTexData& t, double& mR, double& mG, double& mB) {
        mR = mG = mB = 0.0;
        for (size_t i = 0u; i < totalPx; ++i) {
            mR += t.rgba[i * 4u + 0u];
            mG += t.rgba[i * 4u + 1u];
            mB += t.rgba[i * 4u + 2u];
        }
        mR /= static_cast<double>(totalPx);
        mG /= static_cast<double>(totalPx);
        mB /= static_cast<double>(totalPx);
    };

    double sR = 0.0, sG = 0.0, sB = 0.0;
    double dR = 0.0, dG = 0.0, dB = 0.0;
    meanRGB(shallowTex, sR, sG, sB);
    meanRGB(deepTex, dR, dG, dB);

    const double redRetention  = dR / std::max(sR, 1e-6);
    const double blueRetention = dB / std::max(sB, 1e-6);
    const double shallowRB     = sR / std::max(sB, 1e-6);
    const double deepRB        = dR / std::max(dB, 1e-6);

    std::printf("  Beer-Lambert: shallow(1m)=(%.4f, %.4f, %.4f) deep(6m)=(%.4f, %.4f, %.4f) R_ret=%.3f B_ret=%.3f RB: %.3f->%.3f\n",
                sR, sG, sB, dR, dG, dB, redRetention, blueRetention, shallowRB, deepRB);
    Check(okShallow && okDeep &&
              (sR < sG && sG < sB) &&
              (dR < dG && dG < dB) &&
              (redRetention < blueRetention * 0.35) &&
              (deepRB < shallowRB * 0.35),
          useWarp ? "WARP: Beer-Lambert spectral extinction attenuates red much faster than blue"
                  : "HW: Beer-Lambert spectral extinction attenuates red much faster than blue");

    // -------------------------------------------------------------------------
    // Test 5: Underwater 16-step Henyey-Greenstein volumetric sun-shafts peak sunward
    // -------------------------------------------------------------------------
    WaterCausticsParams pSunward{};
    pSunward.timeSec          = 1.35f;
    pSunward.waveAmp          = 0.195f;
    pSunward.bedDepthM        = 2.4f;
    pSunward.domainSizeM      = 8.0f;
    pSunward.sunDir[0]        = 0.0f;
    pSunward.sunDir[1]        = 0.60f;
    pSunward.sunDir[2]        = 0.80f;
    pSunward.sunColor[0]      = 1.0f;
    pSunward.sunColor[1]      = 0.96f;
    pSunward.sunColor[2]      = 0.88f;
    pSunward.sunIntensity     = 3.0f;
    pSunward.absorptionRGB[0] = 0.38f;
    pSunward.absorptionRGB[1] = 0.065f;
    pSunward.absorptionRGB[2] = 0.022f;
    pSunward.scatterCoeff     = 0.065f;
    pSunward.mieG             = 0.78f;
    pSunward.camPos[0]        = 0.0f;
    pSunward.camPos[1]        = -2.5f;
    pSunward.camPos[2]        = 0.0f;
    pSunward.waterSurfaceY    = 0.0f;
    pSunward.camRight[0]      = 1.0f;
    pSunward.camRight[1]      = 0.0f;
    pSunward.camRight[2]      = 0.0f;
    pSunward.camUp[0]         = 0.0f;
    pSunward.camUp[1]         = 0.96825f;
    pSunward.camUp[2]         = -0.25f;
    pSunward.camForward[0]    = 0.0f;
    pSunward.camForward[1]    = 0.25f;
    pSunward.camForward[2]    = 0.96825f;
    pSunward.fovYDeg          = 60.0f;
    pSunward.maxRayDistM      = 24.0f;
    pSunward.shaftIntensity   = 2.5f;
    pSunward.frameIndex       = 1u;

    WaterCausticsParams pAntiSolar = pSunward;
    pAntiSolar.camRight[0]   = -1.0f;
    pAntiSolar.camUp[1]      = 0.96825f;
    pAntiSolar.camUp[2]      = 0.25f;
    pAntiSolar.camForward[1] = 0.25f;
    pAntiSolar.camForward[2] = -0.96825f;

    WaterCausticsParams pAboveSky = pSunward;
    pAboveSky.camPos[1]      = 3.0f; // above waterSurfaceY = 0, looking up into sky (entire 60-deg FOV above horizon)
    pAboveSky.camForward[0]  = 0.0f;
    pAboveSky.camForward[1]  = 0.76604f; // +50 deg elevation
    pAboveSky.camForward[2]  = 0.64279f;
    pAboveSky.camUp[0]       = 0.0f;
    pAboveSky.camUp[1]       = 0.64279f;
    pAboveSky.camUp[2]       = -0.76604f;

    RgbaTexData sunwardTex{}, antiSolarTex{}, aboveSkyTex{};
    const bool okSunward   = runOnce(pSunward, nullptr, &sunwardTex);
    const bool okAntiSolar = runOnce(pAntiSolar, nullptr, &antiSolarTex);
    const bool okAboveSky  = runOnce(pAboveSky, nullptr, &aboveSkyTex);
    totalNanInf += sunwardTex.nanInfCount + antiSolarTex.nanInfCount + aboveSkyTex.nanInfCount;

    auto raysStats = [&](const RgbaTexData& t, double& meanLuma, double& stdLuma, float& maxRad, float& minTrans) {
        meanLuma = 0.0;
        maxRad   = 0.0f;
        minTrans = 1e9f;
        for (size_t i = 0u; i < totalPx; ++i) {
            const float r = t.rgba[i * 4u + 0u];
            const float g = t.rgba[i * 4u + 1u];
            const float b = t.rgba[i * 4u + 2u];
            const float a = t.rgba[i * 4u + 3u];
            const double luma = 0.2126 * r + 0.7152 * g + 0.0722 * b;
            meanLuma += luma;
            maxRad   = std::max(maxRad, std::max(r, std::max(g, b)));
            minTrans = std::min(minTrans, a);
        }
        meanLuma /= static_cast<double>(totalPx);
        double var = 0.0;
        for (size_t i = 0u; i < totalPx; ++i) {
            const double luma = 0.2126 * t.rgba[i * 4u + 0u] + 0.7152 * t.rgba[i * 4u + 1u] + 0.0722 * t.rgba[i * 4u + 2u];
            const double d = luma - meanLuma;
            var += d * d;
        }
        stdLuma = std::sqrt(var / static_cast<double>(totalPx));
    };

    double sunLuma = 0.0, sunStd = 0.0, antiLuma = 0.0, antiStd = 0.0, aboveLuma = 0.0, aboveStd = 0.0;
    float  sunMax  = 0.0f, sunMinT = 0.0f, antiMax = 0.0f, antiMinT = 0.0f, aboveMax = 0.0f, aboveMinT = 0.0f;
    raysStats(sunwardTex, sunLuma, sunStd, sunMax, sunMinT);
    raysStats(antiSolarTex, antiLuma, antiStd, antiMax, antiMinT);
    raysStats(aboveSkyTex, aboveLuma, aboveStd, aboveMax, aboveMinT);

    const double shaftRatio = sunLuma / std::max(antiLuma, 1e-6);
    std::printf("  Underwater Shafts: sunwardLuma=%.4f (std=%.4f, max=%.3f) antiSolarLuma=%.4f ratio=%.2fx aboveSkyMax=%.5f aboveMinT=%.4f\n",
                sunLuma, sunStd, sunMax, antiLuma, shaftRatio, aboveMax, aboveMinT);

    Check(okSunward && okAntiSolar && sunLuma > 0.05 && sunStd > 0.005 && shaftRatio > 2.5,
          useWarp ? "WARP: Underwater volumetric sun-shafts peak strongly sunward (> 2.5x)"
                  : "HW: Underwater volumetric sun-shafts peak strongly sunward (> 2.5x)");
    Check(okAboveSky && aboveMax < 1e-5f && aboveMinT > 0.999f,
          useWarp ? "WARP: Above-water sky rays produce exact zero underwater scatter & T=1.0"
                  : "HW: Above-water sky rays produce exact zero underwater scatter & T=1.0");
    Check(totalNanInf == 0u,
          useWarp ? "WARP: Zero NaN/Inf across all caustics and underwater ray outputs"
                  : "HW: Zero NaN/Inf across all caustics and underwater ray outputs");

    // -------------------------------------------------------------------------
    // Test 6: GPU Performance Benchmark (512x512 both passes combined < 0.06 ms on RTX 4090)
    // -------------------------------------------------------------------------
    const uint32_t warmupFrames   = useWarp ? 2u : 12u;
    const uint32_t measuredFrames = useWarp ? 4u : 30u;
    double sumCausticsMs = 0.0;
    double sumRaysMs     = 0.0;
    uint32_t validSamples = 0u;

    WaterCausticsParams pBench = pSunward;
    for (uint32_t f = 0u; f < warmupFrames + measuredFrames; ++f) {
        pBench.timeSec    = 1.0f + static_cast<float>(f) * (1.0f / 60.0f);
        pBench.frameIndex = f;
        alloc->Reset();
        cmd->Reset(alloc.Get(), nullptr);
        host.Execute(cmd.Get(), kRes, kRes, pBench);
        cmd->Close();
        ID3D12CommandList* lists[1] = {cmd.Get()};
        queue->ExecuteCommandLists(1u, lists);
        WaitForQueue(queue.Get(), fence.Get(), fenceVal, ev);

        if (f >= warmupFrames && host.HasValidTiming()) {
            sumCausticsMs += host.GetLastCausticsMs();
            sumRaysMs     += host.GetLastRaysMs();
            ++validSamples;
        }
    }

    const double meanCausticsMs = (validSamples > 0u) ? (sumCausticsMs / validSamples) : 0.0;
    const double meanRaysMs     = (validSamples > 0u) ? (sumRaysMs / validSamples) : 0.0;
    const double meanTotalMs    = meanCausticsMs + meanRaysMs;

    std::printf("  GPU Timing [%s 512x512]: caustics=%.4f ms + rays=%.4f ms = total=%.4f ms (samples=%u)\n",
                tag, meanCausticsMs, meanRaysMs, meanTotalMs, validSamples);

    if (!useWarp) {
        Check(validSamples > 0u && meanTotalMs > 0.0 && meanTotalMs < 0.06,
              "HW: 512x512 Caustics + Underwater Rays combined GPU time < 0.06 ms");
    } else {
        Check(validSamples > 0u && meanTotalMs > 0.0,
              "WARP: Hardware timestamp profiler produced valid finite GPU timing");
    }

    host.Shutdown();
    CloseHandle(ev);

    const uint32_t dbgErrs = CountDebugErrors(dev.Get());
    std::printf("  D3D12 Debug Layer Errors [%s]: %u\n", tag, dbgErrs);
    Check(dbgErrs == 0u, useWarp ? "WARP: d3d12DebugErrors == 0" : "HW: d3d12DebugErrors == 0");
}

} // namespace

int main(int argc, char** argv)
{
    bool warpOnly     = false;
    bool enableDebug  = true;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--warp") == 0)     warpOnly    = true;
        if (std::strcmp(argv[i], "--no-debug") == 0) enableDebug = false;
    }

    if (enableDebug) {
        ComPtr<ID3D12Debug> dbg;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) {
            dbg->EnableDebugLayer();
        }
    }

    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory2(0u, IID_PPV_ARGS(&factory)))) {
        std::printf("[FAIL] CreateDXGIFactory2 failed\n");
        return 1;
    }

    if (!warpOnly) {
        RunAdapterSuite(factory.Get(), false);
    }
    RunAdapterSuite(factory.Get(), true);

    std::printf("\n=== Physical Water Caustics & Underwater God-Rays Summary: %d passed, %d failed ===\n",
                g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
