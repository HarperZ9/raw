#include "SB_ShaderDebug.h"
#include "ShaderCache.h"

// RAW is always built as an SKSE plugin
#define HAS_SKSE 1

// For SKSE logging — falls back to OutputDebugString if unavailable
#ifdef HAS_SKSE
#include <SKSE/SKSE.h>
#define SB_LOG_INFO(...)  SKSE::log::info(__VA_ARGS__)
#define SB_LOG_ERROR(...) SKSE::log::error(__VA_ARGS__)
#else
#include <cstdio>
#define SB_LOG_INFO(fmt, ...)  do { char _b[1024]; snprintf(_b, sizeof(_b), fmt, ##__VA_ARGS__); OutputDebugStringA(_b); } while(0)
#define SB_LOG_ERROR(fmt, ...) do { char _b[1024]; snprintf(_b, sizeof(_b), fmt, ##__VA_ARGS__); OutputDebugStringA(_b); } while(0)
#endif

#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>
#include <ctime>
#include <iomanip>
#include <Psapi.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "Psapi.lib")

namespace SB::Debug
{

// ═══════════════════════════════════════════════════════════════════════════
//  Static original function pointers
// ═══════════════════════════════════════════════════════════════════════════

ShaderDebug::fnD3DCompile  ShaderDebug::s_origD3DCompile  = nullptr;
ShaderDebug::fnD3DCompile2 ShaderDebug::s_origD3DCompile2 = nullptr;


// ═══════════════════════════════════════════════════════════════════════════
//  IAT patching — must be defined before Install() which calls them
// ═══════════════════════════════════════════════════════════════════════════

// Check if a memory range is readable (avoids crashes on bad PE data)
static bool IsMemoryReadable(const void* addr, SIZE_T size)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(addr, &mbi, sizeof(mbi)) == 0)
        return false;
    if (mbi.State != MEM_COMMIT)
        return false;
    constexpr DWORD readableFlags = PAGE_READONLY | PAGE_READWRITE
        | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE
        | PAGE_EXECUTE_WRITECOPY | PAGE_WRITECOPY;
    return (mbi.Protect & readableFlags) != 0;
}

// Patch a single module's IAT for d3dcompiler_47.dll imports.
// Wrapped in SEH to survive modules with non-standard PE layouts
// (e.g. MO2's usvfs_x64.dll virtual filesystem hook).
template<typename FnPtr>
static void PatchModuleIAT(BYTE* base, FnPtr target, FnPtr hook, FnPtr& original)
{
    __try
    {
        auto* dosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) return;

        // Sanity check e_lfanew
        if (dosHeader->e_lfanew < 0 || dosHeader->e_lfanew > 0x10000000) return;

        auto* ntHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dosHeader->e_lfanew);
        if (!IsMemoryReadable(ntHeaders, sizeof(IMAGE_NT_HEADERS))) return;
        if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) return;

        auto& importDir = ntHeaders->OptionalHeader
            .DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (importDir.Size == 0 || importDir.VirtualAddress == 0) return;

        auto* importDesc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
            base + importDir.VirtualAddress);
        if (!IsMemoryReadable(importDesc, sizeof(IMAGE_IMPORT_DESCRIPTOR))) return;

        for (; importDesc->Name != 0; importDesc++)
        {
            if (!IsMemoryReadable(importDesc, sizeof(IMAGE_IMPORT_DESCRIPTOR))) break;

            const char* dllName = reinterpret_cast<const char*>(
                base + importDesc->Name);
            if (!IsMemoryReadable(dllName, 22)) continue; // strlen("d3dcompiler_47.dll")+1

            if (_stricmp(dllName, "d3dcompiler_47.dll") != 0)
                continue;

            auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(
                base + importDesc->FirstThunk);
            if (!IsMemoryReadable(thunk, sizeof(IMAGE_THUNK_DATA))) continue;

            for (; thunk->u1.Function != 0; thunk++)
            {
                auto* funcPtr = reinterpret_cast<FnPtr*>(&thunk->u1.Function);
                if (*funcPtr == target)
                {
                    DWORD oldProt;
                    VirtualProtect(funcPtr, sizeof(FnPtr),
                                   PAGE_EXECUTE_READWRITE, &oldProt);
                    *funcPtr = hook;
                    VirtualProtect(funcPtr, sizeof(FnPtr), oldProt, &oldProt);
                }
            }
            return; // Found the d3dcompiler import — no need to keep scanning
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        // Module has a corrupt or non-standard PE layout — skip it silently
    }
}

// IAT patching: scan all loaded modules for imports of the target function
// and redirect them to our hook.
template<typename FnPtr>
static void PatchAllIATEntries(FnPtr target, FnPtr hook, FnPtr& original)
{
    original = target; // Save original before any patching

    HANDLE hProcess = GetCurrentProcess();
    HMODULE hMods[512];
    DWORD cbNeeded;

    if (!EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded))
        return;

    DWORD numModules = cbNeeded / sizeof(HMODULE);
    if (numModules > static_cast<DWORD>(std::size(hMods)))
        numModules = static_cast<DWORD>(std::size(hMods));

    for (DWORD i = 0; i < numModules; i++)
    {
        BYTE* base = reinterpret_cast<BYTE*>(hMods[i]);
        if (!base) continue;
        PatchModuleIAT(base, target, hook, original);
    }
}

// NOTE: GetProcAddress IAT hook removed — some callers statically link
// their own D3D shader compiler and never call the system d3dcompiler_47.dll.
// IAT patching of D3DCompile (above) still catches our own ImGui/compute
// compilations.


// ═══════════════════════════════════════════════════════════════════════════
//  Install / Shutdown
// ═══════════════════════════════════════════════════════════════════════════

// ── Phase 1: Early hook installation ──────────────────────────────────
// Called from SKSEPlugin_Load — before any shaders are compiled.
// No D3D11 device needed. Just IAT hooks + file paths.

void ShaderDebug::InstallHooksEarly()
{
    if (m_hooksInstalled) return;

    // ── Set up log/capture file paths ────────────────────────────────
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    auto basePath = std::filesystem::path(exePath).parent_path()
                    / "Data" / "SKSE" / "Plugins";
    m_logPath = basePath / "RAW_ShaderErrors.log";
    EnsureLogDirectory();

    if (m_capturePath.empty())
        m_capturePath = basePath / "RAW" / "ShaderCapture";

    // ── Hook D3DCompile from d3dcompiler_47.dll ──────────────────────
    //
    // Two-layer strategy:
    //   1) IAT patching: catches callers that statically import D3DCompile
    //   2) GetProcAddress hook: catches callers that resolve dynamically
    //
    // No inline/trampoline hooks — those require prologue relocation which
    // is fragile across different d3dcompiler_47.dll builds.

    HMODULE hCompiler = GetModuleHandleA("d3dcompiler_47.dll");
    if (!hCompiler)
    {
        // Pre-load it so we can resolve the real function pointers now.
        hCompiler = LoadLibraryA("d3dcompiler_47.dll");
    }

    if (hCompiler)
    {
        auto pD3DCompile = reinterpret_cast<fnD3DCompile>(
            GetProcAddress(hCompiler, "D3DCompile"));
        auto pD3DCompile2 = reinterpret_cast<fnD3DCompile2>(
            GetProcAddress(hCompiler, "D3DCompile2"));

        // Save the real function pointers FIRST
        if (pD3DCompile)
            s_origD3DCompile = pD3DCompile;
        if (pD3DCompile2)
            s_origD3DCompile2 = pD3DCompile2;

        // IAT patch D3DCompile across all currently loaded modules.
        // This catches our own ImGui/compute compilations and anything
        // else that statically imports d3dcompiler_47.dll.
        if (pD3DCompile)
        {
            PatchAllIATEntries(pD3DCompile, HookD3DCompile, s_origD3DCompile);
            s_origD3DCompile = pD3DCompile;  // restore after IAT patching
            SB_LOG_INFO("ShaderDebug: IAT-patched D3DCompile at {:p}",
                        reinterpret_cast<void*>(pD3DCompile));
        }

        if (pD3DCompile2)
        {
            PatchAllIATEntries(pD3DCompile2, HookD3DCompile2, s_origD3DCompile2);
            s_origD3DCompile2 = pD3DCompile2;
            SB_LOG_INFO("ShaderDebug: IAT-patched D3DCompile2 at {:p}",
                        reinterpret_cast<void*>(pD3DCompile2));
        }
    }
    else
    {
        SB_LOG_ERROR("ShaderDebug: d3dcompiler_47.dll not found — "
                     "shader error capture unavailable");
    }

    WriteLogHeader();

    m_hooksInstalled = true;
    SB_LOG_INFO("ShaderDebug: IAT hooks installed early — "
                "captures all shader compilations");
}


// ── Phase 2: D3D11 resource setup ────────────────────────────────────
// Called at kDataLoaded after D3D11 device is available.

void ShaderDebug::Install(ID3D11Device* device, ID3D11DeviceContext* context,
                          IDXGISwapChain* swapChain)
{
    if (m_installed) return;

    m_device    = device;
    m_context   = context;
    m_swapChain = swapChain;

    // If hooks weren't installed early, do it now (fallback)
    if (!m_hooksInstalled)
        InstallHooksEarly();

    m_installed = true;
    SB_LOG_INFO("ShaderDebug: fully installed — overlay + capture ready");
    SB_LOG_INFO("ShaderDebug: log file: {}", m_logPath.string());
    SB_LOG_INFO("ShaderDebug: captured {} compilations during early init",
                m_attempts.size());
    SB_LOG_INFO("ShaderDebug: press F10 to toggle overlay, F11 to clear");
}

void ShaderDebug::Shutdown()
{
    if (!m_installed) return;
    FlushLog();
    m_installed = false;
    SB_LOG_INFO("ShaderDebug: shutdown");
}


// ═══════════════════════════════════════════════════════════════════════════
//  D3DCompile Hooks — the core interception
// ═══════════════════════════════════════════════════════════════════════════

HRESULT WINAPI ShaderDebug::HookD3DCompile(
    LPCVOID pSrcData, SIZE_T SrcDataSize,
    LPCSTR pSourceName, const D3D_SHADER_MACRO* pDefines,
    ID3DInclude* pInclude, LPCSTR pEntrypoint,
    LPCSTR pTarget, UINT Flags1, UINT Flags2,
    ID3DBlob** ppCode, ID3DBlob** ppErrorMsgs)
{
    auto& self = Get();

    // ── Pre-process: parse annotations & transform source ─────────────
    // Guard: null/empty source passes straight through to real D3DCompile
    if (!pSrcData || SrcDataSize == 0) {
        return s_origD3DCompile(pSrcData, SrcDataSize, pSourceName, pDefines, pInclude,
                                pEntrypoint, pTarget, Flags1, Flags2, ppCode, ppErrorMsgs);
    }

    std::string sourceName = pSourceName ? pSourceName : "";
    std::string rawSource(reinterpret_cast<const char*>(pSrcData), SrcDataSize);

    // ShaderPreProcessor removed — compile raw source directly
    const char* compileSource = rawSource.c_str();
    SIZE_T compileSize = rawSource.size();

    // Use defines as-is (no pre-processor)
    const D3D_SHADER_MACRO* finalDefines = pDefines;

    // Cache check — skip real compilation if we have a cached blob
    auto& cache = SB::ShaderCache::Get();
    if (cache.IsEnabled() && ppCode) {
        if (cache.TryGetCached(compileSource, compileSize, finalDefines,
                               pEntrypoint, pTarget, Flags1, Flags2, ppCode))
        {
            if (ppErrorMsgs) *ppErrorMsgs = nullptr;
            return S_OK;
        }
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    // Call the real D3DCompile with cleaned source
    HRESULT hr = s_origD3DCompile(
        compileSource, compileSize, pSourceName, finalDefines, pInclude,
        pEntrypoint, pTarget, Flags1, Flags2, ppCode, ppErrorMsgs);

    auto endTime = std::chrono::high_resolution_clock::now();
    double elapsedMs = std::chrono::duration<double, std::milli>(
        endTime - startTime).count();

    // Record the attempt regardless of success/failure
    self.RecordCompilation(hr, compileSource, compileSize,
                           pSourceName, pEntrypoint, pTarget,
                           ppErrorMsgs, elapsedMs);

    // Cache store — save successful compilations (hash cleaned source)
    if (cache.IsEnabled() && SUCCEEDED(hr) && ppCode && *ppCode) {
        cache.StoreCached(compileSource, compileSize, finalDefines,
                          pEntrypoint, pTarget, Flags1, Flags2, *ppCode);
    }

    // Capture shader source + DXBC to disk (if enabled)
    self.CaptureShaderToDisk(pSourceName, pEntrypoint, pTarget,
                             compileSource, compileSize,
                             (ppCode && *ppCode) ? *ppCode : nullptr);

    return hr;
}

HRESULT WINAPI ShaderDebug::HookD3DCompile2(
    LPCVOID pSrcData, SIZE_T SrcDataSize,
    LPCSTR pSourceName, const D3D_SHADER_MACRO* pDefines,
    ID3DInclude* pInclude, LPCSTR pEntrypoint,
    LPCSTR pTarget, UINT Flags1, UINT Flags2,
    UINT SecondaryDataFlags, LPCVOID pSecondaryData,
    SIZE_T SecondaryDataSize,
    ID3DBlob** ppCode, ID3DBlob** ppErrorMsgs)
{
    auto& self = Get();

    // Guard: null/empty source passes straight through
    if (!pSrcData || SrcDataSize == 0) {
        return s_origD3DCompile2(pSrcData, SrcDataSize, pSourceName, pDefines, pInclude,
                                 pEntrypoint, pTarget, Flags1, Flags2,
                                 SecondaryDataFlags, pSecondaryData, SecondaryDataSize,
                                 ppCode, ppErrorMsgs);
    }

    // ── Pre-process: parse annotations & transform source ─────────────
    std::string sourceName = pSourceName ? pSourceName : "";
    std::string rawSource(reinterpret_cast<const char*>(pSrcData), SrcDataSize);

    // ShaderPreProcessor removed — compile raw source directly
    const char* compileSource = rawSource.c_str();
    SIZE_T compileSize = rawSource.size();

    const D3D_SHADER_MACRO* finalDefines = pDefines;

    // Cache check — skip real compilation if we have a cached blob
    auto& cache = SB::ShaderCache::Get();
    if (cache.IsEnabled() && ppCode) {
        if (cache.TryGetCached(compileSource, compileSize, finalDefines,
                               pEntrypoint, pTarget, Flags1, Flags2, ppCode))
        {
            if (ppErrorMsgs) *ppErrorMsgs = nullptr;
            return S_OK;
        }
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    HRESULT hr = s_origD3DCompile2(
        compileSource, compileSize, pSourceName, finalDefines, pInclude,
        pEntrypoint, pTarget, Flags1, Flags2,
        SecondaryDataFlags, pSecondaryData, SecondaryDataSize,
        ppCode, ppErrorMsgs);

    auto endTime = std::chrono::high_resolution_clock::now();
    double elapsedMs = std::chrono::duration<double, std::milli>(
        endTime - startTime).count();

    self.RecordCompilation(hr, compileSource, compileSize,
                           pSourceName, pEntrypoint, pTarget,
                           ppErrorMsgs, elapsedMs);

    // Cache store — save successful compilations (hash cleaned source)
    if (cache.IsEnabled() && SUCCEEDED(hr) && ppCode && *ppCode) {
        cache.StoreCached(compileSource, compileSize, finalDefines,
                          pEntrypoint, pTarget, Flags1, Flags2, *ppCode);
    }

    // Capture shader source + DXBC to disk (if enabled)
    self.CaptureShaderToDisk(pSourceName, pEntrypoint, pTarget,
                             compileSource, compileSize,
                             (ppCode && *ppCode) ? *ppCode : nullptr);

    return hr;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Record a compilation attempt
// ═══════════════════════════════════════════════════════════════════════════

void ShaderDebug::RecordCompilation(
    HRESULT hr, LPCVOID pSrcData, SIZE_T srcSize,
    LPCSTR pSourceName, LPCSTR pEntrypoint, LPCSTR pTarget,
    ID3DBlob** ppErrorMsgs, double elapsedMs)
{
    CompilationAttempt attempt;
    attempt.sourceFile  = pSourceName ? pSourceName : "<memory>";
    attempt.entryPoint  = pEntrypoint ? pEntrypoint : "<unknown>";
    attempt.profile     = pTarget ? pTarget : "??";
    attempt.succeeded   = SUCCEEDED(hr);
    attempt.timestamp   = std::chrono::system_clock::now();
    attempt.compileTimeMs = elapsedMs;

    // Extract error blob text
    if (ppErrorMsgs && *ppErrorMsgs)
    {
        ID3DBlob* blob = *ppErrorMsgs;
        attempt.rawErrorBlob = std::string(
            reinterpret_cast<const char*>(blob->GetBufferPointer()),
            blob->GetBufferSize());

        // Trim trailing nulls
        while (!attempt.rawErrorBlob.empty() &&
               attempt.rawErrorBlob.back() == '\0')
            attempt.rawErrorBlob.pop_back();
    }

    // Parse errors from blob
    if (!attempt.rawErrorBlob.empty())
    {
        ParseErrorBlob(attempt.rawErrorBlob, attempt.sourceFile,
                       attempt.entryPoint, attempt.profile, attempt);
    }

    // Extract source snippets for each error
    if (m_config.showSourceSnippets && pSrcData && srcSize > 0)
    {
        std::string source(reinterpret_cast<const char*>(pSrcData), srcSize);

        // Cache the source for this file
        {
            std::lock_guard<std::mutex> lk(m_sourceCacheMtx);
            m_sourceCache[attempt.sourceFile] = source;
        }

        for (auto& err : attempt.errors)
        {
            if (err.line > 0)
            {
                attempt.snippets.push_back(
                    ExtractSnippet(source, err.line, m_config.snippetRadius));
            }
        }
    }

    // Update counters
    std::lock_guard<std::mutex> lk(m_errorMtx);

    if (attempt.succeeded)
    {
        m_totalSuccess++;
    }
    else
    {
        m_totalErrors += attempt.errors.size();
        m_totalWarnings += attempt.warnings.size();

        // Auto-show overlay on first error
        if (m_config.autoShow && !m_overlayVisible)
            m_overlayVisible = true;

        // Log to SKSE
        SB_LOG_ERROR("ShaderDebug: COMPILATION FAILED — {} [{}] entry={}",
                     attempt.sourceFile, attempt.profile, attempt.entryPoint);
        for (auto& err : attempt.errors)
        {
            SB_LOG_ERROR("  {}({},{}): {} — {}",
                         err.filename, err.line, err.column,
                         err.errorCode, err.message);
        }
    }

    // Write to log file
    if (m_config.persistLog && !attempt.succeeded)
        WriteLogEntry(attempt);

    // Store attempt (cap at maxErrors)
    m_attempts.push_back(std::move(attempt));
    if (static_cast<int>(m_attempts.size()) > m_config.maxErrors)
    {
        m_attempts.erase(m_attempts.begin());
    }
}


// ═══════════════════════════════════════════════════════════════════════════
//  Shader source capture to disk
// ═══════════════════════════════════════════════════════════════════════════

void ShaderDebug::CaptureShaderToDisk(
    LPCSTR pSourceName, LPCSTR pEntrypoint, LPCSTR pTarget,
    LPCVOID pSrcData, SIZE_T srcSize, ID3DBlob* pCode)
{
    if (!m_captureEnabled || m_capturePath.empty())
        return;
    if (!pSrcData || srcSize == 0)
        return;

    // FNV-1a hash for dedup + filename
    uint64_t hash = 0xcbf29ce484222325ULL;
    auto* bytes = static_cast<const uint8_t*>(pSrcData);
    for (SIZE_T i = 0; i < srcSize; ++i) {
        hash ^= bytes[i];
        hash *= 0x100000001b3ULL;
    }
    // Mix in entry point
    if (pEntrypoint) {
        for (auto* p = pEntrypoint; *p; ++p) {
            hash ^= static_cast<uint8_t>(*p);
            hash *= 0x100000001b3ULL;
        }
    }

    // Skip if already captured
    if (m_capturedHashes.count(hash))
        return;
    m_capturedHashes.insert(hash);

    // Build filename: sourceName_entryPoint_hash
    std::string baseName;
    if (pSourceName) {
        baseName = pSourceName;
        // Strip path separators
        auto slash = baseName.find_last_of("\\/");
        if (slash != std::string::npos)
            baseName = baseName.substr(slash + 1);
        // Strip extension
        auto dot = baseName.find_last_of('.');
        if (dot != std::string::npos)
            baseName = baseName.substr(0, dot);
    } else {
        baseName = "memory";
    }
    if (pEntrypoint)
        baseName += std::string("_") + pEntrypoint;

    char hashStr[20];
    snprintf(hashStr, sizeof(hashStr), "_%016llX", static_cast<unsigned long long>(hash));
    baseName += hashStr;

    // Ensure capture directory exists
    std::error_code ec;
    std::filesystem::create_directories(m_capturePath, ec);

    // Save HLSL source
    {
        auto hlslPath = m_capturePath / (baseName + ".hlsl");
        std::ofstream ofs(hlslPath, std::ios::binary);
        if (ofs.is_open()) {
            // Write header comment
            ofs << "// Captured shader source\n";
            ofs << "// Source: " << (pSourceName ? pSourceName : "<memory>") << "\n";
            ofs << "// Entry:  " << (pEntrypoint ? pEntrypoint : "?") << "\n";
            ofs << "// Target: " << (pTarget ? pTarget : "?") << "\n";
            ofs << "// Size:   " << srcSize << " bytes\n";
            ofs << "// Hash:   0x" << hashStr + 1 << "\n";  // skip leading underscore
            ofs << "//\n\n";
            ofs.write(static_cast<const char*>(pSrcData), srcSize);
            ofs.close();
        }
    }

    // Save compiled DXBC if available
    if (pCode && pCode->GetBufferSize() > 0) {
        auto dxbcPath = m_capturePath / (baseName + ".dxbc");
        std::ofstream ofs(dxbcPath, std::ios::binary);
        if (ofs.is_open()) {
            ofs.write(static_cast<const char*>(pCode->GetBufferPointer()),
                      pCode->GetBufferSize());
            ofs.close();
        }
    }

    ++m_capturedCount;
    SB_LOG_INFO("ShaderCapture: saved '{}' ({} bytes HLSL{})",
        baseName, srcSize, pCode ? " + DXBC" : "");
}


// ═══════════════════════════════════════════════════════════════════════════
//  Error blob parsing
//
//  D3DCompile error blobs contain one or more lines in these formats:
//
//    filename(line,col-col): error XNNNN: message text
//    filename(line,col):     error XNNNN: message text
//    (line,col-col):         error XNNNN: message text
//    error XNNNN:            message text
//    warning X3206:          message text
//
//  Multiple errors are separated by newlines.
//  Some errors span multiple lines (continuation lines lack the prefix).
// ═══════════════════════════════════════════════════════════════════════════

void ShaderDebug::ParseErrorBlob(
    const std::string& blobText,
    const std::string& sourceName,
    const std::string& entryPoint,
    const std::string& profile,
    CompilationAttempt& outAttempt)
{
    std::istringstream stream(blobText);
    std::string line;
    ShaderError* currentError = nullptr;

    while (std::getline(stream, line))
    {
        // Skip empty lines
        if (line.empty() || line.find_first_not_of(" \t\r\n") == std::string::npos)
            continue;

        // Check if this is a new error/warning line (has "error" or "warning" keyword)
        bool isNewEntry = (line.find("error") != std::string::npos ||
                           line.find("warning") != std::string::npos) &&
                          (line.find(':') != std::string::npos);

        if (isNewEntry)
        {
            ShaderError err = ParseSingleError(line, sourceName);
            err.shaderProfile = profile;
            err.entryPoint    = entryPoint;
            err.sourceHint    = InferSourceFile(sourceName, entryPoint);
            err.rawBlobText   = line;

            if (err.severity == Severity::Warning)
            {
                outAttempt.warnings.push_back(std::move(err));
                currentError = &outAttempt.warnings.back();
            }
            else
            {
                outAttempt.errors.push_back(std::move(err));
                currentError = &outAttempt.errors.back();
            }
        }
        else if (currentError)
        {
            // Continuation line — append to current error's message
            currentError->message += "\n  " + line;
            currentError->rawBlobText += "\n" + line;
        }
    }
}

ShaderError ShaderDebug::ParseSingleError(
    const std::string& line,
    const std::string& defaultFilename)
{
    ShaderError err;
    err.timestamp = std::chrono::system_clock::now();

    // ── Regex patterns for D3DCompile error formats ──────────────────
    //
    // Pattern 1: filename(line,col-col): error XNNNN: message
    // Pattern 2: filename(line,col): error XNNNN: message
    // Pattern 3: filename(line): error XNNNN: message
    // Pattern 4: (line,col-col): error XNNNN: message
    // Pattern 5: error XNNNN: message

    // Try Pattern 1/2/3: filename(line[,col[-col]]): severity code: message
    static const std::regex rxFull(
        R"(^(.+?)\((\d+)(?:,(\d+)(?:-\d+)?)?\)\s*:\s*(error|warning)\s+([A-Z]\d+)\s*:\s*(.*)$)",
        std::regex::icase);

    // Try Pattern 4: (line,col): severity code: message
    static const std::regex rxNoFile(
        R"(^\((\d+)(?:,(\d+)(?:-\d+)?)?\)\s*:\s*(error|warning)\s+([A-Z]\d+)\s*:\s*(.*)$)",
        std::regex::icase);

    // Try Pattern 5: severity code: message
    static const std::regex rxBare(
        R"(^(error|warning)\s+([A-Z]\d+)\s*:\s*(.*)$)",
        std::regex::icase);

    std::smatch match;

    if (std::regex_match(line, match, rxFull))
    {
        err.filename  = match[1].str();
        err.line      = std::stoi(match[2].str());
        err.column    = match[3].matched ? std::stoi(match[3].str()) : -1;
        err.severity  = (_stricmp(match[4].str().c_str(), "warning") == 0)
                        ? Severity::Warning : Severity::Error;
        err.errorCode = match[5].str();
        err.message   = match[6].str();
    }
    else if (std::regex_match(line, match, rxNoFile))
    {
        err.filename  = defaultFilename;
        err.line      = std::stoi(match[1].str());
        err.column    = match[2].matched ? std::stoi(match[2].str()) : -1;
        err.severity  = (_stricmp(match[3].str().c_str(), "warning") == 0)
                        ? Severity::Warning : Severity::Error;
        err.errorCode = match[4].str();
        err.message   = match[5].str();
    }
    else if (std::regex_match(line, match, rxBare))
    {
        err.filename  = defaultFilename;
        err.severity  = (_stricmp(match[1].str().c_str(), "warning") == 0)
                        ? Severity::Warning : Severity::Error;
        err.errorCode = match[2].str();
        err.message   = match[3].str();
    }
    else
    {
        // Unparseable — store as-is
        err.filename  = defaultFilename;
        err.severity  = Severity::Error;
        err.errorCode = "????";
        err.message   = line;
    }

    // Clean up common artifacts
    // Trim trailing \r
    while (!err.message.empty() && (err.message.back() == '\r' ||
                                     err.message.back() == '\n'))
        err.message.pop_back();

    return err;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Source snippet extraction
// ═══════════════════════════════════════════════════════════════════════════

CompilationAttempt::SourceSnippet ShaderDebug::ExtractSnippet(
    const std::string& sourceCode,
    int errorLine, int radius)
{
    CompilationAttempt::SourceSnippet snippet;
    snippet.errorLine = errorLine;

    std::istringstream stream(sourceCode);
    std::string line;
    std::vector<std::string> allLines;

    while (std::getline(stream, line))
    {
        // Trim trailing \r
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        allLines.push_back(line);
    }

    int startLine = (std::max)(1, errorLine - radius);
    int endLine   = (std::min)(static_cast<int>(allLines.size()), errorLine + radius);

    snippet.startLine = startLine;
    snippet.endLine   = endLine;

    for (int i = startLine - 1; i < endLine && i < static_cast<int>(allLines.size()); i++)
    {
        snippet.lines.push_back(allLines[i]);
    }

    return snippet;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Source file inference from naming conventions
// ═══════════════════════════════════════════════════════════════════════════

std::string ShaderDebug::InferSourceFile(
    const std::string& sourceName,
    const std::string& entryPoint)
{
    if (!sourceName.empty() && sourceName != "<memory>")
    {
        // Extract just the filename from any path
        auto pos = sourceName.find_last_of("\\/");
        if (pos != std::string::npos)
            return sourceName.substr(pos + 1);
        return sourceName;
    }

    // Try to infer from entry point name
    std::string ep = entryPoint;
    std::transform(ep.begin(), ep.end(), ep.begin(), ::tolower);

    if (ep.find("bloom") != std::string::npos)    return "bloom.hlsl";
    if (ep.find("adapt") != std::string::npos)    return "adaptation.hlsl";
    if (ep.find("dof") != std::string::npos)      return "dof.hlsl";
    if (ep.find("lens") != std::string::npos)     return "lens.hlsl";
    if (ep.find("underwater") != std::string::npos)return "underwater.hlsl";

    return "<unknown>";
}


// ═══════════════════════════════════════════════════════════════════════════
//  Log file writing
// ═══════════════════════════════════════════════════════════════════════════

void ShaderDebug::EnsureLogDirectory()
{
    auto dir = m_logPath.parent_path();
    if (!std::filesystem::exists(dir))
    {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
    }
}

void ShaderDebug::WriteLogHeader()
{
    if (m_logHeaderWritten) return;

    std::ofstream file(m_logPath, std::ios::trunc);
    if (!file.is_open()) return;

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    struct tm tmBuf;
    localtime_s(&tmBuf, &time);

    file << "╔══════════════════════════════════════════════════════════════════╗\n";
    file << "║       RAW — Shader Compilation Diagnostic Log                    ║\n";
    file << "╚══════════════════════════════════════════════════════════════════╝\n";
    file << "\n";
    file << "  Session: " << std::put_time(&tmBuf, "%Y-%m-%d %H:%M:%S") << "\n";
    file << "  Log:     " << m_logPath.string() << "\n";
    file << "\n";
    file << "  Hotkeys: F10 = toggle overlay, F11 = clear errors\n";
    file << "           Page Up / Page Down = scroll error list\n";
    file << "\n";
    file << "──────────────────────────────────────────────────────────────────\n";
    file << "\n";

    m_logHeaderWritten = true;
}

void ShaderDebug::WriteLogEntry(const CompilationAttempt& attempt)
{
    std::ofstream file(m_logPath, std::ios::app);
    if (!file.is_open()) return;

    auto time = std::chrono::system_clock::to_time_t(attempt.timestamp);
    struct tm tmBuf;
    localtime_s(&tmBuf, &time);

    // ── Header ───────────────────────────────────────────────────────
    file << "┌─ COMPILATION " << (attempt.succeeded ? "SUCCESS" : "FAILED")
         << " ─────────────────────────────────────────────\n";
    file << "│  Time:    " << std::put_time(&tmBuf, "%H:%M:%S") << "\n";
    file << "│  File:    " << attempt.sourceFile << "\n";
    file << "│  Entry:   " << attempt.entryPoint << "\n";
    file << "│  Profile: " << attempt.profile << "\n";
    file << "│  Compile: " << std::fixed << std::setprecision(1)
         << attempt.compileTimeMs << " ms\n";

    if (!attempt.errors.empty())
    {
        file << "│\n";
        file << "│  ERRORS (" << attempt.errors.size() << "):\n";
        file << "│\n";

        for (size_t i = 0; i < attempt.errors.size(); i++)
        {
            const auto& err = attempt.errors[i];
            file << "│  [" << (i + 1) << "] ";

            if (!err.filename.empty())
                file << err.filename;

            if (err.line > 0)
            {
                file << "(" << err.line;
                if (err.column > 0) file << "," << err.column;
                file << ")";
            }

            file << ": " << err.errorCode << " — " << err.message << "\n";
        }
    }

    if (!attempt.warnings.empty())
    {
        file << "│\n";
        file << "│  WARNINGS (" << attempt.warnings.size() << "):\n";
        file << "│\n";

        for (size_t i = 0; i < attempt.warnings.size(); i++)
        {
            const auto& warn = attempt.warnings[i];
            file << "│  [" << (i + 1) << "] ";

            if (!warn.filename.empty())
                file << warn.filename;

            if (warn.line > 0)
            {
                file << "(" << warn.line;
                if (warn.column > 0) file << "," << warn.column;
                file << ")";
            }

            file << ": " << warn.errorCode << " — " << warn.message << "\n";
        }
    }

    // ── Source snippets ──────────────────────────────────────────────
    if (!attempt.snippets.empty())
    {
        file << "│\n";
        file << "│  SOURCE CONTEXT:\n";

        for (const auto& snippet : attempt.snippets)
        {
            file << "│\n";
            int lineNum = snippet.startLine;
            for (const auto& srcLine : snippet.lines)
            {
                bool isErrorLine = (lineNum == snippet.errorLine);
                file << "│  " << (isErrorLine ? ">>>" : "   ")
                     << " " << std::setw(5) << lineNum << " │ "
                     << srcLine << "\n";
                lineNum++;
            }
        }
    }

    // ── Raw blob (if parsing missed anything) ────────────────────────
    if (!attempt.rawErrorBlob.empty())
    {
        file << "│\n";
        file << "│  RAW ERROR BLOB:\n";
        std::istringstream blobStream(attempt.rawErrorBlob);
        std::string blobLine;
        while (std::getline(blobStream, blobLine))
        {
            file << "│    " << blobLine << "\n";
        }
    }

    file << "└──────────────────────────────────────────────────────────────\n\n";
    file.flush();
}

void ShaderDebug::FlushLog()
{
    // Re-write entire log from stored attempts (for when we need a clean dump)
    WriteLogHeader();
    for (const auto& attempt : m_attempts)
    {
        if (!attempt.succeeded)
            WriteLogEntry(attempt);
    }
}


// ═══════════════════════════════════════════════════════════════════════════
//  Error queries
// ═══════════════════════════════════════════════════════════════════════════

bool   ShaderDebug::HasErrors()    const { return m_totalErrors > 0; }
bool   ShaderDebug::HasWarnings()  const { return m_totalWarnings > 0; }
size_t ShaderDebug::ErrorCount()   const { return m_totalErrors; }
size_t ShaderDebug::WarningCount() const { return m_totalWarnings; }
size_t ShaderDebug::TotalAttempts() const { return m_attempts.size(); }

std::vector<const CompilationAttempt*> ShaderDebug::GetFailedAttempts() const
{
    std::vector<const CompilationAttempt*> result;
    std::lock_guard<std::mutex> lk(m_errorMtx);
    for (const auto& a : m_attempts)
    {
        if (!a.succeeded)
            result.push_back(&a);
    }
    return result;
}

void ShaderDebug::ClearAll()
{
    std::lock_guard<std::mutex> lk(m_errorMtx);
    m_attempts.clear();
    m_totalErrors = 0;
    m_totalWarnings = 0;
    m_totalSuccess = 0;
    m_scrollOffset = 0;
    SB_LOG_INFO("ShaderDebug: all errors cleared");
}

std::string ShaderDebug::GetCachedSource(const std::string& sourceFile) const
{
    std::lock_guard<std::mutex> lk(m_sourceCacheMtx);
    auto it = m_sourceCache.find(sourceFile);
    if (it != m_sourceCache.end())
        return it->second;
    return {};
}


// ═══════════════════════════════════════════════════════════════════════════
//  Overlay control
// ═══════════════════════════════════════════════════════════════════════════

void ShaderDebug::SetOverlayVisible(bool visible)  { m_overlayVisible = visible; }
void ShaderDebug::ToggleOverlay()                   { m_overlayVisible = !m_overlayVisible; }
void ShaderDebug::ScrollUp()    { m_scrollOffset = (std::max)(0, m_scrollOffset - 5); }
void ShaderDebug::ScrollDown()  { m_scrollOffset += 5; }
void ShaderDebug::ScrollToTop() { m_scrollOffset = 0; }
void ShaderDebug::ScrollToBottom()
{
    // Scroll to show the last error
    // Actual max offset computed during rendering
    m_scrollOffset = 99999;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Input handling
// ═══════════════════════════════════════════════════════════════════════════

bool ShaderDebug::IsKeyPressed(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

bool ShaderDebug::WasKeyJustPressed(int vk)
{
    bool current = IsKeyPressed(vk);
    bool prev = m_prevKeyStates[vk & 0xFF];
    m_prevKeyStates[vk & 0xFF] = current;
    return current && !prev;
}

void ShaderDebug::ProcessInput()
{
    if (WasKeyJustPressed(m_config.toggleKey))
        ToggleOverlay();

    if (WasKeyJustPressed(m_config.clearKey))
        ClearAll();

    if (m_overlayVisible)
    {
        if (IsKeyPressed(m_config.scrollUpKey))
            ScrollUp();
        if (IsKeyPressed(m_config.scrollDownKey))
            ScrollDown();
    }
}


// ═══════════════════════════════════════════════════════════════════════════
//  Overlay GPU resource initialization
//
//  Creates a minimal rendering pipeline for text + rectangles:
//    - A procedurally generated 8×16 bitmap font atlas (CP437-style)
//    - A simple vertex shader (screen-space quad transform)
//    - A simple pixel shader (texture sample × color tint)
//    - Blend state for alpha transparency
// ═══════════════════════════════════════════════════════════════════════════

void ShaderDebug::InitOverlayResources()
{
    if (!m_device || m_overlayInited) return;

    // ── Overlay vertex shader ────────────────────────────────────────
    static const char* vsSource = R"(
        cbuffer CB : register(b0) {
            float2 screenSize;
            float2 padding;
        };
        struct VS_IN  { float2 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; };
        struct VS_OUT { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR; };
        VS_OUT main(VS_IN i) {
            VS_OUT o;
            // Convert pixel coords to NDC
            o.pos = float4(i.pos / screenSize * 2.0 - 1.0, 0, 1);
            o.pos.y = -o.pos.y; // Flip Y for top-left origin
            o.uv  = i.uv;
            o.col = i.col;
            return o;
        }
    )";

    // ── Overlay pixel shader ─────────────────────────────────────────
    static const char* psSource = R"(
        Texture2D    fontTex : register(t0);
        SamplerState samp   : register(s0);
        struct PS_IN { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR; };
        float4 main(PS_IN i) : SV_Target {
            float alpha = fontTex.Sample(samp, i.uv).r;
            return float4(i.col.rgb, i.col.a * alpha);
        }
    )";

    // ── Rect pixel shader (solid color, no texture) ──────────────────
    static const char* psRectSource = R"(
        struct PS_IN { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR; };
        float4 main(PS_IN i) : SV_Target {
            return i.col;
        }
    )";

    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3DBlob* psRectBlob = nullptr;
    ID3DBlob* errBlob = nullptr;

    // Compile the overlay shaders (using the REAL D3DCompile, not our hook)
    auto compileInternal = s_origD3DCompile ? s_origD3DCompile
        : reinterpret_cast<fnD3DCompile>(
            GetProcAddress(GetModuleHandleA("d3dcompiler_47.dll"), "D3DCompile"));

    if (!compileInternal) return;

    compileInternal(vsSource, strlen(vsSource), "SB_DebugVS", nullptr, nullptr,
                    "main", "vs_5_0", 0, 0, &vsBlob, &errBlob);
    if (errBlob) { errBlob->Release(); errBlob = nullptr; }

    compileInternal(psSource, strlen(psSource), "SB_DebugPS", nullptr, nullptr,
                    "main", "ps_5_0", 0, 0, &psBlob, &errBlob);
    if (errBlob) { errBlob->Release(); errBlob = nullptr; }

    compileInternal(psRectSource, strlen(psRectSource), "SB_DebugPSRect", nullptr, nullptr,
                    "main", "ps_5_0", 0, 0, &psRectBlob, &errBlob);
    if (errBlob) { errBlob->Release(); errBlob = nullptr; }

    if (vsBlob)
    {
        m_device->CreateVertexShader(vsBlob->GetBufferPointer(),
                                     vsBlob->GetBufferSize(), nullptr,
                                     m_overlayVS.GetAddressOf());

        // Input layout
        D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
              D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8,
              D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16,
              D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        m_device->CreateInputLayout(layout, 3,
                                    vsBlob->GetBufferPointer(),
                                    vsBlob->GetBufferSize(),
                                    m_overlayLayout.GetAddressOf());
        vsBlob->Release();
    }

    if (psBlob)
    {
        m_device->CreatePixelShader(psBlob->GetBufferPointer(),
                                    psBlob->GetBufferSize(), nullptr,
                                    m_overlayPS.GetAddressOf());
        psBlob->Release();
    }
    if (psRectBlob) psRectBlob->Release();

    // ── Constant buffer ──────────────────────────────────────────────
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth      = 16; // float2 screenSize + padding
    cbDesc.Usage           = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags       = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags  = D3D11_CPU_ACCESS_WRITE;
    m_device->CreateBuffer(&cbDesc, nullptr, m_overlayCB.GetAddressOf());

    // ── Dynamic vertex buffer (resized each frame) ───────────────────
    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth      = 65536 * 32; // ~65k vertices × 32 bytes each
    vbDesc.Usage           = D3D11_USAGE_DYNAMIC;
    vbDesc.BindFlags       = D3D11_BIND_VERTEX_BUFFER;
    vbDesc.CPUAccessFlags  = D3D11_CPU_ACCESS_WRITE;
    m_device->CreateBuffer(&vbDesc, nullptr, m_overlayVB.GetAddressOf());

    // ── Blend state (premultiplied alpha) ────────────────────────────
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable    = TRUE;
    blendDesc.RenderTarget[0].SrcBlend       = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend      = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp        = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha   = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    m_device->CreateBlendState(&blendDesc, m_overlayBlend.GetAddressOf());

    // ── Rasterizer state (no culling) ────────────────────────────────
    D3D11_RASTERIZER_DESC rasterDesc = {};
    rasterDesc.FillMode = D3D11_FILL_SOLID;
    rasterDesc.CullMode = D3D11_CULL_NONE;
    rasterDesc.ScissorEnable = FALSE;
    m_device->CreateRasterizerState(&rasterDesc, m_overlayRaster.GetAddressOf());

    // ── Depth stencil state (no depth test) ──────────────────────────
    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = FALSE;
    m_device->CreateDepthStencilState(&dsDesc, m_overlayDepthState.GetAddressOf());

    // ── Sampler (point filtering for crisp font) ─────────────────────
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter   = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    m_device->CreateSamplerState(&sampDesc, m_overlaySampler.GetAddressOf());

    // ── Generate bitmap font atlas ───────────────────────────────────
    InitFontAtlas();

    m_overlayInited = true;
    SB_LOG_INFO("ShaderDebug: overlay GPU resources initialized");
}


// ═══════════════════════════════════════════════════════════════════════════
//  Procedural bitmap font atlas
//
//  Generates a 128×64 pixel texture containing a 8×8 pixel grid of
//  ASCII characters (space through tilde). This is entirely self-contained
//  with no external file dependencies.
//
//  Each glyph is 8 pixels wide × 8 pixels tall, arranged in a 16×6 grid.
//  Based on the classic CP437 / VGA hardware font.
// ═══════════════════════════════════════════════════════════════════════════

void ShaderDebug::InitFontAtlas()
{
    // 8×8 pixel font data — each byte is a row of 8 pixels
    // Covers ASCII 32 (space) through 126 (tilde), 95 characters
    // Arranged: glyph[charIndex][row], MSB = leftmost pixel
    static const uint8_t fontData[95][8] = {
        // 32: space
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        // 33: !
        {0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x00},
        // 34: "
        {0x6C,0x6C,0x24,0x00,0x00,0x00,0x00,0x00},
        // 35: #
        {0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00},
        // 36: $
        {0x18,0x7E,0xC0,0x7C,0x06,0xFC,0x18,0x00},
        // 37: %
        {0x00,0xC6,0xCC,0x18,0x30,0x66,0xC6,0x00},
        // 38: &
        {0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00},
        // 39: '
        {0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00},
        // 40: (
        {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00},
        // 41: )
        {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00},
        // 42: *
        {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00},
        // 43: +
        {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00},
        // 44: ,
        {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30},
        // 45: -
        {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00},
        // 46: .
        {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00},
        // 47: /
        {0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00},
        // 48-57: 0-9
        {0x7C,0xC6,0xCE,0xDE,0xF6,0xE6,0x7C,0x00},
        {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00},
        {0x7C,0xC6,0x06,0x1C,0x30,0x66,0xFE,0x00},
        {0x7C,0xC6,0x06,0x3C,0x06,0xC6,0x7C,0x00},
        {0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x1E,0x00},
        {0xFE,0xC0,0xFC,0x06,0x06,0xC6,0x7C,0x00},
        {0x38,0x60,0xC0,0xFC,0xC6,0xC6,0x7C,0x00},
        {0xFE,0xC6,0x0C,0x18,0x30,0x30,0x30,0x00},
        {0x7C,0xC6,0xC6,0x7C,0xC6,0xC6,0x7C,0x00},
        {0x7C,0xC6,0xC6,0x7E,0x06,0x0C,0x78,0x00},
        // 58: :
        {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00},
        // 59: ;
        {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30},
        // 60: <
        {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00},
        // 61: =
        {0x00,0x00,0x7E,0x00,0x00,0x7E,0x00,0x00},
        // 62: >
        {0x60,0x30,0x18,0x0C,0x18,0x30,0x60,0x00},
        // 63: ?
        {0x7C,0xC6,0x0C,0x18,0x18,0x00,0x18,0x00},
        // 64: @
        {0x7C,0xC6,0xDE,0xDE,0xDE,0xC0,0x78,0x00},
        // 65-90: A-Z
        {0x38,0x6C,0xC6,0xFE,0xC6,0xC6,0xC6,0x00},
        {0xFC,0x66,0x66,0x7C,0x66,0x66,0xFC,0x00},
        {0x3C,0x66,0xC0,0xC0,0xC0,0x66,0x3C,0x00},
        {0xF8,0x6C,0x66,0x66,0x66,0x6C,0xF8,0x00},
        {0xFE,0x62,0x68,0x78,0x68,0x62,0xFE,0x00},
        {0xFE,0x62,0x68,0x78,0x68,0x60,0xF0,0x00},
        {0x3C,0x66,0xC0,0xC0,0xCE,0x66,0x3E,0x00},
        {0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0x00},
        {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
        {0x1E,0x0C,0x0C,0x0C,0xCC,0xCC,0x78,0x00},
        {0xE6,0x66,0x6C,0x78,0x6C,0x66,0xE6,0x00},
        {0xF0,0x60,0x60,0x60,0x62,0x66,0xFE,0x00},
        {0xC6,0xEE,0xFE,0xD6,0xC6,0xC6,0xC6,0x00},
        {0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6,0x00},
        {0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00},
        {0xFC,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00},
        {0x7C,0xC6,0xC6,0xC6,0xD6,0xDE,0x7C,0x0E},
        {0xFC,0x66,0x66,0x7C,0x6C,0x66,0xE6,0x00},
        {0x7C,0xC6,0xE0,0x7C,0x0E,0xC6,0x7C,0x00},
        {0x7E,0x5A,0x18,0x18,0x18,0x18,0x3C,0x00},
        {0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00},
        {0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x00},
        {0xC6,0xC6,0xC6,0xD6,0xFE,0xEE,0xC6,0x00},
        {0xC6,0xC6,0x6C,0x38,0x6C,0xC6,0xC6,0x00},
        {0x66,0x66,0x66,0x3C,0x18,0x18,0x3C,0x00},
        {0xFE,0xC6,0x8C,0x18,0x32,0x66,0xFE,0x00},
        // 91: [
        {0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00},
        // 92: backslash
        {0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0x00},
        // 93: ]
        {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00},
        // 94: ^
        {0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00},
        // 95: _
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF},
        // 96: `
        {0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00},
        // 97-122: a-z
        {0x00,0x00,0x78,0x0C,0x7C,0xCC,0x76,0x00},
        {0xE0,0x60,0x7C,0x66,0x66,0x66,0xDC,0x00},
        {0x00,0x00,0x7C,0xC6,0xC0,0xC6,0x7C,0x00},
        {0x1C,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00},
        {0x00,0x00,0x7C,0xC6,0xFE,0xC0,0x7C,0x00},
        {0x1C,0x36,0x30,0x78,0x30,0x30,0x78,0x00},
        {0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0x78},
        {0xE0,0x60,0x6C,0x76,0x66,0x66,0xE6,0x00},
        {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00},
        {0x06,0x00,0x0E,0x06,0x06,0x66,0x66,0x3C},
        {0xE0,0x60,0x66,0x6C,0x78,0x6C,0xE6,0x00},
        {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
        {0x00,0x00,0xEC,0xFE,0xD6,0xC6,0xC6,0x00},
        {0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x00},
        {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0x00},
        {0x00,0x00,0xDC,0x66,0x66,0x7C,0x60,0xF0},
        {0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0x1E},
        {0x00,0x00,0xDC,0x76,0x60,0x60,0xF0,0x00},
        {0x00,0x00,0x7C,0xC0,0x7C,0x06,0xFC,0x00},
        {0x30,0x30,0x7C,0x30,0x30,0x36,0x1C,0x00},
        {0x00,0x00,0xCC,0xCC,0xCC,0xCC,0x76,0x00},
        {0x00,0x00,0xC6,0xC6,0xC6,0x6C,0x38,0x00},
        {0x00,0x00,0xC6,0xC6,0xD6,0xFE,0x6C,0x00},
        {0x00,0x00,0xC6,0x6C,0x38,0x6C,0xC6,0x00},
        {0x00,0x00,0xC6,0xC6,0xC6,0x7E,0x06,0xFC},
        {0x00,0x00,0xFE,0x8C,0x18,0x32,0xFE,0x00},
        // 123: {
        {0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00},
        // 124: |
        {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00},
        // 125: }
        {0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00},
        // 126: ~
        {0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00},
    };

    // Generate texture: 16 chars per row, 6 rows, 8×8 pixels per glyph
    m_glyphW = 8;
    m_glyphH = 8;
    m_fontAtlasW = kFontCharsPerRow * m_glyphW; // 128
    m_fontAtlasH = 6 * m_glyphH;                 // 48

    std::vector<uint8_t> pixels(m_fontAtlasW * m_fontAtlasH, 0);

    for (int charIdx = 0; charIdx < 95; charIdx++)
    {
        int gridX = charIdx % kFontCharsPerRow;
        int gridY = charIdx / kFontCharsPerRow;

        for (int row = 0; row < 8; row++)
        {
            uint8_t rowBits = fontData[charIdx][row];
            for (int col = 0; col < 8; col++)
            {
                int px = gridX * 8 + col;
                int py = gridY * 8 + row;
                pixels[py * m_fontAtlasW + px] =
                    (rowBits & (0x80 >> col)) ? 255 : 0;
            }
        }
    }

    // Create D3D11 texture
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width     = m_fontAtlasW;
    texDesc.Height    = m_fontAtlasH;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format    = DXGI_FORMAT_R8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage     = D3D11_USAGE_IMMUTABLE;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem     = pixels.data();
    initData.SysMemPitch = m_fontAtlasW;

    m_device->CreateTexture2D(&texDesc, &initData, m_fontTex.GetAddressOf());
    m_device->CreateShaderResourceView(m_fontTex.Get(), nullptr,
                                       m_fontSRV.GetAddressOf());
}


// ═══════════════════════════════════════════════════════════════════════════
//  Overlay rendering — builds vertex buffer and draws the error panel
// ═══════════════════════════════════════════════════════════════════════════

struct OverlayVertex
{
    float x, y;     // Screen-space position (pixels)
    float u, v;     // Font atlas UV
    float r, g, b, a; // Color
};

// Thread-local vertex accumulator
static std::vector<OverlayVertex> g_overlayVerts;

void ShaderDebug::DrawRect(float x, float y, float w, float h, const float color[4])
{
    // Two triangles forming a quad, UV set to a fully white area of the font atlas
    // (the space character area is all zeros, so use a known lit pixel instead)
    float u0 = 0, v0 = 0, u1 = 0.001f, v1 = 0.001f;

    // Actually: for filled rects, we'll use a separate pixel shader pass.
    // Simpler: UV (0,0) maps to top-left of font atlas which may be lit or not.
    // Solution: Set UV to center of a known filled glyph like '#' (ASCII 35, index 3)
    // '#' at grid position (3, 0), pixel center = (3*8+4, 4) / atlas size
    u0 = (3.0f * 8 + 2) / m_fontAtlasW;
    v0 = 2.0f / m_fontAtlasH;
    u1 = u0 + 0.001f;
    v1 = v0 + 0.001f;

    OverlayVertex verts[6] = {
        { x,     y,     u0, v0, color[0], color[1], color[2], color[3] },
        { x + w, y,     u1, v0, color[0], color[1], color[2], color[3] },
        { x + w, y + h, u1, v1, color[0], color[1], color[2], color[3] },
        { x,     y,     u0, v0, color[0], color[1], color[2], color[3] },
        { x + w, y + h, u1, v1, color[0], color[1], color[2], color[3] },
        { x,     y + h, u0, v1, color[0], color[1], color[2], color[3] },
    };

    for (auto& v : verts) g_overlayVerts.push_back(v);
}

void ShaderDebug::DrawText(float x, float y, const std::string& text,
                           const float color[4], float scale)
{
    float cx = x;
    float cy = y;
    float glyphW = m_glyphW * scale;
    float glyphH = m_glyphH * scale;

    for (char ch : text)
    {
        if (ch == '\n') { cx = x; cy += glyphH + 1; continue; }
        if (ch == '\r') continue;

        int charIdx = static_cast<int>(ch) - kFontFirstChar;
        if (charIdx < 0 || charIdx > (kFontLastChar - kFontFirstChar))
            charIdx = 0; // Unknown chars → space

        int gridX = charIdx % kFontCharsPerRow;
        int gridY = charIdx / kFontCharsPerRow;

        float u0 = (float)(gridX * m_glyphW) / m_fontAtlasW;
        float v0 = (float)(gridY * m_glyphH) / m_fontAtlasH;
        float u1 = u0 + (float)m_glyphW / m_fontAtlasW;
        float v1 = v0 + (float)m_glyphH / m_fontAtlasH;

        OverlayVertex verts[6] = {
            { cx,          cy,          u0, v0, color[0], color[1], color[2], color[3] },
            { cx + glyphW, cy,          u1, v0, color[0], color[1], color[2], color[3] },
            { cx + glyphW, cy + glyphH, u1, v1, color[0], color[1], color[2], color[3] },
            { cx,          cy,          u0, v0, color[0], color[1], color[2], color[3] },
            { cx + glyphW, cy + glyphH, u1, v1, color[0], color[1], color[2], color[3] },
            { cx,          cy + glyphH, u0, v1, color[0], color[1], color[2], color[3] },
        };

        for (auto& v : verts) g_overlayVerts.push_back(v);

        cx += glyphW;
    }
}


void ShaderDebug::RenderOverlay()
{
    if (!m_overlayInited || !m_context) return;

    // ── Get backbuffer dimensions ────────────────────────────────────
    ID3D11Texture2D* backBuffer = nullptr;
    m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                           reinterpret_cast<void**>(&backBuffer));
    if (!backBuffer) return;

    D3D11_TEXTURE2D_DESC bbDesc;
    backBuffer->GetDesc(&bbDesc);
    float screenW = static_cast<float>(bbDesc.Width);
    float screenH = static_cast<float>(bbDesc.Height);
    backBuffer->Release();

    // ── Build the error panel vertex data ────────────────────────────
    g_overlayVerts.clear();
    g_overlayVerts.reserve(65536);

    float scale = m_config.fontSize / 8.0f; // Font is 8px base
    float lineH = m_glyphH * scale + 2.0f;
    float charW = m_glyphW * scale;

    float panelX = m_config.panelX * screenW;
    float panelY = m_config.panelY * screenH;
    float panelW = m_config.panelW * screenW;
    float panelH = m_config.panelH * screenH;

    m_maxVisibleLines = static_cast<int>(panelH / lineH) - 3; // Leave room for header

    // ── Background ───────────────────────────────────────────────────
    DrawRect(panelX, panelY, panelW, panelH, m_config.colorBg);

    // ── Header bar ───────────────────────────────────────────────────
    DrawRect(panelX, panelY, panelW, lineH * 2 + 4, m_config.colorHeaderBg);

    // Title line
    char title[256];
    snprintf(title, sizeof(title),
             " SKYRIMBRIDGE SHADER DIAGNOSTICS  [%zu errors, %zu warnings, %zu OK]",
             m_totalErrors, m_totalWarnings, m_totalSuccess);
    float titleColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    DrawText(panelX + 4, panelY + 2, title, titleColor, scale);

    // Hotkey hint line
    char hint[128];
    snprintf(hint, sizeof(hint),
             " F10=Toggle  F11=Clear  PgUp/PgDn=Scroll  [%zu compilation attempts]",
             m_attempts.size());
    float hintColor[4] = { 0.7f, 0.7f, 0.7f, 1.0f };
    DrawText(panelX + 4, panelY + lineH + 2, hint, hintColor, scale * 0.85f);

    // ── Error entries ────────────────────────────────────────────────
    float curY = panelY + lineH * 2 + 8;
    int lineNum = 0;

    std::lock_guard<std::mutex> lk(m_errorMtx);

    for (const auto& attempt : m_attempts)
    {
        if (attempt.succeeded) continue; // Only show failures

        // Skip lines above scroll offset
        if (lineNum < m_scrollOffset)
        {
            lineNum += 2 + static_cast<int>(attempt.errors.size())
                        + (m_config.showSourceSnippets ?
                           static_cast<int>(attempt.snippets.size()) * 8 : 0);
            continue;
        }

        if (curY + lineH > panelY + panelH - lineH)
            break; // Out of visible space

        // ── Attempt header ───────────────────────────────────────────
        // Draw a subtle separator line
        float sepColor[4] = { 0.3f, 0.3f, 0.4f, 0.8f };
        DrawRect(panelX + 4, curY, panelW - 8, 1, sepColor);
        curY += 4;

        char attemptHeader[512];
        snprintf(attemptHeader, sizeof(attemptHeader),
                 "%s  [%s]  entry=%s  (%.1fms)",
                 attempt.sourceFile.c_str(),
                 attempt.profile.c_str(),
                 attempt.entryPoint.c_str(),
                 attempt.compileTimeMs);
        DrawText(panelX + 8, curY, attemptHeader, m_config.colorFilename, scale);
        curY += lineH;
        lineNum++;

        // ── Individual errors ────────────────────────────────────────
        for (const auto& err : attempt.errors)
        {
            if (curY + lineH > panelY + panelH - lineH) break;

            // Error icon + file:line
            char errLoc[256];
            if (err.line > 0)
            {
                snprintf(errLoc, sizeof(errLoc), "  >> line %d", err.line);
                if (err.column > 0)
                    snprintf(errLoc + strlen(errLoc), sizeof(errLoc) - strlen(errLoc),
                             ", col %d", err.column);
            }
            else
            {
                snprintf(errLoc, sizeof(errLoc), "  >> (no line info)");
            }
            DrawText(panelX + 12, curY, errLoc, m_config.colorLineNum, scale);

            // Error code
            float codeX = panelX + 12 + strlen(errLoc) * charW + charW;
            DrawText(codeX, curY, err.errorCode, m_config.colorCode, scale);

            curY += lineH;
            lineNum++;

            // Error message (may wrap)
            if (curY + lineH > panelY + panelH - lineH) break;

            char msgLine[512];
            snprintf(msgLine, sizeof(msgLine), "     %s", err.message.c_str());
            // Truncate if too long for panel
            int maxChars = static_cast<int>((panelW - 24) / charW);
            if (static_cast<int>(strlen(msgLine)) > maxChars)
                msgLine[maxChars] = '\0';

            DrawText(panelX + 12, curY, msgLine, m_config.colorError, scale * 0.9f);
            curY += lineH;
            lineNum++;
        }

        // ── Source snippets ──────────────────────────────────────────
        if (m_config.showSourceSnippets)
        {
            for (const auto& snippet : attempt.snippets)
            {
                if (curY + lineH > panelY + panelH - lineH) break;

                int srcLine = snippet.startLine;
                for (const auto& srcText : snippet.lines)
                {
                    if (curY + lineH > panelY + panelH - lineH) break;

                    bool isErrLine = (srcLine == snippet.errorLine);

                    // Highlight the error line
                    if (isErrLine)
                    {
                        DrawRect(panelX + 4, curY - 1,
                                 panelW - 8, lineH + 1, m_config.colorSourceErr);
                    }

                    char srcBuf[512];
                    snprintf(srcBuf, sizeof(srcBuf), "    %s %5d | %s",
                             isErrLine ? ">>>" : "   ", srcLine,
                             srcText.c_str());

                    int maxSrcChars = static_cast<int>((panelW - 24) / (charW * 0.85f));
                    if (static_cast<int>(strlen(srcBuf)) > maxSrcChars)
                        srcBuf[maxSrcChars] = '\0';

                    const float* srcColor = isErrLine ?
                        m_config.colorError : m_config.colorSourceLine;
                    DrawText(panelX + 12, curY, srcBuf, srcColor, scale * 0.85f);

                    curY += lineH * 0.9f;
                    srcLine++;
                    lineNum++;
                }

                curY += 4; // Gap between snippets
            }
        }

        curY += 4; // Gap between attempts
    }

    // ── Upload vertex buffer and render ──────────────────────────────
    if (g_overlayVerts.empty()) return;

    // Save D3D11 state (we restore after drawing)
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> savedRTV;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> savedDSV;
    m_context->OMGetRenderTargets(1, savedRTV.GetAddressOf(), savedDSV.GetAddressOf());

    D3D11_VIEWPORT savedVP;
    UINT numVP = 1;
    m_context->RSGetViewports(&numVP, &savedVP);

    // Set backbuffer as render target
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> bbRTV;
    {
        ID3D11Texture2D* bb = nullptr;
        m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                               reinterpret_cast<void**>(&bb));
        if (bb) {
            m_device->CreateRenderTargetView(bb, nullptr, bbRTV.GetAddressOf());
            bb->Release();
        }
    }
    m_context->OMSetRenderTargets(1, bbRTV.GetAddressOf(), nullptr);

    // Set viewport
    D3D11_VIEWPORT vp = {};
    vp.Width    = screenW;
    vp.Height   = screenH;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);

    // Update constant buffer
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(m_context->Map(m_overlayCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        float* cbData = reinterpret_cast<float*>(mapped.pData);
        cbData[0] = screenW;
        cbData[1] = screenH;
        cbData[2] = 0;
        cbData[3] = 0;
        m_context->Unmap(m_overlayCB.Get(), 0);
    }

    // Upload vertex data
    size_t vertBytes = g_overlayVerts.size() * sizeof(OverlayVertex);
    if (SUCCEEDED(m_context->Map(m_overlayVB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        memcpy(mapped.pData, g_overlayVerts.data(),
               (std::min)(vertBytes, static_cast<size_t>(65536 * 32)));
        m_context->Unmap(m_overlayVB.Get(), 0);
    }

    // Set pipeline state
    UINT stride = sizeof(OverlayVertex);
    UINT offset = 0;
    m_context->IASetVertexBuffers(0, 1, m_overlayVB.GetAddressOf(), &stride, &offset);
    m_context->IASetInputLayout(m_overlayLayout.Get());
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_overlayVS.Get(), nullptr, 0);
    m_context->VSSetConstantBuffers(0, 1, m_overlayCB.GetAddressOf());
    m_context->PSSetShader(m_overlayPS.Get(), nullptr, 0);
    m_context->PSSetShaderResources(0, 1, m_fontSRV.GetAddressOf());
    m_context->PSSetSamplers(0, 1, m_overlaySampler.GetAddressOf());

    float blendFactor[4] = { 0, 0, 0, 0 };
    m_context->OMSetBlendState(m_overlayBlend.Get(), blendFactor, 0xFFFFFFFF);
    m_context->RSSetState(m_overlayRaster.Get());
    m_context->OMSetDepthStencilState(m_overlayDepthState.Get(), 0);

    // Draw
    m_context->Draw(static_cast<UINT>(g_overlayVerts.size()), 0);

    // Restore state
    m_context->OMSetRenderTargets(1, savedRTV.GetAddressOf(), savedDSV.Get());
    m_context->RSSetViewports(1, &savedVP);
}


} // namespace SB::Debug
