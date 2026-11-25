// RawWorldShowcase.cpp: Live Frontier World Streaming Manager (LRU + VRAM Budget) & Offline 6-Biome Showcase.
// Original work, MIT.
#include "RawWorldShowcase.h"

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

        if (!GenerateBiomeTerrainChunk(w.key.x, w.key.y, w.lod, m_cfg.terrain, w.stitch, &entry.terrain)) {
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

}  // namespace raw::worldgen
