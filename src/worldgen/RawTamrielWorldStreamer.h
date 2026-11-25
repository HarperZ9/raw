#pragma once
// RawTamrielWorldStreamer.h: Multi-Cell Streaming Tamriel World Engine (`raw::worldgen`).
// Clean-room MIT C++23 runtime streaming all 11,186 exterior cells of Tamriel natively from
// `Skyrim.esm` + `Skyrim - Meshes0.bsa` (+ auto-discovered sibling `Skyrim - Meshes1.bsa`) + `.NIF`:
//   - Persistent single-open `EsmWorldIndex` + `EsmModelIndex` + `RawBsaArchive` handles
//   - Shared LRU-cached decoded `.NIF` model pool (decodes each unique `.nif` once across all cells!)
//   - Concentric `(cellX, cellY)` cell-ring streaming with bounded `maxCellsPerUpdate`
//   - Strict LRU eviction under configurable `vramBudgetBytes` & `modelCacheBudgetBytes`
//   - Zero Bethesda executable / D3D12 / Windows header dependencies.
// Original work, MIT.

#include "RawBsaArchive.h"
#include "RawEsmModelResolver.h"
#include "RawEsmWorldLoader.h"
#include "RawWorldShowcase.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace raw::worldgen {

struct TamrielStreamConfig {
    uint32_t targetWorldFormId       = kTamrielWorldFormId;          // 0x0000003C = Tamriel
    int32_t  radiusCells             = 1;                            // 1 = 3x3 ring (9 cells), 2 = 5x5 (25 cells)
    uint32_t maxCellsPerUpdate       = 4u;                           // Smooth per-frame streaming budget
    uint32_t maxUniqueModelsPerCell  = 32u;                          // Cap decoded unique NIFs per cell
    uint32_t maxMeshletTris          = 126u;                         // DX12 AS+MS hardware limit (<=126)
    uint64_t vramBudgetBytes         = 128ull * 1024ull * 1024ull;   // Strict resident cell GPU VRAM cap
    uint64_t modelCacheBudgetBytes   = 64ull * 1024ull * 1024ull;    // Strict decoded NIF LRU cache cap
    bool     bakeCellProps           = true;                         // Bake 6-DOF NIF props into merged world meshlets
};

struct StreamedTamrielCell {
    ChunkKey                               key{};
    uint32_t                               cellFormId           = 0u;
    std::string                            cellEdid;
    float                                  waterHeightZ         = -14000.0f;
    uint64_t                               lastTouchedFrame     = 0ull;
    uint64_t                               estimatedVramBytes   = 0ull;
    uint32_t                               totalCellRefrs       = 0u;
    uint32_t                               resolvedPropInstances = 0u;
    TerrainChunkData                       terrainChunk{};
    ProceduralMesh                         terrainMesh{};
    ProceduralMesh                         propsMesh{}; // World-space baked 64v/126t prop meshlets (when non-empty)
    uint64_t                               gpuHandle            = 0ull;
};

class ITamrielChunkSink {
public:
    virtual ~ITamrielChunkSink() = default;
    virtual uint64_t OnTamrielCellLoaded(const StreamedTamrielCell& cell) = 0;
    virtual void     OnTamrielCellEvicted(ChunkKey key, uint64_t gpuHandle) = 0;
};

struct TamrielStreamStats {
    int32_t  camCellX             = 0;
    int32_t  camCellY             = 0;
    uint32_t activeCells          = 0u;
    uint32_t cachedNifModels      = 0u;
    uint32_t totalTerrainMeshlets = 0u;
    uint32_t totalPropMeshlets    = 0u;
    uint32_t totalPropInstances   = 0u;
    uint64_t usedVramBytes        = 0ull;
    uint64_t vramBudgetBytes      = 0ull;
    uint64_t usedModelCacheBytes  = 0ull;
    uint32_t loadedThisStep       = 0u;
    uint32_t evictedThisStep      = 0u;
    uint64_t cacheHits            = 0ull;
    uint64_t cacheMisses          = 0ull;
    uint32_t indexedWorldCells    = 0u;
    uint32_t indexedModels        = 0u;
};

class RawTamrielWorldStreamer {
public:
    bool Init(const std::string& esmPath, const std::string& meshesBsaPath,
              const TamrielStreamConfig& cfg = {}, std::string* outErr = nullptr);
    void Reset(ITamrielChunkSink* sink = nullptr);
    void Shutdown(ITamrielChunkSink* sink = nullptr);

    bool IsInitialized() const noexcept { return initialized_; }

    // Advances concentric Tamriel streaming for `camWorldPos` (Skyrim world Z-up, 4096u/cell).
    TamrielStreamStats Update(Vec3 camWorldPos, ITamrielChunkSink* sink = nullptr);

    // Drains all desired cells in the current ring around `camWorldPos` (convenience for pre-warming/tests).
    TamrielStreamStats StreamRingComplete(Vec3 camWorldPos, ITamrielChunkSink* sink = nullptr);

    const StreamedTamrielCell* FindCell(int32_t cellX, int32_t cellY) const;
    const std::unordered_map<ChunkKey, StreamedTamrielCell, ChunkKeyHash>& ResidentCells() const {
        return cells_;
    }

    const TamrielStreamConfig& Config() const noexcept { return cfg_; }
    const EsmWorldIndex&       WorldIndex() const noexcept { return worldIndex_; }
    const EsmModelIndex&       ModelIndex() const noexcept { return modelIndex_; }
    TamrielStreamStats         GetLastStats() const noexcept { return lastStats_; }

private:
    struct CachedNifModel {
        uint32_t       baseFormId        = 0u;
        std::string    recordType;
        std::string    editorId;
        std::string    bsaVirtualPath;
        std::string    primaryDiffuseDds;
        uint32_t       nifShapeCount     = 0u;
        uint64_t       estimatedBytes    = 0ull;
        uint64_t       lastUsedFrame     = 0ull;
        ProceduralMesh mesh{};
    };

    const CachedNifModel* AcquireCachedNifModel(const EsmModelRecord& mr);
    void                  EvictLruModelsToBudget(uint32_t protectedFormId);
    uint32_t              EvictLruCellsToBudget(ChunkKey protectedKey, ITamrielChunkSink* sink);
    bool                  BuildStreamedCell(int32_t cellX, int32_t cellY, StreamedTamrielCell* outCell);

    bool                                                           initialized_         = false;
    TamrielStreamConfig                                            cfg_{};
    EsmWorldIndex                                                  worldIndex_{};
    EsmModelIndex                                                  modelIndex_{};
    RawBsaArchive                                                  primaryBsa_{};
    RawBsaArchive                                                  siblingBsa_{};
    bool                                                           hasSiblingBsa_       = false;

    uint64_t                                                       frame_               = 0ull;
    uint64_t                                                       usedVramBytes_       = 0ull;
    uint64_t                                                       usedModelCacheBytes_ = 0ull;
    uint64_t                                                       cacheHits_           = 0ull;
    uint64_t                                                       cacheMisses_         = 0ull;

    std::unordered_map<ChunkKey, StreamedTamrielCell, ChunkKeyHash> cells_;
    std::unordered_map<uint32_t, CachedNifModel>                    modelCache_;
    std::unordered_set<uint32_t>                                    failedModels_; // Negative cache for marker/FX NIFs
    TamrielStreamStats                                              lastStats_{};
};

}  // namespace raw::worldgen
