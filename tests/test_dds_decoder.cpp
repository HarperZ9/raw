// test_dds_decoder.cpp: Comprehensive unit & live-BSA tests for `raw::worldgen::RawDdsDecoder`
// (BC1/BC2/BC3/BC4/BC5/BC7/RGBA8/BGRA8 DX9 & DX10 DDS decoding, unit-Z BC5 normal reconstruction,
// Toksvig roughness PBR inspection, and real Skyrim SE `Skyrim - Textures0.bsa` integration).
//
// Link directly into `ce_runtime_check` (call `raw::worldgen::RunDdsDecoderTests(Check)`)
// OR compile standalone with `-DRAW_DDS_TEST_STANDALONE=1`!
// Original work, MIT.

#include "../src/worldgen/RawBsaArchive.h"
#include "../src/worldgen/RawDdsDecoder.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace raw::worldgen {
namespace {

struct BitWriter128 {
    uint64_t lo = 0ull;
    uint64_t hi = 0ull;
    uint32_t pos = 0u;

    void Write(uint32_t val, uint32_t bits) {
        for (uint32_t i = 0u; i < bits; ++i) {
            const uint64_t b = uint64_t((val >> i) & 1u);
            if (pos < 64u) lo |= (b << pos);
            else           hi |= (b << (pos - 64u));
            ++pos;
        }
    }

    void Flush(uint8_t out[16]) const {
        std::memcpy(out, &lo, 8u);
        std::memcpy(out + 8u, &hi, 8u);
    }
};

std::vector<uint8_t> WrapDds4x4(uint32_t fourCC, uint32_t dxgiFormat, const uint8_t* blockData, size_t blockLen) {
    const bool isDx10 = (dxgiFormat != 0u);
    std::vector<uint8_t> dds(isDx10 ? 148u : 128u, 0u);
    auto put32 = [&](size_t off, uint32_t v) { std::memcpy(dds.data() + off, &v, 4u); };
    put32(0u, kDdsMagic);
    put32(4u, 124u);          // dwSize
    put32(8u, 0x1007u);       // DDSD_CAPS | HEIGHT | WIDTH | PIXELFORMAT
    put32(12u, 4u);           // height
    put32(16u, 4u);           // width
    put32(28u, 1u);           // mipCount
    put32(76u, 32u);          // ddspf.dwSize
    put32(80u, 0x4u);         // DDPF_FOURCC
    put32(84u, isDx10 ? 0x30315844u : fourCC); // "DX10" or legacy FourCC
    if (isDx10) {
        put32(128u, dxgiFormat);
        put32(132u, 3u);      // D3D10_RESOURCE_DIMENSION_TEXTURE2D
        put32(140u, 1u);      // arraySize
    }
    dds.insert(dds.end(), blockData, blockData + blockLen);
    return dds;
}

}  // namespace

int RunDdsDecoderTests(void (*checkFn)(bool, const char*)) {
    int fails = 0;
    auto Report = [&](bool cond, const char* name) {
        if (!cond) ++fails;
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s %s\n", cond ? "PASS" : "FAIL", name);
        }
    };

    // 1. Synthetic BC1 (DXT1) 4-color & 3-color+alpha decoding!
    {
        // 4-color: c0 = pure red (0xF800), c1 = pure blue (0x001F), c0 > c1
        uint8_t blk4[8] = {0x00u, 0xF8u, 0x1Fu, 0x00u, 0xE4u, 0x00u, 0x00u, 0x00u}; // idx 0,1,2,3 on first 4 pixels
        uint8_t rgba4[64]{};
        DecodeBc1Block(blk4, rgba4, false);
        const bool ok4 = (rgba4[0] == 255 && rgba4[2] == 0 && rgba4[3] == 255) &&   // p0 = red
                         (rgba4[4] == 0 && rgba4[6] == 255 && rgba4[7] == 255) &&   // p1 = blue
                         (rgba4[8] >= 168 && rgba4[10] >= 80 && rgba4[11] == 255);  // p2 = 2/3 red + 1/3 blue

        // 3-color + 1-bit alpha: c0 <= c1 -> index 3 must be transparent (0,0,0,0)!
        uint8_t blk3[8] = {0x1Fu, 0x00u, 0xF8u, 0x00u, 0xE4u, 0x00u, 0x00u, 0x00u};
        uint8_t rgba3[64]{};
        DecodeBc1Block(blk3, rgba3, false);
        const bool ok3 = (rgba3[12] == 0 && rgba3[13] == 0 && rgba3[14] == 0 && rgba3[15] == 0);
        Report(ok4 && ok3, "dds_bc1_4color_and_1bit_alpha_decode");
    }

    // 2. Synthetic BC2 (DXT3) & BC3 (DXT5) interpolated alpha DDS decode!
    {
        // BC3 block: alpha endpoints a0=210, a1=0 (a0 > a1 -> 8-level interp), all idx=2 -> (6*210+0)/7 = 180
        // Color block: c0=0x07E0 (green), c1=0x0000, all idx=0 -> (0, 255, 0)
        uint8_t bc3[16] = {210u, 0u, 0x92u, 0x49u, 0x24u, 0x92u, 0x49u, 0x24u,
                           0xE0u, 0x07u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
        const auto dds = WrapDds4x4(0x35545844u, 0u, bc3, 16u); // "DXT5"
        DdsImage img{};
        DdsHeaderInfo hdr{};
        std::string err;
        const bool ok = DecodeDdsBytes(dds.data(), dds.size(), 0u, &img, &hdr, &err);
        const bool pixOk = ok && img.format == DdsFormat::BC3_UNORM &&
                           img.rgba8[0] == 0 && img.rgba8[1] == 255 && img.rgba8[2] == 0 &&
                           std::abs(int(img.rgba8[3]) - 180) <= 1;
        Report(pixOk, "dds_bc3_dxt5_interpolated_alpha_decode");
    }

    // 3. Synthetic BC4 & BC5 (ATI2) verifying exact unit-Z tangent normal reconstruction + Toksvig analysis!
    {
        // BC5 block: rx = 204 (nx = 204/255*2 - 1 = +0.60), gy = 128 (ny ~ 0.004) -> nz = sqrt(1 - 0.36) = 0.80 -> B = 230!
        uint8_t bc5[16] = {204u, 204u, 0,0,0,0,0,0,  128u, 128u, 0,0,0,0,0,0};
        const auto dds = WrapDds4x4(0x32495441u, 0u, bc5, 16u); // "ATI2"
        DdsImage img{};
        std::string err;
        const bool ok = DecodeDdsBytes(dds.data(), dds.size(), 0u, &img, nullptr, &err);
        const auto pbr = AnalyzeDecodedTexture(img, true);
        const bool nzOk = ok && img.rgba8[0] == 204u && img.rgba8[1] == 128u &&
                          std::abs(int(img.rgba8[2]) - 230) <= 2 &&
                          pbr.isNormalMap && pbr.meanNormalLength > 0.99f && pbr.toksvigRoughness <= 0.08f;

        // BC4 block: constant 180 -> (180, 180, 180, 255)
        uint8_t bc4[8] = {180u, 180u, 0,0,0,0,0,0};
        uint8_t rgba4[64]{};
        DecodeBc4Block(bc4, rgba4);
        const bool bc4Ok = (rgba4[0] == 180u && rgba4[1] == 180u && rgba4[2] == 180u && rgba4[3] == 255u);
        Report(nzOk && bc4Ok, "dds_bc4_and_bc5_unit_z_normal_reconstruction");
    }

    // 4. Synthetic BC7 Modes 0, 1, 5, and 6 (exact D3D11 bit-level verification)!
    {
        // Mode 6: 1 subset, 7-bit RGBA + 1 individual p-bit each -> 8-bit exact RGBA!
        // e0 = (200, 100, 50, 255) -> top7=(100,50,25,127), pbit0=0 (even -> 200,100,50,254); e1=(201,101,51,255) pbit1=1 -> all idx=0 => (200,100,50,254)
        BitWriter128 w6;
        w6.Write(0x40u, 7u); // Mode 6 prefix (bit 6 set)
        w6.Write(100u, 7u); w6.Write(100u, 7u); // R0, R1
        w6.Write(50u,  7u); w6.Write(50u,  7u); // G0, G1
        w6.Write(25u,  7u); w6.Write(25u,  7u); // B0, B1
        w6.Write(127u, 7u); w6.Write(127u, 7u); // A0, A1
        w6.Write(0u, 1u); w6.Write(1u, 1u);     // pbit0=0, pbit1=1
        // 63 index bits all 0 -> selects e0 = (200, 100, 50, 254) on every pixel!
        uint8_t blk6[16]{};
        w6.Flush(blk6);
        const auto dds6 = WrapDds4x4(0u, 98u, blk6, 16u); // DXGI_FORMAT_BC7_UNORM
        DdsImage img6{};
        const bool ok6 = DecodeDdsBytes(dds6.data(), dds6.size(), 0u, &img6) &&
                         img6.rgba8[0] == 200u && img6.rgba8[1] == 100u &&
                         img6.rgba8[2] == 50u  && img6.rgba8[3] == 254u;

        // Mode 5: 1 subset, rotation=1 (swap A<->R!), 7-bit RGB, 8-bit A -> e0 RGB=(16,32,64), A=240 -> after rot=1: RGBA=(240,32,64,16)!
        BitWriter128 w5;
        w5.Write(0x20u, 6u); // Mode 5 prefix (bit 5 set)
        w5.Write(1u, 2u);    // rotation = 1 (swap R <-> A)
        w5.Write(8u, 7u);  w5.Write(8u, 7u);    // R = unquant7(8) = 16
        w5.Write(16u, 7u); w5.Write(16u, 7u);   // G = unquant7(16) = 32
        w5.Write(32u, 7u); w5.Write(32u, 7u);   // B = unquant7(32) = 64
        w5.Write(240u, 8u); w5.Write(240u, 8u); // A = 240
        uint8_t blk5[16]{};
        w5.Flush(blk5);
        uint8_t out5[64]{};
        DecodeBc7Block(blk5, out5);
        const bool ok5 = (out5[0] == 240u && out5[1] == 32u && out5[2] == 64u && out5[3] == 16u);

        // Mode 1: 2 subsets, 6-bit RGB + 1 shared p-bit per subset; subset0 R=0x3F,p=1 -> 255; subset1 G=0x3F,p=1 -> 255
        BitWriter128 w1;
        w1.Write(0x02u, 2u); // Mode 1 prefix (bit 1 set)
        w1.Write(0u, 6u);    // partition 0 (0xCCCC: cols 0,1 = subset0; cols 2,3 = subset1)
        w1.Write(63u, 6u); w1.Write(63u, 6u); w1.Write(0u, 6u);  w1.Write(0u, 6u);  // R: s0=255, s1=0
        w1.Write(0u, 6u);  w1.Write(0u, 6u);  w1.Write(63u, 6u); w1.Write(63u, 6u); // G: s0=0,   s1=255
        w1.Write(0u, 6u);  w1.Write(0u, 6u);  w1.Write(0u, 6u);  w1.Write(0u, 6u);  // B: 0
        w1.Write(1u, 1u);  w1.Write(1u, 1u);                                        // shared p-bits
        uint8_t blk1[16]{};
        w1.Flush(blk1);
        uint8_t out1[64]{};
        DecodeBc7Block(blk1, out1);
        // Texel 0 is in subset 0 (red: 255,0,0,255); Texel 2 is in subset 1 (green: 0,255,0,255)
        const bool ok1 = (out1[0] == 255u && out1[1] <= 2u && out1[3] == 255u) &&
                         (out1[8] <= 2u && out1[9] == 255u && out1[11] == 255u);

        // Mode 0: 3 subsets, 4-bit RGB + individual p-bits (5-bit prec -> 0x1F unquantizes to 255!)
        BitWriter128 w0;
        w0.Write(0x01u, 1u); // Mode 0 prefix (bit 0 set)
        w0.Write(0u, 4u);    // partition 0
        for (int e = 0; e < 6; ++e) w0.Write(15u, 4u); // R = 15 (with p=1 -> 255)
        for (int e = 0; e < 6; ++e) w0.Write(0u, 4u);  // G = 0
        for (int e = 0; e < 6; ++e) w0.Write(15u, 4u); // B = 15 (with p=1 -> 255)
        for (int e = 0; e < 6; ++e) w0.Write(1u, 1u);  // p-bits = 1
        uint8_t blk0[16]{};
        w0.Flush(blk0);
        uint8_t out0[64]{};
        DecodeBc7Block(blk0, out0);
        const bool ok0 = (out0[0] == 255u && out0[1] <= 10u && out0[2] == 255u && out0[3] == 255u);

        Report(ok6 && ok5 && ok1 && ok0, "dds_bc7_modes_0_1_5_6_exact_decode");
    }

    // 5. Planted-failure controls: bad magic, truncated buffer, unsupported DXGI format!
    {
        DdsImage img{};
        std::string err;
        uint8_t bad[148]{};
        const bool rejMagic = !DecodeDdsBytes(bad, sizeof(bad), 0u, &img, nullptr, &err);
        const auto badFmt = WrapDds4x4(0u, 9999u, bad, 16u);
        const bool rejFmt = !DecodeDdsBytes(badFmt.data(), badFmt.size(), 0u, &img, nullptr, &err);
        const auto truncDds = WrapDds4x4(0x31545844u, 0u, bad, 2u); // Only 2 bytes instead of 8 for BC1!
        const bool rejTrunc = !DecodeDdsBytes(truncDds.data(), truncDds.size(), 0u, &img, nullptr, &err);
        Report(rejMagic && rejFmt && rejTrunc, "dds_corrupted_and_truncated_fail_closed");
    }

    // 6. LIVE Real `Skyrim - Textures0.bsa` Integration Test (gracefully skipped if Skyrim SE not installed!)
    const std::string texBsaPath = "H:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data/Skyrim - Textures0.bsa";
    if (std::filesystem::exists(texBsaPath)) {
        RawBsaArchive bsa;
        std::string err;
        const bool opened = bsa.Open(texBsaPath, &err);
        uint32_t decodedDiffuse = 0u;
        uint32_t decodedNormals = 0u;
        uint32_t bc7Decoded     = 0u;
        bool pbrStatsValid      = true;

        if (opened) {
            const auto allDds = bsa.ListFiles(".dds", false);
            for (const auto& vp : allDds) {
                if (decodedDiffuse >= 4u && decodedNormals >= 4u) break;
                const bool isNrm = (vp.find("_n.dds") != std::string::npos);
                const bool isAuxMask = (vp.find("_g.dds") != std::string::npos) ||
                                       (vp.find("_m.dds") != std::string::npos) ||
                                       (vp.find("_e.dds") != std::string::npos) ||
                                       (vp.find("_p.dds") != std::string::npos) ||
                                       (vp.find("black") != std::string::npos);
                if (isNrm && decodedNormals >= 4u) continue;
                if (!isNrm && (decodedDiffuse >= 4u || isAuxMask)) continue;

                std::vector<uint8_t> ddsBytes;
                if (!bsa.ExtractFile(vp, &ddsBytes, &err)) continue;
                DdsHeaderInfo hdr{};
                if (!ParseDdsHeader(ddsBytes.data(), ddsBytes.size(), &hdr, &err)) continue;
                const uint32_t targetMip = (hdr.width > 512u && hdr.mipCount > 1u) ? 1u : 0u;
                DdsImage img{};
                if (!DecodeDdsBytes(ddsBytes.data(), ddsBytes.size(), targetMip, &img, nullptr, &err)) continue;
                const DdsPbrAnalysis pbr = AnalyzeDecodedTexture(img, isNrm);

                if (img.width == 0u || img.height == 0u || pbr.nonZeroPixels == 0u ||
                    !std::isfinite(pbr.meanLuminance) || !std::isfinite(pbr.toksvigRoughness)) {
                    pbrStatsValid = false;
                }
                if (hdr.format == DdsFormat::BC7_UNORM) ++bc7Decoded;
                if (isNrm) ++decodedNormals;
                else       ++decodedDiffuse;
            }
        }
        std::printf("  [LIVE Textures0.bsa DDS stats] diffuse=%u normals=%u bc7=%u pbrValid=%d\n",
                    decodedDiffuse, decodedNormals, bc7Decoded, int(pbrStatsValid));
        Report(opened && decodedDiffuse >= 4u && decodedNormals >= 4u && pbrStatsValid,
               "dds_real_skyrim_textures0_bsa_diffuse_and_normal_decode");
    } else {
        Report(true, "dds_real_skyrim_textures0_bsa_skipped_not_installed");
    }

    return fails;
}

}  // namespace raw::worldgen

#ifdef RAW_DDS_TEST_STANDALONE
int main() {
    const int fails = raw::worldgen::RunDdsDecoderTests(nullptr);
    std::printf("RawDdsDecoder standalone test summary: %s (%d failures)\n",
                fails == 0 ? "ALL PASSED" : "FAILED", fails);
    return fails == 0 ? 0 : 1;
}
#endif
