#pragma once
// RawLiveWorldHost.h: Native DX12 GPU Compute Host for Real-Time World-Space Compositing
// (`RawLiveWorldComposite.hlsl`) uniting RawSnowDeformHost, RawWaterCausticsHost, RawGrassWindHost,
// world-space glacier facets & conifer micro-depth, and F11 Visual Debug Layers directly onto
// HDR ColorOut (RGBA16F) with strict SceneDepth occlusion and actor/sky exclusion.
// Original clean-room implementation, MIT License.

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>

namespace raw::bridge
{

struct LiveWorldCompositeParams {
    float    clipToWorld[16]       = {};
    float    viewToWorld[12]       = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    float    camPosW[3]            = {};
    float    simTimeSec            = 0.0f;
    float    resScale[2]           = {1.0f, 1.0f};
    float    snowOriginWorldX_m    = 0.0f;
    float    snowOriginWorldZ_m    = 0.0f;
    float    snowSpanM             = 64.0f;
    float    snowStrength          = 0.0f;
    float    snowBaselineDepthM    = 0.35f;
    float    grassOriginWorldX_m   = 0.0f;
    float    grassOriginWorldZ_m   = 0.0f;
    float    grassSpanM            = 64.0f;
    float    grassStrength         = 0.0f;
    float    waterPlaneZ_u         = -14000.0f;
    float    causticsDomainM       = 8.0f;
    float    causticsStrength      = 0.0f;
    float    seasonSnowDepthM      = 0.0f;
    float    sunDir[3]             = {0.42f, 0.30f, 0.85f};
    float    sunIntensity          = 1.0f;
    float    sunCol[3]             = {1.0f, 0.95f, 0.88f};
    float    windXY[2]             = {};
    float    rain01                = 0.0f;
    uint32_t width                 = 0u;
    uint32_t height                = 0u;
    uint32_t activeDebugView       = 0u;    // 0=LiveWorld, 1, 2, 8, 28..33
    bool     isInterior            = false;
    bool     menuPaused            = false;
    bool     lightingDebugActive   = false;
};

struct LiveWorldInputResources {
    ID3D12Resource*       colorOut         = nullptr;
    D3D12_RESOURCE_STATES colorOutState    = D3D12_RESOURCE_STATE_COMMON;
    ID3D12Resource*       depthIn          = nullptr;
    D3D12_RESOURCE_STATES depthInState     = D3D12_RESOURCE_STATE_COMMON;
    ID3D12Resource*       normalIn         = nullptr;
    D3D12_RESOURCE_STATES normalInState    = D3D12_RESOURCE_STATE_COMMON;
    ID3D12Resource*       albedoIn         = nullptr;
    D3D12_RESOURCE_STATES albedoInState    = D3D12_RESOURCE_STATE_COMMON;
    ID3D12Resource*       specularIn       = nullptr;
    D3D12_RESOURCE_STATES specularInState  = D3D12_RESOURCE_STATE_COMMON;
    ID3D12Resource*       snowState        = nullptr;
    D3D12_RESOURCE_STATES snowStateState   = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    ID3D12Resource*       snowNormal       = nullptr;
    D3D12_RESOURCE_STATES snowNormalState  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    ID3D12Resource*       waterCaustics    = nullptr;
    D3D12_RESOURCE_STATES waterCausticsState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    ID3D12Resource*       grassBend        = nullptr;
    D3D12_RESOURCE_STATES grassBendState   = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
};

class RawLiveWorldHost {
public:
    static constexpr uint32_t kRingCount      = 4u;
    static constexpr uint32_t kDescsPerSlot   = 9u; // t0..t7 + u0

    RawLiveWorldHost() = default;
    ~RawLiveWorldHost();

    RawLiveWorldHost(const RawLiveWorldHost&) = delete;
    RawLiveWorldHost& operator=(const RawLiveWorldHost&) = delete;

    // Initializes using CMake-generated embedded DXIL header (raw_live_world_composite_cs.h).
    bool Init(ID3D12Device* dev, std::string* outErr = nullptr);

    // Initializes using caller-supplied DXIL bytecode (for standalone tests or hot-reload).
    bool InitWithBlob(ID3D12Device* dev, D3D12_SHADER_BYTECODE csBlob, std::string* outErr = nullptr);

    void Shutdown();
    bool Ready() const { return m_ready; }

    // Records live world-space compositing into an open command list, transitioning every resource
    // cleanly to its shader state and restoring it to its original caller-provided state on exit.
    bool RecordComposite(ID3D12GraphicsCommandList*      cmd,
                         const LiveWorldInputResources&  res,
                         const LiveWorldCompositeParams& params);

    // Self-contained per-frame execution on a direct command queue with internal 4-slot ring & fence.
    bool Composite(ID3D12CommandQueue*             queue,
                   const LiveWorldInputResources&  res,
                   const LiveWorldCompositeParams& params);

    void  SetTimestampFrequency(uint64_t hz) { m_tsFreqHz = hz; }
    float GetLastCompositeMs() const { return m_lastMs; }
    bool  HasValidTiming() const { return m_hasValidTiming; }

private:
    bool HarvestTimestampPair(uint32_t queryStartIdx, float& outMs) const;
    bool CreateFallbackTextures();

    bool                                        m_ready = false;
    Microsoft::WRL::ComPtr<ID3D12Device>        m_dev;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_heap;
    UINT                                        m_descInc = 0u;

    Microsoft::WRL::ComPtr<ID3D12Resource>      m_cbBuf;
    uint8_t*                                    m_cbMapped = nullptr;

    // 1x1 zero-initialized safe fallback textures for any optional null SRV input:
    Microsoft::WRL::ComPtr<ID3D12Resource>      m_fallbackRgba16f;
    Microsoft::WRL::ComPtr<ID3D12Resource>      m_fallbackRgba8;

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator>    m_alloc[kRingCount];
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_list;
    Microsoft::WRL::ComPtr<ID3D12Fence>               m_fence;
    HANDLE                                            m_event = nullptr;
    uint64_t                                          m_fenceVal = 0u;
    uint64_t                                          m_slotVal[kRingCount] = {};
    uint32_t                                          m_frameIdx = 0u;

    Microsoft::WRL::ComPtr<ID3D12QueryHeap>           m_tsHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource>            m_tsReadback;
    uint64_t                                          m_tsFreqHz       = 0u;
    float                                             m_lastMs         = 0.0f;
    bool                                              m_hasValidTiming = false;
};

} // namespace raw::bridge
