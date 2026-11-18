#pragma once
//=============================================================================
//  D3D12Bridge: stage 1 of RAW's move to Direct3D 12.
//
//  The game keeps its D3D11 device. RAW opens a D3D12 device on the same
//  adapter (matched by LUID), shares three textures and one fence with the
//  D3D11 device, and runs its own passes on the D3D12 queue:
//
//    D3D11: depth -> shared R32F depth        (end of the opaque pass; a tiny
//           compute pass, since D3D11 cannot open D3D12 depth-stencils)
//    D3D11: copy scene -> shared colour, Signal(fence, n)
//    D3D12: Wait(fence, n), render pass -> shared output, Signal(fence, n+1)
//    D3D11: Wait(fence, n+1), copy shared output -> the game's frame
//
//  The D3D11 side issues copies, Signal and Wait, plus the depth compute
//  pass, which saves and restores the compute-stage bindings it uses. The
//  proxy's state cache covers only graphics stages, so it stays valid.
//
//  This file has no dependency on the proxy, so the standalone round-trip
//  test drives the same code the game does.
//=============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdint>
#include <memory>

namespace raw::bridge
{

using LogFn = void (*)(const char* fmt, ...);

// 16 floats, uploaded as root constants (register b0 in DepthFog.hlsl).
struct PassConstants
{
    float fogColor[3] = {0.55f, 0.62f, 0.80f};
    float maxOpacity  = 0.85f;
    float density     = 0.00012f;   // per game unit
    float nearZ       = 15.0f;
    float farZ        = 353840.0f;
    float mode        = 1.0f;       // 0 identity, 1 haze, 2 linear depth view
    float marker      = 0.0f;       // 1 draws four swatches top-left
    float frame       = 0.0f;
    float pad[2]      = {};
    float dims[4]     = {};         // filled by the bridge
};
static_assert(sizeof(PassConstants) == 64, "PassConstants must be 16 floats");

struct BridgeConfig
{
    UINT        width          = 0;
    UINT        height         = 0;
    DXGI_FORMAT colorFormat    = DXGI_FORMAT_R8G8B8A8_UNORM;  // the game's frame format
    bool        debugLayer     = false;
    DWORD       cpuWaitTimeout = 2000;  // ms; a longer wait marks the bridge unhealthy
    bool        probeStages    = false; // log ProbeFenceSupport between setup stages
};

// GPU times for one frame, in milliseconds.
struct BridgeTimings
{
    uint64_t frame        = 0;
    double   depthCopyMs  = 0;  // D3D11: depth -> shared depth (compute pass)
    double   copyInMs     = 0;  // D3D11: scene -> shared colour
    double   stallMs      = 0;  // D3D11 queue idle between Signal and the end of Wait
    double   copyOutMs    = 0;  // D3D11: shared output -> frame
    double   passMs       = 0;  // D3D12: the pass itself
    double   syncMs       = 0;  // stallMs - passMs: what the fence round trip costs
    double   bridgeCostMs = 0;  // depthCopy + copyIn + copyOut + sync
};

// A pass that runs on the D3D12 side. DepthFogPass is the first; the
// superframe native/ backend is meant to sit behind this same seam.
struct PassIO
{
    D3D12_GPU_DESCRIPTOR_HANDLE inputs;   // t0 colour, t1 depth
    D3D12_CPU_DESCRIPTOR_HANDLE output;   // RTV of the shared output
    UINT width, height;
};

class IBridgePass
{
public:
    virtual ~IBridgePass() = default;
    virtual const char* Name() const = 0;
    virtual bool Create(ID3D12Device* dev, DXGI_FORMAT outFormat, LogFn log) = 0;
    virtual void Record(ID3D12GraphicsCommandList* cl, const PassIO& io,
                        const PassConstants& k) = 0;
};

std::unique_ptr<IBridgePass> MakeDepthFogPass();

// Logs whether dev can create and signal D3D11 fences (local and shared) right
// now, with the device flags, feature level, thread and context identity.
bool ProbeFenceSupport(ID3D11Device* dev, ID3D11DeviceContext* ctx, LogFn log, const char* where);

class D3D12Bridge
{
public:
    D3D12Bridge();
    ~D3D12Bridge();
    D3D12Bridge(const D3D12Bridge&) = delete;
    D3D12Bridge& operator=(const D3D12Bridge&) = delete;

    // dev/ctx are the game's real (unwrapped) D3D11 objects.
    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                    const BridgeConfig& cfg, LogFn log);
    void Shutdown();
    bool Ready() const;
    bool Matches(UINT w, UINT h, DXGI_FORMAT color) const;
    const BridgeConfig& Config() const;

    // D3D11 queue: copy the game's depth into the shared depth texture.
    // depthSrv is any float-readable view of the depth (R24_UNORM_X8, R32F).
    // Call at the end of the opaque pass.
    bool CaptureDepth(ID3D11ShaderResourceView* depthSrv);

    // One round trip. colorSrc and colorDst may be the same texture (the
    // backbuffer). Returns false if the bridge is not ready.
    bool Execute(ID3D11Resource* colorSrc, ID3D11Resource* colorDst,
                 const PassConstants& k);

    // Timings for a frame executed earlier. block=false returns false if the
    // GPU has not finished it; the game polls the oldest ring slot.
    bool ReadTimings(uint64_t frame, bool block, BridgeTimings* out);
    uint64_t FramesExecuted() const;

    // Test controls. A skew makes the D3D12 queue wait for a fence value
    // the D3D11 side never signals; the round-trip test uses it to prove a
    // broken handshake is caught.
    void     SetWaitSkew(uint64_t skew);
    bool     WaitForD3D12(DWORD timeoutMs);   // CPU wait for the last D3D12 signal
    void     ForceSignal(uint64_t value);     // CPU-side signal, to release a skewed wait
    uint64_t LastD3D12Value() const;
    uint64_t LastWaitedValue() const;
    uint64_t CompletedValue() const;      // the shared fence as the GPU sees it

    struct Impl;
private:
    std::unique_ptr<Impl> m;
};

} // namespace raw::bridge
