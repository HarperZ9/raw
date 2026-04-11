#pragma once
//=============================================================================
//  SceneCompositor — Fullscreen composite pass for proxy-only mode
//
//  Reads all compute shader outputs (AO, GI, SSR, Clouds) and composites
//  them onto the backbuffer in a single fullscreen pixel shader pass.
//  This replaces the role ENB shaders normally play as data consumers.
//
//  Calibrated against Boris's enbeffectprepass.fx blending patterns:
//    AO  (multiplicative, ~0.6)  → GI  (clamped additive, ~0.15)
//    → SSR (energy-conserving lerp, ~0.3)  → Clouds (transmittance over)
//
//  Registered as PrePresent pipeline pass at priority 90 (before ToneMap 100).
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include "RenderPassManager.h"
#include "RenderPipeline.h"

namespace SB
{

class SceneCompositor
{
public:
    static SceneCompositor& Get()
    {
        static SceneCompositor inst;
        return inst;
    }

    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // Per-effect intensity controls (tunable via DebugGUI)
    float GetAOIntensity() const { return m_aoIntensity; }
    void  SetAOIntensity(float v) { m_aoIntensity = v; }
    float GetGIIntensity() const { return m_giIntensity; }
    void  SetGIIntensity(float v) { m_giIntensity = v; }
    float GetGIMaxAdd() const { return m_giMaxAdd; }
    void  SetGIMaxAdd(float v) { m_giMaxAdd = v; }
    float GetSSRIntensity() const { return m_ssrIntensity; }
    void  SetSSRIntensity(float v) { m_ssrIntensity = v; }
    float GetCloudIntensity() const { return m_cloudIntensity; }
    void  SetCloudIntensity(float v) { m_cloudIntensity = v; }
    float GetShadowIntensity() const { return m_shadowIntensity; }
    void  SetShadowIntensity(float v) { m_shadowIntensity = v; }
    float GetSkylightIntensity() const { return m_skylightIntensity; }
    void  SetSkylightIntensity(float v) { m_skylightIntensity = v; }

    bool IsEnabled() const { return m_enabled; }
    void SetEnabled(bool e) { m_enabled = e; }

    // HDR10 controls
    bool IsHDR10Enabled() const { return m_hdr10Enabled; }
    void SetHDR10Enabled(bool e) { m_hdr10Enabled = e; }
    float GetPaperWhiteNits() const { return m_paperWhiteNits; }
    void SetPaperWhiteNits(float n) { m_paperWhiteNits = n; }
    float GetPeakNits() const { return m_peakNits; }
    void SetPeakNits(float n) { m_peakNits = n; }

    // Debug visualization: 0=off, 1=AO, 2=GI, 3=SSR, 4=Clouds, 5=Shadow, 6=Skylight, 7=HDR heatmap
    int  GetDebugMode() const { return m_debugMode; }
    void SetDebugMode(int m) { m_debugMode = m; }

private:
    SceneCompositor() = default;

    bool m_initialized = false;
    bool m_enabled     = true;   // safe to default-on: early-returns when no effects provide SRVs

    ID3D11Device*        m_device  = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    // ── Intensity controls (conservative defaults matching Boris's calibration) ──
    //
    // Boris's enbeffectprepass.fx uses:
    //   AO: multiplicative ~0.5-0.8 with DNI fade
    //   IL: additive but very subtle, dark-area biased
    //   SSR: not available in ENB (we add energy-conserving reflections)
    //   Clouds: not available in ENB (we add volumetric over-blend)
    //
    float m_aoIntensity    = 0.60f;   // AO darkening strength
    float m_giIntensity    = 0.15f;   // GI bounce light (subtle — Boris uses ~0.1-0.3)
    float m_giMaxAdd       = 0.25f;   // Max luminance GI can add (Reinhard soft-clamp)
    float m_ssrIntensity   = 0.30f;   // Reflection blend (energy-conserving lerp)
    float m_cloudIntensity    = 1.0f;    // Cloud inscatter (physical, no attenuation)
    float m_shadowIntensity   = 0.80f;   // Contact shadow darkening
    float m_skylightIntensity = 0.50f;   // Sky visibility ambient modulation
    int   m_debugMode         = 0;       // 0=off, 1-6 = show raw buffer, 7=HDR heatmap

    // HDR10 output
    bool  m_hdr10Enabled     = false;    // false = SDR (sRGB), true = HDR10 (PQ BT.2020)
    float m_paperWhiteNits   = 200.0f;   // SDR white level in nits (typical monitor)
    float m_peakNits         = 1000.0f;  // Display peak brightness in nits

    // Backbuffer copy (read as SRV while writing to backbuffer RTV)
    ID3D11Texture2D*          m_bbCopyTex = nullptr;
    ID3D11ShaderResourceView* m_bbCopySRV = nullptr;

    // Samplers
    ID3D11SamplerState* m_pointSampler  = nullptr;  // s0: full-res scene
    ID3D11SamplerState* m_linearSampler = nullptr;  // s1: bilinear upsample

    // Fullscreen pass
    RenderPassID m_compositePass = 0;

    // Pipeline handle
    PassHandle m_pipelineHandle = 0;

    // ---- Internal ----
    void ExecutePass(PassContext& ctx);
};

} // namespace SB
