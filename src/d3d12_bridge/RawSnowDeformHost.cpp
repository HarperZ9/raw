// RawSnowDeformHost.cpp: Native DX12 GPU Compute Host for Real-Time 512x512 Snow
// Accumulation, Campfire/Solar Thermal Melt, Slush-to-Black-Ice Refreeze &
// Mass-Conserving Footprint Deformation Field (SM 6.6 / 6.10).
//
// Original clean-room implementation, MIT License.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "RawSnowDeformHost.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(__has_include)
#if __has_include("raw_snow_deform_step_cs.h") && __has_include("raw_snow_normal_height_cs.h")
#include "raw_snow_deform_step_cs.h"
#include "raw_snow_normal_height_cs.h"
#define RAW_SNOW_DEFORM_HAS_EMBEDDED_DXIL 1
#endif
#endif
#ifndef RAW_SNOW_DEFORM_HAS_EMBEDDED_DXIL
#define RAW_SNOW_DEFORM_HAS_EMBEDDED_DXIL 0
#endif

namespace raw::bridge
{
namespace
{

// Matches SnowDeformCB in RawSnowDeformSim.hlsl (ring-aligned via kCbStrideBytes)
struct SnowDeformGpuCB {
    uint32_t gridDims[4];       // (width, height, stampCount, heatCount)
    int32_t  toroidScroll[4];   // (deltaCellX, deltaCellZ, originCellX, originCellZ)
    float    worldParams[4];    // (originWorldX, originWorldZ, cellSizeM, worldSpanM)
    float    climateParams[4];  // (dtSec, snowfallRateMps, ambientTempC, solarWarmthC)
    float    simParams[4];      // (baselineDepthM, maxDepthM, windSpeedMps, meltCoeff)
    float    windAndFlags[4];   // (windDirX, windDirZ, normalStrength, flagsBitmask)
    float    stamps[RawSnowDeformHost::kMaxStamps][4];
    float    stampMeta[RawSnowDeformHost::kMaxStamps][4];
    float    heatSources[RawSnowDeformHost::kMaxHeatSources][4];
};
static constexpr uint32_t kCbStrideBytes = ((sizeof(SnowDeformGpuCB) + 255u) & ~255u);

// Descriptor heap layout (shader-visible CBV_SRV_UAV):
//   0: m_stateTex[0] SRV
//   1: m_stateTex[1] SRV
//   2: m_stateTex[0] UAV
//   3: m_stateTex[1] UAV
//   4: m_normalTex   UAV
//   5: m_normalTex   SRV
static constexpr uint32_t kSlotStateSrv0  = 0u;
static constexpr uint32_t kSlotStateSrv1  = 1u;
static constexpr uint32_t kSlotStateUav0  = 2u;
static constexpr uint32_t kSlotStateUav1  = 3u;
static constexpr uint32_t kSlotNormalUav  = 4u;
static constexpr uint32_t kSlotNormalSrv  = 5u;
static constexpr uint32_t kTotalDescCount = 6u;

// 3 timestamps per ring frame: [stepStart, stepEnd/normalStart, normalEnd]
static constexpr uint32_t kTsPerFrame = 3u;
static constexpr uint32_t kTotalTs    = RawSnowDeformHost::kCbRingCount * kTsPerFrame;

void WriteTex2DSrv(ID3D12Device* dev, ID3D12Resource* res, DXGI_FORMAT fmt, D3D12_CPU_DESCRIPTOR_HANDLE dst)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC d{};
    d.Format                  = fmt;
    d.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    d.Texture2D.MipLevels     = 1u;
    dev->CreateShaderResourceView(res, &d, dst);
}

void WriteTex2DUav(ID3D12Device* dev, ID3D12Resource* res, DXGI_FORMAT fmt, D3D12_CPU_DESCRIPTOR_HANDLE dst)
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC d{};
    d.Format        = fmt;
    d.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    dev->CreateUnorderedAccessView(res, nullptr, &d, dst);
}

bool CreateTex2DRgba16F(ID3D12Device* dev, uint32_t w, uint32_t h, Microsoft::WRL::ComPtr<ID3D12Resource>& outTex)
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
    return SUCCEEDED(dev->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&outTex)));
}

} // namespace

RawSnowDeformHost::~RawSnowDeformHost()
{
    Shutdown();
}

D3D12_CPU_DESCRIPTOR_HANDLE RawSnowDeformHost::CpuAt(uint32_t slot) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(slot) * m_descInc;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE RawSnowDeformHost::GpuAt(uint32_t slot) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE h = m_heap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<UINT64>(slot) * m_descInc;
    return h;
}

void RawSnowDeformHost::Transition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
                                   D3D12_RESOURCE_STATES& curState, D3D12_RESOURCE_STATES target)
{
    if (!res || curState == target) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = res;
    b.Transition.StateBefore = curState;
    b.Transition.StateAfter  = target;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1u, &b);
    curState = target;
}

bool RawSnowDeformHost::HarvestTimestampRange(uint32_t idxA, uint32_t idxB, float& outMs) const
{
    if (!m_tsReadback || m_tsFreqHz == 0ull) return false;
    void* mapped = nullptr;
    const D3D12_RANGE rr{0u, static_cast<SIZE_T>(kTotalTs) * sizeof(uint64_t)};
    if (FAILED(m_tsReadback->Map(0u, &rr, &mapped)) || !mapped) return false;
    const auto* ticks = static_cast<const uint64_t*>(mapped);
    const uint64_t t0 = ticks[idxA];
    const uint64_t t1 = ticks[idxB];
    const D3D12_RANGE wr{0u, 0u};
    m_tsReadback->Unmap(0u, &wr);
    if (t1 < t0) return false;
    outMs = static_cast<float>(static_cast<double>(t1 - t0) * 1000.0 / static_cast<double>(m_tsFreqHz));
    return std::isfinite(outMs);
}

bool RawSnowDeformHost::Init(ID3D12Device* dev, const SnowDeformDesc& desc, std::string* outErr)
{
    Shutdown();
    if (!dev) {
        if (outErr) *outErr = "RawSnowDeformHost::Init: null ID3D12Device";
        return false;
    }

    m_dev        = dev;
    m_width      = std::clamp(desc.gridWidth, 64u, 2048u);
    m_height     = std::clamp(desc.gridHeight, 64u, 2048u);
    m_worldSpanM = std::clamp(desc.worldSpanM, 4.0f, 1024.0f);
    m_cellSizeM  = m_worldSpanM / static_cast<float>(m_width);

    D3D12_SHADER_BYTECODE stepBc = desc.customStepShader;
    D3D12_SHADER_BYTECODE normBc = desc.customNormalShader;
#if RAW_SNOW_DEFORM_HAS_EMBEDDED_DXIL
    if (!stepBc.pShaderBytecode || stepBc.BytecodeLength == 0u) {
        stepBc.pShaderBytecode = kDxil_raw_snow_deform_step;
        stepBc.BytecodeLength  = sizeof(kDxil_raw_snow_deform_step);
    }
    if (!normBc.pShaderBytecode || normBc.BytecodeLength == 0u) {
        normBc.pShaderBytecode = kDxil_raw_snow_normal_height;
        normBc.BytecodeLength  = sizeof(kDxil_raw_snow_normal_height);
    }
#endif
    if (!stepBc.pShaderBytecode || stepBc.BytecodeLength == 0u ||
        !normBc.pShaderBytecode || normBc.BytecodeLength == 0u) {
        if (outErr) *outErr = "RawSnowDeformHost::Init: missing DXIL bytecode for SnowDeform passes";
        Shutdown();
        return false;
    }

    // 1. Shader-visible descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = kTotalDescCount;
    hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_heap)))) {
        if (outErr) *outErr = "RawSnowDeformHost::Init: CreateDescriptorHeap failed";
        Shutdown();
        return false;
    }
    m_descInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // 2. Shared Root Signature: Root CBV (b0), SRV table (t0), UAV table (u0)
    {
        D3D12_DESCRIPTOR_RANGE rSrv{};
        rSrv.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        rSrv.NumDescriptors                    = 1u;
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
        rsd.Flags         = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        Microsoft::WRL::ComPtr<ID3DBlob> sigBlob, errBlob;
        if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob)) ||
            FAILED(dev->CreateRootSignature(0u, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
                                            IID_PPV_ARGS(&m_root)))) {
            if (outErr) *outErr = "RawSnowDeformHost::Init: CreateRootSignature failed";
            Shutdown();
            return false;
        }
    }

    // 3. Compute PSOs (cs_snow_deform_step + cs_snow_normal_height)
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = m_root.Get();
        pd.CS             = stepBc;
        if (FAILED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_stepPso)))) {
            if (outErr) *outErr = "RawSnowDeformHost::Init: CreateComputePipelineState(step) failed";
            Shutdown();
            return false;
        }
        pd.CS = normBc;
        if (FAILED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_normalPso)))) {
            if (outErr) *outErr = "RawSnowDeformHost::Init: CreateComputePipelineState(normal) failed";
            Shutdown();
            return false;
        }
    }

    // 4. Ping-pong state textures + normal/sparkle output texture (RGBA16F)
    if (!CreateTex2DRgba16F(dev, m_width, m_height, m_stateTex[0]) ||
        !CreateTex2DRgba16F(dev, m_width, m_height, m_stateTex[1]) ||
        !CreateTex2DRgba16F(dev, m_width, m_height, m_normalTex)) {
        if (outErr) *outErr = "RawSnowDeformHost::Init: failed to allocate RGBA16F snow textures";
        Shutdown();
        return false;
    }
    m_stateResState[0] = D3D12_RESOURCE_STATE_COMMON;
    m_stateResState[1] = D3D12_RESOURCE_STATE_COMMON;
    m_normalState      = D3D12_RESOURCE_STATE_COMMON;

    WriteTex2DSrv(dev, m_stateTex[0].Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, CpuAt(kSlotStateSrv0));
    WriteTex2DSrv(dev, m_stateTex[1].Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, CpuAt(kSlotStateSrv1));
    WriteTex2DUav(dev, m_stateTex[0].Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, CpuAt(kSlotStateUav0));
    WriteTex2DUav(dev, m_stateTex[1].Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, CpuAt(kSlotStateUav1));
    WriteTex2DUav(dev, m_normalTex.Get(),   DXGI_FORMAT_R16G16B16A16_FLOAT, CpuAt(kSlotNormalUav));
    WriteTex2DSrv(dev, m_normalTex.Get(),   DXGI_FORMAT_R16G16B16A16_FLOAT, CpuAt(kSlotNormalSrv));

    // 5. Persistent upload constant buffer ring (4 frames)
    {
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width            = static_cast<UINT64>(kCbStrideBytes) * kCbRingCount;
        rd.Height           = 1u;
        rd.DepthOrArraySize = 1u;
        rd.MipLevels        = 1u;
        rd.SampleDesc.Count = 1u;
        rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_cbBuf)))) {
            if (outErr) *outErr = "RawSnowDeformHost::Init: constant buffer alloc failed";
            Shutdown();
            return false;
        }
        const D3D12_RANGE rr{0u, 0u};
        void* p = nullptr;
        if (FAILED(m_cbBuf->Map(0u, &rr, &p)) || !p) {
            if (outErr) *outErr = "RawSnowDeformHost::Init: constant buffer Map failed";
            Shutdown();
            return false;
        }
        m_cbMapped = static_cast<uint8_t*>(p);
    }

    // 6. Internal GPU timestamp query heap & readback ring (best-effort)
    {
        D3D12_QUERY_HEAP_DESC qhd{};
        qhd.Type  = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        qhd.Count = kTotalTs;
        if (SUCCEEDED(dev->CreateQueryHeap(&qhd, IID_PPV_ARGS(&m_tsHeap)))) {
            D3D12_HEAP_PROPERTIES hpRb{};
            hpRb.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC rbd{};
            rbd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
            rbd.Width            = static_cast<UINT64>(kTotalTs) * sizeof(uint64_t);
            rbd.Height           = 1u;
            rbd.DepthOrArraySize = 1u;
            rbd.MipLevels        = 1u;
            rbd.SampleDesc.Count = 1u;
            rbd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (FAILED(dev->CreateCommittedResource(&hpRb, D3D12_HEAP_FLAG_NONE, &rbd,
                                                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_tsReadback)))) {
                m_tsHeap.Reset();
            }
        }
    }

    m_pendingReset   = true;
    m_hasOrigin      = false;
    m_stateIdx       = 0u;
    m_frameIdx       = 0u;
    m_lastStepMs     = 0.0f;
    m_lastNormalMs   = 0.0f;
    m_hasValidTiming = false;
    m_ready          = true;
    return true;
}

bool RawSnowDeformHost::Execute(ID3D12GraphicsCommandList* cmd, const SnowDeformParams& params)
{
    if (!m_ready || !cmd) return false;

    const uint32_t ringSlot = m_frameIdx % kCbRingCount;

    // Harvest completed GPU timestamps from ringSlot before reusing it
    if (m_frameIdx >= kCbRingCount && m_tsHeap && m_tsReadback && m_tsFreqHz > 0ull) {
        float sMs = 0.0f, nMs = 0.0f;
        const uint32_t base = ringSlot * kTsPerFrame;
        if (HarvestTimestampRange(base + 0u, base + 1u, sMs) &&
            HarvestTimestampRange(base + 1u, base + 2u, nMs)) {
            m_lastStepMs     = sMs;
            m_lastNormalMs   = nMs;
            m_hasValidTiming = true;
        }
    }

    // Snap world-anchored toroidal grid origin to integer texels centered around camera
    const float halfSpanX = 0.5f * static_cast<float>(m_width) * m_cellSizeM;
    const float halfSpanZ = 0.5f * static_cast<float>(m_height) * m_cellSizeM;
    const int32_t newOriginX = static_cast<int32_t>(std::floor((params.camWorldX - halfSpanX) / m_cellSizeM));
    const int32_t newOriginZ = static_cast<int32_t>(std::floor((params.camWorldZ - halfSpanZ) / m_cellSizeM));

    const bool doReset = params.resetField || m_pendingReset || !m_hasOrigin;
    const int32_t deltaCellX = doReset ? 0 : (newOriginX - m_originCellX);
    const int32_t deltaCellZ = doReset ? 0 : (newOriginZ - m_originCellZ);
    m_originCellX  = newOriginX;
    m_originCellZ  = newOriginZ;
    m_hasOrigin    = true;
    m_pendingReset = false;

    const uint32_t stampCount = (params.stamps != nullptr) ? std::min(params.stampCount, kMaxStamps) : 0u;
    const uint32_t heatCount  = (params.heatSources != nullptr) ? std::min(params.heatCount, kMaxHeatSources) : 0u;

    SnowDeformGpuCB cb{};
    cb.gridDims[0]     = m_width;
    cb.gridDims[1]     = m_height;
    cb.gridDims[2]     = stampCount;
    cb.gridDims[3]     = heatCount;
    cb.toroidScroll[0] = deltaCellX;
    cb.toroidScroll[1] = deltaCellZ;
    cb.toroidScroll[2] = m_originCellX;
    cb.toroidScroll[3] = m_originCellZ;
    cb.worldParams[0]  = static_cast<float>(m_originCellX) * m_cellSizeM;
    cb.worldParams[1]  = static_cast<float>(m_originCellZ) * m_cellSizeM;
    cb.worldParams[2]  = m_cellSizeM;
    cb.worldParams[3]  = m_worldSpanM;
    cb.climateParams[0] = std::clamp(params.dtSec, 0.0f, 0.25f);
    cb.climateParams[1] = std::max(params.snowfallRateMps, 0.0f);
    cb.climateParams[2] = params.ambientTempC;
    cb.climateParams[3] = std::max(params.solarWarmthC, 0.0f);
    cb.simParams[0]    = std::max(params.baselineDepthM, 0.0f);
    cb.simParams[1]    = std::max(params.maxDepthM, 0.1f);
    cb.simParams[2]    = std::max(params.windSpeedMps, 0.0f);
    cb.simParams[3]    = std::max(params.meltCoeff, 0.0f);
    cb.windAndFlags[0] = params.windDirX;
    cb.windAndFlags[1] = params.windDirZ;
    cb.windAndFlags[2] = std::max(params.normalStrength, 0.05f);
    cb.windAndFlags[3] = static_cast<float>(doReset ? 1u : 0u);

    for (uint32_t i = 0u; i < stampCount; ++i) {
        const SnowStamp& st = params.stamps[i];
        cb.stamps[i][0]     = st.worldX;
        cb.stamps[i][1]     = st.worldZ;
        cb.stamps[i][2]     = std::max(st.radiusM, m_cellSizeM * 0.5f);
        cb.stamps[i][3]     = std::max(st.compressDepthM, 0.0f);
        cb.stampMeta[i][0]  = std::clamp(st.rimRatio, 0.0f, 1.25f);
        cb.stampMeta[i][1]  = std::cos(st.headingRad);
        cb.stampMeta[i][2]  = std::sin(st.headingRad);
        cb.stampMeta[i][3]  = std::clamp(st.aspectRatio, 0.35f, 3.0f);
    }
    for (uint32_t i = 0u; i < heatCount; ++i) {
        const SnowHeatSource& hs = params.heatSources[i];
        cb.heatSources[i][0] = hs.worldX;
        cb.heatSources[i][1] = hs.worldZ;
        cb.heatSources[i][2] = std::max(hs.radiusM, 0.1f);
        cb.heatSources[i][3] = std::max(hs.heatPowerC, 0.0f);
    }

    const UINT64 cbOffset = static_cast<UINT64>(ringSlot) * kCbStrideBytes;
    std::memcpy(m_cbMapped + cbOffset, &cb, sizeof(cb));
    const D3D12_GPU_VIRTUAL_ADDRESS cbGpuAddr = m_cbBuf->GetGPUVirtualAddress() + cbOffset;

    const uint32_t readIdx  = m_stateIdx;
    const uint32_t writeIdx = 1u - m_stateIdx;

    ID3D12DescriptorHeap* heaps[1] = {m_heap.Get()};
    cmd->SetDescriptorHeaps(1u, heaps);
    cmd->SetComputeRootSignature(m_root.Get());
    cmd->SetComputeRootConstantBufferView(0u, cbGpuAddr);

    const uint32_t gx = (m_width + 7u) / 8u;
    const uint32_t gy = (m_height + 7u) / 8u;
    const uint32_t tsBase = ringSlot * kTsPerFrame;

    // --- Pass 1: cs_snow_deform_step (readIdx SRV -> writeIdx UAV) ---
    Transition(cmd, m_stateTex[readIdx].Get(),  m_stateResState[readIdx],  D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_stateTex[writeIdx].Get(), m_stateResState[writeIdx], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    if (m_tsHeap) cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, tsBase + 0u);
    cmd->SetPipelineState(m_stepPso.Get());
    cmd->SetComputeRootDescriptorTable(1u, GpuAt(readIdx == 0u ? kSlotStateSrv0 : kSlotStateSrv1));
    cmd->SetComputeRootDescriptorTable(2u, GpuAt(writeIdx == 0u ? kSlotStateUav0 : kSlotStateUav1));
    cmd->Dispatch(gx, gy, 1u);
    if (m_tsHeap) cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, tsBase + 1u);

    // --- Pass 2: cs_snow_normal_height (writeIdx SRV -> m_normalTex UAV) ---
    Transition(cmd, m_stateTex[writeIdx].Get(), m_stateResState[writeIdx], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_normalTex.Get(),          m_normalState,             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    cmd->SetPipelineState(m_normalPso.Get());
    cmd->SetComputeRootDescriptorTable(1u, GpuAt(writeIdx == 0u ? kSlotStateSrv0 : kSlotStateSrv1));
    cmd->SetComputeRootDescriptorTable(2u, GpuAt(kSlotNormalUav));
    cmd->Dispatch(gx, gy, 1u);
    if (m_tsHeap) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, tsBase + 2u);
        cmd->ResolveQueryData(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, tsBase, kTsPerFrame,
                              m_tsReadback.Get(), static_cast<UINT64>(tsBase) * sizeof(uint64_t));
    }

    // Leave normal output in SRV state ready for terrain / lighting sampling
    Transition(cmd, m_normalTex.Get(), m_normalState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    m_stateIdx = writeIdx;
    ++m_frameIdx;
    return true;
}

D3D12_GPU_DESCRIPTOR_HANDLE RawSnowDeformHost::GetStateSRV() const
{
    if (!m_heap) return {};
    return GpuAt(m_stateIdx == 0u ? kSlotStateSrv0 : kSlotStateSrv1);
}

D3D12_CPU_DESCRIPTOR_HANDLE RawSnowDeformHost::GetStateSRVCpu() const
{
    if (!m_heap) return {};
    return CpuAt(m_stateIdx == 0u ? kSlotStateSrv0 : kSlotStateSrv1);
}

D3D12_GPU_DESCRIPTOR_HANDLE RawSnowDeformHost::GetNormalSRV() const
{
    if (!m_heap) return {};
    return GpuAt(kSlotNormalSrv);
}

D3D12_CPU_DESCRIPTOR_HANDLE RawSnowDeformHost::GetNormalSRVCpu() const
{
    if (!m_heap) return {};
    return CpuAt(kSlotNormalSrv);
}

ID3D12Resource* RawSnowDeformHost::GetStateResource() const
{
    return m_stateTex[m_stateIdx].Get();
}

D3D12_RESOURCE_STATES RawSnowDeformHost::StateResourceState() const
{
    return m_stateResState[m_stateIdx];
}

void RawSnowDeformHost::Shutdown()
{
    if (m_cbBuf && m_cbMapped) {
        m_cbBuf->Unmap(0u, nullptr);
        m_cbMapped = nullptr;
    }
    m_tsReadback.Reset();
    m_tsHeap.Reset();
    m_cbBuf.Reset();
    m_normalTex.Reset();
    m_stateTex[0].Reset();
    m_stateTex[1].Reset();
    m_normalPso.Reset();
    m_stepPso.Reset();
    m_root.Reset();
    m_heap.Reset();

    m_stateResState[0] = D3D12_RESOURCE_STATE_COMMON;
    m_stateResState[1] = D3D12_RESOURCE_STATE_COMMON;
    m_normalState      = D3D12_RESOURCE_STATE_COMMON;
    m_dev              = nullptr;
    m_ready            = false;
    m_pendingReset     = true;
    m_hasOrigin        = false;
    m_stateIdx         = 0u;
    m_frameIdx         = 0u;
    m_lastStepMs       = 0.0f;
    m_lastNormalMs     = 0.0f;
    m_hasValidTiming   = false;
}

} // namespace raw::bridge
