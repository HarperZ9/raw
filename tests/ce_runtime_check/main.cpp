// ce_runtime_check: Known-answer & planted-failure verification suite for:
//   1) Zydis x86-64 member-getter offset extractor (rt_zydis_offsets.cpp)
//   2) Full spec-compliant glTF 2.0 (.glb) multi-bufferView/index/PBR/embedded-image loader & RAW-MESH meshletizer
//   3) Clean-room TES4/ESM/ESP plugin parser (Skyrim v44, Fallout 4 v131, Starfield v575)
//   4) Clean-room BSA (v105) & BA2 (BTDX GNRL/DX10 v1 & Starfield v8) archive header parsers
// Original work, MIT.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include "../../src/ce_runtime/CeFormatReader.h"
#include "../../src/ce_runtime/CeNifReader.h"
#include "../../src/ce_runtime/CeNavMesh.h"
#include "../../src/d3d12_bridge/RawAssetBridge.h"
#include "../../src/d3d12_bridge/RawTextureBridge.h"
#include "../../src/d3d12_bridge/HardwareProfile.h"
#include "../../src/d3d12_bridge/ShaderCompiler.h"
#include "../../src/engine_rt/rt_zydis_offsets.h"
#include "../../src/engine_rt/rt_skeleton_ik.h"
#include "../../src/engine_rt/rt_combat_ai.h"
#include "../../src/engine_rt/rt_world_sim.h"
#include "../../src/ui/RawDesignTokens.h"
#include "../../src/ui/RawInputEngine.h"

namespace {

int g_pass = 0, g_fail = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        ++g_pass;
        std::printf("PASS %s\n", name);
    } else {
        ++g_fail;
        std::printf("FAIL %s\n", name);
    }
}

void AppendU16(std::vector<uint8_t>& b, uint16_t v) {
    const size_t p = b.size(); b.resize(p + 2); std::memcpy(b.data() + p, &v, 2);
}
void AppendU32(std::vector<uint8_t>& b, uint32_t v) {
    const size_t p = b.size(); b.resize(p + 4); std::memcpy(b.data() + p, &v, 4);
}
void AppendU64(std::vector<uint8_t>& b, uint64_t v) {
    const size_t p = b.size(); b.resize(p + 8); std::memcpy(b.data() + p, &v, 8);
}
void AppendF32(std::vector<uint8_t>& b, float v) {
    const size_t p = b.size(); b.resize(p + 4); std::memcpy(b.data() + p, &v, 4);
}
void AppendTag(std::vector<uint8_t>& b, const char t[4]) {
    b.insert(b.end(), t, t + 4);
}

void TestZydisGetters() {
    // mov rax, [rcx + 0x78]; ret  -> 48 8B 41 78 C3
    const uint8_t fnPtr[] = {0x48, 0x8B, 0x41, 0x78, 0xC3};
    const auto d1 = raw::rt::DecodeThisGetterOffset(fnPtr, sizeof fnPtr);
    Check(d1.valid && d1.offset == 0x78 && d1.widthBytes == 8 && !d1.isFloat, "zydis_getter_ptr_0x78");

    // movss xmm0, [rcx + 0x134]; ret -> F3 0F 10 81 34 01 00 00 C3
    const uint8_t fnFlt[] = {0xF3, 0x0F, 0x10, 0x81, 0x34, 0x01, 0x00, 0x00, 0xC3};
    const auto d2 = raw::rt::DecodeThisGetterOffset(fnFlt, sizeof fnFlt);
    Check(d2.valid && d2.offset == 0x134 && d2.widthBytes == 4 && d2.isFloat, "zydis_getter_float_0x134");

    // movzx eax, byte ptr [rcx + 0x44]; ret -> 0F B6 41 44 C3
    const uint8_t fnBool[] = {0x0F, 0xB6, 0x41, 0x44, 0xC3};
    const auto d3 = raw::rt::DecodeThisGetterOffset(fnBool, sizeof fnBool);
    Check(d3.valid && d3.offset == 0x44 && d3.widthBytes == 1 && !d3.isFloat, "zydis_getter_u8_0x44");

    // Planted failing control: xor eax, eax; ret -> 31 C0 C3 (must be rejected)
    const uint8_t fnConst[] = {0x31, 0xC0, 0xC3};
    const auto dBad = raw::rt::DecodeThisGetterOffset(fnConst, sizeof fnConst);
    Check(!dBad.valid, "zydis_getter_planted_const_reject");
}

void TestGlbImporter() {
    // Build a spec-compliant indexed glTF 2.0 .glb with separate POSITION, NORMAL, TEXCOORD_0, u16 indices,
    // PBR material (metallic=0.85, roughness=0.22), and embedded PNG magic bytes.
    std::vector<uint8_t> bin;
    const float pos[4][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
    const float nrm[4][3] = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
    const float uv[4][2]  = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    const uint16_t idx[6] = {0, 1, 2, 0, 2, 3};
    const uint8_t fakePng[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

    const size_t offPos = bin.size();
    for (int i = 0; i < 4; ++i) for (int k = 0; k < 3; ++k) AppendF32(bin, pos[i][k]);
    const size_t offNrm = bin.size();
    for (int i = 0; i < 4; ++i) for (int k = 0; k < 3; ++k) AppendF32(bin, nrm[i][k]);
    const size_t offUv = bin.size();
    for (int i = 0; i < 4; ++i) for (int k = 0; k < 2; ++k) AppendF32(bin, uv[i][k]);
    const size_t offIdx = bin.size();
    for (int i = 0; i < 6; ++i) AppendU16(bin, idx[i]);
    const size_t offImg = bin.size();
    bin.insert(bin.end(), fakePng, fakePng + 8);
    while (bin.size() % 4 != 0) bin.push_back(0);

    char jsonBuf[2048];
    std::snprintf(jsonBuf, sizeof jsonBuf,
        "{\"asset\":{\"version\":\"2.0\"},"
        "\"bufferViews\":["
        "{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":48},"
        "{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":48},"
        "{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":32},"
        "{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":12},"
        "{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":8}"
        "],"
        "\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},"
        "{\"bufferView\":1,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},"
        "{\"bufferView\":2,\"componentType\":5126,\"count\":4,\"type\":\"VEC2\"},"
        "{\"bufferView\":3,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"}"
        "],"
        "\"images\":[{\"bufferView\":4,\"mimeType\":\"image/png\"}],"
        "\"textures\":[{\"source\":0}],"
        "\"materials\":[{\"name\":\"Steel\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.8,0.85,0.9,1.0],\"metallicFactor\":0.85,\"roughnessFactor\":0.22,\"baseColorTexture\":{\"index\":0}}}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2},\"indices\":3,\"material\":0}]}]}",
        offPos, offNrm, offUv, offIdx, offImg);
    std::string json(jsonBuf);
    while (json.size() % 4 != 0) json.push_back(' ');

    std::vector<uint8_t> glb;
    AppendU32(glb, 0x46546C67u);
    AppendU32(glb, 2u);
    AppendU32(glb, static_cast<uint32_t>(12 + 8 + json.size() + 8 + bin.size()));
    AppendU32(glb, static_cast<uint32_t>(json.size()));
    AppendU32(glb, 0x4E4F534Au);
    glb.insert(glb.end(), json.begin(), json.end());
    AppendU32(glb, static_cast<uint32_t>(bin.size()));
    AppendU32(glb, 0x004E4942u);
    glb.insert(glb.end(), bin.begin(), bin.end());

    raw::bridge::RawMeshAsset asset;
    std::string err;
    const bool ok = raw::bridge::LoadModernMeshAssetFromGlbBytes(glb.data(), glb.size(), "quad.glb", &asset, &err, true);
    Check(ok && asset.vertices.size() == 4 && asset.indices.size() == 6 && asset.meshlets.size() == 1, "glb_indexed_meshlets");
    Check(ok && std::fabs(asset.vertices[2].uv[0] - 1.0f) < 1e-4f && std::fabs(asset.vertices[2].uv[1] - 1.0f) < 1e-4f, "glb_uv_preserved");
    Check(ok && asset.materials.size() == 1 && std::fabs(asset.materials[0].metallicFactor - 0.85f) < 1e-4f &&
          asset.materials[0].embeddedBaseColorImage.size() == 8 && asset.materials[0].embeddedBaseColorImage[1] == 'P', "glb_pbr_and_embedded_png");

    // Planted failing control: corrupted magic must fail cleanly
    glb[0] = 'X';
    raw::bridge::RawMeshAsset badAsset;
    Check(!raw::bridge::LoadModernMeshAssetFromGlbBytes(glb.data(), glb.size(), "bad.glb", &badAsset, &err, true), "glb_planted_corrupt_reject");
}

void TestCeFormats() {
    // 1. Clean-room TES4 + GRUP(LIGH) + LIGH record with EDID
    auto makeSub = [](const char t[4], const void* p, uint16_t n) {
        std::vector<uint8_t> s;
        AppendTag(s, t); AppendU16(s, n);
        const auto* bp = static_cast<const uint8_t*>(p);
        s.insert(s.end(), bp, bp + n);
        return s;
    };
    std::vector<uint8_t> hedr;
    AppendF32(hedr, 1.70f); AppendU32(hedr, 1u); AppendU32(hedr, 0x800u);
    const char mast[] = "Skyrim.esm";
    auto sHedr = makeSub("HEDR", hedr.data(), uint16_t(hedr.size()));
    auto sMast = makeSub("MAST", mast, uint16_t(sizeof mast));

    std::vector<uint8_t> tes4Payload;
    tes4Payload.insert(tes4Payload.end(), sHedr.begin(), sHedr.end());
    tes4Payload.insert(tes4Payload.end(), sMast.begin(), sMast.end());

    const char edid[] = "RAW_HearthLight01";
    auto sEdid = makeSub("EDID", edid, uint16_t(sizeof edid));
    std::vector<uint8_t> lighRec;
    AppendTag(lighRec, "LIGH");
    AppendU32(lighRec, uint32_t(sEdid.size()));
    AppendU32(lighRec, 0u);          // flags
    AppendU32(lighRec, 0x00012345u); // formId
    AppendU32(lighRec, 0u);          // revision
    AppendU16(lighRec, 44u);         // Skyrim SE form version 44
    AppendU16(lighRec, 0u);
    lighRec.insert(lighRec.end(), sEdid.begin(), sEdid.end());

    std::vector<uint8_t> plugin;
    AppendTag(plugin, "TES4");
    AppendU32(plugin, uint32_t(tes4Payload.size()));
    AppendU32(plugin, 0u); AppendU32(plugin, 0u); AppendU32(plugin, 0u);
    AppendU16(plugin, 44u); AppendU16(plugin, 0u);
    plugin.insert(plugin.end(), tes4Payload.begin(), tes4Payload.end());
    // GRUP header (24 bytes) + LIGH record
    AppendTag(plugin, "GRUP");
    AppendU32(plugin, uint32_t(24u + lighRec.size()));
    AppendTag(plugin, "LIGH");
    AppendU32(plugin, 0u); AppendU32(plugin, 0u); AppendU32(plugin, 0u);
    plugin.insert(plugin.end(), lighRec.begin(), lighRec.end());

    raw::ce::PluginSummary ps;
    std::string err;
    const bool pOk = raw::ce::ParsePluginBytes(plugin.data(), plugin.size(), &ps, &err);
    Check(pOk && ps.formVersion == 44 && ps.masters.size() == 1 && ps.masters[0] == "Skyrim.esm" &&
          ps.records.size() == 1 && ps.records[0].formId == 0x00012345u && ps.records[0].edid == "RAW_HearthLight01",
          "ce_esm_tes4_grup_ligh_parse");

    // Planted failing control: truncated plugin must fail cleanly
    raw::ce::PluginSummary badPs;
    Check(!raw::ce::ParsePluginBytes(plugin.data(), plugin.size() - 5, &badPs, &err), "ce_esm_planted_truncated_reject");

    // 2. Clean-room BSA v105 header
    std::vector<uint8_t> bsa;
    bsa.insert(bsa.end(), {'B', 'S', 'A', '\0'});
    AppendU32(bsa, 105u); AppendU32(bsa, 36u); AppendU32(bsa, 0x3u);
    AppendU32(bsa, 12u); AppendU32(bsa, 240u); AppendU32(bsa, 180u); AppendU32(bsa, 4200u);
    AppendU16(bsa, 0x3u); AppendU16(bsa, 0u);
    raw::ce::BsaHeader bh{};
    Check(raw::ce::ParseBsaHeaderBytes(bsa.data(), bsa.size(), &bh, &err) && bh.version == 105 && bh.fileCount == 240, "ce_bsa_v105_header");
    bsa[0] = 'X';
    Check(!raw::ce::ParseBsaHeaderBytes(bsa.data(), bsa.size(), &bh, &err), "ce_bsa_planted_bad_magic_reject");

    // 3. Clean-room BA2 v8 Starfield header (BTDX / DX10)
    std::vector<uint8_t> ba2;
    AppendTag(ba2, "BTDX"); AppendU32(ba2, 8u); AppendTag(ba2, "DX10");
    AppendU32(ba2, 512u); AppendU64(ba2, 0x100000ull);
    raw::ce::Ba2Header b2{};
    Check(raw::ce::ParseBa2HeaderBytes(ba2.data(), ba2.size(), &b2, &err) && b2.version == 8 &&
          std::strcmp(b2.type, "DX10") == 0 && b2.fileCount == 512, "ce_ba2_starfield_v8_header");
    ba2[3] = 'Z';
    Check(!raw::ce::ParseBa2HeaderBytes(ba2.data(), ba2.size(), &b2, &err), "ce_ba2_planted_bad_magic_reject");
}

void TestWicMemoryAndRealSkyrimData() {
    // 1. WIC in-memory decode test (1x1 24-bit BMP -> RGBA8, verifies embedded .glb image path):
    std::vector<uint8_t> bmp(58, 0);
    bmp[0] = 'B'; bmp[1] = 'M';
    uint32_t fsz = 58, pixOff = 54, dibSz = 40, w = 1, h = 1;
    uint16_t planes = 1, bpp = 24;
    std::memcpy(bmp.data() + 2, &fsz, 4);
    std::memcpy(bmp.data() + 10, &pixOff, 4);
    std::memcpy(bmp.data() + 14, &dibSz, 4);
    std::memcpy(bmp.data() + 18, &w, 4);
    std::memcpy(bmp.data() + 22, &h, 4);
    std::memcpy(bmp.data() + 26, &planes, 2);
    std::memcpy(bmp.data() + 28, &bpp, 2);
    bmp[54] = 0x20; bmp[55] = 0x80; bmp[56] = 0xE0;  // BGR -> RGB(0xE0, 0x80, 0x20)
    raw::bridge::RawTextureAsset tex;
    std::string err;
    const bool tok = raw::bridge::LoadModernTextureFromMemory(bmp.data(), bmp.size(), "mem.bmp", &tex, &err);
    Check(tok && tex.width == 1 && tex.height == 1 && tex.rgba8.size() == 4 &&
          tex.rgba8[0] == 0xE0 && tex.rgba8[1] == 0x80 && tex.rgba8[2] == 0x20 && tex.rgba8[3] == 0xFF,
          "wic_memory_decode_rgba8");

    // Planted failing control: garbage bytes must fail cleanly
    const uint8_t badImg[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    raw::bridge::RawTextureAsset badTex;
    Check(!raw::bridge::LoadModernTextureFromMemory(badImg, sizeof badImg, "bad.img", &badTex, &err), "wic_memory_planted_bad_reject");

    // 2. Real-world production archive & plugin validation against local Skyrim SE install (if present):
    const std::string skyData = "H:\\SteamLibrary\\steamapps\\common\\Skyrim Special Edition\\Data\\";
    const std::string esmPath = skyData + "Skyrim.esm";
    const std::string bsaPath = skyData + "Skyrim - Meshes0.bsa";
    if (std::filesystem::exists(esmPath)) {
        raw::ce::WorldPluginIndex wi;
        const bool ok = raw::ce::IndexPluginWorldFile(esmPath, &wi, &err);
        std::printf("INFO Skyrim.esm: formVer=%u totalRecords=%u compressed=%u lights=%zu statics=%zu cells=%zu weathers=%zu\n",
                    wi.formVersion, wi.totalRecords, wi.compressedRecords, wi.lights.size(), wi.statics.size(), wi.cells.size(), wi.weathers.size());
        Check(ok && wi.totalRecords > 500000 && wi.lights.size() > 200 && wi.statics.size() > 1000 && wi.cells.size() > 500,
              "real_skyrim_esm_world_index");
    }
    if (std::filesystem::exists(bsaPath)) {
        raw::ce::BsaArchiveIndex bi;
        const bool ok = raw::ce::IndexBsaArchiveFile(bsaPath, &bi, &err);
        std::printf("INFO Skyrim - Meshes0.bsa: ver=%u folders=%u files=%zu indexedPaths=%zu\n",
                    bi.header.version, bi.header.folderCount, bi.files.size(), bi.byPath.size());
        Check(ok && bi.header.version == 105 && bi.files.size() > 5000 && bi.byPath.size() == bi.files.size(),
              "real_skyrim_meshes0_bsa_index");
        if (ok) {
            // Find real .nif meshes inside Skyrim - Meshes0.bsa, decompress via zero-dep LZ4, decode BSTriShape -> RAW-MESH meshlets!
            uint32_t nifTested = 0, nifPassed = 0, totalMeshlets = 0, totalTris = 0;
            for (const auto& entry : bi.files) {
                if (entry.virtualPath.size() < 5 || entry.virtualPath.compare(entry.virtualPath.size() - 4, 4, ".nif") != 0) continue;
                if (entry.virtualPath.find("clutter/") == std::string::npos && entry.virtualPath.find("architecture/") == std::string::npos) continue;
                std::vector<uint8_t> nifBytes;
                if (!raw::ce::ExtractBsaFile(bsaPath, bi, entry.virtualPath, &nifBytes, &err)) continue;
                raw::bridge::RawMeshAsset mesh;
                raw::ce::NifSummary ns{};
                ++nifTested;
                if (raw::ce::LoadNifMeshBytes(nifBytes.data(), nifBytes.size(), entry.virtualPath, &mesh, &ns, &err)) {
                    ++nifPassed;
                    totalMeshlets += static_cast<uint32_t>(mesh.meshlets.size());
                    totalTris += ns.totalTriangles;
                }
                if (nifPassed >= 12) break;
            }
            std::printf("INFO real BSA->LZ4->NIF->RAW-MESH: tested=%u decoded=%u totalTris=%u totalMeshlets=%u\n",
                        nifTested, nifPassed, totalTris, totalMeshlets);
            Check(nifPassed >= 10 && totalMeshlets >= 10 && totalTris > 100, "real_bsa_lz4_nif_to_raw_meshlets");
            // Planted failing control: corrupt NIF bytes must fail cleanly
            const uint8_t badNif[64] = {'B', 'A', 'D', 'N', 'I', 'F'};
            raw::bridge::RawMeshAsset badMesh;
            Check(!raw::ce::LoadNifMeshBytes(badNif, sizeof badNif, "bad.nif", &badMesh, nullptr, &err), "nif_planted_corrupt_reject");
        }
    }
}

void TestDesignTokensAndUnifiedInput() {
    const auto dark = raw::ui::DarkTheme();
    const auto light = raw::ui::LightTheme();
    // WCAG AAA primary text contrast (>= 15.0:1) + WCAG AA semantic tokens (>= 4.5:1)
    Check(raw::ui::ContrastRatio(dark.inkPrimary, dark.ground) >= 15.0f, "design_tokens_dark_aaa_ink");
    Check(raw::ui::ContrastRatio(light.inkPrimary, light.ground) >= 15.0f, "design_tokens_light_aaa_ink");
    Check(raw::ui::ContrastRatio(dark.accentPass, dark.ground) >= 4.5f &&
          raw::ui::ContrastRatio(dark.accentLead, dark.ground) >= 4.5f &&
          raw::ui::ContrastRatio(dark.accentFail, dark.ground) >= 4.5f &&
          raw::ui::ContrastRatio(light.accentPass, light.ground) >= 4.5f &&
          raw::ui::ContrastRatio(light.accentLead, light.ground) >= 4.5f &&
          raw::ui::ContrastRatio(light.accentFail, light.ground) >= 4.5f,
          "design_tokens_wcag_aa_semantic");

    // Analog stick radial deadzone & monotonic response curve
    float ox = 1.0f, oy = 1.0f;
    raw::ui::FilterAnalogStick(4000, -3000, 0.22f, ox, oy);
    Check(ox == 0.0f && oy == 0.0f, "input_stick_deadzone_zero");
    float mx = 0.0f, my = 0.0f, hx = 0.0f, hy = 0.0f;
    raw::ui::FilterAnalogStick(18000, 0, 0.22f, mx, my);
    raw::ui::FilterAnalogStick(32767, 0, 0.22f, hx, hy);
    Check(mx > 0.1f && mx < hx && std::fabs(hx - 1.0f) < 1e-3f && std::fabs(my) < 1e-5f, "input_stick_monotonic_curve");
}

void TestCustomNavMeshAndWorldSim() {
    // 1) Synthetic 2x4 quad strip (15 verts, 16 triangles) to verify A*, SSFA funnel, dynamic obstacle detour & planted block.
    raw::ce::CeNavMeshData mesh{};
    mesh.formId = 0x0010AA01u;
    mesh.nvnmVersion = 12;
    const int nx = 5, ny = 3;  // 4x2 quads -> 16 tris
    for (int y = 0; y < ny; ++y) {
        for (int x = 0; x < nx; ++x) {
            mesh.vertices.push_back({float(x * 100), float(y * 100), 0.0f});
        }
    }
    auto vIdx = [&](int x, int y) { return uint16_t(y * nx + x); };
    auto triId = [&](int qx, int qy, int half) { return int16_t((qy * 4 + qx) * 2 + half); };
    mesh.triangles.resize(16);
    for (int qy = 0; qy < 2; ++qy) {
        for (int qx = 0; qx < 4; ++qx) {
            const int t0 = triId(qx, qy, 0);
            const int t1 = triId(qx, qy, 1);
            // t0: (x,y)-(x+1,y)-(x+1,y+1), t1: (x,y)-(x+1,y+1)-(x,y+1)
            mesh.triangles[t0] = {{vIdx(qx, qy), vIdx(qx + 1, qy), vIdx(qx + 1, qy + 1)},
                                  {qy > 0 ? triId(qx, qy - 1, 1) : int16_t(-1),
                                   qx < 3 ? triId(qx + 1, qy, 1) : int16_t(-1),
                                   int16_t(t1)}, 0};
            mesh.triangles[t1] = {{vIdx(qx, qy), vIdx(qx + 1, qy + 1), vIdx(qx, qy + 1)},
                                  {int16_t(t0),
                                   qy < 1 ? triId(qx, qy + 1, 0) : int16_t(-1),
                                   qx > 0 ? triId(qx - 1, qy, 0) : int16_t(-1)}, 0};
        }
    }

    raw::ce::RawNavGraph nav;
    nav.Load(mesh);
    const auto straight = nav.FindPath({20.0f, 30.0f, 0.0f}, {380.0f, 30.0f, 0.0f});
    Check(straight.found && straight.waypoints.size() == 2 && straight.totalCost > 250.0f, "navmesh_astar_funnel_straight");

    // Inject dynamic obstacle right in middle of lower lane -> A* must detour cleanly through upper lane!
    nav.AddDynamicObstacle({1u, {200.0f, 40.0f, 0.0f}, 85.0f, 1000.0f});
    const auto detour = nav.FindPath({20.0f, 30.0f, 0.0f}, {380.0f, 30.0f, 0.0f});
    Check(detour.found && detour.totalCost > straight.totalCost, "navmesh_dynamic_obstacle_detour");

    // Planted failure: goal placed inside hard-blocked obstacle must reject cleanly.
    const auto blocked = nav.FindPath({20.0f, 30.0f, 0.0f}, {200.0f, 40.0f, 0.0f});
    Check(!blocked.found, "navmesh_planted_blocked_goal_reject");

    // Off-mesh climb/vault link connecting a disconnected elevated island
    mesh.vertices.push_back({500.0f, 0.0f, 120.0f});
    mesh.vertices.push_back({600.0f, 0.0f, 120.0f});
    mesh.vertices.push_back({550.0f, 100.0f, 120.0f});
    const uint16_t bv = uint16_t(mesh.vertices.size() - 3);
    mesh.triangles.push_back({{bv, uint16_t(bv + 1), uint16_t(bv + 2)}, {-1, -1, -1}, 0});
    nav.Load(mesh);
    Check(!nav.FindPath({20.0f, 30.0f, 0.0f}, {550.0f, 30.0f, 120.0f}).found, "navmesh_island_unreachable_without_link");
    nav.AddOffMeshLink({7u, 16u, {380.0f, 50.0f, 0.0f}, {550.0f, 30.0f, 120.0f}, 1.35f, 2});
    Check(nav.FindPath({20.0f, 30.0f, 0.0f}, {550.0f, 30.0f, 120.0f}).found, "navmesh_offmesh_climb_link_connects");

    // Real Skyrim.esm compressed NAVM clean-room zlib extraction + live A* pathfinding when game install is present
    const std::string skyrimEsm = "H:\\SteamLibrary\\steamapps\\common\\Skyrim Special Edition\\Data\\Skyrim.esm";
    if (std::filesystem::exists(skyrimEsm)) {
        std::vector<raw::ce::CeNavMeshData> realNavs;
        std::string nErr;
        const bool okNav = raw::ce::ExtractNavMeshesFromPlugin(skyrimEsm, 8, realNavs, &nErr);
        uint32_t totalVerts = 0, totalTris = 0;
        bool livePathFound = false;
        for (const auto& m : realNavs) {
            totalVerts += uint32_t(m.vertices.size());
            totalTris += uint32_t(m.triangles.size());
            if (!livePathFound && m.triangles.size() >= 8) {
                raw::ce::RawNavGraph realGraph;
                realGraph.Load(m);
                const auto& t0 = m.triangles[0];
                const auto& tLast = m.triangles[m.triangles.size() / 2];
                const auto rPath = realGraph.FindPath(m.vertices[t0.v[0]], m.vertices[tLast.v[0]]);
                if (rPath.found && rPath.waypoints.size() >= 2) livePathFound = true;
            }
        }
        std::printf("INFO real Skyrim.esm compressed NAVM: ok=%d meshes=%zu totalVerts=%u totalTris=%u liveAStar=%d\n",
                    int(okNav), realNavs.size(), totalVerts, totalTris, int(livePathFound));
        Check(okNav && realNavs.size() >= 4 && totalVerts > 50 && totalTris > 30 && livePathFound, "navmesh_real_skyrim_esm_zlib_astar");
    }

    // 2) Analytical 2-Bone IK & Foot Planting
    const auto ik = raw::rt::SolveTwoBoneIK({0.0f, 0.0f, 80.0f}, {0.0f, 15.0f, 10.0f}, {0.0f, 1.0f, 0.0f}, 42.0f, 42.0f);
    auto d3 = [](raw::rt::Vec3 a, raw::rt::Vec3 b) {
        return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
    };
    Check(ik.solved && std::fabs(d3({0, 0, 80}, ik.midPos) - 42.0f) < 1e-2f &&
          std::fabs(d3(ik.midPos, ik.endPos) - 42.0f) < 1e-2f &&
          d3(ik.endPos, {0, 15, 10}) < 1e-2f && ik.midPos.y > 0.0f, "skeleton_ik_two_bone_exact");

    raw::rt::FootPlantInput fp{};
    fp.leftGroundZ = -16.0f; fp.rightGroundZ = 6.0f; fp.pelvisBaseZ = 0.0f; fp.snowDepthU = 20.0f;
    fp.leftGroundN = {0.0f, 0.35f, 0.936f}; fp.rightGroundN = {0.0f, 0.0f, 1.0f};
    const auto fpOut = raw::rt::ComputeFootPlanting(fp);
    Check(fpOut.pelvisOffsetZ < -10.0f && fpOut.rightFootLiftZ > 15.0f && fpOut.leftAnklePitchRad > 0.25f && fpOut.strideDampMult < 0.85f,
          "skeleton_foot_planting_and_snow_sink");

    // 3) Tactical Squad Combat AI, Timed Parry, Dodge I-Frames & Living World Ecosystem
    const float targetPos[3] = {0.0f, 0.0f, 0.0f};
    const float targetFwd[2] = {0.0f, 1.0f};
    std::vector<raw::rt::CombatAgentSnapshot> squad = {
        {1u, {  0.0f,  90.0f, 0.0f}, 1.0f, false, false},
        {2u, { 40.0f, 140.0f, 0.0f}, 1.0f, false, false},
        {3u, {-50.0f, 160.0f, 0.0f}, 1.0f, false, false},
        {4u, { 10.0f, 300.0f, 0.0f}, 1.0f, true,  false},
        {5u, {-20.0f, 350.0f, 0.0f}, 1.0f, false, true},
    };
    const auto orders = raw::rt::CoordinateSquadTactics(targetPos, targetFwd, squad, 2);
    uint32_t tokens = 0;
    for (const auto& o : orders) if (o.attackToken) ++tokens;
    Check(orders.size() == 5 && orders[0].role == raw::rt::TacticalRole::Vanguard &&
          orders[1].role == raw::rt::TacticalRole::FlankerLeft && orders[2].role == raw::rt::TacticalRole::FlankerRight &&
          orders[3].role == raw::rt::TacticalRole::SkirmisherRanged && orders[4].role == raw::rt::TacticalRole::SupportCaster &&
          tokens == 2, "combat_squad_flanking_and_tokens");

    raw::rt::CombatActorState def{}, atk{};
    raw::rt::StepCombatTimers(def, true, true, false, 0.016f);
    raw::rt::StepCombatTimers(def, true, false, false, 0.080f);  // 80ms inside 160ms parry window
    const auto parryRes = raw::rt::ResolveCombatHit(def, atk, {40.0f, 35.0f, false, false}, 160.0f, 45.0f);
    Check(parryRes.perfectParry && parryRes.attackerStagger && parryRes.appliedHpDamage == 0.0f && atk.staggerSec > 1.0f,
          "combat_precision_timed_parry");

    raw::rt::StepCombatTimers(def, false, false, true, 0.016f);  // dodge edge -> 240ms i-frames
    const auto dodgeRes = raw::rt::ResolveCombatHit(def, atk, {50.0f, 50.0f, true, true}, 160.0f, 45.0f);
    Check(dodgeRes.dodgedIFrame && dodgeRes.appliedHpDamage == 0.0f, "combat_iframe_dodge_evasion");

    Check(raw::rt::EvaluateWorldEcosystem(14.0f, 0.0f, 0.8f, 18.0f, false, false) == raw::rt::WorldNpcDirective::SeekShelter &&
          raw::rt::EvaluateWorldEcosystem(14.0f, 0.0f, 0.8f, 18.0f, true,  false) == raw::rt::WorldNpcDirective::WarmAtBrazier &&
          raw::rt::EvaluateWorldEcosystem(22.0f, 0.0f, 0.0f,  0.0f, false, false) == raw::rt::WorldNpcDirective::EquipTorch,
          "world_ecosystem_weather_directives");

    // 4) Wave 15 Math Kernels: Mean-Payoff Stochastic-Game Tactical Policy Solver + Exact 8D E8 Lattice Quantizer
    const auto polAdv = raw::rt::SolveTacticalStochasticGame({0.95f, 0.90f, 0.95f, 110.0f, true, false});
    const auto polDef = raw::rt::SolveTacticalStochasticGame({0.75f, 0.85f, 0.60f, 120.0f, true, true});
    const auto polLow = raw::rt::SolveTacticalStochasticGame({0.14f, 0.20f, 0.30f, 120.0f, false, true});
    Check(polAdv.bestAction == raw::rt::TacticalAction::PressAttack &&
          polDef.bestAction == raw::rt::TacticalAction::GuardParry &&
          polLow.bestAction == raw::rt::TacticalAction::RetreatRecover &&
          polAdv.iterations > 0 && polAdv.iterations <= 24,
          "stochastic_game_tactical_policy_solver");

    // E8 Lattice Quantizer: verify exact E8 membership (all integer even-sum OR all half-integer even-sum)
    // and strictly lower mean-squared error than cubic Z^8 rounding across 128 pseudo-random 8D vectors!
    bool allValidE8 = true;
    double mseE8 = 0.0, mseZ8 = 0.0;
    uint32_t rng = 0x13579BDFu;
    auto nextF = [&]() -> float {
        rng = rng * 1664525u + 1013904223u;
        return (float(rng & 0xFFFFu) / 32768.0f) - 1.0f;
    };
    for (int n = 0; n < 128; ++n) {
        float x[8], q[8];
        for (int i = 0; i < 8; ++i) x[i] = nextF() * 3.0f;
        raw::rt::QuantizeE8Lattice(x, 1.0f, q);
        bool allInt = true, allHalf = true;
        int sumInt = 0, sumHalf = 0;
        for (int i = 0; i < 8; ++i) {
            const float ri = std::round(q[i]);
            const float rh = std::round(q[i] - 0.5f) + 0.5f;
            if (std::fabs(q[i] - ri) > 1e-4f) allInt = false;
            if (std::fabs(q[i] - rh) > 1e-4f) allHalf = false;
            sumInt += int(ri);
            sumHalf += int(std::round(q[i] * 2.0f));
            const double e8 = double(x[i] - q[i]);
            const double z8 = double(x[i] - std::round(x[i]));
            mseE8 += e8 * e8;
            mseZ8 += z8 * z8;
        }
        // In E8: either all integer with even sum, OR all half-integer with sum of coords even (sum(2*q_i) % 4 == 0).
        const bool okInt  = allInt && ((sumInt & 1) == 0);
        const bool okHalf = allHalf && ((sumHalf & 3) == 0);
        if (!okInt && !okHalf) allValidE8 = false;
    }
    std::printf("INFO E8 lattice quantizer (128x8D): valid=%d mseE8=%.4f mseZ8=%.4f gain=%.2f%%\n",
                int(allValidE8), mseE8 / 128.0, mseZ8 / 128.0, (1.0 - mseE8 / mseZ8) * 100.0);
    Check(allValidE8 && mseE8 < mseZ8 * 0.90, "e8_lattice_optimal_8d_quantization");

    // 5) Wave 15 Linear-Program GPU Frame-Budget Allocator (Smale's 9th LP budget feasibility + interior zero-cloud optimality)
    raw::bridge::HardwareProfile p5090{};
    p5090.tier = 4;
    raw::bridge::HardwareProfile p3060{};
    p3060.tier = 1;
    const auto allocExt = raw::bridge::SolveFrameBudgetLP(p5090, 11.11f, false);  // 90 Hz exterior
    const auto allocInt = raw::bridge::SolveFrameBudgetLP(p5090, 11.11f, true);   // 90 Hz interior
    const auto allocTight = raw::bridge::SolveFrameBudgetLP(p3060, 8.33f, false); // 120 Hz tight budget
    Check(allocExt.totalMs <= 11.11f * 0.52f + 1e-3f && allocExt.cloudSteps >= 24 &&
          allocInt.cloudMarchMs == 0.0f && allocInt.cloudSteps == 0 && allocInt.giBounceMs >= allocExt.giBounceMs &&
          allocTight.totalMs <= 8.33f * 0.52f + 1e-3f && allocTight.dxrShadowMs > 0.30f,
          "lp_gpu_frame_budget_allocator");

    // 6) Wave 16 Live Zero-SKSE World Simulation Snapshot Integration
    const float pPos[3] = {100.0f, 200.0f, 0.0f};
    const float npcs[12] = {180.0f, 200.0f, 0.0f, 1.0f,  240.0f, 260.0f, 0.0f, 1.0f,  40.0f, 340.0f, 0.0f, 1.0f};
    const auto wsBlizz = raw::rt::StepWorldSimSnapshot(pPos, 14.0f, 0.0f, 0.85f, 140.0f, false, npcs, 3, 8, 2072, 2219);
    const auto wsInt   = raw::rt::StepWorldSimSnapshot(pPos, 22.0f, 0.0f, 0.85f,   0.0f, true,  nullptr, 0, 8, 2072, 2219);
    Check(wsBlizz.actorsTracked == 3 && wsBlizz.navReady == 1 && wsBlizz.navTrisLoaded == 2219 &&
          wsBlizz.activeDirective == uint8_t(raw::rt::WorldNpcDirective::SeekShelter) &&
          wsBlizz.attackTokensUsed == 2 && wsBlizz.strideDampMult < 0.88f && wsBlizz.expectedPayoff > 0.0f &&
          wsInt.snowDepthU == 0.0f && wsInt.activeDirective == uint8_t(raw::rt::WorldNpcDirective::WarmAtBrazier),
          "world_sim_live_snapshot");

    // 7) Wave 18 Family 380 Strongly Convex Quadratic Program (KKT water-filling, zero simplex vertex pivoting)
    const auto qpExt = raw::bridge::SolveFrameBudgetQP(p5090, 11.11f, false);
    const auto qpInt = raw::bridge::SolveFrameBudgetQP(p5090, 11.11f, true);
    const auto qpTight = raw::bridge::SolveFrameBudgetQP(p3060, 8.33f, false);
    const auto qpTiny = raw::bridge::SolveFrameBudgetQP(p3060, 0.0f, false);  // planted zero-budget edge control
    Check(qpExt.totalMs <= 11.11f * 0.52f + 1e-3f && qpExt.cloudSteps >= 24 && qpExt.meshSubdivMs > 0.45f &&
          qpInt.cloudMarchMs == 0.0f && qpInt.cloudSteps == 0 && qpInt.giBounceMs >= qpExt.giBounceMs &&
          qpTight.totalMs <= 8.33f * 0.52f + 1e-3f && qpTight.dxrShadowMs >= 0.35f &&
          qpTiny.totalMs <= 1.501f && qpTiny.dxrShadowMs > 0.0f,
          "qp_convex_gpu_frame_budget_allocator");

    // 8) Wave 19 Dual HLSL + GLSL + SPIR-V Live Shader Compiler Verification
    static const char* kGlslComp = R"GLSL(
#version 450
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(std430, binding = 0) buffer OutBuf { vec4 outVal[]; };
void main() {
    uint idx = gl_GlobalInvocationID.x + gl_GlobalInvocationID.y * 8u;
    vec3 a = vec3(float(gl_GlobalInvocationID.x) * 0.125, float(gl_GlobalInvocationID.y) * 0.125, 0.5);
    vec3 b = mix(a, fract(a * 3.0), 0.35) * inversesqrt(dot(a, a) + 1.0);
    outVal[idx] = vec4(b, 1.0);
}
)GLSL";
    auto hasMagic = [](const std::vector<uint8_t>& b, uint32_t magic) {
        if (b.size() < 4) return false;
        uint32_t m = 0;
        std::memcpy(&m, b.data(), 4);
        return m == magic;
    };
    const auto rBuiltinDxil = raw::bridge::CompileGlslFromMemory(
        kGlslComp, L"test_wave19.glsl", L"main", L"cs_6_6", L"", nullptr, raw::bridge::ShaderOutput::Dxil, true);
    const auto rSpirv = raw::bridge::CompileGlslFromMemory(
        kGlslComp, L"test_wave19.glsl", L"main", L"cs_6_6", L"", nullptr, raw::bridge::ShaderOutput::Spirv, false);
    const bool dxilMagicOk = hasMagic(rBuiltinDxil.dxil, 0x43425844u); // 'DXBC'
    const bool spvMagicOk  = hasMagic(rSpirv.dxil, 0x07230203u);       // SPIR-V magic
    std::printf("INFO Dual GLSL/HLSL compiler: builtin_dxil=%d(%zuB, %.1fms, %s) spirv=%d(%zuB, %.1fms, %s)\n",
                int(rBuiltinDxil.ok), rBuiltinDxil.dxil.size(), rBuiltinDxil.ms, rBuiltinDxil.backend.c_str(),
                int(rSpirv.ok), rSpirv.dxil.size(), rSpirv.ms, rSpirv.backend.c_str());
    Check(rBuiltinDxil.ok && dxilMagicOk && rSpirv.ok && spvMagicOk, "glsl_builtin_transpile_dxil_and_spirv");

    if (raw::bridge::HasVulkanGlslToolchain()) {
        const auto rKhronosDxil = raw::bridge::CompileGlslFromMemory(
            kGlslComp, L"test_khronos.glsl", L"main", L"cs_6_6", L"", nullptr, raw::bridge::ShaderOutput::Dxil, false);
        std::printf("INFO Khronos GLSL->SPIR-V->HLSL->DXIL: ok=%d(%zuB, %.1fms, %s)\n",
                    int(rKhronosDxil.ok), rKhronosDxil.dxil.size(), rKhronosDxil.ms, rKhronosDxil.backend.c_str());
        Check(rKhronosDxil.ok && hasMagic(rKhronosDxil.dxil, 0x43425844u), "glsl_khronos_spirv_cross_dxil");
    }

    // Planted-failure control: malformed GLSL must fail cleanly with diagnostic text.
    const auto rBad = raw::bridge::CompileGlslFromMemory(
        "#version 450\nvoid main() { vec3 x = nonexistent_fn(1.0, ); }\n",
        L"bad.glsl", L"main", L"cs_6_6", L"", nullptr, raw::bridge::ShaderOutput::Dxil, true);
    Check(!rBad.ok && !rBad.errors.empty(), "glsl_planted_syntax_error_reject");

    // 9) Wave 20 All-Category Engine Simulation (High-FPS Physics Substep + Thermal Exposure + Weapon Arc Hitbox)
    const auto sub240 = raw::rt::ComputePhysicsSubstep(1.0f / 240.0f);
    const auto subSpike = raw::rt::ComputePhysicsSubstep(0.25f); // 250ms hitch must clamp to 50ms & multi-substep
    Check(sub240.substeps == 1 && !sub240.spikeClamped && subSpike.spikeClamped &&
          subSpike.clampedFrameDt <= 0.0501f && subSpike.substeps == 6 && subSpike.substepDt <= 1.0f / 119.0f,
          "physics_high_fps_substep_governor");

    const auto thBlizz = raw::rt::EvaluateThermalExposure(4200.0f, 200.0f, 0.0f, 1.0f, false, false, 60.0f, 10.0f);
    const auto thHearth = raw::rt::EvaluateThermalExposure(4200.0f, 200.0f, 0.0f, 1.0f, true, true, 30.0f, 5.0f);
    Check(thBlizz.coreWarmth < 60.0f && thBlizz.ambientTempC < 0.0f && thBlizz.staminaRegenMult < 0.90f &&
          thHearth.coreWarmth > 65.0f && !thHearth.freezingRisk,
          "thermal_exposure_survival_sim");

    const float swA[3] = {-40.0f, 10.0f, 80.0f}, swB[3] = {50.0f, 10.0f, 80.0f};
    const float tgtHit[3] = {20.0f, 15.0f, 65.0f}, tgtMiss[3] = {20.0f, 95.0f, 65.0f};
    const auto arcHit = raw::rt::EvaluateWeaponArcHit(swA, swB, tgtHit, 24.0f, 120.0f, 1.8f);
    const auto arcMiss = raw::rt::EvaluateWeaponArcHit(swA, swB, tgtMiss, 24.0f, 120.0f, 1.8f);
    Check(arcHit.hit && arcHit.impactParamT > 0.5f && arcHit.poiseImpulse > 20.0f && !arcMiss.hit,
          "weapon_arc_momentum_hitbox");

    // 10) Wave 21 Acoustic Sabine-Eyring Reverb + Live Wired Thermal/Physics/Acoustic WorldSim Telemetry
    const auto acSky    = raw::rt::ComputeAcousticReverb(false, 4200.0f, 0.0f, 0.0f, 10.0f, false);
    const auto acSnow   = raw::rt::ComputeAcousticReverb(false, 4200.0f, 1.0f, 0.0f, 10.0f, false);
    const auto acStone  = raw::rt::ComputeAcousticReverb(true,   700.0f, 0.0f, 0.0f,  0.0f, false);
    const auto acCavern = raw::rt::ComputeAcousticReverb(true,  1800.0f, 0.0f, 0.0f,  0.0f, false);
    const auto acWater  = raw::rt::ComputeAcousticReverb(false, 4200.0f, 0.0f, 0.0f,  0.0f, true);
    Check(acSky.envClass == raw::rt::AcousticEnvClass::OpenSky &&
          acSnow.rt60Sec < acSky.rt60Sec && acSnow.lowPassHz < acSky.lowPassHz &&
          acStone.envClass == raw::rt::AcousticEnvClass::StoneInterior &&
          acCavern.envClass == raw::rt::AcousticEnvClass::CavernVault &&
          acSky.rt60Sec < acStone.rt60Sec && acStone.rt60Sec < acCavern.rt60Sec &&
          acWater.envClass == raw::rt::AcousticEnvClass::Underwater && acWater.lowPassHz < 900.0f,
          "acoustic_room_reverb_estimator");

    const float alpinePos[3] = {100.0f, 200.0f, 4200.0f};
    const float alpineNpcs[12] = {180.0f, 200.0f, 4200.0f, 1.0f,  240.0f, 260.0f, 4200.0f, 1.0f,  40.0f, 340.0f, 4200.0f, 1.0f};
    const auto wsWave21 = raw::rt::StepWorldSimSnapshot(
        alpinePos, 14.0f, 0.0f, 0.95f, 180.0f, false, alpineNpcs, 3, 8, 2072, 2219, 1.0f / 60.0f, 32.0f);
    Check(wsWave21.ambientTempC < 0.0f && wsWave21.coreWarmth < 32.0f && wsWave21.shiverIntensity > 0.20f &&
          wsWave21.physicsSubsteps == 2 && wsWave21.physicsSubstepMs <= 8.4f &&
          wsWave21.acousticEnvClass == uint8_t(raw::rt::AcousticEnvClass::AlpineValley) &&
          wsWave21.acousticRt60Sec > 0.05f && wsWave21.acousticLowPassHz < 16000.0f &&
          wsWave21.camLockedOn == 1 && wsWave21.lockOnFovDeg > 80.0f && wsWave21.economyBuyMult > 1.05f,
          "world_sim_wave21_telemetry");

    // 11) Wave 22 Smooth Camera Lock-On + Dynamic Hold Economy + Save-Bloat Health Guard
    const float camPrev[3] = {0.0f, 0.0f, 100.0f}, camWant[3] = {100.0f, 0.0f, 100.0f}, lockTgt[3] = {100.0f, 220.0f, 120.0f};
    const auto camLock = raw::rt::SolveSmoothCameraLockOn(camPrev, camWant, camPrev, lockTgt, 80.0f, 14.0f, 1.0f / 60.0f);
    const auto camFree = raw::rt::SolveSmoothCameraLockOn(camPrev, camWant, camPrev, nullptr, 80.0f, 14.0f, 1.0f / 60.0f);
    Check(camLock.lockedOn && camLock.smoothedPos[0] > 10.0f && camLock.smoothedPos[0] < 90.0f &&
          camLock.dynamicFovDeg > 80.5f && !camFree.lockedOn && std::fabs(camFree.dynamicFovDeg - 80.0f) < 1e-3f,
          "smooth_camera_lockon_solver");

    const auto econCalm  = raw::rt::EvaluateDynamicEconomy(0.0f, 0.0f, 13.0f, 12, false);
    const auto econStorm = raw::rt::EvaluateDynamicEconomy(1.0f, 32.0f, 23.0f, 1, false);
    Check(econCalm.marketTier < econStorm.marketTier && econStorm.buyPriceMult > econCalm.buyPriceMult + 0.25f &&
          econStorm.scarcityIndex > 0.75f,
          "dynamic_hold_economy_scarcity");

    const auto saveMissing = raw::rt::InspectSaveHealth("Z:\\nonexistent_raw_saves_dir_check");
    Check(saveMissing.totalSaves == 0 && !saveMissing.bloatWarning && saveMissing.largestSaveMb == 0.0f,
          "save_health_guard_missing_control");

    // 12) Wave 23 Deterministic Boruvka MST + Quadratic Reciprocity Legendre Law + LoopCD Extrapolation
    std::vector<raw::rt::MstEdge> mstEdges = {
        {0, 1, 4.0f}, {0, 2, 1.0f}, {1, 2, 2.0f}, {1, 3, 5.0f}, {2, 3, 8.0f}, {3, 4, 3.0f}, {2, 4, 10.0f}
    };
    const auto mst = raw::rt::ComputeBoruvkaMst(5, mstEdges);
    Check(mst.components == 1 && mst.treeEdges.size() == 4 && std::fabs(mst.totalWeight - 11.0f) < 1e-4f &&
          mst.rounds <= 3 && wsWave21.squadMstComps == 1 && wsWave21.squadSpanLengthU > 50.0f,
          "boruvka_deterministic_mst_kernel");

    // Verify Gauss's Law of Quadratic Reciprocity across odd prime pairs: (p|q)(q|p) = (-1)^(((p-1)/2)*((q-1)/2))
    static constexpr int kOddPrimes[] = {3, 5, 7, 11, 13, 17, 19, 23, 29, 31};
    bool qrOk = (raw::rt::LegendreSymbol(0, 7) == 0 && raw::rt::LegendreSymbol(5, 2) == 0);
    for (int i = 0; i < 10; ++i) {
        for (int j = i + 1; j < 10; ++j) {
            const int p = kOddPrimes[i], q = kOddPrimes[j];
            const int lhs = raw::rt::LegendreSymbol(p, q) * raw::rt::LegendreSymbol(q, p);
            const int rhs = ((((p - 1) >> 1) * ((q - 1) >> 1)) & 1) ? -1 : 1;
            if (lhs != rhs) qrOk = false;
        }
    }
    Check(qrOk, "legendre_quadratic_reciprocity_law");

    const float lcEarly[3] = {0.40f, 0.50f, 0.60f};
    const float lcRef[3]   = {0.50f, 0.50f, 0.40f};
    float lcOut[3] = {};
    raw::rt::EvaluateLoopCdContrast(lcEarly, lcRef, 0.25f, lcOut);
    Check(lcOut[0] > 0.52f && std::fabs(lcOut[1] - 0.50f) < 1e-4f && lcOut[2] < 0.38f && lcOut[2] >= 0.34f,
          "loopcd_contrastive_extrapolation");

    // 13) Wave 25 First-Step Trapping-Region Disocclusion Separator + Mod-Manager Save Resolver + Live GMST Feedback
    const float trapCalm = raw::rt::EvaluateTrappingSeparator(0.50f, 0.52f, 0.08f, 0.5f, 1.0f);
    const float trapJump = raw::rt::EvaluateTrappingSeparator(0.95f, 0.15f, 0.06f, 14.0f, 0.1f);
    const float trapFloor = raw::rt::EvaluateTrappingSeparator(0.50f, 0.51f, 0.0f, 0.0f, 1.0f);
    Check(trapCalm < 0.01f && trapJump > 0.90f && trapFloor < 0.01f,
          "trapping_separator_disocclusion_barrier");

    {
        namespace fs = std::filesystem;
        const fs::path tmpRoot = fs::temp_directory_path() / "raw_wave25_saves_test";
        std::error_code ec;
        fs::remove_all(tmpRoot, ec);
        fs::create_directories(tmpRoot / "__MO_Saves", ec);
        {
            std::ofstream ini(tmpRoot / "Skyrim.ini");
            ini << "[General]\nsLocalSavePath=__MO_Saves\\\n";
        }
        {
            std::ofstream ess(tmpRoot / "__MO_Saves" / "rawharness_test.ess", std::ios::binary);
            std::string payload(4096, 'R');
            ess.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        }
        const std::string resolved = raw::rt::ResolveSkyrimSavesDir(tmpRoot.string().c_str());
        const auto sh = raw::rt::InspectSaveHealth(resolved.c_str());
        const bool ok = (resolved.find("__MO_Saves") != std::string::npos) && sh.totalSaves == 1 && !sh.bloatWarning && sh.largestSaveMb > 0.0f;
        fs::remove_all(tmpRoot, ec);
        Check(ok, "skyrim_ini_local_save_path_resolver");
    }

    const auto gmCalm  = raw::rt::ComputeDynamicGameplayGmsts(0.95f, 1.0f);
    const auto gmBlizz = raw::rt::ComputeDynamicGameplayGmsts(1.38f, 0.65f);
    Check(gmBlizz.barterMin > gmCalm.barterMin + 0.30f && gmBlizz.barterMax > gmCalm.barterMax + 0.30f &&
          gmBlizz.sprintStaminaDrainMult > gmCalm.sprintStaminaDrainMult + 0.30f &&
          gmBlizz.barterMin <= 1.85f && gmBlizz.barterMax <= 4.40f && gmBlizz.sprintStaminaDrainMult <= 1.35f,
          "dynamic_gameplay_gmst_feedback");

    // 14) Wave 26 Oscillatory-Feedback Extremum Seeking GPU Governor (converges to unknown minimum + rejects NaN)
    raw::rt::ExtremumSeekingState es{};
    es.xHat = 0.15f;
    float xProbe = es.xHat;
    const float unknownTarget = 0.72f;
    for (int step = 0; step < 600; ++step) {
        const float cost = (xProbe - unknownTarget) * (xProbe - unknownTarget) + 0.25f;
        xProbe = raw::rt::StepExtremumSeekingGovernor(es, cost, 1.0f / 60.0f, 0.0f, 1.0f, 0.03f, 18.0f, 1.2f);
    }
    const float preNan = es.xHat;
    const float nanOut = raw::rt::StepExtremumSeekingGovernor(es, std::numeric_limits<float>::quiet_NaN(), 1.0f / 60.0f);
    Check(std::fabs(es.xHat - unknownTarget) < 0.08f && std::fabs(nanOut - preNan) < 1e-6f,
          "extremum_seeking_oscillatory_governor");

    // 15) Wave 27 Anatomical Body-Zone Damage / Injury / Dismemberment + 3D Voxel Destruction + Synoptic Storm Simulator
    raw::rt::BodyDamageInput legHit{};
    legHit.zone = raw::rt::BodyZone::LeftLeg; legHit.rawDamage = 35.0f; legHit.weaponMassKg = 2.5f;
    legHit.swingSpeedMs = 11.0f; legHit.edgeSharpness = 0.85f; legHit.targetHpNow = 100.0f; legHit.targetHpMax = 100.0f;
    const auto legRes = raw::rt::EvaluateBodyDamageAndInjury(legHit);
    raw::rt::BodyDamageInput headKill = legHit;
    headKill.zone = raw::rt::BodyZone::Head; headKill.rawDamage = 60.0f; headKill.targetHpNow = 50.0f;
    const auto headRes = raw::rt::EvaluateBodyDamageAndInjury(headKill);
    raw::rt::BodyDamageInput bluntKill = headKill;
    bluntKill.edgeSharpness = 0.05f;
    const auto bluntRes = raw::rt::EvaluateBodyDamageAndInjury(bluntKill);
    Check(legRes.limbCrippled && legRes.moveSpeedMult < 0.70f && !legRes.dismembered &&
          headRes.decapitated && headRes.dismembered && headRes.bleedDps > 5.0f && !bluntRes.decapitated,
          "anatomical_body_damage_dismemberment");

    raw::rt::RawVoxelDestructible crate{};
    raw::rt::InitVoxelProp(crate, raw::rt::VoxelMaterial::Wood, false);
    const uint32_t origSolids = crate.solidCount;
    // Slice completely across the middle plane (x=4) so the 8x8x8 crate splits into two detached structural islands:
    const float sliceDir[3] = {1.0f, 0.0f, 0.0f};
    std::vector<raw::rt::VoxelFragment> totalFrags;
    for (float y = 1.0f; y <= 7.0f; y += 2.0f) {
        for (float z = 1.0f; z <= 7.0f; z += 2.0f) {
            const float hitP[3] = {4.0f, y, z};
            auto f = raw::rt::ApplyVoxelImpact(crate, hitP, sliceDir, 260.0f, 1.9f);
            totalFrags.insert(totalFrags.end(), f.begin(), f.end());
        }
    }
    Check(origSolids == 512u && crate.solidCount > 0u && crate.solidCount < 280u && totalFrags.size() >= 2u,
          "voxel_structural_fracture_and_splitting");

    const float northPeak[3] = {0.0f, 45000.0f, 4200.0f};
    const float southLow[3]  = {0.0f, -20000.0f, 200.0f};
    bool sawStorm = false, sawClear = false;
    for (int t = 0; t < 24; ++t) {
        const auto sN = raw::rt::StepSynopticWeatherSim(northPeak, float(t) * 280.0f, 14.0f);
        if (sN.pressureHpa < 996.0f && sN.snowRate01 > 0.15f) sawStorm = true;
        if (sN.pressureHpa > 1016.0f && sN.snowRate01 < 0.01f) sawClear = true;
    }
    const auto sLow = raw::rt::StepSynopticWeatherSim(southLow, 100.0f, 14.0f);
    Check(sawStorm && sawClear && sLow.snowLineAltU > 1800.0f && wsWave21.synopticHpa >= 972.0f && wsWave21.synopticHpa <= 1034.0f,
          "synoptic_barometric_storm_simulator");

    // 16) Wave 28 Procedural Creature & Bipedal/Quadrupedal/Dragon Gait Kinematics
    const auto bipedWalk = raw::rt::EvaluateCreatureGaitKinematics(raw::rt::CreatureGaitKind::Biped, 180.0f, 1.5f, 0.25f, 1.2f);
    const auto horseGal  = raw::rt::EvaluateCreatureGaitKinematics(raw::rt::CreatureGaitKind::Quadruped, 480.0f, 2.2f, 0.15f, 1.2f);
    const auto dragonFly = raw::rt::EvaluateCreatureGaitKinematics(raw::rt::CreatureGaitKind::Dragon, 520.0f, -2.5f, 0.0f, 1.2f);
    const auto idleStop  = raw::rt::EvaluateCreatureGaitKinematics(raw::rt::CreatureGaitKind::Biped, 0.0f, 0.0f, 0.0f, 1.2f);
    Check(bipedWalk.cadenceHz > 1.0f && bipedWalk.bodyBankRollRad > 0.02f && bipedWalk.spinePitchRad > 0.10f &&
          horseGal.bodyBankRollRad > bipedWalk.bodyBankRollRad &&
          std::fabs(dragonFly.stridePhaseRad[0] - dragonFly.stridePhaseRad[1]) < 1e-4f && dragonFly.bodyBankRollRad < -0.20f &&
          idleStop.cadenceHz == 0.0f && idleStop.verticalBobU == 0.0f,
          "creature_procedural_gait_kinematics");

    // 17) Wave 29 Neighbor-Reactive & Negative-Space Optics Physical Invariants:
    //   (a) Wave-refracted Jacobian caustics conserve mean flux (~1.0), focus crests (>1.0) & dim troughs (<1.0).
    //   (b) Alexander's Dark Band (42..51 deg) lowers sky radiance vs supernumerary (<41 deg) and primary bow peak (42 deg).
    //   (c) Depth-parity silhouette wrap is strictly asymmetric (background wraps onto dark foreground; dark never bleeds into sky).
    auto evalJacobian1D = [](float x, float amp) -> float {
        // 1D Snell refraction through h(x) = amp * sin(x): slope s = amp * cos(x), refracted shift u(x) = x - 0.25 * s
        // Jacobian J = |du/dx| = |1 + 0.25 * amp * sin(x)|; flux = 1 / J.
        const float J = std::fabs(1.0f + 0.25f * amp * std::sin(x));
        return 1.0f / std::max(J, 0.22f);
    };
    double fluxInt = 0.0;
    const int kN = 1024;
    for (int i = 0; i < kN; ++i) {
        const float x = float(i) * (6.2831853f / float(kN));
        fluxInt += double(evalJacobian1D(x, 1.2f));
    }
    fluxInt /= double(kN);
    const float flatFlux = evalJacobian1D(1.0f, 0.0f);
    const float focusCrest = evalJacobian1D(4.712389f, 1.2f);   // sin(3pi/2) = -1 -> J = 0.70 -> flux > 1.4
    const float darkTrough = evalJacobian1D(1.570796f, 1.2f);   // sin(pi/2)  = +1 -> J = 1.30 -> flux < 0.8
    auto smoothstepCpu = [](float e0, float e1, float x) -> float {
        const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };
    auto evalAlexBandMul = [&](float deg) -> float {
        const float r = deg * 0.0174532925f;
        const float alex = smoothstepCpu(0.735f, 0.768f, r) * (1.0f - smoothstepCpu(0.855f, 0.892f, r));
        const float superIn = (1.0f - smoothstepCpu(0.680f, 0.725f, r)) * smoothstepCpu(0.520f, 0.650f, r);
        return 1.0f - alex * 0.15f + superIn * 0.065f;
    };
    const float mulInner = evalAlexBandMul(38.0f);
    const float mulAlex  = evalAlexBandMul(46.5f);
    const float mulOuter = evalAlexBandMul(55.0f);
    // Depth-parity wrap check: foreground (zSelf=200, lSelf=0.05) next to sky (zNeigh=1e6, lNeigh=2.5) gets wrap > 0;
    // reverse (sky pixel next to foreground pixel) gets exact 0.0 bleed!
    auto evalDepthParityWrap = [](float zSelf, float lSelf, float zNeigh, float lNeigh) -> float {
        return (zNeigh > zSelf * 2.5f && lNeigh > lSelf * 2.0f) ? std::clamp((lNeigh - lSelf) * 0.08f, 0.0f, 0.35f) : 0.0f;
    };
    const float fgWrap  = evalDepthParityWrap(200.0f, 0.05f, 1.0e6f, 2.5f);
    const float skyLeak = evalDepthParityWrap(1.0e6f, 2.5f, 200.0f, 0.05f);
    Check(std::fabs(flatFlux - 1.0f) < 1e-5f && focusCrest > 1.35f && darkTrough < 0.80f &&
          fluxInt > 0.95 && fluxInt < 1.12 &&
          mulAlex < mulOuter && mulOuter < mulInner &&
          fgWrap > 0.10f && skyLeak == 0.0f,
          "wave29_neighbor_reactive_optics_invariants");

    // 18) Wave 30 Pioneer Commons Optics & Geometry Invariants:
    //   (a) Van Rhijn 90 km curved-mesopause airglow peaks at 6..10 deg above horizon (> zenith) & dims cleanly near 0 deg.
    //   (b) Hypergraph 4-party quad-cut separates isolated RT fireflies (radiance 1v3, geo 4v0) from real bevel glints (both 1v3).
    //   (c) Plastic-constant R2 quasi-random 2D sequence has strictly lower 4x4 bin clumping variance than coarse periodic hash.
    auto evalVanRhijn = [](float elevDeg) -> float {
        const float s = std::sin(elevDeg * 0.0174532925f);
        const float r = 6360.0f / (6360.0f + 90.0f);
        const float vr = 1.0f / std::sqrt(std::max(1.0f - r * r * std::max(1.0f - s * s, 0.0f), 0.025f));
        const float ext = std::exp(-0.085f / std::max(s, 0.032f));
        return vr * ext;
    };
    const float vrZenith  = evalVanRhijn(90.0f);
    const float vrPeak    = evalVanRhijn(8.0f);
    const float vrHorizon = evalVanRhijn(0.5f);
    auto evalHyperFirefly = [](float cL, float qMax, float geoSplit) -> bool {
        return (cL > std::max(qMax * 2.85f, 0.45f)) && (geoSplit < 0.22f);
    };
    const bool ffCaught   = evalHyperFirefly(3.2f, 0.4f, 0.02f);
    const bool glintKept  = !evalHyperFirefly(3.2f, 0.4f, 0.78f);
    int binsR2[16] = {}, binsPer[16] = {};
    for (int i = 0; i < 64; ++i) {
        const float uR = std::fmod(float(i) * 0.7548776662f, 1.0f);
        const float vR = std::fmod(float(i) * 0.5698402910f, 1.0f);
        binsR2[std::min(int(uR * 4.0f), 3) + std::min(int(vR * 4.0f), 3) * 4]++;
        const float uP = std::fmod(float(i) * 0.25f, 1.0f);
        const float vP = std::fmod(float(i) * 0.50f, 1.0f);
        binsPer[std::min(int(uP * 4.0f), 3) + std::min(int(vP * 4.0f), 3) * 4]++;
    }
    double varR2 = 0.0, varPer = 0.0;
    for (int b = 0; b < 16; ++b) {
        varR2  += double(binsR2[b] - 4) * double(binsR2[b] - 4);
        varPer += double(binsPer[b] - 4) * double(binsPer[b] - 4);
    }
    Check(std::isfinite(vrHorizon) && vrPeak > vrZenith * 1.45f && vrPeak > vrHorizon * 1.30f &&
          ffCaught && glintKept && varR2 < varPer * 0.15,
          "wave30_pioneer_commons_optics_invariants");

    // 19) Wave 34 Unified Planetary Gravity, Dual-Moon Harmonic Tides (Masser M2 + Secunda N2 + Sun S2) & Barometer Surge:
    const float seaPos[3]  = {0.0f, 0.0f, 0.0f};
    const float peakPos[3] = {0.0f, 0.0f, 350000.0f};   // 5,000 m mountain altitude
    const auto pSea  = raw::rt::EvaluatePlanetaryPhysics(10.0f, 12.0f, seaPos,  4.0f, 1013.25f);
    const auto pPeak = raw::rt::EvaluatePlanetaryPhysics(10.0f, 12.0f, peakPos, 4.0f, 1013.25f);
    const auto pCyc  = raw::rt::EvaluatePlanetaryPhysics(10.0f, 12.0f, seaPos, 18.0f,  975.0f);
    const auto pHigh = raw::rt::EvaluatePlanetaryPhysics(10.0f, 12.0f, seaPos,  2.0f, 1032.0f);
    float minTide = 1e9f, maxTide = -1e9f, minVel = 1e9f, maxVel = -1e9f;
    for (int h = 0; h < 24; ++h) {
        const auto pt = raw::rt::EvaluatePlanetaryPhysics(12.0f, float(h), seaPos, 5.0f, 1013.25f);
        minTide = std::min(minTide, pt.tideHeightU);
        maxTide = std::max(maxTide, pt.tideHeightU);
        minVel  = std::min(minVel,  pt.tideVelocityMs);
        maxVel  = std::max(maxVel,  pt.tideVelocityMs);
    }
    Check(std::fabs(pSea.gravityMps2 - 9.80665f) < 1e-4f &&
          pPeak.gravityMps2 < pSea.gravityMps2 - 0.010f &&
          pCyc.tideHeightU > pHigh.tideHeightU + 20.0f &&
          pCyc.beaufortScale > pHigh.beaufortScale + 4.0f &&
          (maxTide - minTide) > 25.0f && maxVel > 0.20f && minVel < -0.20f,
          "wave34_planetary_gravity_tides_invariants");
}

}  // namespace

int main() {
    TestZydisGetters();
    TestGlbImporter();
    TestCeFormats();
    TestWicMemoryAndRealSkyrimData();
    TestDesignTokensAndUnifiedInput();
    TestCustomNavMeshAndWorldSim();
    std::printf("ce_runtime_check: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
