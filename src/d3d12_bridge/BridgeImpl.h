#pragma once
// Private state of D3D12Bridge, shared by D3D12Bridge.cpp (setup) and
// BridgeFrame.cpp (per-frame work).
#include "D3D12Bridge.h"
#include <wrl/client.h>

namespace raw::bridge
{
using Microsoft::WRL::ComPtr;

constexpr UINT kRing = 4;          // frames in flight for allocators and queries
constexpr UINT kD11Stamps = 8;     // depth0 depth1 normal0 normal1 in0 in1 stall-end out-end

struct D11Slot
{
    ComPtr<ID3D11Query> disjoint;
    ComPtr<ID3D11Query> ts[kD11Stamps];
    bool     open      = false;   // disjoint begun
    bool     hasDepth  = false;
    bool     hasNormal = false;
    bool     hasMotion = false;
    bool     hasSurface = false;   // albedo, specular and shadow mask copied (lighting pass)
    bool     hasShadows = false;   // the cascade atlas written (lighting pass)
    bool     executed  = false;
    uint64_t frame     = ~0ull;
};

struct D12Slot
{
    uint64_t    frameTag = ~0ull;  // frame whose pass timings the slot holds
    bool        havePass = false;
    PassTimings pass;
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
    ComPtr<ID3D11ComputeShader>  stencilCS, volumeCS;   // StencilCopy.hlsl, VolumeCopy.hlsl; made on first use
    ComPtr<ID3D11UnorderedAccessView> depthUAV;

    // D3D12 (ours)
    HMODULE                            d3d12Dll = nullptr;
    ComPtr<ID3D12Device>               dev12;
    ComPtr<ID3D12CommandQueue>         queue;
    ComPtr<ID3D12Fence>                fence12;
    HANDLE                             cpuEvent = nullptr;
    // ReadShared's own list (BridgeReadback.cpp), made on first use.
    ComPtr<ID3D12CommandAllocator>     readAlloc;
    ComPtr<ID3D12GraphicsCommandList>  readList;
    ComPtr<ID3D12Fence>                readFence;
    uint64_t                           readValue = 0;

    // Textures made on demand for ShareAndRead, one per size and format.
    struct Extra
    {
        UINT w = 0, h = 0;
        DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
        bool uav = false;
        SharedTex tex;
        ComPtr<ID3D11UnorderedAccessView> uav11;
    };
    std::vector<Extra> extras;
    Extra* ExtraFor(UINT w, UINT h, DXGI_FORMAT fmt, bool uav);
    // One compute copy on the D3D11 queue from src into the extra's texture, then a D3D12 read of it.
    bool ComputeCopyAndRead(ID3D11ComputeShader* cs, ID3D11ShaderResourceView* src, Extra* e, std::vector<uint8_t>* out, UINT* rowBytes);

    SharedTex shared[kSharedCount];
    SharedTex& colorIn  = shared[static_cast<UINT>(Shared::ColorIn)];
    SharedTex& depthIn  = shared[static_cast<UINT>(Shared::Depth)];
    SharedTex& normalIn = shared[static_cast<UINT>(Shared::Normal)];
    SharedTex& colorOut = shared[static_cast<UINT>(Shared::ColorOut)];
    SharedTex& motionIn = shared[static_cast<UINT>(Shared::Motion)];
    ID3D11Texture2D* pendingMotion = nullptr;   // set by CaptureMotion, copied by CaptureNormal
    // Lighting pass (BridgeLighting.cpp).
    SceneLighting lighting;
    ComPtr<ID3D11ComputeShader> shadowCS;           // ShadowAtlas.hlsl
    ComPtr<ID3D11UnorderedAccessView> shadowUAV;
    bool CreateLightingShared();
    DXGI_FORMAT sharedFormat[kSharedCount] = {};
    std::unique_ptr<IBridgePass> pass;
    CameraConstants camera;

    D12Slot  d12[kRing];
    D11Slot  d11[kRing];
    uint64_t nextValue  = 1;      // next fence value to use
    uint64_t lastD12    = 0;      // last value D3D12 signals
    uint64_t lastWaited = 0;      // last value the D3D12 queue waits for
    uint64_t skew       = 0;
    uint64_t frames     = 0;      // Execute calls that submitted
    // The hand-over cost: how often the D3D11 queue had already executed the copy-in when the CPU
    // submitted the D3D12 list (the GPU then waits on the CPU), and the CPU time from Flush to submit.
    uint32_t caughtUp = 0, handOvers = 0;
    double   flushToSubmitUs = 0;

    bool CreateDevice12(ID3D11Device* dev);
    bool CreateShared(SharedTex& t, DXGI_FORMAT fmt, D3D12_RESOURCE_FLAGS flags,
                      const char* name, UINT w = 0, UINT h = 0);
    bool CreatePass();
    void HarvestPassTimings();
    bool CreateDepthCopy();
    bool SelfTest();
    bool SelfTestOnce();
    bool FenceFromD3D11();
    bool CreateQueries();
    bool CpuWait(uint64_t value, DWORD timeoutMs);
    bool ReadTexture12(ID3D12Resource* tex, uint64_t waitValue, std::vector<uint8_t>* out, UINT* rowBytes);
    D11Slot& SlotFor(uint64_t frame) { return d11[frame % kRing]; }
    void OpenD11Slot(D11Slot& s, uint64_t frame);
};

constexpr DXGI_FORMAT kSharedDepthFormat = DXGI_FORMAT_R32_FLOAT;

// CPU wait until `fence` reaches `value`. The event is auto-reset and may
// carry a wake from an older wait, so the loop re-reads the fence.
inline bool WaitFence(ID3D12Fence* fence, uint64_t value, HANDLE event, DWORD timeoutMs)
{
    if (!fence) return false;
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    while (fence->GetCompletedValue() < value) {
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline) return false;
        fence->SetEventOnCompletion(value, event);
        WaitForSingleObject(event, static_cast<DWORD>(deadline - now));
    }
    return true;
}
} // namespace raw::bridge
