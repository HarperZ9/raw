// RawAssetScaffold.cpp: Handcrafted Asset Authoring Scaffolding, Quality Gates & Clean-Room BC7/RGBA8 Cooker.
// Original work, MIT.
#include "RawAssetScaffold.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "RawOverrideRegistry.h"

namespace raw::assets {
namespace {

constexpr uint64_t kFnvOffset = 14695981039346656037ull;
constexpr uint64_t kFnvPrime  = 1099511628211ull;

constexpr int kBc7Weights4[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};

inline bool IsPowerOfTwo(uint32_t v) {
    return v != 0u && (v & (v - 1u)) == 0u;
}

inline float SrgbToLinear(float c) {
    return (c <= 0.04045f) ? (c / 12.92f) : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

inline bool IsFinite3(const float v[3]) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

inline float Len3(const float v[3]) {
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

inline float Dot3(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

uint64_t Fnv1aBytes(const uint8_t* data, size_t n) {
    uint64_t h = kFnvOffset;
    for (size_t i = 0; i < n; ++i) {
        h ^= static_cast<uint64_t>(data[i]);
        h *= kFnvPrime;
    }
    return h;
}

uint64_t PackEdgeKey(uint32_t a, uint32_t b) {
    return (static_cast<uint64_t>(a) << 32u) | static_cast<uint64_t>(b);
}

uint64_t PackUndirectedEdgeKey(uint32_t a, uint32_t b) {
    return (a < b) ? PackEdgeKey(a, b) : PackEdgeKey(b, a);
}

std::string FmtFloat(float v) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.6g", static_cast<double>(v));
    return std::string(buf);
}

struct BitWriter {
    uint8_t b[16]{};
    int     at = 0;
    void Put(uint32_t v, int n) {
        for (int i = 0; i < n; ++i, ++at) {
            if ((v >> i) & 1u) {
                b[at >> 3] |= static_cast<uint8_t>(1u << (at & 7));
            }
        }
    }
};

struct BitReader {
    const uint8_t* b = nullptr;
    int            at = 0;
    uint32_t Get(int n) {
        uint32_t v = 0u;
        for (int i = 0; i < n; ++i, ++at) {
            if ((b[at >> 3] >> (at & 7)) & 1u) {
                v |= (1u << i);
            }
        }
        return v;
    }
};

inline int Bc7Interp(int e0, int e1, int w) {
    return ((64 - w) * e0 + w * e1 + 32) >> 6;
}

void DownsampleBox2x2(const std::vector<uint8_t>& src, uint32_t sw, uint32_t sh,
                      std::vector<uint8_t>* outDst, uint32_t* outDw, uint32_t* outDh) {
    const uint32_t dw = std::max(1u, sw >> 1u);
    const uint32_t dh = std::max(1u, sh >> 1u);
    outDst->resize(static_cast<size_t>(dw) * dh * 4u);
    for (uint32_t y = 0; y < dh; ++y) {
        const uint32_t sy0 = std::min(sh - 1u, y * 2u);
        const uint32_t sy1 = std::min(sh - 1u, y * 2u + 1u);
        for (uint32_t x = 0; x < dw; ++x) {
            const uint32_t sx0 = std::min(sw - 1u, x * 2u);
            const uint32_t sx1 = std::min(sw - 1u, x * 2u + 1u);
            const size_t o00 = (static_cast<size_t>(sy0) * sw + sx0) * 4u;
            const size_t o10 = (static_cast<size_t>(sy0) * sw + sx1) * 4u;
            const size_t o01 = (static_cast<size_t>(sy1) * sw + sx0) * 4u;
            const size_t o11 = (static_cast<size_t>(sy1) * sw + sx1) * 4u;
            const size_t od  = (static_cast<size_t>(y) * dw + x) * 4u;
            for (int c = 0; c < 4; ++c) {
                const uint32_t sum = src[o00 + c] + src[o10 + c] + src[o01 + c] + src[o11 + c] + 2u;
                (*outDst)[od + c] = static_cast<uint8_t>(sum >> 2u);
            }
        }
    }
    *outDw = dw;
    *outDh = dh;
}

}  // namespace

QualityReport ValidateHandcraftedMesh(const ScaffoldMesh& mesh, const MeshBudgetConfig& cfg) {
    QualityReport rep{};
    rep.vertexCount = static_cast<uint32_t>(mesh.vertices.size());

    if (mesh.vertices.empty()) {
        rep.AddError("MESH_EMPTY", "mesh contains zero vertices");
        return rep;
    }
    if (mesh.indices.empty() || (mesh.indices.size() % 3u) != 0u) {
        rep.AddError("MESH_BAD_INDICES", "index count must be non-zero and a multiple of 3");
        return rep;
    }

    rep.triangleCount = static_cast<uint32_t>(mesh.indices.size() / 3u);
    if (rep.triangleCount > cfg.maxTriangles) {
        rep.AddError("MESH_TRI_BUDGET", "triangle count exceeds budget");
    }

    // 1. Validate per-vertex finiteness, unit normals, orthonormal MikkTSpace tangents, UVs, and AABB/sphere
    float minP[3] = {1e30f, 1e30f, 1e30f};
    float maxP[3] = {-1e30f, -1e30f, -1e30f};
    bool badVertexFinite = false;
    bool badNormal = false;
    bool badTangent = false;
    bool badUv = false;

    for (const auto& v : mesh.vertices) {
        if (!IsFinite3(v.pos) || !IsFinite3(v.normal) || !IsFinite3(v.tangent) ||
            !std::isfinite(v.tangent[3]) || !std::isfinite(v.uv[0]) || !std::isfinite(v.uv[1])) {
            badVertexFinite = true;
            break;
        }
        for (int k = 0; k < 3; ++k) {
            minP[k] = std::min(minP[k], v.pos[k]);
            maxP[k] = std::max(maxP[k], v.pos[k]);
        }
        const float nLen = Len3(v.normal);
        const float nErr = std::fabs(nLen - 1.0f);
        rep.worstNormalLenErr = std::max(rep.worstNormalLenErr, nErr);
        if (nErr > cfg.normalLenTol) badNormal = true;

        const float tLen = Len3(v.tangent);
        const float ntDot = std::fabs(Dot3(v.normal, v.tangent));
        const float handAbs = std::fabs(std::fabs(v.tangent[3]) - 1.0f);
        if (std::fabs(tLen - 1.0f) > cfg.normalLenTol || ntDot > cfg.tangentOrthoTol || handAbs > 0.05f) {
            badTangent = true;
        }
        if (v.uv[0] < cfg.uvMinBound || v.uv[0] > cfg.uvMaxBound ||
            v.uv[1] < cfg.uvMinBound || v.uv[1] > cfg.uvMaxBound) {
            badUv = true;
        }
    }

    if (badVertexFinite) {
        rep.AddError("MESH_NON_FINITE", "vertex buffer contains NaN or Inf values");
        return rep;
    }
    if (badNormal) rep.AddError("MESH_NON_UNIT_NORMAL", "one or more vertex normals are not unit-length");
    if (badTangent) rep.AddError("MESH_BAD_TANGENT_BASIS", "tangent basis fails unit-length, orthogonality, or handedness (+/-1)");
    if (badUv) rep.AddError("MESH_UV_OUT_OF_BOUNDS", "UV coordinates exceed configured bounds");

    const float dx = maxP[0] - minP[0];
    const float dy = maxP[1] - minP[1];
    const float dz = maxP[2] - minP[2];
    rep.boundSphereRadius = 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz);

    // 2. Triangle bounds, area degeneracy, directed winding & manifoldness edge checks
    std::unordered_map<uint64_t, uint32_t> directedEdgeCounts;
    std::unordered_map<uint64_t, uint32_t> undirectedEdgeCounts;
    directedEdgeCounts.reserve(mesh.indices.size());
    undirectedEdgeCounts.reserve(mesh.indices.size());

    // Simulate greedy 64v/64t RAW-MESH meshlet packing to validate cluster budget
    std::unordered_set<uint32_t> curMeshletVerts;
    uint32_t curMeshletTris = 0u;
    uint32_t meshlets = 0u;

    for (size_t i = 0; i < mesh.indices.size(); i += 3u) {
        const uint32_t i0 = mesh.indices[i + 0u];
        const uint32_t i1 = mesh.indices[i + 1u];
        const uint32_t i2 = mesh.indices[i + 2u];
        if (i0 >= rep.vertexCount || i1 >= rep.vertexCount || i2 >= rep.vertexCount) {
            rep.AddError("MESH_INDEX_OOB", "triangle index out of vertex buffer bounds");
            return rep;
        }
        if (i0 == i1 || i1 == i2 || i2 == i0) {
            ++rep.degenerateTriangles;
            continue;
        }

        const auto& p0 = mesh.vertices[i0].pos;
        const auto& p1 = mesh.vertices[i1].pos;
        const auto& p2 = mesh.vertices[i2].pos;
        const float e1[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
        const float e2[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
        const float cx = e1[1] * e2[2] - e1[2] * e2[1];
        const float cy = e1[2] * e2[0] - e1[0] * e2[2];
        const float cz = e1[0] * e2[1] - e1[1] * e2[0];
        const float area2 = std::sqrt(cx * cx + cy * cy + cz * cz);
        if (area2 < 1.0e-7f) {
            ++rep.degenerateTriangles;
            continue;
        }

        const uint32_t eA[3] = {i0, i1, i2};
        const uint32_t eB[3] = {i1, i2, i0};
        for (int e = 0; e < 3; ++e) {
            ++directedEdgeCounts[PackEdgeKey(eA[e], eB[e])];
            ++undirectedEdgeCounts[PackUndirectedEdgeKey(eA[e], eB[e])];
        }

        // Pack into 64v / 64t meshlet
        uint32_t addVerts = 0u;
        if (curMeshletVerts.find(i0) == curMeshletVerts.end()) ++addVerts;
        if (curMeshletVerts.find(i1) == curMeshletVerts.end()) ++addVerts;
        if (curMeshletVerts.find(i2) == curMeshletVerts.end()) ++addVerts;
        if (curMeshletTris >= 64u || (curMeshletVerts.size() + addVerts) > 64u) {
            ++meshlets;
            curMeshletVerts.clear();
            curMeshletTris = 0u;
        }
        curMeshletVerts.insert(i0);
        curMeshletVerts.insert(i1);
        curMeshletVerts.insert(i2);
        ++curMeshletTris;
    }
    if (curMeshletTris > 0u) ++meshlets;
    rep.meshletCount = meshlets;

    for (const auto& kv : undirectedEdgeCounts) {
        if (kv.second > 2u) ++rep.nonManifoldEdges;
    }
    for (const auto& kv : directedEdgeCounts) {
        if (kv.second > 1u) ++rep.invertedWindingEdges;
    }

    if (rep.degenerateTriangles > 0u) {
        rep.AddError("MESH_DEGENERATE_TRIS", "mesh contains zero-area or degenerate triangles");
    }
    if (cfg.requireManifold && rep.nonManifoldEdges > 0u) {
        rep.AddError("MESH_NON_MANIFOLD", "mesh contains non-manifold edges shared by >2 triangles");
    }
    if (cfg.requireManifold && rep.invertedWindingEdges > 0u) {
        rep.AddError("MESH_INCONSISTENT_WINDING", "adjacent triangles share a directed edge with inverted winding");
    }
    if (rep.meshletCount > cfg.maxMeshlets) {
        rep.AddError("MESH_MESHLET_BUDGET", "RAW-MESH 64v/64t meshlet count exceeds budget");
    }

    return rep;
}

QualityReport ValidatePbrTextureMap(uint32_t width, uint32_t height, const std::vector<uint8_t>& rgba8,
                                    TextureRole role, const TextureGateConfig& cfg) {
    QualityReport rep{};
    if (width < cfg.minDim || height < cfg.minDim || width > cfg.maxDim || height > cfg.maxDim) {
        rep.AddError("TEX_BAD_DIMENSIONS", "texture dimensions outside allowed bounds");
        return rep;
    }
    if (cfg.requirePowerOfTwo && (!IsPowerOfTwo(width) || !IsPowerOfTwo(height))) {
        rep.AddError("TEX_NON_POT", "texture dimensions must be power-of-two for mipchain/BC7 cooking");
        return rep;
    }
    const size_t expectedBytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    if (rgba8.size() != expectedBytes) {
        rep.AddError("TEX_SIZE_MISMATCH", "RGBA8 byte size does not match width * height * 4");
        return rep;
    }

    const size_t pxCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    float minL = 1.0e9f, maxL = -1.0e9f;
    uint32_t overBrightCount = 0u;
    uint32_t badNormalCount = 0u;
    uint32_t zeroRoughCount = 0u;

    for (size_t i = 0; i < pxCount; ++i) {
        const size_t off = i * 4u;
        const float r = static_cast<float>(rgba8[off + 0]) * (1.0f / 255.0f);
        const float g = static_cast<float>(rgba8[off + 1]) * (1.0f / 255.0f);
        const float b = static_cast<float>(rgba8[off + 2]) * (1.0f / 255.0f);

        if (role == TextureRole::AlbedoSrgb || role == TextureRole::AlbedoLinear) {
            const float lr = (role == TextureRole::AlbedoSrgb) ? SrgbToLinear(r) : r;
            const float lg = (role == TextureRole::AlbedoSrgb) ? SrgbToLinear(g) : g;
            const float lb = (role == TextureRole::AlbedoSrgb) ? SrgbToLinear(b) : b;
            const float luma = 0.2126f * lr + 0.7152f * lg + 0.0722f * lb;
            minL = std::min(minL, luma);
            maxL = std::max(maxL, luma);
            if (luma > cfg.maxDielectricAlbedo) ++overBrightCount;
        } else if (role == TextureRole::TangentNormal) {
            const float nx = r * 2.0f - 1.0f;
            const float ny = g * 2.0f - 1.0f;
            const float nz = b * 2.0f - 1.0f;
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            const float err = std::fabs(len - 1.0f);
            rep.worstNormalLenErr = std::max(rep.worstNormalLenErr, err);
            if (err > cfg.normalLenTol || nz <= 0.0f) ++badNormalCount;
        } else if (role == TextureRole::RmaohLinear) {
            if (rgba8[off + 0] < 3u) ++zeroRoughCount;
            minL = std::min(minL, r);
            maxL = std::max(maxL, r);
        } else {
            const float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
            minL = std::min(minL, luma);
            maxL = std::max(maxL, luma);
        }
    }

    rep.minLinearLuma = (minL < 1.0e8f) ? minL : 0.0f;
    rep.maxLinearLuma = (maxL > -1.0e8f) ? maxL : 0.0f;

    if (role == TextureRole::AlbedoSrgb || role == TextureRole::AlbedoLinear) {
        if (overBrightCount > pxCount / 20u) {
            rep.AddError("TEX_ALBEDO_ENERGY_VIOLATION",
                         "more than 5% of albedo pixels exceed physical linear reflectance cap");
        }
        if (rep.maxLinearLuma < cfg.minDielectricAlbedo) {
            rep.AddWarning("TEX_ALBEDO_TOO_DARK", "albedo map is nearly crushed pitch black");
        }
    } else if (role == TextureRole::TangentNormal) {
        if (badNormalCount > 0u) {
            rep.AddError("TEX_NORMAL_DEGENERATE",
                         "tangent normal map contains non-unit or inverted (nz <= 0) normals");
        }
    } else if (role == TextureRole::RmaohLinear) {
        if (zeroRoughCount > pxCount / 4u) {
            rep.AddWarning("TEX_ROUGHNESS_NEAR_ZERO", "roughness map has widespread near-zero values");
        }
    }

    return rep;
}

std::string GenerateAssetScaffoldJson(const AssetScaffoldSpec& spec) {
    const std::string normVanilla = NormalizeVirtualPath(spec.vanillaPath);
    const std::string slug = spec.assetSlug.empty() ? "custom_asset" : spec.assetSlug;
    std::ostringstream os;
    os << "{"
       << "\"schema\":\"raw.asset.scaffold.v1\","
       << "\"vanillaPath\":\"" << normVanilla << "\","
       << "\"vanillaHash64\":" << HashVirtualPath64(normVanilla) << ","
       << "\"formId\":" << spec.formId << ","
       << "\"materialPreset\":\"" << spec.materialPreset << "\","
       << "\"targets\":{"
       << "\"mesh\":\"meshes/" << slug << ".glb\","
       << "\"albedo\":\"textures/" << slug << "_albedo.png\","
       << "\"normal\":\"textures/" << slug << "_normal.png\","
       << "\"rmaoh\":\"textures/" << slug << "_rmaoh.png\","
       << "\"emissive\":\"textures/" << slug << "_emissive.png\","
       << "\"height\":\"textures/" << slug << "_height.png\","
       << "\"scaleCage\":\"cages/" << slug << "_cage.obj\""
       << "},"
       << "\"scaleCage\":{"
       << "\"aabbMin\":[" << FmtFloat(spec.aabbMin[0]) << "," << FmtFloat(spec.aabbMin[1]) << "," << FmtFloat(spec.aabbMin[2]) << "],"
       << "\"aabbMax\":[" << FmtFloat(spec.aabbMax[0]) << "," << FmtFloat(spec.aabbMax[1]) << "," << FmtFloat(spec.aabbMax[2]) << "],"
       << "\"pivot\":[" << FmtFloat(spec.pivot[0]) << "," << FmtFloat(spec.pivot[1]) << "," << FmtFloat(spec.pivot[2]) << "]"
       << "},"
       << "\"budgets\":{"
       << "\"triangleBudget\":" << spec.triangleBudget << ","
       << "\"meshletSize\":\"64v/64t\","
       << "\"texelDensityPxPerCm\":" << FmtFloat(spec.texelDensityPxPerCm)
       << "}"
       << "}";
    return os.str();
}

std::string GenerateScaleCageObj(const AssetScaffoldSpec& spec) {
    const float x0 = spec.aabbMin[0] - spec.pivot[0], x1 = spec.aabbMax[0] - spec.pivot[0];
    const float y0 = spec.aabbMin[1] - spec.pivot[1], y1 = spec.aabbMax[1] - spec.pivot[1];
    const float z0 = spec.aabbMin[2] - spec.pivot[2], z1 = spec.aabbMax[2] - spec.pivot[2];
    std::ostringstream os;
    os << "# RAW Handcrafted Scale Cage OBJ (exact vanilla AABB relative to pivot)\n"
       << "o " << (spec.assetSlug.empty() ? "scale_cage" : spec.assetSlug) << "_cage\n"
       << "v " << FmtFloat(x0) << " " << FmtFloat(y0) << " " << FmtFloat(z0) << "\n"
       << "v " << FmtFloat(x1) << " " << FmtFloat(y0) << " " << FmtFloat(z0) << "\n"
       << "v " << FmtFloat(x1) << " " << FmtFloat(y1) << " " << FmtFloat(z0) << "\n"
       << "v " << FmtFloat(x0) << " " << FmtFloat(y1) << " " << FmtFloat(z0) << "\n"
       << "v " << FmtFloat(x0) << " " << FmtFloat(y0) << " " << FmtFloat(z1) << "\n"
       << "v " << FmtFloat(x1) << " " << FmtFloat(y0) << " " << FmtFloat(z1) << "\n"
       << "v " << FmtFloat(x1) << " " << FmtFloat(y1) << " " << FmtFloat(z1) << "\n"
       << "v " << FmtFloat(x0) << " " << FmtFloat(y1) << " " << FmtFloat(z1) << "\n"
       << "f 1 4 3 2\n"
       << "f 5 6 7 8\n"
       << "f 1 2 6 5\n"
       << "f 2 3 7 6\n"
       << "f 3 4 8 7\n"
       << "f 4 1 5 8\n";
    return os.str();
}

// ============================================================================
// Clean-Room MIT BC7 Mode 6 Block Encoder & Decoder (adapted from superframe lwframe_bc7.cpp)
// ============================================================================
void EncodeBc7Mode6Block(const uint8_t px[64], uint8_t out[16]) {
    float mean[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    for (int i = 0; i < 16; ++i) {
        for (int c = 0; c < 4; ++c) mean[c] += static_cast<float>(px[i * 4 + c]) * (1.0f / 16.0f);
    }
    float cov[4][4] = {};
    for (int i = 0; i < 16; ++i) {
        for (int a = 0; a < 4; ++a) {
            const float da = static_cast<float>(px[i * 4 + a]) - mean[a];
            for (int b = 0; b < 4; ++b) {
                cov[a][b] += da * (static_cast<float>(px[i * 4 + b]) - mean[b]);
            }
        }
    }
    float ax[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    for (int it = 0; it < 6; ++it) {
        float n[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        for (int a = 0; a < 4; ++a) {
            for (int b = 0; b < 4; ++b) n[a] += cov[a][b] * ax[b];
        }
        const float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2] + n[3] * n[3]);
        if (l < 1.0e-6f) break;
        for (int a = 0; a < 4; ++a) ax[a] = n[a] / l;
    }

    float lo = 1.0e9f, hi = -1.0e9f;
    for (int i = 0; i < 16; ++i) {
        float t = 0.0f;
        for (int c = 0; c < 4; ++c) t += (static_cast<float>(px[i * 4 + c]) - mean[c]) * ax[c];
        lo = std::min(lo, t);
        hi = std::max(hi, t);
    }

    int bestErr = 1 << 30;
    uint8_t best[16]{};
    for (int p0 = 0; p0 < 2; ++p0) {
        for (int p1 = 0; p1 < 2; ++p1) {
            int e[2][4]{}, q[2][4]{};
            for (int c = 0; c < 4; ++c) {
                const float v0 = mean[c] + ax[c] * lo;
                const float v1 = mean[c] + ax[c] * hi;
                q[0][c] = std::clamp(static_cast<int>(std::lround((v0 - static_cast<float>(p0)) * 0.5f)), 0, 127);
                q[1][c] = std::clamp(static_cast<int>(std::lround((v1 - static_cast<float>(p1)) * 0.5f)), 0, 127);
                e[0][c] = (q[0][c] << 1) | p0;
                e[1][c] = (q[1][c] << 1) | p1;
            }
            int idx[16]{};
            int err = 0;
            for (int i = 0; i < 16; ++i) {
                float dd = 0.0f, tt = 0.0f;
                for (int c = 0; c < 4; ++c) {
                    const float D = static_cast<float>(e[1][c] - e[0][c]);
                    dd += D * D;
                    tt += (static_cast<float>(px[i * 4 + c]) - static_cast<float>(e[0][c])) * D;
                }
                const int k0 = dd > 0.0f ? std::clamp(static_cast<int>(std::lround(tt / dd * 15.0f)), 0, 15) : 0;
                int bi = k0, be = 1 << 30;
                for (int k = std::max(0, k0 - 1); k <= std::min(15, k0 + 1); ++k) {
                    int d = 0;
                    for (int c = 0; c < 4; ++c) {
                        const int diff = Bc7Interp(e[0][c], e[1][c], kBc7Weights4[k]) - static_cast<int>(px[i * 4 + c]);
                        d += diff * diff;
                    }
                    if (d < be) { be = d; bi = k; }
                }
                idx[i] = bi;
                err += be;
            }
            if (err >= bestErr) continue;
            bestErr = err;

            int pp0 = p0, pp1 = p1;
            if (idx[0] >= 8) {
                for (int c = 0; c < 4; ++c) std::swap(q[0][c], q[1][c]);
                std::swap(pp0, pp1);
                for (int& v : idx) v = 15 - v;
            }
            BitWriter w;
            w.Put(1u << 6u, 7);
            for (int c = 0; c < 4; ++c) {
                w.Put(static_cast<uint32_t>(q[0][c]), 7);
                w.Put(static_cast<uint32_t>(q[1][c]), 7);
            }
            w.Put(static_cast<uint32_t>(pp0), 1);
            w.Put(static_cast<uint32_t>(pp1), 1);
            w.Put(static_cast<uint32_t>(idx[0]), 3);
            for (int i = 1; i < 16; ++i) w.Put(static_cast<uint32_t>(idx[i]), 4);
            std::memcpy(best, w.b, 16);
        }
    }
    std::memcpy(out, best, 16);
}

bool DecodeBc7Mode6Block(const uint8_t block16[16], uint8_t outRgba4x4[64]) {
    BitReader r{block16, 0};
    const uint32_t mode = r.Get(7);
    if (mode != (1u << 6u)) {
        return false;
    }
    int q[2][4]{};
    for (int c = 0; c < 4; ++c) {
        q[0][c] = static_cast<int>(r.Get(7));
        q[1][c] = static_cast<int>(r.Get(7));
    }
    const int p0 = static_cast<int>(r.Get(1));
    const int p1 = static_cast<int>(r.Get(1));
    int e[2][4]{};
    for (int c = 0; c < 4; ++c) {
        e[0][c] = (q[0][c] << 1) | p0;
        e[1][c] = (q[1][c] << 1) | p1;
    }
    int idx[16]{};
    idx[0] = static_cast<int>(r.Get(3));
    for (int i = 1; i < 16; ++i) idx[i] = static_cast<int>(r.Get(4));

    for (int i = 0; i < 16; ++i) {
        const int w = kBc7Weights4[idx[i]];
        for (int c = 0; c < 4; ++c) {
            outRgba4x4[i * 4 + c] = static_cast<uint8_t>(std::clamp(Bc7Interp(e[0][c], e[1][c], w), 0, 255));
        }
    }
    return true;
}

bool CookRgba8ToBc7Mode6(uint32_t width, uint32_t height, const std::vector<uint8_t>& rgba8,
                         std::vector<uint8_t>* outBc7Blocks, std::string* outErr) {
    if (!outBc7Blocks || width == 0u || height == 0u ||
        rgba8.size() != static_cast<size_t>(width) * height * 4u) {
        if (outErr) *outErr = "invalid RGBA8 input for BC7 cooking";
        return false;
    }
    const uint32_t bw = (width + 3u) / 4u;
    const uint32_t bh = (height + 3u) / 4u;
    outBc7Blocks->resize(static_cast<size_t>(bw) * bh * 16u);

    uint8_t px[64]{};
    for (uint32_t by = 0; by < bh; ++by) {
        for (uint32_t bx = 0; bx < bw; ++bx) {
            for (uint32_t y = 0; y < 4u; ++y) {
                const uint32_t sy = std::min(height - 1u, by * 4u + y);
                for (uint32_t x = 0; x < 4u; ++x) {
                    const uint32_t sx = std::min(width - 1u, bx * 4u + x);
                    std::memcpy(&px[(y * 4u + x) * 4u],
                                &rgba8[(static_cast<size_t>(sy) * width + sx) * 4u], 4u);
                }
            }
            EncodeBc7Mode6Block(px, &(*outBc7Blocks)[(static_cast<size_t>(by) * bw + bx) * 16u]);
        }
    }
    return true;
}

bool CookTexturePackage(uint32_t width, uint32_t height, const std::vector<uint8_t>& rgba8,
                        CookedTextureFormat format, bool generateMips,
                        CookedTextureBlob* outBlob, std::string* outErr) {
    if (!outBlob || width == 0u || height == 0u ||
        rgba8.size() != static_cast<size_t>(width) * height * 4u) {
        if (outErr) *outErr = "invalid RGBA8 input for CookTexturePackage";
        return false;
    }
    outBlob->magic = 0x58545752u;
    outBlob->version = 1u;
    outBlob->format = format;
    outBlob->width = width;
    outBlob->height = height;
    outBlob->contentHash = Fnv1aBytes(rgba8.data(), rgba8.size());
    outBlob->payload.clear();

    uint32_t cw = width, ch = height, mips = 0u;
    std::vector<uint8_t> cur = rgba8;
    while (true) {
        ++mips;
        if (format == CookedTextureFormat::BC7_UNORM) {
            std::vector<uint8_t> bc7;
            if (!CookRgba8ToBc7Mode6(cw, ch, cur, &bc7, outErr)) return false;
            outBlob->payload.insert(outBlob->payload.end(), bc7.begin(), bc7.end());
        } else {
            outBlob->payload.insert(outBlob->payload.end(), cur.begin(), cur.end());
        }
        if (!generateMips || (cw == 1u && ch == 1u)) break;
        std::vector<uint8_t> next;
        uint32_t nw = 1u, nh = 1u;
        DownsampleBox2x2(cur, cw, ch, &next, &nw, &nh);
        cur.swap(next);
        cw = nw;
        ch = nh;
    }
    outBlob->mipCount = mips;
    return true;
}

std::vector<uint8_t> SerializeRawTexBlob(const CookedTextureBlob& blob) {
    constexpr size_t kHdrSize = 36u;
    std::vector<uint8_t> out;
    out.resize(kHdrSize + blob.payload.size());
    size_t p = 0u;
    auto putU32 = [&](uint32_t v) { std::memcpy(out.data() + p, &v, 4u); p += 4u; };
    auto putU64 = [&](uint64_t v) { std::memcpy(out.data() + p, &v, 8u); p += 8u; };
    putU32(blob.magic);
    putU32(blob.version);
    putU32(static_cast<uint32_t>(blob.format));
    putU32(blob.width);
    putU32(blob.height);
    putU32(blob.mipCount);
    putU64(blob.contentHash);
    putU32(static_cast<uint32_t>(blob.payload.size()));
    if (!blob.payload.empty()) {
        std::memcpy(out.data() + p, blob.payload.data(), blob.payload.size());
    }
    return out;
}

bool DeserializeRawTexBlob(const uint8_t* bytes, size_t size,
                           CookedTextureBlob* outBlob, std::string* outErr) {
    constexpr size_t kHdrSize = 36u;
    if (!bytes || !outBlob || size < kHdrSize) {
        if (outErr) *outErr = "rawtex buffer too small";
        return false;
    }
    size_t p = 0u;
    auto getU32 = [&]() -> uint32_t { uint32_t v = 0u; std::memcpy(&v, bytes + p, 4u); p += 4u; return v; };
    auto getU64 = [&]() -> uint64_t { uint64_t v = 0ull; std::memcpy(&v, bytes + p, 8u); p += 8u; return v; };
    const uint32_t magic = getU32();
    const uint32_t version = getU32();
    const uint32_t fmt = getU32();
    const uint32_t w = getU32();
    const uint32_t h = getU32();
    const uint32_t mips = getU32();
    const uint64_t hash = getU64();
    const uint32_t paySz = getU32();

    if (magic != 0x58545752u || version != 1u) {
        if (outErr) *outErr = "invalid RWTX magic or unsupported version";
        return false;
    }
    if (fmt != static_cast<uint32_t>(CookedTextureFormat::RGBA8_UNORM) &&
        fmt != static_cast<uint32_t>(CookedTextureFormat::BC7_UNORM)) {
        if (outErr) *outErr = "invalid CookedTextureFormat";
        return false;
    }
    if (w == 0u || h == 0u || mips == 0u || size != kHdrSize + paySz) {
        if (outErr) *outErr = "rawtex header/payload size mismatch";
        return false;
    }

    outBlob->magic = magic;
    outBlob->version = version;
    outBlob->format = static_cast<CookedTextureFormat>(fmt);
    outBlob->width = w;
    outBlob->height = h;
    outBlob->mipCount = mips;
    outBlob->contentHash = hash;
    outBlob->payload.assign(bytes + kHdrSize, bytes + size);
    return true;
}

}  // namespace raw::assets
