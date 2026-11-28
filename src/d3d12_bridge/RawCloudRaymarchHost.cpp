// RawCloudRaymarchHost.cpp: Native DX12 GPU Compute Volumetric Cloud Raymarching Solver (SM 6.6 / 6.10).
// Original clean-room implementation, MIT License.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "RawCloudRaymarchHost.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if __has_include("raw_cloud_raymarch_cs.h")
#include "raw_cloud_raymarch_cs.h"
#define RAW_CLOUD_HAS_EMBEDDED_DXIL 1
#else
#define RAW_CLOUD_HAS_EMBEDDED_DXIL 0
#endif

namespace raw::bridge
{
namespace
{

constexpr uint32_t kCbStrideBytes = 256u;
constexpr uint32_t kQueriesPerSlot = 2u; // [start, end]

struct alignas(16) CloudRaymarchGpuCB {
    float    camPosAndTime[4];
    float    camRightAndFov[4];
    float    camUpAndFov[4];
    float    camFwdAndRad[4];
    float    sunDirAndInt[4];
    float    sunColAndAmb[4];
    float    skyZenAndCov[4];
    float    skyHorAndDens[4];
    float    shellParams[4];
    float    windAndScale[4];
    uint32_t dimsAndFrame[4];
};
static_assert(sizeof(CloudRaymarchGpuCB) <= kCbStrideBytes, "CloudRaymarchGpuCB must fit in 256 bytes");

void Normalize3Safe(const float inV[3], float outV[3], float fbX, float fbY, float fbZ)
{
    const float x = std::isfinite(inV[0]) ? inV[0] : fbX;
    const float y = std::isfinite(inV[1]) ? inV[1] : fbY;
    const float z = std::isfinite(inV[2]) ? inV[2] : fbZ;
    const float len = std::sqrt(x * x + y * y + z * z);
    if (!std::isfinite(len) || len <= 1.0e-6f) {
        outV[0] = fbX;
        outV[1] = fbY;
        outV[2] = fbZ;
    } else {
        outV[0] = x / len;
        outV[1] = y / len;
        outV[2] = z / len;
    }
}

} // namespace

RawCloudRaymarchHost::~RawCloudRaymarchHost()
{
    Shutdown();
}

D3D12_CPU_DESCRIPTOR_HANDLE RawCloudRaymarchHost::CpuAt(uint32_t slot) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(slot) * m_descInc;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE RawCloudRaymarchHost::GpuAt(uint32_t slot) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE h = m_heap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<UINT64>(slot) * m_descInc;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE RawCloudRaymarchHost::GetOutputSRV() const
{
    if (!m_heap) return {};
    return GpuAt(m_lastOutIdx == 0u ? kSlotExtOutSrv0 : kSlotExtOutSrv1);
}

D3D12_CPU_DESCRIPTOR_HANDLE RawCloudRaymarchHost::GetOutputSRVCpu() const
{
    if (!m_heap) return {};
    return CpuAt(m_lastOutIdx == 0u ? kSlotExtOutSrv0 : kSlotExtOutSrv1);
}

bool RawCloudRaymarchHost::EnsureTargets(uint32_t width, uint32_t height)
{
    if (!m_dev || width == 0u || height == 0u) return false;
    if (m_pingPong[0] && m_pingPong[1] && m_outW == width && m_outH == height) {
        return true;
    }

    m_pingPong[0].Reset();
    m_pingPong[1].Reset();
    m_pingState[0] = D3D12_RESOURCE_STATE_COMMON;
    m_pingState[1] = D3D12_RESOURCE_STATE_COMMON;
    m_historyValid = false;
    m_writeIdx     = 0u;
    m_lastOutIdx   = 0u;

    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC rd{};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width            = width;
    rd.Height           = height;
    rd.DepthOrArraySize = 1u;
    rd.MipLevels        = 1u;
    rd.Format           = DXGI_FORMAT_R16G16B16A16_FLOAT;
    rd.SampleDesc.Count = 1u;
    rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    for (uint32_t i = 0u; i < 2u; ++i) {
        if (FAILED(m_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                  D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                  IID_PPV_ARGS(&m_pingPong[i])))) {
            m_pingPong[0].Reset();
            m_pingPong[1].Reset();
            return false;
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format                    = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srv.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels       = 1u;
        m_dev->CreateShaderResourceView(m_pingPong[i].Get(), &srv,
                                        CpuAt(i == 0u ? kSlotExtOutSrv0 : kSlotExtOutSrv1));
    }

    m_outW = width;
    m_outH = height;
    return true;
}

bool RawCloudRaymarchHost::Init(ID3D12Device* dev, const CloudRaymarchDesc& desc, std::string* outErr)
{
    Shutdown();
    if (!dev) {
        if (outErr) *outErr = "RawCloudRaymarchHost::Init: null device";
        return false;
    }
    m_dev = dev;

    D3D12_SHADER_BYTECODE cs = desc.customShader;
#if RAW_CLOUD_HAS_EMBEDDED_DXIL
    if (!cs.pShaderBytecode || cs.BytecodeLength == 0u) {
        cs.pShaderBytecode = kDxil_raw_cloud_raymarch;
        cs.BytecodeLength  = sizeof(kDxil_raw_cloud_raymarch);
    }
#endif
    if (!cs.pShaderBytecode || cs.BytecodeLength == 0u) {
        if (outErr) *outErr = "RawCloudRaymarchHost::Init: missing cs_cloud_raymarch DXIL bytecode";
        Shutdown();
        return false;
    }

    // Shader-visible CBV/SRV/UAV descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.NumDescriptors = kTotalDescCount;
    hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(m_dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_heap)))) {
        if (outErr) *outErr = "RawCloudRaymarchHost::Init: CreateDescriptorHeap failed";
        Shutdown();
        return false;
    }
    m_descInc = m_dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Root signature:
    //   [0] Root CBV (b0)
    //   [1] Descriptor table: t0 (1 SRV: PrevHistory) + u0 (1 UAV: CloudOut)
    //   Static sampler s0: LinearClamp
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors                    = 1u;
    ranges[0].BaseShaderRegister                = 0u;
    ranges[0].OffsetInDescriptorsFromTableStart = 0u;

    ranges[1].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors                    = 1u;
    ranges[1].BaseShaderRegister                = 0u;
    ranges[1].OffsetInDescriptorsFromTableStart = 1u;

    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0u;
    params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

    params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 2u;
    params[1].DescriptorTable.pDescriptorRanges   = ranges;
    params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC samp{};
    samp.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samp.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samp.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samp.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samp.MaxLOD           = D3D12_FLOAT32_MAX;
    samp.ShaderRegister   = 0u;
    samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters     = 2u;
    rsd.pParameters       = params;
    rsd.NumStaticSamplers = 1u;
    rsd.pStaticSamplers   = &samp;

    Microsoft::WRL::ComPtr<ID3DBlob> sigBlob, errBlob;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob)) ||
        FAILED(m_dev->CreateRootSignature(0u, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
                                          IID_PPV_ARGS(&m_root)))) {
        if (outErr) {
            *outErr = (errBlob && errBlob->GetBufferPointer())
                ? std::string(static_cast<const char*>(errBlob->GetBufferPointer()), errBlob->GetBufferSize())
                : "RawCloudRaymarchHost::Init: CreateRootSignature failed";
        }
        Shutdown();
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = m_root.Get();
    psoDesc.CS             = cs;
    if (FAILED(m_dev->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_pso)))) {
        if (outErr) *outErr = "RawCloudRaymarchHost::Init: CreateComputePipelineState failed";
        Shutdown();
        return false;
    }

    // 4-frame persistently mapped constant buffer ring
    D3D12_HEAP_PROPERTIES hpUp{};
    hpUp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC cbd{};
    cbd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    cbd.Width            = static_cast<UINT64>(kRingCount) * kCbStrideBytes;
    cbd.Height           = 1u;
    cbd.DepthOrArraySize = 1u;
    cbd.MipLevels        = 1u;
    cbd.SampleDesc.Count = 1u;
    cbd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(m_dev->CreateCommittedResource(&hpUp, D3D12_HEAP_FLAG_NONE, &cbd,
                                              D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                              IID_PPV_ARGS(&m_cbBuf)))) {
        if (outErr) *outErr = "RawCloudRaymarchHost::Init: create CB ring failed";
        Shutdown();
        return false;
    }
    D3D12_RANGE rr{0u, 0u};
    void* mapped = nullptr;
    if (FAILED(m_cbBuf->Map(0u, &rr, &mapped)) || !mapped) {
        if (outErr) *outErr = "RawCloudRaymarchHost::Init: map CB ring failed";
        Shutdown();
        return false;
    }
    m_cbMapped = static_cast<uint8_t*>(mapped);

    // GPU timestamp profiler query heap + readback buffer
    D3D12_QUERY_HEAP_DESC qhd{};
    qhd.Type  = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    qhd.Count = kRingCount * kQueriesPerSlot;
    if (SUCCEEDED(m_dev->CreateQueryHeap(&qhd, IID_PPV_ARGS(&m_tsHeap)))) {
        D3D12_HEAP_PROPERTIES hpRb{};
        hpRb.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rbd{};
        rbd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        rbd.Width            = static_cast<UINT64>(qhd.Count) * sizeof(uint64_t);
        rbd.Height           = 1u;
        rbd.DepthOrArraySize = 1u;
        rbd.MipLevels        = 1u;
        rbd.SampleDesc.Count = 1u;
        rbd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(m_dev->CreateCommittedResource(&hpRb, D3D12_HEAP_FLAG_NONE, &rbd,
                                                  D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                  IID_PPV_ARGS(&m_tsReadback)))) {
            m_tsHeap.Reset();
        }
    }

    const uint32_t initW = std::max(desc.initialWidth, 16u);
    const uint32_t initH = std::max(desc.initialHeight, 16u);
    if (!EnsureTargets(initW, initH)) {
        if (outErr) *outErr = "RawCloudRaymarchHost::Init: create RGBA16F ping-pong targets failed";
        Shutdown();
        return false;
    }

    m_ready = true;
    return true;
}

bool RawCloudRaymarchHost::HarvestTimestampPair(uint32_t queryStartIdx, float& outMs) const
{
    if (!m_tsReadback || m_tsFreqHz == 0u) return false;
    const SIZE_T byteOff = static_cast<SIZE_T>(queryStartIdx) * sizeof(uint64_t);
    D3D12_RANGE rr{byteOff, byteOff + 2u * sizeof(uint64_t)};
    void* mapped = nullptr;
    if (FAILED(m_tsReadback->Map(0u, &rr, &mapped)) || !mapped) return false;
    const auto* ticks = reinterpret_cast<const uint64_t*>(static_cast<const uint8_t*>(mapped) + byteOff);
    const uint64_t t0 = ticks[0];
    const uint64_t t1 = ticks[1];
    D3D12_RANGE wr{0u, 0u};
    m_tsReadback->Unmap(0u, &wr);
    if (t1 < t0) return false;
    outMs = static_cast<float>(static_cast<double>(t1 - t0) * 1000.0 / static_cast<double>(m_tsFreqHz));
    return std::isfinite(outMs);
}

bool RawCloudRaymarchHost::Execute(ID3D12GraphicsCommandList* cmd,
                                   uint32_t width,
                                   uint32_t height,
                                   const CloudRaymarchParams& params)
{
    if (!m_ready || !cmd || width == 0u || height == 0u) return false;
    if (!EnsureTargets(width, height)) return false;

    const uint32_t ringSlot = m_frameIdx % kRingCount;
    if (m_frameIdx >= kRingCount && m_tsHeap && m_tsReadback && m_tsFreqHz > 0u) {
        float ms = 0.0f;
        if (HarvestTimestampPair(ringSlot * kQueriesPerSlot, ms)) {
            m_lastCloudMs    = ms;
            m_hasValidTiming = true;
        }
    }

    const uint32_t writeIdx = m_writeIdx & 1u;
    const uint32_t readIdx  = 1u - writeIdx;

    // Transition read history -> NON_PIXEL_SHADER_RESOURCE and write output -> UNORDERED_ACCESS
    D3D12_RESOURCE_BARRIER preBarriers[2]{};
    uint32_t preCount = 0u;
    const D3D12_RESOURCE_STATES kReadState =
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    if (m_pingState[readIdx] != kReadState) {
        preBarriers[preCount].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        preBarriers[preCount].Transition.pResource   = m_pingPong[readIdx].Get();
        preBarriers[preCount].Transition.StateBefore = m_pingState[readIdx];
        preBarriers[preCount].Transition.StateAfter  = kReadState;
        preBarriers[preCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++preCount;
        m_pingState[readIdx] = kReadState;
    }
    if (m_pingState[writeIdx] != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        preBarriers[preCount].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        preBarriers[preCount].Transition.pResource   = m_pingPong[writeIdx].Get();
        preBarriers[preCount].Transition.StateBefore = m_pingState[writeIdx];
        preBarriers[preCount].Transition.StateAfter  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        preBarriers[preCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++preCount;
        m_pingState[writeIdx] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    if (preCount > 0u) {
        cmd->ResourceBarrier(preCount, preBarriers);
    }

    // Populate per-ring descriptor slots: t0 = PrevHistory[readIdx], u0 = CloudOut[writeIdx]
    const uint32_t descBase = ringSlot * kDescsPerRingSlot;
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format                  = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels     = 1u;
        m_dev->CreateShaderResourceView(m_pingPong[readIdx].Get(), &srv, CpuAt(descBase + 0u));

        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
        uav.Format        = DXGI_FORMAT_R16G16B16A16_FLOAT;
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        m_dev->CreateUnorderedAccessView(m_pingPong[writeIdx].Get(), nullptr, &uav, CpuAt(descBase + 1u));
    }

    // Populate constant buffer
    float camR[3], camU[3], camF[3], sunD[3];
    Normalize3Safe(params.camRight,   camR, 1.0f, 0.0f, 0.0f);
    Normalize3Safe(params.camUp,      camU, 0.0f, 1.0f, 0.0f);
    Normalize3Safe(params.camForward, camF, 0.0f, 0.0f, 1.0f);
    Normalize3Safe(params.sunDir,     sunD, 0.0f, 0.30f, 0.95394f);

    const float fovYDeg = std::clamp(std::isfinite(params.fovYDeg) ? params.fovYDeg : 60.0f, 15.0f, 140.0f);
    const float tanHalfY = std::tan(0.5f * fovYDeg * (3.14159265359f / 180.0f));
    const float aspect   = static_cast<float>(width) / static_cast<float>(height);
    const float tanHalfX = tanHalfY * aspect;

    CloudRaymarchGpuCB cb{};
    cb.camPosAndTime[0]  = std::isfinite(params.camPos[0]) ? params.camPos[0] : 0.0f;
    cb.camPosAndTime[1]  = std::isfinite(params.camPos[1]) ? params.camPos[1] : 50.0f;
    cb.camPosAndTime[2]  = std::isfinite(params.camPos[2]) ? params.camPos[2] : 0.0f;
    cb.camPosAndTime[3]  = std::isfinite(params.timeSec)   ? params.timeSec   : 0.0f;

    cb.camRightAndFov[0] = camR[0];
    cb.camRightAndFov[1] = camR[1];
    cb.camRightAndFov[2] = camR[2];
    cb.camRightAndFov[3] = tanHalfX;

    cb.camUpAndFov[0]    = camU[0];
    cb.camUpAndFov[1]    = camU[1];
    cb.camUpAndFov[2]    = camU[2];
    cb.camUpAndFov[3]    = tanHalfY;

    cb.camFwdAndRad[0]   = camF[0];
    cb.camFwdAndRad[1]   = camF[1];
    cb.camFwdAndRad[2]   = camF[2];
    cb.camFwdAndRad[3]   = std::max(params.planetRadiusM, 100000.0f);

    cb.sunDirAndInt[0]   = sunD[0];
    cb.sunDirAndInt[1]   = sunD[1];
    cb.sunDirAndInt[2]   = sunD[2];
    cb.sunDirAndInt[3]   = std::max(params.sunIntensity, 0.0f);

    cb.sunColAndAmb[0]   = std::max(params.sunColor[0], 0.0f);
    cb.sunColAndAmb[1]   = std::max(params.sunColor[1], 0.0f);
    cb.sunColAndAmb[2]   = std::max(params.sunColor[2], 0.0f);
    cb.sunColAndAmb[3]   = std::max(params.ambientIntensity, 0.0f);

    cb.skyZenAndCov[0]   = std::max(params.skyZenith[0], 0.0f);
    cb.skyZenAndCov[1]   = std::max(params.skyZenith[1], 0.0f);
    cb.skyZenAndCov[2]   = std::max(params.skyZenith[2], 0.0f);
    cb.skyZenAndCov[3]   = std::clamp(params.cloudCoverage, 0.0f, 1.0f);

    cb.skyHorAndDens[0]  = std::max(params.skyHorizon[0], 0.0f);
    cb.skyHorAndDens[1]  = std::max(params.skyHorizon[1], 0.0f);
    cb.skyHorAndDens[2]  = std::max(params.skyHorizon[2], 0.0f);
    cb.skyHorAndDens[3]  = std::max(params.densityScale, 0.0f);

    cb.shellParams[0]    = std::max(params.cloudBaseM, 50.0f);
    cb.shellParams[1]    = std::max(params.cloudTopM, cb.shellParams[0] + 100.0f);
    cb.shellParams[2]    = std::clamp(params.anvilFactor, 0.0f, 1.0f);
    cb.shellParams[3]    = std::clamp(params.historyWeight, 0.0f, 0.85f);

    cb.windAndScale[0]   = std::isfinite(params.windVel[0]) ? params.windVel[0] : 0.0f;
    cb.windAndScale[1]   = std::isfinite(params.windVel[1]) ? params.windVel[1] : 0.0f;
    cb.windAndScale[2]   = std::isfinite(params.windVel[2]) ? params.windVel[2] : 0.0f;
    cb.windAndScale[3]   = std::max(params.noiseScale, 1.0e-6f);

    cb.dimsAndFrame[0]   = width;
    cb.dimsAndFrame[1]   = height;
    cb.dimsAndFrame[2]   = params.frameIndex;
    cb.dimsAndFrame[3]   = m_historyValid ? 1u : 0u;

    const uint64_t cbOff = static_cast<uint64_t>(ringSlot) * kCbStrideBytes;
    std::memcpy(m_cbMapped + cbOff, &cb, sizeof(cb));

    // Record compute dispatch with GPU timestamps
    const uint32_t qStart = ringSlot * kQueriesPerSlot;
    if (m_tsHeap) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qStart);
    }

    ID3D12DescriptorHeap* heaps[1] = {m_heap.Get()};
    cmd->SetDescriptorHeaps(1u, heaps);
    cmd->SetComputeRootSignature(m_root.Get());
    cmd->SetPipelineState(m_pso.Get());
    cmd->SetComputeRootConstantBufferView(0u, m_cbBuf->GetGPUVirtualAddress() + cbOff);
    cmd->SetComputeRootDescriptorTable(1u, GpuAt(descBase));
    cmd->Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);

    if (m_tsHeap) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qStart + 1u);
        cmd->ResolveQueryData(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                              qStart, kQueriesPerSlot, m_tsReadback.Get(),
                              static_cast<UINT64>(qStart) * sizeof(uint64_t));
    }

    // Transition write output from UNORDERED_ACCESS -> SRV for immediate consumer reads
    D3D12_RESOURCE_BARRIER post{};
    post.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    post.Transition.pResource   = m_pingPong[writeIdx].Get();
    post.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    post.Transition.StateAfter  = kReadState;
    post.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1u, &post);
    m_pingState[writeIdx] = kReadState;

    m_lastOutIdx   = writeIdx;
    m_writeIdx     = 1u - writeIdx;
    m_historyValid = true;
    ++m_frameIdx;
    return true;
}

void RawCloudRaymarchHost::Shutdown()
{
    if (m_cbBuf && m_cbMapped) {
        D3D12_RANGE wr{0u, 0u};
        m_cbBuf->Unmap(0u, &wr);
        m_cbMapped = nullptr;
    }
    m_tsReadback.Reset();
    m_tsHeap.Reset();
    m_cbBuf.Reset();
    m_pingPong[0].Reset();
    m_pingPong[1].Reset();
    m_pingState[0] = D3D12_RESOURCE_STATE_COMMON;
    m_pingState[1] = D3D12_RESOURCE_STATE_COMMON;
    m_pso.Reset();
    m_root.Reset();
    m_heap.Reset();
    m_dev            = nullptr;
    m_descInc        = 0u;
    m_outW           = 0u;
    m_outH           = 0u;
    m_writeIdx       = 0u;
    m_lastOutIdx     = 0u;
    m_historyValid   = false;
    m_frameIdx       = 0u;
    m_lastCloudMs    = 0.0f;
    m_hasValidTiming = false;
    m_ready          = false;
}

} // namespace raw::bridge
