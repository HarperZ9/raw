#pragma once
//=============================================================================
//  ParticleLightingRenderer — Screen-space emissive light propagation
//
//  Detects bright/emissive pixels (via MaterialClassifier MAT_EMISSIVE=11
//  or HDR luminance threshold), downsamples them to find "virtual point
//  lights," then scatters their light contribution onto nearby geometry.
//
//  Three-pass compute pipeline:
//    1. Emissive Detect CS (full-res -> quarter-res): Identifies bright
//       pixels, downsamples to quarter-res with max-luminance filter.
//       MAT_EMISSIVE pixels pass regardless of luminance.
//    2. Light Scatter CS (quarter-res): For each bright texel, scatters
//       its light contribution as radial falloff weighted by depth proximity.
//    3. Composite CS (full-res): Additively blends scattered light onto
//       the backbuffer with depth-aware attenuation.
//
//  Output: Modifies backbuffer in-place via UAV (no SRV slot).
//  Registered as PreENB pipeline pass, priority 23.
//  Dependencies: Depth (D3D11Hook::GetGameDepthSRV()),
//                MaterialClassifier (GetMaterialSRV())
//
//  VRAM budget: ~12 MB at 1920x1080 (2 quarter-res RGBA16F + 1 full-res copy)
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include "RenderPipeline.h"

namespace SB
{

class ParticleLightingRenderer
{
public:
    static ParticleLightingRenderer& Get()
    {
        static ParticleLightingRenderer inst;
        return inst;
    }

    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    bool  IsEnabled() const { return m_enabled; }
    void  SetEnabled(bool v) { m_enabled = v; }

    // Settings
    float GetIntensity() const { return m_intensity; }
    void  SetIntensity(float v) { m_intensity = v; }
    float GetLuminanceThreshold() const { return m_luminanceThreshold; }
    void  SetLuminanceThreshold(float v) { m_luminanceThreshold = v; }
    float GetScatterRadius() const { return m_scatterRadius; }
    void  SetScatterRadius(float v) { m_scatterRadius = v; }
    float GetFalloffExponent() const { return m_falloffExponent; }
    void  SetFalloffExponent(float v) { m_falloffExponent = v; }
    float GetDepthTolerance() const { return m_depthTolerance; }
    void  SetDepthTolerance(float v) { m_depthTolerance = v; }

private:
    ParticleLightingRenderer() = default;

    bool CompileShaders();
    bool CreateResources();
    void ReleaseResources();

    void ExecutePass(PassContext& ctx);

    bool m_initialized = false;
    bool m_enabled     = false;  // opt-in: writes backbuffer via UAV

    ID3D11Device*        m_device  = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    uint32_t m_screenW = 0;
    uint32_t m_screenH = 0;
    uint32_t m_quarterW = 0;
    uint32_t m_quarterH = 0;

    // Settings
    float m_intensity          = 1.0f;    // Overall scatter intensity (0-5)
    float m_luminanceThreshold = 2.0f;    // HDR luminance above which a pixel is a light source (0-10)
    float m_scatterRadius      = 64.0f;   // Scatter radius in pixels (8-256)
    float m_falloffExponent    = 2.0f;    // Distance falloff power (1-4)
    float m_depthTolerance     = 0.1f;    // Depth similarity threshold for bleed prevention (0-1)

    // ── Pass 1: Emissive detection (full-res -> quarter-res) ─────────
    ID3D11ComputeShader* m_emissiveDetectCS = nullptr;

    // Quarter-res emissive detection texture (SRV + UAV)
    ID3D11Texture2D*            m_emissiveTex    = nullptr;
    ID3D11ShaderResourceView*   m_emissiveSRV    = nullptr;
    ID3D11UnorderedAccessView*  m_emissiveUAV    = nullptr;

    // ── Pass 2: Light scatter (quarter-res) ──────────────────────────
    ID3D11ComputeShader* m_lightScatterCS = nullptr;

    // Quarter-res scatter result texture (SRV + UAV)
    ID3D11Texture2D*            m_scatterTex     = nullptr;
    ID3D11ShaderResourceView*   m_scatterSRV     = nullptr;
    ID3D11UnorderedAccessView*  m_scatterUAV     = nullptr;

    // ── Pass 3: Composite (full-res) ─────────────────────────────────
    ID3D11ComputeShader* m_compositeCS = nullptr;

    // Backbuffer copy texture (full-res, SRV) — for reading emissive pixels
    ID3D11Texture2D*            m_bbCopyTex      = nullptr;
    ID3D11ShaderResourceView*   m_bbCopySRV      = nullptr;

    // Shared resources
    ID3D11Buffer*               m_constantsCB    = nullptr;
    ID3D11UnorderedAccessView*  m_backbufferUAV  = nullptr;
    ID3D11SamplerState*         m_linearSampler  = nullptr;

    // Pipeline handle
    PassHandle m_pipelineHandle = 0;
    uint32_t   m_frameIndex     = 0;
};

} // namespace SB
