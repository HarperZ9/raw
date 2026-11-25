// rt_shader_hotreload.cpp: Wave 58 Fail-Closed Live Shader Hot-Reload Watchdog (`raw::rt`).
// Original work, MIT.

#include "rt_shader_hotreload.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>

namespace raw::rt {
namespace {

constexpr uint32_t kWatchCadenceFrames = 60u;

struct TrackedShaderEntry {
    int64_t  writeTimeTicks = 0;
    uint64_t fileSize       = 0ull;
    uint64_t lastValidHash  = 0ull;
    uint64_t lastSeenHash   = 0ull;
};

struct ShaderHotReloadRuntime {
    std::mutex                                    mu;
    bool                                          initialized = false;
    std::string                                   shadersDir;
    uint32_t                                      presentTick = 0u;
    std::unordered_map<std::string, TrackedShaderEntry> tracked;
    std::vector<std::string>                      dirtyShaders;
    RawShaderHotReloadState                       state{};
} g_shr;

void CopyStrTrunc(char* dst, size_t dstCap, const std::string& src) noexcept {
    if (!dst || dstCap == 0u) return;
    const size_t n = std::min(src.size(), dstCap - 1u);
    if (n > 0u) std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

bool IsShaderExtension(const std::filesystem::path& p) {
    std::string ext = p.extension().string();
    for (char& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return ext == ".hlsl" || ext == ".hlsli";
}

bool ReadFileBounded(const std::filesystem::path& path,
                     std::vector<uint8_t>& outBytes,
                     ShaderRejectReason& outFailReason) {
    outBytes.clear();
    outFailReason = ShaderRejectReason::Ok;
    try {
        std::error_code ec;
        const uint64_t sz = std::filesystem::file_size(path, ec);
        if (ec) {
            outFailReason = ShaderRejectReason::IoReadError;
            return false;
        }
        if (sz == 0ull) {
            outFailReason = ShaderRejectReason::EmptyFile;
            return false;
        }
        if (sz > static_cast<uint64_t>(kMaxShaderSourceBytes)) {
            outFailReason = ShaderRejectReason::ExceedsMaxSize;
            return false;
        }
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            outFailReason = ShaderRejectReason::IoReadError;
            return false;
        }
        outBytes.resize(static_cast<size_t>(sz));
        in.read(reinterpret_cast<char*>(outBytes.data()), static_cast<std::streamsize>(sz));
        if (in.gcount() != static_cast<std::streamsize>(sz)) {
            outBytes.clear();
            outFailReason = ShaderRejectReason::IoReadError;
            return false;
        }
        return true;
    } catch (...) {
        outBytes.clear();
        outFailReason = ShaderRejectReason::IoReadError;
        return false;
    }
}

uint64_t ComputeCombinedHashLocked() noexcept {
    uint64_t combined = 0xCBF29CE484222325ull;
    for (const auto& kv : g_shr.tracked) {
        combined ^= kv.second.lastValidHash;
        combined *= 0x100000001B3ull;
    }
    return combined;
}

bool ScanDirectoryLocked(bool isInitialBaseline, bool forceReloadAll) {
    ++g_shr.state.scanCount;
    g_shr.state.watcherActive = g_shr.initialized ? 1u : 0u;

    std::error_code ec;
    const std::filesystem::path root(g_shr.shadersDir);
    if (g_shr.shadersDir.empty() || !std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) {
        g_shr.state.dirExists        = 0u;
        g_shr.state.trackedFileCount = static_cast<uint32_t>(g_shr.tracked.size());
        return false;
    }
    g_shr.state.dirExists = 1u;

    std::vector<std::filesystem::path> files;
    try {
        for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
            if (ec) break;
            if (!entry.is_regular_file(ec) || ec) continue;
            if (!IsShaderExtension(entry.path())) continue;
            files.push_back(entry.path());
        }
    } catch (...) {
        return false;
    }
    std::sort(files.begin(), files.end());

    uint32_t newlyAccepted = 0u;
    std::vector<std::string> batchDirty;

    for (const auto& filePath : files) {
        const std::string relName = filePath.filename().string();
        const auto ftime = std::filesystem::last_write_time(filePath, ec);
        if (ec) continue;
        const int64_t writeTicks = static_cast<int64_t>(ftime.time_since_epoch().count());
        const uint64_t fileSize  = std::filesystem::file_size(filePath, ec);
        if (ec) continue;

        auto it = g_shr.tracked.find(relName);
        const bool isNew = (it == g_shr.tracked.end());
        const bool statChanged = isNew ||
                                 it->second.writeTimeTicks != writeTicks ||
                                 it->second.fileSize != fileSize;

        if (!isInitialBaseline && !forceReloadAll && !statChanged) {
            continue;
        }

        std::vector<uint8_t> bytes;
        ShaderRejectReason readFail = ShaderRejectReason::Ok;
        if (!ReadFileBounded(filePath, bytes, readFail)) {
            if (!isInitialBaseline) {
                ++g_shr.state.rejectedEdits;
                g_shr.state.lastRejectReason = static_cast<uint8_t>(readFail);
                CopyStrTrunc(g_shr.state.lastRejectedShader, sizeof(g_shr.state.lastRejectedShader), relName);
            }
            continue;
        }

        const uint64_t rawHash = ComputeShaderFnv1a64(bytes.data(), bytes.size());

        // If mtime changed (e.g., editor saved identical bytes), suppress spurious reload unless forced.
        if (!isInitialBaseline && !forceReloadAll && !isNew && rawHash == it->second.lastSeenHash) {
            it->second.writeTimeTicks = writeTicks;
            it->second.fileSize       = fileSize;
            continue;
        }

        const ShaderPreValidationResult val = ValidateShaderSourceBytes(bytes.data(), bytes.size(), relName.c_str());
        if (!val.valid) {
            // Fail-closed: record rejection without advancing reloadGeneration or corrupting lastValidHash!
            ++g_shr.state.rejectedEdits;
            g_shr.state.lastRejectReason = static_cast<uint8_t>(val.reason);
            CopyStrTrunc(g_shr.state.lastRejectedShader, sizeof(g_shr.state.lastRejectedShader), relName);
            if (!isNew) {
                it->second.writeTimeTicks = writeTicks;
                it->second.fileSize       = fileSize;
                it->second.lastSeenHash   = rawHash;
            }
            continue;
        }

        if (isInitialBaseline) {
            g_shr.tracked[relName] = TrackedShaderEntry{writeTicks, fileSize, val.fnv1a64, val.fnv1a64};
        } else {
            const bool contentActuallyChanged = isNew || (val.fnv1a64 != it->second.lastValidHash);
            g_shr.tracked[relName] = TrackedShaderEntry{writeTicks, fileSize, val.fnv1a64, val.fnv1a64};
            if (contentActuallyChanged || forceReloadAll) {
                ++newlyAccepted;
                batchDirty.push_back(relName);
            }
        }
    }

    g_shr.state.trackedFileCount  = static_cast<uint32_t>(g_shr.tracked.size());
    g_shr.state.combinedHashFnv1a = ComputeCombinedHashLocked();

    if (!isInitialBaseline && newlyAccepted > 0u) {
        ++g_shr.state.reloadGeneration;
        g_shr.state.acceptedReloads += newlyAccepted;
        g_shr.dirtyShaders           = batchDirty;
        g_shr.state.dirtyFileCount   = static_cast<uint32_t>(g_shr.dirtyShaders.size());
        CopyStrTrunc(g_shr.state.lastDirtyShader, sizeof(g_shr.state.lastDirtyShader), batchDirty.back());
        return true;
    }
    return false;
}

}  // namespace

const char* ShaderRejectReasonName(ShaderRejectReason reason) noexcept {
    switch (reason) {
        case ShaderRejectReason::Ok:                          return "Ok";
        case ShaderRejectReason::EmptyFile:                   return "EmptyFile";
        case ShaderRejectReason::ExceedsMaxSize:              return "ExceedsMaxSize";
        case ShaderRejectReason::ContainsNulByte:             return "ContainsNulByte";
        case ShaderRejectReason::UnbalancedBraces:            return "UnbalancedBraces";
        case ShaderRejectReason::MalformedInclude:            return "MalformedInclude";
        case ShaderRejectReason::UnterminatedCommentOrString: return "UnterminatedCommentOrString";
        case ShaderRejectReason::IoReadError:                 return "IoReadError";
        default:                                              return "Unknown";
    }
}

uint64_t ComputeShaderFnv1a64(const uint8_t* data, size_t size) noexcept {
    uint64_t h = 0xCBF29CE484222325ull;
    if (!data || size == 0u) return h;
    for (size_t i = 0u; i < size; ++i) {
        h ^= static_cast<uint64_t>(data[i]);
        h *= 0x100000001B3ull;
    }
    return h;
}

ShaderPreValidationResult ValidateShaderSourceBytes(const uint8_t* data,
                                                    size_t size,
                                                    const char* fileName) noexcept {
    ShaderPreValidationResult res{};
    const char* label = (fileName && fileName[0] != '\0') ? fileName : "<shader>";

    if (!data || size == 0u) {
        res.reason = ShaderRejectReason::EmptyFile;
        std::snprintf(res.detail, sizeof(res.detail), "%s: empty shader source", label);
        return res;
    }
    if (size > kMaxShaderSourceBytes) {
        res.reason    = ShaderRejectReason::ExceedsMaxSize;
        res.sizeBytes = static_cast<uint32_t>(std::min<size_t>(size, 0xFFFFFFFFu));
        std::snprintf(res.detail, sizeof(res.detail), "%s: exceeds max size (%zu > %zu bytes)",
                      label, size, kMaxShaderSourceBytes);
        return res;
    }

    res.sizeBytes = static_cast<uint32_t>(size);
    res.fnv1a64   = ComputeShaderFnv1a64(data, size);

    bool inLineComment  = false;
    bool inBlockComment = false;
    bool inString       = false;
    bool atLineStart    = true;
    int32_t braceDepth  = 0;

    for (size_t i = 0u; i < size; ++i) {
        const uint8_t b = data[i];
        if (b == 0u) {
            res.reason = ShaderRejectReason::ContainsNulByte;
            std::snprintf(res.detail, sizeof(res.detail), "%s: embedded NUL byte at offset %zu", label, i);
            return res;
        }
        const char c    = static_cast<char>(b);
        const char next = (i + 1u < size) ? static_cast<char>(data[i + 1u]) : '\0';

        if (inLineComment) {
            if (c == '\n') {
                inLineComment = false;
                atLineStart   = true;
            }
            continue;
        }
        if (inBlockComment) {
            if (c == '*' && next == '/') {
                inBlockComment = false;
                ++i;
            } else if (c == '\n') {
                atLineStart = true;
            }
            continue;
        }
        if (inString) {
            if (c == '\\' && i + 1u < size) {
                ++i; // skip escaped char
                continue;
            }
            if (c == '"') {
                inString = false;
            } else if (c == '\n') {
                res.reason = ShaderRejectReason::UnterminatedCommentOrString;
                std::snprintf(res.detail, sizeof(res.detail), "%s: unterminated string literal at offset %zu", label, i);
                return res;
            }
            continue;
        }

        if (c == '/' && next == '/') {
            inLineComment = true;
            ++i;
            continue;
        }
        if (c == '/' && next == '*') {
            inBlockComment = true;
            ++i;
            continue;
        }
        if (c == '"') {
            inString    = true;
            atLineStart = false;
            continue;
        }

        // Check #include directives at line start for syntax & path-traversal safety
        if (atLineStart && c == '#') {
            size_t j = i + 1u;
            while (j < size && (data[j] == ' ' || data[j] == '\t')) ++j;
            constexpr const char kInc[] = "include";
            constexpr size_t kIncLen    = 7u;
            if (j + kIncLen <= size && std::memcmp(data + j, kInc, kIncLen) == 0 &&
                (j + kIncLen == size || std::isspace(data[j + kIncLen]) != 0 ||
                 data[j + kIncLen] == '"' || data[j + kIncLen] == '<')) {
                j += kIncLen;
                while (j < size && (data[j] == ' ' || data[j] == '\t')) ++j;
                if (j >= size || (data[j] != '"' && data[j] != '<')) {
                    res.reason = ShaderRejectReason::MalformedInclude;
                    std::snprintf(res.detail, sizeof(res.detail), "%s: malformed #include directive", label);
                    return res;
                }
                const char closeDelim = (data[j] == '"') ? '"' : '>';
                ++j;
                const size_t pathStart = j;
                bool foundClose = false;
                bool hasBadPath = false;
                while (j < size && data[j] != '\n' && data[j] != '\r') {
                    if (data[j] == 0u) {
                        res.reason = ShaderRejectReason::ContainsNulByte;
                        std::snprintf(res.detail, sizeof(res.detail), "%s: embedded NUL byte in #include", label);
                        return res;
                    }
                    if (static_cast<char>(data[j]) == closeDelim) {
                        foundClose = true;
                        break;
                    }
                    if (data[j] == '.' && j + 1u < size && data[j + 1u] == '.') {
                        hasBadPath = true;
                    }
                    ++j;
                }
                const size_t pathLen = j - pathStart;
                if (!foundClose || pathLen == 0u || hasBadPath) {
                    res.reason = ShaderRejectReason::MalformedInclude;
                    std::snprintf(res.detail, sizeof(res.detail), "%s: invalid #include target path", label);
                    return res;
                }
                ++res.includeCount;
                i = j; // advance past closing delimiter
                atLineStart = false;
                continue;
            }
        }

        if (c == '{') {
            ++braceDepth;
            atLineStart = false;
        } else if (c == '}') {
            --braceDepth;
            atLineStart = false;
            if (braceDepth < 0) {
                res.reason = ShaderRejectReason::UnbalancedBraces;
                std::snprintf(res.detail, sizeof(res.detail), "%s: unexpected closing '}' at offset %zu", label, i);
                return res;
            }
        } else if (c == '\n') {
            atLineStart = true;
        } else if (c != ' ' && c != '\t' && c != '\r') {
            atLineStart = false;
        }
    }

    if (inBlockComment || inString) {
        res.reason = ShaderRejectReason::UnterminatedCommentOrString;
        std::snprintf(res.detail, sizeof(res.detail), "%s: unterminated block comment or string at EOF", label);
        return res;
    }
    if (braceDepth != 0) {
        res.reason = ShaderRejectReason::UnbalancedBraces;
        std::snprintf(res.detail, sizeof(res.detail), "%s: unbalanced braces (depth=%d at EOF)", label, braceDepth);
        return res;
    }

    res.valid  = true;
    res.reason = ShaderRejectReason::Ok;
    std::snprintf(res.detail, sizeof(res.detail), "%s: ok (%u bytes, %u includes, fnv1a=0x%016llX)",
                  label, res.sizeBytes, res.includeCount, static_cast<unsigned long long>(res.fnv1a64));
    return res;
}

std::string ShaderHotReloadInit(const std::string& shadersDir) {
    RawShaderHotReloadState snap{};
    {
        std::lock_guard<std::mutex> lk(g_shr.mu);
        g_shr.initialized = true;
        g_shr.shadersDir  = shadersDir;
        g_shr.presentTick = 0u;
        g_shr.tracked.clear();
        g_shr.dirtyShaders.clear();
        g_shr.state = {};
        g_shr.state.watcherActive = 1u;
        (void)ScanDirectoryLocked(/*isInitialBaseline=*/true, /*forceReloadAll=*/false);
        snap = g_shr.state;
    }
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "[ShaderHotReload] initialized (dir='%s' exists=%u tracked=%u gen=%u poll=%uf)",
                  shadersDir.c_str(), static_cast<unsigned>(snap.dirExists),
                  snap.trackedFileCount, snap.reloadGeneration, kWatchCadenceFrames);
    return buf;
}

void ShaderHotReloadOnPresent() {
    std::lock_guard<std::mutex> lk(g_shr.mu);
    if (!g_shr.initialized) return;
    ++g_shr.presentTick;
    if ((g_shr.presentTick % kWatchCadenceFrames) != 0u) return;
    (void)ScanDirectoryLocked(/*isInitialBaseline=*/false, /*forceReloadAll=*/false);
}

bool ShaderHotReloadPollScan(bool forceReloadAll) {
    std::lock_guard<std::mutex> lk(g_shr.mu);
    if (!g_shr.initialized) return false;
    return ScanDirectoryLocked(/*isInitialBaseline=*/false, forceReloadAll);
}

bool ShaderHotReloadTriggerNow(std::string* outSummary) {
    bool triggered = false;
    RawShaderHotReloadState snap{};
    {
        std::lock_guard<std::mutex> lk(g_shr.mu);
        if (g_shr.initialized) {
            triggered = ScanDirectoryLocked(/*isInitialBaseline=*/false, /*forceReloadAll=*/true);
        }
        snap = g_shr.state;
    }
    if (outSummary) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
                      "[ShaderHotReload] triggered=%u gen=%u dirExists=%u tracked=%u dirty=%u accepted=%u rejected=%u lastDirty='%s'",
                      triggered ? 1u : 0u, snap.reloadGeneration, static_cast<unsigned>(snap.dirExists),
                      snap.trackedFileCount, snap.dirtyFileCount, snap.acceptedReloads, snap.rejectedEdits,
                      snap.lastDirtyShader[0] ? snap.lastDirtyShader : "none");
        *outSummary = buf;
    }
    return triggered;
}

std::vector<std::string> ShaderHotReloadGetDirtyList() {
    std::lock_guard<std::mutex> lk(g_shr.mu);
    return g_shr.dirtyShaders;
}

void ShaderHotReloadShutdown() {
    std::lock_guard<std::mutex> lk(g_shr.mu);
    g_shr.initialized = false;
    g_shr.shadersDir.clear();
    g_shr.presentTick = 0u;
    g_shr.tracked.clear();
    g_shr.dirtyShaders.clear();
    g_shr.state.watcherActive = 0u;
}

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetShaderHotReloadState(raw::rt::RawShaderHotReloadState* out) {
    if (!out) return 0;
    std::lock_guard<std::mutex> lk(raw::rt::g_shr.mu);
    *out = raw::rt::g_shr.state;
    return raw::rt::g_shr.initialized ? 1 : 0;
}
