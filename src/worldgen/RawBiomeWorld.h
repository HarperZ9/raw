#pragma once
// RawBiomeWorld.h: 6-Biome Procedural Showcase World, Canonical 8-Channel PBR Splatting & Poisson Prop Distribution.
// Standalone C++20/23 clean-room MIT framework (`raw::worldgen`), zero Skyrim header dependencies.
// Showcases every RAW engine upgrade across 6 distinct frontier biomes beyond Tamriel's borders:
//   0. Glacial Fjord Spires          (crystalline ice arches, turquoise crevasses, sea swells, aurora reflections)
//   1. Geothermal Volcanic Caldera   (2400K blackbody basalt magma fissures, steam vents, obsidian columns)
//   2. Old-Growth Conifer Highlands  (dense multi-tier procedural spruce/fir forests, mossy granite cliffs)
//   3. Obsidian Basalt Canyons       (Giant's Causeway hexagonal columnar basalt, river rapids, stone bridges)
//   4. Bioluminescent Crystal Tundra (refractive quartz/amethyst monoliths, diamond snowfields, ice caves)
//   5. Alpine Meltwater Deltas       (braided turquoise glacial rivers, wet cobblestone banks, wildflower meadows)
// Original work, MIT.

#include "RawTerrainGen.h"

#include <array>
#include <cstdint>
#include <vector>

namespace raw::worldgen {

enum class BiomeId : uint8_t {
    GlacialFjordSpires          = 0,
    GeothermalVolcanicCaldera   = 1,
    OldGrowthConiferHighlands   = 2,
    ObsidianBasaltCanyons       = 3,
    BioluminescentCrystalTundra = 4,
    AlpineMeltwaterDeltas       = 5,
    Count                       = 6,
};

// Canonical 8 terrain/world PBR material channels (synchronized with RawTerrainWorld.hlsl & RawMaterials3D.hlsli).
enum class TerrainMaterialId : uint8_t {
    RockGranite     = 0,
    SnowPack        = 1,
    GlacialIce      = 2, // IOR 1.31, zero time-pulse
    BasaltColumn    = 3,
    MagmaFissure    = 4, // 1000K..3400K Planck blackbody
    MossMeadow      = 5,
    RiverPebbleSand = 6,
    ArcaneCrystal   = 7, // Refractive quartz/amethyst/bioluminescent crystal
    Count           = 8,
};

enum class PropKind : uint8_t {
    ConiferTree         = 0,
    AlpineBoulder       = 1,
    GlacialIceArch      = 2,
    SnowDrift           = 3,
    BasaltColumnCluster = 4,
    VolcanicSteamVent   = 5,
    CrystalMonolith     = 6,
    AlpineGrassClump    = 7,
    Count               = 8,
};

struct BiomeSpec {
    BiomeId     id;
    const char* name;
    float       elevationScale;
    float       snowLineOffsetU;
    float       minPropSpacingU;
    float       propDensity01;
    PropKind    heroProp;
    float       accentRgb[3];
};

struct BiomeBlendResult {
    std::array<float, 6> weights{};        // Normalized weights summing to 1.0
    BiomeId              primary   = BiomeId::OldGrowthConiferHighlands;
    BiomeId              secondary = BiomeId::AlpineMeltwaterDeltas;
    float                temperature01      = 0.5f;
    float                moisture01         = 0.5f;
    float                volcanism01        = 0.0f;
    float                crystalResonance01 = 0.0f;
};

struct MaterialSplat8 {
    Vec4              splat0{1, 0, 0, 0}; // x=RockGranite, y=SnowPack, z=GlacialIce, w=BasaltColumn
    Vec4              splat1{0, 0, 0, 0}; // x=MagmaFissure, y=MossMeadow, z=RiverPebbleSand, w=ArcaneCrystal
    float             emissiveMask  = 0.0f;
    float             roughnessHint = 0.72f;
    TerrainMaterialId dominant      = TerrainMaterialId::RockGranite;
};

struct PropInstance {
    PropKind          kind = PropKind::AlpineBoulder;
    BiomeId           biome = BiomeId::OldGrowthConiferHighlands;
    TerrainMaterialId material = TerrainMaterialId::RockGranite;
    Vec3              position{};
    Vec3              upNormal{0.0f, 0.0f, 1.0f};
    float             yawRad = 0.0f;
    float             scale  = 1.0f;
    uint32_t          seed   = 0u;
};

const BiomeSpec& GetBiomeSpec(BiomeId id);
const char*      GetBiomeName(BiomeId id);
const char*      GetMaterialName(TerrainMaterialId id);
const char*      GetPropKindName(PropKind kind);

// Evaluates deterministic soft-blended 6-biome climate weights at any world (x, y).
BiomeBlendResult EvaluateBiomeBlend(float worldX, float worldY, uint32_t seed);

// Shapes raw tectonic elevation with biome-specific geological landforms (fjords, calderas, hexagonal canyons, etc.).
float ShapeBiomeElevation(float worldX, float worldY, const TectonicSample& tec,
                          const HydrographySample& hydro, uint32_t seed, BiomeBlendResult* outBlend = nullptr);

// Computes normalized 8-channel PBR material splat weights from biome weights + geological surface attributes.
MaterialSplat8 EvaluateBiomeMaterials(Vec3 worldPos, Vec3 normal, float curvature, float flow,
                                      float erosion, float wetness, float riverDepth,
                                      const BiomeBlendResult& blend, float seaLevelZ, float snowLineZ,
                                      uint32_t seed);

// Generates a complete 6-biome terrain chunk with elevation shaping, hydraulic/thermal erosion,
// watertight skirtless LOD stitching, and normalized 8-channel PBR material splat maps.
bool GenerateBiomeTerrainChunk(int32_t chunkX, int32_t chunkY, uint32_t lodLevel, const TerrainConfig& cfg,
                               uint8_t neighborCoarserMask, TerrainChunkData* outChunk);

// Deterministic 2D Poisson-disc prop & foliage placement on a generated terrain chunk.
// Guarantees minimum spacing between all placed instances in the chunk.
uint32_t GenerateChunkProps(const TerrainChunkData& chunk, uint32_t seed, float minSpacingOverrideU,
                            std::vector<PropInstance>* outProps);

}  // namespace raw::worldgen
