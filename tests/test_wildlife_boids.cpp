// test_wildlife_boids.cpp: Verification Suite for `raw::worldgen::RawWildlifeBoids`.
// Exports `int raw::worldgen::RunWildlifeBoidsTests(void (*checkFn)(bool, const char*) = nullptr)`
// and supports `#ifdef RAW_WILDLIFE_BOIDS_TEST_STANDALONE` standalone execution.
// Original work, MIT.

#include "RawWildlifeBoids.h"

#include <cmath>
#include <cstdio>

namespace raw::worldgen {
namespace {

bool IsFinite3(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

TerrainCellHeightfield MakeTestTerrain(uint64_t key, float originX, float originY,
                                       float sizeX, float sizeY, float baseZ, float waterZ) {
    TerrainCellHeightfield hf{};
    hf.cellKey     = key;
    hf.originX     = originX;
    hf.originY     = originY;
    hf.sizeX       = sizeX;
    hf.sizeY       = sizeY;
    hf.gridW       = 17u;
    hf.gridH       = 17u;
    hf.waterLevelZ = waterZ;
    hf.heights.assign(17u * 17u, baseZ);
    return hf;
}

}  // namespace

int RunWildlifeBoidsTests(void (*checkFn)(bool, const char*)) {
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
    // Test 1: Planted-failure controls (empty swarm step, zero dt no-op, huge count clamp)
    // =========================================================================
    {
        RawWildlifeBoids emptyBoids;
        WildlifeEnvInput env{};
        const WildlifeTelemetry emptyTel = emptyBoids.Step(1.0f / 60.0f, env);

        // Zero-dt no-op check:
        emptyBoids.SpawnGuild(FaunaGuild::AlpineHawk, 4u, {100.0f, 200.0f, 900.0f}, 80.0f, 11u);
        const Vec3 posBefore = emptyBoids.Agents()[0].position;
        emptyBoids.Step(0.0f, env);
        emptyBoids.Step(-0.05f, env);
        const Vec3 posAfter = emptyBoids.Agents()[0].position;
        const bool zeroDtNoOp = (posBefore.x == posAfter.x && posBefore.y == posAfter.y && posBefore.z == posAfter.z);

        // Huge count safely clamped to kMaxTotalAgents:
        RawWildlifeBoids clampTest;
        const uint32_t spawned = clampTest.SpawnGuild(FaunaGuild::PineCrowFlock, 999999u, {0.0f, 0.0f, 300.0f}, 100.0f, 7u);
        const bool clampedOk = (spawned == RawWildlifeBoids::kMaxTotalAgents) &&
                               (clampTest.Agents().size() == RawWildlifeBoids::kMaxTotalAgents);

        check(emptyTel.activeAgents == 0u && zeroDtNoOp && clampedOk,
              "wildlife_boids_planted_failure_controls");
    }

    // =========================================================================
    // Test 2: AlpineHawk Thermal Soaring vs Blizzard Roosting
    // =========================================================================
    {
        RawWorldPhysics phys;
        phys.AddTerrainHeightfield(MakeTestTerrain(1ull, -4096.0f, -4096.0f, 8192.0f, 8192.0f, 0.0f, -500.0f));

        RawWildlifeBoids hawks;
        hawks.SpawnGuild(FaunaGuild::AlpineHawk, 16u, {0.0f, 0.0f, 750.0f}, 220.0f, 42u);

        auto meanHawkZ = [&]() {
            double s = 0.0;
            for (const auto& a : hawks.Agents()) s += a.position.z;
            return static_cast<float>(s / static_cast<double>(hawks.Agents().size()));
        };
        const float startZ = meanHawkZ();

        // Sunny thermal updraft phase (3 seconds):
        WildlifeEnvInput sunny{};
        sunny.solarInsolation01 = 1.0f;
        sunny.snowIntensity01   = 0.0f;
        sunny.windBeaufort      = 2.0f;
        for (int f = 0; f < 180; ++f) {
            hawks.Step(1.0f / 60.0f, sunny, &phys);
        }
        const float thermalZ = meanHawkZ();

        // Severe blizzard forces descent & roosting (6 seconds):
        WildlifeEnvInput blizzard{};
        blizzard.solarInsolation01 = 0.0f;
        blizzard.snowIntensity01   = 0.95f;
        blizzard.windBeaufort      = 10.0f;
        WildlifeTelemetry blizTel{};
        for (int f = 0; f < 360; ++f) {
            blizTel = hawks.Step(1.0f / 60.0f, blizzard, &phys);
        }
        const float roostZ = meanHawkZ();

        std::printf("  [AlpineHawk] startZ=%.1f -> thermalZ=%.1f -> blizzardRoostZ=%.1f (roosting=%u/16)\n",
                    startZ, thermalZ, roostZ, blizTel.roostingCount);

        check(thermalZ > startZ + 120.0f && roostZ < thermalZ - 350.0f && blizTel.roostingCount >= 12u,
              "wildlife_boids_hawk_thermal_soaring_and_blizzard_roosting");
    }

    // =========================================================================
    // Test 3: PineCrowFlock Acoustic Flush (`EmitStimulus(LoudNoise)`)
    // =========================================================================
    {
        RawWorldPhysics phys;
        phys.AddTerrainHeightfield(MakeTestTerrain(1ull, -4096.0f, -4096.0f, 8192.0f, 8192.0f, 0.0f, -500.0f));

        RawWildlifeBoids crows;
        constexpr uint32_t kCrowCount = 24u;
        crows.SpawnGuild(FaunaGuild::PineCrowFlock, kCrowCount, {0.0f, 0.0f, 280.0f}, 140.0f, 77u);

        WildlifeEnvInput calm{};
        calm.solarInsolation01 = 0.6f;
        WildlifeTelemetry preTel{};
        for (int f = 0; f < 30; ++f) {
            preTel = crows.Step(1.0f / 60.0f, calm, &phys);
        }
        auto meanCrowZ = [&]() {
            double s = 0.0;
            for (const auto& a : crows.Agents()) s += a.position.z;
            return static_cast<float>(s / static_cast<double>(crows.Agents().size()));
        };
        const float preZ     = meanCrowZ();
        const float preSpeed = preTel.meanSpeed;

        // Emit loud shout/thunder stimulus right under the flock!
        crows.EmitStimulus({0.0f, 0.0f, 250.0f}, 1200.0f, StimulusKind::LoudNoise, 1.0f, 2.5f);

        WildlifeTelemetry flushTel{};
        for (int f = 0; f < 45; ++f) {
            flushTel = crows.Step(1.0f / 60.0f, calm, &phys);
        }
        const float postZ = meanCrowZ();

        std::printf("  [PineCrowFlock Flush] Z %.1f->%.1f speed %.1f->%.1f startled=%u/%u\n",
                    preZ, postZ, preSpeed, flushTel.meanSpeed, flushTel.startledCount, kCrowCount);

        check(flushTel.startledCount == kCrowCount && postZ > preZ + 120.0f &&
                  flushTel.meanSpeed > preSpeed * 1.25f,
              "wildlife_boids_crow_acoustic_flush");
    }

    // =========================================================================
    // Test 4: TundraDeerHerd Wildfire Evasion (coupled with `RawWorldEcosystem`)
    // =========================================================================
    {
        RawWorldPhysics phys;
        phys.AddTerrainHeightfield(MakeTestTerrain(1ull, -4096.0f, -4096.0f, 8192.0f, 8192.0f, 120.0f, -500.0f));

        EcoGridConfig ecoCfg{};
        ecoCfg.gridW     = 32u;
        ecoCfg.gridH     = 32u;
        ecoCfg.originX   = 0.0f;
        ecoCfg.originY   = 0.0f;
        ecoCfg.cellSizeM = 2.0f;
        RawWorldEcosystem eco(ecoCfg);

        // Ignite wildfire at eco cell (16, 16) -> 33m * 70u/m = (2310u, 2310u)
        const Vec2 fireM = eco.CellCenterWorldM(16u, 16u);
        eco.IgniteAt(fireM.x, fireM.y, 6.0f, 600.0f);
        const Vec3 fireWorldU{fireM.x * 70.0f, fireM.y * 70.0f, 120.0f};

        RawWildlifeBoids deer;
        // Spawn deer herd 450u away from the wildfire ignition:
        deer.SpawnGuild(FaunaGuild::TundraDeerHerd, 16u, {fireWorldU.x + 450.0f, fireWorldU.y + 100.0f, 120.0f}, 90.0f, 99u);

        auto meanDistFromFire = [&]() {
            double s = 0.0;
            for (const auto& a : deer.Agents()) {
                const float dx = a.position.x - fireWorldU.x;
                const float dy = a.position.y - fireWorldU.y;
                s += std::sqrt(dx * dx + dy * dy);
            }
            return static_cast<float>(s / static_cast<double>(deer.Agents().size()));
        };

        const float distBefore = meanDistFromFire();
        WildlifeEnvInput env{};
        env.ecoUnitsPerMeter = 70.0f;
        env.windDirXY        = {1.0f, 0.0f};

        bool allGrounded = true;
        for (int f = 0; f < 120; ++f) { // 2.0 seconds
            deer.Step(1.0f / 60.0f, env, &phys, &eco);
            for (const auto& a : deer.Agents()) {
                if (std::abs(a.position.z - 120.0f) > 1.0e-2f || !IsFinite3(a.position)) {
                    allGrounded = false;
                }
            }
        }
        const float distAfter = meanDistFromFire();

        std::printf("  [TundraDeer Wildfire Evasion] distFromFire %.1fu -> %.1fu grounded=%d\n",
                    distBefore, distAfter, allGrounded ? 1 : 0);

        check(allGrounded && distAfter > distBefore + 150.0f,
              "wildlife_boids_deer_wildfire_evasion_and_grounding");
    }

    // =========================================================================
    // Test 5: RiverSalmon Depth Confinement + 512-Agent O(N) < 1.0 ms/frame Perf Proof
    // =========================================================================
    {
        RawWorldPhysics phys;
        // Riverbed at Z = -200u, water surface at Z = 0u -> salmon strictly in [-190u .. -10u]
        phys.AddTerrainHeightfield(MakeTestTerrain(1ull, -8192.0f, -8192.0f, 16384.0f, 16384.0f, -200.0f, 0.0f));

        RawWildlifeBoids swarm;
        swarm.SpawnGuild(FaunaGuild::AlpineHawk,        64u,  {0.0f, 0.0f, 900.0f},  800.0f, 101u);
        swarm.SpawnGuild(FaunaGuild::PineCrowFlock,     160u, {300.0f, 200.0f, 260.0f}, 500.0f, 102u);
        swarm.SpawnGuild(FaunaGuild::TundraDeerHerd,    128u, {-400.0f, 300.0f, -200.0f}, 600.0f, 103u);
        swarm.SpawnGuild(FaunaGuild::RiverSalmonSchool, 160u, {0.0f, 0.0f, -90.0f},  350.0f, 104u);

        WildlifeEnvInput env{};
        env.defaultWaterLevelZ = 0.0f;
        env.riverCurrentXY     = {1.0f, 0.0f};

        double sumStepMs  = 0.0;
        bool salmonInBox  = true;
        bool allFinite    = true;
        constexpr int kFrames = 40;
        for (int f = 0; f < kFrames; ++f) {
            const WildlifeTelemetry tel = swarm.Step(1.0f / 60.0f, env, &phys);
            if (f >= 5) { // Skip first few cold cache warmup frames
                sumStepMs += tel.stepMs;
            }
            for (const auto& a : swarm.Agents()) {
                if (!IsFinite3(a.position) || !IsFinite3(a.velocity)) {
                    allFinite = false;
                }
                if (a.guild == FaunaGuild::RiverSalmonSchool) {
                    if (a.position.z < -200.0f + 10.0f || a.position.z > 0.0f - 10.0f) {
                        salmonInBox = false;
                    }
                }
            }
        }
        const double meanStepMs = sumStepMs / static_cast<double>(kFrames - 5);

        std::printf("  [512-Agent O(N) Swarm + Salmon] agents=%zu meanStep=%.3fms salmonConfined=%d finite=%d\n",
                    swarm.Agents().size(), meanStepMs, salmonInBox ? 1 : 0, allFinite ? 1 : 0);

        check(swarm.Agents().size() == 512u && salmonInBox && allFinite && meanStepMs < 1.0,
              "wildlife_boids_salmon_confinement_and_512_agent_perf");
    }

    return failures;
}

}  // namespace raw::worldgen

#ifdef RAW_WILDLIFE_BOIDS_TEST_STANDALONE
int main() {
    const int failures = raw::worldgen::RunWildlifeBoidsTests(nullptr);
    std::printf("RawWildlifeBoids standalone test summary: %s (%d failures)\n",
                failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
#endif
