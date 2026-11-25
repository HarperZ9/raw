#pragma once
// RawWaterCausticsHost.h: Native DX12 GPU Compute Physical Water Caustics & Underwater Volumetric God-Ray Solver (SM 6.6 / 6.10).
// Implements Gerstner wave-surface Snell refraction Jacobian focusing with Cauchy RGB chromatic dispersion,
// spectral Pope & Fry Beer-Lambert underwater extinction, 16-step Henyey-Greenstein underwater sun-shafts,
// dynamic resolution auto-reallocation, and hardware ID3D12QueryHeap GPU profiling.
// Original clean-room implementation, MIT License.

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>

namespace raw::bridge
{

struct WaterCausticsDesc {
    uint32_t              initialWidth        = 512u;
    uint32_t              initialHeight       = 512u;
    D3D12_SHADER_BYTECODE customCausticsShader = {}; // optional override; uses raw_water_caustics_trace_cs.h
    D3D12_SHADER_BYTECODE customRaysShader     = {}; // optional override; uses raw_water_underwater_rays_cs.h
};

struct WaterCausticsParams {
    float    timeSec           = 0.0f;                     // Wave simulation time (seconds)
    float    sunDir[3]         = {0.42f, 0.82f, 0.39f};    // Normalized direction toward sun (Y-up)
    float    sunIntensity      = 1.0f;                     // Incident solar irradiance scale
    float    sunColor[3]       = {1.0f, 0.96f, 0.88f};     // Solar RGB tint
    float    bedDepthM         = 2.4f;                     // Mean water depth to riverbed (m)
    float    absorptionRGB[3]  = {0.38f, 0.065f, 0.022f};  // Spectral Beer-Lambert sigma_a (1/m), R > G > B
    float    scatterCoeff      = 0.055f;                   // Underwater volumetric scattering sigma_s (1/m)
    float    waveAmp           = 0.195f;                   // Gerstner wave amplitude scale (m); 0.0 = flat
    float    domainSizeM       = 8.0f;                     // Periodic caustic domain width (m)
    float    dispersionScale   = 1.0f;                     // Cauchy RGB chromatic dispersion scale (1.0 = physical)
    float    mieG              = 0.78f;                    // Henyey-Greenstein forward anisotropy [0..0.92]
    float    camPos[3]         = {0.0f, -2.5f, 0.0f};      // Underwater camera position (m, Y-up)
    float    waterSurfaceY     = 0.0f;                     // Water surface plane altitude (m)
    float    camRight[3]       = {1.0f, 0.0f, 0.0f};       // Orthonormal camera right vector
    float    fovYDeg           = 60.0f;                    // Vertical camera FOV (degrees)
    float    camUp[3]          = {0.0f, 1.0f, 0.0f};       // Orthonormal camera up vector
    float    maxRayDistM       = 24.0f;                    // Maximum underwater shaft march distance (m)
    float    camForward[3]     = {0.0f, 0.0f, 1.0f};       // Orthonormal camera forward vector
    float    shaftIntensity    = 2.2f;                     // Underwater volumetric god-ray intensity
    uint32_t frameIndex        = 0u;                       // Frame counter for IGN raymarch jitter
};

class RawWaterCausticsHost {
public:
    static constexpr uint32_t kRingCount        = 4u;
    static constexpr uint32_t kSlotCausticsUav  = 0u;
    static constexpr uint32_t kSlotRaysUav      = 1u;
    static constexpr uint32_t kSlotCausticsSrv  = 2u;
    static constexpr uint32_t kSlotRaysSrv      = 3u;
    static constexpr uint32_t kTotalDescCount   = 4u;

    RawWaterCausticsHost() = default;
    ~RawWaterCausticsHost();

    RawWaterCausticsHost(const RawWaterCausticsHost&) = delete;
    RawWaterCausticsHost& operator=(const RawWaterCausticsHost&) = delete;

    bool Init(ID3D12Device* dev, const WaterCausticsDesc& desc = {}, std::string* outErr = nullptr);
    bool Ready() const { return m_ready; }

    // Executes cs_caustics_trace -> transition -> cs_underwater_rays.
    // Leaves both CausticsResource and UnderwaterRaysResource in NON_PIXEL_SHADER_RESOURCE | PIXEL_SHADER_RESOURCE.
    bool Execute(ID3D12GraphicsCommandList* cmd,
                 uint32_t width,
                 uint32_t height,
                 const WaterCausticsParams& params);

    ID3D12DescriptorHeap*       DescriptorHeap() const { return m_heap.Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE GetCausticsSRV() const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetUnderwaterRaysSRV() const;
    ID3D12Resource*             GetCausticsResource() const { return m_causticsTex.Get(); }
    ID3D12Resource*             GetUnderwaterRaysResource() const { return m_raysTex.Get(); }
    D3D12_RESOURCE_STATES       CausticsResourceState() const { return m_causticsState; }
    D3D12_RESOURCE_STATES       UnderwaterRaysResourceState() const { return m_raysState; }
    uint32_t                    OutputWidth() const { return m_outW; }
    uint32_t                    OutputHeight() const { return m_outH; }

    void  SetTimestampFrequency(uint64_t hz) { m_tsFreqHz = hz; }
    float GetLastCausticsMs() const { return m_lastCausticsMs; }
    float GetLastRaysMs() const { return m_lastRaysMs; }
    float GetLastTotalMs() const { return m_lastCausticsMs + m_lastRaysMs; }
    bool  HasValidTiming() const { return m_hasValidTiming; }

    void Shutdown();

private:
    bool EnsureTargets(uint32_t width, uint32_t height);
    bool HarvestTimestampTriplet(uint32_t queryStartIdx, float& outCausticsMs, float& outRaysMs) const;
    D3D12_CPU_DESCRIPTOR_HANDLE CpuAt(uint32_t slot) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GpuAt(uint32_t slot) const;

    bool                                         m_ready = false;
    ID3D12Device*                                m_dev   = nullptr;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_heap;
    uint32_t                                     m_descInc = 0u;
    Microsoft::WRL::ComPtr<ID3D12RootSignature>  m_root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>  m_causticsPso;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>  m_raysPso;

    Microsoft::WRL::ComPtr<ID3D12Resource>       m_causticsTex;
    Microsoft::WRL::ComPtr<ID3D12Resource>       m_raysTex;
    D3D12_RESOURCE_STATES                        m_causticsState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES                        m_raysState     = D3D12_RESOURCE_STATE_COMMON;
    uint32_t                                     m_outW          = 0u;
    uint32_t                                     m_outH          = 0u;

    Microsoft::WRL::ComPtr<ID3D12Resource>       m_cbBuf;
    uint8_t*                                     m_cbMapped = nullptr;

    Microsoft::WRL::ComPtr<ID3D12QueryHeap>      m_tsHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource>       m_tsReadback;
    uint64_t                                     m_tsFreqHz       = 0u;
    uint32_t                                     m_frameIdx       = 0u;
    float                                        m_lastCausticsMs = 0.0f;
    float                                        m_lastRaysMs     = 0.0f;
    bool                                         m_hasValidTiming = false;
};

} // namespace raw::bridge
