// RawLiveWorldHost.cpp: Native DX12 GPU Compute Host for Real-Time World-Space Compositing
// & Live Visual Debug Layers (`RawLiveWorldComposite.hlsl`).
// Original clean-room implementation, MIT License.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "RawLiveWorldHost.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#if __has_include("raw_live_world_composite_cs.h")
#define RAW_LIVE_WORLD_HAS_EMBEDDED_DXIL 1
#include "raw_live_world_composite_cs.h"
#else
#define RAW_LIVE_WORLD_HAS_EMBEDDED_DXIL 0
#endif

namespace raw::bridge
{
namespace
{
using Microsoft::WRL::ComPtr;

// Matches cbuffer LiveWorldCompCB : register(b0) in RawLiveWorldComposite.hlsl (exact 256 bytes).
struct GpuLiveWorldCompCB {
    float clipToWorld[16];
    float viewToWorld[12];
    float camPosW[4];
    float dimsAndInv[4];
    float resScaleAndFlags[4];
    float snowDomain[4];
    float grassDomain[4];
    float waterAndCaustics[4];
    float sunDir[4];
    float sunCol[4];
    float windAndWeather[4];
};
static_assert(sizeof(GpuLiveWorldCompCB) == 256u, "GpuLiveWorldCompCB must be exact 256 bytes");

inline DXGI_FORMAT ToReadableSrvFormat(DXGI_FORMAT fmt)
{
    switch (fmt) {
        case DXGI_FORMAT_R32_TYPELESS:
        case DXGI_FORMAT_D32_FLOAT:
            return DXGI_FORMAT_R32_FLOAT;
        case DXGI_FORMAT_R24G8_TYPELESS:
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
            return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        case DXGI_FORMAT_R16_TYPELESS:
        case DXGI_FORMAT_D16_UNORM:
            return DXGI_FORMAT_R16_UNORM;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS:
            return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
            return DXGI_FORMAT_B8G8R8A8_UNORM;
        default:
            return fmt;
    }
}

inline DXGI_FORMAT ToWritableUavFormat(DXGI_FORMAT fmt)
{
    switch (fmt) {
        case DXGI_FORMAT_R16G16B16A16_TYPELESS:
            return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            return DXGI_FORMAT_B8G8R8A8_UNORM;
        default:
            return fmt;
    }
}

inline D3D12_RESOURCE_BARRIER MakeTransition(ID3D12Resource* res,
                                             D3D12_RESOURCE_STATES before,
                                             D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER b{};
    b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = res;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter  = after;
    return b;
}

inline float SafeFinite(float v, float fallback)
{
    return std::isfinite(v) ? v : fallback;
}

} // namespace

RawLiveWorldHost::~RawLiveWorldHost()
{
    Shutdown();
}

void RawLiveWorldHost::Shutdown()
{
    if (m_fence && m_event && m_fenceVal > 0u) {
        if (m_fence->GetCompletedValue() < m_fenceVal) {
            m_fence->SetEventOnCompletion(m_fenceVal, m_event);
            WaitForSingleObject(m_event, 3000);
        }
    }
    if (m_cbBuf && m_cbMapped) {
        m_cbBuf->Unmap(0, nullptr);
        m_cbMapped = nullptr;
    }
    m_cbBuf.Reset();
    m_tsReadback.Reset();
    m_tsHeap.Reset();
    m_fallbackRgba16f.Reset();
    m_fallbackRgba8.Reset();
    m_list.Reset();
    for (uint32_t i = 0u; i < kRingCount; ++i) {
        m_alloc[i].Reset();
        m_slotVal[i] = 0u;
    }
    m_fence.Reset();
    if (m_event) {
        CloseHandle(m_event);
        m_event = nullptr;
    }
    m_heap.Reset();
    m_pso.Reset();
    m_root.Reset();
    m_dev.Reset();
    m_fenceVal       = 0u;
    m_frameIdx       = 0u;
    m_lastMs         = 0.0f;
    m_hasValidTiming = false;
    m_ready          = false;
}

bool RawLiveWorldHost::CreateFallbackTextures()
{
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width            = 1u;
    rd.Height           = 1u;
    rd.DepthOrArraySize = 1u;
    rd.MipLevels        = 1u;
    rd.SampleDesc.Count = 1u;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    rd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    if (FAILED(m_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                              D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                              nullptr, IID_PPV_ARGS(&m_fallbackRgba16f)))) {
        return false;
    }

    rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    if (FAILED(m_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                              D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                              nullptr, IID_PPV_ARGS(&m_fallbackRgba8)))) {
        return false;
    }
    return true;
}

bool RawLiveWorldHost::Init(ID3D12Device* dev, std::string* outErr)
{
#if RAW_LIVE_WORLD_HAS_EMBEDDED_DXIL
    const D3D12_SHADER_BYTECODE blob{kDxil_raw_live_world_composite, sizeof kDxil_raw_live_world_composite};
    return InitWithBlob(dev, blob, outErr);
#else
    (void)dev;
    if (outErr) {
        *outErr = "RawLiveWorldHost: raw_live_world_composite_cs.h not present in include path";
    }
    return false;
#endif
}

bool RawLiveWorldHost::InitWithBlob(ID3D12Device* dev, D3D12_SHADER_BYTECODE csBlob, std::string* outErr)
{
    Shutdown();
    if (!dev || !csBlob.pShaderBytecode || csBlob.BytecodeLength == 0u) {
        if (outErr) *outErr = "RawLiveWorldHost: invalid device or shader bytecode";
        return false;
    }
    m_dev = dev;

    // Root signature: b0 (CBV), t0..t7 (8 SRVs), u0 (1 UAV: ColorOut), s0 (LinClamp), s1 (LinWrap)
    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors     = 8u;
    srvRange.BaseShaderRegister = 0u;

    D3D12_DESCRIPTOR_RANGE uavRange{};
    uavRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors     = 1u;
    uavRange.BaseShaderRegister = 0u;

    D3D12_ROOT_PARAMETER rp[3]{};
    rp[0].ParameterType    = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rp[0].Descriptor       = {0u, 0u};
    rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rp[1].ParameterType    = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rp[1].DescriptorTable  = {1u, &srvRange};
    rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rp[2].ParameterType    = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rp[2].DescriptorTable  = {1u, &uavRange};
    rp[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC ss[2]{};
    ss[0].Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    ss[0].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    ss[0].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    ss[0].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    ss[0].MaxLOD           = D3D12_FLOAT32_MAX;
    ss[0].ShaderRegister   = 0u;
    ss[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    ss[1]                  = ss[0];
    ss[1].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    ss[1].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    ss[1].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    ss[1].ShaderRegister   = 1u;

    D3D12_ROOT_SIGNATURE_DESC rsd{3u, rp, 2u, ss, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> sig, err;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err)) ||
        FAILED(dev->CreateRootSignature(0u, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_root)))) {
        if (outErr) *outErr = "RawLiveWorldHost: root signature creation failed";
        Shutdown();
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC cpd{};
    cpd.pRootSignature = m_root.Get();
    cpd.CS             = csBlob;
    if (FAILED(dev->CreateComputePipelineState(&cpd, IID_PPV_ARGS(&m_pso)))) {
        if (outErr) *outErr = "RawLiveWorldHost: Compute PSO creation failed";
        Shutdown();
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = kRingCount * kDescsPerSlot;
    hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_heap)))) {
        if (outErr) *outErr = "RawLiveWorldHost: CreateDescriptorHeap failed";
        Shutdown();
        return false;
    }
    m_descInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // 4-slot upload CBV ring (4 * 256 = 1024 bytes)
    {
        D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_UPLOAD};
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width            = sizeof(GpuLiveWorldCompCB) * kRingCount;
        rd.Height           = 1u;
        rd.DepthOrArraySize = 1u;
        rd.MipLevels        = 1u;
        rd.SampleDesc.Count = 1u;
        rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_RANGE zeroRange{0u, 0u};
        void* mapped = nullptr;
        if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_cbBuf))) ||
            FAILED(m_cbBuf->Map(0u, &zeroRange, &mapped))) {
            if (outErr) *outErr = "RawLiveWorldHost: CBV ring allocation failed";
            Shutdown();
            return false;
        }
        m_cbMapped = static_cast<uint8_t*>(mapped);
        std::memset(m_cbMapped, 0, sizeof(GpuLiveWorldCompCB) * kRingCount);
    }

    if (!CreateFallbackTextures()) {
        if (outErr) *outErr = "RawLiveWorldHost: fallback texture allocation failed";
        Shutdown();
        return false;
    }

    // GPU timestamp query heap & readback buffer (2 timestamps per ring slot = 8 queries):
    {
        D3D12_QUERY_HEAP_DESC qd{};
        qd.Type  = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        qd.Count = kRingCount * 2u;
        if (SUCCEEDED(dev->CreateQueryHeap(&qd, IID_PPV_ARGS(&m_tsHeap)))) {
            D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_READBACK};
            D3D12_RESOURCE_DESC rd{};
            rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
            rd.Width            = sizeof(uint64_t) * kRingCount * 2u;
            rd.Height           = 1u;
            rd.DepthOrArraySize = 1u;
            rd.MipLevels        = 1u;
            rd.SampleDesc.Count = 1u;
            rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                    IID_PPV_ARGS(&m_tsReadback)))) {
                m_tsHeap.Reset();
            }
        }
    }

    for (uint32_t i = 0u; i < kRingCount; ++i) {
        if (FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_alloc[i])))) {
            if (outErr) *outErr = "RawLiveWorldHost: CreateCommandAllocator failed";
            Shutdown();
            return false;
        }
    }
    if (FAILED(dev->CreateCommandList(0u, D3D12_COMMAND_LIST_TYPE_DIRECT, m_alloc[0].Get(), nullptr, IID_PPV_ARGS(&m_list))) ||
        FAILED(m_list->Close()) ||
        FAILED(dev->CreateFence(0u, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)))) {
        if (outErr) *outErr = "RawLiveWorldHost: command list or fence creation failed";
        Shutdown();
        return false;
    }
    m_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_event) {
        if (outErr) *outErr = "RawLiveWorldHost: CreateEventW failed";
        Shutdown();
        return false;
    }

    m_ready = true;
    return true;
}

bool RawLiveWorldHost::HarvestTimestampPair(uint32_t queryStartIdx, float& outMs) const
{
    if (!m_tsReadback || m_tsFreqHz == 0u) return false;
    D3D12_RANGE range{
        static_cast<SIZE_T>(queryStartIdx) * sizeof(uint64_t),
        static_cast<SIZE_T>(queryStartIdx + 2u) * sizeof(uint64_t)};
    void* ptr = nullptr;
    if (FAILED(m_tsReadback->Map(0u, &range, &ptr)) || !ptr) return false;
    const auto* vals = static_cast<const uint64_t*>(ptr);
    const uint64_t t0 = vals[queryStartIdx];
    const uint64_t t1 = vals[queryStartIdx + 1u];
    D3D12_RANGE empty{0u, 0u};
    m_tsReadback->Unmap(0u, &empty);
    if (t1 >= t0) {
        outMs = static_cast<float>(static_cast<double>(t1 - t0) * 1000.0 / static_cast<double>(m_tsFreqHz));
        return std::isfinite(outMs);
    }
    return false;
}

bool RawLiveWorldHost::RecordComposite(ID3D12GraphicsCommandList*      cmd,
                                       const LiveWorldInputResources&  res,
                                       const LiveWorldCompositeParams& params)
{
    if (!m_ready || !cmd || !res.colorOut || !res.depthIn ||
        params.width == 0u || params.height == 0u) {
        return false;
    }
    const D3D12_RESOURCE_DESC colorDesc = res.colorOut->GetDesc();
    if ((colorDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0) {
        return false;
    }

    const uint32_t slot     = m_frameIdx % kRingCount;
    const uint32_t baseDesc = slot * kDescsPerSlot;

    auto cpuAt = [&](uint32_t idx) {
        D3D12_CPU_DESCRIPTOR_HANDLE h = m_heap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += static_cast<SIZE_T>(idx) * m_descInc;
        return h;
    };
    auto gpuAt = [&](uint32_t idx) {
        D3D12_GPU_DESCRIPTOR_HANDLE h = m_heap->GetGPUDescriptorHandleForHeapStart();
        h.ptr += static_cast<UINT64>(idx) * m_descInc;
        return h;
    };

    // Pack 256-byte constant buffer:
    GpuLiveWorldCompCB cb{};
    std::memcpy(cb.clipToWorld, params.clipToWorld, sizeof(cb.clipToWorld));
    std::memcpy(cb.viewToWorld, params.viewToWorld, sizeof(cb.viewToWorld));
    cb.camPosW[0] = SafeFinite(params.camPosW[0], 0.0f);
    cb.camPosW[1] = SafeFinite(params.camPosW[1], 0.0f);
    cb.camPosW[2] = SafeFinite(params.camPosW[2], 0.0f);
    cb.camPosW[3] = SafeFinite(params.simTimeSec, 0.0f);

    cb.dimsAndInv[0] = static_cast<float>(params.width);
    cb.dimsAndInv[1] = static_cast<float>(params.height);
    cb.dimsAndInv[2] = 1.0f / static_cast<float>(params.width);
    cb.dimsAndInv[3] = 1.0f / static_cast<float>(params.height);

    cb.resScaleAndFlags[0] = (params.resScale[0] > 0.01f && std::isfinite(params.resScale[0])) ? params.resScale[0] : 1.0f;
    cb.resScaleAndFlags[1] = (params.resScale[1] > 0.01f && std::isfinite(params.resScale[1])) ? params.resScale[1] : 1.0f;
    cb.resScaleAndFlags[2] = params.isInterior ? 1.0f : 0.0f;
    cb.resScaleAndFlags[3] = static_cast<float>(params.activeDebugView);

    const bool hasSnow   = (res.snowState != nullptr && res.snowNormal != nullptr && params.snowSpanM > 0.5f);
    const bool hasGrass  = (res.grassBend != nullptr && params.grassSpanM > 0.5f);
    const bool hasCaust  = (res.waterCaustics != nullptr && params.causticsDomainM > 0.25f);

    cb.snowDomain[0] = SafeFinite(params.snowOriginWorldX_m, 0.0f);
    cb.snowDomain[1] = SafeFinite(params.snowOriginWorldZ_m, 0.0f);
    cb.snowDomain[2] = hasSnow ? (1.0f / params.snowSpanM) : 0.0f;
    cb.snowDomain[3] = hasSnow ? std::clamp(SafeFinite(params.snowStrength, 0.0f), 0.0f, 1.5f) : 0.0f;

    cb.grassDomain[0] = SafeFinite(params.grassOriginWorldX_m, 0.0f);
    cb.grassDomain[1] = SafeFinite(params.grassOriginWorldZ_m, 0.0f);
    cb.grassDomain[2] = hasGrass ? (1.0f / params.grassSpanM) : 0.0f;
    cb.grassDomain[3] = hasGrass ? std::clamp(SafeFinite(params.grassStrength, 0.0f), 0.0f, 1.5f) : 0.0f;

    cb.waterAndCaustics[0] = SafeFinite(params.waterPlaneZ_u, -14000.0f);
    cb.waterAndCaustics[1] = hasCaust ? (1.0f / params.causticsDomainM) : 0.125f;
    cb.waterAndCaustics[2] = hasCaust ? std::clamp(SafeFinite(params.causticsStrength, 0.0f), 0.0f, 1.5f) : 0.0f;
    cb.waterAndCaustics[3] = std::max(0.0f, SafeFinite(params.seasonSnowDepthM, 0.0f));

    cb.sunDir[0] = SafeFinite(params.sunDir[0], 0.42f);
    cb.sunDir[1] = SafeFinite(params.sunDir[1], 0.30f);
    cb.sunDir[2] = SafeFinite(params.sunDir[2], 0.85f);
    cb.sunDir[3] = std::max(0.0f, SafeFinite(params.sunIntensity, 1.0f));

    uint32_t flagsMask = 0u;
    if (params.menuPaused)          flagsMask |= 1u;
    if (params.lightingDebugActive) flagsMask |= 2u;
    if (res.normalIn != nullptr)    flagsMask |= 4u;
    if (res.albedoIn != nullptr && res.specularIn != nullptr) flagsMask |= 8u;

    cb.sunCol[0] = std::max(0.0f, SafeFinite(params.sunCol[0], 1.0f));
    cb.sunCol[1] = std::max(0.0f, SafeFinite(params.sunCol[1], 0.95f));
    cb.sunCol[2] = std::max(0.0f, SafeFinite(params.sunCol[2], 0.88f));
    cb.sunCol[3] = static_cast<float>(flagsMask);

    cb.windAndWeather[0] = SafeFinite(params.windXY[0], 0.0f);
    cb.windAndWeather[1] = SafeFinite(params.windXY[1], 0.0f);
    cb.windAndWeather[2] = std::clamp(SafeFinite(params.rain01, 0.0f), 0.0f, 1.0f);
    cb.windAndWeather[3] = std::clamp(SafeFinite(params.snowBaselineDepthM, 0.35f), 0.05f, 1.5f);

    std::memcpy(m_cbMapped + static_cast<size_t>(slot) * sizeof(GpuLiveWorldCompCB), &cb, sizeof(cb));

    // Create 8 SRVs (t0..t7) + 1 UAV (u0) in this ring slot:
    auto bindSrv2D = [&](uint32_t offset, ID3D12Resource* tex) {
        const D3D12_RESOURCE_DESC d = tex->GetDesc();
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format                  = ToReadableSrvFormat(d.Format);
        sd.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Texture2D.MipLevels     = 1u;
        m_dev->CreateShaderResourceView(tex, &sd, cpuAt(baseDesc + offset));
    };

    bindSrv2D(0u, res.depthIn);
    bindSrv2D(1u, res.normalIn ? res.normalIn : m_fallbackRgba16f.Get());
    bindSrv2D(2u, res.albedoIn ? res.albedoIn : m_fallbackRgba8.Get());
    bindSrv2D(3u, res.specularIn ? res.specularIn : m_fallbackRgba8.Get());
    bindSrv2D(4u, res.snowState ? res.snowState : m_fallbackRgba16f.Get());
    bindSrv2D(5u, res.snowNormal ? res.snowNormal : m_fallbackRgba16f.Get());
    bindSrv2D(6u, res.waterCaustics ? res.waterCaustics : m_fallbackRgba16f.Get());
    bindSrv2D(7u, res.grassBend ? res.grassBend : m_fallbackRgba16f.Get());

    D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};
    ud.Format        = ToWritableUavFormat(colorDesc.Format);
    ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    m_dev->CreateUnorderedAccessView(res.colorOut, nullptr, &ud, cpuAt(baseDesc + 8u));

    // Build deduplicated pre/post resource barriers so every input & output transitions cleanly:
    struct TrackedBarrier {
        ID3D12Resource*       res;
        D3D12_RESOURCE_STATES before;
        D3D12_RESOURCE_STATES after;
    };
    std::vector<TrackedBarrier> tracked;
    tracked.reserve(9u);
    auto addTransition = [&](ID3D12Resource* r, D3D12_RESOURCE_STATES cur, D3D12_RESOURCE_STATES need) {
        if (!r || cur == need) return;
        for (const auto& tb : tracked) {
            if (tb.res == r) return;
        }
        // If resource is already in a superset of NON_PIXEL_SHADER_RESOURCE (e.g. NON_PIXEL | PIXEL), no transition needed:
        if (need == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE &&
            (cur & D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) != 0 &&
            (cur & D3D12_RESOURCE_STATE_UNORDERED_ACCESS) == 0) {
            return;
        }
        tracked.push_back({r, cur, need});
    };

    addTransition(res.colorOut,      res.colorOutState,      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    addTransition(res.depthIn,       res.depthInState,       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    addTransition(res.normalIn,      res.normalInState,      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    addTransition(res.albedoIn,      res.albedoInState,      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    addTransition(res.specularIn,    res.specularInState,    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    addTransition(res.snowState,     res.snowStateState,     D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    addTransition(res.snowNormal,    res.snowNormalState,    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    addTransition(res.waterCaustics, res.waterCausticsState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    addTransition(res.grassBend,     res.grassBendState,     D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    if (!tracked.empty()) {
        std::vector<D3D12_RESOURCE_BARRIER> pre;
        pre.reserve(tracked.size());
        for (const auto& tb : tracked) pre.push_back(MakeTransition(tb.res, tb.before, tb.after));
        cmd->ResourceBarrier(static_cast<UINT>(pre.size()), pre.data());
    }

    const uint32_t qStart = slot * 2u;
    const bool doTs       = (m_tsHeap && m_tsReadback && m_tsFreqHz > 0u);
    if (doTs) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qStart);
    }

    ID3D12DescriptorHeap* heaps[1] = {m_heap.Get()};
    cmd->SetDescriptorHeaps(1u, heaps);
    cmd->SetComputeRootSignature(m_root.Get());
    cmd->SetPipelineState(m_pso.Get());
    cmd->SetComputeRootConstantBufferView(
        0u, m_cbBuf->GetGPUVirtualAddress() + static_cast<UINT64>(slot) * sizeof(GpuLiveWorldCompCB));
    cmd->SetComputeRootDescriptorTable(1u, gpuAt(baseDesc + 0u));
    cmd->SetComputeRootDescriptorTable(2u, gpuAt(baseDesc + 8u));
    cmd->Dispatch((params.width + 7u) / 8u, (params.height + 7u) / 8u, 1u);

    if (doTs) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qStart + 1u);
        cmd->ResolveQueryData(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qStart, 2u,
                              m_tsReadback.Get(), static_cast<UINT64>(qStart) * sizeof(uint64_t));
    }

    // Restore every transitioned resource cleanly back to its original state:
    if (!tracked.empty()) {
        std::vector<D3D12_RESOURCE_BARRIER> post;
        post.reserve(tracked.size());
        for (const auto& tb : tracked) post.push_back(MakeTransition(tb.res, tb.after, tb.before));
        cmd->ResourceBarrier(static_cast<UINT>(post.size()), post.data());
    } else {
        D3D12_RESOURCE_BARRIER uavB{};
        uavB.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavB.UAV.pResource = res.colorOut;
        cmd->ResourceBarrier(1u, &uavB);
    }

    ++m_frameIdx;
    return true;
}

bool RawLiveWorldHost::Composite(ID3D12CommandQueue*             queue,
                                 const LiveWorldInputResources&  res,
                                 const LiveWorldCompositeParams& params)
{
    if (!m_ready || !queue || !m_list || !m_fence || !m_event) {
        return false;
    }

    const uint32_t slot = m_frameIdx % kRingCount;
    if (m_slotVal[slot] > 0u && m_fence->GetCompletedValue() < m_slotVal[slot]) {
        m_fence->SetEventOnCompletion(m_slotVal[slot], m_event);
        WaitForSingleObject(m_event, 3000);
    }

    // Harvest GPU timing from the retired slot before reusing it:
    if (m_slotVal[slot] > 0u) {
        float ms = 0.0f;
        if (HarvestTimestampPair(slot * 2u, ms)) {
            m_lastMs         = ms;
            m_hasValidTiming = true;
        }
    }

    if (FAILED(m_alloc[slot]->Reset()) || FAILED(m_list->Reset(m_alloc[slot].Get(), nullptr))) {
        return false;
    }
    if (!RecordComposite(m_list.Get(), res, params)) {
        m_list->Close();
        return false;
    }
    if (FAILED(m_list->Close())) {
        return false;
    }

    ID3D12CommandList* lists[1] = {m_list.Get()};
    queue->ExecuteCommandLists(1u, lists);
    queue->Signal(m_fence.Get(), ++m_fenceVal);
    m_slotVal[slot] = m_fenceVal;
    return true;
}

} // namespace raw::bridge
