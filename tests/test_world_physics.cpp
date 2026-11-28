// test_world_physics.cpp: Comprehensive Verification Suite for `raw::worldgen::RawWorldPhysics`.
// Exports `int raw::worldgen::RunWorldPhysicsTests(void (*checkFn)(bool, const char*) = nullptr)`
// and supports `#ifdef RAW_WORLD_PHYSICS_TEST_STANDALONE` standalone execution.
// Original work, MIT.

#include "RawWorldPhysics.h"
#include "RawEsmModelResolver.h"
#include "RawEsmWorldLoader.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace raw::worldgen {
namespace {

constexpr float kPiDegToRad = 3.14159265358979323846f / 180.0f;

bool IsFinite3(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

TerrainCellHeightfield MakeAnalyticalSlopeHeightfield(uint64_t cellKey,
                                                      float originX, float originY,
                                                      float sizeX, float sizeY,
                                                      uint32_t gridN,
                                                      float baseZ,
                                                      float slopeDegAlongX,
                                                      float waterLevelZ = -14000.0f) {
    TerrainCellHeightfield hf{};
    hf.cellKey     = cellKey;
    hf.originX     = originX;
    hf.originY     = originY;
    hf.sizeX       = sizeX;
    hf.sizeY       = sizeY;
    hf.gridW       = gridN;
    hf.gridH       = gridN;
    hf.waterLevelZ = waterLevelZ;
    hf.materialTag = 1u;
    hf.heights.resize(static_cast<size_t>(gridN) * gridN, baseZ);

    const float tanSlope = std::tan(slopeDegAlongX * kPiDegToRad);
    const float dx = sizeX / static_cast<float>(gridN - 1u);
    for (uint32_t iy = 0u; iy < gridN; ++iy) {
        for (uint32_t ix = 0u; ix < gridN; ++ix) {
            const float x = static_cast<float>(ix) * dx;
            hf.heights[static_cast<size_t>(iy) * gridN + ix] = baseZ + x * tanSlope;
        }
    }
    return hf;
}

TriangleMeshCollider MakeBoxCollider(Vec3 bmin, Vec3 bmax, uint32_t instanceId, uint32_t materialTag) {
    TriangleMeshCollider m{};
    m.instanceId  = instanceId;
    m.materialTag = materialTag;
    m.vertices = {
        {bmin.x, bmin.y, bmin.z}, // 0
        {bmax.x, bmin.y, bmin.z}, // 1
        {bmax.x, bmax.y, bmin.z}, // 2
        {bmin.x, bmax.y, bmin.z}, // 3
        {bmin.x, bmin.y, bmax.z}, // 4
        {bmax.x, bmin.y, bmax.z}, // 5
        {bmax.x, bmax.y, bmax.z}, // 6
        {bmin.x, bmax.y, bmax.z}, // 7
    };
    m.indices = {
        // Bottom (-Z)
        0, 2, 1,  0, 3, 2,
        // Top (+Z)
        4, 5, 6,  4, 6, 7,
        // Front (-Y)
        0, 1, 5,  0, 5, 4,
        // Back (+Y)
        3, 6, 2,  3, 7, 6,
        // Left (-X)
        0, 4, 7,  0, 7, 3,
        // Right (+X)
        1, 2, 6,  1, 6, 5,
    };
    return m;
}

TriangleMeshCollider MakeRampCollider(float x0, float x1, float y0, float y1,
                                      float z0, float z1,
                                      uint32_t instanceId, uint32_t materialTag) {
    TriangleMeshCollider m{};
    m.instanceId  = instanceId;
    m.materialTag = materialTag;
    m.vertices = {
        {x0, y0, z0},
        {x1, y0, z1},
        {x1, y1, z1},
        {x0, y1, z0},
    };
    m.indices = {0, 1, 2, 0, 2, 3};
    return m;
}

}  // namespace

int RunWorldPhysicsTests(void (*checkFn)(bool, const char*)) {
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
    // Test 1: Planted-failure controls (empty world miss, zero-length ray, zero dt no-op)
    // =========================================================================
    {
        RawWorldPhysics emptyWorld;
        const RayHit missEmpty = emptyWorld.RayCast({0.0f, 0.0f, 500.0f}, {0.0f, 0.0f, -1.0f}, 2000.0f);
        const RayHit zeroDir   = emptyWorld.RayCast({0.0f, 0.0f, 500.0f}, {0.0f, 0.0f, 0.0f}, 2000.0f);
        const RayHit negDist   = emptyWorld.RayCast({0.0f, 0.0f, 500.0f}, {0.0f, 0.0f, -1.0f}, -10.0f);
        const RayHit zeroDist  = emptyWorld.RayCast({0.0f, 0.0f, 500.0f}, {0.0f, 0.0f, -1.0f}, 0.0f);

        // Invalid heightfield / collider rejections:
        TerrainCellHeightfield badHf{};
        badHf.gridW = 1u;
        badHf.gridH = 1u;
        const bool badHfRejected = !emptyWorld.AddTerrainHeightfield(badHf);
        TriangleMeshCollider emptyMesh{};
        const bool emptyMeshRejected = !emptyWorld.AddCellCollider(1ull, emptyMesh);

        // Zero-dt character step must be an exact no-op:
        CharacterBody body{};
        body.position = {123.5f, -456.25f, 789.0f};
        body.velocity = {10.0f, -20.0f, -50.0f};
        CharacterInput in{};
        in.wishDirXY   = {1.0f, 0.0f};
        in.jumpPressed = true;
        emptyWorld.StepCharacterCapsule(body, in, 0.0f);
        emptyWorld.StepCharacterCapsule(body, in, -0.016f);
        const bool zeroDtNoOp =
            (body.position.x == 123.5f && body.position.y == -456.25f && body.position.z == 789.0f &&
             body.velocity.x == 10.0f && body.velocity.y == -20.0f && body.velocity.z == -50.0f);

        check(!missEmpty.hit && !zeroDir.hit && !negDist.hit && !zeroDist.hit &&
                  badHfRejected && emptyMeshRejected && zeroDtNoOp,
              "world_physics_planted_failure_controls");
    }

    // =========================================================================
    // Test 2: Analytical terrain grounding, 65-deg cliff slide, 25-deg hill climb
    // =========================================================================
    {
        // 2a. Drop capsule from +500u onto flat heightfield z = 100u -> lands within 1e-2u:
        RawWorldPhysics flatWorld;
        const bool addedFlat = flatWorld.AddTerrainHeightfield(
            MakeAnalyticalSlopeHeightfield(PackCellKey(0, 0), 0.0f, 0.0f, 4096.0f, 4096.0f, 33u, 100.0f, 0.0f));
        CharacterBody dropBody{};
        dropBody.position = {2048.0f, 2048.0f, 600.0f}; // +500u above terrain z=100
        CharacterInput idle{};
        for (int f = 0; f < 120; ++f) {
            flatWorld.StepCharacterCapsule(dropBody, idle, 1.0f / 60.0f);
        }
        const float flatErr = std::abs(dropBody.position.z - 100.0f);
        const bool flatLandOk = addedFlat && dropBody.grounded && !dropBody.sliding &&
                                flatErr < 1.0e-2f && std::abs(dropBody.velocity.z) < 1.0f;

        // 2b. Steep 65-degree cliff (> 48 deg maxSlopeDeg): capsule slides downhill, NOT grounded:
        RawWorldPhysics cliffWorld;
        cliffWorld.AddTerrainHeightfield(
            MakeAnalyticalSlopeHeightfield(PackCellKey(0, 0), 0.0f, 0.0f, 4096.0f, 4096.0f, 33u, 0.0f, 65.0f));
        float startCliffZ = 0.0f;
        Vec3  cliffN{};
        cliffWorld.SampleTerrainHeight(2500.0f, 2048.0f, &startCliffZ, &cliffN);
        CharacterBody cliffBody{};
        cliffBody.position = {2500.0f, 2048.0f, startCliffZ + 40.0f};
        bool observedSliding = false;
        for (int f = 0; f < 90; ++f) {
            cliffWorld.StepCharacterCapsule(cliffBody, idle, 1.0f / 60.0f);
            if (cliffBody.sliding) {
                observedSliding = true;
            }
        }
        const bool cliffSlideOk = observedSliding && !cliffBody.grounded &&
                                  (cliffBody.position.x < 2350.0f) &&
                                  (cliffBody.position.z < startCliffZ - 200.0f) &&
                                  IsFinite3(cliffBody.position);

        // 2c. Walkable 25-degree hill (<= 48 deg maxSlopeDeg): capsule walks smoothly uphill:
        RawWorldPhysics hillWorld;
        hillWorld.AddTerrainHeightfield(
            MakeAnalyticalSlopeHeightfield(PackCellKey(0, 0), 0.0f, 0.0f, 4096.0f, 4096.0f, 33u, 0.0f, 25.0f));
        float hillStartZ = 0.0f;
        hillWorld.SampleTerrainHeight(1000.0f, 2048.0f, &hillStartZ, nullptr);
        CharacterBody hillBody{};
        hillBody.position = {1000.0f, 2048.0f, hillStartZ + 5.0f};
        // Settle 10 frames then walk +X uphill for 90 frames:
        for (int f = 0; f < 10; ++f) {
            hillWorld.StepCharacterCapsule(hillBody, idle, 1.0f / 60.0f);
        }
        CharacterInput walkUphill{};
        walkUphill.wishDirXY = {1.0f, 0.0f};
        walkUphill.maxSpeed  = 280.0f;
        int groundedClimbFrames = 0;
        for (int f = 0; f < 90; ++f) {
            hillWorld.StepCharacterCapsule(hillBody, walkUphill, 1.0f / 60.0f);
            if (hillBody.grounded) {
                ++groundedClimbFrames;
            }
        }
        float hillEndZ = 0.0f;
        hillWorld.SampleTerrainHeight(hillBody.position.x, hillBody.position.y, &hillEndZ, nullptr);
        const bool hillClimbOk = (hillBody.position.x > 1300.0f) &&
                                 (hillBody.position.z > hillStartZ + 120.0f) &&
                                 (groundedClimbFrames >= 75) &&
                                 (hillBody.position.z >= hillEndZ - 0.5f) &&
                                 IsFinite3(hillBody.position);

        check(flatLandOk && cliffSlideOk && hillClimbOk,
              "world_physics_analytical_terrain_grounding_and_slopes");
    }

    // =========================================================================
    // Test 3: BVH raycast, capsule wall-slide, ramp/step-up, and RemoveCellCollider eviction
    // =========================================================================
    {
        RawWorldPhysics world;
        constexpr uint64_t kFloorKey = 10ull;
        constexpr uint64_t kWallKey  = 20ull;
        constexpr uint64_t kRampKey  = 30ull;
        constexpr uint64_t kStepKey  = 40ull;

        world.AddTerrainHeightfield(
            MakeAnalyticalSlopeHeightfield(kFloorKey, -1024.0f, -1024.0f, 4096.0f, 4096.0f, 17u, 0.0f, 0.0f));
        // Solid vertical wall box at X in [200..300], Y in [-400..400], Z in [0..300]:
        const bool wallAdded = world.AddCellCollider(
            kWallKey, MakeBoxCollider({200.0f, -400.0f, 0.0f}, {300.0f, 400.0f, 300.0f}, 77u, 5u));
        // Walkable ramp along +Y at X in [-300..-100], Y in [0..300], rising Z 0 -> 100:
        const bool rampAdded = world.AddCellCollider(
            kRampKey, MakeRampCollider(-300.0f, -100.0f, 0.0f, 300.0f, 0.0f, 100.0f, 88u, 6u));
        // Step-up curb box (height 28u <= 42u) at X in [-600..-400], Y in [-200..200], Z in [0..28]:
        const bool stepAdded = world.AddCellCollider(
            kStepKey, MakeBoxCollider({-600.0f, -200.0f, 0.0f}, {-400.0f, 200.0f, 28.0f}, 99u, 7u));

        // 3a. Exact BVH raycast hits wall front face at X = 200 with normal (-1, 0, 0):
        const RayHit hitWall = world.RayCast({0.0f, 0.0f, 100.0f}, {1.0f, 0.0f, 0.0f}, 1000.0f);
        const bool rayHitWallOk = hitWall.hit && !hitWall.hitTerrain &&
                                  std::abs(hitWall.distance - 200.0f) < 1.0e-2f &&
                                  hitWall.normal[0] < -0.99f &&
                                  hitWall.cellKey == kWallKey && hitWall.instanceId == 77u &&
                                  hitWall.materialTag == 5u;

        // 3b. Capsule wall-slide: push diagonally (+X, +Y) into wall -> stopped at X <= 200 - radius, slides +Y:
        CharacterBody wallBody{};
        wallBody.position = {140.0f, -50.0f, 0.0f};
        CharacterInput diagMove{};
        diagMove.wishDirXY = {0.7071f, 0.7071f};
        diagMove.maxSpeed  = 280.0f;
        for (int f = 0; f < 60; ++f) {
            world.StepCharacterCapsule(wallBody, diagMove, 1.0f / 60.0f);
        }
        const bool wallSlideOk = (wallBody.position.x <= 200.0f - wallBody.radius + 0.5f) &&
                                 (wallBody.position.y > 100.0f) &&
                                 wallBody.grounded && IsFinite3(wallBody.position);

        // 3c. Step-up test: walk -X onto 28u curb (<= 42u step-up limit):
        CharacterBody stepBody{};
        stepBody.position = {-340.0f, 0.0f, 0.0f};
        CharacterInput walkNegX{};
        walkNegX.wishDirXY = {-1.0f, 0.0f};
        walkNegX.maxSpeed  = 240.0f;
        for (int f = 0; f < 60; ++f) {
            world.StepCharacterCapsule(stepBody, walkNegX, 1.0f / 60.0f);
        }
        const bool stepUpOk = (stepBody.position.x < -440.0f) &&
                              (std::abs(stepBody.position.z - 28.0f) < 1.0f) &&
                              stepBody.grounded;

        // 3d. Dynamic RemoveCellCollider eviction proof: after removing kWallKey, same ray passes clean through!
        const bool removedWall = world.RemoveCellCollider(kWallKey);
        const RayHit afterEvict = world.RayCast({0.0f, 0.0f, 100.0f}, {1.0f, 0.0f, 0.0f}, 1000.0f);
        const bool evictionOk = removedWall && !afterEvict.hit;

        // 3e. Dynamic rigid-body sphere bounce against ramp/floor:
        DynamicSphereBody ball{};
        ball.position = {-200.0f, 150.0f, 300.0f};
        ball.radius   = 16.0f;
        for (int f = 0; f < 120; ++f) {
            world.StepDynamicSphere(ball, 1.0f / 60.0f);
        }
        const bool sphereOk = IsFinite3(ball.position) && (ball.position.z >= 15.0f);

        if (!(wallAdded && rampAdded && stepAdded && rayHitWallOk && wallSlideOk &&
              stepUpOk && evictionOk && sphereOk)) {
            std::printf("  [Test3 diag] wall=%d ramp=%d step=%d ray=%d slide=%d(%.1f,%.1f,g=%d) stepUp=%d(%.1f,%.1f,g=%d) evict=%d sphere=%d\n",
                        wallAdded, rampAdded, stepAdded, rayHitWallOk,
                        wallSlideOk, wallBody.position.x, wallBody.position.y, wallBody.grounded ? 1 : 0,
                        stepUpOk, stepBody.position.x, stepBody.position.z, stepBody.grounded ? 1 : 0,
                        evictionOk, sphereOk);
        }
        check(wallAdded && rampAdded && stepAdded && rayHitWallOk && wallSlideOk &&
                  stepUpOk && evictionOk && sphereOk,
              "world_physics_bvh_raycast_wall_slide_step_up_and_eviction");
    }

    // =========================================================================
    // Test 4: Water buoyancy & swim draft stabilization
    // =========================================================================
    {
        RawWorldPhysics lakeWorld;
        lakeWorld.AddTerrainHeightfield(
            MakeAnalyticalSlopeHeightfield(PackCellKey(0, 0), 0.0f, 0.0f, 4096.0f, 4096.0f, 17u,
                                           -600.0f, 0.0f, 0.0f)); // Deep lake bed at -600u, water surface at Z = 0

        CharacterBody swimmer{};
        swimmer.position = {1024.0f, 1024.0f, -350.0f}; // Deeply submerged below waterLevelZ = 0
        CharacterInput floatIdle{};
        for (int f = 0; f < 240; ++f) { // 4 seconds to rise & stabilize at swim draft
            lakeWorld.StepCharacterCapsule(swimmer, floatIdle, 1.0f / 60.0f);
        }
        // Expected equilibrium submersion = |gravityZ| / waterBuoyancy = 980 / 1450 ~= 0.676
        const float expectedSubmersion = std::abs(swimmer.gravityZ) / swimmer.waterBuoyancy;
        const bool buoyancyOk = swimmer.inWater && !swimmer.grounded &&
                                IsFinite3(swimmer.position) && IsFinite3(swimmer.velocity) &&
                                std::abs(swimmer.submersion01 - expectedSubmersion) < 0.08f &&
                                std::abs(swimmer.velocity.z) < 15.0f &&
                                (swimmer.position.z > -120.0f && swimmer.position.z < -40.0f);

        check(buoyancyOk, "world_physics_water_buoyancy_stabilization");
    }

    // =========================================================================
    // Test 5: Live Real-Tamriel Proof — Riverwood (4, -12) Terrain + Real .NIF Props
    // =========================================================================
    const std::string skyrimEsm  = "H:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data/Skyrim.esm";
    const std::string meshesBsa  = "H:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data/Skyrim - Meshes0.bsa";
    if (std::filesystem::exists(skyrimEsm) && std::filesystem::exists(meshesBsa)) {
        ReconstructedCellScene riverwood{};
        std::string err;
        const bool okScene = ReconstructTamrielCellScene(skyrimEsm, meshesBsa, 4, -12, 48u, &riverwood, &err);

        ProceduralMesh bakedProps{};
        const bool okProps = okScene && BakeReconstructedCellPropsToWorldMesh(riverwood, 126u, &bakedProps, &err);

        RawWorldPhysics tamrielPhysics;
        const uint64_t rwKey = PackCellKey(4, -12);
        const bool okHf = okScene && tamrielPhysics.AddTerrainHeightfield(
            MakeHeightfieldFromTerrainChunk(riverwood.terrainChunk, riverwood.waterHeightZ, 1u));
        const bool okBvh = okProps && tamrielPhysics.AddCellCollider(rwKey, bakedProps, 1001u, 2u);

        const WorldPhysicsStats stats = tamrielPhysics.GetStats();

        // Cast 16x16 vertical downward rays across Riverwood (4, -12) confirming real building/prop & terrain hits:
        uint32_t totalHits   = 0u;
        uint32_t propHits    = 0u;
        uint32_t terrainHits = 0u;
        bool allHitsValid    = true;
        const float originX  = riverwood.terrainChunk.originX;
        const float originY  = riverwood.terrainChunk.originY;
        const float cellSize = riverwood.terrainChunk.chunkSizeU;

        for (int gy = 0; gy < 16; ++gy) {
            for (int gx = 0; gx < 16; ++gx) {
                const float rx = originX + (static_cast<float>(gx) + 0.5f) * (cellSize / 16.0f);
                const float ry = originY + (static_cast<float>(gy) + 0.5f) * (cellSize / 16.0f);
                const RayHit h = tamrielPhysics.RayCast({rx, ry, 10000.0f}, {0.0f, 0.0f, -1.0f}, 30000.0f);
                if (h.hit) {
                    ++totalHits;
                    if (h.hitTerrain) ++terrainHits;
                    else              ++propHits;
                    const Vec3 n{h.normal[0], h.normal[1], h.normal[2]};
                    const Vec3 p{h.point[0], h.point[1], h.point[2]};
                    const float nLen = Length(n);
                    if (!IsFinite3(p) || !IsFinite3(n) || std::abs(nLen - 1.0f) > 1.0e-2f || h.distance <= 0.0f) {
                        allHitsValid = false;
                    }
                }
            }
        }

        // Step a character capsule 120 frames across Riverwood terrain (starting near cell center):
        float spawnTerrainZ = 0.0f;
        const float spawnX = originX + 2048.0f;
        const float spawnY = originY + 2048.0f;
        const bool sampledSpawn = tamrielPhysics.SampleTerrainHeight(spawnX, spawnY, &spawnTerrainZ, nullptr);

        CharacterBody hero{};
        hero.position = {spawnX, spawnY, spawnTerrainZ + 120.0f};
        CharacterInput walkNorthEast{};
        walkNorthEast.wishDirXY = {0.7071f, 0.7071f};
        walkNorthEast.maxSpeed  = 240.0f;

        bool allFramesFinite  = true;
        bool neverTunneled    = true;
        int  groundedFrames   = 0;
        for (int f = 0; f < 120; ++f) {
            tamrielPhysics.StepCharacterCapsule(hero, walkNorthEast, 1.0f / 60.0f);
            if (!IsFinite3(hero.position) || !IsFinite3(hero.velocity)) {
                allFramesFinite = false;
                break;
            }
            float curTerrainZ = 0.0f;
            if (tamrielPhysics.SampleTerrainHeight(hero.position.x, hero.position.y, &curTerrainZ, nullptr)) {
                if (hero.position.z < curTerrainZ - 0.5f) {
                    neverTunneled = false;
                }
            }
            if (hero.grounded) {
                ++groundedFrames;
            }
        }

        const float walkedDist = Length(Vec2{hero.position.x - spawnX, hero.position.y - spawnY});
        std::printf(
            "  [Riverwood (4,-12) Live Physics] tris=%u bvhNodes=%u rays=%u/256 (propHits=%u terrainHits=%u) "
            "groundedFrames=%d/120 walked=%.1fu finalZ=%.1f\n",
            stats.totalTriangles, stats.totalBvhNodes, totalHits, propHits, terrainHits,
            groundedFrames, walkedDist, hero.position.z);

        check(okScene && okProps && okHf && okBvh && stats.totalTriangles > 1000u &&
                  totalHits == 256u && propHits >= 10u && terrainHits >= 10u && allHitsValid &&
                  sampledSpawn && allFramesFinite && neverTunneled &&
                  groundedFrames >= 60 && walkedDist > 100.0f,
              "world_physics_live_tamriel_riverwood_raycast_and_capsule");
    } else {
        std::printf("SKIP world_physics_live_tamriel_riverwood_raycast_and_capsule (Skyrim SE Data not present)\n");
    }

    return failures;
}

}  // namespace raw::worldgen

#ifdef RAW_WORLD_PHYSICS_TEST_STANDALONE
int main() {
    const int failures = raw::worldgen::RunWorldPhysicsTests(nullptr);
    std::printf("RawWorldPhysics standalone test summary: %s (%d failures)\n",
                failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
#endif
