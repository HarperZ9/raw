// tx_subdiv.h: Format-preserving GPU compute 1->4 curved Vlachos PN-Triangle geometry uplift cache
// inside raw::tx. Subdivides static low-poly world meshes on first draw and caches the resulting
// 4x vertex/index buffers in the exact original InputLayout and stride for 0.00 ms steady-state cost.
// Original work, MIT.
#pragma once
#include "tx_common.h"

#include <d3d12.h>
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace raw::tx {

struct LayoutData;
struct TrackedRes;
struct Retire;
class Device;

struct SubdivLayoutInfo {
    bool     eligible = false;
    bool     waterEligible = false; // single-slot rigid geometry with POSITION (no NORMAL required for water quads)
    uint32_t posOffFmt = 0;
    uint32_t normOffFmt = 0;
    uint32_t tangOffFmt = 0;
    uint32_t uvOffFmt = 0;
    uint32_t colOffFmt = 0;
};

struct SubdivKey {
    const TrackedRes* vbTr = nullptr;
    const TrackedRes* ibTr = nullptr;
    uint32_t vbOffset = 0;
    uint32_t ibOffset = 0;
    uint32_t startIndex = 0;
    uint32_t indexCount = 0;
    int32_t  baseVertex = 0;
    uint32_t stride = 0;
    uint32_t ibFmt = 0;
    uint32_t epoch = 0;
    uint32_t isWater = 0;
    uint32_t pad = 0;
    bool operator==(const SubdivKey& o) const {
        return vbTr == o.vbTr && ibTr == o.ibTr && vbOffset == o.vbOffset && ibOffset == o.ibOffset &&
               startIndex == o.startIndex && indexCount == o.indexCount && baseVertex == o.baseVertex &&
               stride == o.stride && ibFmt == o.ibFmt && epoch == o.epoch && isWater == o.isWater;
    }
};
static_assert(sizeof(SubdivKey) == 56, "SubdivKey must have zero implicit padding for HashBytes");

struct SubdivKeyHash {
    size_t operator()(const SubdivKey& k) const {
        return static_cast<size_t>(HashBytes(&k, sizeof(k)));
    }
};

struct SubdivEntry {
    std::shared_ptr<TrackedRes> vbTr;
    std::shared_ptr<TrackedRes> ibTr;
    D3D12_VERTEX_BUFFER_VIEW    vbView{};
    D3D12_INDEX_BUFFER_VIEW     ibView{};
    uint32_t                    outIndexCount = 0;
    uint64_t                    vramBytes = 0;
    uint32_t                    waterMode = 0; // 0=static PN 1->4, 1=water 1->4, 2=water 1->16
    // 2-slot depth-pass history (slot 0 = non-square main screen DSV, slot 1 = square shadow/env DSV).
    // Guarantees shadow-map passes (bDrawLandShadows=1) can NEVER clobber mainDepth and trigger DEPTH_EQUAL holes!
    mutable std::atomic<uint64_t> lastDepthFrame[2]{{0}, {0}};
    mutable std::atomic<const TrackedRes*> lastDepthRes[2]{{nullptr}, {nullptr}};
    mutable std::atomic<uint64_t> lastWaterFrame{0};
};

struct SubdivBuildReq {
    SubdivKey                   key{};
    const SubdivLayoutInfo*     layout = nullptr;
    ID3D12Resource*             inVerts = nullptr;
    ID3D12Resource*             inIdx = nullptr;
    uint64_t                    inVertsBytes = 0;
    uint64_t                    inIdxBytes = 0;
    float                       curvatureGain = 0.62f;
    float                       snowDepthU = 0.0f;
    float                       displaceAmp = 0.0f;
    bool                        isWater = false;
    float                       waterTimeSec = 0.0f;
    float                       waterGravity = 9.80665f;
    float                       waterTideU = 0.0f;
    float                       waterBeaufort = 2.0f;
    float                       waterSwellX = 0.866f;
    float                       waterSwellY = 0.500f;
    uint64_t                    frameSeq = 0;
};

class SubdivCache {
public:
    bool Init(ID3D12Device* dev);
    void Shutdown();
    bool Ready() const { return pso_ != nullptr && root_ != nullptr; }

    SubdivLayoutInfo AnalyzeLayout(const LayoutData& data) const;
    const SubdivLayoutInfo* GetLayoutInfo(uint32_t layoutId, const LayoutData* data);

    // Fast shared-lock lookup for steady-state frames (safe from parallel worker threads).
    std::shared_ptr<const SubdivEntry> Find(const SubdivKey& key) const;

    // Allocates cached 4x (or 16x coarse-water) GPU buffers and records the TxSubdiv compute dispatch + barriers on `cl`.
    // Caller must ensure inVerts and inIdx are in NON_PIXEL_SHADER_RESOURCE state before calling.
    std::shared_ptr<const SubdivEntry> Build(ID3D12Device* dev, ID3D12GraphicsCommandList* cl,
                                             const SubdivBuildReq& req, bool& outDispatched);

    // Re-dispatches live 3D ocean-wave & tidal vertex displacement in place on a cached water SubdivEntry once per frame.
    bool UpdateWater(ID3D12GraphicsCommandList* cl, const SubdivEntry& entry, const SubdivBuildReq& req);

    // Removes any cached entries referencing a destroyed source buffer and moves their TrackedRes into `retire`.
    void InvalidateSource(const TrackedRes* tr, Retire& retire);
    // Retires all cached meshes cleanly (e.g. when live snowpack depth or curvature changes substantially).
    void ClearAll(Retire& retire);

    uint64_t MeshCount() const { return meshCount_.load(std::memory_order_relaxed); }
    uint64_t TotalVramBytes() const { return vramBytes_.load(std::memory_order_relaxed); }

private:
    static constexpr uint64_t kMaxVramBytes = 512ull * 1024ull * 1024ull; // 512 MB VRAM cap (modern 16-32 GB GPUs)
    static constexpr size_t   kMaxMeshes = 6144;

    ComPtr<ID3D12RootSignature> root_;
    ComPtr<ID3D12PipelineState> pso_;

    mutable std::shared_mutex m_;
    std::unordered_map<uint32_t, SubdivLayoutInfo> layoutInfo_;
    std::unordered_map<SubdivKey, std::shared_ptr<SubdivEntry>, SubdivKeyHash> cache_;
    std::atomic<uint64_t> meshCount_{0};
    std::atomic<uint64_t> vramBytes_{0};
};

}  // namespace raw::tx
