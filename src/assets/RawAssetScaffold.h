#pragma once
// RawAssetScaffold.h: Handcrafted Asset Authoring Scaffolding, Quality Gates & Clean-Room BC7/RGBA8 Cooker.
// Validates handcrafted meshes (winding, manifoldness, orthonormal MikkTSpace tangents, UV bounds,
// triangle & 64v/64t meshlet budgets), validates PBR textures (power-of-2, sRGB/linear calibration,
// unit-length positive-Z tangent normals, energy-conserving albedo bounds), generates per-asset
// authoring scaffold manifests + Blender scale cages (.obj), and cooks RGBA8 textures into
// clean-room BC7 Mode 6 compressed mipmapped .rawtex packages.
// Zero Skyrim / Windows / D3D12 dependencies — clean-room MIT, reusable across RAW & general gamedev.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace raw::assets {

enum class IssueSeverity : uint8_t {
    Warning = 0,
    Error   = 1
};

struct QualityIssue {
    IssueSeverity severity = IssueSeverity::Error;
    std::string   code;
    std::string   message;
};

struct QualityReport {
    bool                      passed = true;
    std::vector<QualityIssue> issues;

    uint32_t vertexCount         = 0;
    uint32_t triangleCount       = 0;
    uint32_t meshletCount        = 0;
    uint32_t nonManifoldEdges    = 0;
    uint32_t invertedWindingEdges = 0;
    uint32_t degenerateTriangles = 0;
    float    boundSphereRadius   = 0.0f;

    float    minLinearLuma       = 0.0f;
    float    maxLinearLuma       = 0.0f;
    float    worstNormalLenErr   = 0.0f;

    void AddError(std::string code, std::string msg) {
        passed = false;
        issues.push_back({IssueSeverity::Error, std::move(code), std::move(msg)});
    }
    void AddWarning(std::string code, std::string msg) {
        issues.push_back({IssueSeverity::Warning, std::move(code), std::move(msg)});
    }
};

// Standalone handcrafted mesh representation (zero D3D12 coupling; trivially maps to RawMeshAsset).
struct ScaffoldMeshVertex {
    float pos[3]     = {0.0f, 0.0f, 0.0f};
    float normal[3]  = {0.0f, 0.0f, 1.0f};
    float tangent[4] = {1.0f, 0.0f, 0.0f, 1.0f};  // xyz unit tangent, w handedness (+1 or -1)
    float uv[2]      = {0.0f, 0.0f};
};

struct ScaffoldMesh {
    std::string                     name;
    std::vector<ScaffoldMeshVertex> vertices;
    std::vector<uint32_t>           indices;
};

struct MeshBudgetConfig {
    uint32_t maxTriangles        = 120000u;
    uint32_t maxMeshlets         = 2048u;
    float    uvMinBound          = -8.0f;
    float    uvMaxBound          =  8.0f;
    float    normalLenTol        =  0.08f;
    float    tangentOrthoTol     =  0.18f;
    bool     requireManifold     =  true;
};

// Runs full geometry, winding, manifoldness, MikkTSpace tangent, UV, and 64v/64t meshlet budget quality gates.
QualityReport ValidateHandcraftedMesh(const ScaffoldMesh& mesh, const MeshBudgetConfig& cfg = {});

enum class TextureRole : uint8_t {
    AlbedoSrgb    = 0,
    AlbedoLinear  = 1,
    TangentNormal = 2,
    RmaohLinear   = 3,
    EmissiveHdr   = 4,
    HeightLinear  = 5
};

struct TextureGateConfig {
    uint32_t minDim               = 16u;
    uint32_t maxDim               = 8192u;
    bool     requirePowerOfTwo    = true;
    float    maxDielectricAlbedo  = 0.90f;  // Linear luminance upper bound (energy conservation)
    float    minDielectricAlbedo  = 0.015f; // Linear luminance lower bound warning threshold
    float    normalLenTol         = 0.14f;
};

// Validates a handcrafted RGBA8 PBR texture map against physical PBR calibration & format rules.
QualityReport ValidatePbrTextureMap(uint32_t width, uint32_t height, const std::vector<uint8_t>& rgba8,
                                    TextureRole role, const TextureGateConfig& cfg = {});

// ============================================================================
// Per-Asset Authoring Scaffold Manifest & Blender Scale Cage (.obj) Generator
// ============================================================================
struct AssetScaffoldSpec {
    std::string vanillaPath;          // e.g. "meshes/architecture/whiterun/wrwall01.nif"
    uint32_t    formId        = 0;
    std::string assetSlug;            // e.g. "wrwall01"
    std::string materialPreset;       // e.g. "NordicAshlarStone"
    float       aabbMin[3]    = {-64.0f, -64.0f, 0.0f};
    float       aabbMax[3]    = { 64.0f,  64.0f, 256.0f};
    float       pivot[3]      = {  0.0f,   0.0f, 0.0f};
    float       texelDensityPxPerCm = 5.12f;
    uint32_t    triangleBudget      = 24000u;
};

// Generates a per-asset authoring scaffold JSON manifest linking target .glb, 5 PBR maps, bounds & budgets.
std::string GenerateAssetScaffoldJson(const AssetScaffoldSpec& spec);

// Exports a clean-room reference Scale Cage .obj (exact vanilla AABB + pivot) for direct Blender import.
std::string GenerateScaleCageObj(const AssetScaffoldSpec& spec);

// ============================================================================
// Clean-Room MIT BC7 Mode 6 Compressor + Content-Addressed .rawtex Cooker
// ============================================================================
enum class CookedTextureFormat : uint32_t {
    RGBA8_UNORM = 1u,
    BC7_UNORM   = 2u
};

struct CookedTextureBlob {
    uint32_t            magic       = 0x58545752u;  // "RWTX"
    uint32_t            version     = 1u;
    CookedTextureFormat format      = CookedTextureFormat::BC7_UNORM;
    uint32_t            width       = 0;
    uint32_t            height      = 0;
    uint32_t            mipCount    = 0;
    uint64_t            contentHash = 0;
    std::vector<uint8_t> payload;
};

// Encodes one 4x4 RGBA8 block (64 bytes) into 16 bytes of BC7 Mode 6 (7-bit RGBA endpoints + shared p-bit + 4-bit indices).
void EncodeBc7Mode6Block(const uint8_t rgba4x4[64], uint8_t outBlock16[16]);

// Decodes one 16-byte BC7 Mode 6 block back to 4x4 RGBA8 (64 bytes) for verification & CPU preview.
bool DecodeBc7Mode6Block(const uint8_t block16[16], uint8_t outRgba4x4[64]);

// Compresses a full RGBA8 image into BC7 Mode 6 blocks (4:1 VRAM compression).
bool CookRgba8ToBc7Mode6(uint32_t width, uint32_t height, const std::vector<uint8_t>& rgba8,
                         std::vector<uint8_t>* outBc7Blocks, std::string* outErr = nullptr);

// Builds a mipmapped content-addressed CookedTextureBlob (either RGBA8 or BC7 Mode 6) and serializes to .rawtex bytes.
bool CookTexturePackage(uint32_t width, uint32_t height, const std::vector<uint8_t>& rgba8,
                        CookedTextureFormat format, bool generateMips,
                        CookedTextureBlob* outBlob, std::string* outErr = nullptr);

std::vector<uint8_t> SerializeRawTexBlob(const CookedTextureBlob& blob);
bool                 DeserializeRawTexBlob(const uint8_t* bytes, size_t size,
                                           CookedTextureBlob* outBlob, std::string* outErr = nullptr);

}  // namespace raw::assets
