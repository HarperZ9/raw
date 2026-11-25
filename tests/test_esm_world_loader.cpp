// test_esm_world_loader.cpp: Synthetic & REAL Skyrim.esm Verification Suite for RawEsmWorldLoader.
// Export: int raw::worldgen::RunEsmWorldLoaderTests(void (*checkFn)(bool, const char*) = nullptr)
// Original work, MIT.

#include "../src/pipeline/RawAssetValidator.h"
#include "../src/worldgen/RawEsmWorldLoader.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace raw::worldgen {
namespace {

void PushU16(std::vector<uint8_t>& b, uint16_t v) {
    const size_t p = b.size(); b.resize(p + 2); std::memcpy(b.data() + p, &v, 2);
}
void PushU32(std::vector<uint8_t>& b, uint32_t v) {
    const size_t p = b.size(); b.resize(p + 4); std::memcpy(b.data() + p, &v, 4);
}
void PushI32(std::vector<uint8_t>& b, int32_t v) {
    const size_t p = b.size(); b.resize(p + 4); std::memcpy(b.data() + p, &v, 4);
}
void PushF32(std::vector<uint8_t>& b, float v) {
    const size_t p = b.size(); b.resize(p + 4); std::memcpy(b.data() + p, &v, 4);
}
void PushTag(std::vector<uint8_t>& b, const char t[4]) {
    b.insert(b.end(), reinterpret_cast<const uint8_t*>(t), reinterpret_cast<const uint8_t*>(t) + 4);
}
void PushSub(std::vector<uint8_t>& b, const char t[4], const std::vector<uint8_t>& data) {
    PushTag(b, t);
    PushU16(b, static_cast<uint16_t>(data.size()));
    b.insert(b.end(), data.begin(), data.end());
}
void PushRec(std::vector<uint8_t>& b, const char t[4], uint32_t flags, uint32_t formId, const std::vector<uint8_t>& payload) {
    PushTag(b, t);
    PushU32(b, static_cast<uint32_t>(payload.size()));
    PushU32(b, flags);
    PushU32(b, formId);
    PushU32(b, 0u);
    PushU16(b, 44u);
    PushU16(b, 0u);
    b.insert(b.end(), payload.begin(), payload.end());
}
void PushGrup(std::vector<uint8_t>& b, uint32_t label, uint32_t gType, const std::vector<uint8_t>& body) {
    PushTag(b, "GRUP");
    PushU32(b, static_cast<uint32_t>(24u + body.size()));
    PushU32(b, label);
    PushU32(b, gType);
    PushU32(b, 0u);
    PushU32(b, 0u);
    b.insert(b.end(), body.begin(), body.end());
}

// Wraps raw bytes into a valid RFC 1950 zlib stream using RFC 1951 stored DEFLATE blocks (btype=0) + Adler-32.
std::vector<uint8_t> WrapZlibStored(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> z;
    z.push_back(0x78u); // CMF: DEFLATE, 32K window
    z.push_back(0x01u); // FLG: (0x7801 % 31 == 0)
    size_t pos = 0;
    while (pos < raw.size()) {
        const size_t chunk = (std::min)(raw.size() - pos, size_t(65535u));
        const bool last = (pos + chunk == raw.size());
        z.push_back(last ? 0x01u : 0x00u);
        const uint16_t len = static_cast<uint16_t>(chunk);
        const uint16_t nlen = static_cast<uint16_t>(~len);
        PushU16(z, len);
        PushU16(z, nlen);
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos),
                 raw.begin() + static_cast<std::ptrdiff_t>(pos + chunk));
        pos += chunk;
    }
    // Adler-32 trailer (big-endian)
    uint32_t s1 = 1u, s2 = 0u;
    for (uint8_t c : raw) {
        s1 = (s1 + c) % 65521u;
        s2 = (s2 + s1) % 65521u;
    }
    const uint32_t adler = (s2 << 16) | s1;
    z.push_back(uint8_t((adler >> 24) & 0xFFu));
    z.push_back(uint8_t((adler >> 16) & 0xFFu));
    z.push_back(uint8_t((adler >> 8)  & 0xFFu));
    z.push_back(uint8_t(adler & 0xFFu));
    return z;
}

std::vector<uint8_t> BuildSyntheticEsm(bool compressLand) {
    std::vector<uint8_t> esm;

    // 1. TES4 header
    std::vector<uint8_t> tes4Body;
    {
        std::vector<uint8_t> hedr;
        PushF32(hedr, 1.70f);
        PushU32(hedr, 10u);
        PushU32(hedr, 0x800u);
        PushSub(tes4Body, "HEDR", hedr);
    }
    PushRec(esm, "TES4", 0x01u, 0u, tes4Body);

    // 2. LTEX GRUP with 2 land textures (LSnow01 -> SnowPack, LGrass01 -> MossMeadow)
    std::vector<uint8_t> ltexGrupBody;
    {
        std::vector<uint8_t> l1, l2;
        const std::string s1 = "LSnow01";
        const std::string s2 = "LGrass01";
        PushSub(l1, "EDID", std::vector<uint8_t>(s1.begin(), s1.end() + 1));
        PushSub(l2, "EDID", std::vector<uint8_t>(s2.begin(), s2.end() + 1));
        PushRec(ltexGrupBody, "LTEX", 0u, 0x00010001u, l1);
        PushRec(ltexGrupBody, "LTEX", 0u, 0x00010002u, l2);
    }
    const uint32_t ltexLbl = uint32_t('L') | (uint32_t('T') << 8) | (uint32_t('E') << 16) | (uint32_t('X') << 24);
    PushGrup(esm, ltexLbl, 0u, ltexGrupBody);

    // 3. WRLD GRUP -> Tamriel (0x3C) -> Block (Type 4) -> SubBlock (Type 5) -> CELL (2, -3) + Type 6 -> Type 9 (LAND + REFR)
    std::vector<uint8_t> cellTempBody;
    {
        // LAND record: VHGT baseOffset=100.0 (=> 800.0u), +1 per row/col ramp, VNML tilted normal, VCLR, BTXT, ATXT/VTXT
        std::vector<uint8_t> landSub;
        {
            std::vector<uint8_t> vhgt;
            PushF32(vhgt, 100.0f);
            for (int y = 0; y < 33; ++y) {
                for (int x = 0; x < 33; ++x) {
                    int8_t d = (x == 0) ? int8_t(y == 0 ? 0 : 2) : int8_t(1);
                    vhgt.push_back(static_cast<uint8_t>(d));
                }
            }
            vhgt.push_back(0); vhgt.push_back(0); vhgt.push_back(0);
            PushSub(landSub, "VHGT", vhgt);

            std::vector<uint8_t> vnml(33 * 33 * 3);
            for (size_t i = 0; i < 33 * 33; ++i) {
                vnml[i * 3 + 0] = static_cast<uint8_t>(int8_t(-8));
                vnml[i * 3 + 1] = static_cast<uint8_t>(int8_t(-16));
                vnml[i * 3 + 2] = static_cast<uint8_t>(int8_t(125));
            }
            PushSub(landSub, "VNML", vnml);

            std::vector<uint8_t> vclr(33 * 33 * 3, 220u);
            PushSub(landSub, "VCLR", vclr);

            // BTXT base grass on all 4 quadrants + ATXT/VTXT snow overlay on NE quadrant
            for (uint8_t q = 0; q < 4u; ++q) {
                std::vector<uint8_t> btxt;
                PushU32(btxt, 0x00010002u); // LGrass01
                btxt.push_back(q); btxt.push_back(0);
                PushU16(btxt, 0);
                PushSub(landSub, "BTXT", btxt);
            }
            std::vector<uint8_t> atxt;
            PushU32(atxt, 0x00010001u); // LSnow01
            atxt.push_back(3u); atxt.push_back(0u);
            PushU16(atxt, 0u);
            PushSub(landSub, "ATXT", atxt);

            std::vector<uint8_t> vtxt;
            for (uint16_t p = 0; p < 289u; ++p) {
                PushU16(vtxt, p);
                PushU16(vtxt, 0u);
                PushF32(vtxt, 0.85f);
            }
            PushSub(landSub, "VTXT", vtxt);
        }

        if (compressLand) {
            std::vector<uint8_t> compPayload;
            PushU32(compPayload, static_cast<uint32_t>(landSub.size()));
            const auto zBytes = WrapZlibStored(landSub);
            compPayload.insert(compPayload.end(), zBytes.begin(), zBytes.end());
            PushRec(cellTempBody, "LAND", 0x00040000u, 0x00020010u, compPayload);
        } else {
            PushRec(cellTempBody, "LAND", 0u, 0x00020010u, landSub);
        }

        // REFR record: Pine tree placement at (8400, -11800, 960)
        std::vector<uint8_t> refrSub;
        const std::string refEdid = "TreePineForest01Ref";
        PushSub(refrSub, "EDID", std::vector<uint8_t>(refEdid.begin(), refEdid.end() + 1));
        std::vector<uint8_t> nameSub; PushU32(nameSub, 0x0001306Du);
        PushSub(refrSub, "NAME", nameSub);
        std::vector<uint8_t> dataSub;
        PushF32(dataSub, 8400.0f); PushF32(dataSub, -11800.0f); PushF32(dataSub, 960.0f);
        PushF32(dataSub, 0.0f);    PushF32(dataSub, 0.0f);      PushF32(dataSub, 1.25f);
        PushSub(refrSub, "DATA", dataSub);
        std::vector<uint8_t> xsclSub; PushF32(xsclSub, 1.35f);
        PushSub(refrSub, "XSCL", xsclSub);
        PushRec(cellTempBody, "REFR", 0u, 0x00030001u, refrSub);
    }

    std::vector<uint8_t> cellChildrenBody;
    PushGrup(cellChildrenBody, 0x00020001u, 9u, cellTempBody);

    std::vector<uint8_t> subBlockBody;
    {
        std::vector<uint8_t> cellSub;
        const std::string cEdid = "SyntheticCell2_m3";
        PushSub(cellSub, "EDID", std::vector<uint8_t>(cEdid.begin(), cEdid.end() + 1));
        std::vector<uint8_t> cData = {0x02u, 0x00u}; // HasWater, exterior
        PushSub(cellSub, "DATA", cData);
        std::vector<uint8_t> xclc; PushI32(xclc, 2); PushI32(xclc, -3); PushU32(xclc, 0u);
        PushSub(cellSub, "XCLC", xclc);
        std::vector<uint8_t> xclw; PushF32(xclw, 820.0f);
        PushSub(cellSub, "XCLW", xclw);
        PushRec(subBlockBody, "CELL", 0u, 0x00020001u, cellSub);
        PushGrup(subBlockBody, 0x00020001u, 6u, cellChildrenBody);
    }

    std::vector<uint8_t> blockBody;
    PushGrup(blockBody, 0u, 5u, subBlockBody);

    std::vector<uint8_t> worldChildrenBody;
    PushGrup(worldChildrenBody, 0u, 4u, blockBody);

    std::vector<uint8_t> wrldGrupBody;
    {
        std::vector<uint8_t> wSub;
        const std::string wEdid = "Tamriel";
        PushSub(wSub, "EDID", std::vector<uint8_t>(wEdid.begin(), wEdid.end() + 1));
        std::vector<uint8_t> dnam; PushF32(dnam, -2048.0f); PushF32(dnam, -14000.0f);
        PushSub(wSub, "DNAM", dnam);
        PushRec(wrldGrupBody, "WRLD", 0u, kTamrielWorldFormId, wSub);
        PushGrup(wrldGrupBody, kTamrielWorldFormId, 1u, worldChildrenBody);
    }
    const uint32_t wrldLbl = uint32_t('W') | (uint32_t('R') << 8) | (uint32_t('L') << 16) | (uint32_t('D') << 24);
    PushGrup(esm, wrldLbl, 0u, wrldGrupBody);
    return esm;
}

}  // namespace

int RunEsmWorldLoaderTests(void (*checkFn)(bool, const char*)) {
    int localFail = 0;
    auto check = [&](bool cond, const char* name) {
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s %s\n", cond ? "PASS" : "FAIL", name);
        }
        if (!cond) ++localFail;
    };

    const auto tmpDir = std::filesystem::temp_directory_path() / "raw_esm_loader_tests";
    std::error_code ec;
    std::filesystem::create_directories(tmpDir, ec);

    // 1. Synthetic uncompressed + zlib-compressed LAND/VHGT/VNML/VCLR/BTXT/ATXT/REFR + Strict Meshlet Validation
    for (int mode = 0; mode < 2; ++mode) {
        const bool compressed = (mode == 1);
        const std::string p = (tmpDir / (compressed ? "synth_comp.esm" : "synth_raw.esm")).string();
        {
            const auto bytes = BuildSyntheticEsm(compressed);
            std::ofstream ofs(p, std::ios::binary);
            ofs.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }

        EsmWorldIndex idx{};
        std::string err;
        const bool okIdx = IndexEsmWorldspace(p, kTamrielWorldFormId, &idx, &err);
        check(okIdx && idx.totalIndexedCells == 1u && idx.totalCellsWithLand == 1u && idx.landTextureEdids.size() == 2u,
              compressed ? "esm_synth_compressed_index" : "esm_synth_uncompressed_index");

        EsmLoadedCell cell{};
        const bool okCell = okIdx && LoadEsmExteriorCell(idx, 2, -3, &cell, &err);
        // Exact VHGT math: corner (0,0) = 100 * 8 = 800.0u; corner (32,32) = (100 + 32*2 + 32*1) * 8 = 196 * 8 = 1568.0u!
        const bool exactVhgt = okCell && cell.land.hasVhgt && cell.land.hasVnml && cell.land.hasVclr &&
                               std::fabs(cell.land.heightsZ[0] - 800.0f) < 1.0e-3f &&
                               std::fabs(cell.land.heightsZ[32 * 33 + 32] - 1568.0f) < 1.0e-3f &&
                               cell.placements.size() == 1u &&
                               cell.placements[0].baseFormId == 0x0001306Du &&
                               std::fabs(cell.placements[0].scale - 1.35f) < 1.0e-3f &&
                               cell.props.size() == 1u && cell.props[0].kind == PropKind::ConiferTree;
        check(exactVhgt, compressed ? "esm_synth_compressed_vhgt_vnml_refr_exact" : "esm_synth_uncompressed_vhgt_vnml_refr_exact");

        // Verify NE quadrant ATXT snow splat weight dominates over SW grass
        const bool splatOk = okCell &&
                             cell.land.splat0[32 * 33 + 32].y > 0.45f && // SnowPack in NE quad
                             cell.land.splat1[0].y > 0.90f;              // MossMeadow in SW quad
        check(splatOk, compressed ? "esm_synth_compressed_splat_quadrants" : "esm_synth_uncompressed_splat_quadrants");

        // Convert to 64v/126t GPU meshlets and pass strict hardware validator!
        ProceduralMesh mesh{};
        TerrainChunkData chunk{};
        const bool okMesh = okCell && BuildMeshFromEsmCell(cell, idx.selectedWorld.defaultWaterHeight, 126u, &mesh, &chunk, &err);
        raw::pipeline::ValidationReport rep{};
        const bool validStrict = okMesh && raw::pipeline::ValidateProceduralMeshStrict(mesh, &rep);
        check(validStrict && rep.TotalErrors() == 0u && rep.meshletCount > 0u &&
              rep.maxMeshletVertsSeen <= 64u && rep.maxMeshletTrisSeen <= 126u,
              compressed ? "esm_synth_compressed_meshlet_strict_pass" : "esm_synth_uncompressed_meshlet_strict_pass");

        if (!compressed && validStrict) {
            const std::string glbPath = (tmpDir / "synth_cell.glb").string();
            const bool okGlb = ExportEsmCellToGlb(cell, idx.selectedWorld.defaultWaterHeight, glbPath, true, &err);
            check(okGlb && std::filesystem::file_size(glbPath) > 512u, "esm_synth_cell_glb_export");
        }
    }

    // 2. Malformed-file fail-closed tests
    {
        const std::string badPath = (tmpDir / "malformed.esm").string();
        {
            std::ofstream ofs(badPath, std::ios::binary);
            ofs << "NOT_A_TES4_PLUGIN_GARBAGE_BYTES_0123456789";
        }
        EsmWorldIndex badIdx{};
        std::string err;
        check(!IndexEsmWorldspace(badPath, kTamrielWorldFormId, &badIdx, &err) && !err.empty(),
              "esm_malformed_header_fail_closed");

        std::vector<uint8_t> badOut;
        const uint8_t garbage[8] = {0xFF, 0xAA, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
        check(!InflateEsmZlib(garbage, sizeof(garbage), 128u, &badOut), "esm_malformed_zlib_fail_closed");
    }

    // 3. REAL Skyrim.esm Integration Test (gracefully skipped only if Skyrim.esm not installed)
    const char* realEsmPath = "H:\\SteamLibrary\\steamapps\\common\\Skyrim Special Edition\\Data\\Skyrim.esm";
    if (std::filesystem::exists(realEsmPath)) {
        const auto t0 = std::chrono::steady_clock::now();
        EsmWorldIndex tamriel{};
        std::string err;
        const bool okRealIdx = IndexEsmWorldspace(realEsmPath, kTamrielWorldFormId, &tamriel, &err);
        const auto msIdx = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - t0).count();

        std::printf("[Skyrim.esm LIVE] Indexed Tamriel in %lld ms: worldspaces=%zu, LTEX=%zu, cells=%u, withLAND=%u, REFRs=%u, compressed=%u\n",
                    static_cast<long long>(msIdx),
                    tamriel.worldspaces.size(),
                    tamriel.landTextureEdids.size(),
                    tamriel.totalIndexedCells,
                    tamriel.totalCellsWithLand,
                    tamriel.totalIndexedRefrs,
                    tamriel.compressedRecordsSeen);

        check(okRealIdx && tamriel.selectedWorld.edid == "Tamriel" &&
              tamriel.totalIndexedCells > 5000u && tamriel.totalCellsWithLand > 4000u,
              "esm_real_skyrim_tamriel_worldspace_index");

        EsmLoadedCell riverwood{}, whiterun{}, hrothgar{};
        const bool okRiv = okRealIdx && LoadEsmExteriorCell(tamriel, 4, -12, &riverwood, &err);
        const bool okWhi = okRealIdx && LoadEsmExteriorCell(tamriel, 5, -3, &whiterun, &err);
        const bool okHro = okRealIdx && LoadEsmExteriorCell(tamriel, 14, -14, &hrothgar, &err);

        if (okRiv && okWhi && okHro) {
            std::printf("[Skyrim.esm LIVE] Riverwood (4,-12) '%s': Z=[%.1f .. %.1f] mean=%.1f u, REFRs=%zu\n",
                        riverwood.meta.edid.c_str(), riverwood.land.minHeightZ, riverwood.land.maxHeightZ,
                        riverwood.land.meanHeightZ, riverwood.placements.size());
            std::printf("[Skyrim.esm LIVE] Whiterun  (5,-3)  '%s': Z=[%.1f .. %.1f] mean=%.1f u, REFRs=%zu\n",
                        whiterun.meta.edid.c_str(), whiterun.land.minHeightZ, whiterun.land.maxHeightZ,
                        whiterun.land.meanHeightZ, whiterun.placements.size());
            std::printf("[Skyrim.esm LIVE] Hrothgar (14,-14) '%s': Z=[%.1f .. %.1f] mean=%.1f u, REFRs=%zu\n",
                        hrothgar.meta.edid.c_str(), hrothgar.land.minHeightZ, hrothgar.land.maxHeightZ,
                        hrothgar.land.meanHeightZ, hrothgar.placements.size());
        }

        check(okRiv && okWhi && okHro, "esm_real_skyrim_load_riverwood_whiterun_hrothgar");
        // True Skyrim White River topography:
        // Throat of the World / High Hrothgar (~+32,037u) >> Riverwood Gorge (~-94u) >> Whiterun Tundra Plains (~-3,836u)!
        check(okRiv && okWhi && okHro &&
              hrothgar.land.maxHeightZ > 30000.0f &&
              hrothgar.land.meanHeightZ > riverwood.land.meanHeightZ + 25000.0f &&
              riverwood.land.meanHeightZ > whiterun.land.meanHeightZ + 2000.0f,
              "esm_real_skyrim_elevation_hierarchy_hrothgar_gt_riverwood_gt_whiterun_plains");

        // Build 64v/126t GPU meshlets for ALL 3 iconic Tamriel cells and pass ValidateProceduralMeshStrict with 0 errors!
        const EsmLoadedCell* liveCells[3] = {&riverwood, &whiterun, &hrothgar};
        const char* liveNames[3] = {
            "esm_real_riverwood_meshlet_strict_pass",
            "esm_real_whiterun_meshlet_strict_pass",
            "esm_real_hrothgar_meshlet_strict_pass"
        };
        for (int i = 0; i < 3; ++i) {
            ProceduralMesh mesh{};
            raw::pipeline::ValidationReport rep{};
            const bool okM = liveCells[i]->land.hasVhgt &&
                             BuildMeshFromEsmCell(*liveCells[i], tamriel.selectedWorld.defaultWaterHeight, 126u, &mesh, nullptr, &err) &&
                             raw::pipeline::ValidateProceduralMeshStrict(mesh, &rep);
            if (okM) {
                std::printf("[Skyrim.esm MESHLET] %s: verts=%u tris=%u meshlets=%u (maxV=%u maxT=%u) gpu=%llu B errors=%u\n",
                            mesh.name.c_str(), rep.vertexCount, rep.triangleCount, rep.meshletCount,
                            rep.maxMeshletVertsSeen, rep.maxMeshletTrisSeen,
                            static_cast<unsigned long long>(rep.estimatedGpuBytes), rep.TotalErrors());
            }
            check(okM && rep.TotalErrors() == 0u && rep.maxMeshletVertsSeen <= 64u && rep.maxMeshletTrisSeen <= 126u,
                  liveNames[i]);
        }
    }

    std::filesystem::remove_all(tmpDir, ec);
    return localFail;
}

}  // namespace raw::worldgen

#ifdef RAW_ESM_WORLD_LOADER_STANDALONE_MAIN
int main() {
    const int fails = raw::worldgen::RunEsmWorldLoaderTests(nullptr);
    std::printf("EsmWorldLoaderTests completed: %s (failures=%d)\n", fails == 0 ? "PASS" : "FAIL", fails);
    return fails == 0 ? 0 : 1;
}
#endif
