// RawProceduralLib.cpp: Zero-External-Asset Procedural Geometry & PBR Material Synthesis Library.
// Original work, MIT.
#include "RawProceduralLib.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <utility>

namespace raw::bridge {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kGoldenAngle = 2.39996322972865332223f;

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 Cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float Len(Vec3 a) { return std::sqrt(Dot(a, a)); }
inline Vec3 Norm(Vec3 a) {
    const float l = Len(a);
    return l > 1.0e-6f ? Vec3{a.x / l, a.y / l, a.z / l} : Vec3{0.0f, 0.0f, 1.0f};
}

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
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

float HashTile2(int ix, int iy, int periodX, int periodY, uint32_t seed) {
    const int wx = ((ix % periodX) + periodX) % periodX;
    const int wy = ((iy % periodY) + periodY) % periodY;
    uint32_t h = uint32_t(wx) * 374761393u ^ uint32_t(wy) * 668265263u ^ (seed * 2246822519u);
    h = (h ^ (h >> 13)) * 1274126177u;
    return float((h ^ (h >> 16)) & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

float TileValueNoise(float u, float v, int freqX, int freqY, uint32_t seed) {
    const float x = u * float(freqX);
    const float y = v * float(freqY);
    const int ix = int(std::floor(x));
    const int iy = int(std::floor(y));
    const float fx = x - float(ix);
    const float fy = y - float(iy);
    const float sx = fx * fx * (3.0f - 2.0f * fx);
    const float sy = fy * fy * (3.0f - 2.0f * fy);
    const float n00 = HashTile2(ix, iy, freqX, freqY, seed);
    const float n10 = HashTile2(ix + 1, iy, freqX, freqY, seed);
    const float n01 = HashTile2(ix, iy + 1, freqX, freqY, seed);
    const float n11 = HashTile2(ix + 1, iy + 1, freqX, freqY, seed);
    const float a = n00 + (n10 - n00) * sx;
    const float b = n01 + (n11 - n01) * sx;
    return a + (b - a) * sy;
}

float TileFbm(float u, float v, int baseFreqX, int baseFreqY, uint32_t seed, int octaves) {
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    int fx = std::max(baseFreqX, 1), fy = std::max(baseFreqY, 1);
    for (int i = 0; i < octaves; ++i) {
        sum += TileValueNoise(u, v, fx, fy, seed + uint32_t(i) * 1013u) * amp;
        norm += amp;
        amp *= 0.5f;
        fx *= 2;
        fy *= 2;
    }
    return norm > 0.0f ? (sum / norm) : 0.0f;
}

float TileWorleyEdge(float u, float v, int cells, uint32_t seed) {
    const float x = u * float(cells);
    const float y = v * float(cells);
    const int cx = int(std::floor(x));
    const int cy = int(std::floor(y));
    float f1 = 8.0f, f2 = 8.0f;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int nx = cx + dx;
            const int ny = cy + dy;
            const float px = float(nx) + 0.15f + 0.70f * HashTile2(nx, ny, cells, cells, seed);
            const float py = float(ny) + 0.15f + 0.70f * HashTile2(nx, ny, cells, cells, seed + 7919u);
            const float d = std::hypot(x - px, y - py);
            if (d < f1) { f2 = f1; f1 = d; }
            else if (d < f2) { f2 = d; }
        }
    }
    return std::clamp(f2 - f1, 0.0f, 1.0f);
}

void RecomputeSmoothNormals(std::vector<RawMeshVertex>& verts, const std::vector<uint32_t>& idx) {
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
        const Vec3 n = Norm(acc[i]);
        verts[i].normal[0] = n.x;
        verts[i].normal[1] = n.y;
        verts[i].normal[2] = n.z;
    }
}

void FinalizeAsset(const std::string& name, const std::vector<RawMeshVertex>& srcVerts,
                   const std::vector<uint32_t>& srcIdx, RawMeshAsset* outMesh) {
    outMesh->name = name;
    outMesh->fromGlb = false;
    outMesh->vertices.clear();
    outMesh->indices.clear();
    outMesh->meshlets.clear();
    outMesh->materials.clear();
    BuildRawMeshlets(srcVerts, srcIdx, &outMesh->vertices, &outMesh->indices, &outMesh->meshlets);
}

void AppendBoughFrond(std::vector<RawMeshVertex>& verts, std::vector<uint32_t>& idx,
                      Vec3 root, Vec3 dir, float lengthU, float widthU, float droopU, float flexBase) {
    const Vec3 fwd = Norm({dir.x, dir.y, 0.0f});
    const Vec3 right{-fwd.y, fwd.x, 0.0f};
    const uint32_t base = static_cast<uint32_t>(verts.size());

    // 4-ring tented 3D conifer bough volume (ridge spine + sloped needle aprons, zero flat billboard card)
    constexpr int kRings = 4;
    for (int r = 0; r < kRings; ++r) {
        const float t = float(r) / float(kRings - 1);
        const float taper = std::sin(t * kPi * 0.85f + 0.15f) * (1.0f - 0.55f * t);
        const float hw = widthU * (0.45f + 0.75f * taper);
        const float lift = widthU * 0.24f * (1.0f - 0.65f * t);
        const float sag = -droopU * t * t;
        const Vec3 spine = root + fwd * (lengthU * t) + Vec3{0.0f, 0.0f, dir.z * lengthU * t + sag};
        const float uvY = std::clamp(flexBase * (1.0f - 0.65f * t), 0.0f, 1.0f);

        const Vec3 pL = spine - right * hw;
        const Vec3 pC = spine + Vec3{0.0f, 0.0f, lift};
        const Vec3 pR = spine + right * hw;
        const Vec3 nL = Norm((right * -0.55f) + Vec3{0.0f, 0.0f, 0.83f});
        const Vec3 nC = Norm(fwd * 0.25f + Vec3{0.0f, 0.0f, 0.96f});
        const Vec3 nR = Norm((right * 0.55f) + Vec3{0.0f, 0.0f, 0.83f});

        verts.push_back({{pL.x, pL.y, pL.z}, {nL.x, nL.y, nL.z}, {0.0f, uvY}});
        verts.push_back({{pC.x, pC.y, pC.z}, {nC.x, nC.y, nC.z}, {0.5f, uvY}});
        verts.push_back({{pR.x, pR.y, pR.z}, {nR.x, nR.y, nR.z}, {1.0f, uvY}});
    }
    for (int r = 0; r < kRings - 1; ++r) {
        const uint32_t a = base + uint32_t(r * 3);
        const uint32_t b = base + uint32_t((r + 1) * 3);
        // Left apron
        idx.push_back(a + 0); idx.push_back(a + 1); idx.push_back(b + 1);
        idx.push_back(a + 0); idx.push_back(b + 1); idx.push_back(b + 0);
        // Right apron
        idx.push_back(a + 1); idx.push_back(a + 2); idx.push_back(b + 2);
        idx.push_back(a + 1); idx.push_back(b + 2); idx.push_back(b + 1);
    }
}

uint8_t ToU8(float v) {
    return static_cast<uint8_t>(std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f));
}

}  // namespace

bool GenerateNordicSpruceTree(uint32_t seed, float heightU, float crownRadiusU, float snowLoad01,
                              RawMeshAsset* outMesh) {
    if (!outMesh || heightU < 40.0f || crownRadiusU < 10.0f) return false;
    Rng rng(seed ^ 0x51C0E11Au);
    const float snow = std::clamp(snowLoad01, 0.0f, 1.0f);

    std::vector<RawMeshVertex> verts;
    std::vector<uint32_t> idx;

    // 1. Organic curved & buttressed Nordic conifer trunk (8 radial sides x 9 vertical rings).
    constexpr int kSides = 8;
    constexpr int kRings = 9;
    const float baseRad = std::clamp(crownRadiusU * 0.14f, 4.0f, 36.0f);
    const float leanX = rng.Range(-heightU * 0.025f, heightU * 0.025f);
    const float leanY = rng.Range(-heightU * 0.025f, heightU * 0.025f);
    for (int r = 0; r < kRings; ++r) {
        const float t = float(r) / float(kRings - 1);
        const float z = t * heightU;
        const float buttress = 1.0f + 0.55f * std::exp(-t * 9.0f);
        const float rad = std::max(baseRad * (1.0f - 0.88f * t) * buttress, 1.2f);
        const float cx = leanX * t * t;
        const float cy = leanY * t * t;
        for (int s = 0; s < kSides; ++s) {
            const float ang = (float(s) / float(kSides)) * kTwoPi;
            const float flute = 1.0f + 0.12f * std::exp(-t * 7.0f) * std::cos(ang * 4.0f + float(seed & 15u));
            const float nx = std::cos(ang);
            const float ny = std::sin(ang);
            const Vec3 p{cx + nx * rad * flute, cy + ny * rad * flute, z};
            const Vec3 n = Norm({nx, ny, 0.14f});
            verts.push_back({{p.x, p.y, p.z}, {n.x, n.y, n.z}, {float(s) / float(kSides), 1.0f - t}});
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

    // 2. Golden-angle phyllotaxis conifer bough tiers (18 primary whorled boughs from 22%..96% height).
    constexpr int kBoughs = 18;
    for (int bi = 0; bi < kBoughs; ++bi) {
        const float u = float(bi) / float(kBoughs - 1);
        const float tH = 0.22f + 0.74f * u;
        const float z = tH * heightU;
        const float cx = leanX * tH * tH;
        const float cy = leanY * tH * tH;
        const float ang = float(bi) * kGoldenAngle + rng.Range(-0.18f, 0.18f);
        const float envelope = std::pow(std::max(1.0f - u * 0.92f, 0.08f), 0.78f);
        const float len = crownRadiusU * (0.28f + 0.78f * envelope) * rng.Range(0.88f, 1.12f);
        const float wid = len * 0.42f;
        const float pitch = (-0.20f + 0.34f * u) - 0.18f * snow;
        const float droop = len * (0.16f + 0.14f * snow);
        const Vec3 dir = Norm({std::cos(ang), std::sin(ang), pitch});
        const float trunkR = std::max(baseRad * (1.0f - 0.88f * tH), 1.5f);
        const Vec3 root{cx + dir.x * trunkR * 0.7f, cy + dir.y * trunkR * 0.7f, z};
        const float flexBase = std::clamp(1.0f - tH * 0.65f, 0.15f, 0.95f);
        AppendBoughFrond(verts, idx, root, dir, len, wid, droop, flexBase);
    }

    FinalizeAsset("proc_nordic_spruce", verts, idx, outMesh);
    RawPbrMaterial mat{};
    mat.name = "proc_nordic_spruce_mat";
    mat.baseColorFactor[0] = 0.14f; mat.baseColorFactor[1] = 0.24f;
    mat.baseColorFactor[2] = 0.12f; mat.baseColorFactor[3] = 1.0f;
    mat.roughnessFactor = 0.82f;
    outMesh->materials.push_back(mat);
    return !outMesh->meshlets.empty();
}

bool GenerateNordicBoulder(uint32_t seed, float radiusU, float strataTilt, uint32_t fracturePlanes,
                           RawMeshAsset* outMesh) {
    if (!outMesh || radiusU < 4.0f) return false;
    Rng rng(seed ^ 0xB01D3877u);
    const uint32_t nPlanes = std::clamp(fracturePlanes, 4u, 24u);

    // Base icosahedron (12 verts, 20 tris) subdivided twice -> 162 verts, 320 tris.
    const float phi = (1.0f + std::sqrt(5.0f)) * 0.5f;
    std::vector<Vec3> pos = {
        Norm({-1, phi, 0}), Norm({1, phi, 0}), Norm({-1, -phi, 0}), Norm({1, -phi, 0}),
        Norm({0, -1, phi}), Norm({0, 1, phi}), Norm({0, -1, -phi}), Norm({0, 1, -phi}),
        Norm({phi, 0, -1}), Norm({phi, 0, 1}), Norm({-phi, 0, -1}), Norm({-phi, 0, 1}),
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
            const Vec3 m = Norm((pos[a] + pos[b]) * 0.5f);
            const uint32_t id = static_cast<uint32_t>(pos.size());
            pos.push_back(m);
            midCache.emplace(key, id);
            return id;
        };
        std::vector<uint32_t> nextIdx;
        nextIdx.reserve(idx.size() * 4);
        for (size_t i = 0; i + 2 < idx.size(); i += 3) {
            const uint32_t a = idx[i], b = idx[i + 1], c = idx[i + 2];
            const uint32_t ab = getMid(a, b), bc = getMid(b, c), ca = getMid(c, a);
            nextIdx.insert(nextIdx.end(), {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca});
        }
        idx = std::move(nextIdx);
    }

    // Generate deterministic Voronoi basalt fracture half-spaces.
    struct Plane { Vec3 n; float d; };
    std::vector<Plane> planes;
    planes.reserve(nPlanes);
    for (uint32_t i = 0; i < nPlanes; ++i) {
        const float z = rng.Range(-0.92f, 0.92f);
        const float a = rng.Range(0.0f, kTwoPi);
        const float r = std::sqrt(std::max(1.0f - z * z, 0.0f));
        planes.push_back({Norm({r * std::cos(a), r * std::sin(a), z}), rng.Range(0.68f, 0.96f)});
    }

    const Vec3 strataAxis = Norm({std::sin(strataTilt), 0.18f, std::cos(strataTilt)});
    const float sx = rng.Range(0.85f, 1.35f), sy = rng.Range(0.85f, 1.30f), sz = rng.Range(0.62f, 0.95f);

    std::vector<RawMeshVertex> verts(pos.size());
    for (size_t i = 0; i < pos.size(); ++i) {
        Vec3 p = pos[i];
        float rScale = 1.0f;
        for (const auto& pl : planes) {
            const float proj = Dot(p, pl.n);
            if (proj > pl.d) {
                rScale = std::min(rScale, pl.d / std::max(proj, 0.1f));
            }
        }
        const float sCoord = Dot(p, strataAxis);
        const float ledge = 0.045f * std::sin(sCoord * 11.0f + 0.5f * std::cos(p.x * 5.0f));
        p = {p.x * radiusU * rScale * sx * (1.0f + ledge),
             p.y * radiusU * rScale * sy * (1.0f + ledge),
             p.z * radiusU * rScale * sz};
        // Flatten underside slightly so boulders seat firmly into Skyrim terrain.
        if (p.z < -radiusU * 0.45f) p.z = -radiusU * 0.45f + (p.z + radiusU * 0.45f) * 0.25f;
        verts[i].pos[0] = p.x; verts[i].pos[1] = p.y; verts[i].pos[2] = p.z;
        verts[i].uv[0] = 0.5f + std::atan2(pos[i].y, pos[i].x) / kTwoPi;
        verts[i].uv[1] = 0.5f - std::asin(std::clamp(pos[i].z, -1.0f, 1.0f)) / kPi;
    }
    RecomputeSmoothNormals(verts, idx);
    FinalizeAsset("proc_nordic_boulder", verts, idx, outMesh);
    RawPbrMaterial mat{};
    mat.name = "proc_nordic_granite_mat";
    mat.baseColorFactor[0] = 0.28f; mat.baseColorFactor[1] = 0.29f;
    mat.baseColorFactor[2] = 0.31f; mat.baseColorFactor[3] = 1.0f;
    mat.roughnessFactor = 0.78f;
    outMesh->materials.push_back(mat);
    return !outMesh->meshlets.empty();
}

bool GenerateAlpineGrassClump(uint32_t seed, uint32_t bladeCount, float heightU,
                              RawMeshAsset* outMesh) {
    if (!outMesh || bladeCount == 0u || heightU < 4.0f) return false;
    Rng rng(seed ^ 0x6AA55C10u);
    const uint32_t blades = std::clamp(bladeCount, 3u, 32u);
    constexpr uint32_t kSegs = 4u;

    std::vector<RawMeshVertex> verts;
    std::vector<uint32_t> idx;
    verts.reserve(size_t(blades) * (kSegs * 2 + 1));
    idx.reserve(size_t(blades) * (kSegs * 6 - 3));

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
            const Vec3 tang = Norm(dir * (2.0f * lean * t) + Vec3{0.0f, 0.0f, h});
            const Vec3 nrm = Norm(Cross(side, tang));
            const float uvY = 1.0f - t;  // 1 at anchored root, 0 at flexible tip (RawMesh.hlsl convention)
            const Vec3 pL = ctr - side * (0.5f * w);
            const Vec3 pR = ctr + side * (0.5f * w);
            verts.push_back({{pL.x, pL.y, pL.z}, {nrm.x, nrm.y, nrm.z}, {0.0f, uvY}});
            verts.push_back({{pR.x, pR.y, pR.z}, {nrm.x, nrm.y, nrm.z}, {1.0f, uvY}});
        }
        // Tapered pointed blade tip
        const Vec3 tip = root + dir * lean + Vec3{0.0f, 0.0f, h};
        const Vec3 tipN = Norm(dir * 0.35f + Vec3{0.0f, 0.0f, 0.94f});
        const uint32_t vTip = static_cast<uint32_t>(verts.size());
        verts.push_back({{tip.x, tip.y, tip.z}, {tipN.x, tipN.y, tipN.z}, {0.5f, 0.0f}});

        for (uint32_t s = 0; s + 1 < kSegs; ++s) {
            const uint32_t a = v0 + s * 2u;
            const uint32_t c = v0 + (s + 1u) * 2u;
            idx.push_back(a + 0); idx.push_back(a + 1); idx.push_back(c + 1);
            idx.push_back(a + 0); idx.push_back(c + 1); idx.push_back(c + 0);
        }
        const uint32_t topBase = v0 + (kSegs - 1u) * 2u;
        idx.push_back(topBase + 0); idx.push_back(topBase + 1); idx.push_back(vTip);
    }

    FinalizeAsset("proc_alpine_grass", verts, idx, outMesh);
    RawPbrMaterial mat{};
    mat.name = "proc_alpine_grass_mat";
    mat.baseColorFactor[0] = 0.16f; mat.baseColorFactor[1] = 0.30f;
    mat.baseColorFactor[2] = 0.11f; mat.baseColorFactor[3] = 1.0f;
    mat.roughnessFactor = 0.68f;
    outMesh->materials.push_back(mat);
    return !outMesh->meshlets.empty();
}

bool GenerateOceanTileMesh(uint32_t gridN, float spanU, RawMeshAsset* outMesh) {
    if (!outMesh || gridN < 2u || spanU < 16.0f) return false;
    const uint32_t n = std::clamp(gridN, 2u, 64u);
    const uint32_t stride = n + 1u;
    std::vector<RawMeshVertex> verts;
    verts.reserve(size_t(stride) * stride);
    std::vector<uint32_t> idx;
    idx.reserve(size_t(n) * n * 6u);

    const float half = spanU * 0.5f;
    for (uint32_t y = 0; y <= n; ++y) {
        const float v = float(y) / float(n);
        for (uint32_t x = 0; x <= n; ++x) {
            const float u = float(x) / float(n);
            verts.push_back({{-half + u * spanU, -half + v * spanU, 0.0f}, {0.0f, 0.0f, 1.0f}, {u, v}});
        }
    }
    for (uint32_t y = 0; y < n; ++y) {
        for (uint32_t x = 0; x < n; ++x) {
            const uint32_t a = y * stride + x;
            const uint32_t b = a + 1u;
            const uint32_t c = a + stride;
            const uint32_t d = c + 1u;
            idx.push_back(a); idx.push_back(b); idx.push_back(d);
            idx.push_back(a); idx.push_back(d); idx.push_back(c);
        }
    }
    FinalizeAsset("proc_ocean_tile", verts, idx, outMesh);
    return !outMesh->meshlets.empty();
}

bool SynthesizeProceduralPbrTexture(ProceduralTexKind kind, uint32_t width, uint32_t height,
                                    uint32_t seed, RawTextureAsset* outTex) {
    if (!outTex || width < 8u || height < 8u || width > 2048u || height > 2048u) return false;
    outTex->width = width;
    outTex->height = height;
    outTex->rgba8.resize(size_t(width) * height * 4u);
    static const char* kNames[5] = {
        "proc_bark_furrow", "proc_pine_needles", "proc_granite_strata",
        "proc_snow_sastrugi", "proc_cellular_foam"
    };
    const uint32_t ki = std::min<uint32_t>(static_cast<uint32_t>(kind), 4u);
    outTex->name = kNames[ki];

    for (uint32_t y = 0; y < height; ++y) {
        const float v = (float(y) + 0.5f) / float(height);
        for (uint32_t x = 0; x < width; ++x) {
            const float u = (float(x) + 0.5f) / float(width);
            float r = 0.5f, g = 0.5f, b = 0.5f, a = 0.5f;
            if (kind == ProceduralTexKind::BarkFurrow) {
                // Anisotropic vertical conifer bark plates + dark fissure cracks
                const float warp = (TileFbm(u, v, 4, 4, seed + 17u, 3) - 0.5f) * 0.08f;
                const float ridge = TileFbm(u + warp, v, 24, 4, seed + 31u, 4);
                const float crack = 1.0f - std::clamp(std::abs(ridge - 0.48f) * 7.5f, 0.0f, 1.0f);
                const float plate = std::clamp(ridge * 1.15f - crack * 0.42f, 0.05f, 0.95f);
                r = 0.24f * plate + 0.05f;
                g = 0.17f * plate + 0.035f;
                b = 0.11f * plate + 0.025f;
                a = plate;
            } else if (kind == ProceduralTexKind::PineNeedles) {
                // Layered boreal needle cluster micro-albedo + alpha/height density
                const float n1 = TileFbm(u, v, 12, 12, seed + 53u, 4);
                const float fine = TileValueNoise(u, v, 32, 8, seed + 71u);
                const float dens = std::clamp(n1 * 0.65f + fine * 0.35f, 0.0f, 1.0f);
                r = 0.08f + 0.10f * dens;
                g = 0.16f + 0.18f * dens;
                b = 0.07f + 0.08f * dens;
                a = dens;
            } else if (kind == ProceduralTexKind::GraniteStrata) {
                // Sedimentary horizontal bands + crystalline mineral grain + fracture edge
                const float band = 0.5f + 0.5f * std::sin((v + TileFbm(u, v, 4, 4, seed + 97u, 3) * 0.14f) * kTwoPi * 4.0f);
                const float grain = TileFbm(u, v, 16, 16, seed + 131u, 4);
                const float edge = TileWorleyEdge(u, v, 6, seed + 163u);
                const float val = std::clamp(0.28f + 0.26f * band + 0.24f * grain - (1.0f - std::min(edge * 4.0f, 1.0f)) * 0.18f, 0.05f, 0.92f);
                r = val * 0.96f; g = val * 0.98f; b = val * 1.03f; a = val;
            } else if (kind == ProceduralTexKind::SnowSastrugi) {
                // Wind-sculpted sastrugi snow ripples + pinpoint crystalline sparkle mask in A
                const float dune = TileFbm(u, v, 4, 12, seed + 211u, 4);
                const float glint = TileValueNoise(u, v, 48, 48, seed + 257u);
                const float h = std::clamp(0.65f + 0.32f * (dune - 0.5f), 0.25f, 0.98f);
                r = 0.72f + 0.18f * h;
                g = 0.78f + 0.16f * h;
                b = 0.85f + 0.13f * h;
                a = std::clamp(h * 0.7f + (glint > 0.88f ? 0.30f : 0.0f), 0.0f, 1.0f);
            } else {
                // Open-cell dual-scale Voronoi cellular foam web (24Ronin / EasyWaterscape)
                const float w1 = TileWorleyEdge(u, v, 8, seed + 307u);
                const float w2 = TileWorleyEdge(u + 0.37f, v + 0.19f, 16, seed + 349u);
                const float lace1 = 1.0f - std::clamp((w1 - 0.02f) / 0.22f, 0.0f, 1.0f);
                const float lace2 = 1.0f - std::clamp((w2 - 0.015f) / 0.18f, 0.0f, 1.0f);
                const float foam = std::clamp(std::max(lace1, lace2 * 0.72f), 0.0f, 1.0f);
                r = 0.92f * foam; g = 0.96f * foam; b = 1.00f * foam; a = foam;
            }
            const size_t off = (size_t(y) * width + x) * 4u;
            outTex->rgba8[off + 0] = ToU8(r);
            outTex->rgba8[off + 1] = ToU8(g);
            outTex->rgba8[off + 2] = ToU8(b);
            outTex->rgba8[off + 3] = ToU8(a);
        }
    }
    return true;
}

}  // namespace raw::bridge
