#pragma once
//=============================================================================
//  RmlD3D11 — RmlUi D3D11 render interface + system interface
//
//  Bridges RmlUi's abstract rendering API to D3D11. Renders HTML/CSS UI
//  elements as textured quads with proper blending onto the game's backbuffer.
//
//  Usage:
//    RmlD3D11::Init(device, context, width, height);
//    RmlD3D11::LoadDocument("Data/SKSE/Plugins/RAW/UI/panel.rml");
//    // In Present hook:
//    RmlD3D11::Update();   // process animations, layout
//    RmlD3D11::Render();   // draw to current render target
//
//  Copyright (c) 2026 Zain D. Harper. All rights reserved.
//=============================================================================

#include <d3d11.h>
#include <cstdint>

namespace RmlD3D11
{
    bool Init(ID3D11Device* device, ID3D11DeviceContext* context,
              uint32_t width, uint32_t height, HWND hwnd);
    void Shutdown();

    // Load an RML document from file
    bool LoadDocument(const char* path);

    // Per-frame: update layout + animations, then render
    void Update();
    void Render();

    // Input forwarding from WndProc
    bool ProcessWindowMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Resize when the window changes
    void OnResize(uint32_t width, uint32_t height);

    bool IsInitialized();
    bool IsVisible();
    void SetVisible(bool visible);
}
