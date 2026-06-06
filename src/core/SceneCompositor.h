#pragma once
//=============================================================================
//  SceneCompositor — Fullscreen composite pass for proxy-only mode
//
//  Reads all compute shader outputs (AO, GI, SSR, Clouds) and composites
//  them onto the backbuffer in a single fullscreen pixel shader pass.
//  Composites all compute shader outputs into the final scene.
//
//  Calibrated for Skyrim SE rendering pipeline:
//    AO  (multiplicative, ~0.6)  → GI  (clamped additive, ~0.15)
//    → SSR (energy-conserving lerp, ~0.3)  → Clouds (transmittance over)
//
//  Registered as PrePresent pipeline pass at priority 90 (before ToneMap 100).
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include "RenderPassManager.h"
#include "RenderPipeline.h"
#include "RendererBase.h"

namespace SB
{

class SceneCompositor : public RendererBase
{
public:
    static SceneCompositor& Get()
    {
        static SceneCompositor inst;
        return inst;
    }

    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc);
    void Shutdown();
    // IsInitialized inherited from RendererBase

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

    // IsEnabled / SetEnabled inherited from RendererBase

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
    SceneCompositor() { m_enabled = true; }  // safe to default-on: early-returns when no effects provide SRVs

    // Common state inherited from RendererBase
    bool m_executedThisFrame = false; // Set true when PostGeometry runs, prevents double-composite at PrePresent

    // ── Intensity controls (calibrated for Skyrim SE) ──
    //
    float m_aoIntensity    = 0.60f;   // AO darkening strength
    float m_giIntensity    = 0.50f;   // GI bounce light (raised from 0.15 — was too subtle)
    float m_giMaxAdd       = 0.50f;   // Max luminance GI can add (raised from 0.25)
    float m_ssrIntensity   = 0.30f;   // Reflection blend (energy-conserving lerp)
    float m_cloudIntensity    = 1.0f;    // Cloud inscatter (physical, no attenuation)
    float m_shadowIntensity   = 0.80f;   // Contact shadow darkening
    float m_skylightIntensity = 0.80f;   // Sky visibility (raised from 0.50 — was too subtle)
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

    // Pipeline handles
    PassHandle m_pipelineHandle = 0;       // PostGeometry: normal compositing
    PassHandle m_debugOverlayHandle = 0;   // PrePresent: debug visualization (runs after all game rendering)

    // ---- Internal ----
    void ExecutePass(PassContext& ctx);
    void ExecuteDebugOverlay(PassContext& ctx);
};

} // namespace SB
