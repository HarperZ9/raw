// RawPostCinematicHost.cpp: Native DX12 GPU Compute Host for RAW's Cinematic Post-Process Solver
// (16x16 Tile-Max Velocity Reduction + 3x3 Neighbor-Max Dilation + 12-Tap Directional Motion Blur
//  + Depth-Aware Physical Circle-of-Confusion Hexagonal Bokeh DoF with Foreground Bleed Guard
//  + Filmic Optical Vignette, SM 6.6 / 6.10).
// Original clean-room implementation, MIT License.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "RawPostCinematicHost.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if __has_include("raw_post_tile_max_cs.h") && __has_include("raw_post_neighbor_max_cs.h") && __has_include("raw_post_cinematic_cs.h")
#define RAW_POST_CINEMATIC_HAS_EMBEDDED_DXIL 1
#include "raw_post_tile_max_cs.h"
#include "raw_post_neighbor_max_cs.h"
#include "raw_post_cinematic_cs.h"
#else
#define RAW_POST_CINEMATIC_HAS_EMBEDDED_DXIL 0
#endif

namespace raw::bridge
{
namespace
{
using Microsoft::WRL::ComPtr;

// GPU constant buffer matching PostCinematicCB in RawPostCinematicSim.hlsl (256-byte aligned).
struct alignas(256) GpuPostCinematicCB {
    uint32_t dims[4];        // x=w, y=h, z=tileW, w=tileH
    float    invDims[4];     // x=1/w, y=1/h, z=nearZ, w=farZ
    float    dofLens[4];     // x=focusDistanceM, y=focalLengthM, z=apertureFStop, w=cocScale
    float    dofMotion[4];   // x=dofStrength, y=maxBokehRadiusPx, z=motionBlurScale, w=maxMotionRadiusPx
    float    vignetteJit[4]; // x=vignetteStrength, y=frameJitter, z=hasVelocityTex, w=0
    uint32_t pad[44];        // 80 + 176 = 256 bytes exact
};
static_assert(sizeof(GpuPostCinematicCB) == 256u, "GpuPostCinematicCB must be 256 bytes");

// Fixed shader-visible descriptor heap layout (56 descriptors):
//   Slots  0..15 : Pass 1 (TileMax) SRV table (t0=color, t1=depth, t2=vel, t3=null) x 4-frame ring
//   Slots 16..31 : Pass 2 (NeighborMax) SRV table (t0=null, t1=null, t2=null, t3=m_tileMaxTex) x 4-frame ring
//   Slots 32..47 : Pass 3 (Composite) SRV table (t0=color, t1=depth, t2=vel, t3=m_neighborMaxTex) x 4-frame ring
//   Slot  48     : m_tileMaxTex UAV (u0)
//   Slot  49     : m_neighborMaxTex UAV (u0)
//   Slot  50     : m_colorOut UAV (u0)
//   Slot  51     : m_colorOut SRV (external consumer)
constexpr uint32_t kTotalDescCount         = 56u;
constexpr uint32_t kSlotTileSrvRingBase    = 0u;
constexpr uint32_t kSlotNbrSrvRingBase     = 16u;
constexpr uint32_t kSlotCompSrvRingBase    = 32u;
constexpr uint32_t kSlotTileUav            = 48u;
constexpr uint32_t kSlotNeighborUav        = 49u;
constexpr uint32_t kSlotColorOutUav        = 50u;
constexpr uint32_t kSlotColorOutExtSrv     = 51u;

constexpr uint32_t kQueriesPerFrame        = 4u; // [0]=start, [1]=post-tile, [2]=post-nbr, [3]=post-comp
constexpr uint32_t kTotalQueries           = RawPostCinematicHost::kCbRingCount * kQueriesPerFrame; // 16

DXGI_FORMAT ResolveColorSrvFormat(DXGI_FORMAT fmt)
{
    switch (fmt) {
        case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case DXGI_FORMAT_R32G32B32A32_TYPELESS: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:     return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:     return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS:  return DXGI_FORMAT_R10G10B10A2_UNORM;
        default:                                return fmt;
    }
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

DXGI_FORMAT ResolveVelSrvFormat(DXGI_FORMAT fmt)
{
    switch (fmt) {
        case DXGI_FORMAT_R16G16_TYPELESS: return DXGI_FORMAT_R16G16_FLOAT;
        case DXGI_FORMAT_R32G32_TYPELESS: return DXGI_FORMAT_R32G32_FLOAT;
        default:                          return fmt;
    }
}

bool CreateBuffer(ID3D12Device* dev, uint64_t bytes, D3D12_HEAP_TYPE heapType,
                  D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initState, ComPtr<ID3D12Resource>& out)
{
    D3D12_HEAP_PROPERTIES hp{heapType};
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

bool CreateTex2D(ID3D12Device* dev, uint32_t w, uint32_t h, DXGI_FORMAT fmt, ComPtr<ID3D12Resource>& out)
{
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width            = w;
    rd.Height           = h;
    rd.DepthOrArraySize = 1u;
    rd.MipLevels        = 1u;
    rd.Format           = fmt;
    rd.SampleDesc.Count = 1u;
    rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    return SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                  D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&out)));
}

void WriteTex2DSrv(ID3D12Device* dev, ID3D12Resource* res, DXGI_FORMAT fmt, D3D12_CPU_DESCRIPTOR_HANDLE dst)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC d{};
    d.Format                    = fmt;
    d.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
    d.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    d.Texture2D.MostDetailedMip = 0u;
    d.Texture2D.MipLevels       = 1u;
    dev->CreateShaderResourceView(res, &d, dst);
}

void WriteTex2DUav(ID3D12Device* dev, ID3D12Resource* res, DXGI_FORMAT fmt, D3D12_CPU_DESCRIPTOR_HANDLE dst)
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC d{};
    d.Format             = fmt;
    d.ViewDimension      = D3D12_UAV_DIMENSION_TEXTURE2D;
    d.Texture2D.MipSlice = 0u;
    dev->CreateUnorderedAccessView(res, nullptr, &d, dst);
}

} // namespace

RawPostCinematicHost::~RawPostCinematicHost()
{
    Shutdown();
}

D3D12_CPU_DESCRIPTOR_HANDLE RawPostCinematicHost::CpuAt(uint32_t slot) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(slot) * m_descInc;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE RawPostCinematicHost::GpuAt(uint32_t slot) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE h = m_heap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<UINT64>(slot) * m_descInc;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE RawPostCinematicHost::GetOutputSRV() const
{
    return m_heap ? GpuAt(kSlotColorOutExtSrv) : D3D12_GPU_DESCRIPTOR_HANDLE{};
}

D3D12_CPU_DESCRIPTOR_HANDLE RawPostCinematicHost::GetOutputSRVCpu() const
{
    return m_heap ? CpuAt(kSlotColorOutExtSrv) : D3D12_CPU_DESCRIPTOR_HANDLE{};
}

void RawPostCinematicHost::Transition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
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

bool RawPostCinematicHost::HarvestTimestampRange(uint32_t idxA, uint32_t idxB, float& outMs) const
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

bool RawPostCinematicHost::EnsureOutputs(uint32_t width, uint32_t height)
{
    if (!m_dev || width == 0u || height == 0u) return false;
    if (m_tileMaxTex && m_neighborMaxTex && m_colorOut && m_width == width && m_height == height) {
        return true;
    }

    const uint32_t tileW = (width + 15u) / 16u;
    const uint32_t tileH = (height + 15u) / 16u;

    ComPtr<ID3D12Resource> tileTex, nbrTex, outTex;
    if (!CreateTex2D(m_dev, tileW, tileH, DXGI_FORMAT_R16G16_FLOAT, tileTex) ||
        !CreateTex2D(m_dev, tileW, tileH, DXGI_FORMAT_R16G16_FLOAT, nbrTex) ||
        !CreateTex2D(m_dev, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, outTex)) {
        return false;
    }

    m_tileMaxTex       = std::move(tileTex);
    m_neighborMaxTex   = std::move(nbrTex);
    m_colorOut         = std::move(outTex);
    m_tileMaxState     = D3D12_RESOURCE_STATE_COMMON;
    m_neighborMaxState = D3D12_RESOURCE_STATE_COMMON;
    m_colorOutState    = D3D12_RESOURCE_STATE_COMMON;
    m_width            = width;
    m_height           = height;
    m_tileW            = tileW;
    m_tileH            = tileH;

    WriteTex2DUav(m_dev, m_tileMaxTex.Get(), DXGI_FORMAT_R16G16_FLOAT, CpuAt(kSlotTileUav));
    WriteTex2DUav(m_dev, m_neighborMaxTex.Get(), DXGI_FORMAT_R16G16_FLOAT, CpuAt(kSlotNeighborUav));
    WriteTex2DUav(m_dev, m_colorOut.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, CpuAt(kSlotColorOutUav));
    WriteTex2DSrv(m_dev, m_colorOut.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, CpuAt(kSlotColorOutExtSrv));

    // Pre-bind static descriptor slots across all 4 ring frames
    for (uint32_t r = 0u; r < kCbRingCount; ++r) {
        // Pass 1 (TileMax): t3 = null
        WriteTex2DSrv(m_dev, nullptr, DXGI_FORMAT_R16G16_FLOAT, CpuAt(kSlotTileSrvRingBase + r * 4u + 3u));
        // Pass 2 (NeighborMax): t0..t2 = null, t3 = m_tileMaxTex
        WriteTex2DSrv(m_dev, nullptr, DXGI_FORMAT_R16G16B16A16_FLOAT, CpuAt(kSlotNbrSrvRingBase + r * 4u + 0u));
        WriteTex2DSrv(m_dev, nullptr, DXGI_FORMAT_R32_FLOAT,          CpuAt(kSlotNbrSrvRingBase + r * 4u + 1u));
        WriteTex2DSrv(m_dev, nullptr, DXGI_FORMAT_R16G16_FLOAT,       CpuAt(kSlotNbrSrvRingBase + r * 4u + 2u));
        WriteTex2DSrv(m_dev, m_tileMaxTex.Get(), DXGI_FORMAT_R16G16_FLOAT, CpuAt(kSlotNbrSrvRingBase + r * 4u + 3u));
        // Pass 3 (Composite): t3 = m_neighborMaxTex
        WriteTex2DSrv(m_dev, m_neighborMaxTex.Get(), DXGI_FORMAT_R16G16_FLOAT, CpuAt(kSlotCompSrvRingBase + r * 4u + 3u));
    }

    return true;
}

bool RawPostCinematicHost::Init(ID3D12Device* dev, const PostCinematicDesc& desc, std::string* outErr)
{
    Shutdown();
    if (!dev) {
        if (outErr) *outErr = "RawPostCinematicHost::Init: null ID3D12Device";
        return false;
    }

    m_dev = dev;

    D3D12_SHADER_BYTECODE tileBc = desc.customTileMaxShader;
    D3D12_SHADER_BYTECODE nbrBc  = desc.customNeighborMaxShader;
    D3D12_SHADER_BYTECODE compBc = desc.customCompositeShader;
#if RAW_POST_CINEMATIC_HAS_EMBEDDED_DXIL
    if (!tileBc.pShaderBytecode || tileBc.BytecodeLength == 0u) {
        tileBc.pShaderBytecode = kDxil_raw_post_tile_max;
        tileBc.BytecodeLength  = sizeof(kDxil_raw_post_tile_max);
    }
    if (!nbrBc.pShaderBytecode || nbrBc.BytecodeLength == 0u) {
        nbrBc.pShaderBytecode = kDxil_raw_post_neighbor_max;
        nbrBc.BytecodeLength  = sizeof(kDxil_raw_post_neighbor_max);
    }
    if (!compBc.pShaderBytecode || compBc.BytecodeLength == 0u) {
        compBc.pShaderBytecode = kDxil_raw_post_cinematic;
        compBc.BytecodeLength  = sizeof(kDxil_raw_post_cinematic);
    }
#endif
    if (!tileBc.pShaderBytecode || tileBc.BytecodeLength == 0u ||
        !nbrBc.pShaderBytecode || nbrBc.BytecodeLength == 0u ||
        !compBc.pShaderBytecode || compBc.BytecodeLength == 0u) {
        if (outErr) *outErr = "RawPostCinematicHost::Init: missing DXIL bytecode for PostCinematic passes";
        Shutdown();
        return false;
    }

    // 1. Shader-visible descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = kTotalDescCount;
    hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_heap)))) {
        if (outErr) *outErr = "RawPostCinematicHost::Init: CreateDescriptorHeap failed";
        Shutdown();
        return false;
    }
    m_descInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // 2. Shared Root Signature: Root CBV (b0), SRV table (t0..t3), UAV table (u0), Static linear sampler (s0)
    {
        D3D12_DESCRIPTOR_RANGE rSrv{};
        rSrv.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        rSrv.NumDescriptors                    = 4u;
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
        samp.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
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
            if (outErr) *outErr = "RawPostCinematicHost::Init: CreateRootSignature failed";
            Shutdown();
            return false;
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = m_root.Get();
        pd.CS             = tileBc;
        if (FAILED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_tileMaxPso)))) {
            if (outErr) *outErr = "RawPostCinematicHost::Init: CreateComputePipelineState (cs_post_tile_max) failed";
            Shutdown();
            return false;
        }
        pd.CS = nbrBc;
        if (FAILED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_neighborMaxPso)))) {
            if (outErr) *outErr = "RawPostCinematicHost::Init: CreateComputePipelineState (cs_post_neighbor_max) failed";
            Shutdown();
            return false;
        }
        pd.CS = compBc;
        if (FAILED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_compositePso)))) {
            if (outErr) *outErr = "RawPostCinematicHost::Init: CreateComputePipelineState (cs_post_cinematic_composite) failed";
            Shutdown();
            return false;
        }
    }

    // 3. Initial internal targets
    const uint32_t initW = std::max(desc.initialWidth, 16u);
    const uint32_t initH = std::max(desc.initialHeight, 16u);
    if (!EnsureOutputs(initW, initH)) {
        if (outErr) *outErr = "RawPostCinematicHost::Init: EnsureOutputs failed";
        Shutdown();
        return false;
    }

    // 4. 4-frame Constant Buffer ring
    if (!CreateBuffer(dev, sizeof(GpuPostCinematicCB) * kCbRingCount, D3D12_HEAP_TYPE_UPLOAD,
                      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, m_cbBuf)) {
        if (outErr) *outErr = "RawPostCinematicHost::Init: CreateBuffer (CB ring) failed";
        Shutdown();
        return false;
    }
    const D3D12_RANGE rr{0u, 0u};
    if (FAILED(m_cbBuf->Map(0u, &rr, reinterpret_cast<void**>(&m_cbMapped))) || !m_cbMapped) {
        if (outErr) *outErr = "RawPostCinematicHost::Init: Map CB ring failed";
        Shutdown();
        return false;
    }

    // 5. GPU Timestamp Query Heap & Readback Ring
    D3D12_QUERY_HEAP_DESC qhd{};
    qhd.Type  = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    qhd.Count = kTotalQueries;
    if (SUCCEEDED(dev->CreateQueryHeap(&qhd, IID_PPV_ARGS(&m_tsHeap)))) {
        CreateBuffer(dev, static_cast<uint64_t>(kTotalQueries) * sizeof(uint64_t), D3D12_HEAP_TYPE_READBACK,
                     D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, m_tsReadback);
    }

    m_frameIdx        = 0u;
    m_lastTileMs      = 0.0f;
    m_lastCompositeMs = 0.0f;
    m_hasValidTiming  = false;
    m_ready           = true;
    return true;
}

bool RawPostCinematicHost::Execute(ID3D12GraphicsCommandList* cmd,
                                   ID3D12Resource* sceneColor,
                                   ID3D12Resource* sceneDepth,
                                   ID3D12Resource* velocityTex,
                                   uint32_t width,
                                   uint32_t height,
                                   const PostCinematicParams& params,
                                   D3D12_RESOURCE_STATES srcColorState,
                                   D3D12_RESOURCE_STATES srcDepthState,
                                   D3D12_RESOURCE_STATES srcVelState)
{
    if (!m_ready || !cmd || !sceneColor || !sceneDepth || width == 0u || height == 0u) return false;
    if (!EnsureOutputs(width, height)) return false;

    const uint32_t ringSlot = m_frameIdx % kCbRingCount;

    // Harvest oldest retired GPU timestamp ring slot
    if (m_frameIdx >= kCbRingCount && m_tsFreqHz > 0ull && m_tsReadback) {
        const uint32_t qBase = ringSlot * kQueriesPerFrame;
        float tMs = 0.0f, cMs = 0.0f;
        if (HarvestTimestampRange(qBase + 0u, qBase + 2u, tMs) &&
            HarvestTimestampRange(qBase + 2u, qBase + 3u, cMs)) {
            m_lastTileMs      = tMs;
            m_lastCompositeMs = cMs;
            m_hasValidTiming  = true;
        }
    }

    // Write per-frame dynamic SRVs into Pass 1 & Pass 3 descriptor tables for ringSlot
    const DXGI_FORMAT colorFmt = ResolveColorSrvFormat(sceneColor->GetDesc().Format);
    const DXGI_FORMAT depthFmt = ResolveDepthSrvFormat(sceneDepth->GetDesc().Format);
    const DXGI_FORMAT velFmt   = velocityTex ? ResolveVelSrvFormat(velocityTex->GetDesc().Format) : DXGI_FORMAT_R16G16_FLOAT;

    const uint32_t tileBase = kSlotTileSrvRingBase + ringSlot * 4u;
    WriteTex2DSrv(m_dev, sceneColor,  colorFmt, CpuAt(tileBase + 0u));
    WriteTex2DSrv(m_dev, sceneDepth,  depthFmt, CpuAt(tileBase + 1u));
    WriteTex2DSrv(m_dev, velocityTex, velFmt,   CpuAt(tileBase + 2u));

    const uint32_t compBase = kSlotCompSrvRingBase + ringSlot * 4u;
    WriteTex2DSrv(m_dev, sceneColor,  colorFmt, CpuAt(compBase + 0u));
    WriteTex2DSrv(m_dev, sceneDepth,  depthFmt, CpuAt(compBase + 1u));
    WriteTex2DSrv(m_dev, velocityTex, velFmt,   CpuAt(compBase + 2u));

    // Populate 256-byte Constant Buffer
    const float nearZ = std::max(params.nearZ, 1.0e-4f);
    const float farZ  = std::max(params.farZ, nearZ + 1.0f);
    const bool hasVel = (velocityTex != nullptr) && (params.motionBlurScale > 0.0f) && (params.maxMotionRadiusPx > 0.25f);

    GpuPostCinematicCB cb{};
    cb.dims[0]        = width;
    cb.dims[1]        = height;
    cb.dims[2]        = m_tileW;
    cb.dims[3]        = m_tileH;

    cb.invDims[0]     = 1.0f / static_cast<float>(width);
    cb.invDims[1]     = 1.0f / static_cast<float>(height);
    cb.invDims[2]     = nearZ;
    cb.invDims[3]     = farZ;

    cb.dofLens[0]     = std::max(params.focusDistanceM, 0.1f);
    cb.dofLens[1]     = std::clamp(params.focalLengthMm * 0.001f, 0.008f, 0.400f);
    cb.dofLens[2]     = std::max(params.apertureFStop, 0.7f);
    cb.dofLens[3]     = std::max(params.cocScale, 0.0f);

    cb.dofMotion[0]   = std::clamp(params.dofStrength, 0.0f, 4.0f);
    cb.dofMotion[1]   = std::clamp(params.maxBokehRadiusPx, 0.0f, 32.0f);
    cb.dofMotion[2]   = std::max(params.motionBlurScale, 0.0f);
    cb.dofMotion[3]   = std::clamp(params.maxMotionRadiusPx, 0.0f, 64.0f);

    cb.vignetteJit[0] = std::clamp(params.vignetteStrength, 0.0f, 1.0f);
    cb.vignetteJit[1] = params.frameJitter;
    cb.vignetteJit[2] = hasVel ? 1.0f : 0.0f;
    cb.vignetteJit[3] = 0.0f;

    std::memcpy(m_cbMapped + static_cast<size_t>(ringSlot) * sizeof(GpuPostCinematicCB), &cb, sizeof(cb));
    const D3D12_GPU_VIRTUAL_ADDRESS cbGpuAddr =
        m_cbBuf->GetGPUVirtualAddress() + static_cast<uint64_t>(ringSlot) * sizeof(GpuPostCinematicCB);

    // Transition external input textures to NON_PIXEL_SHADER_RESOURCE as needed
    D3D12_RESOURCE_BARRIER inB[3]{};
    uint32_t inCount = 0u;
    if (srcColorState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        inB[inCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        inB[inCount].Transition = {sceneColor, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                   srcColorState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
        ++inCount;
    }
    if (srcDepthState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        inB[inCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        inB[inCount].Transition = {sceneDepth, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                   srcDepthState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
        ++inCount;
    }
    if (velocityTex && srcVelState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        inB[inCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        inB[inCount].Transition = {velocityTex, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                   srcVelState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
        ++inCount;
    }
    if (inCount > 0u) cmd->ResourceBarrier(inCount, inB);

    ID3D12DescriptorHeap* heaps[1] = {m_heap.Get()};
    cmd->SetDescriptorHeaps(1u, heaps);
    cmd->SetComputeRootSignature(m_root.Get());
    cmd->SetComputeRootConstantBufferView(0u, cbGpuAddr);

    const bool doTs = (m_tsHeap != nullptr && m_tsReadback != nullptr);
    const uint32_t qBase = ringSlot * kQueriesPerFrame;
    if (doTs) cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qBase + 0u);

    // Pass 1: 16x16 Tile-Max Velocity Reduction
    Transition(cmd, m_tileMaxTex.Get(), m_tileMaxState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmd->SetPipelineState(m_tileMaxPso.Get());
    cmd->SetComputeRootDescriptorTable(1u, GpuAt(tileBase));
    cmd->SetComputeRootDescriptorTable(2u, GpuAt(kSlotTileUav));
    cmd->Dispatch(m_tileW, m_tileH, 1u);

    if (doTs) cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qBase + 1u);

    // Pass 2: 3x3 Neighbor-Max Velocity Dilation
    Transition(cmd, m_tileMaxTex.Get(), m_tileMaxState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_neighborMaxTex.Get(), m_neighborMaxState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmd->SetPipelineState(m_neighborMaxPso.Get());
    cmd->SetComputeRootDescriptorTable(1u, GpuAt(kSlotNbrSrvRingBase + ringSlot * 4u));
    cmd->SetComputeRootDescriptorTable(2u, GpuAt(kSlotNeighborUav));
    cmd->Dispatch((m_tileW + 7u) / 8u, (m_tileH + 7u) / 8u, 1u);

    if (doTs) cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qBase + 2u);

    // Pass 3: Hexagonal Bokeh DoF + 12-Tap Motion Blur + Filmic Vignette
    Transition(cmd, m_neighborMaxTex.Get(), m_neighborMaxState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_colorOut.Get(), m_colorOutState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmd->SetPipelineState(m_compositePso.Get());
    cmd->SetComputeRootDescriptorTable(1u, GpuAt(compBase));
    cmd->SetComputeRootDescriptorTable(2u, GpuAt(kSlotColorOutUav));
    cmd->Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);

    if (doTs) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qBase + 3u);
        cmd->ResolveQueryData(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qBase, kQueriesPerFrame,
                              m_tsReadback.Get(), static_cast<uint64_t>(qBase) * sizeof(uint64_t));
    }

    // Leave ColorOut ready for downstream SRV consumers and restore all external input states
    Transition(cmd, m_colorOut.Get(), m_colorOutState,
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    if (inCount > 0u) {
        for (uint32_t i = 0u; i < inCount; ++i) {
            std::swap(inB[i].Transition.StateBefore, inB[i].Transition.StateAfter);
        }
        cmd->ResourceBarrier(inCount, inB);
    }

    ++m_frameIdx;
    return true;
}

void RawPostCinematicHost::Shutdown()
{
    if (m_cbBuf && m_cbMapped) {
        m_cbBuf->Unmap(0u, nullptr);
        m_cbMapped = nullptr;
    }
    m_tsReadback.Reset();
    m_tsHeap.Reset();
    m_cbBuf.Reset();
    m_colorOut.Reset();
    m_neighborMaxTex.Reset();
    m_tileMaxTex.Reset();
    m_compositePso.Reset();
    m_neighborMaxPso.Reset();
    m_tileMaxPso.Reset();
    m_root.Reset();
    m_heap.Reset();
    m_dev              = nullptr;
    m_width            = 0u;
    m_height           = 0u;
    m_tileW            = 0u;
    m_tileH            = 0u;
    m_descInc          = 0u;
    m_frameIdx         = 0u;
    m_lastTileMs       = 0.0f;
    m_lastCompositeMs  = 0.0f;
    m_hasValidTiming   = false;
    m_tileMaxState     = D3D12_RESOURCE_STATE_COMMON;
    m_neighborMaxState = D3D12_RESOURCE_STATE_COMMON;
    m_colorOutState    = D3D12_RESOURCE_STATE_COMMON;
    m_ready            = false;
}

} // namespace raw::bridge
