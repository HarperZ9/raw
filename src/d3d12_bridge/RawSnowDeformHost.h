#pragma once
// RawSnowDeformHost.h: Native DX12 GPU Compute Host for Real-Time 512x512 Snow
// Accumulation, Campfire/Solar Thermal Melt, Slush-to-Black-Ice Refreeze &
// Mass-Conserving Footprint Deformation Field (SM 6.6 / 6.10).
//
// Original clean-room implementation, MIT License.

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>

namespace raw::bridge
{

struct SnowStamp {
    float worldX         = 0.0f;  // World-space X center (meters)
    float worldZ         = 0.0f;  // World-space Z center (meters)
    float radiusM        = 0.35f; // Minor footprint radius (meters)
    float compressDepthM = 0.18f; // Target depression depth (meters)
    float rimRatio       = 0.88f; // Fraction of displaced volume pushed into outer rim [0..1.25]
    float headingRad     = 0.0f;  // Footprint orientation angle in XZ plane (radians)
    float aspectRatio    = 1.0f;  // Major/minor axis ratio (1.0=round boot, 1.8=hoof/ski/wheel)
};

struct SnowHeatSource {
    float worldX     = 0.0f;  // World-space X center (meters)
    float worldZ     = 0.0f;  // World-space Z center (meters)
    float radiusM    = 3.5f;  // Thermal influence radius (meters)
    float heatPowerC = 45.0f; // Peak thermal boost at center (degrees C)
};

struct SnowDeformDesc {
    uint32_t              gridWidth         = 512u;
    uint32_t              gridHeight        = 512u;
    float                 worldSpanM        = 64.0f; // 64m / 512 = 0.125 m/texel
    D3D12_SHADER_BYTECODE customStepShader  = {};    // optional override; uses raw_snow_deform_step_cs.h
    D3D12_SHADER_BYTECODE customNormalShader = {};   // optional override; uses raw_snow_normal_height_cs.h
};

struct SnowDeformParams {
    float           camWorldX       = 0.0f;    // Camera/anchor world X (meters)
    float           camWorldZ       = 0.0f;    // Camera/anchor world Z (meters)
    float           dtSec           = 1.0f / 60.0f;
    float           snowfallRateMps = 0.0f;    // Snowfall accumulation rate (meters/sec)
    float           ambientTempC    = -6.0f;   // Ambient air temperature (degrees C)
    float           solarWarmthC    = 0.0f;    // Direct solar insolation warmth boost (degrees C)
    float           baselineDepthM  = 0.40f;   // Fresh snow depth on reset / newly streamed borders
    float           maxDepthM       = 1.50f;   // Maximum clamped snowpack depth (meters)
    float           windSpeedMps    = 1.5f;    // Surface wind speed for gentle drift smoothing (m/s)
    float           windDirX        = 1.0f;
    float           windDirZ        = 0.0f;
    float           meltCoeff       = 0.008f;  // Melt rate coefficient (m / (degC * sec))
    float           normalStrength  = 1.0f;    // Analytical Sobel slope scale
    bool            resetField      = false;   // Re-initialize entire field to baselineDepthM
    const SnowStamp*      stamps     = nullptr;
    uint32_t              stampCount = 0u;     // Up to 32 simultaneous stamps
    const SnowHeatSource* heatSources = nullptr;
    uint32_t              heatCount   = 0u;    // Up to 16 simultaneous heat sources
};

class RawSnowDeformHost {
public:
    static constexpr uint32_t kCbRingCount    = 4u;
    static constexpr uint32_t kMaxStamps      = 32u;
    static constexpr uint32_t kMaxHeatSources = 16u;

    RawSnowDeformHost() = default;
    ~RawSnowDeformHost();

    RawSnowDeformHost(const RawSnowDeformHost&) = delete;
    RawSnowDeformHost& operator=(const RawSnowDeformHost&) = delete;

    bool Init(ID3D12Device* dev, const SnowDeformDesc& desc = {}, std::string* outErr = nullptr);
    bool Ready() const { return m_ready; }

    // Runs Pass 1 (cs_snow_deform_step) -> UAV->SRV barrier -> Pass 2 (cs_snow_normal_height).
    // Leaves both StateResource and NormalResource in NON_PIXEL_SHADER_RESOURCE state.
    bool Execute(ID3D12GraphicsCommandList* cmd, const SnowDeformParams& params);

    void ResetField() { m_pendingReset = true; }

    ID3D12DescriptorHeap*       DescriptorHeap() const { return m_heap.Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE GetStateSRV() const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetStateSRVCpu() const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetNormalSRV() const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetNormalSRVCpu() const;

    ID3D12Resource*       GetStateResource() const;
    D3D12_RESOURCE_STATES StateResourceState() const;
    ID3D12Resource*       GetNormalResource() const { return m_normalTex.Get(); }
    D3D12_RESOURCE_STATES NormalResourceState() const { return m_normalState; }

    uint32_t Width() const { return m_width; }
    uint32_t Height() const { return m_height; }
    float    WorldSpanM() const { return m_worldSpanM; }
    float    CellSizeM() const { return m_cellSizeM; }
    float    OriginWorldX() const { return static_cast<float>(m_originCellX) * m_cellSizeM; }
    float    OriginWorldZ() const { return static_cast<float>(m_originCellZ) * m_cellSizeM; }

    void  SetTimestampFrequency(uint64_t hz) { m_tsFreqHz = hz; }
    float GetLastStepMs() const { return m_lastStepMs; }
    float GetLastNormalMs() const { return m_lastNormalMs; }
    float GetLastTotalMs() const { return m_lastStepMs + m_lastNormalMs; }
    bool  HasValidTiming() const { return m_hasValidTiming; }

    void Shutdown();

private:
    void Transition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
                    D3D12_RESOURCE_STATES& curState, D3D12_RESOURCE_STATES target);
    bool HarvestTimestampRange(uint32_t idxA, uint32_t idxB, float& outMs) const;
    D3D12_CPU_DESCRIPTOR_HANDLE CpuAt(uint32_t slot) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GpuAt(uint32_t slot) const;

    bool          m_ready        = false;
    bool          m_pendingReset = true;
    ID3D12Device* m_dev          = nullptr;
    uint32_t      m_width        = 512u;
    uint32_t      m_height       = 512u;
    float         m_worldSpanM   = 64.0f;
    float         m_cellSizeM    = 0.125f;
    int32_t       m_originCellX  = 0;
    int32_t       m_originCellZ  = 0;
    bool          m_hasOrigin    = false;
    uint32_t      m_stateIdx     = 0u; // Ping-pong index of most recently written state

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_heap;
    uint32_t                                     m_descInc = 0u;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_stepPso;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_normalPso;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_stateTex[2];
    D3D12_RESOURCE_STATES                  m_stateResState[2] = {D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON};
    Microsoft::WRL::ComPtr<ID3D12Resource> m_normalTex;
    D3D12_RESOURCE_STATES                  m_normalState = D3D12_RESOURCE_STATE_COMMON;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_cbBuf;
    uint8_t*                               m_cbMapped = nullptr;

    Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_tsHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource>  m_tsReadback;
    uint64_t                                m_tsFreqHz       = 0u;
    uint32_t                                m_frameIdx       = 0u;
    float                                   m_lastStepMs     = 0.0f;
    float                                   m_lastNormalMs   = 0.0f;
    bool                                    m_hasValidTiming = false;
};

} // namespace raw::bridge
