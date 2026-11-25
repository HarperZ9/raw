// test_radiance_cascades.cpp: Standalone Hardware GPU + WARP + D3D12 Debug Layer Verification
// for RAW's 4-Cascade Penumbra-Exact Radiance Cascades Probe GI Compute Solver.
//
// Synthetic room scene (inspired by mytinodev's fire + Radiance Cascades demo):
//   - Bright incandescent fire emitter on the left: center (52, 128), radius 16, HDR orange emission
//   - Solid opaque stone pillar in the center: x in [120..144], y in [96..160], alpha = 1.0, emission = 0
// Verifies with D3D12 Debug Layer enabled (0 errors/corruption):
//   1. Open side (between fire and pillar) lit brightly (openIrrad >> 1.0)
//   2. Deep umbra directly behind pillar strongly shadowed (umbraIrrad < openIrrad * 0.20)
//   3. Smooth monotonic penumbra transition across the shadow boundary (zero stochastic fireflies)
//   4. Zero NaN / Inf across all 256 x 256 x 4 output floats
//   5. Strict linear energy scaling when emitter radiance doubles (ratio == 2.00 +- 0.03)
//
// Original clean-room implementation, MIT License.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "../src/d3d12_bridge/RawRadianceHost.h"

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

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

int g_failCount = 0;

void Check(bool ok, const char* label)
{
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) {
        ++g_failCount;
    }
}

uint16_t FloatToHalf(float f)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &f, sizeof(bits));
    const uint32_t sign = (bits >> 16) & 0x8000u;
    const int32_t  exp  = static_cast<int32_t>((bits >> 23) & 0xFFu) - 127 + 15;
    const uint32_t mant = bits & 0x007FFFFFu;
    if (exp <= 0) {
        return static_cast<uint16_t>(sign);
    }
    if (exp >= 31) {
        return static_cast<uint16_t>(sign | 0x7C00u);
    }
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | (mant >> 13));
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

struct RcGpuRig {
    ComPtr<ID3D12Device>              dev;
    ComPtr<ID3D12InfoQueue>           infoQueue;
    ComPtr<ID3D12CommandQueue>        queue;
    ComPtr<ID3D12CommandAllocator>    alloc;
    ComPtr<ID3D12GraphicsCommandList> cmd;
    ComPtr<ID3D12Fence>               fence;
    HANDLE                            fenceEvent = nullptr;
    uint64_t                          fenceVal   = 0;
    bool                              isWarp     = false;
    bool                              debugLayer = false;

    ~RcGpuRig()
    {
        if (fenceEvent) {
            CloseHandle(fenceEvent);
            fenceEvent = nullptr;
        }
    }

    bool Init(bool preferWarp)
    {
        ComPtr<ID3D12Debug> dbg;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) {
            dbg->EnableDebugLayer();
            debugLayer = true;
        }

        ComPtr<IDXGIFactory4> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return false;

        if (!preferWarp) {
            if (SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&dev)))) {
                D3D12_FEATURE_DATA_SHADER_MODEL sm{D3D_SHADER_MODEL_6_6};
                if (FAILED(dev->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof sm)) ||
                    sm.HighestShaderModel < D3D_SHADER_MODEL_6_6) {
                    dev.Reset();
                }
            }
        }
        if (!dev) {
            ComPtr<IDXGIAdapter> warp;
            if (FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))) ||
                FAILED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&dev)))) {
                return false;
            }
            isWarp = true;
        }

        if (debugLayer) {
            dev.As(&infoQueue);
        }

        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue))) ||
            FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))) ||
            FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&cmd))) ||
            FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
            return false;
        }
        cmd->Close();
        fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        return fenceEvent != nullptr;
    }

    bool BeginFrame()
    {
        return SUCCEEDED(alloc->Reset()) && SUCCEEDED(cmd->Reset(alloc.Get(), nullptr));
    }

    bool FlushAndWait()
    {
        if (FAILED(cmd->Close())) return false;
        ID3D12CommandList* lists[1] = {cmd.Get()};
        queue->ExecuteCommandLists(1, lists);
        ++fenceVal;
        if (FAILED(queue->Signal(fence.Get(), fenceVal))) return false;
        if (fence->GetCompletedValue() < fenceVal) {
            if (FAILED(fence->SetEventOnCompletion(fenceVal, fenceEvent))) return false;
            WaitForSingleObject(fenceEvent, 15000);
        }
        return fence->GetCompletedValue() >= fenceVal;
    }

    uint32_t CountDebugErrors()
    {
        if (!infoQueue) return 0u;
        uint32_t errCount = 0u;
        const UINT64 n = infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
        for (UINT64 i = 0; i < n; ++i) {
            SIZE_T len = 0;
            infoQueue->GetMessage(i, nullptr, &len);
            if (len == 0) continue;
            std::vector<uint8_t> buf(len);
            auto* msg = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
            if (SUCCEEDED(infoQueue->GetMessage(i, msg, &len))) {
                if (msg->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION ||
                    msg->Severity == D3D12_MESSAGE_SEVERITY_ERROR) {
                    std::printf("       [D3D12 ERROR] %s\n", msg->pDescription ? msg->pDescription : "(null)");
                    ++errCount;
                }
            }
        }
        infoQueue->ClearStoredMessages();
        return errCount;
    }

    bool UploadSceneTexture(ComPtr<ID3D12Resource>& tex, uint32_t dim, const std::vector<float>& rgbaFloat)
    {
        D3D12_HEAP_PROPERTIES hpDef{};
        hpDef.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = dim;
        rd.Height = dim;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        rd.SampleDesc.Count = 1;
        if (!tex) {
            if (FAILED(dev->CreateCommittedResource(&hpDef, D3D12_HEAP_FLAG_NONE, &rd,
                                                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex)))) {
                return false;
            }
        } else {
            if (!BeginFrame()) return false;
            D3D12_RESOURCE_BARRIER b{};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource = tex.Get();
            b.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmd->ResourceBarrier(1, &b);
            if (!FlushAndWait()) return false;
        }

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        UINT numRows = 0;
        UINT64 rowBytes = 0, totalBytes = 0;
        dev->GetCopyableFootprints(&rd, 0, 1, 0, &fp, &numRows, &rowBytes, &totalBytes);

        D3D12_HEAP_PROPERTIES hpUp{};
        hpUp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC ubd{};
        ubd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        ubd.Width = totalBytes;
        ubd.Height = 1;
        ubd.DepthOrArraySize = 1;
        ubd.MipLevels = 1;
        ubd.SampleDesc.Count = 1;
        ubd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ComPtr<ID3D12Resource> upBuf;
        if (FAILED(dev->CreateCommittedResource(&hpUp, D3D12_HEAP_FLAG_NONE, &ubd,
                                                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upBuf)))) {
            return false;
        }
        void* mapped = nullptr;
        D3D12_RANGE rr{0, 0};
        if (FAILED(upBuf->Map(0, &rr, &mapped))) return false;
        auto* dstBase = static_cast<uint8_t*>(mapped) + fp.Offset;
        for (uint32_t y = 0; y < dim; ++y) {
            auto* dstRow = reinterpret_cast<uint16_t*>(dstBase + static_cast<size_t>(y) * fp.Footprint.RowPitch);
            for (uint32_t x = 0; x < dim; ++x) {
                const size_t src = (static_cast<size_t>(y) * dim + x) * 4u;
                dstRow[x * 4 + 0] = FloatToHalf(rgbaFloat[src + 0]);
                dstRow[x * 4 + 1] = FloatToHalf(rgbaFloat[src + 1]);
                dstRow[x * 4 + 2] = FloatToHalf(rgbaFloat[src + 2]);
                dstRow[x * 4 + 3] = FloatToHalf(rgbaFloat[src + 3]);
            }
        }
        upBuf->Unmap(0, nullptr);

        if (!BeginFrame()) return false;
        D3D12_TEXTURE_COPY_LOCATION dstLoc{};
        dstLoc.pResource = tex.Get();
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION srcLoc{};
        srcLoc.pResource = upBuf.Get();
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint = fp;
        cmd->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = tex.Get();
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &b);
        return FlushAndWait();
    }

    bool ReadbackRgba16Rect(ID3D12Resource* tex, D3D12_RESOURCE_STATES curState, uint32_t w, uint32_t h, std::vector<float>& outRgba)
    {
        const D3D12_RESOURCE_DESC rd = tex->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        UINT numRows = 0;
        UINT64 rowBytes = 0, totalBytes = 0;
        dev->GetCopyableFootprints(&rd, 0, 1, 0, &fp, &numRows, &rowBytes, &totalBytes);

        D3D12_HEAP_PROPERTIES hpRb{};
        hpRb.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rbd{};
        rbd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rbd.Width = totalBytes;
        rbd.Height = 1;
        rbd.DepthOrArraySize = 1;
        rbd.MipLevels = 1;
        rbd.SampleDesc.Count = 1;
        rbd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ComPtr<ID3D12Resource> rbBuf;
        if (FAILED(dev->CreateCommittedResource(&hpRb, D3D12_HEAP_FLAG_NONE, &rbd,
                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&rbBuf)))) {
            return false;
        }

        if (!BeginFrame()) return false;
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = tex;
        b.Transition.StateBefore = curState;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &b);

        D3D12_TEXTURE_COPY_LOCATION srcLoc{};
        srcLoc.pResource = tex;
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcLoc.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION dstLoc{};
        dstLoc.pResource = rbBuf.Get();
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dstLoc.PlacedFootprint = fp;
        cmd->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        b.Transition.StateAfter = curState;
        cmd->ResourceBarrier(1, &b);
        if (!FlushAndWait()) return false;

        void* mapped = nullptr;
        D3D12_RANGE rr{0, static_cast<SIZE_T>(totalBytes)};
        if (FAILED(rbBuf->Map(0, &rr, &mapped))) return false;
        const auto* base = static_cast<const uint8_t*>(mapped) + fp.Offset;
        outRgba.resize(static_cast<size_t>(w) * h * 4u);
        for (uint32_t y = 0; y < h; ++y) {
            const auto* row = reinterpret_cast<const uint16_t*>(base + static_cast<size_t>(y) * fp.Footprint.RowPitch);
            for (uint32_t x = 0; x < w; ++x) {
                const size_t dst = (static_cast<size_t>(y) * w + x) * 4u;
                outRgba[dst + 0] = HalfToFloat(row[x * 4 + 0]);
                outRgba[dst + 1] = HalfToFloat(row[x * 4 + 1]);
                outRgba[dst + 2] = HalfToFloat(row[x * 4 + 2]);
                outRgba[dst + 3] = HalfToFloat(row[x * 4 + 3]);
            }
        }
        D3D12_RANGE wr{0, 0};
        rbBuf->Unmap(0, &wr);
        return true;
    }

    bool ReadbackRgba16(ID3D12Resource* tex, D3D12_RESOURCE_STATES curState, uint32_t dim, std::vector<float>& outRgba)
    {
        return ReadbackRgba16Rect(tex, curState, dim, dim, outRgba);
    }

    bool CreateAndUploadScreenTargets(uint32_t w, uint32_t h,
                                      const std::vector<float>& rgbaFloat,
                                      const std::vector<float>& depthFloat,
                                      ComPtr<ID3D12Resource>& outColor,
                                      ComPtr<ID3D12Resource>& outDepth)
    {
        D3D12_HEAP_PROPERTIES hpDef{};
        hpDef.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_HEAP_PROPERTIES hpUp{};
        hpUp.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC cRd{};
        cRd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        cRd.Width = w; cRd.Height = h; cRd.DepthOrArraySize = 1; cRd.MipLevels = 1;
        cRd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        cRd.SampleDesc.Count = 1;
        cRd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        D3D12_RESOURCE_DESC dRd{};
        dRd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        dRd.Width = w; dRd.Height = h; dRd.DepthOrArraySize = 1; dRd.MipLevels = 1;
        dRd.Format = DXGI_FORMAT_R32_FLOAT;
        dRd.SampleDesc.Count = 1;

        if (FAILED(dev->CreateCommittedResource(&hpDef, D3D12_HEAP_FLAG_NONE, &cRd,
                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&outColor))) ||
            FAILED(dev->CreateCommittedResource(&hpDef, D3D12_HEAP_FLAG_NONE, &dRd,
                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&outDepth)))) {
            return false;
        }

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT cFp{}, dFp{};
        UINT cRows = 0, dRows = 0;
        UINT64 cRowB = 0, cTot = 0, dRowB = 0, dTot = 0;
        dev->GetCopyableFootprints(&cRd, 0, 1, 0, &cFp, &cRows, &cRowB, &cTot);
        dev->GetCopyableFootprints(&dRd, 0, 1, 0, &dFp, &dRows, &dRowB, &dTot);

        D3D12_RESOURCE_DESC uRd{};
        uRd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        uRd.Height = 1; uRd.DepthOrArraySize = 1; uRd.MipLevels = 1; uRd.SampleDesc.Count = 1;
        uRd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ComPtr<ID3D12Resource> cUp, dUp;
        uRd.Width = cTot;
        if (FAILED(dev->CreateCommittedResource(&hpUp, D3D12_HEAP_FLAG_NONE, &uRd,
                                                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&cUp)))) return false;
        uRd.Width = dTot;
        if (FAILED(dev->CreateCommittedResource(&hpUp, D3D12_HEAP_FLAG_NONE, &uRd,
                                                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&dUp)))) return false;

        D3D12_RANGE rr{0, 0};
        void* cp = nullptr;
        if (FAILED(cUp->Map(0, &rr, &cp))) return false;
        auto* cDstBase = static_cast<uint8_t*>(cp) + cFp.Offset;
        for (uint32_t y = 0; y < h; ++y) {
            auto* row = reinterpret_cast<uint16_t*>(cDstBase + static_cast<size_t>(y) * cFp.Footprint.RowPitch);
            for (uint32_t x = 0; x < w; ++x) {
                const size_t s = (static_cast<size_t>(y) * w + x) * 4u;
                row[x * 4 + 0] = FloatToHalf(rgbaFloat[s + 0]);
                row[x * 4 + 1] = FloatToHalf(rgbaFloat[s + 1]);
                row[x * 4 + 2] = FloatToHalf(rgbaFloat[s + 2]);
                row[x * 4 + 3] = FloatToHalf(rgbaFloat[s + 3]);
            }
        }
        cUp->Unmap(0, nullptr);

        void* dp = nullptr;
        if (FAILED(dUp->Map(0, &rr, &dp))) return false;
        auto* dDstBase = static_cast<uint8_t*>(dp) + dFp.Offset;
        for (uint32_t y = 0; y < h; ++y) {
            std::memcpy(dDstBase + static_cast<size_t>(y) * dFp.Footprint.RowPitch,
                        depthFloat.data() + static_cast<size_t>(y) * w,
                        static_cast<size_t>(w) * sizeof(float));
        }
        dUp->Unmap(0, nullptr);

        if (!BeginFrame()) return false;
        D3D12_TEXTURE_COPY_LOCATION srcC{cUp.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}};
        srcC.PlacedFootprint = cFp;
        D3D12_TEXTURE_COPY_LOCATION dstC{outColor.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
        cmd->CopyTextureRegion(&dstC, 0, 0, 0, &srcC, nullptr);

        D3D12_TEXTURE_COPY_LOCATION srcD{dUp.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}};
        srcD.PlacedFootprint = dFp;
        D3D12_TEXTURE_COPY_LOCATION dstD{outDepth.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
        cmd->CopyTextureRegion(&dstD, 0, 0, 0, &srcD, nullptr);

        // Leave both colorInOut and sceneDepth in COMMON (exact contract of ExecuteScreenGI)
        D3D12_RESOURCE_BARRIER b[2]{};
        b[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b[0].Transition = {outColor.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON};
        b[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b[1].Transition = {outDepth.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON};
        cmd->ResourceBarrier(2, b);
        return FlushAndWait();
    }
};

// Generates the synthetic room scene: bright HDR incandescent fire on the left + opaque stone pillar in the center.
std::vector<float> BuildRoomScene(uint32_t dim, float emitterScale)
{
    std::vector<float> rgba(static_cast<size_t>(dim) * dim * 4u, 0.0f);
    for (uint32_t y = 0; y < dim; ++y) {
        for (uint32_t x = 0; x < dim; ++x) {
            const size_t o = (static_cast<size_t>(y) * dim + x) * 4u;
            const float fx = static_cast<float>(x) + 0.5f;
            const float fy = static_cast<float>(y) + 0.5f;

            // Left incandescent fire emitter: center (52, 128), radius 16 texels (bright HDR blackbody fire)
            const float dx = fx - 52.0f;
            const float dy = fy - 128.0f;
            const float dist = std::hypot(dx, dy);
            if (dist <= 16.0f) {
                const float core = 1.0f - (dist / 16.0f) * 0.35f;
                rgba[o + 0] = 120.0f * core * emitterScale;
                rgba[o + 1] =  45.0f * core * emitterScale;
                rgba[o + 2] =  11.0f * core * emitterScale;
                rgba[o + 3] =   1.0f;
                continue;
            }

            // Central solid opaque stone pillar: x in [120..144], y in [96..160]
            if (x >= 120u && x <= 144u && y >= 96u && y <= 160u) {
                rgba[o + 0] = 0.0f;
                rgba[o + 1] = 0.0f;
                rgba[o + 2] = 0.0f;
                rgba[o + 3] = 1.0f;
            }
        }
    }
    return rgba;
}

float SampleLum(const std::vector<float>& img, uint32_t dim, uint32_t x, uint32_t y)
{
    const size_t o = (static_cast<size_t>(y) * dim + x) * 4u;
    return 0.2126f * img[o + 0] + 0.7152f * img[o + 1] + 0.0722f * img[o + 2];
}

void RunRadianceCascadesSuite(bool preferWarp)
{
    RcGpuRig rig;
    if (!rig.Init(preferWarp)) {
        Check(false, "DX12 device + Debug Layer initialization succeeded");
        return;
    }
    std::printf("       Adapter: %s | DebugLayer: %s\n",
                rig.isWarp ? "WARP software rasterizer" : "Hardware GPU",
                rig.debugLayer ? "ON" : "OFF");

    RawRadianceHost host;
    std::string err;
    const bool initOk = host.Init(rig.dev.Get(), &err);
    if (!initOk) {
        std::printf("       RawRadianceHost::Init error: %s\n", err.c_str());
    }
    Check(initOk && host.Ready() && host.IrradianceSrv().ptr != 0,
          "RawRadianceHost::Init created 256x256 atlases, irradiance map, root sig & PSOs");
    if (!initOk) return;

    constexpr uint32_t kDim = RawRadianceHost::kAtlasRes;
    ComPtr<ID3D12Resource> sceneTex;
    RadianceCascadeParams params{};
    params.baseIntervalTexels = 3.5f;
    params.maxTraceTexels = 300.0f;
    params.opacityScale = 2.5f;
    params.irradianceScale = 1.0f;

    // --- Pass A: Baseline 1x emitter radiance ---
    const auto scene1x = BuildRoomScene(kDim, 1.0f);
    Check(rig.UploadSceneTexture(sceneTex, kDim, scene1x), "Uploaded 1x synthetic fire+pillar room scene");

    bool exec1Ok = rig.BeginFrame();
    if (exec1Ok) {
        host.ComputeGI(rig.cmd.Get(), sceneTex.Get(), params);
        exec1Ok = rig.FlushAndWait();
    }
    Check(exec1Ok && host.FrameIndex() == 1u, "ComputeGI executed full 4-cascade c=3->2->1->0->Resolve pass (1x)");

    std::vector<float> irrad1x;
    Check(rig.ReadbackRgba16(host.IrradianceResource(), host.IrradianceResourceState(), kDim, irrad1x),
          "Readback 256x256 RGBA16F diffuse GI irradiance map (1x)");

    bool allFinite = true;
    for (float v : irrad1x) {
        if (!std::isfinite(v) || v < -1.0e-4f) { allFinite = false; break; }
    }
    Check(allFinite, "Irradiance map contains zero NaN/Inf and strictly non-negative radiance");

    // Probe 1: Open side between fire (52,128) and pillar (120..144, 96..160) -> (92, 128)
    const float openLum = SampleLum(irrad1x, kDim, 92u, 128u);
    const size_t openOff = (static_cast<size_t>(128u) * kDim + 92u) * 4u;
    const float openR = irrad1x[openOff + 0], openG = irrad1x[openOff + 1], openB = irrad1x[openOff + 2];

    // Probe 2: Deep umbra directly behind opaque stone pillar -> (168, 128)
    const float umbraLum = SampleLum(irrad1x, kDim, 168u, 128u);

    // Probe 3: Open side far above pillar (unoccluded direct line-of-sight to fire) -> (168, 48)
    const float unoccludedFarLum = SampleLum(irrad1x, kDim, 168u, 48u);

    // Probe 4: Penumbra vertical scan along x = 168 from deep umbra (y = 128) outward into lit zone (y = 56)
    float prevPenLum = -1.0f;
    float maxLocalSpike = 0.0f;
    bool penumbraMonotonic = true;
    float penSamples[10] = {};
    for (int i = 0; i < 10; ++i) {
        const uint32_t py = static_cast<uint32_t>(128 - i * 8); // 128, 120, 112, 104, 96, 88, 80, 72, 64, 56
        const float L = SampleLum(irrad1x, kDim, 168u, py);
        penSamples[i] = L;
        if (prevPenLum >= 0.0f) {
            if (L + 0.02f < prevPenLum) {
                penumbraMonotonic = false;
            }
            maxLocalSpike = std::max(maxLocalSpike, std::fabs(L - prevPenLum));
        }
        prevPenLum = L;
    }

    // Check smooth zero-firefly spatial smoothness in open lit region around (92, 128)
    float openMin = 1.0e9f, openMax = -1.0e9f;
    for (uint32_t dy = 0; dy < 5u; ++dy) {
        for (uint32_t dx = 0; dx < 5u; ++dx) {
            const float L = SampleLum(irrad1x, kDim, 90u + dx, 126u + dy);
            openMin = std::min(openMin, L);
            openMax = std::max(openMax, L);
        }
    }
    const float openSpreadRatio = openMax / std::max(openMin, 1.0e-5f);

    std::printf("       1x Room GI: open(92,128)=%.3f RGB=(%.2f,%.2f,%.2f) | umbra(168,128)=%.4f | farLit(168,48)=%.3f\n",
                openLum, openR, openG, openB, umbraLum, unoccludedFarLum);
    std::printf("       Penumbra x=168 y=128..56: [%.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f]\n",
                penSamples[0], penSamples[1], penSamples[2], penSamples[3], penSamples[4],
                penSamples[5], penSamples[6], penSamples[7], penSamples[8], penSamples[9]);
    std::printf("       Open 5x5 smoothness ratio (max/min): %.3f\n", openSpreadRatio);

    Check(openLum > 2.0f && openR > openG && openG > openB,
          "Open side lit brightly (>2.0) with warm fire chromaticity (R > G > B)");
    Check(umbraLum < openLum * 0.20f && umbraLum < unoccludedFarLum * 0.25f,
          "Umbra directly behind opaque stone pillar strongly shadowed (<20% of open side)");
    Check(penumbraMonotonic && penSamples[9] > penSamples[0] * 4.0f && penSamples[5] > penSamples[0] && penSamples[5] < penSamples[9],
          "Smooth monotonic penumbra transition across shadow boundary");
    Check(openSpreadRatio < 1.35f,
          "Noise-free zero-firefly spatial coherence in open lit region");

    // --- Pass B: Strict Linear Energy Scaling (2x emitter radiance) ---
    const auto scene2x = BuildRoomScene(kDim, 2.0f);
    Check(rig.UploadSceneTexture(sceneTex, kDim, scene2x), "Uploaded 2x synthetic fire+pillar room scene");

    bool exec2Ok = rig.BeginFrame();
    if (exec2Ok) {
        host.ComputeGI(rig.cmd.Get(), sceneTex.Get(), params);
        exec2Ok = rig.FlushAndWait();
    }
    std::vector<float> irrad2x;
    Check(exec2Ok && rig.ReadbackRgba16(host.IrradianceResource(), host.IrradianceResourceState(), kDim, irrad2x),
          "ComputeGI + readback succeeded for 2x emitter radiance");

    const float openLum2x = SampleLum(irrad2x, kDim, 92u, 128u);
    const float farLum2x  = SampleLum(irrad2x, kDim, 168u, 48u);
    const float ratioOpen = openLum2x / std::max(openLum, 1.0e-6f);
    const float ratioFar  = farLum2x / std::max(unoccludedFarLum, 1.0e-6f);
    std::printf("       Linear scaling (2x emitter): openRatio=%.4f farRatio=%.4f\n", ratioOpen, ratioFar);
    Check(std::fabs(ratioOpen - 2.0f) < 0.03f && std::fabs(ratioFar - 2.0f) < 0.03f,
          "Strict linear energy scaling when emitter radiance doubles (2.00 +- 0.03)");

    // --- Pass C: Full-Screen ExecuteScreenGI (512x288 HDR + Standard-Z Depth) ---
    Check(host.ScreenGIReady(), "RawRadianceHost::ScreenGIReady() reports true (prep + apply PSOs loaded)");
    if (host.ScreenGIReady()) {
        constexpr uint32_t kSW = 512u, kSH = 288u;
        std::vector<float> scrColor(static_cast<size_t>(kSW) * kSH * 4u, 0.0f);
        std::vector<float> scrDepth(static_cast<size_t>(kSW) * kSH, 0.75f);

        for (uint32_t y = 0; y < kSH; ++y) {
            for (uint32_t x = 0; x < kSW; ++x) {
                const size_t o = (static_cast<size_t>(y) * kSW + x) * 4u;
                const size_t di = static_cast<size_t>(y) * kSW + x;
                if (y < 76u) {
                    // Sky band (depth >= 0.99998 -> MUST stay 100% untouched)
                    scrDepth[di] = 1.0f;
                    scrColor[o + 0] = 0.18f;
                    scrColor[o + 1] = 0.34f;
                    scrColor[o + 2] = 0.72f;
                    scrColor[o + 3] = 1.0f;
                } else {
                    // Dark stone room wall
                    scrDepth[di] = 0.75f;
                    scrColor[o + 0] = 0.04f;
                    scrColor[o + 1] = 0.04f;
                    scrColor[o + 2] = 0.05f;
                    scrColor[o + 3] = 1.0f;

                    // Bright hearth fire on left: center (128, 190), radius 30 px
                    const float dist = std::hypot(static_cast<float>(x) - 128.0f, static_cast<float>(y) - 190.0f);
                    if (dist <= 30.0f) {
                        const float k = 1.0f - (dist / 30.0f) * 0.3f;
                        scrColor[o + 0] = 16.0f * k;
                        scrColor[o + 1] =  6.0f * k;
                        scrColor[o + 2] =  1.4f * k;
                    }

                    // Foreground opaque dark stone pillar in center: x in [232..276], y in [104..272]
                    if (x >= 232u && x <= 276u && y >= 104u && y <= 272u) {
                        scrDepth[di] = 0.22f;
                        scrColor[o + 0] = 0.05f;
                        scrColor[o + 1] = 0.05f;
                        scrColor[o + 2] = 0.06f;
                    }
                }
            }
        }

        ComPtr<ID3D12Resource> colorTex, depthTex;
        Check(rig.CreateAndUploadScreenTargets(kSW, kSH, scrColor, scrDepth, colorTex, depthTex),
              "Uploaded 512x288 HDR color + standard-Z depth targets in COMMON state");

        bool scrExecOk = rig.BeginFrame();
        if (scrExecOk) {
            host.ExecuteScreenGI(rig.cmd.Get(), colorTex.Get(), depthTex.Get(), kSW, kSH, 0.18f);
            scrExecOk = rig.FlushAndWait();
        }
        std::vector<float> scrOut;
        Check(scrExecOk && rig.ReadbackRgba16Rect(colorTex.Get(), D3D12_RESOURCE_STATE_COMMON, kSW, kSH, scrOut),
              "ExecuteScreenGI ran prep->4-cascade->bilateral apply and restored COMMON cleanly");

        bool scrFinite = true;
        float maxSkyDiff = 0.0f;
        for (uint32_t y = 0; y < kSH; ++y) {
            for (uint32_t x = 0; x < kSW; ++x) {
                const size_t o = (static_cast<size_t>(y) * kSW + x) * 4u;
                for (int c = 0; c < 4; ++c) {
                    if (!std::isfinite(scrOut[o + c])) scrFinite = false;
                }
                if (y < 76u) {
                    maxSkyDiff = std::max(maxSkyDiff, std::fabs(scrOut[o + 0] - 0.18f));
                    maxSkyDiff = std::max(maxSkyDiff, std::fabs(scrOut[o + 1] - 0.34f));
                    maxSkyDiff = std::max(maxSkyDiff, std::fabs(scrOut[o + 2] - 0.72f));
                }
            }
        }

        auto deltaAt = [&](uint32_t x, uint32_t y, float& dR, float& dG, float& dB) {
            const size_t o = (static_cast<size_t>(y) * kSW + x) * 4u;
            dR = scrOut[o + 0] - scrColor[o + 0];
            dG = scrOut[o + 1] - scrColor[o + 1];
            dB = scrOut[o + 2] - scrColor[o + 2];
        };
        float openDR = 0, openDG = 0, openDB = 0;
        float shadDR = 0, shadDG = 0, shadDB = 0;
        float fireDR = 0, fireDG = 0, fireDB = 0;
        deltaAt(190u, 190u, openDR, openDG, openDB); // dark wall near fire (open side)
        deltaAt(320u, 190u, shadDR, shadDG, shadDB); // dark wall shadowed behind foreground pillar
        deltaAt(128u, 190u, fireDR, fireDG, fireDB); // bright fire core (shoulder-clamped)

        const float openBounceLum = 0.2126f * openDR + 0.7152f * openDG + 0.0722f * openDB;
        const float shadBounceLum = 0.2126f * shadDR + 0.7152f * shadDG + 0.0722f * shadDB;
        std::printf("       ExecuteScreenGI 512x288: maxSkyDiff=%.5f | openWallBounce=(%.3f,%.3f,%.3f) | pillarShadowBounce=(%.4f,%.4f,%.4f) | fireCoreDeltaR=%.4f\n",
                    maxSkyDiff, openDR, openDG, openDB, shadDR, shadDG, shadDB, fireDR);
        Check(scrFinite, "ExecuteScreenGI 512x288 output contains zero NaN/Inf");
        Check(maxSkyDiff < 0.002f, "ExecuteScreenGI strictly preserves sky pixels (depth >= 0.99998 untouched)");
        Check(openDR > 0.03f && openDR > openDG && openDG > openDB,
              "ExecuteScreenGI casts warm orange diffuse fire bounce onto dark wall near fire");
        Check(shadBounceLum < openBounceLum * 0.45f,
              "ExecuteScreenGI foreground stone pillar casts clear penumbra bounce shadow onto far wall");
        Check(std::fabs(fireDR) < 0.08f,
              "ExecuteScreenGI luminance-shoulder suppresses bright emitter self-amplification");
    }

    // --- Pass D: D3D12 Debug Layer Error Count ---
    const uint32_t d3d12Errors = rig.CountDebugErrors();
    std::printf("       D3D12 Debug Layer errors: %u\n", d3d12Errors);
    Check(d3d12Errors == 0u, "D3D12 Debug Layer reported 0 errors/corruptions");
}

} // namespace

int main(int argc, char** argv)
{
    bool preferWarp = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--warp") == 0) {
            preferWarp = true;
        }
    }

    std::printf("=== RAW 4-Cascade Radiance Cascades Probe GI Verification ===\n");
    RunRadianceCascadesSuite(preferWarp);
    std::printf("=== Summary: %d failure(s) ===\n", g_failCount);
    return g_failCount == 0 ? 0 : 1;
}
