#pragma once
// RawProceduralLib.h: Zero-External-Asset Procedural Geometry & PBR Material Synthesis Library.
// Generates clean-room 3D botanical trees, alpine boulders, 3D grass clumps, tessellated ocean grids,
// and seamless tileable RGBA8 PBR micro-textures directly in memory into RAW-MESH 64v/64t meshlets
// and RawTextureAsset buffers with zero reliance on Megascans, third-party packs, or external files.
// Original work, MIT.

#include <cstdint>

#include "RawAssetBridge.h"
#include "RawTextureBridge.h"

namespace raw::bridge {

enum class ProceduralTexKind : uint32_t {
    BarkFurrow    = 0,
    PineNeedles   = 1,
    GraniteStrata = 2,
    SnowSastrugi  = 3,
    CellularFoam  = 4,
};

// Generates a 3D old-growth Nordic spruce with buttressed trunk, golden-angle whorled boughs,
// snow-load droop, outward botanical normals, wind-flex UV.y (0=tip, 1=root), and RAW-MESH meshlets.
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

// Synthesizes a seamless tileable RGBA8 procedural PBR micro-texture in memory (no external images).
// RGB stores linear-ready albedo or sculpted detail color; A stores physical height/roughness micro-mask.
bool SynthesizeProceduralPbrTexture(ProceduralTexKind kind, uint32_t width, uint32_t height,
                                    uint32_t seed, RawTextureAsset* outTex);

}  // namespace raw::bridge
