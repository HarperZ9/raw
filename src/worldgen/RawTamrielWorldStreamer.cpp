// RawTamrielWorldStreamer.cpp: Multi-Cell Streaming Tamriel World Engine (`raw::worldgen`).
// Original work, MIT.

#include "RawTamrielWorldStreamer.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace raw::worldgen {
namespace {

void SetErr(std::string* outErr, const std::string& msg) {
    if (outErr) *outErr = msg;
}

}  // namespace

bool RawTamrielWorldStreamer::Init(const std::string& esmPath, const std::string& meshesBsaPath,
                                   const TamrielStreamConfig& cfg, std::string* outErr) {
    Shutdown(nullptr);
    if (cfg.radiusCells < 0 || cfg.radiusCells > 6 || cfg.vramBudgetBytes < 65536ull ||
        cfg.modelCacheBudgetBytes < 65536ull || cfg.maxCellsPerUpdate == 0u) {
        SetErr(outErr, "Invalid TamrielStreamConfig parameters.");
        return false;
    }

    if (!IndexEsmWorldspace(esmPath, cfg.targetWorldFormId, &worldIndex_, outErr)) {
        return false;
    }
    if (!IndexEsmModels(esmPath, &modelIndex_, outErr)) {
        return false;
    }
    if (!primaryBsa_.Open(meshesBsaPath, outErr)) {
        return false;
    }

    // Auto-discover sibling `Skyrim - Meshes1.bsa` next to `Skyrim - Meshes0.bsa`
    hasSiblingBsa_ = false;
    {
        const std::filesystem::path p(meshesBsaPath);
        const std::string fname = p.filename().string();
        if (fname.find("Meshes0.bsa") != std::string::npos || fname.find("meshes0.bsa") != std::string::npos) {
            const std::filesystem::path sib = p.parent_path() / "Skyrim - Meshes1.bsa";
            std::error_code ec;
            if (std::filesystem::exists(sib, ec)) {
                hasSiblingBsa_ = siblingBsa_.Open(sib.string(), nullptr);
            }
        }
    }

    cfg_ = cfg;
    cfg_.maxMeshletTris = std::clamp(cfg.maxMeshletTris, 16u, 126u);
    initialized_ = true;
    return true;
}

void RawTamrielWorldStreamer::Reset(ITamrielChunkSink* sink) {
    if (sink) {
        for (const auto& kv : cells_) {
            sink->OnTamrielCellEvicted(kv.first, kv.second.gpuHandle);
        }
    }
    cells_.clear();
    usedVramBytes_ = 0ull;
    modelCache_.clear();
    failedModels_.clear();
    usedModelCacheBytes_ = 0ull;
    cacheHits_ = 0ull;
    cacheMisses_ = 0ull;
    frame_ = 0ull;
    lastStats_ = {};
}

void RawTamrielWorldStreamer::Shutdown(ITamrielChunkSink* sink) {
    Reset(sink);
    primaryBsa_.Close();
    siblingBsa_.Close();
    hasSiblingBsa_ = false;
    worldIndex_ = {};
    modelIndex_ = {};
    initialized_ = false;
}

const StreamedTamrielCell* RawTamrielWorldStreamer::FindCell(int32_t cellX, int32_t cellY) const {
    auto it = cells_.find(ChunkKey{cellX, cellY});
    return (it == cells_.end()) ? nullptr : &it->second;
}

void RawTamrielWorldStreamer::EvictLruModelsToBudget(uint32_t protectedFormId) {
    while (usedModelCacheBytes_ > cfg_.modelCacheBudgetBytes && !modelCache_.empty()) {
        auto victim = modelCache_.end();
        for (auto it = modelCache_.begin(); it != modelCache_.end(); ++it) {
            if (it->first == protectedFormId && modelCache_.size() > 1u) continue;
            if (victim == modelCache_.end() || it->second.lastUsedFrame < victim->second.lastUsedFrame) {
                victim = it;
            }
        }
        if (victim == modelCache_.end()) break;
        const uint64_t freed = victim->second.estimatedBytes;
        usedModelCacheBytes_ = (usedModelCacheBytes_ >= freed) ? (usedModelCacheBytes_ - freed) : 0ull;
        modelCache_.erase(victim);
    }
}

const RawTamrielWorldStreamer::CachedNifModel* RawTamrielWorldStreamer::AcquireCachedNifModel(const EsmModelRecord& mr) {
    if (auto it = modelCache_.find(mr.formId); it != modelCache_.end()) {
        it->second.lastUsedFrame = frame_;
        ++cacheHits_;
        return &it->second;
    }
    if (failedModels_.find(mr.formId) != failedModels_.end()) {
        return nullptr;
    }

    ++cacheMisses_;
    std::vector<uint8_t> nifBytes;
    bool extracted = false;
    if (primaryBsa_.HasFile(mr.bsaVirtualPath) &&
        primaryBsa_.ExtractFile(mr.bsaVirtualPath, &nifBytes, nullptr)) {
        extracted = true;
    } else if (hasSiblingBsa_ && siblingBsa_.HasFile(mr.bsaVirtualPath) &&
               siblingBsa_.ExtractFile(mr.bsaVirtualPath, &nifBytes, nullptr)) {
        extracted = true;
    }

    if (!extracted) {
        failedModels_.insert(mr.formId);
        return nullptr;
    }

    NifScene nifScene{};
    ProceduralMesh mesh{};
    if (!ParseNifBytes(nifBytes.data(), nifBytes.size(), mr.bsaVirtualPath, &nifScene, nullptr) ||
        !BuildProceduralMeshFromNif(nifScene, cfg_.maxMeshletTris, &mesh, nullptr)) {
        failedModels_.insert(mr.formId);
        return nullptr;
    }

    CachedNifModel entry{};
    entry.baseFormId     = mr.formId;
    entry.recordType     = mr.recordType;
    entry.editorId       = mr.editorId;
    entry.bsaVirtualPath = mr.bsaVirtualPath;
    entry.nifShapeCount  = static_cast<uint32_t>(nifScene.shapes.size());
    for (const auto& sh : nifScene.shapes) {
        if (!sh.textureSet.diffusePath.empty()) {
            entry.primaryDiffuseDds = sh.textureSet.diffusePath;
            break;
        }
    }
    entry.estimatedBytes = mesh.EstimateGpuBytes() + 256ull;
    entry.lastUsedFrame  = frame_;
    entry.mesh           = std::move(mesh);

    // Fail-closed guard: never store a single model exceeding the entire model cache cap
    if (entry.estimatedBytes > cfg_.modelCacheBudgetBytes) {
        return nullptr;
    }

    usedModelCacheBytes_ += entry.estimatedBytes;
    auto [insIt, inserted] = modelCache_.emplace(mr.formId, std::move(entry));
    (void)inserted;
    EvictLruModelsToBudget(mr.formId);
    return &insIt->second;
}

bool RawTamrielWorldStreamer::BuildStreamedCell(int32_t cellX, int32_t cellY, StreamedTamrielCell* outCell) {
    if (!outCell) return false;
    EsmLoadedCell esmCell{};
    if (!LoadEsmExteriorCell(worldIndex_, cellX, cellY, &esmCell, nullptr)) {
        return false;
    }

    StreamedTamrielCell sc{};
    sc.key              = {cellX, cellY};
    sc.cellFormId       = esmCell.meta.cellFormId;
    sc.cellEdid         = esmCell.meta.edid;
    sc.waterHeightZ     = esmCell.meta.hasWaterHeight ? esmCell.meta.waterHeight : worldIndex_.selectedWorld.defaultWaterHeight;
    sc.lastTouchedFrame = frame_;
    sc.totalCellRefrs   = static_cast<uint32_t>(esmCell.placements.size());

    if (!BuildMeshFromEsmCell(esmCell, worldIndex_.selectedWorld.defaultWaterHeight, cfg_.maxMeshletTris,
                              &sc.terrainMesh, &sc.terrainChunk, nullptr)) {
        return false;
    }

    if (cfg_.bakeCellProps && !esmCell.placements.empty()) {
        ReconstructedCellScene tempScene{};
        tempScene.cellX = cellX;
        tempScene.cellY = cellY;
        std::unordered_map<uint32_t, uint32_t> localSlotByFormId;

        for (const auto& refr : esmCell.placements) {
            const EsmModelRecord* mr = modelIndex_.FindByFormId(refr.baseFormId);
            if (!mr || mr->bsaVirtualPath.empty()) continue;

            uint32_t slot = 0u;
            if (auto it = localSlotByFormId.find(mr->formId); it != localSlotByFormId.end()) {
                slot = it->second;
                // Touch LRU cache hit on repeated instance reuse too
                if (auto cit = modelCache_.find(mr->formId); cit != modelCache_.end()) {
                    cit->second.lastUsedFrame = frame_;
                    ++cacheHits_;
                }
            } else {
                if (tempScene.uniqueModels.size() >= cfg_.maxUniqueModelsPerCell) continue;
                const CachedNifModel* cached = AcquireCachedNifModel(*mr);
                if (!cached) continue;
                ReconstructedPropModel rpm{};
                rpm.baseFormId        = cached->baseFormId;
                rpm.recordType        = cached->recordType;
                rpm.editorId          = cached->editorId;
                rpm.bsaVirtualPath    = cached->bsaVirtualPath;
                rpm.primaryDiffuseDds = cached->primaryDiffuseDds;
                rpm.nifShapeCount     = cached->nifShapeCount;
                rpm.mesh              = cached->mesh;
                slot = static_cast<uint32_t>(tempScene.uniqueModels.size());
                tempScene.uniqueModels.push_back(std::move(rpm));
                localSlotByFormId.emplace(mr->formId, slot);
            }

            ReconstructedPropInstance inst{};
            inst.refrFormId  = refr.refrFormId;
            inst.baseFormId  = refr.baseFormId;
            inst.modelIndex  = slot;
            inst.position    = refr.position;
            inst.rotationRad = refr.rotationRad;
            inst.scale       = (std::isfinite(refr.scale) && refr.scale > 0.05f && refr.scale < 25.0f) ? refr.scale : 1.0f;
            tempScene.instances.push_back(inst);
        }

        sc.resolvedPropInstances = static_cast<uint32_t>(tempScene.instances.size());
        if (!tempScene.instances.empty()) {
            BakeReconstructedCellPropsToWorldMesh(tempScene, cfg_.maxMeshletTris, &sc.propsMesh, nullptr);
        }
    }

    sc.estimatedVramBytes = sc.terrainMesh.EstimateGpuBytes() + sc.propsMesh.EstimateGpuBytes() + 4096ull;
    *outCell = std::move(sc);
    return true;
}

uint32_t RawTamrielWorldStreamer::EvictLruCellsToBudget(ChunkKey protectedKey, ITamrielChunkSink* sink) {
    uint32_t evicted = 0u;
    while (usedVramBytes_ > cfg_.vramBudgetBytes && !cells_.empty()) {
        auto victim = cells_.end();
        for (auto it = cells_.begin(); it != cells_.end(); ++it) {
            if (it->first == protectedKey && cells_.size() > 1u) continue;
            if (victim == cells_.end() || it->second.lastTouchedFrame < victim->second.lastTouchedFrame) {
                victim = it;
            }
        }
        if (victim == cells_.end()) break;
        const uint64_t freed = victim->second.estimatedVramBytes;
        if (sink) sink->OnTamrielCellEvicted(victim->first, victim->second.gpuHandle);
        usedVramBytes_ = (usedVramBytes_ >= freed) ? (usedVramBytes_ - freed) : 0ull;
        cells_.erase(victim);
        ++evicted;
    }
    return evicted;
}

TamrielStreamStats RawTamrielWorldStreamer::Update(Vec3 camWorldPos, ITamrielChunkSink* sink) {
    TamrielStreamStats st{};
    st.vramBudgetBytes   = cfg_.vramBudgetBytes;
    st.indexedWorldCells = worldIndex_.totalIndexedCells;
    st.indexedModels     = static_cast<uint32_t>(modelIndex_.byFormId.size());
    if (!initialized_) return st;

    ++frame_;
    const int32_t camX = static_cast<int32_t>(std::floor(camWorldPos.x / kEsmCellSizeU));
    const int32_t camY = static_cast<int32_t>(std::floor(camWorldPos.y / kEsmCellSizeU));
    const ChunkKey camKey{camX, camY};
    st.camCellX = camX;
    st.camCellY = camY;

    // 1. Evict cells now outside concentric ring
    for (auto it = cells_.begin(); it != cells_.end(); ) {
        const int32_t dx = std::abs(it->first.x - camX);
        const int32_t dy = std::abs(it->first.y - camY);
        if (dx > cfg_.radiusCells || dy > cfg_.radiusCells) {
            const uint64_t freed = it->second.estimatedVramBytes;
            if (sink) sink->OnTamrielCellEvicted(it->first, it->second.gpuHandle);
            usedVramBytes_ = (usedVramBytes_ >= freed) ? (usedVramBytes_ - freed) : 0ull;
            it = cells_.erase(it);
            ++st.evictedThisStep;
        } else {
            ++it;
        }
    }

    // 2. Build nearest-first wanted list within concentric ring
    struct WantedCell {
        ChunkKey key;
        int32_t  distSq;
    };
    std::vector<WantedCell> wanted;
    const int32_t R = cfg_.radiusCells;
    wanted.reserve(size_t((2 * R + 1) * (2 * R + 1)));
    for (int32_t dy = -R; dy <= R; ++dy) {
        for (int32_t dx = -R; dx <= R; ++dx) {
            wanted.push_back({ChunkKey{camX + dx, camY + dy}, dx * dx + dy * dy});
        }
    }
    std::sort(wanted.begin(), wanted.end(), [](const WantedCell& a, const WantedCell& b) {
        if (a.distSq != b.distSq) return a.distSq < b.distSq;
        if (a.key.y != b.key.y) return a.key.y < b.key.y;
        return a.key.x < b.key.x;
    });

    // 3. Touch resident cells and load up to maxCellsPerUpdate missing cells
    uint32_t loadsLeft = cfg_.maxCellsPerUpdate;
    for (const auto& w : wanted) {
        if (auto it = cells_.find(w.key); it != cells_.end()) {
            it->second.lastTouchedFrame = frame_;
            continue;
        }
        if (loadsLeft == 0u) continue;
        if (!worldIndex_.FindCell(w.key.x, w.key.y)) continue;

        StreamedTamrielCell sc{};
        if (!BuildStreamedCell(w.key.x, w.key.y, &sc)) continue;

        // If a single cell with props exceeds budget, retry terrain-only before skipping so VRAM budget is strictly obeyed
        if (sc.estimatedVramBytes > cfg_.vramBudgetBytes && !sc.propsMesh.meshlets.empty()) {
            sc.propsMesh = {};
            sc.resolvedPropInstances = 0u;
            sc.estimatedVramBytes = sc.terrainMesh.EstimateGpuBytes() + 4096ull;
        }
        if (sc.estimatedVramBytes > cfg_.vramBudgetBytes) continue;

        if (sink) sc.gpuHandle = sink->OnTamrielCellLoaded(sc);
        usedVramBytes_ += sc.estimatedVramBytes;
        cells_.emplace(w.key, std::move(sc));
        ++st.loadedThisStep;
        --loadsLeft;

        st.evictedThisStep += EvictLruCellsToBudget(camKey, sink);
    }

    // 4. Aggregate live telemetry
    st.activeCells         = static_cast<uint32_t>(cells_.size());
    st.cachedNifModels     = static_cast<uint32_t>(modelCache_.size());
    st.usedVramBytes       = usedVramBytes_;
    st.usedModelCacheBytes = usedModelCacheBytes_;
    st.cacheHits           = cacheHits_;
    st.cacheMisses         = cacheMisses_;
    for (const auto& kv : cells_) {
        st.totalTerrainMeshlets += static_cast<uint32_t>(kv.second.terrainMesh.meshlets.size());
        st.totalPropMeshlets    += static_cast<uint32_t>(kv.second.propsMesh.meshlets.size());
        st.totalPropInstances   += kv.second.resolvedPropInstances;
    }

    lastStats_ = st;
    return st;
}

TamrielStreamStats RawTamrielWorldStreamer::StreamRingComplete(Vec3 camWorldPos, ITamrielChunkSink* sink) {
    TamrielStreamStats st{};
    if (!initialized_) return st;
    const uint32_t maxSteps = uint32_t((2 * cfg_.radiusCells + 1) * (2 * cfg_.radiusCells + 1)) + 2u;
    for (uint32_t s = 0u; s < maxSteps; ++s) {
        st = Update(camWorldPos, sink);
        if (st.loadedThisStep == 0u) break;
    }
    return st;
}

}  // namespace raw::worldgen
