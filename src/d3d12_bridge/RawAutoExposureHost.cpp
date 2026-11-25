// RawAutoExposureHost.cpp: Native DX12 GPU Compute Host for 256-Bin Center-Weighted Log-Luminance
// Histogram & Percentile-Trimmed Asymmetric Human Eye-Adaptation Auto-Exposure (SM 6.6 / 6.10).
// Original clean-room implementation, MIT License.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "RawAutoExposureHost.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if __has_include("raw_exposure_hist_cs.h") && __has_include("raw_exposure_adapt_cs.h")
#define RAW_AUTO_EXPOSURE_HAS_EMBEDDED_DXIL 1
#include "raw_exposure_hist_cs.h"
#include "raw_exposure_adapt_cs.h"
#else
#define RAW_AUTO_EXPOSURE_HAS_EMBEDDED_DXIL 0
#endif

namespace raw::bridge
{
namespace
{
using Microsoft::WRL::ComPtr;

// GPU constant buffer matching AutoExposureCB in RawAutoExposureSim.hlsl (256-byte aligned).
struct alignas(256) GpuAutoExposureCB {
    uint32_t dimsAndFlags[4]; // x=width, y=height, z=flags (bit0: resetAdapt), w=0
    float    logRange[4];     // x=minLogLum, y=maxLogLum, z=invLogLumRange, w=keyValue
    float    adaptParams[4];  // x=dtSec, y=tauBrightSec, z=tauDarkSec, w=minExposure
    float    trimAndClamp[4]; // x=maxExposure, y=lowPercentile, z=highPercentile, w=minLumClamp
    uint32_t pad[48];         // 64 + 192 = 256 bytes exact
};
static_assert(sizeof(GpuAutoExposureCB) == 256u, "GpuAutoExposureCB must be 256 bytes");

constexpr uint32_t kHistBinCount     = 256u;
constexpr uint64_t kHistBufferBytes  = static_cast<uint64_t>(kHistBinCount) * sizeof(uint32_t); // 1024 bytes
constexpr uint64_t kStateBufferBytes = sizeof(AutoExposureGpuState); // 32 bytes

// Shader-visible descriptor heap layout (8 descriptors):
//   Slots 0..3 : Per-frame HDR color SRV ring (t0)
//   Slot  4    : HistogramBuf Raw UAV (u0)
//   Slot  5    : ExposureStateBuf Structured UAV (u1)
//   Slot  6    : ExposureStateBuf Structured SRV (external consumer)
constexpr uint32_t kTotalDescCount     = 8u;
constexpr uint32_t kSlotHdrSrvRingBase = 0u;
constexpr uint32_t kSlotUavTableBase   = 4u; // u0=slot4, u1=slot5
constexpr uint32_t kSlotStateExtSrv    = 6u;

constexpr uint32_t kQueriesPerFrame    = 3u; // [0]=start, [1]=post-hist, [2]=post-adapt
constexpr uint32_t kTotalQueries       = RawAutoExposureHost::kCbRingCount * kQueriesPerFrame; // 12

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

bool CreateBuffer(ID3D12Device* dev, uint64_t bytes, D3D12_HEAP_TYPE heapType,
                  D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initState, ComPtr<ID3D12Resource>& out)
{
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = heapType;
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

} // namespace

RawAutoExposureHost::~RawAutoExposureHost()
{
    Shutdown();
}

D3D12_CPU_DESCRIPTOR_HANDLE RawAutoExposureHost::CpuAt(uint32_t slot) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(slot) * m_descInc;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE RawAutoExposureHost::GpuAt(uint32_t slot) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE h = m_heap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<UINT64>(slot) * m_descInc;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE RawAutoExposureHost::GetExposureBufferSRV() const
{
    return m_heap ? GpuAt(kSlotStateExtSrv) : D3D12_GPU_DESCRIPTOR_HANDLE{};
}

D3D12_CPU_DESCRIPTOR_HANDLE RawAutoExposureHost::GetExposureBufferSRVCpu() const
{
    return m_heap ? CpuAt(kSlotStateExtSrv) : D3D12_CPU_DESCRIPTOR_HANDLE{};
}

void RawAutoExposureHost::Transition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
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

bool RawAutoExposureHost::HarvestTimestampRange(uint32_t idxA, uint32_t idxB, float& outMs) const
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

bool RawAutoExposureHost::Init(ID3D12Device* dev, const AutoExposureDesc& desc, std::string* outErr)
{
    Shutdown();
    if (!dev) {
        if (outErr) *outErr = "RawAutoExposureHost::Init: null ID3D12Device";
        return false;
    }

    m_dev = dev;

    D3D12_SHADER_BYTECODE histBc  = desc.customHistShader;
    D3D12_SHADER_BYTECODE adaptBc = desc.customAdaptShader;
#if RAW_AUTO_EXPOSURE_HAS_EMBEDDED_DXIL
    if (!histBc.pShaderBytecode || histBc.BytecodeLength == 0u) {
        histBc.pShaderBytecode = kDxil_raw_exposure_hist;
        histBc.BytecodeLength  = sizeof(kDxil_raw_exposure_hist);
    }
    if (!adaptBc.pShaderBytecode || adaptBc.BytecodeLength == 0u) {
        adaptBc.pShaderBytecode = kDxil_raw_exposure_adapt;
        adaptBc.BytecodeLength  = sizeof(kDxil_raw_exposure_adapt);
    }
#endif
    if (!histBc.pShaderBytecode || histBc.BytecodeLength == 0u ||
        !adaptBc.pShaderBytecode || adaptBc.BytecodeLength == 0u) {
        if (outErr) *outErr = "RawAutoExposureHost::Init: missing DXIL bytecode for Histogram / Adapt";
        Shutdown();
        return false;
    }

    // 1. Shader-visible descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = kTotalDescCount;
    hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_heap)))) {
        if (outErr) *outErr = "RawAutoExposureHost::Init: CreateDescriptorHeap failed";
        Shutdown();
        return false;
    }
    m_descInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // 2. Shared Root Signature: Root CBV (b0), SRV table (t0), UAV table (u0..u1)
    {
        D3D12_DESCRIPTOR_RANGE rSrv{};
        rSrv.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        rSrv.NumDescriptors                    = 1u;
        rSrv.BaseShaderRegister                = 0u;
        rSrv.OffsetInDescriptorsFromTableStart = 0u;

        D3D12_DESCRIPTOR_RANGE rUav{};
        rUav.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        rUav.NumDescriptors                    = 2u;
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

        D3D12_ROOT_SIGNATURE_DESC rsd{};
        rsd.NumParameters = 3u;
        rsd.pParameters   = params;

        ComPtr<ID3DBlob> sig, errBlob;
        if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &errBlob)) ||
            FAILED(dev->CreateRootSignature(0u, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_root)))) {
            if (outErr) *outErr = "RawAutoExposureHost::Init: CreateRootSignature failed";
            Shutdown();
            return false;
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = m_root.Get();
        pd.CS             = histBc;
        if (FAILED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_histPso)))) {
            if (outErr) *outErr = "RawAutoExposureHost::Init: CreateComputePipelineState (cs_exposure_histogram) failed";
            Shutdown();
            return false;
        }
        pd.CS = adaptBc;
        if (FAILED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_adaptPso)))) {
            if (outErr) *outErr = "RawAutoExposureHost::Init: CreateComputePipelineState (cs_exposure_adapt) failed";
            Shutdown();
            return false;
        }
    }

    // 3. GPU Histogram Buffer (1024B raw UAV), Exposure State Buffer (32B structured UAV/SRV), Zero Upload Buffer
    if (!CreateBuffer(dev, kHistBufferBytes, D3D12_HEAP_TYPE_DEFAULT,
                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, m_histBuf) ||
        !CreateBuffer(dev, kStateBufferBytes, D3D12_HEAP_TYPE_DEFAULT,
                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, m_exposureBuf) ||
        !CreateBuffer(dev, kHistBufferBytes, D3D12_HEAP_TYPE_UPLOAD,
                      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, m_zeroUploadBuf)) {
        if (outErr) *outErr = "RawAutoExposureHost::Init: CreateBuffer (hist/state/zero) failed";
        Shutdown();
        return false;
    }
    m_histState     = D3D12_RESOURCE_STATE_COMMON;
    m_exposureState = D3D12_RESOURCE_STATE_COMMON;

    {
        void* zPtr = nullptr;
        const D3D12_RANGE rr{0u, 0u};
        if (FAILED(m_zeroUploadBuf->Map(0u, &rr, &zPtr)) || !zPtr) {
            if (outErr) *outErr = "RawAutoExposureHost::Init: Map zeroUploadBuf failed";
            Shutdown();
            return false;
        }
        std::memset(zPtr, 0, static_cast<size_t>(kHistBufferBytes));
        m_zeroUploadBuf->Unmap(0u, nullptr);
    }

    // Create persistent UAVs and external SRV
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavHist{};
        uavHist.Format                      = DXGI_FORMAT_R32_TYPELESS;
        uavHist.ViewDimension               = D3D12_UAV_DIMENSION_BUFFER;
        uavHist.Buffer.FirstElement         = 0u;
        uavHist.Buffer.NumElements          = kHistBinCount;
        uavHist.Buffer.StructureByteStride  = 0u;
        uavHist.Buffer.Flags                = D3D12_BUFFER_UAV_FLAG_RAW;
        dev->CreateUnorderedAccessView(m_histBuf.Get(), nullptr, &uavHist, CpuAt(kSlotUavTableBase + 0u));

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavState{};
        uavState.Format                     = DXGI_FORMAT_UNKNOWN;
        uavState.ViewDimension              = D3D12_UAV_DIMENSION_BUFFER;
        uavState.Buffer.FirstElement        = 0u;
        uavState.Buffer.NumElements         = 1u;
        uavState.Buffer.StructureByteStride = sizeof(AutoExposureGpuState);
        uavState.Buffer.Flags               = D3D12_BUFFER_UAV_FLAG_NONE;
        dev->CreateUnorderedAccessView(m_exposureBuf.Get(), nullptr, &uavState, CpuAt(kSlotUavTableBase + 1u));

        D3D12_SHADER_RESOURCE_VIEW_DESC srvState{};
        srvState.Format                     = DXGI_FORMAT_UNKNOWN;
        srvState.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
        srvState.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvState.Buffer.FirstElement        = 0u;
        srvState.Buffer.NumElements         = 1u;
        srvState.Buffer.StructureByteStride = sizeof(AutoExposureGpuState);
        srvState.Buffer.Flags               = D3D12_BUFFER_SRV_FLAG_NONE;
        dev->CreateShaderResourceView(m_exposureBuf.Get(), &srvState, CpuAt(kSlotStateExtSrv));
    }

    // 4. 4-frame Constant Buffer ring
    if (!CreateBuffer(dev, sizeof(GpuAutoExposureCB) * kCbRingCount, D3D12_HEAP_TYPE_UPLOAD,
                      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, m_cbBuf)) {
        if (outErr) *outErr = "RawAutoExposureHost::Init: CreateBuffer (CB ring) failed";
        Shutdown();
        return false;
    }
    const D3D12_RANGE rr{0u, 0u};
    if (FAILED(m_cbBuf->Map(0u, &rr, reinterpret_cast<void**>(&m_cbMapped))) || !m_cbMapped) {
        if (outErr) *outErr = "RawAutoExposureHost::Init: Map CB ring failed";
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
    m_needsZero      = true;
    m_pendingReset   = true;
    m_lastHistMs     = 0.0f;
    m_lastAdaptMs    = 0.0f;
    m_hasValidTiming = false;
    m_ready          = true;
    return true;
}

void RawAutoExposureHost::ResetAdaptation(ID3D12GraphicsCommandList* cmd)
{
    m_pendingReset = true;
    if (cmd && m_ready && m_exposureBuf && m_zeroUploadBuf) {
        Transition(cmd, m_exposureBuf.Get(), m_exposureState, D3D12_RESOURCE_STATE_COPY_DEST);
        cmd->CopyBufferRegion(m_exposureBuf.Get(), 0ull, m_zeroUploadBuf.Get(), 0ull, kStateBufferBytes);
        Transition(cmd, m_exposureBuf.Get(), m_exposureState,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
}

bool RawAutoExposureHost::Execute(ID3D12GraphicsCommandList* cmd,
                                  ID3D12Resource* hdrColorTex,
                                  uint32_t width,
                                  uint32_t height,
                                  const AutoExposureParams& params,
                                  D3D12_RESOURCE_STATES srcState)
{
    if (!m_ready || !cmd || !hdrColorTex || width == 0u || height == 0u) return false;

    const uint32_t ringSlot = m_frameIdx % kCbRingCount;

    // Harvest oldest retired GPU timestamp ring slot
    if (m_frameIdx >= kCbRingCount && m_tsFreqHz > 0ull && m_tsReadback) {
        const uint32_t qBase = ringSlot * kQueriesPerFrame;
        float hMs = 0.0f, aMs = 0.0f;
        if (HarvestTimestampRange(qBase + 0u, qBase + 1u, hMs) &&
            HarvestTimestampRange(qBase + 1u, qBase + 2u, aMs)) {
            m_lastHistMs     = hMs;
            m_lastAdaptMs    = aMs;
            m_hasValidTiming = true;
        }
    }

    // One-time deterministic zero-initialization of HistogramBuf and ExposureStateBuf
    if (m_needsZero) {
        Transition(cmd, m_histBuf.Get(), m_histState, D3D12_RESOURCE_STATE_COPY_DEST);
        Transition(cmd, m_exposureBuf.Get(), m_exposureState, D3D12_RESOURCE_STATE_COPY_DEST);
        cmd->CopyBufferRegion(m_histBuf.Get(), 0ull, m_zeroUploadBuf.Get(), 0ull, kHistBufferBytes);
        cmd->CopyBufferRegion(m_exposureBuf.Get(), 0ull, m_zeroUploadBuf.Get(), 0ull, kStateBufferBytes);
        m_needsZero = false;
    }

    // Write per-frame HDR color SRV into current ring slot
    const D3D12_RESOURCE_DESC colorDesc = hdrColorTex->GetDesc();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format                    = ResolveColorSrvFormat(colorDesc.Format);
    srvDesc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MostDetailedMip = 0u;
    srvDesc.Texture2D.MipLevels       = 1u;
    m_dev->CreateShaderResourceView(hdrColorTex, &srvDesc, CpuAt(kSlotHdrSrvRingBase + ringSlot));

    // Populate 256-byte Constant Buffer
    const float minLogLum = std::isfinite(params.minLogLum) ? params.minLogLum : -10.0f;
    const float maxLogLum = (std::isfinite(params.maxLogLum) && params.maxLogLum > minLogLum + 0.1f)
        ? params.maxLogLum : (minLogLum + 24.0f);
    const float lowPct    = std::clamp(params.lowPercentile, 0.0f, 0.90f);
    const float highPct   = std::clamp(params.highPercentile, lowPct + 0.01f, 1.0f);
    const bool  doReset   = params.resetAdapt || m_pendingReset;
    m_pendingReset        = false;

    GpuAutoExposureCB cb{};
    cb.dimsAndFlags[0] = width;
    cb.dimsAndFlags[1] = height;
    cb.dimsAndFlags[2] = doReset ? 1u : 0u;
    cb.dimsAndFlags[3] = 0u;

    cb.logRange[0]     = minLogLum;
    cb.logRange[1]     = maxLogLum;
    cb.logRange[2]     = 1.0f / (maxLogLum - minLogLum);
    cb.logRange[3]     = std::max(params.keyValue, 1.0e-4f);

    cb.adaptParams[0]  = std::max(params.dtSec, 0.0f);
    cb.adaptParams[1]  = std::max(params.tauBrightSec, 1.0e-4f);
    cb.adaptParams[2]  = std::max(params.tauDarkSec, 1.0e-4f);
    cb.adaptParams[3]  = std::max(params.minExposure, 1.0e-5f);

    cb.trimAndClamp[0] = std::max(params.maxExposure, cb.adaptParams[3]);
    cb.trimAndClamp[1] = lowPct;
    cb.trimAndClamp[2] = highPct;
    cb.trimAndClamp[3] = std::max(params.minLumClamp, 1.0e-6f);

    std::memcpy(m_cbMapped + static_cast<size_t>(ringSlot) * sizeof(GpuAutoExposureCB), &cb, sizeof(cb));
    const D3D12_GPU_VIRTUAL_ADDRESS cbGpuAddr =
        m_cbBuf->GetGPUVirtualAddress() + static_cast<uint64_t>(ringSlot) * sizeof(GpuAutoExposureCB);

    // Transition resources
    if (srcState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = hdrColorTex;
        b.Transition.StateBefore = srcState;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1u, &b);
    }
    Transition(cmd, m_histBuf.Get(), m_histState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(cmd, m_exposureBuf.Get(), m_exposureState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    ID3D12DescriptorHeap* heaps[1] = {m_heap.Get()};
    cmd->SetDescriptorHeaps(1u, heaps);
    cmd->SetComputeRootSignature(m_root.Get());
    cmd->SetComputeRootConstantBufferView(0u, cbGpuAddr);
    cmd->SetComputeRootDescriptorTable(1u, GpuAt(kSlotHdrSrvRingBase + ringSlot));
    cmd->SetComputeRootDescriptorTable(2u, GpuAt(kSlotUavTableBase));

    const bool doTs = (m_tsHeap != nullptr && m_tsReadback != nullptr);
    const uint32_t qBase = ringSlot * kQueriesPerFrame;
    if (doTs) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qBase + 0u);
    }

    // Pass 1: 256-Bin Center-Weighted Log-Luminance Histogram
    cmd->SetPipelineState(m_histPso.Get());
    cmd->Dispatch((width + 15u) / 16u, (height + 15u) / 16u, 1u);

    if (doTs) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qBase + 1u);
    }

    // UAV barrier so HistogramBuf writes complete before cs_exposure_adapt reads & zeroes them
    {
        D3D12_RESOURCE_BARRIER uavB{};
        uavB.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavB.UAV.pResource = m_histBuf.Get();
        cmd->ResourceBarrier(1u, &uavB);
    }

    // Pass 2: Parallel Prefix-Sum Percentile Trim + Asymmetric Eye Adaptation + Histogram Clear
    cmd->SetPipelineState(m_adaptPso.Get());
    cmd->Dispatch(1u, 1u, 1u);

    if (doTs) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qBase + 2u);
        cmd->ResolveQueryData(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qBase, kQueriesPerFrame,
                              m_tsReadback.Get(), static_cast<uint64_t>(qBase) * sizeof(uint64_t));
    }

    // Leave ExposureStateBuf ready for downstream tonemap/lighting SRV reads, restore hdrColorTex
    Transition(cmd, m_exposureBuf.Get(), m_exposureState,
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    if (srcState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = hdrColorTex;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b.Transition.StateAfter  = srcState;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1u, &b);
    }

    ++m_frameIdx;
    return true;
}

void RawAutoExposureHost::Shutdown()
{
    if (m_cbBuf && m_cbMapped) {
        m_cbBuf->Unmap(0u, nullptr);
        m_cbMapped = nullptr;
    }
    m_tsReadback.Reset();
    m_tsHeap.Reset();
    m_cbBuf.Reset();
    m_zeroUploadBuf.Reset();
    m_exposureBuf.Reset();
    m_histBuf.Reset();
    m_adaptPso.Reset();
    m_histPso.Reset();
    m_root.Reset();
    m_heap.Reset();
    m_dev            = nullptr;
    m_descInc        = 0u;
    m_frameIdx       = 0u;
    m_needsZero      = true;
    m_pendingReset   = true;
    m_lastHistMs     = 0.0f;
    m_lastAdaptMs    = 0.0f;
    m_hasValidTiming = false;
    m_histState      = D3D12_RESOURCE_STATE_COMMON;
    m_exposureState  = D3D12_RESOURCE_STATE_COMMON;
    m_ready          = false;
}

} // namespace raw::bridge
