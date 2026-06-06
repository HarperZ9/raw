#pragma once
//=============================================================================
//  TemporalSuperRes — Temporal super-resolution upscaler (FSR-style)
//
//  Two-pass system:
//    1. Upscale CS (8x8 threads, output-res): Temporal accumulation upscaler.
//       Reads current frame at render resolution, previous output at display
//       resolution (history), motion vectors, depth buffer, and material
//       classification buffer (t25).  Algorithm:
//         - Reproject history via motion vectors
//         - Lanczos-2 sampling of current frame
//         - 3x3 neighbourhood clamping on reprojected history
//         - Per-material blend weights (architecture=sharp, foliage=less ghost)
//         - Velocity-based rejection (fast pixels reject more history)
//         - Output: display-res R16G16B16A16_FLOAT
//
//    2. Sharpen PS (fullscreen via RenderPassManager): Contrast-Adaptive
//       Sharpening (AMD CAS-style).  Material-aware: architecture sharpened
//       more, foliage less.  Writes to backbuffer.
//
//  Quality presets:
//    Performance (50%), Balanced (67%), Quality (75%), Native (100% = TAA-only)
//
//  Registers as a PrePresent pipeline pass (priority 50, before tone mapping).
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include "ComputeManager.h"
#include "RenderPassManager.h"
#include "RenderPipeline.h"

namespace SB
{

// ── Quality presets ──────────────────────────────────────────────────────

enum class TSRQuality : int
{
    Performance = 0,   // 50% render scale
    Balanced    = 1,   // 67% render scale (default)
    Quality     = 2,   // 75% render scale
    Native      = 3,   // 100% — TAA-only (upscale pass becomes passthrough)
    Count
};

const char* TSRQualityName(TSRQuality q);
float       TSRQualityScale(TSRQuality q);


// ── Temporal Super Resolution manager ────────────────────────────────────

class TemporalSuperRes
{
public:
    static TemporalSuperRes& Get()
    {
        static TemporalSuperRes inst;
        return inst;
    }

    /// Initialize all GPU resources.  Call after ComputeManager, RenderPassManager,
    /// and RenderPipeline are initialized.
    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc);
    void Shutdown();

    /// Per-frame execution (called by the pipeline pass callback).
    /// Runs the upscale CS + sharpen PS.
    void Execute(PassContext& ctx);

    // ── Settings ─────────────────────────────────────────────────────────

    bool IsInitialized() const { return m_initialized; }
    bool IsEnabled()     const { return m_enabled; }
    void SetEnabled(bool v)    { m_enabled = v; }

    TSRQuality GetQuality() const              { return m_quality; }
    void       SetQuality(TSRQuality q)        { m_quality = q; m_renderScale = TSRQualityScale(q); }

    float GetRenderScale()  const { return m_renderScale; }
    float GetSharpness()    const { return m_sharpness; }
    void  SetSharpness(float s)   { m_sharpness = s; }

    // Dynamic resolution
    bool  IsDynamicResEnabled() const      { return m_dynamicRes; }
    void  SetDynamicResEnabled(bool v)     { m_dynamicRes = v; }
    float GetTargetFrameTimeMs() const     { return m_targetFrameTimeMs; }
    void  SetTargetFrameTimeMs(float ms)   { m_targetFrameTimeMs = ms; }

    // Dimensions
    uint32_t GetRenderWidth()  const { return m_renderW; }
    uint32_t GetRenderHeight() const { return m_renderH; }
    uint32_t GetDisplayWidth()  const { return m_displayW; }
    uint32_t GetDisplayHeight() const { return m_displayH; }

    // Jitter (set externally from TAAManager each frame)
    void SetJitter(float x, float y) { m_jitterX = x; m_jitterY = y; }
    void SetMotionScale(float x, float y) { m_motionScaleX = x; m_motionScaleY = y; }

    uint32_t GetFrameIndex() const { return m_frameIndex; }

private:
    TemporalSuperRes() = default;

    bool CompileUpscaleCS(ID3D11Device* dev);
    bool CompileSharpenPS();
    bool CreateResources(ID3D11Device* dev);
    bool CreateSamplers(ID3D11Device* dev);
    bool RecreateRenderTarget(ID3D11Device* dev, uint32_t w, uint32_t h);
    void UpdateDynamicResolution(float deltaTime);
    void ReleaseResources();

    bool AcquireDepthSRV(ID3D11DeviceContext* ctx);
    bool AcquireMotionVectorsSRV(ID3D11DeviceContext* ctx);

    // ── State ────────────────────────────────────────────────────────────

    bool m_initialized = false;
    bool m_enabled     = false;  // Opt-in: overwrites backbuffer
    bool m_firstFrame  = true;

    ID3D11Device*        m_device  = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    // ── Quality / resolution ─────────────────────────────────────────────

    TSRQuality m_quality     = TSRQuality::Balanced;
    float      m_renderScale = 0.67f;
    float      m_sharpness   = 0.5f;   // CAS sharpness [0..1]

    // Dynamic resolution
    bool  m_dynamicRes        = false;
    float m_targetFrameTimeMs = 16.6f;  // 60 fps
    float m_frameTimeAccum    = 0.0f;
    int   m_frameTimeSamples  = 0;

    // Display resolution (backbuffer)
    uint32_t m_displayW = 0;
    uint32_t m_displayH = 0;

    // Render resolution (render scale * display)
    uint32_t m_renderW = 0;
    uint32_t m_renderH = 0;

    // ── Jitter ───────────────────────────────────────────────────────────

    float m_jitterX = 0.0f;
    float m_jitterY = 0.0f;
    float m_motionScaleX = 1.0f;
    float m_motionScaleY = 1.0f;
    uint32_t m_frameIndex = 0;

    // Halton(2,3) 16-sample jitter sequence (precomputed)
    static constexpr uint32_t kJitterSamples = 16;
    static const float kHaltonX[kJitterSamples];
    static const float kHaltonY[kJitterSamples];

    // ── GPU resources: Upscale CS ────────────────────────────────────────

    ID3D11ComputeShader* m_upscaleCS = nullptr;

    // Ping-pong history textures (display-res, R16G16B16A16_FLOAT)
    ID3D11Texture2D*            m_historyTex[2]  = {};
    ID3D11ShaderResourceView*   m_historySRV[2]  = {};
    ID3D11UnorderedAccessView*  m_historyUAV[2]  = {};
    int m_writeIdx = 0;  // ping-pong index

    // Render-resolution input copy (current frame downscaled)
    ID3D11Texture2D*            m_renderInputTex = nullptr;
    ID3D11ShaderResourceView*   m_renderInputSRV = nullptr;

    // Upscale output (display-res, used as sharpen input)
    ID3D11Texture2D*            m_upscaleOutputTex = nullptr;
    ID3D11ShaderResourceView*   m_upscaleOutputSRV = nullptr;
    ID3D11UnorderedAccessView*  m_upscaleOutputUAV = nullptr;

    // Constant buffer (upscale CS parameters)
    ID3D11Buffer* m_upscaleCB = nullptr;

    // Temporary depth / motion vector SRVs (reacquired each frame)
    ID3D11ShaderResourceView* m_depthSRV   = nullptr;
    ID3D11ShaderResourceView* m_motionSRV  = nullptr;

    // Samplers
    ID3D11SamplerState* m_linearClampSampler = nullptr;
    ID3D11SamplerState* m_pointClampSampler  = nullptr;

    // ── GPU resources: Sharpen PS ────────────────────────────────────────

    RenderPassID m_sharpenPass = 0;
    ID3D11Buffer* m_sharpenCB = nullptr;

    // ── Pipeline registration ────────────────────────────────────────────

    PassHandle m_pipelineHandle = 0;

    // ── Material weight presets (per material class) ─────────────────────
    // Index: 0=default, 1=architecture/stone/metal, 2=foliage, 3=skin
    float m_materialHistoryWeight[4] = { 0.90f, 0.95f, 0.80f, 0.88f };
    float m_materialSharpenWeight[4] = { 0.50f, 0.70f, 0.30f, 0.40f };
};

} // namespace SB
