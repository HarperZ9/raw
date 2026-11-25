#pragma once
// RawAssetValidator.h: Studio-Grade Geometry, Meshlet & Material Quality-Gate Verifier (`raw::pipeline`).
// Clean-room MIT C++23 framework with zero external/Skyrim/D3D12 header dependencies.
// Validates every `raw::worldgen::ProceduralMesh` against strict hardware DX12 AS+MS meshlet limits
// (<=64 verts, <=126 tris), finite attributes, unit normals, non-degenerate triangle area, normalized
// 8-channel PBR splat weights, tight bounding spheres, valid normal cones, and VRAM budget caps.
// Original work, MIT.

#include "../worldgen/RawMeshPipeline.h"

#include <cstdint>
#include <string>
#include <vector>

namespace raw::pipeline {

struct ValidatorPolicy {
    uint32_t maxMeshletVerts    = 64u;
    uint32_t maxMeshletTris     = 126u;
    float    maxNormalLenError  = 0.02f;
    float    maxSplatSumError   = 1.0e-3f;
    float    minTriangleAreaSq  = 1.0e-10f; // Minimum |Cross(e1, e2)|^2 for non-degenerate triangle
    float    boundSphereEpsilon = 1.0e-2f;
    uint64_t maxVramBytes       = 64ull * 1024ull * 1024ull; // 64 MB default per-asset GPU budget
    bool     requireNonEmpty    = true;
    bool     checkNormalCones   = true;
};

struct ValidationReport {
    std::string          assetName;
    bool                 passed                   = false;
    uint32_t             vertexCount              = 0u;
    uint32_t             triangleCount            = 0u;
    uint32_t             meshletCount             = 0u;
    uint32_t             maxMeshletVertsSeen      = 0u;
    uint32_t             maxMeshletTrisSeen       = 0u;
    uint64_t             estimatedGpuBytes        = 0u;
    raw::worldgen::Vec3  aabbMin{};
    raw::worldgen::Vec3  aabbMax{};

    // Granular quality-gate defect counters (all must be 0 for passed == true):
    uint32_t             nonFiniteErrors          = 0u;
    uint32_t             normalLengthErrors       = 0u;
    uint32_t             indexOutOfRangeErrors    = 0u;
    uint32_t             degenerateTriangleErrors = 0u;
    uint32_t             splatWeightErrors        = 0u;
    uint32_t             meshletLimitErrors       = 0u;
    uint32_t             boundSphereErrors        = 0u;
    uint32_t             normalConeErrors         = 0u;
    uint32_t             vramBudgetErrors         = 0u;

    std::vector<std::string> diagnostics; // Capped sample of human-readable defect messages

    uint32_t TotalErrors() const {
        return nonFiniteErrors + normalLengthErrors + indexOutOfRangeErrors +
               degenerateTriangleErrors + splatWeightErrors + meshletLimitErrors +
               boundSphereErrors + normalConeErrors + vramBudgetErrors;
    }

    std::string ToJson() const;
};

// Runs full quality-gate validation over a ProceduralMesh and returns a structured ValidationReport.
ValidationReport ValidateProceduralMesh(const raw::worldgen::ProceduralMesh& mesh,
                                        const ValidatorPolicy& policy = {});

// Convenience boolean wrapper writing the optional detailed report.
bool ValidateProceduralMeshStrict(const raw::worldgen::ProceduralMesh& mesh,
                                  ValidationReport* outReport = nullptr,
                                  const ValidatorPolicy& policy = {});

// Serializes a batch of validation reports into a clean JSON document with aggregate pass/fail totals.
std::string SerializeValidationBatchJson(const std::vector<ValidationReport>& reports);

}  // namespace raw::pipeline
