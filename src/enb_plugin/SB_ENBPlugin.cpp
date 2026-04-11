//=============================================================================
//  SB_ENBPlugin.cpp — Playground ENB External Plugin (.dllplugin)
//
//  This standalone DLL is loaded by ENBSeries from the enbseries/ folder.
//  It reads game data from shared memory written by the Playground SKSE
//  plugin and pushes it to ENB shaders via ENBSetParameter.
//
//  ENB Plugin Interface (verified against ENB v504 SDK 1002):
//    1. ENB calls LoadLibrary("enbseries/Playground_ENB.dllplugin")
//    2. DllMain(DLL_PROCESS_ATTACH) fires
//    3. Plugin finds ENB's module via EnumProcessModules
//    4. Plugin resolves API via GetProcAddress (ENBGetSDKVersion, etc.)
//    5. Plugin registers callback via ENBSetCallbackFunction
//    6. ENB invokes callback each frame — plugin pushes data via SetParameter
//
//  Dependencies: Windows.h + PSAPI only — NO SKSE, NO CommonLibSSE.
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include <windows.h>
#include <psapi.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <iterator>  // std::size

// Shared layout between SKSE plugin (writer) and this ENB plugin (reader)
#include "../SB_SharedLayout.h"

// BridgeData.h provides AllData, Float4, kParamTable, kParamCount — all POD
// (included transitively via SB_SharedLayout.h → BridgeData.h)

#pragma comment(lib, "psapi.lib")

//=============================================================================
//  Simple File Logger (no SKSE dependency)
//=============================================================================

static FILE* g_logFile = nullptr;

static void LogOpen()
{
    if (g_logFile) return;
    g_logFile = fopen("Playground_ENBPlugin.log", "w");
    if (g_logFile) {
        fprintf(g_logFile, "Playground ENB Plugin v1.1 — Diagnostic Log\n");
        fprintf(g_logFile, "=============================================\n");
        fflush(g_logFile);
    }
}

static void Log(const char* fmt, ...)
{
    if (!g_logFile) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(g_logFile, fmt, args);
    va_end(args);
    fprintf(g_logFile, "\n");
    fflush(g_logFile);
}

static void LogClose()
{
    if (g_logFile) {
        fclose(g_logFile);
        g_logFile = nullptr;
    }
}

//=============================================================================
//  ENB SDK Type Definitions (verified against ENB v504 SDK v1002)
//=============================================================================

// Parameter types — must match Boris's enbseries.h exactly
enum ENBParameterType : long
{
    ENBParam_NONE       = 0,
    ENBParam_FLOAT      = 1,    // 1 float  (4 bytes)
    ENBParam_INT        = 2,    // 1 int    (4 bytes)
    ENBParam_HEX        = 3,    // 1 DWORD  (4 bytes)
    ENBParam_BOOL       = 4,    // 1 BOOL   (4 bytes)
    ENBParam_COLOR3     = 5,    // 3 floats (12 bytes)
    ENBParam_COLOR4     = 6,    // 4 floats (16 bytes) — used for float4
    ENBParam_VECTOR3    = 7,    // 3 floats (12 bytes)
};

// Parameter struct — layout verified by d3d11.dll disassembly
// ENBSetParameter reads [R9+0x10] as Size, [R9+0x14] as Type
struct ENBParameter
{
    unsigned char    Data[16];  // offset 0x00: raw value (max float4 = 16 bytes)
    unsigned long    Size;      // offset 0x10: data size in bytes
    ENBParameterType Type;      // offset 0x14: parameter type
};

// Callback types — SDK v1001/1002 values (verified against enbseries.h)
enum ENBCallbackType : long
{
    ENBCallback_EndFrame   = 1,  // Before Present — push data here
    ENBCallback_BeginFrame = 2,  // After Present
    ENBCallback_PreSave    = 3,  // Before user saves config
    ENBCallback_PostLoad   = 4,  // After config load
    ENBCallback_OnInit     = 5,  // ENB fully initialized
    ENBCallback_OnExit     = 6,  // Game closing
    ENBCallback_PreReset   = 7,  // Display mode change — destroy resources
    ENBCallback_PostReset  = 8,  // Display mode change — recreate resources
};

// ENB SDK function signatures (x64: __stdcall == __cdecl == default)
using FN_ENBGetSDKVersion       = long(WINAPI*)();
using FN_ENBGetVersion          = long(WINAPI*)();
using FN_ENBGetGameIdentifier   = long(WINAPI*)();
using FN_ENBSetCallbackFunction = void(WINAPI*)(void(WINAPI*)(ENBCallbackType));
using FN_ENBGetParameter        = BOOL(WINAPI*)(const char*, const char*, const char*, ENBParameter*);
using FN_ENBSetParameter        = BOOL(WINAPI*)(const char*, const char*, const char*, ENBParameter*);

//=============================================================================
//  Resolved ENB Function Pointers
//=============================================================================

static FN_ENBGetSDKVersion       g_enbGetSDKVersion       = nullptr;
static FN_ENBGetVersion          g_enbGetVersion          = nullptr;
static FN_ENBGetGameIdentifier   g_enbGetGameIdentifier   = nullptr;
static FN_ENBSetCallbackFunction g_enbSetCallbackFunction = nullptr;
static FN_ENBGetParameter        g_enbGetParameter        = nullptr;
static FN_ENBSetParameter        g_enbSetParameter        = nullptr;

static bool g_enbResolved = false;

//=============================================================================
//  Shared Memory Reader
//=============================================================================

class SharedMemoryReader
{
public:
    static SharedMemoryReader& Get()
    {
        static SharedMemoryReader inst;
        return inst;
    }

    bool Initialize()
    {
        if (m_initialized) return true;

        m_hMapFile = OpenFileMappingW(
            FILE_MAP_READ, FALSE,
            SB::kSharedMemName   // L"Playground_GameState"
        );
        if (!m_hMapFile) return false;

        m_pData = static_cast<const SB::SB_SharedData*>(
            MapViewOfFile(m_hMapFile, FILE_MAP_READ, 0, 0, sizeof(SB::SB_SharedData))
        );
        if (!m_pData) {
            CloseHandle(m_hMapFile);
            m_hMapFile = nullptr;
            return false;
        }

        if (m_pData->header.magic != SB::kSharedMemMagic) {
            UnmapViewOfFile(m_pData);
            CloseHandle(m_hMapFile);
            m_pData = nullptr;
            m_hMapFile = nullptr;
            return false;
        }

        m_initialized = true;
        return true;
    }

    void Shutdown()
    {
        if (m_pData)    { UnmapViewOfFile(m_pData); m_pData = nullptr; }
        if (m_hMapFile) { CloseHandle(m_hMapFile);  m_hMapFile = nullptr; }
        m_initialized = false;
    }

    bool IsValid() const { return m_initialized && m_pData != nullptr; }
    const SB::SB_SharedData* GetData() const { return m_pData; }

private:
    SharedMemoryReader() = default;
    ~SharedMemoryReader() { Shutdown(); }

    HANDLE                    m_hMapFile    = nullptr;
    const SB::SB_SharedData*  m_pData       = nullptr;
    bool                      m_initialized = false;
};

//=============================================================================
//  Local Data Cache + Dirty Tracking
//=============================================================================

static SB::AllData g_allData{};
static SB::AllData g_prevData{};
static bool        g_dataValid    = false;
static bool        g_hasPrevData  = false;
static uint32_t    g_pushCount    = 0;
static int         g_setParamCalls = 0;

//=============================================================================
//  UpdateFromSharedMemory — called once per frame from callback
//=============================================================================

static void UpdateFromSharedMemory()
{
    auto& reader = SharedMemoryReader::Get();

    if (!reader.IsValid()) {
        if (!reader.Initialize()) return;
        Log("SharedMemory connected on frame %u", g_pushCount);
    }

    const auto* shared = reader.GetData();
    if (!shared || shared->header.magic != SB::kSharedMemMagic) return;

    memcpy(&g_allData, &shared->allData, sizeof(SB::AllData));
    g_dataValid = true;
}

//=============================================================================
//  PushAllDataToENB — push dirty params via ENBSetParameter
//=============================================================================

static void PushAllDataToENB()
{
    if (!g_enbSetParameter || !g_dataValid) return;

    const auto* rawData = reinterpret_cast<const char*>(&g_allData);
    const auto* prevRaw = reinterpret_cast<const char*>(&g_prevData);

    // Reusable param struct — all SB params are float4 (COLOR4, 16 bytes)
    ENBParameter param;
    memset(&param, 0, sizeof(param));
    param.Size = 16;
    param.Type = ENBParam_COLOR4;

    int dirtyCount = 0;

    for (size_t i = 0; i < SB::kParamCount; ++i) {
        const auto& entry = SB::kParamTable[i];

        // Skip unchanged parameters (after first frame)
        if (g_hasPrevData && memcmp(rawData + entry.offset, prevRaw + entry.offset, 16) == 0)
            continue;

        ++dirtyCount;
        memcpy(param.Data, rawData + entry.offset, 16);

        // Push to all 9 target shaders (UPPERCASE names required)
        for (const auto* shader : SB::kTargetShaders) {
            g_enbSetParameter(nullptr, shader, entry.name, &param);
            ++g_setParamCalls;
        }
    }

    // Store current data for next-frame dirty comparison
    g_prevData = g_allData;
    g_hasPrevData = true;
    ++g_pushCount;

    // Log first push + milestones
    if (g_pushCount == 1) {
        Log("First push: %d dirty params x %zu shaders = %d SetParameter calls",
            dirtyCount, std::size(SB::kTargetShaders),
            dirtyCount * static_cast<int>(std::size(SB::kTargetShaders)));
    }
    if (g_pushCount == 300 || g_pushCount == 3000) {
        Log("Push #%u: %d/%zu params dirty, %d total SetParameter calls",
            g_pushCount, dirtyCount, SB::kParamCount, g_setParamCalls);
    }
}

//=============================================================================
//  ENB Callback — invoked by ENB each frame
//=============================================================================

static void WINAPI ENBCallback(ENBCallbackType calltype)
{
    switch (calltype)
    {
    case ENBCallback_EndFrame:
        // This is the primary data push point — called BEFORE Present.
        // ENBSetParameter MUST be called from within a callback.
        UpdateFromSharedMemory();
        PushAllDataToENB();
        break;

    case ENBCallback_PostLoad:
        // Config was loaded — re-push all data on next frame
        g_hasPrevData = false;
        Log("PostLoad callback — will re-push all params next frame");
        break;

    case ENBCallback_OnInit:
        Log("OnInit callback — ENB fully initialized");
        break;

    case ENBCallback_OnExit:
        Log("OnExit callback — game closing (total %d SetParameter calls)", g_setParamCalls);
        SharedMemoryReader::Get().Shutdown();
        g_dataValid = false;
        break;

    default:
        break;
    }
}

//=============================================================================
//  FindENBModule — scan loaded modules for ENBGetSDKVersion export
//  (Same pattern used by Boris's ExamplePlugin.cpp)
//=============================================================================

static HMODULE FindENBModule()
{
    HMODULE modules[1024];
    DWORD cbNeeded = 0;

    if (!EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &cbNeeded))
        return nullptr;

    DWORD count = cbNeeded / sizeof(HMODULE);
    for (DWORD i = 0; i < count; ++i) {
        auto fn = reinterpret_cast<FN_ENBGetSDKVersion>(
            GetProcAddress(modules[i], "ENBGetSDKVersion"));
        if (fn) {
            // Verify SDK version compatibility (same major = 1xxx)
            long ver = fn();
            if ((ver / 1000) == 1) {
                Log("Found ENB module at HMODULE %p, SDK version %ld", modules[i], ver);
                return modules[i];
            }
        }
    }
    return nullptr;
}

//=============================================================================
//  ResolveENBAPI — resolve all ENB SDK functions from the found module
//=============================================================================

static bool ResolveENBAPI(HMODULE enbMod)
{
    g_enbGetSDKVersion = reinterpret_cast<FN_ENBGetSDKVersion>(
        GetProcAddress(enbMod, "ENBGetSDKVersion"));
    g_enbGetVersion = reinterpret_cast<FN_ENBGetVersion>(
        GetProcAddress(enbMod, "ENBGetVersion"));
    g_enbGetGameIdentifier = reinterpret_cast<FN_ENBGetGameIdentifier>(
        GetProcAddress(enbMod, "ENBGetGameIdentifier"));
    g_enbSetCallbackFunction = reinterpret_cast<FN_ENBSetCallbackFunction>(
        GetProcAddress(enbMod, "ENBSetCallbackFunction"));
    g_enbGetParameter = reinterpret_cast<FN_ENBGetParameter>(
        GetProcAddress(enbMod, "ENBGetParameter"));
    g_enbSetParameter = reinterpret_cast<FN_ENBSetParameter>(
        GetProcAddress(enbMod, "ENBSetParameter"));

    if (!g_enbSetCallbackFunction || !g_enbSetParameter) {
        Log("FATAL: Failed to resolve core ENB SDK functions");
        Log("  SetCallbackFunction: %p", g_enbSetCallbackFunction);
        Log("  SetParameter: %p", g_enbSetParameter);
        return false;
    }

    Log("ENB API resolved successfully:");
    Log("  GetSDKVersion:       %p (v%ld)", g_enbGetSDKVersion,
        g_enbGetSDKVersion ? g_enbGetSDKVersion() : 0);
    Log("  GetVersion:          %p (v%ld)", g_enbGetVersion,
        g_enbGetVersion ? g_enbGetVersion() : 0);
    Log("  GetGameIdentifier:   %p (0x%08lX)", g_enbGetGameIdentifier,
        g_enbGetGameIdentifier ? g_enbGetGameIdentifier() : 0);
    Log("  SetCallbackFunction: %p", g_enbSetCallbackFunction);
    Log("  GetParameter:        %p", g_enbGetParameter);
    Log("  SetParameter:        %p", g_enbSetParameter);
    Log("  kParamCount:         %zu", SB::kParamCount);
    Log("  kTargetShaders:      %zu", std::size(SB::kTargetShaders));

    return true;
}

//=============================================================================
//  DLL Entry Point
//=============================================================================

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID)
{
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
    {
        DisableThreadLibraryCalls(hinstDLL);
        LogOpen();
        Log("DllMain(DLL_PROCESS_ATTACH) — Playground ENB plugin loaded");

        // Step 1: Find ENB's module (d3d11.dll or enbseries.dll)
        HMODULE enbMod = FindENBModule();
        if (!enbMod) {
            Log("ERROR: Could not find ENB module in process — plugin inactive");
            break;
        }

        // Step 2: Resolve all ENB SDK functions
        if (!ResolveENBAPI(enbMod)) {
            Log("ERROR: Failed to resolve ENB API — plugin inactive");
            break;
        }

        // Step 3: Register our per-frame callback
        g_enbSetCallbackFunction(ENBCallback);
        g_enbResolved = true;
        Log("Callback registered — plugin active, waiting for first EndFrame");

        // Step 4: Try to connect shared memory (may not be ready yet)
        if (SharedMemoryReader::Get().Initialize()) {
            Log("SharedMemory connected immediately");
        } else {
            Log("SharedMemory not yet available — will retry each frame");
        }
        break;
    }

    case DLL_PROCESS_DETACH:
        Log("DllMain(DLL_PROCESS_DETACH) — total %d SetParameter calls across %u frames",
            g_setParamCalls, g_pushCount);
        SharedMemoryReader::Get().Shutdown();
        g_dataValid = false;
        LogClose();
        break;
    }

    return TRUE;
}
