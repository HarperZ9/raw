// RawWorldPhysics.cpp: Clean-Room 3D World Collision, SAH AABB BVH Raycaster & Capsule Character Controller.
// Original work, MIT. Zero Windows / D3D12 / proprietary physics SDK dependencies.

#include "RawWorldPhysics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>

namespace raw::worldgen {
namespace {

constexpr float kPi         = 3.14159265358979323846f;
constexpr float kDegToRad   = kPi / 180.0f;
constexpr float kEps        = 1.0e-6f;
constexpr float kBigFloat   = 1.0e30f;

inline bool IsFiniteVec3(Vec3 v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

inline float LengthSq(Vec3 v) noexcept {
    return v.x * v.x + v.y * v.y + v.z * v.z;
}

inline Vec3 MinVec3(Vec3 a, Vec3 b) noexcept {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}

inline Vec3 MaxVec3(Vec3 a, Vec3 b) noexcept {
    return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}

inline float AabbSurfaceArea(Vec3 bmin, Vec3 bmax) noexcept {
    const float dx = std::max(0.0f, bmax.x - bmin.x);
    const float dy = std::max(0.0f, bmax.y - bmin.y);
    const float dz = std::max(0.0f, bmax.z - bmin.z);
    return 2.0f * (dx * dy + dy * dz + dz * dx);
}

inline bool AabbOverlaps(Vec3 aMin, Vec3 aMax, Vec3 bMin, Vec3 bMax) noexcept {
    return (aMin.x <= bMax.x && aMax.x >= bMin.x) &&
           (aMin.y <= bMax.y && aMax.y >= bMin.y) &&
           (aMin.z <= bMax.z && aMax.z >= bMin.z);
}

// Slab ray-AABB intersection; returns true if ray overlaps box within [0, maxT].
inline bool RayIntersectsAabb(Vec3 origin, Vec3 invDir, Vec3 bmin, Vec3 bmax,
                              float maxT, float& outTMin, float& outTMax) noexcept {
    const float tx1 = (bmin.x - origin.x) * invDir.x;
    const float tx2 = (bmax.x - origin.x) * invDir.x;
    float tmin = std::min(tx1, tx2);
    float tmax = std::max(tx1, tx2);

    const float ty1 = (bmin.y - origin.y) * invDir.y;
    const float ty2 = (bmax.y - origin.y) * invDir.y;
    tmin = std::max(tmin, std::min(ty1, ty2));
    tmax = std::min(tmax, std::max(ty1, ty2));

    const float tz1 = (bmin.z - origin.z) * invDir.z;
    const float tz2 = (bmax.z - origin.z) * invDir.z;
    tmin = std::max(tmin, std::min(tz1, tz2));
    tmax = std::min(tmax, std::max(tz1, tz2));

    if (tmax < 0.0f || tmin > tmax || tmin > maxT) {
        return false;
    }
    outTMin = std::max(0.0f, tmin);
    outTMax = std::min(maxT, tmax);
    return outTMin <= outTMax;
}

// Double-sided Moller-Trumbore ray-triangle intersection.
inline bool RayIntersectsTriangle(Vec3 origin, Vec3 dir, Vec3 v0, Vec3 v1, Vec3 v2,
                                  float maxT, float& outT) noexcept {
    const Vec3 e1 = v1 - v0;
    const Vec3 e2 = v2 - v0;
    const Vec3 p  = Cross(dir, e2);
    const float det = Dot(e1, p);
    if (std::abs(det) < 1.0e-8f) {
        return false;
    }
    const float invDet = 1.0f / det;
    const Vec3 tvec = origin - v0;
    const float u = Dot(tvec, p) * invDet;
    if (u < -1.0e-5f || u > 1.0f + 1.0e-5f) {
        return false;
    }
    const Vec3 q = Cross(tvec, e1);
    const float v = Dot(dir, q) * invDet;
    if (v < -1.0e-5f || (u + v) > 1.0f + 1.0e-5f) {
        return false;
    }
    const float t = Dot(e2, q) * invDet;
    if (t < 0.0f || t > maxT) {
        return false;
    }
    outT = t;
    return true;
}

// Exact closest point on triangle (a, b, c) to query point p (Ericson, Real-Time Collision Detection).
Vec3 ClosestPointOnTriangle(Vec3 p, Vec3 a, Vec3 b, Vec3 c) noexcept {
    const Vec3 ab = b - a;
    const Vec3 ac = c - a;
    const Vec3 ap = p - a;
    const float d1 = Dot(ab, ap);
    const float d2 = Dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return a;

    const Vec3 bp = p - b;
    const float d3 = Dot(ab, bp);
    const float d4 = Dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return b;

    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        const float denom = d1 - d3;
        const float v = (std::abs(denom) > kEps) ? (d1 / denom) : 0.0f;
        return a + ab * v;
    }

    const Vec3 cp = p - c;
    const float d5 = Dot(ab, cp);
    const float d6 = Dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return c;

    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        const float denom = d2 - d6;
        const float w = (std::abs(denom) > kEps) ? (d2 / denom) : 0.0f;
        return a + ac * w;
    }

    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        const float denom = (d4 - d3) + (d5 - d6);
        const float w = (std::abs(denom) > kEps) ? ((d4 - d3) / denom) : 0.0f;
        return b + (c - b) * w;
    }

    const float denom = va + vb + vc;
    if (std::abs(denom) <= 1.0e-12f) {
        return a;
    }
    const float invDenom = 1.0f / denom;
    const float v = vb * invDenom;
    const float w = vc * invDenom;
    return a + ab * v + ac * w;
}

// Exact closest points between segments (p1, q1) and (p2, q2); returns squared distance.
float ClosestPointsSegmentSegment(Vec3 p1, Vec3 q1, Vec3 p2, Vec3 q2,
                                  Vec3& outC1, Vec3& outC2) noexcept {
    const Vec3 d1 = q1 - p1;
    const Vec3 d2 = q2 - p2;
    const Vec3 r  = p1 - p2;
    const float a = Dot(d1, d1);
    const float e = Dot(d2, d2);
    const float f = Dot(d2, r);

    float s = 0.0f;
    float t = 0.0f;
    if (a <= 1.0e-10f && e <= 1.0e-10f) {
        outC1 = p1;
        outC2 = p2;
        return LengthSq(outC1 - outC2);
    }
    if (a <= 1.0e-10f) {
        s = 0.0f;
        t = Clamp01(f / e);
    } else {
        const float c = Dot(d1, r);
        if (e <= 1.0e-10f) {
            t = 0.0f;
            s = Clamp01(-c / a);
        } else {
            const float b = Dot(d1, d2);
            const float denom = a * e - b * b;
            if (denom > 1.0e-10f) {
                s = Clamp01((b * f - c * e) / denom);
            } else {
                s = 0.0f;
            }
            t = (b * s + f) / e;
            if (t < 0.0f) {
                t = 0.0f;
                s = Clamp01(-c / a);
            } else if (t > 1.0f) {
                t = 1.0f;
                s = Clamp01((b - c) / a);
            }
        }
    }
    outC1 = p1 + d1 * s;
    outC2 = p2 + d2 * t;
    return LengthSq(outC1 - outC2);
}

// Exact closest point pair between segment (segA, segB) and triangle (v0, v1, v2).
// Returns squared distance and writes closest points outSegPt and outTriPt.
float ClosestSegmentTriangle(Vec3 segA, Vec3 segB, Vec3 v0, Vec3 v1, Vec3 v2,
                             Vec3& outSegPt, Vec3& outTriPt) noexcept {
    // 1. Check if segment intersects the triangle interior directly (distance == 0).
    const Vec3 segDir = segB - segA;
    const float segLen = Length(segDir);
    if (segLen > 1.0e-6f) {
        const Vec3 dirN = segDir / segLen;
        float hitT = 0.0f;
        if (RayIntersectsTriangle(segA, dirN, v0, v1, v2, segLen, hitT)) {
            const Vec3 pt = segA + dirN * hitT;
            outSegPt = pt;
            outTriPt = pt;
            return 0.0f;
        }
    }

    // 2. Endpoints against triangle.
    Vec3 bestSeg = segA;
    Vec3 bestTri = ClosestPointOnTriangle(segA, v0, v1, v2);
    float bestD2 = LengthSq(bestSeg - bestTri);

    const Vec3 triB = ClosestPointOnTriangle(segB, v0, v1, v2);
    const float d2B = LengthSq(segB - triB);
    if (d2B < bestD2) {
        bestD2  = d2B;
        bestSeg = segB;
        bestTri = triB;
    }

    // 3. Segment against each of the 3 triangle edges.
    const Vec3 edges[3][2] = {{v0, v1}, {v1, v2}, {v2, v0}};
    for (int i = 0; i < 3; ++i) {
        Vec3 c1{}, c2{};
        const float d2 = ClosestPointsSegmentSegment(segA, segB, edges[i][0], edges[i][1], c1, c2);
        if (d2 < bestD2) {
            bestD2  = d2;
            bestSeg = c1;
            bestTri = c2;
        }
    }

    outSegPt = bestSeg;
    outTriPt = bestTri;
    return bestD2;
}

inline Vec3 MakeInvDir(Vec3 dir) noexcept {
    auto safeInv = [](float c) noexcept -> float {
        if (std::abs(c) < 1.0e-12f) {
            return (c < 0.0f) ? -1.0e12f : 1.0e12f;
        }
        return 1.0f / c;
    };
    return {safeInv(dir.x), safeInv(dir.y), safeInv(dir.z)};
}

}  // namespace

// ============================================================================
// Heightfield Convenience Builders
// ============================================================================

TerrainCellHeightfield MakeHeightfieldFromTerrainChunk(const TerrainChunkData& chunk,
                                                       float waterLevelZ,
                                                       uint32_t materialTag) {
    TerrainCellHeightfield hf{};
    hf.cellKey     = PackCellKey(chunk.chunkX, chunk.chunkY);
    hf.cellX       = chunk.chunkX;
    hf.cellY       = chunk.chunkY;
    hf.originX     = chunk.originX;
    hf.originY     = chunk.originY;
    hf.sizeX       = chunk.chunkSizeU;
    hf.sizeY       = chunk.chunkSizeU;
    hf.gridW       = chunk.stride;
    hf.gridH       = chunk.stride;
    hf.minZ        = chunk.minZ;
    hf.maxZ        = chunk.maxZ;
    hf.waterLevelZ = waterLevelZ;
    hf.materialTag = materialTag;
    hf.heights     = chunk.heights;
    return hf;
}

TerrainCellHeightfield MakeHeightfieldFromEsmCell(const EsmLoadedCell& cell,
                                                  float defaultWaterHeightZ,
                                                  uint32_t materialTag) {
    TerrainCellHeightfield hf{};
    hf.cellKey     = PackCellKey(cell.meta.cellX, cell.meta.cellY);
    hf.cellX       = cell.meta.cellX;
    hf.cellY       = cell.meta.cellY;
    hf.originX     = static_cast<float>(cell.meta.cellX) * kEsmCellSizeU;
    hf.originY     = static_cast<float>(cell.meta.cellY) * kEsmCellSizeU;
    hf.sizeX       = kEsmCellSizeU;
    hf.sizeY       = kEsmCellSizeU;
    hf.gridW       = kEsmLandGridSize;
    hf.gridH       = kEsmLandGridSize;
    hf.minZ        = cell.land.minHeightZ;
    hf.maxZ        = cell.land.maxHeightZ;
    hf.waterLevelZ = cell.meta.hasWaterHeight ? cell.meta.waterHeight : defaultWaterHeightZ;
    hf.materialTag = materialTag;
    hf.heights.assign(cell.land.heightsZ.begin(), cell.land.heightsZ.end());
    return hf;
}

// ============================================================================
// World Management
// ============================================================================

void RawWorldPhysics::Clear() {
    heightfields_.clear();
    cellBvhs_.clear();
}

WorldPhysicsStats RawWorldPhysics::GetStats() const {
    WorldPhysicsStats s{};
    s.heightfieldCount = static_cast<uint32_t>(heightfields_.size());
    s.bvhCellCount     = static_cast<uint32_t>(cellBvhs_.size());
    for (const auto& [key, bvh] : cellBvhs_) {
        (void)key;
        s.totalTriangles += static_cast<uint32_t>(bvh.triangles.size());
        s.totalBvhNodes  += static_cast<uint32_t>(bvh.nodes.size());
    }
    return s;
}

// ============================================================================
// Heightfield Terrain Collider
// ============================================================================

bool RawWorldPhysics::AddTerrainHeightfield(const TerrainCellHeightfield& hf) {
    if (hf.gridW < 2u || hf.gridH < 2u || hf.sizeX <= 1.0e-3f || hf.sizeY <= 1.0e-3f) {
        return false;
    }
    const size_t expectedCount = static_cast<size_t>(hf.gridW) * static_cast<size_t>(hf.gridH);
    if (hf.heights.size() != expectedCount) {
        return false;
    }
    TerrainCellHeightfield stored = hf;
    float minZ =  kBigFloat;
    float maxZ = -kBigFloat;
    for (float h : stored.heights) {
        if (!std::isfinite(h)) {
            return false;
        }
        minZ = std::min(minZ, h);
        maxZ = std::max(maxZ, h);
    }
    stored.minZ = minZ;
    stored.maxZ = maxZ;
    heightfields_[stored.cellKey] = std::move(stored);
    return true;
}

bool RawWorldPhysics::RemoveTerrainHeightfield(uint64_t cellKey) {
    return heightfields_.erase(cellKey) > 0u;
}

bool RawWorldPhysics::SampleHeightfieldLocal(const TerrainCellHeightfield& hf, float x, float y,
                                             float* outZ, Vec3* outNormal) const {
    const float relX = x - hf.originX;
    const float relY = y - hf.originY;
    if (relX < -1.0e-3f || relX > hf.sizeX + 1.0e-3f ||
        relY < -1.0e-3f || relY > hf.sizeY + 1.0e-3f) {
        return false;
    }

    const float u = Clamp01(relX / hf.sizeX);
    const float v = Clamp01(relY / hf.sizeY);
    const float fx = u * static_cast<float>(hf.gridW - 1u);
    const float fy = v * static_cast<float>(hf.gridH - 1u);

    const uint32_t ix0 = std::min(static_cast<uint32_t>(fx), hf.gridW - 2u);
    const uint32_t iy0 = std::min(static_cast<uint32_t>(fy), hf.gridH - 2u);
    const uint32_t ix1 = ix0 + 1u;
    const uint32_t iy1 = iy0 + 1u;

    const float tx = Clamp01(fx - static_cast<float>(ix0));
    const float ty = Clamp01(fy - static_cast<float>(iy0));

    const float z00 = hf.heights[static_cast<size_t>(iy0) * hf.gridW + ix0];
    const float z10 = hf.heights[static_cast<size_t>(iy0) * hf.gridW + ix1];
    const float z01 = hf.heights[static_cast<size_t>(iy1) * hf.gridW + ix0];
    const float z11 = hf.heights[static_cast<size_t>(iy1) * hf.gridW + ix1];

    const float z0 = Lerp(z00, z10, tx);
    const float z1 = Lerp(z01, z11, tx);
    const float z  = Lerp(z0, z1, ty);
    if (outZ) {
        *outZ = z;
    }

    if (outNormal) {
        const float dx = hf.sizeX / static_cast<float>(hf.gridW - 1u);
        const float dy = hf.sizeY / static_cast<float>(hf.gridH - 1u);
        const float dzdx = ((1.0f - ty) * (z10 - z00) + ty * (z11 - z01)) / dx;
        const float dzdy = ((1.0f - tx) * (z01 - z00) + tx * (z11 - z10)) / dy;
        *outNormal = Normalize(Vec3{-dzdx, -dzdy, 1.0f});
    }
    return true;
}

bool RawWorldPhysics::SampleTerrainHeight(float x, float y,
                                          float* outZ,
                                          Vec3* outNormal,
                                          float* outWaterLevelZ,
                                          uint64_t* outCellKey) const {
    if (!std::isfinite(x) || !std::isfinite(y) || heightfields_.empty()) {
        return false;
    }

    // Fast-path O(1) lookup for standard 4096u Skyrim exterior cell coordinates:
    const int32_t guessX = static_cast<int32_t>(std::floor(x / kEsmCellSizeU));
    const int32_t guessY = static_cast<int32_t>(std::floor(y / kEsmCellSizeU));
    const auto it = heightfields_.find(PackCellKey(guessX, guessY));
    if (it != heightfields_.end()) {
        if (SampleHeightfieldLocal(it->second, x, y, outZ, outNormal)) {
            if (outWaterLevelZ) *outWaterLevelZ = it->second.waterLevelZ;
            if (outCellKey)     *outCellKey     = it->second.cellKey;
            return true;
        }
    }

    // Fallback scan for arbitrary custom heightfield sizes/origins:
    for (const auto& [key, hf] : heightfields_) {
        if (x >= hf.originX - 1.0e-3f && x <= hf.originX + hf.sizeX + 1.0e-3f &&
            y >= hf.originY - 1.0e-3f && y <= hf.originY + hf.sizeY + 1.0e-3f) {
            if (SampleHeightfieldLocal(hf, x, y, outZ, outNormal)) {
                if (outWaterLevelZ) *outWaterLevelZ = hf.waterLevelZ;
                if (outCellKey)     *outCellKey     = key;
                return true;
            }
        }
    }
    return false;
}

// ============================================================================
// Static Prop Triangle / MeshletChunk SAH AABB BVH Collider
// ============================================================================

bool RawWorldPhysics::AddCellCollider(uint64_t cellKey, const ProceduralMesh& mesh,
                                      uint32_t instanceId, uint32_t materialTag) {
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        return false;
    }

    CellBvh& cell = cellBvhs_[cellKey];
    cell.cellKey  = cellKey;
    const size_t beforeCount = cell.triangles.size();

    auto emitTri = [&](uint32_t i0, uint32_t i1, uint32_t i2) {
        if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size()) {
            return;
        }
        const auto& a = mesh.vertices[i0];
        const auto& b = mesh.vertices[i1];
        const auto& c = mesh.vertices[i2];
        const Vec3 v0{a.pos[0], a.pos[1], a.pos[2]};
        const Vec3 v1{b.pos[0], b.pos[1], b.pos[2]};
        const Vec3 v2{c.pos[0], c.pos[1], c.pos[2]};
        if (!IsFiniteVec3(v0) || !IsFiniteVec3(v1) || !IsFiniteVec3(v2)) {
            return;
        }
        const Vec3 cr = Cross(v1 - v0, v2 - v0);
        const float len2 = LengthSq(cr);
        if (len2 < 1.0e-10f) {
            return;
        }
        PhysicsTriangle tri{};
        tri.v0          = v0;
        tri.v1          = v1;
        tri.v2          = v2;
        tri.faceNormal  = cr / std::sqrt(len2);
        tri.aabbMin     = MinVec3(v0, MinVec3(v1, v2));
        tri.aabbMax     = MaxVec3(v0, MaxVec3(v1, v2));
        tri.centroid    = (v0 + v1 + v2) * (1.0f / 3.0f);
        tri.cellKey     = cellKey;
        tri.instanceId  = instanceId;
        tri.materialTag = materialTag;
        cell.triangles.push_back(tri);
    };

    if (!mesh.meshlets.empty()) {
        size_t totalTris = 0u;
        for (const auto& m : mesh.meshlets) {
            totalTris += m.triangleCount;
        }
        cell.triangles.reserve(beforeCount + totalTris);
        for (const auto& m : mesh.meshlets) {
            for (uint32_t t = 0u; t < m.triangleCount; ++t) {
                const uint32_t base = m.triangleOffset + t * 3u;
                if (base + 2u >= mesh.indices.size()) {
                    break;
                }
                const uint32_t i0 = m.vertexOffset + mesh.indices[base + 0u];
                const uint32_t i1 = m.vertexOffset + mesh.indices[base + 1u];
                const uint32_t i2 = m.vertexOffset + mesh.indices[base + 2u];
                emitTri(i0, i1, i2);
            }
        }
    } else {
        const size_t triCount = mesh.indices.size() / 3u;
        cell.triangles.reserve(beforeCount + triCount);
        for (size_t t = 0u; t < triCount; ++t) {
            emitTri(mesh.indices[t * 3u + 0u],
                    mesh.indices[t * 3u + 1u],
                    mesh.indices[t * 3u + 2u]);
        }
    }

    if (cell.triangles.size() == beforeCount) {
        if (cell.triangles.empty()) {
            cellBvhs_.erase(cellKey);
        }
        return false;
    }

    RebuildCellBvh(cell);
    return true;
}

bool RawWorldPhysics::AddCellCollider(uint64_t cellKey, const TriangleMeshCollider& mesh) {
    if (mesh.vertices.empty() || mesh.indices.size() < 3u) {
        return false;
    }

    CellBvh& cell = cellBvhs_[cellKey];
    cell.cellKey  = cellKey;
    const size_t beforeCount = cell.triangles.size();
    const size_t triCount    = mesh.indices.size() / 3u;
    cell.triangles.reserve(beforeCount + triCount);

    for (size_t t = 0u; t < triCount; ++t) {
        const uint32_t i0 = mesh.indices[t * 3u + 0u];
        const uint32_t i1 = mesh.indices[t * 3u + 1u];
        const uint32_t i2 = mesh.indices[t * 3u + 2u];
        if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size()) {
            continue;
        }
        const Vec3 v0 = mesh.vertices[i0];
        const Vec3 v1 = mesh.vertices[i1];
        const Vec3 v2 = mesh.vertices[i2];
        if (!IsFiniteVec3(v0) || !IsFiniteVec3(v1) || !IsFiniteVec3(v2)) {
            continue;
        }
        const Vec3 cr = Cross(v1 - v0, v2 - v0);
        const float len2 = LengthSq(cr);
        if (len2 < 1.0e-10f) {
            continue;
        }
        PhysicsTriangle tri{};
        tri.v0          = v0;
        tri.v1          = v1;
        tri.v2          = v2;
        tri.faceNormal  = cr / std::sqrt(len2);
        tri.aabbMin     = MinVec3(v0, MinVec3(v1, v2));
        tri.aabbMax     = MaxVec3(v0, MaxVec3(v1, v2));
        tri.centroid    = (v0 + v1 + v2) * (1.0f / 3.0f);
        tri.cellKey     = cellKey;
        tri.instanceId  = mesh.instanceId;
        tri.materialTag = mesh.materialTag;
        cell.triangles.push_back(tri);
    }

    if (cell.triangles.size() == beforeCount) {
        if (cell.triangles.empty()) {
            cellBvhs_.erase(cellKey);
        }
        return false;
    }

    RebuildCellBvh(cell);
    return true;
}

bool RawWorldPhysics::AddCellColliders(uint64_t cellKey, const std::vector<TriangleMeshCollider>& meshes) {
    bool anyAdded = false;
    CellBvh& cell = cellBvhs_[cellKey];
    cell.cellKey  = cellKey;
    const size_t beforeCount = cell.triangles.size();

    for (const auto& mesh : meshes) {
        const size_t triCount = mesh.indices.size() / 3u;
        for (size_t t = 0u; t < triCount; ++t) {
            const uint32_t i0 = mesh.indices[t * 3u + 0u];
            const uint32_t i1 = mesh.indices[t * 3u + 1u];
            const uint32_t i2 = mesh.indices[t * 3u + 2u];
            if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size()) {
                continue;
            }
            const Vec3 v0 = mesh.vertices[i0];
            const Vec3 v1 = mesh.vertices[i1];
            const Vec3 v2 = mesh.vertices[i2];
            if (!IsFiniteVec3(v0) || !IsFiniteVec3(v1) || !IsFiniteVec3(v2)) {
                continue;
            }
            const Vec3 cr = Cross(v1 - v0, v2 - v0);
            const float len2 = LengthSq(cr);
            if (len2 < 1.0e-10f) {
                continue;
            }
            PhysicsTriangle tri{};
            tri.v0          = v0;
            tri.v1          = v1;
            tri.v2          = v2;
            tri.faceNormal  = cr / std::sqrt(len2);
            tri.aabbMin     = MinVec3(v0, MinVec3(v1, v2));
            tri.aabbMax     = MaxVec3(v0, MaxVec3(v1, v2));
            tri.centroid    = (v0 + v1 + v2) * (1.0f / 3.0f);
            tri.cellKey     = cellKey;
            tri.instanceId  = mesh.instanceId;
            tri.materialTag = mesh.materialTag;
            cell.triangles.push_back(tri);
            anyAdded = true;
        }
    }

    if (!anyAdded || cell.triangles.size() == beforeCount) {
        if (cell.triangles.empty()) {
            cellBvhs_.erase(cellKey);
        }
        return false;
    }

    RebuildCellBvh(cell);
    return true;
}

bool RawWorldPhysics::RemoveCellCollider(uint64_t cellKey) {
    const bool removedMesh = cellBvhs_.erase(cellKey) > 0u;
    const bool removedHf   = heightfields_.erase(cellKey) > 0u;
    return removedMesh || removedHf;
}

bool RawWorldPhysics::RemoveCellMeshCollider(uint64_t cellKey) {
    return cellBvhs_.erase(cellKey) > 0u;
}

void RawWorldPhysics::RebuildCellBvh(CellBvh& cell) {
    const uint32_t triCount = static_cast<uint32_t>(cell.triangles.size());
    cell.nodes.clear();
    cell.triIndices.resize(triCount);
    std::iota(cell.triIndices.begin(), cell.triIndices.end(), 0u);

    if (triCount == 0u) {
        cell.worldMin = {};
        cell.worldMax = {};
        return;
    }

    cell.nodes.reserve(static_cast<size_t>(triCount) * 2u);
    cell.nodes.push_back(BvhNode{});
    BuildBvhRecursive(cell, 0u, 0u, triCount);
    cell.worldMin = cell.nodes[0].aabbMin;
    cell.worldMax = cell.nodes[0].aabbMax;
}

void RawWorldPhysics::BuildBvhRecursive(CellBvh& cell, uint32_t nodeIdx, uint32_t first, uint32_t count) {
    Vec3 bmin{ kBigFloat,  kBigFloat,  kBigFloat};
    Vec3 bmax{-kBigFloat, -kBigFloat, -kBigFloat};
    Vec3 cmin{ kBigFloat,  kBigFloat,  kBigFloat};
    Vec3 cmax{-kBigFloat, -kBigFloat, -kBigFloat};

    for (uint32_t i = 0u; i < count; ++i) {
        const auto& tri = cell.triangles[cell.triIndices[first + i]];
        bmin = MinVec3(bmin, tri.aabbMin);
        bmax = MaxVec3(bmax, tri.aabbMax);
        cmin = MinVec3(cmin, tri.centroid);
        cmax = MaxVec3(cmax, tri.centroid);
    }

    cell.nodes[nodeIdx].aabbMin = bmin;
    cell.nodes[nodeIdx].aabbMax = bmax;

    constexpr uint32_t kLeafMaxTris = 4u;
    if (count <= kLeafMaxTris) {
        cell.nodes[nodeIdx].leftFirst = first;
        cell.nodes[nodeIdx].triCount  = count;
        return;
    }

    // 8-bin Surface Area Heuristic (SAH) split across X, Y, Z axes:
    constexpr int kBins = 8;
    struct Bin {
        Vec3     bmin{ kBigFloat,  kBigFloat,  kBigFloat};
        Vec3     bmax{-kBigFloat, -kBigFloat, -kBigFloat};
        uint32_t count = 0u;
    };

    const float parentArea = AabbSurfaceArea(bmin, bmax);
    float bestCost  = static_cast<float>(count) * parentArea; // Leaf cost baseline
    int   bestAxis  = -1;
    float bestSplit = 0.0f;

    auto axisCoord = [](Vec3 v, int axis) noexcept -> float {
        return (axis == 0) ? v.x : ((axis == 1) ? v.y : v.z);
    };

    for (int axis = 0; axis < 3; ++axis) {
        const float lo   = axisCoord(cmin, axis);
        const float hi   = axisCoord(cmax, axis);
        const float span = hi - lo;
        if (span < 1.0e-5f) {
            continue;
        }

        std::array<Bin, kBins> bins{};
        const float scale = static_cast<float>(kBins) / span;
        for (uint32_t i = 0u; i < count; ++i) {
            const auto& tri = cell.triangles[cell.triIndices[first + i]];
            int bi = static_cast<int>((axisCoord(tri.centroid, axis) - lo) * scale);
            bi = std::clamp(bi, 0, kBins - 1);
            bins[bi].count += 1u;
            bins[bi].bmin = MinVec3(bins[bi].bmin, tri.aabbMin);
            bins[bi].bmax = MaxVec3(bins[bi].bmax, tri.aabbMax);
        }

        std::array<float, kBins - 1> leftArea{};
        std::array<uint32_t, kBins - 1> leftCount{};
        {
            Vec3 lbmin{ kBigFloat,  kBigFloat,  kBigFloat};
            Vec3 lbmax{-kBigFloat, -kBigFloat, -kBigFloat};
            uint32_t lc = 0u;
            for (int i = 0; i < kBins - 1; ++i) {
                if (bins[i].count > 0u) {
                    lbmin = MinVec3(lbmin, bins[i].bmin);
                    lbmax = MaxVec3(lbmax, bins[i].bmax);
                    lc += bins[i].count;
                }
                leftCount[i] = lc;
                leftArea[i]  = (lc > 0u) ? AabbSurfaceArea(lbmin, lbmax) : 0.0f;
            }
        }

        {
            Vec3 rbmin{ kBigFloat,  kBigFloat,  kBigFloat};
            Vec3 rbmax{-kBigFloat, -kBigFloat, -kBigFloat};
            uint32_t rc = 0u;
            for (int i = kBins - 1; i >= 1; --i) {
                if (bins[i].count > 0u) {
                    rbmin = MinVec3(rbmin, bins[i].bmin);
                    rbmax = MaxVec3(rbmax, bins[i].bmax);
                    rc += bins[i].count;
                }
                const uint32_t lc = leftCount[i - 1];
                if (lc > 0u && rc > 0u) {
                    const float ra = AabbSurfaceArea(rbmin, rbmax);
                    const float cost = 0.25f * parentArea +
                                       static_cast<float>(lc) * leftArea[i - 1] +
                                       static_cast<float>(rc) * ra;
                    if (cost < bestCost) {
                        bestCost  = cost;
                        bestAxis  = axis;
                        bestSplit = lo + (static_cast<float>(i) / static_cast<float>(kBins)) * span;
                    }
                }
            }
        }
    }

    uint32_t mid = first + count / 2u;
    if (bestAxis >= 0) {
        auto beginIt = cell.triIndices.begin() + static_cast<std::ptrdiff_t>(first);
        auto endIt   = beginIt + static_cast<std::ptrdiff_t>(count);
        auto partIt  = std::partition(beginIt, endIt, [&](uint32_t idx) {
            return axisCoord(cell.triangles[idx].centroid, bestAxis) < bestSplit;
        });
        mid = static_cast<uint32_t>(partIt - cell.triIndices.begin());
        if (mid == first || mid == first + count) {
            mid = first + count / 2u;
        }
    } else {
        // Fallback: split longest centroid axis by median if count > 16, else leaf.
        if (count <= 16u) {
            cell.nodes[nodeIdx].leftFirst = first;
            cell.nodes[nodeIdx].triCount  = count;
            return;
        }
        const Vec3 cspan = cmax - cmin;
        int axis = 0;
        if (cspan.y > cspan.x && cspan.y >= cspan.z) axis = 1;
        else if (cspan.z > cspan.x && cspan.z > cspan.y) axis = 2;

        auto beginIt = cell.triIndices.begin() + static_cast<std::ptrdiff_t>(first);
        auto midIt   = beginIt + static_cast<std::ptrdiff_t>(count / 2u);
        auto endIt   = beginIt + static_cast<std::ptrdiff_t>(count);
        std::nth_element(beginIt, midIt, endIt, [&](uint32_t a, uint32_t b) {
            return axisCoord(cell.triangles[a].centroid, axis) <
                   axisCoord(cell.triangles[b].centroid, axis);
        });
        mid = first + count / 2u;
    }

    const uint32_t leftIdx  = static_cast<uint32_t>(cell.nodes.size());
    const uint32_t rightIdx = leftIdx + 1u;
    cell.nodes.push_back(BvhNode{});
    cell.nodes.push_back(BvhNode{});

    cell.nodes[nodeIdx].leftFirst = leftIdx;
    cell.nodes[nodeIdx].triCount  = 0u;

    BuildBvhRecursive(cell, leftIdx, first, mid - first);
    BuildBvhRecursive(cell, rightIdx, mid, first + count - mid);
}

// ============================================================================
// Exact Raycast (BVH Triangles + Heightfield March)
// ============================================================================

void RawWorldPhysics::RayCastCellBvh(const CellBvh& cell, Vec3 origin, Vec3 dir, Vec3 invDir,
                                     float& bestT, RayHit& bestHit) const {
    if (cell.nodes.empty()) {
        return;
    }

    float rootMin = 0.0f, rootMax = 0.0f;
    if (!RayIntersectsAabb(origin, invDir, cell.worldMin, cell.worldMax, bestT, rootMin, rootMax)) {
        return;
    }

    uint32_t stack[64];
    int sp = 0;
    stack[sp++] = 0u;

    while (sp > 0) {
        const BvhNode& node = cell.nodes[stack[--sp]];
        if (node.triCount > 0u) {
            for (uint32_t i = 0u; i < node.triCount; ++i) {
                const PhysicsTriangle& tri = cell.triangles[cell.triIndices[node.leftFirst + i]];
                float t = 0.0f;
                if (RayIntersectsTriangle(origin, dir, tri.v0, tri.v1, tri.v2, bestT, t)) {
                    bestT = t;
                    const Vec3 pt = origin + dir * t;
                    Vec3 n = tri.faceNormal;
                    if (Dot(n, dir) > 0.0f) {
                        n = Vec3{-n.x, -n.y, -n.z};
                    }
                    bestHit.hit         = true;
                    bestHit.hitTerrain  = false;
                    bestHit.distance    = t;
                    bestHit.point[0]    = pt.x;
                    bestHit.point[1]    = pt.y;
                    bestHit.point[2]    = pt.z;
                    bestHit.normal[0]   = n.x;
                    bestHit.normal[1]   = n.y;
                    bestHit.normal[2]   = n.z;
                    bestHit.cellKey     = tri.cellKey;
                    bestHit.instanceId  = tri.instanceId;
                    bestHit.materialTag = tri.materialTag;
                }
            }
        } else {
            const uint32_t leftIdx  = node.leftFirst;
            const uint32_t rightIdx = leftIdx + 1u;
            float lMin = kBigFloat, lMax = 0.0f;
            float rMin = kBigFloat, rMax = 0.0f;
            const bool hitL = RayIntersectsAabb(origin, invDir, cell.nodes[leftIdx].aabbMin,
                                                cell.nodes[leftIdx].aabbMax, bestT, lMin, lMax);
            const bool hitR = RayIntersectsAabb(origin, invDir, cell.nodes[rightIdx].aabbMin,
                                                cell.nodes[rightIdx].aabbMax, bestT, rMin, rMax);

            if (hitL && hitR) {
                // Push farther child first so nearer child is popped first (tightest early pruning).
                if (lMin < rMin) {
                    if (sp < 62) { stack[sp++] = rightIdx; stack[sp++] = leftIdx; }
                } else {
                    if (sp < 62) { stack[sp++] = leftIdx; stack[sp++] = rightIdx; }
                }
            } else if (hitL) {
                if (sp < 63) stack[sp++] = leftIdx;
            } else if (hitR) {
                if (sp < 63) stack[sp++] = rightIdx;
            }
        }
    }
}

void RawWorldPhysics::RayCastHeightfield(const TerrainCellHeightfield& hf, Vec3 origin, Vec3 dir,
                                         Vec3 invDir, float& bestT, RayHit& bestHit) const {
    const Vec3 bmin{hf.originX, hf.originY, hf.minZ - 1.0e-2f};
    const Vec3 bmax{hf.originX + hf.sizeX, hf.originY + hf.sizeY, hf.maxZ + 1.0e-2f};
    float tEnter = 0.0f, tExit = 0.0f;
    if (!RayIntersectsAabb(origin, invDir, bmin, bmax, bestT, tEnter, tExit)) {
        return;
    }

    const float horizSpeed = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    // Exact O(1) fast path for vertical rays:
    if (horizSpeed < 1.0e-5f) {
        float h = 0.0f;
        Vec3 n{0.0f, 0.0f, 1.0f};
        if (SampleHeightfieldLocal(hf, origin.x, origin.y, &h, &n)) {
            if (std::abs(dir.z) > 1.0e-6f) {
                const float t = (h - origin.z) / dir.z;
                if (t >= 0.0f && t <= bestT) {
                    bestT = t;
                    const Vec3 pt = origin + dir * t;
                    if (Dot(n, dir) > 0.0f) {
                        n = Vec3{-n.x, -n.y, -n.z};
                    }
                    bestHit.hit         = true;
                    bestHit.hitTerrain  = true;
                    bestHit.distance    = t;
                    bestHit.point[0]    = pt.x;
                    bestHit.point[1]    = pt.y;
                    bestHit.point[2]    = pt.z;
                    bestHit.normal[0]   = n.x;
                    bestHit.normal[1]   = n.y;
                    bestHit.normal[2]   = n.z;
                    bestHit.cellKey     = hf.cellKey;
                    bestHit.instanceId  = 0u;
                    bestHit.materialTag = hf.materialTag;
                }
            }
        }
        return;
    }

    // Adaptive heightfield march + 14-step bisection refinement:
    const float cellDx = hf.sizeX / static_cast<float>(hf.gridW - 1u);
    const float cellDy = hf.sizeY / static_cast<float>(hf.gridH - 1u);
    const float stepT  = std::max(0.25f, 0.35f * std::min(cellDx, cellDy) / horizSpeed);

    const float spanT  = std::max(0.0f, tExit - tEnter);
    const int   steps  = std::clamp(static_cast<int>(std::ceil(spanT / stepT)), 1, 2048);
    const float dt     = (spanT > 0.0f) ? (spanT / static_cast<float>(steps)) : 0.0f;

    float tPrev = tEnter;
    float hPrev = 0.0f;
    Vec3  pPrev = origin + dir * tPrev;
    if (!SampleHeightfieldLocal(hf, pPrev.x, pPrev.y, &hPrev, nullptr)) {
        return;
    }
    float fPrev = pPrev.z - hPrev;

    for (int i = 1; i <= steps; ++i) {
        const float tCurr = std::min(tExit, tEnter + dt * static_cast<float>(i));
        if (tCurr > bestT && tPrev > bestT) {
            break;
        }
        const Vec3 pCurr = origin + dir * tCurr;
        float hCurr = 0.0f;
        if (!SampleHeightfieldLocal(hf, pCurr.x, pCurr.y, &hCurr, nullptr)) {
            tPrev = tCurr;
            continue;
        }
        const float fCurr = pCurr.z - hCurr;

        // Crossing detected (either descending through terrain or ascending from below):
        if ((fPrev >= 0.0f && fCurr <= 0.0f) || (fPrev <= 0.0f && fCurr >= 0.0f)) {
            float lo = tPrev;
            float hi = tCurr;
            float fLo = fPrev;
            for (int iter = 0; iter < 14; ++iter) {
                const float mid = 0.5f * (lo + hi);
                const Vec3 pm = origin + dir * mid;
                float hm = 0.0f;
                SampleHeightfieldLocal(hf, pm.x, pm.y, &hm, nullptr);
                const float fm = pm.z - hm;
                if ((fLo >= 0.0f && fm <= 0.0f) || (fLo <= 0.0f && fm >= 0.0f)) {
                    hi = mid;
                } else {
                    lo  = mid;
                    fLo = fm;
                }
            }
            const float tHit = 0.5f * (lo + hi);
            if (tHit >= 0.0f && tHit <= bestT) {
                const Vec3 pt = origin + dir * tHit;
                float hFinal = 0.0f;
                Vec3  nFinal{0.0f, 0.0f, 1.0f};
                SampleHeightfieldLocal(hf, pt.x, pt.y, &hFinal, &nFinal);
                if (Dot(nFinal, dir) > 0.0f) {
                    nFinal = Vec3{-nFinal.x, -nFinal.y, -nFinal.z};
                }
                bestT               = tHit;
                bestHit.hit         = true;
                bestHit.hitTerrain  = true;
                bestHit.distance    = tHit;
                bestHit.point[0]    = pt.x;
                bestHit.point[1]    = pt.y;
                bestHit.point[2]    = hFinal;
                bestHit.normal[0]   = nFinal.x;
                bestHit.normal[1]   = nFinal.y;
                bestHit.normal[2]   = nFinal.z;
                bestHit.cellKey     = hf.cellKey;
                bestHit.instanceId  = 0u;
                bestHit.materialTag = hf.materialTag;
            }
            return;
        }
        tPrev = tCurr;
        fPrev = fCurr;
    }
}

RayHit RawWorldPhysics::RayCast(Vec3 origin, Vec3 dir, float maxDist) const {
    RayHit bestHit{};
    if (!IsFiniteVec3(origin) || !IsFiniteVec3(dir) || !std::isfinite(maxDist) || maxDist <= 0.0f) {
        return bestHit;
    }
    const float dirLen2 = LengthSq(dir);
    if (dirLen2 < 1.0e-12f) {
        return bestHit;
    }
    const Vec3 dirN   = dir / std::sqrt(dirLen2);
    const Vec3 invDir = MakeInvDir(dirN);
    float bestT       = maxDist;

    for (const auto& [key, cell] : cellBvhs_) {
        (void)key;
        RayCastCellBvh(cell, origin, dirN, invDir, bestT, bestHit);
    }
    for (const auto& [key, hf] : heightfields_) {
        (void)key;
        RayCastHeightfield(hf, origin, dirN, invDir, bestT, bestHit);
    }
    return bestHit;
}

// ============================================================================
// Capsule & Sphere Contact Gathering (BVH Triangles + Heightfield Patches)
// ============================================================================

void RawWorldPhysics::GatherCapsuleContacts(Vec3 feetPos, float radius, float height,
                                            std::vector<ContactInfo>& outContacts) const {
    outContacts.clear();

    const float capR   = std::max(1.0f, radius);
    const float capH   = std::max(2.0f * capR + 0.01f, height);
    const Vec3  segA{feetPos.x, feetPos.y, feetPos.z + capR};
    const Vec3  segB{feetPos.x, feetPos.y, feetPos.z + capH - capR};
    const Vec3  segMid = (segA + segB) * 0.5f;

    const Vec3 qMin{feetPos.x - capR - 0.5f, feetPos.y - capR - 0.5f, feetPos.z - 0.5f};
    const Vec3 qMax{feetPos.x + capR + 0.5f, feetPos.y + capR + 0.5f, feetPos.z + capH + 0.5f};

    auto testTriangle = [&](Vec3 v0, Vec3 v1, Vec3 v2, Vec3 faceNormal,
                            uint64_t cellKey, bool isTerrain) {
        Vec3 cSeg{}, cTri{};
        const float d2 = ClosestSegmentTriangle(segA, segB, v0, v1, v2, cSeg, cTri);
        if (d2 >= capR * capR) {
            return;
        }
        const float dist = std::sqrt(std::max(0.0f, d2));
        Vec3 n{};
        if (dist > 1.0e-5f) {
            n = (cSeg - cTri) / dist;
        } else {
            n = faceNormal;
            if (Dot(segMid - cTri, n) < 0.0f) {
                n = Vec3{-n.x, -n.y, -n.z};
            }
        }
        if (isTerrain && n.z < 0.0f) {
            n = Vec3{-n.x, -n.y, -n.z};
        }

        ContactInfo ci{};
        ci.hit         = true;
        ci.hitTerrain  = isTerrain;
        ci.penetration = capR - dist;
        ci.normal      = n;
        ci.point       = cTri;
        ci.cellKey     = cellKey;
        outContacts.push_back(ci);
    };

    // 1. BVH static triangles:
    for (const auto& [key, cell] : cellBvhs_) {
        (void)key;
        if (cell.nodes.empty() || !AabbOverlaps(qMin, qMax, cell.worldMin, cell.worldMax)) {
            continue;
        }
        uint32_t stack[64];
        int sp = 0;
        stack[sp++] = 0u;
        while (sp > 0) {
            const BvhNode& node = cell.nodes[stack[--sp]];
            if (!AabbOverlaps(qMin, qMax, node.aabbMin, node.aabbMax)) {
                continue;
            }
            if (node.triCount > 0u) {
                for (uint32_t i = 0u; i < node.triCount; ++i) {
                    const PhysicsTriangle& tri = cell.triangles[cell.triIndices[node.leftFirst + i]];
                    if (!AabbOverlaps(qMin, qMax, tri.aabbMin, tri.aabbMax)) {
                        continue;
                    }
                    testTriangle(tri.v0, tri.v1, tri.v2, tri.faceNormal, tri.cellKey, false);
                }
            } else {
                if (sp < 62) {
                    stack[sp++] = node.leftFirst;
                    stack[sp++] = node.leftFirst + 1u;
                }
            }
        }
    }

    // 2. Terrain heightfield patches:
    for (const auto& [key, hf] : heightfields_) {
        const Vec3 hfMin{hf.originX, hf.originY, hf.minZ - 1.0f};
        const Vec3 hfMax{hf.originX + hf.sizeX, hf.originY + hf.sizeY, hf.maxZ + 1.0f};
        if (!AabbOverlaps(qMin, qMax, hfMin, hfMax)) {
            continue;
        }

        // Exact analytical terrain tangent-plane contact at capsule XY centerline (fast, smooth, exact on flat/slopes):
        float centerH = 0.0f;
        Vec3  centerN{0.0f, 0.0f, 1.0f};
        if (SampleHeightfieldLocal(hf, feetPos.x, feetPos.y, &centerH, &centerN)) {
            // Distance from bottom sphere center segA to tangent plane at (feetPos.x, feetPos.y, centerH):
            // (segA - P_ground) . N = (feetPos.z + capR - centerH) * N.z
            const float signedDist = (segA.z - centerH) * centerN.z;
            if (signedDist < capR) {
                ContactInfo ci{};
                ci.hit         = true;
                ci.hitTerrain  = true;
                ci.penetration = capR - signedDist;
                ci.normal      = centerN;
                ci.point       = Vec3{feetPos.x, feetPos.y, centerH};
                ci.cellKey     = key;
                outContacts.push_back(ci);
            }
        }

        // Also test overlapped heightfield grid quads (catches sharp terrain ridges / cliff steps touching capsule side):
        const float dx = hf.sizeX / static_cast<float>(hf.gridW - 1u);
        const float dy = hf.sizeY / static_cast<float>(hf.gridH - 1u);
        const int ixMin = std::max(0, static_cast<int>(std::floor((qMin.x - hf.originX) / dx)));
        const int ixMax = std::min(static_cast<int>(hf.gridW) - 2,
                                   static_cast<int>(std::floor((qMax.x - hf.originX) / dx)));
        const int iyMin = std::max(0, static_cast<int>(std::floor((qMin.y - hf.originY) / dy)));
        const int iyMax = std::min(static_cast<int>(hf.gridH) - 2,
                                   static_cast<int>(std::floor((qMax.y - hf.originY) / dy)));

        for (int iy = iyMin; iy <= iyMax; ++iy) {
            const float y0 = hf.originY + static_cast<float>(iy) * dy;
            const float y1 = y0 + dy;
            for (int ix = ixMin; ix <= ixMax; ++ix) {
                const float x0 = hf.originX + static_cast<float>(ix) * dx;
                const float x1 = x0 + dx;
                const float z00 = hf.heights[static_cast<size_t>(iy) * hf.gridW + static_cast<size_t>(ix)];
                const float z10 = hf.heights[static_cast<size_t>(iy) * hf.gridW + static_cast<size_t>(ix + 1)];
                const float z01 = hf.heights[static_cast<size_t>(iy + 1) * hf.gridW + static_cast<size_t>(ix)];
                const float z11 = hf.heights[static_cast<size_t>(iy + 1) * hf.gridW + static_cast<size_t>(ix + 1)];

                const Vec3 p00{x0, y0, z00};
                const Vec3 p10{x1, y0, z10};
                const Vec3 p01{x0, y1, z01};
                const Vec3 p11{x1, y1, z11};

                const Vec3 nA = Normalize(Cross(p10 - p00, p11 - p00));
                const Vec3 nB = Normalize(Cross(p11 - p00, p01 - p00));
                testTriangle(p00, p10, p11, nA, key, true);
                testTriangle(p00, p11, p01, nB, key, true);
            }
        }
    }
}

void RawWorldPhysics::GatherSphereContacts(Vec3 center, float radius,
                                           std::vector<ContactInfo>& outContacts) const {
    outContacts.clear();
    const float r = std::max(0.5f, radius);
    const Vec3 qMin{center.x - r - 0.5f, center.y - r - 0.5f, center.z - r - 0.5f};
    const Vec3 qMax{center.x + r + 0.5f, center.y + r + 0.5f, center.z + r + 0.5f};

    auto testTri = [&](Vec3 v0, Vec3 v1, Vec3 v2, Vec3 faceNormal, uint64_t cellKey, bool isTerrain) {
        const Vec3 closest = ClosestPointOnTriangle(center, v0, v1, v2);
        const Vec3 delta   = center - closest;
        const float d2     = LengthSq(delta);
        if (d2 >= r * r) return;
        const float dist = std::sqrt(std::max(0.0f, d2));
        Vec3 n = (dist > 1.0e-5f) ? (delta / dist) : faceNormal;
        if (isTerrain && n.z < 0.0f) n = Vec3{-n.x, -n.y, -n.z};
        ContactInfo ci{};
        ci.hit         = true;
        ci.hitTerrain  = isTerrain;
        ci.penetration = r - dist;
        ci.normal      = n;
        ci.point       = closest;
        ci.cellKey     = cellKey;
        outContacts.push_back(ci);
    };

    for (const auto& [key, cell] : cellBvhs_) {
        (void)key;
        if (cell.nodes.empty() || !AabbOverlaps(qMin, qMax, cell.worldMin, cell.worldMax)) {
            continue;
        }
        uint32_t stack[64];
        int sp = 0;
        stack[sp++] = 0u;
        while (sp > 0) {
            const BvhNode& node = cell.nodes[stack[--sp]];
            if (!AabbOverlaps(qMin, qMax, node.aabbMin, node.aabbMax)) continue;
            if (node.triCount > 0u) {
                for (uint32_t i = 0u; i < node.triCount; ++i) {
                    const PhysicsTriangle& tri = cell.triangles[cell.triIndices[node.leftFirst + i]];
                    if (!AabbOverlaps(qMin, qMax, tri.aabbMin, tri.aabbMax)) continue;
                    testTri(tri.v0, tri.v1, tri.v2, tri.faceNormal, tri.cellKey, false);
                }
            } else if (sp < 62) {
                stack[sp++] = node.leftFirst;
                stack[sp++] = node.leftFirst + 1u;
            }
        }
    }

    float h = 0.0f;
    Vec3  n{0.0f, 0.0f, 1.0f};
    uint64_t hfKey = 0ull;
    if (SampleTerrainHeight(center.x, center.y, &h, &n, nullptr, &hfKey)) {
        const float signedDist = (center.z - h) * n.z;
        if (signedDist < r) {
            ContactInfo ci{};
            ci.hit         = true;
            ci.hitTerrain  = true;
            ci.penetration = r - signedDist;
            ci.normal      = n;
            ci.point       = Vec3{center.x, center.y, h};
            ci.cellKey     = hfKey;
            outContacts.push_back(ci);
        }
    }
}

void RawWorldPhysics::DepenetrateCapsule(Vec3& feetPos, Vec3& velocity, float radius, float height,
                                         float cosMaxSlope, bool& outGrounded, bool& outSliding,
                                         Vec3& outGroundNormal, uint64_t& outGroundCellKey,
                                         uint32_t& outContactCount, bool& outHitWall) const {
    outGrounded      = false;
    outSliding       = false;
    outGroundNormal  = Vec3{0.0f, 0.0f, 1.0f};
    outGroundCellKey = 0ull;
    outContactCount  = 0u;
    outHitWall       = false;

    float bestGroundNz = -1.0f;
    std::vector<ContactInfo> contacts;
    contacts.reserve(16u);

    constexpr int kMaxDepenIters = 4;
    for (int iter = 0; iter < kMaxDepenIters; ++iter) {
        GatherCapsuleContacts(feetPos, radius, height, contacts);
        if (contacts.empty()) {
            break;
        }

        // Resolve deepest contacts first for stable corner/ramp convergence:
        std::sort(contacts.begin(), contacts.end(), [](const ContactInfo& a, const ContactInfo& b) {
            return a.penetration > b.penetration;
        });

        bool resolvedAny = false;
        for (const auto& c : contacts) {
            if (c.penetration <= 1.0e-5f) {
                continue;
            }
            outContactCount += 1u;

            // Push capsule out along contact normal:
            feetPos = feetPos + c.normal * c.penetration;

            // Slide velocity along contact plane (remove inward component):
            const float vn = Dot(velocity, c.normal);
            if (vn < 0.0f) {
                velocity = velocity - c.normal * vn;
            }

            if (c.normal.z >= cosMaxSlope) {
                outGrounded = true;
                if (c.normal.z > bestGroundNz) {
                    bestGroundNz     = c.normal.z;
                    outGroundNormal  = c.normal;
                    outGroundCellKey = c.cellKey;
                }
            } else {
                outHitWall = true;
                if (c.normal.z > 0.05f) {
                    outSliding = true;
                }
            }
            resolvedAny = true;
            // Re-gather after resolving the deepest contact so subsequent contacts reflect updated position.
            break;
        }
        if (!resolvedAny) {
            break;
        }
    }

    // Hard terrain safety clamp: capsule feet never tunnel beneath sampled terrain height at (x, y).
    float terrainZ = 0.0f;
    Vec3  terrainN{0.0f, 0.0f, 1.0f};
    uint64_t terrainKey = 0ull;
    if (SampleTerrainHeight(feetPos.x, feetPos.y, &terrainZ, &terrainN, nullptr, &terrainKey)) {
        if (feetPos.z < terrainZ) {
            feetPos.z = terrainZ;
            const float vn = Dot(velocity, terrainN);
            if (vn < 0.0f) {
                velocity = velocity - terrainN * vn;
            }
            if (terrainN.z >= cosMaxSlope) {
                outGrounded = true;
                if (terrainN.z > bestGroundNz) {
                    outGroundNormal  = terrainN;
                    outGroundCellKey = terrainKey;
                }
            } else if (terrainN.z > 0.05f) {
                outSliding = true;
            }
            outContactCount += 1u;
        }
    }

    if (outGrounded) {
        outSliding = false;
    }
}

// ============================================================================
// Capsule Character Controller (Sub-Stepped)
// ============================================================================

void RawWorldPhysics::StepCharacterCapsule(CharacterBody& body, const CharacterInput& input, float dt) const {
    if (!std::isfinite(dt) || dt <= 0.0f) {
        return;
    }
    if (!IsFiniteVec3(body.position) || !IsFiniteVec3(body.velocity)) {
        return;
    }

    body.radius       = std::max(1.0f, body.radius);
    body.height       = std::max(2.0f * body.radius + 1.0f, body.height);
    body.stepUpHeight = Clamp(body.stepUpHeight, 0.0f, 42.0f);
    body.maxSlopeDeg  = Clamp(body.maxSlopeDeg, 5.0f, 85.0f);

    const float cosMaxSlope = std::cos(body.maxSlopeDeg * kDegToRad);

    // Clamp wish direction length to <= 1:
    Vec2 wishXY = input.wishDirXY;
    if (!std::isfinite(wishXY.x) || !std::isfinite(wishXY.y)) {
        wishXY = {0.0f, 0.0f};
    }
    const float wishLen = Length(wishXY);
    if (wishLen > 1.0f) {
        wishXY = wishXY * (1.0f / wishLen);
    }
    const float maxSpeed = std::isfinite(input.maxSpeed) ? std::max(0.0f, input.maxSpeed) : 0.0f;

    // Anti-tunneling substep calculation (displacement <= 0.45 * radius and subDt <= 1/60s):
    const float estDisp = Length(body.velocity) * dt +
                          0.5f * std::abs(body.gravityZ) * dt * dt +
                          maxSpeed * dt;
    const float maxSafeDisp = 0.45f * body.radius;
    const int stepsByTime   = static_cast<int>(std::ceil(dt * 60.0f));
    const int stepsByDisp   = static_cast<int>(std::ceil(estDisp / std::max(1.0f, maxSafeDisp)));
    const int subSteps      = std::clamp(std::max(stepsByTime, stepsByDisp), 1, 32);
    const float subDt       = dt / static_cast<float>(subSteps);

    bool jumpPending = input.jumpPressed;

    for (int step = 0; step < subSteps; ++step) {
        // 1. Evaluate local water level & submersion fraction:
        float cellWaterZ = defaultWaterLevelZ_;
        SampleTerrainHeight(body.position.x, body.position.y, nullptr, nullptr, &cellWaterZ, nullptr);
        const float activeWaterZ = input.hasWaterOverride ? input.waterLevelOverrideZ : cellWaterZ;
        body.waterLevelZ  = activeWaterZ;
        body.submersion01 = Clamp01((activeWaterZ - body.position.z) / body.height);
        body.inWater      = (body.submersion01 > 0.05f);

        const bool wasGrounded = body.grounded && !body.sliding;

        // 2. Horizontal steering & friction:
        const Vec2 targetHoriz{wishXY.x * maxSpeed, wishXY.y * maxSpeed};
        if (body.inWater) {
            const float swimMax = maxSpeed * 0.72f;
            const Vec2 swimTarget{wishXY.x * swimMax, wishXY.y * swimMax};
            const float accel = body.groundAccel * 0.45f * subDt;
            const Vec2 diff{swimTarget.x - body.velocity.x, swimTarget.y - body.velocity.y};
            const float diffLen = Length(diff);
            if (diffLen <= accel) {
                body.velocity.x = swimTarget.x;
                body.velocity.y = swimTarget.y;
            } else if (diffLen > 1.0e-6f) {
                body.velocity.x += (diff.x / diffLen) * accel;
                body.velocity.y += (diff.y / diffLen) * accel;
            }
            const float swimVert = Clamp(input.swimVertical, -1.0f, 1.0f);
            body.velocity.z += swimVert * body.groundAccel * 0.35f * subDt;
        } else if (wasGrounded) {
            if (wishLen < 1.0e-3f) {
                const float damp = std::exp(-body.groundFriction * subDt);
                body.velocity.x *= damp;
                body.velocity.y *= damp;
                if (std::abs(body.velocity.x) < 1.0e-3f) body.velocity.x = 0.0f;
                if (std::abs(body.velocity.y) < 1.0e-3f) body.velocity.y = 0.0f;
            } else {
                const Vec2 diff{targetHoriz.x - body.velocity.x, targetHoriz.y - body.velocity.y};
                const float diffLen = Length(diff);
                const float maxDelta = body.groundAccel * subDt;
                if (diffLen <= maxDelta) {
                    body.velocity.x = targetHoriz.x;
                    body.velocity.y = targetHoriz.y;
                } else if (diffLen > 1.0e-6f) {
                    body.velocity.x += (diff.x / diffLen) * maxDelta;
                    body.velocity.y += (diff.y / diffLen) * maxDelta;
                }
            }
            // Project horizontal velocity cleanly onto walkable ground tangent plane so walking uphill
            // follows the slope without bouncing or losing speed:
            if (body.groundNormal.z >= cosMaxSlope && !jumpPending) {
                const float slopeDz = -(body.groundNormal.x * body.velocity.x +
                                        body.groundNormal.y * body.velocity.y) /
                                        std::max(0.25f, body.groundNormal.z);
                body.velocity.z = slopeDz;
            }
        } else {
            // Airborne or sliding down a steep (> maxSlopeDeg) slope:
            if (wishLen > 1.0e-3f && !body.sliding) {
                const Vec2 diff{targetHoriz.x - body.velocity.x, targetHoriz.y - body.velocity.y};
                const float diffLen = Length(diff);
                const float maxDelta = body.airAccel * subDt;
                if (diffLen <= maxDelta) {
                    body.velocity.x = targetHoriz.x;
                    body.velocity.y = targetHoriz.y;
                } else if (diffLen > 1.0e-6f) {
                    body.velocity.x += (diff.x / diffLen) * maxDelta;
                    body.velocity.y += (diff.y / diffLen) * maxDelta;
                }
            }
        }

        // 3. Jump impulse (consumed once):
        if (jumpPending && wasGrounded && !body.inWater) {
            body.velocity.z = body.jumpSpeed;
            body.grounded   = false;
            jumpPending     = false;
        }

        // 4. Gravity + Archimedes buoyancy + fluid drag:
        if (!wasGrounded || body.inWater || body.velocity.z > 0.0f) {
            body.velocity.z += body.gravityZ * subDt;
        } else {
            // Slight downward bias keeps capsule firmly seated on slopes/stairs:
            body.velocity.z += body.gravityZ * subDt * 0.25f;
        }

        if (body.submersion01 > 0.0f) {
            body.velocity.z += body.waterBuoyancy * body.submersion01 * subDt;
            const float drag = std::exp(-body.swimDrag * body.submersion01 * subDt);
            body.velocity.x *= drag;
            body.velocity.y *= drag;
            body.velocity.z *= drag;
        }

        // 5. Integrate candidate position & depenetrate:
        const Vec3 startPos = body.position;
        Vec3 posA = startPos + body.velocity * subDt;
        Vec3 velA = body.velocity;
        bool     gA = false, sA = false, wallA = false;
        Vec3     gnA{0.0f, 0.0f, 1.0f};
        uint64_t gcA = 0ull;
        uint32_t ccA = 0u;

        DepenetrateCapsule(posA, velA, body.radius, body.height, cosMaxSlope,
                           gA, sA, gnA, gcA, ccA, wallA);

        // 6. Smooth step-up probe (<= stepUpHeight) when horizontally obstructed while grounded:
        bool usedStepUp = false;
        const float horizSpeed = std::sqrt(body.velocity.x * body.velocity.x + body.velocity.y * body.velocity.y);
        if (wasGrounded && wallA && body.stepUpHeight > 1.0f && horizSpeed > 1.0f) {
            const float progressA = Length(Vec2{posA.x - startPos.x, posA.y - startPos.y});

            // Step 1: lift vertically by stepUpHeight (check ceiling clearance)
            Vec3 posB = Vec3{startPos.x, startPos.y, startPos.z + body.stepUpHeight};
            Vec3 velB{body.velocity.x, body.velocity.y, 0.0f};
            bool gTmp = false, sTmp = false, wTmp = false;
            Vec3 gnTmp{0.0f, 0.0f, 1.0f};
            uint64_t gcTmp = 0ull;
            uint32_t ccTmp = 0u;
            DepenetrateCapsule(posB, velB, body.radius, body.height, cosMaxSlope,
                               gTmp, sTmp, gnTmp, gcTmp, ccTmp, wTmp);

            // Step 2: advance horizontally at lifted height
            posB.x += body.velocity.x * subDt;
            posB.y += body.velocity.y * subDt;
            DepenetrateCapsule(posB, velB, body.radius, body.height, cosMaxSlope,
                               gTmp, sTmp, gnTmp, gcTmp, ccTmp, wTmp);

            // Step 3: sweep strictly vertically downward from lifted height onto the step surface.
            // Resolving downward contact strictly along +Z prevents the rounded capsule bottom from
            // being pushed horizontally backwards off a sharp stair riser edge.
            const float maxDrop   = body.stepUpHeight + 2.0f;
            const float stepInc   = std::max(1.5f, std::min(4.0f, 0.2f * body.radius));
            const int   dropSteps = std::clamp(static_cast<int>(std::ceil(maxDrop / stepInc)), 1, 24);
            const float dz        = maxDrop / static_cast<float>(dropSteps);
            const float liftedZ   = posB.z;

            bool     landedStep = false;
            Vec3     gnB{0.0f, 0.0f, 1.0f};
            uint64_t gcB = 0ull;
            uint32_t ccB = 0u;
            std::vector<ContactInfo> dropContacts;
            dropContacts.reserve(8u);

            for (int ds = 0; ds < dropSteps; ++ds) {
                posB.z -= dz;
                GatherCapsuleContacts(posB, body.radius, body.height, dropContacts);
                float bestLift = 0.0f;
                float bestNz   = -1.0f;
                for (const auto& c : dropContacts) {
                    if (c.penetration > 1.0e-5f && c.normal.z > 0.15f) {
                        const float lift = c.penetration / std::max(0.25f, c.normal.z);
                        if (lift > bestLift) {
                            bestLift = lift;
                        }
                        if (c.normal.z > bestNz) {
                            bestNz = c.normal.z;
                            gnB    = c.normal;
                            gcB    = c.cellKey;
                        }
                        ccB += 1u;
                    }
                }
                if (bestLift > 0.0f) {
                    posB.z = std::min(liftedZ, posB.z + bestLift);
                    landedStep = true;
                    break;
                }
            }

            const float progressB = Length(Vec2{posB.x - startPos.x, posB.y - startPos.y});
            const float riseZ     = posB.z - startPos.z;
            if (landedStep && riseZ >= -1.0f && riseZ <= body.stepUpHeight + 0.5f &&
                progressB > progressA + 0.05f) {
                body.position      = posB;
                body.velocity      = Vec3{velB.x, velB.y, 0.0f};
                body.grounded      = true;
                body.sliding       = false;
                body.groundNormal  = (gnB.z >= cosMaxSlope) ? gnB : Vec3{0.0f, 0.0f, 1.0f};
                body.groundCellKey = gcB;
                body.contactCount  = ccB;
                usedStepUp         = true;
            }
        }

        if (!usedStepUp) {
            body.position      = posA;
            body.velocity      = velA;
            body.grounded      = gA;
            body.sliding       = sA;
            body.groundNormal  = gnA;
            body.groundCellKey = gcA;
            body.contactCount  = ccA;
        }

        // Update final water state for the resolved position:
        body.submersion01 = Clamp01((activeWaterZ - body.position.z) / body.height);
        body.inWater      = (body.submersion01 > 0.05f);

        // Final finite clamp safety net:
        if (!IsFiniteVec3(body.position)) body.position = startPos;
        if (!IsFiniteVec3(body.velocity)) body.velocity = {};
    }
}

// ============================================================================
// Dynamic Rigid-Body Sphere Stepper
// ============================================================================

void RawWorldPhysics::StepDynamicSphere(DynamicSphereBody& body, float dt,
                                        bool hasWaterOverride, float waterLevelOverrideZ) const {
    if (!std::isfinite(dt) || dt <= 0.0f || !IsFiniteVec3(body.position) || !IsFiniteVec3(body.velocity)) {
        return;
    }

    body.radius      = std::max(0.5f, body.radius);
    body.restitution = Clamp01(body.restitution);
    body.friction    = Clamp01(body.friction);

    const float estDisp = Length(body.velocity) * dt + 0.5f * std::abs(body.gravityZ) * dt * dt;
    const int subSteps  = std::clamp(
        std::max(static_cast<int>(std::ceil(dt * 60.0f)),
                 static_cast<int>(std::ceil(estDisp / std::max(1.0f, 0.45f * body.radius)))),
        1, 32);
    const float subDt = dt / static_cast<float>(subSteps);

    std::vector<ContactInfo> contacts;
    contacts.reserve(12u);

    for (int s = 0; s < subSteps; ++s) {
        float cellWaterZ = defaultWaterLevelZ_;
        SampleTerrainHeight(body.position.x, body.position.y, nullptr, nullptr, &cellWaterZ, nullptr);
        const float waterZ = hasWaterOverride ? waterLevelOverrideZ : cellWaterZ;
        const float submersion = Clamp01((waterZ - (body.position.z - body.radius)) / (2.0f * body.radius));
        body.inWater = (submersion > 0.05f);

        body.velocity.z += body.gravityZ * subDt;
        if (submersion > 0.0f) {
            body.velocity.z += body.waterBuoyancy * submersion * subDt;
            const float wd = std::exp(-body.waterDrag * submersion * subDt);
            body.velocity = body.velocity * wd;
        } else if (body.linearDrag > 0.0f) {
            body.velocity = body.velocity * std::exp(-body.linearDrag * subDt);
        }

        body.position = body.position + body.velocity * subDt;
        body.resting  = false;

        for (int iter = 0; iter < 4; ++iter) {
            GatherSphereContacts(body.position, body.radius, contacts);
            if (contacts.empty()) break;

            std::sort(contacts.begin(), contacts.end(), [](const ContactInfo& a, const ContactInfo& b) {
                return a.penetration > b.penetration;
            });
            const ContactInfo& c = contacts[0];
            if (c.penetration <= 1.0e-5f) break;

            body.position = body.position + c.normal * c.penetration;
            const float vn = Dot(body.velocity, c.normal);
            if (vn < 0.0f) {
                const float bounce = (std::abs(vn) > 25.0f) ? body.restitution : 0.0f;
                const Vec3 vNormal  = c.normal * vn;
                const Vec3 vTangent = body.velocity - vNormal;
                body.velocity = vTangent * (1.0f - body.friction * 0.25f) - vNormal * bounce;
                if (c.normal.z > 0.65f && std::abs(vn) <= 25.0f && Length(vTangent) < 5.0f) {
                    body.resting = true;
                }
            }
        }
    }
}

}  // namespace raw::worldgen
