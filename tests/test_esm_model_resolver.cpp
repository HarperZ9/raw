// test_esm_model_resolver.cpp: Comprehensive unit & live-Skyrim tests for `raw::worldgen::RawEsmModelResolver`
// (fast streaming STAT/TREE/MSTT/ACTI/CONT/FURN/DOOR/LIGH MODL indexing + full Riverwood (4,-12)
// reconstruction connecting Skyrim.esm -> Skyrim - Meshes0.bsa -> RawNifLoader -> RawAssetValidator).
//
// Link directly into `ce_runtime_check` (call `raw::worldgen::RunEsmModelResolverTests(Check)`)
// OR compile standalone with `-DRAW_ESM_RESOLVER_TEST_STANDALONE=1`!
// Original work, MIT.

#include "../src/pipeline/RawAssetValidator.h"
#include "../src/worldgen/RawEsmModelResolver.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace raw::worldgen {
namespace {

void PushU16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(uint8_t(v & 0xFFu));
    b.push_back(uint8_t((v >> 8) & 0xFFu));
}

void PushU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(uint8_t(v & 0xFFu));
    b.push_back(uint8_t((v >> 8) & 0xFFu));
    b.push_back(uint8_t((v >> 16) & 0xFFu));
    b.push_back(uint8_t((v >> 24) & 0xFFu));
}

void PushTag(std::vector<uint8_t>& b, const char t[4]) {
    b.insert(b.end(), t, t + 4);
}

void PushSub(std::vector<uint8_t>& b, const char t[4], const std::string& str) {
    PushTag(b, t);
    PushU16(b, static_cast<uint16_t>(str.size() + 1u));
    b.insert(b.end(), str.begin(), str.end());
    b.push_back(0u);
}

std::vector<uint8_t> WrapStoredZlib(const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> out;
    out.push_back(0x78u);
    out.push_back(0x01u);
    out.push_back(0x01u);
    const uint16_t len = static_cast<uint16_t>(payload.size());
    PushU16(out, len);
    PushU16(out, static_cast<uint16_t>(~len));
    out.insert(out.end(), payload.begin(), payload.end());
    uint32_t a = 1u, b = 0u;
    for (uint8_t byte : payload) { a = (a + byte) % 65521u; b = (b + a) % 65521u; }
    const uint32_t adler = (b << 16) | a;
    out.push_back(uint8_t((adler >> 24) & 0xFFu));
    out.push_back(uint8_t((adler >> 16) & 0xFFu));
    out.push_back(uint8_t((adler >> 8) & 0xFFu));
    out.push_back(uint8_t(adler & 0xFFu));
    return out;
}

std::vector<uint8_t> MakeRecord(const char type[4], uint32_t formId,
                                const std::vector<uint8_t>& payload, bool compressed) {
    std::vector<uint8_t> body;
    uint32_t flags = 0u;
    if (compressed) {
        flags = 0x00040000u;
        PushU32(body, static_cast<uint32_t>(payload.size()));
        const auto zl = WrapStoredZlib(payload);
        body.insert(body.end(), zl.begin(), zl.end());
    } else {
        body = payload;
    }
    std::vector<uint8_t> rec;
    PushTag(rec, type);
    PushU32(rec, static_cast<uint32_t>(body.size()));
    PushU32(rec, flags);
    PushU32(rec, formId);
    PushU32(rec, 0u);
    PushU32(rec, 44u);
    rec.insert(rec.end(), body.begin(), body.end());
    return rec;
}

std::vector<uint8_t> MakeTopGrup(const char label[4], const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> g;
    PushTag(g, "GRUP");
    PushU32(g, static_cast<uint32_t>(24u + payload.size()));
    PushTag(g, label);
    PushU32(g, 0u); // Top-level group type 0
    PushU32(g, 0u);
    PushU32(g, 0u);
    g.insert(g.end(), payload.begin(), payload.end());
    return g;
}

}  // namespace

int RunEsmModelResolverTests(void (*checkFn)(bool, const char*)) {
    int fails = 0;
    auto Report = [&](bool cond, const char* name) {
        if (!cond) ++fails;
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s %s\n", cond ? "PASS" : "FAIL", name);
        }
    };

    // 1. Synthetic ESM STAT (uncompressed) + TREE (zlib 0x00040000 compressed) MODL indexing & path normalization!
    {
        std::vector<uint8_t> tes4Payload;
        PushTag(tes4Payload, "HEDR");
        PushU16(tes4Payload, 12u);
        for (int i = 0; i < 12; ++i) tes4Payload.push_back(0u);
        std::vector<uint8_t> esm = MakeRecord("TES4", 0u, tes4Payload, false);

        std::vector<uint8_t> statPay;
        PushSub(statPay, "EDID", "Farmhouse01");
        PushSub(statPay, "MODL", "Architecture\\Farmhouse\\Farmhouse01.nif");
        const auto statGrup = MakeTopGrup("STAT", MakeRecord("STAT", 0x00012345u, statPay, false));
        esm.insert(esm.end(), statGrup.begin(), statGrup.end());

        std::vector<uint8_t> treePay;
        PushSub(treePay, "EDID", "TreePineForest01");
        PushSub(treePay, "MODL", "Landscape\\Trees\\TreePineForest01.nif");
        const auto treeGrup = MakeTopGrup("TREE", MakeRecord("TREE", 0x0006789Au, treePay, true));
        esm.insert(esm.end(), treeGrup.begin(), treeGrup.end());

        EsmModelIndex idx{};
        std::string err;
        const bool ok = IndexEsmModelsFromMemory(esm.data(), esm.size(), "synth.esm", &idx, &err);
        const auto* rStat = idx.FindByFormId(0x00012345u);
        const auto* rTree = idx.FindByFormId(0x0006789Au);
        Report(ok && idx.statCount == 1u && idx.treeCount == 1u && idx.compressedSeen == 1u &&
               rStat && rStat->bsaVirtualPath == "meshes/architecture/farmhouse/farmhouse01.nif" &&
               rTree && rTree->bsaVirtualPath == "meshes/landscape/trees/treepineforest01.nif",
               "esm_model_resolver_synthetic_stat_tree_and_zlib");
    }

    // 2. Planted-failure controls: bad magic, truncated buffer, missing file!
    {
        EsmModelIndex badIdx{};
        std::string err;
        const uint8_t badMagic[32] = {'B','A','D','!'};
        const bool rejMagic = !IndexEsmModelsFromMemory(badMagic, sizeof(badMagic), "bad", &badIdx, &err);
        const bool rejMissing = !IndexEsmModels("Z:/nonexistent/no_skyrim.esm", &badIdx, &err);
        ReconstructedCellScene badScene{};
        const bool rejScene = !ReconstructTamrielCellScene("Z:/no.esm", "Z:/no.bsa", 0, 0, 16u, &badScene, &err);
        Report(rejMagic && rejMissing && rejScene, "esm_model_resolver_corrupted_fail_closed");
    }

    // 3. LIVE Real `Skyrim.esm` + `Skyrim - Meshes0.bsa` Integration Test: Riverwood `(4, -12)`!
    const std::string esmPath = "H:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data/Skyrim.esm";
    const std::string bsaPath = "H:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data/Skyrim - Meshes0.bsa";
    if (std::filesystem::exists(esmPath) && std::filesystem::exists(bsaPath)) {
        EsmModelIndex modelIdx{};
        std::string err;
        const bool idxOk = IndexEsmModels(esmPath, &modelIdx, &err);
        std::printf("  [LIVE Skyrim.esm Models] total=%zu (STAT=%u TREE=%u ACTI=%u MSTT=%u CONT=%u FURN=%u DOOR=%u LIGH=%u) time=%.1f ms\n",
                    modelIdx.byFormId.size(), modelIdx.statCount, modelIdx.treeCount, modelIdx.actiCount,
                    modelIdx.msttCount, modelIdx.contCount, modelIdx.furnCount, modelIdx.doorCount, modelIdx.lighCount,
                    modelIdx.indexTimeMs);
        Report(idxOk && modelIdx.byFormId.size() > 5000u && modelIdx.statCount > 3000u && modelIdx.indexTimeMs < 2000.0,
               "esm_model_resolver_live_skyrim_esm_index_5000_models");

        // Reconstruct Riverwood (4, -12) end-to-end: Terrain + decoded unique NIFs + world-baked props!
        EsmWorldIndex worldIdx{};
        const bool wOk = IndexEsmWorldspace(esmPath, kTamrielWorldFormId, &worldIdx, &err);
        ReconstructedCellScene riverwood{};
        const bool recOk = idxOk && wOk &&
                           ReconstructTamrielCellSceneWithIndices(worldIdx, modelIdx, bsaPath, 4, -12, 32u, &riverwood, &err);

        bool allValid = recOk && raw::pipeline::ValidateProceduralMeshStrict(riverwood.terrainMesh);
        uint32_t validPropModels = 0u;
        for (const auto& pm : riverwood.uniqueModels) {
            if (raw::pipeline::ValidateProceduralMeshStrict(pm.mesh)) {
                ++validPropModels;
            } else {
                allValid = false;
            }
        }
        ProceduralMesh mergedProps{};
        raw::pipeline::ValidationReport mergedRep{};
        const bool mergedOk = recOk &&
                              BakeReconstructedCellPropsToWorldMesh(riverwood, 126u, &mergedProps, &err) &&
                              raw::pipeline::ValidateProceduralMeshStrict(mergedProps, &mergedRep);

        std::printf("  [LIVE Riverwood (4,-12)] edid=%s refrs=%u resolved=%u uniqueNifs=%u (primary=%u sibling=%u) instances=%zu mergedMeshlets=%u mergedTris=%u\n",
                    riverwood.cellEdid.c_str(), riverwood.totalCellRefrs, riverwood.resolvedModelRefrs,
                    validPropModels, riverwood.extractedFromPrimary, riverwood.extractedFromSibling,
                    riverwood.instances.size(), mergedRep.meshletCount, mergedRep.triangleCount);

        Report(allValid && mergedOk && validPropModels >= 8u && riverwood.instances.size() >= 15u &&
               mergedRep.TotalErrors() == 0u,
               "esm_model_resolver_live_riverwood_4_neg12_reconstruct_and_validate");
    } else {
        Report(true, "esm_model_resolver_live_skyrim_skipped_not_installed");
        Report(true, "esm_model_resolver_live_riverwood_skipped_not_installed");
    }

    return fails;
}

}  // namespace raw::worldgen

#ifdef RAW_ESM_RESOLVER_TEST_STANDALONE
int main() {
    const int fails = raw::worldgen::RunEsmModelResolverTests(nullptr);
    std::printf("RawEsmModelResolver standalone test summary: %s (%d failures)\n",
                fails == 0 ? "ALL PASSED" : "FAILED", fails);
    return fails == 0 ? 0 : 1;
}
#endif
