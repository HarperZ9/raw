#pragma once
// RawOverrideRegistry.h: Creator Hot-Swap Asset Override Registry (Zero .nif / .dds / .esp Workflow).
// Lets artists drop Blender .glb/.obj meshes and .png/.jpg/.tga PBR textures directly into:
//   <gameDir>\raw_assets\overrides\
// mapped automatically by filename FormID ("0x00012345.glb"), EDID ("WHIronSword.glb"), or overrides.ini.
// Automatically decodes embedded .glb PBR images via WIC, meshletizes to 64v/64t RAW-MESH clusters,
// uploads to RawMeshHost when a DX12 device is bound, and supports live timestamp hot-reload.
// Original work, MIT.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "RawAssetBridge.h"
#include "RawMeshHost.h"
#include "RawTextureBridge.h"

namespace raw::bridge {

struct RawOverrideEntry {
    std::string          key;             // normalized lowercase EDID, virtual mesh path, or hex FormID
    uint32_t             formId = 0;      // non-zero if mapped to a Creation Engine FormID
    std::string          sourcePath;
    uint64_t             lastWriteTicks = 0;
    RawMeshAsset         mesh;
    RawTextureAsset      embeddedAlbedo;  // decoded from .glb embedded PNG/JPEG when present
    RawMeshGpuAsset      gpuMesh;
    bool                 hasGpuMesh = false;
};

class RawOverrideRegistry {
public:
    // Scans <gameDir>\raw_assets\overrides\ (plus optional overrides.ini) and optionally uploads to RawMeshHost.
    uint32_t ScanDirectory(const std::string& overridesDir, RawMeshHost* meshHost = nullptr, std::string* outLog = nullptr);

    // Checks file write timestamps and hot-reloads any modified .glb/.obj overrides in place.
    uint32_t PollHotReload(RawMeshHost* meshHost = nullptr);

    const RawOverrideEntry* FindByFormId(uint32_t formId) const;
    const RawOverrideEntry* FindByKey(const std::string& key) const;
    uint32_t Count() const { return static_cast<uint32_t>(m_entries.size()); }
    uint32_t TotalMeshlets() const;

private:
    bool LoadEntry(RawOverrideEntry& e, RawMeshHost* meshHost);

    std::vector<RawOverrideEntry>          m_entries;
    std::unordered_map<uint32_t, size_t>   m_byFormId;
    std::unordered_map<std::string, size_t> m_byKey;
};

}  // namespace raw::bridge
