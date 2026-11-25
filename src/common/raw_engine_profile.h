#pragma once
// raw_engine_profile.h: Cross-Title Creation Engine 1 / 1.5 / 1.8 / 2.0 Host Profile & ABI Descriptor.
// Shared header-only descriptor for Skyrim SE/AE/VR, Fallout 4/VR, Fallout 76, and Starfield (CE2).
// Lets raw_engine.dll, raw_skse.dll, and the DX11->DX12 / native DX12 render bridge adapt cleanly per title
// without hardcoding Skyrim-only paths, offsets, or hooks. Original work, MIT.

#include <windows.h>

#include <cctype>
#include <cstdint>
#include <string>

namespace raw::versions {

enum class EngineTitle : uint32_t {
    Unknown    = 0,
    SkyrimSE   = 1,
    SkyrimVR   = 2,
    Fallout4   = 3,
    Fallout4VR = 4,
    Fallout76  = 5,
    Starfield  = 6,
};

enum class EngineGen : uint32_t {
    Unknown           = 0,
    CE1_DX11          = 1,   // Skyrim SE / AE / VR (DX11 -> raw::tx DX12 translation)
    CE1_5_FO4_DX11    = 2,   // Fallout 4 / FO4VR (DX11 -> raw::tx DX12 translation, PreVis/Precombines)
    CE1_8_FO76_DX11   = 3,   // Fallout 76 (DX11 PBR/Enlighten, online multiplayer safety guard)
    CE2_Starfield_DX12 = 4,  // Starfield (native DX12 CE2, direct DXGI/D3D12 bridge without raw::tx)
};

struct EngineProfile {
    EngineTitle title = EngineTitle::Unknown;
    EngineGen   gen   = EngineGen::Unknown;
    const char* displayName   = "Unknown Creation Engine";
    const char* tablePrefix   = "skyrimse";
    const char* addrLibRelDir = "Data\\SKSE\\Plugins\\";
    const char* addrLibPrefix = "versionlib-";
    bool        usesNativeDx12       = false;
    bool        hasHavok60HzBug      = false;
    bool        hasStdio512Bug       = false;
    bool        hasPrevisPrecombines = false;
    bool        isMultiplayerOnline  = false;
    uint32_t    offTesGridCells      = 0;
    uint32_t    offCellWorldSpace    = 0;
    uint32_t    offUiMenuMap         = 0;
    uint32_t    offVmImpl            = 0;
};

inline std::string LowerAscii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

inline std::string RunningExeBaseName() {
    char path[MAX_PATH]{};
    const DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (n == 0) return {};
    std::string s(path, n);
    const size_t slash = s.find_last_of("\\/");
    return slash == std::string::npos ? s : s.substr(slash + 1);
}

inline EngineProfile DetectEngineProfile(uint32_t major = 0, uint32_t minor = 0) {
    const std::string exe = LowerAscii(RunningExeBaseName());
    EngineProfile p{};
    if (exe.find("skyrimvr") != std::string::npos) {
        p.title = EngineTitle::SkyrimVR;
        p.gen = EngineGen::CE1_DX11;
        p.displayName = "Skyrim VR (CE1 DX11)";
        p.tablePrefix = "skyrimvr";
        p.addrLibRelDir = "Data\\SKSE\\Plugins\\";
        p.addrLibPrefix = "versionlib-";
        p.hasHavok60HzBug = true;
        p.hasStdio512Bug = true;
        p.offTesGridCells = 0x78;
        p.offCellWorldSpace = 0x128;
        p.offUiMenuMap = 0x128;
        p.offVmImpl = 0x200;
        return p;
    }
    if (exe.find("skyrim") != std::string::npos) {
        p.title = EngineTitle::SkyrimSE;
        p.gen = EngineGen::CE1_DX11;
        p.displayName = "Skyrim SE/AE (CE1 DX11)";
        p.tablePrefix = "skyrimse";
        p.addrLibRelDir = "Data\\SKSE\\Plugins\\";
        p.addrLibPrefix = "versionlib-";
        p.hasHavok60HzBug = true;
        p.hasStdio512Bug = true;
        p.offTesGridCells = 0x78;
        p.offCellWorldSpace = 0x128;
        p.offUiMenuMap = 0x128;
        p.offVmImpl = (major > 1 || minor >= 7) ? 0x210u : 0x200u;
        return p;
    }
    if (exe.find("fallout4vr") != std::string::npos) {
        p.title = EngineTitle::Fallout4VR;
        p.gen = EngineGen::CE1_5_FO4_DX11;
        p.displayName = "Fallout 4 VR (CE1.5 DX11)";
        p.tablePrefix = "fallout4vr";
        p.addrLibRelDir = "Data\\F4SE\\Plugins\\";
        p.addrLibPrefix = "version-";
        p.hasHavok60HzBug = true;
        p.hasStdio512Bug = true;
        p.hasPrevisPrecombines = true;
        return p;
    }
    if (exe.find("fallout4") != std::string::npos) {
        p.title = EngineTitle::Fallout4;
        p.gen = EngineGen::CE1_5_FO4_DX11;
        p.displayName = "Fallout 4 (CE1.5 DX11)";
        p.tablePrefix = "fallout4";
        p.addrLibRelDir = "Data\\F4SE\\Plugins\\";
        p.addrLibPrefix = "version-";
        p.hasHavok60HzBug = true;
        p.hasStdio512Bug = true;
        p.hasPrevisPrecombines = true;
        return p;
    }
    if (exe.find("fallout76") != std::string::npos) {
        p.title = EngineTitle::Fallout76;
        p.gen = EngineGen::CE1_8_FO76_DX11;
        p.displayName = "Fallout 76 (CE1.8 DX11)";
        p.tablePrefix = "fallout76";
        p.addrLibRelDir = "";
        p.addrLibPrefix = "";
        p.hasHavok60HzBug = false;
        p.hasStdio512Bug = false;
        p.hasPrevisPrecombines = true;
        p.isMultiplayerOnline = true;
        return p;
    }
    if (exe.find("starfield") != std::string::npos) {
        p.title = EngineTitle::Starfield;
        p.gen = EngineGen::CE2_Starfield_DX12;
        p.displayName = "Starfield (CE2 Native DX12)";
        p.tablePrefix = "starfield";
        p.addrLibRelDir = "Data\\SFSE\\Plugins\\";
        p.addrLibPrefix = "versionlib-";
        p.usesNativeDx12 = true;
        p.hasHavok60HzBug = false;
        p.hasStdio512Bug = false;
        return p;
    }
    return p;
}

}  // namespace raw::versions
