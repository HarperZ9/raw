#pragma once
// RawEsmWorldLoader.h: 100% Clean-Room Native TES5 (.ESM/.ESP) Binary Worldspace Loader (`raw::worldgen`).
// Streams Skyrim.esm directly via std::ifstream (seeking past non-WRLD/LTEX GRUPs with minimal RAM),
// transparently decompresses zlib (0x00040000) records via built-in RFC 1950/1951 inflate, decodes
// WRLD / CELL / LAND (VHGT, VNML, VCLR, BTXT, ATXT, VTXT) / REFR (NAME, DATA, XSCL), and converts any
// Tamriel exterior cell directly into `TerrainChunkData` + hardware-validated 64v/126t GPU meshlets
// (`ProceduralMesh`) ready for `RawMeshHost`, `RawTerrainWorld.hlsl`, and `.glb`/`.obj` export!
// Zero Bethesda executable/SDK/D3D12/Windows dependencies. Original work, MIT.

#include "RawMeshPipeline.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace raw::worldgen {

inline constexpr uint32_t kTamrielWorldFormId = 0x0000003Cu;
inline constexpr uint32_t kEsmLandGridSize    = 33u;           // 33x33 vertices (32x32 quads @ 128u = 4096u cell)
inline constexpr uint32_t kEsmLandVertCount   = 33u * 33u;     // 1089 vertices per exterior cell
inline constexpr float    kEsmCellSizeU       = 4096.0f;
inline constexpr float    kEsmHeightScaleU    = 8.0f;          // VHGT accumulated units -> Skyrim world Z units

struct EsmCellCoordKey {
    int32_t x = 0;
    int32_t y = 0;
    bool operator==(const EsmCellCoordKey& o) const noexcept { return x == o.x && y == o.y; }
};

struct EsmCellCoordHash {
    size_t operator()(const EsmCellCoordKey& k) const noexcept {
        const uint64_t ux = static_cast<uint32_t>(k.x);
        const uint64_t uy = static_cast<uint32_t>(k.y);
        uint64_t h = (ux << 32) ^ (uy * 0x9E3779B97F4A7C15ull);
        h ^= h >> 30;
        h *= 0xBF58476D1CE4E5B9ull;
        h ^= h >> 27;
        return static_cast<size_t>(h);
    }
};

struct EsmWorldspaceInfo {
    uint32_t    formId             = 0u;
    std::string edid;
    float       defaultLandHeight  = -2048.0f;
    float       defaultWaterHeight = -14000.0f;
    uint64_t    grupFileOffset     = 0ull; // File offset of WRLD Children GRUP (Type 1)
    uint32_t    grupByteSize       = 0u;
};

struct EsmCellIndexEntry {
    uint32_t    cellFormId         = 0u;
    int32_t     cellX              = 0;
    int32_t     cellY              = 0;
    std::string edid;
    uint16_t    cellFlags          = 0u;
    bool        hasWaterHeight     = false;
    float       waterHeight        = -14000.0f;
    uint64_t    cellGrupFileOffset = 0ull; // File offset of Cell Children GRUP (Type 6)
    uint32_t    cellGrupByteSize   = 0u;
    bool        hasLand            = false;
    uint64_t    landRecordOffset   = 0ull; // Direct file offset of LAND record (fast O(1) load!)
    uint32_t    refrCountEstimate  = 0u;
};

struct EsmRefPlacement {
    uint32_t    refrFormId = 0u;
    uint32_t    baseFormId = 0u;
    std::string edid;
    Vec3        position{};
    Vec3        rotationRad{}; // Euler XYZ in radians (Skyrim REFR::DATA)
    float       scale      = 1.0f;
    bool        persistent = false;
};

struct EsmLandData {
    uint32_t                         landFormId    = 0u;
    bool                             hasVhgt       = false;
    bool                             hasVnml       = false;
    bool                             hasVclr       = false;
    bool                             hasTextures   = false;
    float                            vhgtBaseOffset = 0.0f;
    float                            minHeightZ    = 0.0f;
    float                            maxHeightZ    = 0.0f;
    float                            meanHeightZ   = 0.0f;
    std::array<float, kEsmLandVertCount>  heightsZ{}; // Exact Skyrim world Z = (offset + accum) * 8.0f
    std::array<Vec3,  kEsmLandVertCount>  normals{};  // Unit normals decoded from VNML (or finite-diff fallback)
    std::array<Vec3,  kEsmLandVertCount>  colors{};   // RGB vertex tint [0..1] from VCLR (default 1,1,1)
    std::array<Vec4,  kEsmLandVertCount>  splat0{};   // RockGranite, SnowPack, GlacialIce, BasaltColumn
    std::array<Vec4,  kEsmLandVertCount>  splat1{};   // MagmaFissure, MossMeadow, RiverPebbleSand, ArcaneCrystal
    std::array<uint32_t, 4>               baseQuadTexFormId{}; // BTXT per quadrant (0=SW,1=SE,2=NW,3=NE)
    uint32_t                              alphaLayerCount = 0u;
};

struct EsmLoadedCell {
    EsmCellIndexEntry              meta{};
    EsmLandData                    land{};
    std::vector<EsmRefPlacement>   placements;
    std::vector<PropInstance>      props;
};

struct EsmWorldIndex {
    std::string                                                            esmPath;
    float                                                                  headerVersion = 0.0f;
    uint16_t                                                               formVersion   = 0u;
    std::vector<EsmWorldspaceInfo>                                         worldspaces;
    EsmWorldspaceInfo                                                      selectedWorld{};
    std::unordered_map<uint32_t, std::string>                              landTextureEdids; // LTEX formId -> EDID
    std::unordered_map<EsmCellCoordKey, EsmCellIndexEntry, EsmCellCoordHash> cells;
    uint32_t                                                               totalIndexedCells     = 0u;
    uint32_t                                                               totalCellsWithLand    = 0u;
    uint32_t                                                               totalIndexedRefrs     = 0u;
    uint32_t                                                               compressedRecordsSeen = 0u;

    const EsmCellIndexEntry* FindCell(int32_t cellX, int32_t cellY) const;
};

// Clean-room zero-dependency RFC 1950 zlib / RFC 1951 DEFLATE inflator (handles TES5 0x00040000 records).
bool InflateEsmZlib(const uint8_t* src, size_t srcLen, size_t expectedOut, std::vector<uint8_t>* out);

// Maps a Skyrim LTEX EditorID / FormID to one of RAW's 8 canonical physical terrain material channels.
TerrainMaterialId ClassifySkyrimLandTexture(const std::string& ltexEdid, uint32_t ltexFormId);

// Decodes a raw LAND record payload (uncompressed subrecord stream) into EsmLandData.
bool DecodeEsmLandPayload(const uint8_t* payload, size_t payloadSize, uint32_t landFormId,
                          const std::unordered_map<uint32_t, std::string>& landTexEdids,
                          EsmLandData* outLand, std::string* outErr = nullptr);

// Streaming indexer: scans LTEX + WRLD headers and indexes all exterior CELL/LAND/REFR offsets
// for `targetWorldFormId` (default 0x0000003C = Tamriel) with minimal RAM footprint.
bool IndexEsmWorldspace(const std::string& esmPath,
                        uint32_t targetWorldFormId,
                        EsmWorldIndex* outIndex,
                        std::string* outErr = nullptr);

// Streams and decodes a single indexed exterior cell (LAND heightfield/normals/colors/splats + REFR placements).
bool LoadEsmExteriorCell(const EsmWorldIndex& index,
                         int32_t cellX,
                         int32_t cellY,
                         EsmLoadedCell* outCell,
                         std::string* outErr = nullptr);

// Converts a decoded EsmLoadedCell directly into a watertight `raw::worldgen::TerrainChunkData` (33x33 vertices, 4096u).
bool BuildTerrainChunkFromEsmCell(const EsmLoadedCell& cell,
                                  float defaultWaterHeightZ,
                                  TerrainChunkData* outChunk,
                                  std::string* outErr = nullptr);

// Converts a decoded EsmLoadedCell directly into a hardware-validated 64v/126t GPU meshlet `ProceduralMesh`
// passing `raw::pipeline::ValidateProceduralMeshStrict(...)` with 0 errors.
bool BuildMeshFromEsmCell(const EsmLoadedCell& cell,
                          float defaultWaterHeightZ,
                          uint32_t maxMeshletTris,
                          ProceduralMesh* outMesh,
                          TerrainChunkData* outChunkOpt = nullptr,
                          std::string* outErr = nullptr);

// Convenience exporters for REAL Tamriel cells to standard `.glb` (glTF 2.0) or Wavefront `.obj`.
bool ExportEsmCellToGlb(const EsmLoadedCell& cell, float defaultWaterHeightZ, const std::string& outGlbPath,
                        bool zUpToYUp = true, std::string* outErr = nullptr);
bool ExportEsmCellToObj(const EsmLoadedCell& cell, float defaultWaterHeightZ, const std::string& outObjPath,
                        std::string* outErr = nullptr);

}  // namespace raw::worldgen
