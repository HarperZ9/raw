// RawShadowPcssHost.cpp: Native DX12 GPU Compute Variable-Penumbra PCSS Contact-Hardening Shadow Solver
// & World-Space Texel-Snapped Directional Cascade Stabilizer.
// Original clean-room implementation, MIT License.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "RawShadowPcssHost.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if __has_include("raw_shadow_pcss_cs.h")
#define RAW_SHADOW_PCSS_HAS_EMBEDDED_DXIL 1
#include "raw_shadow_pcss_cs.h"
#else
#define RAW_SHADOW_PCSS_HAS_EMBEDDED_DXIL 0
#endif

namespace raw::bridge
{
namespace
{
using Microsoft::WRL::ComPtr;

// GPU constant buffer matching RawShadowPcssSim.hlsl byte-for-byte (256-byte aligned).
struct alignas(256) GpuShadowPcssCB {
    float    invViewProj[16];    // 64 B
    float    lightViewProj[16];  // 64 B
    float    sunDirAndBias[4];   // 16 B
    float    biasAndJitter[4];   // 16 B
    float    filterParams[4];    // 16 B
    uint32_t dims[4];            // 16 B
    uint32_t _pad[16];           // 64 B -> 256 B exact
};
static_assert(sizeof(GpuShadowPcssCB) == 256u, "GpuShadowPcssCB must be 256 bytes");

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

DXGI_FORMAT ResolveDepthSrvFormat(DXGI_FORMAT fmt)
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
    default:
        return fmt;
    }
}

void Normalize3(float v[3])
{
    const float lenSq = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    if (lenSq > 1.0e-12f) {
        const float inv = 1.0f / std::sqrt(lenSq);
        v[0] *= inv; v[1] *= inv; v[2] *= inv;
    } else {
        v[0] = 0.0f; v[1] = 0.0f; v[2] = 1.0f;
    }
}

void Cross3(const float a[3], const float b[3], float out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

float Dot3(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

} // namespace

TexelSnappedCascade BuildTexelSnappedCascade(const float cameraTargetW[3],
                                             const float sunDirW[3],
                                             float cascadeRadius,
                                             float depthSpan,
                                             uint32_t shadowMapSize)
{
    TexelSnappedCascade out{};
    const float safeRadius = std::max(cascadeRadius, 0.5f);
    const float safeDepth  = std::max(depthSpan, 1.0f);
    const uint32_t safeRes = std::max(shadowMapSize, 16u);

    // Light travels along forward = -sunDirW
    float fwd[3] = {-sunDirW[0], -sunDirW[1], -sunDirW[2]};
    Normalize3(fwd);

    // Fixed world up axis (independent of camera rotation -> zero shimmer on yaw/pitch!)
    float refUp[3] = {0.0f, 0.0f, 1.0f};
    if (std::fabs(Dot3(fwd, refUp)) > 0.985f) {
        refUp[0] = 0.0f; refUp[1] = 1.0f; refUp[2] = 0.0f;
    }
    float right[3]{};
    Cross3(refUp, fwd, right);
    Normalize3(right);
    float up[3]{};
    Cross3(fwd, right, up);
    Normalize3(up);

    // Project camera target into light space and snap (x, y) strictly to texel multiples
    const float texelSize = (2.0f * safeRadius) / static_cast<float>(safeRes);
    const float lsX = Dot3(cameraTargetW, right);
    const float lsY = Dot3(cameraTargetW, up);
    const float lsZ = Dot3(cameraTargetW, fwd);

    const float snapX = std::floor(lsX / texelSize) * texelSize;
    const float snapY = std::floor(lsY / texelSize) * texelSize;
    const float nearLS = lsZ - 0.5f * safeDepth;

    // Row-vector ortho matrix M where clip = [wx, wy, wz, 1] * M:
    //   clip.x = (dot(W, right) - snapX) / safeRadius         -> [-1, +1]
    //   clip.y = (dot(W, up)    - snapY) / safeRadius         -> [-1, +1]
    //   clip.z = (dot(W, fwd)   - nearLS) / safeDepth         -> [ 0, +1]
    //   clip.w = 1
    const float invR = 1.0f / safeRadius;
    const float invD = 1.0f / safeDepth;
    std::memset(out.lightViewProj, 0, sizeof(out.lightViewProj));

    out.lightViewProj[0 * 4 + 0] = right[0] * invR;
    out.lightViewProj[1 * 4 + 0] = right[1] * invR;
    out.lightViewProj[2 * 4 + 0] = right[2] * invR;
    out.lightViewProj[3 * 4 + 0] = -snapX * invR;

    out.lightViewProj[0 * 4 + 1] = up[0] * invR;
    out.lightViewProj[1 * 4 + 1] = up[1] * invR;
    out.lightViewProj[2 * 4 + 1] = up[2] * invR;
    out.lightViewProj[3 * 4 + 1] = -snapY * invR;

    out.lightViewProj[0 * 4 + 2] = fwd[0] * invD;
    out.lightViewProj[1 * 4 + 2] = fwd[1] * invD;
    out.lightViewProj[2 * 4 + 2] = fwd[2] * invD;
    out.lightViewProj[3 * 4 + 2] = -nearLS * invD;

    out.lightViewProj[3 * 4 + 3] = 1.0f;

    for (int i = 0; i < 3; ++i) {
        out.lightRight[i]   = right[i];
        out.lightUp[i]      = up[i];
        out.lightForward[i] = fwd[i];
    }
    out.snappedCenterLS[0] = snapX;
    out.snappedCenterLS[1] = snapY;
    out.snappedCenterLS[2] = lsZ;
    out.texelWorldSize     = texelSize;
    return out;
}

RawShadowPcssHost::~RawShadowPcssHost()
{
    Shutdown();
}

void RawShadowPcssHost::Shutdown()
{
    if (m_cbBuf && m_cbMapped) {
        m_cbBuf->Unmap(0, nullptr);
        m_cbMapped = nullptr;
    }
    m_tsReadback.Reset();
    m_tsHeap.Reset();
    m_cbBuf.Reset();
    m_pso.Reset();
    m_root.Reset();
    m_heap.Reset();
    m_dev = nullptr;
    m_ready = false;
    m_frameIdx = 0u;
    m_lastPcssMs = 0.0f;
    m_hasValidTiming = false;
}

bool RawShadowPcssHost::Init(ID3D12Device* dev, const ShadowPcssDesc& desc, std::string* outErr)
{
    Shutdown();
    if (!dev) {
        if (outErr) *outErr = "RawShadowPcssHost: null ID3D12Device";
        return false;
    }

    D3D12_SHADER_BYTECODE cs = desc.customShader;
    if (!cs.pShaderBytecode || cs.BytecodeLength == 0) {
#if RAW_SHADOW_PCSS_HAS_EMBEDDED_DXIL
        cs = {kDxil_raw_shadow_pcss, sizeof kDxil_raw_shadow_pcss};
#else
        if (outErr) *outErr = "RawShadowPcssHost: missing raw_shadow_pcss_cs.h and no customShader";
        return false;
#endif
    }

    m_dev = dev;

    // 1) Shader-visible descriptor heap (4 ring slots x 3 descriptors = 12)
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = kRingCount * kDescsPerRingSlot;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_heap)))) {
        if (outErr) *outErr = "RawShadowPcssHost: CreateDescriptorHeap failed";
        return false;
    }
    m_descInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // 2) Root signature: b0 (CBV), t0..t1 (SceneDepth + ShadowMap SRVs), u0 (ShadowOut UAV) + 2 static samplers (s0, s1)
    {
        D3D12_DESCRIPTOR_RANGE srvRange{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, 0};
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
        // s0: Point Clamp for Stage-1 blocker search
        samp[0].Filter           = D3D12_FILTER_MIN_MAG_MIP_POINT;
        samp[0].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp[0].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp[0].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp[0].MaxLOD           = D3D12_FLOAT32_MAX;
        samp[0].ShaderRegister   = 0u;
        samp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // s1: Linear Clamp Comparison (LESS_EQUAL) for Stage-3 hardware PCF
        samp[1].Filter           = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        samp[1].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        samp[1].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        samp[1].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        samp[1].ComparisonFunc   = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        samp[1].BorderColor      = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
        samp[1].MaxLOD           = D3D12_FLOAT32_MAX;
        samp[1].ShaderRegister   = 1u;
        samp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rsd{3, rp, 2, samp, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        ComPtr<ID3DBlob> sig, err;
        if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err)) ||
            FAILED(dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_root)))) {
            if (outErr) *outErr = "RawShadowPcssHost: CreateRootSignature failed";
            return false;
        }
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = m_root.Get();
    pd.CS = cs;
    if (FAILED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_pso)))) {
        if (outErr) *outErr = "RawShadowPcssHost: CreateComputePipelineState failed";
        return false;
    }

    // 3) 4-frame persistently mapped CBV upload ring
    const uint64_t cbBytes = static_cast<uint64_t>(kRingCount) * sizeof(GpuShadowPcssCB);
    if (!CreateBuffer(dev, cbBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
                      D3D12_RESOURCE_STATE_GENERIC_READ, m_cbBuf)) {
        if (outErr) *outErr = "RawShadowPcssHost: CBV upload ring failed";
        return false;
    }
    D3D12_RANGE rr{0, 0};
    void* ptr = nullptr;
    if (FAILED(m_cbBuf->Map(0, &rr, &ptr))) {
        if (outErr) *outErr = "RawShadowPcssHost: failed to map CBV ring";
        return false;
    }
    m_cbMapped = static_cast<uint8_t*>(ptr);
    std::memset(m_cbMapped, 0, static_cast<size_t>(cbBytes));

    // 4) 4-frame GPU timestamp query heap (2 queries/frame)
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

bool RawShadowPcssHost::HarvestTimestampPair(uint32_t queryStartIdx, float& outMs) const
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

D3D12_CPU_DESCRIPTOR_HANDLE RawShadowPcssHost::CpuAt(uint32_t slot) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(slot) * m_descInc;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE RawShadowPcssHost::GpuAt(uint32_t slot) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE h = m_heap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<UINT64>(slot) * m_descInc;
    return h;
}

bool RawShadowPcssHost::Execute(ID3D12GraphicsCommandList* cmd,
                                ID3D12Resource* sceneDepth,
                                ID3D12Resource* shadowMap,
                                ID3D12Resource* shadowOut,
                                uint32_t width,
                                uint32_t height,
                                const ShadowPcssParams& params,
                                D3D12_RESOURCE_STATES depthState,
                                D3D12_RESOURCE_STATES shadowMapState,
                                D3D12_RESOURCE_STATES shadowOutState)
{
    if (!m_ready || !cmd || !sceneDepth || !shadowMap || !shadowOut || !m_cbMapped || width == 0u || height == 0u) {
        return false;
    }

    // Harvest 3-frames-old retired timestamp slot (non-blocking)
    if (m_frameIdx >= (kRingCount - 1u)) {
        const uint32_t retired = (m_frameIdx - (kRingCount - 1u)) % kRingCount;
        float ms = 0.0f;
        if (HarvestTimestampPair(retired * 2u, ms)) {
            m_lastPcssMs = ms;
            m_hasValidTiming = true;
        }
    }

    const D3D12_RESOURCE_DESC depthDesc = sceneDepth->GetDesc();
    const D3D12_RESOURCE_DESC smDesc    = shadowMap->GetDesc();
    const D3D12_RESOURCE_DESC outDesc   = shadowOut->GetDesc();

    const uint32_t smSize = (params.shadowMapSize > 0u)
        ? params.shadowMapSize
        : static_cast<uint32_t>(std::max<UINT64>(smDesc.Width, 16ull));

    float sunDir[3] = {params.sunDirW[0], params.sunDirW[1], params.sunDirW[2]};
    Normalize3(sunDir);

    GpuShadowPcssCB cb{};
    std::memcpy(cb.invViewProj, params.invViewProj, sizeof(cb.invViewProj));
    std::memcpy(cb.lightViewProj, params.lightViewProj, sizeof(cb.lightViewProj));
    cb.sunDirAndBias[0] = sunDir[0];
    cb.sunDirAndBias[1] = sunDir[1];
    cb.sunDirAndBias[2] = sunDir[2];
    cb.sunDirAndBias[3] = std::max(params.baseDepthBias, 0.0f);

    cb.biasAndJitter[0] = std::max(params.slopeDepthBias, 0.0f);
    cb.biasAndJitter[1] = std::max(params.normalOffsetWorld, 0.0f);
    cb.biasAndJitter[2] = std::max(params.lightAngularSize, 0.01f);
    cb.biasAndJitter[3] = params.frameJitter;

    cb.filterParams[0] = std::max(params.blockerSearchTexels, 1.0f);
    cb.filterParams[1] = std::max(params.minFilterTexels, 0.25f);
    cb.filterParams[2] = std::max(params.maxFilterTexels, cb.filterParams[1] + 0.5f);
    cb.filterParams[3] = 1.0f / static_cast<float>(smSize);

    cb.dims[0] = width;
    cb.dims[1] = height;
    cb.dims[2] = smSize;
    cb.dims[3] = params.flags;

    const uint32_t ring = m_frameIdx % kRingCount;
    const size_t cbOff  = static_cast<size_t>(ring) * sizeof(GpuShadowPcssCB);
    std::memcpy(m_cbMapped + cbOff, &cb, sizeof(cb));
    const D3D12_GPU_VIRTUAL_ADDRESS cbGpu = m_cbBuf->GetGPUVirtualAddress() + cbOff;

    // Populate this ring slot's 3 descriptors: [t0 SceneDepth, t1 ShadowMap, u0 ShadowOut]
    const uint32_t baseSlot = ring * kDescsPerRingSlot;

    D3D12_SHADER_RESOURCE_VIEW_DESC dSrv{};
    dSrv.Format                  = ResolveDepthSrvFormat(depthDesc.Format);
    dSrv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    dSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    dSrv.Texture2D.MipLevels     = 1u;
    m_dev->CreateShaderResourceView(sceneDepth, &dSrv, CpuAt(baseSlot + 0u));

    D3D12_SHADER_RESOURCE_VIEW_DESC smSrv{};
    smSrv.Format                  = ResolveDepthSrvFormat(smDesc.Format);
    smSrv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    smSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    smSrv.Texture2D.MipLevels     = 1u;
    m_dev->CreateShaderResourceView(shadowMap, &smSrv, CpuAt(baseSlot + 1u));

    D3D12_UNORDERED_ACCESS_VIEW_DESC outUav{};
    outUav.Format        = outDesc.Format;
    outUav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    m_dev->CreateUnorderedAccessView(shadowOut, nullptr, &outUav, CpuAt(baseSlot + 2u));

    // Pre-dispatch barriers if caller states differ
    D3D12_RESOURCE_BARRIER preB[3]{};
    uint32_t preCount = 0u;
    auto pushPre = [&](ID3D12Resource* r, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
        if ((before & after) == after && before == after) return;
        D3D12_RESOURCE_BARRIER& b = preB[preCount++];
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
    };
    const bool depthNeedsTrans = ((depthState & D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) == 0);
    const bool smNeedsTrans    = ((shadowMapState & D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) == 0);
    const bool outNeedsTrans   = (shadowOutState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (depthNeedsTrans) pushPre(sceneDepth, depthState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (smNeedsTrans)    pushPre(shadowMap, shadowMapState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (outNeedsTrans)   pushPre(shadowOut, shadowOutState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (preCount > 0u) cmd->ResourceBarrier(preCount, preB);

    if (m_tsHeap) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, ring * 2u + 0u);
    }

    ID3D12DescriptorHeap* heaps[1] = {m_heap.Get()};
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetComputeRootSignature(m_root.Get());
    cmd->SetPipelineState(m_pso.Get());
    cmd->SetComputeRootConstantBufferView(0, cbGpu);
    cmd->SetComputeRootDescriptorTable(1, GpuAt(baseSlot + 0u));
    cmd->SetComputeRootDescriptorTable(2, GpuAt(baseSlot + 2u));
    cmd->Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);

    if (m_tsHeap && m_tsReadback) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, ring * 2u + 1u);
        cmd->ResolveQueryData(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, ring * 2u + 0u, 2u,
                              m_tsReadback.Get(), static_cast<UINT64>(ring * 2u) * sizeof(uint64_t));
    }

    // Restore caller states cleanly
    D3D12_RESOURCE_BARRIER postB[3]{};
    uint32_t postCount = 0u;
    if (depthNeedsTrans) {
        postB[postCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        postB[postCount++].Transition = {sceneDepth, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                         D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, depthState};
    }
    if (smNeedsTrans) {
        postB[postCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        postB[postCount++].Transition = {shadowMap, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                         D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, shadowMapState};
    }
    if (outNeedsTrans) {
        postB[postCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        postB[postCount++].Transition = {shadowOut, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS, shadowOutState};
    } else {
        postB[postCount].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        postB[postCount++].UAV.pResource = shadowOut;
    }
    if (postCount > 0u) cmd->ResourceBarrier(postCount, postB);

    ++m_frameIdx;
    return true;
}

} // namespace raw::bridge
