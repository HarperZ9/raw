// test_overlay_gpu.cpp: Standalone DX12 GPU Verification Suite for Wave 48 Zero-Dependency Telemetry & Sparkline HUD.
// Original clean-room implementation, MIT License.
//
// Runs on BOTH WARP and Hardware GPU (e.g., RTX 4090) with D3D12 Debug Layer ENABLED:
//   1. Pure CPU FormatEngineTelemetryLines & ComputeOverlayPanelRect validation.
//   2. enabled == false invariance proof: 0 modified pixels (bit-exact background preservation).
//   3. enabled == true visual & isolation proof: top-left glass card + crisp 5x7 glyphs + 64-sample color-coded
//      sparkline bars (green/amber/crimson), 0 out-of-card modifications, 0 NaN/Inf, 0 D3D12 Debug Layer errors.
//   4. Real hardware GPU ID3D12QueryHeap timing proof: steady-state overlayMs < 0.05 ms on RTX 4090.

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

#include "RawOverlayHost.h"

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

void BuildPatternRgba8(std::vector<uint8_t>& out, uint32_t w, uint32_t h)
{
    out.resize(static_cast<size_t>(w) * h * 4u);
    for (uint32_t y = 0u; y < h; ++y) {
        for (uint32_t x = 0u; x < w; ++x) {
            const size_t idx = (static_cast<size_t>(y) * w + x) * 4u;
            // Smooth medium-bright sky/terrain backdrop so glass darkening & bright glyphs both read clearly
            out[idx + 0u] = static_cast<uint8_t>(96u + ((x * 90u) / w));
            out[idx + 1u] = static_cast<uint8_t>(112u + ((y * 70u) / h));
            out[idx + 2u] = static_cast<uint8_t>(142u + (((x + y) * 50u) / (w + h)));
            out[idx + 3u] = 255u;
        }
    }
}

void RunAdapterTest(IDXGIFactory6* factory, bool useWarp)
{
    const char* tag = useWarp ? "WARP" : "HW-GPU";
    std::printf("\n--- Running GPU Telemetry & Sparkline HUD Proof [%s] ---\n", tag);

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

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
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
    uint64_t fenceVal = 0u;

    uint64_t tsFreq = 0u;
    queue->GetTimestampFrequency(&tsFreq);

    RawOverlayHost host;
    std::string initErr;
    const bool initOk = host.Init(dev.Get(), &initErr);
    if (!initOk) {
        std::printf("  Init error: %s\n", initErr.c_str());
    }
    Check(initOk && host.Ready(), useWarp ? "WARP: RawOverlayHost::Init" : "HW: RawOverlayHost::Init");
    if (!initOk) {
        CloseHandle(ev);
        return;
    }
    host.SetTimestampFrequency(tsFreq);

    // Create 640x360 RGBA8_UNORM target texture (UAV + RT) + upload + readback buffers
    constexpr uint32_t kW = 640u;
    constexpr uint32_t kH = 360u;

    D3D12_HEAP_PROPERTIES defHp{};
    defHp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC texDesc{};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = kW;
    texDesc.Height = kH;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    ComPtr<ID3D12Resource> targetTex;
    if (FAILED(dev->CreateCommittedResource(&defHp, D3D12_HEAP_FLAG_NONE, &texDesc,
                                            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&targetTex)))) {
        Check(false, useWarp ? "WARP: Create RGBA8 target texture" : "HW: Create RGBA8 target texture");
        CloseHandle(ev);
        return;
    }

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT numRows = 0u;
    UINT64 rowBytes = 0u;
    UINT64 totalBytes = 0u;
    dev->GetCopyableFootprints(&texDesc, 0, 1, 0, &fp, &numRows, &rowBytes, &totalBytes);

    D3D12_HEAP_PROPERTIES upHp{};
    upHp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_HEAP_PROPERTIES rbHp{};
    rbHp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC bufDesc{};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width = totalBytes;
    bufDesc.Height = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels = 1;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> uploadBuf, readbackBuf;
    if (FAILED(dev->CreateCommittedResource(&upHp, D3D12_HEAP_FLAG_NONE, &bufDesc,
                                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuf))) ||
        FAILED(dev->CreateCommittedResource(&rbHp, D3D12_HEAP_FLAG_NONE, &bufDesc,
                                            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readbackBuf)))) {
        Check(false, useWarp ? "WARP: Upload/Readback buffers" : "HW: Upload/Readback buffers");
        CloseHandle(ev);
        return;
    }

    std::vector<uint8_t> basePixels;
    BuildPatternRgba8(basePixels, kW, kH);
    {
        void* p = nullptr;
        D3D12_RANGE rr{0, 0};
        uploadBuf->Map(0, &rr, &p);
        auto* dst = static_cast<uint8_t*>(p) + fp.Offset;
        for (uint32_t y = 0u; y < kH; ++y) {
            std::memcpy(dst + static_cast<size_t>(y) * fp.Footprint.RowPitch,
                        basePixels.data() + static_cast<size_t>(y) * kW * 4u,
                        static_cast<size_t>(kW) * 4u);
        }
        uploadBuf->Unmap(0, nullptr);
    }

    auto UploadAndRunOverlay = [&](const OverlayHudData& hud, uint32_t repeatDispatches, std::vector<uint8_t>& outReadback) -> bool {
        alloc->Reset();
        cmd->Reset(alloc.Get(), nullptr);

        // Copy clean baseline pattern into targetTex
        D3D12_RESOURCE_BARRIER toCopy{};
        toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toCopy.Transition.pResource = targetTex.Get();
        toCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &toCopy);

        D3D12_TEXTURE_COPY_LOCATION srcLoc{};
        srcLoc.pResource = uploadBuf.Get();
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint = fp;

        D3D12_TEXTURE_COPY_LOCATION dstLoc{};
        dstLoc.pResource = targetTex.Get();
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = 0u;
        cmd->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

        D3D12_RESOURCE_BARRIER toRt{};
        toRt.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toRt.Transition.pResource = targetTex.Get();
        toRt.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        toRt.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        toRt.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &toRt);

        for (uint32_t i = 0u; i < repeatDispatches; ++i) {
            host.RecordOverlay(cmd.Get(), targetTex.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, kW, kH, hud);
        }

        D3D12_RESOURCE_BARRIER toCopySrc{};
        toCopySrc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toCopySrc.Transition.pResource = targetTex.Get();
        toCopySrc.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        toCopySrc.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        toCopySrc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &toCopySrc);

        D3D12_TEXTURE_COPY_LOCATION rbDst{};
        rbDst.pResource = readbackBuf.Get();
        rbDst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        rbDst.PlacedFootprint = fp;
        cmd->CopyTextureRegion(&rbDst, 0, 0, 0, &dstLoc, nullptr);

        D3D12_RESOURCE_BARRIER toCommon{};
        toCommon.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toCommon.Transition.pResource = targetTex.Get();
        toCommon.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        toCommon.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        toCommon.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &toCommon);

        if (FAILED(cmd->Close())) return false;
        ID3D12CommandList* lists[1] = {cmd.Get()};
        queue->ExecuteCommandLists(1, lists);
        if (!WaitForQueue(queue.Get(), fence.Get(), fenceVal, ev)) return false;

        outReadback.resize(static_cast<size_t>(kW) * kH * 4u);
        void* p = nullptr;
        D3D12_RANGE rr{0, static_cast<SIZE_T>(totalBytes)};
        if (FAILED(readbackBuf->Map(0, &rr, &p))) return false;
        const auto* src = static_cast<const uint8_t*>(p) + fp.Offset;
        for (uint32_t y = 0u; y < kH; ++y) {
            std::memcpy(outReadback.data() + static_cast<size_t>(y) * kW * 4u,
                        src + static_cast<size_t>(y) * fp.Footprint.RowPitch,
                        static_cast<size_t>(kW) * 4u);
        }
        D3D12_RANGE wr{0, 0};
        readbackBuf->Unmap(0, &wr);
        return true;
    };

    // Build realistic multi-threshold sparkline history (green <8.3ms, amber 8.3..16.6ms, crimson >16.6ms)
    float history[64]{};
    for (uint32_t i = 0u; i < 64u; ++i) {
        if (i < 24u) {
            history[i] = 4.2f + 0.14f * static_cast<float>(i);       // green zone
        } else if (i < 46u) {
            history[i] = 9.5f + 0.28f * static_cast<float>(i - 24u); // amber zone
        } else {
            history[i] = 17.5f + 0.35f * static_cast<float>(i - 46u);// crimson zone
        }
    }

    OverlayTelemetrySnapshot snap{};
    snap.fps = 142.8f;
    snap.frameMs = 7.00f;
    snap.fireMs = 0.22f;
    snap.waterMs = 0.02f;
    snap.giMs = 0.10f;
    snap.xpbdMs = 0.05f;
    snap.overlayMs = 0.011f;
    snap.seasonName = "WINTER";
    snap.weatherName = "BLIZZARD";
    snap.tempC = -14.5f;
    snap.windMps = 11.2f;
    snap.snowPackM = 0.48f;
    snap.cellX = 4;
    snap.cellY = -12;
    snap.landmarkName = "RIVERWOOD";
    snap.activeCells = 9u;
    snap.activeMeshlets = 4766u;
    snap.lruHitRatePct = 72.1f;
    snap.historyFrameMs = history;
    snap.historyCount = 64u;

    OverlayHudData hud{};
    FormatEngineTelemetryLines(snap, hud);

    // 1) Proof: enabled == false leaves target 100% bit-exact untouched
    hud.enabled = false;
    std::vector<uint8_t> disabledRb;
    const bool disRunOk = UploadAndRunOverlay(hud, 1u, disabledRb);
    uint32_t disabledDiffPx = 0u;
    if (disRunOk) {
        for (uint32_t i = 0u; i < kW * kH; ++i) {
            const size_t off = static_cast<size_t>(i) * 4u;
            if (disabledRb[off + 0] != basePixels[off + 0] ||
                disabledRb[off + 1] != basePixels[off + 1] ||
                disabledRb[off + 2] != basePixels[off + 2] ||
                disabledRb[off + 3] != basePixels[off + 3]) {
                ++disabledDiffPx;
            }
        }
    }
    Check(disRunOk && disabledDiffPx == 0u,
          useWarp ? "WARP: enabled==false bit-exact 0 modified pixels"
                  : "HW: enabled==false bit-exact 0 modified pixels");

    // 2) Proof: enabled == true renders card + glyphs + sparkline strictly inside card rect
    hud.enabled = true;
    const OverlayPanelRect rect = ComputeOverlayPanelRect(hud, kW, kH);

    // Run 8 individual GPU frames so 4-frame timestamp ring stabilizes & harvests cleanly
    std::vector<uint8_t> enabledRb;
    bool enRunOk = true;
    float minMs = 1e9f;
    float sumMs = 0.0f;
    uint32_t validSamples = 0u;
    for (uint32_t f = 0u; f < 10u; ++f) {
        enRunOk = enRunOk && UploadAndRunOverlay(hud, 1u, enabledRb);
        if (host.HasValidTiming()) {
            const float ms = host.GetLastOverlayMs();
            minMs = std::min(minMs, ms);
            sumMs += ms;
            ++validSamples;
        }
    }

    uint32_t panelModifiedPx = 0u;
    uint32_t outsideModifiedPx = 0u;
    uint32_t glyphLitPx = 0u;
    uint32_t sparkGreenPx = 0u;
    uint32_t sparkAmberPx = 0u;
    uint32_t sparkCrimsonPx = 0u;

    const uint32_t sparkStartY = rect.y + 9u + hud.lineCount * 10u + 5u;
    if (enRunOk) {
        for (uint32_t y = 0u; y < kH; ++y) {
            for (uint32_t x = 0u; x < kW; ++x) {
                const size_t off = (static_cast<size_t>(y) * kW + x) * 4u;
                const uint8_t r = enabledRb[off + 0];
                const uint8_t g = enabledRb[off + 1];
                const uint8_t b = enabledRb[off + 2];
                const uint8_t a = enabledRb[off + 3];
                const bool modified = (r != basePixels[off + 0] || g != basePixels[off + 1] ||
                                       b != basePixels[off + 2] || a != basePixels[off + 3]);
                const bool insideCard = (x >= rect.x && x < rect.x + rect.w &&
                                         y >= rect.y && y < rect.y + rect.h);
                if (modified) {
                    if (insideCard) {
                        ++panelModifiedPx;
                    } else {
                        ++outsideModifiedPx;
                    }
                }
                if (insideCard) {
                    if (y >= rect.y + 8u && y < sparkStartY) {
                        // High-contrast glyph pixels over dark slate glass
                        const uint32_t maxC = std::max<uint32_t>(r, std::max<uint32_t>(g, b));
                        if (maxC >= 170u) {
                            ++glyphLitPx;
                        }
                    } else if (y >= sparkStartY) {
                        // Color-coded sparkline bar detection
                        if (g >= 140u && r < 110u && b < 140u) {
                            ++sparkGreenPx;
                        } else if (r >= 170u && g >= 110u && b < 90u) {
                            ++sparkAmberPx;
                        } else if (r >= 170u && g < 90u && b < 90u) {
                            ++sparkCrimsonPx;
                        }
                    }
                }
            }
        }
    }

    // 3) Also validate RGBA16F float target for strict 0 NaN/Inf check
    uint32_t nanInfCount = 0u;
    {
        D3D12_RESOURCE_DESC hdrDesc = texDesc;
        hdrDesc.Width = 512u;
        hdrDesc.Height = 256u;
        hdrDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        ComPtr<ID3D12Resource> hdrTex;
        if (SUCCEEDED(dev->CreateCommittedResource(&defHp, D3D12_HEAP_FLAG_NONE, &hdrDesc,
                                                   D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&hdrTex)))) {
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT hfp{};
            UINT hRows = 0u;
            UINT64 hRowB = 0u, hTotB = 0u;
            dev->GetCopyableFootprints(&hdrDesc, 0, 1, 0, &hfp, &hRows, &hRowB, &hTotB);
            D3D12_RESOURCE_DESC hBufDesc = bufDesc;
            hBufDesc.Width = hTotB;
            ComPtr<ID3D12Resource> hRb;
            if (SUCCEEDED(dev->CreateCommittedResource(&rbHp, D3D12_HEAP_FLAG_NONE, &hBufDesc,
                                                       D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&hRb)))) {
                alloc->Reset();
                cmd->Reset(alloc.Get(), nullptr);
                host.RecordOverlay(cmd.Get(), hdrTex.Get(), D3D12_RESOURCE_STATE_COMMON, 512u, 256u, hud);
                D3D12_RESOURCE_BARRIER toSrc{};
                toSrc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                toSrc.Transition.pResource = hdrTex.Get();
                toSrc.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
                toSrc.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
                toSrc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                cmd->ResourceBarrier(1, &toSrc);
                D3D12_TEXTURE_COPY_LOCATION dLoc{hRb.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}};
                dLoc.PlacedFootprint = hfp;
                D3D12_TEXTURE_COPY_LOCATION sLoc{hdrTex.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
                sLoc.SubresourceIndex = 0u;
                cmd->CopyTextureRegion(&dLoc, 0, 0, 0, &sLoc, nullptr);
                cmd->Close();
                ID3D12CommandList* lists[1] = {cmd.Get()};
                queue->ExecuteCommandLists(1, lists);
                WaitForQueue(queue.Get(), fence.Get(), fenceVal, ev);

                void* p = nullptr;
                D3D12_RANGE rr{0, static_cast<SIZE_T>(hTotB)};
                if (SUCCEEDED(hRb->Map(0, &rr, &p))) {
                    const auto* base = static_cast<const uint8_t*>(p) + hfp.Offset;
                    for (uint32_t y = 0u; y < 256u; ++y) {
                        const auto* row = reinterpret_cast<const uint16_t*>(base + static_cast<size_t>(y) * hfp.Footprint.RowPitch);
                        for (uint32_t x = 0u; x < 512u * 4u; ++x) {
                            // IEEE 754 binary16 NaN/Inf have exponent == 0x1F (all 5 exponent bits set)
                            if ((row[x] & 0x7C00u) == 0x7C00u) {
                                ++nanInfCount;
                            }
                        }
                    }
                    D3D12_RANGE wr{0, 0};
                    hRb->Unmap(0, &wr);
                }
            }
        }
    }

    const float meanMs = (validSamples > 0u) ? (sumMs / static_cast<float>(validSamples)) : -1.0f;
    const uint32_t dbgErrs = CountDebugErrors(dev.Get());

    std::printf("  [%s] card=(%u,%u %ux%u) panelPx=%u glyphLitPx=%u spark(G=%u A=%u R=%u) outsideMod=%u nanInf=%u overlayMs=%.4f (min=%.4f) dbgErrors=%u\n",
                tag, rect.x, rect.y, rect.w, rect.h, panelModifiedPx, glyphLitPx,
                sparkGreenPx, sparkAmberPx, sparkCrimsonPx, outsideModifiedPx, nanInfCount,
                static_cast<double>(meanMs), static_cast<double>(minMs), dbgErrs);

    Check(enRunOk && panelModifiedPx > 5000u,
          useWarp ? "WARP: HUD card rendered (panelPixels > 5000)"
                  : "HW: HUD card rendered (panelPixels > 5000)");
    Check(glyphLitPx > 200u,
          useWarp ? "WARP: Crisp 5x7 ASCII glyphs rendered (glyphLitPixels > 200)"
                  : "HW: Crisp 5x7 ASCII glyphs rendered (glyphLitPixels > 200)");
    Check(sparkGreenPx > 40u && sparkAmberPx > 40u && sparkCrimsonPx > 40u,
          useWarp ? "WARP: Color-coded sparkline bars (green/amber/crimson)"
                  : "HW: Color-coded sparkline bars (green/amber/crimson)");
    Check(outsideModifiedPx == 0u,
          useWarp ? "WARP: Outside-card pixels 0 modified"
                  : "HW: Outside-card pixels 0 modified");
    Check(nanInfCount == 0u,
          useWarp ? "WARP: 0 NaN/Inf pixels on RGBA16F target"
                  : "HW: 0 NaN/Inf pixels on RGBA16F target");
    if (!useWarp) {
        Check(validSamples > 0u && meanMs >= 0.0f && meanMs < 0.05f,
              "HW: Real GPU timestamp cost < 0.05 ms");
    } else {
        Check(validSamples > 0u && meanMs >= 0.0f,
              "WARP: Valid GPU timestamp harvested");
    }
    Check(dbgErrs == 0u,
          useWarp ? "WARP: 0 D3D12 Debug Layer errors"
                  : "HW: 0 D3D12 Debug Layer errors");

    host.Shutdown();
    CloseHandle(ev);
}

} // namespace

int main()
{
    std::printf("=== Wave 48 GPU Compute Telemetry & Sparkline HUD Proof ===\n");

    // Pure CPU helper checks
    {
        OverlayTelemetrySnapshot s{};
        s.fps = 120.5f;
        s.frameMs = 8.30f;
        s.fireMs = 0.22f;
        s.waterMs = 0.02f;
        s.giMs = 0.10f;
        s.xpbdMs = 0.05f;
        s.seasonName = "autumn";
        s.weatherName = "rain";
        s.landmarkName = "whiterun";
        float hist[4] = {6.0f, 9.0f, 12.0f, 18.0f};
        s.historyFrameMs = hist;
        s.historyCount = 4u;

        OverlayHudData hud{};
        FormatEngineTelemetryLines(s, hud);
        Check(hud.lineCount == 7u && hud.sparkCount == 4u &&
                  std::strstr(hud.lines[0], "RAW ENGINE TELEMETRY") != nullptr &&
                  std::strstr(hud.lines[2], "FIRE") != nullptr &&
                  std::strstr(hud.lines[2], "XPBD") != nullptr &&
                  std::strstr(hud.lines[4], "AUTUMN") != nullptr &&
                  std::strstr(hud.lines[5], "WHITERUN") != nullptr,
              "CPU: FormatEngineTelemetryLines uppercase formatting & sparkline copy");

        const OverlayPanelRect r = ComputeOverlayPanelRect(hud, 1920u, 1080u);
        Check(r.x == 16u && r.y == 16u && r.w >= 280u && r.h >= 110u,
              "CPU: ComputeOverlayPanelRect sane top-left bounding box");
    }

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
    if (!factory) {
        return 1;
    }

    RunAdapterTest(factory.Get(), true);  // WARP
    RunAdapterTest(factory.Get(), false); // HW GPU (RTX 4090)

    std::printf("\n=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
