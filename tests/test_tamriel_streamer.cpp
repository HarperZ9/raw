// test_tamriel_streamer.cpp: Live Multi-Cell Streaming Tamriel World Engine verification suite
// (`raw::worldgen::RawTamrielWorldStreamer`).
// Flies continuously across 3 iconic Skyrim landmarks: Riverwood (4,-12) -> Whiterun (5,-3) -> High Hrothgar (14,-14),
// verifying 0-error hardware 64v/126t meshlet validation, LRU NIF model cache reuse, and strict VRAM budget eviction!
//
// Link directly into `ce_runtime_check` (call `raw::worldgen::RunTamrielStreamerTests(Check)`)
// OR compile standalone with `-DRAW_TAMRIEL_STREAMER_TEST_STANDALONE=1`!
// Original work, MIT.

#include "../src/pipeline/RawAssetValidator.h"
#include "../src/worldgen/RawTamrielWorldStreamer.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

namespace raw::worldgen {
namespace {

Vec3 CellCenterPos(int32_t cx, int32_t cy, float z = 0.0f) {
    return {(float(cx) + 0.5f) * kEsmCellSizeU, (float(cy) + 0.5f) * kEsmCellSizeU, z};
}

bool ValidateAllResidentCells(const RawTamrielWorldStreamer& streamer, uint32_t* outCheckedMeshes) {
    uint32_t count = 0u;
    for (const auto& kv : streamer.ResidentCells()) {
        const StreamedTamrielCell& c = kv.second;
        if (!raw::pipeline::ValidateProceduralMeshStrict(c.terrainMesh)) return false;
        ++count;
        if (!c.propsMesh.meshlets.empty()) {
            if (!raw::pipeline::ValidateProceduralMeshStrict(c.propsMesh)) return false;
            ++count;
        }
    }
    if (outCheckedMeshes) *outCheckedMeshes = count;
    return count > 0u;
}

}  // namespace

int RunTamrielStreamerTests(void (*checkFn)(bool, const char*)) {
    int fails = 0;
    auto Report = [&](bool cond, const char* name) {
        if (!cond) ++fails;
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s %s\n", cond ? "PASS" : "FAIL", name);
        }
    };

    // 1. Planted-failure controls: invalid config & missing ESM/BSA paths must fail cleanly!
    {
        RawTamrielWorldStreamer bad;
        std::string err;
        TamrielStreamConfig badCfg{};
        badCfg.vramBudgetBytes = 0ull;
        const bool rejBudget = !bad.Init("Z:/no.esm", "Z:/no.bsa", badCfg, &err);
        TamrielStreamConfig okCfg{};
        const bool rejMissing = !bad.Init("Z:/no.esm", "Z:/no.bsa", okCfg, &err);
        Report(rejBudget && rejMissing && !bad.IsInitialized(), "tamriel_streamer_planted_failure_controls");
    }

    // 2. LIVE Real-Tamriel 3-Region Continuous Flight Test (gracefully skipped only if Skyrim SE is not installed!)
    const std::string esmPath = "H:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data/Skyrim.esm";
    const std::string bsaPath = "H:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data/Skyrim - Meshes0.bsa";
    if (std::filesystem::exists(esmPath) && std::filesystem::exists(bsaPath)) {
        struct MockTamrielSink final : ITamrielChunkSink {
            uint64_t nextHandle = 1ull;
            uint32_t loads = 0u, evicts = 0u;
            uint64_t OnTamrielCellLoaded(const StreamedTamrielCell&) override { ++loads; return nextHandle++; }
            void     OnTamrielCellEvicted(ChunkKey, uint64_t) override { ++evicts; }
        };

        RawTamrielWorldStreamer streamer;
        TamrielStreamConfig cfg{};
        cfg.radiusCells            = 1;  // 3x3 concentric ring (9 cells per landmark)
        cfg.maxCellsPerUpdate      = 4u;
        cfg.maxUniqueModelsPerCell = 24u;
        cfg.vramBudgetBytes        = 64ull * 1024ull * 1024ull;
        cfg.modelCacheBudgetBytes  = 32ull * 1024ull * 1024ull;
        cfg.bakeCellProps          = true;

        std::string err;
        MockTamrielSink sink{};
        const bool initOk = streamer.Init(esmPath, bsaPath, cfg, &err);

        // Leg 1: Riverwood (4, -12)
        const auto stRiv = streamer.StreamRingComplete(CellCenterPos(4, -12), &sink);
        uint32_t rivMeshes = 0u;
        const bool rivValid = initOk && ValidateAllResidentCells(streamer, &rivMeshes);
        std::printf("  [Flight 1: Riverwood (4,-12)] cells=%u cachedNifs=%u terrainMl=%u propMl=%u hits=%llu misses=%llu vram=%.2fMB meshesValid=%u\n",
                    stRiv.activeCells, stRiv.cachedNifModels, stRiv.totalTerrainMeshlets, stRiv.totalPropMeshlets,
                    (unsigned long long)stRiv.cacheHits, (unsigned long long)stRiv.cacheMisses,
                    double(stRiv.usedVramBytes) / (1024.0 * 1024.0), rivMeshes);
        Report(rivValid && stRiv.indexedWorldCells > 10000u && stRiv.activeCells >= 6u &&
               stRiv.totalTerrainMeshlets >= 100u && stRiv.totalPropMeshlets > 50u &&
               stRiv.usedVramBytes <= cfg.vramBudgetBytes,
               "tamriel_streamer_flight_leg1_riverwood_validated");

        // Leg 2: Fly North to Whiterun (5, -3) -> Riverwood evicts cleanly, shared NIF models hit LRU cache!
        const uint64_t hitsBeforeWhite = stRiv.cacheHits;
        const auto stWhite = streamer.StreamRingComplete(CellCenterPos(5, -3), &sink);
        uint32_t whiteMeshes = 0u;
        const bool whiteValid = initOk && ValidateAllResidentCells(streamer, &whiteMeshes);
        std::printf("  [Flight 2: Whiterun (5,-3)]   cells=%u cachedNifs=%u terrainMl=%u propMl=%u hits=%llu misses=%llu vram=%.2fMB evicts=%u\n",
                    stWhite.activeCells, stWhite.cachedNifModels, stWhite.totalTerrainMeshlets, stWhite.totalPropMeshlets,
                    (unsigned long long)stWhite.cacheHits, (unsigned long long)stWhite.cacheMisses,
                    double(stWhite.usedVramBytes) / (1024.0 * 1024.0), sink.evicts);
        Report(whiteValid && stWhite.activeCells >= 6u && stWhite.totalPropMeshlets > 50u &&
               stWhite.cacheHits > hitsBeforeWhite && sink.evicts >= 6u &&
               stWhite.usedVramBytes <= cfg.vramBudgetBytes,
               "tamriel_streamer_flight_leg2_whiterun_lru_cache_hits");

        // Leg 3: Fly East/Up to High Hrothgar (14, -14)
        const auto stHroth = streamer.StreamRingComplete(CellCenterPos(14, -14, 30000.0f), &sink);
        uint32_t hrothMeshes = 0u;
        const bool hrothValid = initOk && ValidateAllResidentCells(streamer, &hrothMeshes);
        std::printf("  [Flight 3: Hrothgar (14,-14)] cells=%u cachedNifs=%u terrainMl=%u propMl=%u hits=%llu misses=%llu vram=%.2fMB\n",
                    stHroth.activeCells, stHroth.cachedNifModels, stHroth.totalTerrainMeshlets, stHroth.totalPropMeshlets,
                    (unsigned long long)stHroth.cacheHits, (unsigned long long)stHroth.cacheMisses,
                    double(stHroth.usedVramBytes) / (1024.0 * 1024.0));
        Report(hrothValid && stHroth.activeCells >= 6u && stHroth.totalTerrainMeshlets >= 100u &&
               stHroth.usedVramBytes <= cfg.vramBudgetBytes,
               "tamriel_streamer_flight_leg3_high_hrothgar_validated");

        // Leg 4: Strict Tight VRAM & Model-Cache Budget Eviction Stress!
        streamer.Shutdown(&sink);
        TamrielStreamConfig tightCfg = cfg;
        tightCfg.vramBudgetBytes       = 1500ull * 1024ull; // Tight 1.5 MB VRAM cap forces continuous LRU cell eviction!
        tightCfg.modelCacheBudgetBytes = 768ull * 1024ull;  // Tight 768 KB model cache cap forces LRU NIF eviction!
        tightCfg.maxUniqueModelsPerCell = 8u;
        MockTamrielSink tightSink{};
        const bool tightInit = streamer.Init(esmPath, bsaPath, tightCfg, &err);
        const auto stT1 = streamer.Update(CellCenterPos(4, -12), &tightSink);
        const auto stT2 = streamer.Update(CellCenterPos(4, -12), &tightSink);
        const auto stT3 = streamer.Update(CellCenterPos(5, -12), &tightSink);
        std::printf("  [Tight Budget Eviction] cells=%u usedVram=%llu/%llu modelCache=%llu/%llu evicts=%u\n",
                    stT3.activeCells, (unsigned long long)stT3.usedVramBytes, (unsigned long long)tightCfg.vramBudgetBytes,
                    (unsigned long long)stT3.usedModelCacheBytes, (unsigned long long)tightCfg.modelCacheBudgetBytes,
                    tightSink.evicts);
        Report(tightInit &&
               stT1.usedVramBytes <= tightCfg.vramBudgetBytes &&
               stT2.usedVramBytes <= tightCfg.vramBudgetBytes &&
               stT3.usedVramBytes <= tightCfg.vramBudgetBytes &&
               stT3.usedModelCacheBytes <= tightCfg.modelCacheBudgetBytes &&
               tightSink.loads >= 2u && tightSink.evicts >= 1u,
               "tamriel_streamer_strict_vram_and_model_cache_lru_eviction");

        streamer.Shutdown(&tightSink);
    } else {
        Report(true, "tamriel_streamer_live_flight_skipped_not_installed");
    }

    return fails;
}

}  // namespace raw::worldgen

#ifdef RAW_TAMRIEL_STREAMER_TEST_STANDALONE
int main() {
    const int fails = raw::worldgen::RunTamrielStreamerTests(nullptr);
    std::printf("RawTamrielWorldStreamer standalone test summary: %s (%d failures)\n",
                fails == 0 ? "ALL PASSED" : "FAILED", fails);
    return fails == 0 ? 0 : 1;
}
#endif
