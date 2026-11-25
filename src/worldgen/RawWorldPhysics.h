#pragma once
// RawWorldPhysics.h: Clean-Room 3D World Collision, SAH AABB BVH Raycaster & Capsule Character Controller (`raw::worldgen`).
// Pure portable C++23 with zero Windows, D3D12, or proprietary physics SDK dependencies.
// Works natively over:
//   - `.ESM` exterior heightfields (`EsmLoadedCell` / `TerrainChunkData`) with exact bilinear height & analytical normals
//   - Streamed `.NIF` / procedural `ProceduralMesh` 64v/126t meshlets & arbitrary triangle colliders in per-cell SAH AABB BVHs
//   - Cell-keyed insertion & O(1) eviction (`AddCellCollider` / `RemoveCellCollider`) for seamless `RawTamrielWorldStreamer` coupling
//   - Exact Moller-Trumbore raycasting + terrain segment marching
//   - Sub-stepped capsule character controller (gravity, jump, slope-angle grounding <=48 deg, step-up <=42u, water buoyancy & swim drag)
//   - Lightweight dynamic sphere rigid-body stepper for world props/projectiles.
// Original work, MIT.

#include "RawEsmWorldLoader.h"
#include "RawMeshPipeline.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace raw::worldgen {

inline constexpr uint64_t PackCellKey(int32_t cellX, int32_t cellY) noexcept {
    return (static_cast<uint64_t>(static_cast<uint32_t>(cellX)) << 32) |
            static_cast<uint64_t>(static_cast<uint32_t>(cellY));
}

struct TerrainCellHeightfield {
    uint64_t           cellKey     = 0ull;
    int32_t            cellX       = 0;
    int32_t            cellY       = 0;
    float              originX     = 0.0f;
    float              originY     = 0.0f;
    float              sizeX       = 4096.0f;
    float              sizeY       = 4096.0f;
    uint32_t           gridW       = 0u;      // Vertices along X (>= 2)
    uint32_t           gridH       = 0u;      // Vertices along Y (>= 2)
    float              minZ        = 0.0f;
    float              maxZ        = 0.0f;
    float              waterLevelZ = -14000.0f;
    uint32_t           materialTag = 0u;
    std::vector<float> heights;               // Row-major: iy * gridW + ix (size == gridW * gridH)
};

struct TriangleMeshCollider {
    std::vector<Vec3>     vertices;
    std::vector<uint32_t> indices;            // 3 indices per triangle
    uint32_t              instanceId  = 0u;
    uint32_t              materialTag = 0u;
};

struct RayHit {
    bool     hit         = false;
    bool     hitTerrain  = false;
    float    distance    = 0.0f;
    float    point[3]    = {0.0f, 0.0f, 0.0f};
    float    normal[3]   = {0.0f, 0.0f, 1.0f};
    uint64_t cellKey     = 0ull;
    uint32_t instanceId  = 0u;
    uint32_t materialTag = 0u;
};

struct CharacterBody {
    Vec3  position{0.0f, 0.0f, 0.0f};   // Bottom of capsule (feet) in Z-up world coordinates
    Vec3  velocity{0.0f, 0.0f, 0.0f};
    float radius         = 22.0f;       // Capsule radius (world units)
    float height         = 128.0f;      // Total capsule height (>= 2 * radius)
    float stepUpHeight   = 42.0f;       // Smooth stair/ledge step-up limit (<= 42u)
    float maxSlopeDeg    = 48.0f;       // Max walkable slope angle (degrees)
    float gravityZ       = -980.0f;     // World Z-up gravitational acceleration (u/s^2)
    float groundAccel    = 2200.0f;     // Horizontal ground acceleration (u/s^2)
    float airAccel       = 450.0f;      // Horizontal air-control acceleration (u/s^2)
    float groundFriction = 8.0f;        // Ground deceleration factor (1/s)
    float jumpSpeed      = 420.0f;      // Initial vertical jump velocity (u/s)
    float waterBuoyancy  = 1450.0f;     // Upward buoyancy acceleration at full immersion (u/s^2)
    float swimDrag       = 4.5f;        // Exponential fluid damping (1/s)

    // Updated every StepCharacterCapsule call:
    bool     grounded      = false;
    bool     sliding       = false;     // Contacting a steep non-walkable slope (> maxSlopeDeg)
    bool     inWater       = false;
    float    submersion01  = 0.0f;      // [0..1] fraction of capsule height below waterLevelZ
    float    waterLevelZ   = -14000.0f;
    Vec3     groundNormal{0.0f, 0.0f, 1.0f};
    uint64_t groundCellKey = 0ull;
    uint32_t contactCount  = 0u;
};

struct CharacterInput {
    Vec2  wishDirXY{0.0f, 0.0f};        // Desired horizontal move direction (unit or sub-unit)
    float maxSpeed            = 280.0f; // Target horizontal move speed (u/s)
    bool  jumpPressed         = false;
    float swimVertical        = 0.0f;   // [-1..+1] vertical swim input when submerged
    bool  hasWaterOverride    = false;
    float waterLevelOverrideZ = -14000.0f;
};

struct DynamicSphereBody {
    Vec3  position{0.0f, 0.0f, 0.0f};   // Sphere center
    Vec3  velocity{0.0f, 0.0f, 0.0f};
    float radius         = 16.0f;
    float restitution    = 0.35f;       // [0..1] bounce factor
    float friction       = 0.55f;       // Tangential impulse damping on impact
    float gravityZ       = -980.0f;
    float linearDrag     = 0.15f;
    float waterBuoyancy  = 1350.0f;
    float waterDrag      = 4.0f;
    bool  resting        = false;
    bool  inWater        = false;
};

struct WorldPhysicsStats {
    uint32_t heightfieldCount = 0u;
    uint32_t bvhCellCount     = 0u;
    uint32_t totalTriangles   = 0u;
    uint32_t totalBvhNodes    = 0u;
};

// Convenience builders from RAW terrain / ESM structures:
TerrainCellHeightfield MakeHeightfieldFromTerrainChunk(const TerrainChunkData& chunk,
                                                       float waterLevelZ = -14000.0f,
                                                       uint32_t materialTag = 0u);
TerrainCellHeightfield MakeHeightfieldFromEsmCell(const EsmLoadedCell& cell,
                                                  float defaultWaterHeightZ = -14000.0f,
                                                  uint32_t materialTag = 0u);

class RawWorldPhysics {
public:
    void Clear();

    void  SetDefaultWaterLevel(float waterLevelZ) noexcept { defaultWaterLevelZ_ = waterLevelZ; }
    float DefaultWaterLevel() const noexcept { return defaultWaterLevelZ_; }

    // Heightfield terrain registration & exact bilinear sampling:
    bool AddTerrainHeightfield(const TerrainCellHeightfield& hf);
    bool RemoveTerrainHeightfield(uint64_t cellKey);

    bool SampleTerrainHeight(float x, float y,
                             float* outZ,
                             Vec3* outNormal = nullptr,
                             float* outWaterLevelZ = nullptr,
                             uint64_t* outCellKey = nullptr) const;

    // Static prop triangle / MeshletChunk BVH collider management (keyed by cellKey):
    // Calling AddCellCollider multiple times with the same cellKey appends triangles & rebuilds that cell's SAH BVH.
    bool AddCellCollider(uint64_t cellKey, const ProceduralMesh& mesh,
                         uint32_t instanceId = 0u, uint32_t materialTag = 0u);
    bool AddCellCollider(uint64_t cellKey, const TriangleMeshCollider& mesh);
    bool AddCellColliders(uint64_t cellKey, const std::vector<TriangleMeshCollider>& meshes);

    // Removes static BVH mesh collider AND heightfield registered under `cellKey` (for RawTamrielWorldStreamer eviction).
    bool RemoveCellCollider(uint64_t cellKey);
    // Removes only the static BVH mesh collider for `cellKey`, leaving any heightfield intact.
    bool RemoveCellMeshCollider(uint64_t cellKey);

    // Exact Moller-Trumbore BVH + heightfield raycast:
    RayHit RayCast(Vec3 origin, Vec3 dir, float maxDist) const;

    // Sub-stepped capsule character controller with terrain + BVH depenetration, step-up, slope slide & water buoyancy:
    void StepCharacterCapsule(CharacterBody& body, const CharacterInput& input, float dt) const;

    // Sub-stepped dynamic rigid-body sphere solver against terrain + BVH triangles:
    void StepDynamicSphere(DynamicSphereBody& body, float dt,
                           bool hasWaterOverride = false, float waterLevelOverrideZ = -14000.0f) const;

    WorldPhysicsStats GetStats() const;

private:
    struct PhysicsTriangle {
        Vec3     v0{};
        Vec3     v1{};
        Vec3     v2{};
        Vec3     faceNormal{0.0f, 0.0f, 1.0f};
        Vec3     aabbMin{};
        Vec3     aabbMax{};
        Vec3     centroid{};
        uint64_t cellKey     = 0ull;
        uint32_t instanceId  = 0u;
        uint32_t materialTag = 0u;
    };

    struct BvhNode {
        Vec3     aabbMin{};
        Vec3     aabbMax{};
        uint32_t leftFirst = 0u;  // If triCount > 0: first index in triIndices; else left child node index
        uint32_t triCount  = 0u;  // 0 = interior node (right child = leftFirst + 1); >0 = leaf
    };

    struct CellBvh {
        uint64_t                     cellKey = 0ull;
        Vec3                         worldMin{};
        Vec3                         worldMax{};
        std::vector<PhysicsTriangle> triangles;
        std::vector<uint32_t>        triIndices;
        std::vector<BvhNode>         nodes;
    };

    struct ContactInfo {
        bool     hit         = false;
        bool     hitTerrain  = false;
        float    penetration = 0.0f;
        Vec3     normal{0.0f, 0.0f, 1.0f};
        Vec3     point{};
        uint64_t cellKey     = 0ull;
    };

    static void RebuildCellBvh(CellBvh& cell);
    static void BuildBvhRecursive(CellBvh& cell, uint32_t nodeIdx, uint32_t first, uint32_t count);

    void RayCastCellBvh(const CellBvh& cell, Vec3 origin, Vec3 dir, Vec3 invDir,
                        float& bestT, RayHit& bestHit) const;
    void RayCastHeightfield(const TerrainCellHeightfield& hf, Vec3 origin, Vec3 dir, Vec3 invDir,
                            float& bestT, RayHit& bestHit) const;

    bool SampleHeightfieldLocal(const TerrainCellHeightfield& hf, float x, float y,
                                float* outZ, Vec3* outNormal) const;

    // Gathers all overlapping triangle + heightfield contacts for a Z-up capsule `[pos.z .. pos.z + height]`.
    void GatherCapsuleContacts(Vec3 feetPos, float radius, float height,
                               std::vector<ContactInfo>& outContacts) const;
    void GatherSphereContacts(Vec3 center, float radius,
                              std::vector<ContactInfo>& outContacts) const;

    void DepenetrateCapsule(Vec3& feetPos, Vec3& velocity, float radius, float height,
                            float cosMaxSlope, bool& outGrounded, bool& outSliding,
                            Vec3& outGroundNormal, uint64_t& outGroundCellKey,
                            uint32_t& outContactCount, bool& outHitWall) const;

    float                                                 defaultWaterLevelZ_ = -14000.0f;
    std::unordered_map<uint64_t, TerrainCellHeightfield>  heightfields_;
    std::unordered_map<uint64_t, CellBvh>                 cellBvhs_;
};

}  // namespace raw::worldgen
