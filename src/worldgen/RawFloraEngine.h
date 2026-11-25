#pragma once
// RawFloraEngine.h: First-Principles Biomechanical Botany & 3D Procedural Tree Engine (`raw::worldgen`).
// Clean-room MIT C++23 framework with zero external assets, zero Skyrim/D3D12 header dependencies.
// Emits 100% native `raw::worldgen::ProceduralMesh` (`TerrainVertex` 80B + `MeshletDesc` 48B + uint32 indices)
// ready for direct GPU AS+MS/VS+PS rendering in `RawMeshHost` & `RawTerrainWorld.hlsl` with zero conversion!
//
// Core First-Principles Systems:
//   - Runions/Prusinkiewicz 3D Space-Colonization + Directional Phototropism + Gravitropism + Wind Shear
//   - Leonardian da Vinci Pipe Model (r_parent^p = sum r_child^p, default p = 2.5) for smooth organic taper
//   - Terrain-slope buttress root flaring gripping the exact local ground plane
//   - Self-pruning of shaded lower limbs into gnarled stub knots + helical bark grain relief
//   - 5 Hero Species: OldGrowthSitkaSpruce, WindsweptCoastalPine, NordicSilverBirch,
//                     AncientGnarledOak, AlpineUnderstoryShrub
//   - Real 3D sculpted needle whorls & curved broadleaf cluster fans (zero flat cross-cards!)
//   - Hierarchical Euler-Bernoulli cantilever bend weights (`meta.y`) & branch-order wind phase (`meta.z`)
//   - Watertight 64v/126t GPU meshlets, 3-tier LOD chains (LOD0 hero -> LOD1 mid -> LOD2 forest), .glb/.obj export
// Original work, MIT.

#include "RawMeshPipeline.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace raw::worldgen {

// 5 Distinct Hero Botanical Species.
enum class FloraSpecies : uint8_t {
    OldGrowthSitkaSpruce  = 0, // 22-35m boreal spire, drooping layered boughs, 3D needle sprays, north-trunk moss
    WindsweptCoastalPine  = 1, // Gnarled leaning twisted trunk gripping granite, sculpted windswept canopy pads
    NordicSilverBirch     = 2, // Slender multi-stem white bark with dark lenticel bands & graceful pendulous twigs
    AncientGnarledOak     = 3, // Massive fluted buttress roots, spreading multi-order boughs, layered broadleaf fans
    AlpineUnderstoryShrub = 4, // Dense multi-stemmed fern/juniper/bush cluster with per-stem skeletal bend weights
    Count                 = 5,
};

// Seasonal presets for quick integration or offline baking (continuous PhenologyState values also supported directly).
enum class FloraSeasonPreset : uint8_t {
    SpringBudBreak = 0,
    SummerLush     = 1,
    PeakAutumn     = 2,
    DeepWinter     = 3,
};

struct FloraGrowthParams {
    FloraSpecies species             = FloraSpecies::OldGrowthSitkaSpruce;
    uint32_t     seed                = 202607u;
    uint32_t     lodLevel            = 0u;     // 0=hero close-up, 1=mid-distance, 2=distant forest
    float        heightMeters        = 0.0f;   // <=0 uses biological species default
    float        crownRadiusMeters   = 0.0f;   // <=0 uses biological species default
    float        unitsPerMeter       = 70.0f;  // 70.0 = Skyrim world units (1m = 70u); 1.0 = pure metric export
    Vec3         terrainNormal{0.0f, 0.0f, 1.0f}; // Local ground slope normal for flared buttress root gripping
    Vec3         sunDir{0.35f, -0.45f, 0.82f};    // Directional sunlight phototropism vector
    Vec2         prevailingWindXY{0.75f, 0.20f};  // Prevailing wind shear vector (magnitude ~0..2)
    float        snowLoad01          = 0.25f;  // Surface snow coverage on upper boughs (N.z > 0.45)
    float        ageFactor01         = 0.85f;  // 0.2=sapling .. 1.0=ancient old-growth
    float        daVinciExponent     = 2.5f;   // Pipe-model exponent: r_parent^p = sum(r_child^p)
    uint32_t     maxMeshletTris      = 126u;   // Clamped to [16, 126], max 64 vertices per meshlet

    // Seasonal Phenology & Physical Winter Snow-Load Cantilever Sag (coupled directly to RawWeatherSim::PhenologyState):
    float        chlorophyll01       = 1.0f;   // [0..1] summer chlorophyll retention (1=lush green, 0=senescent)
    float        autumnCarotenoid01  = 0.0f;   // [0..1] golden/amber autumn pigment (Silver Birch, Oak)
    float        autumnAnthocyanin01 = 0.0f;   // [0..1] deep crimson/russet autumn pigment (Oak, Alpine Shrub)
    float        leafAbscission01    = 0.0f;   // [0..1] deciduous leaf shedding (1=bare winter limbs; conifers evergreen)
    float        canopySnowLoad01    = 0.0f;   // [0..1] physical winter snow weight driving downward Euler-Bernoulli sag
};

// Biological branch skeleton node (in world units, Z-up, root at local origin).
struct BranchNode {
    int32_t              parentIndex      = -1;
    Vec3                 position{};
    Vec3                 direction{0.0f, 0.0f, 1.0f};
    float                radius           = 0.0f; // Pipe-model radius in world units
    float                normalizedHeight = 0.0f; // [0..1] root to crown apex
    float                bendFlex         = 0.0f; // [0..1] cantilever wind flexibility (0 rooted -> 1 tip)
    float                windPhaseHash    = 0.0f; // [0..1] branch-order oscillation phase hash
    uint8_t              branchOrder      = 0u;   // 0=trunk/stem, 1=primary, 2=secondary, 3=twig, 4=pruned stub
    uint8_t              stemId           = 0u;   // Distinct stem index for multi-stem Birch & UnderstoryShrub
    bool                 isDeadStub       = false;
    bool                 hasFoliage       = false;
    std::vector<int32_t> children;
};

struct FloraBuildStats {
    uint32_t totalNodes           = 0u;
    uint32_t prunedStubCount      = 0u;
    uint32_t foliageClusterCount  = 0u;
    uint32_t abscisedClusterCount = 0u;
    uint32_t rootButtressCount    = 0u;
    uint32_t stemCount            = 1u;
    float    rootRadiusU          = 0.0f;
    float    minTipRadiusU        = 0.0f;
    float    maxTipRadiusU        = 0.0f;
    float    actualHeightU        = 0.0f;
    float    meanCanopyZ          = 0.0f; // Mean Z elevation of crown boughs (drops under winter canopySnowLoad01!)
    float    lowestRootZ          = 0.0f;
    bool     pipeModelMonotonic   = true;
};

struct FloraAsset {
    FloraSpecies            species = FloraSpecies::OldGrowthSitkaSpruce;
    FloraGrowthParams       params{};
    std::vector<BranchNode> skeleton;
    ProceduralMesh          mesh;  // Ready for RawMeshHost + RawTerrainWorld.hlsl (TerrainVertex 80B + MeshletDesc 48B)
    FloraBuildStats         stats{};
};

struct FloraLodChain {
    FloraSpecies    species = FloraSpecies::OldGrowthSitkaSpruce;
    ProceduralMesh  lods[3];      // LOD0 hero, LOD1 mid, LOD2 distant forest
    FloraBuildStats lodStats[3];
};

// Species metadata, seasonal phenology helpers & biome-aware selector.
const char*       GetFloraSpeciesName(FloraSpecies species);
FloraSpecies      SelectFloraSpeciesForBiome(BiomeId biome, uint32_t seed, float slope01 = 0.0f, float altitudeNorm01 = 0.5f);
FloraGrowthParams MakeDefaultFloraParams(FloraSpecies species, uint32_t seed, uint32_t lodLevel = 0u);
void              ApplyFloraSeasonPreset(FloraSeasonPreset preset, FloraGrowthParams* params);
void              ApplyPhenologyToFloraParams(float chlorophyll01, float autumnCarotenoid01,
                                              float autumnAnthocyanin01, float leafAbscission01,
                                              float canopySnowLoad01, FloraGrowthParams* params);

// Full biomechanical growth + 3D foliage cluster + 64v/126t GPU meshlet generation.
bool GenerateFloraAsset(const FloraGrowthParams& params, FloraAsset* outAsset);
bool GenerateFloraMesh(const FloraGrowthParams& params, ProceduralMesh* outMesh, FloraBuildStats* outStats = nullptr);
bool GenerateFloraLodChain(const FloraGrowthParams& baseParams, FloraLodChain* outChain);

// Drop-in integration helper for `RawWorldShowcase` / `RawMeshHost` prop baking:
// Generates the biome-matched biomechanical tree/shrub directly from a `PropInstance`!
bool BuildBiomechanicalTreeForProp(const PropInstance& prop, uint32_t lodLevel, float snowLoad01,
                                   uint32_t maxMeshletTris, ProceduralMesh* outMesh);

// General gamedev exporters (delegating to RawMeshPipeline's zero-dependency Wavefront .obj & glTF 2.0 .glb writers).
bool ExportFloraToGlb(const FloraAsset& asset, const std::string& path, bool zUpToYUp = true);
bool ExportFloraToObj(const FloraAsset& asset, const std::string& path);
bool ExportFloraLodChainToGlb(const FloraLodChain& chain, const std::string& dirPath,
                              const std::string& baseName, bool zUpToYUp = true);

}  // namespace raw::worldgen
