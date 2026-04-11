// ═══════════════════════════════════════════════════════════════════════════
//  D3DCompiler_43.dll — Proxy DLL for ENB Shader Capture
//
//  ENB v504's d3d11.dll statically imports D3DCompile from D3DCOMPILER_43.dll.
//  Place this proxy in the game directory to intercept ENB's shader
//  compilations and capture the preprocessed HLSL + compiled DXBC.
//
//  The real D3DCompiler_43.dll is loaded from System32.
//
//  Author: Zain Dana Harper (SkyrimBridge project)
// ═══════════════════════════════════════════════════════════════════════════

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Shlobj.h>  // SHGetFolderPathA

#include <d3d11.h>  // For ID3DBlob, D3D_SHADER_MACRO, ID3DInclude, REFIID

typedef struct _D3D_SHADER_DATA {
    LPCVOID pBytecode;
    SIZE_T  BytecodeLength;
} D3D_SHADER_DATA;

typedef enum D3D_BLOB_PART {
    D3D_BLOB_INPUT_SIGNATURE_BLOB,
    D3D_BLOB_OUTPUT_SIGNATURE_BLOB,
    D3D_BLOB_INPUT_AND_OUTPUT_SIGNATURE_BLOB,
    D3D_BLOB_PATCH_CONSTANT_SIGNATURE_BLOB,
    D3D_BLOB_ALL_SIGNATURE_BLOB,
    D3D_BLOB_DEBUG_INFO,
    D3D_BLOB_LEGACY_SHADER,
    D3D_BLOB_XNA_PREPASS_SHADER,
    D3D_BLOB_XNA_SHADER,
    D3D_BLOB_PDB,
    D3D_BLOB_PRIVATE_DATA,
    D3D_BLOB_ROOT_SIGNATURE,
    D3D_BLOB_DEBUG_NAME,
    D3D_BLOB_TEST_ALTERNATE_SHADER = 0x8000,
    D3D_BLOB_TEST_COMPILE_DETAILS,
    D3D_BLOB_TEST_COMPILE_PERF,
    D3D_BLOB_TEST_COMPILE_REPORT,
} D3D_BLOB_PART;

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <mutex>
#include <unordered_set>
#include <filesystem>
#include <fstream>
#include <string>


// ═══════════════════════════════════════════════════════════════════════════
//  Real DLL handles + function pointers
//  - g_realDll: System32 D3DCompiler_43.dll (utility functions)
//  - g_enbCompiler: d3dcompiler_46e.dll (Boris's SM5.0 compiler for D3DCompile)
// ═══════════════════════════════════════════════════════════════════════════

static HMODULE g_realDll = nullptr;
static HMODULE g_enbCompiler = nullptr;

using fn_D3DAssemble = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR,
    const D3D_SHADER_MACRO*, ID3DInclude*, UINT, ID3DBlob**, ID3DBlob**);
using fn_D3DCompile = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR,
    const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT,
    ID3DBlob**, ID3DBlob**);
using fn_D3DCompressShaders = HRESULT(WINAPI*)(UINT, D3D_SHADER_DATA*, UINT,
    ID3DBlob**);
using fn_D3DCreateBlob = HRESULT(WINAPI*)(SIZE_T, ID3DBlob**);
using fn_D3DDecompressShaders = HRESULT(WINAPI*)(LPCVOID, SIZE_T, UINT,
    UINT, UINT*, UINT, ID3DBlob**, UINT*);
using fn_D3DDisassemble = HRESULT(WINAPI*)(LPCVOID, SIZE_T, UINT, LPCSTR,
    ID3DBlob**);
using fn_D3DDisassemble10Effect = HRESULT(WINAPI*)(void*, UINT, ID3DBlob**);
using fn_D3DGetBlobPart = HRESULT(WINAPI*)(LPCVOID, SIZE_T,
    D3D_BLOB_PART, UINT, ID3DBlob**);
using fn_D3DGetDebugInfo = HRESULT(WINAPI*)(LPCVOID, SIZE_T, ID3DBlob**);
using fn_D3DGetInputAndOutputSignatureBlob = HRESULT(WINAPI*)(LPCVOID,
    SIZE_T, ID3DBlob**);
using fn_D3DGetInputSignatureBlob = HRESULT(WINAPI*)(LPCVOID, SIZE_T,
    ID3DBlob**);
using fn_D3DGetOutputSignatureBlob = HRESULT(WINAPI*)(LPCVOID, SIZE_T,
    ID3DBlob**);
using fn_D3DPreprocess = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR,
    const D3D_SHADER_MACRO*, ID3DInclude*, ID3DBlob**, ID3DBlob**);
using fn_D3DReflect = HRESULT(WINAPI*)(LPCVOID, SIZE_T, REFIID, void**);
using fn_D3DReturnFailure1 = HRESULT(WINAPI*)();
using fn_D3DStripShader = HRESULT(WINAPI*)(LPCVOID, SIZE_T, UINT,
    ID3DBlob**);
using fn_DebugSetMute = HRESULT(WINAPI*)();

static fn_D3DAssemble                       real_D3DAssemble = nullptr;
static fn_D3DCompile                        real_D3DCompile = nullptr;
static fn_D3DCompressShaders                real_D3DCompressShaders = nullptr;
static fn_D3DCreateBlob                     real_D3DCreateBlob = nullptr;
static fn_D3DDecompressShaders              real_D3DDecompressShaders = nullptr;
static fn_D3DDisassemble                    real_D3DDisassemble = nullptr;
static fn_D3DDisassemble10Effect            real_D3DDisassemble10Effect = nullptr;
static fn_D3DGetBlobPart                    real_D3DGetBlobPart = nullptr;
static fn_D3DGetDebugInfo                   real_D3DGetDebugInfo = nullptr;
static fn_D3DGetInputAndOutputSignatureBlob real_D3DGetInputAndOutputSignatureBlob = nullptr;
static fn_D3DGetInputSignatureBlob          real_D3DGetInputSignatureBlob = nullptr;
static fn_D3DGetOutputSignatureBlob         real_D3DGetOutputSignatureBlob = nullptr;
static fn_D3DPreprocess                     real_D3DPreprocess = nullptr;
static fn_D3DReflect                        real_D3DReflect = nullptr;
static fn_D3DReturnFailure1                 real_D3DReturnFailure1 = nullptr;
static fn_D3DStripShader                    real_D3DStripShader = nullptr;
static fn_DebugSetMute                      real_DebugSetMute = nullptr;


// ═══════════════════════════════════════════════════════════════════════════
//  Capture state
// ═══════════════════════════════════════════════════════════════════════════

static std::filesystem::path g_capturePath;
static std::filesystem::path g_logPath;
static FILE*                 g_logFile    = nullptr;
// IMPORTANT: Shader capture is DISABLED by default to protect third-party
// shader source code (ENBSeries by Boris Vorontsov). Capturing must be
// explicitly enabled via d3dcompiler_43_proxy.ini [Capture] Enabled=1.
// Captured files are for LOCAL DEBUGGING ONLY and must NEVER be distributed.
static bool                  g_captureEnabled = false;  // OFF by default
static bool                  g_captureHLSL   = true;
static bool                  g_captureDXBC   = true;
static bool                  g_captureDefines = true;
static bool                  g_autoCleanCaptures = true; // delete captures on exit
static std::mutex            g_mutex;
static std::unordered_set<uint64_t> g_capturedHashes;
static int                   g_captureCount = 0;
static int                   g_compileCount = 0;

// ── SkyrimBridge header auto-injection ──────────────────────────────────
static bool                  g_injectEnabled = true;
static bool                  g_injectHeaderExists = false;
static int                   g_injectCount = 0;
static int                   g_injectSkipCount = 0;

// The HLSL snippet prepended to ENB .fx shaders.
// Uses #ifndef guard so shaders that already #include SkyrimBridge_CB.fxh
// don't get duplicate declarations. ENB's ID3DInclude handler resolves
// the #include from the enbseries/ directory.
static const char* const kInjectSnippet =
    "// ── SkyrimBridge auto-injection ──\n"
    "#ifndef SKYRIMBRIDGE_CB_FXH\n"
    "#include \"Helper/SkyrimBridge_CB.fxh\"\n"
    "#endif\n"
    "// ── end SkyrimBridge injection ──\n";


// ═══════════════════════════════════════════════════════════════════════════
//  Logging
// ═══════════════════════════════════════════════════════════════════════════

static void ProxyLog(const char* fmt, ...)
{
    if (!g_logFile) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(g_logFile, "[%04d-%02d-%02d %02d:%02d:%02d.%03d] ",
            st.wYear, st.wMonth, st.wDay,
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list args;
    va_start(args, fmt);
    vfprintf(g_logFile, fmt, args);
    va_end(args);
    fprintf(g_logFile, "\n");
    fflush(g_logFile);
}


// ═══════════════════════════════════════════════════════════════════════════
//  FNV-1a hash + capture
// ═══════════════════════════════════════════════════════════════════════════

static uint64_t FNV1a(const void* data, size_t size, const char* extra = nullptr)
{
    uint64_t hash = 0xcbf29ce484222325ULL;
    auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 0x100000001b3ULL;
    }
    if (extra) {
        for (auto* p = extra; *p; ++p) {
            hash ^= static_cast<uint8_t>(*p);
            hash *= 0x100000001b3ULL;
        }
    }
    return hash;
}

static void CaptureShader(
    LPCSTR pSourceName, LPCSTR pEntrypoint, LPCSTR pTarget,
    const D3D_SHADER_MACRO* pDefines,
    LPCVOID pSrcData, SIZE_T srcSize, ID3DBlob* pCode,
    HRESULT compileResult)
{
    if (!g_captureEnabled || g_capturePath.empty())
        return;
    if (!pSrcData || srcSize == 0)
        return;

    uint64_t hash = FNV1a(pSrcData, srcSize, pEntrypoint);
    if (pTarget)
        hash = FNV1a(pTarget, strlen(pTarget),
                      reinterpret_cast<const char*>(&hash));

    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_capturedHashes.count(hash))
        return;
    g_capturedHashes.insert(hash);

    // Build filename
    std::string baseName;
    if (pSourceName) {
        baseName = pSourceName;
        auto slash = baseName.find_last_of("\\/");
        if (slash != std::string::npos)
            baseName = baseName.substr(slash + 1);
        auto dot = baseName.find_last_of('.');
        if (dot != std::string::npos)
            baseName = baseName.substr(0, dot);
    } else {
        baseName = "memory";
    }
    if (pEntrypoint)
        baseName += std::string("_") + pEntrypoint;

    char hashStr[20];
    snprintf(hashStr, sizeof(hashStr), "_%016llX",
             static_cast<unsigned long long>(hash));
    baseName += hashStr;

    std::error_code ec;
    std::filesystem::create_directories(g_capturePath, ec);

    // Save HLSL
    if (g_captureHLSL) {
        auto hlslPath = g_capturePath / (baseName + ".hlsl");
        std::ofstream ofs(hlslPath, std::ios::binary);
        if (ofs.is_open()) {
            ofs << "// ═══════════════════════════════════════════════════\n";
            ofs << "// Captured by D3DCompiler_43 proxy (SkyrimBridge)\n";
            ofs << "//\n";
            ofs << "// *** DO NOT DISTRIBUTE ***\n";
            ofs << "// This file contains third-party shader source code\n";
            ofs << "// (ENBSeries by Boris Vorontsov). It is captured for\n";
            ofs << "// local debugging ONLY. Redistribution is prohibited.\n";
            ofs << "// *** DO NOT DISTRIBUTE ***\n";
            ofs << "//\n";
            ofs << "// Source: " << (pSourceName ? pSourceName : "<memory>") << "\n";
            ofs << "// Entry:  " << (pEntrypoint ? pEntrypoint : "?") << "\n";
            ofs << "// Target: " << (pTarget ? pTarget : "?") << "\n";
            ofs << "// Size:   " << srcSize << " bytes\n";
            ofs << "// Hash:   0x" << (hashStr + 1) << "\n";
            ofs << "// Result: 0x" << std::hex << compileResult << std::dec
                << (SUCCEEDED(compileResult) ? " (OK)" : " (FAILED)") << "\n";

            if (g_captureDefines && pDefines && pDefines->Name) {
                ofs << "// ── Preprocessor Defines ──────────────────────\n";
                for (auto* d = pDefines; d->Name; ++d) {
                    ofs << "// #define " << d->Name;
                    if (d->Definition && d->Definition[0])
                        ofs << " " << d->Definition;
                    ofs << "\n";
                }
            }
            ofs << "// ═══════════════════════════════════════════════════\n\n";
            ofs.write(static_cast<const char*>(pSrcData), srcSize);
        }
    }

    // Save DXBC
    if (g_captureDXBC && pCode && pCode->GetBufferSize() > 0) {
        auto dxbcPath = g_capturePath / (baseName + ".dxbc");
        std::ofstream ofs(dxbcPath, std::ios::binary);
        if (ofs.is_open()) {
            ofs.write(static_cast<const char*>(pCode->GetBufferPointer()),
                      pCode->GetBufferSize());
        }
    }

    ++g_captureCount;

    // Log summary
    std::string definesSummary;
    if (pDefines) {
        int count = 0;
        for (auto* d = pDefines; d->Name; ++d)
            ++count;
        if (count > 0) {
            definesSummary = " [" + std::to_string(count) + " defines";
            int shown = 0;
            for (auto* d = pDefines; d->Name && shown < 5; ++d, ++shown) {
                definesSummary += shown == 0 ? ": " : ", ";
                definesSummary += d->Name;
                if (d->Definition && d->Definition[0]) {
                    definesSummary += "=";
                    definesSummary += d->Definition;
                }
            }
            if (count > 5)
                definesSummary += ", ...";
            definesSummary += "]";
        }
    }

    ProxyLog("Captured #%d: %s (%zu bytes%s)%s — %s",
             g_captureCount, baseName.c_str(), srcSize,
             pCode ? " + DXBC" : "",
             definesSummary.c_str(),
             SUCCEEDED(compileResult) ? "OK" : "FAILED");
}


// ═══════════════════════════════════════════════════════════════════════════
//  Hooked D3DCompile (the only compile function in D3DCompiler_43)
// ═══════════════════════════════════════════════════════════════════════════

extern "C" __declspec(dllexport) HRESULT WINAPI D3DCompile(
    LPCVOID pSrcData, SIZE_T SrcDataSize,
    LPCSTR pSourceName, const D3D_SHADER_MACRO* pDefines,
    ID3DInclude* pInclude, LPCSTR pEntrypoint,
    LPCSTR pTarget, UINT Flags1, UINT Flags2,
    ID3DBlob** ppCode, ID3DBlob** ppErrorMsgs)
{
    ++g_compileCount;

    // Log every compile call (not just unique captures)
    ProxyLog("D3DCompile #%d: source=%s entry=%s target=%s size=%zu flags=0x%X",
             g_compileCount,
             pSourceName ? pSourceName : "<memory>",
             pEntrypoint ? pEntrypoint : "?",
             pTarget ? pTarget : "?",
             SrcDataSize, Flags1);

    // ── SkyrimBridge header injection ────────────────────────────────
    // For ENB .fx shaders (fx_5_0 target), prepend SkyrimBridge_CB.fxh
    // so all 133 SB_ parameters are available without manual #include.
    std::string injectedSource;
    LPCVOID  compileSrc  = pSrcData;
    SIZE_T   compileSize = SrcDataSize;

    bool shouldInject = g_injectEnabled && g_injectHeaderExists
        && pTarget && strstr(pTarget, "fx_5_0")
        && SrcDataSize > 0;

    if (shouldInject) {
        // Check if source already includes SkyrimBridge (avoid double injection)
        auto srcView = std::string_view(
            static_cast<const char*>(pSrcData),
            std::min(SrcDataSize, static_cast<SIZE_T>(2048)));

        if (srcView.find("SKYRIMBRIDGE_CB_FXH") == std::string_view::npos) {
            injectedSource.reserve(strlen(kInjectSnippet) + SrcDataSize);
            injectedSource.append(kInjectSnippet);
            injectedSource.append(static_cast<const char*>(pSrcData), SrcDataSize);
            compileSrc  = injectedSource.c_str();
            compileSize = injectedSource.size();
            ++g_injectCount;

            if (g_injectCount <= 10) {
                ProxyLog("  INJECTED SkyrimBridge header (%zu → %zu bytes)",
                         SrcDataSize, compileSize);
            }
        } else {
            ++g_injectSkipCount;
            if (g_injectSkipCount <= 5) {
                ProxyLog("  Skipped injection (already has SkyrimBridge)");
            }
        }
    }

    HRESULT hr = real_D3DCompile(compileSrc, compileSize, pSourceName,
        pDefines, pInclude, pEntrypoint, pTarget, Flags1, Flags2,
        ppCode, ppErrorMsgs);

    // Capture uses ORIGINAL source (pre-injection) for clean captures
    CaptureShader(pSourceName, pEntrypoint, pTarget, pDefines,
                  pSrcData, SrcDataSize,
                  (ppCode && *ppCode) ? *ppCode : nullptr, hr);

    // Log errors
    if (FAILED(hr) && ppErrorMsgs && *ppErrorMsgs) {
        auto* errBlob = *ppErrorMsgs;
        ProxyLog("  COMPILE ERROR: %.*s",
                 static_cast<int>(errBlob->GetBufferSize()),
                 static_cast<const char*>(errBlob->GetBufferPointer()));
    }

    return hr;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Forwarding stubs — 16 pass-through exports
// ═══════════════════════════════════════════════════════════════════════════

extern "C" __declspec(dllexport) HRESULT WINAPI D3DAssemble(
    LPCVOID pSrcData, SIZE_T SrcDataSize, LPCSTR pFileName,
    const D3D_SHADER_MACRO* pDefines, ID3DInclude* pInclude, UINT Flags,
    ID3DBlob** ppCode, ID3DBlob** ppErrorMsgs)
{
    return real_D3DAssemble(pSrcData, SrcDataSize, pFileName, pDefines,
                            pInclude, Flags, ppCode, ppErrorMsgs);
}

extern "C" __declspec(dllexport) HRESULT WINAPI DebugSetMute()
{
    return real_DebugSetMute();
}

extern "C" __declspec(dllexport) HRESULT WINAPI D3DCompressShaders(
    UINT uNumShaders, D3D_SHADER_DATA* pShaderData, UINT uFlags,
    ID3DBlob** ppCompressedData)
{
    return real_D3DCompressShaders(uNumShaders, pShaderData, uFlags,
                                   ppCompressedData);
}

extern "C" __declspec(dllexport) HRESULT WINAPI D3DCreateBlob(
    SIZE_T Size, ID3DBlob** ppBlob)
{
    return real_D3DCreateBlob(Size, ppBlob);
}

extern "C" __declspec(dllexport) HRESULT WINAPI D3DDecompressShaders(
    LPCVOID pSrcData, SIZE_T SrcDataSize, UINT uNumShaders,
    UINT uStartIndex, UINT* pIndices, UINT uFlags,
    ID3DBlob** ppShaders, UINT* pTotalShaders)
{
    return real_D3DDecompressShaders(pSrcData, SrcDataSize, uNumShaders,
        uStartIndex, pIndices, uFlags, ppShaders, pTotalShaders);
}

extern "C" __declspec(dllexport) HRESULT WINAPI D3DDisassemble(
    LPCVOID pSrcData, SIZE_T SrcDataSize, UINT Flags,
    LPCSTR szComments, ID3DBlob** ppDisassembly)
{
    return real_D3DDisassemble(pSrcData, SrcDataSize, Flags, szComments,
                               ppDisassembly);
}

extern "C" __declspec(dllexport) HRESULT WINAPI D3DDisassemble10Effect(
    void* pEffect, UINT Flags, ID3DBlob** ppDisassembly)
{
    return real_D3DDisassemble10Effect(pEffect, Flags, ppDisassembly);
}

extern "C" __declspec(dllexport) HRESULT WINAPI D3DGetBlobPart(
    LPCVOID pSrcData, SIZE_T SrcDataSize, D3D_BLOB_PART Part, UINT Flags,
    ID3DBlob** ppPart)
{
    return real_D3DGetBlobPart(pSrcData, SrcDataSize, Part, Flags, ppPart);
}

extern "C" __declspec(dllexport) HRESULT WINAPI D3DGetDebugInfo(
    LPCVOID pSrcData, SIZE_T SrcDataSize, ID3DBlob** ppDebugInfo)
{
    return real_D3DGetDebugInfo(pSrcData, SrcDataSize, ppDebugInfo);
}

extern "C" __declspec(dllexport) HRESULT WINAPI D3DGetInputAndOutputSignatureBlob(
    LPCVOID pSrcData, SIZE_T SrcDataSize, ID3DBlob** ppSignatureBlob)
{
    return real_D3DGetInputAndOutputSignatureBlob(pSrcData, SrcDataSize,
                                                   ppSignatureBlob);
}

extern "C" __declspec(dllexport) HRESULT WINAPI D3DGetInputSignatureBlob(
    LPCVOID pSrcData, SIZE_T SrcDataSize, ID3DBlob** ppSignatureBlob)
{
    return real_D3DGetInputSignatureBlob(pSrcData, SrcDataSize,
                                          ppSignatureBlob);
}

extern "C" __declspec(dllexport) HRESULT WINAPI D3DGetOutputSignatureBlob(
    LPCVOID pSrcData, SIZE_T SrcDataSize, ID3DBlob** ppSignatureBlob)
{
    return real_D3DGetOutputSignatureBlob(pSrcData, SrcDataSize,
                                           ppSignatureBlob);
}

extern "C" __declspec(dllexport) HRESULT WINAPI D3DPreprocess(
    LPCVOID pSrcData, SIZE_T SrcDataSize, LPCSTR pSourceName,
    const D3D_SHADER_MACRO* pDefines, ID3DInclude* pInclude,
    ID3DBlob** ppCodeText, ID3DBlob** ppErrorMsgs)
{
    return real_D3DPreprocess(pSrcData, SrcDataSize, pSourceName, pDefines,
                              pInclude, ppCodeText, ppErrorMsgs);
}

extern "C" __declspec(dllexport) HRESULT WINAPI D3DReflect(
    LPCVOID pSrcData, SIZE_T SrcDataSize, REFIID pInterface, void** ppReflector)
{
    return real_D3DReflect(pSrcData, SrcDataSize, pInterface, ppReflector);
}

extern "C" __declspec(dllexport) HRESULT WINAPI D3DReturnFailure1()
{
    return real_D3DReturnFailure1();
}

extern "C" __declspec(dllexport) HRESULT WINAPI D3DStripShader(
    LPCVOID pShaderBytecode, SIZE_T BytecodeLength, UINT uStripFlags,
    ID3DBlob** ppStrippedBlob)
{
    return real_D3DStripShader(pShaderBytecode, BytecodeLength, uStripFlags,
                               ppStrippedBlob);
}


// ═══════════════════════════════════════════════════════════════════════════
//  INI config
// ═══════════════════════════════════════════════════════════════════════════

static void LoadConfig(const std::filesystem::path& iniPath)
{
    char buf[MAX_PATH];
    GetPrivateProfileStringA("Capture", "Directory",
        "Data\\SKSE\\Plugins\\SkyrimBridge\\ENBShaderCapture",
        buf, sizeof(buf), iniPath.string().c_str());
    std::filesystem::path dir(buf);
    if (dir.is_relative()) {
        char exePath[MAX_PATH];
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        dir = std::filesystem::path(exePath).parent_path() / dir;
    }
    g_capturePath = dir;

    g_captureEnabled = GetPrivateProfileIntA("Capture", "Enabled", 1,
        iniPath.string().c_str()) != 0;
    g_captureHLSL = GetPrivateProfileIntA("Capture", "SaveHLSL", 1,
        iniPath.string().c_str()) != 0;
    g_captureDXBC = GetPrivateProfileIntA("Capture", "SaveDXBC", 1,
        iniPath.string().c_str()) != 0;
    g_captureDefines = GetPrivateProfileIntA("Capture", "SaveDefines", 1,
        iniPath.string().c_str()) != 0;

    g_autoCleanCaptures = GetPrivateProfileIntA("Capture", "AutoClean", 1,
        iniPath.string().c_str()) != 0;

    g_injectEnabled = GetPrivateProfileIntA("Injection", "Enabled", 1,
        iniPath.string().c_str()) != 0;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Resolve exports
// ═══════════════════════════════════════════════════════════════════════════

#define RESOLVE(name) \
    real_##name = reinterpret_cast<fn_##name>(GetProcAddress(g_realDll, #name))

static bool ResolveExports()
{
    RESOLVE(D3DAssemble);
    RESOLVE(D3DCompile);
    RESOLVE(D3DCompressShaders);
    RESOLVE(D3DCreateBlob);
    RESOLVE(D3DDecompressShaders);
    RESOLVE(D3DDisassemble);
    RESOLVE(D3DDisassemble10Effect);
    RESOLVE(D3DGetBlobPart);
    RESOLVE(D3DGetDebugInfo);
    RESOLVE(D3DGetInputAndOutputSignatureBlob);
    RESOLVE(D3DGetInputSignatureBlob);
    RESOLVE(D3DGetOutputSignatureBlob);
    RESOLVE(D3DPreprocess);
    RESOLVE(D3DReflect);
    RESOLVE(D3DReturnFailure1);
    RESOLVE(D3DStripShader);
    RESOLVE(DebugSetMute);

    // Override D3DCompile with ENB's d3dcompiler_46e if available
    // (System32's D3DCompiler_43 is too old for SM5.0 shaders)
    if (g_enbCompiler) {
        auto enbCompile = reinterpret_cast<fn_D3DCompile>(
            GetProcAddress(g_enbCompiler, "D3DCompile"));
        if (enbCompile)
            real_D3DCompile = enbCompile;
    }

    return real_D3DCompile != nullptr;
}

#undef RESOLVE


// ═══════════════════════════════════════════════════════════════════════════
//  DllMain — load real D3DCompiler_43.dll from System32
// ═══════════════════════════════════════════════════════════════════════════

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hinstDLL);

        // ── Load the REAL D3DCompiler_43.dll from System32 ───────────
        char sysDir[MAX_PATH];
        GetSystemDirectoryA(sysDir, MAX_PATH);
        std::string realPath = std::string(sysDir) + "\\D3DCompiler_43.dll";
        g_realDll = LoadLibraryA(realPath.c_str());
        if (!g_realDll)
            return FALSE;

        // ── Load ENB's d3dcompiler_46e.dll for actual compilation ────
        // ENB ships this as its SM5.0 capable compiler. We route
        // D3DCompile through it instead of the ancient System32 version.
        g_enbCompiler = GetModuleHandleA("d3dcompiler_46e.dll");
        if (!g_enbCompiler) {
            // Not yet loaded — try loading from exe directory
            char exeBuf[MAX_PATH];
            GetModuleFileNameA(nullptr, exeBuf, MAX_PATH);
            auto enbCompPath = std::filesystem::path(exeBuf).parent_path()
                               / "d3dcompiler_46e.dll";
            g_enbCompiler = LoadLibraryA(enbCompPath.string().c_str());
        }

        if (!ResolveExports()) {
            FreeLibrary(g_realDll);
            g_realDll = nullptr;
            return FALSE;
        }

        // ── Set up paths ─────────────────────────────────────────────
        char exePath[MAX_PATH];
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        auto exeDir = std::filesystem::path(exePath).parent_path();

        // ── Check if SkyrimBridge shader header exists on disk ─────
        auto sbHeaderPath = exeDir / "enbseries" / "Helper" / "SkyrimBridge_CB.fxh";
        g_injectHeaderExists = std::filesystem::exists(sbHeaderPath);

        auto iniPath = exeDir / "d3dcompiler_43_proxy.ini";
        if (std::filesystem::exists(iniPath)) {
            LoadConfig(iniPath);
        } else {
            // Write captures to user Documents to survive MO2 Stock Game cleanup
            char docs[MAX_PATH];
            if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, 0, docs))) {
                g_capturePath = std::filesystem::path(docs)
                    / "My Games" / "Skyrim Special Edition"
                    / "SKSE" / "SkyrimBridge" / "ENBShaderCapture";
            } else {
                g_capturePath = exeDir / "Data" / "SKSE" / "Plugins"
                               / "SkyrimBridge" / "ENBShaderCapture";
            }
        }

        // ── Open log (same persistent directory as captures) ─────────
        g_logPath = g_capturePath.parent_path() / "d3dcompiler_43_proxy.log";
        std::error_code ec;
        std::filesystem::create_directories(g_logPath.parent_path(), ec);
        g_logFile = fopen(g_logPath.string().c_str(), "w");

        ProxyLog("═══════════════════════════════════════════════");
        ProxyLog("D3DCompiler_43 proxy loaded (SkyrimBridge ENB shader capture)");
        ProxyLog("Real DLL:     %s", realPath.c_str());
        ProxyLog("ENB compiler: %s", g_enbCompiler ? "d3dcompiler_46e.dll (SM5.0)" : "NOT FOUND — using System32 fallback");
        ProxyLog("Capture:      %s", g_capturePath.string().c_str());
        ProxyLog("SB Header: %s (%s)",
                 g_injectHeaderExists ? "FOUND" : "NOT FOUND",
                 sbHeaderPath.string().c_str());
        ProxyLog("Injection: %s",
                 (g_injectEnabled && g_injectHeaderExists) ? "ACTIVE — all ENB shaders get SB_ params"
                 : !g_injectEnabled ? "DISABLED by config"
                 : "INACTIVE — SkyrimBridge_CB.fxh not deployed");
        ProxyLog("Options:  enabled=%s hlsl=%s dxbc=%s defines=%s autoclean=%s",
                 g_captureEnabled ? "yes" : "no",
                 g_captureHLSL ? "yes" : "no",
                 g_captureDXBC ? "yes" : "no",
                 g_captureDefines ? "yes" : "no",
                 g_autoCleanCaptures ? "yes" : "no");
        ProxyLog("═══════════════════════════════════════════════");
    }
    else if (fdwReason == DLL_PROCESS_DETACH)
    {
        // ── Auto-clean captured shader files (IP protection) ────────
        // Deletes .hlsl and .dxbc captures on exit to prevent
        // accidental distribution of third-party shader source code.
        // Disable via d3dcompiler_43_proxy.ini [Capture] AutoClean=0
        int cleanedCount = 0;
        if (g_autoCleanCaptures && !g_capturePath.empty()) {
            std::error_code ec;
            if (std::filesystem::exists(g_capturePath, ec)) {
                for (auto& entry : std::filesystem::directory_iterator(g_capturePath, ec)) {
                    if (!entry.is_regular_file(ec)) continue;
                    auto ext = entry.path().extension().string();
                    if (ext == ".hlsl" || ext == ".dxbc") {
                        std::filesystem::remove(entry.path(), ec);
                        if (!ec) ++cleanedCount;
                    }
                }
                // Remove capture directory if empty
                if (std::filesystem::is_empty(g_capturePath, ec))
                    std::filesystem::remove(g_capturePath, ec);
            }
        }

        if (g_logFile) {
            ProxyLog("═══════════════════════════════════════════════");
            ProxyLog("Proxy unloading — %d compilations, %d captured, %d injected, %d skipped",
                     g_compileCount, g_captureCount, g_injectCount, g_injectSkipCount);
            if (cleanedCount > 0)
                ProxyLog("Auto-cleaned %d captured shader files (IP protection)", cleanedCount);
            ProxyLog("═══════════════════════════════════════════════");
            fclose(g_logFile);
            g_logFile = nullptr;
        }
        if (g_realDll) {
            FreeLibrary(g_realDll);
            g_realDll = nullptr;
        }
        // Don't FreeLibrary g_enbCompiler — if we got it via
        // GetModuleHandle, we don't own the reference.
    }

    return TRUE;
}
