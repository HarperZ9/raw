#pragma once
// RtScene: the game's own geometry as a ray-tracing scene, on raw::tx's device (docs/RAYTRACING.md).
//
// The game runs on the translation layer, so its vertex and index buffers are D3D12 buffers. Each
// rigid opaque lit draw of the frame arrives as an RtDraw. A (buffer, range) seen for the first
// time gets a BLAS built straight from the game's buffers (positions as the draw's input layout
// gives them: half or float); every frame a TLAS places the BLASes with the draws' world matrices
// (camera-relative, as the game's vertex shader uses them), and RtShadow.hlsl traces the sun and
// the three strongest point lights of each pixel with inline ray queries. Output: two RGBA8
// textures the lighting pass reads (visibility; which lights). Original work, MIT.
#include "D3D12Bridge.h"
#include "RtAlpha.h"
#include "RtSky.h"
#include <wrl/client.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace raw::bridge
{
using Microsoft::WRL::ComPtr;

class RtScene
{
public:
    ~RtScene();
    bool Create(ID3D12Device* dev, UINT width, UINT height, LogFn log, std::string& err);
    void Tune(const RtTuning& t) { m_tune = t; }
    // The frame's draws (the buffers carry a reference, released here or when evicted). Returns the
    // game buffers a new BLAS reads this frame; the bridge hands them to the queue in COMMON.
    void SetDraws(std::vector<RtDraw>&& draws, std::vector<ID3D11Resource*>& toHand,
                  ID3D12Resource* (*native)(ID3D11Resource*));
    // Records the builds and the trace; uniform: the lighting pass's cbuffer P for this frame.
    bool Record(const void* uniform, size_t bytes, ID3D12Resource* depth, ID3D12Resource* normal, ID3D12Resource* spec,
                ID3D12Resource* motion);
    bool Submit(ID3D12CommandQueue* q);
    bool Active() const { return m_tune.enabled && m_ready; }
    ID3D12Resource* Visibility() const { return m_vis.Get(); }
    ID3D12Resource* Lights() const { return m_lights.Get(); }
    RtFrameStats Stats();   // the newest retired frame's GPU times and this frame's counts

private:
    struct Mesh
    {
        ID3D11Buffer* vb = nullptr;
        ID3D11Buffer* ib = nullptr;
        D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC tri{};
        uint64_t blas = 0, bytes = 0, seen = 0;
        uint32_t alphaRecord = 0;   // RtAlpha record + 1; 0: opaque
        bool built = false, failed = false;
    };
    struct Key { uintptr_t vb, ib; uint64_t a, b; bool operator==(const Key& o) const { return vb == o.vb && ib == o.ib && a == o.a && b == o.b; } };
    struct KeyHash { size_t operator()(const Key& k) const { return size_t(k.vb * 0x9E3779B97F4A7C15ull ^ k.ib * 0xC2B2AE3D27D4EB4Full ^ k.a * 31 ^ k.b); } };

    bool Buffer(ComPtr<ID3D12Resource>& r, uint64_t bytes, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES s, bool uav, const wchar_t* name);
    bool Texture(ComPtr<ID3D12Resource>& r, const wchar_t* name);
    bool Pipeline(std::string& err);
    void Evict();

    RtTuning m_tune;
    LogFn m_log = nullptr;
    bool m_ready = false;
    UINT m_w = 0, m_h = 0;
    uint64_t m_frame = 0, m_freq = 0;
    ComPtr<ID3D12Device5> m_dev;
    ComPtr<ID3D12Resource> m_blasHeap, m_scratch, m_tlas, m_tlasScratch, m_inst[3], m_cb[3], m_vis, m_lights, m_tsRead;
    ComPtr<ID3D12QueryHeap> m_ts;
    ComPtr<ID3D12CommandAllocator> m_alloc[3];
    ComPtr<ID3D12GraphicsCommandList4> m_list;
    ComPtr<ID3D12RootSignature> m_root;
    ComPtr<ID3D12PipelineState> m_pso;
    ComPtr<ID3D12DescriptorHeap> m_heap;
    ComPtr<ID3D12Fence> m_fence;
    uint64_t m_fenceValue = 0, m_slotValue[3] = {};
    bool m_recorded = false;
    std::unordered_map<Key, uint32_t, KeyHash> m_meshOf;
    std::vector<Mesh> m_meshes;
    std::vector<uint32_t> m_build;    // meshes whose BLAS this frame builds
    std::vector<RtDraw> m_draws;
    uint64_t m_blasUsed = 0;
    RtFrameStats m_stats;
    RtAlpha m_alpha;
    RtSky m_sky;
    bool m_skyReady = false;
};

} // namespace raw::bridge
