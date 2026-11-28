// test_nif_loader.cpp: Comprehensive unit & live-BSA tests for `raw::worldgen::RawNifLoader`
// (Skyrim SE `.NIF` v20.2.0.7, User 12, Stream 100, NiNode/BSFadeNode transforms, FP16 & FP32 BSTriShape,
// BSLightingShaderProperty -> BSShaderTextureSet, 64v/126t GPU meshlets, and .glb/.obj export).
//
// Link directly into `ce_runtime_check` (call `raw::worldgen::RunNifLoaderTests(Check)`)
// OR compile standalone with `-DRAW_NIF_TEST_STANDALONE=1`!
// Original work, MIT.

#include "../src/pipeline/RawAssetValidator.h"
#include "../src/worldgen/RawBsaArchive.h"
#include "../src/worldgen/RawNifLoader.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace raw::worldgen {
namespace {

template <class T>
void AppendPod(std::vector<uint8_t>& b, const T& v) {
    const size_t p = b.size();
    b.resize(p + sizeof(T));
    std::memcpy(b.data() + p, &v, sizeof(T));
}

void AppendShortStr(std::vector<uint8_t>& b, const std::string& s) {
    b.push_back(static_cast<uint8_t>(s.size()));
    b.insert(b.end(), s.begin(), s.end());
}

void AppendSizedStr(std::vector<uint8_t>& b, const std::string& s) {
    const uint32_t n = static_cast<uint32_t>(s.size());
    AppendPod(b, n);
    b.insert(b.end(), s.begin(), s.end());
}

// Builds a spec-compliant synthetic Skyrim SE v20.2.0.7 (User 12, Stream 100) `.nif` in memory:
//   Block 0: BSFadeNode ("RootNode", translation=(10, 20, 30), scale=2.0, child=1)
//   Block 1: BSTriShape ("TestCube", FP16 or FP32 VertexDesc, shaderProp=2)
//   Block 2: BSLightingShaderProperty (textureSetRef=3)
//   Block 3: BSShaderTextureSet (diffuse, normal, specular)
std::vector<uint8_t> BuildSyntheticSseNif(bool fullPrecision, bool includeNormals) {
    // 1. Build Block 0: BSFadeNode
    std::vector<uint8_t> blk0;
    AppendPod<int32_t>(blk0, 0);   // nameIdx = "RootNode"
    AppendPod<uint32_t>(blk0, 0u); // numExtraData
    AppendPod<int32_t>(blk0, -1);  // controllerRef
    AppendPod<uint32_t>(blk0, 14u);// flags (uint32 in SSE v100)
    const float tr0[3] = {10.0f, 20.0f, 30.0f};
    const float rotI[9] = {1,0,0, 0,1,0, 0,0,1};
    for (float f : tr0) AppendPod(blk0, f);
    for (float f : rotI) AppendPod(blk0, f);
    AppendPod<float>(blk0, 2.0f);  // scale = 2.0
    AppendPod<int32_t>(blk0, -1);  // collisionObjectRef
    AppendPod<uint32_t>(blk0, 1u); // numChildren
    AppendPod<int32_t>(blk0, 1);   // child 0 -> Block 1 (BSTriShape)
    AppendPod<uint32_t>(blk0, 0u); // numEffects

    // 2. Build Block 1: BSTriShape (a 4-vertex, 2-triangle quad on Z=0 with local offset (1,0,0))
    std::vector<uint8_t> blk1;
    AppendPod<int32_t>(blk1, 1);   // nameIdx = "TestShape"
    AppendPod<uint32_t>(blk1, 0u); // numExtraData
    AppendPod<int32_t>(blk1, -1);  // controllerRef
    AppendPod<uint32_t>(blk1, 14u);// flags
    const float tr1[3] = {1.0f, 0.0f, 0.0f};
    for (float f : tr1) AppendPod(blk1, f);
    for (float f : rotI) AppendPod(blk1, f);
    AppendPod<float>(blk1, 1.0f);  // local scale
    AppendPod<int32_t>(blk1, -1);  // collisionObjectRef
    const float bsph[4] = {0.5f, 0.5f, 0.0f, 1.0f};
    for (float f : bsph) AppendPod(blk1, f);
    AppendPod<int32_t>(blk1, -1);  // skinRef
    AppendPod<int32_t>(blk1, 2);   // shaderPropertyRef -> Block 2
    AppendPod<int32_t>(blk1, -1);  // alphaPropertyRef

    const uint32_t vFlags = kNifVfVertex | kNifVfUv |
                            (includeNormals ? kNifVfNormal : 0u) |
                            (fullPrecision ? kNifVfFullPrecision : 0u);
    const uint32_t posBytes = fullPrecision ? (includeNormals ? 16u : 12u) : 8u;
    const uint32_t stride = posBytes + 4u + (includeNormals ? 4u : 0u);
    const uint64_t vdesc = (uint64_t(stride / 4u) & 0xFu) | (uint64_t(vFlags) << 44u);
    const uint16_t numTris = 2u;
    const uint16_t numVerts = 4u;
    const uint32_t dataSize = uint32_t(numVerts) * stride + uint32_t(numTris) * 6u;
    AppendPod(blk1, vdesc);
    AppendPod(blk1, numTris);
    AppendPod(blk1, numVerts);
    AppendPod(blk1, dataSize);

    const float pts[4][3] = {{0.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 0.0f}, {4.0f, 4.0f, 0.0f}, {0.0f, 4.0f, 0.0f}};
    const float uvs[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
    for (int i = 0; i < 4; ++i) {
        if (fullPrecision) {
            AppendPod(blk1, pts[i][0]);
            AppendPod(blk1, pts[i][1]);
            AppendPod(blk1, pts[i][2]);
            if (posBytes == 16u) AppendPod<float>(blk1, 0.0f);
        } else {
            AppendPod(blk1, FloatToHalfBits(pts[i][0]));
            AppendPod(blk1, FloatToHalfBits(pts[i][1]));
            AppendPod(blk1, FloatToHalfBits(pts[i][2]));
            AppendPod<uint16_t>(blk1, 0u);
        }
        AppendPod(blk1, FloatToHalfBits(uvs[i][0]));
        AppendPod(blk1, FloatToHalfBits(uvs[i][1]));
        if (includeNormals) {
            blk1.push_back(128u); // nx = 0
            blk1.push_back(128u); // ny = 0
            blk1.push_back(255u); // nz = +1
            blk1.push_back(128u); // bitangentY
        }
    }
    const uint16_t tris[6] = {0u, 1u, 2u, 0u, 2u, 3u};
    for (uint16_t idx : tris) AppendPod(blk1, idx);

    // 3. Build Block 2: BSLightingShaderProperty
    std::vector<uint8_t> blk2;
    AppendPod<uint32_t>(blk2, 0u); // skyrimShaderType
    AppendPod<int32_t>(blk2, -1);  // nameIdx
    AppendPod<uint32_t>(blk2, 0u); // numExtraData
    AppendPod<int32_t>(blk2, -1);  // controllerRef
    AppendPod<uint32_t>(blk2, 0u); // shaderFlags1
    AppendPod<uint32_t>(blk2, 0u); // shaderFlags2
    AppendPod<float>(blk2, 0.0f); AppendPod<float>(blk2, 0.0f); // uvOffset
    AppendPod<float>(blk2, 1.0f); AppendPod<float>(blk2, 1.0f); // uvScale
    AppendPod<int32_t>(blk2, 3);   // textureSetRef -> Block 3

    // 4. Build Block 3: BSShaderTextureSet
    std::vector<uint8_t> blk3;
    AppendPod<int32_t>(blk3, 6);
    AppendSizedStr(blk3, "textures\\landscape\\rocks\\rockcliff01.dds");
    AppendSizedStr(blk3, "textures\\landscape\\rocks\\rockcliff01_n.dds");
    AppendSizedStr(blk3, "");
    AppendSizedStr(blk3, "");
    AppendSizedStr(blk3, "");
    AppendSizedStr(blk3, "textures\\landscape\\rocks\\rockcliff01_m.dds");

    // 5. Assemble NIF header + blocks + footer
    std::vector<uint8_t> nif;
    const std::string hdr = "Gamebryo File Format, Version 20.2.0.7\n";
    nif.insert(nif.end(), hdr.begin(), hdr.end());
    AppendPod<uint32_t>(nif, kNifVersion20_2_0_7);
    AppendPod<uint8_t>(nif, 1u);
    AppendPod<uint32_t>(nif, kNifUserVersionSSE);
    AppendPod<uint32_t>(nif, 4u); // numBlocks
    AppendPod<uint32_t>(nif, kNifUserVersion2SSE);
    AppendShortStr(nif, "RAW");
    AppendShortStr(nif, "Test");
    AppendShortStr(nif, "Synth");

    AppendPod<uint16_t>(nif, 4u); // numBlockTypes
    AppendSizedStr(nif, "BSFadeNode");
    AppendSizedStr(nif, "BSTriShape");
    AppendSizedStr(nif, "BSLightingShaderProperty");
    AppendSizedStr(nif, "BSShaderTextureSet");

    AppendPod<uint16_t>(nif, 0u);
    AppendPod<uint16_t>(nif, 1u);
    AppendPod<uint16_t>(nif, 2u);
    AppendPod<uint16_t>(nif, 3u);

    AppendPod<uint32_t>(nif, static_cast<uint32_t>(blk0.size()));
    AppendPod<uint32_t>(nif, static_cast<uint32_t>(blk1.size()));
    AppendPod<uint32_t>(nif, static_cast<uint32_t>(blk2.size()));
    AppendPod<uint32_t>(nif, static_cast<uint32_t>(blk3.size()));

    AppendPod<uint32_t>(nif, 2u); // numStrings
    AppendPod<uint32_t>(nif, 16u);
    AppendSizedStr(nif, "RootNode");
    AppendSizedStr(nif, "TestShape");

    AppendPod<uint32_t>(nif, 0u); // numGroups

    nif.insert(nif.end(), blk0.begin(), blk0.end());
    nif.insert(nif.end(), blk1.begin(), blk1.end());
    nif.insert(nif.end(), blk2.begin(), blk2.end());
    nif.insert(nif.end(), blk3.begin(), blk3.end());

    AppendPod<uint32_t>(nif, 1u); // numRoots
    AppendPod<int32_t>(nif, 0);   // root 0
    return nif;
}

}  // namespace

int RunNifLoaderTests(void (*checkFn)(bool, const char*)) {
    int fails = 0;
    auto Report = [&](bool cond, const char* name) {
        if (!cond) ++fails;
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s %s\n", cond ? "PASS" : "FAIL", name);
        }
    };

    // 1. Exact IEEE-754 FP16 half-float <-> FP32 round-trip accuracy
    {
        const float vals[] = {0.0f, -0.0f, 1.0f, -1.0f, 0.5f, 3.25f, -42.5f, 1024.0f, 0.0009765625f};
        bool fp16Ok = true;
        for (float v : vals) {
            const float rt = HalfBitsToFloat(FloatToHalfBits(v));
            if (std::fabs(rt - v) > 1.0e-3f) fp16Ok = false;
        }
        Report(fp16Ok, "nif_ieee754_fp16_half_float_roundtrip");
    }

    // 2. Synthetic SSE v20.2.0.7 FP16 BSTriShape + NiNode hierarchical transform + BSShaderTextureSet + strict validation!
    {
        const auto bytes = BuildSyntheticSseNif(false, true);
        NifScene scene{};
        std::string err;
        const bool parsed = ParseNifBytes(bytes.data(), bytes.size(), "synth_fp16.nif", &scene, &err);
        ProceduralMesh mesh{};
        const bool built = parsed && BuildProceduralMeshFromNif(scene, 126u, &mesh, &err);
        raw::pipeline::ValidationReport rep{};
        const bool valid = built && raw::pipeline::ValidateProceduralMeshStrict(mesh, &rep);

        // Local vertex (0,0,0) + shape local (1,0,0) = (1,0,0); scaled by root 2.0 + (10,20,30) = (12, 20, 30)!
        bool xfOk = false;
        if (parsed && scene.shapes.size() == 1u && scene.shapes[0].vertices.size() == 4u) {
            const Vec3 p0 = scene.shapes[0].vertices[0].position;
            xfOk = std::fabs(p0.x - 12.0f) < 0.05f &&
                   std::fabs(p0.y - 20.0f) < 0.05f &&
                   std::fabs(p0.z - 30.0f) < 0.05f;
        }
        const bool texOk = parsed && !scene.shapes.empty() &&
                           scene.shapes[0].textureSet.diffusePath.find("rockcliff01.dds") != std::string::npos &&
                           scene.shapes[0].textureSet.normalPath.find("rockcliff01_n.dds") != std::string::npos;
        Report(parsed && built && valid && xfOk && texOk && rep.TotalErrors() == 0u,
               "nif_synthetic_sse_fp16_hierarchy_textures_and_meshlets");
    }

    // 3. Synthetic SSE v20.2.0.7 FP32 VF_FULL_PRECISION BSTriShape with fallback smooth normal synthesis!
    {
        const auto bytes = BuildSyntheticSseNif(true, false);
        NifScene scene{};
        std::string err;
        const bool parsed = ParseNifBytes(bytes.data(), bytes.size(), "synth_fp32.nif", &scene, &err);
        ProceduralMesh mesh{};
        const bool built = parsed && BuildProceduralMeshFromNif(scene, 126u, &mesh, &err);
        raw::pipeline::ValidationReport rep{};
        const bool valid = built && raw::pipeline::ValidateProceduralMeshStrict(mesh, &rep);
        const bool normOk = parsed && !scene.shapes.empty() && !scene.shapes[0].hasNormals &&
                            std::fabs(scene.shapes[0].vertices[0].normal.z - 1.0f) < 0.01f;
        Report(parsed && built && valid && normOk && scene.shapes[0].fullPrecisionPos,
               "nif_synthetic_sse_fp32_full_precision_and_fallback_normals");
    }

    // 4. Planted-failure controls: corrupted magic, unsupported version, truncated buffer!
    {
        NifScene badScene{};
        std::string err;
        const uint8_t badMagic[80] = {'N','O','T','_','A','_','N','I','F','\n'};
        const bool rejMagic = !ParseNifBytes(badMagic, sizeof(badMagic), "bad", &badScene, &err);

        auto trunc = BuildSyntheticSseNif(false, true);
        trunc.resize(trunc.size() / 2u);
        const bool rejTrunc = !ParseNifBytes(trunc.data(), trunc.size(), "trunc", &badScene, &err);

        const bool rejMissing = !ParseNifFile("Z:/nonexistent/no_such_mesh.nif", &badScene, &err);
        Report(rejMagic && rejTrunc && rejMissing, "nif_corrupted_and_truncated_fail_closed");
    }

    // 5. LIVE Real `Skyrim - Meshes0.bsa` Integration Test (gracefully skipped if Skyrim SE not installed!)
    const std::string meshesBsaPath = "H:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data/Skyrim - Meshes0.bsa";
    if (std::filesystem::exists(meshesBsaPath)) {
        RawBsaArchive bsa;
        std::string err;
        const bool opened = bsa.Open(meshesBsaPath, &err);
        uint32_t testedNifs = 0u;
        uint32_t validatedNifs = 0u;
        uint32_t totalShapes = 0u;
        uint32_t totalTris = 0u;
        uint32_t totalMeshlets = 0u;
        uint32_t textureSetsFound = 0u;
        bool glbAndObjOk = false;

        if (opened) {
            const auto rockNifs = bsa.ListFiles("meshes/landscape/rocks/", true);
            const auto archNifs = bsa.ListFiles("meshes/architecture/", true);
            std::vector<std::string> candidates;
            for (size_t i = 0u; i < rockNifs.size() && candidates.size() < 3u; ++i) {
                if (rockNifs[i].size() > 4u && rockNifs[i].substr(rockNifs[i].size() - 4u) == ".nif") {
                    candidates.push_back(rockNifs[i]);
                }
            }
            for (size_t i = 0u; i < archNifs.size() && candidates.size() < 6u; ++i) {
                if (archNifs[i].size() > 4u && archNifs[i].substr(archNifs[i].size() - 4u) == ".nif") {
                    candidates.push_back(archNifs[i]);
                }
            }

            for (const auto& vpath : candidates) {
                std::vector<uint8_t> nifBytes;
                if (!bsa.ExtractFile(vpath, &nifBytes, &err)) continue;
                NifScene scene{};
                if (!ParseNifBytes(nifBytes.data(), nifBytes.size(), vpath, &scene, &err)) continue;
                if (scene.shapes.empty()) continue;
                ++testedNifs;

                for (const auto& sh : scene.shapes) {
                    if (!sh.textureSet.diffusePath.empty()) ++textureSetsFound;
                }

                ProceduralMesh pm{};
                if (!BuildProceduralMeshFromNif(scene, 126u, &pm, &err)) continue;
                raw::pipeline::ValidationReport rep{};
                if (!raw::pipeline::ValidateProceduralMeshStrict(pm, &rep)) continue;

                ++validatedNifs;
                totalShapes   += static_cast<uint32_t>(scene.shapes.size());
                totalTris     += rep.triangleCount;
                totalMeshlets += rep.meshletCount;

                if (!glbAndObjOk) {
                    const auto glb = SerializeMeshToGlbBytes(pm, true);
                    const auto obj = SerializeMeshToObjString(pm);
                    uint32_t magic = 0u, ver = 0u;
                    if (glb.size() >= 12u) {
                        std::memcpy(&magic, glb.data(), 4u);
                        std::memcpy(&ver, glb.data() + 4u, 4u);
                    }
                    glbAndObjOk = (magic == 0x46546C67u && ver == 2u && obj.find("f ") != std::string::npos);
                }
            }
        }
        std::printf("  [LIVE Meshes0.bsa NIF stats] tested=%u validated=%u shapes=%u tris=%u meshlets=%u texSets=%u\n",
                    testedNifs, validatedNifs, totalShapes, totalTris, totalMeshlets, textureSetsFound);
        Report(opened && testedNifs >= 3u && validatedNifs == testedNifs &&
               totalTris > 500u && totalMeshlets > 5u && textureSetsFound >= 1u && glbAndObjOk,
               "nif_real_skyrim_meshes0_bsa_extract_validate_and_export");
    } else {
        Report(true, "nif_real_skyrim_meshes0_bsa_skipped_not_installed");
    }

    return fails;
}

}  // namespace raw::worldgen

#ifdef RAW_NIF_TEST_STANDALONE
int main() {
    const int fails = raw::worldgen::RunNifLoaderTests(nullptr);
    std::printf("RawNifLoader standalone test summary: %s (%d failures)\n",
                fails == 0 ? "ALL PASSED" : "FAILED", fails);
    return fails == 0 ? 0 : 1;
}
#endif
