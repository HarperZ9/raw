// RawSsrHost.cpp: Native DX12 GPU Compute GGX Stochastic Screen-Space Reflections (SSR) Solver.
// Original clean-room implementation, MIT License.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "RawSsrHost.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if __has_include("raw_ssr_trace_cs.h")
#define RAW_SSR_HAS_EMBEDDED_DXIL 1
#include "raw_ssr_trace_cs.h"
#else
#define RAW_SSR_HAS_EMBEDDED_DXIL 0
#endif

namespace raw::bridge
{
namespace
{
using Microsoft::WRL::ComPtr;

// GPU constant buffer matching RawSsrSim.hlsl byte-for-byte (256-byte aligned).
struct alignas(256) GpuSsrCB {
    float    proj[16];         // 64 B
    float    invProj[16];      // 64 B
    float    rayParams[4];     // 16 B
    float    roughParams[4];   // 16 B
    uint32_t dimsAndFlags[4];  // 16 B
    uint32_t _pad[20];         // 80 B -> 256 B exact
};
static_assert(sizeof(GpuSsrCB) == 256u, "GpuSsrCB must be 256 bytes");

bool CreateBuffer(ID3D12Device* dev, uint64_t bytes, D3D12_HEAP_TYPE heapType,
                  D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initState, ComPtr<ID3D12Resource>& out)
{
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = heapType;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = (bytes + 255ull) & ~255ull;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Flags = flags;
    return SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, initState, nullptr, IID_PPV_ARGS(&out)));
}

DXGI_FORMAT ResolveSrvFormat(DXGI_FORMAT fmt)
{
    switch (fmt) {
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R32_TYPELESS:
        return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24G8_TYPELESS:
        return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case DXGI_FORMAT_D16_UNORM:
    case DXGI_FORMAT_R16_TYPELESS:
        return DXGI_FORMAT_R16_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    default:
        return fmt;
    }
}

} // namespace

void BuildSsrPerspectiveLH(float fovYRadians, float aspect, float nearZ, float farZ,
                           float outProj[16], float outInvProj[16])
{
    std::memset(outProj, 0, sizeof(float) * 16u);
    std::memset(outInvProj, 0, sizeof(float) * 16u);

    const float safeNear = std::max(nearZ, 1.0e-3f);
    const float safeFar  = std::max(farZ, safeNear + 1.0f);
    const float safeAsp  = std::max(aspect, 1.0e-3f);
    const float tanHalf  = std::tan(std::clamp(fovYRadians, 0.05f, 3.05f) * 0.5f);
    const float yScale   = 1.0f / tanHalf;
    const float xScale   = yScale / safeAsp;
    const float q        = safeFar / (safeFar - safeNear);

    // Row-vector DX12 LH perspective P: [x, y, z, 1] * P = [x*xScale, y*yScale, z*q - near*q, z]
    outProj[0 * 4 + 0] = xScale;
    outProj[1 * 4 + 1] = yScale;
    outProj[2 * 4 + 2] = q;
    outProj[2 * 4 + 3] = 1.0f;
    outProj[3 * 4 + 2] = -safeNear * q;

    // Exact inverse P^-1: [nx, ny, nz, 1] * P^-1 = [nx/xScale, ny/yScale, 1, (1/near) - nz*(far-near)/(near*far)]
    outInvProj[0 * 4 + 0] = safeAsp * tanHalf;
    outInvProj[1 * 4 + 1] = tanHalf;
    outInvProj[2 * 4 + 3] = -(safeFar - safeNear) / (safeNear * safeFar);
    outInvProj[3 * 4 + 2] = 1.0f;
    outInvProj[3 * 4 + 3] = 1.0f / safeNear;
}

RawSsrHost::~RawSsrHost()
{
    Shutdown();
}

void RawSsrHost::Shutdown()
{
    if (m_cbBuf && m_cbMapped) {
        m_cbBuf->Unmap(0, nullptr);
        m_cbMapped = nullptr;
    }
    m_tsReadback.Reset();
    m_tsHeap.Reset();
    m_cbBuf.Reset();
    m_outputTex.Reset();
    m_outputState = D3D12_RESOURCE_STATE_COMMON;
    m_outW = 0u;
    m_outH = 0u;
    m_pso.Reset();
    m_root.Reset();
    m_heap.Reset();
    m_dev = nullptr;
    m_ready = false;
    m_frameIdx = 0u;
    m_lastSsrMs = 0.0f;
    m_hasValidTiming = false;
}

bool RawSsrHost::EnsureOutputTarget(uint32_t width, uint32_t height)
{
    if (m_outputTex && m_outW == width && m_outH == height) {
        return true;
    }
    m_outputTex.Reset();
    m_outW = 0u;
    m_outH = 0u;

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

    if (FAILED(m_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                              D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m_outputTex)))) {
        return false;
    }
    m_outputState = D3D12_RESOURCE_STATE_COMMON;
    m_outW = width;
    m_outH = height;

    // Populate persistent external consumer SRV + all ring UAV slots
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format                  = DXGI_FORMAT_R16G16B16A16_FLOAT;
    srv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels     = 1u;
    m_dev->CreateShaderResourceView(m_outputTex.Get(), &srv, CpuAt(kSlotExtOutSrv));

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format        = DXGI_FORMAT_R16G16B16A16_FLOAT;
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    for (uint32_t r = 0u; r < kRingCount; ++r) {
        m_dev->CreateUnorderedAccessView(m_outputTex.Get(), nullptr, &uav, CpuAt(r * kDescsPerRingSlot + 3u));
    }
    return true;
}

bool RawSsrHost::Init(ID3D12Device* dev, const SsrDesc& desc, std::string* outErr)
{
    Shutdown();
    if (!dev) {
        if (outErr) *outErr = "RawSsrHost: null ID3D12Device";
        return false;
    }

    D3D12_SHADER_BYTECODE cs = desc.customShader;
    if (!cs.pShaderBytecode || cs.BytecodeLength == 0) {
#if RAW_SSR_HAS_EMBEDDED_DXIL
        cs = {kDxil_raw_ssr_trace, sizeof kDxil_raw_ssr_trace};
#else
        if (outErr) *outErr = "RawSsrHost: missing raw_ssr_trace_cs.h and no customShader";
        return false;
#endif
    }

    m_dev = dev;

    // 1) Shader-visible descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = kTotalDescCount;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_heap)))) {
        if (outErr) *outErr = "RawSsrHost: CreateDescriptorHeap failed";
        return false;
    }
    m_descInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // 2) Root signature: b0 (CBV), t0..t2 (Color/Depth/Rough SRVs), u0 (SsrOut UAV) + s0 PointClamp, s1 LinearClamp
    {
        D3D12_DESCRIPTOR_RANGE srvRange{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 0, 0};
        D3D12_DESCRIPTOR_RANGE uavRange{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0};

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

        D3D12_STATIC_SAMPLER_DESC samp[2]{};
        samp[0].Filter           = D3D12_FILTER_MIN_MAG_MIP_POINT;
        samp[0].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp[0].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp[0].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp[0].MaxLOD           = D3D12_FLOAT32_MAX;
        samp[0].ShaderRegister   = 0u;
        samp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        samp[1].Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samp[1].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp[1].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp[1].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp[1].MaxLOD           = D3D12_FLOAT32_MAX;
        samp[1].ShaderRegister   = 1u;
        samp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rsd{3, rp, 2, samp, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        ComPtr<ID3DBlob> sig, err;
        if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err)) ||
            FAILED(dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_root)))) {
            if (outErr) *outErr = "RawSsrHost: CreateRootSignature failed";
            return false;
        }
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = m_root.Get();
    pd.CS = cs;
    if (FAILED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_pso)))) {
        if (outErr) *outErr = "RawSsrHost: CreateComputePipelineState failed";
        return false;
    }

    // 3) 4-frame persistently mapped CBV upload ring
    const uint64_t cbBytes = static_cast<uint64_t>(kRingCount) * sizeof(GpuSsrCB);
    if (!CreateBuffer(dev, cbBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
                      D3D12_RESOURCE_STATE_GENERIC_READ, m_cbBuf)) {
        if (outErr) *outErr = "RawSsrHost: CBV upload ring failed";
        return false;
    }
    D3D12_RANGE rr{0, 0};
    void* ptr = nullptr;
    if (FAILED(m_cbBuf->Map(0, &rr, &ptr))) {
        if (outErr) *outErr = "RawSsrHost: failed to map CBV ring";
        return false;
    }
    m_cbMapped = static_cast<uint8_t*>(ptr);
    std::memset(m_cbMapped, 0, static_cast<size_t>(cbBytes));

    // 4) Initial RGBA16F SSR output target
    if (desc.initialWidth > 0u && desc.initialHeight > 0u) {
        if (!EnsureOutputTarget(desc.initialWidth, desc.initialHeight)) {
            if (outErr) *outErr = "RawSsrHost: initial RGBA16F output target allocation failed";
            return false;
        }
    }

    // 5) 4-frame GPU timestamp query heap (2 queries/frame)
    {
        constexpr uint32_t kTotalQueries = kRingCount * 2u;
        D3D12_QUERY_HEAP_DESC qd{};
        qd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        qd.Count = kTotalQueries;
        if (FAILED(dev->CreateQueryHeap(&qd, IID_PPV_ARGS(&m_tsHeap))) ||
            !CreateBuffer(dev, static_cast<uint64_t>(kTotalQueries) * sizeof(uint64_t),
                          D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
                          D3D12_RESOURCE_STATE_COPY_DEST, m_tsReadback)) {
            m_tsHeap.Reset();
            m_tsReadback.Reset();
        }
    }

    m_ready = true;
    return true;
}

bool RawSsrHost::HarvestTimestampPair(uint32_t queryStartIdx, float& outMs) const
{
    if (!m_tsReadback || m_tsFreqHz == 0u) return false;
    const SIZE_T byteOff = static_cast<SIZE_T>(queryStartIdx) * sizeof(uint64_t);
    const D3D12_RANGE rr{byteOff, byteOff + 2u * sizeof(uint64_t)};
    void* ptr = nullptr;
    if (FAILED(m_tsReadback->Map(0, &rr, &ptr))) return false;
    const auto* ticks = reinterpret_cast<const uint64_t*>(static_cast<const uint8_t*>(ptr) + byteOff);
    const uint64_t t0 = ticks[0];
    const uint64_t t1 = ticks[1];
    const D3D12_RANGE wr{0, 0};
    m_tsReadback->Unmap(0, &wr);
    if (t1 < t0) return false;
    const double ms = static_cast<double>(t1 - t0) * 1000.0 / static_cast<double>(m_tsFreqHz);
    if (!std::isfinite(ms) || ms < 0.0 || ms > 5000.0) return false;
    outMs = static_cast<float>(ms);
    return true;
}

D3D12_CPU_DESCRIPTOR_HANDLE RawSsrHost::CpuAt(uint32_t slot) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(slot) * m_descInc;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE RawSsrHost::GpuAt(uint32_t slot) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE h = m_heap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<UINT64>(slot) * m_descInc;
    return h;
}

bool RawSsrHost::Execute(ID3D12GraphicsCommandList* cmd,
                         ID3D12Resource* sceneColor,
                         ID3D12Resource* sceneDepth,
                         uint32_t width,
                         uint32_t height,
                         const SsrParams& params,
                         D3D12_RESOURCE_STATES srcColorState,
                         D3D12_RESOURCE_STATES srcDepthState,
                         ID3D12Resource* roughnessMap,
                         D3D12_RESOURCE_STATES roughnessState)
{
    if (!m_ready || !cmd || !sceneColor || !sceneDepth || !m_cbMapped || width == 0u || height == 0u) {
        return false;
    }
    if (!EnsureOutputTarget(width, height)) {
        return false;
    }

    // Harvest 3-frames-old retired timestamp slot (non-blocking)
    if (m_frameIdx >= (kRingCount - 1u)) {
        const uint32_t retired = (m_frameIdx - (kRingCount - 1u)) % kRingCount;
        float ms = 0.0f;
        if (HarvestTimestampPair(retired * 2u, ms)) {
            m_lastSsrMs = ms;
            m_hasValidTiming = true;
        }
    }

    GpuSsrCB cb{};
    std::memcpy(cb.proj, params.proj, sizeof(cb.proj));
    std::memcpy(cb.invProj, params.invProj, sizeof(cb.invProj));
    cb.rayParams[0] = std::max(params.maxRayDist, 1.0f);
    cb.rayParams[1] = std::max(params.thicknessM, 0.05f);
    cb.rayParams[2] = std::max(params.nearZ, 0.01f);
    cb.rayParams[3] = std::max(params.startBias, 0.002f);

    cb.roughParams[0] = std::clamp(params.fallbackRoughness, 0.0f, 1.0f);
    cb.roughParams[1] = std::clamp(params.maxRoughness, 0.05f, 1.0f);
    cb.roughParams[2] = std::clamp(params.edgeFadeWidth, 0.01f, 0.5f);
    cb.roughParams[3] = params.frameJitter;

    cb.dimsAndFlags[0] = width;
    cb.dimsAndFlags[1] = height;
    cb.dimsAndFlags[2] = std::clamp(params.maxSteps, 8u, 40u);
    cb.dimsAndFlags[3] = (roughnessMap != nullptr) ? 1u : 0u;

    const uint32_t ring = m_frameIdx % kRingCount;
    const size_t cbOff  = static_cast<size_t>(ring) * sizeof(GpuSsrCB);
    std::memcpy(m_cbMapped + cbOff, &cb, sizeof(cb));
    const D3D12_GPU_VIRTUAL_ADDRESS cbGpu = m_cbBuf->GetGPUVirtualAddress() + cbOff;

    // Populate ring slot SRVs: [t0 SceneColor, t1 SceneDepth, t2 RoughnessMap (or SceneDepth dummy)]
    const uint32_t baseSlot = ring * kDescsPerRingSlot;

    D3D12_SHADER_RESOURCE_VIEW_DESC cSrv{};
    cSrv.Format                  = ResolveSrvFormat(sceneColor->GetDesc().Format);
    cSrv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    cSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    cSrv.Texture2D.MipLevels     = 1u;
    m_dev->CreateShaderResourceView(sceneColor, &cSrv, CpuAt(baseSlot + 0u));

    D3D12_SHADER_RESOURCE_VIEW_DESC dSrv{};
    dSrv.Format                  = ResolveSrvFormat(sceneDepth->GetDesc().Format);
    dSrv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    dSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    dSrv.Texture2D.MipLevels     = 1u;
    m_dev->CreateShaderResourceView(sceneDepth, &dSrv, CpuAt(baseSlot + 1u));

    ID3D12Resource* rRes = roughnessMap ? roughnessMap : sceneDepth;
    D3D12_SHADER_RESOURCE_VIEW_DESC rSrv{};
    rSrv.Format                  = ResolveSrvFormat(rRes->GetDesc().Format);
    rSrv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    rSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    rSrv.Texture2D.MipLevels     = 1u;
    m_dev->CreateShaderResourceView(rRes, &rSrv, CpuAt(baseSlot + 2u));

    // Pre-dispatch barriers
    D3D12_RESOURCE_BARRIER preB[4]{};
    uint32_t preN = 0u;
    const bool colorNeeds = ((srcColorState & D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) == 0);
    const bool depthNeeds = ((srcDepthState & D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) == 0);
    const bool roughNeeds = (roughnessMap != nullptr) && ((roughnessState & D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) == 0);
    if (colorNeeds) {
        preB[preN].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        preB[preN++].Transition = {sceneColor, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                   srcColorState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
    }
    if (depthNeeds) {
        preB[preN].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        preB[preN++].Transition = {sceneDepth, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                   srcDepthState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
    }
    if (roughNeeds) {
        preB[preN].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        preB[preN++].Transition = {roughnessMap, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                   roughnessState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
    }
    if (m_outputState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        preB[preN].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        preB[preN++].Transition = {m_outputTex.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                   m_outputState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
        m_outputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    if (preN > 0u) cmd->ResourceBarrier(preN, preB);

    if (m_tsHeap) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, ring * 2u + 0u);
    }

    ID3D12DescriptorHeap* heaps[1] = {m_heap.Get()};
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetComputeRootSignature(m_root.Get());
    cmd->SetPipelineState(m_pso.Get());
    cmd->SetComputeRootConstantBufferView(0, cbGpu);
    cmd->SetComputeRootDescriptorTable(1, GpuAt(baseSlot + 0u));
    cmd->SetComputeRootDescriptorTable(2, GpuAt(baseSlot + 3u));
    cmd->Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);

    if (m_tsHeap && m_tsReadback) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, ring * 2u + 1u);
        cmd->ResolveQueryData(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, ring * 2u + 0u, 2u,
                              m_tsReadback.Get(), static_cast<UINT64>(ring * 2u) * sizeof(uint64_t));
    }

    // Post-dispatch barriers: restore caller inputs + transition SsrOut to SRV for consumers
    constexpr D3D12_RESOURCE_STATES kAllSrv =
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_BARRIER postB[4]{};
    uint32_t postN = 0u;
    if (colorNeeds) {
        postB[postN].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        postB[postN++].Transition = {sceneColor, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                     D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, srcColorState};
    }
    if (depthNeeds) {
        postB[postN].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        postB[postN++].Transition = {sceneDepth, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                     D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, srcDepthState};
    }
    if (roughNeeds) {
        postB[postN].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        postB[postN++].Transition = {roughnessMap, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                     D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, roughnessState};
    }
    postB[postN].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    postB[postN++].Transition = {m_outputTex.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS, kAllSrv};
    m_outputState = kAllSrv;
    cmd->ResourceBarrier(postN, postB);

    ++m_frameIdx;
    return true;
}

D3D12_GPU_DESCRIPTOR_HANDLE RawSsrHost::GetOutputSRV() const { return GpuAt(kSlotExtOutSrv); }
D3D12_CPU_DESCRIPTOR_HANDLE RawSsrHost::GetOutputSRVCpu() const { return CpuAt(kSlotExtOutSrv); }

} // namespace raw::bridge
