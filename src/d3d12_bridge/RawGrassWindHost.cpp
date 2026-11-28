// RawGrassWindHost.cpp: Native DX12 GPU Compute 256x256 (65,536-Blade) Dynamic Grass Wind,
// Seasonal Snow-Weight Droop, Wildfire Char & Critically-Damped Actor-Trample Host (SM 6.6 / 6.10).
// Original clean-room implementation, MIT License.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "RawGrassWindHost.h"

#include "raw_grass_wind_step_cs.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace raw::bridge
{
namespace
{

constexpr uint32_t kCbStride       = 2048u; // 256-byte aligned, fits 32 capsules + 16 heat sources
constexpr uint32_t kTsPerFrame     = 2u;
constexpr uint32_t kTotalTsQueries = RawGrassWindHost::kCbRingCount * kTsPerFrame;

// Descriptor heap layout (8 descriptors total):
//   [0] SRV trample[0], [1] SRV bend[0]
//   [2] SRV trample[1], [3] SRV bend[1]
//   [4] UAV trample[0], [5] UAV bend[0]
//   [6] UAV trample[1], [7] UAV bend[1]
constexpr uint32_t kSrvBase[2]   = {0u, 2u};
constexpr uint32_t kUavBase[2]   = {4u, 6u};
constexpr uint32_t kTotalDescs   = 8u;

struct alignas(16) GpuCapsulePacked {
    float startX, startZ, endX, endZ;
    float velX, velZ, radiusM, flattenAngleRad;
};

struct alignas(16) GpuHeatPacked {
    float worldX, worldZ, radiusM, heatPower;
};

struct alignas(16) GrassWindGpuCB {
    uint32_t width;
    uint32_t height;
    int32_t  shiftX;
    int32_t  shiftZ;

    float    originWorldX;
    float    originWorldZ;
    float    cellSizeM;
    float    dtSec;

    float    timeSec;
    float    windSpeedMps;
    float    windDirX;
    float    windDirZ;

    float    gustStrength;
    float    springOmega;
    float    snowLoad01;
    float    charRateScale;

    uint32_t capsuleCount;
    uint32_t heatCount;
    uint32_t resetField;
    uint32_t pad0;

    GpuCapsulePacked capsules[RawGrassWindHost::kMaxCapsules];
    GpuHeatPacked    heats[RawGrassWindHost::kMaxHeatSources];
};
static_assert(sizeof(GrassWindGpuCB) <= kCbStride, "GrassWindGpuCB must fit in 2048-byte CB ring slot");

} // namespace

RawGrassWindHost::~RawGrassWindHost()
{
    Shutdown();
}

D3D12_CPU_DESCRIPTOR_HANDLE RawGrassWindHost::CpuAt(uint32_t slot) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(slot) * m_descInc;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE RawGrassWindHost::GpuAt(uint32_t slot) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE h = m_heap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<UINT64>(slot) * m_descInc;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE RawGrassWindHost::GetBendFieldSRV() const
{
    return m_ready ? GpuAt(kSrvBase[m_curIdx] + 1u) : D3D12_GPU_DESCRIPTOR_HANDLE{0ull};
}

D3D12_CPU_DESCRIPTOR_HANDLE RawGrassWindHost::GetBendFieldSRVCpu() const
{
    return m_ready ? CpuAt(kSrvBase[m_curIdx] + 1u) : D3D12_CPU_DESCRIPTOR_HANDLE{0u};
}

ID3D12Resource* RawGrassWindHost::GetBendFieldResource() const
{
    return m_ready ? m_bendTex[m_curIdx].Get() : nullptr;
}

D3D12_RESOURCE_STATES RawGrassWindHost::BendFieldResourceState() const
{
    return m_bendState[m_curIdx];
}

ID3D12Resource* RawGrassWindHost::GetTrampleStateResource() const
{
    return m_ready ? m_trampleTex[m_curIdx].Get() : nullptr;
}

D3D12_RESOURCE_STATES RawGrassWindHost::TrampleStateResourceState() const
{
    return m_trampleState[m_curIdx];
}

void RawGrassWindHost::Transition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
                                  D3D12_RESOURCE_STATES& curState, D3D12_RESOURCE_STATES target)
{
    if (!cmd || !res || curState == target) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type       = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {res, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, curState, target};
    cmd->ResourceBarrier(1u, &b);
    curState = target;
}

bool RawGrassWindHost::Init(ID3D12Device* dev, const GrassWindDesc& desc, std::string* outErr)
{
    Shutdown();
    if (!dev) {
        if (outErr) *outErr = "RawGrassWindHost::Init: null ID3D12Device";
        return false;
    }
    m_dev        = dev;
    m_width      = std::max(desc.gridWidth, 16u);
    m_height     = std::max(desc.gridHeight, 16u);
    m_worldSpanM = std::max(desc.worldSpanM, 4.0f);
    m_cellSizeM  = m_worldSpanM / static_cast<float>(m_width);

    // 1. Shader-visible descriptor heap (8 descriptors: 4 SRV + 4 UAV for ping-pong)
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = kTotalDescs;
    hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_heap)))) {
        if (outErr) *outErr = "RawGrassWindHost: CreateDescriptorHeap failed";
        Shutdown();
        return false;
    }
    m_descInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // 2. Root signature: [0]=CBV(b0), [1]=SRV table(t0..t1), [2]=UAV table(u0..u1)
    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors                    = 2u;
    srvRange.BaseShaderRegister                = 0u;
    srvRange.RegisterSpace                     = 0u;
    srvRange.OffsetInDescriptorsFromTableStart = 0u;

    D3D12_DESCRIPTOR_RANGE uavRange{};
    uavRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors                    = 2u;
    uavRange.BaseShaderRegister                = 0u;
    uavRange.RegisterSpace                     = 0u;
    uavRange.OffsetInDescriptorsFromTableStart = 0u;

    D3D12_ROOT_PARAMETER params[3]{};
    params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0u;
    params[0].Descriptor.RegisterSpace  = 0u;
    params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

    params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1u;
    params[1].DescriptorTable.pDescriptorRanges   = &srvRange;
    params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

    params[2].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1u;
    params[2].DescriptorTable.pDescriptorRanges   = &uavRange;
    params[2].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters     = 3u;
    rsd.pParameters       = params;
    rsd.NumStaticSamplers = 0u;
    rsd.pStaticSamplers   = nullptr;
    rsd.Flags             = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    Microsoft::WRL::ComPtr<ID3DBlob> sigBlob, errBlob;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob))) {
        if (outErr) {
            *outErr = "RawGrassWindHost: D3D12SerializeRootSignature failed";
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
        if (outErr) *outErr = "RawGrassWindHost: CreateRootSignature failed";
        Shutdown();
        return false;
    }

    // 3. Compute PSO
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = m_root.Get();
    if (desc.customStepShader.pShaderBytecode && desc.customStepShader.BytecodeLength > 0u) {
        psoDesc.CS = desc.customStepShader;
    } else {
        psoDesc.CS = {kDxil_raw_grass_wind_step, sizeof(kDxil_raw_grass_wind_step)};
    }
    if (FAILED(dev->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_stepPso)))) {
        if (outErr) *outErr = "RawGrassWindHost: CreateComputePipelineState failed";
        Shutdown();
        return false;
    }

    // 4. Ping-pong 256x256 RGBA16F textures (trample spring state & bend/droop/char field)
    D3D12_HEAP_PROPERTIES hpDef{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC texRd{};
    texRd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texRd.Width            = m_width;
    texRd.Height           = m_height;
    texRd.DepthOrArraySize = 1u;
    texRd.MipLevels        = 1u;
    texRd.Format           = DXGI_FORMAT_R16G16B16A16_FLOAT;
    texRd.SampleDesc.Count = 1u;
    texRd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texRd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvd{};
    srvd.Format                  = DXGI_FORMAT_R16G16B16A16_FLOAT;
    srvd.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvd.Texture2D.MipLevels     = 1u;

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavd{};
    uavd.Format        = DXGI_FORMAT_R16G16B16A16_FLOAT;
    uavd.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

    for (uint32_t i = 0u; i < 2u; ++i) {
        if (FAILED(dev->CreateCommittedResource(&hpDef, D3D12_HEAP_FLAG_NONE, &texRd,
                                                D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                IID_PPV_ARGS(&m_trampleTex[i]))) ||
            FAILED(dev->CreateCommittedResource(&hpDef, D3D12_HEAP_FLAG_NONE, &texRd,
                                                D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                IID_PPV_ARGS(&m_bendTex[i])))) {
            if (outErr) *outErr = "RawGrassWindHost: CreateCommittedResource(ping-pong) failed";
            Shutdown();
            return false;
        }
        m_trampleState[i] = D3D12_RESOURCE_STATE_COMMON;
        m_bendState[i]    = D3D12_RESOURCE_STATE_COMMON;

        dev->CreateShaderResourceView(m_trampleTex[i].Get(), &srvd, CpuAt(kSrvBase[i] + 0u));
        dev->CreateShaderResourceView(m_bendTex[i].Get(), &srvd, CpuAt(kSrvBase[i] + 1u));
        dev->CreateUnorderedAccessView(m_trampleTex[i].Get(), nullptr, &uavd, CpuAt(kUavBase[i] + 0u));
        dev->CreateUnorderedAccessView(m_bendTex[i].Get(), nullptr, &uavd, CpuAt(kUavBase[i] + 1u));
    }

    // 5. Persistent 4-slot upload CB ring (4 x 2048 bytes)
    {
        D3D12_HEAP_PROPERTIES hpUp{D3D12_HEAP_TYPE_UPLOAD};
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width            = static_cast<UINT64>(kCbRingCount) * kCbStride;
        rd.Height           = 1u;
        rd.DepthOrArraySize = 1u;
        rd.MipLevels        = 1u;
        rd.SampleDesc.Count = 1u;
        rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(dev->CreateCommittedResource(&hpUp, D3D12_HEAP_FLAG_NONE, &rd,
                                                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                IID_PPV_ARGS(&m_cbBuf)))) {
            if (outErr) *outErr = "RawGrassWindHost: CreateCommittedResource(CB) failed";
            Shutdown();
            return false;
        }
        D3D12_RANGE rr{0u, 0u};
        void* mapped = nullptr;
        if (FAILED(m_cbBuf->Map(0u, &rr, &mapped)) || !mapped) {
            if (outErr) *outErr = "RawGrassWindHost: CB Map failed";
            Shutdown();
            return false;
        }
        m_cbMapped = static_cast<uint8_t*>(mapped);
    }

    // 6. Hardware GPU timestamp profiler ring (4 frames x 2 timestamps = 8 queries)
    {
        D3D12_QUERY_HEAP_DESC qd{};
        qd.Type     = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        qd.Count    = kTotalTsQueries;
        qd.NodeMask = 0u;
        if (SUCCEEDED(dev->CreateQueryHeap(&qd, IID_PPV_ARGS(&m_tsHeap)))) {
            D3D12_HEAP_PROPERTIES hpRb{D3D12_HEAP_TYPE_READBACK};
            D3D12_RESOURCE_DESC rd{};
            rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
            rd.Width            = static_cast<UINT64>(kTotalTsQueries) * sizeof(uint64_t);
            rd.Height           = 1u;
            rd.DepthOrArraySize = 1u;
            rd.MipLevels        = 1u;
            rd.SampleDesc.Count = 1u;
            rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (FAILED(dev->CreateCommittedResource(&hpRb, D3D12_HEAP_FLAG_NONE, &rd,
                                                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                    IID_PPV_ARGS(&m_tsReadback)))) {
                m_tsHeap.Reset();
            }
        }
    }

    m_curIdx         = 0u;
    m_originCellX    = 0;
    m_originCellZ    = 0;
    m_hasOrigin      = false;
    m_pendingReset   = true;
    m_frameIdx       = 0u;
    m_lastStepMs     = 0.0f;
    m_hasValidTiming = false;
    m_ready          = true;
    return true;
}

bool RawGrassWindHost::HarvestTimestampPair(uint32_t queryStartIdx, float& outMs) const
{
    if (!m_tsReadback || m_tsFreqHz == 0u) return false;
    const SIZE_T byteOff = static_cast<SIZE_T>(queryStartIdx) * sizeof(uint64_t);
    D3D12_RANGE rr{byteOff, byteOff + kTsPerFrame * sizeof(uint64_t)};
    void* ptr = nullptr;
    if (FAILED(m_tsReadback->Map(0u, &rr, &ptr)) || !ptr) return false;
    uint64_t t[2] = {0u, 0u};
    std::memcpy(t, static_cast<const uint8_t*>(ptr) + byteOff, sizeof(t));
    D3D12_RANGE wr{0u, 0u};
    m_tsReadback->Unmap(0u, &wr);

    if (t[1] < t[0]) return false;
    const double ms = static_cast<double>(t[1] - t[0]) * (1000.0 / static_cast<double>(m_tsFreqHz));
    outMs = static_cast<float>(ms);
    return std::isfinite(outMs);
}

bool RawGrassWindHost::Execute(ID3D12GraphicsCommandList* cmd, const GrassWindParams& params)
{
    if (!m_ready || !cmd) return false;

    const uint32_t slot = m_frameIdx % kCbRingCount;
    if (m_frameIdx >= kCbRingCount && m_tsFreqHz > 0u) {
        float stepMs = 0.0f;
        if (HarvestTimestampPair(slot * kTsPerFrame, stepMs)) {
            m_lastStepMs     = stepMs;
            m_hasValidTiming = true;
        }
    }

    // Integer-texel toroidal origin snapping so trampled trails never blur on scroll
    const double camX = std::isfinite(params.camWorldX) ? static_cast<double>(params.camWorldX) : 0.0;
    const double camZ = std::isfinite(params.camWorldZ) ? static_cast<double>(params.camWorldZ) : 0.0;
    const int32_t newCellX = static_cast<int32_t>(std::floor(camX / static_cast<double>(m_cellSizeM) + 0.5));
    const int32_t newCellZ = static_cast<int32_t>(std::floor(camZ / static_cast<double>(m_cellSizeM) + 0.5));

    const bool doReset = params.resetField || m_pendingReset || !m_hasOrigin;
    int32_t shiftX = 0;
    int32_t shiftZ = 0;
    if (!doReset) {
        shiftX = newCellX - m_originCellX;
        shiftZ = newCellZ - m_originCellZ;
    }
    m_originCellX  = newCellX;
    m_originCellZ  = newCellZ;
    m_hasOrigin    = true;
    m_pendingReset = false;

    // Pack constant buffer slot
    GrassWindGpuCB cb{};
    cb.width         = m_width;
    cb.height        = m_height;
    cb.shiftX        = shiftX;
    cb.shiftZ        = shiftZ;
    cb.originWorldX  = static_cast<float>(m_originCellX) * m_cellSizeM;
    cb.originWorldZ  = static_cast<float>(m_originCellZ) * m_cellSizeM;
    cb.cellSizeM     = m_cellSizeM;
    cb.dtSec         = std::clamp(std::isfinite(params.dtSec) ? params.dtSec : (1.0f / 60.0f), 0.0f, 0.10f);
    cb.timeSec       = std::isfinite(params.timeSec) ? params.timeSec : 0.0f;
    cb.windSpeedMps  = std::max(std::isfinite(params.windSpeedMps) ? params.windSpeedMps : 0.0f, 0.0f);
    cb.windDirX      = std::isfinite(params.windDirX) ? params.windDirX : 1.0f;
    cb.windDirZ      = std::isfinite(params.windDirZ) ? params.windDirZ : 0.0f;
    cb.gustStrength  = std::clamp(std::isfinite(params.gustStrength) ? params.gustStrength : 0.75f, 0.0f, 1.25f);
    cb.springOmega   = std::clamp(std::isfinite(params.springOmega) ? params.springOmega : 2.8f, 0.25f, 12.0f);
    cb.snowLoad01    = std::clamp(std::isfinite(params.snowLoad01) ? params.snowLoad01 : 0.0f, 0.0f, 1.0f);
    cb.charRateScale = std::max(std::isfinite(params.charRateScale) ? params.charRateScale : 0.60f, 0.0f);
    cb.resetField    = doReset ? 1u : 0u;

    const uint32_t capN = (params.capsules && params.capsuleCount > 0u)
        ? std::min(params.capsuleCount, kMaxCapsules) : 0u;
    cb.capsuleCount = capN;
    for (uint32_t i = 0u; i < capN; ++i) {
        const GrassTrampleCapsule& c = params.capsules[i];
        cb.capsules[i] = {
            std::isfinite(c.startX) ? c.startX : 0.0f,
            std::isfinite(c.startZ) ? c.startZ : 0.0f,
            std::isfinite(c.endX)   ? c.endX   : 0.0f,
            std::isfinite(c.endZ)   ? c.endZ   : 0.0f,
            std::isfinite(c.velX)   ? c.velX   : 0.0f,
            std::isfinite(c.velZ)   ? c.velZ   : 0.0f,
            std::max(std::isfinite(c.radiusM) ? c.radiusM : 0.5f, 0.05f),
            std::clamp(std::isfinite(c.flattenAngleRad) ? c.flattenAngleRad : 1.22f, 0.0f, 1.38f)
        };
    }

    const uint32_t heatN = (params.heatSources && params.heatCount > 0u)
        ? std::min(params.heatCount, kMaxHeatSources) : 0u;
    cb.heatCount = heatN;
    for (uint32_t i = 0u; i < heatN; ++i) {
        const GrassHeatSource& h = params.heatSources[i];
        cb.heats[i] = {
            std::isfinite(h.worldX)    ? h.worldX    : 0.0f,
            std::isfinite(h.worldZ)    ? h.worldZ    : 0.0f,
            std::max(std::isfinite(h.radiusM) ? h.radiusM : 1.0f, 0.1f),
            std::max(std::isfinite(h.heatPower) ? h.heatPower : 0.0f, 0.0f)
        };
    }

    std::memcpy(m_cbMapped + static_cast<size_t>(slot) * kCbStride, &cb, sizeof(cb));

    const uint32_t readIdx  = m_curIdx;
    const uint32_t writeIdx = 1u - m_curIdx;
    const D3D12_RESOURCE_STATES srvState =
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    Transition(cmd, m_trampleTex[readIdx].Get(),  m_trampleState[readIdx],  srvState);
    Transition(cmd, m_bendTex[readIdx].Get(),     m_bendState[readIdx],     srvState);
    Transition(cmd, m_trampleTex[writeIdx].Get(), m_trampleState[writeIdx], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(cmd, m_bendTex[writeIdx].Get(),    m_bendState[writeIdx],    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    ID3D12DescriptorHeap* heaps[1] = {m_heap.Get()};
    cmd->SetDescriptorHeaps(1u, heaps);
    cmd->SetComputeRootSignature(m_root.Get());
    cmd->SetPipelineState(m_stepPso.Get());
    cmd->SetComputeRootConstantBufferView(
        0u, m_cbBuf->GetGPUVirtualAddress() + static_cast<UINT64>(slot) * kCbStride);
    cmd->SetComputeRootDescriptorTable(1u, GpuAt(kSrvBase[readIdx]));
    cmd->SetComputeRootDescriptorTable(2u, GpuAt(kUavBase[writeIdx]));

    const uint32_t qBase = slot * kTsPerFrame;
    if (m_tsHeap && m_tsReadback) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qBase + 0u);
    }

    cmd->Dispatch((m_width + 7u) / 8u, (m_height + 7u) / 8u, 1u);

    if (m_tsHeap && m_tsReadback) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qBase + 1u);
        cmd->ResolveQueryData(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                              qBase, kTsPerFrame, m_tsReadback.Get(),
                              static_cast<UINT64>(qBase) * sizeof(uint64_t));
    }

    // Leave newly written outputs in SRV state for downstream grass/foliage shaders
    Transition(cmd, m_trampleTex[writeIdx].Get(), m_trampleState[writeIdx], srvState);
    Transition(cmd, m_bendTex[writeIdx].Get(),    m_bendState[writeIdx],    srvState);

    m_curIdx = writeIdx;
    ++m_frameIdx;
    return true;
}

void RawGrassWindHost::Shutdown()
{
    if (m_cbBuf && m_cbMapped) {
        D3D12_RANGE wr{0u, 0u};
        m_cbBuf->Unmap(0u, &wr);
        m_cbMapped = nullptr;
    }
    m_tsReadback.Reset();
    m_tsHeap.Reset();
    m_cbBuf.Reset();
    for (uint32_t i = 0u; i < 2u; ++i) {
        m_trampleTex[i].Reset();
        m_bendTex[i].Reset();
        m_trampleState[i] = D3D12_RESOURCE_STATE_COMMON;
        m_bendState[i]    = D3D12_RESOURCE_STATE_COMMON;
    }
    m_stepPso.Reset();
    m_root.Reset();
    m_heap.Reset();
    m_dev            = nullptr;
    m_descInc        = 0u;
    m_curIdx         = 0u;
    m_originCellX    = 0;
    m_originCellZ    = 0;
    m_hasOrigin      = false;
    m_pendingReset   = true;
    m_frameIdx       = 0u;
    m_lastStepMs     = 0.0f;
    m_hasValidTiming = false;
    m_ready          = false;
}

} // namespace raw::bridge
