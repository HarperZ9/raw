// RawWaterCausticsHost.cpp: Native DX12 GPU Compute Physical Water Caustics & Underwater God-Ray Host (SM 6.6 / 6.10).
// Original clean-room implementation, MIT License.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "RawWaterCausticsHost.h"

#include "raw_water_caustics_trace_cs.h"
#include "raw_water_underwater_rays_cs.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace raw::bridge
{
namespace
{

constexpr uint32_t kCbStride = 256u;
constexpr uint32_t kTsPerFrame = 3u;
constexpr uint32_t kTotalTsQueries = RawWaterCausticsHost::kRingCount * kTsPerFrame;
constexpr float    kDegToRad = 3.14159265359f / 180.0f;

struct alignas(16) CausticsGpuCB {
    uint32_t width;
    uint32_t height;
    uint32_t frameIndex;
    float    timeSec;

    float    sunDir[3];
    float    sunIntensity;

    float    sunColor[3];
    float    bedDepthM;

    float    absorptionRGB[3];
    float    scatterCoeff;

    float    waveAmp;
    float    domainSizeM;
    float    dispersionScale;
    float    mieG;

    float    camPos[3];
    float    waterSurfaceY;

    float    camRight[3];
    float    fovYRad;

    float    camUp[3];
    float    maxRayDistM;

    float    camForward[3];
    float    shaftIntensity;
};
static_assert(sizeof(CausticsGpuCB) <= kCbStride, "CausticsGpuCB must fit in 256-byte CB ring slot");

void Normalize3Safe(const float in[3], float out[3], float fallbackX, float fallbackY, float fallbackZ)
{
    const float lenSq = in[0] * in[0] + in[1] * in[1] + in[2] * in[2];
    if (lenSq > 1e-10f && std::isfinite(lenSq)) {
        const float inv = 1.0f / std::sqrt(lenSq);
        out[0] = in[0] * inv;
        out[1] = in[1] * inv;
        out[2] = in[2] * inv;
    } else {
        out[0] = fallbackX;
        out[1] = fallbackY;
        out[2] = fallbackZ;
    }
}

void TransitionIfNeeded(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
                        D3D12_RESOURCE_STATES& curState, D3D12_RESOURCE_STATES dstState)
{
    if (curState == dstState || !res) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type       = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {res, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, curState, dstState};
    cmd->ResourceBarrier(1u, &b);
    curState = dstState;
}

} // namespace

RawWaterCausticsHost::~RawWaterCausticsHost()
{
    Shutdown();
}

D3D12_CPU_DESCRIPTOR_HANDLE RawWaterCausticsHost::CpuAt(uint32_t slot) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(slot) * m_descInc;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE RawWaterCausticsHost::GpuAt(uint32_t slot) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE h = m_heap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<UINT64>(slot) * m_descInc;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE RawWaterCausticsHost::GetCausticsSRV() const
{
    return m_ready ? GpuAt(kSlotCausticsSrv) : D3D12_GPU_DESCRIPTOR_HANDLE{0ull};
}

D3D12_GPU_DESCRIPTOR_HANDLE RawWaterCausticsHost::GetUnderwaterRaysSRV() const
{
    return m_ready ? GpuAt(kSlotRaysSrv) : D3D12_GPU_DESCRIPTOR_HANDLE{0ull};
}

bool RawWaterCausticsHost::Init(ID3D12Device* dev, const WaterCausticsDesc& desc, std::string* outErr)
{
    Shutdown();
    if (!dev) {
        if (outErr) *outErr = "RawWaterCausticsHost::Init: null ID3D12Device";
        return false;
    }
    m_dev = dev;

    // 1. Shader-visible descriptor heap (u0 Caustics, u1 Rays, t0 CausticsSRV, t1 RaysSRV)
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = kTotalDescCount;
    hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_heap)))) {
        if (outErr) *outErr = "RawWaterCausticsHost: CreateDescriptorHeap failed";
        Shutdown();
        return false;
    }
    m_descInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // 2. Root signature: [0]=CBV(b0), [1]=UAV table(u0..u1), [2]=SRV table(t0), static linear-wrap sampler(s0)
    D3D12_DESCRIPTOR_RANGE uavRange{};
    uavRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors                    = 2u;
    uavRange.BaseShaderRegister                = 0u;
    uavRange.RegisterSpace                     = 0u;
    uavRange.OffsetInDescriptorsFromTableStart = 0u;

    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors                    = 1u;
    srvRange.BaseShaderRegister                = 0u;
    srvRange.RegisterSpace                     = 0u;
    srvRange.OffsetInDescriptorsFromTableStart = 0u;

    D3D12_ROOT_PARAMETER params[3]{};
    params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0u;
    params[0].Descriptor.RegisterSpace  = 0u;
    params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

    params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1u;
    params[1].DescriptorTable.pDescriptorRanges   = &uavRange;
    params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

    params[2].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1u;
    params[2].DescriptorTable.pDescriptorRanges   = &srvRange;
    params[2].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC samp{};
    samp.Filter           = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samp.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samp.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samp.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samp.MipLODBias       = 0.0f;
    samp.MaxAnisotropy    = 1u;
    samp.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
    samp.BorderColor      = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    samp.MinLOD           = 0.0f;
    samp.MaxLOD           = D3D12_FLOAT32_MAX;
    samp.ShaderRegister   = 0u;
    samp.RegisterSpace    = 0u;
    samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters     = 3u;
    rsd.pParameters       = params;
    rsd.NumStaticSamplers = 1u;
    rsd.pStaticSamplers   = &samp;
    rsd.Flags             = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    Microsoft::WRL::ComPtr<ID3DBlob> sigBlob, errBlob;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob))) {
        if (outErr) {
            *outErr = "RawWaterCausticsHost: D3D12SerializeRootSignature failed";
            if (errBlob && errBlob->GetBufferPointer()) {
                *outErr += ": ";
                *outErr += static_cast<const char*>(errBlob->GetBufferPointer());
            }
        }
        Shutdown();
        return false;
    }
    if (FAILED(dev->CreateRootSignature(0u, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
                                        IID_PPV_ARGS(&m_root)))) {
        if (outErr) *outErr = "RawWaterCausticsHost: CreateRootSignature failed";
        Shutdown();
        return false;
    }

    // 3. Compute PSOs (cs_caustics_trace & cs_underwater_rays)
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = m_root.Get();
    if (desc.customCausticsShader.pShaderBytecode && desc.customCausticsShader.BytecodeLength > 0u) {
        psoDesc.CS = desc.customCausticsShader;
    } else {
        psoDesc.CS = {kDxil_raw_water_caustics_trace, sizeof(kDxil_raw_water_caustics_trace)};
    }
    if (FAILED(dev->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_causticsPso)))) {
        if (outErr) *outErr = "RawWaterCausticsHost: CreateComputePipelineState(caustics) failed";
        Shutdown();
        return false;
    }

    if (desc.customRaysShader.pShaderBytecode && desc.customRaysShader.BytecodeLength > 0u) {
        psoDesc.CS = desc.customRaysShader;
    } else {
        psoDesc.CS = {kDxil_raw_water_underwater_rays, sizeof(kDxil_raw_water_underwater_rays)};
    }
    if (FAILED(dev->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_raysPso)))) {
        if (outErr) *outErr = "RawWaterCausticsHost: CreateComputePipelineState(rays) failed";
        Shutdown();
        return false;
    }

    // 4. Persistent upload constant buffer ring (4 x 256 bytes)
    {
        D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_UPLOAD};
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width            = static_cast<UINT64>(kRingCount) * kCbStride;
        rd.Height           = 1u;
        rd.DepthOrArraySize = 1u;
        rd.MipLevels        = 1u;
        rd.SampleDesc.Count = 1u;
        rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                IID_PPV_ARGS(&m_cbBuf)))) {
            if (outErr) *outErr = "RawWaterCausticsHost: CreateCommittedResource(CB) failed";
            Shutdown();
            return false;
        }
        D3D12_RANGE rr{0u, 0u};
        void* mapped = nullptr;
        if (FAILED(m_cbBuf->Map(0u, &rr, &mapped)) || !mapped) {
            if (outErr) *outErr = "RawWaterCausticsHost: CB Map failed";
            Shutdown();
            return false;
        }
        m_cbMapped = static_cast<uint8_t*>(mapped);
    }

    // 5. Hardware GPU timestamp profiler ring (4 frames x 3 timestamps = 12 queries)
    {
        D3D12_QUERY_HEAP_DESC qd{};
        qd.Type     = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        qd.Count    = kTotalTsQueries;
        qd.NodeMask = 0u;
        if (SUCCEEDED(dev->CreateQueryHeap(&qd, IID_PPV_ARGS(&m_tsHeap)))) {
            D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_READBACK};
            D3D12_RESOURCE_DESC rd{};
            rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
            rd.Width            = static_cast<UINT64>(kTotalTsQueries) * sizeof(uint64_t);
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

    const uint32_t initW = std::max(desc.initialWidth, 16u);
    const uint32_t initH = std::max(desc.initialHeight, 16u);
    if (!EnsureTargets(initW, initH)) {
        if (outErr) *outErr = "RawWaterCausticsHost: EnsureTargets failed";
        Shutdown();
        return false;
    }

    m_frameIdx       = 0u;
    m_lastCausticsMs = 0.0f;
    m_lastRaysMs     = 0.0f;
    m_hasValidTiming = false;
    m_ready          = true;
    return true;
}

bool RawWaterCausticsHost::EnsureTargets(uint32_t width, uint32_t height)
{
    if (m_causticsTex && m_raysTex && m_outW == width && m_outH == height) {
        return true;
    }
    if (!m_dev) return false;

    m_causticsTex.Reset();
    m_raysTex.Reset();

    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width            = width;
    rd.Height           = height;
    rd.DepthOrArraySize = 1u;
    rd.MipLevels        = 1u;
    rd.Format           = DXGI_FORMAT_R16G16B16A16_FLOAT;
    rd.SampleDesc.Count = 1u;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    if (FAILED(m_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                              D3D12_RESOURCE_STATE_COMMON, nullptr,
                                              IID_PPV_ARGS(&m_causticsTex)))) {
        return false;
    }
    if (FAILED(m_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                              D3D12_RESOURCE_STATE_COMMON, nullptr,
                                              IID_PPV_ARGS(&m_raysTex)))) {
        m_causticsTex.Reset();
        return false;
    }

    m_causticsState = D3D12_RESOURCE_STATE_COMMON;
    m_raysState     = D3D12_RESOURCE_STATE_COMMON;
    m_outW          = width;
    m_outH          = height;

    // Create UAVs & SRVs in shader-visible heap
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavd{};
    uavd.Format        = DXGI_FORMAT_R16G16B16A16_FLOAT;
    uavd.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    m_dev->CreateUnorderedAccessView(m_causticsTex.Get(), nullptr, &uavd, CpuAt(kSlotCausticsUav));
    m_dev->CreateUnorderedAccessView(m_raysTex.Get(), nullptr, &uavd, CpuAt(kSlotRaysUav));

    D3D12_SHADER_RESOURCE_VIEW_DESC srvd{};
    srvd.Format                  = DXGI_FORMAT_R16G16B16A16_FLOAT;
    srvd.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvd.Texture2D.MipLevels     = 1u;
    m_dev->CreateShaderResourceView(m_causticsTex.Get(), &srvd, CpuAt(kSlotCausticsSrv));
    m_dev->CreateShaderResourceView(m_raysTex.Get(), &srvd, CpuAt(kSlotRaysSrv));

    return true;
}

bool RawWaterCausticsHost::HarvestTimestampTriplet(uint32_t queryStartIdx,
                                                   float& outCausticsMs,
                                                   float& outRaysMs) const
{
    if (!m_tsReadback || m_tsFreqHz == 0u) return false;
    const SIZE_T byteOff = static_cast<SIZE_T>(queryStartIdx) * sizeof(uint64_t);
    D3D12_RANGE rr{byteOff, byteOff + kTsPerFrame * sizeof(uint64_t)};
    void* ptr = nullptr;
    if (FAILED(m_tsReadback->Map(0u, &rr, &ptr)) || !ptr) return false;
    uint64_t t[3] = {0u, 0u, 0u};
    std::memcpy(t, static_cast<const uint8_t*>(ptr) + byteOff, sizeof(t));
    D3D12_RANGE wr{0u, 0u};
    m_tsReadback->Unmap(0u, &wr);

    if (t[1] < t[0] || t[2] < t[1]) return false;
    const double toMs = 1000.0 / static_cast<double>(m_tsFreqHz);
    outCausticsMs = static_cast<float>(static_cast<double>(t[1] - t[0]) * toMs);
    outRaysMs     = static_cast<float>(static_cast<double>(t[2] - t[1]) * toMs);
    return std::isfinite(outCausticsMs) && std::isfinite(outRaysMs);
}

bool RawWaterCausticsHost::Execute(ID3D12GraphicsCommandList* cmd,
                                   uint32_t width,
                                   uint32_t height,
                                   const WaterCausticsParams& params)
{
    if (!m_ready || !cmd || width == 0u || height == 0u) return false;
    if (!EnsureTargets(width, height)) return false;

    const uint32_t slot = m_frameIdx % kRingCount;

    // Harvest oldest ring slot timestamp triplet (kRingCount frames behind)
    if (m_frameIdx >= kRingCount && m_tsFreqHz > 0u) {
        float cMs = 0.0f;
        float rMs = 0.0f;
        if (HarvestTimestampTriplet(slot * kTsPerFrame, cMs, rMs)) {
            m_lastCausticsMs = cMs;
            m_lastRaysMs     = rMs;
            m_hasValidTiming = true;
        }
    }

    // Populate 256-byte constant buffer slot
    CausticsGpuCB cb{};
    cb.width           = width;
    cb.height          = height;
    cb.frameIndex      = params.frameIndex;
    cb.timeSec         = params.timeSec;
    Normalize3Safe(params.sunDir, cb.sunDir, 0.0f, 1.0f, 0.0f);
    cb.sunIntensity    = std::max(params.sunIntensity, 0.0f);
    cb.sunColor[0]     = std::max(params.sunColor[0], 0.0f);
    cb.sunColor[1]     = std::max(params.sunColor[1], 0.0f);
    cb.sunColor[2]     = std::max(params.sunColor[2], 0.0f);
    cb.bedDepthM       = std::max(params.bedDepthM, 0.05f);
    cb.absorptionRGB[0] = std::max(params.absorptionRGB[0], 0.0f);
    cb.absorptionRGB[1] = std::max(params.absorptionRGB[1], 0.0f);
    cb.absorptionRGB[2] = std::max(params.absorptionRGB[2], 0.0f);
    cb.scatterCoeff    = std::max(params.scatterCoeff, 1e-4f);
    cb.waveAmp         = std::max(params.waveAmp, 0.0f);
    cb.domainSizeM     = std::max(params.domainSizeM, 0.5f);
    cb.dispersionScale = std::max(params.dispersionScale, 0.0f);
    cb.mieG            = std::clamp(params.mieG, -0.92f, 0.92f);
    cb.camPos[0]       = params.camPos[0];
    cb.camPos[1]       = params.camPos[1];
    cb.camPos[2]       = params.camPos[2];
    cb.waterSurfaceY   = params.waterSurfaceY;
    Normalize3Safe(params.camRight, cb.camRight, 1.0f, 0.0f, 0.0f);
    cb.fovYRad         = std::clamp(params.fovYDeg, 10.0f, 140.0f) * kDegToRad;
    Normalize3Safe(params.camUp, cb.camUp, 0.0f, 1.0f, 0.0f);
    cb.maxRayDistM     = std::max(params.maxRayDistM, 0.5f);
    Normalize3Safe(params.camForward, cb.camForward, 0.0f, 0.0f, 1.0f);
    cb.shaftIntensity  = std::max(params.shaftIntensity, 0.0f);

    std::memcpy(m_cbMapped + static_cast<size_t>(slot) * kCbStride, &cb, sizeof(cb));

    const D3D12_RESOURCE_STATES srvState =
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    // Transition both targets to UAV for compute passes
    TransitionIfNeeded(cmd, m_causticsTex.Get(), m_causticsState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    TransitionIfNeeded(cmd, m_raysTex.Get(), m_raysState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    ID3D12DescriptorHeap* heaps[1] = {m_heap.Get()};
    cmd->SetDescriptorHeaps(1u, heaps);
    cmd->SetComputeRootSignature(m_root.Get());
    cmd->SetComputeRootConstantBufferView(
        0u, m_cbBuf->GetGPUVirtualAddress() + static_cast<UINT64>(slot) * kCbStride);
    cmd->SetComputeRootDescriptorTable(1u, GpuAt(kSlotCausticsUav));

    const uint32_t gx = (width + 7u) / 8u;
    const uint32_t gy = (height + 7u) / 8u;
    const uint32_t qBase = slot * kTsPerFrame;

    if (m_tsHeap && m_tsReadback) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qBase + 0u);
    }

    // Pass 1: Physical Snell-Jacobian chromatic water caustics
    cmd->SetPipelineState(m_causticsPso.Get());
    cmd->Dispatch(gx, gy, 1u);

    if (m_tsHeap && m_tsReadback) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qBase + 1u);
    }

    // Transition CausticsTex UAV -> SRV so Pass 2 can sample live caustic filaments cleanly
    TransitionIfNeeded(cmd, m_causticsTex.Get(), m_causticsState, srvState);

    // Pass 2: 16-step Henyey-Greenstein underwater volumetric sun-shafts
    cmd->SetComputeRootDescriptorTable(2u, GpuAt(kSlotCausticsSrv));
    cmd->SetPipelineState(m_raysPso.Get());
    cmd->Dispatch(gx, gy, 1u);

    if (m_tsHeap && m_tsReadback) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qBase + 2u);
        cmd->ResolveQueryData(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                              qBase, kTsPerFrame, m_tsReadback.Get(),
                              static_cast<UINT64>(qBase) * sizeof(uint64_t));
    }

    // Leave UnderwaterRaysTex in SRV state as well for downstream compositors
    TransitionIfNeeded(cmd, m_raysTex.Get(), m_raysState, srvState);

    ++m_frameIdx;
    return true;
}

void RawWaterCausticsHost::Shutdown()
{
    if (m_cbBuf && m_cbMapped) {
        D3D12_RANGE wr{0u, 0u};
        m_cbBuf->Unmap(0u, &wr);
        m_cbMapped = nullptr;
    }
    m_tsReadback.Reset();
    m_tsHeap.Reset();
    m_cbBuf.Reset();
    m_raysTex.Reset();
    m_causticsTex.Reset();
    m_raysPso.Reset();
    m_causticsPso.Reset();
    m_root.Reset();
    m_heap.Reset();
    m_dev            = nullptr;
    m_descInc        = 0u;
    m_outW           = 0u;
    m_outH           = 0u;
    m_causticsState  = D3D12_RESOURCE_STATE_COMMON;
    m_raysState      = D3D12_RESOURCE_STATE_COMMON;
    m_frameIdx       = 0u;
    m_lastCausticsMs = 0.0f;
    m_lastRaysMs     = 0.0f;
    m_hasValidTiming = false;
    m_ready          = false;
}

} // namespace raw::bridge
