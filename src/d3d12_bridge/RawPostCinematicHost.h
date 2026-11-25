#pragma once
// RawPostCinematicHost.h: Native DX12 GPU Compute Host for RAW's Cinematic Post-Process Solver
// (16x16 Tile-Max Velocity Reduction + 3x3 Neighbor-Max Dilation + 12-Tap Directional Motion Blur
//  + Depth-Aware Physical Circle-of-Confusion Hexagonal Bokeh DoF with Foreground Bleed Guard
//  + Filmic Optical Vignette, SM 6.6 / 6.10).
//
// Original clean-room implementation, MIT License.

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>

namespace raw::bridge
{

struct PostCinematicDesc {
    uint32_t              initialWidth              = 960u;
    uint32_t              initialHeight             = 540u;
    D3D12_SHADER_BYTECODE customTileMaxShader       = {}; // optional override; uses raw_post_tile_max_cs.h
    D3D12_SHADER_BYTECODE customNeighborMaxShader   = {}; // optional override; uses raw_post_neighbor_max_cs.h
    D3D12_SHADER_BYTECODE customCompositeShader     = {}; // optional override; uses raw_post_cinematic_cs.h
};

struct PostCinematicParams {
    float nearZ             = 0.5f;   // Camera near plane (Standard-Z 0=near, 1=far)
    float farZ              = 500.0f; // Camera far plane
    float focusDistanceM    = 6.0f;   // Thin-lens focus plane distance (meters / world units)
    float focalLengthMm     = 50.0f;  // Cinema lens focal length in millimeters
    float apertureFStop     = 2.0f;   // Lens aperture f-number (lower = shallower DoF)
    float cocScale          = 1.5f;   // Artistic CoC multiplier (1.0 = exact 35mm full-frame)
    float dofStrength       = 1.0f;   // 0 = disable DoF, 1 = full physical bokeh DoF
    float maxBokehRadiusPx  = 10.0f;  // Maximum CoC radius clamp in pixels
    float motionBlurScale   = 1.0f;   // Velocity shutter scale (0 = disable motion blur)
    float maxMotionRadiusPx = 24.0f;  // Maximum motion blur streak radius clamp in pixels
    float vignetteStrength  = 0.20f;  // Subtle filmic optical vignette (0 = off)
    float frameJitter       = 0.0f;   // Temporal IGN jitter phase (0 = deterministic static test)
};

class RawPostCinematicHost {
public:
    static constexpr uint32_t kCbRingCount = 4u;

    RawPostCinematicHost() = default;
    ~RawPostCinematicHost();

    RawPostCinematicHost(const RawPostCinematicHost&) = delete;
    RawPostCinematicHost& operator=(const RawPostCinematicHost&) = delete;

    bool Init(ID3D12Device* dev, const PostCinematicDesc& desc = {}, std::string* outErr = nullptr);
    bool Ready() const { return m_ready; }

    // Runs TileMax -> NeighborMax -> Hexagonal Bokeh DoF + 12-Tap Motion Blur + Vignette.
    // velocityTex may be nullptr (disables motion blur while still running DoF + vignette cleanly).
    // Auto-reallocates internal targets if width/height change; cleanly restores all input resource states.
    // Leaves OutputResource in NON_PIXEL_SHADER_RESOURCE | PIXEL_SHADER_RESOURCE state.
    bool Execute(ID3D12GraphicsCommandList* cmd,
                 ID3D12Resource* sceneColor,
                 ID3D12Resource* sceneDepth,
                 ID3D12Resource* velocityTex,
                 uint32_t width,
                 uint32_t height,
                 const PostCinematicParams& params,
                 D3D12_RESOURCE_STATES srcColorState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                 D3D12_RESOURCE_STATES srcDepthState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                 D3D12_RESOURCE_STATES srcVelState   = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    ID3D12DescriptorHeap*       DescriptorHeap() const { return m_heap.Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE GetOutputSRV() const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetOutputSRVCpu() const;
    ID3D12Resource*             GetOutputResource() const { return m_colorOut.Get(); }
    D3D12_RESOURCE_STATES       OutputResourceState() const { return m_colorOutState; }

    uint32_t Width() const { return m_width; }
    uint32_t Height() const { return m_height; }

    void  SetTimestampFrequency(uint64_t hz) { m_tsFreqHz = hz; }
    float GetLastTileMs() const { return m_lastTileMs; }
    float GetLastCompositeMs() const { return m_lastCompositeMs; }
    float GetLastTotalMs() const { return m_lastTileMs + m_lastCompositeMs; }
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
    uint32_t      m_tileW  = 0u;
    uint32_t      m_tileH  = 0u;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_heap;
    uint32_t                                     m_descInc = 0u;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_tileMaxPso;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_neighborMaxPso;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_compositePso;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_tileMaxTex;
    D3D12_RESOURCE_STATES                  m_tileMaxState = D3D12_RESOURCE_STATE_COMMON;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_neighborMaxTex;
    D3D12_RESOURCE_STATES                  m_neighborMaxState = D3D12_RESOURCE_STATE_COMMON;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_colorOut;
    D3D12_RESOURCE_STATES                  m_colorOutState = D3D12_RESOURCE_STATE_COMMON;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_cbBuf;
    uint8_t*                               m_cbMapped = nullptr;

    Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_tsHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource>  m_tsReadback;
    uint64_t                                m_tsFreqHz        = 0u;
    uint32_t                                m_frameIdx        = 0u;
    float                                   m_lastTileMs      = 0.0f;
    float                                   m_lastCompositeMs = 0.0f;
    bool                                    m_hasValidTiming  = false;
};

} // namespace raw::bridge
