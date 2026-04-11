// ═══════════════════════════════════════════════════════════════════════════
//  d3dcompiler_47.dll — Proxy DLL for Shader Capture
//
//  Intercepts ALL D3DCompile/D3DCompile2 calls and captures the HLSL source
//  + compiled DXBC to disk.  Loads before ENB, SKSE, or any game code.
//
//  Place in the game directory (next to SkyrimSE.exe) to activate.
//  Remove or rename to disable.
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

// We cannot #include <d3dcompiler.h> because it declares functions with
// __declspec(dllimport) which conflicts with our __declspec(dllexport).
// Instead, pull in only the COM types we need and define the rest ourselves.
#include <d3d11.h>  // For ID3DBlob, D3D_SHADER_MACRO, ID3DInclude, REFIID

// Types from d3dcompiler.h that we need
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

// ═══════════════════════════════════════════════════════════════════════════
//  Real DLL handle + function pointers
// ═══════════════════════════════════════════════════════════════════════════

static HMODULE g_realDll = nullptr;

// Function pointer types
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
using fn_D3DCreateFunctionLinkingGraph = HRESULT(WINAPI*)(UINT, void**);
using fn_D3DCreateLinker = HRESULT(WINAPI*)(void**);
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
using fn_D3DLoadModule = HRESULT(WINAPI*)(LPCVOID, SIZE_T, void**);
using fn_D3DPreprocess = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR,
    const D3D_SHADER_MACRO*, ID3DInclude*, ID3DBlob**, ID3DBlob**);
using fn_D3DReadFileToBlob = HRESULT(WINAPI*)(LPCWSTR, ID3DBlob**);
using fn_D3DReflect = HRESULT(WINAPI*)(LPCVOID, SIZE_T, REFIID, void**);
using fn_D3DReflectLibrary = HRESULT(WINAPI*)(LPCVOID, SIZE_T, REFIID, void**);
using fn_D3DReturnFailure1 = HRESULT(WINAPI*)();
using fn_D3DSetBlobPart = HRESULT(WINAPI*)(LPCVOID, SIZE_T,
    D3D_BLOB_PART, UINT, LPCVOID, SIZE_T, ID3DBlob**);
using fn_D3DStripShader = HRESULT(WINAPI*)(LPCVOID, SIZE_T, UINT,
    ID3DBlob**);
using fn_D3DWriteBlobToFile = HRESULT(WINAPI*)(ID3DBlob*, LPCWSTR, BOOL);
using fn_DebugSetMute = HRESULT(WINAPI*)();

// Real function pointers (resolved at load time)
static fn_D3DAssemble                       real_D3DAssemble = nullptr;
static fn_D3DCompile                        real_D3DCompile = nullptr;
static fn_D3DCompile2                       real_D3DCompile2 = nullptr;
static fn_D3DCompileFromFile                real_D3DCompileFromFile = nullptr;
static fn_D3DCompressShaders                real_D3DCompressShaders = nullptr;
static fn_D3DCreateBlob                     real_D3DCreateBlob = nullptr;
static fn_D3DCreateFunctionLinkingGraph     real_D3DCreateFunctionLinkingGraph = nullptr;
static fn_D3DCreateLinker                   real_D3DCreateLinker = nullptr;
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
static fn_D3DLoadModule                     real_D3DLoadModule = nullptr;
static fn_D3DPreprocess                     real_D3DPreprocess = nullptr;
static fn_D3DReadFileToBlob                 real_D3DReadFileToBlob = nullptr;
static fn_D3DReflect                        real_D3DReflect = nullptr;
static fn_D3DReflectLibrary                 real_D3DReflectLibrary = nullptr;
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
static bool                  g_captureEnabled = true;
static bool                  g_captureHLSL   = true;
static bool                  g_captureDXBC   = true;
static std::mutex            g_mutex;
static std::unordered_set<uint64_t> g_capturedHashes;
static int                   g_captureCount = 0;
static int                   g_compileCount = 0;


// ═══════════════════════════════════════════════════════════════════════════
//  Logging (simple file-based, no dependencies)
// ═══════════════════════════════════════════════════════════════════════════

static void ProxyLog(const char* fmt, ...)
{
    if (!g_logFile) return;
    // Timestamp
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
//  FNV-1a hash + capture to disk
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
    LPCVOID pSrcData, SIZE_T srcSize, ID3DBlob* pCode)
{
    if (!g_captureEnabled || g_capturePath.empty())
        return;
    if (!pSrcData || srcSize == 0)
        return;

    uint64_t hash = FNV1a(pSrcData, srcSize, pEntrypoint);

    std::lock_guard<std::mutex> lock(g_mutex);

    // Dedup
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

    // Ensure directory
    std::error_code ec;
    std::filesystem::create_directories(g_capturePath, ec);

    // Save HLSL
    if (g_captureHLSL) {
        auto hlslPath = g_capturePath / (baseName + ".hlsl");
        std::ofstream ofs(hlslPath, std::ios::binary);
        if (ofs.is_open()) {
            ofs << "// Captured by d3dcompiler_47 proxy (SkyrimBridge)\n";
            ofs << "// Source: " << (pSourceName ? pSourceName : "<memory>") << "\n";
            ofs << "// Entry:  " << (pEntrypoint ? pEntrypoint : "?") << "\n";
            ofs << "// Target: " << (pTarget ? pTarget : "?") << "\n";
            ofs << "// Size:   " << srcSize << " bytes\n";
            ofs << "// Hash:   0x" << (hashStr + 1) << "\n";
            ofs << "//\n\n";
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
    ProxyLog("Captured: %s (%zu bytes HLSL%s)",
             baseName.c_str(), srcSize, pCode ? " + DXBC" : "");
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

    CaptureShader(pSourceName, pEntrypoint, pTarget,
                  pSrcData, SrcDataSize,
                  (ppCode && *ppCode) ? *ppCode : nullptr);

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

    CaptureShader(pSourceName, pEntrypoint, pTarget,
                  pSrcData, SrcDataSize,
                  (ppCode && *ppCode) ? *ppCode : nullptr);

    return hr;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Forwarding stubs — 27 pass-through exports
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

extern "C" __declspec(dllexport) HRESULT WINAPI D3DCreateFunctionLinkingGraph(
    UINT uFlags, void** ppFunctionLinkingGraph)
{
    return real_D3DCreateFunctionLinkingGraph(uFlags, ppFunctionLinkingGraph);
}

extern "C" __declspec(dllexport) HRESULT WINAPI D3DCreateLinker(
    void** ppLinker)
{
    return real_D3DCreateLinker(ppLinker);
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

extern "C" __declspec(dllexport) HRESULT WINAPI D3DLoadModule(
    LPCVOID pSrcData, SIZE_T cbSrcDataSize, void** ppModule)
{
    return real_D3DLoadModule(pSrcData, cbSrcDataSize, ppModule);
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

extern "C" __declspec(dllexport) HRESULT WINAPI D3DReflectLibrary(
    LPCVOID pSrcData, SIZE_T SrcDataSize, REFIID riid, void** ppReflector)
{
    return real_D3DReflectLibrary(pSrcData, SrcDataSize, riid, ppReflector);
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
//  INI config (optional, minimal parser)
// ═══════════════════════════════════════════════════════════════════════════

static void LoadConfig(const std::filesystem::path& iniPath)
{
    char buf[MAX_PATH];
    GetPrivateProfileStringA("Capture", "Directory",
        "Data\\SKSE\\Plugins\\SkyrimBridge\\ShaderCapture",
        buf, sizeof(buf), iniPath.string().c_str());
    // If relative, resolve against exe directory
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
    RESOLVE(D3DCreateFunctionLinkingGraph);
    RESOLVE(D3DCreateLinker);
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
    RESOLVE(D3DLoadModule);
    RESOLVE(D3DPreprocess);
    RESOLVE(D3DReadFileToBlob);
    RESOLVE(D3DReflect);
    RESOLVE(D3DReflectLibrary);
    RESOLVE(D3DReturnFailure1);
    RESOLVE(D3DSetBlobPart);
    RESOLVE(D3DStripShader);
    RESOLVE(D3DWriteBlobToFile);
    RESOLVE(DebugSetMute);

    // D3DCompile and D3DCompile2 are required
    return real_D3DCompile != nullptr;
}

#undef RESOLVE


// ═══════════════════════════════════════════════════════════════════════════
//  DllMain
// ═══════════════════════════════════════════════════════════════════════════

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hinstDLL);

        // ── Load the REAL d3dcompiler_47.dll from System32 ───────────
        char sysDir[MAX_PATH];
        GetSystemDirectoryA(sysDir, MAX_PATH);
        std::string realPath = std::string(sysDir) + "\\d3dcompiler_47.dll";
        g_realDll = LoadLibraryA(realPath.c_str());
        if (!g_realDll)
            return FALSE;  // Fatal: can't function without the real DLL

        if (!ResolveExports()) {
            FreeLibrary(g_realDll);
            g_realDll = nullptr;
            return FALSE;
        }

        // ── Set up paths ─────────────────────────────────────────────
        char exePath[MAX_PATH];
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        auto exeDir = std::filesystem::path(exePath).parent_path();

        // Check for INI config
        auto iniPath = exeDir / "d3dcompiler_proxy.ini";
        if (std::filesystem::exists(iniPath)) {
            LoadConfig(iniPath);
        } else {
            g_capturePath = exeDir / "Data" / "SKSE" / "Plugins"
                           / "SkyrimBridge" / "ShaderCapture";
        }

        // ── Open log file ────────────────────────────────────────────
        g_logPath = exeDir / "Data" / "SKSE" / "Plugins"
                   / "SkyrimBridge" / "d3dcompiler_proxy.log";
        std::error_code ec;
        std::filesystem::create_directories(g_logPath.parent_path(), ec);
        g_logFile = fopen(g_logPath.string().c_str(), "w");

        ProxyLog("d3dcompiler_47 proxy loaded (SkyrimBridge shader capture)");
        ProxyLog("Real DLL: %s", realPath.c_str());
        ProxyLog("Capture path: %s", g_capturePath.string().c_str());
        ProxyLog("Capture enabled: %s, HLSL: %s, DXBC: %s",
                 g_captureEnabled ? "yes" : "no",
                 g_captureHLSL ? "yes" : "no",
                 g_captureDXBC ? "yes" : "no");
    }
    else if (fdwReason == DLL_PROCESS_DETACH)
    {
        if (g_logFile) {
            ProxyLog("Proxy unloading — %d compilations, %d unique shaders captured",
                     g_compileCount, g_captureCount);
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
