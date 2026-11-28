# RAW First-Principles Authoring Pipelines & SDK Reference

Clean-room MIT C++23 framework (`raw::pipeline`, `raw::worldgen`, `raw::materials`) with zero Skyrim, D3D12, or external asset dependencies. Designed for both **live Skyrim SE / RAW rendering** and **standalone general gamedev reuse** (glTF 2.0 `.glb`, Wavefront `.obj`, JSON/CSV manifests, and standalone C++ optical tables).

---

## 1. Architecture Overview

```
  RawWeatherSim (Orbital 365d Seasons, Thermals, Snow/Ice, Phenology)
        │
        ├──► RawFloraEngine (Space Colonization, da Vinci Pipe Model, 3D Foliage, Snow Sag)
        ├──► RawWorldShowcase / RawTerrainGen / RawMeshPipeline (6 Biomes, 64v/126t Meshlets)
        └──► RawMaterialLib (Measured Complex IOR, Cauchy Dispersion, Beer-Lambert, Planck)
                    │
                    ▼
          RawAssetValidator (Strict DX12 AS+MS Hardware & Physics Quality Gates)
                    │
                    ▼
          RawForgePipeline  ──►  raw_forge_cli  ──►  .glb / .obj / JSON / CSV / C++ Tables
```

### Core Modules
| Module | Files | Responsibility |
|---|---|---|
| **Asset Quality Gate** | `src/pipeline/RawAssetValidator.h/.cpp` | Verifies DX12 AS+MS meshlet limits (`<=64v`, `<=126t`), finiteness, unit normals, non-degenerate triangle area, normalized 8-channel PBR splats, tight bounding spheres, normal cones, and VRAM caps. |
| **Studio Batch Pipeline** | `src/pipeline/RawForgePipeline.h/.cpp` | Orchestrates deterministic batch baking & validation across Flora (60 assets), WorldGen Diorama (6 biomes / 18 meshes), 365-day Climate Almanac, and Physical Material Catalog. |
| **Standalone CLI** | `tools/raw_forge_cli.cpp` | CI-friendly command-line tool returning exit code `0` on 100% validation pass and non-zero on any defect. |
| **Verification Suite** | `tests/test_pipeline_forge.cpp` | Unit + planted-failure + end-to-end bake/manifest verification (`raw::pipeline::RunPipelineForgeTests`). |

---

## 2. Geometry & Meshlet Quality Gate (`RawAssetValidator`)

Every `raw::worldgen::ProceduralMesh` (`TerrainVertex` 80B + `MeshletDesc` 48B + local `uint32_t` indices) is validated before export or GPU upload:

```cpp
#include "pipeline/RawAssetValidator.h"

raw::pipeline::ValidatorPolicy policy{};
policy.maxMeshletVerts = 64u;
policy.maxMeshletTris  = 126u;
policy.maxVramBytes    = 64ull * 1024ull * 1024ull;

raw::pipeline::ValidationReport rep = raw::pipeline::ValidateProceduralMesh(mesh, policy);
if (!rep.passed) {
    std::fprintf(stderr, "%s\n", rep.ToJson().c_str());
}
```

### Enforced Hardware & Mathematical Invariants
1. **Zero NaN/Inf**: Every position, normal, UV, `splat0`, `splat1`, `meta`, bounding sphere, and normal cone float is verified finite.
2. **Unit Analytical Normals**: `|1.0 - |N|| <= 0.02`.
3. **8-Channel PBR Splat Normalization**: All channels non-negative and `sum(splat0) + sum(splat1) == 1.0 ± 1e-3`.
4. **DX12 Amplification/Mesh Shader Limits**: Every meshlet strictly satisfies `1 <= vertexCount <= 64` and `1 <= triangleCount <= 126`.
5. **Non-Degenerate Manifold Triangles**: Valid local indices (`< vertexCount`), distinct triangle vertices, and non-zero area (`|Cross(e1, e2)|^2 >= minTriangleAreaSq`).
6. **Tight Culling Bounds**: Positive finite `boundSphere.w` containing every meshlet vertex within epsilon, plus finite unit `normalCone` axis.
7. **VRAM Budget Gate**: `mesh.EstimateGpuBytes() <= policy.maxVramBytes`.

---

## 3. Studio Batch Bakers (`RawForgePipeline`)

| Function | Outputs | Validation Scope |
|---|---|---|
| `BakeSeasonalFloraCatalog(outDir, seed)` | 60 `.glb` + 60 `.obj` + `flora_manifest.json` | 5 hero species (`OldGrowthSitkaSpruce`, `WindsweptCoastalPine`, `NordicSilverBirch`, `AncientGnarledOak`, `AlpineUnderstoryShrub`) × 4 seasons (`Spring`, `Summer`, `Autumn`, `Winter`) × 3 LODs. |
| `BakeWorldShowcaseDiorama(outDir, seed)` | 18 `.glb` + 18 `.obj` + `world_manifest.json` | All 6 frontier biomes (terrain chunks + biome-matched biomechanical hero trees + geological hero props). |
| `BakeAnnualClimateAlmanac(outDir, seed, lat)` | `climate_almanac.csv` (365 daily rows) + `climate_summary.json` | Full orbital year: declination, day length, skin temp, dawn/midday inversion burn-off, snowpack depth/albedo, Stefan's law lake ice, and botanical phenology. |
| `BakeMaterialCatalogManifest(outDir)` | `materials_manifest.json` + `raw_materials_catalog.h` | Validates every `PhysicalMaterial` (`ValidateMaterialPhysics`), computes exact `F0`, and exports 64B GPU-aligned optical tables. |
| `RunFullPipelineValidation(outDir, ...)` | All 4 suites + `pipeline_validation_report.json` | Aggregate studio CI quality gate. |

---

## 4. `raw_forge_cli` Reference

```powershell
# Bake & validate all 60 seasonal biomechanical trees (.glb + .obj + flora_manifest.json)
raw_forge_cli --flora build/export/flora --seed 202607

# Bake & validate all 6 showcase frontier biomes + hero props
raw_forge_cli --diorama build/export/diorama --seed 202607

# Simulate & export a full 365-day orbital climate & botanical phenology almanac at 60°N
raw_forge_cli --almanac build/export/almanac --latitude 60.0

# Export physically-measured optical material tables (JSON + standalone C++ header)
raw_forge_cli --materials build/export/materials

# Run all 4 pipelines + hardware quality gates (add --no-write for fast dry-run CI validation)
raw_forge_cli --validate-all build/export/all --seed 202607 --latitude 60.0
```

- **Exit `0`**: 100% of generated assets, optical tables, and climate invariants passed validation.
- **Exit `1`**: One or more validation or export defects detected (details printed to `stderr` and JSON reports).
- **Exit `2`**: Invalid command-line arguments.

---

## 5. CMake Integration Notes

To wire into `CMakeLists.txt`:
1. Add `src/pipeline/RawAssetValidator.cpp` and `src/pipeline/RawForgePipeline.cpp` to a static library (or directly to `ce_runtime_check` & `raw_forge_cli`).
2. Add executable target `raw_forge_cli`:
   - Sources: `tools/raw_forge_cli.cpp`, `src/pipeline/RawAssetValidator.cpp`, `src/pipeline/RawForgePipeline.cpp`, `src/worldgen/RawTerrainGen.cpp`, `src/worldgen/RawBiomeWorld.cpp`, `src/worldgen/RawMeshPipeline.cpp`, `src/worldgen/RawFloraEngine.cpp`, `src/worldgen/RawWeatherSim.cpp`, `src/worldgen/RawWorldShowcase.cpp`, `src/materials/RawMaterialLib.cpp`.
3. Wire `tests/test_pipeline_forge.cpp` into `ce_runtime_check` and call `raw::pipeline::RunPipelineForgeTests(Check);` in `tests/ce_runtime_check/main.cpp` (or build standalone with `/DRAW_PIPELINE_TEST_STANDALONE=1`).
