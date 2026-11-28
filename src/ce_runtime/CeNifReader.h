#pragma once
// CeNifReader.h: Clean-Room Skyrim SE / Fallout 4 NetImmerse (.nif v20.2.0.7 BSTriShape) Geometry Extractor.
// Extracts triangle geometry directly from vanilla .nif files (or BSA-extracted bytes) and builds 64-vert / 64-tri
// RAW-MESH meshlets (RawMeshAsset) so our open runtime & DX12 Mesh Shader / DXR pipeline can consume native
// Creation Engine meshes directly with zero proprietary code. Original work, MIT.

#include <cstddef>
#include <cstdint>
#include <string>

#include "../d3d12_bridge/RawAssetBridge.h"

namespace raw::ce {

struct NifSummary {
    uint32_t version = 0;
    uint32_t userVersion = 0;
    uint32_t userVersion2 = 0;
    uint32_t numBlocks = 0;
    uint32_t triShapeBlocks = 0;
    uint32_t totalVertices = 0;
    uint32_t totalTriangles = 0;
};

// Parses a NetImmerse v20.2.0.7 (.nif) binary buffer, extracts all BSTriShape / BSDynamicTriShape geometry,
// and builds 64-vert / 64-tri RAW-MESH meshlets into outAsset (when non-null).
bool LoadNifMeshBytes(const uint8_t* bytes, size_t size, const std::string& label,
                      raw::bridge::RawMeshAsset* outAsset, NifSummary* outSummary = nullptr, std::string* outErr = nullptr);

}  // namespace raw::ce
