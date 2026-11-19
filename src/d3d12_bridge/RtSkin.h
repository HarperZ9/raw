#pragma once
// RtSkin: skinned actors in the ray-tracing scene (docs/RAYTRACING.md, "Skinned actors"). The game skins
// people, creatures and their clothes in the vertex shader, so their vertex buffers hold the bind pose.
// Each frame every skinned draw of the opaque set (SceneGeometry.cpp) brings its position stream, its
// blend stream (indices R8G8B8A8_UNORM, weights R16G16B16A16_FLOAT) and a copy of its bone palette
// (240 float4, the vertex constant buffer at RtSkinBones). RtSkin.hlsl skins the positions into a
// per-frame pool, a BLAS is built from the pool and the game's index buffer (fast build), and the TLAS
// places it with the identity. The bones give world positions (the first run, which took them as
// camera-relative, found every actor missing from the primary-ray view); the camera's place is taken off. Three frame
// slots, so a slot's pool and BLAS memory are reused only after the GPU is done with them.
// Original work, MIT.
#include "D3D12Bridge.h"
#include <wrl/client.h>
#include <string>
#include <vector>

namespace raw::bridge
{
class RtSkin
{
public:
    static constexpr uint32_t kMaxDraws = 512, kMaxVertices = 65536, kStatWords = 52, kPerDraw = 6;
    static constexpr uint64_t kPool = 32ull << 20, kBlas = 96ull << 20, kScratch = 32ull << 20, kBones = kMaxDraws * 3840ull;
    static constexpr uint64_t kIdx = 16ull << 20, kStats = kMaxDraws * kStatWords * 4ull;
    // per slot, per draw: two streams, the pool, the index pool, the stats and the game's index buffer
    static constexpr uint32_t Descriptors() { return 3 * kMaxDraws * kPerDraw + 3; }   // + the depth view per slot
    bool Create(ID3D12Device5* dev, ID3D12DescriptorHeap* heap, uint32_t base, std::string& err);
    // This frame's skinned draws (taken from the frame's draws; the buffers are handed to the queue).
    void SetDraws(std::vector<RtDraw>& draws, std::vector<ID3D11Resource*>& toHand, ID3D12Resource* (*native)(ID3D11Resource*));
    // The skinning and the builds, before the frame's other BLAS builds (a UAV barrier follows them).
    // depth: the game's depth (R32), uniform: the lighting pass's (its dims, proj and worldToClip), for the floating check.
    bool Record(ID3D12GraphicsCommandList4* cl, uint32_t slot, ID3D12Resource* depth, const void* uniform, size_t bytes);
    // The frame's BLASes as instances (identity, opaque, instance ID 0); returns how many were written.
    uint32_t Place(D3D12_RAYTRACING_INSTANCE_DESC* out, uint32_t room, uint32_t slot, uint64_t& tris) const;
    void SetCamera(const float pos[3]) { for (int k = 0; k < 3; ++k) m_cam[k] = pos[k]; }
    // RtSkinCheck.hlsl: stretched triangles made degenerate (filter), and the counts logged every logEvery frames.
    void SetCheck(int filter, float ratio, float minUnits, uint32_t logEvery, void (*log)(const char*, ...))
    {
        m_filter = filter; m_ratio = ratio; m_minUnits = minUnits; m_logEvery = logEvery; m_log = log;
    }
    uint64_t Stretched() const { return m_stretched; }
    uint64_t Floating() const { return m_floating; }
    uint32_t Draws() const { return uint32_t(m_jobs.size()); }
    uint32_t Built() const { return m_built; }
    uint64_t Vertices() const { return m_vertices; }

private:
    struct Job {
        ID3D12Resource* vb; ID3D12Resource* bv; ID3D12Resource* ib;
        uint64_t vbBytes, bvBytes, iStart; uint32_t c[16];
        DXGI_FORMAT ibFormat; uint32_t indexCount, idxFirst, posFmt, ibBytes;
        bool actor;
        uint64_t blas = 0; bool built = false;
        std::vector<uint8_t> diagVb, diagBv;   // SkinVbCheck: the streams as the game's draw saw them
        std::vector<float> diagBones;
        uint64_t psHash = 0; uint32_t blendInfo = 0;
    };
    struct Meta { uint32_t indexCount, count, stride, posFmt; bool actor; std::vector<uint8_t> diagVb, diagBv; std::vector<float> diagBones;
                  uint32_t posOffset = 0, bStride = 0, idxOffset = 0, wOffset = 0; uint64_t psHash = 0; uint32_t blendInfo = 0; };
    void ReadStats(uint32_t slot);
    int m_filter = 1;   // 0 none, 1 stretched and floating, 2 stretched only
    float m_ratio = 4.0f, m_minUnits = 24.0f;
    uint32_t m_logEvery = 0, m_checkFrames = 0;
    uint64_t m_stretched = 0, m_floating = 0;
    void (*m_log)(const char*, ...) = nullptr;
    std::vector<Meta> m_meta[3];
    Microsoft::WRL::ComPtr<ID3D12Resource> m_idx[3], m_stats[3], m_statsRead[3], m_zero;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_checkRoot;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_checkPso;
    Microsoft::WRL::ComPtr<ID3D12Device5> m_dev;
    ID3D12DescriptorHeap* m_heap = nullptr;
    uint32_t m_base = 0, m_inc = 0, m_built = 0;
    uint64_t m_vertices = 0;
    float m_cam[3] = {};
    Microsoft::WRL::ComPtr<ID3D12Resource> m_pool[3], m_blas[3], m_scratch, m_bones[3];
    float* m_bonesMapped[3] = {};
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;
    std::vector<Job> m_jobs;
    std::vector<float> m_boneData;
};

} // namespace raw::bridge
