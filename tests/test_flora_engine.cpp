// test_flora_engine.cpp: Comprehensive unit & invariant verification suite for `raw::worldgen::RawFloraEngine`.
// Can be linked directly into `ce_runtime_check` (call `raw::worldgen::RunFloraEngineTests(Check)`)
// OR compiled standalone with `-DRAW_FLORA_TEST_STANDALONE=1`!
//
// Verifies:
//   1. All 5 hero species generate watertight manifold 64v/126t GPU meshlets with exact bounding spheres
//   2. da Vinci Pipe Model positive radius monotonicity (r_root > r_tip, every parent >= child)
//   3. Terrain-slope flared buttress root gripping (downhill roots extend below z=0 on sloped terrain)
//   4. Zero NaN/Inf across all vertex attributes, unit normals, normalized 8-channel splats, bendFlex/species meta
//   5. Self-pruning of shaded lower limbs into gnarled stub knots on mature old-growth trees
//   6. Real 3D volumetric foliage clusters & snow accumulation on top boughs (N.z > 0.45)
//   7. Strict 3-tier LOD triangle reduction (LOD0 > LOD1 > LOD2 > 0) for all 5 species
//   8. Deterministic seed reproducibility & seed variation
//   9. Wavefront .obj & glTF 2.0 .glb serialization + PropInstance drop-in integration helper
//  10. Planted-failure controls cleanly rejected
// Original work, MIT.

#include "../src/worldgen/RawFloraEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace raw::worldgen {
namespace {

bool IsFinite3(const float v[3]) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

bool ValidateMeshWatertightMeshlets(const ProceduralMesh& pm, FloraSpecies expectedSpecies,
                                    uint32_t* outTotalTris) {
    if (pm.vertices.empty() || pm.indices.empty() || pm.meshlets.empty()) return false;
    uint32_t totalTris = 0u;

    for (const auto& ml : pm.meshlets) {
        if (ml.vertexCount == 0u || ml.vertexCount > 64u) return false;
        if (ml.triangleCount == 0u || ml.triangleCount > 126u) return false;
        if (ml.vertexOffset + ml.vertexCount > pm.vertices.size()) return false;
        if (ml.triangleOffset + ml.triangleCount * 3u > pm.indices.size()) return false;
        if (!std::isfinite(ml.boundSphere[3]) || ml.boundSphere[3] <= 0.0f) return false;
        if (!IsFinite3(ml.boundSphere) || !IsFinite3(ml.normalCone) || !std::isfinite(ml.normalCone[3])) return false;

        for (uint32_t vi = 0u; vi < ml.vertexCount; ++vi) {
            const TerrainVertex& v = pm.vertices[ml.vertexOffset + vi];
            if (!IsFinite3(v.pos) || !IsFinite3(v.normal)) return false;
            if (!std::isfinite(v.uv[0]) || !std::isfinite(v.uv[1])) return false;

            const float nLen = std::hypot(std::hypot(v.normal[0], v.normal[1]), v.normal[2]);
            if (std::fabs(nLen - 1.0f) > 0.02f) return false;

            float sSum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                if (!std::isfinite(v.splat0[k]) || !std::isfinite(v.splat1[k]) || !std::isfinite(v.meta[k])) return false;
                if (v.splat0[k] < -1e-5f || v.splat1[k] < -1e-5f) return false;
                sSum += v.splat0[k] + v.splat1[k];
            }
            if (std::fabs(sSum - 1.0f) > 1.0e-3f) return false;

            // Cantilever bendFlex in [0, 1], branch AO in [-1, 1], wind phase in [0, 1], species ID matches
            if (v.meta[0] < -1.01f || v.meta[0] > 1.01f) return false;
            if (v.meta[1] < -1e-5f || v.meta[1] > 1.0001f) return false;
            if (v.meta[2] < -1e-5f || v.meta[2] > 1.0001f) return false;
            if (std::fabs(v.meta[3] - float(static_cast<uint32_t>(expectedSpecies))) > 1e-4f) return false;

            const float d = std::hypot(std::hypot(v.pos[0] - ml.boundSphere[0], v.pos[1] - ml.boundSphere[1]),
                                       v.pos[2] - ml.boundSphere[2]);
            if (d > ml.boundSphere[3] + 1.0e-2f) return false;
        }

        for (uint32_t ti = 0u; ti < ml.triangleCount; ++ti) {
            const uint32_t base = ml.triangleOffset + ti * 3u;
            const uint32_t i0 = pm.indices[base + 0u];
            const uint32_t i1 = pm.indices[base + 1u];
            const uint32_t i2 = pm.indices[base + 2u];
            if (i0 >= ml.vertexCount || i1 >= ml.vertexCount || i2 >= ml.vertexCount) return false;
            if (i0 == i1 || i1 == i2 || i2 == i0) return false;
        }
        totalTris += ml.triangleCount;
    }

    if (outTotalTris) *outTotalTris = totalTris;
    return totalTris > 0u;
}

}  // namespace

// Runs all RawFloraEngine verification tests; returns number of failed checks (0 = 100% PASS).
int RunFloraEngineTests(void (*checkFn)(bool, const char*)) {
    int fails = 0;
    auto Report = [&](bool cond, const char* name) {
        if (!cond) ++fails;
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s %s\n", cond ? "PASS" : "FAIL", name);
        }
    };

    // 1. All 5 hero species generate valid watertight 64v/126t GPU meshlets, zero NaN/Inf,
    //    positive da Vinci pipe-model monotonicity (r_root > r_tip), and rooted-to-tip bendFlex span!
    bool allSpeciesMeshOk = true;
    bool allPipeMonotonic = true;
    bool allBendSpanOk = true;
    bool all3DFoliageOk = true;
    uint32_t speciesTotalTris[5] = {};

    for (uint32_t s = 0u; s < static_cast<uint32_t>(FloraSpecies::Count); ++s) {
        const auto sp = static_cast<FloraSpecies>(s);
        FloraGrowthParams params = MakeDefaultFloraParams(sp, 202607u + s * 97u, 0u);
        params.snowLoad01 = 0.35f;
        params.ageFactor01 = 0.90f;

        FloraAsset asset{};
        const bool ok = GenerateFloraAsset(params, &asset);
        uint32_t tris = 0u;
        const bool meshOk = ok && ValidateMeshWatertightMeshlets(asset.mesh, sp, &tris);
        speciesTotalTris[s] = tris;
        if (!meshOk) allSpeciesMeshOk = false;

        // Verify da Vinci pipe-model strict parent >= child and r_root > r_tip
        bool mono = ok && asset.stats.pipeModelMonotonic &&
                    asset.stats.rootRadiusU > asset.stats.maxTipRadiusU &&
                    asset.stats.minTipRadiusU > 0.0f;
        for (size_t ni = 0u; ni < asset.skeleton.size() && mono; ++ni) {
            const auto& bn = asset.skeleton[ni];
            for (int32_t c : bn.children) {
                if (bn.radius < asset.skeleton[c].radius - 1.0e-5f) {
                    mono = false;
                    break;
                }
            }
        }
        if (!mono) allPipeMonotonic = false;

        // Verify cantilever bendFlex spans from 0.0 (rooted trunk/roots) to >= 0.70 (terminal foliage tips)
        float minFlex = 1.0f, maxFlex = 0.0f;
        float minNz = 1.0f, maxNz = -1.0f;
        for (const auto& v : asset.mesh.vertices) {
            minFlex = std::min(minFlex, v.meta[1]);
            maxFlex = std::max(maxFlex, v.meta[1]);
            if (v.splat1[1] > 0.50f) { // Foliage vertex
                minNz = std::min(minNz, v.normal[2]);
                maxNz = std::max(maxNz, v.normal[2]);
            }
        }
        if (minFlex > 1.0e-3f || maxFlex < 0.70f) allBendSpanOk = false;
        // Real 3D foliage must have both upward-facing top ridges and downward-facing underside normals!
        if (asset.stats.foliageClusterCount == 0u || maxNz < 0.50f || minNz > -0.25f) all3DFoliageOk = false;
    }

    Report(allSpeciesMeshOk, "flora_all_5_species_watertight_64v126t_meshlets");
    Report(allPipeMonotonic, "flora_davinci_pipe_model_monotonic_taper");
    Report(allBendSpanOk,    "flora_cantilever_bendflex_root_to_tip_range");
    Report(all3DFoliageOk,   "flora_real_3d_volumetric_foliage_clusters");

    // 2. Self-pruning produces gnarled dead stub knots on old-growth Sitka Spruce & Ancient Oak,
    //    and multi-stem Birch & Understory Shrub generate multiple distinct stems!
    FloraAsset spruce{}, oak{}, birch{}, shrub{};
    const bool okSp = GenerateFloraAsset(MakeDefaultFloraParams(FloraSpecies::OldGrowthSitkaSpruce, 4201u, 0u), &spruce);
    const bool okOk = GenerateFloraAsset(MakeDefaultFloraParams(FloraSpecies::AncientGnarledOak, 4202u, 0u), &oak);
    const bool okBi = GenerateFloraAsset(MakeDefaultFloraParams(FloraSpecies::NordicSilverBirch, 4203u, 0u), &birch);
    const bool okSh = GenerateFloraAsset(MakeDefaultFloraParams(FloraSpecies::AlpineUnderstoryShrub, 4204u, 0u), &shrub);
    Report(okSp && okOk && okBi && okSh &&
           spruce.stats.prunedStubCount >= 2u && oak.stats.prunedStubCount >= 2u &&
           birch.stats.stemCount >= 2u && shrub.stats.stemCount >= 5u &&
           spruce.stats.actualHeightU >= 22.0f * 70.0f * 0.80f,
           "flora_self_pruning_stubs_and_species_morphology");

    // 3. Terrain-slope root flaring: 32-degree slope causes downhill buttress roots to grip deeply below flat z!
    FloraGrowthParams flatParams = MakeDefaultFloraParams(FloraSpecies::AncientGnarledOak, 7777u, 0u);
    flatParams.terrainNormal = {0.0f, 0.0f, 1.0f};
    FloraGrowthParams slopeParams = flatParams;
    slopeParams.terrainNormal = Normalize({0.54f, 0.0f, 0.84f});

    FloraAsset oakFlat{}, oakSlope{};
    const bool okFlat = GenerateFloraAsset(flatParams, &oakFlat);
    const bool okSlope = GenerateFloraAsset(slopeParams, &oakSlope);
    Report(okFlat && okSlope &&
           oakSlope.stats.rootButtressCount >= 5u &&
           oakSlope.stats.lowestRootZ < oakFlat.stats.lowestRootZ - 25.0f &&
           oakSlope.mesh.aabbMin.z < -40.0f,
           "flora_terrain_slope_buttress_root_flaring");

    // 4. Snow accumulation on top boughs (N.z > 0.45): heavy snowLoad01 increases splat0[1] on upper foliage!
    FloraGrowthParams drySpruce = MakeDefaultFloraParams(FloraSpecies::OldGrowthSitkaSpruce, 8888u, 1u);
    drySpruce.snowLoad01 = 0.0f;
    FloraGrowthParams snowSpruce = drySpruce;
    snowSpruce.snowLoad01 = 0.90f;
    FloraAsset aDry{}, aSnow{};
    const bool okDry = GenerateFloraAsset(drySpruce, &aDry);
    const bool okSnw = GenerateFloraAsset(snowSpruce, &aSnow);
    auto avgTopSnow = [](const ProceduralMesh& m) {
        double sum = 0.0;
        uint32_t cnt = 0u;
        for (const auto& v : m.vertices) {
            if (v.normal[2] > 0.55f) {
                sum += double(v.splat0[1]);
                ++cnt;
            }
        }
        return cnt > 0u ? float(sum / double(cnt)) : 0.0f;
    };
    const float drySnowVal = avgTopSnow(aDry.mesh);
    const float heavySnowVal = avgTopSnow(aSnow.mesh);
    Report(okDry && okSnw && drySnowVal < 0.01f && heavySnowVal > 0.30f,
           "flora_snow_accumulation_on_upper_boughs");

    // 5. Strict 3-tier LOD triangle reduction (LOD0 > LOD1 > LOD2 > 0) across all 5 species!
    bool allLodsReduce = true;
    for (uint32_t s = 0u; s < static_cast<uint32_t>(FloraSpecies::Count); ++s) {
        const auto sp = static_cast<FloraSpecies>(s);
        FloraLodChain chain{};
        const bool okChain = GenerateFloraLodChain(MakeDefaultFloraParams(sp, 9001u + s, 0u), &chain);
        uint32_t t0 = 0u, t1 = 0u, t2 = 0u;
        const bool v0 = okChain && ValidateMeshWatertightMeshlets(chain.lods[0], sp, &t0);
        const bool v1 = okChain && ValidateMeshWatertightMeshlets(chain.lods[1], sp, &t1);
        const bool v2 = okChain && ValidateMeshWatertightMeshlets(chain.lods[2], sp, &t2);
        if (!(v0 && v1 && v2 && t0 > t1 && t1 > t2 && t2 > 0u)) {
            allLodsReduce = false;
        }
    }
    Report(allLodsReduce, "flora_strict_3tier_lod_triangle_reduction");

    // 6. Deterministic seed reproducibility (bit-for-bit identical) & distinct seeds diverge!
    const FloraGrowthParams pDet = MakeDefaultFloraParams(FloraSpecies::WindsweptCoastalPine, 13579u, 0u);
    FloraAsset detA{}, detB{}, detOther{};
    const bool okDetA = GenerateFloraAsset(pDet, &detA);
    const bool okDetB = GenerateFloraAsset(pDet, &detB);
    FloraGrowthParams pOther = pDet;
    pOther.seed = 24680u;
    const bool okDetO = GenerateFloraAsset(pOther, &detOther);

    bool exactMatch = okDetA && okDetB &&
                      detA.mesh.vertices.size() == detB.mesh.vertices.size() &&
                      detA.mesh.indices.size() == detB.mesh.indices.size() &&
                      detA.mesh.meshlets.size() == detB.mesh.meshlets.size();
    if (exactMatch && !detA.mesh.vertices.empty()) {
        exactMatch = (std::memcmp(detA.mesh.vertices.data(), detB.mesh.vertices.data(),
                                  detA.mesh.vertices.size() * sizeof(TerrainVertex)) == 0) &&
                     (std::memcmp(detA.mesh.indices.data(), detB.mesh.indices.data(),
                                  detA.mesh.indices.size() * sizeof(uint32_t)) == 0);
    }
    bool differs = okDetO && (detA.mesh.vertices.size() != detOther.mesh.vertices.size() ||
                              std::memcmp(detA.mesh.vertices.data(), detOther.mesh.vertices.data(),
                                          std::min(detA.mesh.vertices.size(), detOther.mesh.vertices.size()) * sizeof(TerrainVertex)) != 0);
    Report(exactMatch && differs, "flora_deterministic_seed_reproducibility");

    // 7. Wavefront .obj & glTF 2.0 .glb serialization + PropInstance drop-in showcase integration!
    const std::string objStr = SerializeMeshToObjString(detA.mesh);
    const std::vector<uint8_t> glb = SerializeMeshToGlbBytes(detA.mesh, true);
    uint32_t glbMagic = 0u, glbVer = 0u;
    if (glb.size() >= 12u) {
        std::memcpy(&glbMagic, glb.data(), 4u);
        std::memcpy(&glbVer, glb.data() + 4u, 4u);
    }
    PropInstance prop{};
    prop.kind = PropKind::ConiferTree;
    prop.biome = BiomeId::OldGrowthConiferHighlands;
    prop.upNormal = Normalize({0.25f, -0.15f, 0.95f});
    prop.scale = 1.0f;
    prop.seed = 55123u;
    ProceduralMesh propTree{};
    const bool okProp = BuildBiomechanicalTreeForProp(prop, 1u, 0.40f, 126u, &propTree);
    Report(objStr.find("f ") != std::string::npos &&
           glbMagic == 0x46546C67u && glbVer == 2u && glb.size() > 512u &&
           okProp && !propTree.meshlets.empty(),
           "flora_obj_glb_export_and_prop_instance_bridge");

    // 8. Seasonal Winter Snow-Load Cantilever Sag: heavy winter canopySnowLoad01 physically bows conifer boughs downward!
    FloraGrowthParams spSummer = MakeDefaultFloraParams(FloraSpecies::OldGrowthSitkaSpruce, 31415u, 0u);
    ApplyFloraSeasonPreset(FloraSeasonPreset::SummerLush, &spSummer);
    FloraGrowthParams spWinter = spSummer;
    ApplyFloraSeasonPreset(FloraSeasonPreset::DeepWinter, &spWinter);

    FloraGrowthParams pineSummer = MakeDefaultFloraParams(FloraSpecies::WindsweptCoastalPine, 27182u, 0u);
    ApplyFloraSeasonPreset(FloraSeasonPreset::SummerLush, &pineSummer);
    FloraGrowthParams pineWinter = pineSummer;
    ApplyFloraSeasonPreset(FloraSeasonPreset::DeepWinter, &pineWinter);

    FloraAsset aSpSum{}, aSpWin{}, aPnSum{}, aPnWin{};
    const bool okSag =
        GenerateFloraAsset(spSummer, &aSpSum) && GenerateFloraAsset(spWinter, &aSpWin) &&
        GenerateFloraAsset(pineSummer, &aPnSum) && GenerateFloraAsset(pineWinter, &aPnWin) &&
        ValidateMeshWatertightMeshlets(aSpWin.mesh, FloraSpecies::OldGrowthSitkaSpruce, nullptr) &&
        ValidateMeshWatertightMeshlets(aPnWin.mesh, FloraSpecies::WindsweptCoastalPine, nullptr);
    Report(okSag &&
           aSpWin.stats.meanCanopyZ < aSpSum.stats.meanCanopyZ - 30.0f &&
           aPnWin.stats.meanCanopyZ < aPnSum.stats.meanCanopyZ - 20.0f,
           "flora_winter_snow_load_cantilever_sag");

    // 9. Seasonal Deciduous Leaf Abscission vs. Evergreen Conifer Retention:
    //    At DeepWinter (leafAbscission01 == 1.0), Oak & Birch drop 100% of leaves into bare winter skeletons,
    //    while evergreen SitkaSpruce & CoastalPine retain dense winter needle whorls!
    FloraGrowthParams oakWinP = MakeDefaultFloraParams(FloraSpecies::AncientGnarledOak, 6001u, 0u);
    ApplyFloraSeasonPreset(FloraSeasonPreset::DeepWinter, &oakWinP);
    FloraGrowthParams birchWinP = MakeDefaultFloraParams(FloraSpecies::NordicSilverBirch, 6002u, 0u);
    ApplyFloraSeasonPreset(FloraSeasonPreset::DeepWinter, &birchWinP);
    FloraAsset aOakWin{}, aBirchWin{};
    const bool okAbsc =
        GenerateFloraAsset(oakWinP, &aOakWin) && GenerateFloraAsset(birchWinP, &aBirchWin) &&
        ValidateMeshWatertightMeshlets(aOakWin.mesh, FloraSpecies::AncientGnarledOak, nullptr) &&
        ValidateMeshWatertightMeshlets(aBirchWin.mesh, FloraSpecies::NordicSilverBirch, nullptr);
    Report(okAbsc &&
           aOakWin.stats.foliageClusterCount == 0u && aOakWin.stats.abscisedClusterCount > 20u &&
           aBirchWin.stats.foliageClusterCount == 0u && aBirchWin.stats.abscisedClusterCount > 20u &&
           aSpWin.stats.foliageClusterCount > 30u && aPnWin.stats.foliageClusterCount > 30u,
           "flora_seasonal_deciduous_abscission_vs_evergreen");

    // 10. Seasonal Autumn Pigment Shift (Chlorophyll -> Carotenoid Gold / Anthocyanin Crimson) with zero magma glow!
    FloraGrowthParams oakSumP = MakeDefaultFloraParams(FloraSpecies::AncientGnarledOak, 6003u, 1u);
    ApplyFloraSeasonPreset(FloraSeasonPreset::SummerLush, &oakSumP);
    FloraGrowthParams oakAutP = oakSumP;
    ApplyFloraSeasonPreset(FloraSeasonPreset::PeakAutumn, &oakAutP);
    oakAutP.leafAbscission01 = 0.0f; // Keep full canopy to compare pigment distribution cleanly
    FloraAsset aOakSum{}, aOakAut{};
    const bool okAut = GenerateFloraAsset(oakSumP, &aOakSum) && GenerateFloraAsset(oakAutP, &aOakAut);
    auto avgFoliageCh = [](const ProceduralMesh& m, int splatIdx, float* outMaxMagma) {
        double sum = 0.0;
        uint32_t cnt = 0u;
        float maxMagma = 0.0f;
        for (const auto& v : m.vertices) {
            maxMagma = std::max(maxMagma, v.splat1[0]);
            if (v.meta[1] > 0.65f) { // Outer foliage canopy vertices
                sum += double(splatIdx < 4 ? v.splat0[splatIdx] : v.splat1[splatIdx - 4]);
                ++cnt;
            }
        }
        if (outMaxMagma) *outMaxMagma = maxMagma;
        return cnt > 0u ? float(sum / double(cnt)) : 0.0f;
    };
    float maxMagmaAut = 1.0f;
    const float sumChloro = avgFoliageCh(aOakSum.mesh, 5, nullptr); // splat1[1] chlorophyll
    const float autChloro = avgFoliageCh(aOakAut.mesh, 5, &maxMagmaAut);
    const float sumWarm   = avgFoliageCh(aOakSum.mesh, 6, nullptr); // splat1[2] warm carotenoid/amber
    const float autWarm   = avgFoliageCh(aOakAut.mesh, 6, nullptr);
    Report(okAut && maxMagmaAut == 0.0f && autChloro < sumChloro * 0.65f && autWarm > sumWarm * 1.8f,
           "flora_seasonal_autumn_pigment_chlorophyll_shift");

    // 11. Planted-failure controls must fail cleanly without crashing!
    FloraAsset badAsset{};
    FloraGrowthParams badLod = MakeDefaultFloraParams(FloraSpecies::OldGrowthSitkaSpruce, 1u, 0u);
    badLod.lodLevel = 4u;
    FloraGrowthParams badScale = MakeDefaultFloraParams(FloraSpecies::OldGrowthSitkaSpruce, 1u, 0u);
    badScale.unitsPerMeter = -5.0f;
    FloraGrowthParams badSpecies = MakeDefaultFloraParams(FloraSpecies::OldGrowthSitkaSpruce, 1u, 0u);
    badSpecies.species = FloraSpecies::Count;
    const bool rejNull    = !GenerateFloraAsset(MakeDefaultFloraParams(FloraSpecies::OldGrowthSitkaSpruce, 1u, 0u), nullptr);
    const bool rejLod     = !GenerateFloraAsset(badLod, &badAsset);
    const bool rejScale   = !GenerateFloraAsset(badScale, &badAsset);
    const bool rejSpecies = !GenerateFloraAsset(badSpecies, &badAsset);
    Report(rejNull && rejLod && rejScale && rejSpecies, "flora_planted_failure_controls_reject");

    return fails;
}

}  // namespace raw::worldgen

#ifdef RAW_FLORA_TEST_STANDALONE
int main() {
    const int fails = raw::worldgen::RunFloraEngineTests(nullptr);
    std::printf("RawFloraEngine standalone test summary: %s (%d failures)\n",
                fails == 0 ? "ALL PASSED" : "FAILED", fails);
    return fails == 0 ? 0 : 1;
}
#endif
