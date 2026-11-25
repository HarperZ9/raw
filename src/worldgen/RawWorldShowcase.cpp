// RawWorldShowcase.cpp: Live Frontier World Streaming Manager (LRU + VRAM Budget) & Offline 6-Biome Showcase.
// Original work, MIT.
#include "RawWorldShowcase.h"
#include "RawFloraEngine.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace raw::worldgen {
namespace {

uint64_t EstimateChunkVramBytes(const ProceduralMesh& mesh, size_t propCount) {
    // Mesh GPU buffers (meshlets + vertices + indices) + per-prop instance transform buffer (64B each)
    // + 4 KB descriptor/metadata overhead.
    return mesh.EstimateGpuBytes() + uint64_t(propCount) * 64ull + 4096ull;
}

}  // namespace

bool RawWorldShowcase::Init(const StreamConfig& cfg) {
    if (cfg.vramBudgetBytes < 65536ull || cfg.terrain.chunkSizeU < 64.0f || cfg.lod3RadiusChunks < 0) {
        m_initialized = false;
        return false;
    }
    m_cfg = cfg;
    m_chunks.clear();
    m_frame = 0u;
    m_usedVramBytes = 0u;
    m_initialized = true;
    return true;
}

void RawWorldShowcase::Reset(IGpuChunkSink* sink) {
    if (sink) {
        for (const auto& kv : m_chunks) {
            sink->OnChunkEvicted(kv.first, kv.second.gpuHandle);
        }
    }
    m_chunks.clear();
    m_usedVramBytes = 0u;
    m_frame = 0u;
}

const StreamedChunkEntry* RawWorldShowcase::FindChunk(ChunkKey key) const {
    auto it = m_chunks.find(key);
    return it == m_chunks.end() ? nullptr : &it->second;
}

bool RawWorldShowcase::IsCameraInFrontier(Vec3 camWorldPos) const {
    if (m_cfg.showcaseMode || !m_cfg.terrain.border.enabled) return true;
    const float margin = std::max(0.0f, m_cfg.frontierActivationMarginU);
    if (m_cfg.useCustomBorderBox) {
        return camWorldPos.x < (m_cfg.borderMinX + margin) || camWorldPos.x > (m_cfg.borderMaxX - margin) ||
               camWorldPos.y < (m_cfg.borderMinY + margin) || camWorldPos.y > (m_cfg.borderMaxY - margin);
    }
    return std::fabs(camWorldPos.x) > (m_cfg.terrain.border.halfExtentX - margin) ||
           std::fabs(camWorldPos.y) > (m_cfg.terrain.border.halfExtentY - margin);
}

uint32_t RawWorldShowcase::ComputeTargetLod(int32_t dx, int32_t dy) const {
    const int32_t r = std::max(std::abs(dx), std::abs(dy));
    if (r <= m_cfg.lod0RadiusChunks) return 0u;
    if (r <= m_cfg.lod1RadiusChunks) return 1u;
    if (r <= m_cfg.lod2RadiusChunks) return 2u;
    if (r <= m_cfg.lod3RadiusChunks) return 3u;
    return 4u; // Outside streaming radius
}

uint8_t RawWorldShowcase::ComputeStitchMask(int32_t dx, int32_t dy, uint32_t myLod) const {
    uint8_t mask = StitchNone;
    if (ComputeTargetLod(dx - 1, dy) > myLod && ComputeTargetLod(dx - 1, dy) <= 3u) mask |= StitchNegX;
    if (ComputeTargetLod(dx + 1, dy) > myLod && ComputeTargetLod(dx + 1, dy) <= 3u) mask |= StitchPosX;
    if (ComputeTargetLod(dx, dy - 1) > myLod && ComputeTargetLod(dx, dy - 1) <= 3u) mask |= StitchNegY;
    if (ComputeTargetLod(dx, dy + 1) > myLod && ComputeTargetLod(dx, dy + 1) <= 3u) mask |= StitchPosY;
    return mask;
}

uint32_t RawWorldShowcase::EvictLruToBudget(ChunkKey protectedKey, IGpuChunkSink* sink) {
    uint32_t evicted = 0u;
    while (m_usedVramBytes > m_cfg.vramBudgetBytes && !m_chunks.empty()) {
        auto victim = m_chunks.end();
        for (auto it = m_chunks.begin(); it != m_chunks.end(); ++it) {
            if (it->first == protectedKey && m_chunks.size() > 1u) continue;
            if (victim == m_chunks.end() || it->second.lastTouchedFrame < victim->second.lastTouchedFrame) {
                victim = it;
            }
        }
        if (victim == m_chunks.end()) break;
        const uint64_t freed = victim->second.estimatedVramBytes;
        if (sink) sink->OnChunkEvicted(victim->first, victim->second.gpuHandle);
        m_usedVramBytes = (m_usedVramBytes >= freed) ? (m_usedVramBytes - freed) : 0ull;
        m_chunks.erase(victim);
        ++evicted;
    }
    return evicted;
}

StreamUpdateStats RawWorldShowcase::Update(Vec3 camWorldPos, IGpuChunkSink* sink) {
    StreamUpdateStats st{};
    st.vramBudgetBytes = m_cfg.vramBudgetBytes;
    if (!m_initialized) return st;

    ++m_frame;
    const float invChunk = 1.0f / m_cfg.terrain.chunkSizeU;
    const ChunkKey camKey{int32_t(std::floor(camWorldPos.x * invChunk)),
                          int32_t(std::floor(camWorldPos.y * invChunk))};
    st.cameraChunk = camKey;
    st.cameraBiome = EvaluateBiomeBlend(camWorldPos.x, camWorldPos.y, m_cfg.terrain.seed).primary;
    st.activeInFrontier = IsCameraInFrontier(camWorldPos);

    if (!st.activeInFrontier) {
        // Player returned well inside Tamriel: smoothly evict frontier chunks to return 100% VRAM.
        if (!m_chunks.empty()) {
            st.evictedThisFrame = static_cast<uint32_t>(m_chunks.size());
            Reset(sink);
        }
        return st;
    }

    // 1. Collect desired chunks sorted nearest-first so high-priority LOD0 chunks stream first.
    struct Wanted {
        ChunkKey key;
        uint32_t lod;
        uint8_t  stitch;
        int32_t  distSq;
    };
    std::vector<Wanted> wanted;
    const int32_t R = m_cfg.lod3RadiusChunks;
    wanted.reserve(size_t((2 * R + 1) * (2 * R + 1)));
    for (int32_t dy = -R; dy <= R; ++dy) {
        for (int32_t dx = -R; dx <= R; ++dx) {
            const uint32_t lod = ComputeTargetLod(dx, dy);
            if (lod > 3u) continue;
            const ChunkKey k{camKey.x + dx, camKey.y + dy};
            // Never stream chunks strictly inside the Tamriel exclusion box (zero overlap with Skyrim's map!).
            if (!m_cfg.showcaseMode && m_cfg.terrain.border.enabled) {
                const float x0 = float(k.x) * m_cfg.terrain.chunkSizeU;
                const float x1 = x0 + m_cfg.terrain.chunkSizeU;
                const float y0 = float(k.y) * m_cfg.terrain.chunkSizeU;
                const float y1 = y0 + m_cfg.terrain.chunkSizeU;
                const float minX = m_cfg.useCustomBorderBox ? m_cfg.borderMinX : -m_cfg.terrain.border.halfExtentX;
                const float maxX = m_cfg.useCustomBorderBox ? m_cfg.borderMaxX :  m_cfg.terrain.border.halfExtentX;
                const float minY = m_cfg.useCustomBorderBox ? m_cfg.borderMinY : -m_cfg.terrain.border.halfExtentY;
                const float maxY = m_cfg.useCustomBorderBox ? m_cfg.borderMaxY :  m_cfg.terrain.border.halfExtentY;
                if (x0 >= minX && x1 <= maxX && y0 >= minY && y1 <= maxY) continue;
            }
            wanted.push_back({k, lod, ComputeStitchMask(dx, dy, lod), dx * dx + dy * dy});
        }
    }
    std::sort(wanted.begin(), wanted.end(), [](const Wanted& a, const Wanted& b) {
        if (a.distSq != b.distSq) return a.distSq < b.distSq;
        if (a.key.y != b.key.y) return a.key.y < b.key.y;
        return a.key.x < b.key.x;
    });

    // 2. Evict any chunks now outside streaming radius.
    for (auto it = m_chunks.begin(); it != m_chunks.end(); ) {
        const int32_t dx = it->first.x - camKey.x;
        const int32_t dy = it->first.y - camKey.y;
        if (ComputeTargetLod(dx, dy) > 3u) {
            const uint64_t freed = it->second.estimatedVramBytes;
            if (sink) sink->OnChunkEvicted(it->first, it->second.gpuHandle);
            m_usedVramBytes = (m_usedVramBytes >= freed) ? (m_usedVramBytes - freed) : 0ull;
            it = m_chunks.erase(it);
            ++st.evictedThisFrame;
        } else {
            ++it;
        }
    }

    // 3. Touch resident chunks and stream up to maxLoadsPerFrame new/LOD-changed chunks.
    uint32_t loadsLeft = std::max(1u, m_cfg.maxLoadsPerFrame);
    for (const auto& w : wanted) {
        auto it = m_chunks.find(w.key);
        if (it != m_chunks.end() && it->second.lodLevel == w.lod && it->second.stitchMask == w.stitch) {
            it->second.lastTouchedFrame = m_frame;
            continue;
        }
        if (loadsLeft == 0u) continue;

        StreamedChunkEntry entry{};
        entry.key = w.key;
        entry.lodLevel = w.lod;
        entry.stitchMask = w.stitch;
        entry.lastTouchedFrame = m_frame;

        TerrainConfig chunkCfg = m_cfg.terrain;
        if (m_cfg.useCustomBorderBox && chunkCfg.border.enabled) {
            chunkCfg.border.halfExtentX = (w.key.x < 0) ? std::fabs(m_cfg.borderMinX) : m_cfg.borderMaxX;
            chunkCfg.border.halfExtentY = (w.key.y < 0) ? std::fabs(m_cfg.borderMinY) : m_cfg.borderMaxY;
        }
        if (!GenerateBiomeTerrainChunk(w.key.x, w.key.y, w.lod, chunkCfg, w.stitch, &entry.terrain)) {
            continue;
        }
        if (!BuildTerrainChunkMesh(entry.terrain, m_cfg.maxMeshletTris, &entry.mesh)) {
            continue;
        }
        // Populate Poisson props on LOD0 and LOD1 chunks (distant LOD2/LOD3 stay lightweight terrain).
        if (w.lod <= 1u) {
            GenerateChunkProps(entry.terrain, m_cfg.terrain.seed, 0.0f, &entry.props);
        }
        const float cx = entry.terrain.originX + entry.terrain.chunkSizeU * 0.5f;
        const float cy = entry.terrain.originY + entry.terrain.chunkSizeU * 0.5f;
        entry.dominantBiome = EvaluateBiomeBlend(cx, cy, m_cfg.terrain.seed).primary;
        entry.estimatedVramBytes = EstimateChunkVramBytes(entry.mesh, entry.props.size());

        // If replacing an existing chunk at a different LOD, release its old handle/VRAM first.
        if (it != m_chunks.end()) {
            const uint64_t oldBytes = it->second.estimatedVramBytes;
            if (sink) sink->OnChunkEvicted(it->first, it->second.gpuHandle);
            m_usedVramBytes = (m_usedVramBytes >= oldBytes) ? (m_usedVramBytes - oldBytes) : 0ull;
            m_chunks.erase(it);
        }

        // Skip inserting if a single chunk exceeds the entire budget cap (fail-closed guard).
        if (entry.estimatedVramBytes > m_cfg.vramBudgetBytes) {
            continue;
        }

        if (sink) entry.gpuHandle = sink->OnChunkLoaded(entry);
        m_usedVramBytes += entry.estimatedVramBytes;
        m_chunks.emplace(w.key, std::move(entry));
        ++st.loadedThisFrame;
        --loadsLeft;

        // Enforce strict VRAM budget via LRU eviction after every insert.
        st.evictedThisFrame += EvictLruToBudget(camKey, sink);
    }

    // 4. Aggregate telemetry
    st.activeChunkCount = static_cast<uint32_t>(m_chunks.size());
    st.usedVramBytes = m_usedVramBytes;
    for (const auto& kv : m_chunks) {
        st.totalMeshlets += static_cast<uint32_t>(kv.second.mesh.meshlets.size());
        st.totalProps += static_cast<uint32_t>(kv.second.props.size());
    }
    return st;
}

OfflineShowcaseSummary GenerateOfflineShowcaseDiorama(uint32_t seed, const std::string& optionalExportDir) {
    OfflineShowcaseSummary sum{};
    TerrainConfig cfg{};
    cfg.seed = seed;
    cfg.border.enabled = false; // Pure showcase mode

    // Deterministic compass coordinates at radius 220,000u (right beyond Tamriel's borders) matching the 6 sectors!
    constexpr float kAngles[6] = {
        +1.5707963f, // 0: Glacial Fjord Spires (N)
        -0.5235988f, // 1: Geothermal Volcanic Caldera (ESE)
        +2.6179939f, // 2: Old-Growth Conifer Highlands (WNW)
        -1.5707963f, // 3: Obsidian Basalt Canyons (S)
        +0.5235988f, // 4: Bioluminescent Crystal Tundra (ENE)
        -2.6179939f, // 5: Alpine Meltwater Deltas (WSW)
    };
    const float kR = 220000.0f;

    if (!optionalExportDir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(optionalExportDir, ec);
    }

    for (int i = 0; i < 6; ++i) {
        const float wx = std::cos(kAngles[i]) * kR;
        const float wy = std::sin(kAngles[i]) * kR;
        const int32_t cx = int32_t(std::floor(wx / cfg.chunkSizeU));
        const int32_t cy = int32_t(std::floor(wy / cfg.chunkSizeU));

        TerrainChunkData chunk{};
        ProceduralMesh terrainMesh{};
        if (!GenerateBiomeTerrainChunk(cx, cy, 1u, cfg, StitchNone, &chunk)) return sum;
        if (!BuildTerrainChunkMesh(chunk, 126u, &terrainMesh)) return sum;

        std::vector<PropInstance> props;
        GenerateChunkProps(chunk, seed, 0.0f, &props);

        // Generate the signature hero prop for biome i
        ProceduralMesh hero{};
        bool heroOk = false;
        switch (i) {
            case 0: heroOk = BuildGlacialIceArch(seed + 10u, 360.0f, 220.0f, 44.0f, 126u, &hero); break;
            case 1: heroOk = BuildVolcanicVent(seed + 20u, 110.0f, 130.0f, 126u, &hero); break;
            case 2: heroOk = BuildMultiTierConifer(seed + 30u, 260.0f, 48.0f, 0.20f, 126u, &hero); break;
            case 3: heroOk = BuildHexagonalBasaltCluster(seed + 40u, 11u, 140.0f, 210.0f, 126u, &hero); break;
            case 4: heroOk = BuildCrystalMonolith(seed + 50u, 76.0f, 240.0f, 5u, 126u, &hero); break;
            case 5: heroOk = BuildAlpineBoulder(seed + 60u, 72.0f, 0.25f, 10u, 126u, &hero); break;
        }
        if (!heroOk) return sum;

        sum.biomesGenerated++;
        sum.biomeOrder.push_back(static_cast<BiomeId>(i));
        sum.totalMeshlets += static_cast<uint32_t>(terrainMesh.meshlets.size() + hero.meshlets.size());
        sum.totalVertices += static_cast<uint32_t>(terrainMesh.vertices.size() + hero.vertices.size());
        sum.totalTriangles += static_cast<uint32_t>((terrainMesh.indices.size() + hero.indices.size()) / 3u);
        sum.totalProps += static_cast<uint32_t>(props.size());

        if (!optionalExportDir.empty()) {
            const std::string base = optionalExportDir + "/showcase_biome_" + std::to_string(i);
            if (ExportMeshToGlbFile(terrainMesh, base + "_terrain.glb")) sum.exportedFiles++;
            if (ExportMeshToObjFile(hero, base + "_hero.obj")) sum.exportedFiles++;
        }
    }
    sum.ok = (sum.biomesGenerated == 6u);
    return sum;
}

bool BuildChunkMergedGpuMesh(const StreamedChunkEntry& entry, uint32_t maxMeshletTris,
                             uint32_t worldSeed, float seaLevelZ, ProceduralMesh* outMerged) {
    if (!outMerged || entry.mesh.meshlets.empty() || entry.mesh.vertices.empty()) return false;

    *outMerged = entry.mesh;
    if (entry.props.empty()) return true;

    // Build reusable canonical 3D prop templates once (2 seed variants per PropKind, zero external files).
    // Frontier trees & understory shrubs are generated via RawFloraEngine's biomechanical da Vinci pipe-model
    // + 3D foliage cluster generator (Sitka Spruce, Windswept Coastal Pine, Silver Birch, Ancient Oak, Shrub).
    struct PropTemplateCache {
        bool ready = false;
        ProceduralMesh variants[8][2];
        ProceduralMesh floraSpecies[5];
        void Ensure(uint32_t seed) {
            if (ready) return;
            const float floraHeightsM[5] = {8.4f, 6.8f, 7.6f, 8.0f, 1.55f};
            const float floraCrownsM[5]  = {2.5f, 3.0f, 2.2f, 3.6f, 1.45f};
            for (uint32_t sp = 0u; sp < 5u; ++sp) {
                FloraGrowthParams fp = MakeDefaultFloraParams(static_cast<FloraSpecies>(sp), seed + sp * 4099u + 17u, 1u);
                fp.heightMeters      = floraHeightsM[sp];
                fp.crownRadiusMeters = floraCrownsM[sp];
                fp.unitsPerMeter     = 70.0f;
                fp.snowLoad01        = (sp <= 1u) ? 0.24f : 0.08f;
                fp.maxMeshletTris    = 126u;
                GenerateFloraMesh(fp, &floraSpecies[sp], nullptr);
            }
            for (uint32_t v = 0; v < 2u; ++v) {
                const uint32_t s = seed + v * 7919u;
                BuildMultiTierConifer(s + 11u, 360.0f + 90.0f * float(v), 78.0f + 16.0f * float(v), 0.28f * float(v), 126u, &variants[0][v]);
                BuildAlpineBoulder(s + 23u, 62.0f + 18.0f * float(v), 0.25f, 8u, 126u, &variants[1][v]);
                BuildGlacialIceArch(s + 37u, 340.0f + 60.0f * float(v), 210.0f + 40.0f * float(v), 42.0f, 126u, &variants[2][v]);
                BuildNordicSnowDrift(s + 53u, 190.0f, 115.0f, 38.0f, 126u, &variants[3][v]);
                BuildHexagonalBasaltCluster(s + 71u, 9u, 120.0f, 210.0f + 50.0f * float(v), 126u, &variants[4][v]);
                BuildVolcanicVent(s + 89u, 105.0f, 125.0f, 126u, &variants[5][v]);
                BuildCrystalMonolith(s + 107u, 74.0f, 240.0f + 55.0f * float(v), 5u, 126u, &variants[6][v]);
                BuildAlpineGrassClump(s + 131u, 10u, 48.0f, 126u, &variants[7][v]);
            }
            ready = true;
        }
    };
    static PropTemplateCache s_cache;
    s_cache.Ensure(worldSeed);

    const size_t maxBakedProps = (entry.lodLevel == 0u) ? 44u : 20u;
    std::vector<TerrainVertex> propVerts;
    std::vector<uint32_t> propIndices;
    propVerts.reserve(8192u);
    propIndices.reserve(16384u);

    size_t bakedCount = 0u;
    for (const PropInstance& inst : entry.props) {
        if (bakedCount >= maxBakedProps) break;
        // Skip props underwater or inside the Tamriel border transition margin where terrain is still submerged.
        if (inst.position.z < seaLevelZ + 12.0f) continue;
        // On LOD1 chunks, skip tiny grass clumps to keep distant chunks crisp & fast.
        if (entry.lodLevel > 0u && inst.kind == PropKind::AlpineGrassClump) continue;

        const size_t ki = std::min<size_t>(static_cast<size_t>(inst.kind), 7u);
        const size_t vi = inst.seed & 1u;
        bool isBiomechanicalFlora = false;
        const ProceduralMesh* tmplPtr = &s_cache.variants[ki][vi];
        if (inst.kind == PropKind::ConiferTree) {
            const float slope01 = Clamp01(1.0f - inst.upNormal.z);
            const FloraSpecies sp = SelectFloraSpeciesForBiome(inst.biome, inst.seed, slope01, 0.5f);
            const uint32_t spIdx = std::min<uint32_t>(static_cast<uint32_t>(sp), 3u);
            if (!s_cache.floraSpecies[spIdx].meshlets.empty()) {
                tmplPtr = &s_cache.floraSpecies[spIdx];
                isBiomechanicalFlora = true;
            }
        } else if (inst.kind == PropKind::AlpineGrassClump && !s_cache.floraSpecies[4].meshlets.empty()) {
            tmplPtr = &s_cache.floraSpecies[4];
            isBiomechanicalFlora = true;
        }
        const ProceduralMesh& tmpl = *tmplPtr;
        if (tmpl.meshlets.empty() || tmpl.vertices.empty()) continue;

        const float cy = std::cos(inst.yawRad);
        const float sy = std::sin(inst.yawRad);
        const float sc = Clamp(inst.scale, 0.5f, 2.2f);
        const float sinkZ = (inst.kind == PropKind::ConiferTree) ? 6.0f : 4.0f;

        const uint32_t baseVert = static_cast<uint32_t>(propVerts.size());
        // Flatten template meshlets into world-transformed vertices/indices.
        for (const auto& ml : tmpl.meshlets) {
            const uint32_t mlVertBase = static_cast<uint32_t>(propVerts.size());
            for (uint32_t i = 0u; i < ml.vertexCount; ++i) {
                TerrainVertex v = tmpl.vertices[ml.vertexOffset + i];
                const float lx = v.pos[0] * sc;
                const float ly = v.pos[1] * sc;
                const float lz = v.pos[2] * sc;
                v.pos[0] = inst.position.x + (lx * cy - ly * sy);
                v.pos[1] = inst.position.y + (lx * sy + ly * cy);
                v.pos[2] = inst.position.z - sinkZ + lz;
                const float nx = v.normal[0];
                const float ny = v.normal[1];
                v.normal[0] = nx * cy - ny * sy;
                v.normal[1] = nx * sy + ny * cy;
                if (isBiomechanicalFlora) {
                    // Map RawFloraEngine bark vs 3D foliage clusters onto RawTerrainWorld.hlsl's
                    // dedicated furrowed bark (10.0) & deep boreal needle/leaf (11.0) material tags.
                    v.meta[3] = (v.splat1[1] > 0.30f) ? 11.0f : 10.0f;
                }
                propVerts.push_back(v);
            }
            for (uint32_t t = 0u; t < ml.triangleCount * 3u; ++t) {
                propIndices.push_back(mlVertBase + tmpl.indices[ml.triangleOffset + t]);
            }
        }
        (void)baseVert;
        ++bakedCount;
    }

    if (propVerts.empty() || propIndices.empty()) return true;

    std::vector<TerrainVertex> packedPropV;
    std::vector<uint32_t> packedPropI;
    std::vector<MeshletDesc> packedPropM;
    const uint32_t limT = std::clamp(maxMeshletTris, 16u, 126u);
    BuildMeshlets(propVerts, propIndices, 64u, limT, &packedPropV, &packedPropI, &packedPropM);
    if (packedPropM.empty()) return true;

    // Concatenate prop meshlets directly onto terrain mesh (O(N) append, 1 DispatchMesh draws terrain + 3D props!).
    const uint32_t vertBase = static_cast<uint32_t>(outMerged->vertices.size());
    const uint32_t idxBase  = static_cast<uint32_t>(outMerged->indices.size());
    outMerged->vertices.insert(outMerged->vertices.end(), packedPropV.begin(), packedPropV.end());
    outMerged->indices.insert(outMerged->indices.end(), packedPropI.begin(), packedPropI.end());
    outMerged->meshlets.reserve(outMerged->meshlets.size() + packedPropM.size());
    for (MeshletDesc m : packedPropM) {
        m.vertexOffset += vertBase;
        m.triangleOffset += idxBase;
        outMerged->meshlets.push_back(m);
    }
    for (const auto& v : packedPropV) {
        outMerged->aabbMin.x = std::min(outMerged->aabbMin.x, v.pos[0]);
        outMerged->aabbMin.y = std::min(outMerged->aabbMin.y, v.pos[1]);
        outMerged->aabbMin.z = std::min(outMerged->aabbMin.z, v.pos[2]);
        outMerged->aabbMax.x = std::max(outMerged->aabbMax.x, v.pos[0]);
        outMerged->aabbMax.y = std::max(outMerged->aabbMax.y, v.pos[1]);
        outMerged->aabbMax.z = std::max(outMerged->aabbMax.z, v.pos[2]);
    }
    return true;
}

}  // namespace raw::worldgen
