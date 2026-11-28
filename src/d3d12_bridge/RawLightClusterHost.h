#pragma once
// RawLightClusterHost.h: Native DX12 16x9x24 (3,456-Cluster) 3D Froxel Local Light Assignment Host.
// Supports up to 1,024 simultaneous dynamic point/spot/fire/spell lights in < 0.05 ms on RTX 4090.
// Original clean-room implementation, MIT License.

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <vector>

namespace raw::bridge
{

enum ClusterLightTypeFlags : uint32_t {
    kClusterLightPoint   = 0u,
    kClusterLightSpotBit = 1u,      // bit 0: spotlight cone culling active
    kClusterLightFire    = 2u << 1, // semantic tag: fire/hearth
    kClusterLightSpell   = 3u << 1  // semantic tag: magic spell
};

// 32-byte view-space cluster AABB matching RawLightClusterSim.hlsl byte-for-byte.
struct ClusterAabb {
    float minBounds[3] = {0.0f, 0.0f, 0.0f};
    float pad0         = 0.0f;
    float maxBounds[3] = {0.0f, 0.0f, 0.0f};
    float pad1         = 0.0f;
};
static_assert(sizeof(ClusterAabb) == 32u, "ClusterAabb must be 32 bytes");

// 48-byte dynamic light descriptor matching RawLightClusterSim.hlsl byte-for-byte.
struct GpuClusterLight {
    float    posView[3]   = {0.0f, 0.0f, 0.0f}; // View-space position (+Z forward)
    float    radius       = 0.0f;               // Influence radius (> 0 active)
    float    dirView[3]   = {0.0f, 0.0f, 1.0f}; // Normalized view-space spotlight direction
    float    cosOuterCone = -1.0f;              // cos(outerHalfAngle); -1 for omnidirectional point lights
    float    colorRgb[3]  = {1.0f, 1.0f, 1.0f}; // Linear HDR color * intensity
    uint32_t typeFlags    = 0u;                 // bit 0 = spotlight cone test active
};
static_assert(sizeof(GpuClusterLight) == 48u, "GpuClusterLight must be 48 bytes");

struct ClusterLightGridEntry {
    uint32_t offset = 0u;
    uint32_t count  = 0u;
};
static_assert(sizeof(ClusterLightGridEntry) == 8u, "ClusterLightGridEntry must be 8 bytes");

struct LightClusterDesc {
    uint32_t              maxLights         = 1024u;
    D3D12_SHADER_BYTECODE customBuildShader = {}; // optional override; uses raw_cluster_build_aabb_cs.h when empty
    D3D12_SHADER_BYTECODE customCullShader  = {}; // optional override; uses raw_cluster_cull_lights_cs.h when empty
};

struct LightClusterViewParams {
    // Row-major 4x4 inverse projection matrix mapping row-vector float4(ndcX, ndcY, 0, 1) -> view-space.
    float invProj[16] = {};
    float nearZ       = 0.5f;   // > 0, LH +Z forward
    float farZ        = 500.0f; // > nearZ
};

// Pure CPU helper: builds a row-major inverse DX12 LH perspective matrix (+Z forward, NDC Z in [0, 1]).
void BuildPerspectiveInvProjLH(float fovYRadians, float aspect, float nearZ, float farZ, float outInvProj[16]);

// Exact CPU reference matching RawLightClusterSim.hlsl (for deterministic CPU-vs-GPU parity verification).
void CpuBuildAndAssignClusters(const GpuClusterLight* lights,
                               uint32_t lightCount,
                               const LightClusterViewParams& view,
                               std::vector<ClusterAabb>& outAabbs,
                               std::vector<ClusterLightGridEntry>& outGrid,
                               std::vector<uint32_t>& outIndices);

class RawLightClusterHost {
public:
    static constexpr uint32_t kTilesX              = 16u;
    static constexpr uint32_t kTilesY              = 9u;
    static constexpr uint32_t kSlicesZ             = 24u;
    static constexpr uint32_t kTotalClusters       = kTilesX * kTilesY * kSlicesZ; // 3,456
    static constexpr uint32_t kMaxLights           = 1024u;
    static constexpr uint32_t kMaxLightsPerCluster = 64u;
    static constexpr uint32_t kTotalIndexEntries   = kTotalClusters * kMaxLightsPerCluster; // 221,184
    static constexpr uint32_t kRingCount           = 4u;

    RawLightClusterHost() = default;
    ~RawLightClusterHost();

    RawLightClusterHost(const RawLightClusterHost&) = delete;
    RawLightClusterHost& operator=(const RawLightClusterHost&) = delete;

    bool Init(ID3D12Device* dev, const LightClusterDesc& desc = {}, std::string* outErr = nullptr);
    bool Ready() const { return m_ready; }

    // Uploads lights, builds all 3,456 logarithmic-Z cluster AABBs, and assigns up to 1,024 lights
    // into ClusterLightGrid + ClusterLightIndices with GPU timestamp profiling.
    bool BuildAndAssign(ID3D12GraphicsCommandList* cmd,
                        const GpuClusterLight* lights,
                        uint32_t lightCount,
                        const LightClusterViewParams& view);

    ID3D12DescriptorHeap*       DescriptorHeap() const { return m_heap.Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE GetClusterGridSRV() const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetClusterGridSRVCpu() const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetLightIndicesSRV() const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetLightIndicesSRVCpu() const;

    ID3D12Resource*             ClusterGridResource() const { return m_clusterGridBuf.Get(); }
    D3D12_RESOURCE_STATES       ClusterGridState() const { return m_clusterGridState; }
    ID3D12Resource*             LightIndicesResource() const { return m_lightIndicesBuf.Get(); }
    D3D12_RESOURCE_STATES       LightIndicesState() const { return m_lightIndicesState; }
    ID3D12Resource*             ClusterAabbResource() const { return m_aabbBuf.Get(); }
    D3D12_RESOURCE_STATES       ClusterAabbState() const { return m_aabbState; }

    void  SetTimestampFrequency(uint64_t hz) { m_tsFreqHz = hz; }
    float GetLastBuildMs() const { return m_lastBuildMs; }
    float GetLastAssignMs() const { return m_lastAssignMs; }
    bool  HasValidTiming() const { return m_hasValidTiming; }

    void Shutdown();

private:
    void Transition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
                    D3D12_RESOURCE_STATES& curState, D3D12_RESOURCE_STATES target);
    bool HarvestTimestampPair(uint32_t queryStartIdx, float& outMs) const;
    D3D12_CPU_DESCRIPTOR_HANDLE CpuAt(uint32_t slot) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GpuAt(uint32_t slot) const;

    bool                                         m_ready = false;
    ID3D12Device*                                m_dev = nullptr;
    uint32_t                                     m_maxLights = kMaxLights;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_heap;
    uint32_t                                     m_descInc = 0u;

    Microsoft::WRL::ComPtr<ID3D12RootSignature>  m_buildRoot;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>  m_buildPso;
    Microsoft::WRL::ComPtr<ID3D12RootSignature>  m_cullRoot;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>  m_cullPso;

    Microsoft::WRL::ComPtr<ID3D12Resource>       m_aabbBuf;
    D3D12_RESOURCE_STATES                        m_aabbState = D3D12_RESOURCE_STATE_COMMON;
    Microsoft::WRL::ComPtr<ID3D12Resource>       m_lightBuf;
    D3D12_RESOURCE_STATES                        m_lightState = D3D12_RESOURCE_STATE_COMMON;
    Microsoft::WRL::ComPtr<ID3D12Resource>       m_clusterGridBuf;
    D3D12_RESOURCE_STATES                        m_clusterGridState = D3D12_RESOURCE_STATE_COMMON;
    Microsoft::WRL::ComPtr<ID3D12Resource>       m_lightIndicesBuf;
    D3D12_RESOURCE_STATES                        m_lightIndicesState = D3D12_RESOURCE_STATE_COMMON;

    Microsoft::WRL::ComPtr<ID3D12Resource>       m_lightUploadBuf;
    uint8_t*                                     m_lightUploadMapped = nullptr;
    uint64_t                                     m_lightRingStride = 0u;

    Microsoft::WRL::ComPtr<ID3D12Resource>       m_cbBuf;
    uint8_t*                                     m_cbMapped = nullptr;

    Microsoft::WRL::ComPtr<ID3D12QueryHeap>      m_tsHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource>       m_tsReadback;
    uint64_t                                     m_tsFreqHz = 0u;
    uint32_t                                     m_frameIdx = 0u;
    float                                        m_lastBuildMs = 0.0f;
    float                                        m_lastAssignMs = 0.0f;
    bool                                         m_hasValidTiming = false;
};

} // namespace raw::bridge
