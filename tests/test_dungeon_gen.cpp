// test_dungeon_gen.cpp: Verification Suite for `raw::worldgen::RawDungeonGen`.
// Exports `int raw::worldgen::RunDungeonGenTests(void (*checkFn)(bool, const char*) = nullptr)`
// and supports `#ifdef RAW_DUNGEON_GEN_TEST_STANDALONE` standalone execution.
// Original work, MIT.

#include "RawDungeonGen.h"
#include "RawNavMeshAi.h"
#include "RawWorldPhysics.h"
#include "../pipeline/RawAssetValidator.h"

#include <cmath>
#include <cstdio>

namespace raw::worldgen {
namespace {

bool IsFinite3(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

}  // namespace

int RunDungeonGenTests(void (*checkFn)(bool, const char*)) {
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
    // Test 1: Planted-failure controls (zero rooms safely clamped + same-seed determinism)
    // =========================================================================
    {
        DungeonGenConfig zeroCfg{};
        zeroCfg.archetype = DungeonArchetype::NordicBarrow;
        zeroCfg.seed      = 12345u;
        zeroCfg.roomCount = 0u; // Must clamp safely to >= 6

        GeneratedDungeon d1{}, d2{};
        const bool ok1 = RawDungeonGen::GenerateDungeon(zeroCfg, d1);
        const bool ok2 = RawDungeonGen::GenerateDungeon(zeroCfg, d2);

        const bool deterministic =
            ok1 && ok2 &&
            (d1.rooms.size() >= 6u) &&
            (d1.rooms.size() == d2.rooms.size()) &&
            (d1.mesh.vertices.size() == d2.mesh.vertices.size()) &&
            (d1.mesh.meshlets.size() == d2.mesh.meshlets.size()) &&
            (d1.bossPos[0] == d2.bossPos[0] && d1.bossPos[1] == d2.bossPos[1] && d1.bossPos[2] == d2.bossPos[2]);

        check(deterministic, "dungeon_gen_planted_failure_and_determinism");
    }

    // =========================================================================
    // Test 2: All 3 Archetypes — 100% BFS Connectivity, Meshlets >= 10, Lights >= 6, Strict Validation
    // =========================================================================
    {
        bool allArchetypesOk = true;
        const DungeonArchetype archetypes[3] = {
            DungeonArchetype::NordicBarrow,
            DungeonArchetype::GlacialIceCavern,
            DungeonArchetype::BasaltDwemerRuin,
        };

        for (int i = 0; i < 3; ++i) {
            DungeonGenConfig cfg{};
            cfg.archetype = archetypes[i];
            cfg.seed      = 5600u + static_cast<uint32_t>(i * 17);
            cfg.roomCount = 9u + static_cast<uint32_t>(i); // 9, 10, 11 rooms

            GeneratedDungeon d{};
            const bool generated = RawDungeonGen::GenerateDungeon(cfg, d);

            raw::pipeline::ValidationReport rep{};
            const bool strictValid = raw::pipeline::ValidateProceduralMeshStrict(d.mesh, &rep);

            const bool archOk = generated &&
                                (d.bfsReachableRooms == d.rooms.size()) &&
                                (d.mesh.meshlets.size() >= 10u) &&
                                (d.lights.size() >= 6u) &&
                                strictValid && (rep.TotalErrors() == 0u);

            std::printf(
                "  [%s] rooms=%zu bfs=%u corridors=%zu meshlets=%zu tris=%u lights=%zu valid=%d(err=%u)\n",
                GetDungeonArchetypeName(archetypes[i]), d.rooms.size(), d.bfsReachableRooms,
                d.corridors.size(), d.mesh.meshlets.size(), rep.triangleCount, d.lights.size(),
                strictValid ? 1 : 0, rep.TotalErrors());

            if (!archOk) allArchetypesOk = false;
        }

        check(allArchetypesOk, "dungeon_gen_all_3_archetypes_bfs_and_strict_meshlets");
    }

    // =========================================================================
    // Test 3: Live Physics + NavMesh End-to-End Proof (Entrance -> Boss A* Walkable Path!)
    // =========================================================================
    {
        DungeonGenConfig cfg{};
        cfg.archetype = DungeonArchetype::NordicBarrow;
        cfg.seed      = 202656u;
        cfg.roomCount = 8u;

        GeneratedDungeon barrow{};
        const bool generated = RawDungeonGen::GenerateDungeon(cfg, barrow);

        RawWorldPhysics phys;
        const bool addedMesh = generated && phys.AddCellCollider(PackCellKey(0, 0), barrow.mesh, 1u, 1u);

        RawNavMesh nav;
        NavBakeConfig ncfg{};
        ncfg.originX        = barrow.worldBoundsMin.x - 64.0f;
        ncfg.originY        = barrow.worldBoundsMin.y - 64.0f;
        ncfg.sizeX          = (barrow.worldBoundsMax.x - barrow.worldBoundsMin.x) + 128.0f;
        ncfg.sizeY          = (barrow.worldBoundsMax.y - barrow.worldBoundsMin.y) + 128.0f;
        ncfg.cellSizeU      = 64.0f;
        ncfg.rayStartZ      = barrow.recommendedNavRayStartZ; // Interior height below vaults & above floors!
        ncfg.rayMaxDist     = 2000.0f;
        ncfg.agentHeadroomU = 130.0f;
        const bool baked = addedMesh && nav.BakeFromPhysics(phys, ncfg);

        const Vec3 start{barrow.entrancePos[0], barrow.entrancePos[1], barrow.entrancePos[2]};
        const Vec3 goal{barrow.bossPos[0], barrow.bossPos[1], barrow.bossPos[2]};
        const NavPathResult path = baked ? nav.FindPath(start, goal, &phys, 32768u) : NavPathResult{};

        // Verify zero wall clipping & finite 3D coordinates along every smoothed segment:
        bool pathClean = path.found && path.waypoints.size() >= 2u;
        for (size_t i = 0u; i < path.waypoints.size(); ++i) {
            if (!IsFinite3(path.waypoints[i].position)) {
                pathClean = false;
                break;
            }
            if (i + 1u < path.waypoints.size()) {
                const Vec3 a = path.waypoints[i].position;
                const Vec3 b = path.waypoints[i + 1u].position;
                const Vec3 p0{a.x, a.y, a.z + 52.0f};
                const Vec3 p1{b.x, b.y, b.z + 52.0f};
                const Vec3 d = p1 - p0;
                const float len = Length(d);
                if (len > 1.0f) {
                    const RayHit h = phys.RayCast(p0, d / len, len - 2.0f);
                    if (h.hit) {
                        pathClean = false;
                    }
                }
            }
        }

        const NavMeshStats nst = nav.GetStats();
        std::printf(
            "  [Dungeon Physics+NavMesh A*] navWalkable=%u bake=%.2fms | Entrance->Boss: found=%d raw=%u smooth=%zu len=%.1fu search=%.3fms clean=%d\n",
            nst.walkableCells, nst.bakeTimeMs, path.found ? 1 : 0, path.rawGridNodes,
            path.waypoints.size(), path.totalLengthU, path.searchTimeMs, pathClean ? 1 : 0);

        check(generated && addedMesh && baked && pathClean && path.totalLengthU > 400.0f,
              "dungeon_gen_live_physics_navmesh_entrance_to_boss_astar");
    }

    return failures;
}

}  // namespace raw::worldgen

#ifdef RAW_DUNGEON_GEN_TEST_STANDALONE
int main() {
    const int failures = raw::worldgen::RunDungeonGenTests(nullptr);
    std::printf("RawDungeonGen standalone test summary: %s (%d failures)\n",
                failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
#endif
