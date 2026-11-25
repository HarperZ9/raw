#pragma once
// RawNifLoader.h: 100% Clean-Room Skyrim SE NetImmerse `.NIF` v20.2.0.7 (User 12, Stream 100)
// Scene-Graph, BSTriShape & BSShaderTextureSet Parser (`raw::worldgen`).
// Zero Bethesda SDK / third-party library / D3D12 / Windows header dependencies.
// Decodes NiNode/BSFadeNode hierarchical transforms, BSTriShape/BSDynamicTriShape/BSSubIndexTriShape
// (FP16 & FP32 VertexDesc, packed byte normals/tangents, vertex colors, uint16 triangle indices),
// BSLightingShaderProperty -> BSShaderTextureSet texture paths, and emits hardware-validated
// 64v/126t GPU meshlets (`ProceduralMesh`) ready for `RawMeshHost`, `RawTerrainWorld.hlsl`, & `.glb`/`.obj`!
// Original work, MIT.

#include "RawMeshPipeline.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace raw::worldgen {

inline constexpr uint32_t kNifVersion20_2_0_7  = 0x14020007u;
inline constexpr uint32_t kNifUserVersionSSE   = 12u;
inline constexpr uint32_t kNifUserVersion2SSE  = 100u;

// SSE BSVertexDesc flags (located at bits 44..54 of uint64_t vertexDesc)
enum NifVertexFlags : uint32_t {
    kNifVfVertex        = 0x001u,
    kNifVfUv            = 0x002u,
    kNifVfUv2           = 0x004u,
    kNifVfNormal        = 0x008u,
    kNifVfTangent       = 0x010u,
    kNifVfColors        = 0x020u,
    kNifVfSkinned       = 0x040u,
    kNifVfLandData      = 0x080u,
    kNifVfEyeData       = 0x100u,
    kNifVfInstance      = 0x200u,
    kNifVfFullPrecision = 0x400u,
};

struct NifTransform {
    Vec3                 translation{0.0f, 0.0f, 0.0f};
    std::array<float, 9> rotation{1.0f, 0.0f, 0.0f,
                                  0.0f, 1.0f, 0.0f,
                                  0.0f, 0.0f, 1.0f}; // Row-major 3x3 NiMatrix3
    float                scale = 1.0f;

    Vec3 TransformPoint(Vec3 p) const;
    Vec3 TransformDirection(Vec3 v) const;
    static NifTransform Compose(const NifTransform& parent, const NifTransform& local);
};

struct NifHeaderInfo {
    std::string headerString;
    uint32_t    version       = 0u;
    uint8_t     endianType    = 1u;
    uint32_t    userVersion   = 0u;
    uint32_t    userVersion2  = 0u;
    uint32_t    numBlocks     = 0u;
    uint16_t    numBlockTypes = 0u;
    uint32_t    numStrings    = 0u;
};

struct NifTextureSet {
    int32_t                  blockIndex = -1;
    std::vector<std::string> textures; // [0]=diffuse, [1]=normal, [2]=glow/detail, [3]=parallax, [4]=env, [5]=spec/mask
    std::string              diffusePath;
    std::string              normalPath;
    std::string              specularPath;
};

struct NifNodeInfo {
    int32_t              blockIndex = -1;
    int32_t              parentBlock = -1;
    std::string          blockType;
    std::string          name;
    uint32_t             flags = 0u;
    NifTransform         localTransform{};
    NifTransform         worldTransform{};
    std::vector<int32_t> children;
};

struct NifVertex {
    Vec3  position{};
    Vec3  normal{0.0f, 0.0f, 1.0f};
    Vec3  tangent{1.0f, 0.0f, 0.0f};
    Vec2  uv{};
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
};

struct NifTriShape {
    int32_t                blockIndex        = -1;
    int32_t                parentBlock       = -1;
    std::string            blockType;
    std::string            name;
    NifTransform           localTransform{};
    NifTransform           worldTransform{};
    uint64_t               vertexDesc        = 0ull;
    uint32_t               vertexFlags       = 0u;
    uint32_t               vertexStride      = 0u;
    bool                   fullPrecisionPos  = false;
    bool                   hasNormals        = false;
    bool                   hasUvs            = false;
    bool                   hasColors         = false;
    bool                   isSkinned         = false;
    int32_t                shaderPropertyRef = -1;
    int32_t                textureSetRef     = -1;
    NifTextureSet          textureSet{};
    std::vector<NifVertex> vertices;  // Already transformed into scene root space!
    std::vector<uint32_t>  indices;   // 3 per triangle
};

struct NifScene {
    std::string              label;
    NifHeaderInfo            header{};
    std::vector<std::string> blockTypes;
    std::vector<std::string> strings;
    std::vector<NifNodeInfo> nodes;
    std::vector<NifTriShape> shapes;
    uint32_t                 totalVertices  = 0u;
    uint32_t                 totalTriangles = 0u;
};

// Exact IEEE-754 binary16 (FP16 half-float) <-> FP32 conversion helpers.
float    HalfBitsToFloat(uint16_t h);
uint16_t FloatToHalfBits(float f);

// Parses a Skyrim SE v20.2.0.7 `.nif` from memory or disk into a full hierarchical NifScene.
bool ParseNifBytes(const uint8_t* bytes, size_t size, const std::string& label,
                   NifScene* outScene, std::string* outErr = nullptr);
bool ParseNifFile(const std::string& path, NifScene* outScene, std::string* outErr = nullptr);

// Classifies a Skyrim texture path (diffuse .dds) into RAW's 8 canonical PBR terrain/world material channels.
TerrainMaterialId ClassifyNifTextureMaterial(const std::string& diffusePath);

// Builds a hardware-validated 64v/126t GPU meshlet `ProceduralMesh` (zero degenerate triangles, unit normals,
// normalized 8-channel splats) directly from a parsed NifScene!
bool BuildProceduralMeshFromNif(const NifScene& scene, uint32_t maxMeshletTris,
                                ProceduralMesh* outMesh, std::string* outErr = nullptr);

}  // namespace raw::worldgen
