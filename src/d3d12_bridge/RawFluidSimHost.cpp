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

void RawFluidSimHost::StepFire(ID3D12GraphicsCommandList* cmd, const FluidFireParams& params)
{
    if (!m_ready || !cmd || !m_fireCbMapped) {
        return;
    }

    const uint32_t p = m_fireParity;
    const uint32_t q = 1u - p;
    const uint32_t jacobiCount = std::clamp((params.jacobiIterations + 1u) & ~1u, 2u, 32u); // enforce even count
    const float cellSize = std::max(params.cellSize, 0.05f);

    GpuFireSimCB cb{};
    cb.domainOriginAndCell[0] = params.domainOriginW[0];
    cb.domainOriginAndCell[1] = params.domainOriginW[1];
    cb.domainOriginAndCell[2] = params.domainOriginW[2];
    cb.domainOriginAndCell[3] = cellSize;

    cb.dtAndTime[0] = std::clamp(params.dt, 1.0e-4f, 0.05f);
    cb.dtAndTime[1] = params.timeSec;
    cb.dtAndTime[2] = static_cast<float>(m_fireFrameIdx);
    cb.dtAndTime[3] = 1.0f / cellSize;

    cb.windAndAmbient[0] = params.windVelW[0];
    cb.windAndAmbient[1] = params.windVelW[1];
    cb.windAndAmbient[2] = params.windVelW[2];
    cb.windAndAmbient[3] = std::max(params.ambientTempK, 200.0f);

    cb.physicsA[0] = params.vorticityEps;
    cb.physicsA[1] = params.buoyancyAlpha;
    cb.physicsA[2] = params.sootWeightBeta;
    cb.physicsA[3] = params.windDrag;

    cb.physicsB[0] = params.arrheniusRateA;
    cb.physicsB[1] = params.activationTempTa;
    cb.physicsB[2] = params.heatReleaseQ;
    cb.physicsB[3] = params.sootYield;

    cb.physicsC[0] = params.radiativeCoolSig;
    cb.physicsC[1] = params.velDamping;
    cb.physicsC[2] = params.sootDamping;
    cb.physicsC[3] = params.thermalExpansion;

    const uint32_t flagBits = (m_fireResetNext ? 1u : 0u) | (params.enableCeiling ? 2u : 0u);
    cb.boundsAndFlags[0] = params.floorZLocal;
    cb.boundsAndFlags[1] = params.ceilingZLocal;
    cb.boundsAndFlags[2] = params.scatterExtinction;
    cb.boundsAndFlags[3] = static_cast<float>(flagBits);

    cb.counts[0] = std::min(params.emitterCount, 16u);
    cb.counts[1] = std::min(params.obstacleCount, 8u);
    cb.counts[2] = kFireGridRes;
    cb.counts[3] = jacobiCount;

    for (uint32_t i = 0; i < cb.counts[0]; ++i) {
        const auto& em = params.emitters[i];
        cb.emitterPosRadius[i][0] = em.posW[0];
        cb.emitterPosRadius[i][1] = em.posW[1];
        cb.emitterPosRadius[i][2] = em.posW[2];
        cb.emitterPosRadius[i][3] = em.radius;
        cb.emitterThermoJet[i][0] = em.ignitionTempK;
        cb.emitterThermoJet[i][1] = em.fuelRate;
        cb.emitterThermoJet[i][2] = em.jetSpeed;
        cb.emitterThermoJet[i][3] = em.curlStrength;
        cb.emitterDirSoot[i][0]   = em.jetDir[0];
        cb.emitterDirSoot[i][1]   = em.jetDir[1];
        cb.emitterDirSoot[i][2]   = em.jetDir[2];
        cb.emitterDirSoot[i][3]   = em.sootSeedRate;
    }
    for (uint32_t i = 0; i < cb.counts[1]; ++i) {
        const auto& ob = params.obstacles[i];
        cb.obstacleBoxMin[i][0] = ob.minW[0];
        cb.obstacleBoxMin[i][1] = ob.minW[1];
        cb.obstacleBoxMin[i][2] = ob.minW[2];
        cb.obstacleBoxMin[i][3] = ob.active ? 1.0f : 0.0f;
        cb.obstacleBoxMax[i][0] = ob.maxW[0];
        cb.obstacleBoxMax[i][1] = ob.maxW[1];
        cb.obstacleBoxMax[i][2] = ob.maxW[2];
        cb.obstacleBoxMax[i][3] = ob.roughness;
    }

    const uint32_t ringIdx = m_fireFrameIdx % kCbRingCount;
    const size_t cbOffset = static_cast<size_t>(ringIdx) * sizeof(GpuFireSimCB);
    std::memcpy(m_fireCbMapped + cbOffset, &cb, sizeof(cb));
    const D3D12_GPU_VIRTUAL_ADDRESS cbGpu = m_fireCbBuf->GetGPUVirtualAddress() + cbOffset;

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

    m_fireDomain.originXYZ[0] = params.domainOriginW[0];
    m_fireDomain.originXYZ[1] = params.domainOriginW[1];
    m_fireDomain.originXYZ[2] = params.domainOriginW[2];
    m_fireDomain.worldSpanU   = static_cast<float>(kFireGridRes) * cellSize;
    m_fireDomain.active       = true;
    m_fireDomain.emitterCount = cb.counts[0];

    m_fireParity = q;
    m_fireResetNext = false;
    ++m_fireFrameIdx;
}

void RawFluidSimHost::StepWater(ID3D12GraphicsCommandList* cmd, const FluidWaterParams& params)
{
    if (!m_ready || !cmd || !m_waterCbMapped) {
        return;
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
