// test_pipeline_forge.cpp: Comprehensive unit & end-to-end tests for `raw::pipeline`
// (`RawAssetValidator` quality gates + `RawForgePipeline` seasonal flora, 6-biome diorama,
// 365-day orbital climate almanac, and physical optical material catalog bakers).
//
// Link directly into `ce_runtime_check` (call `raw::pipeline::RunPipelineForgeTests(Check)`)
// OR compile standalone with `-DRAW_PIPELINE_TEST_STANDALONE=1`!
// Original work, MIT.

#include "../src/pipeline/RawAssetValidator.h"
#include "../src/pipeline/RawForgePipeline.h"
#include "../src/worldgen/RawFloraEngine.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

namespace raw::pipeline {
namespace {

std::string ReadWholeFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

uint32_t CountLines(const std::string& s) {
    uint32_t lines = 0u;
    for (char c : s) {
        if (c == '\n') ++lines;
    }
    return lines;
}

}  // namespace

int RunPipelineForgeTests(void (*checkFn)(bool, const char*)) {
    using namespace raw::worldgen;

    int fails = 0;
    auto Report = [&](bool cond, const char* name) {
        if (!cond) ++fails;
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s %s\n", cond ? "PASS" : "FAIL", name);
        }
    };

    // 1. Valid biomechanical tree & geological prop meshes pass strict RawAssetValidator with 0 errors & valid JSON!
    ProceduralMesh validTree{}, validBasalt{};
    const bool builtTree = GenerateFloraMesh(MakeDefaultFloraParams(FloraSpecies::OldGrowthSitkaSpruce, 202607u, 1u), &validTree);
    const bool builtBasalt = BuildHexagonalBasaltCluster(101u, 7u, 90.0f, 160.0f, 126u, &validBasalt);
    const ValidationReport repTree = ValidateProceduralMesh(validTree);
    const ValidationReport repBasalt = ValidateProceduralMesh(validBasalt);
    const std::string treeJson = repTree.ToJson();
    Report(builtTree && builtBasalt && repTree.passed && repBasalt.passed &&
           repTree.TotalErrors() == 0u && repBasalt.TotalErrors() == 0u &&
           treeJson.find("\"passed\":true") != std::string::npos,
           "pipeline_validator_passes_valid_procedural_meshes");

    // 2. Planted-failure rejection: NaN vertex position must be caught in nonFiniteErrors!
    {
        ProceduralMesh badNan = validTree;
        badNan.vertices[0].pos[1] = std::numeric_limits<float>::quiet_NaN();
        const ValidationReport rep = ValidateProceduralMesh(badNan);
        Report(!rep.passed && rep.nonFiniteErrors >= 1u, "pipeline_validator_rejects_nan_vertex");
    }

    // 3. Planted-failure rejection: non-unit normal must be caught in normalLengthErrors!
    {
        ProceduralMesh badNorm = validTree;
        badNorm.vertices[0].normal[0] = 0.0f;
        badNorm.vertices[0].normal[1] = 0.0f;
        badNorm.vertices[0].normal[2] = 4.5f;
        const ValidationReport rep = ValidateProceduralMesh(badNorm);
        Report(!rep.passed && rep.normalLengthErrors >= 1u, "pipeline_validator_rejects_non_unit_normal");
    }

    // 4. Planted-failure rejection: oversized >126-tri or >64-vert meshlet must be caught in meshletLimitErrors!
    {
        ProceduralMesh badMl = validTree;
        badMl.meshlets[0].triangleCount = 127u;
        const ValidationReport repT = ValidateProceduralMesh(badMl);
        ProceduralMesh badMv = validTree;
        badMv.meshlets[0].vertexCount = 65u;
        const ValidationReport repV = ValidateProceduralMesh(badMv);
        Report(!repT.passed && repT.meshletLimitErrors >= 1u &&
               !repV.passed && repV.meshletLimitErrors >= 1u,
               "pipeline_validator_rejects_oversized_meshlet");
    }

    // 5. Planted-failure rejection: unnormalized 8-channel splat weight must be caught in splatWeightErrors!
    {
        ProceduralMesh badSplat = validTree;
        badSplat.vertices[0].splat0[0] = 1.5f;
        badSplat.vertices[0].splat1[1] = 1.2f;
        const ValidationReport rep = ValidateProceduralMesh(badSplat);
        Report(!rep.passed && rep.splatWeightErrors >= 1u, "pipeline_validator_rejects_unnormalized_splat");
    }

    // 6. Planted-failure rejection: degenerate zero-area triangle must be caught in degenerateTriangleErrors!
    {
        ProceduralMesh badDeg = validTree;
        const auto& ml = badDeg.meshlets[0];
        const uint32_t i0 = badDeg.indices[ml.triangleOffset + 0u];
        const uint32_t i1 = badDeg.indices[ml.triangleOffset + 1u];
        // Collapse vertex i1 onto vertex i0 -> zero area!
        badDeg.vertices[ml.vertexOffset + i1].pos[0] = badDeg.vertices[ml.vertexOffset + i0].pos[0];
        badDeg.vertices[ml.vertexOffset + i1].pos[1] = badDeg.vertices[ml.vertexOffset + i0].pos[1];
        badDeg.vertices[ml.vertexOffset + i1].pos[2] = badDeg.vertices[ml.vertexOffset + i0].pos[2];
        const ValidationReport rep = ValidateProceduralMesh(badDeg);
        Report(!rep.passed && rep.degenerateTriangleErrors >= 1u, "pipeline_validator_rejects_degenerate_triangle");
    }

    // 7. Planted-failure rejection: tight bounding sphere violation & VRAM budget violation!
    {
        ProceduralMesh badSphere = validTree;
        badSphere.meshlets[0].boundSphere[3] = 0.001f;
        const ValidationReport repS = ValidateProceduralMesh(badSphere);

        ValidatorPolicy tinyBudget{};
        tinyBudget.maxVramBytes = 256ull;
        const ValidationReport repB = ValidateProceduralMesh(validTree, tinyBudget);
        Report(!repS.passed && repS.boundSphereErrors >= 1u &&
               !repB.passed && repB.vramBudgetErrors >= 1u,
               "pipeline_validator_rejects_bound_sphere_and_vram_budget");
    }

    // Create isolated temporary test output directory (cleaned up after verification)
    const std::filesystem::path tmpRoot =
        std::filesystem::temp_directory_path() / "raw_forge_pipeline_unit_test";
    std::error_code ec;
    std::filesystem::remove_all(tmpRoot, ec);
    const std::string rootStr = tmpRoot.string();

    // 8. End-to-end Seasonal Flora Catalog (5 species x 4 seasons x 3 LODs = 60 validated meshes + flora_manifest.json)!
    const BakeResult floraRes = BakeSeasonalFloraCatalog(rootStr + "/flora", 202607u, ValidatorPolicy{}, true);
    const std::string floraJson = ReadWholeFile(floraRes.manifestPath);
    const bool floraGlbExists = std::filesystem::exists(rootStr + "/flora/OldGrowthSitkaSpruce_Winter_lod0.glb") &&
                                std::filesystem::exists(rootStr + "/flora/AncientGnarledOak_Autumn_lod2.obj");
    Report(floraRes.ok && floraRes.assetsGenerated == 60u && floraRes.validationFailures == 0u &&
           floraGlbExists && floraJson.find("OldGrowthSitkaSpruce_Winter_lod0") != std::string::npos &&
           floraJson.find("AlpineUnderstoryShrub_Spring_lod1") != std::string::npos,
           "pipeline_bake_seasonal_flora_catalog_60_assets");

    // 9. End-to-end 6-Biome World Showcase Diorama (6 terrains + 6 hero props + 6 biomechanical trees = 18 validated meshes)!
    const BakeResult worldRes = BakeWorldShowcaseDiorama(rootStr + "/diorama", 202607u, ValidatorPolicy{}, true);
    const std::string worldJson = ReadWholeFile(worldRes.manifestPath);
    Report(worldRes.ok && worldRes.assetsGenerated == 18u && worldRes.validationFailures == 0u &&
           worldJson.find("Glacial Fjord Spires") != std::string::npos &&
           worldJson.find("Bioluminescent Crystal Tundra") != std::string::npos,
           "pipeline_bake_world_showcase_diorama_6_biomes");

    // 10. End-to-end 365-Day Orbital Climate & Phenology Almanac (CSV 366 lines + JSON summary)!
    const BakeResult climRes = BakeAnnualClimateAlmanac(rootStr + "/almanac", 20261125u, 60.0f);
    const std::string almanacCsv = ReadWholeFile(rootStr + "/almanac/climate_almanac.csv");
    const std::string almanacJson = ReadWholeFile(climRes.manifestPath);
    Report(climRes.ok && climRes.assetsGenerated == 365u && climRes.validationFailures == 0u &&
           CountLines(almanacCsv) == 366u &&
           almanacJson.find("\"daysSimulated\": 365") != std::string::npos &&
           almanacJson.find("\"season\":\"Winter\"") != std::string::npos &&
           almanacJson.find("\"season\":\"Summer\"") != std::string::npos,
           "pipeline_bake_365_day_orbital_climate_almanac");

    // 11. End-to-end Physical Optical Material Catalog Manifest (JSON + standalone C++ header)!
    const BakeResult matRes = BakeMaterialCatalogManifest(rootStr + "/materials");
    const std::string matJson = ReadWholeFile(matRes.manifestPath);
    const std::string matHdr = ReadWholeFile(rootStr + "/materials/raw_materials_catalog.h");
    Report(matRes.ok && matRes.assetsGenerated >= 20u && matRes.validationFailures == 0u &&
           matJson.find("GlacialIce") != std::string::npos &&
           matHdr.find("kCanonicalOpticalTable") != std::string::npos,
           "pipeline_bake_material_catalog_manifest");

    std::filesystem::remove_all(tmpRoot, ec);
    return fails;
}

}  // namespace raw::pipeline

#ifdef RAW_PIPELINE_TEST_STANDALONE
int main() {
    const int fails = raw::pipeline::RunPipelineForgeTests(nullptr);
    std::printf("RawForgePipeline standalone test summary: %s (%d failures)\n",
                fails == 0 ? "ALL PASSED" : "FAILED", fails);
    return fails == 0 ? 0 : 1;
}
#endif
