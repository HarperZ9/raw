// RawMeshPipeline.cpp: Reusable GPU-Ready Procedural Mesh, 64v/126t Meshletizer & .obj/.glb Exporters.
// Original work, MIT.
#include "RawMeshPipeline.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <unordered_map>

namespace raw::worldgen {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kGoldenAngle = 2.39996322972865332223f;

struct MeshRng {
    uint32_t s = 0x7A4D3E19u;
    explicit MeshRng(uint32_t seed) : s(seed ? seed : 0x7A4D3E19u) {}
    uint32_t NextU32() {
        uint32_t x = s;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        s = x;
        return x;
    }
    float Next01() { return float(NextU32() & 0x00FFFFFFu) * (1.0f / 16777216.0f); }
    float Range(float lo, float hi) { return lo + (hi - lo) * Next01(); }
};

TerrainVertex MakeVertex(Vec3 p, Vec3 n, Vec2 uv, TerrainMaterialId mat,
                         float curv = 0.0f, float wet = 0.0f, float emis = 0.0f, uint8_t biome = 0u) {
    TerrainVertex v{};
    v.pos[0] = p.x; v.pos[1] = p.y; v.pos[2] = p.z;
    const Vec3 nn = Normalize(n);
    v.normal[0] = nn.x; v.normal[1] = nn.y; v.normal[2] = nn.z;
    v.uv[0] = uv.x; v.uv[1] = uv.y;
    v.splat0[0] = v.splat0[1] = v.splat0[2] = v.splat0[3] = 0.0f;
    v.splat1[0] = v.splat1[1] = v.splat1[2] = v.splat1[3] = 0.0f;
    const uint32_t m = std::min<uint32_t>(static_cast<uint32_t>(mat), 7u);
    if (m < 4u) v.splat0[m] = 1.0f;
    else        v.splat1[m - 4u] = 1.0f;
    v.meta[0] = curv;
    v.meta[1] = wet;
    v.meta[2] = emis;
    v.meta[3] = float(biome);
    return v;
}

void RecomputeSmoothNormals(std::vector<TerrainVertex>& verts, const std::vector<uint32_t>& idx) {
    std::vector<Vec3> acc(verts.size());
    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
        const uint32_t i0 = idx[i], i1 = idx[i + 1], i2 = idx[i + 2];
        if (i0 >= verts.size() || i1 >= verts.size() || i2 >= verts.size()) continue;
        const Vec3 p0{verts[i0].pos[0], verts[i0].pos[1], verts[i0].pos[2]};
        const Vec3 p1{verts[i1].pos[0], verts[i1].pos[1], verts[i1].pos[2]};
        const Vec3 p2{verts[i2].pos[0], verts[i2].pos[1], verts[i2].pos[2]};
        const Vec3 fn = Cross(p1 - p0, p2 - p0);
        acc[i0] = acc[i0] + fn;
        acc[i1] = acc[i1] + fn;
        acc[i2] = acc[i2] + fn;
    }
    for (size_t i = 0; i < verts.size(); ++i) {
        const Vec3 n = Normalize(acc[i]);
        verts[i].normal[0] = n.x;
        verts[i].normal[1] = n.y;
        verts[i].normal[2] = n.z;
    }
}

void FinalizeProceduralMesh(const std::string& name, TerrainMaterialId mat, uint32_t lod,
                            const std::vector<TerrainVertex>& srcV, const std::vector<uint32_t>& srcI,
                            uint32_t maxMeshletTris, ProceduralMesh* outMesh) {
    outMesh->name = name;
    outMesh->primaryMaterial = mat;
    outMesh->lodLevel = lod;
    Vec3 mn{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Vec3 mx{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    for (const auto& v : srcV) {
        mn.x = std::min(mn.x, v.pos[0]); mn.y = std::min(mn.y, v.pos[1]); mn.z = std::min(mn.z, v.pos[2]);
        mx.x = std::max(mx.x, v.pos[0]); mx.y = std::max(mx.y, v.pos[1]); mx.z = std::max(mx.z, v.pos[2]);
    }
    outMesh->aabbMin = srcV.empty() ? Vec3{} : mn;
    outMesh->aabbMax = srcV.empty() ? Vec3{} : mx;
    const uint32_t clampedTris = std::clamp(maxMeshletTris, 16u, 126u);
    BuildMeshlets(srcV, srcI, 64u, clampedTris, &outMesh->vertices, &outMesh->indices, &outMesh->meshlets);
}

void AppendHexPrism(std::vector<TerrainVertex>& verts, std::vector<uint32_t>& idx,
                    Vec2 centerXY, float radius, float baseZ, float topZ, float bevelH, float yaw,
                    TerrainMaterialId mat, float emis) {
    constexpr int kSides = 6;
    const float h = std::max(topZ - baseZ, 4.0f);
    const float bev = std::min(bevelH, h * 0.25f);
    const float topInnerR = radius * 0.84f;

    // 6 side faces (hard-faceted hexagonal prism walls with sharp beveled normals!)
    for (int s = 0; s < kSides; ++s) {
        const float a0 = yaw + (float(s) / float(kSides)) * kTwoPi;
        const float a1 = yaw + (float(s + 1) / float(kSides)) * kTwoPi;
        const float am = 0.5f * (a0 + a1);
        const Vec3 sideN = Normalize({std::cos(am), std::sin(am), 0.0f});
        const Vec3 bevN = Normalize({std::cos(am) * 0.55f, std::sin(am) * 0.55f, 0.835f});

        const Vec3 b0{centerXY.x + std::cos(a0) * radius, centerXY.y + std::sin(a0) * radius, baseZ};
        const Vec3 b1{centerXY.x + std::cos(a1) * radius, centerXY.y + std::sin(a1) * radius, baseZ};
        const Vec3 m0{centerXY.x + std::cos(a0) * radius, centerXY.y + std::sin(a0) * radius, topZ - bev};
        const Vec3 m1{centerXY.x + std::cos(a1) * radius, centerXY.y + std::sin(a1) * radius, topZ - bev};
        const Vec3 t0{centerXY.x + std::cos(a0) * topInnerR, centerXY.y + std::sin(a0) * topInnerR, topZ};
        const Vec3 t1{centerXY.x + std::cos(a1) * topInnerR, centerXY.y + std::sin(a1) * topInnerR, topZ};

        const uint32_t vBase = static_cast<uint32_t>(verts.size());
        verts.push_back(MakeVertex(b0, sideN, {0.0f, 0.0f}, mat, 0.0f, 0.0f, emis, 3u));
        verts.push_back(MakeVertex(b1, sideN, {1.0f, 0.0f}, mat, 0.0f, 0.0f, emis, 3u));
        verts.push_back(MakeVertex(m1, sideN, {1.0f, 0.85f}, mat, 0.15f, 0.0f, emis, 3u));
        verts.push_back(MakeVertex(m0, sideN, {0.0f, 0.85f}, mat, 0.15f, 0.0f, emis, 3u));
        idx.push_back(vBase + 0); idx.push_back(vBase + 1); idx.push_back(vBase + 2);
        idx.push_back(vBase + 0); idx.push_back(vBase + 2); idx.push_back(vBase + 3);

        // Beveled crown collar
        const uint32_t cBase = static_cast<uint32_t>(verts.size());
        verts.push_back(MakeVertex(m0, bevN, {0.0f, 0.85f}, mat, 0.45f, 0.0f, emis, 3u));
        verts.push_back(MakeVertex(m1, bevN, {1.0f, 0.85f}, mat, 0.45f, 0.0f, emis, 3u));
        verts.push_back(MakeVertex(t1, bevN, {0.9f, 1.0f}, mat, 0.45f, 0.0f, emis, 3u));
        verts.push_back(MakeVertex(t0, bevN, {0.1f, 1.0f}, mat, 0.45f, 0.0f, emis, 3u));
        idx.push_back(cBase + 0); idx.push_back(cBase + 1); idx.push_back(cBase + 2);
        idx.push_back(cBase + 0); idx.push_back(cBase + 2); idx.push_back(cBase + 3);
    }

    // Flat hexagonal cap fan
    const uint32_t capCenter = static_cast<uint32_t>(verts.size());
    verts.push_back(MakeVertex({centerXY.x, centerXY.y, topZ}, {0, 0, 1}, {0.5f, 0.5f}, mat, 0.10f, 0.0f, emis, 3u));
    for (int s = 0; s < kSides; ++s) {
        const float a = yaw + (float(s) / float(kSides)) * kTwoPi;
        verts.push_back(MakeVertex({centerXY.x + std::cos(a) * topInnerR, centerXY.y + std::sin(a) * topInnerR, topZ},
                                   {0, 0, 1}, {0.5f + 0.45f * std::cos(a), 0.5f + 0.45f * std::sin(a)}, mat, 0.10f, 0.0f, emis, 3u));
    }
    for (int s = 0; s < kSides; ++s) {
        idx.push_back(capCenter);
        idx.push_back(capCenter + 1u + uint32_t(s));
        idx.push_back(capCenter + 1u + uint32_t((s + 1) % kSides));
    }
}

void AppendConiferBough(std::vector<TerrainVertex>& verts, std::vector<uint32_t>& idx,
                        Vec3 root, Vec3 dir, float lengthU, float widthU, float droopU,
                        float flexBase, float snowLoad) {
    const Vec3 fwd = Normalize({dir.x, dir.y, 0.0f});
    const Vec3 right{-fwd.y, fwd.x, 0.0f};
    const uint32_t base = static_cast<uint32_t>(verts.size());
    constexpr int kRings = 4;
    for (int r = 0; r < kRings; ++r) {
        const float t = float(r) / float(kRings - 1);
        const float taper = std::sin(t * kPi * 0.86f + 0.14f) * (1.0f - 0.54f * t);
        const float hw = widthU * (0.42f + 0.78f * taper);
        const float lift = widthU * 0.26f * (1.0f - 0.62f * t);
        const float sag = -droopU * t * t;
        const Vec3 spine = root + fwd * (lengthU * t) + Vec3{0.0f, 0.0f, dir.z * lengthU * t + sag};
        const float uvY = Clamp01(flexBase * (1.0f - 0.68f * t));

        const Vec3 pL = spine - right * hw;
        const Vec3 pC = spine + Vec3{0.0f, 0.0f, lift};
        const Vec3 pR = spine + right * hw;
        const Vec3 nL = Normalize(right * -0.55f + Vec3{0.0f, 0.0f, 0.83f});
        const Vec3 nC = Normalize(fwd * 0.24f + Vec3{0.0f, 0.0f, 0.96f});
        const Vec3 nR = Normalize(right * 0.55f + Vec3{0.0f, 0.0f, 0.83f});

        auto makeNeedleVert = [&](Vec3 p, Vec3 n, float u) {
            TerrainVertex v = MakeVertex(p, n, {u, uvY}, TerrainMaterialId::MossMeadow, 0.15f, 0.0f, 0.0f, 11u);
            v.splat0[1] = Clamp01(snowLoad * 0.72f * std::max(n.z, 0.0f));
            v.splat1[1] = 1.0f - v.splat0[1];
            return v;
        };
        verts.push_back(makeNeedleVert(pL, nL, 0.0f));
        verts.push_back(makeNeedleVert(pC, nC, 0.5f));
        verts.push_back(makeNeedleVert(pR, nR, 1.0f));
    }
    for (int r = 0; r < kRings - 1; ++r) {
        const uint32_t a = base + uint32_t(r * 3);
        const uint32_t b = base + uint32_t((r + 1) * 3);
        idx.push_back(a + 0); idx.push_back(a + 1); idx.push_back(b + 1);
        idx.push_back(a + 0); idx.push_back(b + 1); idx.push_back(b + 0);
        idx.push_back(a + 1); idx.push_back(a + 2); idx.push_back(b + 2);
        idx.push_back(a + 1); idx.push_back(b + 2); idx.push_back(b + 1);
    }
}

void AppendU32(std::vector<uint8_t>& buf, uint32_t v) {
    const size_t p = buf.size();
    buf.resize(p + 4u);
    std::memcpy(buf.data() + p, &v, 4u);
}

void AppendF32(std::vector<uint8_t>& buf, float v) {
    const size_t p = buf.size();
    buf.resize(p + 4u);
    std::memcpy(buf.data() + p, &v, 4u);
}

}  // namespace

void BuildMeshlets(const std::vector<TerrainVertex>& srcVerts, const std::vector<uint32_t>& srcIndices,
                   uint32_t maxVerts, uint32_t maxTris,
                   std::vector<TerrainVertex>* outVerts, std::vector<uint32_t>* outIndices,
                   std::vector<MeshletDesc>* outMeshlets) {
    if (!outVerts || !outIndices || !outMeshlets) return;
    outVerts->clear();
    outIndices->clear();
    outMeshlets->clear();

    const uint32_t limV = std::clamp(maxVerts, 3u, 64u);
    const uint32_t limT = std::clamp(maxTris, 1u, 126u);
    const size_t triTotal = srcIndices.size() / 3u;
    if (triTotal == 0u || srcVerts.empty()) return;

    std::unordered_map<uint32_t, uint32_t> remap;
    remap.reserve(limV * 2u);
    MeshletDesc cur{};
    Vec3 minP{1e30f, 1e30f, 1e30f}, maxP{-1e30f, -1e30f, -1e30f}, avgN{0, 0, 0};
    std::vector<Vec3> triNormals;
    triNormals.reserve(limT);

    auto flush = [&]() {
        if (cur.triangleCount == 0u) return;
        const Vec3 c{(minP.x + maxP.x) * 0.5f, (minP.y + maxP.y) * 0.5f, (minP.z + maxP.z) * 0.5f};
        float r2 = 0.0f;
        for (uint32_t i = 0u; i < cur.vertexCount; ++i) {
            const auto& v = (*outVerts)[cur.vertexOffset + i];
            const Vec3 d{v.pos[0] - c.x, v.pos[1] - c.y, v.pos[2] - c.z};
            r2 = std::max(r2, Dot(d, d));
        }
        cur.boundSphere[0] = c.x;
        cur.boundSphere[1] = c.y;
        cur.boundSphere[2] = c.z;
        cur.boundSphere[3] = std::sqrt(r2);

        const float avgLen = Length(avgN);
        if (avgLen > 1.0e-5f) {
            const Vec3 axis = avgN / avgLen;
            float minDot = 1.0f;
            for (const Vec3& tn : triNormals) minDot = std::min(minDot, Dot(axis, tn));
            cur.normalCone[0] = axis.x;
            cur.normalCone[1] = axis.y;
            cur.normalCone[2] = axis.z;
            cur.normalCone[3] = (minDot > 0.0f) ? -std::sqrt(std::max(1.0f - minDot * minDot, 0.0f)) : -1.0f;
        } else {
            cur.normalCone[0] = 0.0f; cur.normalCone[1] = 0.0f; cur.normalCone[2] = 1.0f; cur.normalCone[3] = -1.0f;
        }
        outMeshlets->push_back(cur);
        remap.clear();
        triNormals.clear();
        cur = {};
        cur.vertexOffset = static_cast<uint32_t>(outVerts->size());
        cur.triangleOffset = static_cast<uint32_t>(outIndices->size());
        minP = {1e30f, 1e30f, 1e30f};
        maxP = {-1e30f, -1e30f, -1e30f};
        avgN = {0.0f, 0.0f, 0.0f};
    };

    for (size_t t = 0; t < triTotal; ++t) {
        const uint32_t i0 = srcIndices[t * 3u + 0u];
        const uint32_t i1 = srcIndices[t * 3u + 1u];
        const uint32_t i2 = srcIndices[t * 3u + 2u];
        if (i0 >= srcVerts.size() || i1 >= srcVerts.size() || i2 >= srcVerts.size()) continue;

        const uint32_t addV = (remap.count(i0) ? 0u : 1u) +
                              (remap.count(i1) ? 0u : 1u) +
                              (remap.count(i2) ? 0u : 1u);
        if (cur.vertexCount + addV > limV || cur.triangleCount + 1u > limT) flush();

        auto emitV = [&](uint32_t gi) -> uint32_t {
            if (auto it = remap.find(gi); it != remap.end()) return it->second;
            const uint32_t li = cur.vertexCount++;
            const auto& v = srcVerts[gi];
            outVerts->push_back(v);
            minP.x = std::min(minP.x, v.pos[0]); minP.y = std::min(minP.y, v.pos[1]); minP.z = std::min(minP.z, v.pos[2]);
            maxP.x = std::max(maxP.x, v.pos[0]); maxP.y = std::max(maxP.y, v.pos[1]); maxP.z = std::max(maxP.z, v.pos[2]);
            remap.emplace(gi, li);
            return li;
        };

        const uint32_t l0 = emitV(i0), l1 = emitV(i1), l2 = emitV(i2);
        outIndices->push_back(l0);
        outIndices->push_back(l1);
        outIndices->push_back(l2);

        const Vec3 p0{srcVerts[i0].pos[0], srcVerts[i0].pos[1], srcVerts[i0].pos[2]};
        const Vec3 p1{srcVerts[i1].pos[0], srcVerts[i1].pos[1], srcVerts[i1].pos[2]};
        const Vec3 p2{srcVerts[i2].pos[0], srcVerts[i2].pos[1], srcVerts[i2].pos[2]};
        const Vec3 tn = Normalize(Cross(p1 - p0, p2 - p0));
        avgN = avgN + tn;
        triNormals.push_back(tn);
        cur.triangleCount++;
    }
    flush();
}

bool BuildTerrainChunkMesh(const TerrainChunkData& chunk, uint32_t maxMeshletTris, ProceduralMesh* outMesh) {
    if (!outMesh || chunk.gridN < 2u || chunk.heights.size() != size_t(chunk.stride) * chunk.stride) return false;

    const uint32_t N = chunk.gridN;
    const uint32_t S = chunk.stride;
    const float step = chunk.chunkSizeU / float(N);

    std::vector<TerrainVertex> srcV(size_t(S) * S);
    for (uint32_t iy = 0; iy < S; ++iy) {
        const float v = float(iy) / float(N);
        const float wy = chunk.originY + float(iy) * step;
        for (uint32_t ix = 0; ix < S; ++ix) {
            const float u = float(ix) / float(N);
            const float wx = chunk.originX + float(ix) * step;
            const size_t vi = size_t(iy) * S + size_t(ix);
            TerrainVertex& tv = srcV[vi];
            tv.pos[0] = wx;
            tv.pos[1] = wy;
            tv.pos[2] = chunk.heights[vi];
            const Vec3 n = Normalize(chunk.normals[vi]);
            tv.normal[0] = n.x; tv.normal[1] = n.y; tv.normal[2] = n.z;
            tv.uv[0] = u;
            tv.uv[1] = v;
            tv.splat0[0] = chunk.splat0[vi].x;
            tv.splat0[1] = chunk.splat0[vi].y;
            tv.splat0[2] = chunk.splat0[vi].z;
            tv.splat0[3] = chunk.splat0[vi].w;
            tv.splat1[0] = chunk.splat1[vi].x;
            tv.splat1[1] = chunk.splat1[vi].y;
            tv.splat1[2] = chunk.splat1[vi].z;
            tv.splat1[3] = chunk.splat1[vi].w;
            tv.meta[0] = chunk.curvatures[vi];
            tv.meta[1] = chunk.wetness[vi];
            tv.meta[2] = chunk.emissiveMask[vi];
            tv.meta[3] = float(chunk.primaryBiome[vi]);
        }
    }

    // Emit triangles in spatially coherent 7x7 quad tiles (64 verts max, 98 tris max per tile!)
    // for ultra-tight AS+MS bounding spheres and minimal vertex duplication across meshlets.
    const uint32_t tileQuads = (maxMeshletTris >= 98u) ? 7u : 5u;
    std::vector<uint32_t> srcI;
    srcI.reserve(size_t(N) * N * 6u);
    for (uint32_t ty = 0; ty < N; ty += tileQuads) {
        const uint32_t yEnd = std::min(N, ty + tileQuads);
        for (uint32_t tx = 0; tx < N; tx += tileQuads) {
            const uint32_t xEnd = std::min(N, tx + tileQuads);
            for (uint32_t y = ty; y < yEnd; ++y) {
                for (uint32_t x = tx; x < xEnd; ++x) {
                    const uint32_t a = y * S + x;
                    const uint32_t b = a + 1u;
                    const uint32_t c = a + S;
                    const uint32_t d = c + 1u;
                    // Alternating diagonal improves isotropic terrain silhouette
                    if (((x + y) & 1u) == 0u) {
                        srcI.push_back(a); srcI.push_back(b); srcI.push_back(d);
                        srcI.push_back(a); srcI.push_back(d); srcI.push_back(c);
                    } else {
                        srcI.push_back(a); srcI.push_back(b); srcI.push_back(c);
                        srcI.push_back(b); srcI.push_back(d); srcI.push_back(c);
                    }
                }
            }
        }
    }

    char nm[96];
    std::snprintf(nm, sizeof nm, "terrain_c%d_%d_lod%u", chunk.chunkX, chunk.chunkY, chunk.lodLevel);
    FinalizeProceduralMesh(nm, TerrainMaterialId::RockGranite, chunk.lodLevel, srcV, srcI, maxMeshletTris, outMesh);
    return !outMesh->meshlets.empty();
}

bool BuildHexagonalBasaltCluster(uint32_t seed, uint32_t columnCount, float radiusU, float maxHeightU,
                                 uint32_t maxMeshletTris, ProceduralMesh* outMesh) {
    if (!outMesh || columnCount == 0u || radiusU < 4.0f || maxHeightU < 8.0f) return false;
    MeshRng rng(seed ^ 0xBA5A1701u);
    const uint32_t count = std::clamp(columnCount, 3u, 19u);

    // Hexagonal close-packed Giant's Causeway lattice offsets
    const float colR = radiusU * 0.34f;
    const float pitch = colR * 1.78f;
    static constexpr int kHexAxial[19][2] = {
        {0,0}, {1,0}, {0,1}, {-1,1}, {-1,0}, {0,-1}, {1,-1},
        {2,0}, {1,1}, {0,2}, {-1,2}, {-2,2}, {-2,1}, {-2,0}, {-1,-1}, {0,-2}, {1,-2}, {2,-1}, {2,-2}
    };

    std::vector<TerrainVertex> verts;
    std::vector<uint32_t> idx;
    const float clusterYaw = rng.Range(0.0f, kTwoPi);
    for (uint32_t i = 0; i < count; ++i) {
        const float q = float(kHexAxial[i][0]);
        const float r = float(kHexAxial[i][1]);
        const float hx = pitch * (q + 0.5f * r);
        const float hy = pitch * (0.8660254f * r);
        const float radialDist = std::hypot(hx, hy) / std::max(radiusU, 1.0f);
        const float stepQuant = std::floor(rng.Range(2.0f, 7.0f)) / 6.0f;
        const float colH = std::max(14.0f, maxHeightU * (1.05f - 0.42f * radialDist) * (0.45f + 0.55f * stepQuant));
        const float bevelH = colR * 0.22f;
        AppendHexPrism(verts, idx, {hx, hy}, colR * rng.Range(0.92f, 1.04f), -12.0f, colH, bevelH,
                       clusterYaw, TerrainMaterialId::BasaltColumn, 0.0f);
    }

    FinalizeProceduralMesh("proc_hex_basalt_cluster", TerrainMaterialId::BasaltColumn, 0u,
                           verts, idx, maxMeshletTris, outMesh);
    return !outMesh->meshlets.empty();
}

bool BuildMultiTierConifer(uint32_t seed, float heightU, float crownRadiusU, float snowLoad01,
                           uint32_t maxMeshletTris, ProceduralMesh* outMesh) {
    if (!outMesh || heightU < 40.0f || crownRadiusU < 10.0f) return false;
    MeshRng rng(seed ^ 0x51C0E11Au);
    const float snow = Clamp01(snowLoad01);

    std::vector<TerrainVertex> verts;
    std::vector<uint32_t> idx;

    // 1. Gnarled buttressed fluted conifer trunk (8 sides x 11 rings, rooted -24u below origin so slopes never float)
    constexpr int kSides = 8;
    constexpr int kRings = 11;
    const float baseRad = Clamp(crownRadiusU * 0.15f, 4.5f, 38.0f);
    const float leanX = rng.Range(-heightU * 0.032f, heightU * 0.032f);
    const float leanY = rng.Range(-heightU * 0.032f, heightU * 0.032f);
    const float sWaveX = rng.Range(-heightU * 0.018f, heightU * 0.018f);
    const float sWaveY = rng.Range(-heightU * 0.018f, heightU * 0.018f);
    const float twistRate = rng.Range(-0.9f, 0.9f);
    for (int r = 0; r < kRings; ++r) {
        const float t = float(r) / float(kRings - 1);
        const float z = Lerp(-24.0f, heightU, t);
        const float hNorm = Clamp01(z / heightU);
        const float buttress = 1.0f + 0.72f * std::exp(-hNorm * 8.5f);
        const float rad = std::max(baseRad * (1.0f - 0.89f * hNorm) * buttress, 1.3f);
        const float cx = leanX * hNorm * hNorm + sWaveX * std::sin(hNorm * kPi);
        const float cy = leanY * hNorm * hNorm + sWaveY * std::sin(hNorm * kPi);
        for (int s = 0; s < kSides; ++s) {
            const float ang = (float(s) / float(kSides)) * kTwoPi + twistRate * hNorm;
            const float flute = 1.0f + 0.16f * std::exp(-hNorm * 6.0f) * std::cos(ang * 5.0f + float(seed & 15u));
            const Vec3 p{cx + std::cos(ang) * rad * flute, cy + std::sin(ang) * rad * flute, z};
            const Vec3 n = Normalize({std::cos(ang), std::sin(ang), 0.14f});
            TerrainVertex tv = MakeVertex(p, n, {float(s) / float(kSides), 1.0f - hNorm},
                                          TerrainMaterialId::RockGranite, -0.10f, 0.0f, 0.0f, 10u);
            // Subtle mossy root-collar blend near base
            tv.splat1[1] = Clamp01((1.0f - hNorm * 3.5f) * 0.65f);
            verts.push_back(tv);
        }
    }
    for (int r = 0; r < kRings - 1; ++r) {
        for (int s = 0; s < kSides; ++s) {
            const int sNext = (s + 1) % kSides;
            const uint32_t a = uint32_t(r * kSides + s);
            const uint32_t b = uint32_t(r * kSides + sNext);
            const uint32_t c = uint32_t((r + 1) * kSides + sNext);
            const uint32_t d = uint32_t((r + 1) * kSides + s);
            idx.push_back(a); idx.push_back(b); idx.push_back(c);
            idx.push_back(a); idx.push_back(c); idx.push_back(d);
        }
    }

    // 1b. 5 Gnarled Flared Surface Roots radiating outward & curving into the ground
    constexpr int kRoots = 5;
    for (int ri = 0; ri < kRoots; ++ri) {
        const float rAng = (float(ri) / float(kRoots)) * kTwoPi + rng.Range(-0.28f, 0.28f);
        const float rLen = baseRad * rng.Range(2.4f, 3.6f);
        const float rThick = baseRad * rng.Range(0.42f, 0.56f);
        const Vec3 dir{std::cos(rAng), std::sin(rAng), 0.0f};
        const Vec3 side{-dir.y, dir.x, 0.0f};
        const uint32_t rBase = static_cast<uint32_t>(verts.size());
        constexpr int kRootSegs = 4;
        for (int rs = 0; rs < kRootSegs; ++rs) {
            const float u = float(rs) / float(kRootSegs - 1);
            const float rad = rThick * (1.0f - 0.65f * u);
            const float zArch = Lerp(baseRad * 0.95f, -20.0f, u * u) + std::sin(u * kPi) * (baseRad * 0.22f);
            const Vec3 spine = dir * (baseRad * 0.6f + rLen * u) + Vec3{0.0f, 0.0f, zArch};
            const Vec3 pL = spine - side * rad + Vec3{0.0f, 0.0f, -rad * 0.45f};
            const Vec3 pT = spine + Vec3{0.0f, 0.0f, rad * 0.65f};
            const Vec3 pR = spine + side * rad + Vec3{0.0f, 0.0f, -rad * 0.45f};
            TerrainVertex vL = MakeVertex(pL, Normalize(side * -0.8f + Vec3{0, 0, 0.5f}), {0.0f, 1.0f}, TerrainMaterialId::RockGranite, 0.1f, 0.0f, 0.0f, 10u);
            TerrainVertex vT = MakeVertex(pT, Normalize(dir * 0.2f + Vec3{0, 0, 0.96f}), {0.5f, 1.0f}, TerrainMaterialId::RockGranite, 0.2f, 0.0f, 0.0f, 10u);
            TerrainVertex vR = MakeVertex(pR, Normalize(side * 0.8f + Vec3{0, 0, 0.5f}), {1.0f, 1.0f}, TerrainMaterialId::RockGranite, 0.1f, 0.0f, 0.0f, 10u);
            vT.splat1[1] = 0.55f;
            verts.push_back(vL); verts.push_back(vT); verts.push_back(vR);
        }
        for (int rs = 0; rs < kRootSegs - 1; ++rs) {
            const uint32_t a = rBase + uint32_t(rs * 3);
            const uint32_t b = rBase + uint32_t((rs + 1) * 3);
            idx.push_back(a + 0); idx.push_back(a + 1); idx.push_back(b + 1);
            idx.push_back(a + 0); idx.push_back(b + 1); idx.push_back(b + 0);
            idx.push_back(a + 1); idx.push_back(a + 2); idx.push_back(b + 2);
            idx.push_back(a + 1); idx.push_back(b + 2); idx.push_back(b + 1);
        }
    }

    // 2. Irregular Multi-Tier Branching Pine Boughs + Lateral 3D Needle Sub-Boughs
    constexpr int kBoughs = 24;
    for (int bi = 0; bi < kBoughs; ++bi) {
        const float u = float(bi) / float(kBoughs - 1);
        const float tH = 0.18f + 0.80f * u;
        const float z = tH * heightU;
        const float cx = leanX * tH * tH + sWaveX * std::sin(tH * kPi);
        const float cy = leanY * tH * tH + sWaveY * std::sin(tH * kPi);
        const float ang = float(bi) * kGoldenAngle + rng.Range(-0.24f, 0.24f);
        // Irregular layered silhouette (not a sterile geometric cone!)
        const float tierWave = 0.84f + 0.28f * std::sin(u * kPi * 5.0f + float(seed & 7u));
        const float envelope = std::pow(std::max(1.0f - u * 0.90f, 0.10f), 0.74f) * tierWave;
        const float len = crownRadiusU * (0.30f + 0.86f * envelope) * rng.Range(0.82f, 1.18f);
        const float wid = len * rng.Range(0.38f, 0.50f);
        const float pitch = (-0.24f + 0.42f * u) - 0.16f * snow + rng.Range(-0.07f, 0.07f);
        const float droop = len * (0.18f + 0.16f * snow);
        const Vec3 dir = Normalize({std::cos(ang), std::sin(ang), pitch});
        const float trunkR = std::max(baseRad * (1.0f - 0.89f * tH), 1.5f);
        const Vec3 root{cx + dir.x * trunkR * 0.65f, cy + dir.y * trunkR * 0.65f, z};
        const float flexBase = Clamp(1.0f - tH * 0.60f, 0.15f, 0.95f);

        AppendConiferBough(verts, idx, root, dir, len, wid, droop, flexBase, snow);

        // Secondary lateral sub-bough forks on lower/mid tiers for lush multi-tier volume
        if (bi < 14) {
            const Vec3 midAnchor = root + Normalize({dir.x, dir.y, 0.0f}) * (len * 0.42f) +
                                   Vec3{0.0f, 0.0f, dir.z * len * 0.42f - droop * 0.18f};
            for (int side : {-1, 1}) {
                const float subAng = ang + float(side) * rng.Range(0.48f, 0.72f);
                const Vec3 subDir = Normalize({std::cos(subAng), std::sin(subAng), pitch - 0.05f});
                AppendConiferBough(verts, idx, midAnchor, subDir, len * 0.58f, wid * 0.72f,
                                   droop * 0.62f, flexBase * 0.75f, snow);
            }
        }
    }

    FinalizeProceduralMesh("proc_multitier_conifer", TerrainMaterialId::MossMeadow, 0u,
                           verts, idx, maxMeshletTris, outMesh);
    return !outMesh->meshlets.empty();
}

bool BuildGlacialIceArch(uint32_t seed, float spanU, float heightU, float thicknessU,
                         uint32_t maxMeshletTris, ProceduralMesh* outMesh) {
    if (!outMesh || spanU < 32.0f || heightU < 24.0f || thicknessU < 6.0f) return false;
    MeshRng rng(seed ^ 0x1CE4A2C4u);

    constexpr int kSteps = 18;
    constexpr int kRingSides = 8;
    std::vector<TerrainVertex> verts;
    std::vector<uint32_t> idx;

    for (int i = 0; i <= kSteps; ++i) {
        const float t = float(i) / float(kSteps);
        const float theta = t * kPi; // 0 .. pi arch curve
        const float x = -0.5f * spanU * std::cos(theta);
        const float z = heightU * std::sin(theta) - 12.0f;
        const float yDrift = std::sin(theta * 2.0f) * thicknessU * 0.35f;

        // Tangent along arch curve
        const Vec3 tang = Normalize({0.5f * spanU * std::sin(theta),
                                     2.0f * std::cos(theta * 2.0f) * thicknessU * 0.35f,
                                     heightU * std::cos(theta)});
        const Vec3 side{0.0f, 1.0f, 0.0f};
        const Vec3 up = Normalize(Cross(side, tang));
        const Vec3 bi = Normalize(Cross(tang, up));

        // Thicker buttresses at bases, sculpted faceted glacial ridge at keystone
        const float taper = 1.38f - 0.48f * std::sin(theta);
        const float rA = thicknessU * taper;
        const float rB = thicknessU * (0.72f + 0.35f * (1.0f - std::sin(theta)));

        for (int s = 0; s < kRingSides; ++s) {
            const float phi = (float(s) / float(kRingSides)) * kTwoPi;
            // Crystalline fracture facet modulation
            const float facet = 0.84f + 0.28f * HashTo01(Hash2I(i, s, seed + 51u));
            const Vec3 radDir = bi * (std::cos(phi) * rB * facet) + up * (std::sin(phi) * rA * facet);
            const Vec3 p = Vec3{x, yDrift, z} + radDir;
            const Vec3 n = Normalize(radDir);
            verts.push_back(MakeVertex(p, n, {t * 3.0f, float(s) / float(kRingSides)},
                                       TerrainMaterialId::GlacialIce, 0.35f, 0.15f, 0.0f, 0u));
        }
    }

    for (int i = 0; i < kSteps; ++i) {
        for (int s = 0; s < kRingSides; ++s) {
            const int sNext = (s + 1) % kRingSides;
            const uint32_t a = uint32_t(i * kRingSides + s);
            const uint32_t b = uint32_t(i * kRingSides + sNext);
            const uint32_t c = uint32_t((i + 1) * kRingSides + sNext);
            const uint32_t d = uint32_t((i + 1) * kRingSides + s);
            idx.push_back(a); idx.push_back(b); idx.push_back(c);
            idx.push_back(a); idx.push_back(c); idx.push_back(d);
        }
    }
    RecomputeSmoothNormals(verts, idx);
    FinalizeProceduralMesh("proc_glacial_ice_arch", TerrainMaterialId::GlacialIce, 0u,
                           verts, idx, maxMeshletTris, outMesh);
    return !outMesh->meshlets.empty();
}

bool BuildVolcanicVent(uint32_t seed, float radiusU, float heightU,
                       uint32_t maxMeshletTris, ProceduralMesh* outMesh) {
    if (!outMesh || radiusU < 12.0f || heightU < 12.0f) return false;
    MeshRng rng(seed ^ 0xF1AE0123u);

    constexpr int kSides = 12;
    constexpr int kRings = 7;
    std::vector<TerrainVertex> verts;
    std::vector<uint32_t> idx;

    // Outer jagged basalt cone -> rim -> inner sunken glowing magma throat
    const float profileR[kRings] = {1.00f, 0.82f, 0.64f, 0.46f, 0.36f, 0.28f, 0.18f};
    const float profileZ[kRings] = {-0.08f, 0.30f, 0.68f, 1.00f, 0.94f, 0.56f, 0.34f};
    const float profileEmis[kRings] = {0.0f, 0.05f, 0.12f, 0.35f, 0.72f, 0.95f, 1.00f};

    for (int r = 0; r < kRings; ++r) {
        for (int s = 0; s < kSides; ++s) {
            const float ang = (float(s) / float(kSides)) * kTwoPi;
            const float jag = 0.86f + 0.28f * HashTo01(Hash2I(r, s, seed + 77u));
            const float rad = radiusU * profileR[r] * jag;
            const float z = heightU * profileZ[r] * (r == 3 ? rng.Range(0.90f, 1.12f) : 1.0f);
            const Vec3 p{std::cos(ang) * rad, std::sin(ang) * rad, z};
            const TerrainMaterialId mat = (r >= 4) ? TerrainMaterialId::MagmaFissure : TerrainMaterialId::BasaltColumn;
            TerrainVertex tv = MakeVertex(p, {std::cos(ang), std::sin(ang), 0.35f},
                                          {float(s) / float(kSides), float(r) / float(kRings - 1)},
                                          mat, (r == 3 ? 0.6f : -0.3f), 0.0f, profileEmis[r], 1u);
            if (r == 3) {
                tv.splat0[3] = 0.65f;
                tv.splat1[0] = 0.35f;
            }
            verts.push_back(tv);
        }
    }
    for (int r = 0; r < kRings - 1; ++r) {
        for (int s = 0; s < kSides; ++s) {
            const int sNext = (s + 1) % kSides;
            const uint32_t a = uint32_t(r * kSides + s);
            const uint32_t b = uint32_t(r * kSides + sNext);
            const uint32_t c = uint32_t((r + 1) * kSides + sNext);
            const uint32_t d = uint32_t((r + 1) * kSides + s);
            idx.push_back(a); idx.push_back(b); idx.push_back(c);
            idx.push_back(a); idx.push_back(c); idx.push_back(d);
        }
    }
    // Magma throat floor cap
    const uint32_t floorCenter = static_cast<uint32_t>(verts.size());
    verts.push_back(MakeVertex({0.0f, 0.0f, heightU * 0.30f}, {0, 0, 1}, {0.5f, 0.5f},
                               TerrainMaterialId::MagmaFissure, -0.5f, 0.0f, 1.0f, 1u));
    const uint32_t lastRing = uint32_t((kRings - 1) * kSides);
    for (int s = 0; s < kSides; ++s) {
        idx.push_back(floorCenter);
        idx.push_back(lastRing + uint32_t((s + 1) % kSides));
        idx.push_back(lastRing + uint32_t(s));
    }

    RecomputeSmoothNormals(verts, idx);
    FinalizeProceduralMesh("proc_volcanic_vent", TerrainMaterialId::MagmaFissure, 0u,
                           verts, idx, maxMeshletTris, outMesh);
    return !outMesh->meshlets.empty();
}

bool BuildCrystalMonolith(uint32_t seed, float radiusU, float heightU, uint32_t shardCount,
                          uint32_t maxMeshletTris, ProceduralMesh* outMesh) {
    if (!outMesh || radiusU < 4.0f || heightU < 12.0f || shardCount == 0u) return false;
    MeshRng rng(seed ^ 0xC2757A11u);
    const uint32_t shards = std::clamp(shardCount, 1u, 9u);

    std::vector<TerrainVertex> verts;
    std::vector<uint32_t> idx;

    for (uint32_t sh = 0; sh < shards; ++sh) {
        const bool isHero = (sh == 0u);
        const float h = isHero ? heightU : heightU * rng.Range(0.35f, 0.78f);
        const float r = isHero ? radiusU * 0.45f : radiusU * rng.Range(0.18f, 0.34f);
        const float angOff = float(sh) * kGoldenAngle;
        const float dist = isHero ? 0.0f : radiusU * rng.Range(0.28f, 0.65f);
        const Vec3 basePos{std::cos(angOff) * dist, std::sin(angOff) * dist, -8.0f};
        const float tilt = isHero ? rng.Range(0.02f, 0.10f) : rng.Range(0.18f, 0.48f);
        const Vec3 axis = Normalize({std::cos(angOff) * std::sin(tilt),
                                     std::sin(angOff) * std::sin(tilt), std::cos(tilt)});
        const Vec3 refUp = std::fabs(axis.z) < 0.95f ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
        const Vec3 uVec = Normalize(Cross(refUp, axis));
        const Vec3 vVec = Normalize(Cross(axis, uVec));

        const float neckZ = h * 0.74f;
        const Vec3 apex = basePos + axis * h;
        const float yaw = rng.Range(0.0f, kTwoPi);

        // Hard-faceted hexagonal quartz/amethyst prism + pointed pyramidal termination
        constexpr int kSides = 6;
        for (int s = 0; s < kSides; ++s) {
            const float a0 = yaw + (float(s) / float(kSides)) * kTwoPi;
            const float a1 = yaw + (float(s + 1) / float(kSides)) * kTwoPi;
            const Vec3 d0 = uVec * std::cos(a0) + vVec * std::sin(a0);
            const Vec3 d1 = uVec * std::cos(a1) + vVec * std::sin(a1);
            const Vec3 b0 = basePos + d0 * (r * 0.85f);
            const Vec3 b1 = basePos + d1 * (r * 0.85f);
            const Vec3 n0 = basePos + axis * neckZ + d0 * r;
            const Vec3 n1 = basePos + axis * neckZ + d1 * r;

            const Vec3 faceN = Normalize(Cross(b1 - b0, n0 - b0));
            const uint32_t vb = static_cast<uint32_t>(verts.size());
            verts.push_back(MakeVertex(b0, faceN, {0.0f, 0.0f}, TerrainMaterialId::ArcaneCrystal, 0.4f, 0.0f, 0.55f, 4u));
            verts.push_back(MakeVertex(b1, faceN, {1.0f, 0.0f}, TerrainMaterialId::ArcaneCrystal, 0.4f, 0.0f, 0.55f, 4u));
            verts.push_back(MakeVertex(n1, faceN, {1.0f, 0.74f}, TerrainMaterialId::ArcaneCrystal, 0.6f, 0.0f, 0.85f, 4u));
            verts.push_back(MakeVertex(n0, faceN, {0.0f, 0.74f}, TerrainMaterialId::ArcaneCrystal, 0.6f, 0.0f, 0.85f, 4u));
            idx.push_back(vb + 0); idx.push_back(vb + 1); idx.push_back(vb + 2);
            idx.push_back(vb + 0); idx.push_back(vb + 2); idx.push_back(vb + 3);

            // Sharp quartz pyramidal tip facet
            const Vec3 tipN = Normalize(Cross(n1 - n0, apex - n0));
            const uint32_t vt = static_cast<uint32_t>(verts.size());
            verts.push_back(MakeVertex(n0, tipN, {0.0f, 0.74f}, TerrainMaterialId::ArcaneCrystal, 0.7f, 0.0f, 0.85f, 4u));
            verts.push_back(MakeVertex(n1, tipN, {1.0f, 0.74f}, TerrainMaterialId::ArcaneCrystal, 0.7f, 0.0f, 0.85f, 4u));
            verts.push_back(MakeVertex(apex, tipN, {0.5f, 1.0f}, TerrainMaterialId::ArcaneCrystal, 1.0f, 0.0f, 1.00f, 4u));
            idx.push_back(vt + 0); idx.push_back(vt + 1); idx.push_back(vt + 2);
        }
    }

    FinalizeProceduralMesh("proc_crystal_monolith", TerrainMaterialId::ArcaneCrystal, 0u,
                           verts, idx, maxMeshletTris, outMesh);
    return !outMesh->meshlets.empty();
}

bool BuildNordicSnowDrift(uint32_t seed, float lengthU, float widthU, float heightU,
                          uint32_t maxMeshletTris, ProceduralMesh* outMesh) {
    if (!outMesh || lengthU < 16.0f || widthU < 16.0f || heightU < 4.0f) return false;
    constexpr int kNx = 12;
    constexpr int kNy = 8;
    std::vector<TerrainVertex> verts;
    std::vector<uint32_t> idx;
    verts.reserve(size_t((kNx + 1) * (kNy + 1)));

    for (int iy = 0; iy <= kNy; ++iy) {
        const float v = float(iy) / float(kNy);
        const float ny = (v * 2.0f - 1.0f);
        for (int ix = 0; ix <= kNx; ++ix) {
            const float u = float(ix) / float(kNx);
            const float nx = (u * 2.0f - 1.0f);
            // Asymmetric windward/leeward sastrugi cornice profile
            const float r2 = nx * nx + ny * ny;
            const float dome = std::max(0.0f, 1.0f - r2);
            const float windSkew = Smoothstep(-1.0f, 0.35f, nx) * (1.0f - Smoothstep(0.45f, 1.0f, nx) * 0.65f);
            const float ripple = 0.90f + 0.15f * Fbm2D(u * 4.5f, v * 2.5f, seed + 881u, 3);
            const float z = dome * dome * (0.45f + 0.55f * windSkew) * heightU * ripple - 2.0f;
            verts.push_back(MakeVertex({nx * lengthU * 0.5f, ny * widthU * 0.5f, z},
                                       {0, 0, 1}, {u, v}, TerrainMaterialId::SnowPack, 0.1f, 0.0f, 0.0f, 0u));
        }
    }
    const uint32_t stride = uint32_t(kNx + 1);
    for (int iy = 0; iy < kNy; ++iy) {
        for (int ix = 0; ix < kNx; ++ix) {
            const uint32_t a = uint32_t(iy) * stride + uint32_t(ix);
            const uint32_t b = a + 1u, c = a + stride, d = c + 1u;
            idx.push_back(a); idx.push_back(b); idx.push_back(d);
            idx.push_back(a); idx.push_back(d); idx.push_back(c);
        }
    }
    RecomputeSmoothNormals(verts, idx);
    FinalizeProceduralMesh("proc_nordic_snow_drift", TerrainMaterialId::SnowPack, 0u,
                           verts, idx, maxMeshletTris, outMesh);
    return !outMesh->meshlets.empty();
}

bool BuildAlpineBoulder(uint32_t seed, float radiusU, float strataTilt, uint32_t fracturePlanes,
                        uint32_t maxMeshletTris, ProceduralMesh* outMesh) {
    if (!outMesh || radiusU < 4.0f) return false;
    MeshRng rng(seed ^ 0xB01D3877u);
    const uint32_t nPlanes = std::clamp(fracturePlanes, 4u, 24u);

    const float phi = (1.0f + std::sqrt(5.0f)) * 0.5f;
    std::vector<Vec3> pos = {
        Normalize({-1, phi, 0}), Normalize({1, phi, 0}), Normalize({-1, -phi, 0}), Normalize({1, -phi, 0}),
        Normalize({0, -1, phi}), Normalize({0, 1, phi}), Normalize({0, -1, -phi}), Normalize({0, 1, -phi}),
        Normalize({phi, 0, -1}), Normalize({phi, 0, 1}), Normalize({-phi, 0, -1}), Normalize({-phi, 0, 1}),
    };
    std::vector<uint32_t> idx = {
        0,11,5, 0,5,1, 0,1,7, 0,7,10, 0,10,11,
        1,5,9, 5,11,4, 11,10,2, 10,7,6, 7,1,8,
        3,9,4, 3,4,2, 3,2,6, 3,6,8, 3,8,9,
        4,9,5, 2,4,11, 6,2,10, 8,6,7, 9,8,1,
    };

    for (int sub = 0; sub < 2; ++sub) {
        std::map<std::pair<uint32_t, uint32_t>, uint32_t> midCache;
        auto getMid = [&](uint32_t a, uint32_t b) -> uint32_t {
            const auto key = a < b ? std::make_pair(a, b) : std::make_pair(b, a);
            if (auto it = midCache.find(key); it != midCache.end()) return it->second;
            const uint32_t id = static_cast<uint32_t>(pos.size());
            pos.push_back(Normalize((pos[a] + pos[b]) * 0.5f));
            midCache.emplace(key, id);
            return id;
        };
        std::vector<uint32_t> nextIdx;
        nextIdx.reserve(idx.size() * 4u);
        for (size_t i = 0; i + 2 < idx.size(); i += 3) {
            const uint32_t a = idx[i], b = idx[i + 1], c = idx[i + 2];
            const uint32_t ab = getMid(a, b), bc = getMid(b, c), ca = getMid(c, a);
            nextIdx.insert(nextIdx.end(), {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca});
        }
        idx = std::move(nextIdx);
    }

    struct Plane { Vec3 n; float d; };
    std::vector<Plane> planes;
    planes.reserve(nPlanes);
    for (uint32_t i = 0; i < nPlanes; ++i) {
        const float z = rng.Range(-0.92f, 0.92f);
        const float a = rng.Range(0.0f, kTwoPi);
        const float r = std::sqrt(std::max(1.0f - z * z, 0.0f));
        planes.push_back({Normalize({r * std::cos(a), r * std::sin(a), z}), rng.Range(0.68f, 0.96f)});
    }
    const Vec3 strataAxis = Normalize({std::sin(strataTilt), 0.18f, std::cos(strataTilt)});
    const float sx = rng.Range(0.85f, 1.35f), sy = rng.Range(0.85f, 1.30f), sz = rng.Range(0.62f, 0.95f);

    std::vector<TerrainVertex> verts(pos.size());
    for (size_t i = 0; i < pos.size(); ++i) {
        Vec3 p = pos[i];
        float rScale = 1.0f;
        for (const auto& pl : planes) {
            const float proj = Dot(p, pl.n);
            if (proj > pl.d) rScale = std::min(rScale, pl.d / std::max(proj, 0.1f));
        }
        const float ledge = 0.045f * std::sin(Dot(p, strataAxis) * 11.0f + 0.5f * std::cos(p.x * 5.0f));
        p = {p.x * radiusU * rScale * sx * (1.0f + ledge),
             p.y * radiusU * rScale * sy * (1.0f + ledge),
             p.z * radiusU * rScale * sz};
        if (p.z < -radiusU * 0.45f) p.z = -radiusU * 0.45f + (p.z + radiusU * 0.45f) * 0.25f;
        const Vec2 uv{0.5f + std::atan2(pos[i].y, pos[i].x) / kTwoPi,
                      0.5f - std::asin(Clamp(pos[i].z, -1.0f, 1.0f)) / kPi};
        verts[i] = MakeVertex(p, pos[i], uv, TerrainMaterialId::RockGranite, 0.2f, 0.0f, 0.0f, 2u);
    }
    RecomputeSmoothNormals(verts, idx);
    FinalizeProceduralMesh("proc_alpine_boulder", TerrainMaterialId::RockGranite, 0u,
                           verts, idx, maxMeshletTris, outMesh);
    return !outMesh->meshlets.empty();
}

bool BuildAlpineGrassClump(uint32_t seed, uint32_t bladeCount, float heightU,
                           uint32_t maxMeshletTris, ProceduralMesh* outMesh) {
    if (!outMesh || bladeCount == 0u || heightU < 4.0f) return false;
    MeshRng rng(seed ^ 0x6AA55C10u);
    const uint32_t blades = std::clamp(bladeCount, 3u, 32u);
    constexpr uint32_t kSegs = 4u;

    std::vector<TerrainVertex> verts;
    std::vector<uint32_t> idx;
    for (uint32_t b = 0; b < blades; ++b) {
        const float ang = float(b) * kGoldenAngle + rng.Range(-0.22f, 0.22f);
        const float radOff = rng.Range(0.0f, heightU * 0.45f);
        const float h = heightU * rng.Range(0.65f, 1.18f);
        const float baseW = std::max(heightU * 0.085f, 0.8f);
        const float lean = rng.Range(0.28f, 0.78f) * h;
        const Vec3 dir{std::cos(ang), std::sin(ang), 0.0f};
        const Vec3 side{-dir.y, dir.x, 0.0f};
        const Vec3 root{dir.x * radOff, dir.y * radOff, 0.0f};
        const uint32_t v0 = static_cast<uint32_t>(verts.size());

        for (uint32_t s = 0; s < kSegs; ++s) {
            const float t = float(s) / float(kSegs);
            const float w = baseW * (1.0f - t * 0.88f);
            const Vec3 ctr = root + dir * (lean * t * t) + Vec3{0.0f, 0.0f, h * t};
            const Vec3 tang = Normalize(dir * (2.0f * lean * t) + Vec3{0.0f, 0.0f, h});
            const Vec3 nrm = Normalize(Cross(side, tang));
            const float uvY = 1.0f - t;
            verts.push_back(MakeVertex(ctr - side * (0.5f * w), nrm, {0.0f, uvY}, TerrainMaterialId::MossMeadow, 0.0f, 0.0f, 0.0f, 5u));
            verts.push_back(MakeVertex(ctr + side * (0.5f * w), nrm, {1.0f, uvY}, TerrainMaterialId::MossMeadow, 0.0f, 0.0f, 0.0f, 5u));
        }
        const Vec3 tip = root + dir * lean + Vec3{0.0f, 0.0f, h};
        const Vec3 tipN = Normalize(dir * 0.35f + Vec3{0.0f, 0.0f, 0.94f});
        const uint32_t vTip = static_cast<uint32_t>(verts.size());
        verts.push_back(MakeVertex(tip, tipN, {0.5f, 0.0f}, TerrainMaterialId::MossMeadow, 0.0f, 0.0f, 0.0f, 5u));

        for (uint32_t s = 0; s + 1 < kSegs; ++s) {
            const uint32_t a = v0 + s * 2u;
            const uint32_t c = v0 + (s + 1u) * 2u;
            idx.push_back(a + 0); idx.push_back(a + 1); idx.push_back(c + 1);
            idx.push_back(a + 0); idx.push_back(c + 1); idx.push_back(c + 0);
        }
        const uint32_t topBase = v0 + (kSegs - 1u) * 2u;
        idx.push_back(topBase + 0); idx.push_back(topBase + 1); idx.push_back(vTip);
    }
    FinalizeProceduralMesh("proc_alpine_grass", TerrainMaterialId::MossMeadow, 0u,
                           verts, idx, maxMeshletTris, outMesh);
    return !outMesh->meshlets.empty();
}

std::string SerializeMeshToObjString(const ProceduralMesh& mesh) {
    std::ostringstream ss;
    ss << "# RAW WorldGen Procedural Mesh Export (Clean-Room MIT)\n";
    ss << "o " << (mesh.name.empty() ? "raw_mesh" : mesh.name) << "\n";
    for (const auto& v : mesh.vertices) {
        ss << "v " << v.pos[0] << " " << v.pos[1] << " " << v.pos[2] << "\n";
    }
    for (const auto& v : mesh.vertices) {
        ss << "vn " << v.normal[0] << " " << v.normal[1] << " " << v.normal[2] << "\n";
    }
    for (const auto& v : mesh.vertices) {
        ss << "vt " << v.uv[0] << " " << (1.0f - v.uv[1]) << "\n";
    }
    for (const auto& m : mesh.meshlets) {
        for (uint32_t t = 0; t < m.triangleCount; ++t) {
            const size_t base = size_t(m.triangleOffset) + size_t(t) * 3u;
            if (base + 2u >= mesh.indices.size()) break;
            const uint32_t i0 = m.vertexOffset + mesh.indices[base + 0u] + 1u;
            const uint32_t i1 = m.vertexOffset + mesh.indices[base + 1u] + 1u;
            const uint32_t i2 = m.vertexOffset + mesh.indices[base + 2u] + 1u;
            ss << "f " << i0 << "/" << i0 << "/" << i0 << " "
                       << i1 << "/" << i1 << "/" << i1 << " "
                       << i2 << "/" << i2 << "/" << i2 << "\n";
        }
    }
    return ss.str();
}

bool ExportMeshToObjFile(const ProceduralMesh& mesh, const std::string& path) {
    if (path.empty() || mesh.vertices.empty() || mesh.meshlets.empty()) return false;
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    const std::string text = SerializeMeshToObjString(mesh);
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    return f.good();
}

std::vector<uint8_t> SerializeMeshToGlbBytes(const ProceduralMesh& mesh, bool zUpToYUp) {
    if (mesh.vertices.empty() || mesh.meshlets.empty()) return {};

    // Flatten per-meshlet local indices into global u32 indices for standard glTF 2.0 consumers.
    std::vector<uint32_t> flatIndices;
    for (const auto& m : mesh.meshlets) {
        for (uint32_t t = 0; t < m.triangleCount; ++t) {
            const size_t base = size_t(m.triangleOffset) + size_t(t) * 3u;
            if (base + 2u >= mesh.indices.size()) break;
            flatIndices.push_back(m.vertexOffset + mesh.indices[base + 0u]);
            flatIndices.push_back(m.vertexOffset + mesh.indices[base + 1u]);
            flatIndices.push_back(m.vertexOffset + mesh.indices[base + 2u]);
        }
    }
    if (flatIndices.size() < 3u) return {};

    const uint32_t vCount = static_cast<uint32_t>(mesh.vertices.size());
    const uint32_t iCount = static_cast<uint32_t>(flatIndices.size());

    Vec3 minP{1e30f, 1e30f, 1e30f}, maxP{-1e30f, -1e30f, -1e30f};
    std::vector<uint8_t> bin;
    bin.reserve(size_t(vCount) * (12u + 12u + 8u) + size_t(iCount) * 4u + 16u);

    const size_t posOff = bin.size();
    for (const auto& v : mesh.vertices) {
        // Inverse of RawAssetBridge's yUpToZUp (x, -z, y) -> (x, z, -y)
        const float px = v.pos[0];
        const float py = zUpToYUp ? v.pos[2] : v.pos[1];
        const float pz = zUpToYUp ? -v.pos[1] : v.pos[2];
        minP.x = std::min(minP.x, px); minP.y = std::min(minP.y, py); minP.z = std::min(minP.z, pz);
        maxP.x = std::max(maxP.x, px); maxP.y = std::max(maxP.y, py); maxP.z = std::max(maxP.z, pz);
        AppendF32(bin, px); AppendF32(bin, py); AppendF32(bin, pz);
    }
    const size_t posLen = bin.size() - posOff;

    const size_t nrmOff = bin.size();
    for (const auto& v : mesh.vertices) {
        const float nx = v.normal[0];
        const float ny = zUpToYUp ? v.normal[2] : v.normal[1];
        const float nz = zUpToYUp ? -v.normal[1] : v.normal[2];
        AppendF32(bin, nx); AppendF32(bin, ny); AppendF32(bin, nz);
    }
    const size_t nrmLen = bin.size() - nrmOff;

    const size_t uvOff = bin.size();
    for (const auto& v : mesh.vertices) {
        AppendF32(bin, v.uv[0]);
        AppendF32(bin, v.uv[1]);
    }
    const size_t uvLen = bin.size() - uvOff;

    const size_t idxOff = bin.size();
    for (uint32_t idxVal : flatIndices) AppendU32(bin, idxVal);
    const size_t idxLen = bin.size() - idxOff;

    while ((bin.size() & 3u) != 0u) bin.push_back(0u);

    std::ostringstream js;
    js << "{\"asset\":{\"version\":\"2.0\",\"generator\":\"RAW WorldGen MIT\"},"
       << "\"buffers\":[{\"byteLength\":" << bin.size() << "}],"
       << "\"bufferViews\":["
       << "{\"buffer\":0,\"byteOffset\":" << posOff << ",\"byteLength\":" << posLen << ",\"target\":34962},"
       << "{\"buffer\":0,\"byteOffset\":" << nrmOff << ",\"byteLength\":" << nrmLen << ",\"target\":34962},"
       << "{\"buffer\":0,\"byteOffset\":" << uvOff << ",\"byteLength\":" << uvLen << ",\"target\":34962},"
       << "{\"buffer\":0,\"byteOffset\":" << idxOff << ",\"byteLength\":" << idxLen << ",\"target\":34963}"
       << "],"
       << "\"accessors\":["
       << "{\"bufferView\":0,\"componentType\":5126,\"count\":" << vCount
       << ",\"type\":\"VEC3\",\"min\":[" << minP.x << "," << minP.y << "," << minP.z
       << "],\"max\":[" << maxP.x << "," << maxP.y << "," << maxP.z << "]},"
       << "{\"bufferView\":1,\"componentType\":5126,\"count\":" << vCount << ",\"type\":\"VEC3\"},"
       << "{\"bufferView\":2,\"componentType\":5126,\"count\":" << vCount << ",\"type\":\"VEC2\"},"
       << "{\"bufferView\":3,\"componentType\":5125,\"count\":" << iCount << ",\"type\":\"SCALAR\"}"
       << "],"
       << "\"materials\":[{\"name\":\"" << GetMaterialName(mesh.primaryMaterial)
       << "\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.8,0.82,0.85,1.0],\"metallicFactor\":0.05,\"roughnessFactor\":0.62}}],"
       << "\"meshes\":[{\"name\":\"" << (mesh.name.empty() ? "raw_mesh" : mesh.name)
       << "\",\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2},\"indices\":3,\"material\":0}]}],"
       << "\"nodes\":[{\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}],\"scene\":0}";

    std::string json = js.str();
    while ((json.size() & 3u) != 0u) json.push_back(' ');

    const uint32_t totalLen = static_cast<uint32_t>(12u + 8u + json.size() + 8u + bin.size());
    std::vector<uint8_t> glb;
    glb.reserve(totalLen);
    AppendU32(glb, 0x46546C67u); // "glTF"
    AppendU32(glb, 2u);          // version 2
    AppendU32(glb, totalLen);
    AppendU32(glb, static_cast<uint32_t>(json.size()));
    AppendU32(glb, 0x4E4F534Au); // "JSON"
    glb.insert(glb.end(), json.begin(), json.end());
    AppendU32(glb, static_cast<uint32_t>(bin.size()));
    AppendU32(glb, 0x004E4942u); // "BIN\0"
    glb.insert(glb.end(), bin.begin(), bin.end());
    return glb;
}

bool ExportMeshToGlbFile(const ProceduralMesh& mesh, const std::string& path, bool zUpToYUp) {
    if (path.empty()) return false;
    const auto bytes = SerializeMeshToGlbBytes(mesh, zUpToYUp);
    if (bytes.empty()) return false;
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return f.good();
}

}  // namespace raw::worldgen
