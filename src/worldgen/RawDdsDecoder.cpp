// RawDdsDecoder.cpp: 100% Clean-Room `.DDS` (BC1/BC2/BC3/BC4/BC5/BC7/RGBA8/BGRA8) Decoder & PBR Analyzer.
// Implements full D3D11 BC7 8-mode decoding (64 2-subset + 64 3-subset partitions, anchor indices, p-bits,
// rotations, index-selection bit), BC5 unit-Z tangent normal reconstruction, and Toksvig roughness analysis.
// Zero third-party / Windows / D3D12 dependencies. Original work, MIT.

#include "RawDdsDecoder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>

namespace raw::worldgen {
namespace {

void SetErr(std::string* outErr, const std::string& msg) {
    if (outErr) *outErr = msg;
}

inline uint16_t ReadU16LE(const uint8_t* p) {
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}

inline uint32_t ReadU32LE(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

inline constexpr uint32_t MakeFourCC(char a, char b, char c, char d) {
    return uint32_t(uint8_t(a)) | (uint32_t(uint8_t(b)) << 8) |
           (uint32_t(uint8_t(c)) << 16) | (uint32_t(uint8_t(d)) << 24);
}

inline uint8_t ClampU8(int v) {
    return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

inline void UnpackRgb565(uint16_t c, uint8_t out[3]) {
    const uint32_t r = (c >> 11) & 0x1Fu;
    const uint32_t g = (c >> 5) & 0x3Fu;
    const uint32_t b = c & 0x1Fu;
    out[0] = static_cast<uint8_t>((r << 3) | (r >> 2));
    out[1] = static_cast<uint8_t>((g << 2) | (g >> 4));
    out[2] = static_cast<uint8_t>((b << 3) | (b >> 2));
}

void DecodeBc4Channel(const uint8_t blk[8], uint8_t out16[16]) {
    uint8_t pal[8]{};
    pal[0] = blk[0];
    pal[1] = blk[1];
    if (pal[0] > pal[1]) {
        for (int i = 1; i <= 6; ++i) {
            pal[i + 1] = static_cast<uint8_t>(((7 - i) * int(pal[0]) + i * int(pal[1]) + 3) / 7);
        }
    } else {
        for (int i = 1; i <= 4; ++i) {
            pal[i + 1] = static_cast<uint8_t>(((5 - i) * int(pal[0]) + i * int(pal[1]) + 2) / 5);
        }
        pal[6] = 0u;
        pal[7] = 255u;
    }
    uint64_t bits = 0ull;
    for (int i = 0; i < 6; ++i) {
        bits |= uint64_t(blk[2 + i]) << (8u * uint32_t(i));
    }
    for (int p = 0; p < 16; ++p) {
        out16[p] = pal[(bits >> (3u * uint32_t(p))) & 7u];
    }
}

// ============================================================================
// Official Direct3D 11 BC7 Functional Specification Tables (Modes 0..7)
// ============================================================================

struct Bc7ModeInfo {
    uint8_t subsets;
    uint8_t partBits;
    uint8_t rotBits;
    uint8_t idxSelBits;
    uint8_t colorBits;
    uint8_t alphaBits;
    uint8_t epPBits;
    uint8_t shPBits;
    uint8_t idxBits;
    uint8_t idx2Bits;
};

constexpr Bc7ModeInfo kBc7Modes[8] = {
    {3, 4, 0, 0, 4, 0, 1, 0, 3, 0}, // Mode 0
    {2, 6, 0, 0, 6, 0, 0, 1, 3, 0}, // Mode 1
    {3, 6, 0, 0, 5, 0, 0, 0, 2, 0}, // Mode 2
    {2, 6, 0, 0, 7, 0, 1, 0, 2, 0}, // Mode 3
    {1, 0, 2, 1, 5, 6, 0, 0, 2, 3}, // Mode 4
    {1, 0, 2, 0, 7, 8, 0, 0, 2, 2}, // Mode 5
    {1, 0, 0, 0, 7, 7, 1, 0, 4, 0}, // Mode 6
    {2, 6, 0, 0, 5, 5, 1, 0, 2, 0}, // Mode 7
};

constexpr uint8_t kBc7Weights2[4] = {0, 21, 43, 64};
constexpr uint8_t kBc7Weights3[8] = {0, 9, 18, 27, 37, 46, 55, 64};
constexpr uint8_t kBc7Weights4[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};

// 64 2-subset partition masks (bit p = subset index 0 or 1 for texel p in 0..15)
constexpr uint16_t kBc7Part2[64] = {
    0xCCCCu, 0x8888u, 0xEEEEu, 0xECC8u, 0xC880u, 0xFEECu, 0xFEC8u, 0xEC80u,
    0xC800u, 0xFFECu, 0xFE80u, 0xE800u, 0xFFE8u, 0xFF00u, 0xFFF0u, 0xF000u,
    0xF710u, 0x008Eu, 0x7100u, 0x08CEu, 0x008Cu, 0x7310u, 0x3100u, 0x8CCEu,
    0x088Cu, 0x3110u, 0x6666u, 0x366Cu, 0x17E8u, 0x0FF0u, 0x718Eu, 0x399Cu,
    0xAAAAu, 0xF0F0u, 0x5A5Au, 0x33CCu, 0x3C3Cu, 0x55AAu, 0x9696u, 0xA55Au,
    0x73CEu, 0x13C8u, 0x324Cu, 0x3BDCu, 0x6996u, 0xC33Cu, 0x9966u, 0x0660u,
    0x0272u, 0x04E4u, 0x4E40u, 0x2720u, 0xC936u, 0x936Cu, 0x39C6u, 0x639Cu,
    0x9336u, 0x9CC6u, 0x817Eu, 0xE718u, 0xCCF0u, 0x0FCCu, 0x7744u, 0xEE22u,
};

// 64 3-subset partition tables (2 bits per texel -> 32 bits per partition)
constexpr uint32_t kBc7Part3[64] = {
    0xAA685050u, 0x6A5A5040u, 0x5A5A4200u, 0x5450A0A8u, 0xA5A50000u, 0xA0A05050u, 0x5555A0A0u, 0x5A5A5050u,
    0xAA550000u, 0xAA555500u, 0xAAAA5500u, 0x90909090u, 0x94949494u, 0xA4A4A4A4u, 0xA9A59450u, 0x2A0A4250u,
    0xA5945040u, 0x0A425054u, 0xA5A5A500u, 0x55A0A0A0u, 0xA8A85454u, 0x6A6A4040u, 0xA4A45000u, 0x1A1A0500u,
    0x0050A4A4u, 0xAAA59090u, 0x14696914u, 0x69691400u, 0xA08585A0u, 0xAA821414u, 0x50A4A450u, 0x6A5A0200u,
    0xA9A58000u, 0x5090A0A8u, 0xA8A09050u, 0x24242424u, 0x00AA5500u, 0x24924924u, 0x24499224u, 0x50A50A50u,
    0x500AA550u, 0xAAAA4444u, 0x66660000u, 0xA5A0A5A0u, 0x50A050A0u, 0x69286928u, 0x44AAAA44u, 0x66666600u,
    0xAA444444u, 0x54A854A8u, 0x95809580u, 0x96969600u, 0xA85454A8u, 0x80959580u, 0xAA141414u, 0x96960000u,
    0xAAAA1414u, 0xA05050A0u, 0xA0A5A5A0u, 0x96000000u, 0x40804080u, 0xA9A8A9A8u, 0xAAAAAA44u, 0x2A4A5254u,
};

// Anchor index for subset 1 in 2-subset partitions
constexpr uint8_t kBc7Anchor2[64] = {
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15,  2,  8,  2,  2,  8,  8, 15,  2,  8,  2,  2,  8,  8,  2,  2,
    15, 15,  6,  8,  2,  8, 15, 15,  2,  8,  2,  2,  2, 15, 15,  6,
     6,  2,  6,  8, 15, 15,  2,  2, 15, 15, 15, 15, 15,  2,  2, 15,
};

// Anchor indices for subset 1 and subset 2 in 3-subset partitions
constexpr uint8_t kBc7Anchor3A[64] = {
     3,  3, 15, 15,  8,  3, 15, 15,  8,  8,  6,  6,  6,  5,  3,  3,
     3,  3,  8, 15,  3,  3,  6, 10,  5,  8,  8,  6,  8,  5, 15, 15,
     8, 15,  3,  5,  6, 10,  8, 15, 15,  3, 15,  5, 15, 15, 15, 15,
     3, 15,  5,  5,  5,  8,  5, 10,  5, 10,  8, 13, 15, 12,  3,  3,
};

constexpr uint8_t kBc7Anchor3B[64] = {
    15,  8,  8,  3, 15, 15,  3,  8, 15, 15, 15, 15, 15, 15, 15,  8,
    15,  8, 15,  3, 15,  8, 15,  8,  3, 15,  6, 10, 15, 15, 10,  8,
    15,  3, 15, 10, 10,  8,  9, 10,  6, 15,  8, 15,  3,  6,  6,  8,
    15,  3, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,  3, 15, 15,  8,
};

struct BitReader128 {
    uint64_t lo = 0ull;
    uint64_t hi = 0ull;
    uint32_t pos = 0u;

    explicit BitReader128(const uint8_t blk[16]) {
        std::memcpy(&lo, blk, 8u);
        std::memcpy(&hi, blk + 8u, 8u);
    }

    uint32_t Read(uint32_t n) {
        if (n == 0u) return 0u;
        uint32_t val = 0u;
        for (uint32_t i = 0u; i < n; ++i) {
            const uint32_t b = pos++;
            const uint32_t bit = (b < 64u) ? uint32_t((lo >> b) & 1ull)
                                           : uint32_t((hi >> (b - 64u)) & 1ull);
            val |= (bit << i);
        }
        return val;
    }
};

inline uint8_t UnquantizeBc7(uint32_t val, uint32_t prec) {
    if (prec >= 8u) return static_cast<uint8_t>(val & 0xFFu);
    const uint32_t shifted = val << (8u - prec);
    return static_cast<uint8_t>(shifted | (shifted >> prec));
}

inline uint8_t InterpBc7(uint8_t e0, uint8_t e1, uint8_t w) {
    return static_cast<uint8_t>(((64u - uint32_t(w)) * uint32_t(e0) + uint32_t(w) * uint32_t(e1) + 32u) >> 6u);
}

inline float SrgbToLinear(float c) {
    return (c <= 0.04045f) ? (c / 12.92f) : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

size_t ComputeMipByteSize(uint32_t w, uint32_t h, DdsFormat fmt) {
    const size_t bw = std::max<size_t>(1u, (size_t(w) + 3u) / 4u);
    const size_t bh = std::max<size_t>(1u, (size_t(h) + 3u) / 4u);
    switch (fmt) {
        case DdsFormat::BC1_UNORM:
        case DdsFormat::BC4_UNORM:   return bw * bh * 8u;
        case DdsFormat::BC2_UNORM:
        case DdsFormat::BC3_UNORM:
        case DdsFormat::BC5_UNORM:
        case DdsFormat::BC7_UNORM:   return bw * bh * 16u;
        case DdsFormat::RGBA8_UNORM:
        case DdsFormat::BGRA8_UNORM: return size_t(w) * size_t(h) * 4u;
        case DdsFormat::RGB8_UNORM:
        case DdsFormat::BGR8_UNORM:  return size_t(w) * size_t(h) * 3u;
        default:                     return 0u;
    }
}

}  // namespace

// ============================================================================
// Block Decoders (BC1, BC2, BC3, BC4, BC5, BC7)
// ============================================================================

const char* GetDdsFormatName(DdsFormat fmt) {
    switch (fmt) {
        case DdsFormat::RGBA8_UNORM: return "RGBA8_UNORM";
        case DdsFormat::BGRA8_UNORM: return "BGRA8_UNORM";
        case DdsFormat::RGB8_UNORM:  return "RGB8_UNORM";
        case DdsFormat::BGR8_UNORM:  return "BGR8_UNORM";
        case DdsFormat::BC1_UNORM:   return "BC1_UNORM";
        case DdsFormat::BC2_UNORM:   return "BC2_UNORM";
        case DdsFormat::BC3_UNORM:   return "BC3_UNORM";
        case DdsFormat::BC4_UNORM:   return "BC4_UNORM";
        case DdsFormat::BC5_UNORM:   return "BC5_UNORM";
        case DdsFormat::BC7_UNORM:   return "BC7_UNORM";
        default:                     return "Unknown";
    }
}

void DecodeBc1Block(const uint8_t blk[8], uint8_t outRgba4x4[64], bool forceFourColor) {
    const uint16_t c0 = ReadU16LE(blk + 0u);
    const uint16_t c1 = ReadU16LE(blk + 2u);
    uint8_t pal[4][4]{};
    UnpackRgb565(c0, pal[0]); pal[0][3] = 255u;
    UnpackRgb565(c1, pal[1]); pal[1][3] = 255u;

    if (forceFourColor || c0 > c1) {
        for (int ch = 0; ch < 3; ++ch) {
            pal[2][ch] = static_cast<uint8_t>((2 * int(pal[0][ch]) + int(pal[1][ch]) + 1) / 3);
            pal[3][ch] = static_cast<uint8_t>((int(pal[0][ch]) + 2 * int(pal[1][ch]) + 1) / 3);
        }
        pal[2][3] = 255u;
        pal[3][3] = 255u;
    } else {
        for (int ch = 0; ch < 3; ++ch) {
            pal[2][ch] = static_cast<uint8_t>((int(pal[0][ch]) + int(pal[1][ch])) / 2);
            pal[3][ch] = 0u;
        }
        pal[2][3] = 255u;
        pal[3][3] = 0u;
    }

    const uint32_t bits = ReadU32LE(blk + 4u);
    for (int p = 0; p < 16; ++p) {
        const uint32_t idx = (bits >> (2u * uint32_t(p))) & 3u;
        std::memcpy(outRgba4x4 + p * 4, pal[idx], 4u);
    }
}

void DecodeBc2Block(const uint8_t blk[16], uint8_t outRgba4x4[64]) {
    DecodeBc1Block(blk + 8u, outRgba4x4, true);
    for (int p = 0; p < 16; ++p) {
        const uint8_t byte = blk[p >> 1];
        const uint8_t nibble = (p & 1) ? (byte >> 4) : (byte & 0x0Fu);
        outRgba4x4[p * 4 + 3] = static_cast<uint8_t>(nibble * 17u);
    }
}

void DecodeBc3Block(const uint8_t blk[16], uint8_t outRgba4x4[64]) {
    uint8_t alpha[16]{};
    DecodeBc4Channel(blk, alpha);
    DecodeBc1Block(blk + 8u, outRgba4x4, true);
    for (int p = 0; p < 16; ++p) {
        outRgba4x4[p * 4 + 3] = alpha[p];
    }
}

void DecodeBc4Block(const uint8_t blk[8], uint8_t outRgba4x4[64]) {
    uint8_t red[16]{};
    DecodeBc4Channel(blk, red);
    for (int p = 0; p < 16; ++p) {
        outRgba4x4[p * 4 + 0] = red[p];
        outRgba4x4[p * 4 + 1] = red[p];
        outRgba4x4[p * 4 + 2] = red[p];
        outRgba4x4[p * 4 + 3] = 255u;
    }
}

void DecodeBc5Block(const uint8_t blk[16], uint8_t outRgba4x4[64]) {
    uint8_t rx[16]{};
    uint8_t gy[16]{};
    DecodeBc4Channel(blk + 0u, rx);
    DecodeBc4Channel(blk + 8u, gy);
    for (int p = 0; p < 16; ++p) {
        const float nx = (float(rx[p]) / 255.0f) * 2.0f - 1.0f;
        const float ny = (float(gy[p]) / 255.0f) * 2.0f - 1.0f;
        const float nz = std::sqrt(std::max(0.0f, 1.0f - nx * nx - ny * ny));
        outRgba4x4[p * 4 + 0] = rx[p];
        outRgba4x4[p * 4 + 1] = gy[p];
        outRgba4x4[p * 4 + 2] = ClampU8(static_cast<int>(std::lround((nz * 0.5f + 0.5f) * 255.0f)));
        outRgba4x4[p * 4 + 3] = 255u;
    }
}

void DecodeBc7Block(const uint8_t blk[16], uint8_t outRgba4x4[64]) {
    // Mode is the index of the lowest set bit in byte 0 (0..7); 0x00 is reserved -> zero output.
    if (blk[0] == 0u) {
        std::memset(outRgba4x4, 0, 64u);
        return;
    }
    uint32_t mode = 0u;
    while (mode < 8u && ((blk[0] >> mode) & 1u) == 0u) ++mode;
    if (mode >= 8u) {
        std::memset(outRgba4x4, 0, 64u);
        return;
    }

    const Bc7ModeInfo& m = kBc7Modes[mode];
    BitReader128 br(blk);
    br.Read(mode + 1u); // consume mode prefix

    const uint32_t partition = br.Read(m.partBits);
    const uint32_t rotation  = br.Read(m.rotBits);
    const uint32_t idxSel    = br.Read(m.idxSelBits);

    // Read raw endpoints in channel-major order: R, G, B, [A]
    const uint32_t numEp = uint32_t(m.subsets) * 2u;
    uint32_t ep[6][4]{};
    for (uint32_t ch = 0u; ch < 3u; ++ch) {
        for (uint32_t e = 0u; e < numEp; ++e) {
            ep[e][ch] = br.Read(m.colorBits);
        }
    }
    if (m.alphaBits > 0u) {
        for (uint32_t e = 0u; e < numEp; ++e) {
            ep[e][3] = br.Read(m.alphaBits);
        }
    } else {
        for (uint32_t e = 0u; e < numEp; ++e) ep[e][3] = 255u;
    }

    // Apply endpoint or shared p-bits and unquantize to 8-bit endpoints
    uint8_t ep8[6][4]{};
    const uint32_t cPrec = uint32_t(m.colorBits) + ((m.epPBits || m.shPBits) ? 1u : 0u);
    const uint32_t aPrec = uint32_t(m.alphaBits) + ((m.alphaBits > 0u && (m.epPBits || m.shPBits)) ? 1u : 0u);

    if (m.epPBits) {
        for (uint32_t e = 0u; e < numEp; ++e) {
            const uint32_t pbit = br.Read(1u);
            for (uint32_t ch = 0u; ch < 3u; ++ch) ep[e][ch] = (ep[e][ch] << 1u) | pbit;
            if (m.alphaBits > 0u) ep[e][3] = (ep[e][3] << 1u) | pbit;
        }
    } else if (m.shPBits) {
        for (uint32_t s = 0u; s < m.subsets; ++s) {
            const uint32_t pbit = br.Read(1u);
            for (uint32_t k = 0u; k < 2u; ++k) {
                const uint32_t e = s * 2u + k;
                for (uint32_t ch = 0u; ch < 3u; ++ch) ep[e][ch] = (ep[e][ch] << 1u) | pbit;
                if (m.alphaBits > 0u) ep[e][3] = (ep[e][3] << 1u) | pbit;
            }
        }
    }

    for (uint32_t e = 0u; e < numEp; ++e) {
        for (uint32_t ch = 0u; ch < 3u; ++ch) ep8[e][ch] = UnquantizeBc7(ep[e][ch], cPrec);
        ep8[e][3] = (m.alphaBits > 0u) ? UnquantizeBc7(ep[e][3], aPrec) : 255u;
    }

    // Determine subset index per texel and anchor indices
    uint8_t subsetOf[16]{};
    uint8_t anchor1 = 255u, anchor2 = 255u;
    if (m.subsets == 2u) {
        const uint16_t mask = kBc7Part2[partition & 63u];
        anchor1 = kBc7Anchor2[partition & 63u];
        for (uint32_t p = 0u; p < 16u; ++p) subsetOf[p] = static_cast<uint8_t>((mask >> p) & 1u);
    } else if (m.subsets == 3u) {
        const uint32_t mask = kBc7Part3[partition & 63u];
        anchor1 = kBc7Anchor3A[partition & 63u];
        anchor2 = kBc7Anchor3B[partition & 63u];
        for (uint32_t p = 0u; p < 16u; ++p) subsetOf[p] = static_cast<uint8_t>((mask >> (p * 2u)) & 3u);
    }

    // Read primary index stream (and secondary index stream for Modes 4 & 5)
    uint8_t idx1[16]{};
    uint8_t idx2[16]{};
    for (uint32_t p = 0u; p < 16u; ++p) {
        const bool isAnchor = (p == 0u) || (p == anchor1) || (p == anchor2);
        idx1[p] = static_cast<uint8_t>(br.Read(isAnchor ? (m.idxBits - 1u) : m.idxBits));
    }
    if (m.idx2Bits > 0u) {
        for (uint32_t p = 0u; p < 16u; ++p) {
            idx2[p] = static_cast<uint8_t>(br.Read((p == 0u) ? (m.idx2Bits - 1u) : m.idx2Bits));
        }
    }

    auto weightOf = [](uint8_t idx, uint8_t bits) -> uint8_t {
        if (bits == 2u) return kBc7Weights2[idx & 3u];
        if (bits == 3u) return kBc7Weights3[idx & 7u];
        return kBc7Weights4[idx & 15u];
    };

    for (uint32_t p = 0u; p < 16u; ++p) {
        const uint32_t s = subsetOf[p];
        const uint32_t e0 = s * 2u;
        const uint32_t e1 = s * 2u + 1u;

        uint8_t wc = 0u, wa = 0u;
        if (m.idx2Bits == 0u) {
            wc = weightOf(idx1[p], m.idxBits);
            wa = wc;
        } else if (idxSel == 0u) {
            wc = weightOf(idx1[p], m.idxBits);
            wa = weightOf(idx2[p], m.idx2Bits);
        } else {
            wc = weightOf(idx2[p], m.idx2Bits);
            wa = weightOf(idx1[p], m.idxBits);
        }

        uint8_t rgba[4] = {
            InterpBc7(ep8[e0][0], ep8[e1][0], wc),
            InterpBc7(ep8[e0][1], ep8[e1][1], wc),
            InterpBc7(ep8[e0][2], ep8[e1][2], wc),
            InterpBc7(ep8[e0][3], ep8[e1][3], wa),
        };

        // Channel rotation (Modes 4 & 5)
        if (rotation == 1u)      std::swap(rgba[0], rgba[3]);
        else if (rotation == 2u) std::swap(rgba[1], rgba[3]);
        else if (rotation == 3u) std::swap(rgba[2], rgba[3]);

        std::memcpy(outRgba4x4 + p * 4u, rgba, 4u);
    }
}

// ============================================================================
// DDS Header & Image Decoder
// ============================================================================

bool ParseDdsHeader(const uint8_t* bytes, size_t size, DdsHeaderInfo* outInfo, std::string* outErr) {
    if (!bytes || !outInfo || size < 128u) {
        SetErr(outErr, "DDS buffer null or smaller than 128-byte header.");
        return false;
    }
    if (ReadU32LE(bytes + 0u) != kDdsMagic) {
        SetErr(outErr, "Invalid DDS magic (expected 'DDS ').");
        return false;
    }
    const uint32_t hdrSize = ReadU32LE(bytes + 4u);
    if (hdrSize != 124u) {
        SetErr(outErr, "Invalid DDS_HEADER dwSize (expected 124).");
        return false;
    }

    DdsHeaderInfo info{};
    info.height   = ReadU32LE(bytes + 12u);
    info.width    = ReadU32LE(bytes + 16u);
    info.mipCount = std::max(1u, ReadU32LE(bytes + 28u));
    if (info.width == 0u || info.height == 0u || info.width > 16384u || info.height > 16384u) {
        SetErr(outErr, "DDS dimensions out of valid range (1..16384).");
        return false;
    }

    const uint32_t pfSize   = ReadU32LE(bytes + 76u);
    const uint32_t pfFlags  = ReadU32LE(bytes + 80u);
    const uint32_t fourCC   = ReadU32LE(bytes + 84u);
    const uint32_t rgbBits  = ReadU32LE(bytes + 88u);
    const uint32_t rMask    = ReadU32LE(bytes + 92u);
    if (pfSize != 32u) {
        SetErr(outErr, "Invalid DDS_PIXELFORMAT dwSize (expected 32).");
        return false;
    }
    info.fourCC = fourCC;
    info.dataOffset = 128u;

    if ((pfFlags & 0x4u) != 0u) { // DDPF_FOURCC
        if (fourCC == MakeFourCC('D','X','1','0')) {
            if (size < 148u) {
                SetErr(outErr, "DDS DX10 header truncated (<148 bytes).");
                return false;
            }
            info.isDx10     = true;
            info.dataOffset = 148u;
            info.dxgiFormat = ReadU32LE(bytes + 128u);
            info.arraySize  = std::max(1u, ReadU32LE(bytes + 140u));

            switch (info.dxgiFormat) {
                case 28u: info.format = DdsFormat::RGBA8_UNORM; break;
                case 29u: info.format = DdsFormat::RGBA8_UNORM; info.isSrgb = true; break;
                case 87u: info.format = DdsFormat::BGRA8_UNORM; break;
                case 91u: info.format = DdsFormat::BGRA8_UNORM; info.isSrgb = true; break;
                case 71u: info.format = DdsFormat::BC1_UNORM;   break;
                case 72u: info.format = DdsFormat::BC1_UNORM;   info.isSrgb = true; break;
                case 74u: info.format = DdsFormat::BC2_UNORM;   break;
                case 75u: info.format = DdsFormat::BC2_UNORM;   info.isSrgb = true; break;
                case 77u: info.format = DdsFormat::BC3_UNORM;   break;
                case 78u: info.format = DdsFormat::BC3_UNORM;   info.isSrgb = true; break;
                case 80u: info.format = DdsFormat::BC4_UNORM;   break;
                case 83u: info.format = DdsFormat::BC5_UNORM;   break;
                case 98u: info.format = DdsFormat::BC7_UNORM;   break;
                case 99u: info.format = DdsFormat::BC7_UNORM;   info.isSrgb = true; break;
                default:
                    SetErr(outErr, "Unsupported DX10 DXGI_FORMAT: " + std::to_string(info.dxgiFormat));
                    return false;
            }
        } else if (fourCC == MakeFourCC('D','X','T','1')) {
            info.format = DdsFormat::BC1_UNORM;
        } else if (fourCC == MakeFourCC('D','X','T','2') || fourCC == MakeFourCC('D','X','T','3')) {
            info.format = DdsFormat::BC2_UNORM;
        } else if (fourCC == MakeFourCC('D','X','T','4') || fourCC == MakeFourCC('D','X','T','5')) {
            info.format = DdsFormat::BC3_UNORM;
        } else if (fourCC == MakeFourCC('A','T','I','1') || fourCC == MakeFourCC('B','C','4','U')) {
            info.format = DdsFormat::BC4_UNORM;
        } else if (fourCC == MakeFourCC('A','T','I','2') || fourCC == MakeFourCC('B','C','5','U')) {
            info.format = DdsFormat::BC5_UNORM;
        } else {
            SetErr(outErr, "Unsupported legacy DDS FourCC.");
            return false;
        }
    } else if ((pfFlags & 0x40u) != 0u) { // DDPF_RGB
        if (rgbBits == 32u) {
            info.format = (rMask == 0x00FF0000u) ? DdsFormat::BGRA8_UNORM : DdsFormat::RGBA8_UNORM;
        } else if (rgbBits == 24u) {
            info.format = (rMask == 0x00FF0000u) ? DdsFormat::BGR8_UNORM : DdsFormat::RGB8_UNORM;
        } else {
            SetErr(outErr, "Unsupported uncompressed DDS bit depth: " + std::to_string(rgbBits));
            return false;
        }
    } else {
        SetErr(outErr, "Unsupported DDS pixel format flags.");
        return false;
    }

    *outInfo = info;
    return true;
}

bool DecodeDdsBytes(const uint8_t* bytes, size_t size, uint32_t mipLevel,
                    DdsImage* outImage, DdsHeaderInfo* outHeader, std::string* outErr) {
    if (!outImage) {
        SetErr(outErr, "Null output DdsImage.");
        return false;
    }
    DdsHeaderInfo hdr{};
    if (!ParseDdsHeader(bytes, size, &hdr, outErr)) return false;
    if (outHeader) *outHeader = hdr;

    if (mipLevel >= hdr.mipCount) {
        SetErr(outErr, "Requested mipLevel exceeds DDS mipCount.");
        return false;
    }

    size_t mipOffset = hdr.dataOffset;
    uint32_t mw = hdr.width;
    uint32_t mh = hdr.height;
    for (uint32_t m = 0u; m < mipLevel; ++m) {
        mipOffset += ComputeMipByteSize(mw, mh, hdr.format);
        mw = std::max(1u, mw >> 1u);
        mh = std::max(1u, mh >> 1u);
    }
    const size_t mipBytes = ComputeMipByteSize(mw, mh, hdr.format);
    if (mipBytes == 0u || mipOffset + mipBytes > size) {
        SetErr(outErr, "DDS mip payload truncated before expected byte count.");
        return false;
    }

    outImage->width    = mw;
    outImage->height   = mh;
    outImage->mipLevel = mipLevel;
    outImage->format   = hdr.format;
    outImage->isSrgb   = hdr.isSrgb;
    outImage->rgba8.assign(size_t(mw) * size_t(mh) * 4u, 255u);

    const uint8_t* src = bytes + mipOffset;
    uint8_t* dst = outImage->rgba8.data();

    if (hdr.format == DdsFormat::RGBA8_UNORM) {
        std::memcpy(dst, src, size_t(mw) * size_t(mh) * 4u);
        return true;
    }
    if (hdr.format == DdsFormat::BGRA8_UNORM) {
        const size_t n = size_t(mw) * size_t(mh);
        for (size_t i = 0u; i < n; ++i) {
            dst[i * 4u + 0u] = src[i * 4u + 2u];
            dst[i * 4u + 1u] = src[i * 4u + 1u];
            dst[i * 4u + 2u] = src[i * 4u + 0u];
            dst[i * 4u + 3u] = src[i * 4u + 3u];
        }
        return true;
    }
    if (hdr.format == DdsFormat::RGB8_UNORM || hdr.format == DdsFormat::BGR8_UNORM) {
        const bool isBgr = (hdr.format == DdsFormat::BGR8_UNORM);
        const size_t n = size_t(mw) * size_t(mh);
        for (size_t i = 0u; i < n; ++i) {
            dst[i * 4u + 0u] = src[i * 3u + (isBgr ? 2u : 0u)];
            dst[i * 4u + 1u] = src[i * 3u + 1u];
            dst[i * 4u + 2u] = src[i * 3u + (isBgr ? 0u : 2u)];
            dst[i * 4u + 3u] = 255u;
        }
        return true;
    }

    const uint32_t blocksX = std::max(1u, (mw + 3u) / 4u);
    const uint32_t blocksY = std::max(1u, (mh + 3u) / 4u);
    const size_t blkStride = (hdr.format == DdsFormat::BC1_UNORM || hdr.format == DdsFormat::BC4_UNORM) ? 8u : 16u;

    uint8_t blockOut[64]{};
    for (uint32_t by = 0u; by < blocksY; ++by) {
        for (uint32_t bx = 0u; bx < blocksX; ++bx) {
            const uint8_t* bp = src + (size_t(by) * blocksX + bx) * blkStride;
            switch (hdr.format) {
                case DdsFormat::BC1_UNORM: DecodeBc1Block(bp, blockOut, false); break;
                case DdsFormat::BC2_UNORM: DecodeBc2Block(bp, blockOut); break;
                case DdsFormat::BC3_UNORM: DecodeBc3Block(bp, blockOut); break;
                case DdsFormat::BC4_UNORM: DecodeBc4Block(bp, blockOut); break;
                case DdsFormat::BC5_UNORM: DecodeBc5Block(bp, blockOut); break;
                case DdsFormat::BC7_UNORM: DecodeBc7Block(bp, blockOut); break;
                default: break;
            }
            for (uint32_t py = 0u; py < 4u; ++py) {
                const uint32_t y = by * 4u + py;
                if (y >= mh) continue;
                for (uint32_t px = 0u; px < 4u; ++px) {
                    const uint32_t x = bx * 4u + px;
                    if (x >= mw) continue;
                    std::memcpy(dst + (size_t(y) * mw + x) * 4u, blockOut + (py * 4u + px) * 4u, 4u);
                }
            }
        }
    }
    return true;
}

bool DecodeDdsFile(const std::string& path, uint32_t mipLevel,
                   DdsImage* outImage, DdsHeaderInfo* outHeader, std::string* outErr) {
    if (path.empty() || !outImage) {
        SetErr(outErr, "Empty DDS path or null output image.");
        return false;
    }
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        SetErr(outErr, "Failed to open DDS file: " + path);
        return false;
    }
    const std::streamoff endPos = f.tellg();
    if (endPos < 128 || endPos > 256ll * 1024ll * 1024ll) {
        SetErr(outErr, "DDS file size out of valid range.");
        return false;
    }
    std::vector<uint8_t> buf(static_cast<size_t>(endPos));
    f.seekg(0, std::ios::beg);
    if (!f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()))) {
        SetErr(outErr, "Failed to read DDS file bytes.");
        return false;
    }
    return DecodeDdsBytes(buf.data(), buf.size(), mipLevel, outImage, outHeader, outErr);
}

// ============================================================================
// PBR Material Inspector & Zero-Dependency Preview Exporters
// ============================================================================

DdsPbrAnalysis AnalyzeDecodedTexture(const DdsImage& img, bool treatAsNormalMap) {
    DdsPbrAnalysis a{};
    a.width = img.width;
    a.height = img.height;
    a.pixelCount = img.width * img.height;
    a.isNormalMap = treatAsNormalMap || (img.format == DdsFormat::BC5_UNORM);
    if (a.pixelCount == 0u || img.rgba8.size() < size_t(a.pixelCount) * 4u) return a;

    double sR = 0.0, sG = 0.0, sB = 0.0;
    double lR = 0.0, lG = 0.0, lB = 0.0, sA = 0.0;
    double lumSum = 0.0, lumSqSum = 0.0;
    double nX = 0.0, nY = 0.0, nZ = 0.0;
    uint32_t alphaPass = 0u;
    uint32_t nonZero = 0u;

    for (uint32_t i = 0u; i < a.pixelCount; ++i) {
        const uint8_t* p = img.rgba8.data() + size_t(i) * 4u;
        if (p[0] > 0u || p[1] > 0u || p[2] > 0u) ++nonZero;
        if (p[3] >= 128u) ++alphaPass;

        const float r = float(p[0]) * (1.0f / 255.0f);
        const float g = float(p[1]) * (1.0f / 255.0f);
        const float b = float(p[2]) * (1.0f / 255.0f);
        const float al = float(p[3]) * (1.0f / 255.0f);
        sR += r; sG += g; sB += b; sA += al;

        const float lr = SrgbToLinear(r);
        const float lg = SrgbToLinear(g);
        const float lb = SrgbToLinear(b);
        lR += lr; lG += lg; lB += lb;

        const float lum = 0.2126f * lr + 0.7152f * lg + 0.0722f * lb;
        lumSum   += lum;
        lumSqSum += lum * lum;

        // Tangent normal accumulation for Toksvig normal-map variance roughness
        const float tx = r * 2.0f - 1.0f;
        const float ty = g * 2.0f - 1.0f;
        const float tz = b * 2.0f - 1.0f;
        const float tl = std::sqrt(tx * tx + ty * ty + tz * tz);
        if (tl > 1.0e-4f) {
            nX += tx / tl;
            nY += ty / tl;
            nZ += tz / tl;
        } else {
            nZ += 1.0;
        }
    }

    const double inv = 1.0 / double(a.pixelCount);
    a.nonZeroPixels     = nonZero;
    a.meanAlbedoSrgb[0] = float(sR * inv);
    a.meanAlbedoSrgb[1] = float(sG * inv);
    a.meanAlbedoSrgb[2] = float(sB * inv);
    a.meanAlbedoLin[0]  = float(lR * inv);
    a.meanAlbedoLin[1]  = float(lG * inv);
    a.meanAlbedoLin[2]  = float(lB * inv);
    a.meanAlpha01       = float(sA * inv);
    a.alphaCoverage01   = float(double(alphaPass) * inv);
    a.meanLuminance     = float(lumSum * inv);
    a.luminanceVariance = std::max(0.0f, float(lumSqSum * inv - (lumSum * inv) * (lumSum * inv)));

    const float mx = float(nX * inv);
    const float my = float(nY * inv);
    const float mz = float(nZ * inv);
    const float mLen = std::min(1.0f, std::sqrt(mx * mx + my * my + mz * mz));
    a.meanNormalLength = mLen;
    if (mLen > 1.0e-4f) {
        a.meanTangentNrm[0] = mx / mLen;
        a.meanTangentNrm[1] = my / mLen;
        a.meanTangentNrm[2] = mz / mLen;
    }
    const float toksvigVar = (1.0f - mLen) / std::max(mLen, 1.0e-4f);
    a.toksvigRoughness = std::clamp(std::sqrt(std::max(0.0f, 2.0f * toksvigVar)), 0.04f, 0.98f);
    return a;
}

bool ExportDdsImageToPpm(const DdsImage& img, const std::string& path) {
    if (path.empty() || img.width == 0u || img.height == 0u ||
        img.rgba8.size() < size_t(img.width) * img.height * 4u) return false;
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    const std::string hdr = "P6\n" + std::to_string(img.width) + " " + std::to_string(img.height) + "\n255\n";
    f.write(hdr.data(), static_cast<std::streamsize>(hdr.size()));
    std::vector<uint8_t> rgb(size_t(img.width) * img.height * 3u);
    const size_t n = size_t(img.width) * img.height;
    for (size_t i = 0u; i < n; ++i) {
        rgb[i * 3u + 0u] = img.rgba8[i * 4u + 0u];
        rgb[i * 3u + 1u] = img.rgba8[i * 4u + 1u];
        rgb[i * 3u + 2u] = img.rgba8[i * 4u + 2u];
    }
    f.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    return f.good();
}

bool ExportDdsImageToBmp(const DdsImage& img, const std::string& path) {
    if (path.empty() || img.width == 0u || img.height == 0u ||
        img.rgba8.size() < size_t(img.width) * img.height * 4u) return false;
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    const uint32_t rowBytes = (img.width * 3u + 3u) & ~3u;
    const uint32_t imgBytes = rowBytes * img.height;
    const uint32_t fileBytes = 54u + imgBytes;
    uint8_t hdr[54]{};
    hdr[0] = 'B'; hdr[1] = 'M';
    std::memcpy(hdr + 2, &fileBytes, 4u);
    const uint32_t pixOff = 54u;
    std::memcpy(hdr + 10, &pixOff, 4u);
    const uint32_t dibSize = 40u;
    std::memcpy(hdr + 14, &dibSize, 4u);
    const int32_t w = static_cast<int32_t>(img.width);
    const int32_t h = -static_cast<int32_t>(img.height); // Top-down BMP
    std::memcpy(hdr + 18, &w, 4u);
    std::memcpy(hdr + 22, &h, 4u);
    const uint16_t planes = 1u, bpp = 24u;
    std::memcpy(hdr + 26, &planes, 2u);
    std::memcpy(hdr + 28, &bpp, 2u);
    std::memcpy(hdr + 34, &imgBytes, 4u);
    f.write(reinterpret_cast<const char*>(hdr), 54);

    std::vector<uint8_t> row(rowBytes, 0u);
    for (uint32_t y = 0u; y < img.height; ++y) {
        const uint8_t* srcRow = img.rgba8.data() + size_t(y) * img.width * 4u;
        for (uint32_t x = 0u; x < img.width; ++x) {
            row[x * 3u + 0u] = srcRow[x * 4u + 2u]; // B
            row[x * 3u + 1u] = srcRow[x * 4u + 1u]; // G
            row[x * 3u + 2u] = srcRow[x * 4u + 0u]; // R
        }
        f.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(rowBytes));
    }
    return f.good();
}

}  // namespace raw::worldgen
