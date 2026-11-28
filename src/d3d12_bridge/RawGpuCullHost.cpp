// RawGpuCullHost.cpp: Native DX12 GPU-Driven Conservative Hi-Z Occlusion Pyramid & 65,536-Instance Culler.
// Original clean-room implementation, MIT License.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "RawGpuCullHost.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if __has_include("raw_hiz_reduce_cs.h") && __has_include("raw_cull_instances_cs.h")
#define RAW_GPU_CULL_HAS_EMBEDDED_DXIL 1
#include "raw_hiz_reduce_cs.h"
#include "raw_cull_instances_cs.h"
#else
#define RAW_GPU_CULL_HAS_EMBEDDED_DXIL 0
#endif

namespace raw::bridge
{
namespace
{
using Microsoft::WRL::ComPtr;

// GPU constant buffer matching RawGpuCull.hlsl CullViewCB byte-for-byte (256-byte aligned).
struct alignas(256) GpuCullViewCB {
    float    viewProj[16];        // 64 bytes
    float    frustumPlanes[6][4]; // 96 bytes
    float    camPosAndFlags[4];   // 16 bytes
    float    hizParams[4];        // 16 bytes
    uint32_t counts[4];           // 16 bytes
    uint32_t pad[12];             // 48 bytes -> 256 bytes exact
};
static_assert(sizeof(GpuCullViewCB) == 256u, "GpuCullViewCB must be 256 bytes");

// 64-byte reset template block (per ring slot) copied into VisibleCountBuf (20B) + IndirectArgsBuf (48B).
struct alignas(64) GpuCullResetSlot {
    uint32_t visibleCounts[16];     // 64 bytes (zeroed)
    uint32_t indirectArgs[16];      // 64 bytes: DrawIndexedArgs at [0..4], DispatchMeshArgs at [8..10]
};
static_assert(sizeof(GpuCullResetSlot) == 128u, "GpuCullResetSlot must be 128 bytes");

// Fixed shader-visible descriptor heap layout (48 descriptors):
//   Slots  0..3  : External srcDepthTex SRV (4-frame ring)
//   Slots  4..17 : Hi-Z per-mip SRVs (mip 0..13)
//   Slots 18..31 : Hi-Z per-mip UAVs (mip 0..13)
//   Slots 32..39 : Cull pass SRV table (t0=instances, t1=full-mip Hi-Z) x 4-frame ring
//   Slots 40..42 : Cull pass UAV table (u0=visibleIndices, u1=visibleCounts, u2=indirectArgs)
//   Slot  43     : External consumer SRV for visibleIndices
constexpr uint32_t kTotalDescCount      = 48u;
constexpr uint32_t kSlotSrcDepthSrvBase = 0u;
constexpr uint32_t kSlotHiZMipSrvBase   = 4u;
constexpr uint32_t kSlotHiZMipUavBase   = 18u;
constexpr uint32_t kSlotCullSrvRingBase = 32u;
constexpr uint32_t kSlotCullUavBase     = 40u;
constexpr uint32_t kSlotVisibleExtSrv   = 43u;

constexpr uint32_t kHiZQueryBase        = 0u; // 8 queries (4 ring slots x 2)
constexpr uint32_t kCullQueryBase       = 8u; // 8 queries (4 ring slots x 2)
constexpr uint32_t kTotalQueries        = 16u;

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

DXGI_FORMAT ResolveDepthSrvFormat(DXGI_FORMAT fmt)
{
    switch (fmt) {
        case DXGI_FORMAT_D32_FLOAT:
        case DXGI_FORMAT_R32_TYPELESS:
        case DXGI_FORMAT_R32_FLOAT:
            return DXGI_FORMAT_R32_FLOAT;
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
        case DXGI_FORMAT_R24G8_TYPELESS:
        case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
            return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        case DXGI_FORMAT_R32G8X24_TYPELESS:
        case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
            return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        case DXGI_FORMAT_D16_UNORM:
        case DXGI_FORMAT_R16_TYPELESS:
        case DXGI_FORMAT_R16_UNORM:
            return DXGI_FORMAT_R16_UNORM;
        case DXGI_FORMAT_R16_FLOAT:
            return DXGI_FORMAT_R16_FLOAT;
        default:
            return fmt;
    }
}

uint32_t ComputeMipCount(uint32_t w, uint32_t h)
{
    uint32_t m = 1u;
    uint32_t d = std::max(w, h);
    while (d > 1u && m < RawGpuCullHost::kMaxHiZMips) {
        d >>= 1u;
        ++m;
    }
    return m;
}

void WriteRawUav(ID3D12Device* dev, ID3D12Resource* res, uint32_t numUint32s, D3D12_CPU_DESCRIPTOR_HANDLE dst)
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC d{};
    d.Format                     = DXGI_FORMAT_R32_TYPELESS;
    d.ViewDimension              = D3D12_UAV_DIMENSION_BUFFER;
    d.Buffer.FirstElement        = 0u;
    d.Buffer.NumElements         = numUint32s;
    d.Buffer.StructureByteStride = 0u;
    d.Buffer.Flags               = D3D12_BUFFER_UAV_FLAG_RAW;
    dev->CreateUnorderedAccessView(res, nullptr, &d, dst);
}

} // namespace

void ExtractFrustumPlanesFromViewProj(const float m[16], float outPlanes[6][4])
{
    // Row-major matrix M used as clip = mul(float4(worldPos, 1), M):
    //   clip.x = dot(col0, p), clip.y = dot(col1, p), clip.z = dot(col2, p), clip.w = dot(col3, p)
    //   where col_k = (M[0*4+k], M[1*4+k], M[2*4+k], M[3*4+k]).
    auto col = [&](int k, float out[4]) {
        out[0] = m[0 * 4 + k];
        out[1] = m[1 * 4 + k];
        out[2] = m[2 * 4 + k];
        out[3] = m[3 * 4 + k];
    };
    float c0[4], c1[4], c2[4], c3[4];
    col(0, c0);
    col(1, c1);
    col(2, c2);
    col(3, c3);

    auto normalizePlane = [](float p[4]) {
        const float len = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        if (len > 1.0e-8f) {
            const float inv = 1.0f / len;
            p[0] *= inv;
            p[1] *= inv;
            p[2] *= inv;
            p[3] *= inv;
        }
    };

    for (int i = 0; i < 4; ++i) {
        outPlanes[0][i] = c3[i] + c0[i]; // Left:   clip.x >= -clip.w
        outPlanes[1][i] = c3[i] - c0[i]; // Right:  clip.x <=  clip.w
        outPlanes[2][i] = c3[i] + c1[i]; // Bottom: clip.y >= -clip.w
        outPlanes[3][i] = c3[i] - c1[i]; // Top:    clip.y <=  clip.w
        outPlanes[4][i] = c2[i];         // Near:   clip.z >=  0 (DX12 NDC)
        outPlanes[5][i] = c3[i] - c2[i]; // Far:    clip.z <=  clip.w
    }
    for (int p = 0; p < 6; ++p) {
        normalizePlane(outPlanes[p]);
    }
}

RawGpuCullHost::~RawGpuCullHost()
{
    Shutdown();
}

D3D12_CPU_DESCRIPTOR_HANDLE RawGpuCullHost::CpuAt(uint32_t slot) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(slot) * m_descInc;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE RawGpuCullHost::GpuAt(uint32_t slot) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE h = m_heap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<UINT64>(slot) * m_descInc;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE RawGpuCullHost::GetVisibleInstancesSRV() const
{
    return m_heap ? GpuAt(kSlotVisibleExtSrv) : D3D12_GPU_DESCRIPTOR_HANDLE{};
}

D3D12_CPU_DESCRIPTOR_HANDLE RawGpuCullHost::GetVisibleInstancesSRVCpu() const
{
    return m_heap ? CpuAt(kSlotVisibleExtSrv) : D3D12_CPU_DESCRIPTOR_HANDLE{};
}

void RawGpuCullHost::Transition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
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

bool RawGpuCullHost::HarvestTimestampPair(uint32_t queryStartIdx, float& outMs) const
{
    if (!m_tsReadback || m_tsFreqHz == 0ull) return false;
    void* mapped = nullptr;
    const D3D12_RANGE rr{static_cast<SIZE_T>(queryStartIdx) * sizeof(uint64_t),
                         static_cast<SIZE_T>(queryStartIdx + 2u) * sizeof(uint64_t)};
    if (FAILED(m_tsReadback->Map(0u, &rr, &mapped)) || !mapped) return false;
    const auto* ticks = static_cast<const uint64_t*>(mapped);
    const uint64_t t0 = ticks[queryStartIdx + 0u];
    const uint64_t t1 = ticks[queryStartIdx + 1u];
    const D3D12_RANGE wr{0u, 0u};
    m_tsReadback->Unmap(0u, &wr);
    if (t1 < t0) {
        outMs = 0.0f;
        return true;
    }
    outMs = static_cast<float>(static_cast<double>(t1 - t0) * 1000.0 / static_cast<double>(m_tsFreqHz));
    return true;
}

bool RawGpuCullHost::EnsureHiZTexture(uint32_t width, uint32_t height)
{
    if (!m_dev || width == 0u || height == 0u) return false;
    if (m_hizTex && m_hizWidth == width && m_hizHeight == height) {
        return true;
    }

    const uint32_t mips = ComputeMipCount(width, height);
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width            = width;
    rd.Height           = height;
    rd.DepthOrArraySize = 1u;
    rd.MipLevels        = static_cast<UINT16>(mips);
    rd.Format           = DXGI_FORMAT_R32_FLOAT;
    rd.SampleDesc.Count = 1u;
    rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    ComPtr<ID3D12Resource> tex;
    if (FAILED(m_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                              D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&tex)))) {
        return false;
    }

    m_hizTex    = std::move(tex);
    m_hizState  = D3D12_RESOURCE_STATE_COMMON;
    m_hizWidth  = width;
    m_hizHeight = height;
    m_hizMips   = mips;
    m_hizBuilt  = false;

    // Populate per-mip SRVs and UAVs in the shader-visible descriptor heap
    for (uint32_t m = 0u; m < mips; ++m) {
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format                        = DXGI_FORMAT_R32_FLOAT;
        sd.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
        sd.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Texture2D.MostDetailedMip     = m;
        sd.Texture2D.MipLevels           = 1u;
        m_dev->CreateShaderResourceView(m_hizTex.Get(), &sd, CpuAt(kSlotHiZMipSrvBase + m));

        D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};
        ud.Format             = DXGI_FORMAT_R32_FLOAT;
        ud.ViewDimension      = D3D12_UAV_DIMENSION_TEXTURE2D;
        ud.Texture2D.MipSlice = m;
        m_dev->CreateUnorderedAccessView(m_hizTex.Get(), nullptr, &ud, CpuAt(kSlotHiZMipUavBase + m));
    }

    // Populate full-mip Hi-Z SRV in every Cull ring slot (t1)
    for (uint32_t r = 0u; r < kCbRingCount; ++r) {
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format                    = DXGI_FORMAT_R32_FLOAT;
        sd.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
        sd.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Texture2D.MostDetailedMip = 0u;
        sd.Texture2D.MipLevels       = mips;
        m_dev->CreateShaderResourceView(m_hizTex.Get(), &sd, CpuAt(kSlotCullSrvRingBase + r * 2u + 1u));
    }

    return true;
}

bool RawGpuCullHost::Init(ID3D12Device* dev, const GpuCullDesc& desc, std::string* outErr)
{
    Shutdown();
    if (!dev) {
        if (outErr) *outErr = "RawGpuCullHost::Init: null ID3D12Device";
        return false;
    }

    m_dev          = dev;
    m_maxInstances = std::max(1u, desc.maxInstances);

    D3D12_SHADER_BYTECODE hizBc  = desc.customHiZShader;
    D3D12_SHADER_BYTECODE cullBc = desc.customCullShader;
#if RAW_GPU_CULL_HAS_EMBEDDED_DXIL
    if (!hizBc.pShaderBytecode || hizBc.BytecodeLength == 0u) {
        hizBc.pShaderBytecode = kDxil_raw_hiz_reduce;
        hizBc.BytecodeLength  = sizeof(kDxil_raw_hiz_reduce);
    }
    if (!cullBc.pShaderBytecode || cullBc.BytecodeLength == 0u) {
        cullBc.pShaderBytecode = kDxil_raw_cull_instances;
        cullBc.BytecodeLength  = sizeof(kDxil_raw_cull_instances);
    }
#endif
    if (!hizBc.pShaderBytecode || hizBc.BytecodeLength == 0u ||
        !cullBc.pShaderBytecode || cullBc.BytecodeLength == 0u) {
        if (outErr) *outErr = "RawGpuCullHost::Init: missing DXIL shader bytecode for Hi-Z / Cull";
        Shutdown();
        return false;
    }

    // 1. Shader-visible CBV_SRV_UAV descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = kTotalDescCount;
    hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_heap)))) {
        if (outErr) *outErr = "RawGpuCullHost::Init: CreateDescriptorHeap failed";
        Shutdown();
        return false;
    }
    m_descInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // 2. Hi-Z Reduce Root Signature: 8 root constants (b0), SRV table (t0), UAV table (u0)
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
        params[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.ShaderRegister = 0u;
        params[0].Constants.RegisterSpace  = 0u;
        params[0].Constants.Num32BitValues = 8u;
        params[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;

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
        ComPtr<ID3DBlob> sig, errBlob;
        if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &errBlob)) ||
            FAILED(dev->CreateRootSignature(0u, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_hizRoot)))) {
            if (outErr) *outErr = "RawGpuCullHost::Init: Hi-Z CreateRootSignature failed";
            Shutdown();
            return false;
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = m_hizRoot.Get();
        pd.CS             = hizBc;
        if (FAILED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_hizPso)))) {
            if (outErr) *outErr = "RawGpuCullHost::Init: Hi-Z CreateComputePipelineState failed";
            Shutdown();
            return false;
        }
    }

    // 3. Cull Instances Root Signature: Root CBV (b0), SRV table (t0..t1), UAV table (u0..u2)
    {
        D3D12_DESCRIPTOR_RANGE rSrv{};
        rSrv.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        rSrv.NumDescriptors                    = 2u;
        rSrv.BaseShaderRegister                = 0u;
        rSrv.OffsetInDescriptorsFromTableStart = 0u;

        D3D12_DESCRIPTOR_RANGE rUav{};
        rUav.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        rUav.NumDescriptors                    = 3u;
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
        ComPtr<ID3DBlob> sig, errBlob;
        if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &errBlob)) ||
            FAILED(dev->CreateRootSignature(0u, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_cullRoot)))) {
            if (outErr) *outErr = "RawGpuCullHost::Init: Cull CreateRootSignature failed";
            Shutdown();
            return false;
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = m_cullRoot.Get();
        pd.CS             = cullBc;
        if (FAILED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_cullPso)))) {
            if (outErr) *outErr = "RawGpuCullHost::Init: Cull CreateComputePipelineState failed";
            Shutdown();
            return false;
        }
    }

    // 4. Default-heap output buffers (VisibleInstanceBuffer, VisibleCountBuffer, IndirectArgsBuffer)
    const uint64_t visBytes = static_cast<uint64_t>(m_maxInstances) * sizeof(uint32_t);
    if (!CreateBuffer(dev, visBytes, D3D12_HEAP_TYPE_DEFAULT,
                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, m_visibleBuf) ||
        !CreateBuffer(dev, 64ull, D3D12_HEAP_TYPE_DEFAULT,
                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, m_visibleCountBuf) ||
        !CreateBuffer(dev, 64ull, D3D12_HEAP_TYPE_DEFAULT,
                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, m_indirectArgsBuf)) {
        if (outErr) *outErr = "RawGpuCullHost::Init: output GPU buffer creation failed";
        Shutdown();
        return false;
    }

    // 5. Persistently mapped 4-frame CB ring + 4-frame reset-template upload ring
    if (!CreateBuffer(dev, static_cast<uint64_t>(kCbRingCount) * sizeof(GpuCullViewCB),
                      D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, m_cbBuf) ||
        !CreateBuffer(dev, static_cast<uint64_t>(kCbRingCount) * sizeof(GpuCullResetSlot),
                      D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, m_resetUploadBuf)) {
        if (outErr) *outErr = "RawGpuCullHost::Init: upload ring buffer creation failed";
        Shutdown();
        return false;
    }
    const D3D12_RANGE zeroRange{0u, 0u};
    void* cbPtr = nullptr;
    void* rstPtr = nullptr;
    if (FAILED(m_cbBuf->Map(0u, &zeroRange, &cbPtr)) || !cbPtr ||
        FAILED(m_resetUploadBuf->Map(0u, &zeroRange, &rstPtr)) || !rstPtr) {
        if (outErr) *outErr = "RawGpuCullHost::Init: failed to map upload rings";
        Shutdown();
        return false;
    }
    m_cbMapped    = static_cast<uint8_t*>(cbPtr);
    m_resetMapped = static_cast<uint8_t*>(rstPtr);
    std::memset(m_cbMapped, 0, static_cast<size_t>(kCbRingCount) * sizeof(GpuCullViewCB));
    std::memset(m_resetMapped, 0, static_cast<size_t>(kCbRingCount) * sizeof(GpuCullResetSlot));

    // 6. Populate output UAVs (u0, u1, u2) and external visible SRV
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};
        ud.Format                     = DXGI_FORMAT_UNKNOWN;
        ud.ViewDimension              = D3D12_UAV_DIMENSION_BUFFER;
        ud.Buffer.FirstElement        = 0u;
        ud.Buffer.NumElements         = m_maxInstances;
        ud.Buffer.StructureByteStride = sizeof(uint32_t);
        dev->CreateUnorderedAccessView(m_visibleBuf.Get(), nullptr, &ud, CpuAt(kSlotCullUavBase + 0u));

        WriteRawUav(dev, m_visibleCountBuf.Get(), 16u, CpuAt(kSlotCullUavBase + 1u));
        WriteRawUav(dev, m_indirectArgsBuf.Get(), 16u, CpuAt(kSlotCullUavBase + 2u));

        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format                     = DXGI_FORMAT_UNKNOWN;
        sd.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
        sd.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Buffer.FirstElement        = 0u;
        sd.Buffer.NumElements         = m_maxInstances;
        sd.Buffer.StructureByteStride = sizeof(uint32_t);
        dev->CreateShaderResourceView(m_visibleBuf.Get(), &sd, CpuAt(kSlotVisibleExtSrv));
    }

    // 7. Initial Hi-Z pyramid texture
    const uint32_t initW = std::max(1u, desc.initialHiZWidth);
    const uint32_t initH = std::max(1u, desc.initialHiZHeight);
    if (!EnsureHiZTexture(initW, initH)) {
        if (outErr) *outErr = "RawGpuCullHost::Init: initial Hi-Z texture creation failed";
        Shutdown();
        return false;
    }

    // 8. Hardware GPU Timestamp QueryHeap + Readback Buffer (16 queries)
    D3D12_QUERY_HEAP_DESC qhd{};
    qhd.Type     = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    qhd.Count    = kTotalQueries;
    qhd.NodeMask = 0u;
    if (SUCCEEDED(dev->CreateQueryHeap(&qhd, IID_PPV_ARGS(&m_tsHeap)))) {
        CreateBuffer(dev, static_cast<uint64_t>(kTotalQueries) * sizeof(uint64_t),
                     D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
                     D3D12_RESOURCE_STATE_COPY_DEST, m_tsReadback);
    }

    m_ready = true;
    return true;
}

bool RawGpuCullHost::BuildHiZPyramid(ID3D12GraphicsCommandList* cmd,
                                     ID3D12Resource* srcDepthTex,
                                     uint32_t srcWidth,
                                     uint32_t srcHeight,
                                     D3D12_RESOURCE_STATES srcState)
{
    if (!m_ready || !cmd || !srcDepthTex || srcWidth == 0u || srcHeight == 0u) {
        return false;
    }
    if (!EnsureHiZTexture(srcWidth, srcHeight)) {
        return false;
    }

    const uint32_t ringSlot = m_hizFrameIdx % kCbRingCount;
    const uint32_t qStart   = kHiZQueryBase + ringSlot * 2u;

    if (m_hizFrameIdx >= kCbRingCount && m_tsReadback && m_tsFreqHz > 0ull) {
        float ms = 0.0f;
        if (HarvestTimestampPair(qStart, ms)) {
            m_lastHiZMs         = ms;
            m_hasValidHiZTiming = true;
        }
    }

    // Create SRV for srcDepthTex into this ring slot
    const D3D12_RESOURCE_DESC srcDesc = srcDepthTex->GetDesc();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format                    = ResolveDepthSrvFormat(srcDesc.Format);
    srvDesc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MostDetailedMip = 0u;
    srvDesc.Texture2D.MipLevels       = 1u;
    m_dev->CreateShaderResourceView(srcDepthTex, &srvDesc, CpuAt(kSlotSrcDepthSrvBase + ringSlot));

    const bool needSrcTransition = (srcState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (needSrcTransition) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = srcDepthTex;
        b.Transition.StateBefore = srcState;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1u, &b);
    }

    // Transition all Hi-Z mips from m_hizState -> UNORDERED_ACCESS
    Transition(cmd, m_hizTex.Get(), m_hizState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    if (m_tsHeap && m_tsReadback) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qStart);
    }

    ID3D12DescriptorHeap* heaps[1] = {m_heap.Get()};
    cmd->SetDescriptorHeaps(1u, heaps);
    cmd->SetComputeRootSignature(m_hizRoot.Get());
    cmd->SetPipelineState(m_hizPso.Get());

    // Pass 0: copy srcDepthTex -> Hi-Z mip 0
    {
        const uint32_t rc[8] = {srcWidth, srcHeight, m_hizWidth, m_hizHeight, 0u, 0u, 0u, 0u};
        cmd->SetComputeRoot32BitConstants(0u, 8u, rc, 0u);
        cmd->SetComputeRootDescriptorTable(1u, GpuAt(kSlotSrcDepthSrvBase + ringSlot));
        cmd->SetComputeRootDescriptorTable(2u, GpuAt(kSlotHiZMipUavBase + 0u));
        cmd->Dispatch((m_hizWidth + 7u) / 8u, (m_hizHeight + 7u) / 8u, 1u);

        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = m_hizTex.Get();
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b.Transition.Subresource = 0u;
        cmd->ResourceBarrier(1u, &b);
    }

    // Passes 1..mips-1: conservative max() mip-to-mip downsample with odd-edge coverage
    uint32_t prevW = m_hizWidth;
    uint32_t prevH = m_hizHeight;
    for (uint32_t m = 1u; m < m_hizMips; ++m) {
        const uint32_t dstW = std::max(1u, prevW >> 1u);
        const uint32_t dstH = std::max(1u, prevH >> 1u);

        const uint32_t rc[8] = {prevW, prevH, dstW, dstH, 1u, 0u, 0u, 0u};
        cmd->SetComputeRoot32BitConstants(0u, 8u, rc, 0u);
        cmd->SetComputeRootDescriptorTable(1u, GpuAt(kSlotHiZMipSrvBase + (m - 1u)));
        cmd->SetComputeRootDescriptorTable(2u, GpuAt(kSlotHiZMipUavBase + m));
        cmd->Dispatch((dstW + 7u) / 8u, (dstH + 7u) / 8u, 1u);

        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = m_hizTex.Get();
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b.Transition.Subresource = m;
        cmd->ResourceBarrier(1u, &b);

        prevW = dstW;
        prevH = dstH;
    }

    // All subresources 0..m_hizMips-1 are now in NON_PIXEL_SHADER_RESOURCE
    m_hizState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    m_hizBuilt = true;

    if (m_tsHeap && m_tsReadback) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qStart + 1u);
        cmd->ResolveQueryData(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qStart, 2u,
                              m_tsReadback.Get(), static_cast<UINT64>(qStart) * sizeof(uint64_t));
    }

    if (needSrcTransition) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = srcDepthTex;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b.Transition.StateAfter  = srcState;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1u, &b);
    }

    ++m_hizFrameIdx;
    return true;
}

bool RawGpuCullHost::CullInstances(ID3D12GraphicsCommandList* cmd,
                                   ID3D12Resource* instancesBuf,
                                   uint32_t instanceCount,
                                   const GpuCullViewParams& view,
                                   D3D12_RESOURCE_STATES instancesState)
{
    if (!m_ready || !cmd || !instancesBuf) {
        return false;
    }

    const uint32_t clampedCount = std::min(instanceCount, m_maxInstances);
    const uint32_t ringSlot     = m_cullFrameIdx % kCbRingCount;
    const uint32_t qStart       = kCullQueryBase + ringSlot * 2u;

    if (m_cullFrameIdx >= kCbRingCount && m_tsReadback && m_tsFreqHz > 0ull) {
        float ms = 0.0f;
        if (HarvestTimestampPair(qStart, ms)) {
            m_lastCullMs         = ms;
            m_hasValidCullTiming = true;
        }
    }

    // 1. Update reset template slot (zero counts + initialize indirect args template)
    GpuCullResetSlot rst{};
    rst.indirectArgs[0]  = view.defaultIndexCountPerInst; // DrawIndexed.IndexCountPerInstance
    rst.indirectArgs[1]  = 0u;                            // DrawIndexed.InstanceCount (GPU atomic)
    rst.indirectArgs[2]  = 0u;                            // StartIndexLocation
    rst.indirectArgs[3]  = 0u;                            // BaseVertexLocation
    rst.indirectArgs[4]  = 0u;                            // StartInstanceLocation
    rst.indirectArgs[8]  = 0u;                            // DispatchMesh.ThreadGroupCountX (GPU atomic)
    rst.indirectArgs[9]  = 1u;                            // DispatchMesh.ThreadGroupCountY
    rst.indirectArgs[10] = 1u;                            // DispatchMesh.ThreadGroupCountZ
    std::memcpy(m_resetMapped + static_cast<size_t>(ringSlot) * sizeof(GpuCullResetSlot), &rst, sizeof(rst));

    // 2. Update CullViewCB slot
    GpuCullViewCB cb{};
    std::memcpy(cb.viewProj, view.viewProj, sizeof(cb.viewProj));
    if (view.autoExtractPlanes) {
        ExtractFrustumPlanesFromViewProj(view.viewProj, cb.frustumPlanes);
    } else {
        std::memcpy(cb.frustumPlanes, view.frustumPlanes, sizeof(cb.frustumPlanes));
    }
    const bool useHiZ = view.enableHiZCull && m_hizBuilt;
    const uint32_t cullFlags = (view.enableFrustumCull ? 1u : 0u) |
                               (view.enableDistanceCull ? 2u : 0u) |
                               (useHiZ ? 4u : 0u);
    cb.camPosAndFlags[0] = view.cameraPosW[0];
    cb.camPosAndFlags[1] = view.cameraPosW[1];
    cb.camPosAndFlags[2] = view.cameraPosW[2];
    cb.camPosAndFlags[3] = 0.0f;
    cb.hizParams[0]      = static_cast<float>(m_hizWidth);
    cb.hizParams[1]      = static_cast<float>(m_hizHeight);
    cb.hizParams[2]      = static_cast<float>(m_hizMips);
    cb.hizParams[3]      = view.depthBias;
    cb.counts[0]         = clampedCount;
    cb.counts[1]         = view.defaultIndexCountPerInst;
    cb.counts[2]         = m_maxInstances;
    cb.counts[3]         = cullFlags;
    std::memcpy(m_cbMapped + static_cast<size_t>(ringSlot) * sizeof(GpuCullViewCB), &cb, sizeof(cb));

    // 3. Create SRV for external instancesBuf at t0 of this ring slot
    D3D12_SHADER_RESOURCE_VIEW_DESC instSrv{};
    instSrv.Format                     = DXGI_FORMAT_UNKNOWN;
    instSrv.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
    instSrv.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    instSrv.Buffer.FirstElement        = 0u;
    instSrv.Buffer.NumElements         = std::max(1u, clampedCount);
    instSrv.Buffer.StructureByteStride = sizeof(GpuCullInstance);
    m_dev->CreateShaderResourceView(instancesBuf, &instSrv, CpuAt(kSlotCullSrvRingBase + ringSlot * 2u));

    const bool needInstTransition = (instancesState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (needInstTransition) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = instancesBuf;
        b.Transition.StateBefore = instancesState;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1u, &b);
    }

    // Ensure Hi-Z pyramid is in NON_PIXEL_SHADER_RESOURCE even if BuildHiZPyramid wasn't called yet
    Transition(cmd, m_hizTex.Get(), m_hizState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    if (m_tsHeap && m_tsReadback) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qStart);
    }

    // 4. Reset VisibleCountBuffer & IndirectArgsBuffer via CopyBufferRegion
    Transition(cmd, m_visibleCountBuf.Get(), m_visibleCountState, D3D12_RESOURCE_STATE_COPY_DEST);
    Transition(cmd, m_indirectArgsBuf.Get(), m_indirectArgsState, D3D12_RESOURCE_STATE_COPY_DEST);

    const uint64_t rstBase = static_cast<uint64_t>(ringSlot) * sizeof(GpuCullResetSlot);
    cmd->CopyBufferRegion(m_visibleCountBuf.Get(), 0ull, m_resetUploadBuf.Get(), rstBase, 64ull);
    cmd->CopyBufferRegion(m_indirectArgsBuf.Get(), 0ull, m_resetUploadBuf.Get(), rstBase + 64ull, 64ull);

    // 5. Transition all outputs to UNORDERED_ACCESS & dispatch cs_cull_instances
    Transition(cmd, m_visibleBuf.Get(), m_visibleState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(cmd, m_visibleCountBuf.Get(), m_visibleCountState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(cmd, m_indirectArgsBuf.Get(), m_indirectArgsState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    if (clampedCount > 0u) {
        ID3D12DescriptorHeap* heaps[1] = {m_heap.Get()};
        cmd->SetDescriptorHeaps(1u, heaps);
        cmd->SetComputeRootSignature(m_cullRoot.Get());
        cmd->SetPipelineState(m_cullPso.Get());

        const D3D12_GPU_VIRTUAL_ADDRESS cbAddr =
            m_cbBuf->GetGPUVirtualAddress() + static_cast<uint64_t>(ringSlot) * sizeof(GpuCullViewCB);
        cmd->SetComputeRootConstantBufferView(0u, cbAddr);
        cmd->SetComputeRootDescriptorTable(1u, GpuAt(kSlotCullSrvRingBase + ringSlot * 2u));
        cmd->SetComputeRootDescriptorTable(2u, GpuAt(kSlotCullUavBase));

        const uint32_t groupsX = (clampedCount + 63u) / 64u;
        cmd->Dispatch(groupsX, 1u, 1u);
    }

    // 6. Transition outputs directly to shader / ExecuteIndirect ready states
    Transition(cmd, m_visibleBuf.Get(), m_visibleState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_visibleCountBuf.Get(), m_visibleCountState, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    Transition(cmd, m_indirectArgsBuf.Get(), m_indirectArgsState, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);

    if (m_tsHeap && m_tsReadback) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qStart + 1u);
        cmd->ResolveQueryData(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qStart, 2u,
                              m_tsReadback.Get(), static_cast<UINT64>(qStart) * sizeof(uint64_t));
    }

    if (needInstTransition) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = instancesBuf;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b.Transition.StateAfter  = instancesState;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1u, &b);
    }

    ++m_cullFrameIdx;
    return true;
}

void RawGpuCullHost::Shutdown()
{
    if (m_cbBuf && m_cbMapped) {
        m_cbBuf->Unmap(0u, nullptr);
        m_cbMapped = nullptr;
    }
    if (m_resetUploadBuf && m_resetMapped) {
        m_resetUploadBuf->Unmap(0u, nullptr);
        m_resetMapped = nullptr;
    }
    m_tsReadback.Reset();
    m_tsHeap.Reset();
    m_resetUploadBuf.Reset();
    m_cbBuf.Reset();
    m_indirectArgsBuf.Reset();
    m_visibleCountBuf.Reset();
    m_visibleBuf.Reset();
    m_hizTex.Reset();
    m_cullPso.Reset();
    m_cullRoot.Reset();
    m_hizPso.Reset();
    m_hizRoot.Reset();
    m_heap.Reset();

    m_dev                = nullptr;
    m_ready              = false;
    m_hizBuilt           = false;
    m_hizWidth           = 0u;
    m_hizHeight          = 0u;
    m_hizMips            = 0u;
    m_hizState           = D3D12_RESOURCE_STATE_COMMON;
    m_visibleState       = D3D12_RESOURCE_STATE_COMMON;
    m_visibleCountState  = D3D12_RESOURCE_STATE_COMMON;
    m_indirectArgsState  = D3D12_RESOURCE_STATE_COMMON;
    m_hizFrameIdx        = 0u;
    m_cullFrameIdx       = 0u;
    m_lastHiZMs          = 0.0f;
    m_lastCullMs         = 0.0f;
    m_hasValidHiZTiming  = false;
    m_hasValidCullTiming = false;
}

} // namespace raw::bridge
