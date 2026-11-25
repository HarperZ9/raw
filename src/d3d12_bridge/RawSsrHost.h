#pragma once
// RawSsrHost.h: Native DX12 GPU Compute GGX Stochastic Screen-Space Reflections (SSR) Solver (SM 6.6 / 6.10).
// Supports dynamic resolution auto-reallocation, Heitz (2018) GGX VNDF stochastic lobe sampling,
// 40-step adaptive raymarching + 5-step binary refinement, and hardware ID3D12QueryHeap GPU profiling.
// Original clean-room implementation, MIT License.

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>

namespace raw::bridge
{

struct SsrDesc {
    uint32_t              initialWidth  = 960u;
    uint32_t              initialHeight = 540u;
    D3D12_SHADER_BYTECODE customShader  = {}; // optional override; uses embedded raw_ssr_trace_cs.h when empty
};

struct SsrParams {
    // Row-major 4x4 DX12 LH projection & inverse projection (row-vector mul(float4(v, 1), M)).
    float    proj[16]          = {};
    float    invProj[16]       = {};
    float    maxRayDist        = 45.0f;  // Max view-space raymarch distance
    float    thicknessM        = 1.25f;  // Depth-thickness rejection threshold
    float    nearZ             = 0.25f;  // Camera near plane (> 0, LH +Z forward)
    float    startBias         = 0.03f;  // View-space normal self-intersection bias
    float    fallbackRoughness = 0.08f;  // Used when roughnessMap is null
    float    maxRoughness      = 0.72f;  // Roughness cutoff above which SSR early-outs
    float    edgeFadeWidth     = 0.07f;  // Smooth screen-border vignette fade width [0..0.5]
    float    frameJitter       = 0.0f;   // Temporal IGN seed
    uint32_t maxSteps          = 40u;    // 8..40 adaptive linear steps (+ 5 binary steps)
};

// Pure CPU helper: builds exact row-major DX12 LH perspective projection & its analytical inverse.
void BuildSsrPerspectiveLH(float fovYRadians, float aspect, float nearZ, float farZ,
                           float outProj[16], float outInvProj[16]);

class RawSsrHost {
public:
    static constexpr uint32_t kRingCount        = 4u;
    static constexpr uint32_t kDescsPerRingSlot = 4u; // t0 SceneColor, t1 SceneDepth, t2 Roughness, u0 SsrOut
    static constexpr uint32_t kSlotExtOutSrv    = kRingCount * kDescsPerRingSlot; // Persistent SRV for consumers
    static constexpr uint32_t kTotalDescCount   = kSlotExtOutSrv + 1u;

    RawSsrHost() = default;
    ~RawSsrHost();

    RawSsrHost(const RawSsrHost&) = delete;
    RawSsrHost& operator=(const RawSsrHost&) = delete;

    bool Init(ID3D12Device* dev, const SsrDesc& desc = {}, std::string* outErr = nullptr);
    bool Ready() const { return m_ready; }

    // Traces GGX stochastic SSR into the host-owned RGBA16F output texture (auto-reallocated on resize)
    // and leaves OutputResource in NON_PIXEL_SHADER_RESOURCE | PIXEL_SHADER_RESOURCE.
    bool Execute(ID3D12GraphicsCommandList* cmd,
                 ID3D12Resource* sceneColor,
                 ID3D12Resource* sceneDepth,
                 uint32_t width,
                 uint32_t height,
                 const SsrParams& params,
                 D3D12_RESOURCE_STATES srcColorState  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                 D3D12_RESOURCE_STATES srcDepthState  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                 ID3D12Resource* roughnessMap         = nullptr,
                 D3D12_RESOURCE_STATES roughnessState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    ID3D12DescriptorHeap*       DescriptorHeap() const { return m_heap.Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE GetOutputSRV() const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetOutputSRVCpu() const;
    ID3D12Resource*             GetOutputResource() const { return m_outputTex.Get(); }
    D3D12_RESOURCE_STATES       OutputResourceState() const { return m_outputState; }
    uint32_t                    OutputWidth() const { return m_outW; }
    uint32_t                    OutputHeight() const { return m_outH; }

    void  SetTimestampFrequency(uint64_t hz) { m_tsFreqHz = hz; }
    float GetLastSsrMs() const { return m_lastSsrMs; }
    bool  HasValidTiming() const { return m_hasValidTiming; }

    void Shutdown();

private:
    bool EnsureOutputTarget(uint32_t width, uint32_t height);
    bool HarvestTimestampPair(uint32_t queryStartIdx, float& outMs) const;
    D3D12_CPU_DESCRIPTOR_HANDLE CpuAt(uint32_t slot) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GpuAt(uint32_t slot) const;

    bool                                         m_ready = false;
    ID3D12Device*                                m_dev = nullptr;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_heap;
    uint32_t                                     m_descInc = 0u;
    Microsoft::WRL::ComPtr<ID3D12RootSignature>  m_root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>  m_pso;

    Microsoft::WRL::ComPtr<ID3D12Resource>       m_outputTex;
    D3D12_RESOURCE_STATES                        m_outputState = D3D12_RESOURCE_STATE_COMMON;
    uint32_t                                     m_outW = 0u;
    uint32_t                                     m_outH = 0u;

    Microsoft::WRL::ComPtr<ID3D12Resource>       m_cbBuf;
    uint8_t*                                     m_cbMapped = nullptr;

    Microsoft::WRL::ComPtr<ID3D12QueryHeap>      m_tsHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource>       m_tsReadback;
    uint64_t                                     m_tsFreqHz = 0u;
    uint32_t                                     m_frameIdx = 0u;
    float                                        m_lastSsrMs = 0.0f;
    bool                                         m_hasValidTiming = false;
};

} // namespace raw::bridge
