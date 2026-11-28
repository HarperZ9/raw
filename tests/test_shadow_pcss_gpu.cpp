// test_shadow_pcss_gpu.cpp: Standalone DX12 GPU Verification Suite for Wave 52 Variable-Penumbra PCSS
// Contact-Hardening Shadow Solver & World-Space Texel-Snapped Cascade Stabilizer.
// Original clean-room implementation, MIT License.
//
// Runs on BOTH Hardware GPU (RTX 4090) and WARP (--warp supported) with D3D12 Debug Layer ENABLED:
//   1. Pure CPU BuildTexelSnappedCascade texel-grid invariance under arbitrary sub-texel camera jitter.
//   2. Synthetic caster-over-ground GPU PCSS proof (960x540 output, 1024x1024 shadow map):
//      - Open sunlit ground -> visibility == 1.0
//      - Deep umbra -> visibility == 0.0
//      - Contact hardening: far-from-caster penumbra > 2.0x near-caster contact penumbra
//      - Sky pixels (depth >= 0.99998) -> visibility == 1.0, penumbra == 0.0
//      - Zero NaN/Inf across all pixels
//      - Real RTX 4090 GPU cost < 0.15 ms, d3d12DebugErrors == 0.

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

#include "RawShadowPcssHost.h"

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

void MulRowVec4(const float v[4], const float m[16], float out[4])
{
    for (int c = 0; c < 4; ++c) {
        out[c] = v[0] * m[0 * 4 + c] + v[1] * m[1 * 4 + c] + v[2] * m[2 * 4 + c] + v[3] * m[3 * 4 + c];
    }
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

bool UploadR32Tex(ID3D12Device* dev, ID3D12CommandQueue* q, ID3D12CommandAllocator* alloc,
                  ID3D12GraphicsCommandList* cmd, ID3D12Fence* fence, uint64_t& fv, HANDLE ev,
                  uint32_t w, uint32_t h, const std::vector<float>& data, ComPtr<ID3D12Resource>& outTex)
{
    D3D12_HEAP_PROPERTIES hpDef{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC td{};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = w; td.Height = h; td.DepthOrArraySize = 1; td.MipLevels = 1;
    td.Format = DXGI_FORMAT_R32_FLOAT; td.SampleDesc.Count = 1;
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
    for (uint32_t y = 0; y < h; ++y) {
        std::memcpy(dst + static_cast<size_t>(y) * fp.Footprint.RowPitch,
                    data.data() + static_cast<size_t>(y) * w,
                    static_cast<size_t>(w) * sizeof(float));
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

void RunCpuTexelSnapTest()
{
    const float sunDir[3] = {0.42f, 0.31f, 0.853f};
    const float baseTarget[3] = {100.0f, -50.0f, 12.0f};
    constexpr uint32_t kSmRes = 2048u;
    const TexelSnappedCascade baseC = BuildTexelSnappedCascade(baseTarget, sunDir, 64.0f, 256.0f, kSmRes);

    // Fixed world probe point
    const float worldPt[4] = {108.37f, -43.91f, 10.25f, 1.0f};
    float clip0[4]{};
    MulRowVec4(worldPt, baseC.lightViewProj, clip0);
    const float u0 = (clip0[0] * 0.5f + 0.5f) * static_cast<float>(kSmRes);
    const float v0 = (0.5f - clip0[1] * 0.5f) * static_cast<float>(kSmRes);
    const float fracU0 = u0 - std::floor(u0);
    const float fracV0 = v0 - std::floor(v0);

    float maxFracDrift = 0.0f;
    for (int i = 0; i < 16; ++i) {
        const float jx = static_cast<float>(i) * 0.0173f - 0.13f;
        const float jy = static_cast<float>(i) * 0.0291f + 0.07f;
        const float jz = static_cast<float>(i) * 0.0119f - 0.04f;
        const float jittered[3] = {baseTarget[0] + jx, baseTarget[1] + jy, baseTarget[2] + jz};
        const TexelSnappedCascade c = BuildTexelSnappedCascade(jittered, sunDir, 64.0f, 256.0f, kSmRes);

        float clip[4]{};
        MulRowVec4(worldPt, c.lightViewProj, clip);
        const float u = (clip[0] * 0.5f + 0.5f) * static_cast<float>(kSmRes);
        const float v = (0.5f - clip[1] * 0.5f) * static_cast<float>(kSmRes);
        const float du = std::fabs((u - std::floor(u)) - fracU0);
        const float dv = std::fabs((v - std::floor(v)) - fracV0);
        const float wrapDu = std::min(du, 1.0f - du);
        const float wrapDv = std::min(dv, 1.0f - dv);
        maxFracDrift = std::max(maxFracDrift, std::max(wrapDu, wrapDv));
    }
    std::printf("  [CPU] Texel-Snapped Cascade sub-texel drift across 16 jitters: maxFracDrift=%.6f texels (texelSize=%.4fm)\n",
                static_cast<double>(maxFracDrift), static_cast<double>(baseC.texelWorldSize));
    Check(maxFracDrift < 1.0e-3f && baseC.texelWorldSize > 0.0f,
          "CPU: BuildTexelSnappedCascade exact texel-grid invariance under sub-texel camera jitter");
}

void RunAdapterSuite(IDXGIFactory6* factory, bool useWarp)
{
    const char* tag = useWarp ? "WARP" : "HW-GPU";
    std::printf("\n--- Running GPU Variable-Penumbra PCSS Shadow Suite [%s] ---\n", tag);

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

    RawShadowPcssHost host;
    std::string initErr;
    const bool initOk = host.Init(dev.Get(), {}, &initErr);
    if (!initOk) std::printf("  Init error: %s\n", initErr.c_str());
    Check(initOk && host.Ready(), useWarp ? "WARP: RawShadowPcssHost::Init" : "HW: RawShadowPcssHost::Init");
    if (!initOk) {
        CloseHandle(ev);
        return;
    }
    host.SetTimestampFrequency(tsFreq);

    // ------------------------------------------------------------------------
    // Synthetic Top-Down Ground Plane + Two Shadow Casters at Distinct Heights
    //   - Output resolution: 960x540 (RG16F)
    //   - Shadow map: 1024x1024 (R32F), overhead sun (0, 0, 1), radius=32m, depthSpan=64m
    //   - Ground plane at worldZ = 0 -> lightDepth = 0.50
    //   - Top 40 rows of SceneDepth = 1.0f (sky)
    //   - Caster A (LEFT, x in [-16..-6], y in [-5..+5]): height = 0.8m above ground -> lightDepth = 0.4875 (CONTACT!)
    //   - Caster B (RIGHT, x in [+6..+16], y in [-5..+5]): height = 22.0m above ground -> lightDepth = 0.15625 (SOFT FAR!)
    // ------------------------------------------------------------------------
    constexpr uint32_t kW = 960u;
    constexpr uint32_t kH = 540u;
    constexpr uint32_t kSmRes = 1024u;

    const float targetW[3] = {0.0f, 0.0f, 0.0f};
    const float sunDir[3]  = {0.0f, 0.0f, 1.0f};
    const TexelSnappedCascade cascade = BuildTexelSnappedCascade(targetW, sunDir, 32.0f, 64.0f, kSmRes);

    // Build simple top-down ortho invViewProj mapping NDC (x,y,depth,1) -> world (x*24, y*13.5, (0.5 - depth)*40)
    // So depth = 0.50 -> worldZ = 0.0 exactly across all non-sky pixels!
    ShadowPcssParams params{};
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

    // Populate SceneDepth: top 40 rows = sky (1.0f), rest = ground plane (0.5f -> worldZ = 0.0)
    std::vector<float> sceneDepthData(static_cast<size_t>(kW) * kH, 0.5f);
    for (uint32_t y = 0u; y < 40u; ++y) {
        for (uint32_t x = 0u; x < kW; ++x) {
            sceneDepthData[static_cast<size_t>(y) * kW + x] = 1.0f;
        }
    }

    // Populate ShadowMap: default ground depth = 0.50; project Caster A & Caster B world footprints cleanly
    std::vector<float> shadowMapData(static_cast<size_t>(kSmRes) * kSmRes, 0.50f);
    auto RasterShadowBox = [&](float minX, float maxX, float minY, float maxY, float worldHeightZ) {
        // Compute exact light-space depth for worldZ = worldHeightZ
        const float pt[4] = {0.5f * (minX + maxX), 0.5f * (minY + maxY), worldHeightZ, 1.0f};
        float lc[4]{};
        MulRowVec4(pt, cascade.lightViewProj, lc);
        const float casterZ = lc[2];

        // Project all 4 XY corners to UV bounds
        float uMin = 1.0f, uMax = 0.0f, vMin = 1.0f, vMax = 0.0f;
        const float xs[2] = {minX, maxX};
        const float ys[2] = {minY, maxY};
        for (int ix = 0; ix < 2; ++ix) {
            for (int iy = 0; iy < 2; ++iy) {
                const float cPt[4] = {xs[ix], ys[iy], worldHeightZ, 1.0f};
                float cLc[4]{};
                MulRowVec4(cPt, cascade.lightViewProj, cLc);
                const float u = cLc[0] * 0.5f + 0.5f;
                const float v = 0.5f - cLc[1] * 0.5f;
                uMin = std::min(uMin, u); uMax = std::max(uMax, u);
                vMin = std::min(vMin, v); vMax = std::max(vMax, v);
            }
        }
        const uint32_t x0 = static_cast<uint32_t>(std::clamp(std::floor(uMin * kSmRes), 0.0f, static_cast<float>(kSmRes - 1)));
        const uint32_t x1 = static_cast<uint32_t>(std::clamp(std::ceil(uMax * kSmRes), 0.0f, static_cast<float>(kSmRes - 1)));
        const uint32_t y0 = static_cast<uint32_t>(std::clamp(std::floor(vMin * kSmRes), 0.0f, static_cast<float>(kSmRes - 1)));
        const uint32_t y1 = static_cast<uint32_t>(std::clamp(std::ceil(vMax * kSmRes), 0.0f, static_cast<float>(kSmRes - 1)));
        for (uint32_t y = y0; y <= y1; ++y) {
            for (uint32_t x = x0; x <= x1; ++x) {
                float& dst = shadowMapData[static_cast<size_t>(y) * kSmRes + x];
                dst = std::min(dst, casterZ);
            }
        }
    };
    RasterShadowBox(-16.0f, -6.0f, -5.0f, 5.0f, 0.8f);  // Caster A: near-ground contact caster
    RasterShadowBox(  6.0f, 16.0f, -5.0f, 5.0f, 22.0f); // Caster B: tall distant-penumbra spire

    ComPtr<ID3D12Resource> depthTex, smTex, shadowOutTex, rbBuf;
    if (!UploadR32Tex(dev.Get(), queue.Get(), alloc.Get(), cmd.Get(), fence.Get(), fv, ev, kW, kH, sceneDepthData, depthTex) ||
        !UploadR32Tex(dev.Get(), queue.Get(), alloc.Get(), cmd.Get(), fence.Get(), fv, ev, kSmRes, kSmRes, shadowMapData, smTex)) {
        Check(false, useWarp ? "WARP: Upload depth/shadow textures" : "HW: Upload depth/shadow textures");
        CloseHandle(ev);
        return;
    }

    D3D12_HEAP_PROPERTIES hpDef{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC outDesc{};
    outDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    outDesc.Width = kW; outDesc.Height = kH; outDesc.DepthOrArraySize = 1; outDesc.MipLevels = 1;
    outDesc.Format = DXGI_FORMAT_R16G16_FLOAT; outDesc.SampleDesc.Count = 1;
    outDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(dev->CreateCommittedResource(&hpDef, D3D12_HEAP_FLAG_NONE, &outDesc,
                                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&shadowOutTex)))) {
        Check(false, useWarp ? "WARP: Create RG16F shadowOut" : "HW: Create RG16F shadowOut");
        CloseHandle(ev);
        return;
    }

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
        Check(false, useWarp ? "WARP: Create readback buffer" : "HW: Create readback buffer");
        CloseHandle(ev);
        return;
    }

    // Run 12 frames on HW (6 on WARP) so the 4-frame timestamp ring stabilizes cleanly
    const uint32_t iters = useWarp ? 6u : 14u;
    float sumMs = 0.0f, minMs = 1e9f;
    uint32_t timeSamples = 0u;
    bool execOk = true;
    for (uint32_t it = 0u; it < iters; ++it) {
        alloc->Reset();
        cmd->Reset(alloc.Get(), nullptr);
        params.frameJitter = static_cast<float>(it);
        execOk = execOk && host.Execute(cmd.Get(), depthTex.Get(), smTex.Get(), shadowOutTex.Get(), kW, kH, params);
        if (it + 1u == iters) {
            D3D12_RESOURCE_BARRIER toCopy{};
            toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            toCopy.Transition = {shadowOutTex.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE};
            cmd->ResourceBarrier(1, &toCopy);

            D3D12_TEXTURE_COPY_LOCATION dLoc{rbBuf.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}};
            dLoc.PlacedFootprint = outFp;
            D3D12_TEXTURE_COPY_LOCATION sLoc{shadowOutTex.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
            sLoc.SubresourceIndex = 0;
            cmd->CopyTextureRegion(&dLoc, 0, 0, 0, &sLoc, nullptr);

            D3D12_RESOURCE_BARRIER toUav = toCopy;
            std::swap(toUav.Transition.StateBefore, toUav.Transition.StateAfter);
            cmd->ResourceBarrier(1, &toUav);
        }
        cmd->Close();
        ID3D12CommandList* lists[1] = {cmd.Get()};
        queue->ExecuteCommandLists(1, lists);
        execOk = execOk && WaitForQueue(queue.Get(), fence.Get(), fv, ev);
        if (host.HasValidTiming()) {
            const float ms = host.GetLastPcssMs();
            sumMs += ms;
            minMs = std::min(minMs, ms);
            ++timeSamples;
        }
    }

    // Readback and verify all physical PCSS invariants
    std::vector<float> visImg(static_cast<size_t>(kW) * kH, 0.0f);
    std::vector<float> penImg(static_cast<size_t>(kW) * kH, 0.0f);
    uint32_t nanInfCount = 0u;
    {
        void* p = nullptr;
        D3D12_RANGE rr{0, static_cast<SIZE_T>(outTotB)};
        rbBuf->Map(0, &rr, &p);
        const auto* base = static_cast<const uint8_t*>(p) + outFp.Offset;
        for (uint32_t y = 0u; y < kH; ++y) {
            const auto* row = reinterpret_cast<const uint16_t*>(base + static_cast<size_t>(y) * outFp.Footprint.RowPitch);
            for (uint32_t x = 0u; x < kW; ++x) {
                const uint16_t hv = row[x * 2u + 0u];
                const uint16_t hp = row[x * 2u + 1u];
                if ((hv & 0x7C00u) == 0x7C00u || (hp & 0x7C00u) == 0x7C00u) ++nanInfCount;
                visImg[static_cast<size_t>(y) * kW + x] = HalfToFloat(hv);
                penImg[static_cast<size_t>(y) * kW + x] = HalfToFloat(hp);
            }
        }
        D3D12_RANGE wr{0, 0};
        rbBuf->Unmap(0, &wr);
    }

    auto SampleWorld = [&](float wx, float wy, float& outVis, float& outPen) {
        const float u = (wx / 24.0f) * 0.5f + 0.5f;
        const float v = 0.5f - (wy / 13.5f) * 0.5f;
        const uint32_t px = static_cast<uint32_t>(std::clamp(std::floor(u * kW), 0.0f, static_cast<float>(kW - 1)));
        const uint32_t py = static_cast<uint32_t>(std::clamp(std::floor(v * kH), 0.0f, static_cast<float>(kH - 1)));
        outVis = visImg[static_cast<size_t>(py) * kW + px];
        outPen = penImg[static_cast<size_t>(py) * kW + px];
    };

    // Sky check (row 10)
    float skyMinVis = 1.0f;
    for (uint32_t x = 0u; x < kW; ++x) {
        skyMinVis = std::min(skyMinVis, visImg[static_cast<size_t>(10u) * kW + x]);
    }

    // Open sunlit center (0, 0)
    float openVis = 0.0f, openPen = 0.0f;
    SampleWorld(0.0f, 0.0f, openVis, openPen);

    // Deep umbra centers under Caster A (-11, 0) and Caster B (+11, 0)
    float nearUmbraVis = 1.0f, nearUmbraPen = 0.0f;
    float farUmbraVis  = 1.0f, farUmbraPen  = 0.0f;
    SampleWorld(-11.0f, 0.0f, nearUmbraVis, nearUmbraPen);
    SampleWorld( 11.0f, 0.0f, farUmbraVis,  farUmbraPen);

    // Shadow edge penumbra probe bands along x = -6.0 (Caster A edge) and x = +6.0 (Caster B edge)
    float nearPenSum = 0.0f, farPenSum = 0.0f;
    uint32_t nearPenCnt = 0u, farPenCnt = 0u, farSoftBandPx = 0u;
    for (uint32_t y = 220u; y < 320u; ++y) {
        for (uint32_t x = 0u; x < kW; ++x) {
            const float wx = ((static_cast<float>(x) + 0.5f) / kW * 2.0f - 1.0f) * 24.0f;
            const float v  = visImg[static_cast<size_t>(y) * kW + x];
            const float pn = penImg[static_cast<size_t>(y) * kW + x];
            if (std::fabs(wx - (-6.0f)) < 0.6f && pn > 1.0e-4f) {
                nearPenSum += pn; ++nearPenCnt;
            }
            if (std::fabs(wx - 6.0f) < 1.2f && pn > 1.0e-4f) {
                farPenSum += pn; ++farPenCnt;
                if (v > 0.05f && v < 0.95f) ++farSoftBandPx;
            }
        }
    }
    const float nearEdgePen = (nearPenCnt > 0u) ? (nearPenSum / static_cast<float>(nearPenCnt)) : nearUmbraPen;
    const float farEdgePen  = (farPenCnt > 0u)  ? (farPenSum / static_cast<float>(farPenCnt))  : farUmbraPen;
    const float penRatio    = (nearEdgePen > 1.0e-5f) ? (farEdgePen / nearEdgePen) : 0.0f;

    const float meanMs = (timeSamples > 0u) ? (sumMs / static_cast<float>(timeSamples)) : -1.0f;
    const uint32_t dbgErrs = CountDebugErrors(dev.Get());

    std::printf("  [%s] 960x540 PCSS: openVis=%.3f nearUmbra=%.3f farUmbra=%.3f skyMin=%.3f nearPen=%.4f farPen=%.4f ratio=%.2fx softBandPx=%u nanInf=%u pcssMs=%.4f (min=%.4f) dbgErrs=%u\n",
                tag, static_cast<double>(openVis), static_cast<double>(nearUmbraVis), static_cast<double>(farUmbraVis),
                static_cast<double>(skyMinVis), static_cast<double>(nearEdgePen), static_cast<double>(farEdgePen),
                static_cast<double>(penRatio), farSoftBandPx, nanInfCount,
                static_cast<double>(meanMs), static_cast<double>(minMs), dbgErrs);

    Check(execOk && openVis >= 0.999f,
          useWarp ? "WARP: Open sunlit ground visibility == 1.0"
                  : "HW: Open sunlit ground visibility == 1.0");
    Check(nearUmbraVis <= 0.001f && farUmbraVis <= 0.001f,
          useWarp ? "WARP: Deep umbra under both casters visibility == 0.0"
                  : "HW: Deep umbra under both casters visibility == 0.0");
    Check(farEdgePen > 2.0f * nearEdgePen && farSoftBandPx > 20u,
          useWarp ? "WARP: Contact-hardening PCSS (farPenumbra > 2.0x nearPenumbra + soft penumbra band)"
                  : "HW: Contact-hardening PCSS (farPenumbra > 2.0x nearPenumbra + soft penumbra band)");
    Check(skyMinVis >= 0.999f && nanInfCount == 0u,
          useWarp ? "WARP: Sky pixels == 1.0 & 0 NaN/Inf"
                  : "HW: Sky pixels == 1.0 & 0 NaN/Inf");
    if (!useWarp) {
        Check(timeSamples > 0u && meanMs >= 0.0f && meanMs < 0.15f,
              "HW: Real RTX 4090 GPU PCSS cost < 0.15 ms @ 960x540");
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

    std::printf("=== Wave 52 GPU Variable-Penumbra PCSS Shadow & Texel-Snapped Cascade Suite ===\n");
    RunCpuTexelSnapTest();

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
