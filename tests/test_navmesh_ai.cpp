// test_navmesh_ai.cpp: Verification Suite for `raw::worldgen::RawNavMesh` & Tactical Squad AI.
// Exports `int raw::worldgen::RunNavMeshAiTests(void (*checkFn)(bool, const char*) = nullptr)`
// and supports `#ifdef RAW_NAVMESH_AI_TEST_STANDALONE` standalone execution.
// Original work, MIT.

#include "RawNavMeshAi.h"
#include "RawEsmModelResolver.h"
#include "RawEsmWorldLoader.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace raw::worldgen {
namespace {

constexpr float kPiRadToDeg = 180.0f / 3.14159265358979323846f;

bool IsFinite3(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

TerrainCellHeightfield MakeFlatHeightfield(uint64_t key, float originX, float originY,
                                           float sizeX, float sizeY, float z = 0.0f) {
    TerrainCellHeightfield hf{};
    hf.cellKey     = key;
    hf.originX     = originX;
    hf.originY     = originY;
    hf.sizeX       = sizeX;
    hf.sizeY       = sizeY;
    hf.gridW       = 17u;
    hf.gridH       = 17u;
    hf.waterLevelZ = -14000.0f;
    hf.heights.assign(17u * 17u, z);
    return hf;
}

TriangleMeshCollider MakeWallBox(Vec3 bmin, Vec3 bmax, uint32_t instanceId) {
    TriangleMeshCollider m{};
    m.instanceId  = instanceId;
    m.materialTag = 2u;
    m.vertices = {
        {bmin.x, bmin.y, bmin.z}, {bmax.x, bmin.y, bmin.z},
        {bmax.x, bmax.y, bmin.z}, {bmin.x, bmax.y, bmin.z},
        {bmin.x, bmin.y, bmax.z}, {bmax.x, bmin.y, bmax.z},
        {bmax.x, bmax.y, bmax.z}, {bmin.x, bmax.y, bmax.z},
    };
    m.indices = {
        0, 2, 1,  0, 3, 2,
        4, 5, 6,  4, 6, 7,
        0, 1, 5,  0, 5, 4,
        3, 6, 2,  3, 7, 6,
        0, 4, 7,  0, 7, 3,
        1, 2, 6,  1, 6, 5,
    };
    return m;
}

}  // namespace

int RunNavMeshAiTests(void (*checkFn)(bool, const char*)) {
    int failures = 0;
    auto check = [&](bool cond, const char* name) {
        if (!cond) {
            ++failures;
        }
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s %s\n", cond ? "PASS" : "FAIL", name);
        }
    };

    // =========================================================================
    // Test 1: Planted-failure controls (unbaked empty navmesh & walled-off island)
    // =========================================================================
    {
        RawNavMesh emptyNav;
        const NavPathResult emptyRes = emptyNav.FindPath({100.0f, 100.0f, 0.0f}, {500.0f, 500.0f, 0.0f});

        // Completely walled-off enclosure around (512, 512):
        RawWorldPhysics islandWorld;
        islandWorld.AddTerrainHeightfield(MakeFlatHeightfield(1ull, 0.0f, 0.0f, 1024.0f, 1024.0f, 0.0f));
        islandWorld.AddCellCollider(2ull, MakeWallBox({320.0f, 320.0f, 0.0f}, {704.0f, 384.0f, 300.0f}, 1u));
        islandWorld.AddCellCollider(2ull, MakeWallBox({320.0f, 640.0f, 0.0f}, {704.0f, 704.0f, 300.0f}, 2u));
        islandWorld.AddCellCollider(2ull, MakeWallBox({320.0f, 320.0f, 0.0f}, {384.0f, 704.0f, 300.0f}, 3u));
        islandWorld.AddCellCollider(2ull, MakeWallBox({640.0f, 320.0f, 0.0f}, {704.0f, 704.0f, 300.0f}, 4u));

        RawNavMesh islandNav;
        NavBakeConfig cfg{};
        cfg.originX   = 0.0f;
        cfg.originY   = 0.0f;
        cfg.sizeX     = 1024.0f;
        cfg.sizeY     = 1024.0f;
        cfg.cellSizeU = 64.0f;
        const bool baked = islandNav.BakeFromPhysics(islandWorld, cfg);
        const NavPathResult blockedRes =
            islandNav.FindPath({512.0f, 512.0f, 0.0f}, {96.0f, 96.0f, 0.0f}, &islandWorld);

        check(!emptyRes.found && baked && !blockedRes.found && blockedRes.waypoints.empty(),
              "navmesh_ai_planted_failure_controls");
    }

    // =========================================================================
    // Test 2: Analytical U-shaped wall maze A* + String-Pull Smoother
    // =========================================================================
    {
        RawWorldPhysics mazeWorld;
        mazeWorld.AddTerrainHeightfield(MakeFlatHeightfield(1ull, 0.0f, 0.0f, 2048.0f, 2048.0f, 0.0f));
        // U-shaped wall opening toward -X:
        // Back wall at X in [1152..1280], Y in [512..1536]
        // Top arm at X in [640..1280], Y in [1408..1536]
        // Bottom arm at X in [640..1280], Y in [512..640]
        mazeWorld.AddCellCollider(2ull, MakeWallBox({1152.0f, 512.0f, 0.0f}, {1280.0f, 1536.0f, 320.0f}, 10u));
        mazeWorld.AddCellCollider(2ull, MakeWallBox({640.0f, 1408.0f, 0.0f}, {1280.0f, 1536.0f, 320.0f}, 11u));
        mazeWorld.AddCellCollider(2ull, MakeWallBox({640.0f, 512.0f, 0.0f}, {1280.0f, 640.0f, 320.0f}, 12u));

        RawNavMesh mazeNav;
        NavBakeConfig cfg{};
        cfg.originX   = 0.0f;
        cfg.originY   = 0.0f;
        cfg.sizeX     = 2048.0f;
        cfg.sizeY     = 2048.0f;
        cfg.cellSizeU = 64.0f;
        const bool baked = mazeNav.BakeFromPhysics(mazeWorld, cfg);

        // Start deep inside the U-pocket (960, 1024), goal on the far right side of the back wall (1600, 1024):
        const Vec3 start{960.0f, 1024.0f, 0.0f};
        const Vec3 goal{1600.0f, 1024.0f, 0.0f};
        const NavPathResult path = mazeNav.FindPath(start, goal, &mazeWorld);

        // Verify zero wall clipping on every smoothed segment:
        bool zeroClip = path.found && path.waypoints.size() >= 3u;
        bool wentAroundU = false;
        for (size_t i = 0u; i + 1u < path.waypoints.size(); ++i) {
            const Vec3 a = path.waypoints[i].position;
            const Vec3 b = path.waypoints[i + 1u].position;
            if (!IsFinite3(a) || !IsFinite3(b)) {
                zeroClip = false;
                break;
            }
            if (a.x < 640.0f || b.x < 640.0f) {
                wentAroundU = true; // Properly exited the U-mouth to go around!
            }
            const Vec3 p0{a.x, a.y, a.z + 56.0f};
            const Vec3 p1{b.x, b.y, b.z + 56.0f};
            const Vec3 d = p1 - p0;
            const float len = Length(d);
            if (len > 1.0f) {
                const RayHit h = mazeWorld.RayCast(p0, d / len, len);
                if (h.hit && !h.hitTerrain) {
                    zeroClip = false;
                }
            }
        }

        // String-pull must significantly reduce redundant collinear nodes from raw A* grid chain:
        const bool smoothedOk = (path.rawGridNodes > path.waypoints.size() * 2u) &&
                                (path.totalLengthU > 1400.0f);

        std::printf("  [U-Maze A*] rawNodes=%u smoothedWaypoints=%zu len=%.1fu time=%.3fms\n",
                    path.rawGridNodes, path.waypoints.size(), path.totalLengthU, path.searchTimeMs);

        check(baked && path.found && zeroClip && wentAroundU && smoothedOk,
              "navmesh_ai_u_maze_astar_and_string_pull");
    }

    // =========================================================================
    // Test 3: Tactical Squad Steering (4 MeleeFlankers, RangedArcher LOS, RetreatingMorale)
    // =========================================================================
    {
        RawWorldPhysics arena;
        arena.AddTerrainHeightfield(MakeFlatHeightfield(1ull, -2048.0f, -2048.0f, 4096.0f, 4096.0f, 0.0f));
        // Blocking pillar between archer at (-800, 0) and threat at (0, 0):
        arena.AddCellCollider(2ull, MakeWallBox({-450.0f, -120.0f, 0.0f}, {-350.0f, 120.0f, 300.0f}, 20u));

        ThreatTarget threat{};
        threat.position = {0.0f, 0.0f, 0.0f};
        threat.facing   = {1.0f, 0.0f, 0.0f};

        std::vector<SquadAgent> squad;
        // 4 MeleeFlankers starting clustered in front of threat:
        for (uint32_t i = 0u; i < 4u; ++i) {
            SquadAgent a{};
            a.id        = i + 1u;
            a.archetype = TacticalArchetype::MeleeFlanker;
            a.position  = {420.0f + static_cast<float>(i) * 15.0f, -30.0f + static_cast<float>(i) * 20.0f, 0.0f};
            a.hpRatio   = 1.0f;
            squad.push_back(a);
        }
        // 1 RangedArcher initially occluded behind pillar at (-800, 0):
        {
            SquadAgent archer{};
            archer.id        = 5u;
            archer.archetype = TacticalArchetype::RangedArcher;
            archer.position  = {-800.0f, 0.0f, 0.0f};
            archer.hpRatio   = 1.0f;
            squad.push_back(archer);
        }
        // 1 Injured RetreatingMorale agent (hpRatio = 0.15 < 0.25) close to threat at (200, 150):
        {
            SquadAgent wounded{};
            wounded.id        = 6u;
            wounded.archetype = TacticalArchetype::MeleeFlanker; // Auto-switches to RetreatingMorale due to low HP!
            wounded.position  = {200.0f, 150.0f, 0.0f};
            wounded.hpRatio   = 0.15f;
            squad.push_back(wounded);
        }

        const float initialWoundedDist = Length(Vec2{squad[5].position.x, squad[5].position.y});
        std::vector<TacticalSteeringOutput> lastOut;
        bool archerDetectedBlockedLos = false;

        for (int step = 0; step < 120; ++step) { // 2.0 seconds @ 60 Hz
            lastOut = StepSquadTactics(squad, threat, arena, nullptr, 1.0f / 60.0f);
            if (step == 0 && lastOut[4].repositioningForLos && !lastOut[4].hasLineOfSight) {
                archerDetectedBlockedLos = true;
            }
        }

        // Check angular spread of the 4 MeleeFlankers around threat (> 90 deg):
        float minAng =  360.0f;
        float maxAng = -360.0f;
        bool allFinite = true;
        bool noOverlap = true;
        for (size_t i = 0u; i < 4u; ++i) {
            if (!IsFinite3(squad[i].position)) allFinite = false;
            const float angDeg = std::atan2(squad[i].position.y - threat.position.y,
                                            squad[i].position.x - threat.position.x) * kPiRadToDeg;
            minAng = std::min(minAng, angDeg);
            maxAng = std::max(maxAng, angDeg);
            for (size_t j = i + 1u; j < 4u; ++j) {
                const float d = Length(Vec2{squad[i].position.x - squad[j].position.x,
                                            squad[i].position.y - squad[j].position.y});
                if (d < 40.0f) noOverlap = false;
            }
        }
        const float flankSpreadDeg = maxAng - minAng;

        // Check RangedArcher repositioned to clear LOS and stayed in standoff band [550..1050u]:
        const float archerDist = Length(Vec2{squad[4].position.x, squad[4].position.y});
        const bool archerOk = archerDetectedBlockedLos && lastOut[4].hasLineOfSight &&
                              archerDist >= 520.0f && archerDist <= 1080.0f && IsFinite3(squad[4].position);

        // Check injured agent switched to RetreatingMorale and substantially increased distance from threat:
        const float finalWoundedDist = Length(Vec2{squad[5].position.x, squad[5].position.y});
        const bool retreatOk = (lastOut[5].effectiveArchetype == TacticalArchetype::RetreatingMorale) &&
                               (finalWoundedDist > initialWoundedDist + 350.0f) && IsFinite3(squad[5].position);

        std::printf("  [Squad AI] flankSpread=%.1fdeg archerLOS=%d(dist=%.0fu) retreatDist=%.0f->%.0fu\n",
                    flankSpreadDeg, lastOut[4].hasLineOfSight ? 1 : 0, archerDist,
                    initialWoundedDist, finalWoundedDist);

        check(allFinite && noOverlap && flankSpreadDeg > 90.0f && archerOk && retreatOk,
              "navmesh_ai_tactical_squad_flanking_los_and_retreat");
    }

    // =========================================================================
    // Test 4: Live Real-Tamriel Riverwood (4, -12) NavMesh Bake & Pathfinding (< 5 ms)
    // =========================================================================
    const std::string skyrimEsm = "H:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data/Skyrim.esm";
    const std::string meshesBsa = "H:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data/Skyrim - Meshes0.bsa";
    if (std::filesystem::exists(skyrimEsm) && std::filesystem::exists(meshesBsa)) {
        ReconstructedCellScene riverwood{};
        std::string err;
        const bool okScene = ReconstructTamrielCellScene(skyrimEsm, meshesBsa, 4, -12, 48u, &riverwood, &err);
        ProceduralMesh bakedProps{};
        const bool okProps = okScene && BakeReconstructedCellPropsToWorldMesh(riverwood, 126u, &bakedProps, &err);

        RawWorldPhysics rwPhysics;
        const uint64_t rwKey = PackCellKey(4, -12);
        const bool okHf  = okScene && rwPhysics.AddTerrainHeightfield(
            MakeHeightfieldFromTerrainChunk(riverwood.terrainChunk, riverwood.waterHeightZ, 1u));
        const bool okBvh = okProps && rwPhysics.AddCellCollider(rwKey, bakedProps, 1001u, 2u);

        RawNavMesh rwNav;
        NavBakeConfig cfg{};
        cfg.originX   = riverwood.terrainChunk.originX;
        cfg.originY   = riverwood.terrainChunk.originY;
        cfg.sizeX     = riverwood.terrainChunk.chunkSizeU;
        cfg.sizeY     = riverwood.terrainChunk.chunkSizeU;
        cfg.cellSizeU = 64.0f;
        const bool okBake = okHf && okBvh && rwNav.BakeFromPhysics(rwPhysics, cfg);
        const NavMeshStats st = rwNav.GetStats();

        // Find two well-separated walkable ground cells across Riverwood village and compute an A* path:
        Vec3 rwStart{};
        Vec3 rwGoal{};
        bool pickedStart = false;
        bool pickedGoal  = false;
        // Scan southwest quadrant for a connected walkable start cell and northeast quadrant for a goal cell:
        for (int32_t gy = 6; gy < 24 && !pickedStart; ++gy) {
            for (int32_t gx = 6; gx < 28 && !pickedStart; ++gx) {
                const NavCell* c = rwNav.GetCell(gx, gy);
                if (c && c->tag == NavCellTag::Walkable && c->passableNeighborMask != 0u) {
                    rwStart     = rwNav.GridToWorldCenter(gx, gy);
                    pickedStart = true;
                }
            }
        }
        for (int32_t gy = 56; gy >= 36 && !pickedGoal; --gy) {
            for (int32_t gx = 56; gx >= 34 && !pickedGoal; --gx) {
                const NavCell* c = rwNav.GetCell(gx, gy);
                if (c && c->tag == NavCellTag::Walkable && c->passableNeighborMask != 0u) {
                    const Vec3 cand = rwNav.GridToWorldCenter(gx, gy);
                    const NavPathResult probe = rwNav.FindPath(rwStart, cand, &rwPhysics);
                    if (probe.found && probe.totalLengthU > 1200.0f) {
                        rwGoal     = cand;
                        pickedGoal = true;
                    }
                }
            }
        }

        NavPathResult rwPath{};
        if (pickedStart && pickedGoal) {
            rwPath = rwNav.FindPath(rwStart, rwGoal, &rwPhysics);
        }

        bool allWpFinite = rwPath.found && rwPath.waypoints.size() >= 2u;
        for (const auto& wp : rwPath.waypoints) {
            if (!IsFinite3(wp.position)) allWpFinite = false;
        }

        std::printf(
            "  [Riverwood (4,-12) NavMesh] grid=%ux%u walkable=%u steep=%u water=%u blocked=%u links=%u "
            "bake=%.2fms | path: raw=%u smooth=%zu len=%.1fu search=%.3fms\n",
            st.gridW, st.gridH, st.walkableCells, st.steepCells, st.shallowWaterCells, st.blockedCells,
            st.totalLinks, st.bakeTimeMs, rwPath.rawGridNodes, rwPath.waypoints.size(),
            rwPath.totalLengthU, rwPath.searchTimeMs);

        check(okBake && st.walkableCells > 500u && st.blockedCells > 100u && st.totalLinks > 2000u &&
                  pickedStart && pickedGoal && rwPath.found && allWpFinite &&
                  rwPath.totalLengthU > 1200.0f && rwPath.searchTimeMs < 5.0,
              "navmesh_ai_live_tamriel_riverwood_bake_and_astar");
    } else {
        std::printf("SKIP navmesh_ai_live_tamriel_riverwood_bake_and_astar (Skyrim SE Data not present)\n");
    }

    return failures;
}

}  // namespace raw::worldgen

#ifdef RAW_NAVMESH_AI_TEST_STANDALONE
int main() {
    const int failures = raw::worldgen::RunNavMeshAiTests(nullptr);
    std::printf("RawNavMeshAi standalone test summary: %s (%d failures)\n",
                failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
#endif
