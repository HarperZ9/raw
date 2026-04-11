#pragma once
//=============================================================================
//  DoFRenderer --- Compute-First Physical Depth-of-Field
//
//  Fully compute-driven physical
//  DoF pipeline that runs as a PrePresent pass (priority 30, after Bloom at
//  10, before ColorPipeline at 50).
//
//  Multi-pass pipeline:
//
//    Pass 1: Autofocus CS (1x1 dispatch)
//            10x10 weighted depth grid around screen center, variance
//            rejection (>2-sigma from median discarded), temporal smoothing.
//            Output: 1-element structured buffer with focus distance.
//
//    Pass 2: CoC + Tile Classification CS (full-res -> 16x16 tiles)
//            Physical thin-lens CoC per pixel (signed: +far, -near).
//            Groupshared min/max CoC per 16x16 tile for gather radius.
//            Output: full-res R16G16_FLOAT CoC map, tile buffer R16G16_SINT.
//
//    Pass 3: Far Field Bokeh Gather CS (half-res)
//            N-gon shaped gather (4-9 configurable blades, roundness,
//            cat-eye vignette, spherical aberration).  48-128 samples per
//            quality preset.  Scatter-as-gather pattern.
//
//    Pass 4: Near Field Bokeh Gather CS (half-res)
//            Same N-gon gather for negative CoC.  Near field bleeds into
//            focused areas via dilated near CoC from the tile buffer.
//            Alpha stores near contribution weight.
//
//    Pass 5: Composite + Effects PS (fullscreen)
//            Bilateral upsample from half-res, blend far+near by CoC sign,
//            longitudinal chromatic aberration, anamorphic stretch,
//            optional focus peaking debug overlay, triangular dither.
//
//  VRAM budget: ~40 MB at 1920x1080 (backbuffer copy, CoC map, tile buffer,
//               far/near half-res R16G16B16A16_FLOAT, focus buffer, output RT)
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include "ComputeManager.h"
#include "RenderPassManager.h"
#include "RenderPipeline.h"

namespace SB
{

class DoFRenderer
{
public:
    static DoFRenderer& Get()
    {
        static DoFRenderer inst;
        return inst;
    }

    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // Enable/disable
    bool IsEnabled() const { return m_enabled; }
    void SetEnabled(bool v) { m_enabled = v; }

    // ── Lens settings ──────────────────────────────────────────────────
    float GetAperture() const { return m_aperture; }
    void  SetAperture(float v) { m_aperture = (v < 1.4f) ? 1.4f : (v > 22.0f) ? 22.0f : v; }

    float GetFocalLength() const { return m_focalLength; }
    void  SetFocalLength(float v) { m_focalLength = (v < 24.f) ? 24.f : (v > 200.f) ? 200.f : v; }

    float GetFocusDistance() const { return m_focusDist; }
    void  SetManualFocus(float dist) { m_manualFocus = dist; }  // 0 = autofocus

    // ── Bokeh shape ────────────────────────────────────────────────────
    int   GetBladeCount() const { return m_bladeCount; }
    void  SetBladeCount(int n) { m_bladeCount = (n < 4) ? 4 : (n > 9) ? 9 : n; }

    float GetRoundness() const { return m_roundness; }
    void  SetRoundness(float v) { m_roundness = (v < 0.f) ? 0.f : (v > 1.f) ? 1.f : v; }

    float GetCatEyeAmount() const { return m_catEye; }
    void  SetCatEyeAmount(float v) { m_catEye = (v < 0.f) ? 0.f : (v > 1.f) ? 1.f : v; }

    float GetAnamorphicRatio() const { return m_anamorphic; }
    void  SetAnamorphicRatio(float v) { m_anamorphic = (v < 0.f) ? 0.f : (v > 1.f) ? 1.f : v; }

    // ── Post effects ───────────────────────────────────────────────────
    float GetCAStrength() const { return m_caStrength; }
    void  SetCAStrength(float v) { m_caStrength = (v < 0.f) ? 0.f : (v > 2.f) ? 2.f : v; }

    bool  IsFocusPeaking() const { return m_focusPeaking; }
    void  SetFocusPeaking(bool v) { m_focusPeaking = v; }

    // ── Quality ────────────────────────────────────────────────────────
    enum Quality { Low = 0, Medium, High, Ultra };
    Quality GetQuality() const { return m_quality; }
    void    SetQuality(Quality q) { m_quality = q; }

    // ── Output SRV ─────────────────────────────────────────────────────
    ID3D11ShaderResourceView* GetDoFOutputSRV() const;

private:
    DoFRenderer() = default;

    // Pipeline pass callback
    void ExecutePass(PassContext& ctx);

    bool CompileShaders();
    bool CreateResources();
    void ReleaseResources();

    bool m_initialized = false;
    bool m_enabled     = false;  // opt-in: default disabled

    ID3D11Device*        m_device  = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    // Screen dimensions
    uint32_t m_screenW = 0;
    uint32_t m_screenH = 0;
    uint32_t m_halfW   = 0;
    uint32_t m_halfH   = 0;
    uint32_t m_tileCountX = 0;
    uint32_t m_tileCountY = 0;

    // ── Lens settings ──────────────────────────────────────────────────
    float m_aperture    = 2.8f;      // f-number (1.4 - 22.0)
    float m_focalLength = 50.0f;     // mm (24 - 200)
    float m_manualFocus = 0.0f;      // 0 = autofocus
    float m_focusDist   = 500.0f;    // Current focus distance (world units)

    // ── Bokeh shape ────────────────────────────────────────────────────
    int   m_bladeCount  = 6;         // Aperture blade count (4-9)
    float m_roundness   = 0.5f;      // 0 = polygon, 1 = circle
    float m_catEye      = 0.3f;      // Cat-eye optical vignette amount
    float m_anamorphic  = 0.0f;      // Anamorphic horizontal stretch

    // ── Post effects ───────────────────────────────────────────────────
    float m_caStrength  = 0.5f;      // Longitudinal CA strength
    bool  m_focusPeaking = false;    // Focus peaking debug overlay

    // ── Quality / limits ───────────────────────────────────────────────
    Quality  m_quality        = Medium;
    float    m_maxBokehRadius = 40.0f;   // Max CoC in pixels
    float    m_focusSpeed     = 3.0f;    // Autofocus temporal smoothing speed
    float    m_prevFocusDist  = 500.0f;  // Previous frame focus distance

    // ── Pass 1: Autofocus CS ───────────────────────────────────────────
    ID3D11ComputeShader*       m_autofocusCS     = nullptr;
    ID3D11Buffer*              m_focusBuf        = nullptr;   // 1-element structured
    ID3D11ShaderResourceView*  m_focusBufSRV     = nullptr;
    ID3D11UnorderedAccessView* m_focusBufUAV     = nullptr;
    ID3D11Buffer*              m_autofocusCB     = nullptr;

    // ── Pass 2: CoC + Tile CS ──────────────────────────────────────────
    ID3D11ComputeShader*       m_cocCS           = nullptr;
    ID3D11Texture2D*           m_cocTex          = nullptr;   // Full-res R16G16_FLOAT
    ID3D11ShaderResourceView*  m_cocSRV          = nullptr;
    ID3D11UnorderedAccessView* m_cocUAV          = nullptr;
    ID3D11Texture2D*           m_tileTex         = nullptr;   // Tile buffer R16G16_FLOAT
    ID3D11ShaderResourceView*  m_tileSRV         = nullptr;
    ID3D11UnorderedAccessView* m_tileUAV         = nullptr;
    ID3D11Buffer*              m_cocCB           = nullptr;

    // ── Pass 3: Far field gather CS ────────────────────────────────────
    ID3D11ComputeShader*       m_farGatherCS     = nullptr;
    ID3D11Texture2D*           m_farTex          = nullptr;   // Half-res RGBA16F
    ID3D11ShaderResourceView*  m_farSRV          = nullptr;
    ID3D11UnorderedAccessView* m_farUAV          = nullptr;
    ID3D11Buffer*              m_farCB           = nullptr;

    // ── Pass 4: Near field gather CS ───────────────────────────────────
    ID3D11ComputeShader*       m_nearGatherCS    = nullptr;
    ID3D11Texture2D*           m_nearTex         = nullptr;   // Half-res RGBA16F
    ID3D11ShaderResourceView*  m_nearSRV         = nullptr;
    ID3D11UnorderedAccessView* m_nearUAV         = nullptr;
    ID3D11Buffer*              m_nearCB          = nullptr;

    // ── Pass 5: Composite PS ───────────────────────────────────────────
    RenderPassID               m_compositePass   = 0;
    ID3D11Buffer*              m_compositeCB     = nullptr;

    // ── Shared resources ───────────────────────────────────────────────
    ID3D11Texture2D*           m_backbufferCopy    = nullptr;
    ID3D11ShaderResourceView*  m_backbufferCopySRV = nullptr;
    ID3D11SamplerState*        m_linearSampler     = nullptr;
    ID3D11SamplerState*        m_pointSampler      = nullptr;

    // ── Output RT ──────────────────────────────────────────────────────
    ID3D11Texture2D*           m_outputTex = nullptr;
    ID3D11ShaderResourceView*  m_outputSRV = nullptr;
    ID3D11RenderTargetView*    m_outputRTV = nullptr;

    // ── Pipeline handle ────────────────────────────────────────────────
    PassHandle                 m_pipelineHandle = 0;
    uint32_t                   m_frameIndex     = 0;
};

} // namespace SB
