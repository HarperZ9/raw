#pragma once
// RawGtaoHost.h: Native DX12 GPU Compute Host for Jimenez (SIGGRAPH 2016) Ground-Truth Ambient
// Occlusion (GTAO) + Unoccluded Bent-Normal & 3x3 Depth-Aware Bilateral Denoise (SM 6.6 / 6.10).
//
// Original clean-room implementation, MIT License.

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>

namespace raw::bridge
{

struct GtaoDesc {
    uint32_t              initialWidth        = 960u;
    uint32_t              initialHeight       = 540u;
    D3D12_SHADER_BYTECODE customMainShader    = {}; // optional override; uses raw_gtao_main_cs.h when empty
    D3D12_SHADER_BYTECODE customDenoiseShader = {}; // optional override; uses raw_gtao_denoise_cs.h when empty
};

struct GtaoParams {
    float    fovYDeg           = 60.0f;
    float    nearZ             = 0.5f;
    float    farZ              = 500.0f;
    float    effectRadius      = 1.5f;   // View-space occlusion search radius (meters / world units)
    float    falloffStart      = 0.65f;  // Fraction of effectRadius where smooth distance falloff starts
    float    aoPower           = 1.0f;   // Artistic contrast exponent (1.0 = physical ground truth)
    float    maxScreenRadiusPx = 64.0f;  // Screen-space pixel search radius clamp
    float    depthEdgeScale    = 0.025f; // Relative depth bilateral edge threshold
    uint32_t numSlices         = 2u;     // Jimenez 2016: 2 slices
    uint32_t numSteps          = 6u;     // Jimenez 2016: 6 steps per side
    float    temporalJitter    = 0.0f;   // Frame jitter phase (0.0 for deterministic static tests)
};

class RawGtaoHost {
public:
    static constexpr uint32_t kCbRingCount = 4u;

    RawGtaoHost() = default;
    ~RawGtaoHost();

    RawGtaoHost(const RawGtaoHost&) = delete;
    RawGtaoHost& operator=(const RawGtaoHost&) = delete;

    bool Init(ID3D12Device* dev, const GtaoDesc& desc = {}, std::string* outErr = nullptr);
    bool Ready() const { return m_ready; }

    // Runs cs_gtao_main + cs_gtao_denoise on sceneDepth (Standard-Z 0=near, 1=far).
    // Auto-reallocates internal RGBA16F outputs if width/height change; cleanly restores srcDepthState.
    // Leaves FilteredOutput in NON_PIXEL_SHADER_RESOURCE | PIXEL_SHADER_RESOURCE state.
    bool Execute(ID3D12GraphicsCommandList* cmd,
                 ID3D12Resource* sceneDepth,
                 uint32_t width,
                 uint32_t height,
                 const GtaoParams& params,
                 D3D12_RESOURCE_STATES srcDepthState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    ID3D12DescriptorHeap*       DescriptorHeap() const { return m_heap.Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE GetFilteredOutputSRV() const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetFilteredOutputSRVCpu() const;
    ID3D12Resource*             GetFilteredOutputResource() const { return m_filteredOut.Get(); }
    D3D12_RESOURCE_STATES       FilteredOutputResourceState() const { return m_filteredState; }
    ID3D12Resource*             GetRawOutputResource() const { return m_rawOut.Get(); }
    D3D12_RESOURCE_STATES       RawOutputResourceState() const { return m_rawState; }

    uint32_t Width() const { return m_width; }
    uint32_t Height() const { return m_height; }

    void  SetTimestampFrequency(uint64_t hz) { m_tsFreqHz = hz; }
    float GetLastMainMs() const { return m_lastMainMs; }
    float GetLastDenoiseMs() const { return m_lastDenoiseMs; }
    bool  HasValidTiming() const { return m_hasValidTiming; }

    void Shutdown();

private:
    bool EnsureOutputs(uint32_t width, uint32_t height);
    void Transition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
                    D3D12_RESOURCE_STATES& curState, D3D12_RESOURCE_STATES target);
    bool HarvestTimestampRange(uint32_t idxA, uint32_t idxB, float& outMs) const;
    D3D12_CPU_DESCRIPTOR_HANDLE CpuAt(uint32_t slot) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GpuAt(uint32_t slot) const;

    bool          m_ready  = false;
    ID3D12Device* m_dev    = nullptr;
    uint32_t      m_width  = 0u;
    uint32_t      m_height = 0u;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_heap;
    uint32_t                                     m_descInc = 0u;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_mainPso;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_denoisePso;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_rawOut;
    D3D12_RESOURCE_STATES                  m_rawState = D3D12_RESOURCE_STATE_COMMON;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_filteredOut;
    D3D12_RESOURCE_STATES                  m_filteredState = D3D12_RESOURCE_STATE_COMMON;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_cbBuf;
    uint8_t*                               m_cbMapped = nullptr;

    Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_tsHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource>  m_tsReadback;
    uint64_t                                m_tsFreqHz       = 0u;
    uint32_t                                m_frameIdx       = 0u;
    float                                   m_lastMainMs     = 0.0f;
    float                                   m_lastDenoiseMs  = 0.0f;
    bool                                    m_hasValidTiming = false;
};

} // namespace raw::bridge
