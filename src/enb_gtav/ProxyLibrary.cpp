//=============================================================================
//  ProxyLibrary.cpp — Proxy DLL Chain-Loading
//=============================================================================

#include "ProxyLibrary.h"
#include <cstdio>
#include <cstring>

ProxyLibrary g_ProxyLib;

bool ProxyLibrary::Load(const char* gameDir, const char* proxyPath, bool initFunctions)
{
    if (!proxyPath || proxyPath[0] == '\0')
        return false;

    // Build full path relative to game directory
    char fullPath[MAX_PATH];
    if (strchr(proxyPath, '\\') || strchr(proxyPath, '/'))
    {
        // Already has path separators — use as-is
        strncpy_s(fullPath, proxyPath, MAX_PATH - 1);
    }
    else
    {
        snprintf(fullPath, MAX_PATH, "%s\\%s", gameDir, proxyPath);
    }

    m_module = LoadLibraryA(fullPath);
    if (!m_module)
    {
        char buf[512];
        snprintf(buf, sizeof(buf), "[ENB] Proxy library not found: %s (error %lu)\n",
                 fullPath, GetLastError());
        OutputDebugStringA(buf);
        return false;
    }

    if (initFunctions)
    {
        m_createDevice = reinterpret_cast<PFN_D3D11CreateDevice>(
            GetProcAddress(m_module, "D3D11CreateDevice"));
        m_createDeviceAndSC = reinterpret_cast<PFN_D3D11CreateDeviceAndSwapChain>(
            GetProcAddress(m_module, "D3D11CreateDeviceAndSwapChain"));
    }

    m_loaded = true;

    char buf[512];
    snprintf(buf, sizeof(buf), "[ENB] Proxy library loaded: %s (functions: %s)\n",
             fullPath, initFunctions ? "yes" : "no");
    OutputDebugStringA(buf);

    return true;
}

void ProxyLibrary::Unload()
{
    if (m_module)
    {
        FreeLibrary(m_module);
        m_module = nullptr;
    }
    m_loaded = false;
    m_createDevice = nullptr;
    m_createDeviceAndSC = nullptr;
}
