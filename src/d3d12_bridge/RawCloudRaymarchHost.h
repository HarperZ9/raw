#pragma once
// RawCloudRaymarchHost.h: Native DX12 GPU Compute Volumetric Cloud Raymarching Solver (SM 6.6 / 6.10).
// Implements Earth-curved spherical cloud shell raymarching, Beer-Powder multi-scattering,
// dual-lobe Henyey-Greenstein silver-lining phase, ping-pong temporal reprojection,
// dynamic resolution auto-reallocation, and hardware ID3D12QueryHeap GPU profiling.
// Original clean-room implementation, MIT License.

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>

namespace raw::bridge
{

struct CloudRaymarchDesc {
    uint32_t              initialWidth  = 480u;
    uint32_t              initialHeight = 270u;
    D3D12_SHADER_BYTECODE customShader  = {}; // optional override; uses embedded raw_cloud_raymarch_cs.h when empty
};

struct CloudRaymarchParams {
    float    camPos[3]        = {0.0f, 50.0f, 0.0f};        // Camera world position (m, Y-up)
    float    camRight[3]      = {1.0f, 0.0f, 0.0f};         // Orthonormal camera right vector
    float    camUp[3]         = {0.0f, 1.0f, 0.0f};         // Orthonormal camera up vector
    float    camForward[3]    = {0.0f, 0.0f, 1.0f};         // Orthonormal camera forward vector
    float    fovYDeg          = 60.0f;                      // Vertical FOV in degrees
    float    timeSec          = 0.0f;                       // Simulation time in seconds
    float    planetRadiusM    = 6371000.0f;                 // Earth radius (6,371 km)
    float    cloudBaseM       = 1500.0f;                    // Cloud layer base altitude (1.5 km)
    float    cloudTopM        = 5500.0f;                    // Cloud layer top altitude (5.5 km)
    float    cloudCoverage    = 0.55f;                      // Cloud coverage [0..1] (0 = clear sky)
    float    densityScale     = 0.0022f;                    // Extinction coefficient scale (1/m)
    float    anvilFactor      = 0.35f;                      // Upper cumulus anvil flare [0..1]
    float    historyWeight    = 0.75f;                      // Temporal reprojection blend [0..0.85]
    float    noiseScale       = 1.0f / 9000.0f;             // World-space cloud noise frequency (1/m)
    float    windVel[3]       = {18.0f, 0.0f, 6.0f};        // Wind velocity (m/s)
    float    sunDir[3]        = {0.0f, 0.30f, 0.95394f};    // Normalized direction toward sun
    float    sunColor[3]      = {1.0f, 0.94f, 0.84f};       // Sun RGB color
    float    sunIntensity     = 6.5f;                       // Sun irradiance scale
    float    skyZenith[3]     = {0.22f, 0.42f, 0.78f};      // Sky zenith ambient tint
    float    skyHorizon[3]    = {0.58f, 0.68f, 0.82f};      // Sky horizon ambient tint
    float    ambientIntensity = 0.45f;                      // Sky ambient fill scale
    uint32_t frameIndex       = 0u;                         // Frame index for IGN jitter cycling
};

class RawCloudRaymarchHost {
public:
    static constexpr uint32_t kRingCount        = 4u;
    static constexpr uint32_t kDescsPerRingSlot = 2u; // t0 PrevHistory, u0 CloudOut
    static constexpr uint32_t kSlotExtOutSrv0   = kRingCount * kDescsPerRingSlot;
    static constexpr uint32_t kSlotExtOutSrv1   = kSlotExtOutSrv0 + 1u;
    static constexpr uint32_t kTotalDescCount   = kSlotExtOutSrv1 + 1u;

    RawCloudRaymarchHost() = default;
    ~RawCloudRaymarchHost();

    RawCloudRaymarchHost(const RawCloudRaymarchHost&) = delete;
    RawCloudRaymarchHost& operator=(const RawCloudRaymarchHost&) = delete;

    bool Init(ID3D12Device* dev, const CloudRaymarchDesc& desc = {}, std::string* outErr = nullptr);
    bool Ready() const { return m_ready; }

    // Raymarches volumetric clouds into ping-pong RGBA16F output (auto-reallocated on resize)
    // and leaves OutputResource in NON_PIXEL_SHADER_RESOURCE | PIXEL_SHADER_RESOURCE.
    bool Execute(ID3D12GraphicsCommandList* cmd,
                 uint32_t width,
                 uint32_t height,
                 const CloudRaymarchParams& params);

    ID3D12DescriptorHeap*       DescriptorHeap() const { return m_heap.Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE GetOutputSRV() const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetOutputSRVCpu() const;
    ID3D12Resource*             GetOutputResource() const { return m_pingPong[m_lastOutIdx].Get(); }
    D3D12_RESOURCE_STATES       OutputResourceState() const { return m_pingState[m_lastOutIdx]; }
    uint32_t                    OutputWidth() const { return m_outW; }
    uint32_t                    OutputHeight() const { return m_outH; }

    // Invalidates temporal history so the next Execute() computes a fresh unblended frame.
    void ResetHistory() { m_historyValid = false; }

    void  SetTimestampFrequency(uint64_t hz) { m_tsFreqHz = hz; }
    float GetLastCloudMs() const { return m_lastCloudMs; }
    bool  HasValidTiming() const { return m_hasValidTiming; }

    void Shutdown();

private:
    bool EnsureTargets(uint32_t width, uint32_t height);
    bool HarvestTimestampPair(uint32_t queryStartIdx, float& outMs) const;
    D3D12_CPU_DESCRIPTOR_HANDLE CpuAt(uint32_t slot) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GpuAt(uint32_t slot) const;

    bool                                         m_ready = false;
    ID3D12Device*                                m_dev   = nullptr;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_heap;
    uint32_t                                     m_descInc = 0u;
    Microsoft::WRL::ComPtr<ID3D12RootSignature>  m_root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>  m_pso;

    Microsoft::WRL::ComPtr<ID3D12Resource>       m_pingPong[2];
    D3D12_RESOURCE_STATES                        m_pingState[2] = {D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON};
    uint32_t                                     m_outW         = 0u;
    uint32_t                                     m_outH         = 0u;
    uint32_t                                     m_writeIdx     = 0u;
    uint32_t                                     m_lastOutIdx   = 0u;
    bool                                         m_historyValid = false;

    Microsoft::WRL::ComPtr<ID3D12Resource>       m_cbBuf;
    uint8_t*                                     m_cbMapped = nullptr;

    Microsoft::WRL::ComPtr<ID3D12QueryHeap>      m_tsHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource>       m_tsReadback;
    uint64_t                                     m_tsFreqHz       = 0u;
    uint32_t                                     m_frameIdx       = 0u;
    float                                        m_lastCloudMs    = 0.0f;
    bool                                         m_hasValidTiming = false;
};

} // namespace raw::bridge
