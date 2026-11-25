#pragma once
// RawAssetsTests.h: Self-Contained Known-Answer & Planted-Failure Verification Suite
// for raw::assets (RawOverrideRegistry + RawAssetScaffold + BC7 Mode 6 Cooker).
// Wire into ce_runtime_check/main.cpp in 1 line: raw::assets::RunRawAssetsTests(Check);
// Original work, MIT.

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include "RawAssetScaffold.h"
#include "RawOverrideRegistry.h"

namespace raw::assets {

template <typename CheckFn>
inline void RunRawAssetsTests(CheckFn&& check) {
    // 1. Override Registry: virtual-path normalization, FormID/path/hash lookup, JSON manifest round-trip
    {
        OverrideRegistry reg;
        CustomAssetOverride wall{};
        wall.vanillaPath = "Data\\Meshes\\Architecture\\Whiterun\\WrWall01.NIF";
        wall.formId = 0x00012345u;
        wall.kind = OverrideAssetKind::Combined;
        wall.meshPath = "meshes/whiterun/wrwall01.glb";
        wall.textures.albedoPath = "textures/whiterun/wrwall01_albedo.png";
        wall.textures.normalPath = "textures/whiterun/wrwall01_normal.png";
        wall.textures.rmaohPath  = "textures/whiterun/wrwall01_rmaoh.png";
        wall.textures.emissivePath = "textures/whiterun/wrwall01_emissive.exr";
        wall.materialPreset = "NordicAshlarStone";
        wall.author = "raw-team";
        wall.revision = "v1";

        std::string err;
        const bool regOk = reg.RegisterOverride(wall, &err);
        const auto* byPath = reg.FindByVanillaPath("meshes/architecture/whiterun/wrwall01.nif");
        const auto* byHash = reg.FindByPathHash(HashVirtualPath64("DATA/MESHES/architecture/whiterun/wrwall01.nif"));
        const auto* byFid  = reg.FindByFormId(0x00012345u);

        const std::string exported = reg.ExportManifestJson();
        OverrideRegistry reg2;
        const bool reparsed = reg2.ParseManifestJsonString(exported, {}, &err);
        const auto* rt = reg2.FindByVanillaPath("meshes/architecture/whiterun/wrwall01.nif");

        check(regOk && byPath && byHash == byPath && byFid == byPath &&
              reparsed && rt && rt->formId == 0x00012345u &&
              rt->meshPath == "meshes/whiterun/wrwall01.glb" &&
              rt->textures.rmaohPath == "textures/whiterun/wrwall01_rmaoh.png" &&
              rt->materialPreset == "NordicAshlarStone",
              "raw_assets_override_registry_roundtrip");
    }

    // 2. Mirror Directory Scan & Live Hot-Reload Diff Detection (added, modified, removed)
    {
        const auto tmpDir = std::filesystem::temp_directory_path() / "raw_assets_hotreload_check";
        std::error_code ec;
        std::filesystem::remove_all(tmpDir, ec);
        std::filesystem::create_directories(tmpDir / "meshes/props", ec);
        std::filesystem::create_directories(tmpDir / "textures/props", ec);

        auto writeTxt = [](const std::filesystem::path& p, const std::string& content) {
            std::ofstream f(p, std::ios::binary | std::ios::trunc);
            f.write(content.data(), static_cast<std::streamsize>(content.size()));
        };
        const auto meshA = tmpDir / "meshes/props/mug01.glb";
        const auto texA  = tmpDir / "textures/props/mug01_albedo.png";
        const auto texAN = tmpDir / "textures/props/mug01_normal.png";
        writeTxt(meshA, "glb-v1");
        writeTxt(texA,  "png-albedo-v1");
        writeTxt(texAN, "png-normal-v1");

        OverrideRegistry reg;
        const uint32_t scanned = reg.ScanMirrorDirectory(tmpDir.string());
        const auto* foundMesh = reg.FindByVanillaPath("meshes/props/mug01.nif");
        const auto* foundTex  = reg.FindByVanillaPath("textures/props/mug01.dds");
        const bool initialFound = (foundMesh != nullptr) && (foundTex != nullptr) &&
                                  !foundTex->textures.albedoPath.empty() && !foundTex->textures.normalPath.empty();
        const HotReloadReport idle = reg.PollHotReload(tmpDir.string(), true);

        // Modify mug01.glb (change size + timestamp), add sword02.obj, remove textures
        writeTxt(meshA, "glb-v2-updated-payload-size");
        const auto meshB = tmpDir / "meshes/props/sword02.obj";
        writeTxt(meshB, "v 0 0 0\n");
        std::filesystem::remove(texA, ec);
        std::filesystem::remove(texAN, ec);

        const HotReloadReport diff = reg.PollHotReload(tmpDir.string(), true);
        const bool afterReloadOk = (reg.FindByVanillaPath("meshes/props/sword02.nif") != nullptr) &&
                                   (reg.FindByVanillaPath("textures/props/mug01.dds") == nullptr);
        std::filesystem::remove_all(tmpDir, ec);

        check(scanned == 2u && initialFound && afterReloadOk &&
              idle.TotalChanges() == 0u &&
              diff.added.size() == 1u && diff.modified.size() == 1u && diff.removed.size() == 1u,
              "raw_assets_mirror_scan_and_hotreload_diff");
    }

    // 3. Mesh Quality Gate (valid manifold quad + meshlet packing + planted NaN/non-manifold/winding/tangent failures)
    {
        ScaffoldMesh good{};
        good.name = "unit_quad";
        good.vertices = {
            {{0, 0, 0}, {0, 0, 1}, {1, 0, 0, 1}, {0, 0}},
            {{1, 0, 0}, {0, 0, 1}, {1, 0, 0, 1}, {1, 0}},
            {{1, 1, 0}, {0, 0, 1}, {1, 0, 0, 1}, {1, 1}},
            {{0, 1, 0}, {0, 0, 1}, {1, 0, 0, 1}, {0, 1}},
        };
        good.indices = {0, 1, 2, 0, 2, 3};
        const QualityReport repGood = ValidateHandcraftedMesh(good);

        // Planted failure A: NaN vertex
        ScaffoldMesh badNan = good;
        badNan.vertices[1].pos[0] = std::numeric_limits<float>::quiet_NaN();
        const QualityReport repNan = ValidateHandcraftedMesh(badNan);

        // Planted failure B: inverted winding on adjacent triangle (0,1,2) and (2,0,3) shares directed edge 0->2? Use (0,1,2) and (0,1,3)
        ScaffoldMesh badWind = good;
        badWind.indices = {0, 1, 2, 0, 1, 3};
        const QualityReport repWind = ValidateHandcraftedMesh(badWind);

        // Planted failure C: non-manifold edge shared by 3 triangles
        ScaffoldMesh badManifold = good;
        badManifold.vertices.push_back({{0.5f, 0.5f, 1.0f}, {0, 0, 1}, {1, 0, 0, 1}, {0.5f, 0.5f}});
        badManifold.indices = {0, 1, 2, 0, 2, 3, 0, 2, 4};
        const QualityReport repMan = ValidateHandcraftedMesh(badManifold);

        // Planted failure D: non-orthogonal tangent (parallel to normal)
        ScaffoldMesh badTan = good;
        badTan.vertices[0].tangent[0] = 0.0f;
        badTan.vertices[0].tangent[2] = 1.0f;
        const QualityReport repTan = ValidateHandcraftedMesh(badTan);

        check(repGood.passed && repGood.triangleCount == 2u && repGood.meshletCount == 1u &&
              !repNan.passed && !repWind.passed && repWind.invertedWindingEdges > 0u &&
              !repMan.passed && repMan.nonManifoldEdges > 0u && !repTan.passed,
              "raw_assets_mesh_quality_gate_and_controls");
    }

    // 4. PBR Texture Quality Gate (valid Albedo/Normal/RMAOH + planted non-POT, blown-out albedo, inverted normal)
    {
        const uint32_t w = 32u, h = 32u;
        std::vector<uint8_t> goodAlb(w * h * 4u), goodNrm(w * h * 4u), goodRma(w * h * 4u);
        for (size_t i = 0; i < w * h; ++i) {
            goodAlb[i * 4 + 0] = 140; goodAlb[i * 4 + 1] = 132; goodAlb[i * 4 + 2] = 124; goodAlb[i * 4 + 3] = 255;
            goodNrm[i * 4 + 0] = 128; goodNrm[i * 4 + 1] = 128; goodNrm[i * 4 + 2] = 255; goodNrm[i * 4 + 3] = 255;
            goodRma[i * 4 + 0] = 160; goodRma[i * 4 + 1] =   0; goodRma[i * 4 + 2] = 220; goodRma[i * 4 + 3] = 128;
        }
        const QualityReport rAlb = ValidatePbrTextureMap(w, h, goodAlb, TextureRole::AlbedoSrgb);
        const QualityReport rNrm = ValidatePbrTextureMap(w, h, goodNrm, TextureRole::TangentNormal);
        const QualityReport rRma = ValidatePbrTextureMap(w, h, goodRma, TextureRole::RmaohLinear);

        // Planted failure: clipped white albedo violating PBR energy conservation
        std::vector<uint8_t> badAlb(w * h * 4u, 255u);
        const QualityReport rBadAlb = ValidatePbrTextureMap(w, h, badAlb, TextureRole::AlbedoSrgb);

        // Planted failure: degenerate/inverted tangent normal (nz = -1 -> B = 0)
        std::vector<uint8_t> badNrm = goodNrm;
        badNrm[2] = 0u;
        const QualityReport rBadNrm = ValidatePbrTextureMap(w, h, badNrm, TextureRole::TangentNormal);

        // Planted failure: non-power-of-two dimensions
        std::vector<uint8_t> badPot(30u * 30u * 4u, 128u);
        const QualityReport rBadPot = ValidatePbrTextureMap(30u, 30u, badPot, TextureRole::AlbedoSrgb);

        check(rAlb.passed && rNrm.passed && rRma.passed &&
              !rBadAlb.passed && !rBadNrm.passed && !rBadPot.passed,
              "raw_assets_texture_pbr_quality_gate_and_controls");
    }

    // 5. Authoring Scaffold + Scale Cage OBJ + Clean-Room BC7 Mode 6 Compression & .rawtex Round-Trip
    {
        AssetScaffoldSpec spec{};
        spec.vanillaPath = "Meshes\\Architecture\\Whiterun\\WrWall01.nif";
        spec.formId = 0x00054321u;
        spec.assetSlug = "wrwall01";
        spec.materialPreset = "NordicAshlarStone";
        const std::string scafJson = GenerateAssetScaffoldJson(spec);
        const std::string cageObj  = GenerateScaleCageObj(spec);
        const bool scafOk =
            scafJson.find("\"raw.asset.scaffold.v1\"") != std::string::npos &&
            scafJson.find("\"meshes/wrwall01.glb\"") != std::string::npos &&
            cageObj.find("o wrwall01_cage") != std::string::npos &&
            cageObj.find("f 4 1 5 8") != std::string::npos;

        // Smooth 16x16 gradient texture -> BC7 Mode 6 cook + decode PSNR check + .rawtex serialize/deserialize
        const uint32_t w = 16u, h = 16u;
        std::vector<uint8_t> src(w * h * 4u);
        for (uint32_t y = 0; y < h; ++y) {
            for (uint32_t x = 0; x < w; ++x) {
                const size_t o = (static_cast<size_t>(y) * w + x) * 4u;
                src[o + 0] = static_cast<uint8_t>(40u + x * 12u);
                src[o + 1] = static_cast<uint8_t>(60u + y * 10u);
                src[o + 2] = static_cast<uint8_t>(80u + (x + y) * 5u);
                src[o + 3] = 255u;
            }
        }
        CookedTextureBlob cooked{};
        const bool cookOk = CookTexturePackage(w, h, src, CookedTextureFormat::BC7_UNORM, true, &cooked);
        const auto bytes = SerializeRawTexBlob(cooked);
        CookedTextureBlob decoded{};
        const bool deserOk = DeserializeRawTexBlob(bytes.data(), bytes.size(), &decoded);

        // Decode mip 0 BC7 blocks and verify PSNR > 32 dB
        double mse = 0.0;
        bool allBlocksValid = true;
        for (uint32_t by = 0; by < 4u; ++by) {
            for (uint32_t bx = 0; bx < 4u; ++bx) {
                uint8_t decPx[64]{};
                const uint8_t* blk = &decoded.payload[(static_cast<size_t>(by) * 4u + bx) * 16u];
                if (!DecodeBc7Mode6Block(blk, decPx)) { allBlocksValid = false; break; }
                for (uint32_t y = 0; y < 4u; ++y) {
                    for (uint32_t x = 0; x < 4u; ++x) {
                        const size_t so = (static_cast<size_t>(by * 4u + y) * w + (bx * 4u + x)) * 4u;
                        const size_t bo = (static_cast<size_t>(y) * 4u + x) * 4u;
                        for (int c = 0; c < 4; ++c) {
                            const double d = static_cast<double>(src[so + c]) - static_cast<double>(decPx[bo + c]);
                            mse += d * d;
                        }
                    }
                }
            }
        }
        mse /= static_cast<double>(w * h * 4u);
        const double psnr = (mse > 1.0e-9) ? (10.0 * std::log10((255.0 * 255.0) / mse)) : 99.0;

        // Planted failure: corrupted RWTX magic must fail deserialization
        auto badBytes = bytes;
        badBytes[0] = 'X';
        CookedTextureBlob badDecoded{};
        const bool rejectCorrupt = !DeserializeRawTexBlob(badBytes.data(), badBytes.size(), &badDecoded);

        check(scafOk && cookOk && deserOk && decoded.mipCount == 5u &&
              decoded.contentHash == cooked.contentHash && allBlocksValid && psnr > 32.0 && rejectCorrupt,
              "raw_assets_scaffold_cage_bc7_cooker_and_control");
    }

    // 6. Planted-failure manifest validation controls (rejects malformed JSON, wrong schema, .nif, .dds, bad scale)
    {
        OverrideRegistry r;
        const bool rejMalformed = !r.ParseManifestJsonString("{not valid json");
        const bool rejSchema    = !r.ParseManifestJsonString("{\"schema\":\"wrong.v0\",\"overrides\":[]}");
        const bool rejNifMesh   = !r.ParseManifestJsonString(
            "{\"schema\":\"raw.overrides.v1\",\"overrides\":[{\"vanillaPath\":\"meshes/a.nif\",\"meshPath\":\"meshes/a.nif\"}]}");
        const bool rejDdsTex    = !r.ParseManifestJsonString(
            "{\"schema\":\"raw.overrides.v1\",\"overrides\":[{\"vanillaPath\":\"textures/a.dds\",\"textures\":{\"albedo\":\"textures/a.dds\"}}]}");
        const bool rejNegScale  = !r.ParseManifestJsonString(
            "{\"schema\":\"raw.overrides.v1\",\"overrides\":[{\"vanillaPath\":\"meshes/a.nif\",\"meshPath\":\"meshes/a.glb\",\"scale\":-2.0}]}");

        check(rejMalformed && rejSchema && rejNifMesh && rejDdsTex && rejNegScale,
              "raw_assets_manifest_planted_invalid_controls");
    }
}

}  // namespace raw::assets
