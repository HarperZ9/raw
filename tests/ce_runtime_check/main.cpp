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
#include <string>
#include <vector>

#include "../../src/ce_runtime/CeFormatReader.h"
#include "../../src/ce_runtime/CeNifReader.h"
#include "../../src/ce_runtime/CeNavMesh.h"
#include "../../src/d3d12_bridge/RawAssetBridge.h"
#include "../../src/d3d12_bridge/RawTextureBridge.h"
#include "../../src/d3d12_bridge/HardwareProfile.h"
#include "../../src/engine_rt/rt_zydis_offsets.h"
#include "../../src/engine_rt/rt_skeleton_ik.h"
#include "../../src/engine_rt/rt_combat_ai.h"
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
