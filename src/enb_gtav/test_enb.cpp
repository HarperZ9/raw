//=============================================================================
//  test_enb.cpp -- Standalone test harness for ENBSeries GTA V d3d11.dll
//
//  Loads the proxy DLL dynamically, resolves all 72 exports, and exercises
//  key SDK functions to verify correct behaviour without a running game.
//
//  Build:  cmake --build build --target test_enb --config Release
//  Run:    build\Release\test_enb.exe
//=============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstdio>
#include <cstring>

// ═══════════════════════════════════════════════════════════════════════════
//  Helpers
// ═══════════════════════════════════════════════════════════════════════════

static int g_passed = 0;
static int g_total  = 0;

static void Test(bool condition, const char* name)
{
    g_total++;
    if (condition) {
        g_passed++;
        printf("  [PASS] %s\n", name);
    } else {
        printf("  [FAIL] %s\n", name);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Export name tables (grouped to match the .def file)
// ═══════════════════════════════════════════════════════════════════════════

static const char* kD3D11Proxy[] = {
    "D3D11CoreCreateDevice",
    "D3D11CoreCreateLayeredDevice",
    "D3D11CoreGetLayeredDeviceSize",
    "D3D11CoreRegisterLayers",
    "D3D11CreateDevice",
    "D3D11CreateDeviceAndSwapChain",
};
static const int kD3D11ProxyCount = sizeof(kD3D11Proxy) / sizeof(kD3D11Proxy[0]);

static const char* kENBSDK[] = {
    "ENBGetSDKVersion",
    "ENBGetVersion",
    "ENBGetGameIdentifier",
    "ENBGetParameter",
    "ENBSetParameter",
    "ENBSetCallbackFunction",
    "ENBGetRenderInfo",
    "ENBGetState",
};
static const int kENBSDKCount = sizeof(kENBSDK) / sizeof(kENBSDK[0]);

static const char* kAPI[] = {
    "API_SetRawD3DMode",
    "API_GetD3D11Device",
    "API_GetD3D11DeviceContext",
    "API_GetDXGISwapChain",
    "API_BeforePresent",
    "API_AfterPresent",
    "API_SetWindow",
    "API_BeforeDisplayModeChange",
    "API_AfterDisplayModeChange",
};
static const int kAPICount = sizeof(kAPI) / sizeof(kAPI[0]);

static const char* kENBD3D11[] = {
    "ENBD3D11CreateDevice",
    "ENBD3D11CreateDeviceAndSwapChain",
};
static const int kENBD3D11Count = sizeof(kENBD3D11) / sizeof(kENBD3D11[0]);

static const char* kTw[] = {
    "TwInit",
    "TwTerminate",
    "TwDraw",
    "TwNewBar",
    "TwDeleteBar",
    "TwDeleteAllBars",
    "TwAddVarRW",
    "TwAddVarRO",
    "TwAddVarCB",
    "TwAddButton",
    "TwAddSeparator",
    "TwRemoveVar",
    "TwRemoveAllVars",
    "TwDefine",
    "TwDefineEnum",
    "TwDefineEnumFromString",
    "TwDefineStruct",
    "TwGetParam",
    "TwSetParam",
    "TwGetBarByIndex",
    "TwGetBarByName",
    "TwGetBarCount",
    "TwGetBarName",
    "TwGetBottomBar",
    "TwGetTopBar",
    "TwSetBottomBar",
    "TwSetTopBar",
    "TwRefreshBar",
    "TwSetBarFontSize",
    "TwGetCurrentWindow",
    "TwSetCurrentWindow",
    "TwWindowExists",
    "TwWindowSize",
    "TwGetLastError",
    "TwHandleErrors",
    "TwKeyPressed",
    "TwKeyTest",
    "TwMouseButton",
    "TwMouseMotion",
    "TwMouseWheel",
    "TwEventWin",
    "TwEventWin32",
    "TwCopyCDStringToClientFunc",
    "TwCopyCDStringToLibrary",
    "TwCopyStdStringToClientFunc",
    "TwCopyStdStringToLibrary",
};
static const int kTwCount = sizeof(kTw) / sizeof(kTw[0]);

// ═══════════════════════════════════════════════════════════════════════════
//  SDK callback type (matches ENBState.h)
// ═══════════════════════════════════════════════════════════════════════════

enum ENBCallbackType : long
{
    ENBCallback_EndFrame   = 1,
    ENBCallback_BeginFrame = 2,
};

typedef void (WINAPI* ENBCallbackFunction)(ENBCallbackType calltype);

// ═══════════════════════════════════════════════════════════════════════════
//  Function pointer typedefs for the SDK functions we call
// ═══════════════════════════════════════════════════════════════════════════

typedef long   (__cdecl* pfn_ENBGetSDKVersion)();
typedef long   (__cdecl* pfn_ENBGetVersion)();
typedef long   (__cdecl* pfn_ENBGetGameIdentifier)();
typedef void   (__cdecl* pfn_ENBSetCallbackFunction)(ENBCallbackFunction func);
typedef long   (__cdecl* pfn_ENBGetState)(long state);
typedef void*  (__cdecl* pfn_ENBGetRenderInfo)();
typedef int    (__cdecl* pfn_TwInit)(int graphAPI, void* device);
typedef void*  (__cdecl* pfn_TwNewBar)(const char* name);

// ═══════════════════════════════════════════════════════════════════════════
//  Test callback
// ═══════════════════════════════════════════════════════════════════════════

static bool g_callbackWasSet = false;

static void WINAPI TestCallback(ENBCallbackType /*calltype*/)
{
    // Just a dummy -- we only care that SetCallbackFunction accepts it
    g_callbackWasSet = true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Resolve helper: returns how many in the group resolved successfully
// ═══════════════════════════════════════════════════════════════════════════

static int ResolveGroup(HMODULE hDll, const char* groupName,
                        const char** names, int count)
{
    int resolved = 0;
    for (int i = 0; i < count; i++) {
        FARPROC proc = GetProcAddress(hDll, names[i]);
        if (proc)
            resolved++;
        else
            printf("    MISSING: %s\n", names[i]);
    }
    char label[256];
    snprintf(label, sizeof(label), "%s: %d/%d exports resolved", groupName, resolved, count);
    Test(resolved == count, label);
    return resolved;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Main
// ═══════════════════════════════════════════════════════════════════════════

int main()
{
    printf("========================================\n");
    printf("  ENBSeries GTA V -- Export Test Harness\n");
    printf("========================================\n\n");

    // ------------------------------------------------------------------
    //  Step 0: Locate and load the DLL
    // ------------------------------------------------------------------

    // Build the path relative to the test executable's location.
    // The exe is in build/Release/, and so is d3d11.dll.
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    char* lastSlash = strrchr(exePath, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';

    char dllPath[MAX_PATH];
    snprintf(dllPath, MAX_PATH, "%sd3d11.dll", exePath);

    printf("[INFO] Loading DLL: %s\n\n", dllPath);

    HMODULE hDll = LoadLibraryA(dllPath);
    if (!hDll) {
        printf("[FATAL] LoadLibraryA failed (error %lu). Is d3d11.dll in the same directory?\n",
               GetLastError());
        return 1;
    }

    // ------------------------------------------------------------------
    //  Step 1: Resolve all 72 exports by group
    // ------------------------------------------------------------------

    printf("--- Export Resolution ---\n");

    int totalResolved = 0;
    totalResolved += ResolveGroup(hDll, "D3D11 Proxy (6)",  kD3D11Proxy, kD3D11ProxyCount);
    totalResolved += ResolveGroup(hDll, "ENB SDK (8)",      kENBSDK,     kENBSDKCount);
    totalResolved += ResolveGroup(hDll, "API_* (9)",        kAPI,        kAPICount);
    totalResolved += ResolveGroup(hDll, "ENBD3D11* (2)",    kENBD3D11,   kENBD3D11Count);
    totalResolved += ResolveGroup(hDll, "Tw* (46)",         kTw,         kTwCount);

    // NvOptimusEnablement is a DATA export -- GetProcAddress still works
    {
        FARPROC nvProc = GetProcAddress(hDll, "NvOptimusEnablement");
        if (nvProc) {
            DWORD* pVal = reinterpret_cast<DWORD*>(nvProc);
            char label[128];
            snprintf(label, sizeof(label),
                     "NvOptimusEnablement DATA: resolved, value=%lu", *pVal);
            Test(*pVal == 1, label);
        } else {
            Test(false, "NvOptimusEnablement DATA: resolved");
        }
    }

    printf("\n");

    // ------------------------------------------------------------------
    //  Step 2: ENBGetSDKVersion() == 1002
    // ------------------------------------------------------------------

    printf("--- Functional Tests ---\n");

    {
        auto fn = reinterpret_cast<pfn_ENBGetSDKVersion>(
            GetProcAddress(hDll, "ENBGetSDKVersion"));
        if (fn) {
            long v = fn();
            char label[128];
            snprintf(label, sizeof(label), "ENBGetSDKVersion() == 1002 (got %ld)", v);
            Test(v == 1002, label);
        } else {
            Test(false, "ENBGetSDKVersion() -- could not resolve");
        }
    }

    // ------------------------------------------------------------------
    //  Step 3: ENBGetVersion() == 492
    // ------------------------------------------------------------------

    {
        auto fn = reinterpret_cast<pfn_ENBGetVersion>(
            GetProcAddress(hDll, "ENBGetVersion"));
        if (fn) {
            long v = fn();
            char label[128];
            snprintf(label, sizeof(label), "ENBGetVersion() == 492 (got %ld)", v);
            Test(v == 492, label);
        } else {
            Test(false, "ENBGetVersion() -- could not resolve");
        }
    }

    // ------------------------------------------------------------------
    //  Step 4: ENBGetGameIdentifier() == 0x10000021
    // ------------------------------------------------------------------

    {
        auto fn = reinterpret_cast<pfn_ENBGetGameIdentifier>(
            GetProcAddress(hDll, "ENBGetGameIdentifier"));
        if (fn) {
            long v = fn();
            char label[128];
            snprintf(label, sizeof(label),
                     "ENBGetGameIdentifier() == 0x10000021 (got 0x%08lX)", v);
            Test(v == 0x10000021, label);
        } else {
            Test(false, "ENBGetGameIdentifier() -- could not resolve");
        }
    }

    // ------------------------------------------------------------------
    //  Step 5: ENBSetCallbackFunction() stores a callback
    // ------------------------------------------------------------------

    {
        auto fn = reinterpret_cast<pfn_ENBSetCallbackFunction>(
            GetProcAddress(hDll, "ENBSetCallbackFunction"));
        if (fn) {
            // Should not crash; the function stores the pointer internally.
            fn(TestCallback);
            // We can't directly read callbackCount from outside the DLL,
            // but if we get here without crashing and the fn pointer was
            // non-null, we consider it stored.
            Test(true, "ENBSetCallbackFunction(TestCallback) -- accepted without crash");
        } else {
            Test(false, "ENBSetCallbackFunction() -- could not resolve");
        }
    }

    // ------------------------------------------------------------------
    //  Step 6: ENBGetState(1) == 0  (IsEditorActive, no editor running)
    // ------------------------------------------------------------------

    {
        auto fn = reinterpret_cast<pfn_ENBGetState>(
            GetProcAddress(hDll, "ENBGetState"));
        if (fn) {
            long v = fn(1); // ENBState_IsEditorActive
            char label[128];
            snprintf(label, sizeof(label),
                     "ENBGetState(IsEditorActive) == 0 (got %ld)", v);
            Test(v == 0, label);
        } else {
            Test(false, "ENBGetState() -- could not resolve");
        }
    }

    // ------------------------------------------------------------------
    //  Step 7: TwInit(TW_DIRECT3D11, nullptr) + TwNewBar("TestBar")
    // ------------------------------------------------------------------

    {
        auto fnInit = reinterpret_cast<pfn_TwInit>(
            GetProcAddress(hDll, "TwInit"));
        auto fnNewBar = reinterpret_cast<pfn_TwNewBar>(
            GetProcAddress(hDll, "TwNewBar"));

        if (fnInit) {
            int r = fnInit(4 /*TW_DIRECT3D11*/, nullptr);
            char label[128];
            snprintf(label, sizeof(label),
                     "TwInit(TW_DIRECT3D11, nullptr) returned %d (expected 1)", r);
            Test(r == 1, label);
        } else {
            Test(false, "TwInit() -- could not resolve");
        }

        if (fnNewBar) {
            void* bar = fnNewBar("TestBar");
            char label[128];
            snprintf(label, sizeof(label),
                     "TwNewBar(\"TestBar\") returned %p (expected non-null)", bar);
            Test(bar != nullptr, label);
        } else {
            Test(false, "TwNewBar() -- could not resolve");
        }
    }

    // ------------------------------------------------------------------
    //  Step 8: ENBGetRenderInfo() == nullptr (no device created yet)
    // ------------------------------------------------------------------

    {
        auto fn = reinterpret_cast<pfn_ENBGetRenderInfo>(
            GetProcAddress(hDll, "ENBGetRenderInfo"));
        if (fn) {
            void* info = fn();
            char label[128];
            snprintf(label, sizeof(label),
                     "ENBGetRenderInfo() == nullptr (got %p)", info);
            Test(info == nullptr, label);
        } else {
            Test(false, "ENBGetRenderInfo() -- could not resolve");
        }
    }

    // ------------------------------------------------------------------
    //  Summary
    // ------------------------------------------------------------------

    printf("\n========================================\n");
    printf("  Results: %d/%d tests passed\n", g_passed, g_total);
    printf("========================================\n");

    FreeLibrary(hDll);
    return (g_passed == g_total) ? 0 : 1;
}
