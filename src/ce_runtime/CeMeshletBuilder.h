#pragma once
// CeMeshletBuilder.h: Wave 85 Watertight 64v/64t Meshlet Clusterizer & Monotonic QEM LOD DAG.
// Partitions repaired meshes into strict <= 64 vertex / <= 64 triangle meshlets matching
// Wave 83's 48-byte GpuMeshletDesc (bounding sphere + snorm8 packed normal bounding cone),
// and constructs a monotonic Quadric Error Metric (QEM) cluster LOD DAG enforcing
// parentErrorWorld >= lodErrorWorld so continuous GPU screen-space LOD cuts never crack or pop.
// Clean-room MIT, zero external dependencies, MSVC /W4 /WX clean.

#include <cstdint>
#include <string>
#include <vector>

#include "CeTopologyRepair.h"
#include "../d3d12_bridge/RawTwoPassHiZ.h"

namespace raw::ce
{

constexpr uint32_t kMaxMeshletVertices  = 64u;
constexpr uint32_t kMaxMeshletTriangles = 64u;

struct BuiltMeshlet
{
    raw::bridge::wave83::GpuMeshletDesc gpuDesc{};
    std::vector<uint32_t>               vertexIndices;    // <= 64 global vertex indices
    std::vector<uint8_t>                primitiveIndices; // 3 * triCount (<= 192 local indices)
    uint32_t                            lodLevel      = 0u;
    uint32_t                            groupId       = 0u;
    uint32_t                            parentGroupId = 0xFFFFFFFFu;
    float                               coneAxis[3]   = {0.0f, 0.0f, 1.0f};
    float                               coneCutoff    = 1.0f;
    bool                                degenerateCone = false;
};

struct MeshletBuildConfig
{
    uint32_t maxVertices                 = kMaxMeshletVertices;
    uint32_t maxTriangles                = kMaxMeshletTriangles;
    uint32_t maxLodLevels                = 4u;
    uint32_t psoBucketId                 = 0u;
    bool     enforceMonotonicParentError = true;
};

struct MeshletLodDag
{
    std::vector<BuiltMeshlet>                        meshlets;
    std::vector<raw::bridge::wave83::GpuMeshletDesc> gpuDescriptors;
    uint32_t                                         lodLevels        = 0u;
    uint32_t                                         lod0MeshletCount = 0u;
    float                                            avgVertsPerTri   = 0.0f;
    bool                                             monotonicValid   = false;
};

// Packs a unit normal cone axis [-1..1]^3 and cutoff sin(halfAngle) [-1..1] into snorm8x4 uint32.
uint32_t PackNormalConeSnorm8(float ax, float ay, float az, float cutoff) noexcept;

// Unpacks snorm8x4 uint32 back to axis and cutoff.
void UnpackNormalConeSnorm8(uint32_t packed, float outAxis[3], float* outCutoff) noexcept;

// Partitions a repaired mesh into strict <=64v / <=64t meshlets with bounding spheres & cones,
// and builds a monotonic QEM (Quadric Error Metric) cluster LOD DAG.
MeshletLodDag BuildWatertightMeshletLodDag(
    const std::vector<RepairedVertex>& verts,
    const std::vector<uint32_t>&       indices,
    const MeshletBuildConfig&          cfg = {});

// Fail-closed validator verifying strict <=64v/<=64t limits, valid finite bounding spheres & cones,
// and monotonic parentErrorWorld >= lodErrorWorld across the entire LOD DAG.
bool ValidateMeshletLodDag(const MeshletLodDag& dag, std::string* outErr = nullptr);

} // namespace raw::ce
