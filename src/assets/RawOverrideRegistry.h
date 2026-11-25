#pragma once
// RawOverrideRegistry.h: Non-NIF / Non-DDS Custom Handcrafted Asset Override & Hot-Reload Registry.
// Maps vanilla virtual paths (.nif / .dds) or Creation Engine FormIDs to handcrafted modern assets
// (.glb / .gltf / .obj meshes + .png / .tga / .tif / .hdr / .exr / .ktx2 PBR texture sets)
// with deterministic FNV-1a path hashing, strict JSON manifest validation, directory mirror scanning,
// and live multi-file hot-reload diff detection.
// Zero Skyrim / Windows / D3D12 dependencies — clean-room MIT, reusable across RAW & general gamedev.

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace raw::assets {

enum class OverrideAssetKind : uint8_t {
    Mesh               = 0,
    MaterialTextureSet = 1,
    Combined           = 2
};

struct PbrTextureSetPaths {
    std::string albedoPath;    // Base color (.png, .tga, .tif, .ktx2)
    std::string normalPath;    // Tangent-space normal map
    std::string rmaohPath;     // Packed Roughness / Metallic / AO / Height
    std::string emissivePath;  // Emissive color / HDR (.png, .hdr, .exr)
    std::string heightPath;    // Optional standalone height/displacement map
};

struct CustomAssetOverride {
    std::string        vanillaPath;    // Normalized lowercase virtual path (e.g. "meshes/architecture/whiterun/wrwall01.nif")
    uint64_t           pathHash = 0;   // Deterministic 64-bit FNV-1a hash of vanillaPath
    uint32_t           formId   = 0;   // Optional Creation Engine FormID (0 if path-keyed only)
    OverrideAssetKind  kind     = OverrideAssetKind::Combined;

    std::string        meshPath;       // Handcrafted .glb / .gltf / .obj (never .nif)
    PbrTextureSetPaths textures;       // Handcrafted non-.dds PBR texture set
    std::string        materialPreset; // Optional raw::materials preset name (e.g. "NordicAshlarStone")
    float              scale      = 1.0f;
    bool               yUpToZUp   = true;
    std::string        author;
    std::string        revision;

    // Combined multi-file write-time + size fingerprint used for live hot-reload detection.
    uint64_t           fileFingerprint = 0;
};

struct HotReloadReport {
    std::vector<std::string> added;
    std::vector<std::string> modified;
    std::vector<std::string> removed;

    uint32_t TotalChanges() const {
        return static_cast<uint32_t>(added.size() + modified.size() + removed.size());
    }
};

// Normalizes any vanilla virtual path: converts '\' -> '/', lowercases ASCII, strips leading "./" and "data/".
std::string NormalizeVirtualPath(std::string_view rawPath);

// Deterministic 64-bit FNV-1a hash over a normalized virtual path.
uint64_t HashVirtualPath64(std::string_view virtualPath);

// Extension validators enforcing the modern non-.nif / non-.dds handcrafted pipeline.
bool IsAllowedModernMeshExtension(std::string_view path);
bool IsAllowedModernTextureExtension(std::string_view path);

// Validates a single CustomAssetOverride record (rejects .nif/.dds modern targets, NaN/<=0 scale, empty mapping).
bool ValidateOverrideEntry(const CustomAssetOverride& entry, std::string* outErr = nullptr);

// Computes a combined 64-bit fingerprint from filesystem write times + sizes of all non-empty paths in an entry.
uint64_t ComputeOverrideFilesFingerprint(const CustomAssetOverride& entry, std::string_view baseDir = {});

class OverrideRegistry {
public:
    void Clear();

    // Registers or updates a single override entry after strict validation.
    bool RegisterOverride(CustomAssetOverride entry, std::string* outErr = nullptr);

    // Parses & validates a JSON manifest string ("schema": "raw.overrides.v1").
    bool ParseManifestJsonString(std::string_view jsonText, std::string_view baseDir = {},
                                 std::string* outErr = nullptr);

    // Loads & validates a JSON manifest file from disk.
    bool LoadManifestFile(const std::string& manifestPath, std::string* outErr = nullptr);

    // Exports the full registry as clean, deterministic JSON ("schema": "raw.overrides.v1").
    std::string ExportManifestJson() const;
    bool        SaveManifestFile(const std::string& manifestPath, std::string* outErr = nullptr) const;

    // Scans a virtual-path mirror directory (e.g. raw_assets/overrides/meshes/... & textures/...)
    // automatically pairing _n/_normal, _rmaoh/_rmaos, _e/_emissive, _h/_height maps.
    uint32_t ScanMirrorDirectory(const std::string& overridesRootDir, std::string* outErr = nullptr);

    // Detects added, modified, or removed files across registered entries & optional scanned root dir.
    HotReloadReport PollHotReload(const std::string& baseDir = {}, bool rescanMirrorDir = false);

    // O(1) deterministic lookups.
    const CustomAssetOverride* FindByVanillaPath(std::string_view vanillaPath) const;
    const CustomAssetOverride* FindByPathHash(uint64_t pathHash) const;
    const CustomAssetOverride* FindByFormId(uint32_t formId) const;

    const std::vector<CustomAssetOverride>& All() const { return entries_; }
    size_t Count() const { return entries_.size(); }

private:
    void RebuildIndices();

    std::vector<CustomAssetOverride>       entries_;
    std::unordered_map<uint64_t, size_t>   byPathHash_;
    std::unordered_map<uint32_t, size_t>   byFormId_;
    std::string                            lastMirrorRoot_;
};

}  // namespace raw::assets
