// RawFluidCompositeHost.cpp: Native DX12 Compute Compositor for 3D Navier-Stokes Fire/Smoke/Radiance.
// Original clean-room implementation, MIT License.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "RawFluidCompositeHost.h"

#include <algorithm>
#include <cstring>

#if __has_include("raw_fluid_fire_composite_cs.h")
#define RAW_FLUID_COMP_HAS_EMBEDDED_DXIL 1
#include "raw_fluid_fire_composite_cs.h"
#else
#define RAW_FLUID_COMP_HAS_EMBEDDED_DXIL 0
#endif

namespace raw::bridge
{
namespace
{
using Microsoft::WRL::ComPtr;

// Matches cbuffer FireCompCB : register(b0) in RawFluidFireComposite.hlsl (256 bytes).
struct GpuFireCompCB {
    float clipToWorld[16];
    float camPosW[4];
    float domainMinW[4];
    float domainInvSpanW[4];
    float dimsAndInv[4];
    float resScaleAndFlags[4];
    float sunDir[4];
    float sunCol[4];
    float _pad[20];
};
static_assert(sizeof(GpuFireCompCB) == 256u, "GpuFireCompCB must be 256 bytes");

D3D12_RESOURCE_BARRIER MakeTransition(ID3D12Resource* res, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = res;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    return b;
}

} // namespace

RawFluidCompositeHost::~RawFluidCompositeHost()
{
    Shutdown();
}

void RawFluidCompositeHost::Shutdown()
{
    if (m_fence && m_event && m_fenceVal > 0) {
        if (m_fence->GetCompletedValue() < m_fenceVal) {
            m_fence->SetEventOnCompletion(m_fenceVal, m_event);
            WaitForSingleObject(m_event, 3000);
        }
    }
    if (m_cbBuf && m_cbMapped) {
        m_cbBuf->Unmap(0, nullptr);
        m_cbMapped = nullptr;
    }
    m_cbBuf.Reset();
    m_list.Reset();
    for (uint32_t i = 0; i < kRingCount; ++i) {
        m_alloc[i].Reset();
        m_slotVal[i] = 0;
    }
    m_fence.Reset();
    if (m_event) {
        CloseHandle(m_event);
        m_event = nullptr;
    }
    m_heap.Reset();
    m_pso.Reset();
    m_root.Reset();
    m_dev.Reset();
    m_fenceVal = 0;
    m_frameIdx = 0;
    m_ready = false;
}

bool RawFluidCompositeHost::Init(ID3D12Device* dev, std::string* outErr)
{
#if RAW_FLUID_COMP_HAS_EMBEDDED_DXIL
    const D3D12_SHADER_BYTECODE blob{kDxil_raw_fluid_fire_composite, sizeof kDxil_raw_fluid_fire_composite};
    return InitWithBlob(dev, blob, outErr);
#else
    (void)dev;
    if (outErr) {
        *outErr = "RawFluidCompositeHost: raw_fluid_fire_composite_cs.h not present in include path";
    }
    return false;
#endif
}

bool RawFluidCompositeHost::InitWithBlob(ID3D12Device* dev, D3D12_SHADER_BYTECODE csBlob, std::string* outErr)
{
    Shutdown();
    if (!dev || !csBlob.pShaderBytecode || csBlob.BytecodeLength == 0) {
        if (outErr) *outErr = "RawFluidCompositeHost: invalid device or shader bytecode";
        return false;
    }
    m_dev = dev;

    // Root signature: b0 (CBV), t0..t2 (3 SRVs: SceneDepth, FireThermo, FireRadiance), u0 (1 UAV: ColorOut), s0 (LinearClamp)
    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 3;
    srvRange.BaseShaderRegister = 0;

    D3D12_DESCRIPTOR_RANGE uavRange{};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 1;
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
        FAILED(dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_root)))) {
        if (outErr) *outErr = "RawFluidCompositeHost: root signature creation failed";
        Shutdown();
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC cpd{};
    cpd.pRootSignature = m_root.Get();
    cpd.CS = csBlob;
    if (FAILED(dev->CreateComputePipelineState(&cpd, IID_PPV_ARGS(&m_pso)))) {
        if (outErr) *outErr = "RawFluidCompositeHost: Compute PSO creation failed";
        Shutdown();
        return false;
    }

    // Shader-visible heap: 4 descriptors per ring slot (t0, t1, t2, u0) * 4 ring slots = 16 descriptors
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = kRingCount * 4u;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_heap)))) {
        if (outErr) *outErr = "RawFluidCompositeHost: CreateDescriptorHeap failed";
        Shutdown();
        return false;
    }
    m_descInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Upload CBV ring (4 * 256 = 1024 bytes)
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_UPLOAD};
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = sizeof(GpuFireCompCB) * kRingCount;
    rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_RANGE zeroRange{0, 0};
    void* mapped = nullptr;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_cbBuf))) ||
        FAILED(m_cbBuf->Map(0, &zeroRange, &mapped))) {
        if (outErr) *outErr = "RawFluidCompositeHost: CBV allocation failed";
        Shutdown();
        return false;
    }
    m_cbMapped = static_cast<uint8_t*>(mapped);
    std::memset(m_cbMapped, 0, sizeof(GpuFireCompCB) * kRingCount);

    for (uint32_t i = 0; i < kRingCount; ++i) {
        if (FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_alloc[i])))) {
            if (outErr) *outErr = "RawFluidCompositeHost: CreateCommandAllocator failed";
            Shutdown();
            return false;
        }
    }
    if (FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_alloc[0].Get(), nullptr, IID_PPV_ARGS(&m_list))) ||
        FAILED(m_list->Close()) ||
        FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)))) {
        if (outErr) *outErr = "RawFluidCompositeHost: command list / fence creation failed";
        Shutdown();
        return false;
    }
    m_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_event) {
        if (outErr) *outErr = "RawFluidCompositeHost: CreateEventW failed";
        Shutdown();
        return false;
    }

    m_ready = true;
    return true;
}

bool RawFluidCompositeHost::RecordComposite(ID3D12GraphicsCommandList* cmd,
                                            ID3D12Resource* colorOut,
                                            ID3D12Resource* depthIn,
                                            ID3D12Resource* fireThermo,
                                            ID3D12Resource* fireRadiance,
                                            const FluidCompositeParams& params)
{
    if (!m_ready || !cmd || !colorOut || !depthIn || !fireThermo || !fireRadiance ||
        params.width == 0u || params.height == 0u || params.activeEmitters == 0u || params.domainSpanW <= 1.0f) {
        return false;
    }
    const D3D12_RESOURCE_DESC colorDesc = colorOut->GetDesc();
    if ((colorDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0) {
        return false;
    }

    const uint32_t slot = m_frameIdx % kRingCount;
    const uint32_t baseDesc = slot * 4u;

    auto cpuAt = [&](uint32_t idx) {
        D3D12_CPU_DESCRIPTOR_HANDLE h = m_heap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += static_cast<SIZE_T>(idx) * m_descInc;
        return h;
    };
    auto gpuAt = [&](uint32_t idx) {
        D3D12_GPU_DESCRIPTOR_HANDLE h = m_heap->GetGPUDescriptorHandleForHeapStart();
        h.ptr += static_cast<UINT64>(idx) * m_descInc;
        return h;
    };

    // t0: SceneDepth (R32_FLOAT 2D)
    D3D12_SHADER_RESOURCE_VIEW_DESC depthSrv{};
    depthSrv.Format = DXGI_FORMAT_R32_FLOAT;
    depthSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    depthSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depthSrv.Texture2D.MipLevels = 1;
    m_dev->CreateShaderResourceView(depthIn, &depthSrv, cpuAt(baseDesc + 0u));

    // t1: FireThermo (RGBA16F 3D) & t2: FireRadiance (RGBA16F 3D)
    D3D12_SHADER_RESOURCE_VIEW_DESC volSrv{};
    volSrv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    volSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
    volSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    volSrv.Texture3D.MipLevels = 1;
    m_dev->CreateShaderResourceView(fireThermo, &volSrv, cpuAt(baseDesc + 1u));
    m_dev->CreateShaderResourceView(fireRadiance, &volSrv, cpuAt(baseDesc + 2u));

    // u0: ColorOut (RGBA16F 2D UAV)
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavD{};
    uavD.Format = colorDesc.Format;
    uavD.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavD.Texture2D.MipSlice = 0;
    m_dev->CreateUnorderedAccessView(colorOut, nullptr, &uavD, cpuAt(baseDesc + 3u));

    // Pack FireCompCB
    GpuFireCompCB cb{};
    std::memcpy(cb.clipToWorld, params.clipToWorld, 64u);
    cb.camPosW[0] = params.camPosW[0];
    cb.camPosW[1] = params.camPosW[1];
    cb.camPosW[2] = params.camPosW[2];
    cb.camPosW[3] = params.simTimeSec;
    cb.domainMinW[0] = params.domainMinW[0];
    cb.domainMinW[1] = params.domainMinW[1];
    cb.domainMinW[2] = params.domainMinW[2];
    cb.domainMinW[3] = params.domainSpanW;
    const float invSpan = 1.0f / params.domainSpanW;
    cb.domainInvSpanW[0] = invSpan;
    cb.domainInvSpanW[1] = invSpan;
    cb.domainInvSpanW[2] = invSpan;
    cb.domainInvSpanW[3] = static_cast<float>(m_frameIdx & 0xFFFFu);
    cb.dimsAndInv[0] = static_cast<float>(params.width);
    cb.dimsAndInv[1] = static_cast<float>(params.height);
    cb.dimsAndInv[2] = 1.0f / cb.dimsAndInv[0];
    cb.dimsAndInv[3] = 1.0f / cb.dimsAndInv[1];
    cb.resScaleAndFlags[0] = params.resScale[0] > 0.01f ? params.resScale[0] : 1.0f;
    cb.resScaleAndFlags[1] = params.resScale[1] > 0.01f ? params.resScale[1] : 1.0f;
    cb.resScaleAndFlags[2] = static_cast<float>(params.activeEmitters);
    cb.resScaleAndFlags[3] = params.isInterior ? 1.0f : 0.0f;
    cb.sunDir[0] = params.sunDir[0];
    cb.sunDir[1] = params.sunDir[1];
    cb.sunDir[2] = params.sunDir[2];
    cb.sunDir[3] = params.sunIntensity;
    cb.sunCol[0] = params.sunCol[0];
    cb.sunCol[1] = params.sunCol[1];
    cb.sunCol[2] = params.sunCol[2];
    cb.sunCol[3] = params.ambientFill;

    const size_t cbOffset = static_cast<size_t>(slot) * sizeof(GpuFireCompCB);
    std::memcpy(m_cbMapped + cbOffset, &cb, sizeof cb);

    // Barriers: colorOut COMMON -> UAV, depthIn COMMON -> NON_PIXEL_SRV (fire volumes stay in ALL_SRV from StepFire)
    D3D12_RESOURCE_BARRIER pre[2] = {
        MakeTransition(colorOut, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        MakeTransition(depthIn,  D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
    };
    cmd->ResourceBarrier(2, pre);

    ID3D12DescriptorHeap* heaps[1] = {m_heap.Get()};
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetComputeRootSignature(m_root.Get());
    cmd->SetPipelineState(m_pso.Get());
    cmd->SetComputeRootConstantBufferView(0, m_cbBuf->GetGPUVirtualAddress() + cbOffset);
    cmd->SetComputeRootDescriptorTable(1, gpuAt(baseDesc + 0u));
    cmd->SetComputeRootDescriptorTable(2, gpuAt(baseDesc + 3u));
    cmd->Dispatch((params.width + 7u) / 8u, (params.height + 7u) / 8u, 1u);

    D3D12_RESOURCE_BARRIER post[2] = {
        MakeTransition(colorOut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON),
        MakeTransition(depthIn,  D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
    };
    cmd->ResourceBarrier(2, post);

    ++m_frameIdx;
    return true;
}

bool RawFluidCompositeHost::Composite(ID3D12CommandQueue* queue,
                                      ID3D12Resource* colorOut,
                                      ID3D12Resource* depthIn,
                                      ID3D12Resource* fireThermo,
                                      ID3D12Resource* fireRadiance,
                                      const FluidCompositeParams& params)
{
    if (!m_ready || !queue || !m_list || !m_fence || !m_event) return false;
    const uint32_t slot = m_frameIdx % kRingCount;
    if (m_slotVal[slot] > 0 && m_fence->GetCompletedValue() < m_slotVal[slot]) {
        m_fence->SetEventOnCompletion(m_slotVal[slot], m_event);
        WaitForSingleObject(m_event, 3000);
    }
    if (FAILED(m_alloc[slot]->Reset()) || FAILED(m_list->Reset(m_alloc[slot].Get(), nullptr))) {
        return false;
    }
    if (!RecordComposite(m_list.Get(), colorOut, depthIn, fireThermo, fireRadiance, params)) {
        m_list->Close();
        return false;
    }
    if (FAILED(m_list->Close())) return false;

    ID3D12CommandList* lists[1] = {m_list.Get()};
    queue->ExecuteCommandLists(1, lists);
    queue->Signal(m_fence.Get(), ++m_fenceVal);
    m_slotVal[slot] = m_fenceVal;
    return true;
}

} // namespace raw::bridge
