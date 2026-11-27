// CeTopologyRepair.cpp: Wave 85 Clean-Room NIF Geometry Topology Repair & Plain-English
// Corrupt-NIF Diagnostics. Original work, MIT.
#include "CeTopologyRepair.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <sstream>
#include <unordered_map>

namespace raw::ce
{
namespace
{

struct Vec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

inline bool IsFinite3(const float v[3]) noexcept
{
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

inline bool IsFinite2(const float v[2]) noexcept
{
    return std::isfinite(v[0]) && std::isfinite(v[1]);
}

inline Vec3 Sub(const Vec3& a, const Vec3& b) noexcept
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline Vec3 Cross(const Vec3& a, const Vec3& b) noexcept
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

inline float Dot(const Vec3& a, const Vec3& b) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline float Length(const Vec3& v) noexcept
{
    return std::sqrt(Dot(v, v));
}

inline Vec3 SafeNormalize(const Vec3& v, const Vec3& fallback) noexcept
{
    const float len = Length(v);
    if (!std::isfinite(len) || len <= 1.0e-12f) {
        return fallback;
    }
    const float inv = 1.0f / len;
    return {v.x * inv, v.y * inv, v.z * inv};
}

inline Vec3 OrthogonalFallback(const Vec3& n) noexcept
{
    const Vec3 ref = (std::fabs(n.z) < 0.9f) ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{1.0f, 0.0f, 0.0f};
    return SafeNormalize(Cross(ref, n), {1.0f, 0.0f, 0.0f});
}

struct SpatialKey
{
    int64_t qx = 0;
    int64_t qy = 0;
    int64_t qz = 0;

    bool operator==(const SpatialKey& o) const noexcept
    {
        return qx == o.qx && qy == o.qy && qz == o.qz;
    }
};

struct SpatialKeyHash
{
    size_t operator()(const SpatialKey& k) const noexcept
    {
        uint64_t h = 14695981039346656037ull;
        auto mix = [&](int64_t v) {
            h ^= static_cast<uint64_t>(v);
            h *= 1099511628211ull;
        };
        mix(k.qx);
        mix(k.qy);
        mix(k.qz);
        return static_cast<size_t>(h);
    }
};

inline uint64_t MakeUndirectedEdgeKey(uint32_t a, uint32_t b) noexcept
{
    const uint32_t lo = (a < b) ? a : b;
    const uint32_t hi = (a < b) ? b : a;
    return (static_cast<uint64_t>(lo) << 32u) | static_cast<uint64_t>(hi);
}

} // namespace

TopologyRepairResult RepairNifMeshTopology(
    const std::string&                             meshName,
    const std::vector<raw::bridge::RawMeshVertex>& srcVerts,
    const std::vector<uint32_t>&                   srcIndices,
    const TopologyRepairConfig&                    cfg)
{
    TopologyRepairResult res{};
    res.inputVertexCount = static_cast<uint32_t>(srcVerts.size());

    const std::string label = meshName.empty() ? "<unnamed-nif-mesh>" : meshName;

    if (srcVerts.empty() || srcIndices.size() < 3u) {
        res.ok = false;
        TopologyDiagnostic d{};
        d.meshName = label;
        d.code     = "EMPTY_MESH";
        d.message  = "Mesh '" + label + "' has insufficient geometry (" +
                     std::to_string(srcVerts.size()) + " vertices, " +
                     std::to_string(srcIndices.size()) + " indices); expected >= 3 indices.";
        res.diagnostics.push_back(std::move(d));
        return res;
    }

    if ((srcIndices.size() % 3u) != 0u) {
        TopologyDiagnostic d{};
        d.meshName      = label;
        d.firstIndexIdx = static_cast<uint32_t>(srcIndices.size() - (srcIndices.size() % 3u));
        d.code          = "TRAILING_INDICES";
        d.message       = "Mesh '" + label + "' has non-multiple-of-3 index buffer (" +
                          std::to_string(srcIndices.size()) + " indices); trailing indices ignored.";
        res.diagnostics.push_back(std::move(d));
    }

    // Step 1: Audit source vertices for NaN/Inf.
    std::vector<uint8_t> vertValid(srcVerts.size(), 1u);
    uint32_t             corruptVertCount = 0u;
    uint32_t             firstCorruptVert = 0u;
    for (uint32_t i = 0u; i < static_cast<uint32_t>(srcVerts.size()); ++i) {
        const auto& v = srcVerts[i];
        if (!IsFinite3(v.pos) || !IsFinite3(v.normal) || !IsFinite2(v.uv)) {
            vertValid[i] = 0u;
            if (corruptVertCount == 0u) {
                firstCorruptVert = i;
            }
            ++corruptVertCount;
        }
    }

    if (corruptVertCount > 0u) {
        TopologyDiagnostic d{};
        d.meshName       = label;
        d.firstVertexIdx = firstCorruptVert;
        d.code           = "CORRUPT_NAN_VERTEX";
        std::ostringstream oss;
        oss << "Corrupt NIF mesh '" << label << "' contains " << corruptVertCount
            << " vertex/vertices with NaN or Inf attributes (first at vertex index "
            << firstCorruptVert << "). Re-export or strip corrupt BSTriShape vertices.";
        d.message = oss.str();
        res.diagnostics.push_back(std::move(d));
    }

    // Step 2: Filter triangles (out-of-range, NaN-vertex, duplicate-index, degenerate area).
    const size_t triCount = srcIndices.size() / 3u;
    std::vector<uint32_t> cleanRawIndices;
    cleanRawIndices.reserve(triCount * 3u);

    uint32_t firstOobIndexPos = 0u;
    for (size_t t = 0u; t < triCount; ++t) {
        const uint32_t idxPos = static_cast<uint32_t>(t * 3u);
        const uint32_t i0     = srcIndices[idxPos + 0u];
        const uint32_t i1     = srcIndices[idxPos + 1u];
        const uint32_t i2     = srcIndices[idxPos + 2u];

        if (i0 >= srcVerts.size() || i1 >= srcVerts.size() || i2 >= srcVerts.size()) {
            if (res.strippedOutOfRangeTriangles == 0u) {
                firstOobIndexPos = idxPos;
            }
            ++res.strippedOutOfRangeTriangles;
            continue;
        }

        if (!vertValid[i0] || !vertValid[i1] || !vertValid[i2]) {
            ++res.strippedNanTriangles;
            continue;
        }

        if (cfg.enableDegenerateStrip) {
            if (i0 == i1 || i1 == i2 || i2 == i0) {
                ++res.strippedDegenerateTriangles;
                continue;
            }
            const Vec3 p0{srcVerts[i0].pos[0], srcVerts[i0].pos[1], srcVerts[i0].pos[2]};
            const Vec3 p1{srcVerts[i1].pos[0], srcVerts[i1].pos[1], srcVerts[i1].pos[2]};
            const Vec3 p2{srcVerts[i2].pos[0], srcVerts[i2].pos[1], srcVerts[i2].pos[2]};
            const float area = 0.5f * Length(Cross(Sub(p1, p0), Sub(p2, p0)));
            if (!std::isfinite(area) || area <= cfg.minTriangleArea) {
                ++res.strippedDegenerateTriangles;
                continue;
            }
        }

        cleanRawIndices.push_back(i0);
        cleanRawIndices.push_back(i1);
        cleanRawIndices.push_back(i2);
    }

    if (res.strippedOutOfRangeTriangles > 0u) {
        TopologyDiagnostic d{};
        d.meshName      = label;
        d.firstIndexIdx = firstOobIndexPos;
        d.code          = "OUT_OF_RANGE_INDEX";
        std::ostringstream oss;
        oss << "Corrupt NIF mesh '" << label << "' has " << res.strippedOutOfRangeTriangles
            << " triangle(s) referencing out-of-range vertex indices (vertex count = "
            << srcVerts.size() << ", first corrupt index span at index " << firstOobIndexPos
            << ".." << (firstOobIndexPos + 2u) << ").";
        d.message = oss.str();
        res.diagnostics.push_back(std::move(d));
    }

    if (cfg.failClosedOnCorruptInput &&
        (corruptVertCount > 0u || res.strippedOutOfRangeTriangles > 0u)) {
        res.ok = false;
        return res;
    }

    if (cleanRawIndices.empty()) {
        res.ok = false;
        TopologyDiagnostic d{};
        d.meshName = label;
        d.code     = "NO_VALID_TRIANGLES";
        d.message  = "Mesh '" + label + "' has zero valid triangles after topology repair.";
        res.diagnostics.push_back(std::move(d));
        return res;
    }

    // Step 3: Quantized spatial-hash seam welding (preserving UV and hard-edge splits).
    const float invPosEps = (cfg.posWeldEps > 1.0e-8f) ? (1.0f / cfg.posWeldEps) : 1.0e4f;
    std::unordered_map<SpatialKey, std::vector<uint32_t>, SpatialKeyHash> spatialBuckets;
    spatialBuckets.reserve(srcVerts.size());

    std::vector<uint32_t> remap(srcVerts.size(), UINT32_MAX);
    res.vertices.reserve(srcVerts.size());

    auto makeRepairedFromRaw = [&](const raw::bridge::RawMeshVertex& rv) -> RepairedVertex {
        RepairedVertex out{};
        out.pos[0] = rv.pos[0];
        out.pos[1] = rv.pos[1];
        out.pos[2] = rv.pos[2];
        const Vec3 n = SafeNormalize({rv.normal[0], rv.normal[1], rv.normal[2]}, {0.0f, 0.0f, 1.0f});
        out.normal[0] = n.x;
        out.normal[1] = n.y;
        out.normal[2] = n.z;
        out.uv[0]     = rv.uv[0];
        out.uv[1]     = rv.uv[1];
        const Vec3 t0 = OrthogonalFallback(n);
        out.tangent[0] = t0.x;
        out.tangent[1] = t0.y;
        out.tangent[2] = t0.z;
        out.tangent[3] = 1.0f;
        return out;
    };

    res.indices.reserve(cleanRawIndices.size());
    for (uint32_t rawIdx : cleanRawIndices) {
        if (remap[rawIdx] != UINT32_MAX) {
            res.indices.push_back(remap[rawIdx]);
            continue;
        }

        const RepairedVertex cand = makeRepairedFromRaw(srcVerts[rawIdx]);
        uint32_t assigned = UINT32_MAX;

        if (cfg.enableSeamWelding) {
            const int64_t bx = static_cast<int64_t>(std::llround(static_cast<double>(cand.pos[0]) * invPosEps));
            const int64_t by = static_cast<int64_t>(std::llround(static_cast<double>(cand.pos[1]) * invPosEps));
            const int64_t bz = static_cast<int64_t>(std::llround(static_cast<double>(cand.pos[2]) * invPosEps));

            const float posEpsSq = cfg.posWeldEps * cfg.posWeldEps;
            const float uvEpsSq  = cfg.uvWeldEps * cfg.uvWeldEps;

            // Search immediate 3x3x3 quantized neighborhood to prevent cell-boundary misses.
            for (int64_t dz = -1; dz <= 1 && assigned == UINT32_MAX; ++dz) {
                for (int64_t dy = -1; dy <= 1 && assigned == UINT32_MAX; ++dy) {
                    for (int64_t dx = -1; dx <= 1 && assigned == UINT32_MAX; ++dx) {
                        const auto it = spatialBuckets.find(SpatialKey{bx + dx, by + dy, bz + dz});
                        if (it == spatialBuckets.end()) {
                            continue;
                        }
                        for (uint32_t existingIdx : it->second) {
                            const RepairedVertex& ex = res.vertices[existingIdx];
                            const float dpx = cand.pos[0] - ex.pos[0];
                            const float dpy = cand.pos[1] - ex.pos[1];
                            const float dpz = cand.pos[2] - ex.pos[2];
                            if ((dpx * dpx + dpy * dpy + dpz * dpz) > posEpsSq) {
                                continue;
                            }
                            const float du = cand.uv[0] - ex.uv[0];
                            const float dv = cand.uv[1] - ex.uv[1];
                            if ((du * du + dv * dv) > uvEpsSq) {
                                continue;
                            }
                            const float ndot = cand.normal[0] * ex.normal[0] +
                                               cand.normal[1] * ex.normal[1] +
                                               cand.normal[2] * ex.normal[2];
                            if (ndot < cfg.normalDotMin) {
                                continue;
                            }
                            assigned = existingIdx;
                            ++res.weldedVertexCount;
                            break;
                        }
                    }
                }
            }

            if (assigned == UINT32_MAX) {
                assigned = static_cast<uint32_t>(res.vertices.size());
                res.vertices.push_back(cand);
                spatialBuckets[SpatialKey{bx, by, bz}].push_back(assigned);
            }
        } else {
            assigned = static_cast<uint32_t>(res.vertices.size());
            res.vertices.push_back(cand);
        }

        remap[rawIdx] = assigned;
        res.indices.push_back(assigned);
    }

    // Step 4: Drop any triangles that collapsed to duplicate indices after welding.
    if (cfg.enableDegenerateStrip) {
        std::vector<uint32_t> postWeldIndices;
        postWeldIndices.reserve(res.indices.size());
        for (size_t t = 0u; t + 2u < res.indices.size(); t += 3u) {
            const uint32_t i0 = res.indices[t + 0u];
            const uint32_t i1 = res.indices[t + 1u];
            const uint32_t i2 = res.indices[t + 2u];
            if (i0 == i1 || i1 == i2 || i2 == i0) {
                ++res.strippedDegenerateTriangles;
                continue;
            }
            postWeldIndices.push_back(i0);
            postWeldIndices.push_back(i1);
            postWeldIndices.push_back(i2);
        }
        res.indices.swap(postWeldIndices);
    }

    if (res.indices.empty()) {
        res.ok = false;
        return res;
    }

    // Step 5: Watertight edge audit (boundary & non-manifold edge counts).
    std::unordered_map<uint64_t, uint32_t> edgeCounts;
    edgeCounts.reserve(res.indices.size());
    for (size_t t = 0u; t + 2u < res.indices.size(); t += 3u) {
        const uint32_t i0 = res.indices[t + 0u];
        const uint32_t i1 = res.indices[t + 1u];
        const uint32_t i2 = res.indices[t + 2u];
        ++edgeCounts[MakeUndirectedEdgeKey(i0, i1)];
        ++edgeCounts[MakeUndirectedEdgeKey(i1, i2)];
        ++edgeCounts[MakeUndirectedEdgeKey(i2, i0)];
    }
    for (const auto& kv : edgeCounts) {
        if (kv.second == 1u) {
            ++res.boundaryEdgeCount;
        } else if (kv.second > 2u) {
            ++res.nonManifoldEdgeCount;
        }
    }

    // Step 6: MikkTSpace-style UV-gradient tangent accumulation + Gram-Schmidt orthonormalization.
    if (cfg.enableTangentOrthonormalize) {
        std::vector<Vec3> tanAccum(res.vertices.size(), Vec3{0.0f, 0.0f, 0.0f});
        std::vector<Vec3> bitanAccum(res.vertices.size(), Vec3{0.0f, 0.0f, 0.0f});

        for (size_t t = 0u; t + 2u < res.indices.size(); t += 3u) {
            const uint32_t i0 = res.indices[t + 0u];
            const uint32_t i1 = res.indices[t + 1u];
            const uint32_t i2 = res.indices[t + 2u];

            const auto& v0 = res.vertices[i0];
            const auto& v1 = res.vertices[i1];
            const auto& v2 = res.vertices[i2];

            const Vec3 e1{v1.pos[0] - v0.pos[0], v1.pos[1] - v0.pos[1], v1.pos[2] - v0.pos[2]};
            const Vec3 e2{v2.pos[0] - v0.pos[0], v2.pos[1] - v0.pos[1], v2.pos[2] - v0.pos[2]};
            const float du1 = v1.uv[0] - v0.uv[0];
            const float dv1 = v1.uv[1] - v0.uv[1];
            const float du2 = v2.uv[0] - v0.uv[0];
            const float dv2 = v2.uv[1] - v0.uv[1];

            const float det = du1 * dv2 - du2 * dv1;
            if (std::fabs(det) <= 1.0e-10f) {
                continue;
            }
            const float invDet = 1.0f / det;
            const Vec3  triT{
                (e1.x * dv2 - e2.x * dv1) * invDet,
                (e1.y * dv2 - e2.y * dv1) * invDet,
                (e1.z * dv2 - e2.z * dv1) * invDet
            };
            const Vec3 triB{
                (e2.x * du1 - e1.x * du2) * invDet,
                (e2.y * du1 - e1.y * du2) * invDet,
                (e2.z * du1 - e1.z * du2) * invDet
            };

            const uint32_t triIdx[3] = {i0, i1, i2};
            for (uint32_t k : triIdx) {
                tanAccum[k].x   += triT.x;
                tanAccum[k].y   += triT.y;
                tanAccum[k].z   += triT.z;
                bitanAccum[k].x += triB.x;
                bitanAccum[k].y += triB.y;
                bitanAccum[k].z += triB.z;
            }
        }

        float maxOrthoErr = 0.0f;
        for (size_t i = 0u; i < res.vertices.size(); ++i) {
            auto& v = res.vertices[i];
            const Vec3 n = SafeNormalize({v.normal[0], v.normal[1], v.normal[2]}, {0.0f, 0.0f, 1.0f});
            v.normal[0] = n.x;
            v.normal[1] = n.y;
            v.normal[2] = n.z;

            const Vec3 tRaw = tanAccum[i];
            // Gram-Schmidt orthogonalize T against N: T' = T - N * dot(N, T)
            const float nt = Dot(n, tRaw);
            Vec3        tOrtho{tRaw.x - n.x * nt, tRaw.y - n.y * nt, tRaw.z - n.z * nt};
            if (Length(tOrtho) <= 1.0e-8f) {
                tOrtho = OrthogonalFallback(n);
            } else {
                tOrtho = SafeNormalize(tOrtho, OrthogonalFallback(n));
            }
            // Second pass Gram-Schmidt for strict |dot(N,T)| <= 1e-6 precision.
            const float nt2 = Dot(n, tOrtho);
            tOrtho = SafeNormalize({tOrtho.x - n.x * nt2, tOrtho.y - n.y * nt2, tOrtho.z - n.z * nt2},
                                   OrthogonalFallback(n));

            const float hand = (Dot(Cross(n, tOrtho), bitanAccum[i]) < 0.0f) ? -1.0f : 1.0f;
            v.tangent[0] = tOrtho.x;
            v.tangent[1] = tOrtho.y;
            v.tangent[2] = tOrtho.z;
            v.tangent[3] = hand;

            maxOrthoErr = (std::max)(maxOrthoErr, std::fabs(Dot(n, tOrtho)));
        }
        res.maxTangentOrthoError = maxOrthoErr;
    }

    res.ok = true;
    return res;
}

bool VerifyOrthonormalTangentFrames(
    const std::vector<RepairedVertex>& verts,
    float*                             outMaxOrthoErr,
    float*                             outMaxUnitErr) noexcept
{
    if (verts.empty()) {
        return false;
    }
    float maxOrtho = 0.0f;
    float maxUnit  = 0.0f;
    for (const auto& v : verts) {
        if (!IsFinite3(v.pos) || !IsFinite3(v.normal) || !IsFinite2(v.uv) ||
            !IsFinite3(v.tangent) || !std::isfinite(v.tangent[3])) {
            return false;
        }
        const Vec3 n{v.normal[0], v.normal[1], v.normal[2]};
        const Vec3 t{v.tangent[0], v.tangent[1], v.tangent[2]};
        const float nErr = std::fabs(Length(n) - 1.0f);
        const float tErr = std::fabs(Length(t) - 1.0f);
        const float oErr = std::fabs(Dot(n, t));
        const float wAbs = std::fabs(v.tangent[3]);
        maxUnit  = (std::max)(maxUnit, (std::max)(nErr, tErr));
        maxOrtho = (std::max)(maxOrtho, oErr);
        if (nErr > 1.0e-4f || tErr > 1.0e-4f || oErr > 1.0e-5f || std::fabs(wAbs - 1.0f) > 1.0e-5f) {
            if (outMaxOrthoErr) *outMaxOrthoErr = maxOrtho;
            if (outMaxUnitErr) *outMaxUnitErr   = maxUnit;
            return false;
        }
    }
    if (outMaxOrthoErr) *outMaxOrthoErr = maxOrtho;
    if (outMaxUnitErr) *outMaxUnitErr   = maxUnit;
    return true;
}

} // namespace raw::ce
