#pragma once
//=============================================================================
//  DepthIntercept — ReShade-style depth texture interception
//
//  Intercepts CreateTexture2D to upgrade the game's depth textures from
//  D24_UNORM_S8_UINT (no SRV) to R24G8_TYPELESS (SRV-readable).
//  Provides zero-copy live depth access at any point in the frame.
//=============================================================================

#include <d3d11.h>

namespace SB::Proxy
{
    // Live SRV for the game's main depth buffer. Zero-copy.
    // Returns nullptr before the game creates its depth texture.
    ID3D11ShaderResourceView* DepthIntercept_GetSRV();

    // True once a depth texture has been intercepted and upgraded.
    bool DepthIntercept_IsActive();

    // Check if a texture was upgraded (used by CreateDepthStencilView fixup).
    bool DepthIntercept_WasUpgraded(ID3D11Texture2D* tex);

    // Original depth format for a given typeless format (for DSV creation fixup).
    DXGI_FORMAT DepthIntercept_GetDSVFormat(DXGI_FORMAT typelessFmt);
}
