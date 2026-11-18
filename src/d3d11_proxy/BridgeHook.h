#pragma once
// BridgeHook: runs RAW's D3D12 pass inside the game's D3D11 frame.
// See BridgeHook.cpp for the ini keys. Original work, MIT.
namespace SB::Proxy
{
// Called once per frame from WrappedSwapChain::Present/Present1, before the
// phase detector resets for the next frame.
void BridgeHook_OnPresent();
}
