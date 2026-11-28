// RawVolumetricFroxelHost.cpp: Native DX12 GPU Compute Host for 160x90x64 Exponential-Z
// Volumetric Froxel Fog, Mie Scattering, Local Point-Light Halos & Temporal Reprojection.
// Original clean-room implementation, MIT License.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "RawVolumetricFroxelHost.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if __has_include("raw_froxel_inject_cs.h") && __has_include("raw_froxel_integrate_cs.h")
#define RAW_FROXEL_HAS_EMBEDDED_DXIL 1
#include "raw_froxel_inject_cs.h"
#include "raw_froxel_integrate_cs.h"
#else
#define RAW_FROXEL_HAS_EMBEDDED_DXIL 0
#endif

namespace raw::bridge
{
namespace
{
using Microsoft::WRL::ComPtr;

// GPU constant buffer matching RawVolumetricFroxelSim.hlsl FroxelCB byte-for-byte (1024 bytes).
struct alignas(256) GpuFroxelCB {
    float            invViewProj[16];       // 64
    float            prevViewProj[16];      // 64
    float            camPosAndNear[4];      // 16
    float            camFwdAndFar[4];       // 16
    float            prevCamPosAndTime[4];  // 16
    float            prevCamFwdAndHist[4];  // 16
    float            fogParams[4];          // 16
    float            weatherAndNoise[4];    // 16
    float            windAndPhase[4];       // 16
    float            sunDirAndInt[4];       // 16
    float            sunColAndAmbInt[4];    // 16
    float            ambColAndJitterZ[4];   // 16
    float            jitterXYAndLog[4];     // 16
    uint32_t         gridAndLights[4];      // 16
    FroxelPointLight lights[16];            // 512 -> 832 bytes
    uint32_t         pad[48];               // 192 -> 1024 bytes exact
};
static_assert(sizeof(GpuFroxelCB) == 1024u, "GpuFroxelCB must be 1024 bytes");

// Fixed shader-visible descriptor heap layout (8 slots):
//   Slot 0: injectVol[0] SRV
//   Slot 1: injectVol[1] SRV
//   Slot 2: injectVol[0] UAV
//   Slot 3: injectVol[1] UAV
//   Slot 4: integratedVol UAV
//   Slot 5: integratedVol SRV (external consumer)
constexpr uint32_t kTotalDescCount        = 8u;
constexpr uint32_t kSlotInjectSrvBase     = 0u;
constexpr uint32_t kSlotInjectUavBase     = 2u;
constexpr uint32_t kSlotIntegratedUav     = 4u;
constexpr uint32_t kSlotIntegratedExtSrv  = 5u;

constexpr uint32_t kQueriesPerFrame       = 3u; // [0]=start, [1]=post-inject, [2]=post-integrate
constexpr uint32_t kTotalQueries          = RawVolumetricFroxelHost::kCbRingCount * kQueriesPerFrame; // 12

float Halton(uint32_t index, uint32_t base)
{
    float f = 1.0f;
    float r = 0.0f;
    uint32_t i = index;
    while (i > 0u) {
        f /= static_cast<float>(base);
        r += f * static_cast<float>(i % base);
        i /= base;
    }
    return r;
}

void Normalize3(const float in[3], float out[3], float fbX, float fbY, float fbZ)
{
    const float len = std::sqrt(in[0] * in[0] + in[1] * in[1] + in[2] * in[2]);
    if (len > 1.0e-6f) {
        const float inv = 1.0f / len;
        out[0] = in[0] * inv;
        out[1] = in[1] * inv;
        out[2] = in[2] * inv;
    } else {
        out[0] = fbX;
        out[1] = fbY;
        out[2] = fbZ;
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

bool CreateVolume3D(ID3D12Device* dev, uint32_t w, uint32_t h, uint32_t d, ComPtr<ID3D12Resource>& out)
{
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    rd.Width            = w;
    rd.Height           = h;
    rd.DepthOrArraySize = static_cast<UINT16>(d);
    rd.MipLevels        = 1u;
    rd.Format           = DXGI_FORMAT_R16G16B16A16_FLOAT;
    rd.SampleDesc.Count = 1u;
    rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    return SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                  D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&out)));
}

void WriteVol3DSrv(ID3D12Device* dev, ID3D12Resource* res, D3D12_CPU_DESCRIPTOR_HANDLE dst)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC d{};
    d.Format                    = DXGI_FORMAT_R16G16B16A16_FLOAT;
    d.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE3D;
    d.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    d.Texture3D.MostDetailedMip = 0u;
    d.Texture3D.MipLevels       = 1u;
    dev->CreateShaderResourceView(res, &d, dst);
}

void WriteVol3DUav(ID3D12Device* dev, ID3D12Resource* res, uint32_t depth, D3D12_CPU_DESCRIPTOR_HANDLE dst)
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC d{};
    d.Format                = DXGI_FORMAT_R16G16B16A16_FLOAT;
    d.ViewDimension         = D3D12_UAV_DIMENSION_TEXTURE3D;
    d.Texture3D.MipSlice    = 0u;
    d.Texture3D.FirstWSlice = 0u;
    d.Texture3D.WSize       = depth;
    dev->CreateUnorderedAccessView(res, nullptr, &d, dst);
}

} // namespace

RawVolumetricFroxelHost::~RawVolumetricFroxelHost()
{
    Shutdown();
}

D3D12_CPU_DESCRIPTOR_HANDLE RawVolumetricFroxelHost::CpuAt(uint32_t slot) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(slot) * m_descInc;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE RawVolumetricFroxelHost::GpuAt(uint32_t slot) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE h = m_heap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<UINT64>(slot) * m_descInc;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE RawVolumetricFroxelHost::GetIntegratedVolumeSRV() const
{
    return m_heap ? GpuAt(kSlotIntegratedExtSrv) : D3D12_GPU_DESCRIPTOR_HANDLE{};
}

D3D12_CPU_DESCRIPTOR_HANDLE RawVolumetricFroxelHost::GetIntegratedVolumeSRVCpu() const
{
    return m_heap ? CpuAt(kSlotIntegratedExtSrv) : D3D12_CPU_DESCRIPTOR_HANDLE{};
}

void RawVolumetricFroxelHost::Transition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
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

bool RawVolumetricFroxelHost::HarvestTimestampRange(uint32_t idxA, uint32_t idxB, float& outMs) const
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

bool RawVolumetricFroxelHost::Init(ID3D12Device* dev, const VolumetricFroxelDesc& desc, std::string* outErr)
{
    Shutdown();
    if (!dev) {
        if (outErr) *outErr = "RawVolumetricFroxelHost::Init: null ID3D12Device";
        return false;
    }

    m_dev   = dev;
    m_gridW = std::max(8u, desc.gridWidth);
    m_gridH = std::max(8u, desc.gridHeight);
    m_gridD = std::max(8u, desc.gridDepth);

    D3D12_SHADER_BYTECODE injectBc    = desc.customInjectShader;
    D3D12_SHADER_BYTECODE integrateBc = desc.customIntegrateShader;
#if RAW_FROXEL_HAS_EMBEDDED_DXIL
    if (!injectBc.pShaderBytecode || injectBc.BytecodeLength == 0u) {
        injectBc.pShaderBytecode = kDxil_raw_froxel_inject;
        injectBc.BytecodeLength  = sizeof(kDxil_raw_froxel_inject);
    }
    if (!integrateBc.pShaderBytecode || integrateBc.BytecodeLength == 0u) {
        integrateBc.pShaderBytecode = kDxil_raw_froxel_integrate;
        integrateBc.BytecodeLength  = sizeof(kDxil_raw_froxel_integrate);
    }
#endif
    if (!injectBc.pShaderBytecode || injectBc.BytecodeLength == 0u ||
        !integrateBc.pShaderBytecode || integrateBc.BytecodeLength == 0u) {
        if (outErr) *outErr = "RawVolumetricFroxelHost::Init: missing DXIL bytecode for Froxel Inject / Integrate";
        Shutdown();
        return false;
    }

    // 1. Shader-visible CBV_SRV_UAV descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = kTotalDescCount;
    hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_heap)))) {
        if (outErr) *outErr = "RawVolumetricFroxelHost::Init: CreateDescriptorHeap failed";
        Shutdown();
        return false;
    }
    m_descInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // 2. Shared Root Signature: Root CBV (b0), SRV table (t0), UAV table (u0), Static Linear Clamp Sampler (s0)
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

        D3D12_STATIC_SAMPLER_DESC samp{};
        samp.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samp.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
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
            if (outErr) *outErr = "RawVolumetricFroxelHost::Init: CreateRootSignature failed";
            Shutdown();
            return false;
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = m_root.Get();
        pd.CS             = injectBc;
        if (FAILED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_injectPso)))) {
            if (outErr) *outErr = "RawVolumetricFroxelHost::Init: Inject PSO creation failed";
            Shutdown();
            return false;
        }
        pd.CS = integrateBc;
        if (FAILED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_integratePso)))) {
            if (outErr) *outErr = "RawVolumetricFroxelHost::Init: Integrate PSO creation failed";
            Shutdown();
            return false;
        }
    }

    // 3. Ping-pong 3D Inject Volumes + Integrated 3D Volume (RGBA16F)
    if (!CreateVolume3D(dev, m_gridW, m_gridH, m_gridD, m_injectVol[0]) ||
        !CreateVolume3D(dev, m_gridW, m_gridH, m_gridD, m_injectVol[1]) ||
        !CreateVolume3D(dev, m_gridW, m_gridH, m_gridD, m_integratedVol)) {
        if (outErr) *outErr = "RawVolumetricFroxelHost::Init: 3D froxel Texture3D creation failed";
        Shutdown();
        return false;
    }

    WriteVol3DSrv(dev, m_injectVol[0].Get(), CpuAt(kSlotInjectSrvBase + 0u));
    WriteVol3DSrv(dev, m_injectVol[1].Get(), CpuAt(kSlotInjectSrvBase + 1u));
    WriteVol3DUav(dev, m_injectVol[0].Get(), m_gridD, CpuAt(kSlotInjectUavBase + 0u));
    WriteVol3DUav(dev, m_injectVol[1].Get(), m_gridD, CpuAt(kSlotInjectUavBase + 1u));
    WriteVol3DUav(dev, m_integratedVol.Get(), m_gridD, CpuAt(kSlotIntegratedUav));
    WriteVol3DSrv(dev, m_integratedVol.Get(), CpuAt(kSlotIntegratedExtSrv));

    // 4. Persistently mapped 4-frame CB ring
    if (!CreateBuffer(dev, static_cast<uint64_t>(kCbRingCount) * sizeof(GpuFroxelCB),
                      D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, m_cbBuf)) {
        if (outErr) *outErr = "RawVolumetricFroxelHost::Init: CB ring creation failed";
        Shutdown();
        return false;
    }
    const D3D12_RANGE zeroRange{0u, 0u};
    void* mapped = nullptr;
    if (FAILED(m_cbBuf->Map(0u, &zeroRange, &mapped)) || !mapped) {
        if (outErr) *outErr = "RawVolumetricFroxelHost::Init: CB ring Map failed";
        Shutdown();
        return false;
    }
    m_cbMapped = static_cast<uint8_t*>(mapped);
    std::memset(m_cbMapped, 0, static_cast<size_t>(kCbRingCount) * sizeof(GpuFroxelCB));

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

    m_pingIdx    = 0u;
    m_hasHistory = false;
    m_ready      = true;
    return true;
}

bool RawVolumetricFroxelHost::Execute(ID3D12GraphicsCommandList* cmd, const VolumetricFroxelParams& params)
{
    if (!m_ready || !cmd) return false;

    const uint32_t ringSlot = m_frameIdx % kCbRingCount;
    const uint32_t qBase    = ringSlot * kQueriesPerFrame;

    if (m_frameIdx >= kCbRingCount && m_tsReadback && m_tsFreqHz > 0ull) {
        float injMs = 0.0f, intMs = 0.0f;
        if (HarvestTimestampRange(qBase + 0u, qBase + 1u, injMs) &&
            HarvestTimestampRange(qBase + 1u, qBase + 2u, intMs)) {
            m_lastInjectMs    = injMs;
            m_lastIntegrateMs = intMs;
            m_hasValidTiming  = true;
        }
    }

    const uint32_t currIdx = m_pingIdx;
    const uint32_t prevIdx = 1u - m_pingIdx;

    const float nearZ = std::max(params.nearZ, 1.0e-3f);
    const float farZ  = std::max(params.farZ, nearZ + 1.0f);
    const float logRatio = std::log(farZ / nearZ);

    float camFwd[3]{}, prevFwd[3]{}, sunDir[3]{};
    Normalize3(params.cameraForwardW, camFwd, 0.0f, 1.0f, 0.0f);
    Normalize3(params.prevCameraForwardW, prevFwd, camFwd[0], camFwd[1], camFwd[2]);
    Normalize3(params.sunDirW, sunDir, 0.0f, 1.0f, 0.25f);

    float jitterX = 0.0f, jitterY = 0.0f, jitterZ = 0.5f;
    if (params.enableHaltonJitter) {
        const uint32_t hIdx = (m_frameIdx % 16u) + 1u;
        jitterX = Halton(hIdx, 2u) - 0.5f;
        jitterY = Halton(hIdx, 3u) - 0.5f;
        jitterZ = 0.15f + 0.70f * Halton(hIdx, 5u);
    }

    const float effHistWeight = (params.enableTemporal && m_hasHistory)
        ? std::clamp(params.historyWeight, 0.0f, 0.95f)
        : 0.0f;

    GpuFroxelCB cb{};
    std::memcpy(cb.invViewProj, params.invViewProj, sizeof(cb.invViewProj));
    std::memcpy(cb.prevViewProj, params.prevViewProj, sizeof(cb.prevViewProj));
    cb.camPosAndNear[0]     = params.cameraPosW[0];
    cb.camPosAndNear[1]     = params.cameraPosW[1];
    cb.camPosAndNear[2]     = params.cameraPosW[2];
    cb.camPosAndNear[3]     = nearZ;
    cb.camFwdAndFar[0]      = camFwd[0];
    cb.camFwdAndFar[1]      = camFwd[1];
    cb.camFwdAndFar[2]      = camFwd[2];
    cb.camFwdAndFar[3]      = farZ;
    cb.prevCamPosAndTime[0] = params.prevCameraPosW[0];
    cb.prevCamPosAndTime[1] = params.prevCameraPosW[1];
    cb.prevCamPosAndTime[2] = params.prevCameraPosW[2];
    cb.prevCamPosAndTime[3] = params.simTimeSec;
    cb.prevCamFwdAndHist[0] = prevFwd[0];
    cb.prevCamFwdAndHist[1] = prevFwd[1];
    cb.prevCamFwdAndHist[2] = prevFwd[2];
    cb.prevCamFwdAndHist[3] = effHistWeight;
    cb.fogParams[0]         = std::max(0.0f, params.baseFogDensity);
    cb.fogParams[1]         = params.valleyFloorZ;
    cb.fogParams[2]         = std::max(1.0e-2f, params.fogScaleHeight);
    cb.fogParams[3]         = std::clamp(params.scatterAlbedo, 0.0f, 1.0f);
    cb.weatherAndNoise[0]   = std::max(0.0f, params.rainIntensity);
    cb.weatherAndNoise[1]   = std::max(0.0f, params.snowIntensity);
    cb.weatherAndNoise[2]   = std::max(0.0f, params.noiseAmp);
    cb.weatherAndNoise[3]   = std::max(1.0e-4f, params.noiseScale);
    cb.windAndPhase[0]      = params.windVelW[0];
    cb.windAndPhase[1]      = params.windVelW[1];
    cb.windAndPhase[2]      = params.windVelW[2];
    cb.windAndPhase[3]      = std::clamp(params.mieAnisotropyG, -0.95f, 0.95f);
    cb.sunDirAndInt[0]      = sunDir[0];
    cb.sunDirAndInt[1]      = sunDir[1];
    cb.sunDirAndInt[2]      = sunDir[2];
    cb.sunDirAndInt[3]      = std::max(0.0f, params.sunIntensity);
    cb.sunColAndAmbInt[0]   = std::max(0.0f, params.sunColor[0]);
    cb.sunColAndAmbInt[1]   = std::max(0.0f, params.sunColor[1]);
    cb.sunColAndAmbInt[2]   = std::max(0.0f, params.sunColor[2]);
    cb.sunColAndAmbInt[3]   = std::max(0.0f, params.ambientIntensity);
    cb.ambColAndJitterZ[0]  = std::max(0.0f, params.ambientColor[0]);
    cb.ambColAndJitterZ[1]  = std::max(0.0f, params.ambientColor[1]);
    cb.ambColAndJitterZ[2]  = std::max(0.0f, params.ambientColor[2]);
    cb.ambColAndJitterZ[3]  = jitterZ;
    cb.jitterXYAndLog[0]    = jitterX;
    cb.jitterXYAndLog[1]    = jitterY;
    cb.jitterXYAndLog[2]    = logRatio;
    cb.jitterXYAndLog[3]    = 1.0f / std::max(logRatio, 1.0e-6f);
    cb.gridAndLights[0]     = m_gridW;
    cb.gridAndLights[1]     = m_gridH;
    cb.gridAndLights[2]     = m_gridD;
    cb.gridAndLights[3]     = std::min(params.lightCount, 16u);
    for (uint32_t i = 0u; i < cb.gridAndLights[3]; ++i) {
        cb.lights[i] = params.lights[i];
    }

    std::memcpy(m_cbMapped + static_cast<size_t>(ringSlot) * sizeof(GpuFroxelCB), &cb, sizeof(cb));
    const D3D12_GPU_VIRTUAL_ADDRESS cbAddr =
        m_cbBuf->GetGPUVirtualAddress() + static_cast<uint64_t>(ringSlot) * sizeof(GpuFroxelCB);

    ID3D12DescriptorHeap* heaps[1] = {m_heap.Get()};
    cmd->SetDescriptorHeaps(1u, heaps);
    cmd->SetComputeRootSignature(m_root.Get());
    cmd->SetComputeRootConstantBufferView(0u, cbAddr);

    if (m_tsHeap && m_tsReadback) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qBase + 0u);
    }

    // Pass 1: cs_froxel_inject (prev history SRV -> curr inject UAV)
    Transition(cmd, m_injectVol[prevIdx].Get(), m_injectState[prevIdx], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_injectVol[currIdx].Get(), m_injectState[currIdx], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    cmd->SetPipelineState(m_injectPso.Get());
    cmd->SetComputeRootDescriptorTable(1u, GpuAt(kSlotInjectSrvBase + prevIdx));
    cmd->SetComputeRootDescriptorTable(2u, GpuAt(kSlotInjectUavBase + currIdx));
    cmd->Dispatch((m_gridW + 7u) / 8u, (m_gridH + 7u) / 8u, m_gridD);

    if (m_tsHeap && m_tsReadback) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qBase + 1u);
    }

    // Pass 2: cs_froxel_integrate (curr inject SRV -> integrated volume UAV)
    Transition(cmd, m_injectVol[currIdx].Get(), m_injectState[currIdx], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_integratedVol.Get(), m_integratedState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    cmd->SetPipelineState(m_integratePso.Get());
    cmd->SetComputeRootDescriptorTable(1u, GpuAt(kSlotInjectSrvBase + currIdx));
    cmd->SetComputeRootDescriptorTable(2u, GpuAt(kSlotIntegratedUav));
    cmd->Dispatch((m_gridW + 7u) / 8u, (m_gridH + 7u) / 8u, 1u);

    // Leave integrated froxel volume directly readable by both Pixel & Compute shaders
    Transition(cmd, m_integratedVol.Get(), m_integratedState,
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    if (m_tsHeap && m_tsReadback) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qBase + 2u);
        cmd->ResolveQueryData(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qBase, kQueriesPerFrame,
                              m_tsReadback.Get(), static_cast<UINT64>(qBase) * sizeof(uint64_t));
    }

    m_pingIdx    = prevIdx;
    m_hasHistory = true;
    ++m_frameIdx;
    return true;
}

void RawVolumetricFroxelHost::Shutdown()
{
    if (m_cbBuf && m_cbMapped) {
        m_cbBuf->Unmap(0u, nullptr);
        m_cbMapped = nullptr;
    }
    m_tsReadback.Reset();
    m_tsHeap.Reset();
    m_cbBuf.Reset();
    m_integratedVol.Reset();
    m_injectVol[0].Reset();
    m_injectVol[1].Reset();
    m_integratePso.Reset();
    m_injectPso.Reset();
    m_root.Reset();
    m_heap.Reset();

    m_dev             = nullptr;
    m_ready           = false;
    m_hasHistory      = false;
    m_pingIdx         = 0u;
    m_injectState[0]  = D3D12_RESOURCE_STATE_COMMON;
    m_injectState[1]  = D3D12_RESOURCE_STATE_COMMON;
    m_integratedState = D3D12_RESOURCE_STATE_COMMON;
    m_frameIdx        = 0u;
    m_lastInjectMs    = 0.0f;
    m_lastIntegrateMs = 0.0f;
    m_hasValidTiming  = false;
}

} // namespace raw::bridge
