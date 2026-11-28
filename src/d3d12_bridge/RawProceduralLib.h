#pragma once
// RawProceduralLib.h: Zero-External-Asset Procedural Geometry & PBR Material Synthesis Library.
// Generates clean-room 3D botanical trees, alpine boulders, glacial ice arches, snow drifts,
// hexagonal basalt columns, volcanic vents, crystal monoliths, 3D grass clumps, tessellated ocean grids,
// live procedural multi-biome terrain chunks (backed by `raw::worldgen`), and seamless tileable RGBA8 PBR
// micro-textures directly in memory into RAW-MESH meshlets and RawTextureAsset buffers with zero external assets.
// Original work, MIT.

#include <cstdint>

#include "RawAssetBridge.h"
#include "RawTextureBridge.h"
#include "../worldgen/RawMeshPipeline.h"

namespace raw::bridge {

enum class ProceduralTexKind : uint32_t {
    BarkFurrow          = 0,
    PineNeedles         = 1,
    GraniteStrata       = 2,
    SnowSastrugi        = 3,
    CellularFoam        = 4,
    // Expanded multi-biome zero-external-asset PBR library:
    GlacialIceFracture  = 5,
    VolcanicBasaltMagma = 6,
    HexagonalBasalt     = 7,
    CrystalQuartzFacet  = 8,
    MossyGraniteCobble  = 9,
};

// Converts any `raw::worldgen::ProceduralMesh` directly into a GPU-uploadable `RawMeshAsset` (64v/64t RAW-MESH).
bool ConvertWorldGenMeshToRawMeshAsset(const raw::worldgen::ProceduralMesh& src, RawMeshAsset* outMesh);

// Generates a 3D old-growth Nordic spruce with buttressed trunk, golden-angle whorled boughs,
// secondary 3D needle sub-boughs, snow-load droop, wind-flex UV.y (0=tip, 1=root), and RAW-MESH meshlets.
bool GenerateNordicSpruceTree(uint32_t seed, float heightU, float crownRadiusU, float snowLoad01,
                              RawMeshAsset* outMesh);

// Generates a sculpted Nordic granite/basalt boulder from a subdivided icosahedron carved by
// Voronoi fracture planes and sedimentary strata ledges, packed into RAW-MESH meshlets.
bool GenerateNordicBoulder(uint32_t seed, float radiusU, float strataTilt, uint32_t fracturePlanes,
                           RawMeshAsset* outMesh);

// Generates a 3D multi-segment arched alpine grass/fern clump with root-to-tip wind flexibility
// encoded in UV.y (1=root, 0=tip) for GPU gust fronts and actor-wake bending.
bool GenerateAlpineGrassClump(uint32_t seed, uint32_t bladeCount, float heightU,
                              RawMeshAsset* outMesh);

// Generates a tessellated ocean patch meshlet grid (gridN x gridN quads) ready for GPU displacement.
bool GenerateOceanTileMesh(uint32_t gridN, float spanU, RawMeshAsset* outMesh);

// Expanded `raw::worldgen`-backed zero-external-asset 3D hero biome assets for RawMeshHost AS+MS:
bool GenerateGlacialIceArch(uint32_t seed, float spanU, float heightU, float thicknessU,
                            RawMeshAsset* outMesh);

bool GenerateNordicSnowDrift(uint32_t seed, float lengthU, float widthU, float heightU,
                             RawMeshAsset* outMesh);

bool GenerateBasaltColumnCluster(uint32_t seed, uint32_t columnCount, float radiusU, float maxHeightU,
                                 RawMeshAsset* outMesh);

bool GenerateVolcanicVent(uint32_t seed, float radiusU, float heightU, RawMeshAsset* outMesh);

bool GenerateCrystalMonolith(uint32_t seed, float radiusU, float heightU, uint32_t shardCount,
                             RawMeshAsset* outMesh);

// Generates a live procedural 6-biome terrain chunk at (chunkX, chunkY, lodLevel 0..3) into RawMeshAsset.
bool GenerateProceduralTerrainChunk(int32_t chunkX, int32_t chunkY, uint32_t lodLevel, uint32_t seed,
                                    uint8_t neighborCoarserMask, RawMeshAsset* outMesh);

// Synthesizes a seamless tileable RGBA8 procedural PBR micro-texture in memory (no external images).
// RGB stores linear-ready albedo or sculpted detail color; A stores physical height/roughness micro-mask.
bool SynthesizeProceduralPbrTexture(ProceduralTexKind kind, uint32_t width, uint32_t height,
                                    uint32_t seed, RawTextureAsset* outTex);

}  // namespace raw::bridge
