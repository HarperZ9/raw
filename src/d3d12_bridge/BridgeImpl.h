#pragma once
// Private state of D3D12Bridge, shared by D3D12Bridge.cpp (setup) and
// BridgeFrame.cpp (per-frame work).
#include "D3D12Bridge.h"
#include <wrl/client.h>

namespace raw::bridge
{
using Microsoft::WRL::ComPtr;

constexpr UINT kRing = 4;          // frames in flight for allocators and queries
constexpr UINT kD11Stamps = 6;     // depth0 depth1 in0 in1 stall-end out-end

struct D11Slot
{
    ComPtr<ID3D11Query> disjoint;
    ComPtr<ID3D11Query> ts[kD11Stamps];
    bool     open      = false;   // disjoint begun
    bool     hasDepth  = false;
    bool     executed  = false;
    uint64_t frame     = ~0ull;
};

struct D12Slot
{
    ComPtr<ID3D12CommandAllocator> alloc;
    uint64_t fenceValue = 0;      // D3D12 signal that retires this slot
    uint64_t frameTag   = ~0ull;  // frame whose timestamps the slot holds
};

struct SharedTex
{
    ComPtr<ID3D12Resource>  res12;
    ComPtr<ID3D11Texture2D> tex11;
};

struct D3D12Bridge::Impl
{
    BridgeConfig cfg{};
    LogFn        log = nullptr;
    bool         ready = false;

    // D3D11 (borrowed from the game, AddRef'd)
    ComPtr<ID3D11Device5>        dev11;
    ComPtr<ID3D11DeviceContext4> ctx11;
    ComPtr<ID3D11Fence>          fence11;
    ComPtr<ID3D11ComputeShader>  depthCS;     // DepthCopy.hlsl
    ComPtr<ID3D11UnorderedAccessView> depthUAV;

    // D3D12 (ours)
    HMODULE                            d3d12Dll = nullptr;
    ComPtr<ID3D12Device>               dev12;
    ComPtr<ID3D12CommandQueue>         queue;
    ComPtr<ID3D12GraphicsCommandList>  cl;
    ComPtr<ID3D12Fence>                fence12;
    ComPtr<ID3D12DescriptorHeap>       srvHeap;   // shader visible: colour, depth
    ComPtr<ID3D12DescriptorHeap>       rtvHeap;   // output
    ComPtr<ID3D12QueryHeap>            tsHeap;
    ComPtr<ID3D12Resource>             tsReadback;
    HANDLE                             cpuEvent = nullptr;
    uint64_t                           d12Freq = 1;

    SharedTex colorIn, depthIn, colorOut;
    std::unique_ptr<IBridgePass> pass;

    D12Slot  d12[kRing];
    D11Slot  d11[kRing];
    uint64_t nextValue  = 1;      // next fence value to use
    uint64_t lastD12    = 0;      // last value D3D12 signals
    uint64_t lastWaited = 0;      // last value the D3D12 queue waits for
    uint64_t skew       = 0;
    uint64_t frames     = 0;      // Execute calls that submitted

    bool CreateDevice12(ID3D11Device* dev);
    bool CreateShared(SharedTex& t, DXGI_FORMAT fmt, D3D12_RESOURCE_FLAGS flags,
                      const char* name);
    bool CreateViews();
    bool CreateDepthCopy();
    bool SelfTest();
    bool SelfTestOnce();
    bool FenceFromD3D11();
    bool CreateQueries();
    bool CpuWait(uint64_t value, DWORD timeoutMs);
    D11Slot& SlotFor(uint64_t frame) { return d11[frame % kRing]; }
    void OpenD11Slot(D11Slot& s, uint64_t frame);
};

constexpr DXGI_FORMAT kSharedDepthFormat = DXGI_FORMAT_R32_FLOAT;
} // namespace raw::bridge
