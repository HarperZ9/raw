//=============================================================================
//  ShaderLoader.cpp — External HLSL loading + developer tools
//  Copyright (c) 2026 Zain D. Harper (papacr0w). All rights reserved.
//=============================================================================

#include "ShaderLoader.h"
#include <SKSE/SKSE.h>
#include <d3dcompiler.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <mutex>
#include <unordered_map>
#include <chrono>

namespace SB
{

static std::filesystem::path s_shaderDir;
static std::mutex s_mutex;
static std::unordered_map<std::string, std::string> s_fileCache;
static std::unordered_map<std::string, std::filesystem::file_time_type> s_fileTimestamps;

// Developer tool state
static std::vector<ShaderInfo> s_shaderInfos;
static std::vector<ShaderError> s_errors;
static uint64_t s_frameCounter = 0;
static uint32_t s_fromDisk = 0;
static uint32_t s_fromEmbedded = 0;

void ShaderLoader::SetShaderDir(const std::filesystem::path& dir)
{
    std::lock_guard lock(s_mutex);
    s_shaderDir = dir;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
}

void ShaderLoader::InvalidateAll()
{
    std::lock_guard lock(s_mutex);

    // Selective invalidation: only clear entries whose disk files changed
    int invalidated = 0;
    if (!s_shaderDir.empty()) {
        std::vector<std::string> toRemove;
        for (auto& [name, cachedSource] : s_fileCache) {
            auto filePath = s_shaderDir / (name + ".hlsl");
            std::error_code ec;
            if (std::filesystem::exists(filePath, ec)) {
                auto currentTime = std::filesystem::last_write_time(filePath, ec);
                auto it = s_fileTimestamps.find(name);
                if (it == s_fileTimestamps.end() || it->second != currentTime) {
                    toRemove.push_back(name);
                    s_fileTimestamps[name] = currentTime;
                }
            }
        }
        for (auto& name : toRemove) {
            s_fileCache.erase(name);
            invalidated++;
        }
    }

    // If no selective invalidation found anything, clear all (fallback)
    if (invalidated == 0) {
        s_fileCache.clear();
        invalidated = static_cast<int>(s_fileCache.size());
    }

    // Clear errors on reload
    s_errors.clear();
    s_frameCounter++;

    SKSE::log::info("ShaderLoader: invalidated {} cached shaders", invalidated);
}

std::vector<std::string> ShaderLoader::CheckForChanges()
{
    std::lock_guard lock(s_mutex);
    std::vector<std::string> changed;
    if (s_shaderDir.empty()) return changed;

    for (auto& [name, cachedSource] : s_fileCache) {
        auto filePath = s_shaderDir / (name + ".hlsl");
        std::error_code ec;
        if (!std::filesystem::exists(filePath, ec)) continue;
        auto currentTime = std::filesystem::last_write_time(filePath, ec);
        if (ec) continue;
        auto it = s_fileTimestamps.find(name);
        if (it != s_fileTimestamps.end() && it->second != currentTime)
            changed.push_back(name);
    }

    // Also check for new files that aren't cached yet
    for (auto& entry : std::filesystem::directory_iterator(s_shaderDir)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        if (ext != ".hlsl") continue;
        auto stem = entry.path().stem().string();
        if (s_fileCache.find(stem) == s_fileCache.end() &&
            s_fileTimestamps.find(stem) != s_fileTimestamps.end()) {
            // File exists on disk, was loaded before but cache was cleared
            auto currentTime = std::filesystem::last_write_time(entry.path());
            if (s_fileTimestamps[stem] != currentTime)
                changed.push_back(stem);
        }
    }

    return changed;
}

std::string ShaderLoader::Load(const char* name, const char* embeddedSource)
{
    if (!name || !embeddedSource) return embeddedSource ? embeddedSource : "";

    std::lock_guard lock(s_mutex);

    // Check file cache
    auto it = s_fileCache.find(name);
    if (it != s_fileCache.end())
        return it->second;

    // Try external file
    if (!s_shaderDir.empty()) {
        auto filePath = s_shaderDir / (std::string(name) + ".hlsl");
        std::error_code ec;
        if (std::filesystem::exists(filePath, ec)) {
            std::ifstream file(filePath, std::ios::in);
            if (file.is_open()) {
                std::stringstream ss;
                ss << file.rdbuf();
                std::string source = ss.str();
                if (!source.empty()) {
                    s_fileCache[name] = source;
                    // Track file timestamp for selective invalidation
                    std::error_code tec;
                    s_fileTimestamps[name] = std::filesystem::last_write_time(filePath, tec);
                    return source;
                }
            }
        }
    }

    return embeddedSource;
}

ID3DBlob* ShaderLoader::Compile(const char* name, const char* embeddedSource,
                                 const char* entryPoint, const char* target, UINT flags)
{
    // Load source (disk or embedded)
    bool fromDisk = false;
    std::string source;
    {
        std::lock_guard lock(s_mutex);

        // Check if external file exists
        if (!s_shaderDir.empty()) {
            auto filePath = s_shaderDir / (std::string(name) + ".hlsl");
            std::error_code ec;
            if (std::filesystem::exists(filePath, ec)) {
                // Check file cache
                auto it = s_fileCache.find(name);
                if (it != s_fileCache.end()) {
                    source = it->second;
                    fromDisk = true;
                } else {
                    std::ifstream file(filePath, std::ios::in);
                    if (file.is_open()) {
                        std::stringstream ss;
                        ss << file.rdbuf();
                        source = ss.str();
                        if (!source.empty()) {
                            s_fileCache[name] = source;
                            fromDisk = true;
                        }
                    }
                }
            }
        }

        if (source.empty() && embeddedSource) {
            source = embeddedSource;
            fromDisk = false;
        }
    }

    if (source.empty()) return nullptr;

    // Compile with timing
    auto t0 = std::chrono::high_resolution_clock::now();

    ID3DBlob* blob = nullptr;
    ID3DBlob* err  = nullptr;

    // Build the full path for the include handler to resolve relative #include directives.
    // D3D_COMPILE_STANDARD_FILE_INCLUDE resolves #include relative to pSourceName's directory.
    std::string sourcePath;
    if (fromDisk && !s_shaderDir.empty()) {
        sourcePath = (s_shaderDir / (std::string(name) + ".hlsl")).string();
    }

    HRESULT hr = D3DCompile(
        source.c_str(), source.size(),
        sourcePath.empty() ? name : sourcePath.c_str(),
        nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entryPoint, target, flags, 0,
        &blob, &err);

    auto t1 = std::chrono::high_resolution_clock::now();
    float compileMs = std::chrono::duration<float, std::milli>(t1 - t0).count();

    // Record shader info for developer tools
    {
        std::lock_guard lock(s_mutex);

        ShaderInfo info;
        info.name = name ? name : "unknown";
        info.source = fromDisk ? "disk" : "embedded";
        info.entryPoint = entryPoint ? entryPoint : "";
        info.target = target ? target : "";
        info.compileTimeMs = compileMs;
        info.fromDisk = fromDisk;

        if (FAILED(hr)) {
            std::string errMsg;
            if (err) {
                errMsg = static_cast<const char*>(err->GetBufferPointer());
                err->Release();
            } else {
                errMsg = "Unknown compilation error";
            }

            info.compiled = false;
            info.errorMsg = errMsg;

            // Add to error list
            ShaderError se;
            se.shaderName = info.name;
            se.errorMsg = errMsg;
            se.timestamp = s_frameCounter;
            s_errors.push_back(se);

            SKSE::log::error("ShaderLoader: '{}' FAILED ({}ms): {}",
                info.name, compileMs, errMsg);
        } else {
            info.compiled = true;
            info.bytecodeSize = blob ? static_cast<uint32_t>(blob->GetBufferSize()) : 0;

            if (fromDisk) s_fromDisk++;
            else s_fromEmbedded++;
        }

        if (err) err->Release();

        // Update or add to shader info list
        bool found = false;
        for (auto& si : s_shaderInfos) {
            if (si.name == info.name) {
                si = info;
                found = true;
                break;
            }
        }
        if (!found) s_shaderInfos.push_back(info);
    }

    return blob;
}

const std::vector<ShaderInfo>& ShaderLoader::GetShaderInfos()
{
    return s_shaderInfos;
}

const std::vector<ShaderError>& ShaderLoader::GetErrors()
{
    return s_errors;
}

void ShaderLoader::ClearErrors()
{
    std::lock_guard lock(s_mutex);
    s_errors.clear();
}

bool ShaderLoader::HasErrors()
{
    return !s_errors.empty();
}

void ShaderLoader::GetStats(uint32_t& fromDisk, uint32_t& fromEmbedded, uint32_t& errors)
{
    // Count from actual shader info list (accurate even after reloads)
    fromDisk = 0;
    fromEmbedded = 0;
    for (auto& si : s_shaderInfos) {
        if (si.compiled) {
            if (si.fromDisk) fromDisk++;
            else fromEmbedded++;
        }
    }
    errors = static_cast<uint32_t>(s_errors.size());
}

} // namespace SB
