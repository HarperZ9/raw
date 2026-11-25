#pragma once
// RawAssetForge.h: Zero-External-Asset Seamless Procedural PBR Texture Synthesizer.
// Generates tileable Albedo, Tangent Normal, Roughness/Metallic/AO/Height (RMAOH), and Emissive RGBA8 maps
// for all 8 physical material archetypes using exact periodic Worley/Voronoi, Simplex-gradient, and Curl kernels.
// Exports directly to binary .ppm / .raw files or keeps RGBA8 buffers ready for zero-copy D3D12/Vulkan/WebGPU upload.
// Zero Skyrim or platform dependencies — clean-room MIT, reusable across RAW, Playground, low-water & general gamedev.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "RawMaterialLib.h"

namespace raw::materials {

enum class ForgeArchetype : uint8_t {
    ArcaneMagic  = 0,
    FrostGlacier = 1,
    MagmaHeat    = 2,
    BrushedMetal = 3,
    CutGlassGem  = 4,
    BasaltStone  = 5,
    WoodBark     = 6,
    AlpineSnow   = 7,
    Count        = 8
};

struct ProceduralPbrSet {
    ForgeArchetype archetype        = ForgeArchetype::BasaltStone;
    MaterialId     linkedMaterialId = MaterialId::ColumnarBasalt;
    uint32_t       width            = 0;
    uint32_t       height           = 0;
    uint32_t       seed             = 0;

    // Seamless tileable RGBA8 maps (each width * height * 4 bytes):
    std::vector<uint8_t> albedoRgba8;   // R,G,B = sRGB-ready / linear base color, A = 255
    std::vector<uint8_t> normalRgba8;   // Tangent-space unit normal packed to [0..255], A = 255
    std::vector<uint8_t> rmaohRgba8;    // R = Roughness, G = Metallic, B = AO, A = Height
    std::vector<uint8_t> emissiveRgba8; // R,G,B = Emissive radiance color, A = Emissive mask
};

std::string_view ArchetypeName(ForgeArchetype archetype);
MaterialId       DefaultMaterialForArchetype(ForgeArchetype archetype);

// Synthesizes a complete seamless tileable PBR texture set in memory (supports 16..2048, e.g. 256x256 / 512x512).
bool SynthesizePbrSet(ForgeArchetype archetype, uint32_t width, uint32_t height, uint32_t seed,
                      ProceduralPbrSet* outSet, std::string* outErr = nullptr);

// Writes a binary P6 .ppm RGB image (zero external image-codec dependencies).
bool WritePpmRgb(const std::string& path, uint32_t width, uint32_t height,
                 const std::vector<uint8_t>& rgba8, std::string* outErr = nullptr);

// Writes raw tightly-packed RGBA8 bytes (.raw) for direct tool/GPU staging pipelines.
bool WriteRawRgba8(const std::string& path, const std::vector<uint8_t>& rgba8,
                   std::string* outErr = nullptr);

// Exports all 4 maps of a ProceduralPbrSet into dirPath as prefix_{albedo,normal,rmaoh,emissive}.ppm + prefix_rmaoh.raw.
bool ExportPbrSetToDirectory(const ProceduralPbrSet& set, const std::string& dirPath,
                             const std::string& prefix, std::string* outErr = nullptr);

}  // namespace raw::materials
