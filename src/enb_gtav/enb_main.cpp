//=============================================================================
//  enb_main.cpp — ENBSeries v0.492 for GTA V: Main DLL Entry Point
//
//  Loaded as d3d11.dll in the GTA V game directory. Wraps D3D11 device,
//  context, and swap chain. Injects ENB post-processing pipeline at Present.
//
//  The D3D11CreateDevice* functions are exported via the .def file as:
//    D3D11CreateDevice = ENB_D3D11CreateDevice_Impl
//    D3D11CreateDeviceAndSwapChain = ENB_D3D11CreateDeviceAndSwapChain_Impl
//  This avoids linker conflicts with the dllimport in d3d11.h.
//=============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>
#include <cstring>

#include "ENBState.h"
#include "ENBSwapChain.h"
#include "ConfigManager.h"
#include "TimeOfDaySystem.h"
#include "WeatherSystem.h"
#include "ENBHelperBridge.h"
#include "RenderTargetPool.h"
#include "PostProcessPipeline.h"
#include "InputManager.h"
#include "ScreenshotCapture.h"
#include "PluginLoader.h"
#include "FPSLimiter.h"
#include "ProxyLibrary.h"
#include "ENBParamsCompute.h"
#include "ENBLog.h"

// ═══════════════════════════════════════════════════════════════════════════
//  Real d3d11.dll function pointers (loaded from System32)
// ═══════════════════════════════════════════════════════════════════════════

using PFN_D3D11CreateDevice = HRESULT(WINAPI*)(
    IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL*, UINT, UINT,
    ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

using PFN_D3D11CreateDeviceAndSwapChain = HRESULT(WINAPI*)(
    IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL*, UINT, UINT,
    const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**,
    ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

static HMODULE                            s_realDLL = nullptr;
static PFN_D3D11CreateDevice              s_realCreateDevice = nullptr;
static PFN_D3D11CreateDeviceAndSwapChain  s_realCreateDeviceAndSwapChain = nullptr;
static bool                               s_initialized = false;

// Trampoline targets for D3D11Core* exports (populated by LazyInit)
extern "C" {
    FARPROC g_d3d11Original_D3D11CoreCreateDevice = nullptr;
    FARPROC g_d3d11Original_D3D11CoreCreateLayeredDevice = nullptr;
    FARPROC g_d3d11Original_D3D11CoreGetLayeredDeviceSize = nullptr;
    FARPROC g_d3d11Original_D3D11CoreRegisterLayers = nullptr;

    // Force discrete GPU on laptops (data export, value=1)
    __declspec(dllexport) DWORD NvOptimusEnablement = 1;
}

static HINSTANCE s_hInstance = nullptr;

// Use g_Log for all logging (writes to enbseries.log + OutputDebugString)
#define ENBLog(...) g_Log.Log(__VA_ARGS__)

// ═══════════════════════════════════════════════════════════════════════════
//  LazyInit — Load real d3d11.dll and resolve all function pointers
// ═══════════════════════════════════════════════════════════════════════════

static bool LazyInit()
{
    if (s_initialized)
        return true;

    // Load real d3d11.dll from System32
    char systemDir[MAX_PATH];
    GetSystemDirectoryA(systemDir, MAX_PATH);

    char realPath[MAX_PATH];
    snprintf(realPath, MAX_PATH, "%s\\d3d11.dll", systemDir);

    s_realDLL = LoadLibraryA(realPath);
    if (!s_realDLL)
    {
        ENBLog("[ENB] FATAL: Failed to load real d3d11.dll from %s\n", realPath);
        return false;
    }

    // Resolve the two Create functions
    s_realCreateDevice = reinterpret_cast<PFN_D3D11CreateDevice>(
        GetProcAddress(s_realDLL, "D3D11CreateDevice"));
    s_realCreateDeviceAndSwapChain = reinterpret_cast<PFN_D3D11CreateDeviceAndSwapChain>(
        GetProcAddress(s_realDLL, "D3D11CreateDeviceAndSwapChain"));

    // Resolve the 4 Core functions (for ASM trampolines)
    g_d3d11Original_D3D11CoreCreateDevice =
        GetProcAddress(s_realDLL, "D3D11CoreCreateDevice");
    g_d3d11Original_D3D11CoreCreateLayeredDevice =
        GetProcAddress(s_realDLL, "D3D11CoreCreateLayeredDevice");
    g_d3d11Original_D3D11CoreGetLayeredDeviceSize =
        GetProcAddress(s_realDLL, "D3D11CoreGetLayeredDeviceSize");
    g_d3d11Original_D3D11CoreRegisterLayers =
        GetProcAddress(s_realDLL, "D3D11CoreRegisterLayers");

    if (!s_realCreateDevice || !s_realCreateDeviceAndSwapChain)
    {
        ENBLog("[ENB] FATAL: Failed to resolve D3D11CreateDevice functions\n");
        return false;
    }

    // Initialize global ENB state
    g_ENB.Init();

    // Get game directory (where our DLL is loaded from)
    char gameDir[MAX_PATH];
    GetModuleFileNameA(s_hInstance, gameDir, MAX_PATH);
    // Strip filename to get directory
    char* lastSlash = strrchr(gameDir, '\\');
    if (lastSlash) *lastSlash = '\0';

    // Initialize file logger (writes to enbseries.log)
    g_Log.Initialize(gameDir);

    // ── Load all subsystems (SEH-protected — crash here = passthrough mode) ──
    __try
    {
        // Phase 2: Load configuration
        g_Config.Initialize(gameDir);
        g_Config.LoadAll();

        // Configure time-of-day system from enbseries.ini [TIMEOFDAY]
        {
            TimeOfDayConfig todConfig;
            char enbPath[MAX_PATH];
            snprintf(enbPath, MAX_PATH, "%s\\enbseries.ini", gameDir);
            char buf[64];
            GetPrivateProfileStringA("TIMEOFDAY", "DawnDuration", "2.0", buf, sizeof(buf), enbPath);
            todConfig.dawnDuration = static_cast<float>(atof(buf));
            GetPrivateProfileStringA("TIMEOFDAY", "SunriseTime", "6.0", buf, sizeof(buf), enbPath);
            todConfig.sunriseTime = static_cast<float>(atof(buf));
            GetPrivateProfileStringA("TIMEOFDAY", "DayTime", "13.0", buf, sizeof(buf), enbPath);
            todConfig.dayTime = static_cast<float>(atof(buf));
            GetPrivateProfileStringA("TIMEOFDAY", "SunsetTime", "20.0", buf, sizeof(buf), enbPath);
            todConfig.sunsetTime = static_cast<float>(atof(buf));
            GetPrivateProfileStringA("TIMEOFDAY", "DuskDuration", "2.0", buf, sizeof(buf), enbPath);
            todConfig.duskDuration = static_cast<float>(atof(buf));
            GetPrivateProfileStringA("TIMEOFDAY", "NightTime", "1.0", buf, sizeof(buf), enbPath);
            todConfig.nightTime = static_cast<float>(atof(buf));
            g_TOD.SetConfig(todConfig);
        }

        // Initialize weather system
        g_Weather.Initialize(gameDir);

        // Phase 3: Load enbhelper.dll bridge
        g_Helper.Load(gameDir);

        // Load proxy library chain (e.g., ReShade)
        {
            const auto& proxyCfg = g_Config.GetLocalConfig();
            if (proxyCfg.enableProxyLibrary && proxyCfg.proxyLibrary[0] != '\0')
            {
                g_ProxyLib.Load(gameDir, proxyCfg.proxyLibrary, proxyCfg.initProxyFunctions);
            }
        }

        // Initialize FPS limiter
        {
            const auto& cfg = g_Config.GetLocalConfig();
            g_FPSLimiter.Initialize(cfg.enableFPSLimit, cfg.fpsLimit);
        }

        // Phase 5: Initialize input manager
        g_Input.Initialize(s_hInstance);

        // Phase 6: Load .dllplugin files
        g_Plugins.LoadAll(gameDir);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ENBLog("[ENB] WARNING: Exception during subsystem init — running in passthrough mode\n");
        g_ENB.useEffect = false;
    }

    ENBLog("[ENB] ENBSeries v0.%d initialized (SDK %d, GameID 0x%08X)\n",
           ENB_VERSION, ENB_SDK_VERSION, ENB_GAME_ID_GTA5);

    s_initialized = true;
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  ENB_D3D11CreateDevice_Impl — Our implementation of D3D11CreateDevice
//  Exported as "D3D11CreateDevice" via .def file aliasing
// ═══════════════════════════════════════════════════════════════════════════

extern "C" HRESULT WINAPI ENB_D3D11CreateDevice_Impl(
    IDXGIAdapter*            pAdapter,
    D3D_DRIVER_TYPE          DriverType,
    HMODULE                  Software,
    UINT                     Flags,
    const D3D_FEATURE_LEVEL* pFeatureLevels,
    UINT                     FeatureLevels,
    UINT                     SDKVersion,
    ID3D11Device**           ppDevice,
    D3D_FEATURE_LEVEL*       pFeatureLevel,
    ID3D11DeviceContext**    ppImmediateContext)
{
    if (!LazyInit())
        return E_FAIL;

    HRESULT hr = s_realCreateDevice(
        pAdapter, DriverType, Software, Flags,
        pFeatureLevels, FeatureLevels, SDKVersion,
        ppDevice, pFeatureLevel, ppImmediateContext);

    if (FAILED(hr))
        return hr;

    // Cap feature level to 11_0 (matching original: cmp [rdi], 0xB000)
    if (pFeatureLevel && *pFeatureLevel > D3D_FEATURE_LEVEL_11_0)
        *pFeatureLevel = D3D_FEATURE_LEVEL_11_0;

    if (ppDevice && *ppDevice)
    {
        g_ENB.realDevice = *ppDevice;
        ENBLog("[ENB] D3D11CreateDevice: device=%p\n", *ppDevice);
    }
    if (ppImmediateContext && *ppImmediateContext)
        g_ENB.realContext = *ppImmediateContext;

    // TODO: Wrap device and context in ENB wrapper objects
    g_ENB.wrappedDevice  = g_ENB.realDevice;
    g_ENB.wrappedContext = g_ENB.realContext;

    return hr;
}

// ═══════════════════════════════════════════════════════════════════════════
//  ENB_D3D11CreateDeviceAndSwapChain_Impl — Main hook point for ENB
//  Exported as "D3D11CreateDeviceAndSwapChain" via .def file aliasing
// ═══════════════════════════════════════════════════════════════════════════

extern "C" HRESULT WINAPI ENB_D3D11CreateDeviceAndSwapChain_Impl(
    IDXGIAdapter*               pAdapter,
    D3D_DRIVER_TYPE             DriverType,
    HMODULE                     Software,
    UINT                        Flags,
    const D3D_FEATURE_LEVEL*    pFeatureLevels,
    UINT                        FeatureLevels,
    UINT                        SDKVersion,
    const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
    IDXGISwapChain**            ppSwapChain,
    ID3D11Device**              ppDevice,
    D3D_FEATURE_LEVEL*          pFeatureLevel,
    ID3D11DeviceContext**       ppImmediateContext)
{
    if (!LazyInit())
        return E_FAIL;

    HRESULT hr = s_realCreateDeviceAndSwapChain(
        pAdapter, DriverType, Software, Flags,
        pFeatureLevels, FeatureLevels, SDKVersion,
        pSwapChainDesc, ppSwapChain,
        ppDevice, pFeatureLevel, ppImmediateContext);

    if (FAILED(hr))
    {
        ENBLog("[ENB] D3D11CreateDeviceAndSwapChain FAILED: 0x%08X\n", hr);
        return hr;
    }

    if (ppDevice && *ppDevice)
        g_ENB.realDevice = *ppDevice;
    if (ppImmediateContext && *ppImmediateContext)
        g_ENB.realContext = *ppImmediateContext;
    if (ppSwapChain && *ppSwapChain)
        g_ENB.realSwapChain = *ppSwapChain;

    if (pSwapChainDesc)
    {
        g_ENB.screenWidth  = pSwapChainDesc->BufferDesc.Width;
        g_ENB.screenHeight = pSwapChainDesc->BufferDesc.Height;
        g_ENB.gameWindow   = pSwapChainDesc->OutputWindow;
        ENBLog("[ENB] SwapChain created: %ux%u, HWND=%p\n",
               g_ENB.screenWidth, g_ENB.screenHeight, g_ENB.gameWindow);
    }

    // TODO: Wrap device and context in ENB wrapper objects
    g_ENB.wrappedDevice  = g_ENB.realDevice;
    g_ENB.wrappedContext = g_ENB.realContext;

    // Wrap the swapchain — intercepts Present() for ENB post-processing
    if (ppSwapChain && *ppSwapChain && g_ENB.realDevice && g_ENB.realContext)
    {
        ENBSwapChain* wrapped = new ENBSwapChain(
            g_ENB.realSwapChain, g_ENB.realDevice, g_ENB.realContext);
        g_ENB.wrappedSwapChain = wrapped;
        *ppSwapChain = wrapped;  // Return wrapper to caller
        ENBLog("[ENB] SwapChain wrapped: real=%p, wrapper=%p\n",
               g_ENB.realSwapChain, wrapped);
    }
    else
    {
        g_ENB.wrappedSwapChain = g_ENB.realSwapChain;
    }

    g_ENB.initialized = true;
    g_ENB.UpdateRenderInfo();

    // Phase 4: Initialize post-processing pipeline
    {
        char gameDir[MAX_PATH];
        GetModuleFileNameA(s_hInstance, gameDir, MAX_PATH);
        char* lastSlash2 = strrchr(gameDir, '\\');
        if (lastSlash2) *lastSlash2 = '\0';
        g_Pipeline.Initialize(g_ENB.realDevice, gameDir);
    }

    g_ENB.InvokeCallbacks(ENBCallback_OnInit);

    ENBLog("[ENB] ENBSeries v0.%d fully initialized\n", ENB_VERSION);
    return hr;
}

// ═══════════════════════════════════════════════════════════════════════════
//  ENBD3D11* — Alternative entry points (delegates to our implementations)
// ═══════════════════════════════════════════════════════════════════════════

extern "C" __declspec(dllexport) HRESULT WINAPI ENBD3D11CreateDevice(
    IDXGIAdapter* a, D3D_DRIVER_TYPE b, HMODULE c, UINT d,
    const D3D_FEATURE_LEVEL* e, UINT f, UINT g,
    ID3D11Device** h, D3D_FEATURE_LEVEL* i, ID3D11DeviceContext** j)
{
    return ENB_D3D11CreateDevice_Impl(a, b, c, d, e, f, g, h, i, j);
}

extern "C" __declspec(dllexport) HRESULT WINAPI ENBD3D11CreateDeviceAndSwapChain(
    IDXGIAdapter* a, D3D_DRIVER_TYPE b, HMODULE c, UINT d,
    const D3D_FEATURE_LEVEL* e, UINT f, UINT g,
    const DXGI_SWAP_CHAIN_DESC* h, IDXGISwapChain** i,
    ID3D11Device** j, D3D_FEATURE_LEVEL* k, ID3D11DeviceContext** l)
{
    return ENB_D3D11CreateDeviceAndSwapChain_Impl(a, b, c, d, e, f, g, h, i, j, k, l);
}

// ═══════════════════════════════════════════════════════════════════════════
//  DllMain
// ═══════════════════════════════════════════════════════════════════════════

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    (void)lpvReserved;

    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        s_hInstance = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
        // Write a marker file to confirm DLL loading (debug only)
        {
            char markerPath[MAX_PATH];
            GetModuleFileNameA(hinstDLL, markerPath, MAX_PATH);
            char* sl = strrchr(markerPath, '\\');
            if (sl) { strcpy(sl + 1, "enb_loaded.txt"); }
            HANDLE hMarker = CreateFileA(markerPath, GENERIC_WRITE, 0, nullptr,
                                          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hMarker != INVALID_HANDLE_VALUE)
            {
                const char msg[] = "ENBSeries d3d11.dll proxy loaded successfully.\r\n";
                DWORD written;
                WriteFile(hMarker, msg, sizeof(msg) - 1, &written, nullptr);
                CloseHandle(hMarker);
            }
        }
        break;

    case DLL_PROCESS_DETACH:
        if (g_ENB.initialized)
        {
            g_Log.Log("[ENB] Shutting down (frame count: %llu)\n", g_ENB.frameCount);
            g_ENB.InvokeCallbacks(ENBCallback_OnExit);
            g_Plugins.UnloadAll();
            g_Input.Shutdown();
            g_Pipeline.Shutdown();
            g_Helper.Unload();
            g_Log.Log("[ENB] Shutdown complete\n");
            g_Log.Shutdown();
            g_ENB.Shutdown();
        }
        if (s_realDLL)
        {
            FreeLibrary(s_realDLL);
            s_realDLL = nullptr;
        }
        break;
    }

    return TRUE;
}
