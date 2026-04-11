#pragma once
//=============================================================================
//  ContactShadowRenderer — Screen-Space Contact Shadows
//
//  Replaces Community Shaders' "Screen-Space Shadows" feature.
//  Ray-marches from each pixel toward the directional light in screen space.
//  If the ray intersects geometry (depth buffer), the pixel is shadowed.
//
//  Two-pass compute pipeline:
//    1. Contact Shadow CS (8x8, full-res): Per-pixel screen-space ray march
//       toward the sun.  Reads Hi-Z depth (t19).  Output: R8_UNORM raw mask.
//    2. Spatial Denoise CS (8x8, full-res): 5x5 bilateral filter with
//       depth-gradient edge stopping + per-pixel thickness-aware fade.
//
//  Output: Shadow mask SRV at t28 (1 = lit, 0 = shadowed).
//  Registered as PreENB pipeline pass — visible to ENB shaders same-frame.
//
//  VRAM budget: ~6 MB at 1920x1080 (3 full-res R8_UNORM + 2 R16_FLOAT)
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include "ComputeManager.h"
#include "RenderPipeline.h"

namespace SB
{

class ContactShadowRenderer
{
public:
    static ContactShadowRenderer& Get()
    {
        static ContactShadowRenderer inst;
        return inst;
    }

    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // Settings
    bool  IsEnabled() const { return m_enabled; }
    void  SetEnabled(bool v) { m_enabled = v; }
    float GetRayLength() const { return m_rayLength; }
    void  SetRayLength(float v) { m_rayLength = v; }
    float GetThickness() const { return m_thickness; }
    void  SetThickness(float v) { m_thickness = v; }
    float GetIntensity() const { return m_intensity; }
    void  SetIntensity(float v) { m_intensity = v; }
    int   GetMaxSteps() const { return m_maxSteps; }
    void  SetMaxSteps(int n) { m_maxSteps = (n < 4) ? 4 : (n > 64) ? 64 : n; }

    // Shadow mask SRV (t28) — full-res, 1=lit, 0=shadowed
    ID3D11ShaderResourceView* GetShadowSRV() const;

    static constexpr uint32_t kSRVSlot = 28;  // t28 — contact shadow output

private:
    ContactShadowRenderer() = default;

    bool CompileShaders();
    bool CreateResources();
    bool AcquireDepthSRV();
    void ReleaseResources();

    // Pipeline pass callback
    void ExecutePass(PassContext& ctx);

    bool m_initialized = false;
    bool m_enabled     = false;  // opt-in: verify baseline first

    ID3D11Device*        m_device  = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    // Screen dimensions
    uint32_t m_screenW = 0;
    uint32_t m_screenH = 0;

    // Settings
    float m_rayLength  = 0.10f;   // Screen-space ray length (fraction of screen)
    float m_thickness  = 0.02f;   // Depth thickness tolerance (view-space)
    float m_intensity  = 1.0f;    // Shadow darkness multiplier
    int   m_maxSteps   = 16;      // Max steps per ray

    // ── Pass 1: Ray march ────────────────────────────────────────────
    ID3D11ComputeShader*       m_shadowCS      = nullptr;
    ID3D11Texture2D*           m_shadowRaw     = nullptr;  // Full-res R8_UNORM
    ID3D11ShaderResourceView*  m_shadowRawSRV  = nullptr;
    ID3D11UnorderedAccessView* m_shadowRawUAV  = nullptr;
    ID3D11Buffer*              m_shadowCB      = nullptr;

    // ── Pass 2: Spatial denoise ──────────────────────────────────────
    ID3D11ComputeShader*       m_denoiseCS     = nullptr;
    ID3D11Texture2D*           m_shadowFinal   = nullptr;  // Full-res R8_UNORM
    ID3D11ShaderResourceView*  m_shadowFinalSRV= nullptr;
    ID3D11UnorderedAccessView* m_shadowFinalUAV= nullptr;
    ID3D11Buffer*              m_denoiseCB     = nullptr;

    // Depth SRV (reacquired per frame)
    ID3D11ShaderResourceView*  m_depthSRV      = nullptr;

    // Pipeline handle
    PassHandle                 m_pipelineHandle = 0;

    uint32_t                   m_frameIndex     = 0;
};

} // namespace SB
