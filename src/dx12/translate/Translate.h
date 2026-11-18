// D3D11-to-D3D12 translation layer: public surface.
//
// CreateDevice returns objects that implement ID3D11Device and
// ID3D11DeviceContext and run every call on a Direct3D 12 device. A D3D11
// application, or the RAW proxy, uses them like the system runtime objects.
#pragma once
#include <d3d11.h>
#include <dxgi.h>
#include <cstdint>

namespace raw::tx {

// Deliberate defects for the control tests. Never set outside tests.
enum Fault : uint32_t {
    FaultNone = 0,
    FaultBlendSwap = 1,     // source and destination blend factors swapped in the PSO
    FaultSkipBarrier = 2,   // render target -> shader resource transitions are dropped
    FaultStaleCB = 4,       // a renamed constant buffer is not rebound
};

struct CreateParams {
    bool warp = false;            // software rasteriser
    bool debugLayer = false;      // D3D12 debug layer; errors are counted
    const LUID* adapter = nullptr;   // a specific adapter (ignored with warp)
    uint32_t faults = FaultNone;
};

struct Stats {
    uint64_t debugErrors = 0;      // D3D12 debug-layer errors and corruptions seen
    uint64_t psoCreated = 0;
    uint64_t rootSignatures = 0;
    uint64_t submissions = 0;
    uint64_t barriers = 0;
    uint64_t draws = 0;
    uint64_t unsupportedCalls = 0; // calls answered E_NOTIMPL or ignored
};

HRESULT CreateDevice(const CreateParams& params, ID3D11Device** device,
                     ID3D11DeviceContext** immediateContext);

// A D3D12 flip-model swap chain for a device made by CreateDevice.
HRESULT CreateSwapChain(ID3D11Device* device, const DXGI_SWAP_CHAIN_DESC* desc,
                        IDXGISwapChain** swapChain);

// Reads the debug layer's stored messages into the counters, then returns them.
Stats GetStats(ID3D11Device* device);

// The D3D12 device and direct queue, for a renderer that adopts them
// (superframe native/raw/rhi/host.hpp, HostDevice). No reference is added.
void GetNative(ID3D11Device* device, void** d3d12Device, void** d3d12Queue);

}  // namespace raw::tx
