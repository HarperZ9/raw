// RawAssetValidator.cpp: Studio-Grade Geometry, Meshlet & Material Quality-Gate Verifier (`raw::pipeline`).
// Original work, MIT.

#include "RawAssetValidator.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace raw::pipeline {
namespace {

constexpr size_t kMaxDiagnostics = 16u;

void PushDiag(ValidationReport& r, const std::string& msg) {
    if (r.diagnostics.size() < kMaxDiagnostics) {
        r.diagnostics.push_back(msg);
    }
}

std::string JsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4u);
    for (char c : s) {
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out.push_back(c); break;
        }
    }
    return out;
}

inline bool IsFinite3(const float v[3]) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

inline float SafeJsonFloat(float v) {
    return std::isfinite(v) ? v : 0.0f;
}

}  // namespace

std::string ValidationReport::ToJson() const {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(4);
    ss << "{"
       << "\"assetName\":\"" << JsonEscape(assetName) << "\","
       << "\"passed\":" << (passed ? "true" : "false") << ","
       << "\"totalErrors\":" << TotalErrors() << ","
       << "\"vertexCount\":" << vertexCount << ","
       << "\"triangleCount\":" << triangleCount << ","
       << "\"meshletCount\":" << meshletCount << ","
       << "\"maxMeshletVertsSeen\":" << maxMeshletVertsSeen << ","
       << "\"maxMeshletTrisSeen\":" << maxMeshletTrisSeen << ","
       << "\"estimatedGpuBytes\":" << estimatedGpuBytes << ","
       << "\"aabbMin\":[" << SafeJsonFloat(aabbMin.x) << "," << SafeJsonFloat(aabbMin.y) << "," << SafeJsonFloat(aabbMin.z) << "],"
       << "\"aabbMax\":[" << SafeJsonFloat(aabbMax.x) << "," << SafeJsonFloat(aabbMax.y) << "," << SafeJsonFloat(aabbMax.z) << "],"
       << "\"errors\":{"
       << "\"nonFinite\":" << nonFiniteErrors << ","
       << "\"normalLength\":" << normalLengthErrors << ","
       << "\"indexOutOfRange\":" << indexOutOfRangeErrors << ","
       << "\"degenerateTriangle\":" << degenerateTriangleErrors << ","
       << "\"splatWeight\":" << splatWeightErrors << ","
       << "\"meshletLimit\":" << meshletLimitErrors << ","
       << "\"boundSphere\":" << boundSphereErrors << ","
       << "\"normalCone\":" << normalConeErrors << ","
       << "\"vramBudget\":" << vramBudgetErrors
       << "},\"diagnostics\":[";
    for (size_t i = 0; i < diagnostics.size(); ++i) {
        if (i > 0u) ss << ",";
        ss << "\"" << JsonEscape(diagnostics[i]) << "\"";
    }
    ss << "]}";
    return ss.str();
}

ValidationReport ValidateProceduralMesh(const raw::worldgen::ProceduralMesh& mesh,
                                        const ValidatorPolicy& policy) {
    ValidationReport rep{};
    rep.assetName         = mesh.name.empty() ? "unnamed_mesh" : mesh.name;
    rep.vertexCount       = static_cast<uint32_t>(mesh.vertices.size());
    rep.meshletCount      = static_cast<uint32_t>(mesh.meshlets.size());
    rep.estimatedGpuBytes = mesh.EstimateGpuBytes();

    const uint32_t limVerts = std::clamp(policy.maxMeshletVerts, 3u, 64u);
    const uint32_t limTris  = std::clamp(policy.maxMeshletTris, 1u, 126u);

    // 1. Empty asset & VRAM budget gate
    if (policy.requireNonEmpty && (mesh.vertices.empty() || mesh.indices.empty() || mesh.meshlets.empty())) {
        ++rep.meshletLimitErrors;
        PushDiag(rep, "Empty mesh (zero vertices, indices, or meshlets).");
    }
    if (policy.maxVramBytes > 0ull && rep.estimatedGpuBytes > policy.maxVramBytes) {
        ++rep.vramBudgetErrors;
        PushDiag(rep, "Estimated GPU bytes (" + std::to_string(rep.estimatedGpuBytes) +
                      ") exceeds budget (" + std::to_string(policy.maxVramBytes) + ").");
    }

    // 2. Per-vertex attribute verification (finiteness, unit normals, 8-channel splat normalization, AABB)
    raw::worldgen::Vec3 mn{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    raw::worldgen::Vec3 mx{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};

    for (size_t vi = 0; vi < mesh.vertices.size(); ++vi) {
        const auto& v = mesh.vertices[vi];
        bool finiteOk = IsFinite3(v.pos) && IsFinite3(v.normal) &&
                        std::isfinite(v.uv[0]) && std::isfinite(v.uv[1]);
        for (int k = 0; k < 4; ++k) {
            if (!std::isfinite(v.splat0[k]) || !std::isfinite(v.splat1[k]) || !std::isfinite(v.meta[k])) {
                finiteOk = false;
            }
        }
        if (!finiteOk) {
            ++rep.nonFiniteErrors;
            PushDiag(rep, "Non-finite NaN/Inf attribute at vertex " + std::to_string(vi));
            continue;
        }

        mn.x = std::min(mn.x, v.pos[0]); mn.y = std::min(mn.y, v.pos[1]); mn.z = std::min(mn.z, v.pos[2]);
        mx.x = std::max(mx.x, v.pos[0]); mx.y = std::max(mx.y, v.pos[1]); mx.z = std::max(mx.z, v.pos[2]);

        const float nLen = std::hypot(std::hypot(v.normal[0], v.normal[1]), v.normal[2]);
        if (std::fabs(nLen - 1.0f) > policy.maxNormalLenError) {
            ++rep.normalLengthErrors;
            PushDiag(rep, "Non-unit normal (len=" + std::to_string(nLen) + ") at vertex " + std::to_string(vi));
        }

        float sSum = 0.0f;
        bool negSplat = false;
        for (int k = 0; k < 4; ++k) {
            if (v.splat0[k] < -1.0e-5f || v.splat1[k] < -1.0e-5f) negSplat = true;
            sSum += v.splat0[k] + v.splat1[k];
        }
        if (negSplat || std::fabs(sSum - 1.0f) > policy.maxSplatSumError) {
            ++rep.splatWeightErrors;
            PushDiag(rep, "Unnormalized or negative 8-channel splat (sum=" + std::to_string(sSum) +
                          ") at vertex " + std::to_string(vi));
        }
    }

    if (!mesh.vertices.empty() && mn.x <= mx.x) {
        rep.aabbMin = mn;
        rep.aabbMax = mx;
    }

    // 3. Per-meshlet DX12 AS+MS hardware compliance, bounding spheres, normal cones & triangle integrity
    uint32_t totalTris = 0u;
    for (size_t mi = 0; mi < mesh.meshlets.size(); ++mi) {
        const auto& ml = mesh.meshlets[mi];
        rep.maxMeshletVertsSeen = std::max(rep.maxMeshletVertsSeen, ml.vertexCount);
        rep.maxMeshletTrisSeen  = std::max(rep.maxMeshletTrisSeen, ml.triangleCount);

        if (ml.vertexCount == 0u || ml.vertexCount > limVerts ||
            ml.triangleCount == 0u || ml.triangleCount > limTris) {
            ++rep.meshletLimitErrors;
            PushDiag(rep, "Meshlet " + std::to_string(mi) + " violates hardware limits (v=" +
                          std::to_string(ml.vertexCount) + ", t=" + std::to_string(ml.triangleCount) + ").");
        }

        const bool vertSliceOk = (size_t(ml.vertexOffset) + size_t(ml.vertexCount) <= mesh.vertices.size());
        const bool idxSliceOk  = (size_t(ml.triangleOffset) + size_t(ml.triangleCount) * 3u <= mesh.indices.size());
        if (!vertSliceOk || !idxSliceOk) {
            ++rep.indexOutOfRangeErrors;
            PushDiag(rep, "Meshlet " + std::to_string(mi) + " buffer slice exceeds vertex/index array bounds.");
            continue;
        }

        // Bounding sphere finiteness & containment check
        if (!IsFinite3(ml.boundSphere) || !std::isfinite(ml.boundSphere[3]) || ml.boundSphere[3] <= 0.0f) {
            ++rep.boundSphereErrors;
            PushDiag(rep, "Meshlet " + std::to_string(mi) + " has invalid/non-positive bounding sphere.");
        } else {
            const float radTol = ml.boundSphere[3] + policy.boundSphereEpsilon;
            for (uint32_t vi = 0; vi < ml.vertexCount; ++vi) {
                const auto& v = mesh.vertices[ml.vertexOffset + vi];
                if (!IsFinite3(v.pos)) continue;
                const float d = std::hypot(std::hypot(v.pos[0] - ml.boundSphere[0], v.pos[1] - ml.boundSphere[1]),
                                           v.pos[2] - ml.boundSphere[2]);
                if (d > radTol) {
                    ++rep.boundSphereErrors;
                    PushDiag(rep, "Meshlet " + std::to_string(mi) + " vertex " + std::to_string(vi) +
                                  " outside boundSphere (dist=" + std::to_string(d) + " > r=" + std::to_string(ml.boundSphere[3]) + ").");
                    break;
                }
            }
        }

        // Normal cone check
        if (policy.checkNormalCones) {
            if (!IsFinite3(ml.normalCone) || !std::isfinite(ml.normalCone[3])) {
                ++rep.normalConeErrors;
                PushDiag(rep, "Meshlet " + std::to_string(mi) + " has non-finite normalCone.");
            } else {
                const float cLen = std::hypot(std::hypot(ml.normalCone[0], ml.normalCone[1]), ml.normalCone[2]);
                if (std::fabs(cLen - 1.0f) > 0.05f || ml.normalCone[3] < -1.05f || ml.normalCone[3] > 1.05f) {
                    ++rep.normalConeErrors;
                    PushDiag(rep, "Meshlet " + std::to_string(mi) + " normalCone axis/cutoff out of valid range.");
                }
            }
        }

        // Per-triangle index & non-zero area check
        for (uint32_t ti = 0; ti < ml.triangleCount; ++ti) {
            const size_t base = size_t(ml.triangleOffset) + size_t(ti) * 3u;
            const uint32_t i0 = mesh.indices[base + 0u];
            const uint32_t i1 = mesh.indices[base + 1u];
            const uint32_t i2 = mesh.indices[base + 2u];
            if (i0 >= ml.vertexCount || i1 >= ml.vertexCount || i2 >= ml.vertexCount) {
                ++rep.indexOutOfRangeErrors;
                PushDiag(rep, "Meshlet " + std::to_string(mi) + " tri " + std::to_string(ti) + " local index out of range.");
                continue;
            }
            if (i0 == i1 || i1 == i2 || i2 == i0) {
                ++rep.degenerateTriangleErrors;
                PushDiag(rep, "Meshlet " + std::to_string(mi) + " tri " + std::to_string(ti) + " has duplicate vertex indices.");
                continue;
            }
            const auto& v0 = mesh.vertices[ml.vertexOffset + i0];
            const auto& v1 = mesh.vertices[ml.vertexOffset + i1];
            const auto& v2 = mesh.vertices[ml.vertexOffset + i2];
            if (IsFinite3(v0.pos) && IsFinite3(v1.pos) && IsFinite3(v2.pos)) {
                const raw::worldgen::Vec3 e1{v1.pos[0] - v0.pos[0], v1.pos[1] - v0.pos[1], v1.pos[2] - v0.pos[2]};
                const raw::worldgen::Vec3 e2{v2.pos[0] - v0.pos[0], v2.pos[1] - v0.pos[1], v2.pos[2] - v0.pos[2]};
                const raw::worldgen::Vec3 cr = raw::worldgen::Cross(e1, e2);
                const float areaSq = raw::worldgen::Dot(cr, cr);
                if (!std::isfinite(areaSq) || areaSq < policy.minTriangleAreaSq) {
                    ++rep.degenerateTriangleErrors;
                    PushDiag(rep, "Meshlet " + std::to_string(mi) + " tri " + std::to_string(ti) + " has zero/degenerate area.");
                }
            }
        }
        totalTris += ml.triangleCount;
    }

    rep.triangleCount = totalTris;
    rep.passed = (rep.TotalErrors() == 0u) && (!policy.requireNonEmpty || rep.triangleCount > 0u);
    return rep;
}

bool ValidateProceduralMeshStrict(const raw::worldgen::ProceduralMesh& mesh,
                                  ValidationReport* outReport,
                                  const ValidatorPolicy& policy) {
    ValidationReport rep = ValidateProceduralMesh(mesh, policy);
    const bool ok = rep.passed;
    if (outReport) *outReport = std::move(rep);
    return ok;
}

std::string SerializeValidationBatchJson(const std::vector<ValidationReport>& reports) {
    uint32_t passedCount = 0u;
    uint32_t failedCount = 0u;
    uint64_t totalGpuBytes = 0ull;
    uint32_t totalMeshlets = 0u;
    uint32_t totalTris = 0u;
    for (const auto& r : reports) {
        if (r.passed) ++passedCount;
        else          ++failedCount;
        totalGpuBytes += r.estimatedGpuBytes;
        totalMeshlets += r.meshletCount;
        totalTris     += r.triangleCount;
    }

    std::ostringstream ss;
    ss << "{\"totalAssets\":" << reports.size()
       << ",\"passedAssets\":" << passedCount
       << ",\"failedAssets\":" << failedCount
       << ",\"totalMeshlets\":" << totalMeshlets
       << ",\"totalTriangles\":" << totalTris
       << ",\"totalGpuBytes\":" << totalGpuBytes
       << ",\"reports\":[";
    for (size_t i = 0; i < reports.size(); ++i) {
        if (i > 0u) ss << ",";
        ss << reports[i].ToJson();
    }
    ss << "]}";
    return ss.str();
}

}  // namespace raw::pipeline
