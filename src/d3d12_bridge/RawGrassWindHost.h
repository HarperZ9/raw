#pragma once
// RawGrassWindHost.h: Native DX12 GPU Compute 256x256 (65,536-Blade) Dynamic Grass/Undergrowth Wind,
// Seasonal Snow-Weight Droop, Wildfire Char & Critically-Damped Actor-Trample Solver (SM 6.6 / 6.10).
// Original clean-room implementation, MIT License.

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>

namespace raw::bridge
{

struct GrassTrampleCapsule {
    float startX          = 0.0f;  // Capsule segment start world X (meters)
    float startZ          = 0.0f;  // Capsule segment start world Z (meters)
    float endX            = 0.0f;  // Capsule segment end world X (meters)
    float endZ            = 0.0f;  // Capsule segment end world Z (meters)
    float velX            = 0.0f;  // Actor/horse/cart travel velocity X (m/s)
    float velZ            = 0.0f;  // Actor/horse/cart travel velocity Z (m/s)
    float radiusM         = 0.90f; // Trample influence radius (meters)
    float flattenAngleRad = 1.22f; // Target blade bend angle under capsule core (radians)
};

struct GrassHeatSource {
    float worldX    = 0.0f;  // Wildfire/campfire center world X (meters)
    float worldZ    = 0.0f;  // Wildfire/campfire center world Z (meters)
    float radiusM   = 3.0f;  // Thermal scorch influence radius (meters)
    float heatPower = 2.5f;  // Scorch intensity multiplier
};

struct GrassWindDesc {
    uint32_t              gridWidth        = 256u;  // 256x256 = 65,536 grass bend cells
    uint32_t              gridHeight       = 256u;
    float                 worldSpanM       = 64.0f; // 64m / 256 = 0.25 m/texel
    D3D12_SHADER_BYTECODE customStepShader = {};    // optional override; uses raw_grass_wind_step_cs.h
};

struct GrassWindParams {
    float                      camWorldX      = 0.0f;   // Anchor world X for toroidal scrolling (meters)
    float                      camWorldZ      = 0.0f;   // Anchor world Z for toroidal scrolling (meters)
    float                      dtSec          = 1.0f / 60.0f;
    float                      timeSec        = 0.0f;   // Simulation clock for travelling gust waves (seconds)
    float                      windSpeedMps   = 4.0f;   // Mean wind speed (m/s)
    float                      windDirX       = 1.0f;   // Wind direction X
    float                      windDirZ       = 0.0f;   // Wind direction Z
    float                      gustStrength   = 0.75f;  // Dual-harmonic travelling gust & curl amplitude [0..1.25]
    float                      springOmega    = 2.8f;   // Critically-damped spring recovery frequency (rad/s)
    float                      snowLoad01     = 0.0f;   // Seasonal snow-weight droop load [0..1]
    float                      charRateScale  = 0.60f;  // Wildfire char accumulation rate multiplier
    bool                       resetField     = false;  // Clear trample/char field to upright lush grass
    const GrassTrampleCapsule* capsules       = nullptr;
    uint32_t                   capsuleCount   = 0u;     // Up to 32 simultaneous trample capsules
    const GrassHeatSource*     heatSources    = nullptr;
    uint32_t                   heatCount      = 0u;     // Up to 16 simultaneous wildfire heat sources
};

class RawGrassWindHost {
public:
    static constexpr uint32_t kCbRingCount    = 4u;
    static constexpr uint32_t kMaxCapsules    = 32u;
    static constexpr uint32_t kMaxHeatSources = 16u;

    RawGrassWindHost() = default;
    ~RawGrassWindHost();

    RawGrassWindHost(const RawGrassWindHost&) = delete;
    RawGrassWindHost& operator=(const RawGrassWindHost&) = delete;

    bool Init(ID3D12Device* dev, const GrassWindDesc& desc = {}, std::string* outErr = nullptr);
    bool Ready() const { return m_ready; }

    // Steps the 256x256 (65,536-blade) grass wind, droop, char & actor-trample field.
    // Leaves both BendFieldResource and TrampleStateResource in NON_PIXEL_SHADER_RESOURCE | PIXEL_SHADER_RESOURCE.
    bool Execute(ID3D12GraphicsCommandList* cmd, const GrassWindParams& params);

    void ResetField() { m_pendingReset = true; }

    ID3D12DescriptorHeap*       DescriptorHeap() const { return m_heap.Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE GetBendFieldSRV() const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetBendFieldSRVCpu() const;
    ID3D12Resource*             GetBendFieldResource() const;
    D3D12_RESOURCE_STATES       BendFieldResourceState() const;
    ID3D12Resource*             GetTrampleStateResource() const;
    D3D12_RESOURCE_STATES       TrampleStateResourceState() const;

    uint32_t Width() const { return m_width; }
    uint32_t Height() const { return m_height; }
    uint32_t BladeCount() const { return m_width * m_height; }
    float    WorldSpanM() const { return m_worldSpanM; }
    float    CellSizeM() const { return m_cellSizeM; }
    float    OriginWorldX() const { return static_cast<float>(m_originCellX) * m_cellSizeM; }
    float    OriginWorldZ() const { return static_cast<float>(m_originCellZ) * m_cellSizeM; }

    void  SetTimestampFrequency(uint64_t hz) { m_tsFreqHz = hz; }
    float GetLastStepMs() const { return m_lastStepMs; }
    bool  HasValidTiming() const { return m_hasValidTiming; }

    void Shutdown();

private:
    void Transition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
                    D3D12_RESOURCE_STATES& curState, D3D12_RESOURCE_STATES target);
    bool HarvestTimestampPair(uint32_t queryStartIdx, float& outMs) const;
    D3D12_CPU_DESCRIPTOR_HANDLE CpuAt(uint32_t slot) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GpuAt(uint32_t slot) const;

    bool          m_ready        = false;
    bool          m_pendingReset = true;
    ID3D12Device* m_dev          = nullptr;
    uint32_t      m_width        = 256u;
    uint32_t      m_height       = 256u;
    float         m_worldSpanM   = 64.0f;
    float         m_cellSizeM    = 0.25f;
    int32_t       m_originCellX  = 0;
    int32_t       m_originCellZ  = 0;
    bool          m_hasOrigin    = false;
    uint32_t      m_curIdx       = 0u; // Ping-pong index holding most recently written output

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_heap;
    uint32_t                                     m_descInc = 0u;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_stepPso;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_trampleTex[2];
    D3D12_RESOURCE_STATES                  m_trampleState[2] = {D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON};
    Microsoft::WRL::ComPtr<ID3D12Resource> m_bendTex[2];
    D3D12_RESOURCE_STATES                  m_bendState[2] = {D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON};

    Microsoft::WRL::ComPtr<ID3D12Resource> m_cbBuf;
    uint8_t*                               m_cbMapped = nullptr;

    Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_tsHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource>  m_tsReadback;
    uint64_t                                m_tsFreqHz       = 0u;
    uint32_t                                m_frameIdx       = 0u;
    float                                   m_lastStepMs     = 0.0f;
    bool                                    m_hasValidTiming = false;
};

} // namespace raw::bridge
