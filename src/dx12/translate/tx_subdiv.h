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
    uint32_t layoutId = 0;
    uint32_t stride = 0;
    uint32_t ibFmt = 0;
    bool operator==(const SubdivKey& o) const {
        return vbTr == o.vbTr && ibTr == o.ibTr && vbOffset == o.vbOffset && ibOffset == o.ibOffset &&
               startIndex == o.startIndex && indexCount == o.indexCount && baseVertex == o.baseVertex &&
               layoutId == o.layoutId && stride == o.stride && ibFmt == o.ibFmt;
    }
};

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
};

struct SubdivBuildReq {
    SubdivKey                   key{};
    const SubdivLayoutInfo*     layout = nullptr;
    ID3D12Resource*             inVerts = nullptr;
    ID3D12Resource*             inIdx = nullptr;
    uint64_t                    inVertsBytes = 0;
    uint64_t                    inIdxBytes = 0;
    float                       curvatureGain = 0.62f;
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

    // Allocates cached 4x GPU buffers and records the TxSubdiv compute dispatch + UAV->VB/IB barriers on `cl`.
    // Caller must ensure inVerts and inIdx are in NON_PIXEL_SHADER_RESOURCE state before calling.
    std::shared_ptr<const SubdivEntry> Build(ID3D12Device* dev, ID3D12GraphicsCommandList* cl,
                                             const SubdivBuildReq& req, bool& outDispatched);

    // Removes any cached entries referencing a destroyed source buffer and moves their TrackedRes into `retire`.
    void InvalidateSource(const TrackedRes* tr, Retire& retire);

    uint64_t MeshCount() const { return meshCount_.load(std::memory_order_relaxed); }
    uint64_t TotalVramBytes() const { return vramBytes_.load(std::memory_order_relaxed); }

private:
    static constexpr uint64_t kMaxVramBytes = 256ull * 1024ull * 1024ull; // 256 MB VRAM cap
    static constexpr size_t   kMaxMeshes = 2048;

    ComPtr<ID3D12RootSignature> root_;
    ComPtr<ID3D12PipelineState> pso_;

    mutable std::shared_mutex m_;
    std::unordered_map<uint32_t, SubdivLayoutInfo> layoutInfo_;
    std::unordered_map<SubdivKey, std::shared_ptr<SubdivEntry>, SubdivKeyHash> cache_;
    std::atomic<uint64_t> meshCount_{0};
    std::atomic<uint64_t> vramBytes_{0};
};

}  // namespace raw::tx
