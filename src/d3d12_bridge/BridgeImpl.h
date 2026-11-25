#pragma once
// Private state of D3D12Bridge, shared by D3D12Bridge.cpp (setup) and
// BridgeFrame.cpp (per-frame work).
#include "D3D12Bridge.h"
#include "RtScene.h"
#include "RawBindlessHeap.h"
#include "RawMeshHost.h"
#include "RawFluidSimHost.h"
#include "RawFluidCompositeHost.h"
#include "RawRadianceHost.h"
#include "RawXpbdSimHost.h"
#include "RawOverlayHost.h"
#include "RawGpuCullHost.h"
#include "RawOverrideRegistry.h"
#include "RawAsyncCompute.h"
#include "RawIndirectBatch.h"
#include "../worldgen/RawWorldShowcase.h"
#include "../materials/RawMaterialLib.h"
#include "../assets/RawOverrideRegistry.h"
#include <wrl/client.h>
#include <unordered_map>

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
    ComPtr<ID3D11Device>         dev11;
    ComPtr<ID3D11DeviceContext>  ctx11;
    ComPtr<ID3D11Device5>        dev5;    // side-by-side only: shared resources and the fence
    ComPtr<ID3D11DeviceContext4> ctx4;
    // Attached to raw::tx (BridgeConfig::layer): one device and one queue, no fence crossing.
    bool attached = false;
    ComPtr<ID3D12Fence> ownFence;          // the pass's own retirement fence when attached
    uint64_t ownValue = 0;
    bool CreateAttached(SharedTex& t, DXGI_FORMAT fmt, bool uav, const char* name, UINT w, UINT h);
    bool HandToHost(const std::vector<ID3D11Resource*>& extra);
    std::unique_ptr<RtScene> rt;           // attached only
    RawBindlessHeap          bindless;     // RAW-LUMEN 65K SM 6.6 ResourceDescriptorHeap[] pool
    RawMeshHost              meshHost;     // RAW-MESH Hardware DX12 AS+MS PN-Triangle subdivision & terrain host
    RawFluidSimHost          fluidSim;     // Live GPU 64^3 3D Navier-Stokes Fire/Smoke + 256^2 SWE Water solver
    RawFluidCompositeHost    fluidComposite; // Live GPU 64^3 NS Fire/Smoke volumetric compositor onto colorOut
    RawRadianceHost          radianceHost; // Live GPU 4-cascade penumbra-exact Radiance Cascades Screen GI
    std::unique_ptr<RawXpbdSimHost> xpbdSim; // Live GPU XPBD soft-body/hair/cloth/bush-bone solver
    std::unique_ptr<RawOverlayHost> overlayHost; // Opt-in F9 <0.02ms GPU Compute Telemetry & Sparkline HUD
    std::unique_ptr<RawGpuCullHost> gpuCull;     // Live GPU Hi-Z Pyramid + Frustum/Occlusion/Distance Culler
    bool                     fluidReady = false;
    bool                     radianceReady = false;
    bool                     xpbdSimReady = false;
    bool                     xpbdChainsUploaded = false;
    bool                     overlayHostReady = false;
    bool                     overlayHudEnabled = false; // Strictly OFF by default (zero viewport clutter; F9 toggles)
    bool                     overlayPrevF9Down = false;
    bool                     gpuCullReady = false;
    ComPtr<ID3D12CommandAllocator>    fluidAlloc[kRing];
    ComPtr<ID3D12GraphicsCommandList> fluidList;
    ComPtr<ID3D12Fence>               fluidFence;
    HANDLE                            fluidEvent = nullptr;
    uint64_t                          fluidFenceVal = 0;
    uint64_t                          fluidSlotVal[kRing] = {};
    uint32_t                          fluidSlot = 0;
    uint64_t                          fluidStepFrames = 0;
    uint64_t                          fluidCompFrames = 0;
    uint64_t                          fluidLastMs = 0;
    uint32_t                          lastFluidFireEmitters = 0;
    uint32_t                          lastFluidWaterWakes = 0;
    ComPtr<ID3D12CommandAllocator>    radAlloc[kRing];
    ComPtr<ID3D12GraphicsCommandList> radList;
    ComPtr<ID3D12Fence>               radFence;
    HANDLE                            radEvent = nullptr;
    uint64_t                          radFenceVal = 0;
    uint64_t                          radSlotVal[kRing] = {};
    uint32_t                          radSlot = 0;
    uint64_t                          radStepFrames = 0;
    ComPtr<ID3D12CommandAllocator>    xpbdAlloc[kRing];
    ComPtr<ID3D12GraphicsCommandList> xpbdList;
    ComPtr<ID3D12Fence>               xpbdFence;
    HANDLE                            xpbdEvent = nullptr;
    uint64_t                          xpbdFenceVal = 0;
    uint64_t                          xpbdSlotVal[kRing] = {};
    uint32_t                          xpbdSlot = 0;
    uint64_t                          xpbdStepFrames = 0;
    uint64_t                          xpbdLastMs = 0;
    ComPtr<ID3D12CommandAllocator>    overlayAlloc[kRing];
    ComPtr<ID3D12GraphicsCommandList> overlayList;
    ComPtr<ID3D12Fence>               overlayFence;
    HANDLE                            overlayEvent = nullptr;
    uint64_t                          overlayFenceVal = 0;
    uint64_t                          overlaySlotVal[kRing] = {};
    uint32_t                          overlaySlot = 0;
    ComPtr<ID3D12CommandAllocator>    cullAlloc[kRing];
    ComPtr<ID3D12GraphicsCommandList> cullList;
    ComPtr<ID3D12Fence>               cullFence;
    HANDLE                            cullEvent = nullptr;
    uint64_t                          cullFenceVal = 0;
    uint64_t                          cullSlotVal[kRing] = {};
    uint32_t                          cullSlot = 0;
    uint64_t                          cullStepFrames = 0;
    ComPtr<ID3D12Resource>            cullInstUploadBuf;
    uint8_t*                          cullInstUploadMapped = nullptr;
    ComPtr<ID3D12Resource>            cullInstGpuBuf;
    D3D12_RESOURCE_STATES             cullInstGpuState = D3D12_RESOURCE_STATE_COMMON;
    uint32_t                          lastGpuCullInstances = 0;
    float                             frameMsHistory[64] = {};
    uint32_t                          frameMsHistoryHead = 0;
    uint32_t                          frameMsHistoryCount = 0;
    int64_t                           lastFrameQpc = 0;
    int64_t                           qpcFreq = 0;
    float                             lastFrameMs = 0.0f;
    std::string                       telemetryOutDir;
    bool InitFluidSim();
    void ShutdownFluidSim();
    bool StepFluidSimOnQueue(bool isInterior);
    bool InitRadianceCascades();
    void ShutdownRadianceCascades();
    bool ExecuteRadianceScreenGiOnQueue();
    bool InitXpbdSim();
    void ShutdownXpbdSim();
    bool StepXpbdSimOnQueue(bool isInterior);
    bool InitGpuCull();
    void ShutdownGpuCull();
    bool ExecuteGpuCullOnQueue();
    bool InitOverlayHost();
    void ShutdownOverlayHost();
    void PollOverlayHotkey();
    void RecordFrameTimingSample();
    bool ExecuteOverlayHudOnQueue(bool isInterior);
    void EmitLiveTelemetry(bool rtActive);
    std::vector<RawMeshGpuAsset> proceduralMeshes; // Zero-external-asset built-in procedural GPU meshlet library
    raw::worldgen::RawWorldShowcase worldShowcase; // Live beyond-Tamriel 6-biome procedural frontier streaming manager
    raw::worldgen::StreamUpdateStats lastWorldStats{};
    std::unordered_map<uint64_t, TerrainGpuChunk> streamedTerrainGpu;
    uint64_t nextTerrainHandle = 1;
    uint64_t terrainRenderFrames = 0;
    std::vector<raw::materials::GpuMaterialRecord> materialGpuRecords; // Canonical 64-byte physical material SRV buffer
    raw::assets::OverrideRegistry customOverrides; // Non-NIF/Non-DDS handcrafted override + hot-reload registry
    RawOverrideRegistry      overrides;    // Creator hot-swap .glb/.obj override registry (raw_assets/overrides/)
    RawAsyncCompute          asyncCompute; // Split-Submit Hardware DX12 Async Compute queue
    RawIndirectBatch         indirect;     // GPU-Driven ExecuteIndirect DrawIndexed + DispatchMesh signatures
    std::vector<RtDraw> pendingDraws;
    RtTuning rtTune;
    bool CreateRt();
    bool AttachLayer();                     // every shared texture to COMMON, the layer's work submitted
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
    SharedTex waterIn, waterOut;   // attached only, made on first use: the frame with the game's water, and with RAW's (RtWater)
    bool waterFailed = false;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> bed11;   // the bed's depth (CaptureBedDepth), same typeless format as the game's
    ID3D12Resource* bed12 = nullptr;
    DXGI_FORMAT bedSrv = DXGI_FORMAT_UNKNOWN;
    uint64_t bedFrame = ~0ull;                        // frames when it was last copied
    SharedTex preWater;                               // CapturePreWater: the HDR colour before the water composite
    uint64_t preWaterFrame = ~0ull;
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

    // The hand-over probe (Probe.cpp): one timestamp list before the pass on the D3D12 queue and
    // one after, a slot per frame of the ring.
    ComPtr<ID3D12QueryHeap> probeHeap;
    ComPtr<ID3D12Resource> probeRead;
    ComPtr<ID3D12CommandAllocator> probeAlloc;
    ComPtr<ID3D12GraphicsCommandList> probeHead[kRing], probeTail[kRing];
    uint64_t probeFreq = 0, probeFrame[kRing] = {~0ull, ~0ull, ~0ull, ~0ull};
    bool CreateProbe();
    void ProbeHead(uint64_t frame, uint64_t waitValue);
    void ProbeTail(uint64_t frame);
    bool ProbeRead(uint64_t frame, uint64_t d11Freq, uint64_t t5, uint64_t t6, BridgeTimings* r);
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
