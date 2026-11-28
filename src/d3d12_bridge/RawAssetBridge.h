#pragma once
// RawAssetBridge.h: Native Zero-Dependency Modern Asset Loader & Meshletizer for RAW (THE PLAYGROUND).
// Lets creators drop standard Blender exports (.glb / glTF 2.0 binary, .obj) directly into raw_assets/
// without NifSkope / .nif conversion, automatically building 64-vert/64-tri RAW-MESH meshlets with
// bounding spheres and normal cones for Hardware DX12 Amplification + Mesh Shaders (RawMesh.hlsl) and DXR 1.2 BLAS.
// Original work, MIT.
#include <cstdint>
#include <string>
#include <vector>

namespace raw::bridge
{

struct RawMeshVertex {
    float pos[3];
    float normal[3];
    float uv[2];
};

struct RawMeshletDesc {
    uint32_t vertexOffset;
    uint32_t triangleOffset;
    uint32_t vertexCount;
    uint32_t triangleCount;
    float    boundSphere[4];   // xyz center, w radius
    float    normalCone[4];    // xyz axis, w cutoff cos(halfAngle + pi/2)
};

struct RawPbrMaterial {
    std::string          name;
    float                baseColorFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float                metallicFactor     = 0.0f;
    float                roughnessFactor    = 0.65f;
    float                emissiveFactor[3]  = {0.0f, 0.0f, 0.0f};
    std::vector<uint8_t> embeddedBaseColorImage;   // raw PNG/JPEG bytes extracted from glTF .glb bufferView when present
    std::string          embeddedBaseColorMime;
};

struct RawMeshAsset {
    std::string                 name;
    std::vector<RawMeshVertex>  vertices;
    std::vector<uint32_t>       indices;
    std::vector<RawMeshletDesc> meshlets;
    std::vector<RawPbrMaterial> materials;
    bool                        fromGlb = false;
};

// Loads a standard Wavefront .obj or Blender glTF 2.0 binary (.glb) mesh (full bufferView/accessor/index/PBR support)
// and builds 64-vert / 64-tri RAW-MESH meshlets.
bool LoadModernMeshAsset(const std::string& path, RawMeshAsset* outAsset, std::string* outErr = nullptr);
bool LoadModernMeshAssetFromGlbBytes(const uint8_t* bytes, size_t size, const std::string& label,
                                     RawMeshAsset* outAsset, std::string* outErr = nullptr, bool yUpToZUp = true);

// Partitions any indexed triangle mesh into 64-vert / 64-tri RAW-MESH meshlets with exact spheres & normal cones.
void BuildRawMeshlets(const std::vector<RawMeshVertex>& srcVerts, const std::vector<uint32_t>& srcIndices,
                      std::vector<RawMeshVertex>* outVerts, std::vector<uint32_t>* outIndices,
                      std::vector<RawMeshletDesc>* outMeshlets);

} // namespace raw::bridge
