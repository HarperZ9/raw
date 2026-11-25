#pragma once
// RawForgePipeline.h: Unified Studio Authoring, Baking & Validation Pipeline (`raw::pipeline`).
// Clean-room MIT C++23 framework orchestrating RAW's first-principles engines for both general
// gamedev reuse (glTF 2.0 .glb / Wavefront .obj / JSON / CSV / C++ tables) and Skyrim overrides:
//   - BakeSeasonalFloraCatalog  : 5 hero species x 4 orbital seasons x 3 LODs (60 validated meshes + flora_manifest.json)
//   - BakeWorldShowcaseDiorama  : 6 frontier biomes + biomechanical hero trees & geological props + world_manifest.json
//   - BakeAnnualClimateAlmanac  : 365-day orbital climate/thermal/snow/ice/phenology simulation -> CSV + JSON summary
//   - BakeMaterialCatalogManifest: Physically-measured optical tables (IOR, Cauchy, Beer-Lambert, F0) -> JSON + C++ header
// Original work, MIT.

#include "RawAssetValidator.h"

#include <cstdint>
#include <string>
#include <vector>

namespace raw::pipeline {

struct BakeResult {
    bool                          ok                 = false;
    uint32_t                      assetsGenerated    = 0u;
    uint32_t                      assetsValidated    = 0u;
    uint32_t                      validationFailures = 0u;
    uint32_t                      filesWritten       = 0u;
    uint64_t                      totalGpuBytes      = 0ull;
    uint32_t                      totalMeshlets      = 0u;
    uint32_t                      totalTriangles     = 0u;
    std::string                   manifestPath;
    std::string                   summaryMessage;
    std::vector<ValidationReport> reports;
};

// Generates, validates, and exports all 5 biomechanical flora species x 4 seasons (Spring, Summer, Autumn, Winter)
// x 3 LODs (LOD0 hero, LOD1 mid, LOD2 distant forest) -> .glb + .obj + flora_manifest.json.
BakeResult BakeSeasonalFloraCatalog(const std::string& outDir,
                                    uint32_t seed = 202607u,
                                    const ValidatorPolicy& policy = {},
                                    bool writeGeometryFiles = true);

// Generates, validates, and exports all 6 showcase frontier biomes + biome-matched biomechanical trees
// and geological hero props -> .glb + .obj + world_manifest.json.
BakeResult BakeWorldShowcaseDiorama(const std::string& outDir,
                                    uint32_t seed = 202607u,
                                    const ValidatorPolicy& policy = {},
                                    bool writeGeometryFiles = true);

// Steps RawWeatherSim across a full 365-day orbital year at `latitudeDeg` (daily solar declination, day length,
// skin temperature, snowpack depth & metamorphic albedo, Stefan's law lake ice thickness, dawn inversion burn-off,
// and 4-season botanical phenology) -> exports climate_almanac.csv + climate_summary.json.
BakeResult BakeAnnualClimateAlmanac(const std::string& outDir,
                                    uint32_t seed = 20261125u,
                                    float latitudeDeg = 60.0f);

// Validates and serializes RawMaterialLib's physically-measured optical registry (IOR, Cauchy dispersion,
// Beer-Lambert absorption, roughness, F0, Planck blackbody, thin-film OPD) -> materials_manifest.json + raw_materials_catalog.h.
BakeResult BakeMaterialCatalogManifest(const std::string& outDir);

// Runs all 4 studio baking & quality-gate validation pipelines and emits pipeline_validation_report.json.
BakeResult RunFullPipelineValidation(const std::string& outDir,
                                     uint32_t seed = 202607u,
                                     float latitudeDeg = 60.0f,
                                     const ValidatorPolicy& policy = {},
                                     bool writeGeometryFiles = true);

}  // namespace raw::pipeline
