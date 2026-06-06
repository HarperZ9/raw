#pragma once
//=============================================================================
//  VolumetricLightingRenderer — Screen-Space Volumetric Lighting (God Rays)
//
//  Screen-space light shaft / volumetric scattering using a 2-pass compute
//  pipeline:
//
//    1. Half-res Ray March CS (8x8): For each pixel, march from the camera
//       toward the sun through the depth buffer.  Uses Henyey-Greenstein
//       phase function for anisotropic scattering.  Accumulates in-scattered
//       light, accounting for depth-buffer occlusion.  Output to half-res
//       R16G16B16A16_FLOAT (scatter.rgb, transmittance.a).
//
//    2. Bilateral Upsample CS (8x8): Upsample half-res scatter result to
//       full-res with depth-aware bilateral filter.
//
//  Output: SRV at t31 — R16G16B16A16_FLOAT (scatter.rgb, transmittance.a).
//  Registered as PostGeometry pipeline pass, priority 19.
//  Dependencies: Depth buffer (D3D11Hook::GetGameDepthSRV()), sun direction
//  and camera data (SceneMatrices).
//
//  VRAM budget: ~20 MB at 1920x1080 (half-res + full-res RGBA16F)
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include "RenderPipeline.h"

namespace SB
{

class VolumetricLightingRenderer
{
public:
    static VolumetricLightingRenderer& Get()
    {
        static VolumetricLightingRenderer inst;
        return inst;
    }

    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    bool  IsEnabled() const { return m_enabled; }
    void  SetEnabled(bool v) { m_enabled = v; }

    // Scattering parameters
    float GetIntensity() const { return m_intensity; }
    void  SetIntensity(float v) { m_intensity = v; }
    float GetScatterDensity() const { return m_scatterDensity; }
    void  SetScatterDensity(float v) { m_scatterDensity = v; }
    float GetAnisotropy() const { return m_anisotropy; }
    void  SetAnisotropy(float v) { m_anisotropy = v; }
    int   GetNumSteps() const { return m_numSteps; }
    void  SetNumSteps(int n) { m_numSteps = (n < 8) ? 8 : (n > 128) ? 128 : n; }
    float GetMaxDistance() const { return m_maxDistance; }
    void  SetMaxDistance(float v) { m_maxDistance = v; }

    // Final full-res output SRV (t33) — scatter.rgb + transmittance.a
    ID3D11ShaderResourceView* GetOutputSRV() const;

    static constexpr uint32_t kSRVSlot = 33;  // t33 — volumetric lighting output (was t31, conflicted with SharedGPUResources::kLinearDepthSlot)

private:
    VolumetricLightingRenderer() = default;

    bool CompileShaders();
    bool CreateResources();
    void ReleaseResources();

    // Pipeline pass callback
    void ExecutePass(PassContext& ctx);

    bool m_initialized = false;
    bool m_enabled     = false;  // opt-in: default disabled

    ID3D11Device*        m_device  = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    // Screen dimensions
    uint32_t m_screenW = 0;
    uint32_t m_screenH = 0;

    // Settings
    float m_intensity      = 1.0f;     // 0-5 range
    float m_scatterDensity = 0.015f;   // 0.001-0.1
    float m_anisotropy     = 0.7f;     // 0-0.99, HG phase asymmetry
    int   m_numSteps       = 64;       // 8-128
    float m_maxDistance     = 5000.0f;  // 100-20000

    // ── Pass 1: Half-res ray march ───────────────────────────────────
    ID3D11ComputeShader*       m_rayMarchCS       = nullptr;
    ID3D11Texture2D*           m_halfResScatter    = nullptr;  // Half-res R16G16B16A16_FLOAT
    ID3D11ShaderResourceView*  m_halfResScatterSRV = nullptr;
    ID3D11UnorderedAccessView* m_halfResScatterUAV = nullptr;

    // ── Pass 2: Full-res bilateral upsample ──────────────────────────
    ID3D11ComputeShader*       m_upsampleCS       = nullptr;
    ID3D11Texture2D*           m_fullResOutput     = nullptr;  // Full-res R16G16B16A16_FLOAT
    ID3D11ShaderResourceView*  m_fullResOutputSRV  = nullptr;
    ID3D11UnorderedAccessView* m_fullResOutputUAV  = nullptr;

    // Shared resources
    ID3D11Buffer*              m_constantsCB       = nullptr;
    ID3D11SamplerState*        m_linearClampSampler = nullptr;

    // Pipeline handle
    PassHandle                 m_pipelineHandle = 0;

    uint32_t                   m_frameIndex     = 0;
};

} // namespace SB
