#pragma once
// RawGpuCullHost.h: Native DX12 GPU-Driven Conservative Hi-Z Occlusion Pyramid & 65,536-Instance
// Frustum / Distance / Occlusion Culler producing ExecuteIndirect DrawIndexed & DispatchMesh arguments.
//
// Standard-Z depth convention (0.0 = near, 1.0 = far): conservative max() Hi-Z reduction with odd-edge coverage.
// Original clean-room implementation, MIT License.

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>

namespace raw::bridge
{

// 64-byte GPU instance bounds & meshlet descriptor matching RawGpuCull.hlsl byte-for-byte.
struct GpuCullInstance {
    float    center[3]     = {0.0f, 0.0f, 0.0f};
    float    radius        = 1.0f;
    float    aabbMin[3]    = {-1.0f, -1.0f, -1.0f};
    float    maxDrawDist   = 10000.0f;
    float    aabbMax[3]    = {1.0f, 1.0f, 1.0f};
    uint32_t meshletOffset = 0u;
    uint32_t meshletCount  = 1u;
    uint32_t instanceId    = 0u;
    uint32_t lodBiasFlags  = 0u; // bit 0: skipHiZ, bit 1: forceVisible
    uint32_t pad0          = 0u;
};
static_assert(sizeof(GpuCullInstance) == 64u, "GpuCullInstance must be 64 bytes");

// GPU readback / telemetry summary layout stored in VisibleCountBuffer (20 bytes, 5 uint32s).
struct GpuCullStats {
    uint32_t visibleInstances = 0u;
    uint32_t visibleMeshlets  = 0u;
    uint32_t frustumCulled    = 0u;
    uint32_t distanceCulled   = 0u;
    uint32_t hizCulled        = 0u;
};

// Indirect argument buffer layout (64 bytes):
//   Bytes  0..19: D3D12_DRAW_INDEXED_ARGUMENTS  (InstanceCount updated by GPU)
//   Bytes 32..43: D3D12_DISPATCH_MESH_ARGUMENTS (ThreadGroupCountX = total visible meshlets)
struct GpuCullIndirectArgsLayout {
    D3D12_DRAW_INDEXED_ARGUMENTS drawIndexed{};
    uint32_t                     pad0[3] = {};
    uint32_t                     dispatchMeshX = 0u;
    uint32_t                     dispatchMeshY = 1u;
    uint32_t                     dispatchMeshZ = 1u;
    uint32_t                     pad1 = 0u;
};
static_assert(sizeof(GpuCullIndirectArgsLayout) == 48u, "GpuCullIndirectArgsLayout layout check");

struct GpuCullDesc {
    uint32_t              maxInstances     = 65536u;
    uint32_t              initialHiZWidth  = 512u;
    uint32_t              initialHiZHeight = 512u;
    D3D12_SHADER_BYTECODE customHiZShader  = {}; // optional override; uses raw_hiz_reduce_cs.h when empty
    D3D12_SHADER_BYTECODE customCullShader = {}; // optional override; uses raw_cull_instances_cs.h when empty
};

struct GpuCullViewParams {
    // Row-major 4x4 ViewProj matrix used with row-vector clip = mul(float4(worldPos, 1), viewProj).
    float    viewProj[16]              = {};
    // Optional explicit 6 inward-pointing normalized frustum planes; if autoExtractPlanes is true,
    // extracted automatically from viewProj using Gribb-Hartmann for DX12 NDC (z in [0, w]).
    float    frustumPlanes[6][4]       = {};
    bool     autoExtractPlanes         = true;
    float    cameraPosW[3]             = {0.0f, 0.0f, 0.0f};
    float    depthBias                 = 1.0e-4f;
    uint32_t defaultIndexCountPerInst  = 36u;
    bool     enableFrustumCull         = true;
    bool     enableDistanceCull        = true;
    bool     enableHiZCull             = true;
};

// Extracts 6 normalized inward-pointing frustum planes (Left, Right, Bottom, Top, Near, Far)
// from a DX12 row-vector ViewProj matrix (clip = mul(float4(p, 1), viewProj), z in [0, w]).
void ExtractFrustumPlanesFromViewProj(const float viewProj[16], float outPlanes[6][4]);

class RawGpuCullHost {
public:
    static constexpr uint32_t kMaxHiZMips  = 14u;
    static constexpr uint32_t kCbRingCount = 4u;

    RawGpuCullHost() = default;
    ~RawGpuCullHost();

    RawGpuCullHost(const RawGpuCullHost&) = delete;
    RawGpuCullHost& operator=(const RawGpuCullHost&) = delete;

    bool Init(ID3D12Device* dev, const GpuCullDesc& desc = {}, std::string* outErr = nullptr);
    bool Ready() const { return m_ready; }

    // Builds the full conservative max() Hi-Z pyramid from a Standard-Z depth texture (0=near, 1=far).
    // Automatically reallocates the Hi-Z mip chain if srcWidth/srcHeight change.
    bool BuildHiZPyramid(ID3D12GraphicsCommandList* cmd,
                         ID3D12Resource* srcDepthTex,
                         uint32_t srcWidth,
                         uint32_t srcHeight,
                         D3D12_RESOURCE_STATES srcState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    // Executes ONE wave-compacted GPU compute dispatch culling up to maxInstances (65,536) instances
    // against 6 frustum planes, distance LOD cutoff, and the built Hi-Z pyramid.
    bool CullInstances(ID3D12GraphicsCommandList* cmd,
                       ID3D12Resource* instancesBuf,
                       uint32_t instanceCount,
                       const GpuCullViewParams& view,
                       D3D12_RESOURCE_STATES instancesState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    ID3D12DescriptorHeap*       DescriptorHeap() const { return m_heap.Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE GetVisibleInstancesSRV() const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetVisibleInstancesSRVCpu() const;
    ID3D12Resource*             GetVisibleInstancesBuffer() const { return m_visibleBuf.Get(); }
    D3D12_RESOURCE_STATES       VisibleInstancesResourceState() const { return m_visibleState; }

    ID3D12Resource*             GetIndirectArgsBuffer() const { return m_indirectArgsBuf.Get(); }
    D3D12_RESOURCE_STATES       IndirectArgsResourceState() const { return m_indirectArgsState; }

    ID3D12Resource*             GetVisibleCountBuffer() const { return m_visibleCountBuf.Get(); }
    D3D12_RESOURCE_STATES       VisibleCountResourceState() const { return m_visibleCountState; }

    ID3D12Resource*             GetHiZTexture() const { return m_hizTex.Get(); }
    D3D12_RESOURCE_STATES       HiZResourceState() const { return m_hizState; }
    uint32_t                    HiZWidth() const { return m_hizWidth; }
    uint32_t                    HiZHeight() const { return m_hizHeight; }
    uint32_t                    HiZMipCount() const { return m_hizMips; }
    uint32_t                    MaxInstances() const { return m_maxInstances; }

    void  SetTimestampFrequency(uint64_t hz) { m_tsFreqHz = hz; }
    float GetLastHiZMs() const { return m_lastHiZMs; }
    float GetLastCullMs() const { return m_lastCullMs; }
    bool  HasValidHiZTiming() const { return m_hasValidHiZTiming; }
    bool  HasValidCullTiming() const { return m_hasValidCullTiming; }

    void Shutdown();

private:
    bool EnsureHiZTexture(uint32_t width, uint32_t height);
    void Transition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
                    D3D12_RESOURCE_STATES& curState, D3D12_RESOURCE_STATES target);
    bool HarvestTimestampPair(uint32_t queryStartIdx, float& outMs) const;
    D3D12_CPU_DESCRIPTOR_HANDLE CpuAt(uint32_t slot) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GpuAt(uint32_t slot) const;

    bool          m_ready        = false;
    ID3D12Device* m_dev          = nullptr;
    uint32_t      m_maxInstances = 65536u;
    uint32_t      m_hizWidth     = 0u;
    uint32_t      m_hizHeight    = 0u;
    uint32_t      m_hizMips      = 0u;
    bool          m_hizBuilt     = false;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_heap;
    uint32_t                                     m_descInc = 0u;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_hizRoot;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_hizPso;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_cullRoot;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_cullPso;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_hizTex;
    D3D12_RESOURCE_STATES                  m_hizState = D3D12_RESOURCE_STATE_COMMON;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_visibleBuf;
    D3D12_RESOURCE_STATES                  m_visibleState = D3D12_RESOURCE_STATE_COMMON;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_visibleCountBuf;
    D3D12_RESOURCE_STATES                  m_visibleCountState = D3D12_RESOURCE_STATE_COMMON;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_indirectArgsBuf;
    D3D12_RESOURCE_STATES                  m_indirectArgsState = D3D12_RESOURCE_STATE_COMMON;

    // 4-frame persistently mapped CB + reset template upload ring
    Microsoft::WRL::ComPtr<ID3D12Resource> m_cbBuf;
    uint8_t*                               m_cbMapped = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_resetUploadBuf;
    uint8_t*                               m_resetMapped = nullptr;

    Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_tsHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource>  m_tsReadback;
    uint64_t                                m_tsFreqHz           = 0u;
    uint32_t                                m_hizFrameIdx        = 0u;
    uint32_t                                m_cullFrameIdx       = 0u;
    float                                   m_lastHiZMs          = 0.0f;
    float                                   m_lastCullMs         = 0.0f;
    bool                                    m_hasValidHiZTiming  = false;
    bool                                    m_hasValidCullTiming = false;
};

} // namespace raw::bridge
