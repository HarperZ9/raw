#pragma once
// RtAlpha: the alpha of the game's alpha-tested draws, for the rays (docs/RAYTRACING.md).
//
// A ray that meets a leaf card has to know whether the texel it crosses is leaf or air. For each
// alpha-tested mesh this keeps two things of its own, made once on the GPU when the mesh is first seen:
//   - its texture coordinates, three per triangle, expanded from the game's vertex and index buffers
//     into one pool (RtAlphaUv.hlsl, main), so the hit needs no game buffer after that frame;
//   - an R8 mask of the alpha of the game's texture at up to 512 x 512, one per game texture
//     (RtAlphaUv.hlsl, mask), so the hit needs no game texture either.
// A record per mesh (its first coordinate, its mask) is found from the instance ID in the ray query.
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
    static constexpr uint32_t kMaxMasks = 2048, kMaxRecords = 40000, kStage = 1200, kMaskSide = 512;
    static constexpr uint64_t kPool = 96ull << 20;
    // Descriptors this owns, from `base` in the shader-visible heap: the pool's UAV, kMaxMasks mask
    // UAVs, then 3 x kStage staging descriptors (one region per frame in flight).
    static constexpr uint32_t Descriptors() { return 1 + kMaxMasks + 3 * kStage; }

    ~RtAlpha();
    bool Create(ID3D12Device5* dev, ID3D12DescriptorHeap* heap, uint32_t base, std::string& err);
    void Reset();   // the GPU is idle: masks and coordinates are dropped and made again as meshes return
    // A new alpha-tested mesh. Returns its record index + 1 for the instance ID, or 0 when it cannot
    // join (no texture on the layer, the pool or the mask table full).
    uint32_t AddMesh(const RtDraw& d, ID3D12Resource* v12, ID3D12Resource* i12, uint64_t vStart, uint64_t iStart,
                     uint32_t triangles, std::vector<ID3D11Resource*>& toHand, ID3D12Resource* (*native)(ID3D11Resource*));
    // Records this frame's jobs into cl (before the BLAS builds); true when any ran (a UAV barrier must follow).
    bool Record(ID3D12GraphicsCommandList4* cl, uint32_t slot);
    bool Room() const { return m_uv.size() + m_maskJobs.size() + 2 <= kStage / 3; }   // this frame's staging has space for one more mesh
    D3D12_GPU_VIRTUAL_ADDRESS RecordsAddress() const { return m_records ? m_records->GetGPUVirtualAddress() : 0; }
    uint32_t Masks() const { return uint32_t(m_masks.size()); }
    uint32_t Meshes() const { return m_recordCount; }
    uint32_t NoMask() const { return m_noMask; }
    uint64_t PoolBytes() const { return m_poolUsed; }
    // A record's first coordinate, mask index, mask width and height (record = AddMesh's result - 1).
    void Info(uint32_t record, uint32_t out[4]) const { for (int k = 0; k < 4; ++k) out[k] = m_rec[record * 4 + k]; }

private:
    struct Mask { ID3D11Resource* src = nullptr; Microsoft::WRL::ComPtr<ID3D12Resource> tex; uint32_t w = 0, h = 0; };
    struct UvJob { ID3D12Resource* v12; ID3D12Resource* i12; uint64_t vbBytes, ibBytes; uint32_t c[8]; };
    struct MaskJob { ID3D12Resource* src12; D3D12_SHADER_RESOURCE_VIEW_DESC srv; uint32_t mask, w, h; };

    uint32_t MaskFor(ID3D11ShaderResourceView* v, std::vector<ID3D11Resource*>& toHand, ID3D12Resource* (*native)(ID3D11Resource*));

    Microsoft::WRL::ComPtr<ID3D12Device5> m_dev;
    ID3D12DescriptorHeap* m_heap = nullptr;
    uint32_t m_base = 0, m_inc = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_pool, m_records;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_uvPso, m_maskPso;
    uint32_t* m_rec = nullptr;   // mapped: (first coordinate, mask, mask width, mask height) per record
    uint32_t m_recordCount = 0, m_noMask = 0;
    uint64_t m_poolUsed = 0;
    std::vector<Mask> m_masks;
    std::unordered_map<ID3D11Resource*, uint32_t> m_maskOf;
    std::vector<UvJob> m_uv;
    std::vector<MaskJob> m_maskJobs;
};

} // namespace raw::bridge
