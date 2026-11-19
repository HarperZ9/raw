#pragma once
// RtOmm: opacity micromaps (DXR 1.2) for the alpha-tested meshes (docs/RAYTRACING.md).
//
// Without them every ray that crosses a leaf card stops in the ray query's candidate loop, which reads
// the card's coordinates and mask in the shader. With a micromap the hardware already knows, for each of
// a triangle's 256 micro-triangles (level 4), whether it is leaf, air or mixed; only mixed ones come back
// as candidates. Each new alpha-tested mesh gets one micromap array, baked on the GPU from RtAlpha's
// coordinates and mask (RtOmmBake.hlsl) and built in the same frame as its BLAS, which links to it
// triangle for triangle. The micromap order comes from OmmTable.h, read from the GPU by tests/omm_check.
// Needs the Agility SDK and DXR tier 1.2. Original work, MIT.
#include "D3D12Bridge.h"
#include <wrl/client.h>
#include <string>
#include <vector>

namespace raw::bridge
{
class RtOmm
{
public:
    static constexpr uint32_t kLevel = 4, kWordsPerTriangle = (1u << (2 * kLevel)) * 2 / 32;
    static constexpr uint64_t kInput = 96ull << 20, kHeap = 160ull << 20, kScratch = 32ull << 20;
    static constexpr uint32_t kMaxTriangles = 1u << 20;   // the linkage index buffer: 0, 1, 2, ...
    static constexpr uint32_t Descriptors() { return 2; } // the micromap bits and the descriptors, as UAVs

    // base: this pass's descriptors in the shader-visible heap; pool, maskBase: RtAlpha's coordinates
    // and first mask descriptor.
    bool Create(ID3D12Device5* dev, ID3D12DescriptorHeap* heap, uint32_t base, uint32_t pool, uint32_t maskBase, std::string& err);
    bool Ready() const { return m_ready; }
    void Reset();   // the GPU is idle
    // A new alpha-tested mesh: returns its micromap array's address (0 when it does not fit).
    uint64_t Add(uint32_t triangles, uint32_t uvBase, uint32_t mask, uint32_t maskW, uint32_t maskH);
    // The frame's bakes and micromap array builds, after RtAlpha's jobs; true when any ran.
    bool Record(ID3D12GraphicsCommandList4* cl, float alphaRef);
    D3D12_RAYTRACING_GEOMETRY_OMM_LINKAGE_DESC Linkage(uint64_t array) const;
    uint32_t Arrays() const { return m_arrays; }
    uint64_t HeapBytes() const { return m_heapUsed; }

private:
    struct Job { uint32_t triangles, uvBase, mask, maskW, maskH, word, desc; uint64_t array, bytes; };
    Microsoft::WRL::ComPtr<ID3D12Device5> m_dev;
    ID3D12DescriptorHeap* m_heap = nullptr;
    uint32_t m_base = 0, m_pool = 0, m_maskBase = 0, m_inc = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_input, m_descs, m_arrayHeap, m_scratch, m_iota, m_cells;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;
    bool m_ready = false;
    uint32_t m_wordsUsed = 0, m_descsUsed = 0, m_arrays = 0;
    uint64_t m_heapUsed = 0;
    std::vector<Job> m_jobs;
};

} // namespace raw::bridge
