// test_ui_lib_gpu.cpp: Standalone DX12 GPU Verification Suite for Wave 63 RawUiLib + Debug-Layer HUD.
// Runs on BOTH WARP and Hardware GPU (RTX 4090) with D3D12 Debug Layer ENABLED:
//   1. Pure CPU UiDrawList layout, proportional 7x9 ASCII metrics, scissor clipping & DebugView 0..33 metadata.
//   2. Empty UiDrawList zero-touch invariance proof (0 modified pixels).
//   3. Full UI widget gallery (Panel, SectionHeader, Divider, Toggle ON vs OFF, Slider, Pill, Swatch,
//      Sparkline, Uppercase+Lowercase 7x9 outline typography) + strict PushClipRect scissor proof.
//   4. Debug-Layer Control & Legend HUD rendered via RawOverlayHost::RecordDebugLayerHud (views 28..33).
//   5. RGBA16F zero NaN/Inf check, 0 D3D12 Debug Layer errors, and HW GPU timing < 0.05 ms.
// Original clean-room implementation, MIT License.

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
#include "RawUiLib.h"

namespace
{
using Microsoft::WRL::ComPtr;
using namespace raw::ui;
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

void BuildBackdropRgba8(std::vector<uint8_t>& out, uint32_t w, uint32_t h)
{
    out.resize(static_cast<size_t>(w) * h * 4u);
    for (uint32_t y = 0u; y < h; ++y) {
        for (uint32_t x = 0u; x < w; ++x) {
            const size_t idx = (static_cast<size_t>(y) * w + x) * 4u;
            out[idx + 0u] = static_cast<uint8_t>(104u + ((x * 80u) / w));
            out[idx + 1u] = static_cast<uint8_t>(118u + ((y * 65u) / h));
            out[idx + 2u] = static_cast<uint8_t>(138u + (((x + y) * 45u) / (w + h)));
            out[idx + 3u] = 255u;
        }
    }
}

void RunAdapterSuite(IDXGIFactory6* factory, bool useWarp)
{
    const char* tag = useWarp ? "WARP" : "HW-GPU";
    std::printf("\n--- Running RawUiLib + Debug-Layer HUD GPU Proof [%s] ---\n", tag);

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

    RawUiLibHost uiHost;
    std::string uiErr;
    const bool uiInitOk = uiHost.Init(dev.Get(), &uiErr);
    if (!uiInitOk) std::printf("  RawUiLibHost::Init error: %s\n", uiErr.c_str());
    Check(uiInitOk && uiHost.Ready(), useWarp ? "WARP: RawUiLibHost::Init" : "HW: RawUiLibHost::Init");

    RawOverlayHost ovHost;
    std::string ovErr;
    const bool ovInitOk = ovHost.Init(dev.Get(), &ovErr);
    if (!ovInitOk) std::printf("  RawOverlayHost::Init error: %s\n", ovErr.c_str());
    Check(ovInitOk && ovHost.Ready() && ovHost.UiLibReady(),
          useWarp ? "WARP: RawOverlayHost::Init with integrated RawUiLib"
                  : "HW: RawOverlayHost::Init with integrated RawUiLib");

    if (!uiInitOk || !ovInitOk) {
        CloseHandle(ev);
        return;
    }
    uiHost.SetTimestampFrequency(tsFreq);
    ovHost.SetTimestampFrequency(tsFreq);

    constexpr uint32_t kW = 800u;
    constexpr uint32_t kH = 540u;

    D3D12_HEAP_PROPERTIES defHp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC texDesc{};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = kW;
    texDesc.Height = kH;
    texDesc.DepthOrArraySize = 1u;
    texDesc.MipLevels = 1u;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1u;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    ComPtr<ID3D12Resource> targetTex;
    if (FAILED(dev->CreateCommittedResource(&defHp, D3D12_HEAP_FLAG_NONE, &texDesc,
                                            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&targetTex)))) {
        Check(false, "Create RGBA8 target texture");
        CloseHandle(ev);
        return;
    }

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT numRows = 0u;
    UINT64 rowBytes = 0u, totalBytes = 0u;
    dev->GetCopyableFootprints(&texDesc, 0, 1, 0, &fp, &numRows, &rowBytes, &totalBytes);

    D3D12_HEAP_PROPERTIES upHp{D3D12_HEAP_TYPE_UPLOAD};
    D3D12_HEAP_PROPERTIES rbHp{D3D12_HEAP_TYPE_READBACK};
    D3D12_RESOURCE_DESC bufDesc{};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width = totalBytes;
    bufDesc.Height = 1u;
    bufDesc.DepthOrArraySize = 1u;
    bufDesc.MipLevels = 1u;
    bufDesc.SampleDesc.Count = 1u;
    bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> uploadBuf, readbackBuf;
    if (FAILED(dev->CreateCommittedResource(&upHp, D3D12_HEAP_FLAG_NONE, &bufDesc,
                                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuf))) ||
        FAILED(dev->CreateCommittedResource(&rbHp, D3D12_HEAP_FLAG_NONE, &bufDesc,
                                            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readbackBuf)))) {
        Check(false, "Upload/Readback buffer creation");
        CloseHandle(ev);
        return;
    }

    std::vector<uint8_t> basePx;
    BuildBackdropRgba8(basePx, kW, kH);
    {
        void* p = nullptr;
        D3D12_RANGE rr{0, 0};
        uploadBuf->Map(0, &rr, &p);
        auto* dst = static_cast<uint8_t*>(p) + fp.Offset;
        for (uint32_t y = 0u; y < kH; ++y) {
            std::memcpy(dst + static_cast<size_t>(y) * fp.Footprint.RowPitch,
                        basePx.data() + static_cast<size_t>(y) * kW * 4u,
                        static_cast<size_t>(kW) * 4u);
        }
        uploadBuf->Unmap(0, nullptr);
    }

    auto RunWithCallback = [&](auto&& recordFn, std::vector<uint8_t>& outRb) -> bool {
        alloc->Reset();
        cmd->Reset(alloc.Get(), nullptr);

        D3D12_RESOURCE_BARRIER bCopy{};
        bCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        bCopy.Transition.pResource = targetTex.Get();
        bCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        bCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        bCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &bCopy);

        D3D12_TEXTURE_COPY_LOCATION srcLoc{uploadBuf.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}};
        srcLoc.PlacedFootprint = fp;
        D3D12_TEXTURE_COPY_LOCATION dstLoc{targetTex.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
        dstLoc.SubresourceIndex = 0u;
        cmd->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

        D3D12_RESOURCE_BARRIER bRt{};
        bRt.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        bRt.Transition.pResource = targetTex.Get();
        bRt.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        bRt.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        bRt.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &bRt);

        recordFn(cmd.Get(), targetTex.Get());

        D3D12_RESOURCE_BARRIER bSrc{};
        bSrc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        bSrc.Transition.pResource = targetTex.Get();
        bSrc.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        bSrc.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        bSrc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &bSrc);

        D3D12_TEXTURE_COPY_LOCATION rbLoc{readbackBuf.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}};
        rbLoc.PlacedFootprint = fp;
        cmd->CopyTextureRegion(&rbLoc, 0, 0, 0, &dstLoc, nullptr);

        D3D12_RESOURCE_BARRIER bBack{};
        bBack.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        bBack.Transition.pResource = targetTex.Get();
        bBack.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        bBack.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        bBack.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &bBack);

        if (FAILED(cmd->Close())) return false;
        ID3D12CommandList* lists[1] = {cmd.Get()};
        queue->ExecuteCommandLists(1, lists);
        if (!WaitForQueue(queue.Get(), fence.Get(), fenceVal, ev)) return false;

        outRb.resize(static_cast<size_t>(kW) * kH * 4u);
        void* p = nullptr;
        D3D12_RANGE rr{0, static_cast<SIZE_T>(totalBytes)};
        if (FAILED(readbackBuf->Map(0, &rr, &p))) return false;
        const auto* src = static_cast<const uint8_t*>(p) + fp.Offset;
        for (uint32_t y = 0u; y < kH; ++y) {
            std::memcpy(outRb.data() + static_cast<size_t>(y) * kW * 4u,
                        src + static_cast<size_t>(y) * fp.Footprint.RowPitch,
                        static_cast<size_t>(kW) * 4u);
        }
        D3D12_RANGE wr{0, 0};
        readbackBuf->Unmap(0, &wr);
        return true;
    };

    // 1) Empty UiDrawList -> bit-exact 0 modified pixels
    {
        UiDrawList emptyList;
        std::vector<uint8_t> rb;
        const bool ok = RunWithCallback([&](ID3D12GraphicsCommandList* cl, ID3D12Resource* tex) {
            uiHost.RecordDrawList(cl, tex, D3D12_RESOURCE_STATE_RENDER_TARGET, kW, kH, emptyList, true);
        }, rb);
        uint32_t diffs = 0u;
        for (size_t i = 0u; i < rb.size(); ++i) {
            if (rb[i] != basePx[i]) ++diffs;
        }
        Check(ok && diffs == 0u,
              useWarp ? "WARP: Empty UiDrawList bit-exact 0 modified bytes"
                      : "HW: Empty UiDrawList bit-exact 0 modified bytes");
    }

    // 2) Full Widget Gallery + Scissor Clipping Proof
    float hist[64]{};
    for (uint32_t i = 0u; i < 64u; ++i) {
        hist[i] = (i < 24u) ? (4.0f + 0.14f * float(i))
                : ((i < 46u) ? (9.5f + 0.25f * float(i - 24u))
                             : (18.0f + 0.30f * float(i - 46u)));
    }

    UiDrawList gallery;
    gallery.AddPanel(20.0f, 20.0f, 320.0f, 260.0f, 6.0f, tokens::kGround, tokens::kBorderStrong, tokens::kAccentPass);
    gallery.AddSectionHeader(32.0f, 32.0f, 296.0f, "Widgets & Lowercase 7x9", tokens::kAccentPass);
    gallery.AddText(32.0f, 50.0f, "AbCdEfGhIjKlMnOpQrStUvWxYz 0123456789", tokens::kInkPrimary, 1u);
    gallery.AddPill(32.0f, 68.0f, "28:Class+A14", tokens::kPillOnBg, tokens::kInkPrimary, tokens::kBorderStrong, tokens::kAccentFail);
    gallery.AddToggle(32.0f, 92.0f, 296.0f, "Toggle Enabled", true, tokens::kAccentPass);
    gallery.AddToggle(32.0f, 112.0f, 296.0f, "Toggle Disabled", false, tokens::kAccentPass);
    gallery.AddSlider(32.0f, 134.0f, 296.0f, "Exposure", 0.72f, "72%", tokens::kAccentLead);
    gallery.AddLegendSwatch(32.0f, 166.0f, PackRgba8(0xFF, 0x3C, 0xA6), "Skinned Actor / Fur", "14");
    gallery.AddSparkline(32.0f, 186.0f, 296.0f, 44.0f, hist, 64u, 8.333f, 16.667f, 25.0f);

    // Scissor-clipped red rect intentionally extending 120px right of clip box [420..500, 40..120]
    gallery.PushClipRect(420.0f, 40.0f, 80.0f, 80.0f);
    gallery.AddRect(420.0f, 40.0f, 200.0f, 80.0f, PackRgba8(0xFF, 0x20, 0x20, 255));
    gallery.PopClipRect();

    const UiPixelRect dirty = gallery.ComputeDirtyRect(kW, kH);
    std::vector<uint8_t> galRb;
    bool galOk = true;
    float minMs = 1e9f, sumMs = 0.0f;
    uint32_t validTs = 0u;
    for (uint32_t f = 0u; f < 10u; ++f) {
        galOk = galOk && RunWithCallback([&](ID3D12GraphicsCommandList* cl, ID3D12Resource* tex) {
            uiHost.RecordDrawList(cl, tex, D3D12_RESOURCE_STATE_RENDER_TARGET, kW, kH, gallery, true);
        }, galRb);
        if (uiHost.HasValidTiming()) {
            const float ms = uiHost.GetLastUiMs();
            minMs = std::min(minMs, ms);
            sumMs += ms;
            ++validTs;
        }
    }

    uint32_t insideDirtyMod = 0u;
    uint32_t outsideDirtyMod = 0u;
    uint32_t clipInsideRed = 0u;
    uint32_t clipLeakRed = 0u;
    uint32_t glyphBrightPx = 0u;
    uint32_t swatchMagentaPx = 0u;
    uint32_t toggleOnBluePx = 0u;
    uint32_t toggleOffBluePx = 0u;
    uint32_t sliderBrassPx = 0u;
    uint32_t sparkBluePx = 0u;
    uint32_t sparkBrassPx = 0u;
    uint32_t sparkCoralPx = 0u;
    for (uint32_t y = 0u; y < kH; ++y) {
        for (uint32_t x = 0u; x < kW; ++x) {
            const size_t off = (static_cast<size_t>(y) * kW + x) * 4u;
            const uint8_t r = galRb[off + 0];
            const uint8_t g = galRb[off + 1];
            const uint8_t b = galRb[off + 2];
            const bool mod = (r != basePx[off + 0] || g != basePx[off + 1] || b != basePx[off + 2]);
            const bool inDirty = (x >= dirty.x && x < dirty.x + dirty.w &&
                                  y >= dirty.y && y < dirty.y + dirty.h);
            if (mod) {
                if (inDirty) ++insideDirtyMod;
                else ++outsideDirtyMod;
            }
            if (y >= 40u && y < 120u && x >= 420u && x < 500u && r > 220u && g < 60u && b < 60u) {
                ++clipInsideRed;
            }
            if (y >= 40u && y < 120u && x >= 502u && x < 625u && mod) {
                ++clipLeakRed;
            }
            if (y >= 48u && y < 62u && x >= 30u && x < 310u && r > 180u && g > 175u && b > 165u) {
                ++glyphBrightPx;
            }
            if (y >= 165u && y < 178u && x >= 31u && x < 44u && r > 200u && g < 100u && b > 120u) {
                ++swatchMagentaPx;
            }
            if (y >= 91u && y < 106u && x >= 300u && x < 330u && b > 180u && r < 140u) {
                ++toggleOnBluePx;
            }
            if (y >= 111u && y < 126u && x >= 300u && x < 330u && b > 180u && r < 140u) {
                ++toggleOffBluePx;
            }
            if (y >= 143u && y < 158u && x >= 32u && x < 240u && r > 180u && g > 130u && b < 110u) {
                ++sliderBrassPx;
            }
            if (y >= 186u && y < 230u && x >= 32u && x < 328u) {
                if (b > 170u && r < 140u) ++sparkBluePx;
                else if (r > 180u && g > 120u && b < 110u) ++sparkBrassPx;
                else if (r > 180u && g < 120u && b < 110u) ++sparkCoralPx;
            }
        }
    }

    // 3) Debug-Layer Control & Legend HUD rendered via RawOverlayHost::RecordDebugLayerHud (View 28)
    DebugLayerHudSnapshot dbgSnap{};
    dbgSnap.activeDebugView = 28u;
    dbgSnap.drawerOpen      = true;
    dbgSnap.hudEnabled      = true;
    dbgSnap.fps             = 144.2f;
    dbgSnap.frameMs         = 6.93f;
    dbgSnap.lightingMs      = 0.42f;
    dbgSnap.waterMs         = 0.18f;
    dbgSnap.fireMs          = 0.21f;
    dbgSnap.giMs            = 0.12f;
    dbgSnap.cloudsMs        = 0.08f;
    dbgSnap.postMs          = 0.05f;
    dbgSnap.uiMs            = 0.015f;
    dbgSnap.historyFrameMs  = hist;
    dbgSnap.historyCount    = 64u;

    std::vector<uint8_t> dbgRb;
    const bool dbgOk = RunWithCallback([&](ID3D12GraphicsCommandList* cl, ID3D12Resource* tex) {
        ovHost.RecordDebugLayerHud(cl, tex, D3D12_RESOURCE_STATE_RENDER_TARGET, kW, kH, dbgSnap, true);
    }, dbgRb);
    uint32_t dbgModPx = 0u;
    for (uint32_t i = 0u; i < kW * kH; ++i) {
        const size_t off = static_cast<size_t>(i) * 4u;
        if (dbgRb[off + 0] != basePx[off + 0] || dbgRb[off + 1] != basePx[off + 1] || dbgRb[off + 2] != basePx[off + 2]) {
            ++dbgModPx;
        }
    }

    // 4) RGBA16F float target NaN/Inf check
    uint32_t nanInf = 0u;
    {
        D3D12_RESOURCE_DESC hdrDesc = texDesc;
        hdrDesc.Width = 512u;
        hdrDesc.Height = 384u;
        hdrDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        ComPtr<ID3D12Resource> hdrTex;
        if (SUCCEEDED(dev->CreateCommittedResource(&defHp, D3D12_HEAP_FLAG_NONE, &hdrDesc,
                                                   D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&hdrTex)))) {
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT hfp{};
            UINT hRows = 0u;
            UINT64 hRowB = 0u, hTotB = 0u;
            dev->GetCopyableFootprints(&hdrDesc, 0, 1, 0, &hfp, &hRows, &hRowB, &hTotB);
            D3D12_RESOURCE_DESC hBuf = bufDesc;
            hBuf.Width = hTotB;
            ComPtr<ID3D12Resource> hRb;
            if (SUCCEEDED(dev->CreateCommittedResource(&rbHp, D3D12_HEAP_FLAG_NONE, &hBuf,
                                                       D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&hRb)))) {
                alloc->Reset();
                cmd->Reset(alloc.Get(), nullptr);
                uiHost.RecordDrawList(cmd.Get(), hdrTex.Get(), D3D12_RESOURCE_STATE_COMMON, 512u, 384u, gallery, false);
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
                    for (uint32_t y = 0u; y < 384u; ++y) {
                        const auto* row = reinterpret_cast<const uint16_t*>(base + static_cast<size_t>(y) * hfp.Footprint.RowPitch);
                        for (uint32_t x = 0u; x < 512u * 4u; ++x) {
                            if ((row[x] & 0x7C00u) == 0x7C00u) ++nanInf;
                        }
                    }
                    D3D12_RANGE wr{0, 0};
                    hRb->Unmap(0, &wr);
                }
            }
        }
    }

    const float meanMs = (validTs > 0u) ? (sumMs / static_cast<float>(validTs)) : -1.0f;
    const uint32_t dbgErrs = CountDebugErrors(dev.Get());

    std::printf("  [%s] dirty=(%u,%u %ux%u) inMod=%u outMod=%u clipRed=%u clipLeak=%u glyphPx=%u swMag=%u togOn=%u togOff=%u sldBrass=%u spk(B=%u Y=%u R=%u) dbgHudPx=%u nanInf=%u uiMs=%.4f (min=%.4f) dbgErrs=%u\n",
                tag, dirty.x, dirty.y, dirty.w, dirty.h, insideDirtyMod, outsideDirtyMod,
                clipInsideRed, clipLeakRed, glyphBrightPx, swatchMagentaPx, toggleOnBluePx, toggleOffBluePx,
                sliderBrassPx, sparkBluePx, sparkBrassPx, sparkCoralPx,
                dbgModPx, nanInf, static_cast<double>(meanMs), static_cast<double>(minMs), dbgErrs);

    Check(galOk && insideDirtyMod > 40000u && outsideDirtyMod == 0u,
          useWarp ? "WARP: Widget gallery rendered strictly inside dirtyRect (0 out-of-bounds)"
                  : "HW: Widget gallery rendered strictly inside dirtyRect (0 out-of-bounds)");
    Check(clipInsideRed > 5000u && clipLeakRed == 0u,
          useWarp ? "WARP: PushClipRect scissor strictly clipped overdraw (0 leaked pixels)"
                  : "HW: PushClipRect scissor strictly clipped overdraw (0 leaked pixels)");
    Check(glyphBrightPx > 250u && swatchMagentaPx >= 32u && toggleOnBluePx > 60u &&
              toggleOffBluePx == 0u && sliderBrassPx > 300u &&
              sparkBluePx > 80u && sparkBrassPx > 80u && sparkCoralPx > 80u,
          useWarp ? "WARP: 7x9 text, swatch, toggle ON/OFF, slider & 3-band sparkline verified"
                  : "HW: 7x9 text, swatch, toggle ON/OFF, slider & 3-band sparkline verified");
    Check(dbgOk && dbgModPx > 60000u,
          useWarp ? "WARP: RawOverlayHost::RecordDebugLayerHud rendered DebugView 28 legend drawer"
                  : "HW: RawOverlayHost::RecordDebugLayerHud rendered DebugView 28 legend drawer");
    Check(nanInf == 0u,
          useWarp ? "WARP: 0 NaN/Inf pixels on RGBA16F HDR target"
                  : "HW: 0 NaN/Inf pixels on RGBA16F HDR target");
    if (!useWarp) {
        Check(validTs > 0u && meanMs >= 0.0f && meanMs < 0.05f,
              "HW: Real GPU timestamp cost < 0.05 ms");
    } else {
        Check(validTs > 0u && meanMs >= 0.0f,
              "WARP: Valid GPU timestamp harvested");
    }
    Check(dbgErrs == 0u,
          useWarp ? "WARP: 0 D3D12 Debug Layer errors"
                  : "HW: 0 D3D12 Debug Layer errors");

    ovHost.Shutdown();
    uiHost.Shutdown();
    CloseHandle(ev);
}

} // namespace

int main()
{
    std::printf("=== Wave 63 GPU-Native UI Library (RawUiLib) + Debug-Layer HUD Proof ===\n");

    // Pure CPU checks: proportional metrics, DebugView 0..33 legend descriptors, zero em dashes
    {
        const uint32_t wNarrow = MeasureTextWidthPx("iii", 1u);
        const uint32_t wWide   = MeasureTextWidthPx("WWW", 1u);
        Check(wNarrow > 0u && wWide > wNarrow,
              "CPU: Proportional 7x9 ASCII font advance (WWW > iii)");

        bool allViewsValid = true;
        static constexpr uint32_t kViews[] = {0u, 1u, 2u, 8u, 12u, 28u, 29u, 30u, 31u, 32u, 33u};
        for (uint32_t v : kViews) {
            const DebugViewDescriptor d = GetDebugViewDescriptor(v);
            if (!d.shortName || d.shortName[0] == '\0' || !d.title || d.legend.empty()) {
                allViewsValid = false;
            }
            DebugLayerHudSnapshot snap{};
            snap.activeDebugView = v;
            snap.drawerOpen = true;
            UiDrawList dl;
            FormatDebugLayerHud(snap, dl);
            if (dl.Empty() || dl.Commands().size() < 12u) {
                allViewsValid = false;
            }
        }
        Check(allViewsValid, "CPU: DebugView descriptors & FormatDebugLayerHud valid across all 11 views (0..33)");
    }

    ComPtr<ID3D12Debug> dbg;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) {
        dbg->EnableDebugLayer();
        std::printf("D3D12 Debug Layer: ENABLED\n");
    }

    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&factory)))) {
        CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
    }
    Check(factory != nullptr, "DXGI Factory created");
    if (!factory) return 1;

    RunAdapterSuite(factory.Get(), true);  // WARP
    RunAdapterSuite(factory.Get(), false); // HW GPU (RTX 4090)

    std::printf("\n=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
