#pragma once
// rt_shader_hotreload.h: Wave 58 Fail-Closed Live Shader Hot-Reload Watchdog (`raw::rt`).
// Non-blocking raw_shaders\ timestamp + 64-bit FNV-1a content-hash watcher for .hlsl/.hlsli files
// with atomic pre-validation (non-empty, max-size guard, zero NUL bytes, comment/string-aware
// balanced braces & #include hygiene), dirty shader tracking, monotonic reload generation C ABI
// (RAW_GetShaderHotReloadState), and on-demand `reload_shaders` trigger support.
// Original work, MIT.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace raw::rt {

constexpr size_t kMaxShaderSourceBytes = 2u * 1024u * 1024u; // 2 MB per-file safety guard

enum class ShaderRejectReason : uint8_t {
    Ok                          = 0u,
    EmptyFile                   = 1u,
    ExceedsMaxSize              = 2u,
    ContainsNulByte             = 3u,
    UnbalancedBraces            = 4u,
    MalformedInclude            = 5u,
    UnterminatedCommentOrString = 6u,
    IoReadError                 = 7u,
};

const char* ShaderRejectReasonName(ShaderRejectReason reason) noexcept;

// Pure 64-bit FNV-1a content hash.
uint64_t ComputeShaderFnv1a64(const uint8_t* data, size_t size) noexcept;

struct ShaderPreValidationResult {
    bool               valid        = false;
    ShaderRejectReason reason       = ShaderRejectReason::EmptyFile;
    uint64_t           fnv1a64      = 0ull;
    uint32_t           sizeBytes    = 0u;
    uint32_t           includeCount = 0u;
    char               detail[128]{};
};

// Pure fail-closed HLSL/HLSLI source pre-validator (zero exceptions, zero allocations).
ShaderPreValidationResult ValidateShaderSourceBytes(const uint8_t* data,
                                                    size_t size,
                                                    const char* fileName = "") noexcept;

// Exported C ABI telemetry struct (RAW_GetShaderHotReloadState).
struct RawShaderHotReloadState {
    uint8_t  watcherActive      = 0u;
    uint8_t  dirExists          = 0u;
    uint8_t  lastRejectReason   = 0u; // ShaderRejectReason
    uint8_t  reserved0          = 0u;
    uint32_t reloadGeneration   = 0u; // Strictly monotonic on accepted reloads
    uint32_t trackedFileCount   = 0u;
    uint32_t dirtyFileCount     = 0u;
    uint32_t acceptedReloads    = 0u;
    uint32_t rejectedEdits      = 0u;
    uint32_t scanCount          = 0u;
    uint64_t combinedHashFnv1a  = 0ull;
    char     lastDirtyShader[96]{};
    char     lastRejectedShader[96]{};
};

// Runtime lifecycle hooks (safe in BOTH raw_engine.dll and RAW_SIM_PURE_TEST=1).
std::string              ShaderHotReloadInit(const std::string& shadersDir);
void                     ShaderHotReloadOnPresent();
bool                     ShaderHotReloadPollScan(bool forceReloadAll = false);
bool                     ShaderHotReloadTriggerNow(std::string* outSummary = nullptr);
std::vector<std::string> ShaderHotReloadGetDirtyList();
void                     ShaderHotReloadShutdown();

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetShaderHotReloadState(raw::rt::RawShaderHotReloadState* out);
