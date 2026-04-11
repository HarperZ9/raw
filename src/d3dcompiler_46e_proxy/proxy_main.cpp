// ═══════════════════════════════════════════════════════════════════════════
//  d3dcompiler_46e.dll — Proxy DLL for ENB Shader Capture
//
//  ENB v504 uses Boris's custom d3dcompiler_46e.dll (not the system
//  d3dcompiler_47.dll).  This proxy intercepts ALL D3DCompile/D3DCompile2
//  calls from ENB and captures the preprocessed HLSL + compiled DXBC.
//
//  Deployment:
//    1. Rename the original d3dcompiler_46e.dll → d3dcompiler_46e_real.dll
//    2. Place this proxy DLL as d3dcompiler_46e.dll next to it
//  Both files must be in the same directory (game ROOT via MO2).
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

// Cannot #include <d3dcompiler.h> — dllimport conflicts with our dllexport.
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
#include <sstream>


// ═══════════════════════════════════════════════════════════════════════════
//  Real DLL handle + function pointers
// ═══════════════════════════════════════════════════════════════════════════

static HMODULE g_realDll = nullptr;

using fn_D3DAssemble = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR,
    const D3D_SHADER_MACRO*, ID3DInclude*, UINT, ID3DBlob**, ID3DBlob**);
using fn_D3DCompile = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR,
    const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT,
    ID3DBlob**, ID3DBlob**);
using fn_D3DCompile2 = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR,
    const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT,
    UINT, LPCVOID, SIZE_T, ID3DBlob**, ID3DBlob**);
using fn_D3DCompileFromFile = HRESULT(WINAPI*)(LPCWSTR,
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
using fn_D3DDisassemble11Trace = HRESULT(WINAPI*)(LPCVOID, SIZE_T, void*,
    UINT, UINT, UINT, ID3DBlob**);
using fn_D3DDisassembleRegion = HRESULT(WINAPI*)(LPCVOID, SIZE_T, UINT,
    LPCSTR, SIZE_T, SIZE_T, SIZE_T*, ID3DBlob**);
using fn_D3DGetBlobPart = HRESULT(WINAPI*)(LPCVOID, SIZE_T,
    D3D_BLOB_PART, UINT, ID3DBlob**);
using fn_D3DGetDebugInfo = HRESULT(WINAPI*)(LPCVOID, SIZE_T, ID3DBlob**);
using fn_D3DGetInputAndOutputSignatureBlob = HRESULT(WINAPI*)(LPCVOID,
    SIZE_T, ID3DBlob**);
using fn_D3DGetInputSignatureBlob = HRESULT(WINAPI*)(LPCVOID, SIZE_T,
    ID3DBlob**);
using fn_D3DGetOutputSignatureBlob = HRESULT(WINAPI*)(LPCVOID, SIZE_T,
    ID3DBlob**);
using fn_D3DGetTraceInstructionOffsets = HRESULT(WINAPI*)(LPCVOID, SIZE_T,
    UINT, SIZE_T, SIZE_T, SIZE_T*, SIZE_T*);
using fn_D3DPreprocess = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR,
    const D3D_SHADER_MACRO*, ID3DInclude*, ID3DBlob**, ID3DBlob**);
using fn_D3DReadFileToBlob = HRESULT(WINAPI*)(LPCWSTR, ID3DBlob**);
using fn_D3DReflect = HRESULT(WINAPI*)(LPCVOID, SIZE_T, REFIID, void**);
using fn_D3DReturnFailure1 = HRESULT(WINAPI*)();
using fn_D3DSetBlobPart = HRESULT(WINAPI*)(LPCVOID, SIZE_T,
    D3D_BLOB_PART, UINT, LPCVOID, SIZE_T, ID3DBlob**);
using fn_D3DStripShader = HRESULT(WINAPI*)(LPCVOID, SIZE_T, UINT,
    ID3DBlob**);
using fn_D3DWriteBlobToFile = HRESULT(WINAPI*)(ID3DBlob*, LPCWSTR, BOOL);
using fn_DebugSetMute = HRESULT(WINAPI*)();

static fn_D3DAssemble                       real_D3DAssemble = nullptr;
static fn_D3DCompile                        real_D3DCompile = nullptr;
static fn_D3DCompile2                       real_D3DCompile2 = nullptr;
static fn_D3DCompileFromFile                real_D3DCompileFromFile = nullptr;
static fn_D3DCompressShaders                real_D3DCompressShaders = nullptr;
static fn_D3DCreateBlob                     real_D3DCreateBlob = nullptr;
static fn_D3DDecompressShaders              real_D3DDecompressShaders = nullptr;
static fn_D3DDisassemble                    real_D3DDisassemble = nullptr;
static fn_D3DDisassemble10Effect            real_D3DDisassemble10Effect = nullptr;
static fn_D3DDisassemble11Trace             real_D3DDisassemble11Trace = nullptr;
static fn_D3DDisassembleRegion              real_D3DDisassembleRegion = nullptr;
static fn_D3DGetBlobPart                    real_D3DGetBlobPart = nullptr;
static fn_D3DGetDebugInfo                   real_D3DGetDebugInfo = nullptr;
static fn_D3DGetInputAndOutputSignatureBlob real_D3DGetInputAndOutputSignatureBlob = nullptr;
static fn_D3DGetInputSignatureBlob          real_D3DGetInputSignatureBlob = nullptr;
static fn_D3DGetOutputSignatureBlob         real_D3DGetOutputSignatureBlob = nullptr;
static fn_D3DGetTraceInstructionOffsets     real_D3DGetTraceInstructionOffsets = nullptr;
static fn_D3DPreprocess                     real_D3DPreprocess = nullptr;
static fn_D3DReadFileToBlob                 real_D3DReadFileToBlob = nullptr;
static fn_D3DReflect                        real_D3DReflect = nullptr;
static fn_D3DReturnFailure1                 real_D3DReturnFailure1 = nullptr;
static fn_D3DSetBlobPart                    real_D3DSetBlobPart = nullptr;
static fn_D3DStripShader                    real_D3DStripShader = nullptr;
static fn_D3DWriteBlobToFile                real_D3DWriteBlobToFile = nullptr;
static fn_DebugSetMute                      real_DebugSetMute = nullptr;


// ═══════════════════════════════════════════════════════════════════════════
//  Capture state
// ═══════════════════════════════════════════════════════════════════════════

static std::filesystem::path g_capturePath;
static std::filesystem::path g_logPath;
static FILE*                 g_logFile    = nullptr;
// IMPORTANT: Shader capture is DISABLED by default to protect third-party
// shader source code (ENBSeries by Boris Vorontsov). Capturing must be
// explicitly enabled via d3dcompiler_46e_proxy.ini [Capture] Enabled=1.
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

// Format D3D_SHADER_MACRO defines for logging/capture
static std::string FormatDefines(const D3D_SHADER_MACRO* pDefines)
{
    std::string result;
    if (!pDefines) return result;
    for (auto* d = pDefines; d->Name; ++d) {
        result += "#define ";
        result += d->Name;
        if (d->Definition && d->Definition[0]) {
            result += " ";
            result += d->Definition;
        }
        result += "\n";
    }
    return result;
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

    // Hash source + entrypoint + target + defines for dedup
    uint64_t hash = FNV1a(pSrcData, srcSize, pEntrypoint);
    if (pTarget)
        hash = FNV1a(pTarget, strlen(pTarget),
                      reinterpret_cast<const char*>(&hash));

    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_capturedHashes.count(hash))
        return;
    g_capturedHashes.insert(hash);

    // Build filename: sourceName_entryPoint_HASH
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

    // Save HLSL (with preprocessor defines + metadata header)
    if (g_captureHLSL) {
        auto hlslPath = g_capturePath / (baseName + ".hlsl");
        std::ofstream ofs(hlslPath, std::ios::binary);
        if (ofs.is_open()) {
            ofs << "// ═══════════════════════════════════════════════════\n";
            ofs << "// Captured by d3dcompiler_46e proxy (SkyrimBridge)\n";
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

            // Log defines (critical for ENB — shows IMPROVE_TAA, E_SSAO_*, etc.)
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

    // Save DXBC bytecode
    if (g_captureDXBC && pCode && pCode->GetBufferSize() > 0) {
        auto dxbcPath = g_capturePath / (baseName + ".dxbc");
        std::ofstream ofs(dxbcPath, std::ios::binary);
        if (ofs.is_open()) {
            ofs.write(static_cast<const char*>(pCode->GetBufferPointer()),
                      pCode->GetBufferSize());
        }
    }

    ++g_captureCount;

    // Log a summary line with defines
    std::string definesSummary;
    if (pDefines) {
        int count = 0;
        for (auto* d = pDefines; d->Name; ++d)
            ++count;
        if (count > 0) {
            definesSummary = " [" + std::to_string(count) + " defines";
            // List first few
            int shown = 0;
            for (auto* d = pDefines; d->Name && shown < 4; ++d, ++shown) {
                definesSummary += shown == 0 ? ": " : ", ";
                definesSummary += d->Name;
            }
            if (count > 4)
                definesSummary += ", ...";
            definesSummary += "]";
        }
    }

    ProxyLog("Captured #%d: %s (%zu bytes HLSL%s)%s — %s",
             g_captureCount, baseName.c_str(), srcSize,
             pCode ? " + DXBC" : "",
             definesSummary.c_str(),
             SUCCEEDED(compileResult) ? "OK" : "FAILED");
}


// ═══════════════════════════════════════════════════════════════════════════
//  Hooked exports — D3DCompile and D3DCompile2
// ═══════════════════════════════════════════════════════════════════════════

extern "C" __declspec(dllexport) HRESULT WINAPI D3DCompile(
    LPCVOID pSrcData, SIZE_T SrcDataSize,
    LPCSTR pSourceName, const D3D_SHADER_MACRO* pDefines,
    ID3DInclude* pInclude, LPCSTR pEntrypoint,
    LPCSTR pTarget, UINT Flags1, UINT Flags2,
    ID3DBlob** ppCode, ID3DBlob** ppErrorMsgs)
{
    ++g_compileCount;

    HRESULT hr = real_D3DCompile(pSrcData, SrcDataSize, pSourceName,
        pDefines, pInclude, pEntrypoint, pTarget, Flags1, Flags2,
        ppCode, ppErrorMsgs);

    CaptureShader(pSourceName, pEntrypoint, pTarget, pDefines,
                  pSrcData, SrcDataSize,
                  (ppCode && *ppCode) ? *ppCode : nullptr, hr);

    return hr;
}

extern "C" __declspec(dllexport) HRESULT WINAPI D3DCompile2(
    LPCVOID pSrcData, SIZE_T SrcDataSize,
    LPCSTR pSourceName, const D3D_SHADER_MACRO* pDefines,
    ID3DInclude* pInclude, LPCSTR pEntrypoint,
    LPCSTR pTarget, UINT Flags1, UINT Flags2,
    UINT SecondaryDataFlags, LPCVOID pSecondaryData,
    SIZE_T SecondaryDataSize,
    ID3DBlob** ppCode, ID3DBlob** ppErrorMsgs)
{
    ++g_compileCount;

    HRESULT hr = real_D3DCompile2(pSrcData, SrcDataSize, pSourceName,
        pDefines, pInclude, pEntrypoint, pTarget, Flags1, Flags2,
        SecondaryDataFlags, pSecondaryData, SecondaryDataSize,
        ppCode, ppErrorMsgs);

    CaptureShader(pSourceName, pEntrypoint, pTarget, pDefines,
                  pSrcData, SrcDataSize,
                  (ppCode && *ppCode) ? *ppCode : nullptr, hr);

    return hr;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Forwarding stubs — 23 pass-through exports
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

extern "C" __declspec(dllexport) HRESULT WINAPI D3DCompileFromFile(
    LPCWSTR pFileName, const D3D_SHADER_MACRO* pDefines,
    ID3DInclude* pInclude, LPCSTR pEntrypoint, LPCSTR pTarget,
    UINT Flags1, UINT Flags2, ID3DBlob** ppCode, ID3DBlob** ppErrorMsgs)
{
    return real_D3DCompileFromFile(pFileName, pDefines, pInclude,
        pEntrypoint, pTarget, Flags1, Flags2, ppCode, ppErrorMsgs);
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

extern "C" __declspec(dllexport) HRESULT WINAPI D3DDisassemble11Trace(
    LPCVOID pSrcData, SIZE_T SrcDataSize, void* pTrace,
    UINT StartStep, UINT NumSteps, UINT Flags, ID3DBlob** ppDisassembly)
{
    return real_D3DDisassemble11Trace(pSrcData, SrcDataSize, pTrace,
        StartStep, NumSteps, Flags, ppDisassembly);
}

extern "C" __declspec(dllexport) HRESULT WINAPI D3DDisassembleRegion(
    LPCVOID pSrcData, SIZE_T SrcDataSize, UINT Flags, LPCSTR szComments,
    SIZE_T StartByteOffset, SIZE_T NumInsts,
    SIZE_T* pFinishByteOffset, ID3DBlob** ppDisassembly)
{
    return real_D3DDisassembleRegion(pSrcData, SrcDataSize, Flags, szComments,
        StartByteOffset, NumInsts, pFinishByteOffset, ppDisassembly);
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

extern "C" __declspec(dllexport) HRESULT WINAPI D3DGetTraceInstructionOffsets(
    LPCVOID pSrcData, SIZE_T SrcDataSize, UINT Flags,
    SIZE_T StartInstIndex, SIZE_T NumInsts,
    SIZE_T* pOffsets, SIZE_T* pTotalInsts)
{
    return real_D3DGetTraceInstructionOffsets(pSrcData, SrcDataSize, Flags,
        StartInstIndex, NumInsts, pOffsets, pTotalInsts);
}

extern "C" __declspec(dllexport) HRESULT WINAPI D3DPreprocess(
    LPCVOID pSrcData, SIZE_T SrcDataSize, LPCSTR pSourceName,
    const D3D_SHADER_MACRO* pDefines, ID3DInclude* pInclude,
    ID3DBlob** ppCodeText, ID3DBlob** ppErrorMsgs)
{
    return real_D3DPreprocess(pSrcData, SrcDataSize, pSourceName, pDefines,
                              pInclude, ppCodeText, ppErrorMsgs);
}

extern "C" __declspec(dllexport) HRESULT WINAPI D3DReadFileToBlob(
    LPCWSTR pFileName, ID3DBlob** ppContents)
{
    return real_D3DReadFileToBlob(pFileName, ppContents);
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

extern "C" __declspec(dllexport) HRESULT WINAPI D3DSetBlobPart(
    LPCVOID pSrcData, SIZE_T SrcDataSize, D3D_BLOB_PART Part, UINT Flags,
    LPCVOID pPart, SIZE_T PartSize, ID3DBlob** ppNewShader)
{
    return real_D3DSetBlobPart(pSrcData, SrcDataSize, Part, Flags, pPart,
                               PartSize, ppNewShader);
}

extern "C" __declspec(dllexport) HRESULT WINAPI D3DStripShader(
    LPCVOID pShaderBytecode, SIZE_T BytecodeLength, UINT uStripFlags,
    ID3DBlob** ppStrippedBlob)
{
    return real_D3DStripShader(pShaderBytecode, BytecodeLength, uStripFlags,
                               ppStrippedBlob);
}

extern "C" __declspec(dllexport) HRESULT WINAPI D3DWriteBlobToFile(
    ID3DBlob* pBlob, LPCWSTR pFileName, BOOL bOverwrite)
{
    return real_D3DWriteBlobToFile(pBlob, pFileName, bOverwrite);
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
}


// ═══════════════════════════════════════════════════════════════════════════
//  Resolve all real function pointers
// ═══════════════════════════════════════════════════════════════════════════

#define RESOLVE(name) \
    real_##name = reinterpret_cast<fn_##name>(GetProcAddress(g_realDll, #name))

static bool ResolveExports()
{
    RESOLVE(D3DAssemble);
    RESOLVE(D3DCompile);
    RESOLVE(D3DCompile2);
    RESOLVE(D3DCompileFromFile);
    RESOLVE(D3DCompressShaders);
    RESOLVE(D3DCreateBlob);
    RESOLVE(D3DDecompressShaders);
    RESOLVE(D3DDisassemble);
    RESOLVE(D3DDisassemble10Effect);
    RESOLVE(D3DDisassemble11Trace);
    RESOLVE(D3DDisassembleRegion);
    RESOLVE(D3DGetBlobPart);
    RESOLVE(D3DGetDebugInfo);
    RESOLVE(D3DGetInputAndOutputSignatureBlob);
    RESOLVE(D3DGetInputSignatureBlob);
    RESOLVE(D3DGetOutputSignatureBlob);
    RESOLVE(D3DGetTraceInstructionOffsets);
    RESOLVE(D3DPreprocess);
    RESOLVE(D3DReadFileToBlob);
    RESOLVE(D3DReflect);
    RESOLVE(D3DReturnFailure1);
    RESOLVE(D3DSetBlobPart);
    RESOLVE(D3DStripShader);
    RESOLVE(D3DWriteBlobToFile);
    RESOLVE(DebugSetMute);

    return real_D3DCompile != nullptr;
}

#undef RESOLVE


// ═══════════════════════════════════════════════════════════════════════════
//  DllMain — load the REAL d3dcompiler_46e from d3dcompiler_46e_real.dll
// ═══════════════════════════════════════════════════════════════════════════

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hinstDLL);

        // Find our own directory
        char selfPath[MAX_PATH];
        GetModuleFileNameA(hinstDLL, selfPath, MAX_PATH);
        auto selfDir = std::filesystem::path(selfPath).parent_path();

        // ── Load the REAL d3dcompiler_46e.dll (renamed to _real) ─────
        auto realPath = selfDir / "d3dcompiler_46e_real.dll";
        g_realDll = LoadLibraryA(realPath.string().c_str());

        if (!g_realDll) {
            // Fallback: try loading from one directory up (in case of
            // unusual deployment layouts)
            realPath = selfDir.parent_path() / "d3dcompiler_46e_real.dll";
            g_realDll = LoadLibraryA(realPath.string().c_str());
        }

        if (!g_realDll) {
            // Fatal — cannot function without the real compiler
            char msg[512];
            snprintf(msg, sizeof(msg),
                "d3dcompiler_46e proxy: Cannot find d3dcompiler_46e_real.dll\n"
                "Searched: %s\n\n"
                "Rename the original d3dcompiler_46e.dll to "
                "d3dcompiler_46e_real.dll and place it next to this proxy.",
                (selfDir / "d3dcompiler_46e_real.dll").string().c_str());
            MessageBoxA(nullptr, msg, "SkyrimBridge Shader Capture", MB_OK | MB_ICONERROR);
            return FALSE;
        }

        if (!ResolveExports()) {
            FreeLibrary(g_realDll);
            g_realDll = nullptr;
            MessageBoxA(nullptr,
                "d3dcompiler_46e proxy: Failed to resolve D3DCompile export "
                "from d3dcompiler_46e_real.dll",
                "SkyrimBridge Shader Capture", MB_OK | MB_ICONERROR);
            return FALSE;
        }

        // ── Set up paths ─────────────────────────────────────────────
        char exePath[MAX_PATH];
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        auto exeDir = std::filesystem::path(exePath).parent_path();

        // Check for INI config
        auto iniPath = exeDir / "d3dcompiler_46e_proxy.ini";
        if (std::filesystem::exists(iniPath)) {
            LoadConfig(iniPath);
        } else {
            g_capturePath = exeDir / "Data" / "SKSE" / "Plugins"
                           / "SkyrimBridge" / "ENBShaderCapture";
        }

        // ── Open log ─────────────────────────────────────────────────
        g_logPath = exeDir / "Data" / "SKSE" / "Plugins"
                   / "SkyrimBridge" / "d3dcompiler_46e_proxy.log";
        std::error_code ec;
        std::filesystem::create_directories(g_logPath.parent_path(), ec);
        g_logFile = fopen(g_logPath.string().c_str(), "w");

        ProxyLog("═══════════════════════════════════════════════");
        ProxyLog("d3dcompiler_46e proxy loaded (SkyrimBridge ENB shader capture)");
        ProxyLog("Proxy DLL: %s", selfPath);
        ProxyLog("Real DLL:  %s", realPath.string().c_str());
        ProxyLog("Capture:   %s", g_capturePath.string().c_str());
        ProxyLog("Options:   enabled=%s hlsl=%s dxbc=%s defines=%s",
                 g_captureEnabled ? "yes" : "no",
                 g_captureHLSL ? "yes" : "no",
                 g_captureDXBC ? "yes" : "no",
                 g_captureDefines ? "yes" : "no");
        ProxyLog("═══════════════════════════════════════════════");
    }
    else if (fdwReason == DLL_PROCESS_DETACH)
    {
        // ── Auto-clean captured shader files (IP protection) ────────
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
                if (std::filesystem::is_empty(g_capturePath, ec))
                    std::filesystem::remove(g_capturePath, ec);
            }
        }

        if (g_logFile) {
            ProxyLog("═══════════════════════════════════════════════");
            ProxyLog("Proxy unloading — %d total compilations, %d unique captured",
                     g_compileCount, g_captureCount);
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
    }

    return TRUE;
}
