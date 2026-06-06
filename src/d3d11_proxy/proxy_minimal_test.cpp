//=============================================================================
//  proxy_minimal_test.cpp — ABSOLUTE MINIMAL d3d11.dll proxy
//
//  Based on Kyle Halladay's approach that is proven to work with Skyrim SE.
//  Zero infrastructure: no logging, no config, no HDR, no wrapping.
//  Pure forwarding only, to test if the d3d11 proxy approach works at all.
//=============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <d3d11.h>
#include <vector>

// Real function pointers
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
static PFN_D3D11CreateDevice              s_realCreate = nullptr;
static PFN_D3D11CreateDeviceAndSwapChain  s_realCreateSC = nullptr;

// Trampoline targets for D3D11Core* (extern "C" for ASM file)
extern "C" {
    FARPROC g_d3d11Original_D3D11CoreCreateDevice = nullptr;
    FARPROC g_d3d11Original_D3D11CoreCreateLayeredDevice = nullptr;
    FARPROC g_d3d11Original_D3D11CoreGetLayeredDeviceSize = nullptr;
    FARPROC g_d3d11Original_D3D11CoreRegisterLayers = nullptr;
}

// PG_IsSafeMode — always true in minimal test
bool PG_IsSafeMode() { return true; }

// Proxy interface stubs (needed for link but unused)
#include "ProxyAPI.h"
static SB::Proxy::ProxyInterface s_proxyInterface = {};
extern "C" __declspec(dllexport) SB::Proxy::ProxyInterface* PG_GetProxyInterface() { return &s_proxyInterface; }
extern "C" __declspec(dllexport) SB::Proxy::ProxyInterface* SB_GetProxyInterface() { return &s_proxyInterface; }

// These are referenced by other TUs but unused in minimal test
namespace SB::Proxy {
    std::vector<PrePresentCallback>   g_prePresentCallbacks;
    std::vector<OnResizeCallback>     g_resizeCallbacks;
    std::vector<OnDrawCallback>       g_drawCallbacks;
    std::vector<OnRTChangeCallback>   g_rtChangeCallbacks;
    std::vector<OnShaderBindCallback> g_shaderBindCallbacks;
    uint32_t g_frameCount = 0;

    void LogInit(const char*) {}
    void Log(const char*, ...) {}
    void LogShutdown() {}
    void ReleaseDepthCache() {}
}

static bool s_initDone = false;

static bool EnsureInit()
{
    if (s_initDone) return (s_realDLL != nullptr);
    s_initDone = true;

    char systemDir[MAX_PATH];
    GetSystemDirectoryA(systemDir, MAX_PATH);
    char realPath[MAX_PATH];
    wsprintfA(realPath, "%s\\d3d11.dll", systemDir);

    s_realDLL = LoadLibraryA(realPath);
    if (!s_realDLL) return false;

    s_realCreate   = (PFN_D3D11CreateDevice)GetProcAddress(s_realDLL, "D3D11CreateDevice");
    s_realCreateSC = (PFN_D3D11CreateDeviceAndSwapChain)GetProcAddress(s_realDLL, "D3D11CreateDeviceAndSwapChain");

    g_d3d11Original_D3D11CoreCreateDevice        = GetProcAddress(s_realDLL, "D3D11CoreCreateDevice");
    g_d3d11Original_D3D11CoreCreateLayeredDevice  = GetProcAddress(s_realDLL, "D3D11CoreCreateLayeredDevice");
    g_d3d11Original_D3D11CoreGetLayeredDeviceSize = GetProcAddress(s_realDLL, "D3D11CoreGetLayeredDeviceSize");
    g_d3d11Original_D3D11CoreRegisterLayers       = GetProcAddress(s_realDLL, "D3D11CoreRegisterLayers");

    return (s_realCreate && s_realCreateSC);
}

extern "C" HRESULT WINAPI SB_D3D11CreateDevice(
    IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags,
    const D3D_FEATURE_LEVEL* pFL, UINT FLCount, UINT SDKVersion,
    ID3D11Device** ppDevice, D3D_FEATURE_LEVEL* pFL_out, ID3D11DeviceContext** ppCtx)
{
    if (!EnsureInit()) return E_FAIL;
    return s_realCreate(pAdapter, DriverType, Software, Flags, pFL, FLCount, SDKVersion, ppDevice, pFL_out, ppCtx);
}

extern "C" HRESULT WINAPI SB_D3D11CreateDeviceAndSwapChain(
    IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags,
    const D3D_FEATURE_LEVEL* pFL, UINT FLCount, UINT SDKVersion,
    const DXGI_SWAP_CHAIN_DESC* pSCDesc, IDXGISwapChain** ppSC,
    ID3D11Device** ppDevice, D3D_FEATURE_LEVEL* pFL_out, ID3D11DeviceContext** ppCtx)
{
    if (!EnsureInit()) return E_FAIL;

    // Pure forwarding — call real function, return real objects
    return s_realCreateSC(pAdapter, DriverType, Software, Flags, pFL, FLCount, SDKVersion,
                          pSCDesc, ppSC, ppDevice, pFL_out, ppCtx);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(hinstDLL);
    else if (fdwReason == DLL_PROCESS_DETACH)
    {
        if (s_realDLL) { FreeLibrary(s_realDLL); s_realDLL = nullptr; }
    }
    return TRUE;
}
