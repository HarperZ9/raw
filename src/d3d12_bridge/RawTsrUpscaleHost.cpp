// RawTsrUpscaleHost.cpp: Native DX12 GPU Compute Host for RAW-TSR (Temporal Super-Resolution
// & RCAS Contrast-Adaptive Sharpening Upscaler, SM 6.6 / 6.10).
// Original clean-room implementation, MIT License.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "RawTsrUpscaleHost.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if __has_include("raw_tsr_resolve_cs.h") && __has_include("raw_tsr_rcas_cs.h")
#define RAW_TSR_HAS_EMBEDDED_DXIL 1
#include "raw_tsr_resolve_cs.h"
#include "raw_tsr_rcas_cs.h"
#else
#define RAW_TSR_HAS_EMBEDDED_DXIL 0
#endif

namespace raw::bridge
{
namespace
{
using Microsoft::WRL::ComPtr;

// GPU constant buffer matching TsrUpscaleCB in RawTsrUpscaleSim.hlsl (256-byte aligned).
struct alignas(256) GpuTsrUpscaleCB {
    uint32_t dims[4];      // x=inW, y=inH, z=outW, w=outH
    float    invDims[4];   // x=1/inW, y=1/inH, z=1/outW, w=1/outH
    float    jitterZ[4];   // x=jitterX, y=jitterY, z=nearZ, w=farZ
    float    tsrParams[4]; // x=historyWeight, y=varianceGamma, z=rcasSharpness, w=flags
    uint32_t pad[48];      // 64 + 192 = 256 bytes exact
};
static_assert(sizeof(GpuTsrUpscaleCB) == 256u, "GpuTsrUpscaleCB must be 256 bytes");

// Fixed shader-visible descriptor heap layout (48 descriptors):
//   Slots  0..19 : Pass 1 (Resolve) SRV table (t0=inColor, t1=inDepth, t2=vel, t3=reactive, t4=prevHistory) x 4-frame ring
//   Slots 20..39 : Pass 2 (RCAS)    SRV table (t0..t3=null, t4=curResolvedHistory) x 4-frame ring
//   Slot  40     : m_history[0] UAV (u0)
//   Slot  41     : m_history[1] UAV (u0)
//   Slot  42     : m_sharpenedOut UAV (u0)
//   Slot  43     : m_sharpenedOut SRV (external consumer)
constexpr uint32_t kTotalDescCount         = 48u;
constexpr uint32_t kSlotResolveSrvRingBase = 0u;
constexpr uint32_t kSlotRcasSrvRingBase    = 20u;
constexpr uint32_t kSlotHistUav0           = 40u;
constexpr uint32_t kSlotHistUav1           = 41u;
constexpr uint32_t kSlotSharpenedUav       = 42u;
constexpr uint32_t kSlotSharpenedExtSrv    = 43u;

constexpr uint32_t kQueriesPerFrame        = 3u; // [0]=start, [1]=post-resolve, [2]=post-rcas
constexpr uint32_t kTotalQueries           = RawTsrUpscaleHost::kCbRingCount * kQueriesPerFrame; // 12

DXGI_FORMAT ResolveColorSrvFormat(DXGI_FORMAT fmt)
{
    switch (fmt) {
        case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case DXGI_FORMAT_R32G32B32A32_TYPELESS: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:     return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:     return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS:  return DXGI_FORMAT_R10G10B10A2_UNORM;
        default:                                return fmt;
    }
}

DXGI_FORMAT ResolveDepthSrvFormat(DXGI_FORMAT fmt)
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
        case DXGI_FORMAT_R16_FLOAT:
            return DXGI_FORMAT_R16_FLOAT;
        default:
            return fmt;
    }
}

DXGI_FORMAT ResolveVelSrvFormat(DXGI_FORMAT fmt)
{
    switch (fmt) {
        case DXGI_FORMAT_R16G16_TYPELESS: return DXGI_FORMAT_R16G16_FLOAT;
        case DXGI_FORMAT_R32G32_TYPELESS: return DXGI_FORMAT_R32G32_FLOAT;
        default:                          return fmt;
    }
}

DXGI_FORMAT ResolveScalarSrvFormat(DXGI_FORMAT fmt)
{
    switch (fmt) {
        case DXGI_FORMAT_R8_TYPELESS:  return DXGI_FORMAT_R8_UNORM;
        case DXGI_FORMAT_R16_TYPELESS: return DXGI_FORMAT_R16_FLOAT;
        case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_R32_FLOAT;
        default:                       return fmt;
    }
}

bool CreateBuffer(ID3D12Device* dev, uint64_t bytes, D3D12_HEAP_TYPE heapType,
                  D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initState, ComPtr<ID3D12Resource>& out)
{
    D3D12_HEAP_PROPERTIES hp{heapType};
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width            = (bytes + 255ull) & ~255ull;
    rd.Height           = 1u;
    rd.DepthOrArraySize = 1u;
    rd.MipLevels        = 1u;
    rd.SampleDesc.Count = 1u;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Flags            = flags;
    return SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, initState, nullptr, IID_PPV_ARGS(&out)));
}

bool CreateRgba16Tex2D(ID3D12Device* dev, uint32_t w, uint32_t h, ComPtr<ID3D12Resource>& out)
{
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width            = w;
    rd.Height           = h;
    rd.DepthOrArraySize = 1u;
    rd.MipLevels        = 1u;
    rd.Format           = DXGI_FORMAT_R16G16B16A16_FLOAT;
    rd.SampleDesc.Count = 1u;
    rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    return SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                  D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&out)));
}

void WriteTex2DSrv(ID3D12Device* dev, ID3D12Resource* res, DXGI_FORMAT fmt, D3D12_CPU_DESCRIPTOR_HANDLE dst)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC d{};
    d.Format                    = fmt;
    d.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
    d.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    d.Texture2D.MostDetailedMip = 0u;
    d.Texture2D.MipLevels       = 1u;
    dev->CreateShaderResourceView(res, &d, dst);
}

void WriteTex2DUav(ID3D12Device* dev, ID3D12Resource* res, DXGI_FORMAT fmt, D3D12_CPU_DESCRIPTOR_HANDLE dst)
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC d{};
    d.Format             = fmt;
    d.ViewDimension      = D3D12_UAV_DIMENSION_TEXTURE2D;
    d.Texture2D.MipSlice = 0u;
    dev->CreateUnorderedAccessView(res, nullptr, &d, dst);
}

} // namespace

RawTsrUpscaleHost::~RawTsrUpscaleHost()
{
    Shutdown();
}

D3D12_CPU_DESCRIPTOR_HANDLE RawTsrUpscaleHost::CpuAt(uint32_t slot) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(slot) * m_descInc;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE RawTsrUpscaleHost::GpuAt(uint32_t slot) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE h = m_heap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<UINT64>(slot) * m_descInc;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE RawTsrUpscaleHost::GetOutputSRV() const
{
    return m_heap ? GpuAt(kSlotSharpenedExtSrv) : D3D12_GPU_DESCRIPTOR_HANDLE{};
}

D3D12_CPU_DESCRIPTOR_HANDLE RawTsrUpscaleHost::GetOutputSRVCpu() const
{
    return m_heap ? CpuAt(kSlotSharpenedExtSrv) : D3D12_CPU_DESCRIPTOR_HANDLE{};
}

void RawTsrUpscaleHost::Transition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
                                   D3D12_RESOURCE_STATES& curState, D3D12_RESOURCE_STATES target)
{
    if (!cmd || !res || curState == target) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = res;
    b.Transition.StateBefore = curState;
    b.Transition.StateAfter  = target;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1u, &b);
    curState = target;
}

bool RawTsrUpscaleHost::HarvestTimestampRange(uint32_t idxA, uint32_t idxB, float& outMs) const
{
    if (!m_tsReadback || m_tsFreqHz == 0ull) return false;
    void* mapped = nullptr;
    const D3D12_RANGE rr{0u, static_cast<SIZE_T>(kTotalQueries) * sizeof(uint64_t)};
    if (FAILED(m_tsReadback->Map(0u, &rr, &mapped)) || !mapped) return false;
    const auto* ticks = static_cast<const uint64_t*>(mapped);
    const uint64_t t0 = ticks[idxA];
    const uint64_t t1 = ticks[idxB];
    const D3D12_RANGE wr{0u, 0u};
    m_tsReadback->Unmap(0u, &wr);
    if (t1 < t0) {
        outMs = 0.0f;
        return true;
    }
    outMs = static_cast<float>(static_cast<double>(t1 - t0) * 1000.0 / static_cast<double>(m_tsFreqHz));
    return true;
}

bool RawTsrUpscaleHost::EnsureOutputs(uint32_t outWidth, uint32_t outHeight)
{
    if (!m_dev || outWidth == 0u || outHeight == 0u) return false;
    if (m_history[0] && m_history[1] && m_sharpenedOut &&
        m_outWidth == outWidth && m_outHeight == outHeight) {
        return true;
    }

    ComPtr<ID3D12Resource> h0, h1, outTex;
    if (!CreateRgba16Tex2D(m_dev, outWidth, outHeight, h0) ||
        !CreateRgba16Tex2D(m_dev, outWidth, outHeight, h1) ||
        !CreateRgba16Tex2D(m_dev, outWidth, outHeight, outTex)) {
        return false;
    }

    m_history[0]      = std::move(h0);
    m_history[1]      = std::move(h1);
    m_sharpenedOut    = std::move(outTex);
    m_historyState[0] = D3D12_RESOURCE_STATE_COMMON;
    m_historyState[1] = D3D12_RESOURCE_STATE_COMMON;
    m_sharpenedState  = D3D12_RESOURCE_STATE_COMMON;
    m_outWidth        = outWidth;
    m_outHeight       = outHeight;
    m_histIndex       = 0u;
    m_pendingReset    = true;

    WriteTex2DUav(m_dev, m_history[0].Get(),   DXGI_FORMAT_R16G16B16A16_FLOAT, CpuAt(kSlotHistUav0));
    WriteTex2DUav(m_dev, m_history[1].Get(),   DXGI_FORMAT_R16G16B16A16_FLOAT, CpuAt(kSlotHistUav1));
    WriteTex2DUav(m_dev, m_sharpenedOut.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, CpuAt(kSlotSharpenedUav));
    WriteTex2DSrv(m_dev, m_sharpenedOut.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, CpuAt(kSlotSharpenedExtSrv));

    // Pre-bind null slots t0..t3 for Pass 2 (RCAS) across all 4 ring slots
    for (uint32_t r = 0u; r < kCbRingCount; ++r) {
        const uint32_t b = kSlotRcasSrvRingBase + r * 5u;
        WriteTex2DSrv(m_dev, nullptr, DXGI_FORMAT_R16G16B16A16_FLOAT, CpuAt(b + 0u));
        WriteTex2DSrv(m_dev, nullptr, DXGI_FORMAT_R32_FLOAT,          CpuAt(b + 1u));
        WriteTex2DSrv(m_dev, nullptr, DXGI_FORMAT_R16G16_FLOAT,       CpuAt(b + 2u));
        WriteTex2DSrv(m_dev, nullptr, DXGI_FORMAT_R8_UNORM,           CpuAt(b + 3u));
    }

    return true;
}

bool RawTsrUpscaleHost::Init(ID3D12Device* dev, const TsrUpscaleDesc& desc, std::string* outErr)
{
    Shutdown();
    if (!dev) {
        if (outErr) *outErr = "RawTsrUpscaleHost::Init: null ID3D12Device";
        return false;
    }

    m_dev = dev;

    D3D12_SHADER_BYTECODE resBc  = desc.customResolveShader;
    D3D12_SHADER_BYTECODE rcasBc = desc.customRcasShader;
#if RAW_TSR_HAS_EMBEDDED_DXIL
    if (!resBc.pShaderBytecode || resBc.BytecodeLength == 0u) {
        resBc.pShaderBytecode = kDxil_raw_tsr_resolve;
        resBc.BytecodeLength  = sizeof(kDxil_raw_tsr_resolve);
    }
    if (!rcasBc.pShaderBytecode || rcasBc.BytecodeLength == 0u) {
        rcasBc.pShaderBytecode = kDxil_raw_tsr_rcas;
        rcasBc.BytecodeLength  = sizeof(kDxil_raw_tsr_rcas);
    }
#endif
    if (!resBc.pShaderBytecode || resBc.BytecodeLength == 0u ||
        !rcasBc.pShaderBytecode || rcasBc.BytecodeLength == 0u) {
        if (outErr) *outErr = "RawTsrUpscaleHost::Init: missing DXIL bytecode for TSR Resolve / RCAS";
        Shutdown();
        return false;
    }

    // 1. Shader-visible descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = kTotalDescCount;
    hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_heap)))) {
        if (outErr) *outErr = "RawTsrUpscaleHost::Init: CreateDescriptorHeap failed";
        Shutdown();
        return false;
    }
    m_descInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // 2. Shared Root Signature: Root CBV (b0), SRV table (t0..t4), UAV table (u0), Static linear sampler (s0)
    {
        D3D12_DESCRIPTOR_RANGE rSrv{};
        rSrv.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        rSrv.NumDescriptors                    = 5u;
        rSrv.BaseShaderRegister                = 0u;
        rSrv.OffsetInDescriptorsFromTableStart = 0u;

        D3D12_DESCRIPTOR_RANGE rUav{};
        rUav.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        rUav.NumDescriptors                    = 1u;
        rUav.BaseShaderRegister                = 0u;
        rUav.OffsetInDescriptorsFromTableStart = 0u;

        D3D12_ROOT_PARAMETER params[3]{};
        params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor.ShaderRegister = 0u;
        params[0].Descriptor.RegisterSpace  = 0u;
        params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

        params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1u;
        params[1].DescriptorTable.pDescriptorRanges   = &rSrv;
        params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

        params[2].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[2].DescriptorTable.NumDescriptorRanges = 1u;
        params[2].DescriptorTable.pDescriptorRanges   = &rUav;
        params[2].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_STATIC_SAMPLER_DESC samp{};
        samp.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samp.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
        samp.MaxLOD           = D3D12_FLOAT32_MAX;
        samp.ShaderRegister   = 0u;
        samp.RegisterSpace    = 0u;
        samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rsd{};
        rsd.NumParameters     = 3u;
        rsd.pParameters       = params;
        rsd.NumStaticSamplers = 1u;
        rsd.pStaticSamplers   = &samp;

        ComPtr<ID3DBlob> sig, errBlob;
        if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &errBlob)) ||
            FAILED(dev->CreateRootSignature(0u, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_root)))) {
            if (outErr) *outErr = "RawTsrUpscaleHost::Init: CreateRootSignature failed";
            Shutdown();
            return false;
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = m_root.Get();
        pd.CS             = resBc;
        if (FAILED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_resolvePso)))) {
            if (outErr) *outErr = "RawTsrUpscaleHost::Init: CreateComputePipelineState (cs_tsr_resolve) failed";
            Shutdown();
            return false;
        }
        pd.CS = rcasBc;
        if (FAILED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_rcasPso)))) {
            if (outErr) *outErr = "RawTsrUpscaleHost::Init: CreateComputePipelineState (cs_tsr_rcas_sharpen) failed";
            Shutdown();
            return false;
        }
    }

    // 3. Initial internal targets
    m_inWidth  = std::max(desc.initialInputWidth, 16u);
    m_inHeight = std::max(desc.initialInputHeight, 16u);
    if (!EnsureOutputs(std::max(desc.initialOutputWidth, 16u), std::max(desc.initialOutputHeight, 16u))) {
        if (outErr) *outErr = "RawTsrUpscaleHost::Init: EnsureOutputs failed";
        Shutdown();
        return false;
    }

    // 4. 4-frame Constant Buffer ring
    if (!CreateBuffer(dev, sizeof(GpuTsrUpscaleCB) * kCbRingCount, D3D12_HEAP_TYPE_UPLOAD,
                      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, m_cbBuf)) {
        if (outErr) *outErr = "RawTsrUpscaleHost::Init: CreateBuffer (CB ring) failed";
        Shutdown();
        return false;
    }
    const D3D12_RANGE rr{0u, 0u};
    if (FAILED(m_cbBuf->Map(0u, &rr, reinterpret_cast<void**>(&m_cbMapped))) || !m_cbMapped) {
        if (outErr) *outErr = "RawTsrUpscaleHost::Init: Map CB ring failed";
        Shutdown();
        return false;
    }

    // 5. GPU Timestamp Query Heap & Readback Ring
    D3D12_QUERY_HEAP_DESC qhd{};
    qhd.Type  = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    qhd.Count = kTotalQueries;
    if (SUCCEEDED(dev->CreateQueryHeap(&qhd, IID_PPV_ARGS(&m_tsHeap)))) {
        CreateBuffer(dev, static_cast<uint64_t>(kTotalQueries) * sizeof(uint64_t), D3D12_HEAP_TYPE_READBACK,
                     D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, m_tsReadback);
    }

    m_frameIdx       = 0u;
    m_lastResolveMs  = 0.0f;
    m_lastRcasMs     = 0.0f;
    m_hasValidTiming = false;
    m_pendingReset   = true;
    m_ready          = true;
    return true;
}

bool RawTsrUpscaleHost::Execute(ID3D12GraphicsCommandList* cmd,
                                ID3D12Resource* inputColor,
                                ID3D12Resource* inputDepth,
                                ID3D12Resource* velocityTex,
                                ID3D12Resource* reactiveMask,
                                uint32_t inWidth,
                                uint32_t inHeight,
                                uint32_t outWidth,
                                uint32_t outHeight,
                                const TsrUpscaleParams& params,
                                D3D12_RESOURCE_STATES srcColorState,
                                D3D12_RESOURCE_STATES srcDepthState,
                                D3D12_RESOURCE_STATES srcVelState,
                                D3D12_RESOURCE_STATES srcReactiveState)
{
    if (!m_ready || !cmd || !inputColor || !inputDepth ||
        inWidth == 0u || inHeight == 0u || outWidth == 0u || outHeight == 0u) {
        return false;
    }
    if (inWidth != m_inWidth || inHeight != m_inHeight) {
        m_inWidth      = inWidth;
        m_inHeight     = inHeight;
        m_pendingReset = true;
    }
    if (!EnsureOutputs(outWidth, outHeight)) return false;

    const uint32_t ringSlot = m_frameIdx % kCbRingCount;

    // Harvest oldest retired GPU timestamp ring slot
    if (m_frameIdx >= kCbRingCount && m_tsFreqHz > 0ull && m_tsReadback) {
        const uint32_t qBase = ringSlot * kQueriesPerFrame;
        float rMs = 0.0f, sMs = 0.0f;
        if (HarvestTimestampRange(qBase + 0u, qBase + 1u, rMs) &&
            HarvestTimestampRange(qBase + 1u, qBase + 2u, sMs)) {
            m_lastResolveMs  = rMs;
            m_lastRcasMs     = sMs;
            m_hasValidTiming = true;
        }
    }

    const uint32_t curHistIdx  = m_histIndex;
    const uint32_t prevHistIdx = 1u - curHistIdx;

    // Populate per-frame SRVs for Pass 1 (Resolve) and Pass 2 (RCAS)
    const DXGI_FORMAT colorFmt    = ResolveColorSrvFormat(inputColor->GetDesc().Format);
    const DXGI_FORMAT depthFmt    = ResolveDepthSrvFormat(inputDepth->GetDesc().Format);
    const DXGI_FORMAT velFmt      = velocityTex  ? ResolveVelSrvFormat(velocityTex->GetDesc().Format)     : DXGI_FORMAT_R16G16_FLOAT;
    const DXGI_FORMAT reactiveFmt = reactiveMask ? ResolveScalarSrvFormat(reactiveMask->GetDesc().Format) : DXGI_FORMAT_R8_UNORM;

    const uint32_t resBase = kSlotResolveSrvRingBase + ringSlot * 5u;
    WriteTex2DSrv(m_dev, inputColor,                 colorFmt,                        CpuAt(resBase + 0u));
    WriteTex2DSrv(m_dev, inputDepth,                 depthFmt,                        CpuAt(resBase + 1u));
    WriteTex2DSrv(m_dev, velocityTex,                velFmt,                          CpuAt(resBase + 2u));
    WriteTex2DSrv(m_dev, reactiveMask,               reactiveFmt,                     CpuAt(resBase + 3u));
    WriteTex2DSrv(m_dev, m_history[prevHistIdx].Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, CpuAt(resBase + 4u));

    const uint32_t rcasBase = kSlotRcasSrvRingBase + ringSlot * 5u;
    WriteTex2DSrv(m_dev, m_history[curHistIdx].Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, CpuAt(rcasBase + 4u));

    // Populate 256-byte Constant Buffer
    const bool doReset = params.resetHistory || m_pendingReset;
    m_pendingReset     = false;
    uint32_t flags = 0u;
    if (doReset)               flags |= 1u;
    if (velocityTex != nullptr)  flags |= 2u;
    if (reactiveMask != nullptr) flags |= 4u;

    const float nearZ = std::max(params.nearZ, 1.0e-4f);
    const float farZ  = std::max(params.farZ, nearZ + 1.0f);

    GpuTsrUpscaleCB cb{};
    cb.dims[0]      = inWidth;
    cb.dims[1]      = inHeight;
    cb.dims[2]      = outWidth;
    cb.dims[3]      = outHeight;

    cb.invDims[0]   = 1.0f / static_cast<float>(inWidth);
    cb.invDims[1]   = 1.0f / static_cast<float>(inHeight);
    cb.invDims[2]   = 1.0f / static_cast<float>(outWidth);
    cb.invDims[3]   = 1.0f / static_cast<float>(outHeight);

    cb.jitterZ[0]   = std::clamp(params.jitterX, -0.5f, 0.5f);
    cb.jitterZ[1]   = std::clamp(params.jitterY, -0.5f, 0.5f);
    cb.jitterZ[2]   = nearZ;
    cb.jitterZ[3]   = farZ;

    cb.tsrParams[0] = std::clamp(params.historyWeight, 0.0f, 0.96f);
    cb.tsrParams[1] = std::clamp(params.varianceGamma, 0.25f, 4.0f);
    cb.tsrParams[2] = std::clamp(params.rcasSharpness, 0.0f, 1.0f);
    cb.tsrParams[3] = static_cast<float>(flags);

    std::memcpy(m_cbMapped + static_cast<size_t>(ringSlot) * sizeof(GpuTsrUpscaleCB), &cb, sizeof(cb));
    const D3D12_GPU_VIRTUAL_ADDRESS cbGpuAddr =
        m_cbBuf->GetGPUVirtualAddress() + static_cast<uint64_t>(ringSlot) * sizeof(GpuTsrUpscaleCB);

    // Transition external inputs to NON_PIXEL_SHADER_RESOURCE
    D3D12_RESOURCE_BARRIER inB[4]{};
    uint32_t inCount = 0u;
    if (srcColorState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        inB[inCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        inB[inCount].Transition = {inputColor, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                   srcColorState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
        ++inCount;
    }
    if (srcDepthState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        inB[inCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        inB[inCount].Transition = {inputDepth, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                   srcDepthState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
        ++inCount;
    }
    if (velocityTex && srcVelState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        inB[inCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        inB[inCount].Transition = {velocityTex, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                   srcVelState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
        ++inCount;
    }
    if (reactiveMask && srcReactiveState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        inB[inCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        inB[inCount].Transition = {reactiveMask, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                   srcReactiveState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
        ++inCount;
    }
    if (inCount > 0u) cmd->ResourceBarrier(inCount, inB);

    ID3D12DescriptorHeap* heaps[1] = {m_heap.Get()};
    cmd->SetDescriptorHeaps(1u, heaps);
    cmd->SetComputeRootSignature(m_root.Get());
    cmd->SetComputeRootConstantBufferView(0u, cbGpuAddr);

    const bool doTs = (m_tsHeap != nullptr && m_tsReadback != nullptr);
    const uint32_t qBase = ringSlot * kQueriesPerFrame;
    if (doTs) cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qBase + 0u);

    // Pass 1: TSR Resolve -> m_history[curHistIdx]
    Transition(cmd, m_history[prevHistIdx].Get(), m_historyState[prevHistIdx], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_history[curHistIdx].Get(),  m_historyState[curHistIdx],  D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmd->SetPipelineState(m_resolvePso.Get());
    cmd->SetComputeRootDescriptorTable(1u, GpuAt(resBase));
    cmd->SetComputeRootDescriptorTable(2u, GpuAt(curHistIdx == 0u ? kSlotHistUav0 : kSlotHistUav1));
    cmd->Dispatch((outWidth + 7u) / 8u, (outHeight + 7u) / 8u, 1u);

    if (doTs) cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qBase + 1u);

    // Pass 2: RCAS Contrast-Adaptive Sharpen -> m_sharpenedOut
    Transition(cmd, m_history[curHistIdx].Get(), m_historyState[curHistIdx], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_sharpenedOut.Get(),        m_sharpenedState,           D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmd->SetPipelineState(m_rcasPso.Get());
    cmd->SetComputeRootDescriptorTable(1u, GpuAt(rcasBase));
    cmd->SetComputeRootDescriptorTable(2u, GpuAt(kSlotSharpenedUav));
    cmd->Dispatch((outWidth + 7u) / 8u, (outHeight + 7u) / 8u, 1u);

    if (doTs) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qBase + 2u);
        cmd->ResolveQueryData(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qBase, kQueriesPerFrame,
                              m_tsReadback.Get(), static_cast<uint64_t>(qBase) * sizeof(uint64_t));
    }

    // Leave SharpenedOut ready for downstream SRV consumers and restore all external inputs
    Transition(cmd, m_sharpenedOut.Get(), m_sharpenedState,
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    if (inCount > 0u) {
        for (uint32_t i = 0u; i < inCount; ++i) {
            std::swap(inB[i].Transition.StateBefore, inB[i].Transition.StateAfter);
        }
        cmd->ResourceBarrier(inCount, inB);
    }

    m_histIndex = 1u - curHistIdx;
    ++m_frameIdx;
    return true;
}

void RawTsrUpscaleHost::Shutdown()
{
    if (m_cbBuf && m_cbMapped) {
        m_cbBuf->Unmap(0u, nullptr);
        m_cbMapped = nullptr;
    }
    m_tsReadback.Reset();
    m_tsHeap.Reset();
    m_cbBuf.Reset();
    m_sharpenedOut.Reset();
    m_history[0].Reset();
    m_history[1].Reset();
    m_rcasPso.Reset();
    m_resolvePso.Reset();
    m_root.Reset();
    m_heap.Reset();
    m_dev             = nullptr;
    m_inWidth         = 0u;
    m_inHeight        = 0u;
    m_outWidth        = 0u;
    m_outHeight       = 0u;
    m_histIndex       = 0u;
    m_descInc         = 0u;
    m_frameIdx        = 0u;
    m_lastResolveMs   = 0.0f;
    m_lastRcasMs      = 0.0f;
    m_hasValidTiming  = false;
    m_pendingReset    = true;
    m_historyState[0] = D3D12_RESOURCE_STATE_COMMON;
    m_historyState[1] = D3D12_RESOURCE_STATE_COMMON;
    m_sharpenedState  = D3D12_RESOURCE_STATE_COMMON;
    m_ready           = false;
}

} // namespace raw::bridge
