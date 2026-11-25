#pragma once
// RawDdsDecoder.h: 100% Clean-Room Zero-Dependency `.DDS` (BC1/BC2/BC3/BC4/BC5/BC7/RGBA8/BGRA8)
// Texture Decoder & PBR Material Inspector (`raw::worldgen`).
// Supports both legacy DX9 DDS headers (DXT1/DXT3/DXT5/ATI1/ATI2/BC4U/BC5U/RGBA8/BGRA8) AND
// DX10 extended headers (DXGI_FORMAT_BC1..BC7_UNORM/_SRGB, R8G8B8A8/B8G8R8A8_UNORM/_SRGB).
// Includes full D3D11-spec 8-mode BC7 decoder, BC5 automatic unit-Z tangent normal reconstruction,
// Toksvig normal-variance roughness estimation, and zero-dependency BMP/PPM preview export.
// Original work, MIT.

#include <cstdint>
#include <string>
#include <vector>

namespace raw::worldgen {

inline constexpr uint32_t kDdsMagic = 0x20534444u; // "DDS "

enum class DdsFormat : uint16_t {
    Unknown     = 0,
    RGBA8_UNORM = 1,
    BGRA8_UNORM = 2,
    RGB8_UNORM  = 3,
    BGR8_UNORM  = 4,
    BC1_UNORM   = 10, // DXT1
    BC2_UNORM   = 11, // DXT3
    BC3_UNORM   = 12, // DXT5
    BC4_UNORM   = 13, // ATI1 / BC4U
    BC5_UNORM   = 14, // ATI2 / BC5U (tangent XY -> auto unit-Z)
    BC7_UNORM   = 15, // DX10 BC7 (all 8 modes 0..7)
};

struct DdsHeaderInfo {
    uint32_t  width       = 0u;
    uint32_t  height      = 0u;
    uint32_t  mipCount    = 1u;
    uint32_t  arraySize   = 1u;
    uint32_t  dxgiFormat  = 0u;
    uint32_t  fourCC      = 0u;
    bool      isDx10      = false;
    bool      isSrgb      = false;
    DdsFormat format      = DdsFormat::Unknown;
    size_t    dataOffset  = 128u;
};

struct DdsImage {
    uint32_t             width    = 0u;
    uint32_t             height   = 0u;
    uint32_t             mipLevel = 0u;
    DdsFormat            format   = DdsFormat::Unknown;
    bool                 isSrgb   = false;
    std::vector<uint8_t> rgba8;   // width * height * 4 bytes (R, G, B, A)
};

struct DdsPbrAnalysis {
    uint32_t width             = 0u;
    uint32_t height            = 0u;
    uint32_t pixelCount        = 0u;
    uint32_t nonZeroPixels     = 0u;
    float    meanAlbedoSrgb[3] = {};
    float    meanAlbedoLin[3]  = {};
    float    meanAlpha01       = 1.0f;
    float    alphaCoverage01   = 1.0f; // Fraction of pixels with alpha >= 128
    float    meanLuminance     = 0.0f;
    float    luminanceVariance = 0.0f;
    bool     isNormalMap       = false;
    float    meanTangentNrm[3] = {0.0f, 0.0f, 1.0f};
    float    meanNormalLength  = 1.0f; // Toksvig |E[N]| in (0..1]
    float    toksvigRoughness  = 0.04f;
};

const char* GetDdsFormatName(DdsFormat fmt);

// Parses DDS header (DX9 legacy or DX10 extended) without decoding pixel payload.
bool ParseDdsHeader(const uint8_t* bytes, size_t size, DdsHeaderInfo* outInfo, std::string* outErr = nullptr);

// Decodes a specified mip level (default 0 = full resolution) of a DDS buffer or file to RGBA8.
bool DecodeDdsBytes(const uint8_t* bytes, size_t size, uint32_t mipLevel,
                    DdsImage* outImage, DdsHeaderInfo* outHeader = nullptr, std::string* outErr = nullptr);
bool DecodeDdsFile(const std::string& path, uint32_t mipLevel,
                   DdsImage* outImage, DdsHeaderInfo* outHeader = nullptr, std::string* outErr = nullptr);

// Low-level single 4x4 block decoders (write 16 RGBA8 pixels = 64 bytes into outRgba4x4).
void DecodeBc1Block(const uint8_t blk[8], uint8_t outRgba4x4[64], bool forceFourColor = false);
void DecodeBc2Block(const uint8_t blk[16], uint8_t outRgba4x4[64]);
void DecodeBc3Block(const uint8_t blk[16], uint8_t outRgba4x4[64]);
void DecodeBc4Block(const uint8_t blk[8], uint8_t outRgba4x4[64]);
void DecodeBc5Block(const uint8_t blk[16], uint8_t outRgba4x4[64]);
void DecodeBc7Block(const uint8_t blk[16], uint8_t outRgba4x4[64]);

// Computes sRGB->linear mean albedo, alpha coverage, luminance variance, and Toksvig normal-map roughness.
DdsPbrAnalysis AnalyzeDecodedTexture(const DdsImage& img, bool treatAsNormalMap = false);

// Zero-dependency preview exporters (24-bit binary PPM & standard Windows 24-bit BMP).
bool ExportDdsImageToPpm(const DdsImage& img, const std::string& path);
bool ExportDdsImageToBmp(const DdsImage& img, const std::string& path);

}  // namespace raw::worldgen
