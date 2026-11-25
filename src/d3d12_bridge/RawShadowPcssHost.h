#pragma once
// RawShadowPcssHost.h: Native DX12 GPU Compute Variable-Penumbra PCSS Contact-Hardening Shadow Solver
// & World-Space Texel-Snapped Directional Cascade Stabilizer (SM 6.6 / 6.10).
// Original clean-room implementation, MIT License.

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>

namespace raw::bridge
{

struct ShadowPcssDesc {
    D3D12_SHADER_BYTECODE customShader = {}; // optional override; uses embedded raw_shadow_pcss_cs.h when empty
};

struct ShadowPcssParams {
    // Row-major 4x4 matrices used with row-vector mul(float4(v, 1), M).
    float    invViewProj[16]     = {};
    float    lightViewProj[16]   = {};
    float    sunDirW[3]          = {0.35f, 0.25f, 0.90f}; // Unit direction toward sun
    float    baseDepthBias       = 4.0e-4f;
    float    slopeDepthBias      = 8.0e-4f;
    float    normalOffsetWorld   = 0.02f;
    float    lightAngularSize    = 18.0f;   // Controls penumbra growth vs. blocker-to-receiver distance
    float    frameJitter         = 0.0f;    // Optional temporal IGN phase
    float    blockerSearchTexels = 14.0f;   // Stage-1 blocker search disk radius in shadow texels
    float    minFilterTexels     = 0.75f;   // Sharp contact-shadow minimum filter radius
    float    maxFilterTexels     = 24.0f;   // Soft distant-penumbra maximum filter radius
    uint32_t shadowMapSize       = 2048u;
    uint32_t flags               = 0u;
};

struct TexelSnappedCascade {
    float lightViewProj[16]  = {}; // Row-major row-vector ortho matrix (world -> DX12 NDC z in [0,1])
    float lightRight[3]      = {};
    float lightUp[3]         = {};
    float lightForward[3]    = {}; // -sunDir (direction light travels)
    float snappedCenterLS[3] = {}; // Snapped cascade center in light space
    float texelWorldSize     = 0.0f;
};

// Pure CPU helper: builds a world-space texel-snapped directional orthographic shadow cascade matrix.
// Snapping (centerX, centerY) in light space to exact integer multiples of (2 * cascadeRadius / shadowMapSize)
// guarantees ZERO shadow-edge shimmer under arbitrary sub-texel camera translation or rotation.
TexelSnappedCascade BuildTexelSnappedCascade(const float cameraTargetW[3],
                                             const float sunDirW[3],
                                             float cascadeRadius,
                                             float depthSpan,
                                             uint32_t shadowMapSize);

class RawShadowPcssHost {
public:
    static constexpr uint32_t kRingCount        = 4u;
    static constexpr uint32_t kDescsPerRingSlot = 3u; // t0 SceneDepth, t1 ShadowMap, u0 ShadowOut

    RawShadowPcssHost() = default;
    ~RawShadowPcssHost();

    RawShadowPcssHost(const RawShadowPcssHost&) = delete;
    RawShadowPcssHost& operator=(const RawShadowPcssHost&) = delete;

    bool Init(ID3D12Device* dev, const ShadowPcssDesc& desc = {}, std::string* outErr = nullptr);
    bool Ready() const { return m_ready; }

    // Executes the 3-stage Fernando (2005) PCSS compute dispatch writing float2(visibility, normPenumbra)
    // to shadowOut. Restores any transitioned resource states back to caller-provided states cleanly.
    bool Execute(ID3D12GraphicsCommandList* cmd,
                 ID3D12Resource* sceneDepth,
                 ID3D12Resource* shadowMap,
                 ID3D12Resource* shadowOut,
                 uint32_t width,
                 uint32_t height,
                 const ShadowPcssParams& params,
                 D3D12_RESOURCE_STATES depthState     = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                 D3D12_RESOURCE_STATES shadowMapState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                 D3D12_RESOURCE_STATES shadowOutState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    void  SetTimestampFrequency(uint64_t hz) { m_tsFreqHz = hz; }
    float GetLastPcssMs() const { return m_lastPcssMs; }
    bool  HasValidTiming() const { return m_hasValidTiming; }

    void Shutdown();

private:
    bool HarvestTimestampPair(uint32_t queryStartIdx, float& outMs) const;
    D3D12_CPU_DESCRIPTOR_HANDLE CpuAt(uint32_t slot) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GpuAt(uint32_t slot) const;

    bool                                         m_ready = false;
    ID3D12Device*                                m_dev = nullptr;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_heap;
    uint32_t                                     m_descInc = 0u;
    Microsoft::WRL::ComPtr<ID3D12RootSignature>  m_root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>  m_pso;

    Microsoft::WRL::ComPtr<ID3D12Resource>       m_cbBuf;
    uint8_t*                                     m_cbMapped = nullptr;

    Microsoft::WRL::ComPtr<ID3D12QueryHeap>      m_tsHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource>       m_tsReadback;
    uint64_t                                     m_tsFreqHz = 0u;
    uint32_t                                     m_frameIdx = 0u;
    float                                        m_lastPcssMs = 0.0f;
    bool                                         m_hasValidTiming = false;
};

} // namespace raw::bridge
