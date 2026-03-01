#pragma once
//=============================================================================
//  D3D11Hook.h — D3D11 SwapChain hook for ImGui overlay rendering
//=============================================================================

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;

namespace D3D11Hook
{
    // Initialize the D3D11 hook (hooks Present)
    bool Init();

    // Shutdown and restore original functions
    void Shutdown();

    // Toggle GUI visibility
    void ToggleGUI();

    // Check if GUI is visible
    bool IsGUIVisible();

    // Set GUI visibility
    void SetGUIVisible(bool a_visible);

    // D3D11 resource accessors (for ShaderDebug overlay integration)
    ID3D11Device*        GetDevice();
    ID3D11DeviceContext* GetContext();
    IDXGISwapChain*      GetSwapChain();
}
