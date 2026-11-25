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
#include <string>
#include <vector>

#include "../../src/ce_runtime/CeFormatReader.h"
#include "../../src/d3d12_bridge/RawAssetBridge.h"
#include "../../src/engine_rt/rt_zydis_offsets.h"

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

}  // namespace

int main() {
    TestZydisGetters();
    TestGlbImporter();
    TestCeFormats();
    std::printf("ce_runtime_check: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
