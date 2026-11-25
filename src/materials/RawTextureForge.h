#pragma once
// RawTextureForge.h: Zero-External-Asset 4D Torus-Mapped Seamless Procedural PBR Texture Forge (`raw::materials`).
// Clean-room MIT C++23 framework generating strictly tileable (periodic in X & Y) PBR material texture sets:
//   - Albedo (sRGB RGBA8 + gamma-correct mip chain)
//   - Tangent-Space Normal (Scharr gradient from continuous wrapped heightfield, renormalized unit-Z mip chain)
//   - Packed ORMH (R = Roughness, G = Ambient Occlusion, B = Height, A = Emissive/Metallic mask)
// Exports standard DX10 `.dds` (R8G8B8A8_UNORM / _SRGB with full mip chain) and 24-bit `.bmp` previews,
// round-trip compatible with `raw::worldgen::RawDdsDecoder` and any D3D12 / Vulkan / glTF PBR engine!
// Original work, MIT.

#include <cstdint>
#include <string>
#include <vector>

namespace raw::materials {

enum class ProceduralTextureKind : uint8_t {
    NordicGraniteStrata  = 0, // Sedimentary rock strata, mineral speckles, fractured crevices
    GlacialStratifiedIce = 1, // Compressed cobalt-white glacial air-bubble bands, glossy glaze
    ForgedBrushedSteel   = 2, // Anisotropic brushed steel streaks, subtle hammer dimples, low roughness
    BorealPineBark       = 3, // Deep longitudinal bark furrows, reddish inner plates, mossy crevices
    HexagonalBasalt      = 4, // Periodic hexagonal columnar basalt with crisp weathered joint bevels
    VolcanicMagmaCrust   = 5, // Dark vesicular cooling basalt plates over glowing incandescent magma cracks
    AlpineMossMeadow     = 6, // Organic boreal moss humus, woven lichen fibers, clover variation
    BraidedRiverPebbles  = 7, // Smooth water-worn rounded granite/slate cobblestones with wet sand interstices
    Count                = 8,
};

struct TextureMipLevel {
    uint32_t             width  = 0u;
    uint32_t             height = 0u;
    std::vector<uint8_t> rgba8; // width * height * 4 bytes
};

struct TextureMipChain {
    bool                         isSrgb      = false;
    bool                         isNormalMap = false;
    std::vector<TextureMipLevel> mips; // mips[0] = base resolution .. mips.back() = 1x1

    uint32_t Width()  const { return mips.empty() ? 0u : mips[0].width; }
    uint32_t Height() const { return mips.empty() ? 0u : mips[0].height; }
    uint32_t MipCount() const { return static_cast<uint32_t>(mips.size()); }
};

struct ProceduralPbrTextureSet {
    ProceduralTextureKind kind = ProceduralTextureKind::NordicGraniteStrata;
    std::string           name;
    uint32_t              resolution = 0u;
    uint32_t              seed       = 0u;
    TextureMipChain       albedo; // sRGB RGBA8 (A = opacity / 255)
    TextureMipChain       normal; // Linear Tangent-Space Normal RGBA8 (unit XYZ in [0..255], Z > 127)
    TextureMipChain       ormh;   // Linear Packed: R=Roughness, G=AO, B=Height, A=Emissive(magma)/Metallic(steel)
};

const char* GetProceduralTextureKindName(ProceduralTextureKind kind);

// Generates a complete seamless tileable PBR texture set (Albedo + Normal + ORMH) with full mip chains.
// `resolution` must be a power of two in [16, 2048] (e.g., 128, 256, 512, 1024).
bool GenerateProceduralPbrTextureSet(ProceduralTextureKind kind, uint32_t resolution, uint32_t seed,
                                     ProceduralPbrTextureSet* outSet);

// Full mip-chain builders (gamma-correct sRGB box filter for albedo, renormalized vector filter for normals).
TextureMipChain BuildMipChainFromRgba8(uint32_t width, uint32_t height, const std::vector<uint8_t>& mip0Rgba8,
                                       bool isSrgb, bool isNormalMap);

// Native DX10 `.dds` writer (R8G8B8A8_UNORM = 28 or R8G8B8A8_UNORM_SRGB = 29, full mip chain).
std::vector<uint8_t> SerializeDdsRgba8Bytes(const TextureMipChain& chain);
bool                 WriteDdsRgba8(const std::string& path, const TextureMipChain& chain);

// Zero-dependency 24-bit uncompressed `.bmp` preview writer (writes mip 0).
bool WriteTextureMip0ToBmp(const std::string& path, const TextureMipChain& chain);

// Measures edge-to-edge periodic wrap seamlessness (max & mean absolute channel difference across X and Y seams).
struct TileabilityMetrics {
    float maxEdgeDiffX  = 0.0f; // Max mean-RGB diff between column x=0 and x=W-1 (0..255)
    float meanEdgeDiffX = 0.0f;
    float maxEdgeDiffY  = 0.0f; // Max mean-RGB diff between row y=0 and y=H-1 (0..255)
    float meanEdgeDiffY = 0.0f;
};
TileabilityMetrics MeasureSeamlessTileability(const TextureMipLevel& mip0);

}  // namespace raw::materials
