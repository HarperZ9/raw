#pragma once
// RawNavMeshAi.h: Clean-Room 3D NavMesh Baker, Octile A* Funnel Pathfinder & Tactical Squad AI (`raw::worldgen`).
// Pure portable C++23 with zero Windows, D3D12, Recast, or Havok dependencies.
// Builds directly over `RawWorldPhysics` (heightfields + SAH AABB BVH `.NIF` props):
//   - 3D Walkability NavMesh baker: slope (<=48 deg), step continuity (<=44u), headroom (>=130u), water depth,
//     thin-wall horizontal clearance & strict non-corner-cutting 8-way adjacency bitmasks.
//   - Binary min-heap Octile A* pathfinder with node expansion budget + grid/ray-verified string-pull smoother.
//   - Tactical Squad Combat AI (`MeleeFlanker`, `RangedArcher`, `Spellcaster`, `RetreatingMorale`) with angular
//     flanking sectors, LOS-verified ranged standoff bands, morale retreat & local separation steering.
// Original work, MIT.

#include "RawWorldPhysics.h"

#include <cstdint>
#include <vector>

namespace raw::worldgen {

enum class NavCellTag : uint8_t {
    Blocked      = 0,
    Walkable     = 1,
    SteepSlope   = 2,
    ShallowWater = 3,
};

struct NavBakeConfig {
    float originX               = 0.0f;
    float originY               = 0.0f;
    float sizeX                 = 4096.0f;
    float sizeY                 = 4096.0f;
    float cellSizeU             = 64.0f;    // Grid resolution (typically 64u..96u)
    float rayStartZ             = 10000.0f;
    float rayMaxDist            = 30000.0f;
    float maxSlopeDeg           = 48.0f;    // Walkable slope cutoff (normal.z >= cos(48 deg))
    float steepSlopeDeg         = 32.0f;    // Above steepSlopeDeg -> SteepSlope traversal cost
    float maxStepHeightU        = 44.0f;    // Max vertical discontinuity between adjacent cells
    float agentHeadroomU        = 130.0f;   // Minimum overhead clearance via upward RayCast
    float maxShallowWaterDepthU = 68.0f;    // Deeper than this -> Blocked
    bool  checkHorizontalWalls  = true;     // Casts waist-height edge rays to catch thin walls/fences
};

struct NavCell {
    float      heightZ              = 0.0f;
    Vec3       normal{0.0f, 0.0f, 1.0f};
    NavCellTag tag                  = NavCellTag::Blocked;
    float      traversalCost        = 1.0e30f; // Walkable=1.0, SteepSlope=1.8, ShallowWater=2.4, Blocked=inf
    uint8_t    passableNeighborMask = 0u;      // 8-way bitmask (0:E, 1:NE, 2:N, 3:NW, 4:W, 5:SW, 6:S, 7:SE)
};

struct NavMeshStats {
    uint32_t gridW             = 0u;
    uint32_t gridH             = 0u;
    uint32_t totalCells        = 0u;
    uint32_t walkableCells     = 0u;
    uint32_t steepCells        = 0u;
    uint32_t shallowWaterCells = 0u;
    uint32_t blockedCells      = 0u;
    uint32_t totalLinks        = 0u;
    double   bakeTimeMs        = 0.0;
};

struct NavWaypoint {
    Vec3       position{};
    NavCellTag tag = NavCellTag::Walkable;
};

struct NavPathResult {
    bool                     found         = false;
    std::vector<NavWaypoint> waypoints;
    uint32_t                 rawGridNodes  = 0u;
    uint32_t                 expandedNodes = 0u;
    float                    totalLengthU  = 0.0f;
    double                   searchTimeMs  = 0.0;
};

class RawNavMesh {
public:
    void Clear();
    bool IsBaked() const noexcept { return gridW_ > 0u && gridH_ > 0u && !cells_.empty(); }

    bool BakeFromPhysics(const RawWorldPhysics& physics, const NavBakeConfig& cfg);

    // Coordinate & cell queries:
    bool WorldToGrid(float worldX, float worldY, int32_t* outGx, int32_t* outGy) const;
    Vec3 GridToWorldCenter(int32_t gx, int32_t gy) const;
    const NavCell* GetCell(int32_t gx, int32_t gy) const;

    // Finds the closest walkable grid cell near `worldPos` (within `searchRadiusCells`), preferring matching Z.
    bool FindNearestWalkableCell(Vec3 worldPos, int32_t searchRadiusCells,
                                 int32_t* outGx, int32_t* outGy) const;

    // Fast binary min-heap Octile A* with non-corner-cutting adjacency + grid/ray-verified string-pull smoother:
    NavPathResult FindPath(Vec3 startWorld, Vec3 goalWorld,
                           const RawWorldPhysics* physicsOpt = nullptr,
                           uint32_t maxExpandedNodes = 16384u) const;

    const NavBakeConfig& Config() const noexcept { return cfg_; }
    NavMeshStats         GetStats() const noexcept { return stats_; }

private:
    bool IsSegmentClearOnGrid(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                              const RawWorldPhysics* physicsOpt) const;

    NavBakeConfig        cfg_{};
    uint32_t             gridW_ = 0u;
    uint32_t             gridH_ = 0u;
    std::vector<NavCell> cells_;
    NavMeshStats         stats_{};
};

// ============================================================================
// Tactical Squad & Combat AI Steering
// ============================================================================

enum class TacticalArchetype : uint8_t {
    MeleeFlanker     = 0,
    RangedArcher     = 1,
    Spellcaster      = 2,
    RetreatingMorale = 3,
};

struct SquadAgent {
    uint32_t          id                 = 0u;
    TacticalArchetype archetype          = TacticalArchetype::MeleeFlanker;
    Vec3              position{};
    Vec3              velocity{};
    Vec3              facing{1.0f, 0.0f, 0.0f};
    float             hpRatio            = 1.0f;    // [0..1]; < 0.25 triggers RetreatingMorale
    float             moveSpeedU         = 260.0f;
    float             personalRadiusU    = 36.0f;
    float             preferredRangeMinU = 550.0f;  // Ranged/caster standoff band min
    float             preferredRangeMaxU = 1050.0f; // Ranged/caster standoff band max
    float             flankRingRadiusU   = 135.0f;  // Melee flanking engagement radius
};

struct ThreatTarget {
    Vec3 position{};
    Vec3 facing{1.0f, 0.0f, 0.0f}; // Unit XY facing direction of threat
};

struct TacticalSteeringOutput {
    uint32_t          agentId               = 0u;
    TacticalArchetype effectiveArchetype    = TacticalArchetype::MeleeFlanker;
    Vec3              desiredMoveDir{};
    Vec3              desiredPosition{};
    float             desiredSpeedU         = 0.0f;
    bool              hasLineOfSight        = false;
    bool              repositioningForLos   = false;
    float             assignedFlankAngleDeg = 0.0f;
};

// Evaluates tactical flanking, ranged standoff + LOS verification, morale retreat, and local separation
// for an entire combat squad, integrating agent positions over `dt` (seconds) cleanly over `physics`.
std::vector<TacticalSteeringOutput> StepSquadTactics(std::vector<SquadAgent>& agents,
                                                     const ThreatTarget& threat,
                                                     const RawWorldPhysics& physics,
                                                     const RawNavMesh* navMeshOpt,
                                                     float dt);

}  // namespace raw::worldgen
