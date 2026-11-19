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
//    D3D12: the pass records first; after the D3D11 Signal it submits
//           Wait(fence, n), its list, Signal(fence, n+1)
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
#include <vector>

#include "SceneLighting.h"

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

// raw::tx's host access (Translate.h), when the game runs on the layer: the bridge then uses the
// layer's own D3D12 device and queue, its textures are the layer's, and no fence crosses.
struct HostLayer
{
    ID3D12Device*       device = nullptr;
    ID3D12CommandQueue* queue  = nullptr;
    ID3D12Resource* (*native)(ID3D11Resource*) = nullptr;
    HRESULT (*prepare)(ID3D11DeviceContext*, ID3D11Resource* const*, UINT, D3D12_RESOURCE_STATES) = nullptr;
};

struct BridgeConfig
{
    UINT        width          = 0;
    UINT        height         = 0;
    DXGI_FORMAT colorFormat    = DXGI_FORMAT_R8G8B8A8_UNORM;  // the game's frame format
    bool        debugLayer     = false;
    DWORD       cpuWaitTimeout = 2000;  // ms; a longer wait marks the bridge unhealthy
    bool        probeStages    = false; // log ProbeFenceSupport between setup stages
    HostLayer   layer;                  // set: attached to raw::tx
    bool        handoverFlush  = false; // Flush the D3D11 context right after the copy-out (docs/LIGHTING.md, indoor cost)
    // The game's normal target format, or UNKNOWN to share no normals.
    DXGI_FORMAT normalFormat   = DXGI_FORMAT_UNKNOWN;
    bool        enginePass     = false; // run the Playground engine's pass instead of the depth haze
    DWORD       passRetireMs   = 15000; // how long the engine pass waits for the frame before
    // The game's motion-vector target format (R16G16_FLOAT), or UNKNOWN to share none.
    DXGI_FORMAT motionFormat   = DXGI_FORMAT_UNKNOWN;
    bool        realtimeAo     = false; // with enginePass: the engine's real-time AO instead of its reference GTAO
    // The engine's deferred lighting (LightingPass.cpp). The bridge then runs at the end of the
    // opaque pass on the HDR scene colour (colorFormat R16G16B16A16_FLOAT) and also shares
    // RAW's albedo and specular targets, the game's shadow mask and its sun cascades.
    bool        lightingPass   = false;
    UINT        shadowMapSize  = 4096;  // one cascade's side; the atlas holds two side by side
};

// GPU time of one retired submission, by the pass's own timestamps.
struct PassTimings
{
    uint64_t submission = 0;        // which Submit, counted from 0
    double totalMs = 0;
    double stageMs[4] = {};         // pass-defined stages; unused ones stay 0
    const char* stageName[4] = {};
};

// GPU times for one frame, in milliseconds.
struct BridgeTimings
{
    uint64_t frame        = 0;
    double   depthCopyMs  = 0;  // D3D11: depth -> shared depth (compute pass)
    double   copyInMs     = 0;  // D3D11: scene -> shared colour
    double   stallMs      = 0;  // D3D11 queue idle between Signal and the end of Wait
    double   copyOutMs    = 0;  // D3D11: shared output -> frame
    double   normalCopyMs = 0;  // D3D11: normals -> shared normals
    double   passMs       = 0;  // D3D12: the pass itself, by the pass's own timestamps
    PassTimings pass;           // the pass's stages
    double   syncMs       = 0;  // stallMs - passMs: what the fence round trip costs
    double   bridgeCostMs = 0;  // depthCopy + normalCopy + copyIn + copyOut + sync
    // The hand-over on one clock (Probe.cpp): D3D11 and D3D12 timestamps of the same GPU, checked
    // by their order (the D3D11 signal, the D3D12 queue's first command, its last, the D3D11 wake).
    bool     probeOk      = false;  // the four times came in that order
    double   wakeInMs     = 0;  // D3D11 copy-in done -> the D3D12 queue starts (fence wake)
    double   d12SpanMs    = 0;  // the D3D12 queue: first command -> after the pass and its signal
    double   wakeOutMs    = 0;  // D3D12 done -> the D3D11 queue runs again (fence wake)
};

// The textures both devices share. Each is render-target capable with
// simultaneous access and sits in D3D12_RESOURCE_STATE_COMMON whenever no
// D3D12 submission is running.
enum class Shared : UINT { ColorIn, Depth, Normal, ColorOut, Motion, Albedo, Specular, ShadowMask, ShadowAtlas, Emissive, Count };
constexpr UINT kSharedCount = static_cast<UINT>(Shared::Count);

// What the bridge hands a pass: its device, its queue, its fence and the
// shared textures. The pass records and submits on them and owns none of them.
struct HostResources
{
    ID3D12Device*       device = nullptr;
    ID3D12CommandQueue* queue  = nullptr;   // the bridge's direct queue
    ID3D12Fence*        fence  = nullptr;   // shared with D3D11
    ID3D12Resource*     tex[kSharedCount] = {};
    DXGI_FORMAT         format[kSharedCount] = {};
    UINT                width = 0, height = 0;
    DWORD               retireTimeoutMs = 15000;
    UINT                shadowSide = 0;     // lighting pass: one cascade's side in the atlas
    LogFn               log = nullptr;
};

// The game camera for the frame, from the game's own per-frame constants.
// valid=false means the bridge has no projection and a pass that needs one
// must leave the frame unchanged.
struct CameraConstants
{
    bool  valid = false;
    float proj[16] = {};        // view to clip, row-major as the game stores it
    float tanHalfFovY = 0, aspect = 0, nearZ = 0, farZ = 0;
};

struct FrameConstants
{
    PassConstants   k;
    CameraConstants camera;
    const SceneLighting* lighting = nullptr;   // lighting pass: the frame's decoded lights; null otherwise
    bool            hasSurface = false;  // the shared albedo, specular and shadow-mask textures hold this frame
    bool            hasShadows = false;  // the shared shadow atlas holds this frame's sun cascades
    bool            hasNormal = false;   // the shared normal texture holds this frame's normals
    bool            hasMotion = false;   // the shared motion texture holds this frame's motion vectors
};

// A pass that runs on the D3D12 side. One frame, in the order the game
// process needs (docs/D3D12_BRIDGE.md, "What broke, and the fix"):
//
//   Record()    record and close this frame's commands; nothing is queued
//   the bridge signals the shared fence from D3D11 and flushes
//   Submit()    queue Wait(waitValue), the recorded list, queue Signal(signalValue)
//
// Record() of the next frame may wait for the last submission to retire.
// DepthFogPass is the built-in pass; EnginePass puts the Playground engine's
// RHI (superframe native/) behind the same seam.
class IBridgePass
{
public:
    virtual ~IBridgePass() = default;
    virtual const char* Name() const = 0;
    virtual bool Create(const HostResources& host) = 0;
    virtual bool Record(const FrameConstants& frame) = 0;
    virtual bool Submit(uint64_t waitValue, uint64_t signalValue) = 0;
    // CPU wait for the last submission. False when it did not end in time.
    virtual bool Retire(DWORD timeoutMs) = 0;
    // Timings of the most recent retired submission; false when there is none yet.
    virtual bool LastTimings(PassTimings* out) = 0;
};

// Knobs of the engine pass. Zero keeps the engine's own default.
struct EngineTuning
{
    float    radius   = 0;     // GTAO radius in game units (the engine's default is 0.5)
    uint32_t slices   = 0;     // GTAO slices (16)
    uint32_t steps    = 0;     // GTAO steps a side (16)
    float    strength = 1.0f;  // how much of the visibility the composite applies
    // The real-time pass only (RealtimeAoPass.cpp). Zero slices and steps mean 2 and 16.
    bool     noise = true;            // blue-noise slice turn and step offset
    bool     temporal = true;         // accumulate over frames along the motion vectors
    bool     levels = true;           // far steps read the coarser depth levels
    float    maxHistory = 32.0f;      // frames the running mean holds
    float    levelBias = 1.0f;        // depth level of a step = log2(its spacing in half-size texels) - levelBias
    float    depthTolerance = 0.1f;   // history further than this fraction of the depth is rejected
    float    motionScale[2] = {1.0f, 1.0f};   // previous uv = uv + motion * scale (a wrong value is a test control)
    bool     resetHistory = false;    // drop the accumulated frames at the next frame
};

std::unique_ptr<IBridgePass> MakeDepthFogPass();
// Null when the build carries no engine (RAW_ENGINE=OFF).
std::unique_ptr<IBridgePass> MakeEnginePass();

bool EnginePassTune(IBridgePass* pass, const EngineTuning& t);
bool EnginePassReadVisibility(IBridgePass* pass, std::vector<float>* out);
// The engine's real-time AO behind the same seam; null and false without an engine.
std::unique_ptr<IBridgePass> MakeRealtimeAoPass();
bool RealtimeAoTune(IBridgePass* pass, const EngineTuning& t);
bool RealtimeAoReadVisibility(IBridgePass* pass, std::vector<float>* out);
std::unique_ptr<IBridgePass> MakeLightingPass();   // LightingPass.h; null without an engine

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

    // D3D11 queue: copy the game's normal target into the shared normal
    // texture. Formats must match BridgeConfig::normalFormat.
    bool CaptureNormal(ID3D11Texture2D* normalTex);

    // The game's motion-vector target for this frame. Call before CaptureNormal:
    // the copy runs with the normal copy and is timed with it.
    bool CaptureMotion(ID3D11Texture2D* motionTex);

    // The camera for the next Execute.
    void SetCamera(const CameraConstants& camera);

    // Lighting pass only (BridgeLighting.cpp). RAW's albedo and specular targets and the game's
    // shadow mask, all full size RGBA8, into their shared textures.
    // emissive: RAW's emissive target (soft-lit draws: the soft-lighting colour); may be null.
    bool CaptureSurface(ID3D11Texture2D* albedo, ID3D11Texture2D* specular, ID3D11Texture2D* shadowMask, ID3D11Texture2D* emissive = nullptr);
    // The game's sun cascades (a 2D array of 16-bit depth, one layer a cascade) into the shared
    // R32F atlas, cascade k at x offset k * shadowMapSize. A small compute copy on the D3D11 queue.
    bool CaptureShadows(ID3D11Texture2D* cascades);
    // The decoded lights of this frame for the next Execute; the bridge keeps a copy.
    void SetLighting(const SceneLighting& lighting);

    // Engine pass only: its knobs, and the visibility it computed for the last
    // frame (one float a pixel, top row first). False without an engine pass.
    bool TuneEngine(const EngineTuning& t);
    IBridgePass* ActivePass();   // the pass behind the seam, for its own knobs (LightingPassTune)
    bool ReadEngineVisibility(std::vector<float>* out);

    // A D3D12-side read of a shared texture, as tight rows: proof of what
    // crossed the bridge. Waits for the queue. Slow; for captures and tests.
    bool ReadShared(Shared which, std::vector<uint8_t>* out, UINT* rowBytes);

    // One round trip. colorSrc and colorDst may be the same texture (the
    // backbuffer). Returns false if the bridge is not ready.
    bool Execute(ID3D11Resource* colorSrc, ID3D11Resource* colorDst,
                 const PassConstants& k);

    // Any other game buffer, on demand: copies mip 0 of one slice of a D3D11
    // texture (an array layer or a cube face) into a texture both devices
    // share, then reads that texture on D3D12. Colour and 16-bit depth cross
    // as their own bytes; D24S8 crosses as R32_FLOAT depth. `sharedFormat`
    // is the format on the D3D12 side. Slow: for captures and tests.
    bool ShareAndRead(ID3D11Texture2D* src, UINT slice, std::vector<uint8_t>* out, UINT* rowBytes,
                      DXGI_FORMAT* sharedFormat);

    // The stencil byte of a D24S8 buffer, as R8_UINT. Same contract as ShareAndRead.
    bool ShareStencilAndRead(ID3D11Texture2D* depthStencil, std::vector<uint8_t>* out, UINT* rowBytes);
    // A 3D texture, as a 2D atlas of its depth slices: slice z at tile (z % columns, z / columns).
    // The atlas keeps the volume's format (typeless forms cross as their float or unorm form).
    bool ShareVolumeAndRead(ID3D11Texture3D* volume, std::vector<uint8_t>* out, UINT* rowBytes, UINT* columns,
                            DXGI_FORMAT* sharedFormat);

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
