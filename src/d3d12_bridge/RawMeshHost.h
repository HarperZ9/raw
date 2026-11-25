#pragma once
// RawMeshHost.h: Live Hardware DX12 Amplification + Mesh Shader (as_6_6 / ms_6_6) & Multi-Biome
// Procedural Terrain/Prop Renderer (`RawTerrainWorld.hlsl`, AS+MS+PS with VS+PS fallback).
// Original work, MIT.
#include "RawAssetBridge.h"
#include "../worldgen/RawMeshPipeline.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace raw::bridge
{

struct RawMeshGpuAsset {
    std::string                            name;
    uint32_t                               meshletCount = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> meshletBuf;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuf;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuf;
};

// Native lossless GPU buffers for RawTerrainWorld.hlsl (48B MeshletDesc + 80B TerrainVertex + uint32 indices).
struct TerrainGpuChunk {
    std::string                            name;
    uint32_t                               meshletCount = 0;
    uint32_t                               indexCount   = 0;
    float                                  boundSphere[4] = {}; // xyz absolute world center, w radius
    Microsoft::WRL::ComPtr<ID3D12Resource> meshletBuf;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuf;
    Microsoft::WRL::ComPtr<ID3D12Resource> meshletIdxBuf;
    Microsoft::WRL::ComPtr<ID3D12Resource> flatIdxBuf;
    D3D12_INDEX_BUFFER_VIEW                flatIbv{};
};

struct alignas(256) RawMeshFrameCB {
    float    worldToClip[16];
    float    frustumPlanes[6][4];
    float    camPosAndNear[4];   // xyz = camera pos, w = PN-Triangle subdivision distance threshold
    uint32_t meshletCount;
    float    curvatureGain;      // Vlachos cubic Bezier normal projection gain (default 0.72)
    float    displaceAmp;        // world-space micro-displacement scale
    float    snowDepthU;         // live 3D volumetric snowpack buildup thickness (0..48 world units)
    float    windAndTime[4];     // xy = synoptic wind, z = timeSec, w = foliageWindMask (0=rigid, 1=vegetation)
};

struct alignas(256) TerrainDrawCB {
    float    frustumPlanes[6][4];     // camera-relative world frustum planes
    float    chunkOriginAndScale[4];  // xyz world offset (0 for world-baked verts), w tri-planar detail scale
    uint32_t meshletCount;
    float    lodMorphFactor;
    float    magmaFlowPhase;
    float    fogHeightFalloff;
};

struct alignas(256) RawSubdivCB {
    uint32_t inTriangleCount  = 0;
    float    curvatureGain    = 0.72f;
    float    displaceAmp      = 0.35f;
    float    snowDepthU       = 0.0f;
    float    snowAvalancheCos = 0.42f;
    float    camPosW[3]       = {};
};

class RawMeshHost {
public:
    RawMeshHost() = default;
    ~RawMeshHost();
    RawMeshHost(const RawMeshHost&) = delete;
    RawMeshHost& operator=(const RawMeshHost&) = delete;

    bool Init(ID3D12Device* dev, std::string* outErr = nullptr);
    void Shutdown();

    bool Ready() const { return m_ready; }
    bool SubdivReady() const { return m_subdivPso != nullptr; }
    bool TerrainReady() const { return m_terrainReady; }
    bool TerrainUsesMeshShaders() const { return m_terrainMsPso != nullptr; }
    bool PbrAtlasReady() const { return m_pbrAtlasUploaded; }
    uint32_t PbrAtlasSliceCount() const { return kPbrAtlasSlices; }
    uint32_t PbrAtlasResolution() const { return kPbrAtlasRes; }
    uint32_t PbrAtlasMipLevels() const { return kPbrAtlasMips; }

    bool UploadAsset(const RawMeshAsset& src, RawMeshGpuAsset* outGpu, std::string* outErr = nullptr);
    bool UploadTerrainChunk(const raw::worldgen::ProceduralMesh& src, TerrainGpuChunk* outGpu, std::string* outErr = nullptr);
    void DeferRelease(TerrainGpuChunk&& chunk);

    void Dispatch(ID3D12GraphicsCommandList* cmd, const RawMeshGpuAsset& asset, const RawMeshFrameCB& cb);
    // Dispatches RawSubdiv.hlsl (cs_6_6) 1->4 curved PN-Triangle GPU subdivision into UAV output vertex/index buffers.
    void SubdivideMesh(ID3D12GraphicsCommandList* cmd,
                       D3D12_GPU_VIRTUAL_ADDRESS inVertsSrv, D3D12_GPU_VIRTUAL_ADDRESS inIdxSrv,
                       D3D12_GPU_VIRTUAL_ADDRESS outVertsUav, D3D12_GPU_VIRTUAL_ADDRESS outIdxUav,
                       const RawSubdivCB& cb);

    // Records and submits live RawTerrainWorld.hlsl pass onto `colorOut` (RGBA16F, COMMON->RT->COMMON)
    // occluded against Skyrim's standard-Z `depthIn` (R32F, COMMON->PS_SRV->COMMON) + hardware D32_FLOAT DSV.
    bool ExecuteTerrainPass(ID3D12CommandQueue* queue,
                            ID3D12Resource* colorOut,
                            ID3D12Resource* depthIn,
                            ID3D12Resource* shadowAtlas,
                            UINT width, UINT height,
                            const void* lightUniformData, size_t lightUniformBytes,
                            const float worldToClip[16],
                            const float camPos[3],
                            float timeSec,
                            const std::unordered_map<uint64_t, TerrainGpuChunk>& chunks,
                            uint32_t* outDrawnChunks = nullptr,
                            uint32_t* outDrawnMeshlets = nullptr);

private:
    static constexpr uint32_t kTerrainRing = 3u;
    static constexpr uint32_t kMaxTerrainDrawsPerFrame = 256u;
    static constexpr uint32_t kPbrAtlasSlices = 8u;
    static constexpr uint32_t kPbrAtlasRes = 128u;
    static constexpr uint32_t kPbrAtlasMips = 8u; // 128, 64, 32, 16, 8, 4, 2, 1
    static constexpr uint32_t kPbrAtlasSubresources = kPbrAtlasSlices * kPbrAtlasMips;
    static constexpr uint32_t kPbrSrvBase = kTerrainRing * 2u;

    bool EnsureTerrainDepth(UINT width, UINT height);
    bool CreatePbrAtlasResources();
    void RecordPbrAtlasUpload(ID3D12GraphicsCommandList* cmd);
    void CollectRetiredChunks();

    bool                                        m_ready = false;
    bool                                        m_terrainReady = false;
    Microsoft::WRL::ComPtr<ID3D12Device>        m_dev;
    Microsoft::WRL::ComPtr<ID3D12Device2>       m_dev2;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;
    Microsoft::WRL::ComPtr<ID3D12Resource>      m_cbBuf;
    void*                                       m_cbMapped = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_subdivRoot;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_subdivPso;
    Microsoft::WRL::ComPtr<ID3D12Resource>      m_subdivCbBuf;
    void*                                       m_subdivCbMapped = nullptr;

    // Live RawTerrainWorld.hlsl graphics pipeline (AS+MS+PS primary, VS+PS fallback)
    Microsoft::WRL::ComPtr<ID3D12RootSignature>        m_terrainRoot;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>        m_terrainMsPso;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>        m_terrainVsPso;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator>     m_terrainAlloc[kTerrainRing];
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>  m_terrainList;
    Microsoft::WRL::ComPtr<ID3D12Fence>                m_terrainFence;
    HANDLE                                             m_terrainEvent = nullptr;
    uint64_t                                           m_terrainFenceVal = 0;
    uint64_t                                           m_terrainSlotVal[kTerrainRing] = {};
    uint32_t                                           m_terrainSlot = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource>             m_terrainLightCb[kTerrainRing];
    void*                                              m_terrainLightCbMap[kTerrainRing] = {};
    Microsoft::WRL::ComPtr<ID3D12Resource>             m_terrainDrawCb[kTerrainRing];
    void*                                              m_terrainDrawCbMap[kTerrainRing] = {};

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>       m_terrainRtvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>       m_terrainDsvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>       m_terrainSrvHeap;
    UINT                                               m_srvStep = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource>             m_terrainDepth;
    UINT                                               m_depthW = 0;
    UINT                                               m_depthH = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource>             m_dummyAtlas;

    // Wave 47: GPU Procedural PBR Texture2DArray Atlas (8 RawTextureForge materials x 8 mips, 128x128)
    Microsoft::WRL::ComPtr<ID3D12Resource>             m_pbrAlbedoArray;
    Microsoft::WRL::ComPtr<ID3D12Resource>             m_pbrNormalArray;
    Microsoft::WRL::ComPtr<ID3D12Resource>             m_pbrOrmhArray;
    Microsoft::WRL::ComPtr<ID3D12Resource>             m_pbrUploadAlbedo;
    Microsoft::WRL::ComPtr<ID3D12Resource>             m_pbrUploadNormal;
    Microsoft::WRL::ComPtr<ID3D12Resource>             m_pbrUploadOrmh;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT                 m_pbrFootprints[kPbrAtlasSubresources] = {};
    bool                                               m_pbrAtlasPendingUpload = false;
    bool                                               m_pbrAtlasUploaded = false;
    uint64_t                                           m_pbrUploadDoneFence = 0;

    std::vector<std::pair<uint64_t, TerrainGpuChunk>>  m_retiredChunks;
};

} // namespace raw::bridge
