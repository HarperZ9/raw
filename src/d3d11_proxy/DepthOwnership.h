#pragma once
// DepthOwnership.h — We own the depth buffer. Game writes to ours, we control clears.
// Copyright (c) 2026 Zain D. Harper (papacr0w). All rights reserved.

#include <d3d11.h>

namespace SB::Proxy
{
    // Call from ClearDepthStencilView — setup on first call, then decides whether to allow clear.
    // Returns true if the clear should proceed, false if we're blocking it.
    bool DepthOwn_OnClear(ID3D11DeviceContext* ctx, ID3D11DepthStencilView* dsv);

    // Call from OMSetRenderTargets — substitutes our DSV for the game's.
    // Returns the DSV to actually bind (ours if depth owned, game's otherwise).
    ID3D11DepthStencilView* DepthOwn_SubstituteDSV(ID3D11DepthStencilView* gameDSV);

    // Call from Present — allow one clear at frame boundary.
    void DepthOwn_OnPresent();

    // Get the SRV for reading our owned depth.
    ID3D11ShaderResourceView* DepthOwn_GetSRV();

    // Get the DSV (for PhaseDispatcher backup).
    ID3D11DepthStencilView* DepthOwn_GetDSV();

    // Is depth ownership active?
    bool DepthOwn_IsActive();
}
