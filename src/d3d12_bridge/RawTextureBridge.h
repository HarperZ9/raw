#pragma once
// RawTextureBridge.h: Zero-Dependency Modern Texture Loader (.png, .jpg, .tif, .bmp via Windows WIC + native .tga).
// Lets creators drop standard Blender / Substance / Photoshop exports directly into raw_assets/textures/
// without .dds BC7 conversion, decoding straight to RGBA8 for DX12 GPU upload & bindless ResourceDescriptorHeap[].
// Original work, MIT.
#include <cstdint>
#include <string>
#include <vector>

namespace raw::bridge
{

struct RawTextureAsset {
    std::string          name;
    uint32_t             width = 0;
    uint32_t             height = 0;
    std::vector<uint8_t> rgba8;   // width * height * 4 bytes (R8G8B8A8)
};

// Decodes .png, .jpg, .jpeg, .tif, .bmp (Windows WIC) or .tga (24/32-bit uncompressed/RLE) into RGBA8.
bool LoadModernTextureAsset(const std::string& path, RawTextureAsset* outTex, std::string* outErr = nullptr);

// Scans <gameDir>raw_assets\ for .glb/.obj meshes and .png/.jpg/.tga textures, returning counts loaded.
struct RawAssetScanSummary {
    uint32_t meshFiles = 0;
    uint32_t totalMeshlets = 0;
    uint32_t textureFiles = 0;
};
RawAssetScanSummary ScanRawAssetsFolder(const std::string& gameDir, std::string* outLog = nullptr);

} // namespace raw::bridge
