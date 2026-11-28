// RawGtaoHost.cpp: Native DX12 GPU Compute Host for Jimenez (SIGGRAPH 2016) Ground-Truth
// Ambient Occlusion (GTAO) + Unoccluded Bent-Normal & 3x3 Depth-Aware Bilateral Denoise.
// Original clean-room implementation, MIT License.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "RawGtaoHost.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if __has_include("raw_gtao_main_cs.h") && __has_include("raw_gtao_denoise_cs.h")
#define RAW_GTAO_HAS_EMBEDDED_DXIL 1
#include "raw_gtao_main_cs.h"
#include "raw_gtao_denoise_cs.h"
#else
#define RAW_GTAO_HAS_EMBEDDED_DXIL 0
#endif

namespace raw::bridge
{
namespace
{
using Microsoft::WRL::ComPtr;

// GPU constant buffer matching RawGtaoSim.hlsl GtaoCB byte-for-byte (256-byte aligned).
struct alignas(256) GpuGtaoCB {
    uint32_t dimsAndSteps[4];   // x = w, y = h, z = numSlices, w = numSteps
    float    invDimsAndProj[4]; // x = 1/w, y = 1/h, z = tanHalfFovX, w = tanHalfFovY
    float    depthParams[4];    // x = nearZ, y = farZ, z = effectRadius, w = falloffStart
    float    filterParams[4];   // x = aoPower, y = maxScreenRadiusPx, z = depthEdgeScale, w = temporalJitter
    uint32_t pad[48];           // 64 + 192 = 256 bytes exact
};
static_assert(sizeof(GpuGtaoCB) == 256u, "GpuGtaoCB must be 256 bytes");

// Fixed shader-visible descriptor heap layout (20 descriptors):
//   Slots  0..7  : Main pass SRV table (t0=sceneDepth, t1=null Texture2D) x 4-frame ring
//   Slots  8..15 : Denoise pass SRV table (t0=sceneDepth, t1=m_rawOut) x 4-frame ring
//   Slot  16     : m_rawOut UAV (u0 during cs_gtao_main)
//   Slot  17     : m_filteredOut UAV (u0 during cs_gtao_denoise)
//   Slot  18     : m_filteredOut SRV (external consumer)
constexpr uint32_t kTotalDescCount          = 20u;
constexpr uint32_t kSlotMainSrvRingBase     = 0u;
constexpr uint32_t kSlotDenoiseSrvRingBase  = 8u;
constexpr uint32_t kSlotRawUav              = 16u;
constexpr uint32_t kSlotFilteredUav         = 17u;
constexpr uint32_t kSlotFilteredExtSrv      = 18u;

constexpr uint32_t kQueriesPerFrame         = 3u; // [0]=start, [1]=post-main, [2]=post-denoise
constexpr uint32_t kTotalQueries            = RawGtaoHost::kCbRingCount * kQueriesPerFrame; // 12

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

bool CreateRgba16Tex2D(ID3D12Device* dev, uint32_t w, uint32_t h, ComPtr<ID3D12Resource>& out)
{
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
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

void WriteRgba16Srv(ID3D12Device* dev, ID3D12Resource* res, D3D12_CPU_DESCRIPTOR_HANDLE dst)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC d{};
    d.Format                    = DXGI_FORMAT_R16G16B16A16_FLOAT;
    d.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
    d.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    d.Texture2D.MostDetailedMip = 0u;
    d.Texture2D.MipLevels       = 1u;
    dev->CreateShaderResourceView(res, &d, dst);
}

void WriteRgba16Uav(ID3D12Device* dev, ID3D12Resource* res, D3D12_CPU_DESCRIPTOR_HANDLE dst)
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC d{};
    d.Format             = DXGI_FORMAT_R16G16B16A16_FLOAT;
    d.ViewDimension      = D3D12_UAV_DIMENSION_TEXTURE2D;
    d.Texture2D.MipSlice = 0u;
    dev->CreateUnorderedAccessView(res, nullptr, &d, dst);
}

} // namespace

RawGtaoHost::~RawGtaoHost()
{
    Shutdown();
}

D3D12_CPU_DESCRIPTOR_HANDLE RawGtaoHost::CpuAt(uint32_t slot) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(slot) * m_descInc;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE RawGtaoHost::GpuAt(uint32_t slot) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE h = m_heap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<UINT64>(slot) * m_descInc;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE RawGtaoHost::GetFilteredOutputSRV() const
{
    return m_heap ? GpuAt(kSlotFilteredExtSrv) : D3D12_GPU_DESCRIPTOR_HANDLE{};
}

D3D12_CPU_DESCRIPTOR_HANDLE RawGtaoHost::GetFilteredOutputSRVCpu() const
{
    return m_heap ? CpuAt(kSlotFilteredExtSrv) : D3D12_CPU_DESCRIPTOR_HANDLE{};
}

void RawGtaoHost::Transition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
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

bool RawGtaoHost::HarvestTimestampRange(uint32_t idxA, uint32_t idxB, float& outMs) const
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

bool RawGtaoHost::EnsureOutputs(uint32_t width, uint32_t height)
{
    if (!m_dev || width == 0u || height == 0u) return false;
    if (m_rawOut && m_filteredOut && m_width == width && m_height == height) {
        return true;
    }

    ComPtr<ID3D12Resource> rawTex, filtTex;
    if (!CreateRgba16Tex2D(m_dev, width, height, rawTex) ||
        !CreateRgba16Tex2D(m_dev, width, height, filtTex)) {
        return false;
    }

    m_rawOut        = std::move(rawTex);
    m_filteredOut   = std::move(filtTex);
    m_rawState      = D3D12_RESOURCE_STATE_COMMON;
    m_filteredState = D3D12_RESOURCE_STATE_COMMON;
    m_width         = width;
    m_height        = height;

    // Bind UAVs, external SRV, null SRVs for main pass t1, and m_rawOut SRVs for denoise pass t1
    WriteRgba16Uav(m_dev, m_rawOut.Get(), CpuAt(kSlotRawUav));
    WriteRgba16Uav(m_dev, m_filteredOut.Get(), CpuAt(kSlotFilteredUav));
    WriteRgba16Srv(m_dev, m_filteredOut.Get(), CpuAt(kSlotFilteredExtSrv));

    for (uint32_t r = 0u; r < kCbRingCount; ++r) {
        WriteRgba16Srv(m_dev, nullptr, CpuAt(kSlotMainSrvRingBase + r * 2u + 1u));
        WriteRgba16Srv(m_dev, m_rawOut.Get(), CpuAt(kSlotDenoiseSrvRingBase + r * 2u + 1u));
    }

    return true;
}

bool RawGtaoHost::Init(ID3D12Device* dev, const GtaoDesc& desc, std::string* outErr)
{
    Shutdown();
    if (!dev) {
        if (outErr) *outErr = "RawGtaoHost::Init: null ID3D12Device";
        return false;
    }

    m_dev = dev;

    D3D12_SHADER_BYTECODE mainBc    = desc.customMainShader;
    D3D12_SHADER_BYTECODE denoiseBc = desc.customDenoiseShader;
#if RAW_GTAO_HAS_EMBEDDED_DXIL
    if (!mainBc.pShaderBytecode || mainBc.BytecodeLength == 0u) {
        mainBc.pShaderBytecode = kDxil_raw_gtao_main;
        mainBc.BytecodeLength  = sizeof(kDxil_raw_gtao_main);
    }
    if (!denoiseBc.pShaderBytecode || denoiseBc.BytecodeLength == 0u) {
        denoiseBc.pShaderBytecode = kDxil_raw_gtao_denoise;
        denoiseBc.BytecodeLength  = sizeof(kDxil_raw_gtao_denoise);
    }
#endif
    if (!mainBc.pShaderBytecode || mainBc.BytecodeLength == 0u ||
        !denoiseBc.pShaderBytecode || denoiseBc.BytecodeLength == 0u) {
        if (outErr) *outErr = "RawGtaoHost::Init: missing DXIL bytecode for GTAO Main / Denoise";
        Shutdown();
        return false;
    }

    // 1. Shader-visible descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = kTotalDescCount;
    hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_heap)))) {
        if (outErr) *outErr = "RawGtaoHost::Init: CreateDescriptorHeap failed";
        Shutdown();
        return false;
    }
    m_descInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // 2. Shared Root Signature: Root CBV (b0), SRV table (t0..t1), UAV table (u0)
    {
        D3D12_DESCRIPTOR_RANGE rSrv{};
        rSrv.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        rSrv.NumDescriptors                    = 2u;
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

        D3D12_ROOT_SIGNATURE_DESC rsd{};
        rsd.NumParameters = 3u;
        rsd.pParameters   = params;

        ComPtr<ID3DBlob> sig, errBlob;
        if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &errBlob)) ||
            FAILED(dev->CreateRootSignature(0u, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_root)))) {
            if (outErr) *outErr = "RawGtaoHost::Init: CreateRootSignature failed";
            Shutdown();
            return false;
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = m_root.Get();
        pd.CS             = mainBc;
        if (FAILED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_mainPso)))) {
            if (outErr) *outErr = "RawGtaoHost::Init: Main PSO creation failed";
            Shutdown();
            return false;
        }
        pd.CS = denoiseBc;
        if (FAILED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_denoisePso)))) {
            if (outErr) *outErr = "RawGtaoHost::Init: Denoise PSO creation failed";
            Shutdown();
            return false;
        }
    }

    // 3. Initial RGBA16F outputs
    if (!EnsureOutputs(std::max(1u, desc.initialWidth), std::max(1u, desc.initialHeight))) {
        if (outErr) *outErr = "RawGtaoHost::Init: initial RGBA16F output creation failed";
        Shutdown();
        return false;
    }

    // 4. Persistently mapped 4-frame CB ring
    if (!CreateBuffer(dev, static_cast<uint64_t>(kCbRingCount) * sizeof(GpuGtaoCB),
                      D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, m_cbBuf)) {
        if (outErr) *outErr = "RawGtaoHost::Init: CB ring creation failed";
        Shutdown();
        return false;
    }
    const D3D12_RANGE zeroRange{0u, 0u};
    void* mapped = nullptr;
    if (FAILED(m_cbBuf->Map(0u, &zeroRange, &mapped)) || !mapped) {
        if (outErr) *outErr = "RawGtaoHost::Init: CB ring Map failed";
        Shutdown();
        return false;
    }
    m_cbMapped = static_cast<uint8_t*>(mapped);
    std::memset(m_cbMapped, 0, static_cast<size_t>(kCbRingCount) * sizeof(GpuGtaoCB));

    // 5. Hardware GPU Timestamp QueryHeap + Readback Buffer (12 queries)
    D3D12_QUERY_HEAP_DESC qhd{};
    qhd.Type     = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    qhd.Count    = kTotalQueries;
    qhd.NodeMask = 0u;
    if (SUCCEEDED(dev->CreateQueryHeap(&qhd, IID_PPV_ARGS(&m_tsHeap)))) {
        CreateBuffer(dev, static_cast<uint64_t>(kTotalQueries) * sizeof(uint64_t),
                     D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
                     D3D12_RESOURCE_STATE_COPY_DEST, m_tsReadback);
    }

    m_ready = true;
    return true;
}

bool RawGtaoHost::Execute(ID3D12GraphicsCommandList* cmd,
                          ID3D12Resource* sceneDepth,
                          uint32_t width,
                          uint32_t height,
                          const GtaoParams& params,
                          D3D12_RESOURCE_STATES srcDepthState)
{
    if (!m_ready || !cmd || !sceneDepth || width == 0u || height == 0u) return false;
    if (!EnsureOutputs(width, height)) return false;

    const uint32_t ringSlot = m_frameIdx % kCbRingCount;
    const uint32_t qBase    = ringSlot * kQueriesPerFrame;

    if (m_frameIdx >= kCbRingCount && m_tsReadback && m_tsFreqHz > 0ull) {
        float mMs = 0.0f, dMs = 0.0f;
        if (HarvestTimestampRange(qBase + 0u, qBase + 1u, mMs) &&
            HarvestTimestampRange(qBase + 1u, qBase + 2u, dMs)) {
            m_lastMainMs     = mMs;
            m_lastDenoiseMs  = dMs;
            m_hasValidTiming = true;
        }
    }

    // Populate depth SRV into BOTH Main and Denoise ring slots (t0)
    const D3D12_RESOURCE_DESC depthDesc = sceneDepth->GetDesc();
    D3D12_SHADER_RESOURCE_VIEW_DESC dsrv{};
    dsrv.Format                    = ResolveDepthSrvFormat(depthDesc.Format);
    dsrv.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
    dsrv.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    dsrv.Texture2D.MostDetailedMip = 0u;
    dsrv.Texture2D.MipLevels       = 1u;
    m_dev->CreateShaderResourceView(sceneDepth, &dsrv, CpuAt(kSlotMainSrvRingBase + ringSlot * 2u));
    m_dev->CreateShaderResourceView(sceneDepth, &dsrv, CpuAt(kSlotDenoiseSrvRingBase + ringSlot * 2u));

    const float aspect      = static_cast<float>(width) / static_cast<float>(height);
    const float fovRad      = std::clamp(params.fovYDeg, 10.0f, 150.0f) * 3.14159265359f / 180.0f;
    const float tanHalfFovY = std::tan(0.5f * fovRad);
    const float tanHalfFovX = tanHalfFovY * aspect;
    const float nearZ       = std::max(params.nearZ, 1.0e-3f);
    const float farZ        = std::max(params.farZ, nearZ + 1.0f);

    GpuGtaoCB cb{};
    cb.dimsAndSteps[0]   = width;
    cb.dimsAndSteps[1]   = height;
    cb.dimsAndSteps[2]   = std::clamp(params.numSlices, 1u, 4u);
    cb.dimsAndSteps[3]   = std::clamp(params.numSteps, 2u, 12u);
    cb.invDimsAndProj[0] = 1.0f / static_cast<float>(width);
    cb.invDimsAndProj[1] = 1.0f / static_cast<float>(height);
    cb.invDimsAndProj[2] = tanHalfFovX;
    cb.invDimsAndProj[3] = tanHalfFovY;
    cb.depthParams[0]    = nearZ;
    cb.depthParams[1]    = farZ;
    cb.depthParams[2]    = std::max(params.effectRadius, 1.0e-3f);
    cb.depthParams[3]    = std::clamp(params.falloffStart, 0.05f, 0.95f);
    cb.filterParams[0]   = std::max(params.aoPower, 0.1f);
    cb.filterParams[1]   = std::clamp(params.maxScreenRadiusPx, 4.0f, 256.0f);
    cb.filterParams[2]   = std::max(params.depthEdgeScale, 1.0e-4f);
    cb.filterParams[3]   = params.temporalJitter;
    std::memcpy(m_cbMapped + static_cast<size_t>(ringSlot) * sizeof(GpuGtaoCB), &cb, sizeof(cb));

    const bool needDepthTrans = (srcDepthState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (needDepthTrans) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = sceneDepth;
        b.Transition.StateBefore = srcDepthState;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1u, &b);
    }

    ID3D12DescriptorHeap* heaps[1] = {m_heap.Get()};
    cmd->SetDescriptorHeaps(1u, heaps);
    cmd->SetComputeRootSignature(m_root.Get());
    const D3D12_GPU_VIRTUAL_ADDRESS cbAddr =
        m_cbBuf->GetGPUVirtualAddress() + static_cast<uint64_t>(ringSlot) * sizeof(GpuGtaoCB);
    cmd->SetComputeRootConstantBufferView(0u, cbAddr);

    if (m_tsHeap && m_tsReadback) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qBase + 0u);
    }

    const uint32_t gx = (width + 7u) / 8u;
    const uint32_t gy = (height + 7u) / 8u;

    // Pass 1: cs_gtao_main (sceneDepth -> m_rawOut)
    Transition(cmd, m_rawOut.Get(), m_rawState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmd->SetPipelineState(m_mainPso.Get());
    cmd->SetComputeRootDescriptorTable(1u, GpuAt(kSlotMainSrvRingBase + ringSlot * 2u));
    cmd->SetComputeRootDescriptorTable(2u, GpuAt(kSlotRawUav));
    cmd->Dispatch(gx, gy, 1u);

    if (m_tsHeap && m_tsReadback) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qBase + 1u);
    }

    // Pass 2: cs_gtao_denoise (sceneDepth + m_rawOut -> m_filteredOut)
    Transition(cmd, m_rawOut.Get(), m_rawState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_filteredOut.Get(), m_filteredState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmd->SetPipelineState(m_denoisePso.Get());
    cmd->SetComputeRootDescriptorTable(1u, GpuAt(kSlotDenoiseSrvRingBase + ringSlot * 2u));
    cmd->SetComputeRootDescriptorTable(2u, GpuAt(kSlotFilteredUav));
    cmd->Dispatch(gx, gy, 1u);

    Transition(cmd, m_filteredOut.Get(), m_filteredState,
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    if (m_tsHeap && m_tsReadback) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qBase + 2u);
        cmd->ResolveQueryData(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qBase, kQueriesPerFrame,
                              m_tsReadback.Get(), static_cast<UINT64>(qBase) * sizeof(uint64_t));
    }

    if (needDepthTrans) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = sceneDepth;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b.Transition.StateAfter  = srcDepthState;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1u, &b);
    }

    ++m_frameIdx;
    return true;
}

void RawGtaoHost::Shutdown()
{
    if (m_cbBuf && m_cbMapped) {
        m_cbBuf->Unmap(0u, nullptr);
        m_cbMapped = nullptr;
    }
    m_tsReadback.Reset();
    m_tsHeap.Reset();
    m_cbBuf.Reset();
    m_filteredOut.Reset();
    m_rawOut.Reset();
    m_denoisePso.Reset();
    m_mainPso.Reset();
    m_root.Reset();
    m_heap.Reset();

    m_dev            = nullptr;
    m_ready          = false;
    m_width          = 0u;
    m_height         = 0u;
    m_rawState       = D3D12_RESOURCE_STATE_COMMON;
    m_filteredState  = D3D12_RESOURCE_STATE_COMMON;
    m_frameIdx       = 0u;
    m_lastMainMs     = 0.0f;
    m_lastDenoiseMs  = 0.0f;
    m_hasValidTiming = false;
}

} // namespace raw::bridge
