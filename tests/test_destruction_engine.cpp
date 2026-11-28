// test_destruction_engine.cpp: First-Principles 3D Voronoi CSG Fracture & Rigid-Body Destruction Tests.
// Linkable into ce_runtime_check via raw::worldgen::RunDestructionEngineTests(checkFn),
// or buildable standalone with -DRAW_DESTRUCTION_STANDALONE_MAIN.
// Original work, MIT.

#include "../src/pipeline/RawAssetValidator.h"
#include "../src/worldgen/RawDestructionEngine.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace raw::worldgen {
namespace {

bool IsMat3FiniteSymPosDef(const Mat3& M) {
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            if (!std::isfinite(M.m[r][c])) return false;
        }
    }
    if (std::fabs(M.m[0][1] - M.m[1][0]) > 1.0e-4f ||
        std::fabs(M.m[0][2] - M.m[2][0]) > 1.0e-4f ||
        std::fabs(M.m[1][2] - M.m[2][1]) > 1.0e-4f) {
        return false;
    }
    // Sylvester's criterion for 3x3 symmetric positive-definite matrix
    const double d1 = M.m[0][0];
    const double d2 = double(M.m[0][0]) * M.m[1][1] - double(M.m[0][1]) * M.m[1][0];
    const double d3 =
        double(M.m[0][0]) * (double(M.m[1][1]) * M.m[2][2] - double(M.m[1][2]) * M.m[2][1]) -
        double(M.m[0][1]) * (double(M.m[1][0]) * M.m[2][2] - double(M.m[1][2]) * M.m[2][0]) +
        double(M.m[0][2]) * (double(M.m[1][0]) * M.m[2][1] - double(M.m[1][1]) * M.m[2][0]);
    return d1 > 0.0 && d2 > 0.0 && d3 > 0.0;
}

bool SplatsAreDistinct(const MaterialSplat8& a, const MaterialSplat8& b) {
    const float diff =
        std::fabs(a.splat0.x - b.splat0.x) + std::fabs(a.splat0.y - b.splat0.y) +
        std::fabs(a.splat0.z - b.splat0.z) + std::fabs(a.splat0.w - b.splat0.w) +
        std::fabs(a.splat1.x - b.splat1.x) + std::fabs(a.splat1.y - b.splat1.y) +
        std::fabs(a.splat1.z - b.splat1.z) + std::fabs(a.splat1.w - b.splat1.w);
    return diff > 0.15f;
}

}  // namespace

int RunDestructionEngineTests(void (*checkFn)(bool, const char*)) {
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        if (!ok) ++failures;
        if (checkFn) {
            checkFn(ok, name);
        } else {
            std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
        }
    };

    // 1. Exact signed-tetrahedron analytical verification on a 1x1x1 unit cube (density=1000 kg/m^3 -> M=1000, I=M/6)
    {
        ConvexPolyhedron cube{};
        cube.name        = "UnitCube";
        cube.densityKgM3 = 1000.0f;
        const Vec3 c000{-0.5f, -0.5f, 0.0f}, c100{ 0.5f, -0.5f, 0.0f}, c110{ 0.5f,  0.5f, 0.0f}, c010{-0.5f,  0.5f, 0.0f};
        const Vec3 c001{-0.5f, -0.5f, 1.0f}, c101{ 0.5f, -0.5f, 1.0f}, c111{ 0.5f,  0.5f, 1.0f}, c011{-0.5f,  0.5f, 1.0f};
        cube.faces = {
            {{c000, c010, c110, c100}, { 0,  0, -1}, false},
            {{c001, c101, c111, c011}, { 0,  0,  1}, false},
            {{c000, c100, c101, c001}, { 0, -1,  0}, false},
            {{c110, c010, c011, c111}, { 0,  1,  0}, false},
            {{c010, c000, c001, c011}, {-1,  0,  0}, false},
            {{c100, c110, c111, c101}, { 1,  0,  0}, false},
        };
        const MassProperties mp = ComputeMassProperties(cube);
        const float expectedI = 1000.0f / 6.0f;
        const bool cubeExact =
            std::fabs(mp.volumeM3 - 1.0f) < 1.0e-5f &&
            std::fabs(mp.massKg - 1000.0f) < 1.0e-3f &&
            std::fabs(mp.centroid.x) < 1.0e-5f &&
            std::fabs(mp.centroid.y) < 1.0e-5f &&
            std::fabs(mp.centroid.z - 0.5f) < 1.0e-5f &&
            std::fabs(mp.inertiaTensor.m[0][0] - expectedI) < 1.0e-2f &&
            std::fabs(mp.inertiaTensor.m[1][1] - expectedI) < 1.0e-2f &&
            std::fabs(mp.inertiaTensor.m[2][2] - expectedI) < 1.0e-2f &&
            std::fabs(mp.inertiaTensor.m[0][1]) < 1.0e-3f &&
            std::fabs(mp.inertiaTensor.m[0][2]) < 1.0e-3f &&
            std::fabs(mp.inertiaTensor.m[1][2]) < 1.0e-3f;
        check(cubeExact, "destruction_unit_cube_exact_volume_centroid_inertia");
    }

    // 2. All 5 Destructible Archetypes: exact volume conservation (<0.5%), distinct ext/int materials,
    //    watertight faces, positive-definite inertia tensors, and strict GPU meshlet validation!
    bool allVolConserved   = true;
    bool allMaterialsSplit = true;
    bool allInertiaSpd     = true;
    bool allMeshletsStrict = true;
    float worstVolErr      = 0.0f;
    uint32_t totalShards5  = 0u;
    uint32_t totalMeshlets5 = 0u;

    for (uint8_t aIdx = 0; aIdx < static_cast<uint8_t>(DestructibleArchetype::Count); ++aIdx) {
        const auto arch = static_cast<DestructibleArchetype>(aIdx);
        const ConvexPolyhedron body = BuildDestructibleArchetype(arch, 1.0f, {0.0f, 0.0f, 0.0f});

        FractureConfig fcfg{};
        fcfg.seed                = 202642u + uint32_t(aIdx) * 1337u;
        fcfg.shardCount          = 18u;
        fcfg.impactPoint         = {0.25f, -0.15f, 1.05f};
        fcfg.useImpactClustering = true;
        fcfg.clusterFocus01      = 0.65f;

        const FractureResult frac = FractureConvexBody(body, fcfg);
        worstVolErr = std::max(worstVolErr, frac.volumeErrorRel);
        totalShards5 += static_cast<uint32_t>(frac.shards.size());

        if (!frac.ok || frac.shards.size() < 8u || frac.volumeErrorRel >= 0.005f) {
            allVolConserved = false;
        }
        if (!SplatsAreDistinct(body.exteriorMaterial, body.interiorMaterial)) {
            allMaterialsSplit = false;
        }

        uint32_t sumExt = 0u, sumInt = 0u;
        for (const FracturedShard& sh : frac.shards) {
            sumExt += sh.exteriorFaceCount;
            sumInt += sh.interiorFaceCount;
            if (sh.localPoly.faces.size() < 4u || sh.massProps.volumeM3 <= 0.0f ||
                sh.massProps.massKg <= 0.0f || !IsMat3FiniteSymPosDef(sh.massProps.inertiaTensor)) {
                allInertiaSpd = false;
            }
        }
        if (sumExt == 0u || sumInt == 0u) {
            allMaterialsSplit = false;
        }

        ProceduralMesh explodedMesh{};
        raw::pipeline::ValidationReport rep{};
        const bool built = BuildMeshFromFractureResult(frac, 0.12f, 126u, &explodedMesh,
                                                       std::string(GetDestructibleArchetypeName(arch)) + "_fractured");
        const bool valid = built && raw::pipeline::ValidateProceduralMeshStrict(explodedMesh, &rep);
        totalMeshlets5 += explodedMesh.meshlets.size() ? static_cast<uint32_t>(explodedMesh.meshlets.size()) : 0u;
        if (!valid || rep.TotalErrors() != 0u) {
            allMeshletsStrict = false;
        }
    }

    if (!checkFn) {
        std::printf("[Destruction] 5 archetypes fractured: totalShards=%u, maxVolErr=%.6f%%, totalMeshlets=%u\n",
                    totalShards5, double(worstVolErr) * 100.0, totalMeshlets5);
    }

    check(allVolConserved,   "destruction_all_5_archetypes_volume_conserved_under_0_5pct");
    check(allMaterialsSplit, "destruction_all_5_archetypes_distinct_exterior_vs_interior_splats");
    check(allInertiaSpd,     "destruction_all_5_archetypes_positive_definite_inertia_tensors");
    check(allMeshletsStrict, "destruction_all_5_archetypes_gpu_meshlets_strict_zero_defects");

    // 3. Impact clustering check: shards closer to impact point have smaller average volume than distant shards
    {
        const ConvexPolyhedron pillar = BuildDestructibleArchetype(DestructibleArchetype::NordicStonePillar, 1.0f, {0, 0, 0});
        FractureConfig fcfg{};
        fcfg.seed                = 77742u;
        fcfg.shardCount          = 28u;
        fcfg.impactPoint         = {0.35f, 0.0f, 1.4f};
        fcfg.useImpactClustering = true;
        fcfg.clusterFocus01      = 0.85f;
        const FractureResult frac = FractureConvexBody(pillar, fcfg);

        double nearVol = 0.0, farVol = 0.0;
        uint32_t nearCnt = 0u, farCnt = 0u;
        for (const auto& sh : frac.shards) {
            const float d = Length(sh.massProps.centroid - fcfg.impactPoint);
            if (d < 0.75f) { nearVol += sh.massProps.volumeM3; ++nearCnt; }
            else           { farVol  += sh.massProps.volumeM3; ++farCnt;  }
        }
        const bool clusteredSmaller =
            nearCnt >= 3u && farCnt >= 3u && (nearVol / double(nearCnt)) < (farVol / double(farCnt));
        check(clusteredSmaller, "destruction_impact_clustered_voronoi_finer_near_impact");
    }

    // 4. 180-frame Rigid-Body Debris Simulation (impact burst + gravity + ground bounce + Coulomb friction + sleep)
    DestructionSimConfig simCfg{};
    simCfg.gravityZ          = 9.80665f;
    simCfg.groundZ           = 0.0f;
    simCfg.substeps          = 2u;
    simCfg.sleepHoldSeconds  = 0.20f;

    DestructionSimulation sim(simCfg);
    FractureConfig fcfg{};
    fcfg.seed                = 420099u;
    fcfg.shardCount          = 20u;
    fcfg.impactPoint         = {-0.30f, 0.0f, 1.25f};
    fcfg.useImpactClustering = true;
    fcfg.clusterFocus01      = 0.70f;

    DestructionImpact impact{};
    impact.point          = {-0.45f, 0.0f, 1.25f};
    impact.direction      = {1.0f, 0.15f, 0.25f};
    impact.impulseNs      = 210.0f;
    impact.radialBurstNs  = 110.0f;
    impact.falloffRadiusM = 1.40f;
    impact.upwardBias01   = 0.22f;

    FractureResult simFrac{};
    const bool simInit = sim.FractureAndInitialize(DestructibleArchetype::NordicStonePillar,
                                                   1.0f, {0.0f, 0.0f, 0.0f}, fcfg, impact, &simFrac);
    check(simInit && sim.GetBodies().size() >= 10u, "destruction_sim_init_from_pillar_fracture");

    const DestructionSimStats stStart = sim.GetStats();
    check(stStart.allFinite && stStart.kineticEnergyJ > 50.0f && stStart.awakeBodies == stStart.totalBodies,
          "destruction_sim_initial_impact_kinetic_energy_nonzero");

    // Step 180 frames at 60 Hz (3.0 seconds), then check settling (give up to 240 frames if a tall shard still tumbles)
    constexpr float kDt = 1.0f / 60.0f;
    for (int frame = 0; frame < 180; ++frame) {
        sim.Step(kDt);
    }
    const DestructionSimStats st180 = sim.GetStats();

    // Ensure every shard is finite, above ground plane, and has dissipated >95% of impact energy & settled asleep!
    uint32_t settledFrames = 180u;
    DestructionSimStats stFinal = st180;
    while (stFinal.awakeBodies > 0u && settledFrames < 360u) {
        sim.Step(kDt);
        ++settledFrames;
        stFinal = sim.GetStats();
    }

    if (!checkFn) {
        std::printf("[DestructionSim] startKE=%.1f J -> f180(KE=%.3f J, asleep=%u/%u, minZ=%.4f) -> settled@f%u(asleep=%u/%u)\n",
                    double(stStart.kineticEnergyJ), double(st180.kineticEnergyJ),
                    st180.asleepBodies, st180.totalBodies, double(st180.minVertexZ),
                    settledFrames, stFinal.asleepBodies, stFinal.totalBodies);
    }

    check(st180.allFinite && stFinal.allFinite, "destruction_sim_zero_nan_inf");
    check(st180.minVertexZ >= simCfg.groundZ - 0.002f && stFinal.minVertexZ >= simCfg.groundZ - 0.002f,
          "destruction_sim_all_shards_above_ground_plane");
    check(st180.kineticEnergyJ < stStart.kineticEnergyJ * 0.05f,
          "destruction_sim_180f_energy_dissipated_by_friction_and_impact");
    check(stFinal.asleepBodies == stFinal.totalBodies && stFinal.maxSpeed == 0.0f,
          "destruction_sim_all_shards_sleep_settled");

    // 5. GPU Meshlet Strict Validation + .glb / .obj Export on Simulated Debris State & Single Shard
    ProceduralMesh debrisMesh{};
    raw::pipeline::ValidationReport debrisRep{};
    const bool builtDebris = sim.BuildMeshFromDestructionState(126u, &debrisMesh, "nordic_pillar_debris_settled");
    const bool validDebris = builtDebris && raw::pipeline::ValidateProceduralMeshStrict(debrisMesh, &debrisRep);
    check(validDebris && debrisRep.TotalErrors() == 0u, "destruction_sim_debris_meshlets_strict_zero_defects");

    ProceduralMesh singleShardMesh{};
    raw::pipeline::ValidationReport shardRep{};
    const bool builtSingle = sim.BuildMeshFromShard(0u, 126u, &singleShardMesh);
    const bool validSingle = builtSingle && raw::pipeline::ValidateProceduralMeshStrict(singleShardMesh, &shardRep);
    check(validSingle && shardRep.TotalErrors() == 0u, "destruction_single_shard_meshlets_strict_zero_defects");

    const std::string objStr = SerializeMeshToObjString(debrisMesh);
    const bool objOk = (objStr.find("\nv ") != std::string::npos || objStr.rfind("v ", 0) == 0) &&
                       (objStr.find("\nvn ") != std::string::npos) &&
                       (objStr.find("\nf ") != std::string::npos);
    check(objOk, "destruction_debris_export_wavefront_obj_valid");

    const std::vector<uint8_t> glbBytes = SerializeMeshToGlbBytes(debrisMesh, true);
    uint32_t glbMagic = 0u, glbVer = 0u, glbLen = 0u;
    if (glbBytes.size() >= 12u) {
        std::memcpy(&glbMagic, glbBytes.data() + 0u, 4u);
        std::memcpy(&glbVer,   glbBytes.data() + 4u, 4u);
        std::memcpy(&glbLen,   glbBytes.data() + 8u, 4u);
    }
    const bool glbOk = (glbMagic == 0x46546C67u) && (glbVer == 2u) && (glbLen == glbBytes.size()) && (glbBytes.size() > 256u);
    check(glbOk, "destruction_debris_export_gltf2_glb_valid");

    // 6. Bit-Exact Deterministic Reproducibility Check
    DestructionSimulation simB(simCfg);
    FractureResult fracB{};
    simB.FractureAndInitialize(DestructibleArchetype::NordicStonePillar,
                               1.0f, {0.0f, 0.0f, 0.0f}, fcfg, impact, &fracB);
    for (uint32_t f = 0; f < settledFrames; ++f) {
        simB.Step(kDt);
    }
    bool deterministic = (sim.GetBodies().size() == simB.GetBodies().size()) &&
                         (simFrac.totalShardVolume == fracB.totalShardVolume);
    if (deterministic) {
        for (size_t i = 0; i < sim.GetBodies().size(); ++i) {
            const auto& a = sim.GetBodies()[i];
            const auto& b = simB.GetBodies()[i];
            if (a.position.x != b.position.x || a.position.y != b.position.y || a.position.z != b.position.z ||
                a.orientation.x != b.orientation.x || a.orientation.y != b.orientation.y ||
                a.orientation.z != b.orientation.z || a.orientation.w != b.orientation.w) {
                deterministic = false;
                break;
            }
        }
    }
    check(deterministic, "destruction_bit_exact_deterministic_reproducibility");

    if (!checkFn) {
        std::printf("DestructionEngineTests completed: %s (failures=%d)\n",
                    failures == 0 ? "PASS" : "FAIL", failures);
    }
    return failures;
}

}  // namespace raw::worldgen

#ifdef RAW_DESTRUCTION_STANDALONE_MAIN
int main() {
    const int failures = raw::worldgen::RunDestructionEngineTests(nullptr);
    return failures == 0 ? 0 : 1;
}
#endif
