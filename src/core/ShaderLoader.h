#pragma once
//=============================================================================
//  ShaderLoader — Load, compile, and hot-reload HLSL shaders
//
//  External file override: Data/SKSE/Plugins/RAW/Shaders/{name}.hlsl
//  Falls back to embedded string if file doesn't exist.
//
//  Developer tools:
//    - Shader error overlay (compilation failures shown in-game)
//    - Source viewer (list all loaded shaders, see source/status)
//    - Hot-reload (F12 invalidates, next compile re-reads from disk)
//    - File change detection (only recompile what changed)
//
//  Copyright (c) 2026 Zain D. Harper (papacr0w). All rights reserved.
//=============================================================================

#include <string>
#include <vector>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <filesystem>

namespace SB
{

struct ShaderInfo
{
    std::string name;
    std::string source;        // "disk" or "embedded"
    std::string entryPoint;
    std::string target;
    std::string errorMsg;      // empty if compiled OK
    uint32_t    bytecodeSize = 0;
    float       compileTimeMs = 0.0f;
    bool        compiled = false;
    bool        fromDisk = false;
};

struct ShaderError
{
    std::string shaderName;
    std::string errorMsg;
    uint64_t    timestamp;     // frame when error occurred
};

class ShaderLoader
{
public:
    /// Load shader source: external file if exists, else embedded fallback.
    static std::string Load(const char* name, const char* embeddedSource);

    /// Compile a shader with external file support + error tracking.
    static ID3DBlob* Compile(const char* name, const char* embeddedSource,
                              const char* entryPoint, const char* target,
                              UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3);

    /// Mark all cached file reads as dirty (forces re-read on next Load).
    static void InvalidateAll();

    /// Check if any shader files changed on disk since last load.
    /// Returns the names of changed files (empty if nothing changed).
    static std::vector<std::string> CheckForChanges();

    /// Set the shader directory path.
    static void SetShaderDir(const std::filesystem::path& dir);

    // ── Developer tool API ──────────────────────────────────────────

    /// Get info about all compiled shaders (for source viewer tab).
    static const std::vector<ShaderInfo>& GetShaderInfos();

    /// Get current compilation errors (for error overlay).
    static const std::vector<ShaderError>& GetErrors();

    /// Clear all errors (after user acknowledges).
    static void ClearErrors();

    /// Check if any errors exist.
    static bool HasErrors();

    /// Get count of shaders loaded from disk vs embedded.
    static void GetStats(uint32_t& fromDisk, uint32_t& fromEmbedded, uint32_t& errors);
};

} // namespace SB
