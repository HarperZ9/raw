#pragma once
// TxMode: the game on raw::tx (docs/TX_IN_GAME.md). With [Translate] Enabled=1 in d3d11_proxy.ini,
// D3D11CreateDeviceAndSwapChain and D3D11CreateDevice return the translation layer's device,
// context and D3D12 flip-model swap chain instead of the system runtime's. With Native=1 the
// system runtime's objects come back unwrapped, so the same harness views can be captured on
// native D3D11 for the comparison. In both forms only a thin swap-chain wrapper sits in front:
// it writes the harness heartbeat, counts drawn primitives to tell world frames from loading
// screens, and captures the backbuffer when the harness asks. Original work, MIT.
#include <d3d11.h>
#include <dxgi.h>

namespace SB::Proxy
{
// 0: off (the normal proxy), 1: raw::tx, 2: native D3D11 behind the same wrapper.
int TxMode_Get();

HRESULT TxMode_CreateDeviceAndSwapChain(IDXGIAdapter* adapter, D3D_DRIVER_TYPE type, HMODULE sw, UINT flags,
                                        const D3D_FEATURE_LEVEL* levels, UINT nLevels, UINT sdk,
                                        const DXGI_SWAP_CHAIN_DESC* scDesc, IDXGISwapChain** sc, ID3D11Device** dev,
                                        D3D_FEATURE_LEVEL* level, ID3D11DeviceContext** ctx, void* realCreate);
HRESULT TxMode_CreateDevice(IDXGIAdapter* adapter, D3D_DRIVER_TYPE type, HMODULE sw, UINT flags,
                            const D3D_FEATURE_LEVEL* levels, UINT nLevels, UINT sdk, ID3D11Device** dev,
                            D3D_FEATURE_LEVEL* level, ID3D11DeviceContext** ctx, void* realCreate);
// A swap chain the game made itself through a DXGI factory on a translated device.
IDXGISwapChain* TxMode_WrapSwapChain(IDXGISwapChain* inner, ID3D11Device* dev);
}
