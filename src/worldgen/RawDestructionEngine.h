#pragma once
// RawDestructionEngine.h: First-Principles 3D Voronoi-Plane CSG Mesh Fracture & Rigid-Body Destruction Engine (`raw::worldgen`).
// Standalone clean-room MIT C++20/23 framework with zero external/Skyrim/D3D12 header dependencies.
// Features:
//   - 5 parametric destructible archetypes (NordicStonePillar, GlacialIceBoulder, TimberBarricade,
//     BasaltColumn, CastleBattlement) + custom convex polyhedron support
//   - Impact-clustered 3D Voronoi cell generation with exact double-precision half-space plane clipping
//     (Sutherland-Hodgman polygon clip + coplanar cap reconstruction with consistent outward CCW winding)
//   - Distinct exterior weathered PBR splat vs newly exposed rough interior fracture face splat
//   - Exact signed-tetrahedron volume, mass, centroid, and 3x3 inertia tensor (strict volume conservation)
//   - Deterministic semi-implicit rigid-body debris simulator (impact burst, gravity, ground-plane
//     impulse + Coulomb friction, angular damping, automatic sleep settling)
//   - Hardware-validated 64v/126t DX12 AS+MS GPU meshlet export (ProceduralMesh + .glb/.obj)
// Original work, MIT.

#include "RawMeshPipeline.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace raw::worldgen {

enum class DestructibleArchetype : uint8_t {
    NordicStonePillar = 0,
    GlacialIceBoulder = 1,
    TimberBarricade   = 2,
    BasaltColumn      = 3,
    CastleBattlement  = 4,
    Count             = 5,
};

struct Mat3 {
    float m[3][3] = {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    };
};

struct Quat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

struct PolyFace {
    std::vector<Vec3> vertices;           // Planar convex polygon, CCW viewed from outside (+normal)
    Vec3              normal{0, 0, 1};    // Unit outward surface normal
    bool              isInterior = false; // false = original exterior weathered face, true = Voronoi fracture cut
};

struct ConvexPolyhedron {
    std::vector<PolyFace> faces;
    MaterialSplat8        exteriorMaterial{};
    MaterialSplat8        interiorMaterial{};
    float                 densityKgM3 = 2500.0f;
    float                 restitution = 0.25f;
    float                 friction    = 0.65f;
    std::string           name;
};

struct MassProperties {
    float volumeM3      = 0.0f;
    float massKg        = 0.0f;
    Vec3  centroid{};
    Mat3  inertiaTensor{}; // 3x3 symmetric positive-definite inertia tensor about centroid (kg*m^2)
    float boundingRadius = 0.0f;
};

struct FractureConfig {
    uint32_t seed               = 202642u;
    uint32_t shardCount         = 18u;     // Target number of Voronoi fracture cells (clamped 2..96)
    Vec3     impactPoint{};                // World/local impact point for fine splinter clustering
    bool     useImpactClustering = true;   // Cluster denser Voronoi sites near impactPoint
    float    clusterFocus01     = 0.65f;   // 0 = uniform across AABB, 1 = strongly concentrated near impact
    float    minShardVolumeM3   = 1.0e-5f; // Discard microscopic numerical dust below this volume
};

struct FracturedShard {
    uint32_t         shardId = 0u;
    ConvexPolyhedron localPoly;            // Vertices centered at local origin (0,0,0) = center of mass
    std::vector<Vec3> uniqueLocalVerts;    // Deduplicated convex hull vertices in local COM space
    MassProperties   massProps;            // centroid stores original world-space COM before centering
    uint32_t         exteriorFaceCount = 0u;
    uint32_t         interiorFaceCount = 0u;
};

struct FractureResult {
    bool                        ok              = false;
    float                       originalVolume  = 0.0f;
    float                       totalShardVolume = 0.0f;
    float                       volumeErrorRel  = 0.0f; // |totalShardVolume - originalVolume| / originalVolume
    std::vector<FracturedShard> shards;
};

struct DebrisBody {
    uint32_t       shardId = 0u;
    FracturedShard shard;
    Vec3           position{};             // World-space center of mass (Z-up)
    Quat           orientation{};          // Unit quaternion local -> world
    Vec3           linearVel{};            // m/s
    Vec3           angularVel{};           // rad/s (world-space)
    float          invMass = 0.0f;
    Mat3           invInertiaLocal{};
    bool           asleep     = false;
    float          sleepTimer = 0.0f;
    uint32_t       contactCount = 0u;
};

struct DestructionSimConfig {
    float    gravityZ           = 9.80665f; // Downward acceleration along -Z (m/s^2)
    float    groundZ            = 0.0f;     // Horizontal ground plane elevation (m)
    float    linearDamping      = 0.22f;    // Exponential air drag (1/s)
    float    angularDamping     = 0.55f;    // Exponential rotational drag (1/s)
    float    groundAngularDamp  = 6.5f;     // Extra rolling damping during ground contact (1/s)
    float    sleepLinSpeed      = 0.14f;    // m/s threshold for automatic sleep settling
    float    sleepAngSpeed      = 0.25f;    // rad/s threshold for automatic sleep settling
    float    sleepHoldSeconds   = 0.20f;    // Continuous quiet duration before sleeping
    float    maxLinearSpeed     = 45.0f;    // Safety clamp (m/s)
    float    maxAngularSpeed    = 35.0f;    // Safety clamp (rad/s)
    uint32_t substeps           = 2u;       // Substeps per Step(dt) call
};

struct DestructionImpact {
    Vec3  point{};                     // World-space impact origin
    Vec3  direction{1.0f, 0.0f, 0.2f}; // Strike direction (normalized internally)
    float impulseNs       = 180.0f;    // Directional strike impulse magnitude (N*s)
    float radialBurstNs   = 95.0f;     // Outward Voronoi fracture expansion burst (N*s)
    float falloffRadiusM  = 1.25f;     // Distance attenuation scale (m)
    float upwardBias01    = 0.25f;     // Subtle upward lift on radial burst for dramatic debris arcs
};

struct DestructionSimStats {
    uint32_t totalBodies     = 0u;
    uint32_t awakeBodies     = 0u;
    uint32_t asleepBodies    = 0u;
    float    minVertexZ      = 0.0f;
    float    maxSpeed        = 0.0f;
    float    kineticEnergyJ  = 0.0f;
    float    elapsedSeconds  = 0.0f;
    uint32_t stepCount       = 0u;
    bool     allFinite       = true;
};

// Returns human-readable name for a destructible archetype.
const char* GetDestructibleArchetypeName(DestructibleArchetype archetype);

// Builds a watertight convex polyhedron for one of RAW's 5 destructible archetypes, resting on baseZ.
ConvexPolyhedron BuildDestructibleArchetype(DestructibleArchetype archetype,
                                            float scale = 1.0f,
                                            Vec3 baseCenter = {0.0f, 0.0f, 0.0f});

// Computes exact signed-tetrahedron volume, mass, centroid, 3x3 inertia tensor (about centroid),
// and bounding radius for any closed convex polyhedron.
MassProperties ComputeMassProperties(const ConvexPolyhedron& poly);

// Clips a convex polyhedron against the half-space Dot(x - planePoint, planeNormalOut) <= 0.
// Preserves original face material flags and generates a watertight coplanar interior cap face.
// Returns false if the polyhedron is completely clipped away.
bool ClipConvexPolyhedronByPlane(const ConvexPolyhedron& inPoly,
                                 Vec3 planePoint,
                                 Vec3 planeNormalOut,
                                 bool markCapInterior,
                                 ConvexPolyhedron* outPoly);

// Exact 3D Voronoi-plane CSG fracture of any convex polyhedron into watertight shards with
// conserved volume, exact mass properties, and exterior/interior material separation.
FractureResult FractureConvexBody(const ConvexPolyhedron& body, const FractureConfig& cfg);

// Deterministic semi-implicit rigid-body debris simulation for fractured Voronoi shards.
class DestructionSimulation {
public:
    explicit DestructionSimulation(const DestructionSimConfig& cfg = {});

    void Reset();
    void SetConfig(const DestructionSimConfig& cfg);
    [[nodiscard]] const DestructionSimConfig& GetConfig() const noexcept { return m_cfg; }

    // Initializes rigid bodies directly from a FractureResult and optional initial impact impulse.
    bool InitializeFromFracture(const FractureResult& fracture, const DestructionImpact* initialImpact = nullptr);

    // Convenience helper: builds an archetype, fractures it at impact.point, and applies impact impulse.
    bool FractureAndInitialize(DestructibleArchetype archetype,
                               float scale,
                               Vec3 baseCenter,
                               const FractureConfig& fractureCfg,
                               const DestructionImpact& impact,
                               FractureResult* outFracture = nullptr);

    // Applies an instantaneous linear + angular impulse (r x J) to all shards from an impact point.
    void ApplyImpactImpulse(const DestructionImpact& impact);

    // Advances deterministic rigid-body dynamics by dt seconds (clamped & substepped).
    void Step(float dtSeconds);

    [[nodiscard]] const std::vector<DebrisBody>& GetBodies() const noexcept { return m_bodies; }
    [[nodiscard]] DestructionSimStats GetStats() const noexcept;

    // Builds a hardware-validated 64v/126t GPU meshlet ProceduralMesh of all active debris shards
    // in their current simulated world-space poses.
    bool BuildMeshFromDestructionState(uint32_t maxMeshletTris,
                                       ProceduralMesh* outMesh,
                                       const std::string& meshName = "destruction_debris") const;

    // Builds a single shard's ProceduralMesh in its current simulated world-space pose.
    bool BuildMeshFromShard(size_t bodyIndex,
                            uint32_t maxMeshletTris,
                            ProceduralMesh* outMesh) const;

private:
    void StepSubstep(float h);

    DestructionSimConfig    m_cfg{};
    std::vector<DebrisBody> m_bodies;
    float                   m_elapsedSeconds = 0.0f;
    uint32_t                m_stepCount      = 0u;
};

// Standalone helper: builds a ProceduralMesh directly from any FractureResult (at rest or exploded).
bool BuildMeshFromFractureResult(const FractureResult& fracture,
                                 float explodeFactor,
                                 uint32_t maxMeshletTris,
                                 ProceduralMesh* outMesh,
                                 const std::string& meshName = "fractured_body");

}  // namespace raw::worldgen
