// RawRadianceHost.cpp: Native DX12 GPU 4-Cascade Penumbra-Exact Radiance Cascades Probe GI Host.
// Original clean-room implementation, MIT License.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "RawRadianceHost.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if __has_include("raw_rc_trace_merge_cs.h") && __has_include("raw_rc_resolve_cs.h")
#define RAW_RC_HAS_EMBEDDED_DXIL 1
#include "raw_rc_trace_merge_cs.h"
#include "raw_rc_resolve_cs.h"
#else
#define RAW_RC_HAS_EMBEDDED_DXIL 0
#endif

#if __has_include("raw_rc_screen_prep_cs.h") && __has_include("raw_rc_screen_apply_cs.h")
#define RAW_RC_HAS_SCREEN_DXIL 1
#include "raw_rc_screen_prep_cs.h"
#include "raw_rc_screen_apply_cs.h"
#else
#define RAW_RC_HAS_SCREEN_DXIL 0
#endif

namespace raw::bridge
{
namespace
{
using Microsoft::WRL::ComPtr;

struct alignas(256) GpuRadianceCascadeCB {
    uint32_t cascadeInfo[4];  // x = c (0..3), y = cascadeCount (4), z = stepCount, w = atlasRes (256)
    float    intervalInfo[4]; // x = tStart, y = tEnd, z = opacityScale, w = emissiveScale
    float    resolveInfo[4];  // xyz = skyRadianceRGB, w = irradianceScale
    float    sceneDims[4];    // xy = dims, zw = invDims
    uint32_t _pad[48];
};
static_assert(sizeof(GpuRadianceCascadeCB) == 256u, "GpuRadianceCascadeCB must be 256 bytes");

struct alignas(256) GpuScreenPrepCB {
    uint32_t dims[4];            // x = fullW, y = fullH, z = atlasRes (256), w = flags
    float    invDims[4];         // x = 1/fullW, y = 1/fullH, z = 1/256, w = giStrength
    float    extractParams[4];   // x = emissiveThresh, y = emissiveCap, z = depthEdgeScale, w = skyDepthThresh
    float    bilateralParams[4]; // x = bilateralDepthSigma, y = lumaShoulder, z = maxBounceLuma, w = 0
    uint32_t _pad[48];
};
static_assert(sizeof(GpuScreenPrepCB) == 256u, "GpuScreenPrepCB must be 256 bytes");

// 5 dispatches per ComputeGI frame + 2 dispatches per ExecuteScreenGI frame (4 frames in flight)
constexpr uint32_t kPassesPerFrame       = 5u;
constexpr uint32_t kCascadeCbSlots       = RawRadianceHost::kCbRingCount * kPassesPerFrame; // 0..19
constexpr uint32_t kScreenPassesPerFrame = 2u;
constexpr uint32_t kTotalCbSlots         = kCascadeCbSlots + RawRadianceHost::kCbRingCount * kScreenPassesPerFrame; // 28

// Descriptor heap layout (64 slots total, zero runtime hazards across 4 frames in flight):
//   Slot 0     : External Irradiance SRV
//   Slots 2..5 : Static UAV tables (1 UAV each: Atlas0, Atlas1, Irradiance, EmissiveAlpha)
//   Slots 8..31: ComputeGI 4-frame ring (6 SRVs/frame: [Scene,null], [Scene,Atlas0], [Scene,Atlas1])
//   Slots 36..55: ExecuteScreenGI 4-frame ring (5 descriptors/frame: [ColorSrv,DepthSrv], [IrradSrv,DepthSrv], [ColorUav])
constexpr uint32_t kTotalDescCount     = 64u;
constexpr uint32_t kSlotExtIrradSrv    = 0u;
constexpr uint32_t kSlotUavAtlas0      = 2u;
constexpr uint32_t kSlotUavAtlas1      = 3u;
constexpr uint32_t kSlotUavIrrad       = 4u;
constexpr uint32_t kSlotUavEmissive    = 5u;

inline constexpr uint32_t RingSrvNull(uint32_t r)    { return 8u + r * 6u + 0u; }
inline constexpr uint32_t RingSrvAtlas0(uint32_t r)  { return 8u + r * 6u + 2u; }
inline constexpr uint32_t RingSrvAtlas1(uint32_t r)  { return 8u + r * 6u + 4u; }

inline constexpr uint32_t ScreenPrepSrv(uint32_t r)  { return 36u + r * 5u + 0u; } // (colorInOut, sceneDepth)
inline constexpr uint32_t ScreenApplySrv(uint32_t r) { return 36u + r * 5u + 2u; } // (m_irradiance, sceneDepth)
inline constexpr uint32_t ScreenApplyUav(uint32_t r) { return 36u + r * 5u + 4u; } // (colorInOut)

DXGI_FORMAT DepthSrvFormat(DXGI_FORMAT fmt)
{
    switch (fmt) {
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_R32_FLOAT:
        return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
        return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32G8X24_TYPELESS:
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
        return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    case DXGI_FORMAT_D16_UNORM:
    case DXGI_FORMAT_R16_TYPELESS:
    case DXGI_FORMAT_R16_UNORM:
        return DXGI_FORMAT_R16_UNORM;
    default:
        return DXGI_FORMAT_R32_FLOAT;
    }
}

bool CreateTex2D(ID3D12Device* dev, uint32_t dim, DXGI_FORMAT fmt, ComPtr<ID3D12Resource>& out)
{
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = dim;
    rd.Height = dim;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = fmt;
    rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    return SUCCEEDED(dev->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&out)));
}

bool CreateUploadCb(ID3D12Device* dev, size_t totalBytes, ComPtr<ID3D12Resource>& out, uint8_t** outMapped)
{
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = (totalBytes + 255u) & ~size_t(255u);
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(dev->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&out)))) {
        return false;
    }
    D3D12_RANGE rr{0, 0};
    void* ptr = nullptr;
    if (FAILED(out->Map(0, &rr, &ptr))) {
        return false;
    }
    *outMapped = static_cast<uint8_t*>(ptr);
    std::memset(*outMapped, 0, totalBytes);
    return true;
}

void WriteSrv2D(ID3D12Device* dev, ID3D12Resource* res, DXGI_FORMAT fmt, D3D12_CPU_DESCRIPTOR_HANDLE dst)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC d{};
    d.Format = fmt;
    d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    d.Texture2D.MipLevels = 1;
    dev->CreateShaderResourceView(res, &d, dst);
}

void WriteUav2D(ID3D12Device* dev, ID3D12Resource* res, DXGI_FORMAT fmt, D3D12_CPU_DESCRIPTOR_HANDLE dst)
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC d{};
    d.Format = fmt;
    d.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    d.Texture2D.MipSlice = 0;
    dev->CreateUnorderedAccessView(res, nullptr, &d, dst);
}

} // namespace

RawRadianceHost::~RawRadianceHost()
{
    if (m_cbBuf && m_cbMapped) {
        m_cbBuf->Unmap(0, nullptr);
        m_cbMapped = nullptr;
    }
}

bool RawRadianceHost::Init(ID3D12Device* dev, std::string* outErr)
{
#if RAW_RC_HAS_EMBEDDED_DXIL
    RadianceShaderBlobs blobs{};
    blobs.traceMerge = {kDxil_raw_rc_trace_merge, sizeof kDxil_raw_rc_trace_merge};
    blobs.resolve    = {kDxil_raw_rc_resolve,    sizeof kDxil_raw_rc_resolve};
#if RAW_RC_HAS_SCREEN_DXIL
    blobs.screenPrep  = {kDxil_raw_rc_screen_prep,  sizeof kDxil_raw_rc_screen_prep};
    blobs.screenApply = {kDxil_raw_rc_screen_apply, sizeof kDxil_raw_rc_screen_apply};
#endif
    return InitWithBlobs(dev, blobs, outErr);
#else
    (void)dev;
    if (outErr) {
        *outErr = "RawRadianceHost: embedded DXIL headers not present; generate via CMake DXC rules or call InitWithBlobs()";
    }
    return false;
#endif
}

bool RawRadianceHost::InitWithBlobs(ID3D12Device* dev, const RadianceShaderBlobs& blobs, std::string* outErr)
{
    m_ready = false;
    if (!dev) {
        if (outErr) *outErr = "RawRadianceHost: null ID3D12Device";
        return false;
    }
    if (!blobs.Complete()) {
        if (outErr) *outErr = "RawRadianceHost: incomplete RadianceShaderBlobs";
        return false;
    }
    m_dev = dev;

    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = kTotalDescCount;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_heap)))) {
        if (outErr) *outErr = "RawRadianceHost: CreateDescriptorHeap failed";
        return false;
    }
    m_descInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    constexpr DXGI_FORMAT kFmt = DXGI_FORMAT_R16G16B16A16_FLOAT;
    if (!CreateTex2D(dev, kAtlasRes, kFmt, m_atlas[0].res) ||
        !CreateTex2D(dev, kAtlasRes, kFmt, m_atlas[1].res) ||
        !CreateTex2D(dev, kAtlasRes, kFmt, m_irradiance.res) ||
        !CreateTex2D(dev, kAtlasRes, kFmt, m_emissiveAlpha.res)) {
        if (outErr) *outErr = "RawRadianceHost: failed to allocate 256x256 RGBA16F textures";
        return false;
    }
    m_atlas[0].state      = D3D12_RESOURCE_STATE_COMMON;
    m_atlas[1].state      = D3D12_RESOURCE_STATE_COMMON;
    m_irradiance.state    = D3D12_RESOURCE_STATE_COMMON;
    m_emissiveAlpha.state = D3D12_RESOURCE_STATE_COMMON;

    // Root signature: b0 (CBV), t0..t1 (2 SRVs), u0 (1 UAV), s0 (LinearClamp)
    {
        D3D12_DESCRIPTOR_RANGE srvRange{};
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors = 2;
        srvRange.BaseShaderRegister = 0;

        D3D12_DESCRIPTOR_RANGE uavRange{};
        uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors = 1;
        uavRange.BaseShaderRegister = 0;

        D3D12_ROOT_PARAMETER rp[3]{};
        rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rp[0].Descriptor = {0, 0};
        rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp[1].DescriptorTable = {1, &srvRange};
        rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        rp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp[2].DescriptorTable = {1, &uavRange};
        rp[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_STATIC_SAMPLER_DESC ss{};
        ss.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        ss.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ss.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ss.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ss.MaxLOD = D3D12_FLOAT32_MAX;
        ss.ShaderRegister = 0;
        ss.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rsd{3, rp, 1, &ss, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        ComPtr<ID3DBlob> sig, err;
        if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err)) ||
            FAILED(dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_root)))) {
            if (outErr) *outErr = "RawRadianceHost: root signature creation failed";
            return false;
        }
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = m_root.Get();
    pd.CS = blobs.traceMerge;
    if (FAILED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_psoTraceMerge)))) {
        if (outErr) *outErr = "RawRadianceHost: cs_rc_trace_merge PSO creation failed";
        return false;
    }
    pd.CS = blobs.resolve;
    if (FAILED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_psoResolve)))) {
        if (outErr) *outErr = "RawRadianceHost: cs_rc_resolve PSO creation failed";
        return false;
    }
    m_psoScreenPrep.Reset();
    m_psoScreenApply.Reset();
    if (blobs.ScreenComplete()) {
        pd.CS = blobs.screenPrep;
        if (FAILED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_psoScreenPrep)))) {
            if (outErr) *outErr = "RawRadianceHost: cs_rc_screen_prep PSO creation failed";
            return false;
        }
        pd.CS = blobs.screenApply;
        if (FAILED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_psoScreenApply)))) {
            if (outErr) *outErr = "RawRadianceHost: cs_rc_screen_apply PSO creation failed";
            return false;
        }
    }

    if (!CreateUploadCb(dev, sizeof(GpuRadianceCascadeCB) * kTotalCbSlots, m_cbBuf, &m_cbMapped)) {
        if (outErr) *outErr = "RawRadianceHost: CBV upload buffer creation failed";
        return false;
    }

    // Optional 4-frame ring GPU timestamp query heap (2 queries/frame: Start, End)
    {
        constexpr uint32_t kTotalQueries = kCbRingCount * 2u;
        D3D12_QUERY_HEAP_DESC qd{};
        qd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        qd.Count = kTotalQueries;
        D3D12_HEAP_PROPERTIES hpRb{};
        hpRb.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rbd{};
        rbd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rbd.Width = static_cast<UINT64>(kTotalQueries) * sizeof(uint64_t);
        rbd.Height = 1;
        rbd.DepthOrArraySize = 1;
        rbd.MipLevels = 1;
        rbd.SampleDesc.Count = 1;
        rbd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(dev->CreateQueryHeap(&qd, IID_PPV_ARGS(&m_tsHeap))) ||
            FAILED(dev->CreateCommittedResource(&hpRb, D3D12_HEAP_FLAG_NONE, &rbd,
                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_tsReadback)))) {
            m_tsHeap.Reset();
            m_tsReadback.Reset();
        }
    }

    // Populate static descriptors
    WriteSrv2D(dev, m_irradiance.Get(),    kFmt, CpuAt(kSlotExtIrradSrv));
    WriteUav2D(dev, m_atlas[0].Get(),      kFmt, CpuAt(kSlotUavAtlas0));
    WriteUav2D(dev, m_atlas[1].Get(),      kFmt, CpuAt(kSlotUavAtlas1));
    WriteUav2D(dev, m_irradiance.Get(),    kFmt, CpuAt(kSlotUavIrrad));
    WriteUav2D(dev, m_emissiveAlpha.Get(), kFmt, CpuAt(kSlotUavEmissive));

    for (uint32_t r = 0; r < kCbRingCount; ++r) {
        WriteSrv2D(dev, nullptr,            kFmt, CpuAt(RingSrvNull(r) + 0));
        WriteSrv2D(dev, nullptr,            kFmt, CpuAt(RingSrvNull(r) + 1));
        WriteSrv2D(dev, nullptr,            kFmt, CpuAt(RingSrvAtlas0(r) + 0));
        WriteSrv2D(dev, m_atlas[0].Get(),   kFmt, CpuAt(RingSrvAtlas0(r) + 1));
        WriteSrv2D(dev, nullptr,            kFmt, CpuAt(RingSrvAtlas1(r) + 0));
        WriteSrv2D(dev, m_atlas[1].Get(),   kFmt, CpuAt(RingSrvAtlas1(r) + 1));

        WriteSrv2D(dev, nullptr,            kFmt, CpuAt(ScreenPrepSrv(r) + 0));
        WriteSrv2D(dev, nullptr,            DXGI_FORMAT_R32_FLOAT, CpuAt(ScreenPrepSrv(r) + 1));
        WriteSrv2D(dev, m_irradiance.Get(), kFmt, CpuAt(ScreenApplySrv(r) + 0));
        WriteSrv2D(dev, nullptr,            DXGI_FORMAT_R32_FLOAT, CpuAt(ScreenApplySrv(r) + 1));
        WriteUav2D(dev, nullptr,            kFmt, CpuAt(ScreenApplyUav(r)));
    }

    m_ready = true;
    return true;
}

void RawRadianceHost::Transition(ID3D12GraphicsCommandList* cmd, TrackedTex& tex, D3D12_RESOURCE_STATES target)
{
    if (!tex.res || tex.state == target) {
        return;
    }
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = tex.res.Get();
    b.Transition.StateBefore = tex.state;
    b.Transition.StateAfter = target;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &b);
    tex.state = target;
}

D3D12_CPU_DESCRIPTOR_HANDLE RawRadianceHost::CpuAt(uint32_t slot) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(slot) * m_descInc;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE RawRadianceHost::GpuAt(uint32_t slot) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE h = m_heap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<UINT64>(slot) * m_descInc;
    return h;
}

bool RawRadianceHost::HarvestTimestampPair(uint32_t queryStartIdx, float& outMs) const
{
    if (!m_tsReadback || m_tsFreqHz == 0u) {
        return false;
    }
    const SIZE_T byteOff = static_cast<SIZE_T>(queryStartIdx) * sizeof(uint64_t);
    const D3D12_RANGE rr{byteOff, byteOff + 2u * sizeof(uint64_t)};
    void* ptr = nullptr;
    if (FAILED(m_tsReadback->Map(0, &rr, &ptr))) {
        return false;
    }
    const auto* ticks = reinterpret_cast<const uint64_t*>(static_cast<const uint8_t*>(ptr) + byteOff);
    const uint64_t t0 = ticks[0];
    const uint64_t t1 = ticks[1];
    const D3D12_RANGE wr{0, 0};
    m_tsReadback->Unmap(0, &wr);
    if (t1 < t0) {
        return false;
    }
    const double ms = static_cast<double>(t1 - t0) * 1000.0 / static_cast<double>(m_tsFreqHz);
    if (!std::isfinite(ms) || ms < 0.0 || ms > 5000.0) {
        return false;
    }
    outMs = static_cast<float>(ms);
    return true;
}

void RawRadianceHost::ComputeGI(ID3D12GraphicsCommandList* cmd,
                                ID3D12Resource* sceneEmissiveAlpha,
                                const RadianceCascadeParams& params,
                                D3D12_RESOURCE_STATES sceneState)
{
    if (!m_ready || !cmd || !sceneEmissiveAlpha || !m_cbMapped) {
        return;
    }

    uint32_t tsSlot = 0u;
    const bool recordTs = (!m_inScreenGi && m_tsHeap != nullptr && m_tsReadback != nullptr);
    if (recordTs) {
        if (m_tsWriteIdx >= (kCbRingCount - 1u)) {
            const uint32_t retired = (m_tsWriteIdx - (kCbRingCount - 1u)) % kCbRingCount;
            float measuredMs = 0.0f;
            if (HarvestTimestampPair(retired * 2u, measuredMs)) {
                m_timings.giPassMs = measuredMs;
                m_timings.valid = true;
            }
        }
        tsSlot = m_tsWriteIdx % kCbRingCount;
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, tsSlot * 2u + 0u);
    }

    const D3D12_RESOURCE_DESC sDesc = sceneEmissiveAlpha->GetDesc();
    const DXGI_FORMAT sFmt = (sDesc.Format != DXGI_FORMAT_UNKNOWN) ? sDesc.Format : DXGI_FORMAT_R16G16B16A16_FLOAT;
    const float sW = static_cast<float>(std::max<UINT64>(sDesc.Width, 1u));
    const float sH = static_cast<float>(std::max<UINT>(sDesc.Height, 1u));

    const uint32_t ring = m_frameIdx % kCbRingCount;
    WriteSrv2D(m_dev, sceneEmissiveAlpha, sFmt, CpuAt(RingSrvNull(ring) + 0));
    WriteSrv2D(m_dev, sceneEmissiveAlpha, sFmt, CpuAt(RingSrvAtlas0(ring) + 0));
    WriteSrv2D(m_dev, sceneEmissiveAlpha, sFmt, CpuAt(RingSrvAtlas1(ring) + 0));

    constexpr D3D12_RESOURCE_STATES kCsSrv  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    constexpr D3D12_RESOURCE_STATES kAllSrv = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    constexpr D3D12_RESOURCE_STATES kUav    = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    const bool transitionScene = (sceneState != kCsSrv && (sceneState & kCsSrv) == 0);
    if (transitionScene) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = sceneEmissiveAlpha;
        b.Transition.StateBefore = sceneState;
        b.Transition.StateAfter = kCsSrv;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &b);
    }

    // Geometric 4x radial intervals matching the 4x angular cascade subdivision:
    //   c=0: [0 .. r0]
    //   c=1: [r0 .. 5*r0]
    //   c=2: [5*r0 .. 21*r0]
    //   c=3: [21*r0 .. max(85*r0, maxTrace)]
    const float r0 = std::max(params.baseIntervalTexels, 0.5f);
    const float tBounds[5] = {
        0.0f,
        r0,
        5.0f * r0,
        21.0f * r0,
        std::max(85.0f * r0, params.maxTraceTexels)
    };
    const uint32_t kStepCounts[4] = {6u, 14u, 32u, 96u};

    auto writeCb = [&](uint32_t passIdx, uint32_t cascadeIdx) -> D3D12_GPU_VIRTUAL_ADDRESS {
        GpuRadianceCascadeCB cb{};
        cb.cascadeInfo[0] = cascadeIdx;
        cb.cascadeInfo[1] = kCascadeCount;
        cb.cascadeInfo[2] = kStepCounts[std::min(cascadeIdx, 3u)];
        cb.cascadeInfo[3] = kAtlasRes;

        cb.intervalInfo[0] = tBounds[std::min(cascadeIdx, 3u)];
        cb.intervalInfo[1] = tBounds[std::min(cascadeIdx, 3u) + 1u];
        cb.intervalInfo[2] = params.opacityScale;
        cb.intervalInfo[3] = params.emissiveScale;

        cb.resolveInfo[0] = params.skyRadianceRGB[0];
        cb.resolveInfo[1] = params.skyRadianceRGB[1];
        cb.resolveInfo[2] = params.skyRadianceRGB[2];
        cb.resolveInfo[3] = params.irradianceScale;

        cb.sceneDims[0] = sW;
        cb.sceneDims[1] = sH;
        cb.sceneDims[2] = 1.0f / sW;
        cb.sceneDims[3] = 1.0f / sH;

        const size_t slot = static_cast<size_t>(ring) * kPassesPerFrame + passIdx;
        const size_t byteOff = slot * sizeof(GpuRadianceCascadeCB);
        std::memcpy(m_cbMapped + byteOff, &cb, sizeof(cb));
        return m_cbBuf->GetGPUVirtualAddress() + byteOff;
    };

    ID3D12DescriptorHeap* heaps[1] = {m_heap.Get()};
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetComputeRootSignature(m_root.Get());
    cmd->SetPipelineState(m_psoTraceMerge.Get());

    constexpr uint32_t kGroups = kAtlasRes / 8u; // 32 x 32 x 1

    // Top-down cascade pass: c = 3 -> 2 -> 1 -> 0
    //   c=3 -> Atlas[0]  (reads Scene, null)
    //   c=2 -> Atlas[1]  (reads Scene, Atlas[0])
    //   c=1 -> Atlas[0]  (reads Scene, Atlas[1])
    //   c=0 -> Atlas[1]  (reads Scene, Atlas[0])
    for (uint32_t step = 0; step < kCascadeCount; ++step) {
        const uint32_t c = (kCascadeCount - 1u) - step; // 3, 2, 1, 0
        const uint32_t dstIdx = step & 1u;              // 0, 1, 0, 1
        const uint32_t srcIdx = 1u - dstIdx;

        if (step > 0u) {
            Transition(cmd, m_atlas[srcIdx], kCsSrv);
        }
        Transition(cmd, m_atlas[dstIdx], kUav);

        const uint32_t srvTable = (step == 0u)
            ? RingSrvNull(ring)
            : (srcIdx == 0u ? RingSrvAtlas0(ring) : RingSrvAtlas1(ring));
        const uint32_t uavTable = (dstIdx == 0u) ? kSlotUavAtlas0 : kSlotUavAtlas1;

        cmd->SetComputeRootConstantBufferView(0, writeCb(step, c));
        cmd->SetComputeRootDescriptorTable(1, GpuAt(srvTable));
        cmd->SetComputeRootDescriptorTable(2, GpuAt(uavTable));
        cmd->Dispatch(kGroups, kGroups, 1u);
    }

    // Resolve Cascade 0 (in Atlas[1]) -> 256x256 RGBA16F Diffuse GI Irradiance
    Transition(cmd, m_atlas[1],   kCsSrv);
    Transition(cmd, m_irradiance, kUav);
    cmd->SetPipelineState(m_psoResolve.Get());
    cmd->SetComputeRootConstantBufferView(0, writeCb(4u, 0u));
    cmd->SetComputeRootDescriptorTable(1, GpuAt(RingSrvAtlas1(ring)));
    cmd->SetComputeRootDescriptorTable(2, GpuAt(kSlotUavIrrad));
    cmd->Dispatch(kGroups, kGroups, 1u);

    // Leave resolved irradiance in ALL_SHADER_RESOURCE ready for LightingPass / LightFog / composite binding
    Transition(cmd, m_irradiance, kAllSrv);

    if (transitionScene) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = sceneEmissiveAlpha;
        b.Transition.StateBefore = kCsSrv;
        b.Transition.StateAfter = sceneState;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &b);
    }

    if (recordTs) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, tsSlot * 2u + 1u);
        cmd->ResolveQueryData(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, tsSlot * 2u + 0u, 2u,
                              m_tsReadback.Get(), static_cast<UINT64>(tsSlot * 2u) * sizeof(uint64_t));
        ++m_tsWriteIdx;
    }

    ++m_frameIdx;
}

void RawRadianceHost::ExecuteScreenGI(ID3D12GraphicsCommandList* cmd,
                                      ID3D12Resource* colorInOut,
                                      ID3D12Resource* sceneDepth,
                                      uint32_t width,
                                      uint32_t height,
                                      float giStrength)
{
    if (!ScreenGIReady() || !cmd || !colorInOut || !sceneDepth || width == 0u || height == 0u || !m_cbMapped) {
        return;
    }

    uint32_t tsSlot = 0u;
    const bool recordTs = (m_tsHeap != nullptr && m_tsReadback != nullptr);
    if (recordTs) {
        if (m_tsWriteIdx >= (kCbRingCount - 1u)) {
            const uint32_t retired = (m_tsWriteIdx - (kCbRingCount - 1u)) % kCbRingCount;
            float measuredMs = 0.0f;
            if (HarvestTimestampPair(retired * 2u, measuredMs)) {
                m_timings.giPassMs = measuredMs;
                m_timings.valid = true;
            }
        }
        tsSlot = m_tsWriteIdx % kCbRingCount;
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, tsSlot * 2u + 0u);
    }

    const D3D12_RESOURCE_DESC cDesc = colorInOut->GetDesc();
    const D3D12_RESOURCE_DESC dDesc = sceneDepth->GetDesc();
    const DXGI_FORMAT colorFmt = (cDesc.Format != DXGI_FORMAT_UNKNOWN) ? cDesc.Format : DXGI_FORMAT_R16G16B16A16_FLOAT;
    const DXGI_FORMAT depthFmt = DepthSrvFormat(dDesc.Format);

    const uint32_t sRing = m_screenFrameIdx % kCbRingCount;
    WriteSrv2D(m_dev, colorInOut, colorFmt, CpuAt(ScreenPrepSrv(sRing) + 0));
    WriteSrv2D(m_dev, sceneDepth, depthFmt, CpuAt(ScreenPrepSrv(sRing) + 1));
    WriteSrv2D(m_dev, sceneDepth, depthFmt, CpuAt(ScreenApplySrv(sRing) + 1));
    WriteUav2D(m_dev, colorInOut, colorFmt, CpuAt(ScreenApplyUav(sRing)));

    GpuScreenPrepCB cb{};
    cb.dims[0] = width;
    cb.dims[1] = height;
    cb.dims[2] = kAtlasRes;
    cb.dims[3] = 0u;
    cb.invDims[0] = 1.0f / static_cast<float>(width);
    cb.invDims[1] = 1.0f / static_cast<float>(height);
    cb.invDims[2] = 1.0f / static_cast<float>(kAtlasRes);
    cb.invDims[3] = std::clamp(giStrength, 0.0f, 1.0f);
    cb.extractParams[0] = 1.35f;    // emissive luma threshold
    cb.extractParams[1] = 6.0f;     // energy-conserving emitter soft-cap
    cb.extractParams[2] = 45.0f;    // depth-edge gradient gain
    cb.extractParams[3] = 0.99998f; // standard-Z sky threshold
    cb.bilateralParams[0] = 0.015f; // depth-aware bilateral sigma
    cb.bilateralParams[1] = 2.5f;   // luminance shoulder preventing emitter self-amplification
    cb.bilateralParams[2] = 1.25f;  // maximum soft-capped bounce luminance
    cb.bilateralParams[3] = 0.0f;

    const size_t prepCbSlot = static_cast<size_t>(kCascadeCbSlots) + static_cast<size_t>(sRing) * kScreenPassesPerFrame;
    const size_t prepByteOff = prepCbSlot * sizeof(GpuScreenPrepCB);
    std::memcpy(m_cbMapped + prepByteOff, &cb, sizeof(cb));
    const D3D12_GPU_VIRTUAL_ADDRESS screenCbGpu = m_cbBuf->GetGPUVirtualAddress() + prepByteOff;

    constexpr D3D12_RESOURCE_STATES kCommon = D3D12_RESOURCE_STATE_COMMON;
    constexpr D3D12_RESOURCE_STATES kCsSrv  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    constexpr D3D12_RESOURCE_STATES kUav    = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    auto makeBarrier = [](ID3D12Resource* r, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = r;
        b.Transition.StateBefore = before;
        b.Transition.StateAfter = after;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        return b;
    };

    // 1) Transition inputs: ColorOut COMMON -> SRV, SceneDepth COMMON -> NON_PIXEL_SRV, EmissiveAlpha -> UAV
    D3D12_RESOURCE_BARRIER preB[2] = {
        makeBarrier(colorInOut, kCommon, kCsSrv),
        makeBarrier(sceneDepth, kCommon, kCsSrv)
    };
    cmd->ResourceBarrier(2, preB);
    Transition(cmd, m_emissiveAlpha, kUav);

    ID3D12DescriptorHeap* heaps[1] = {m_heap.Get()};
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetComputeRootSignature(m_root.Get());
    cmd->SetPipelineState(m_psoScreenPrep.Get());
    cmd->SetComputeRootConstantBufferView(0, screenCbGpu);
    cmd->SetComputeRootDescriptorTable(1, GpuAt(ScreenPrepSrv(sRing)));
    cmd->SetComputeRootDescriptorTable(2, GpuAt(kSlotUavEmissive));
    cmd->Dispatch(kAtlasRes / 8u, kAtlasRes / 8u, 1u);

    // 2) Run 4-cascade penumbra-exact Radiance Cascades GI on extracted 256x256 EmissiveAlpha
    Transition(cmd, m_emissiveAlpha, kCsSrv);
    RadianceCascadeParams rcParams{};
    rcParams.baseIntervalTexels = 3.5f;
    rcParams.maxTraceTexels     = 300.0f;
    rcParams.opacityScale       = 2.2f;
    rcParams.emissiveScale      = 0.15f;
    rcParams.irradianceScale    = 1.0f;
    m_inScreenGi = true;
    ComputeGI(cmd, m_emissiveAlpha.Get(), rcParams, kCsSrv);
    m_inScreenGi = false;

    // 3) Bilateral depth-aware upsample & energy-conserving diffuse GI bounce onto ColorOut
    D3D12_RESOURCE_BARRIER colToUav = makeBarrier(colorInOut, kCsSrv, kUav);
    cmd->ResourceBarrier(1, &colToUav);

    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetComputeRootSignature(m_root.Get());
    cmd->SetPipelineState(m_psoScreenApply.Get());
    cmd->SetComputeRootConstantBufferView(0, screenCbGpu);
    cmd->SetComputeRootDescriptorTable(1, GpuAt(ScreenApplySrv(sRing)));
    cmd->SetComputeRootDescriptorTable(2, GpuAt(ScreenApplyUav(sRing)));
    cmd->Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);

    // 4) Restore external resources cleanly back to COMMON
    D3D12_RESOURCE_BARRIER postB[2] = {
        makeBarrier(colorInOut, kUav, kCommon),
        makeBarrier(sceneDepth, kCsSrv, kCommon)
    };
    cmd->ResourceBarrier(2, postB);

    if (recordTs) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, tsSlot * 2u + 1u);
        cmd->ResolveQueryData(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, tsSlot * 2u + 0u, 2u,
                              m_tsReadback.Get(), static_cast<UINT64>(tsSlot * 2u) * sizeof(uint64_t));
        ++m_tsWriteIdx;
    }

    ++m_screenFrameIdx;
}

D3D12_GPU_DESCRIPTOR_HANDLE RawRadianceHost::IrradianceSrv() const
{
    return GpuAt(kSlotExtIrradSrv);
}

D3D12_CPU_DESCRIPTOR_HANDLE RawRadianceHost::IrradianceSrvCpu() const
{
    return CpuAt(kSlotExtIrradSrv);
}

} // namespace raw::bridge
