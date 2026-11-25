#pragma once
// RawWorldShowcase.h: Live Beyond-the-Borders / Standalone Procedural World Streaming Manager (`raw::worldgen`).
// Tracks camera coordinates, streams concentric LOD0..LOD3 watertight multi-biome terrain chunks + Poisson props,
// enforces strict LRU eviction under a configurable VRAM budget cap, and provides a deterministic offline
// 6-biome showcase diorama generator with optional .glb/.obj export. Zero Skyrim or D3D12 headers required!
// Original work, MIT.

#include "RawMeshPipeline.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace raw::worldgen {

struct ChunkKey {
    int32_t x = 0;
    int32_t y = 0;
    bool operator==(const ChunkKey& o) const { return x == o.x && y == o.y; }
};

struct ChunkKeyHash {
    size_t operator()(const ChunkKey& k) const {
        const uint64_t ux = static_cast<uint32_t>(k.x);
        const uint64_t uy = static_cast<uint32_t>(k.y);
        uint64_t h = (ux << 32) ^ (uy * 0x9E3779B97F4A7C15ull);
        h ^= h >> 30;
        h *= 0xBF58476D1CE4E5B9ull;
        h ^= h >> 27;
        return static_cast<size_t>(h);
    }
};

struct StreamConfig {
    TerrainConfig terrain{};
    uint64_t      vramBudgetBytes      = 128ull * 1024ull * 1024ull; // 128 MB default VRAM cap
    int32_t       lod0RadiusChunks     = 1;
    int32_t       lod1RadiusChunks     = 2;
    int32_t       lod2RadiusChunks     = 3;
    int32_t       lod3RadiusChunks     = 4;
    uint32_t      maxLoadsPerFrame     = 4u;
    uint32_t      maxMeshletTris       = 64u;   // 64 for RawMeshHost AS+MS; up to 126 for RawTerrainWorld.hlsl
    bool          showcaseMode         = false; // true = stream everywhere; false = activate near/beyond Tamriel edge
    float         frontierActivationMarginU = 16384.0f; // wake streaming 4 cells before Tamriel border
};

struct StreamedChunkEntry {
    ChunkKey                  key{};
    uint32_t                  lodLevel           = 0u;
    uint8_t                   stitchMask         = 0u;
    uint64_t                  lastTouchedFrame   = 0u;
    uint64_t                  estimatedVramBytes = 0u;
    BiomeId                   dominantBiome      = BiomeId::OldGrowthConiferHighlands;
    TerrainChunkData          terrain;
    ProceduralMesh            mesh;
    std::vector<PropInstance> props;
    uint64_t                  gpuHandle          = 0u;
};

// Host upload/eviction sink (implemented by D3D12Bridge / RawMeshHost or a mock in unit tests).
class IGpuChunkSink {
public:
    virtual ~IGpuChunkSink() = default;
    virtual uint64_t OnChunkLoaded(const StreamedChunkEntry& entry) = 0;
    virtual void     OnChunkEvicted(ChunkKey key, uint64_t gpuHandle) = 0;
};

struct StreamUpdateStats {
    bool     activeInFrontier = false;
    uint32_t loadedThisFrame  = 0u;
    uint32_t evictedThisFrame = 0u;
    uint32_t activeChunkCount = 0u;
    uint32_t totalMeshlets    = 0u;
    uint32_t totalProps       = 0u;
    uint64_t usedVramBytes    = 0u;
    uint64_t vramBudgetBytes  = 0u;
    BiomeId  cameraBiome      = BiomeId::OldGrowthConiferHighlands;
    ChunkKey cameraChunk{};
};

class RawWorldShowcase {
public:
    bool Init(const StreamConfig& cfg);
    void Reset(IGpuChunkSink* sink = nullptr);

    // Advances streaming for current camera world position; returns live telemetry.
    StreamUpdateStats Update(Vec3 camWorldPos, IGpuChunkSink* sink = nullptr);

    const StreamConfig& Config() const { return m_cfg; }
    uint64_t            UsedVramBytes() const { return m_usedVramBytes; }
    uint32_t            ActiveChunkCount() const { return static_cast<uint32_t>(m_chunks.size()); }
    const StreamedChunkEntry* FindChunk(ChunkKey key) const;

    // Iterates all currently resident chunks (e.g. for DispatchMesh rendering).
    const std::unordered_map<ChunkKey, StreamedChunkEntry, ChunkKeyHash>& ResidentChunks() const {
        return m_chunks;
    }

    // Computes target LOD (0..3) or >3 if outside streaming radius.
    uint32_t ComputeTargetLod(int32_t dx, int32_t dy) const;

private:
    bool IsCameraInFrontier(Vec3 camWorldPos) const;
    uint8_t ComputeStitchMask(int32_t dx, int32_t dy, uint32_t myLod) const;
    uint32_t EvictLruToBudget(ChunkKey protectedKey, IGpuChunkSink* sink);

    bool                                                           m_initialized   = false;
    StreamConfig                                                   m_cfg{};
    uint64_t                                                       m_frame         = 0u;
    uint64_t                                                       m_usedVramBytes = 0u;
    std::unordered_map<ChunkKey, StreamedChunkEntry, ChunkKeyHash> m_chunks;
};

// Deterministic offline showcase diorama across all 6 biomes + hero props.
struct OfflineShowcaseSummary {
    bool                 ok              = false;
    uint32_t             biomesGenerated = 0u;
    uint32_t             totalMeshlets   = 0u;
    uint32_t             totalVertices   = 0u;
    uint32_t             totalTriangles  = 0u;
    uint32_t             totalProps      = 0u;
    uint32_t             exportedFiles   = 0u;
    std::vector<BiomeId> biomeOrder;
};

// Generates 6 representative frontier biome chunks (one per showcase biome) + 6 hero props.
// If optionalExportDir is non-empty, exports .glb and .obj files there for general gamedev reuse.
OfflineShowcaseSummary GenerateOfflineShowcaseDiorama(uint32_t seed, const std::string& optionalExportDir = "");

}  // namespace raw::worldgen
