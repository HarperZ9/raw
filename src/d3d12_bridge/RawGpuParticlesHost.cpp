// RawGpuParticlesHost.cpp: Native DX12 GPU Compute 16,384-Particle 3D Physics & Splat Engine (SM 6.6 / 6.10).
// Original clean-room implementation, MIT License.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "RawGpuParticlesHost.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if __has_include("raw_particles_sim_cs.h")
#include "raw_particles_sim_cs.h"
#define RAW_PARTICLES_HAS_SIM_DXIL 1
#else
#define RAW_PARTICLES_HAS_SIM_DXIL 0
#endif

#if __has_include("raw_particles_splat_cs.h")
#include "raw_particles_splat_cs.h"
#define RAW_PARTICLES_HAS_SPLAT_DXIL 1
#else
#define RAW_PARTICLES_HAS_SPLAT_DXIL 0
#endif

namespace raw::bridge
{
namespace
{

constexpr uint32_t kCbStrideBytes  = 256u;
constexpr uint32_t kQueriesPerSlot = 3u; // [simStart, simEnd/splatStart, splatEnd]

struct alignas(16) ParticlesGpuCB {
    float    viewProj[16];
    float    emitterPosAndRadius[4];
    float    windAndCurl[4];
    float    physParams[4];
    float    modeParams[4];
    uint32_t countsAndFlags[4];
    uint32_t splatDims[4];
};
static_assert(sizeof(ParticlesGpuCB) <= kCbStrideBytes, "ParticlesGpuCB must fit in 256 bytes");

void MulMat4(const float a[16], const float b[16], float out[16])
{
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) {
                s += a[r * 4 + k] * b[k * 4 + c];
            }
            out[r * 4 + c] = s;
        }
    }
}

} // namespace

void BuildParticlesViewProjLH(const float eye[3], const float target[3], const float up[3],
                              float fovYRad, float aspect, float nearZ, float farZ,
                              float outViewProj[16])
{
    float zAxis[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
    float zLen = std::sqrt(zAxis[0] * zAxis[0] + zAxis[1] * zAxis[1] + zAxis[2] * zAxis[2]);
    if (zLen <= 1.0e-6f) {
        zAxis[0] = 0.0f; zAxis[1] = 0.0f; zAxis[2] = 1.0f;
    } else {
        zAxis[0] /= zLen; zAxis[1] /= zLen; zAxis[2] /= zLen;
    }

    // xAxis = normalize(cross(up, zAxis))
    float xAxis[3] = {
        up[1] * zAxis[2] - up[2] * zAxis[1],
        up[2] * zAxis[0] - up[0] * zAxis[2],
        up[0] * zAxis[1] - up[1] * zAxis[0]
    };
    float xLen = std::sqrt(xAxis[0] * xAxis[0] + xAxis[1] * xAxis[1] + xAxis[2] * xAxis[2]);
    if (xLen <= 1.0e-6f) {
        xAxis[0] = 1.0f; xAxis[1] = 0.0f; xAxis[2] = 0.0f;
    } else {
        xAxis[0] /= xLen; xAxis[1] /= xLen; xAxis[2] /= xLen;
    }

    // yAxis = cross(zAxis, xAxis)
    float yAxis[3] = {
        zAxis[1] * xAxis[2] - zAxis[2] * xAxis[1],
        zAxis[2] * xAxis[0] - zAxis[0] * xAxis[2],
        zAxis[0] * xAxis[1] - zAxis[1] * xAxis[0]
    };

    const float tx = -(xAxis[0] * eye[0] + xAxis[1] * eye[1] + xAxis[2] * eye[2]);
    const float ty = -(yAxis[0] * eye[0] + yAxis[1] * eye[1] + yAxis[2] * eye[2]);
    const float tz = -(zAxis[0] * eye[0] + zAxis[1] * eye[1] + zAxis[2] * eye[2]);

    const float view[16] = {
        xAxis[0], yAxis[0], zAxis[0], 0.0f,
        xAxis[1], yAxis[1], zAxis[1], 0.0f,
        xAxis[2], yAxis[2], zAxis[2], 0.0f,
        tx,       ty,       tz,       1.0f
    };

    const float clampedFov = std::clamp(fovYRad, 0.1f, 2.8f);
    const float clampedAsp = std::max(aspect, 0.1f);
    const float clampedN   = std::max(nearZ, 1.0e-3f);
    const float clampedF   = std::max(farZ, clampedN + 0.1f);
    const float yScale     = 1.0f / std::tan(clampedFov * 0.5f);
    const float xScale     = yScale / clampedAsp;
    const float q          = clampedF / (clampedF - clampedN);

    const float proj[16] = {
        xScale, 0.0f,   0.0f,           0.0f,
        0.0f,   yScale, 0.0f,           0.0f,
        0.0f,   0.0f,   q,              1.0f,
        0.0f,   0.0f,   -clampedN * q,  0.0f
    };

    MulMat4(view, proj, outViewProj);
}

RawGpuParticlesHost::~RawGpuParticlesHost()
{
    Shutdown();
}

D3D12_CPU_DESCRIPTOR_HANDLE RawGpuParticlesHost::CpuAt(uint32_t slot) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(slot) * m_descInc;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE RawGpuParticlesHost::GpuAt(uint32_t slot) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE h = m_heap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<UINT64>(slot) * m_descInc;
    return h;
}

D3D12_CPU_DESCRIPTOR_HANDLE RawGpuParticlesHost::ClearCpuAt(uint32_t slot) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_clearHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(slot) * m_descInc;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE RawGpuParticlesHost::GetSplatSRV() const
{
    if (!m_heap) return {};
    return GpuAt(3u);
}

bool RawGpuParticlesHost::Init(ID3D12Device* dev, const GpuParticlesDesc& desc, std::string* outErr)
{
    Shutdown();
    if (!dev) {
        if (outErr) *outErr = "RawGpuParticlesHost::Init: null device";
        return false;
    }
    m_dev = dev;

    D3D12_SHADER_BYTECODE simCs = desc.customSimShader;
    D3D12_SHADER_BYTECODE splatCs = desc.customSplatShader;
#if RAW_PARTICLES_HAS_SIM_DXIL
    if (!simCs.pShaderBytecode || simCs.BytecodeLength == 0u) {
        simCs.pShaderBytecode = kDxil_raw_particles_sim;
        simCs.BytecodeLength  = sizeof(kDxil_raw_particles_sim);
    }
#endif
#if RAW_PARTICLES_HAS_SPLAT_DXIL
    if (!splatCs.pShaderBytecode || splatCs.BytecodeLength == 0u) {
        splatCs.pShaderBytecode = kDxil_raw_particles_splat;
        splatCs.BytecodeLength  = sizeof(kDxil_raw_particles_splat);
    }
#endif
    if (!simCs.pShaderBytecode || simCs.BytecodeLength == 0u ||
        !splatCs.pShaderBytecode || splatCs.BytecodeLength == 0u) {
        if (outErr) *outErr = "RawGpuParticlesHost::Init: missing cs_particles_sim / cs_particles_splat DXIL bytecode";
        Shutdown();
        return false;
    }

    m_particleCount = std::clamp(desc.particleCount, 64u, kMaxParticles);
    m_splatW        = std::max(desc.splatWidth, 32u);
    m_splatH        = std::max(desc.splatHeight, 32u);

    // Shader-visible heap (u0 Particles, u1 Stats, u2 SplatBuf, t0 SplatSRV)
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.NumDescriptors = kTotalDescs;
    hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(m_dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_heap)))) {
        if (outErr) *outErr = "RawGpuParticlesHost::Init: CreateDescriptorHeap (shader-visible) failed";
        Shutdown();
        return false;
    }
    // Non-shader-visible CPU heap required by D3D12 ClearUnorderedAccessViewUint
    D3D12_DESCRIPTOR_HEAP_DESC chd{};
    chd.NumDescriptors = 2u; // [0]=Stats UAV, [1]=SplatBuf UAV
    chd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    chd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(m_dev->CreateDescriptorHeap(&chd, IID_PPV_ARGS(&m_clearHeap)))) {
        if (outErr) *outErr = "RawGpuParticlesHost::Init: CreateDescriptorHeap (CPU clear) failed";
        Shutdown();
        return false;
    }
    m_descInc = m_dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Root signature: [0] Root CBV (b0), [1] UAV table (u0..u2)
    D3D12_DESCRIPTOR_RANGE uavRange{};
    uavRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors                    = 3u;
    uavRange.BaseShaderRegister                = 0u;
    uavRange.OffsetInDescriptorsFromTableStart = 0u;

    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0u;
    params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

    params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1u;
    params[1].DescriptorTable.pDescriptorRanges   = &uavRange;
    params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = 2u;
    rsd.pParameters   = params;

    Microsoft::WRL::ComPtr<ID3DBlob> sigBlob, errBlob;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob)) ||
        FAILED(m_dev->CreateRootSignature(0u, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
                                          IID_PPV_ARGS(&m_root)))) {
        if (outErr) *outErr = "RawGpuParticlesHost::Init: CreateRootSignature failed";
        Shutdown();
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = m_root.Get();
    psoDesc.CS             = simCs;
    if (FAILED(m_dev->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_simPso)))) {
        if (outErr) *outErr = "RawGpuParticlesHost::Init: CreateComputePipelineState (sim) failed";
        Shutdown();
        return false;
    }
    psoDesc.CS = splatCs;
    if (FAILED(m_dev->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_splatPso)))) {
        if (outErr) *outErr = "RawGpuParticlesHost::Init: CreateComputePipelineState (splat) failed";
        Shutdown();
        return false;
    }

    auto CreateUavBuf = [&](UINT64 byteWidth, Microsoft::WRL::ComPtr<ID3D12Resource>& outBuf) -> bool {
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width            = byteWidth;
        rd.Height           = 1u;
        rd.DepthOrArraySize = 1u;
        rd.MipLevels        = 1u;
        rd.SampleDesc.Count = 1u;
        rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        return SUCCEEDED(m_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                        nullptr, IID_PPV_ARGS(&outBuf)));
    };

    const UINT64 particleBytes = static_cast<UINT64>(m_particleCount) * sizeof(GpuParticleCpuView);
    const UINT64 statsBytes    = 64ull; // 16 bytes used (4 uint counters), padded to 64
    const UINT64 splatBytes    = static_cast<UINT64>(m_splatW) * m_splatH * 16ull;

    if (!CreateUavBuf(particleBytes, m_particleBuf) ||
        !CreateUavBuf(statsBytes, m_statsBuf) ||
        !CreateUavBuf(splatBytes, m_splatBuf)) {
        if (outErr) *outErr = "RawGpuParticlesHost::Init: create GPU particle/stats/splat buffers failed";
        Shutdown();
        return false;
    }

    // Create descriptors:
    //   u0: StructuredBuffer<GpuParticle>
    D3D12_UNORDERED_ACCESS_VIEW_DESC pUav{};
    pUav.Format                     = DXGI_FORMAT_UNKNOWN;
    pUav.ViewDimension              = D3D12_UAV_DIMENSION_BUFFER;
    pUav.Buffer.NumElements         = m_particleCount;
    pUav.Buffer.StructureByteStride = sizeof(GpuParticleCpuView);
    m_dev->CreateUnorderedAccessView(m_particleBuf.Get(), nullptr, &pUav, CpuAt(0u));

    //   u1: Raw ByteAddressBuffer stats (both shader-visible and CPU clear heap)
    D3D12_UNORDERED_ACCESS_VIEW_DESC stUav{};
    stUav.Format             = DXGI_FORMAT_R32_TYPELESS;
    stUav.ViewDimension      = D3D12_UAV_DIMENSION_BUFFER;
    stUav.Buffer.NumElements = static_cast<UINT>(statsBytes / 4ull);
    stUav.Buffer.Flags       = D3D12_BUFFER_UAV_FLAG_RAW;
    m_dev->CreateUnorderedAccessView(m_statsBuf.Get(), nullptr, &stUav, CpuAt(1u));
    m_dev->CreateUnorderedAccessView(m_statsBuf.Get(), nullptr, &stUav, ClearCpuAt(0u));

    //   u2: Raw ByteAddressBuffer splat accumulator (both shader-visible and CPU clear heap)
    D3D12_UNORDERED_ACCESS_VIEW_DESC spUav{};
    spUav.Format             = DXGI_FORMAT_R32_TYPELESS;
    spUav.ViewDimension      = D3D12_UAV_DIMENSION_BUFFER;
    spUav.Buffer.NumElements = static_cast<UINT>(splatBytes / 4ull);
    spUav.Buffer.Flags       = D3D12_BUFFER_UAV_FLAG_RAW;
    m_dev->CreateUnorderedAccessView(m_splatBuf.Get(), nullptr, &spUav, CpuAt(2u));
    m_dev->CreateUnorderedAccessView(m_splatBuf.Get(), nullptr, &spUav, ClearCpuAt(1u));

    //   slot 3: Consumer SRV for SplatBuffer (ByteAddressBuffer)
    D3D12_SHADER_RESOURCE_VIEW_DESC spSrv{};
    spSrv.Format                  = DXGI_FORMAT_R32_TYPELESS;
    spSrv.ViewDimension           = D3D12_SRV_DIMENSION_BUFFER;
    spSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    spSrv.Buffer.NumElements      = static_cast<UINT>(splatBytes / 4ull);
    spSrv.Buffer.Flags            = D3D12_BUFFER_SRV_FLAG_RAW;
    m_dev->CreateShaderResourceView(m_splatBuf.Get(), &spSrv, CpuAt(3u));

    // 4-frame persistently mapped CB ring
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
        if (outErr) *outErr = "RawGpuParticlesHost::Init: create CB ring failed";
        Shutdown();
        return false;
    }
    D3D12_RANGE rr{0u, 0u};
    void* mapped = nullptr;
    if (FAILED(m_cbBuf->Map(0u, &rr, &mapped)) || !mapped) {
        if (outErr) *outErr = "RawGpuParticlesHost::Init: map CB ring failed";
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

    m_ready = true;
    return true;
}

bool RawGpuParticlesHost::HarvestTimestampTriplet(uint32_t queryStartIdx, float& outSimMs, float& outSplatMs) const
{
    if (!m_tsReadback || m_tsFreqHz == 0u) return false;
    const SIZE_T byteOff = static_cast<SIZE_T>(queryStartIdx) * sizeof(uint64_t);
    D3D12_RANGE rr{byteOff, byteOff + 3u * sizeof(uint64_t)};
    void* mapped = nullptr;
    if (FAILED(m_tsReadback->Map(0u, &rr, &mapped)) || !mapped) return false;
    const auto* ticks = reinterpret_cast<const uint64_t*>(static_cast<const uint8_t*>(mapped) + byteOff);
    const uint64_t t0 = ticks[0];
    const uint64_t t1 = ticks[1];
    const uint64_t t2 = ticks[2];
    D3D12_RANGE wr{0u, 0u};
    m_tsReadback->Unmap(0u, &wr);
    if (t1 < t0 || t2 < t1) return false;
    const double invFreqMs = 1000.0 / static_cast<double>(m_tsFreqHz);
    outSimMs   = static_cast<float>(static_cast<double>(t1 - t0) * invFreqMs);
    outSplatMs = static_cast<float>(static_cast<double>(t2 - t1) * invFreqMs);
    return std::isfinite(outSimMs) && std::isfinite(outSplatMs);
}

bool RawGpuParticlesHost::Execute(ID3D12GraphicsCommandList* cmd, const GpuParticlesParams& params)
{
    if (!m_ready || !cmd) return false;

    const uint32_t ringSlot = m_frameIdx % kRingCount;
    if (m_frameIdx >= kRingCount && m_tsHeap && m_tsReadback && m_tsFreqHz > 0u) {
        float simMs = 0.0f, splatMs = 0.0f;
        if (HarvestTimestampTriplet(ringSlot * kQueriesPerSlot, simMs, splatMs)) {
            m_lastSimMs      = simMs;
            m_lastSplatMs    = splatMs;
            m_hasValidTiming = true;
        }
    }

    ParticlesGpuCB cb{};
    std::memcpy(cb.viewProj, params.viewProj, sizeof(cb.viewProj));
    cb.emitterPosAndRadius[0] = std::isfinite(params.emitterPos[0]) ? params.emitterPos[0] : 0.0f;
    cb.emitterPosAndRadius[1] = std::isfinite(params.emitterPos[1]) ? params.emitterPos[1] : 1.0f;
    cb.emitterPosAndRadius[2] = std::isfinite(params.emitterPos[2]) ? params.emitterPos[2] : 0.0f;
    cb.emitterPosAndRadius[3] = std::max(params.emitterRadius, 0.01f);

    cb.windAndCurl[0]         = std::isfinite(params.windVel[0]) ? params.windVel[0] : 0.0f;
    cb.windAndCurl[1]         = std::isfinite(params.windVel[1]) ? params.windVel[1] : 0.0f;
    cb.windAndCurl[2]         = std::isfinite(params.windVel[2]) ? params.windVel[2] : 0.0f;
    cb.windAndCurl[3]         = std::max(params.curlStrength, 0.0f);

    cb.physParams[0]          = std::clamp(params.dtSec, 0.0f, 0.15f);
    cb.physParams[1]          = std::isfinite(params.timeSec) ? params.timeSec : 0.0f;
    cb.physParams[2]          = std::isfinite(params.groundY) ? params.groundY : 0.0f;
    cb.physParams[3]          = std::clamp(params.restitution, 0.0f, 0.95f);

    cb.modeParams[0]          = std::clamp(params.gravityScale, 0.0f, 5.0f);
    cb.modeParams[1]          = std::clamp(params.buoyancyAccel, 0.0f, 30.0f);
    cb.modeParams[2]          = std::clamp(params.dragCoeff, 0.0f, 12.0f);
    cb.modeParams[3]          = std::max(params.splatIntensity, 0.0f);

    uint32_t flags = 0u;
    if (params.resetAll)           flags |= 1u;
    if (params.enableRespawn)      flags |= 2u;
    if (params.enableGroundBounce) flags |= 4u;

    cb.countsAndFlags[0]      = m_particleCount;
    cb.countsAndFlags[1]      = static_cast<uint32_t>(params.mode);
    cb.countsAndFlags[2]      = params.frameIndex;
    cb.countsAndFlags[3]      = flags;

    cb.splatDims[0]           = m_splatW;
    cb.splatDims[1]           = m_splatH;
    cb.splatDims[2]           = 0u;
    cb.splatDims[3]           = 0u;

    const uint64_t cbOff = static_cast<uint64_t>(ringSlot) * kCbStrideBytes;
    std::memcpy(m_cbMapped + cbOff, &cb, sizeof(cb));

    ID3D12DescriptorHeap* heaps[1] = {m_heap.Get()};
    cmd->SetDescriptorHeaps(1u, heaps);

    // Zero atomic stats + screen-space additive splat buffer before simulation/splat
    const UINT zeroVals[4] = {0u, 0u, 0u, 0u};
    cmd->ClearUnorderedAccessViewUint(GpuAt(1u), ClearCpuAt(0u), m_statsBuf.Get(), zeroVals, 0u, nullptr);
    cmd->ClearUnorderedAccessViewUint(GpuAt(2u), ClearCpuAt(1u), m_splatBuf.Get(), zeroVals, 0u, nullptr);

    D3D12_RESOURCE_BARRIER clearBarriers[2]{};
    clearBarriers[0].Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    clearBarriers[0].UAV.pResource = m_statsBuf.Get();
    clearBarriers[1].Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    clearBarriers[1].UAV.pResource = m_splatBuf.Get();
    cmd->ResourceBarrier(2u, clearBarriers);

    const uint32_t qStart = ringSlot * kQueriesPerSlot;
    const uint32_t groups = (m_particleCount + 63u) / 64u;

    cmd->SetComputeRootSignature(m_root.Get());
    cmd->SetComputeRootConstantBufferView(0u, m_cbBuf->GetGPUVirtualAddress() + cbOff);
    cmd->SetComputeRootDescriptorTable(1u, GpuAt(0u));

    // Pass 1: 3D Physics Simulation (cs_particles_sim)
    if (m_tsHeap) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qStart + 0u);
    }
    cmd->SetPipelineState(m_simPso.Get());
    cmd->Dispatch(groups, 1u, 1u);

    D3D12_RESOURCE_BARRIER simBarriers[2]{};
    simBarriers[0].Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    simBarriers[0].UAV.pResource = m_particleBuf.Get();
    simBarriers[1].Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    simBarriers[1].UAV.pResource = m_statsBuf.Get();
    cmd->ResourceBarrier(2u, simBarriers);

    // Pass 2: Screen-Space Atomic Additive Splat (cs_particles_splat)
    if (m_tsHeap) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qStart + 1u);
    }
    cmd->SetPipelineState(m_splatPso.Get());
    cmd->Dispatch(groups, 1u, 1u);

    D3D12_RESOURCE_BARRIER splatBarriers[2]{};
    splatBarriers[0].Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    splatBarriers[0].UAV.pResource = m_splatBuf.Get();
    splatBarriers[1].Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    splatBarriers[1].UAV.pResource = m_statsBuf.Get();
    cmd->ResourceBarrier(2u, splatBarriers);

    if (m_tsHeap) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qStart + 2u);
        cmd->ResolveQueryData(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                              qStart, kQueriesPerSlot, m_tsReadback.Get(),
                              static_cast<UINT64>(qStart) * sizeof(uint64_t));
    }

    ++m_frameIdx;
    return true;
}

void RawGpuParticlesHost::Shutdown()
{
    if (m_cbBuf && m_cbMapped) {
        D3D12_RANGE wr{0u, 0u};
        m_cbBuf->Unmap(0u, &wr);
        m_cbMapped = nullptr;
    }
    m_tsReadback.Reset();
    m_tsHeap.Reset();
    m_cbBuf.Reset();
    m_splatBuf.Reset();
    m_statsBuf.Reset();
    m_particleBuf.Reset();
    m_splatPso.Reset();
    m_simPso.Reset();
    m_root.Reset();
    m_clearHeap.Reset();
    m_heap.Reset();
    m_dev            = nullptr;
    m_descInc        = 0u;
    m_particleCount  = 0u;
    m_splatW         = 0u;
    m_splatH         = 0u;
    m_frameIdx       = 0u;
    m_lastSimMs      = 0.0f;
    m_lastSplatMs    = 0.0f;
    m_hasValidTiming = false;
    m_ready          = false;
}

} // namespace raw::bridge
