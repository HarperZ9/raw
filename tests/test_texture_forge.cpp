// test_texture_forge.cpp: Comprehensive verification suite for `raw::materials::RawTextureForge`
// (all 8 4D-torus seamless procedural PBR textures, edge-to-edge tileability, unit-length tangent normals,
// full mip chains, DX10 .dds write -> RawDdsDecoder bit-exact round-trip, and deterministic reproducibility).
//
// Link directly into `ce_runtime_check` (call `raw::materials::RunTextureForgeTests(Check)`)
// OR compile standalone with `-DRAW_TEXFORGE_TEST_STANDALONE=1`!
// Original work, MIT.

#include "../src/materials/RawTextureForge.h"
#include "../src/worldgen/RawDdsDecoder.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace raw::materials {
namespace {

bool VerifyUnitTangentNormals(const TextureMipLevel& mip) {
    if (mip.width == 0u || mip.height == 0u || mip.rgba8.size() < size_t(mip.width) * mip.height * 4u) return false;
    const size_t n = size_t(mip.width) * mip.height;
    for (size_t i = 0u; i < n; ++i) {
        const uint8_t* p = mip.rgba8.data() + i * 4u;
        const float nx = (float(p[0]) / 255.0f) * 2.0f - 1.0f;
        const float ny = (float(p[1]) / 255.0f) * 2.0f - 1.0f;
        const float nz = (float(p[2]) / 255.0f) * 2.0f - 1.0f;
        const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (!std::isfinite(len) || len < 0.96f || len > 1.04f || nz <= 0.0f) {
            return false;
        }
    }
    return true;
}

}  // namespace

int RunTextureForgeTests(void (*checkFn)(bool, const char*)) {
    int fails = 0;
    auto Report = [&](bool cond, const char* name) {
        if (!cond) ++fails;
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s %s\n", cond ? "PASS" : "FAIL", name);
        }
    };

    const uint32_t res = 128u;
    const uint32_t expectedMips = 8u; // 128 -> 64 -> 32 -> 16 -> 8 -> 4 -> 2 -> 1

    bool allGeneratedOk = true;
    bool allSeamlessOk  = true;
    bool allNormalsOk   = true;
    bool allMipsOk      = true;
    bool allDdsRtOk     = true;

    const std::filesystem::path tmpDir = std::filesystem::temp_directory_path() / "raw_texforge_unit_test";
    std::error_code ec;
    std::filesystem::remove_all(tmpDir, ec);
    std::filesystem::create_directories(tmpDir, ec);

    for (uint32_t k = 0u; k < static_cast<uint32_t>(ProceduralTextureKind::Count); ++k) {
        const auto kind = static_cast<ProceduralTextureKind>(k);
        ProceduralPbrTextureSet set{};
        if (!GenerateProceduralPbrTextureSet(kind, res, 202607u, &set)) {
            allGeneratedOk = false;
            continue;
        }

        // 1. Mip chain completeness (128x128 down to 1x1)
        if (set.albedo.MipCount() != expectedMips ||
            set.normal.MipCount() != expectedMips ||
            set.ormh.MipCount()   != expectedMips ||
            set.albedo.mips.back().width != 1u || set.albedo.mips.back().height != 1u) {
            allMipsOk = false;
        }

        // 2. Seamless wrap tileability: verify wrap seam (0 vs W-1) has zero jump vs interior 1-px gradient on BOTH albedo & ORMH!
        auto checkSeamlessMip = [](const TextureMipLevel& mip) -> bool {
            const TileabilityMetrics t = MeasureSeamlessTileability(mip);
            const uint32_t w = mip.width;
            const uint32_t h = mip.height;
            double intX = 0.0, intY = 0.0;
            // Average 1-pixel gradient across 4 interior slices to get exact expected 1-pixel texture delta
            const uint32_t slices[4] = {w / 5u, (2u * w) / 5u, (3u * w) / 5u, (4u * w) / 5u};
            for (uint32_t sx : slices) {
                for (uint32_t y = 0u; y < h; ++y) {
                    const uint8_t* p0 = mip.rgba8.data() + (size_t(y) * w + sx) * 4u;
                    const uint8_t* p1 = mip.rgba8.data() + (size_t(y) * w + (sx - 1u)) * 4u;
                    intX += (std::fabs(float(p0[0]) - float(p1[0])) +
                             std::fabs(float(p0[1]) - float(p1[1])) +
                             std::fabs(float(p0[2]) - float(p1[2]))) / 3.0;
                }
            }
            for (uint32_t sy : slices) {
                for (uint32_t x = 0u; x < w; ++x) {
                    const uint8_t* q0 = mip.rgba8.data() + (size_t(sy) * w + x) * 4u;
                    const uint8_t* q1 = mip.rgba8.data() + (size_t(sy - 1u) * w + x) * 4u;
                    intY += (std::fabs(float(q0[0]) - float(q1[0])) +
                             std::fabs(float(q0[1]) - float(q1[1])) +
                             std::fabs(float(q0[2]) - float(q1[2]))) / 3.0;
                }
            }
            const float avgIntX = float(intX / double(4u * h));
            const float avgIntY = float(intY / double(4u * w));
            return (t.meanEdgeDiffX <= avgIntX * 1.85f + 4.0f) &&
                   (t.meanEdgeDiffY <= avgIntY * 1.85f + 4.0f);
        };
        if (!checkSeamlessMip(set.albedo.mips[0]) || !checkSeamlessMip(set.ormh.mips[0])) {
            allSeamlessOk = false;
        }

        // 3. Unit-length tangent normals (0.96..1.04, nz > 0) across mip 0 AND mip 2!
        if (!VerifyUnitTangentNormals(set.normal.mips[0]) || !VerifyUnitTangentNormals(set.normal.mips[2])) {
            allNormalsOk = false;
        }

        // 4. Native DX10 .dds write -> RawDdsDecoder::DecodeDdsFile bit-exact pixel parity (mip 0 & mip 3)!
        const std::string ddsAlbPath = (tmpDir / (set.name + "_alb.dds")).string();
        const std::string ddsNrmPath = (tmpDir / (set.name + "_nrm.dds")).string();
        if (!WriteDdsRgba8(ddsAlbPath, set.albedo) || !WriteDdsRgba8(ddsNrmPath, set.normal)) {
            allDdsRtOk = false;
            continue;
        }

        raw::worldgen::DdsImage decAlb0{}, decAlb3{}, decNrm0{};
        raw::worldgen::DdsHeaderInfo hdrAlb{};
        std::string err;
        const bool okDec =
            raw::worldgen::DecodeDdsFile(ddsAlbPath, 0u, &decAlb0, &hdrAlb, &err) &&
            raw::worldgen::DecodeDdsFile(ddsAlbPath, 3u, &decAlb3, nullptr, &err) &&
            raw::worldgen::DecodeDdsFile(ddsNrmPath, 0u, &decNrm0, nullptr, &err);
        if (!okDec || hdrAlb.mipCount != expectedMips || !hdrAlb.isSrgb ||
            decAlb0.rgba8 != set.albedo.mips[0].rgba8 ||
            decAlb3.rgba8 != set.albedo.mips[3].rgba8 ||
            decNrm0.rgba8 != set.normal.mips[0].rgba8) {
            allDdsRtOk = false;
        }
        const auto pbrNrm = raw::worldgen::AnalyzeDecodedTexture(decNrm0, true);
        if (!pbrNrm.isNormalMap || pbrNrm.nonZeroPixels == 0u || !std::isfinite(pbrNrm.toksvigRoughness)) {
            allDdsRtOk = false;
        }
    }

    Report(allGeneratedOk, "texforge_all_8_pbr_materials_generated");
    Report(allSeamlessOk,  "texforge_4d_torus_seamless_tileability");
    Report(allNormalsOk,   "texforge_scharr_unit_tangent_normals_and_mips");
    Report(allMipsOk,      "texforge_full_power_of_two_mip_chains");
    Report(allDdsRtOk,     "texforge_dx10_dds_roundtrip_bit_exact_parity");

    // 5. Deterministic seed reproducibility & distinct-seed divergence!
    ProceduralPbrTextureSet detA{}, detB{}, detOther{};
    const bool okDet =
        GenerateProceduralPbrTextureSet(ProceduralTextureKind::BorealPineBark, 64u, 7777u, &detA) &&
        GenerateProceduralPbrTextureSet(ProceduralTextureKind::BorealPineBark, 64u, 7777u, &detB) &&
        GenerateProceduralPbrTextureSet(ProceduralTextureKind::BorealPineBark, 64u, 8888u, &detOther);
    Report(okDet &&
           detA.albedo.mips[0].rgba8 == detB.albedo.mips[0].rgba8 &&
           detA.normal.mips[0].rgba8 == detB.normal.mips[0].rgba8 &&
           detA.albedo.mips[0].rgba8 != detOther.albedo.mips[0].rgba8,
           "texforge_deterministic_seed_reproducibility");

    // 6. Planted-failure controls: non-power-of-two size, invalid kind, null output!
    ProceduralPbrTextureSet badSet{};
    const bool rejNonPot = !GenerateProceduralPbrTextureSet(ProceduralTextureKind::HexagonalBasalt, 100u, 1u, &badSet);
    const bool rejKind   = !GenerateProceduralPbrTextureSet(ProceduralTextureKind::Count, 64u, 1u, &badSet);
    const bool rejNull   = !GenerateProceduralPbrTextureSet(ProceduralTextureKind::HexagonalBasalt, 64u, 1u, nullptr);
    Report(rejNonPot && rejKind && rejNull, "texforge_planted_failure_controls_reject");

    std::filesystem::remove_all(tmpDir, ec);
    return fails;
}

}  // namespace raw::materials

#ifdef RAW_TEXFORGE_TEST_STANDALONE
int main() {
    const int fails = raw::materials::RunTextureForgeTests(nullptr);
    std::printf("RawTextureForge standalone test summary: %s (%d failures)\n",
                fails == 0 ? "ALL PASSED" : "FAILED", fails);
    return fails == 0 ? 0 : 1;
}
#endif
