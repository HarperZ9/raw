#pragma once
//=============================================================================
//  FrameGenerator — DLSS 3-style compute-based frame generation
//
//  Two compute passes + swap chain double-present:
//
//    Pass 1  OpticalFlow CS (8x8, quarter-res)
//            Block-matching motion estimation between frame N-1 and N.
//            8x8 search window, SAD cost, hierarchical 2-level refinement.
//            Output: R16G16_FLOAT motion vectors (in pixels).
//
//    Pass 2  FrameSynth CS (8x8, full-res)
//            Warps frame N with optical flow to synthesize frame N+0.5.
//            Disocclusion: flow divergence detection + nearest-valid fill.
//            Output: R16G16B16A16_FLOAT synthesis buffer.
//
//    Present Double-present integration (PrePresent, priority 900):
//            Stores current backbuffer as history.
//            On next present, inserts synthesized frame before real frame.
//
//  Quality modes:
//    Off     Frame generation disabled
//    Low     Half-res optical flow (no refinement)
//    High    Quarter-res optical flow with 2-level hierarchical refinement
//
//  Scene cut detection via FeedbackProcessor Temporal.x flag.
//  Internal-only — does NOT register an SRV slot.
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include "ComputeManager.h"
#include "RenderPipeline.h"

namespace SB
{

enum class FrameGenQuality : uint8_t
{
    Off  = 0,
    Low  = 1,   // Half-res flow, no refinement
    High = 2    // Quarter-res flow, 2-level hierarchical refinement
};

const char* FrameGenQualityName(FrameGenQuality q);

class FrameGenerator
{
public:
    static FrameGenerator& Get()
    {
        static FrameGenerator instance;
        return instance;
    }

    /// Initialize GPU resources and register pipeline pass.
    /// Call after ComputeManager + RenderPipeline are initialized.
    bool Initialize(ID3D11Device* dev, IDXGISwapChain* swapChain);

    /// Per-frame execution (called from pipeline pass or manually).
    void Execute(PassContext& ctx);

    /// Release all GPU resources and unregister pipeline pass.
    void Shutdown();

    // ── Accessors ────────────────────────────────────────────────────

    bool IsInitialized() const { return m_initialized; }
    bool IsEnabled()     const { return m_enabled && m_quality != FrameGenQuality::Off; }
    void SetEnabled(bool v)    { m_enabled = v; }

    FrameGenQuality GetQuality() const    { return m_quality; }
    void SetQuality(FrameGenQuality q)    { m_quality = q; }

    uint32_t GetWidth()      const { return m_width; }
    uint32_t GetHeight()     const { return m_height; }
    uint32_t GetFlowWidth()  const { return m_flowW; }
    uint32_t GetFlowHeight() const { return m_flowH; }
    uint32_t GetFrameIndex() const { return m_frameIndex; }

    /// True if a synthesized frame is pending insertion on the next Present.
    bool HasPendingSynth() const { return m_synthReady; }

private:
    FrameGenerator() = default;

    bool CompileShaders(ID3D11Device* dev);
    bool CreateResources(ID3D11Device* dev, uint32_t w, uint32_t h);
    void RegisterPipelinePass();
    void ReleaseResources();

    // ── Per-frame subroutines ────────────────────────────────────────
    void DispatchOpticalFlow(ID3D11DeviceContext* ctx);
    void DispatchFrameSynth(ID3D11DeviceContext* ctx);
    void PresentSynthesizedFrame(ID3D11DeviceContext* ctx, IDXGISwapChain* sc);

    bool m_initialized = false;
    bool m_enabled     = false;   // Opt-in: disabled by default
    FrameGenQuality m_quality = FrameGenQuality::High;

    uint32_t m_width  = 0;
    uint32_t m_height = 0;
    uint32_t m_flowW  = 0;        // Quarter-res (or half-res in Low mode)
    uint32_t m_flowH  = 0;
    uint32_t m_frameIndex = 0;

    // Compute shaders
    ID3D11ComputeShader* m_opticalFlowCS = nullptr;
    ID3D11ComputeShader* m_frameSynthCS  = nullptr;

    // History buffers: 2x full-res R16G16B16A16_FLOAT (ping-pong)
    ID3D11Texture2D*            m_historyTex[2] = {};
    ID3D11ShaderResourceView*   m_historySRV[2] = {};
    int m_writeIdx = 0;   // Current write target; read = 1 - writeIdx

    // Flow buffer: quarter-res (or half-res) R16G16_FLOAT
    ID3D11Texture2D*            m_flowTex = nullptr;
    ID3D11ShaderResourceView*   m_flowSRV = nullptr;
    ID3D11UnorderedAccessView*  m_flowUAV = nullptr;

    // Synthesis buffer: full-res R16G16B16A16_FLOAT
    ID3D11Texture2D*            m_synthTex = nullptr;
    ID3D11ShaderResourceView*   m_synthSRV = nullptr;
    ID3D11UnorderedAccessView*  m_synthUAV = nullptr;

    // Constant buffer
    ID3D11Buffer* m_paramsCB = nullptr;

    // Point sampler for flow lookups
    ID3D11SamplerState* m_pointSampler  = nullptr;
    ID3D11SamplerState* m_linearSampler = nullptr;

    // Pipeline pass handle
    PassHandle m_passHandle = 0;

    // Double-present state
    bool m_synthReady    = false;   // A synthesized frame is ready for insertion
    bool m_firstFrame    = true;    // No history yet
    bool m_sceneCutPrev  = false;   // Scene cut detected last frame
};

} // namespace SB
