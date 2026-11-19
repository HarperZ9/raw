// D3D11-to-D3D12 translation layer: public surface.
//
// CreateDevice returns objects that implement ID3D11Device and
// ID3D11DeviceContext and run every call on a Direct3D 12 device. A D3D11
// application, or the RAW proxy, uses them like the system runtime objects.
#pragma once
#include <d3d11.h>
#include <dxgi.h>
#include <d3d12.h>
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
    // The DirectX 12 Agility SDK: the folder holding its D3D12Core.dll and its SDK version. The host
    // exe exports neither D3D12SDKVersion nor D3D12SDKPath, so the device comes from
    // ID3D12SDKConfiguration1::CreateDeviceFactory. Empty: the system runtime.
    const char* agilityPath = nullptr;
    uint32_t agilityVersion = 0;
    // Buffer renames on the immediate context bump through a ring of 4 MB upload pages (on by default);
    // off: one slab chunk per rename, as before.
    bool renameRing = true;
    // The ring's pages in CPU-cached (write-back) system memory instead of write-combined upload memory:
    // writes into them need no write-combining drain. An experiment; off by default.
    bool ringWriteBack = false;
    // Worker threads that translate the immediate context's draws into D3D12 command lists in parallel
    // (tx_stream.h); 0: every call is translated on the application's thread, as before.
    uint32_t workers = 0;
    uint32_t segmentDraws = 256;   // draws a worker's segment takes
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

// Where the layer's log lines go (by default the debugger and stderr). A host such as the
// RAW proxy routes them into its own log.
void SetLogSink(void (*sink)(const char* line));

// The last 48 calls into the layer, oldest first, as one line (for a fault report).
void TraceDump(void (*out)(const char* line));

// A renderer on the layer's own device (host access, tx_host.cpp). NativeResource gives the
// D3D12 resource behind a buffer or texture of the layer (no reference added). PrepareForHost puts
// the listed resources in `state` at the end of the work recorded so far and submits that work;
// lists the host then submits to GetNative's queue see them in that state and must leave them in it.
ID3D12Resource* NativeResource(ID3D11Resource* resource);
HRESULT PrepareForHost(ID3D11DeviceContext* context, ID3D11Resource* const* resources, UINT count, D3D12_RESOURCE_STATES state);

// Per-draw reads for a host that hooks every draw (the RAW proxy): IsLayerContext tells a context of
// this layer from any other; PeekConstantBuffers then gives the constant buffers bound to a stage
// (0 VS, 1 HS, 2 DS, 3 GS, 4 PS, 5 CS) without adding references, so no Get and Release pair runs
// per draw. The pointers are valid while they stay bound.
bool IsLayerContext(ID3D11DeviceContext* context);
void PeekConstantBuffers(ID3D11DeviceContext* context, UINT stage, UINT start, UINT count, ID3D11Buffer** out);
// The rest of a draw's bindings, read the same way (no reference added; valid while bound). Each
// returns nullptr when nothing is bound there. PeekBlendDesc gives the bound blend state's description.
ID3D11InputLayout* PeekInputLayout(ID3D11DeviceContext* context);
ID3D11PixelShader* PeekPixelShader(ID3D11DeviceContext* context);
const D3D11_BLEND_DESC* PeekBlendDesc(ID3D11DeviceContext* context);
ID3D11Buffer* PeekVertexBuffer(ID3D11DeviceContext* context, UINT slot, UINT* stride, UINT* offset);
ID3D11Buffer* PeekIndexBuffer(ID3D11DeviceContext* context, DXGI_FORMAT* format, UINT* offset);
ID3D11ShaderResourceView* PeekShaderResource(ID3D11DeviceContext* context, UINT stage, UINT slot,
                                             D3D11_SRV_DIMENSION* dimension);

// References without a locked instruction, for a host that takes and drops them on the immediate
// context's thread (the RAW proxy's per-draw capture for the ray-tracing scene): +1 or -1 on the immediate
// context's plain binding count (tx_holds.h). While above 0 the object stays alive, as with AddRef; a
// Release by the application meanwhile is fine. Only on objects of this layer.
void HoldBuffer(ID3D11Buffer* buffer, int delta);
void HoldView(ID3D11ShaderResourceView* view, int delta);

// The D3D12 device and direct queue, for a renderer that adopts them
// (superframe native/raw/rhi/host.hpp, HostDevice). No reference is added.
void GetNative(ID3D11Device* device, void** d3d12Device, void** d3d12Queue);

}  // namespace raw::tx
