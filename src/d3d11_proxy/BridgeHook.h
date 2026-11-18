#pragma once
// BridgeHook: runs RAW's D3D12 pass inside the game's D3D11 frame.
// See BridgeHook.cpp for the ini keys. Original work, MIT.
#include <d3d11.h>
namespace SB::Proxy
{
// True while [D3D12Bridge] is enabled: the one branch the target hook costs otherwise.
inline bool g_bridgeHookActive = false;

// Called once per frame from WrappedSwapChain::Present/Present1, before the
// phase detector resets for the next frame.
void BridgeHook_OnPresent();

// Called from proxy_main right after the real device is created; runs the
// fence probe when [D3D12Bridge] Enabled=1.
void BridgeHook_OnDeviceCreated(ID3D11Device* realDevice, ID3D11DeviceContext* realContext);

// Called from OMSetRenderTargets before the new targets are bound, so the
// targets of the span that just ended still hold what the game drew.
void BridgeHook_OnTargets(ID3D11DeviceContext* real, UINT n, ID3D11RenderTargetView* const* rtvs,
                          ID3D11DepthStencilView* dsv);
}
