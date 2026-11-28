// RawProceduralLib.cpp: Zero-External-Asset Procedural Geometry & PBR Material Synthesis Library.
// Backed by `raw::worldgen` reusable mesh/terrain framework for live dogfooding inside RAW.
// Original work, MIT.
#include "RawProceduralLib.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace raw::bridge {
namespace {

constexpr float kTwoPi = 6.28318530717958647692f;

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

uint8_t ToU8(float v) {
    return static_cast<uint8_t>(std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f));
}

RawPbrMaterial MakeDefaultMaterial(raw::worldgen::TerrainMaterialId id) {
    RawPbrMaterial mat{};
    mat.name = std::string("proc_") + raw::worldgen::GetMaterialName(id);
    switch (id) {
        case raw::worldgen::TerrainMaterialId::SnowPack:
            mat.baseColorFactor[0] = 0.80f; mat.baseColorFactor[1] = 0.84f; mat.baseColorFactor[2] = 0.90f;
            mat.roughnessFactor = 0.62f;
            break;
        case raw::worldgen::TerrainMaterialId::GlacialIce:
            mat.baseColorFactor[0] = 0.34f; mat.baseColorFactor[1] = 0.72f; mat.baseColorFactor[2] = 0.86f;
            mat.roughnessFactor = 0.14f;
            break;
        case raw::worldgen::TerrainMaterialId::BasaltColumn:
            mat.baseColorFactor[0] = 0.14f; mat.baseColorFactor[1] = 0.15f; mat.baseColorFactor[2] = 0.18f;
            mat.roughnessFactor = 0.38f;
            break;
        case raw::worldgen::TerrainMaterialId::MagmaFissure:
            mat.baseColorFactor[0] = 0.16f; mat.baseColorFactor[1] = 0.12f; mat.baseColorFactor[2] = 0.11f;
            mat.roughnessFactor = 0.30f;
            mat.emissiveFactor[0] = 2.40f; mat.emissiveFactor[1] = 0.62f; mat.emissiveFactor[2] = 0.08f;
            break;
        case raw::worldgen::TerrainMaterialId::MossMeadow:
            mat.baseColorFactor[0] = 0.14f; mat.baseColorFactor[1] = 0.28f; mat.baseColorFactor[2] = 0.12f;
            mat.roughnessFactor = 0.78f;
            break;
        case raw::worldgen::TerrainMaterialId::RiverPebbleSand:
            mat.baseColorFactor[0] = 0.34f; mat.baseColorFactor[1] = 0.32f; mat.baseColorFactor[2] = 0.29f;
            mat.roughnessFactor = 0.48f;
            break;
        case raw::worldgen::TerrainMaterialId::ArcaneCrystal:
            mat.baseColorFactor[0] = 0.46f; mat.baseColorFactor[1] = 0.36f; mat.baseColorFactor[2] = 0.92f;
            mat.roughnessFactor = 0.10f;
            mat.emissiveFactor[0] = 0.45f; mat.emissiveFactor[1] = 0.32f; mat.emissiveFactor[2] = 1.15f;
            break;
        case raw::worldgen::TerrainMaterialId::RockGranite:
        default:
            mat.baseColorFactor[0] = 0.28f; mat.baseColorFactor[1] = 0.29f; mat.baseColorFactor[2] = 0.31f;
            mat.roughnessFactor = 0.78f;
            break;
    }
    mat.baseColorFactor[3] = 1.0f;
    return mat;
}

}  // namespace

bool ConvertWorldGenMeshToRawMeshAsset(const raw::worldgen::ProceduralMesh& src, RawMeshAsset* outMesh) {
    if (!outMesh || src.vertices.empty() || src.indices.empty() || src.meshlets.empty()) return false;

    // Check whether src meshlets already fit RawMeshHost's 64v/64t limit; if so, copy directly!
    bool fitsDirect = true;
    for (const auto& ml : src.meshlets) {
        if (ml.vertexCount > 64u || ml.triangleCount > 64u) {
            fitsDirect = false;
            break;
        }
    }

    outMesh->name = src.name;
    outMesh->fromGlb = false;
    outMesh->vertices.clear();
    outMesh->indices.clear();
    outMesh->meshlets.clear();
    outMesh->materials.clear();
    outMesh->materials.push_back(MakeDefaultMaterial(src.primaryMaterial));

    if (fitsDirect) {
        outMesh->vertices.resize(src.vertices.size());
        for (size_t i = 0; i < src.vertices.size(); ++i) {
            std::memcpy(outMesh->vertices[i].pos, src.vertices[i].pos, 12u);
            std::memcpy(outMesh->vertices[i].normal, src.vertices[i].normal, 12u);
            std::memcpy(outMesh->vertices[i].uv, src.vertices[i].uv, 8u);
        }
        outMesh->indices = src.indices;
        outMesh->meshlets.resize(src.meshlets.size());
        for (size_t i = 0; i < src.meshlets.size(); ++i) {
            outMesh->meshlets[i].vertexOffset = src.meshlets[i].vertexOffset;
            outMesh->meshlets[i].triangleOffset = src.meshlets[i].triangleOffset;
            outMesh->meshlets[i].vertexCount = src.meshlets[i].vertexCount;
            outMesh->meshlets[i].triangleCount = src.meshlets[i].triangleCount;
            std::memcpy(outMesh->meshlets[i].boundSphere, src.meshlets[i].boundSphere, 16u);
            std::memcpy(outMesh->meshlets[i].normalCone, src.meshlets[i].normalCone, 16u);
        }
        return !outMesh->meshlets.empty();
    }

    // Re-meshletize from 64v/126t into 64v/64t for RawMeshHost compatibility.
    std::vector<RawMeshVertex> flatV(src.vertices.size());
    for (size_t i = 0; i < src.vertices.size(); ++i) {
        std::memcpy(flatV[i].pos, src.vertices[i].pos, 12u);
        std::memcpy(flatV[i].normal, src.vertices[i].normal, 12u);
        std::memcpy(flatV[i].uv, src.vertices[i].uv, 8u);
    }
    std::vector<uint32_t> flatI;
    flatI.reserve(src.indices.size());
    for (const auto& ml : src.meshlets) {
        for (uint32_t t = 0; t < ml.triangleCount; ++t) {
            const size_t base = size_t(ml.triangleOffset) + size_t(t) * 3u;
            if (base + 2u >= src.indices.size()) break;
            flatI.push_back(ml.vertexOffset + src.indices[base + 0u]);
            flatI.push_back(ml.vertexOffset + src.indices[base + 1u]);
            flatI.push_back(ml.vertexOffset + src.indices[base + 2u]);
        }
    }
    BuildRawMeshlets(flatV, flatI, &outMesh->vertices, &outMesh->indices, &outMesh->meshlets);
    return !outMesh->meshlets.empty();
}

bool GenerateNordicSpruceTree(uint32_t seed, float heightU, float crownRadiusU, float snowLoad01,
                              RawMeshAsset* outMesh) {
    raw::worldgen::ProceduralMesh pm{};
    if (!raw::worldgen::BuildMultiTierConifer(seed, heightU, crownRadiusU, snowLoad01, 64u, &pm)) return false;
    pm.name = "proc_nordic_spruce";
    return ConvertWorldGenMeshToRawMeshAsset(pm, outMesh);
}

bool GenerateNordicBoulder(uint32_t seed, float radiusU, float strataTilt, uint32_t fracturePlanes,
                           RawMeshAsset* outMesh) {
    raw::worldgen::ProceduralMesh pm{};
    if (!raw::worldgen::BuildAlpineBoulder(seed, radiusU, strataTilt, fracturePlanes, 64u, &pm)) return false;
    pm.name = "proc_nordic_boulder";
    return ConvertWorldGenMeshToRawMeshAsset(pm, outMesh);
}

bool GenerateAlpineGrassClump(uint32_t seed, uint32_t bladeCount, float heightU,
                              RawMeshAsset* outMesh) {
    raw::worldgen::ProceduralMesh pm{};
    if (!raw::worldgen::BuildAlpineGrassClump(seed, bladeCount, heightU, 64u, &pm)) return false;
    pm.name = "proc_alpine_grass";
    return ConvertWorldGenMeshToRawMeshAsset(pm, outMesh);
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
    outMesh->name = "proc_ocean_tile";
    outMesh->fromGlb = false;
    outMesh->materials.clear();
    BuildRawMeshlets(verts, idx, &outMesh->vertices, &outMesh->indices, &outMesh->meshlets);
    return !outMesh->meshlets.empty();
}

bool GenerateGlacialIceArch(uint32_t seed, float spanU, float heightU, float thicknessU,
                            RawMeshAsset* outMesh) {
    raw::worldgen::ProceduralMesh pm{};
    if (!raw::worldgen::BuildGlacialIceArch(seed, spanU, heightU, thicknessU, 64u, &pm)) return false;
    return ConvertWorldGenMeshToRawMeshAsset(pm, outMesh);
}

bool GenerateNordicSnowDrift(uint32_t seed, float lengthU, float widthU, float heightU,
                             RawMeshAsset* outMesh) {
    raw::worldgen::ProceduralMesh pm{};
    if (!raw::worldgen::BuildNordicSnowDrift(seed, lengthU, widthU, heightU, 64u, &pm)) return false;
    return ConvertWorldGenMeshToRawMeshAsset(pm, outMesh);
}

bool GenerateBasaltColumnCluster(uint32_t seed, uint32_t columnCount, float radiusU, float maxHeightU,
                                 RawMeshAsset* outMesh) {
    raw::worldgen::ProceduralMesh pm{};
    if (!raw::worldgen::BuildHexagonalBasaltCluster(seed, columnCount, radiusU, maxHeightU, 64u, &pm)) return false;
    return ConvertWorldGenMeshToRawMeshAsset(pm, outMesh);
}

bool GenerateVolcanicVent(uint32_t seed, float radiusU, float heightU, RawMeshAsset* outMesh) {
    raw::worldgen::ProceduralMesh pm{};
    if (!raw::worldgen::BuildVolcanicVent(seed, radiusU, heightU, 64u, &pm)) return false;
    return ConvertWorldGenMeshToRawMeshAsset(pm, outMesh);
}

bool GenerateCrystalMonolith(uint32_t seed, float radiusU, float heightU, uint32_t shardCount,
                             RawMeshAsset* outMesh) {
    raw::worldgen::ProceduralMesh pm{};
    if (!raw::worldgen::BuildCrystalMonolith(seed, radiusU, heightU, shardCount, 64u, &pm)) return false;
    return ConvertWorldGenMeshToRawMeshAsset(pm, outMesh);
}

bool GenerateProceduralTerrainChunk(int32_t chunkX, int32_t chunkY, uint32_t lodLevel, uint32_t seed,
                                    uint8_t neighborCoarserMask, RawMeshAsset* outMesh) {
    raw::worldgen::TerrainConfig cfg{};
    cfg.seed = seed;
    raw::worldgen::TerrainChunkData chunk{};
    if (!raw::worldgen::GenerateBiomeTerrainChunk(chunkX, chunkY, lodLevel, cfg, neighborCoarserMask, &chunk)) {
        return false;
    }
    raw::worldgen::ProceduralMesh pm{};
    if (!raw::worldgen::BuildTerrainChunkMesh(chunk, 64u, &pm)) return false;
    return ConvertWorldGenMeshToRawMeshAsset(pm, outMesh);
}

bool SynthesizeProceduralPbrTexture(ProceduralTexKind kind, uint32_t width, uint32_t height,
                                    uint32_t seed, RawTextureAsset* outTex) {
    if (!outTex || width < 8u || height < 8u || width > 2048u || height > 2048u) return false;
    outTex->width = width;
    outTex->height = height;
    outTex->rgba8.resize(size_t(width) * height * 4u);
    static const char* kNames[10] = {
        "proc_bark_furrow", "proc_pine_needles", "proc_granite_strata",
        "proc_snow_sastrugi", "proc_cellular_foam", "proc_glacial_ice_fracture",
        "proc_volcanic_basalt_magma", "proc_hexagonal_basalt", "proc_crystal_quartz_facet",
        "proc_mossy_granite_cobble"
    };
    const uint32_t ki = std::min<uint32_t>(static_cast<uint32_t>(kind), 9u);
    outTex->name = kNames[ki];

    for (uint32_t y = 0; y < height; ++y) {
        const float v = (float(y) + 0.5f) / float(height);
        for (uint32_t x = 0; x < width; ++x) {
            const float u = (float(x) + 0.5f) / float(width);
            float r = 0.5f, g = 0.5f, b = 0.5f, a = 0.5f;
            if (kind == ProceduralTexKind::BarkFurrow) {
                const float warp = (TileFbm(u, v, 4, 4, seed + 17u, 3) - 0.5f) * 0.08f;
                const float ridge = TileFbm(u + warp, v, 24, 4, seed + 31u, 4);
                const float crack = 1.0f - std::clamp(std::abs(ridge - 0.48f) * 7.5f, 0.0f, 1.0f);
                const float plate = std::clamp(ridge * 1.15f - crack * 0.42f, 0.05f, 0.95f);
                r = 0.24f * plate + 0.05f;
                g = 0.17f * plate + 0.035f;
                b = 0.11f * plate + 0.025f;
                a = plate;
            } else if (kind == ProceduralTexKind::PineNeedles) {
                const float n1 = TileFbm(u, v, 12, 12, seed + 53u, 4);
                const float fine = TileValueNoise(u, v, 32, 8, seed + 71u);
                const float dens = std::clamp(n1 * 0.65f + fine * 0.35f, 0.0f, 1.0f);
                r = 0.08f + 0.10f * dens;
                g = 0.16f + 0.18f * dens;
                b = 0.07f + 0.08f * dens;
                a = dens;
            } else if (kind == ProceduralTexKind::GraniteStrata) {
                const float band = 0.5f + 0.5f * std::sin((v + TileFbm(u, v, 4, 4, seed + 97u, 3) * 0.14f) * kTwoPi * 4.0f);
                const float grain = TileFbm(u, v, 16, 16, seed + 131u, 4);
                const float edge = TileWorleyEdge(u, v, 6, seed + 163u);
                const float val = std::clamp(0.28f + 0.26f * band + 0.24f * grain - (1.0f - std::min(edge * 4.0f, 1.0f)) * 0.18f, 0.05f, 0.92f);
                r = val * 0.96f; g = val * 0.98f; b = val * 1.03f; a = val;
            } else if (kind == ProceduralTexKind::SnowSastrugi) {
                const float dune = TileFbm(u, v, 4, 12, seed + 211u, 4);
                const float glint = TileValueNoise(u, v, 48, 48, seed + 257u);
                const float h = std::clamp(0.65f + 0.32f * (dune - 0.5f), 0.25f, 0.98f);
                r = 0.72f + 0.18f * h;
                g = 0.78f + 0.16f * h;
                b = 0.85f + 0.13f * h;
                a = std::clamp(h * 0.7f + (glint > 0.88f ? 0.30f : 0.0f), 0.0f, 1.0f);
            } else if (kind == ProceduralTexKind::CellularFoam) {
                const float w1 = TileWorleyEdge(u, v, 8, seed + 307u);
                const float w2 = TileWorleyEdge(u + 0.37f, v + 0.19f, 16, seed + 349u);
                const float lace1 = 1.0f - std::clamp((w1 - 0.02f) / 0.22f, 0.0f, 1.0f);
                const float lace2 = 1.0f - std::clamp((w2 - 0.015f) / 0.18f, 0.0f, 1.0f);
                const float foam = std::clamp(std::max(lace1, lace2 * 0.72f), 0.0f, 1.0f);
                r = 0.92f * foam; g = 0.96f * foam; b = 1.00f * foam; a = foam;
            } else if (kind == ProceduralTexKind::GlacialIceFracture) {
                // Deep glacial turquoise absorption + crisp internal Voronoi fracture veins (zero pulse)
                const float crack1 = 1.0f - std::clamp(TileWorleyEdge(u, v, 6, seed + 401u) * 6.5f, 0.0f, 1.0f);
                const float crack2 = 1.0f - std::clamp(TileWorleyEdge(u + 0.23f, v + 0.41f, 12, seed + 433u) * 8.0f, 0.0f, 1.0f);
                const float depth = TileFbm(u, v, 6, 6, seed + 457u, 4);
                const float vein = std::clamp(crack1 * 0.85f + crack2 * 0.45f, 0.0f, 1.0f);
                r = std::clamp(0.14f + 0.26f * depth + 0.56f * vein, 0.0f, 1.0f);
                g = std::clamp(0.46f + 0.28f * depth + 0.34f * vein, 0.0f, 1.0f);
                b = std::clamp(0.62f + 0.26f * depth + 0.24f * vein, 0.0f, 1.0f);
                a = std::clamp(0.18f + 0.65f * vein, 0.08f, 0.95f);
            } else if (kind == ProceduralTexKind::VolcanicBasaltMagma) {
                // Cooling dark basalt plates with incandescent 2400K blackbody magma fissures
                const float edge = TileWorleyEdge(u, v, 5, seed + 503u);
                const float warp = TileFbm(u, v, 8, 8, seed + 541u, 3);
                const float hot = 1.0f - std::clamp((edge + (warp - 0.5f) * 0.06f) * 5.2f, 0.0f, 1.0f);
                const float crust = 0.09f + 0.11f * warp;
                r = std::clamp(crust + hot * 0.92f, 0.0f, 1.0f);
                g = std::clamp(crust * 0.92f + hot * hot * 0.58f, 0.0f, 1.0f);
                b = std::clamp(crust * 0.95f + hot * hot * hot * 0.18f, 0.0f, 1.0f);
                a = hot; // Emissive heat mask in alpha
            } else if (kind == ProceduralTexKind::HexagonalBasalt) {
                // Dark glassy volcanic obsidian-basalt column faces with crisp beveled joints
                const float joint = TileWorleyEdge(u, v, 7, seed + 601u);
                const float grain = TileFbm(u, v, 14, 14, seed + 641u, 4);
                const float bevel = std::clamp(joint * 5.5f, 0.0f, 1.0f);
                const float val = (0.11f + 0.14f * grain) * (0.45f + 0.55f * bevel);
                r = val * 0.94f; g = val * 0.98f; b = val * 1.08f;
                a = std::clamp(0.28f + 0.45f * (1.0f - bevel), 0.10f, 0.92f);
            } else if (kind == ProceduralTexKind::CrystalQuartzFacet) {
                // Sharp Voronoi crystal facet planes with amethyst/cyan refractive dispersion tint
                const float fEdge = TileWorleyEdge(u, v, 6, seed + 701u);
                const float sheen = TileFbm(u, v, 10, 10, seed + 739u, 3);
                const float bevelGlint = 1.0f - std::clamp(fEdge * 7.5f, 0.0f, 1.0f);
                r = std::clamp(0.32f + 0.34f * sheen + 0.36f * bevelGlint, 0.0f, 1.0f);
                g = std::clamp(0.26f + 0.30f * (1.0f - sheen) + 0.40f * bevelGlint, 0.0f, 1.0f);
                b = std::clamp(0.68f + 0.24f * sheen + 0.18f * bevelGlint, 0.0f, 1.0f);
                a = std::clamp(0.12f + 0.72f * bevelGlint, 0.05f, 1.0f);
            } else {
                // MossyGraniteCobble: wet river cobblestone + boreal moss crevices
                const float cobble = TileWorleyEdge(u, v, 9, seed + 809u);
                const float moss = TileFbm(u, v, 10, 10, seed + 853u, 4);
                const float crevice = 1.0f - std::clamp(cobble * 4.2f, 0.0f, 1.0f);
                const float mossW = std::clamp(crevice * 0.85f + (moss - 0.42f) * 0.75f, 0.0f, 1.0f);
                const float stone = 0.24f + 0.24f * moss;
                r = stone * (1.0f - mossW) + (0.12f + 0.08f * moss) * mossW;
                g = stone * 1.02f * (1.0f - mossW) + (0.26f + 0.14f * moss) * mossW;
                b = stone * 1.04f * (1.0f - mossW) + (0.10f + 0.06f * moss) * mossW;
                a = std::clamp(1.0f - crevice * 0.7f, 0.10f, 1.0f);
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
