#pragma once
//=============================================================================
//  LensRenderer — Physically-Based Lens Effects (Compute-First)
//
//  Replaces ENB's enblens.fx with a multi-pass compute+pixel pipeline for
//  physically-motivated lens artifacts: ghosts, starburst, veiling glare,
//  anamorphic flares, barrel/pincushion distortion, spectral chromatic
//  aberration, cos^4 vignette, and sensor clipping.
//
//  6-pass pipeline:
//    1. Downsample + Bright Extract CS (4 mips: 1/2->1/4->1/8->1/16)
//       Circular box blur during downsample (4-tap with UV offset).
//
//    2. Ghost Evaluation CS (quarter-res)
//       ABCD matrix ghosts (6) + polynomial hero ghosts (2).
//       Thin-film MgF2 interference coating colors.
//       Fresnel reflectance per ghost surface.
//
//    3. Starburst + Veiling Glare CS (quarter-res)
//       Diffraction starburst for N aperture blades with sinc^2 falloff.
//       Spectral color per ray.  Veiling glare from bloom lowest mip.
//
//    4. Anamorphic Lens Flare CS (half-res)
//       Horizontal/rotatable streak with exponential falloff.
//       Spectral dispersion (R wider than B).  Secondary 90-deg streak.
//
//    5. Composite + Distortion + CA PS (fullscreen)
//       Brown-Conrady barrel/pincushion distortion (4 lens presets).
//       6-band spectral chromatic aberration via Cauchy dispersion.
//       cos^4(theta) optical vignette.  Sensor per-channel soft clip.
//       Lens dirt overlay modulated by ghost/flare brightness.
//
//    6. (Lens dirt is embedded in Pass 5 composite.)
//
//  Pipeline stage: PrePresent, priority 20
//  Input:  Backbuffer copy (t0), BloomRenderer mips (veiling glare proxy)
//  Output: Composited lens effects onto backbuffer
//
//  VRAM budget: ~28 MB at 1920x1080 (4 downsample mips + ghost + starburst
//               + anamorphic + composite targets, R16G16B16A16_FLOAT)
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include "ComputeManager.h"
#include "RenderPassManager.h"
#include "RenderPipeline.h"
#include "BloomRenderer.h"
#include "D3D11Hook.h"
#include "SceneData.h"

#include <SKSE/SKSE.h>
#include <d3dcompiler.h>

namespace SB
{

class LensRenderer
{
public:
    static LensRenderer& Get()
    {
        static LensRenderer inst;
        return inst;
    }

    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // Enable/disable
    bool IsEnabled() const { return m_enabled; }
    void SetEnabled(bool v) { m_enabled = v; }

    // ── Ghost settings ──────────────────────────────────────────────────
    float GetGhostIntensity() const { return m_ghostIntensity; }
    void  SetGhostIntensity(float v) { m_ghostIntensity = v; }
    int   GetGhostCount() const { return m_ghostCount; }
    void  SetGhostCount(int n) { m_ghostCount = (n < 2) ? 2 : (n > 8) ? 8 : n; }

    // ── Starburst settings ──────────────────────────────────────────────
    float GetStarburstIntensity() const { return m_starburstIntensity; }
    void  SetStarburstIntensity(float v) { m_starburstIntensity = v; }
    int   GetApertureBlades() const { return m_apertureBlades; }
    void  SetApertureBlades(int n) { m_apertureBlades = (n < 4) ? 4 : (n > 9) ? 9 : n; }

    // ── Flare ───────────────────────────────────────────────────────────
    float GetFlareIntensity() const { return m_flareIntensity; }
    void  SetFlareIntensity(float v) { m_flareIntensity = v; }

    // ── Distortion ──────────────────────────────────────────────────────
    enum LensPreset : int { Cooke = 0, Zeiss, MIR1, Primo, Custom };
    void  SetLensPreset(LensPreset p);
    float GetDistortionK1() const { return m_distortionK1; }
    void  SetDistortionK1(float v) { m_distortionK1 = v; }
    float GetDistortionK2() const { return m_distortionK2; }
    void  SetDistortionK2(float v) { m_distortionK2 = v; }

    // ── Chromatic Aberration ────────────────────────────────────────────
    float GetCAStrength() const { return m_caStrength; }
    void  SetCAStrength(float v) { m_caStrength = v; }

    // ── Vignette ────────────────────────────────────────────────────────
    float GetVignetteStrength() const { return m_vignetteStrength; }
    void  SetVignetteStrength(float v) { m_vignetteStrength = v; }

    // ── Veiling Glare ───────────────────────────────────────────────────
    float GetVeilingGlareStrength() const { return m_veilingGlareStrength; }
    void  SetVeilingGlareStrength(float v) { m_veilingGlareStrength = v; }

    // ── Lens Dirt ───────────────────────────────────────────────────────
    float GetDirtIntensity() const { return m_dirtIntensity; }
    void  SetDirtIntensity(float v) { m_dirtIntensity = v; }

private:
    LensRenderer() = default;

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

    // ── Settings ────────────────────────────────────────────────────────
    float m_ghostIntensity      = 0.15f;
    int   m_ghostCount          = 8;       // 2-8 ghosts
    float m_starburstIntensity  = 0.10f;
    int   m_apertureBlades      = 6;       // 4-9
    float m_flareIntensity      = 0.08f;
    float m_distortionK1        = -0.02f;  // Zeiss default
    float m_distortionK2        = 0.001f;
    float m_caStrength          = 0.003f;
    float m_vignetteStrength    = 1.0f;
    float m_veilingGlareStrength= 0.02f;
    float m_dirtIntensity       = 0.05f;

    // ── Pass 1: Downsample + Bright Extract CS ─────────────────────────
    ID3D11ComputeShader* m_downsampleCS = nullptr;
    ID3D11Buffer*        m_downsampleCB = nullptr;

    // 4 mip levels (half, quarter, eighth, sixteenth)
    struct MipLevel
    {
        ID3D11Texture2D*           tex = nullptr;
        ID3D11ShaderResourceView*  srv = nullptr;
        ID3D11UnorderedAccessView* uav = nullptr;
        uint32_t w = 0, h = 0;
    };
    MipLevel m_downMips[4];

    // ── Pass 2: Ghost Evaluation CS ────────────────────────────────────
    ID3D11ComputeShader*       m_ghostCS       = nullptr;
    ID3D11Buffer*              m_ghostCB       = nullptr;
    ID3D11Texture2D*           m_ghostTex      = nullptr;
    ID3D11ShaderResourceView*  m_ghostSRV      = nullptr;
    ID3D11UnorderedAccessView* m_ghostUAV      = nullptr;

    // ── Pass 3: Starburst + Veiling Glare CS ───────────────────────────
    ID3D11ComputeShader*       m_starburstCS   = nullptr;
    ID3D11Buffer*              m_starburstCB   = nullptr;
    ID3D11Texture2D*           m_starburstTex  = nullptr;
    ID3D11ShaderResourceView*  m_starburstSRV  = nullptr;
    ID3D11UnorderedAccessView* m_starburstUAV  = nullptr;

    // ── Pass 4: Anamorphic Flare CS ────────────────────────────────────
    ID3D11ComputeShader*       m_anamorphicCS  = nullptr;
    ID3D11Buffer*              m_anamorphicCB  = nullptr;
    ID3D11Texture2D*           m_anamorphicTex = nullptr;
    ID3D11ShaderResourceView*  m_anamorphicSRV = nullptr;
    ID3D11UnorderedAccessView* m_anamorphicUAV = nullptr;

    // ── Pass 5: Composite + Distortion + CA PS ─────────────────────────
    RenderPassID m_compositePass = 0;
    ID3D11Buffer*              m_compositeCB   = nullptr;

    // ── Backbuffer copy (source for passes) ────────────────────────────
    ID3D11Texture2D*           m_backbufferCopy    = nullptr;
    ID3D11ShaderResourceView*  m_backbufferCopySRV = nullptr;

    // ── Sampler ────────────────────────────────────────────────────────
    ID3D11SamplerState* m_linearClampSampler = nullptr;

    // ── Pipeline handle ────────────────────────────────────────────────
    PassHandle m_pipelineHandle = 0;

    uint32_t m_frameIndex = 0;
};

} // namespace SB
