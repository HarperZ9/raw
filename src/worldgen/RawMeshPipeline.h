#pragma once
// RawMeshPipeline.h: Reusable GPU-Ready Procedural Mesh & 64v/126t Meshlet Pipeline (`raw::worldgen`).
// Standalone C++20/23 clean-room MIT framework with zero Skyrim or D3D12 header dependencies.
// Generates LOD0..LOD3 watertight terrain chunks, hexagonal basalt pillars, multi-tier branching
// conifers with 3D needle boughs, glacial ice arches, volcanic steam vents, crystalline monoliths,
// boulders, snow drifts, and grass clumps. Packs into DX12 AS+MS meshlets (up to 64 verts / 126 tris)
// with exact bounding spheres & normal cones, and exports standard Wavefront .obj & glTF 2.0 .glb!
// Original work, MIT.

#include "RawBiomeWorld.h"

#include <cstdint>
#include <string>
#include <vector>

namespace raw::worldgen {

// 80-byte GPU-aligned procedural terrain/prop vertex (matches RawTerrainWorld.hlsl StructuredBuffer).
struct TerrainVertex {
    float pos[3]    = {};
    float normal[3] = {0.0f, 0.0f, 1.0f};
    float uv[2]     = {};
    float splat0[4] = {1.0f, 0.0f, 0.0f, 0.0f}; // Rock, Snow, Ice, Basalt
    float splat1[4] = {0.0f, 0.0f, 0.0f, 0.0f}; // Magma, Moss, PebbleSand, Crystal
    float meta[4]   = {};                        // x=curvature, y=wetness, z=emissiveMask, w=primaryBiome
};
static_assert(sizeof(TerrainVertex) == 80u, "TerrainVertex must be 80 bytes for 16B GPU alignment");

// 48-byte GPU meshlet descriptor (64 verts / up to 126 tris for DX12 AS+MS).
struct MeshletDesc {
    uint32_t vertexOffset   = 0u;
    uint32_t triangleOffset = 0u;
    uint32_t vertexCount    = 0u;
    uint32_t triangleCount  = 0u;
    float    boundSphere[4] = {};               // xyz center, w radius
    float    normalCone[4]  = {0, 0, 1, -1.0f}; // xyz axis, w cutoff cos(halfAngle + pi/2)
};
static_assert(sizeof(MeshletDesc) == 48u, "MeshletDesc must be 48 bytes");

struct ProceduralMesh {
    std::string               name;
    std::vector<TerrainVertex> vertices;
    std::vector<uint32_t>     indices;   // Local per-meshlet indices (3 per triangle)
    std::vector<MeshletDesc>  meshlets;
    TerrainMaterialId         primaryMaterial = TerrainMaterialId::RockGranite;
    uint32_t                  lodLevel        = 0u;
    Vec3                      aabbMin{};
    Vec3                      aabbMax{};

    uint64_t EstimateGpuBytes() const {
        return uint64_t(vertices.size()) * sizeof(TerrainVertex) +
               uint64_t(indices.size()) * sizeof(uint32_t) +
               uint64_t(meshlets.size()) * sizeof(MeshletDesc);
    }
};

// Partitions an indexed mesh into DX12 AS+MS meshlets (up to maxVerts<=64, maxTris<=126)
// with exact bounding spheres and backface normal cones.
void BuildMeshlets(const std::vector<TerrainVertex>& srcVerts, const std::vector<uint32_t>& srcIndices,
                   uint32_t maxVerts, uint32_t maxTris,
                   std::vector<TerrainVertex>* outVerts, std::vector<uint32_t>* outIndices,
                   std::vector<MeshletDesc>* outMeshlets);

// Builds a spatially coherent LOD0..LOD3 terrain chunk mesh & meshlets directly from TerrainChunkData.
bool BuildTerrainChunkMesh(const TerrainChunkData& chunk, uint32_t maxMeshletTris, ProceduralMesh* outMesh);

// Procedural 3D Hero Asset & Biome Prop Generators (zero external files, zero flat billboards):
bool BuildHexagonalBasaltCluster(uint32_t seed, uint32_t columnCount, float radiusU, float maxHeightU,
                                 uint32_t maxMeshletTris, ProceduralMesh* outMesh);

bool BuildMultiTierConifer(uint32_t seed, float heightU, float crownRadiusU, float snowLoad01,
                           uint32_t maxMeshletTris, ProceduralMesh* outMesh);

bool BuildGlacialIceArch(uint32_t seed, float spanU, float heightU, float thicknessU,
                         uint32_t maxMeshletTris, ProceduralMesh* outMesh);

bool BuildVolcanicVent(uint32_t seed, float radiusU, float heightU,
                       uint32_t maxMeshletTris, ProceduralMesh* outMesh);

bool BuildCrystalMonolith(uint32_t seed, float radiusU, float heightU, uint32_t shardCount,
                          uint32_t maxMeshletTris, ProceduralMesh* outMesh);

bool BuildNordicSnowDrift(uint32_t seed, float lengthU, float widthU, float heightU,
                          uint32_t maxMeshletTris, ProceduralMesh* outMesh);

bool BuildAlpineBoulder(uint32_t seed, float radiusU, float strataTilt, uint32_t fracturePlanes,
                        uint32_t maxMeshletTris, ProceduralMesh* outMesh);

bool BuildAlpineGrassClump(uint32_t seed, uint32_t bladeCount, float heightU,
                           uint32_t maxMeshletTris, ProceduralMesh* outMesh);

// General Gamedev Exporters (zero third-party libraries):
// Exports any ProceduralMesh (flattened across meshlets) to standard Wavefront .obj or binary glTF 2.0 .glb.
std::string          SerializeMeshToObjString(const ProceduralMesh& mesh);
bool                 ExportMeshToObjFile(const ProceduralMesh& mesh, const std::string& path);
std::vector<uint8_t> SerializeMeshToGlbBytes(const ProceduralMesh& mesh, bool zUpToYUp = true);
bool                 ExportMeshToGlbFile(const ProceduralMesh& mesh, const std::string& path, bool zUpToYUp = true);

}  // namespace raw::worldgen
