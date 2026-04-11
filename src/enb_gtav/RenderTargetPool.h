#pragma once
//=============================================================================
//  RenderTargetPool.h — Named Render Target Management
//
//  Creates and manages the fixed set of named render targets that ENB
//  shaders reference via technique annotations and texture declarations.
//=============================================================================

#include <Windows.h>
#include <d3d11.h>
#include <unordered_map>
#include <string>

struct RenderTargetEntry
{
    ID3D11Texture2D*          texture     = nullptr;
    ID3D11RenderTargetView*   rtv         = nullptr;
    ID3D11ShaderResourceView* srv         = nullptr;
    UINT                      width       = 0;
    UINT                      height      = 0;
    DXGI_FORMAT               format      = DXGI_FORMAT_UNKNOWN;
};

class RenderTargetPool
{
public:
    bool Initialize(ID3D11Device* device, UINT screenWidth, UINT screenHeight);
    void Shutdown();
    void Resize(ID3D11Device* device, UINT screenWidth, UINT screenHeight);

    // Lookup by name (e.g., "RenderTarget512", "RenderTargetRGBA32")
    RenderTargetEntry* Get(const std::string& name);

    // Get the SRV for a named render target (for texture binding)
    ID3D11ShaderResourceView* GetSRV(const std::string& name);

    // Get the RTV for a named render target (for output)
    ID3D11RenderTargetView*   GetRTV(const std::string& name);

private:
    bool CreateRT(ID3D11Device* device, const std::string& name,
                  UINT width, UINT height, DXGI_FORMAT format);
    void DestroyAll();

    std::unordered_map<std::string, RenderTargetEntry> m_targets;
    UINT m_screenWidth  = 0;
    UINT m_screenHeight = 0;
};

extern RenderTargetPool g_RTPool;
