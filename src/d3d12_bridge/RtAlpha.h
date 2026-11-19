#pragma once
// RtAlpha: what the rays know of the game's materials (docs/RAYTRACING.md).
//
// A ray that meets a leaf card has to know whether the texel it crosses is leaf or air, and a ray that
// lands off screen has to know what colour the surface is and which way it faces. For each mesh this
// keeps, made once on the GPU when the mesh is first seen:
//   - its texture coordinates, three per triangle, expanded from the game's vertex and index buffers
//     into one pool (RtAlphaUv.hlsl, main), so the hit needs no game buffer after that frame;
//   - its face normals, one per triangle in object space, from the same pass (a parallel pool);
//   - for alpha-tested meshes, an R8 mask of the alpha of the game's texture at up to 512 x 512, one
//     per game texture (RtAlphaUv.hlsl, mask);
//   - for every mesh with a colour texture, an RGBA8 copy of it at up to matSide (RtAlphaUv.hlsl,
//     albedo), one per game texture, read by the off-screen hit shading.
// A record per mesh is found from the instance ID in the ray query: Records (first coordinate, mask,
// mask width, height) and Materials (colour copy + 1 or 0, first face normal, flags, 0).
// Original work, MIT.
#include "D3D12Bridge.h"
#include <wrl/client.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace raw::bridge
{
class RtAlpha
{
public:
    static constexpr uint32_t kMaxMasks = 2048, kMaxAlbedo = 4096, kMaxRecords = 40000, kStage = 1200, kMaskSide = 512;
    static constexpr uint64_t kPool = 128ull << 20;
    // Descriptors this owns, from `base` in the shader-visible heap: the pool's UAV, kMaxMasks mask
    // UAVs, 3 x kStage staging descriptors (one region per frame in flight), kMaxAlbedo colour-copy
    // UAVs, then the normal pool's UAV.
    static constexpr uint32_t Descriptors() { return 1 + kMaxMasks + 3 * kStage + kMaxAlbedo + 1; }
    static constexpr uint32_t AlbedoOffset() { return 1 + kMaxMasks + 3 * kStage; }
    static constexpr uint32_t NormalOffset() { return AlbedoOffset() + kMaxAlbedo; }

    ~RtAlpha();
    bool Create(ID3D12Device5* dev, ID3D12DescriptorHeap* heap, uint32_t base, std::string& err);
    void Reset();   // the GPU is idle: masks, copies and coordinates are dropped and made again as meshes return
    void SetAlbedoSide(uint32_t side) { m_albedoSide = side < 16 ? 16 : side > 512 ? 512 : side; }
    // A new mesh. Alpha-tested: needs its mask (and takes a colour copy where it can). Opaque: needs
    // its colour texture. Returns its record index + 1 for the instance ID, or 0 when it cannot join.
    uint32_t AddMesh(const RtDraw& d, ID3D12Resource* v12, ID3D12Resource* i12, uint64_t vStart, uint64_t iStart,
                     uint32_t triangles, bool alphaTested, std::vector<ID3D11Resource*>& toHand,
                     ID3D12Resource* (*native)(ID3D11Resource*));
    // Records this frame's jobs into cl (before the BLAS builds); true when any ran (a UAV barrier must follow).
    bool Record(ID3D12GraphicsCommandList4* cl, uint32_t slot);
    bool Room() const { return m_uv.size() + m_texJobs.size() + 3 <= kStage / 3; }   // this frame's staging has space for one more mesh
    D3D12_GPU_VIRTUAL_ADDRESS RecordsAddress() const { return m_records ? m_records->GetGPUVirtualAddress() : 0; }
    D3D12_GPU_VIRTUAL_ADDRESS MaterialsAddress() const { return m_mats ? m_mats->GetGPUVirtualAddress() : 0; }
    uint32_t Masks() const { return uint32_t(m_tex[0].size()); }
    uint32_t Albedos() const { return uint32_t(m_tex[1].size()); }
    uint64_t AlbedoBytes() const { return m_albedoBytes; }
    uint32_t Meshes() const { return m_recordCount; }
    uint32_t AlphaMeshes() const { return m_alphaRecords; }
    uint32_t NoMask() const { return m_noMask; }
    uint32_t NoAlbedo() const { return m_noAlbedo; }
    uint64_t PoolBytes() const { return m_poolUsed; }
    ID3D12Resource* Pool() const { return m_pool.Get(); }   // the coordinates (RtWater binds its own view of it)
    // A record's first coordinate, mask index, mask width and height (record = AddMesh's result - 1).
    void Info(uint32_t record, uint32_t out[4]) const { for (int k = 0; k < 4; ++k) out[k] = m_rec[record * 4 + k]; }

private:
    struct Tex { ID3D11Resource* src = nullptr; Microsoft::WRL::ComPtr<ID3D12Resource> tex; uint32_t w = 0, h = 0; };
    struct UvJob { ID3D12Resource* v12; ID3D12Resource* i12; uint64_t vbBytes, ibBytes; uint32_t c[12]; };
    struct TexJob { ID3D12Resource* src12; D3D12_SHADER_RESOURCE_VIEW_DESC srv; uint32_t index, w, h; bool albedo; };

    // The mask (albedo false) or colour copy (true) of the texture behind v: its index, or ~0u.
    uint32_t TexFor(ID3D11ShaderResourceView* v, bool albedo, std::vector<ID3D11Resource*>& toHand,
                    ID3D12Resource* (*native)(ID3D11Resource*));
    bool Pipelines(std::string& err);

    Microsoft::WRL::ComPtr<ID3D12Device5> m_dev;
    ID3D12DescriptorHeap* m_heap = nullptr;
    uint32_t m_base = 0, m_inc = 0, m_albedoSide = 128;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_pool, m_normals, m_records, m_mats;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_uvPso, m_maskPso, m_albedoPso;
    uint32_t* m_rec = nullptr;   // mapped: (first coordinate, mask, mask width, mask height) per record
    uint32_t* m_mat = nullptr;   // mapped: (colour copy + 1 or 0, first face normal, flags, 0) per record
    uint32_t m_recordCount = 0, m_alphaRecords = 0, m_noMask = 0, m_noAlbedo = 0;
    uint64_t m_poolUsed = 0, m_albedoBytes = 0;
    std::vector<Tex> m_tex[2];                               // masks, colour copies
    std::unordered_map<ID3D11Resource*, uint32_t> m_texOf[2];
    std::vector<UvJob> m_uv;
    std::vector<TexJob> m_texJobs;                           // masks and colour copies, made this frame
};

} // namespace raw::bridge
