#pragma once
//=============================================================================
//  UnderwaterRenderer — Compute-first underwater post-processing
//
//  Replaces ENB enbunderwater.fx with a physically-based underwater pipeline.
//  Runs only when the player is submerged.
//
//  Four-pass pipeline:
//    1. Caustics CS (quarter-res, 8x8):  3-octave trochoidal wave pattern
//       with depth-stratified visibility.  Output: R8_UNORM caustic mask.
//    2. God Rays CS (quarter-res, 8x8):  Radial blur from sun screen-space
//       position, 16 depth-tested samples with exponential decay.
//       Output: R16_FLOAT god ray buffer.
//    3. Wave Distortion CS (full-res, 8x8):  4-octave Gerstner wave UV
//       displacement.  Output: R16G16_FLOAT distorted UV offset map.
//    4. Composite PS (fullscreen):  Beer-Lambert absorption, photic zone
//       grading, caustic overlay, god ray blend, Tyndall scattering,
//       Snell's window, wet lens transition, depth fog.
//
//  Pipeline stage: PrePresent, priority 5 (runs first — underwater tints
//  the entire scene before bloom/lens/DoF/color).
//
//  Condition: Only executes when PlayerData::Water.x (isUnderwater) == 1.0.
//
//  Input: Backbuffer copy (t0), depth buffer (t1).
//  Output: Writes modified backbuffer.
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include "ComputeManager.h"
#include "RenderPassManager.h"
#include "RenderPipeline.h"
#include "D3D11Hook.h"
#include "HiZPyramid.h"
#include "SceneData.h"

#include <SKSE/SKSE.h>
#include <d3dcompiler.h>
#include <cstring>
#include <cmath>

namespace SB
{

class UnderwaterRenderer
{
public:
    static UnderwaterRenderer& Get()
    {
        static UnderwaterRenderer inst;
        return inst;
    }

    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }
    bool IsEnabled() const { return m_enabled; }
    void SetEnabled(bool v) { m_enabled = v; }

    // Set underwater state (called from game state tracking / DoFrameUpdate)
    void SetUnderwater(bool uw);
    bool IsUnderwater() const { return m_underwater; }

    // Set submersion depth (world units below water surface)
    void SetSubmersionDepth(float d) { m_submersionDepth = d; }
    float GetSubmersionDepth() const { return m_submersionDepth; }

    // Set water surface world-Z for surface proximity effects
    void SetWaterSurfaceZ(float z) { m_waterSurfaceZ = z; }
    float GetWaterSurfaceZ() const { return m_waterSurfaceZ; }

    // ── Beer-Lambert absorption coefficients ─────────────────────────
    float GetAbsorptionR() const { return m_absorptionR; }
    void  SetAbsorptionR(float v) { m_absorptionR = v; }
    float GetAbsorptionG() const { return m_absorptionG; }
    void  SetAbsorptionG(float v) { m_absorptionG = v; }
    float GetAbsorptionB() const { return m_absorptionB; }
    void  SetAbsorptionB(float v) { m_absorptionB = v; }

    // ── Effect intensities ───────────────────────────────────────────
    float GetCausticIntensity() const { return m_causticIntensity; }
    void  SetCausticIntensity(float v) { m_causticIntensity = v; }
    float GetGodRayIntensity() const { return m_godRayIntensity; }
    void  SetGodRayIntensity(float v) { m_godRayIntensity = v; }
    float GetWaveIntensity() const { return m_waveIntensity; }
    void  SetWaveIntensity(float v) { m_waveIntensity = v; }
    float GetFogDensity() const { return m_fogDensity; }
    void  SetFogDensity(float v) { m_fogDensity = v; }
    float GetTyndallDensity() const { return m_tyndallDensity; }
    void  SetTyndallDensity(float v) { m_tyndallDensity = v; }

private:
    UnderwaterRenderer() = default;

    void ExecutePass(PassContext& ctx);
    bool CompileShaders();
    bool CreateResources();
    void ReleaseResources();

    bool m_initialized = false;
    bool m_enabled     = false;   // opt-in: disabled by default
    bool m_underwater  = false;

    ID3D11Device*        m_device  = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    // Screen dimensions
    uint32_t m_screenW = 0;
    uint32_t m_screenH = 0;

    // ── Settings ─────────────────────────────────────────────────────
    float m_absorptionR     = 0.45f;   // Red absorbs fastest (water physics)
    float m_absorptionG     = 0.07f;
    float m_absorptionB     = 0.02f;
    float m_causticIntensity = 0.6f;
    float m_godRayIntensity  = 0.4f;
    float m_waveIntensity    = 0.3f;
    float m_fogDensity       = 0.02f;
    float m_tyndallDensity   = 0.01f;

    // ── Dynamic state ────────────────────────────────────────────────
    float m_waterSurfaceTime = 0.0f;   // wet lens effect timer (counts up after surfacing)
    float m_totalTime        = 0.0f;   // accumulated time for wave animation
    float m_submersionDepth  = 0.0f;   // world units below surface
    float m_waterSurfaceZ    = 0.0f;   // world-space Z of water surface
    bool  m_wasUnderwater    = false;   // previous frame state (for surface transition)

    // ── Pass 1: Caustics CS (quarter-res R8_UNORM) ───────────────────
    ID3D11ComputeShader*       m_causticCS     = nullptr;
    ID3D11Texture2D*           m_causticTex    = nullptr;
    ID3D11ShaderResourceView*  m_causticSRV    = nullptr;
    ID3D11UnorderedAccessView* m_causticUAV    = nullptr;
    ID3D11Buffer*              m_causticCB     = nullptr;

    // ── Pass 2: God Rays CS (quarter-res R16_FLOAT) ──────────────────
    ID3D11ComputeShader*       m_godRayCS      = nullptr;
    ID3D11Texture2D*           m_godRayTex     = nullptr;
    ID3D11ShaderResourceView*  m_godRaySRV     = nullptr;
    ID3D11UnorderedAccessView* m_godRayUAV     = nullptr;
    ID3D11Buffer*              m_godRayCB      = nullptr;

    // ── Pass 3: Wave Distortion CS (full-res R16G16_FLOAT) ───────────
    ID3D11ComputeShader*       m_waveCS        = nullptr;
    ID3D11Texture2D*           m_waveTex       = nullptr;
    ID3D11ShaderResourceView*  m_waveSRV       = nullptr;
    ID3D11UnorderedAccessView* m_waveUAV       = nullptr;
    ID3D11Buffer*              m_waveCB        = nullptr;

    // ── Pass 4: Composite PS (fullscreen) ────────────────────────────
    RenderPassID               m_compositePass = 0;

    // ── Shared resources ─────────────────────────────────────────────
    ID3D11Texture2D*           m_bbCopyTex     = nullptr;  // backbuffer copy
    ID3D11ShaderResourceView*  m_bbCopySRV     = nullptr;
    ID3D11SamplerState*        m_pointSampler  = nullptr;  // s0: full-res exact
    ID3D11SamplerState*        m_linearSampler = nullptr;  // s1: bilinear upsample

    // Pipeline handle
    PassHandle                 m_pipelineHandle = 0;

    uint32_t                   m_frameIndex     = 0;
};

} // namespace SB
