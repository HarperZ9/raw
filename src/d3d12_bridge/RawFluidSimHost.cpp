// RawFluidSimHost.cpp: Native DX12 GPU Compute Host for 3D Eulerian Navier-Stokes Fire/Smoke
// and 2D Toroidal Shallow-Water Hydrodynamics Solvers.
// Original clean-room implementation, MIT License.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "RawFluidSimHost.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// Optional CMake-generated embedded DXIL headers (cs_6_6 / cs_6_10) from RawFluidFireSim.hlsl & RawFluidWaterSim.hlsl:
#if __has_include("raw_fluid_fire_inject_cs.h") && __has_include("raw_fluid_fire_advect_cs.h") && \
    __has_include("raw_fluid_fire_forces_cs.h") && __has_include("raw_fluid_fire_div_cs.h") && \
    __has_include("raw_fluid_fire_press_cs.h") && __has_include("raw_fluid_fire_proj_cs.h") && \
    __has_include("raw_fluid_fire_rad_cs.h") && __has_include("raw_fluid_water_inject_cs.h") && \
    __has_include("raw_fluid_water_step_cs.h") && __has_include("raw_fluid_water_normals_cs.h")
#define RAW_FLUID_HAS_EMBEDDED_DXIL 1
#include "raw_fluid_fire_inject_cs.h"
#include "raw_fluid_fire_advect_cs.h"
#include "raw_fluid_fire_forces_cs.h"
#include "raw_fluid_fire_div_cs.h"
#include "raw_fluid_fire_press_cs.h"
#include "raw_fluid_fire_proj_cs.h"
#include "raw_fluid_fire_rad_cs.h"
#include "raw_fluid_water_inject_cs.h"
#include "raw_fluid_water_step_cs.h"
#include "raw_fluid_water_normals_cs.h"
#else
#define RAW_FLUID_HAS_EMBEDDED_DXIL 0
#endif

namespace raw::bridge
{
namespace
{
using Microsoft::WRL::ComPtr;

// GPU constant buffer layouts matching RawFluidFireSim.hlsl and RawFluidWaterSim.hlsl byte-for-byte.
struct alignas(256) GpuFireSimCB {
    float    domainOriginAndCell[4];
    float    dtAndTime[4];
    float    windAndAmbient[4];
    float    physicsA[4];
    float    physicsB[4];
    float    physicsC[4];
    float    boundsAndFlags[4];
    uint32_t counts[4];
    float    emitterPosRadius[16][4];
    float    emitterThermoJet[16][4];
    float    emitterDirSoot[16][4];
    float    obstacleBoxMin[8][4];
    float    obstacleBoxMax[8][4];
    float    _pad[32];
};
static_assert(sizeof(GpuFireSimCB) == 1280u, "GpuFireSimCB must be 1280 bytes (256-byte aligned)");

struct alignas(256) GpuWaterSimCB {
    float    camAndGrid[4];
    float    prevCamAndTime[4];
    float    swePhysics[4];
    float    swellAndWind[4];
    float    foamAndRain[4];
    float    waterPlaneAndFlags[4];
    uint32_t counts[4];
    float    footTrail[32][4];
    float    footVel[32][4];
    float    _pad[36];
};
static_assert(sizeof(GpuWaterSimCB) == 1280u, "GpuWaterSimCB must be 1280 bytes (256-byte aligned)");

// Fixed descriptor heap slot layout (128 slots total, zero runtime overwrites, zero SRV/UAV state conflicts):
constexpr uint32_t kTotalDescCount = 128u;

// External consumer single SRV slots:
constexpr uint32_t kSlotExtFireThermo0 = 0u;
constexpr uint32_t kSlotExtFireThermo1 = 1u;
constexpr uint32_t kSlotExtFireRad0    = 2u;
constexpr uint32_t kSlotExtFireRad1    = 3u;
constexpr uint32_t kSlotExtWaterState  = 4u;
constexpr uint32_t kSlotExtWaterNormal = 5u;

// Fire 4-descriptor table bases (each table spans 4 consecutive slots):
// Parity p in {0, 1}:
inline constexpr uint32_t FireSrvVT(uint32_t p)     { return  8u + p * 4u; } // (Vel[p], Thermo[p], null, null)
inline constexpr uint32_t FireUavVT(uint32_t p)     { return 16u + p * 4u; } // (Vel[p], Thermo[p], null, null)
inline constexpr uint32_t FireSrvDiv(uint32_t p)    { return 24u + p * 4u; } // (Vel[1-p], Thermo[1-p], Press[p], null)
inline constexpr uint32_t FireUavDiv(uint32_t p)    { return 32u + p * 4u; } // (Vel[p], null, Press[1-p], null)
inline constexpr uint32_t FireSrvVPSame(uint32_t p) { return 40u + p * 4u; } // (Vel[p], null, Press[p], null)
inline constexpr uint32_t FireSrvVPOpp(uint32_t p)  { return 48u + p * 4u; } // (Vel[p], null, Press[1-p], null)
inline constexpr uint32_t FireUavP(uint32_t p)      { return 56u + p * 4u; } // (null, null, Press[p], null)
inline constexpr uint32_t FireUavV(uint32_t p)      { return 64u + p * 4u; } // (Vel[p], null, null, null)
inline constexpr uint32_t FireSrvRad(uint32_t p)    { return 72u + p * 4u; } // (null, Thermo[1-p], null, Rad[p])
inline constexpr uint32_t FireUavRad(uint32_t p)    { return 80u + p * 4u; } // (null, null, null, Rad[p])

// Water descriptor table bases:
constexpr uint32_t kSlotWaterSrvA    = 96u;  // 1 SRV: StateA
constexpr uint32_t kSlotWaterSrvB    = 97u;  // 1 SRV: StateB
constexpr uint32_t kSlotWaterUavA    = 100u; // 2 UAVs: (StateA, null2D)
constexpr uint32_t kSlotWaterUavB    = 102u; // 2 UAVs: (StateB, null2D)
constexpr uint32_t kSlotWaterUavNorm = 104u; // 2 UAVs: (null2D, NormalOut)

bool CreateTex3D(ID3D12Device* dev, uint32_t dim, DXGI_FORMAT fmt, ComPtr<ID3D12Resource>& out)
{
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    rd.Width = dim;
    rd.Height = dim;
    rd.DepthOrArraySize = static_cast<UINT16>(dim);
    rd.MipLevels = 1;
    rd.Format = fmt;
    rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    return SUCCEEDED(dev->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&out)));
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
    D3D12_RANGE readRange{0, 0};
    void* ptr = nullptr;
    if (FAILED(out->Map(0, &readRange, &ptr))) {
        return false;
    }
    *outMapped = static_cast<uint8_t*>(ptr);
    std::memset(*outMapped, 0, totalBytes);
    return true;
}

void WriteSrv3D(ID3D12Device* dev, ID3D12Resource* res, DXGI_FORMAT fmt, D3D12_CPU_DESCRIPTOR_HANDLE dst)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC d{};
    d.Format = fmt;
    d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
    d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    d.Texture3D.MipLevels = 1;
    dev->CreateShaderResourceView(res, &d, dst);
}

void WriteUav3D(ID3D12Device* dev, ID3D12Resource* res, DXGI_FORMAT fmt, uint32_t dim, D3D12_CPU_DESCRIPTOR_HANDLE dst)
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC d{};
    d.Format = fmt;
    d.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
    d.Texture3D.MipSlice = 0;
    d.Texture3D.FirstWSlice = 0;
    d.Texture3D.WSize = dim;
    dev->CreateUnorderedAccessView(res, nullptr, &d, dst);
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

bool CreateComputePso(ID3D12Device* dev, ID3D12RootSignature* rs, D3D12_SHADER_BYTECODE cs,
                      ComPtr<ID3D12PipelineState>& out)
{
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = rs;
    desc.CS = cs;
    return SUCCEEDED(dev->CreateComputePipelineState(&desc, IID_PPV_ARGS(&out)));
}

} // namespace

RawFluidSimHost::~RawFluidSimHost()
{
    if (m_fireCbBuf && m_fireCbMapped) {
        m_fireCbBuf->Unmap(0, nullptr);
        m_fireCbMapped = nullptr;
    }
    if (m_waterCbBuf && m_waterCbMapped) {
        m_waterCbBuf->Unmap(0, nullptr);
        m_waterCbMapped = nullptr;
    }
}

bool RawFluidSimHost::Init(ID3D12Device* dev, std::string* outErr)
{
#if RAW_FLUID_HAS_EMBEDDED_DXIL
    FluidShaderBlobs blobs{};
    blobs.fireInject   = {kDxil_raw_fluid_fire_inject,  sizeof kDxil_raw_fluid_fire_inject};
    blobs.fireAdvect   = {kDxil_raw_fluid_fire_advect,  sizeof kDxil_raw_fluid_fire_advect};
    blobs.fireForces   = {kDxil_raw_fluid_fire_forces,  sizeof kDxil_raw_fluid_fire_forces};
    blobs.fireDiv      = {kDxil_raw_fluid_fire_div,     sizeof kDxil_raw_fluid_fire_div};
    blobs.firePress    = {kDxil_raw_fluid_fire_press,   sizeof kDxil_raw_fluid_fire_press};
    blobs.fireProj     = {kDxil_raw_fluid_fire_proj,    sizeof kDxil_raw_fluid_fire_proj};
    blobs.fireRad      = {kDxil_raw_fluid_fire_rad,     sizeof kDxil_raw_fluid_fire_rad};
    blobs.waterInject  = {kDxil_raw_fluid_water_inject, sizeof kDxil_raw_fluid_water_inject};
    blobs.waterStep    = {kDxil_raw_fluid_water_step,   sizeof kDxil_raw_fluid_water_step};
    blobs.waterNormals = {kDxil_raw_fluid_water_normals, sizeof kDxil_raw_fluid_water_normals};
    return InitWithBlobs(dev, blobs, outErr);
#else
    (void)dev;
    if (outErr) {
        *outErr = "RawFluidSimHost: embedded DXIL headers not present in include path; generate via CMake DXC rules or call InitWithBlobs()";
    }
    return false;
#endif
}

bool RawFluidSimHost::InitWithBlobs(ID3D12Device* dev, const FluidShaderBlobs& blobs, std::string* outErr)
{
    m_ready = false;
    if (!dev) {
        if (outErr) *outErr = "RawFluidSimHost: null ID3D12Device";
        return false;
    }
    if (!blobs.Complete()) {
        if (outErr) *outErr = "RawFluidSimHost: incomplete FluidShaderBlobs";
        return false;
    }
    m_dev = dev;

    // 1) Allocate shader-visible CBV/SRV/UAV descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = kTotalDescCount;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_heap)))) {
        if (outErr) *outErr = "RawFluidSimHost: CreateDescriptorHeap failed";
        return false;
    }
    m_descInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // 2) Create 3D Fire textures (64x64x64) & 2D Water textures (256x256)
    constexpr DXGI_FORMAT kFmtRgba16 = DXGI_FORMAT_R16G16B16A16_FLOAT;
    constexpr DXGI_FORMAT kFmtR32    = DXGI_FORMAT_R32_FLOAT;

    for (uint32_t i = 0; i < 2; ++i) {
        if (!CreateTex3D(dev, kFireGridRes, kFmtRgba16, m_fireVel[i].res) ||
            !CreateTex3D(dev, kFireGridRes, kFmtRgba16, m_fireThermo[i].res) ||
            !CreateTex3D(dev, kFireGridRes, kFmtR32,    m_firePress[i].res) ||
            !CreateTex3D(dev, kFireGridRes, kFmtRgba16, m_fireRad[i].res) ||
            !CreateTex2D(dev, kWaterGridRes, kFmtRgba16, m_waterState[i].res)) {
            if (outErr) *outErr = "RawFluidSimHost: failed to allocate fluid GPU textures";
            return false;
        }
        m_fireVel[i].state    = D3D12_RESOURCE_STATE_COMMON;
        m_fireThermo[i].state = D3D12_RESOURCE_STATE_COMMON;
        m_firePress[i].state  = D3D12_RESOURCE_STATE_COMMON;
        m_fireRad[i].state    = D3D12_RESOURCE_STATE_COMMON;
        m_waterState[i].state = D3D12_RESOURCE_STATE_COMMON;
    }
    if (!CreateTex2D(dev, kWaterGridRes, kFmtRgba16, m_waterNormal.res)) {
        if (outErr) *outErr = "RawFluidSimHost: failed to allocate water normal texture";
        return false;
    }
    m_waterNormal.state = D3D12_RESOURCE_STATE_COMMON;

    // 3) Create Fire Compute Root Signature: b0 (CBV), t0..t3 (SRV table), u0..u3 (UAV table), s0 (LinearClamp)
    {
        D3D12_DESCRIPTOR_RANGE srvRange{};
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors = 4;
        srvRange.BaseShaderRegister = 0;

        D3D12_DESCRIPTOR_RANGE uavRange{};
        uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors = 4;
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
            FAILED(dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_fireRoot)))) {
            if (outErr) *outErr = "RawFluidSimHost: fire root signature creation failed";
            return false;
        }
    }

    // 4) Create Water Compute Root Signature: b0 (CBV), t0 (1 SRV), u0..u1 (2 UAVs), s0 (LinearWrap)
    {
        D3D12_DESCRIPTOR_RANGE srvRange{};
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors = 1;
        srvRange.BaseShaderRegister = 0;

        D3D12_DESCRIPTOR_RANGE uavRange{};
        uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors = 2;
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
        ss.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        ss.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        ss.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        ss.MaxLOD = D3D12_FLOAT32_MAX;
        ss.ShaderRegister = 0;
        ss.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rsd{3, rp, 1, &ss, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        ComPtr<ID3DBlob> sig, err;
        if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err)) ||
            FAILED(dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_waterRoot)))) {
            if (outErr) *outErr = "RawFluidSimHost: water root signature creation failed";
            return false;
        }
    }

    // 5) Create all 10 Compute PSOs (7 Fire + 3 Water)
    if (!CreateComputePso(dev, m_fireRoot.Get(),  blobs.fireInject,   m_psoFireInject)  ||
        !CreateComputePso(dev, m_fireRoot.Get(),  blobs.fireAdvect,   m_psoFireAdvect)  ||
        !CreateComputePso(dev, m_fireRoot.Get(),  blobs.fireForces,   m_psoFireForces)  ||
        !CreateComputePso(dev, m_fireRoot.Get(),  blobs.fireDiv,      m_psoFireDiv)     ||
        !CreateComputePso(dev, m_fireRoot.Get(),  blobs.firePress,    m_psoFirePress)   ||
        !CreateComputePso(dev, m_fireRoot.Get(),  blobs.fireProj,     m_psoFireProj)    ||
        !CreateComputePso(dev, m_fireRoot.Get(),  blobs.fireRad,      m_psoFireRad)     ||
        !CreateComputePso(dev, m_waterRoot.Get(), blobs.waterInject,  m_psoWaterInject) ||
        !CreateComputePso(dev, m_waterRoot.Get(), blobs.waterStep,    m_psoWaterStep)   ||
        !CreateComputePso(dev, m_waterRoot.Get(), blobs.waterNormals, m_psoWaterNormals)) {
        if (outErr) *outErr = "RawFluidSimHost: Compute PSO creation failed";
        return false;
    }

    // 6) Allocate ring-buffered constant buffers (4 frames in flight)
    if (!CreateUploadCb(dev, sizeof(GpuFireSimCB) * kCbRingCount, m_fireCbBuf, &m_fireCbMapped) ||
        !CreateUploadCb(dev, sizeof(GpuWaterSimCB) * kCbRingCount, m_waterCbBuf, &m_waterCbMapped)) {
        if (outErr) *outErr = "RawFluidSimHost: CBV upload buffer allocation failed";
        return false;
    }

    // 6b) Optional 4-frame ring GPU timestamp query heap (4 queries/frame: FireStart/End, WaterStart/End)
    {
        constexpr uint32_t kTotalQueries = kCbRingCount * 4u;
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

    // 7) Populate static descriptor tables cleanly (null descriptors in unused slots prevent D3D12 state hazards)
    WriteSrv3D(dev, m_fireThermo[0].Get(), kFmtRgba16, CpuAt(kSlotExtFireThermo0));
    WriteSrv3D(dev, m_fireThermo[1].Get(), kFmtRgba16, CpuAt(kSlotExtFireThermo1));
    WriteSrv3D(dev, m_fireRad[0].Get(),    kFmtRgba16, CpuAt(kSlotExtFireRad0));
    WriteSrv3D(dev, m_fireRad[1].Get(),    kFmtRgba16, CpuAt(kSlotExtFireRad1));
    WriteSrv2D(dev, m_waterState[0].Get(), kFmtRgba16, CpuAt(kSlotExtWaterState));
    WriteSrv2D(dev, m_waterNormal.Get(),   kFmtRgba16, CpuAt(kSlotExtWaterNormal));

    for (uint32_t p = 0; p < 2; ++p) {
        const uint32_t q = 1u - p;

        // FireSrvVT[p]: (Vel[p], Thermo[p], null, null)
        WriteSrv3D(dev, m_fireVel[p].Get(),    kFmtRgba16, CpuAt(FireSrvVT(p) + 0));
        WriteSrv3D(dev, m_fireThermo[p].Get(), kFmtRgba16, CpuAt(FireSrvVT(p) + 1));
        WriteSrv3D(dev, nullptr,               kFmtR32,    CpuAt(FireSrvVT(p) + 2));
        WriteSrv3D(dev, nullptr,               kFmtRgba16, CpuAt(FireSrvVT(p) + 3));

        // FireUavVT[p]: (Vel[p], Thermo[p], null, null)
        WriteUav3D(dev, m_fireVel[p].Get(),    kFmtRgba16, kFireGridRes, CpuAt(FireUavVT(p) + 0));
        WriteUav3D(dev, m_fireThermo[p].Get(), kFmtRgba16, kFireGridRes, CpuAt(FireUavVT(p) + 1));
        WriteUav3D(dev, nullptr,               kFmtR32,    kFireGridRes, CpuAt(FireUavVT(p) + 2));
        WriteUav3D(dev, nullptr,               kFmtRgba16, kFireGridRes, CpuAt(FireUavVT(p) + 3));

        // FireSrvDiv[p]: (Vel[q], Thermo[q], Press[p], null)
        WriteSrv3D(dev, m_fireVel[q].Get(),    kFmtRgba16, CpuAt(FireSrvDiv(p) + 0));
        WriteSrv3D(dev, m_fireThermo[q].Get(), kFmtRgba16, CpuAt(FireSrvDiv(p) + 1));
        WriteSrv3D(dev, m_firePress[p].Get(),  kFmtR32,    CpuAt(FireSrvDiv(p) + 2));
        WriteSrv3D(dev, nullptr,               kFmtRgba16, CpuAt(FireSrvDiv(p) + 3));

        // FireUavDiv[p]: (Vel[p], null, Press[q], null)
        WriteUav3D(dev, m_fireVel[p].Get(),    kFmtRgba16, kFireGridRes, CpuAt(FireUavDiv(p) + 0));
        WriteUav3D(dev, nullptr,               kFmtRgba16, kFireGridRes, CpuAt(FireUavDiv(p) + 1));
        WriteUav3D(dev, m_firePress[q].Get(),  kFmtR32,    kFireGridRes, CpuAt(FireUavDiv(p) + 2));
        WriteUav3D(dev, nullptr,               kFmtRgba16, kFireGridRes, CpuAt(FireUavDiv(p) + 3));

        // FireSrvVPSame[p]: (Vel[p], null, Press[p], null)
        WriteSrv3D(dev, m_fireVel[p].Get(),    kFmtRgba16, CpuAt(FireSrvVPSame(p) + 0));
        WriteSrv3D(dev, nullptr,               kFmtRgba16, CpuAt(FireSrvVPSame(p) + 1));
        WriteSrv3D(dev, m_firePress[p].Get(),  kFmtR32,    CpuAt(FireSrvVPSame(p) + 2));
        WriteSrv3D(dev, nullptr,               kFmtRgba16, CpuAt(FireSrvVPSame(p) + 3));

        // FireSrvVPOpp[p]: (Vel[p], null, Press[q], null)
        WriteSrv3D(dev, m_fireVel[p].Get(),    kFmtRgba16, CpuAt(FireSrvVPOpp(p) + 0));
        WriteSrv3D(dev, nullptr,               kFmtRgba16, CpuAt(FireSrvVPOpp(p) + 1));
        WriteSrv3D(dev, m_firePress[q].Get(),  kFmtR32,    CpuAt(FireSrvVPOpp(p) + 2));
        WriteSrv3D(dev, nullptr,               kFmtRgba16, CpuAt(FireSrvVPOpp(p) + 3));

        // FireUavP[p]: (null, null, Press[p], null)
        WriteUav3D(dev, nullptr,               kFmtRgba16, kFireGridRes, CpuAt(FireUavP(p) + 0));
        WriteUav3D(dev, nullptr,               kFmtRgba16, kFireGridRes, CpuAt(FireUavP(p) + 1));
        WriteUav3D(dev, m_firePress[p].Get(),  kFmtR32,    kFireGridRes, CpuAt(FireUavP(p) + 2));
        WriteUav3D(dev, nullptr,               kFmtRgba16, kFireGridRes, CpuAt(FireUavP(p) + 3));

        // FireUavV[p]: (Vel[p], null, null, null)
        WriteUav3D(dev, m_fireVel[p].Get(),    kFmtRgba16, kFireGridRes, CpuAt(FireUavV(p) + 0));
        WriteUav3D(dev, nullptr,               kFmtRgba16, kFireGridRes, CpuAt(FireUavV(p) + 1));
        WriteUav3D(dev, nullptr,               kFmtR32,    kFireGridRes, CpuAt(FireUavV(p) + 2));
        WriteUav3D(dev, nullptr,               kFmtRgba16, kFireGridRes, CpuAt(FireUavV(p) + 3));

        // FireSrvRad[p]: (null, Thermo[q], null, Rad[p])
        WriteSrv3D(dev, nullptr,               kFmtRgba16, CpuAt(FireSrvRad(p) + 0));
        WriteSrv3D(dev, m_fireThermo[q].Get(), kFmtRgba16, CpuAt(FireSrvRad(p) + 1));
        WriteSrv3D(dev, nullptr,               kFmtR32,    CpuAt(FireSrvRad(p) + 2));
        WriteSrv3D(dev, m_fireRad[p].Get(),    kFmtRgba16, CpuAt(FireSrvRad(p) + 3));

        // FireUavRad[p]: (null, null, null, Rad[p])
        WriteUav3D(dev, nullptr,               kFmtRgba16, kFireGridRes, CpuAt(FireUavRad(p) + 0));
        WriteUav3D(dev, nullptr,               kFmtRgba16, kFireGridRes, CpuAt(FireUavRad(p) + 1));
        WriteUav3D(dev, nullptr,               kFmtR32,    kFireGridRes, CpuAt(FireUavRad(p) + 2));
        WriteUav3D(dev, m_fireRad[p].Get(),    kFmtRgba16, kFireGridRes, CpuAt(FireUavRad(p) + 3));
    }

    // Water descriptor tables
    WriteSrv2D(dev, m_waterState[0].Get(), kFmtRgba16, CpuAt(kSlotWaterSrvA));
    WriteSrv2D(dev, m_waterState[1].Get(), kFmtRgba16, CpuAt(kSlotWaterSrvB));
    WriteUav2D(dev, m_waterState[0].Get(), kFmtRgba16, CpuAt(kSlotWaterUavA + 0));
    WriteUav2D(dev, nullptr,               kFmtRgba16, CpuAt(kSlotWaterUavA + 1));
    WriteUav2D(dev, m_waterState[1].Get(), kFmtRgba16, CpuAt(kSlotWaterUavB + 0));
    WriteUav2D(dev, nullptr,               kFmtRgba16, CpuAt(kSlotWaterUavB + 1));
    WriteUav2D(dev, nullptr,               kFmtRgba16, CpuAt(kSlotWaterUavNorm + 0));
    WriteUav2D(dev, m_waterNormal.Get(),   kFmtRgba16, CpuAt(kSlotWaterUavNorm + 1));

    Reset();
    m_ready = true;
    return true;
}

void RawFluidSimHost::Reset()
{
    m_fireResetNext = true;
    m_waterResetNext = true;
    m_hasPrevWaterCam = false;
    m_fireDomain = {};
    m_waterDomain = {};
}

void RawFluidSimHost::Transition(ID3D12GraphicsCommandList* cmd, TrackedTex& tex, D3D12_RESOURCE_STATES target)
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

void RawFluidSimHost::UavBarrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res)
{
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    b.UAV.pResource = res;
    cmd->ResourceBarrier(1, &b);
}

D3D12_CPU_DESCRIPTOR_HANDLE RawFluidSimHost::CpuAt(uint32_t slot) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(slot) * m_descInc;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE RawFluidSimHost::GpuAt(uint32_t slot) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE h = m_heap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<UINT64>(slot) * m_descInc;
    return h;
}

void RawFluidSimHost::SetTargetBudgetMs(float maxFireMs)
{
    m_targetBudgetMs = (maxFireMs > 0.0f) ? maxFireMs : 0.0f;
    m_headroomStreak = 0u;
}

bool RawFluidSimHost::HarvestTimestampPair(uint32_t queryStartIdx, float& outMs) const
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

void RawFluidSimHost::StepFire(ID3D12GraphicsCommandList* cmd, const FluidFireParams& params)
{
    if (!m_ready || !cmd || !m_fireCbMapped) {
        return;
    }

    // Non-blocking harvest of 3-frames-old retired Fire timestamp slot + adaptive Jacobi budget [4..8]
    if (m_fireFrameIdx >= (kCbRingCount - 1u)) {
        const uint32_t retiredSlot = (m_fireFrameIdx - (kCbRingCount - 1u)) % kCbRingCount;
        float measuredMs = 0.0f;
        if (HarvestTimestampPair(retiredSlot * 4u + 0u, measuredMs)) {
            m_timings.fireStepMs = measuredMs;
            m_timings.valid = true;
            if (m_targetBudgetMs > 0.0f) {
                if (measuredMs > m_targetBudgetMs * 1.05f) {
                    m_headroomStreak = 0u;
                    if (m_activePressureIters > 4u) {
                        m_activePressureIters -= 2u;
                    }
                } else if (measuredMs < m_targetBudgetMs * 0.80f) {
                    ++m_headroomStreak;
                    if (m_headroomStreak >= 2u && m_activePressureIters < 8u) {
                        m_activePressureIters += 2u;
                        m_headroomStreak = 0u;
                    }
                } else {
                    m_headroomStreak = 0u;
                }
            }
            m_timings.activePressureIters = m_activePressureIters;
        }
    }

    const uint32_t p = m_fireParity;
    const uint32_t q = 1u - p;
    const uint32_t reqEven = std::clamp((params.jacobiIterations + 1u) & ~1u, 2u, 32u);
    const uint32_t jacobiCount = (m_targetBudgetMs > 0.0f && reqEven <= 8u)
        ? std::clamp(m_activePressureIters, 4u, 8u)
        : reqEven;
    m_timings.activePressureIters = jacobiCount;
    auto sanitizeFinite = [](float v, float fallback) -> float {
        return std::isfinite(v) ? v : fallback;
    };

    const float cellSize = std::clamp(sanitizeFinite(params.cellSize, 4.0f), 0.05f, 64.0f);
    const float domainSpan = static_cast<float>(kFireGridRes) * cellSize;
    const float originX = sanitizeFinite(params.domainOriginW[0], 0.0f);
    const float originY = sanitizeFinite(params.domainOriginW[1], 0.0f);
    const float originZ = sanitizeFinite(params.domainOriginW[2], 0.0f);
    const float floorZLocal = std::clamp(sanitizeFinite(params.floorZLocal, 1.0f), 0.0f, static_cast<float>(kFireGridRes - 2u));
    const float ceilingZLocal = std::clamp(sanitizeFinite(params.ceilingZLocal, 63.0f), floorZLocal + 4.0f, static_cast<float>(kFireGridRes));

    GpuFireSimCB cb{};
    cb.domainOriginAndCell[0] = originX;
    cb.domainOriginAndCell[1] = originY;
    cb.domainOriginAndCell[2] = originZ;
    cb.domainOriginAndCell[3] = cellSize;

    cb.dtAndTime[0] = std::clamp(sanitizeFinite(params.dt, 1.0f / 60.0f), 1.0e-4f, 0.05f);
    cb.dtAndTime[1] = sanitizeFinite(params.timeSec, 0.0f);
    cb.dtAndTime[2] = static_cast<float>(m_fireFrameIdx);
    cb.dtAndTime[3] = 1.0f / cellSize;

    cb.windAndAmbient[0] = std::clamp(sanitizeFinite(params.windVelW[0], 0.0f), -250.0f, 250.0f);
    cb.windAndAmbient[1] = std::clamp(sanitizeFinite(params.windVelW[1], 0.0f), -250.0f, 250.0f);
    cb.windAndAmbient[2] = std::clamp(sanitizeFinite(params.windVelW[2], 0.0f), -250.0f, 250.0f);
    cb.windAndAmbient[3] = std::clamp(sanitizeFinite(params.ambientTempK, 293.15f), 200.0f, 400.0f);

    cb.physicsA[0] = std::clamp(sanitizeFinite(params.vorticityEps, 1.85f), 0.0f, 12.0f);
    cb.physicsA[1] = std::clamp(sanitizeFinite(params.buoyancyAlpha, 0.045f), 0.0f, 1.0f);
    cb.physicsA[2] = std::clamp(sanitizeFinite(params.sootWeightBeta, 6.5f), 0.0f, 64.0f);
    cb.physicsA[3] = std::clamp(sanitizeFinite(params.windDrag, 0.85f), 0.0f, 12.0f);

    cb.physicsB[0] = std::clamp(sanitizeFinite(params.arrheniusRateA, 18.0f), 0.0f, 120.0f);
    cb.physicsB[1] = std::clamp(sanitizeFinite(params.activationTempTa, 1600.0f), 100.0f, 12000.0f);
    cb.physicsB[2] = std::clamp(sanitizeFinite(params.heatReleaseQ, 2200.0f), 0.0f, 12000.0f);
    const float rawSootYield = std::clamp(sanitizeFinite(params.sootYield, 0.85f), 0.0f, 4.0f);
    cb.physicsB[3] = params.enableCeiling ? std::min(rawSootYield, 0.25f) : rawSootYield;

    cb.physicsC[0] = std::clamp(sanitizeFinite(params.radiativeCoolSig, 1.6e-10f), 0.0f, 1.0e-7f);
    cb.physicsC[1] = std::clamp(sanitizeFinite(params.velDamping, 0.25f), 0.0f, 10.0f);
    const float rawSootDamping = std::clamp(sanitizeFinite(params.sootDamping, 0.22f), 0.0f, 10.0f);
    cb.physicsC[2] = params.enableCeiling ? std::max(rawSootDamping, 1.65f) : rawSootDamping;
    cb.physicsC[3] = std::clamp(sanitizeFinite(params.thermalExpansion, 1.65f), 0.0f, 12.0f);

    const uint32_t flagBits = (m_fireResetNext ? 1u : 0u)
                            | (params.enableCeiling ? 2u : 0u)
                            | (params.debugOverlayMode ? 4u : 0u);
    cb.boundsAndFlags[0] = floorZLocal;
    cb.boundsAndFlags[1] = ceilingZLocal;
    cb.boundsAndFlags[2] = std::clamp(sanitizeFinite(params.scatterExtinction, 1.65f), 0.25f, 8.0f);
    cb.boundsAndFlags[3] = static_cast<float>(flagBits);

    // Strict emitter hygiene: reject non-finite, oversized neutral room-fill lights, cool non-flame lights, and
    // off-domain points; clamp active fire emitters tightly (radius <= 14u) and keep Z grounded inside [floor, ceiling].
    const uint32_t rawEmCount = std::min(params.emitterCount, 16u);
    const float floorWorldZ = originZ + floorZLocal * cellSize;
    const float ceilingWorldZ = originZ + ceilingZLocal * cellSize;
    uint32_t validEmCount = 0u;
    for (uint32_t i = 0; i < rawEmCount; ++i) {
        const auto& em = params.emitters[i];
        if (!std::isfinite(em.posW[0]) || !std::isfinite(em.posW[1]) || !std::isfinite(em.posW[2]) ||
            !std::isfinite(em.radius) || !std::isfinite(em.ignitionTempK) || !std::isfinite(em.fuelRate)) {
            continue;
        }
        if (em.radius <= 0.05f || em.radius > kRejectOversizedRadiusU ||
            em.ignitionTempK < kMinEmitterIgnitionK || em.fuelRate <= 0.01f) {
            continue;
        }
        const float clampedRadius = std::clamp(em.radius, 2.0f, kMaxEmitterRadiusU);
        const float margin = clampedRadius * 1.5f;
        if (em.posW[0] < originX - margin || em.posW[0] > originX + domainSpan + margin ||
            em.posW[1] < originY - margin || em.posW[1] > originY + domainSpan + margin ||
            em.posW[2] < originZ - margin || em.posW[2] > originZ + domainSpan + margin) {
            continue;
        }

        float posZ = std::max(em.posW[2], floorWorldZ + clampedRadius * 0.35f);
        if (params.enableCeiling) {
            posZ = std::min(posZ, std::max(floorWorldZ + clampedRadius * 0.35f, ceilingWorldZ - clampedRadius * 1.25f));
        }

        float dx = sanitizeFinite(em.jetDir[0], 0.0f);
        float dy = sanitizeFinite(em.jetDir[1], 0.0f);
        float dz = sanitizeFinite(em.jetDir[2], 1.0f);
        const float dLen = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dLen > 1.0e-3f) {
            dx /= dLen;
            dy /= dLen;
            dz /= dLen;
        } else {
            dx = 0.0f;
            dy = 0.0f;
            dz = 1.0f;
        }

        const float rawSootSeed = std::clamp(sanitizeFinite(em.sootSeedRate, 0.35f), 0.0f, 1.5f);
        const float clampedSootSeed = params.enableCeiling ? std::min(rawSootSeed, 0.12f) : rawSootSeed;

        cb.emitterPosRadius[validEmCount][0] = em.posW[0];
        cb.emitterPosRadius[validEmCount][1] = em.posW[1];
        cb.emitterPosRadius[validEmCount][2] = posZ;
        cb.emitterPosRadius[validEmCount][3] = clampedRadius;
        cb.emitterThermoJet[validEmCount][0] = std::clamp(em.ignitionTempK, kMinEmitterIgnitionK, 3600.0f);
        cb.emitterThermoJet[validEmCount][1] = std::clamp(em.fuelRate, 0.05f, 6.0f);
        cb.emitterThermoJet[validEmCount][2] = std::clamp(sanitizeFinite(em.jetSpeed, 32.0f), 0.0f, 120.0f);
        cb.emitterThermoJet[validEmCount][3] = std::clamp(sanitizeFinite(em.curlStrength, 14.0f), 0.0f, 40.0f);
        cb.emitterDirSoot[validEmCount][0]   = dx;
        cb.emitterDirSoot[validEmCount][1]   = dy;
        cb.emitterDirSoot[validEmCount][2]   = dz;
        cb.emitterDirSoot[validEmCount][3]   = clampedSootSeed;
        ++validEmCount;
    }

    cb.counts[0] = validEmCount;
    cb.counts[1] = std::min(params.obstacleCount, 8u);
    cb.counts[2] = kFireGridRes;
    cb.counts[3] = jacobiCount;

    for (uint32_t i = 0; i < cb.counts[1]; ++i) {
        const auto& ob = params.obstacles[i];
        const bool obFinite = std::isfinite(ob.minW[0]) && std::isfinite(ob.minW[1]) && std::isfinite(ob.minW[2]) &&
                              std::isfinite(ob.maxW[0]) && std::isfinite(ob.maxW[1]) && std::isfinite(ob.maxW[2]);
        cb.obstacleBoxMin[i][0] = obFinite ? ob.minW[0] : 0.0f;
        cb.obstacleBoxMin[i][1] = obFinite ? ob.minW[1] : 0.0f;
        cb.obstacleBoxMin[i][2] = obFinite ? ob.minW[2] : 0.0f;
        cb.obstacleBoxMin[i][3] = (ob.active && obFinite) ? 1.0f : 0.0f;
        cb.obstacleBoxMax[i][0] = obFinite ? ob.maxW[0] : 0.0f;
        cb.obstacleBoxMax[i][1] = obFinite ? ob.maxW[1] : 0.0f;
        cb.obstacleBoxMax[i][2] = obFinite ? ob.maxW[2] : 0.0f;
        cb.obstacleBoxMax[i][3] = std::clamp(sanitizeFinite(ob.roughness, 0.5f), 0.0f, 1.0f);
    }

    const uint32_t ringIdx = m_fireFrameIdx % kCbRingCount;
    const size_t cbOffset = static_cast<size_t>(ringIdx) * sizeof(GpuFireSimCB);
    std::memcpy(m_fireCbMapped + cbOffset, &cb, sizeof(cb));
    const D3D12_GPU_VIRTUAL_ADDRESS cbGpu = m_fireCbBuf->GetGPUVirtualAddress() + cbOffset;

    if (m_tsHeap) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, ringIdx * 4u + 0u);
    }

    ID3D12DescriptorHeap* heaps[1] = {m_heap.Get()};
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetComputeRootSignature(m_fireRoot.Get());
    cmd->SetComputeRootConstantBufferView(0, cbGpu);

    constexpr uint32_t kGroups = kFireGridRes / 4u; // 16 x 16 x 16
    constexpr D3D12_RESOURCE_STATES kCsSrv  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    constexpr D3D12_RESOURCE_STATES kAllSrv = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    constexpr D3D12_RESOURCE_STATES kUav    = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    // Pass 1: Inject (Vel[p], Thermo[p] -> Vel[q], Thermo[q])
    Transition(cmd, m_fireVel[p],    kCsSrv);
    Transition(cmd, m_fireThermo[p], kCsSrv);
    Transition(cmd, m_fireVel[q],    kUav);
    Transition(cmd, m_fireThermo[q], kUav);
    cmd->SetPipelineState(m_psoFireInject.Get());
    cmd->SetComputeRootDescriptorTable(1, GpuAt(FireSrvVT(p)));
    cmd->SetComputeRootDescriptorTable(2, GpuAt(FireUavVT(q)));
    cmd->Dispatch(kGroups, kGroups, kGroups);

    // Pass 2: Advect MacCormack + BFECC (Vel[q], Thermo[q] -> Vel[p], Thermo[p])
    Transition(cmd, m_fireVel[q],    kCsSrv);
    Transition(cmd, m_fireThermo[q], kCsSrv);
    Transition(cmd, m_fireVel[p],    kUav);
    Transition(cmd, m_fireThermo[p], kUav);
    cmd->SetPipelineState(m_psoFireAdvect.Get());
    cmd->SetComputeRootDescriptorTable(1, GpuAt(FireSrvVT(q)));
    cmd->SetComputeRootDescriptorTable(2, GpuAt(FireUavVT(p)));
    cmd->Dispatch(kGroups, kGroups, kGroups);

    // Pass 3: Vorticity Confinement + Buoyancy + Arrhenius Combustion (Vel[p], Thermo[p] -> Vel[q], Thermo[q])
    Transition(cmd, m_fireVel[p],    kCsSrv);
    Transition(cmd, m_fireThermo[p], kCsSrv);
    Transition(cmd, m_fireVel[q],    kUav);
    Transition(cmd, m_fireThermo[q], kUav);
    cmd->SetPipelineState(m_psoFireForces.Get());
    cmd->SetComputeRootDescriptorTable(1, GpuAt(FireSrvVT(p)));
    cmd->SetComputeRootDescriptorTable(2, GpuAt(FireUavVT(q)));
    cmd->Dispatch(kGroups, kGroups, kGroups);

    // Pass 4: Divergence + Thermal Expansion (Vel[q], Thermo[q], Press[p] -> Vel[p], Press[q])
    Transition(cmd, m_fireVel[q],    kCsSrv);
    Transition(cmd, m_fireThermo[q], kAllSrv);
    Transition(cmd, m_firePress[p],  kCsSrv);
    Transition(cmd, m_fireVel[p],    kUav);
    Transition(cmd, m_firePress[q],  kUav);
    cmd->SetPipelineState(m_psoFireDiv.Get());
    cmd->SetComputeRootDescriptorTable(1, GpuAt(FireSrvDiv(p)));
    cmd->SetComputeRootDescriptorTable(2, GpuAt(FireUavDiv(p)));
    cmd->Dispatch(kGroups, kGroups, kGroups);

    // Pass 5: Nx Pressure Jacobi Relaxation (Vel[p] stays SRV; Press[q] <-> Press[p], even count ends in Press[q])
    Transition(cmd, m_fireVel[p], kCsSrv);
    cmd->SetPipelineState(m_psoFirePress.Get());
    for (uint32_t iter = 0; iter < jacobiCount; ++iter) {
        if ((iter & 1u) == 0u) {
            Transition(cmd, m_firePress[q], kCsSrv);
            Transition(cmd, m_firePress[p], kUav);
            cmd->SetComputeRootDescriptorTable(1, GpuAt(FireSrvVPOpp(p)));
            cmd->SetComputeRootDescriptorTable(2, GpuAt(FireUavP(p)));
        } else {
            Transition(cmd, m_firePress[p], kCsSrv);
            Transition(cmd, m_firePress[q], kUav);
            cmd->SetComputeRootDescriptorTable(1, GpuAt(FireSrvVPSame(p)));
            cmd->SetComputeRootDescriptorTable(2, GpuAt(FireUavP(q)));
        }
        cmd->Dispatch(kGroups, kGroups, kGroups);
    }

    // Pass 6: Velocity Pressure-Gradient Projection (Vel[p], Press[q] -> Vel[q])
    Transition(cmd, m_firePress[q], kCsSrv);
    Transition(cmd, m_fireVel[q],   kUav);
    cmd->SetPipelineState(m_psoFireProj.Get());
    cmd->SetComputeRootDescriptorTable(1, GpuAt(FireSrvVPOpp(p)));
    cmd->SetComputeRootDescriptorTable(2, GpuAt(FireUavV(q)));
    cmd->Dispatch(kGroups, kGroups, kGroups);

    // Pass 7: 3D Fire-to-Smoke Radiance Cascade (Thermo[q], Rad[p] -> Rad[q])
    Transition(cmd, m_fireRad[p], kCsSrv);
    Transition(cmd, m_fireRad[q], kUav);
    cmd->SetPipelineState(m_psoFireRad.Get());
    cmd->SetComputeRootDescriptorTable(1, GpuAt(FireSrvRad(p)));
    cmd->SetComputeRootDescriptorTable(2, GpuAt(FireUavRad(q)));
    cmd->Dispatch(kGroups, kGroups, kGroups);

    // Leave final outputs in ALL_SHADER_RESOURCE for LightingPass / LightFog / RawFluidCompositeHost sampling
    Transition(cmd, m_fireVel[q],   kAllSrv);
    Transition(cmd, m_fireRad[q],   kAllSrv);

    if (m_tsHeap && m_tsReadback) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, ringIdx * 4u + 1u);
        cmd->ResolveQueryData(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, ringIdx * 4u + 0u, 2u,
                              m_tsReadback.Get(), static_cast<UINT64>(ringIdx * 4u + 0u) * sizeof(uint64_t));
    }

    m_fireDomain.originXYZ[0]     = originX;
    m_fireDomain.originXYZ[1]     = originY;
    m_fireDomain.originXYZ[2]     = originZ;
    m_fireDomain.worldSpanU       = domainSpan;
    m_fireDomain.active           = true;
    m_fireDomain.emitterCount     = cb.counts[0];
    m_fireDomain.isInterior       = params.enableCeiling;
    m_fireDomain.debugOverlayMode = params.debugOverlayMode;

    m_fireParity = q;
    m_fireResetNext = false;
    ++m_fireFrameIdx;
}

void RawFluidSimHost::StepWater(ID3D12GraphicsCommandList* cmd, const FluidWaterParams& params)
{
    if (!m_ready || !cmd || !m_waterCbMapped) {
        return;
    }

    // Non-blocking harvest of 3-frames-old retired Water timestamp slot
    if (m_waterFrameIdx >= (kCbRingCount - 1u)) {
        const uint32_t retiredSlot = (m_waterFrameIdx - (kCbRingCount - 1u)) % kCbRingCount;
        float measuredMs = 0.0f;
        if (HarvestTimestampPair(retiredSlot * 4u + 2u, measuredMs)) {
            m_timings.waterStepMs = measuredMs;
            m_timings.valid = true;
        }
    }

    const float dx = std::max(params.cellSize, 0.25f);
    const float prevCamX = m_hasPrevWaterCam ? m_prevWaterCamXY[0] : params.camPosXY[0];
    const float prevCamY = m_hasPrevWaterCam ? m_prevWaterCamXY[1] : params.camPosXY[1];

    GpuWaterSimCB cb{};
    cb.camAndGrid[0] = params.camPosXY[0];
    cb.camAndGrid[1] = params.camPosXY[1];
    cb.camAndGrid[2] = dx;
    cb.camAndGrid[3] = 1.0f / static_cast<float>(kWaterGridRes);

    cb.prevCamAndTime[0] = prevCamX;
    cb.prevCamAndTime[1] = prevCamY;
    cb.prevCamAndTime[2] = std::clamp(params.dt, 1.0e-4f, 0.05f);
    cb.prevCamAndTime[3] = params.timeSec;

    cb.swePhysics[0] = params.gravityG;
    cb.swePhysics[1] = params.restDepthH0;
    cb.swePhysics[2] = params.velDragGamma;
    cb.swePhysics[3] = params.viscosityNu;

    cb.swellAndWind[0] = params.windDirXY[0];
    cb.swellAndWind[1] = params.windDirXY[1];
    cb.swellAndWind[2] = params.swellAmp;
    cb.swellAndWind[3] = params.riverFlowSpeed;

    cb.foamAndRain[0] = params.foamDecayRate;
    cb.foamAndRain[1] = params.crestFoamThresh;
    cb.foamAndRain[2] = params.rainIntensity;
    cb.foamAndRain[3] = params.shoreBreakStrength;

    cb.waterPlaneAndFlags[0] = params.waterPlaneZ;
    cb.waterPlaneAndFlags[1] = params.shorelineZBias;
    cb.waterPlaneAndFlags[2] = static_cast<float>(m_waterFrameIdx);
    cb.waterPlaneAndFlags[3] = m_waterResetNext ? 1.0f : 0.0f;

    cb.counts[0] = std::min(params.trailCount, 32u);
    cb.counts[1] = kWaterGridRes;
    cb.counts[2] = 0u;
    cb.counts[3] = 0u;

    for (uint32_t i = 0; i < cb.counts[0]; ++i) {
        const auto& tr = params.trails[i];
        cb.footTrail[i][0] = tr.posW[0];
        cb.footTrail[i][1] = tr.posW[1];
        cb.footTrail[i][2] = tr.posW[2];
        cb.footTrail[i][3] = tr.strength;
        cb.footVel[i][0]   = tr.velW[0];
        cb.footVel[i][1]   = tr.velW[1];
        cb.footVel[i][2]   = tr.radius;
        cb.footVel[i][3]   = tr.splashScale;
    }

    const uint32_t ringIdx = m_waterFrameIdx % kCbRingCount;
    const size_t cbOffset = static_cast<size_t>(ringIdx) * sizeof(GpuWaterSimCB);
    std::memcpy(m_waterCbMapped + cbOffset, &cb, sizeof(cb));
    const D3D12_GPU_VIRTUAL_ADDRESS cbGpu = m_waterCbBuf->GetGPUVirtualAddress() + cbOffset;

    if (m_tsHeap) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, ringIdx * 4u + 2u);
    }

    ID3D12DescriptorHeap* heaps[1] = {m_heap.Get()};
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetComputeRootSignature(m_waterRoot.Get());
    cmd->SetComputeRootConstantBufferView(0, cbGpu);

    constexpr uint32_t kGroups = kWaterGridRes / 8u; // 32 x 32 x 1
    constexpr D3D12_RESOURCE_STATES kCsSrv  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    constexpr D3D12_RESOURCE_STATES kUav    = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    // Pass 1: Wake/Rain/Shoreline Inject (StateA -> StateB)
    Transition(cmd, m_waterState[0], kCsSrv);
    Transition(cmd, m_waterState[1], kUav);
    cmd->SetPipelineState(m_psoWaterInject.Get());
    cmd->SetComputeRootDescriptorTable(1, GpuAt(kSlotWaterSrvA));
    cmd->SetComputeRootDescriptorTable(2, GpuAt(kSlotWaterUavB));
    cmd->Dispatch(kGroups, kGroups, 1u);

    // Pass 2: Conservative Semi-Implicit SWE + Foam Advection (StateB -> StateA)
    Transition(cmd, m_waterState[1], kCsSrv);
    Transition(cmd, m_waterState[0], kUav);
    cmd->SetPipelineState(m_psoWaterStep.Get());
    cmd->SetComputeRootDescriptorTable(1, GpuAt(kSlotWaterSrvB));
    cmd->SetComputeRootDescriptorTable(2, GpuAt(kSlotWaterUavA));
    cmd->Dispatch(kGroups, kGroups, 1u);

    // Pass 3: Analytical Curl-Free Normals + Jacobian Crest-Folding Foam Mask (StateA -> NormalOut)
    Transition(cmd, m_waterState[0], kCsSrv);
    Transition(cmd, m_waterNormal,   kUav);
    cmd->SetPipelineState(m_psoWaterNormals.Get());
    cmd->SetComputeRootDescriptorTable(1, GpuAt(kSlotWaterSrvA));
    cmd->SetComputeRootDescriptorTable(2, GpuAt(kSlotWaterUavNorm));
    cmd->Dispatch(kGroups, kGroups, 1u);

    // Leave WaterState and WaterNormal in NON_PIXEL_SHADER_RESOURCE so RtWater (Direct or Async Compute) binds with 0 barrier hazards
    Transition(cmd, m_waterNormal, kCsSrv);

    if (m_tsHeap && m_tsReadback) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, ringIdx * 4u + 3u);
        cmd->ResolveQueryData(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, ringIdx * 4u + 2u, 2u,
                              m_tsReadback.Get(), static_cast<UINT64>(ringIdx * 4u + 2u) * sizeof(uint64_t));
    }

    const float waterSpan = static_cast<float>(kWaterGridRes) * dx;
    m_waterDomain.originXY[0] = params.camPosXY[0] - 0.5f * waterSpan;
    m_waterDomain.originXY[1] = params.camPosXY[1] - 0.5f * waterSpan;
    m_waterDomain.worldSpanU  = waterSpan;
    m_waterDomain.active      = true;

    m_prevWaterCamXY[0] = params.camPosXY[0];
    m_prevWaterCamXY[1] = params.camPosXY[1];
    m_hasPrevWaterCam = true;
    m_waterResetNext = false;
    ++m_waterFrameIdx;
}

D3D12_GPU_DESCRIPTOR_HANDLE RawFluidSimHost::FireThermoSrv() const
{
    return GpuAt(m_fireParity == 0u ? kSlotExtFireThermo0 : kSlotExtFireThermo1);
}

D3D12_GPU_DESCRIPTOR_HANDLE RawFluidSimHost::FireRadianceSrv() const
{
    return GpuAt(m_fireParity == 0u ? kSlotExtFireRad0 : kSlotExtFireRad1);
}

D3D12_GPU_DESCRIPTOR_HANDLE RawFluidSimHost::WaterStateSrv() const
{
    return GpuAt(kSlotExtWaterState);
}

D3D12_GPU_DESCRIPTOR_HANDLE RawFluidSimHost::WaterNormalSrv() const
{
    return GpuAt(kSlotExtWaterNormal);
}

D3D12_CPU_DESCRIPTOR_HANDLE RawFluidSimHost::FireThermoSrvCpu() const
{
    return CpuAt(m_fireParity == 0u ? kSlotExtFireThermo0 : kSlotExtFireThermo1);
}

D3D12_CPU_DESCRIPTOR_HANDLE RawFluidSimHost::FireRadianceSrvCpu() const
{
    return CpuAt(m_fireParity == 0u ? kSlotExtFireRad0 : kSlotExtFireRad1);
}

D3D12_CPU_DESCRIPTOR_HANDLE RawFluidSimHost::WaterStateSrvCpu() const
{
    return CpuAt(kSlotExtWaterState);
}

D3D12_CPU_DESCRIPTOR_HANDLE RawFluidSimHost::WaterNormalSrvCpu() const
{
    return CpuAt(kSlotExtWaterNormal);
}

} // namespace raw::bridge
