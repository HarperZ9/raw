// CeMeshletBuilder.cpp: Wave 85 Watertight 64v/64t Meshlet Clusterizer & Monotonic QEM LOD DAG.
// Original work, MIT.
#include "CeMeshletBuilder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
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

inline Vec3 Sub(const Vec3& a, const Vec3& b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 Add(const Vec3& a, const Vec3& b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 Scale(const Vec3& v, float s) noexcept { return {v.x * s, v.y * s, v.z * s}; }
inline float Dot(const Vec3& a, const Vec3& b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 Cross(const Vec3& a, const Vec3& b) noexcept
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float Length(const Vec3& v) noexcept { return std::sqrt(Dot(v, v)); }
inline Vec3 SafeNorm(const Vec3& v, const Vec3& fb) noexcept
{
    const float l = Length(v);
    if (!std::isfinite(l) || l <= 1.0e-12f) return fb;
    const float inv = 1.0f / l;
    return {v.x * inv, v.y * inv, v.z * inv};
}

// Symmetric 4x4 Garland-Heckbert plane quadric (10 unique entries).
struct Quadric
{
    double a00 = 0.0, a01 = 0.0, a02 = 0.0, a03 = 0.0;
    double            a11 = 0.0, a12 = 0.0, a13 = 0.0;
    double                       a22 = 0.0, a23 = 0.0;
    double                                  a33 = 0.0;

    void AddPlane(double a, double b, double c, double d) noexcept
    {
        a00 += a * a; a01 += a * b; a02 += a * c; a03 += a * d;
        a11 += b * b; a12 += b * c; a13 += b * d;
        a22 += c * c; a23 += c * d;
        a33 += d * d;
    }

    Quadric operator+(const Quadric& o) const noexcept
    {
        Quadric r{};
        r.a00 = a00 + o.a00; r.a01 = a01 + o.a01; r.a02 = a02 + o.a02; r.a03 = a03 + o.a03;
        r.a11 = a11 + o.a11; r.a12 = a12 + o.a12; r.a13 = a13 + o.a13;
        r.a22 = a22 + o.a22; r.a23 = a23 + o.a23;
        r.a33 = a33 + o.a33;
        return r;
    }

    double Evaluate(double x, double y, double z) const noexcept
    {
        const double v = a00 * x * x + 2.0 * a01 * x * y + 2.0 * a02 * x * z + 2.0 * a03 * x +
                         a11 * y * y + 2.0 * a12 * y * z + 2.0 * a13 * y +
                         a22 * z * z + 2.0 * a23 * z +
                         a33;
        return (v > 0.0) ? v : 0.0;
    }
};

inline int8_t FloatToSnorm8(float v) noexcept
{
    const float clamped = (std::clamp)(v, -1.0f, 1.0f);
    const int   q       = static_cast<int>(std::lround(clamped * 127.0f));
    return static_cast<int8_t>((std::clamp)(q, -127, 127));
}

inline uint64_t EdgeKey(uint32_t a, uint32_t b) noexcept
{
    const uint32_t lo = (a < b) ? a : b;
    const uint32_t hi = (a < b) ? b : a;
    return (static_cast<uint64_t>(lo) << 32u) | static_cast<uint64_t>(hi);
}

void ComputeMeshletBoundsAndCone(
    const std::vector<RepairedVertex>& verts,
    BuiltMeshlet&                      m)
{
    if (m.vertexIndices.empty() || m.primitiveIndices.size() < 3u) {
        return;
    }

    // Bounding sphere: centroid + max distance, tightened via 2 Ritter passes.
    Vec3 c{0.0f, 0.0f, 0.0f};
    for (uint32_t vi : m.vertexIndices) {
        c.x += verts[vi].pos[0];
        c.y += verts[vi].pos[1];
        c.z += verts[vi].pos[2];
    }
    const float invV = 1.0f / static_cast<float>(m.vertexIndices.size());
    c = Scale(c, invV);

    float radSq = 0.0f;
    for (uint32_t vi : m.vertexIndices) {
        const Vec3 p{verts[vi].pos[0], verts[vi].pos[1], verts[vi].pos[2]};
        const Vec3 d = Sub(p, c);
        radSq = (std::max)(radSq, Dot(d, d));
    }
    float radius = (std::max)(std::sqrt(radSq), 1.0e-5f);

    // Normal bounding cone across triangle face normals.
    std::vector<Vec3> triNormals;
    const size_t triCount = m.primitiveIndices.size() / 3u;
    triNormals.reserve(triCount);
    Vec3 axisSum{0.0f, 0.0f, 0.0f};

    for (size_t t = 0u; t < triCount; ++t) {
        const uint32_t v0 = m.vertexIndices[m.primitiveIndices[t * 3u + 0u]];
        const uint32_t v1 = m.vertexIndices[m.primitiveIndices[t * 3u + 1u]];
        const uint32_t v2 = m.vertexIndices[m.primitiveIndices[t * 3u + 2u]];
        const Vec3 p0{verts[v0].pos[0], verts[v0].pos[1], verts[v0].pos[2]};
        const Vec3 p1{verts[v1].pos[0], verts[v1].pos[1], verts[v1].pos[2]};
        const Vec3 p2{verts[v2].pos[0], verts[v2].pos[1], verts[v2].pos[2]};
        const Vec3 fn = SafeNorm(Cross(Sub(p1, p0), Sub(p2, p0)), {0.0f, 0.0f, 1.0f});
        triNormals.push_back(fn);
        axisSum = Add(axisSum, fn);
    }

    const float axisLen = Length(axisSum);
    Vec3        axis{0.0f, 0.0f, 1.0f};
    float       cutoff = 1.0f;
    bool        degen  = false;

    if (axisLen <= 1.0e-6f) {
        degen  = true;
        cutoff = 1.0f; // disable backface cone cull safely
    } else {
        axis = Scale(axisSum, 1.0f / axisLen);
        float minDot = 1.0f;
        for (const Vec3& fn : triNormals) {
            minDot = (std::min)(minDot, Dot(fn, axis));
        }
        if (minDot <= 0.05f) {
            // Cone half-angle >= ~87 deg -> unsafe to backface cone-cull.
            degen  = true;
            cutoff = 1.0f;
        } else {
            // cutoff = sin(halfAngle) = sqrt(1 - minDot^2), conservative slight pad +0.01
            const float sinHalf = std::sqrt((std::max)(0.0f, 1.0f - minDot * minDot));
            cutoff = (std::min)(sinHalf + 0.01f, 1.0f);
        }
    }

    m.coneAxis[0]    = axis.x;
    m.coneAxis[1]    = axis.y;
    m.coneAxis[2]    = axis.z;
    m.coneCutoff     = cutoff;
    m.degenerateCone = degen;

    m.gpuDesc.boundSphere[0]       = c.x;
    m.gpuDesc.boundSphere[1]       = c.y;
    m.gpuDesc.boundSphere[2]       = c.z;
    m.gpuDesc.boundSphere[3]       = radius;
    m.gpuDesc.packedConeAxisCutoff = PackNormalConeSnorm8(axis.x, axis.y, axis.z, cutoff);
    const uint32_t vCnt            = static_cast<uint32_t>(m.vertexIndices.size());
    const uint32_t tCnt            = static_cast<uint32_t>(triCount);
    m.gpuDesc.triVertCounts        = (vCnt & 0xFFFFu) | ((tCnt & 0xFFFFu) << 16u);
}

// Greedy adjacency-first spatial clusterizer strictly respecting <= maxVerts and <= maxTris.
std::vector<BuiltMeshlet> ClusterizeTriangles(
    const std::vector<RepairedVertex>& verts,
    const std::vector<uint32_t>&       indices,
    uint32_t                           maxVerts,
    uint32_t                           maxTris,
    uint32_t                           lodLevel,
    uint32_t                           psoBucketId)
{
    std::vector<BuiltMeshlet> out;
    const size_t triCount = indices.size() / 3u;
    if (triCount == 0u || verts.empty()) {
        return out;
    }

    // Build vertex -> incident triangles adjacency list.
    std::vector<std::vector<uint32_t>> vertToTris(verts.size());
    std::vector<Vec3>                  triCentroids(triCount);
    for (uint32_t t = 0u; t < static_cast<uint32_t>(triCount); ++t) {
        const uint32_t i0 = indices[t * 3u + 0u];
        const uint32_t i1 = indices[t * 3u + 1u];
        const uint32_t i2 = indices[t * 3u + 2u];
        vertToTris[i0].push_back(t);
        vertToTris[i1].push_back(t);
        vertToTris[i2].push_back(t);
        triCentroids[t] = {
            (verts[i0].pos[0] + verts[i1].pos[0] + verts[i2].pos[0]) / 3.0f,
            (verts[i0].pos[1] + verts[i1].pos[1] + verts[i2].pos[1]) / 3.0f,
            (verts[i0].pos[2] + verts[i1].pos[2] + verts[i2].pos[2]) / 3.0f
        };
    }

    std::vector<uint8_t> triUsed(triCount, 0u);
    std::vector<int32_t> vertLocalMap(verts.size(), -1);
    std::vector<uint32_t> frontier;
    frontier.reserve(256u);

    uint32_t seedCursor = 0u;
    uint32_t emittedTris = 0u;

    while (emittedTris < static_cast<uint32_t>(triCount)) {
        while (seedCursor < static_cast<uint32_t>(triCount) && triUsed[seedCursor]) {
            ++seedCursor;
        }
        if (seedCursor >= static_cast<uint32_t>(triCount)) {
            break;
        }

        BuiltMeshlet m{};
        m.lodLevel            = lodLevel;
        m.gpuDesc.psoBucketId = psoBucketId;
        m.vertexIndices.reserve(maxVerts);
        m.primitiveIndices.reserve(maxTris * 3u);
        frontier.clear();

        auto addTriangle = [&](uint32_t t) -> bool {
            const uint32_t triV[3] = {indices[t * 3u + 0u], indices[t * 3u + 1u], indices[t * 3u + 2u]};
            uint32_t newVerts = 0u;
            for (uint32_t k = 0u; k < 3u; ++k) {
                const uint32_t v = triV[k];
                bool dupInTri = false;
                for (uint32_t j = 0u; j < k; ++j) {
                    if (triV[j] == v) { dupInTri = true; break; }
                }
                if (!dupInTri && vertLocalMap[v] < 0) {
                    ++newVerts;
                }
            }
            if ((m.vertexIndices.size() + newVerts) > maxVerts ||
                (m.primitiveIndices.size() / 3u + 1u) > maxTris) {
                return false;
            }
            for (uint32_t v : triV) {
                if (vertLocalMap[v] < 0) {
                    vertLocalMap[v] = static_cast<int32_t>(m.vertexIndices.size());
                    m.vertexIndices.push_back(v);
                    for (uint32_t adjT : vertToTris[v]) {
                        if (!triUsed[adjT]) {
                            frontier.push_back(adjT);
                        }
                    }
                }
                m.primitiveIndices.push_back(static_cast<uint8_t>(vertLocalMap[v]));
            }
            triUsed[t] = 1u;
            ++emittedTris;
            return true;
        };

        // Start cluster with seed triangle.
        addTriangle(seedCursor);
        Vec3 clusterCenter = triCentroids[seedCursor];
        uint32_t clusterTris = 1u;

        // Grow greedily by maximizing shared vertices and minimizing distance to cluster centroid.
        while ((m.primitiveIndices.size() / 3u) < maxTris && !frontier.empty()) {
            uint32_t bestTri   = UINT32_MAX;
            float    bestScore = -1.0e30f;

            // Compact dead frontier entries lazily.
            size_t writePos = 0u;
            for (size_t fi = 0u; fi < frontier.size(); ++fi) {
                const uint32_t cand = frontier[fi];
                if (triUsed[cand]) {
                    continue;
                }
                frontier[writePos++] = cand;
                const uint32_t v0 = indices[cand * 3u + 0u];
                const uint32_t v1 = indices[cand * 3u + 1u];
                const uint32_t v2 = indices[cand * 3u + 2u];
                const uint32_t shared = (vertLocalMap[v0] >= 0 ? 1u : 0u) +
                                        (vertLocalMap[v1] >= 0 ? 1u : 0u) +
                                        (vertLocalMap[v2] >= 0 ? 1u : 0u);
                const uint32_t added  = 3u - shared;
                if (m.vertexIndices.size() + added > maxVerts) {
                    continue;
                }
                const Vec3  d     = Sub(triCentroids[cand], clusterCenter);
                const float dist  = Length(d);
                const float score = static_cast<float>(shared) * 10.0f - dist;
                if (score > bestScore) {
                    bestScore = score;
                    bestTri   = cand;
                }
            }
            frontier.resize(writePos);

            if (bestTri == UINT32_MAX) {
                break;
            }
            if (!addTriangle(bestTri)) {
                break;
            }
            const float w = 1.0f / static_cast<float>(++clusterTris);
            clusterCenter = Add(Scale(clusterCenter, 1.0f - w), Scale(triCentroids[bestTri], w));
        }

        for (uint32_t vi : m.vertexIndices) {
            vertLocalMap[vi] = -1;
        }

        ComputeMeshletBoundsAndCone(verts, m);
        out.push_back(std::move(m));
    }

    return out;
}

// Simplifies an indexed mesh by ~50% using Garland-Heckbert QEM edge collapse while
// locking open boundary vertices to preserve watertight cluster boundaries.
// Returns the maximum geometric QEM collapse error incurred in world units.
float SimplifyMeshQemHalf(
    const std::vector<RepairedVertex>& srcVerts,
    const std::vector<uint32_t>&       srcIndices,
    std::vector<RepairedVertex>*       outVerts,
    std::vector<uint32_t>*             outIndices)
{
    std::vector<RepairedVertex> verts = srcVerts;
    std::vector<uint32_t>       inds  = srcIndices;
    const size_t                triCount = inds.size() / 3u;
    if (triCount <= 8u) {
        *outVerts   = std::move(verts);
        *outIndices = std::move(inds);
        return 0.0f;
    }

    // 1. Identify boundary vertices (edges incident to 1 triangle) and lock them.
    std::unordered_map<uint64_t, uint32_t> edgeUse;
    edgeUse.reserve(inds.size());
    for (size_t t = 0u; t < triCount; ++t) {
        const uint32_t i0 = inds[t * 3u + 0u];
        const uint32_t i1 = inds[t * 3u + 1u];
        const uint32_t i2 = inds[t * 3u + 2u];
        ++edgeUse[EdgeKey(i0, i1)];
        ++edgeUse[EdgeKey(i1, i2)];
        ++edgeUse[EdgeKey(i2, i0)];
    }
    std::vector<uint8_t> locked(verts.size(), 0u);
    for (const auto& kv : edgeUse) {
        if (kv.second == 1u) {
            const uint32_t a = static_cast<uint32_t>(kv.first >> 32u);
            const uint32_t b = static_cast<uint32_t>(kv.first & 0xFFFFFFFFu);
            locked[a] = 1u;
            locked[b] = 1u;
        }
    }

    // 2. Accumulate plane quadrics per vertex.
    std::vector<Quadric> quadrics(verts.size());
    for (size_t t = 0u; t < triCount; ++t) {
        const uint32_t i0 = inds[t * 3u + 0u];
        const uint32_t i1 = inds[t * 3u + 1u];
        const uint32_t i2 = inds[t * 3u + 2u];
        const Vec3 p0{verts[i0].pos[0], verts[i0].pos[1], verts[i0].pos[2]};
        const Vec3 p1{verts[i1].pos[0], verts[i1].pos[1], verts[i1].pos[2]};
        const Vec3 p2{verts[i2].pos[0], verts[i2].pos[1], verts[i2].pos[2]};
        const Vec3 n = SafeNorm(Cross(Sub(p1, p0), Sub(p2, p0)), {0.0f, 0.0f, 1.0f});
        const double d = -(static_cast<double>(n.x) * p0.x +
                           static_cast<double>(n.y) * p0.y +
                           static_cast<double>(n.z) * p0.z);
        quadrics[i0].AddPlane(n.x, n.y, n.z, d);
        quadrics[i1].AddPlane(n.x, n.y, n.z, d);
        quadrics[i2].AddPlane(n.x, n.y, n.z, d);
    }

    // 3. Score unique interior edges by QEM collapse cost (midpoint target).
    struct EdgeCandidate
    {
        uint32_t u = 0u;
        uint32_t v = 0u;
        double   cost = 0.0;
    };
    std::vector<EdgeCandidate> edges;
    edges.reserve(edgeUse.size());
    for (const auto& kv : edgeUse) {
        const uint32_t u = static_cast<uint32_t>(kv.first >> 32u);
        const uint32_t v = static_cast<uint32_t>(kv.first & 0xFFFFFFFFu);
        if (locked[u] && locked[v]) {
            continue;
        }
        const Quadric qSum = quadrics[u] + quadrics[v];
        const double  mx   = locked[u] ? verts[u].pos[0] : (locked[v] ? verts[v].pos[0] : 0.5 * (verts[u].pos[0] + verts[v].pos[0]));
        const double  my   = locked[u] ? verts[u].pos[1] : (locked[v] ? verts[v].pos[1] : 0.5 * (verts[u].pos[1] + verts[v].pos[1]));
        const double  mz   = locked[u] ? verts[u].pos[2] : (locked[v] ? verts[v].pos[2] : 0.5 * (verts[u].pos[2] + verts[v].pos[2]));
        const double  dx   = static_cast<double>(verts[u].pos[0]) - verts[v].pos[0];
        const double  dy   = static_cast<double>(verts[u].pos[1]) - verts[v].pos[1];
        const double  dz   = static_cast<double>(verts[u].pos[2]) - verts[v].pos[2];
        const double  eLenSq = dx * dx + dy * dy + dz * dz;
        const double  cost = qSum.Evaluate(mx, my, mz) + 1.0e-4 * eLenSq;
        edges.push_back(EdgeCandidate{u, v, cost});
    }

    std::sort(edges.begin(), edges.end(), [](const EdgeCandidate& a, const EdgeCandidate& b) {
        if (a.cost != b.cost) return a.cost < b.cost;
        if (a.u != b.u) return a.u < b.u;
        return a.v < b.v;
    });

    // 4. Greedy independent vertex-disjoint collapse pass until ~50% triangles remain.
    std::vector<uint32_t> parent(verts.size());
    for (uint32_t i = 0u; i < static_cast<uint32_t>(verts.size()); ++i) {
        parent[i] = i;
    }
    auto findRep = [&](uint32_t x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    };

    const size_t targetCollapses = (std::max)(size_t{1u}, triCount / 4u);
    size_t       collapsesDone   = 0u;
    double       maxCollapseCost = 0.0;
    std::vector<uint8_t> collapsedThisPass(verts.size(), 0u);

    for (const auto& ec : edges) {
        if (collapsesDone >= targetCollapses) {
            break;
        }
        const uint32_t ru = findRep(ec.u);
        const uint32_t rv = findRep(ec.v);
        if (ru == rv || collapsedThisPass[ru] || collapsedThisPass[rv]) {
            continue;
        }
        if (locked[ru] && locked[rv]) {
            continue;
        }

        const uint32_t keep = locked[rv] ? rv : ru;
        const uint32_t drop = (keep == ru) ? rv : ru;
        if (!locked[keep]) {
            for (int k = 0; k < 3; ++k) {
                verts[keep].pos[k] = 0.5f * (verts[ru].pos[k] + verts[rv].pos[k]);
            }
            const Vec3 n = SafeNorm({verts[ru].normal[0] + verts[rv].normal[0],
                                     verts[ru].normal[1] + verts[rv].normal[1],
                                     verts[ru].normal[2] + verts[rv].normal[2]},
                                    {0.0f, 0.0f, 1.0f});
            verts[keep].normal[0] = n.x;
            verts[keep].normal[1] = n.y;
            verts[keep].normal[2] = n.z;
            verts[keep].uv[0]     = 0.5f * (verts[ru].uv[0] + verts[rv].uv[0]);
            verts[keep].uv[1]     = 0.5f * (verts[ru].uv[1] + verts[rv].uv[1]);
        }

        parent[drop]             = keep;
        quadrics[keep]           = quadrics[ru] + quadrics[rv];
        collapsedThisPass[ru]    = 1u;
        collapsedThisPass[rv]    = 1u;
        maxCollapseCost          = (std::max)(maxCollapseCost, ec.cost);
        ++collapsesDone;
    }

    // 5. Rebuild compacted triangle list (drop collapsed degenerate faces).
    std::vector<uint32_t> newInds;
    newInds.reserve(inds.size());
    for (size_t t = 0u; t < triCount; ++t) {
        const uint32_t i0 = findRep(inds[t * 3u + 0u]);
        const uint32_t i1 = findRep(inds[t * 3u + 1u]);
        const uint32_t i2 = findRep(inds[t * 3u + 2u]);
        if (i0 == i1 || i1 == i2 || i2 == i0) {
            continue;
        }
        newInds.push_back(i0);
        newInds.push_back(i1);
        newInds.push_back(i2);
    }

    *outVerts   = std::move(verts);
    *outIndices = std::move(newInds);
    return static_cast<float>(std::sqrt((std::max)(maxCollapseCost, 1.0e-8)));
}

} // namespace

uint32_t PackNormalConeSnorm8(float ax, float ay, float az, float cutoff) noexcept
{
    const uint32_t bx = static_cast<uint8_t>(FloatToSnorm8(ax));
    const uint32_t by = static_cast<uint8_t>(FloatToSnorm8(ay));
    const uint32_t bz = static_cast<uint8_t>(FloatToSnorm8(az));
    const uint32_t bc = static_cast<uint8_t>(FloatToSnorm8(cutoff));
    return bx | (by << 8u) | (bz << 16u) | (bc << 24u);
}

void UnpackNormalConeSnorm8(uint32_t packed, float outAxis[3], float* outCutoff) noexcept
{
    const int8_t sx = static_cast<int8_t>(packed & 0xFFu);
    const int8_t sy = static_cast<int8_t>((packed >> 8u) & 0xFFu);
    const int8_t sz = static_cast<int8_t>((packed >> 16u) & 0xFFu);
    const int8_t sc = static_cast<int8_t>((packed >> 24u) & 0xFFu);
    if (outAxis) {
        outAxis[0] = static_cast<float>(sx) / 127.0f;
        outAxis[1] = static_cast<float>(sy) / 127.0f;
        outAxis[2] = static_cast<float>(sz) / 127.0f;
    }
    if (outCutoff) {
        *outCutoff = static_cast<float>(sc) / 127.0f;
    }
}

MeshletLodDag BuildWatertightMeshletLodDag(
    const std::vector<RepairedVertex>& verts,
    const std::vector<uint32_t>&       indices,
    const MeshletBuildConfig&          cfg)
{
    MeshletLodDag dag{};
    if (verts.empty() || indices.size() < 3u) {
        return dag;
    }

    const uint32_t maxV = (std::clamp)(cfg.maxVertices, 3u, kMaxMeshletVertices);
    const uint32_t maxT = (std::clamp)(cfg.maxTriangles, 1u, kMaxMeshletTriangles);
    const uint32_t maxL = (std::clamp)(cfg.maxLodLevels, 1u, 6u);

    std::vector<RepairedVertex> curVerts = verts;
    std::vector<uint32_t>       curInds  = indices;

    std::vector<std::vector<BuiltMeshlet>> levels;
    std::vector<float>                     levelErrors;

    float cumulativeError = 0.0f;
    for (uint32_t lod = 0u; lod < maxL; ++lod) {
        auto lvlMeshlets = ClusterizeTriangles(curVerts, curInds, maxV, maxT, lod, cfg.psoBucketId);
        if (lvlMeshlets.empty()) {
            break;
        }
        levels.push_back(std::move(lvlMeshlets));
        levelErrors.push_back(cumulativeError);

        if (levels.back().size() <= 1u || (curInds.size() / 3u) <= 12u || (lod + 1u) >= maxL) {
            break;
        }

        std::vector<RepairedVertex> nextVerts;
        std::vector<uint32_t>       nextInds;
        const float                 stepErr = SimplifyMeshQemHalf(curVerts, curInds, &nextVerts, &nextInds);
        if (nextInds.empty() || nextInds.size() >= curInds.size()) {
            break;
        }

        // Strict monotonic increase: parentError >= childError + collapseError.
        const float minStep = (std::max)(stepErr, 0.015f * static_cast<float>(lod + 1u));
        cumulativeError += minStep;
        curVerts.swap(nextVerts);
        curInds.swap(nextInds);
    }

    if (levels.empty()) {
        return dag;
    }

    dag.lodLevels        = static_cast<uint32_t>(levels.size());
    dag.lod0MeshletCount = static_cast<uint32_t>(levels[0].size());

    // Wire parent-child group links & strictly monotonic error intervals across LOD levels.
    uint32_t runningOffset = 0u;
    double   totalVerts    = 0.0;
    double   totalTris     = 0.0;

    for (size_t l = 0u; l < levels.size(); ++l) {
        const float childErr = levelErrors[l];
        const float parentErr = ((l + 1u) < levels.size())
            ? (cfg.enforceMonotonicParentError
                   ? (std::max)(levelErrors[l + 1u], childErr + 1.0e-3f)
                   : levelErrors[l + 1u])
            : 1.0e9f;

        for (size_t i = 0u; i < levels[l].size(); ++i) {
            BuiltMeshlet& m          = levels[l][i];
            m.groupId                = static_cast<uint32_t>(i / 4u);
            m.parentGroupId          = ((l + 1u) < levels.size()) ? static_cast<uint32_t>(i / 8u) : 0xFFFFFFFFu;
            m.gpuDesc.lodErrorWorld  = childErr;
            m.gpuDesc.parentErrorWorld = parentErr;
            m.gpuDesc.meshletOffset  = runningOffset++;

            if (l == 0u) {
                totalVerts += static_cast<double>(m.vertexIndices.size());
                totalTris  += static_cast<double>(m.primitiveIndices.size() / 3u);
            }

            dag.gpuDescriptors.push_back(m.gpuDesc);
            dag.meshlets.push_back(std::move(m));
        }
    }

    dag.avgVertsPerTri = (totalTris > 0.0) ? static_cast<float>(totalVerts / totalTris) : 0.0f;
    dag.monotonicValid = ValidateMeshletLodDag(dag, nullptr);
    return dag;
}

bool ValidateMeshletLodDag(const MeshletLodDag& dag, std::string* outErr)
{
    auto fail = [&](const std::string& msg) -> bool {
        if (outErr) *outErr = msg;
        return false;
    };

    if (dag.meshlets.empty() || dag.meshlets.size() != dag.gpuDescriptors.size()) {
        return fail("FAIL: MESHLET_DAG_EMPTY_OR_DESYNC");
    }

    for (size_t i = 0u; i < dag.meshlets.size(); ++i) {
        const auto& m = dag.meshlets[i];
        const auto& g = dag.gpuDescriptors[i];

        const uint32_t vCnt = static_cast<uint32_t>(m.vertexIndices.size());
        const uint32_t tCnt = static_cast<uint32_t>(m.primitiveIndices.size() / 3u);
        if (vCnt == 0u || vCnt > kMaxMeshletVertices || tCnt == 0u || tCnt > kMaxMeshletTriangles) {
            return fail("FAIL: MESHLET_LIMIT_OR_CONE (meshlet " + std::to_string(i) +
                        " has " + std::to_string(vCnt) + "v/" + std::to_string(tCnt) + "t)");
        }
        const uint32_t packedV = g.triVertCounts & 0xFFFFu;
        const uint32_t packedT = (g.triVertCounts >> 16u) & 0xFFFFu;
        if (packedV == 0u || packedV > kMaxMeshletVertices || packedT == 0u || packedT > kMaxMeshletTriangles) {
            return fail("FAIL: MESHLET_LIMIT_OR_CONE (packed triVertCounts exceeds 64v/64t)");
        }
        if (!std::isfinite(g.boundSphere[0]) || !std::isfinite(g.boundSphere[1]) ||
            !std::isfinite(g.boundSphere[2]) || !std::isfinite(g.boundSphere[3]) ||
            g.boundSphere[3] <= 0.0f) {
            return fail("FAIL: MESHLET_INVALID_BOUND_SPHERE");
        }
        if (!std::isfinite(m.coneCutoff) || m.coneCutoff < -1.0f || m.coneCutoff > 1.0f) {
            return fail("FAIL: MESHLET_LIMIT_OR_CONE (invalid cone cutoff)");
        }
        const float axisLen = std::sqrt(m.coneAxis[0] * m.coneAxis[0] +
                                        m.coneAxis[1] * m.coneAxis[1] +
                                        m.coneAxis[2] * m.coneAxis[2]);
        if (!std::isfinite(axisLen) || axisLen < 0.9f || axisLen > 1.05f) {
            return fail("FAIL: MESHLET_LIMIT_OR_CONE (non-unit cone axis)");
        }
        if (!std::isfinite(g.lodErrorWorld) || !std::isfinite(g.parentErrorWorld) ||
            g.lodErrorWorld < 0.0f || g.parentErrorWorld < g.lodErrorWorld) {
            return fail("FAIL: LOD_CUT_NON_MONOTONIC (lodErrorWorld > parentErrorWorld)");
        }
    }

    if (!raw::bridge::wave83::ValidateMonotonicLodDag(dag.gpuDescriptors)) {
        return fail("FAIL: LOD_CUT_NON_MONOTONIC (Wave 83 GpuMeshletDesc check failed)");
    }

    return true;
}

} // namespace raw::ce
