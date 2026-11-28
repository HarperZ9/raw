#pragma once
// RawWorldGenTests.h: Self-contained known-answer, invariant & planted-failure unit tests for `raw::worldgen`.
// Wire into tests/ce_runtime_check/main.cpp in 1 line:
//   raw::worldgen::RunWorldGenSelfTests(Check);
// Original work, MIT.

#include "RawWorldShowcase.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace raw::worldgen {

template <typename CheckFn>
inline void RunWorldGenSelfTests(CheckFn&& Check) {
    // 1. Tamriel border C2 blend: strictly 0 inside |x|<=185000 && |y|<=145000, smooth ramp in apron, 1.0 beyond.
    TamrielBorderConfig bcfg{};
    const float bWhiterun = EvaluateTamrielBorderBlend(20000.0f, -10000.0f, bcfg);
    const float bEdge     = EvaluateTamrielBorderBlend(185000.0f, 145000.0f, bcfg);
    const float bApronMid = EvaluateTamrielBorderBlend(185000.0f + bcfg.blendApronU * 0.5f, 0.0f, bcfg);
    const float bFrontier = EvaluateTamrielBorderBlend(225000.0f, 0.0f, bcfg);
    Check(bWhiterun == 0.0f && bEdge == 0.0f && bApronMid > 0.45f && bApronMid < 0.55f && bFrontier == 1.0f,
          "worldgen_tamriel_border_c2_blend");

    // 2. Watertight skirtless seam invariants:
    //    (a) Adjacent same-LOD chunks share identical boundary heights (error < 1e-4).
    //    (b) LOD0 chunk stitched against LOD1 neighbor (+X edge) matches linear LOD1 edge with 0.0 gap!
    TerrainConfig tcfg{};
    tcfg.seed = 202607u;
    tcfg.border.enabled = false;
    TerrainChunkData cA{}, cB{}, cLod0Stitch{}, cLod1Neigh{};
    const bool okA = GenerateBiomeTerrainChunk(54, 10, 0u, tcfg, StitchNone, &cA);
    const bool okB = GenerateBiomeTerrainChunk(55, 10, 0u, tcfg, StitchNone, &cB);
    float maxSameLodGap = 0.0f;
    if (okA && okB) {
        for (uint32_t iy = 0; iy < cA.stride; ++iy) {
            const float hRight = cA.heights[iy * cA.stride + cA.gridN];
            const float hLeft  = cB.heights[iy * cB.stride + 0u];
            maxSameLodGap = std::max(maxSameLodGap, std::fabs(hRight - hLeft));
        }
    }

    const bool okS0 = GenerateBiomeTerrainChunk(54, 10, 0u, tcfg, StitchPosX, &cLod0Stitch);
    const bool okS1 = GenerateBiomeTerrainChunk(55, 10, 1u, tcfg, StitchNone, &cLod1Neigh);
    float maxLodStitchGap = 0.0f;
    if (okS0 && okS1) {
        for (uint32_t iy = 0; iy < cLod0Stitch.stride; ++iy) {
            const float hFine = cLod0Stitch.heights[iy * cLod0Stitch.stride + cLod0Stitch.gridN];
            const float hCoarse = ((iy & 1u) == 0u)
                ? cLod1Neigh.heights[(iy / 2u) * cLod1Neigh.stride + 0u]
                : 0.5f * (cLod1Neigh.heights[(iy / 2u) * cLod1Neigh.stride + 0u] +
                          cLod1Neigh.heights[(iy / 2u + 1u) * cLod1Neigh.stride + 0u]);
            maxLodStitchGap = std::max(maxLodStitchGap, std::fabs(hFine - hCoarse));
        }
    }
    Check(okA && okB && okS0 && okS1 && maxSameLodGap < 1.0e-4f && maxLodStitchGap < 1.0e-4f,
          "worldgen_watertight_skirtless_lod_seams");

    // 3. Analytical normals unit length + hydraulic/thermal erosion effect + 3D SDF sea-arch/overhang carving.
    bool normsOk = true;
    bool splatsNormOk = true;
    for (size_t i = 0; i < cA.normals.size(); ++i) {
        const float nl = Length(cA.normals[i]);
        if (!std::isfinite(nl) || std::fabs(nl - 1.0f) > 0.02f) { normsOk = false; break; }
        const float sumS = cA.splat0[i].x + cA.splat0[i].y + cA.splat0[i].z + cA.splat0[i].w +
                           cA.splat1[i].x + cA.splat1[i].y + cA.splat1[i].z + cA.splat1[i].w;
        if (!std::isfinite(sumS) || std::fabs(sumS - 1.0f) > 1.0e-3f) { splatsNormOk = false; break; }
    }
    TerrainConfig noErodeCfg = tcfg;
    noErodeCfg.enableHydraulicErosion = false;
    noErodeCfg.thermalIterations = 0u;
    TerrainChunkData cNoErode{};
    const bool okNE = GenerateBiomeTerrainChunk(54, 10, 1u, noErodeCfg, StitchNone, &cNoErode);
    TerrainChunkData cErode{};
    const bool okER = GenerateBiomeTerrainChunk(54, 10, 1u, tcfg, StitchNone, &cErode);
    float maxErodeDiff = 0.0f;
    if (okNE && okER) {
        for (size_t i = 0; i < cErode.heights.size(); ++i) {
            maxErodeDiff = std::max(maxErodeDiff, std::fabs(cErode.heights[i] - cNoErode.heights[i]));
        }
    }
    const auto sdfAbove = EvaluateOverhangSdf({1000.0f, 1000.0f, 900.0f}, 200.0f, 0.1f, 0u, tcfg.seed);
    const auto sdfBelow = EvaluateOverhangSdf({1000.0f, 1000.0f, -600.0f}, 200.0f, 0.1f, 0u, tcfg.seed);
    Check(normsOk && splatsNormOk && okNE && okER && maxErodeDiff > 0.05f &&
          sdfAbove.sdfU < 0.0f && sdfBelow.sdfU > 0.0f && std::fabs(Length(sdfAbove.normal) - 1.0f) < 0.02f,
          "worldgen_erosion_normals_splats_and_3d_sdf");

    // 4. All 6 showcase biomes reachable around the frontier ring + signature PBR material responses.
    constexpr float kAngles[6] = {+1.5707963f, -0.5235988f, +2.6179939f, -1.5707963f, +0.5235988f, -2.6179939f};
    uint32_t seenMask = 0u;
    bool blendSumOk = true;
    for (int i = 0; i < 6; ++i) {
        const float wx = std::cos(kAngles[i]) * 235000.0f;
        const float wy = std::sin(kAngles[i]) * 235000.0f;
        const auto b = EvaluateBiomeBlend(wx, wy, tcfg.seed);
        float wSum = 0.0f;
        for (float w : b.weights) wSum += w;
        if (std::fabs(wSum - 1.0f) > 1.0e-3f) blendSumOk = false;
        seenMask |= (1u << static_cast<uint32_t>(b.primary));
    }
    BiomeBlendResult pureVolc{}; pureVolc.weights[1] = 1.0f; pureVolc.primary = BiomeId::GeothermalVolcanicCaldera;
    BiomeBlendResult pureFjord{}; pureFjord.weights[0] = 1.0f; pureFjord.primary = BiomeId::GlacialFjordSpires;
    const auto matVolc  = EvaluateBiomeMaterials({210000.0f, -80000.0f, 400.0f}, {0, 0, 1}, -0.45f, 0.7f, 0.2f, 0.0f, 0.0f, pureVolc, -1400.0f, 2100.0f, tcfg.seed);
    const auto matFjord = EvaluateBiomeMaterials({0.0f, 230000.0f, 1200.0f}, {0.6f, 0.0f, 0.8f}, -0.35f, 0.1f, 0.1f, 0.0f, 0.0f, pureFjord, -1400.0f, 2100.0f, tcfg.seed);
    Check(blendSumOk && seenMask == 0x3Fu && matVolc.splat1.x > 0.25f && matVolc.emissiveMask > 0.30f && matFjord.splat0.z > 0.35f,
          "worldgen_six_biomes_and_pbr_splat_signatures");

    // 5. Poisson-disc prop distribution guarantees minimum separation & deterministic repeatability.
    std::vector<PropInstance> propsA, propsB;
    const float minSp = 280.0f;
    const uint32_t nA = GenerateChunkProps(cA, tcfg.seed, minSp, &propsA);
    const uint32_t nB = GenerateChunkProps(cA, tcfg.seed, minSp, &propsB);
    bool poissonSpacingOk = (nA > 0u && nA == nB);
    for (size_t i = 0; i < propsA.size() && poissonSpacingOk; ++i) {
        if (std::fabs(propsA[i].position.x - propsB[i].position.x) > 1e-4f) poissonSpacingOk = false;
        for (size_t j = i + 1; j < propsA.size(); ++j) {
            const float d = std::hypot(propsA[i].position.x - propsA[j].position.x,
                                       propsA[i].position.y - propsA[j].position.y);
            if (d + 1.0e-2f < minSp) { poissonSpacingOk = false; break; }
        }
    }
    Check(poissonSpacingOk, "worldgen_poisson_disc_spacing_and_determinism");

    // 6. Meshlet pipeline (<=64v / <=126t, tight bounding spheres) + .obj & .glb binary export.
    ProceduralMesh mTerrain{}, mBasalt{}, mConifer{}, mArch{}, mVent{}, mCrystal{}, mDrift{};
    const bool meshesOk =
        BuildTerrainChunkMesh(cA, 126u, &mTerrain) &&
        BuildHexagonalBasaltCluster(101u, 9u, 110.0f, 180.0f, 126u, &mBasalt) &&
        BuildMultiTierConifer(202u, 240.0f, 44.0f, 0.35f, 126u, &mConifer) &&
        BuildGlacialIceArch(303u, 320.0f, 190.0f, 38.0f, 126u, &mArch) &&
        BuildVolcanicVent(404u, 96.0f, 120.0f, 126u, &mVent) &&
        BuildCrystalMonolith(505u, 72.0f, 210.0f, 5u, 126u, &mCrystal) &&
        BuildNordicSnowDrift(606u, 180.0f, 110.0f, 36.0f, 126u, &mDrift);

    bool meshletBoundsOk = meshesOk;
    const ProceduralMesh* allMeshes[7] = {&mTerrain, &mBasalt, &mConifer, &mArch, &mVent, &mCrystal, &mDrift};
    for (const ProceduralMesh* pm : allMeshes) {
        if (!pm || pm->meshlets.empty()) { meshletBoundsOk = false; break; }
        for (const auto& ml : pm->meshlets) {
            if (ml.vertexCount == 0u || ml.vertexCount > 64u || ml.triangleCount == 0u || ml.triangleCount > 126u || ml.boundSphere[3] <= 0.0f) {
                meshletBoundsOk = false;
                break;
            }
            for (uint32_t vi = 0; vi < ml.vertexCount; ++vi) {
                const auto& v = pm->vertices[ml.vertexOffset + vi];
                const float d = std::hypot(std::hypot(v.pos[0] - ml.boundSphere[0], v.pos[1] - ml.boundSphere[1]),
                                           v.pos[2] - ml.boundSphere[2]);
                if (d > ml.boundSphere[3] + 1.0e-2f) { meshletBoundsOk = false; break; }
            }
        }
    }
    const std::string objTxt = SerializeMeshToObjString(mCrystal);
    const std::vector<uint8_t> glbBytes = SerializeMeshToGlbBytes(mArch, true);
    uint32_t glbMagic = 0u, glbVer = 0u;
    if (glbBytes.size() >= 12u) {
        std::memcpy(&glbMagic, glbBytes.data(), 4u);
        std::memcpy(&glbVer, glbBytes.data() + 4u, 4u);
    }
    Check(meshletBoundsOk && objTxt.find("f ") != std::string::npos &&
          glbMagic == 0x46546C67u && glbVer == 2u && glbBytes.size() > 256u,
          "worldgen_meshlet_64v126t_bounds_and_obj_glb_export");

    // 7. Live streaming manager: frontier activation, strict VRAM cap, LRU eviction & offline 6-biome diorama.
    struct MockSink final : IGpuChunkSink {
        uint64_t nextHandle = 1u;
        uint32_t loads = 0u, evicts = 0u;
        uint64_t OnChunkLoaded(const StreamedChunkEntry&) override { ++loads; return nextHandle++; }
        void OnChunkEvicted(ChunkKey, uint64_t) override { ++evicts; }
    };
    RawWorldShowcase stream;
    StreamConfig scfg{};
    scfg.vramBudgetBytes = 900ull * 1024ull; // Tight 900 KB budget to force LRU eviction!
    scfg.lod0RadiusChunks = 0;
    scfg.lod1RadiusChunks = 1;
    scfg.lod2RadiusChunks = 1;
    scfg.lod3RadiusChunks = 2;
    scfg.maxLoadsPerFrame = 4u;
    scfg.showcaseMode = false;
    MockSink sink{};
    const bool initOk = stream.Init(scfg);
    const auto stInside = stream.Update({0.0f, 0.0f, 0.0f}, &sink); // Inside Tamriel -> inactive
    const auto stFront1 = stream.Update({220000.0f, 0.0f, 0.0f}, &sink);
    const auto stFront2 = stream.Update({220000.0f, 0.0f, 0.0f}, &sink);
    const auto stFront3 = stream.Update({236000.0f, 16000.0f, 0.0f}, &sink); // Move -> forces LRU/radius evicts
    const auto diorama = GenerateOfflineShowcaseDiorama(202607u);
    Check(initOk && !stInside.activeInFrontier && stInside.activeChunkCount == 0u &&
          stFront1.activeInFrontier && stFront2.activeChunkCount > 0u &&
          stFront2.usedVramBytes <= scfg.vramBudgetBytes &&
          stFront3.usedVramBytes <= scfg.vramBudgetBytes &&
          sink.loads >= 2u && sink.evicts >= 1u &&
          diorama.ok && diorama.biomesGenerated == 6u && diorama.totalMeshlets > 40u,
          "worldgen_streaming_lru_vram_budget_and_offline_diorama");

    // 8. Planted-failure controls: invalid LOD, tiny chunk size, tiny budget, and degenerate prop sizes must fail cleanly!
    TerrainChunkData badChunk{};
    const bool badLodRej = !GenerateTerrainChunk(0, 0, 5u, tcfg, StitchNone, &badChunk);
    TerrainConfig badCfg = tcfg;
    badCfg.chunkSizeU = 4.0f;
    const bool badSizeRej = !GenerateTerrainChunk(0, 0, 0u, badCfg, StitchNone, &badChunk);
    RawWorldShowcase badStream;
    StreamConfig badScfg{};
    badScfg.vramBudgetBytes = 0u;
    const bool badBudgetRej = !badStream.Init(badScfg);
    ProceduralMesh badMesh{};
    const bool badArchRej = !BuildGlacialIceArch(1u, 2.0f, 2.0f, 1.0f, 64u, &badMesh);
    Check(badLodRej && badSizeRej && badBudgetRej && badArchRej,
          "worldgen_planted_failure_controls_reject");
}

}  // namespace raw::worldgen
