#pragma once
//=============================================================================
//  ProxyLibrary.h — Proxy DLL Chain-Loading
//
//  enblocal.ini [PROXY] section allows chain-loading another d3d11.dll proxy.
//  This enables stacking ENB with other d3d11 wrappers (e.g., ReShade).
//
//  If EnableProxyLibrary=true and ProxyLibrary=reshade.dll, we load that DLL
//  and forward the original D3D11CreateDevice* calls through it instead of
//  directly to the system d3d11.dll.
//=============================================================================

#include <Windows.h>
#include <d3d11.h>

class ProxyLibrary
{
public:
    // Try to load the proxy library specified in enblocal.ini.
    // If successful, all D3D11 create calls go through the proxy first.
    // Returns false if no proxy configured or loading failed (non-fatal).
    bool Load(const char* gameDir, const char* proxyPath, bool initFunctions);
    void Unload();

    bool IsLoaded() const { return m_loaded; }

    // Proxy's D3D11 create functions (if loaded, these override the system ones)
    using PFN_D3D11CreateDevice = HRESULT(WINAPI*)(
        IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
        const D3D_FEATURE_LEVEL*, UINT, UINT,
        ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

    using PFN_D3D11CreateDeviceAndSwapChain = HRESULT(WINAPI*)(
        IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
        const D3D_FEATURE_LEVEL*, UINT, UINT,
        const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**,
        ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

    PFN_D3D11CreateDevice              GetCreateDevice()              { return m_createDevice; }
    PFN_D3D11CreateDeviceAndSwapChain  GetCreateDeviceAndSwapChain()  { return m_createDeviceAndSC; }

private:
    HMODULE m_module = nullptr;
    bool    m_loaded = false;

    PFN_D3D11CreateDevice              m_createDevice     = nullptr;
    PFN_D3D11CreateDeviceAndSwapChain  m_createDeviceAndSC = nullptr;
};

extern ProxyLibrary g_ProxyLib;
