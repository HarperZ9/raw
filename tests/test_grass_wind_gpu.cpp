// test_grass_wind_gpu.cpp: Standalone DX12 GPU Verification Suite for Wave 62 65,536-Blade Dynamic Grass Wind,
// Seasonal Snow-Weight Droop, Wildfire Char & Critically-Damped Actor-Trample Solver.
// Original clean-room implementation, MIT License.
//
// Runs on BOTH Hardware GPU (RTX 4090) and WARP (--warp supported) with D3D12 Debug Layer ENABLED:
//   1. Actor trample capsule flattens blade bend angle > 0.75 rad aligned with travel direction, then
//      critically-damped elastic spring-back recovers smoothly toward upright (< 0.15 rad, zero overshoot).
//   2. Gale +X wind creates strong directional travelling wave displacement (meanBendX > 0.30,
//      spatial crest/trough modulation stdDev > 0.03, visible phase propagation over time).
//   3. Seasonal snow-weight droop increases calm blade droop significantly (snowDroop > dryDroop + 0.25).
//   4. Wildfire thermal heat source chars blades persistently (charMask > 0.75 inside radius, 0.0 outside)
//      and burnt brittle blades stiffen (bend less under gale wind than living unburnt blades).
//   5. World-anchored toroidal scrolling shifts trampled trails to exact integer-texel coordinates while
//      initializing freshly revealed borders cleanly upright.
//   6. Zero NaN/Inf across all 65,536 blades, HW RTX 4090 GPU time < 0.05 ms (32 capsules + 16 heat sources),
//      and d3d12DebugErrors == 0 on both HW and WARP.

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

#include "RawGrassWindHost.h"

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

struct FieldData {
    uint32_t           w = 0u;
    uint32_t           h = 0u;
    std::vector<float> rgba; // (bendX, bendZ, droop01, charMask01)
    uint32_t           nanInfCount = 0u;

    const float* At(uint32_t x, uint32_t y) const
    {
        return &rgba[(static_cast<size_t>(y) * w + x) * 4u];
    }
};

bool ReadbackRgba16fTexture(ID3D12Device* dev, ID3D12CommandQueue* q, ID3D12CommandAllocator* alloc,
                            ID3D12GraphicsCommandList* cmd, ID3D12Fence* fence, uint64_t& fv, HANDLE ev,
                            ID3D12Resource* srcTex, D3D12_RESOURCE_STATES curState,
                            uint32_t w, uint32_t h, FieldData& out)
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
    std::printf("\n--- Running 65,536-Blade Dynamic Grass Wind, Droop & Trample Suite [%s] ---\n", tag);

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

    constexpr uint32_t kRes = 256u;
    RawGrassWindHost host;
    GrassWindDesc desc{};
    desc.gridWidth  = kRes;
    desc.gridHeight = kRes;
    desc.worldSpanM = 64.0f; // 0.25 m / texel
    std::string initErr;
    const bool okInit = host.Init(dev.Get(), desc, &initErr);
    if (!okInit) {
        std::printf("  [ERR] host.Init: %s\n", initErr.c_str());
    }
    Check(okInit && host.Ready() && host.BladeCount() == 65536u,
          useWarp ? "WARP: RawGrassWindHost::Init (65,536 blades)"
                  : "HW: RawGrassWindHost::Init (65,536 blades)");
    if (!okInit) {
        CloseHandle(ev);
        return;
    }
    host.SetTimestampFrequency(tsFreq);

    auto stepAndRead = [&](const GrassWindParams& p, uint32_t steps, FieldData* outField) -> bool {
        GrassWindParams cur = p;
        for (uint32_t s = 0u; s < steps; ++s) {
            alloc->Reset();
            cmd->Reset(alloc.Get(), nullptr);
            const bool ok = host.Execute(cmd.Get(), cur);
            cmd->Close();
            if (!ok) return false;
            ID3D12CommandList* lists[1] = {cmd.Get()};
            queue->ExecuteCommandLists(1u, lists);
            if (!WaitForQueue(queue.Get(), fence.Get(), fenceVal, ev)) return false;
            cur.resetField = false;
            cur.timeSec   += cur.dtSec;
        }
        if (outField) {
            return ReadbackRgba16fTexture(dev.Get(), queue.Get(), alloc.Get(), cmd.Get(), fence.Get(), fenceVal, ev,
                                          host.GetBendFieldResource(), host.BendFieldResourceState(),
                                          kRes, kRes, *outField);
        }
        return true;
    };

    const size_t totalBlades = static_cast<size_t>(kRes) * kRes;
    uint32_t totalNanInf = 0u;

    // -------------------------------------------------------------------------
    // Test 1: Actor trample flattens blade (> 0.75 rad) in travel direction, then elastically springs back
    // -------------------------------------------------------------------------
    GrassTrampleCapsule actorCap{};
    actorCap.startX          = -1.0f;
    actorCap.startZ          = 0.0f;
    actorCap.endX            = 1.0f;
    actorCap.endZ            = 0.0f;
    actorCap.velX            = 4.5f;  // Moving in +X
    actorCap.velZ            = 0.0f;
    actorCap.radiusM         = 1.25f;
    actorCap.flattenAngleRad = 1.22f;

    GrassWindParams pTrample{};
    pTrample.camWorldX    = 0.0f;
    pTrample.camWorldZ    = 0.0f;
    pTrample.dtSec        = 1.0f / 60.0f;
    pTrample.windSpeedMps = 0.0f;
    pTrample.springOmega  = 2.8f;
    pTrample.snowLoad01   = 0.0f;
    pTrample.resetField   = true;
    pTrample.capsules     = &actorCap;
    pTrample.capsuleCount = 1u;

    FieldData trampledField{};
    const bool okTrample = stepAndRead(pTrample, 8u, &trampledField);
    totalNanInf += trampledField.nanInfCount;

    const float* ctrT    = trampledField.At(128u, 128u);
    const float  trampBx = ctrT[0];
    const float  trampBz = ctrT[1];
    const float  trampAng = std::hypot(trampBx, trampBz);
    const float  alignX   = (trampAng > 1e-4f) ? (trampBx / trampAng) : 0.0f;

    // Remove actor and step 120 frames (2.0 s) to verify smooth critically-damped elastic spring-back
    GrassWindParams pRecover = pTrample;
    pRecover.resetField   = false;
    pRecover.capsules     = nullptr;
    pRecover.capsuleCount = 0u;

    FieldData midRecField{}, fullRecField{};
    const bool okMidRec  = stepAndRead(pRecover, 30u, &midRecField);
    const bool okFullRec = stepAndRead(pRecover, 90u, &fullRecField);
    totalNanInf += midRecField.nanInfCount + fullRecField.nanInfCount;

    const float midBx  = midRecField.At(128u, 128u)[0];
    const float midAng = std::hypot(midRecField.At(128u, 128u)[0], midRecField.At(128u, 128u)[1]);
    const float recBx  = fullRecField.At(128u, 128u)[0];
    const float recAng = std::hypot(fullRecField.At(128u, 128u)[0], fullRecField.At(128u, 128u)[1]);

    std::printf("  Trample & Spring-Back: flattened=%.3f rad (alignX=%.3f) -> 0.5s=%.3f rad -> 2.0s=%.3f rad (noOvershoot=%d)\n",
                trampAng, alignX, midAng, recAng, (midBx >= 0.0f && recBx >= -1e-4f) ? 1 : 0);

    Check(okTrample && trampAng > 0.75f && alignX > 0.90f,
          useWarp ? "WARP: Actor trample flattens blade (> 0.75 rad) aligned with travel dir"
                  : "HW: Actor trample flattens blade (> 0.75 rad) aligned with travel dir");
    Check(okMidRec && okFullRec && (midAng < trampAng) && (recAng < midAng) && (recAng < 0.15f) && (recBx >= -1e-4f),
          useWarp ? "WARP: Critically-damped spring-back recovers smoothly toward upright (< 0.15 rad)"
                  : "HW: Critically-damped spring-back recovers smoothly toward upright (< 0.15 rad)");

    // -------------------------------------------------------------------------
    // Test 2: Gale wind directional travelling wave displacement
    // -------------------------------------------------------------------------
    GrassWindParams pWind{};
    pWind.camWorldX    = 0.0f;
    pWind.camWorldZ    = 0.0f;
    pWind.dtSec        = 1.0f / 60.0f;
    pWind.timeSec      = 0.5f;
    pWind.windSpeedMps = 14.0f;
    pWind.windDirX     = 1.0f;
    pWind.windDirZ     = 0.0f;
    pWind.gustStrength = 0.85f;
    pWind.snowLoad01   = 0.0f;
    pWind.resetField   = true;

    FieldData windFieldA{}, windFieldB{};
    const bool okWindA = stepAndRead(pWind, 1u, &windFieldA);
    pWind.resetField = false;
    pWind.timeSec    = 1.25f;
    const bool okWindB = stepAndRead(pWind, 1u, &windFieldB);
    totalNanInf += windFieldA.nanInfCount + windFieldB.nanInfCount;

    double sumBx = 0.0, sumAbsBz = 0.0, sumDeltaT = 0.0;
    for (size_t i = 0u; i < totalBlades; ++i) {
        const float bxA = windFieldA.rgba[i * 4u + 0u];
        const float bzA = windFieldA.rgba[i * 4u + 1u];
        const float bxB = windFieldB.rgba[i * 4u + 0u];
        sumBx     += bxA;
        sumAbsBz  += std::fabs(bzA);
        sumDeltaT += std::fabs(bxB - bxA);
    }
    const double meanBx    = sumBx / static_cast<double>(totalBlades);
    const double meanAbsBz = sumAbsBz / static_cast<double>(totalBlades);
    const double meanProp  = sumDeltaT / static_cast<double>(totalBlades);
    double varBx = 0.0;
    for (size_t i = 0u; i < totalBlades; ++i) {
        const double d = static_cast<double>(windFieldA.rgba[i * 4u + 0u]) - meanBx;
        varBx += d * d;
    }
    const double stdBx = std::sqrt(varBx / static_cast<double>(totalBlades));

    std::printf("  Gale Wind (+X 14 m/s): meanBendX=%.4f stdBendX=%.4f mean|BendZ|=%.4f phaseDelta=%.4f\n",
                meanBx, stdBx, meanAbsBz, meanProp);
    Check(okWindA && okWindB && meanBx > 0.30 && meanAbsBz < meanBx * 0.35 && stdBx > 0.03 && meanProp > 0.05,
          useWarp ? "WARP: Gale wind creates directional travelling wave displacement"
                  : "HW: Gale wind creates directional travelling wave displacement");

    // -------------------------------------------------------------------------
    // Test 3: Seasonal snow-weight droop increases blade droop under calm conditions
    // -------------------------------------------------------------------------
    GrassWindParams pDry{};
    pDry.windSpeedMps = 0.0f;
    pDry.snowLoad01   = 0.0f;
    pDry.resetField   = true;

    GrassWindParams pSnow = pDry;
    pSnow.snowLoad01 = 0.85f;

    FieldData dryField{}, snowField{};
    const bool okDry  = stepAndRead(pDry, 1u, &dryField);
    const bool okSnow = stepAndRead(pSnow, 1u, &snowField);
    totalNanInf += dryField.nanInfCount + snowField.nanInfCount;

    double dryDroop = 0.0, snowDroop = 0.0;
    for (size_t i = 0u; i < totalBlades; ++i) {
        dryDroop  += dryField.rgba[i * 4u + 2u];
        snowDroop += snowField.rgba[i * 4u + 2u];
    }
    dryDroop  /= static_cast<double>(totalBlades);
    snowDroop /= static_cast<double>(totalBlades);

    std::printf("  Snow Droop: dry=%.4f -> snow(0.85)=%.4f (delta=+%.4f)\n",
                dryDroop, snowDroop, snowDroop - dryDroop);
    Check(okDry && okSnow && dryDroop < 0.01 && snowDroop > dryDroop + 0.25 && snowDroop <= 1.0,
          useWarp ? "WARP: Seasonal snow-weight increases grass blade droop"
                  : "HW: Seasonal snow-weight increases grass blade droop");

    // -------------------------------------------------------------------------
    // Test 4: Wildfire thermal heat source chars blades persistently & stiffens against wind
    // -------------------------------------------------------------------------
    GrassHeatSource wildfire{};
    wildfire.worldX    = 0.0f;
    wildfire.worldZ    = 0.0f;
    wildfire.radiusM   = 3.5f;
    wildfire.heatPower = 3.5f;

    GrassWindParams pFire{};
    pFire.dtSec         = 1.0f / 30.0f;
    pFire.windSpeedMps  = 0.0f;
    pFire.charRateScale = 1.0f;
    pFire.resetField    = true;
    pFire.heatSources   = &wildfire;
    pFire.heatCount     = 1u;

    FieldData fireField{};
    const bool okFire = stepAndRead(pFire, 20u, &fireField);
    totalNanInf += fireField.nanInfCount;

    // Remove fire and blow steady wind to verify persistence + charred blade stiffening
    GrassWindParams pPostFire{};
    pPostFire.dtSec        = 1.0f / 60.0f;
    pPostFire.timeSec      = 0.5f;
    pPostFire.windSpeedMps = 12.0f;
    pPostFire.windDirX     = 1.0f;
    pPostFire.gustStrength = 0.0f; // uniform wind to compare burnt vs unburnt cleanly
    pPostFire.resetField   = false;

    FieldData postFireField{};
    const bool okPostFire = stepAndRead(pPostFire, 2u, &postFireField);
    totalNanInf += postFireField.nanInfCount;

    const float coreChar    = postFireField.At(128u, 128u)[3];
    const float outsideChar = postFireField.At(32u, 32u)[3];
    const float burntBendX  = postFireField.At(128u, 128u)[0];
    const float livingBendX = postFireField.At(32u, 32u)[0];

    std::printf("  Wildfire Char: coreChar=%.3f outsideChar=%.3f burntBendX=%.3f vs livingBendX=%.3f\n",
                coreChar, outsideChar, burntBendX, livingBendX);
    Check(okFire && okPostFire && coreChar > 0.75f && outsideChar < 1e-4f && burntBendX < livingBendX * 0.65f,
          useWarp ? "WARP: Wildfire chars blades persistently & stiffens burnt stalks"
                  : "HW: Wildfire chars blades persistently & stiffens burnt stalks");

    // -------------------------------------------------------------------------
    // Test 5: World-anchored toroidal scroll preserves trampled trails & initializes fresh borders
    // -------------------------------------------------------------------------
    GrassWindParams pStampScroll = pTrample;
    pStampScroll.resetField   = true;
    pStampScroll.capsules     = &actorCap;
    pStampScroll.capsuleCount = 1u;
    FieldData preScrollField{};
    const bool okPreScroll = stepAndRead(pStampScroll, 6u, &preScrollField);
    const float preBend = std::hypot(preScrollField.At(128u, 128u)[0], preScrollField.At(128u, 128u)[1]);

    // Shift camera origin by +8.0 m in X (= +32 texels at 0.25 m/texel -> trail moves from x=128 to x=96)
    GrassWindParams pScrolled{};
    pScrolled.camWorldX    = 8.0f;
    pScrolled.camWorldZ    = 0.0f;
    pScrolled.dtSec        = 0.0f; // zero dt so elastic decay doesn't alter magnitude during scroll check
    pScrolled.windSpeedMps = 0.0f;
    pScrolled.resetField   = false;
    FieldData postScrollField{};
    const bool okPostScroll = stepAndRead(pScrolled, 1u, &postScrollField);
    totalNanInf += preScrollField.nanInfCount + postScrollField.nanInfCount;

    const float shiftedBend = std::hypot(postScrollField.At(96u, 128u)[0], postScrollField.At(96u, 128u)[1]);
    const float borderBend  = std::hypot(postScrollField.At(248u, 128u)[0], postScrollField.At(248u, 128u)[1]);
    const float borderChar  = postScrollField.At(248u, 128u)[3];

    std::printf("  Toroidal Scroll (+8m): pre(128,128)=%.3f -> shifted(96,128)=%.3f freshBorder(248,128)=%.4f char=%.4f\n",
                preBend, shiftedBend, borderBend, borderChar);
    Check(okPreScroll && okPostScroll && std::fabs(shiftedBend - preBend) < 0.02f &&
              borderBend < 1e-4f && borderChar < 1e-4f,
          useWarp ? "WARP: Toroidal scroll preserves trampled trail & initializes fresh borders"
                  : "HW: Toroidal scroll preserves trampled trail & initializes fresh borders");
    Check(totalNanInf == 0u,
          useWarp ? "WARP: Zero NaN/Inf across all 65,536 grass blades"
                  : "HW: Zero NaN/Inf across all 65,536 grass blades");

    // -------------------------------------------------------------------------
    // Test 6: Full-load GPU timing benchmark (65,536 blades + 32 capsules + 16 heat sources < 0.05 ms on RTX 4090)
    // -------------------------------------------------------------------------
    GrassTrampleCapsule benchCaps[RawGrassWindHost::kMaxCapsules]{};
    for (uint32_t i = 0u; i < RawGrassWindHost::kMaxCapsules; ++i) {
        const float a = static_cast<float>(i) * (6.2831853f / 32.0f);
        benchCaps[i].startX          = std::cos(a) * 14.0f;
        benchCaps[i].startZ          = std::sin(a) * 14.0f;
        benchCaps[i].endX            = benchCaps[i].startX + std::cos(a + 0.5f) * 1.2f;
        benchCaps[i].endZ            = benchCaps[i].startZ + std::sin(a + 0.5f) * 1.2f;
        benchCaps[i].velX            = std::cos(a + 0.5f) * 4.0f;
        benchCaps[i].velZ            = std::sin(a + 0.5f) * 4.0f;
        benchCaps[i].radiusM         = 0.95f;
        benchCaps[i].flattenAngleRad = 1.20f;
    }
    GrassHeatSource benchHeats[RawGrassWindHost::kMaxHeatSources]{};
    for (uint32_t i = 0u; i < RawGrassWindHost::kMaxHeatSources; ++i) {
        const float a = static_cast<float>(i) * (6.2831853f / 16.0f);
        benchHeats[i].worldX    = std::cos(a) * 20.0f;
        benchHeats[i].worldZ    = std::sin(a) * 20.0f;
        benchHeats[i].radiusM   = 2.8f;
        benchHeats[i].heatPower = 1.5f;
    }

    GrassWindParams pBench{};
    pBench.dtSec         = 1.0f / 60.0f;
    pBench.windSpeedMps  = 11.5f;
    pBench.windDirX      = 0.85f;
    pBench.windDirZ      = 0.52f;
    pBench.gustStrength  = 0.85f;
    pBench.snowLoad01    = 0.20f;
    pBench.capsules      = benchCaps;
    pBench.capsuleCount  = RawGrassWindHost::kMaxCapsules;
    pBench.heatSources   = benchHeats;
    pBench.heatCount     = RawGrassWindHost::kMaxHeatSources;

    const uint32_t warmupFrames   = useWarp ? 2u : 12u;
    const uint32_t measuredFrames = useWarp ? 4u : 30u;
    double   sumMs        = 0.0;
    uint32_t validSamples = 0u;

    for (uint32_t f = 0u; f < warmupFrames + measuredFrames; ++f) {
        pBench.resetField = (f == 0u);
        pBench.timeSec    = static_cast<float>(f) * (1.0f / 60.0f);
        pBench.camWorldX  = static_cast<float>(f) * 0.08f;
        alloc->Reset();
        cmd->Reset(alloc.Get(), nullptr);
        host.Execute(cmd.Get(), pBench);
        cmd->Close();
        ID3D12CommandList* lists[1] = {cmd.Get()};
        queue->ExecuteCommandLists(1u, lists);
        WaitForQueue(queue.Get(), fence.Get(), fenceVal, ev);
        if (f >= warmupFrames && host.HasValidTiming()) {
            sumMs += host.GetLastStepMs();
            ++validSamples;
        }
    }

    const double meanMs = (validSamples > 0u) ? (sumMs / validSamples) : 0.0;
    std::printf("  GPU Timing [%s 256x256, 32 caps + 16 heats]: mean=%.4f ms (samples=%u)\n",
                tag, meanMs, validSamples);

    if (!useWarp) {
        Check(validSamples > 0u && meanMs > 0.0 && meanMs < 0.05,
              "HW: 65,536-blade Grass Wind + 32 Capsules + 16 Heats GPU time < 0.05 ms");
    } else {
        Check(validSamples > 0u && meanMs > 0.0,
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
    bool warpOnly    = false;
    bool enableDebug = true;
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

    std::printf("\n=== 65,536-Blade Dynamic Grass Wind, Droop & Trample Summary: %d passed, %d failed ===\n",
                g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
