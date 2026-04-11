#pragma once
//=============================================================================
//  SkylightingRenderer — Hemisphere-based sky visibility for ambient lighting
//
//  Replaces Community Shaders' "Skylighting" feature.
//  Samples the upper hemisphere in screen space to determine how much sky
//  is visible from each pixel. Combined with surface normals and the
//  atmosphere model, this produces physically-motivated ambient lighting
//  that's dramatically better than Skyrim's flat ambient.
//
//  Four-pass compute pipeline:
//    0. ProbeUpdate CS (4x4x4): Fill 128x128x64 3D voxel grid with SH2
//       sky visibility.  Camera-centered, each voxel marches upward
//       through the depth buffer.
//    1. ProbeQuery CS (8x8, full-res): Per-pixel trilinear 3D sample
//       from probe grid.  Output: R16_FLOAT raw sky visibility.
//    2. Spatial Denoise CS (8x8, full-res): 5x5 bilateral filter with
//       depth-gradient + normal edge stopping.
//    3. Temporal Accumulation CS (8x8, full-res): Ping-pong history
//       for stable noise-free output.
//
//  Output: Skylighting SRV at t29 (0 = fully occluded, 1 = full sky).
//  Registered as PreENB pipeline pass — visible to ENB shaders same-frame.
//
//  VRAM budget: ~10 MB at 1920x1080 (4 R16_FLOAT full-res)
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include "ComputeManager.h"
#include "RenderPipeline.h"

namespace SB
{

class SkylightingRenderer
{
public:
    static SkylightingRenderer& Get()
    {
        static SkylightingRenderer inst;
        return inst;
    }

    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // Settings
    bool  IsEnabled() const { return m_enabled; }
    void  SetEnabled(bool v) { m_enabled = v; }
    float GetRadius() const { return m_sampleRadius; }
    void  SetRadius(float r) { m_sampleRadius = r; }
    float GetIntensity() const { return m_intensity; }
    void  SetIntensity(float v) { m_intensity = v; }
    int   GetDirections() const { return m_numDirections; }
    void  SetDirections(int n) { m_numDirections = (n < 2) ? 2 : (n > 12) ? 12 : n; }
    int   GetSteps() const { return m_numSteps; }
    void  SetSteps(int n) { m_numSteps = (n < 2) ? 2 : (n > 16) ? 16 : n; }

    // Skylighting output SRV (t29) — full-res sky visibility
    ID3D11ShaderResourceView* GetSkylightSRV() const;

    static constexpr uint32_t kSRVSlot = 29;  // t29 — skylighting output

private:
    SkylightingRenderer() = default;

    bool CompileShaders();
    bool CreateResources();
    bool AcquireDepthSRV();
    void ReleaseResources();

    // Pipeline pass callback
    void ExecutePass(PassContext& ctx);

    bool m_initialized = false;
    bool m_enabled     = false;  // opt-in: verify baseline first
    bool m_firstFrame  = true;

    ID3D11Device*        m_device  = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    // Screen dimensions
    uint32_t m_screenW = 0;
    uint32_t m_screenH = 0;

    // Settings
    float m_sampleRadius   = 3.0f;   // World-space sample radius
    float m_intensity      = 1.0f;   // Output multiplier
    int   m_numDirections  = 6;      // Hemisphere sample directions
    int   m_numSteps       = 8;      // Steps per direction

    // ── Volumetric sky probe grid ─────────────────────────────────────
    static constexpr uint32_t kProbeResXY = 128;
    static constexpr uint32_t kProbeResZ  = 64;

    ID3D11Texture3D*           m_probeGrid      = nullptr;
    ID3D11ShaderResourceView*  m_probeGridSRV   = nullptr;
    ID3D11UnorderedAccessView* m_probeGridUAV   = nullptr;
    ID3D11ComputeShader*       m_probeUpdateCS  = nullptr;
    ID3D11Buffer*              m_probeUpdateCB  = nullptr;

    float m_probeRangeXY = 8192.0f;  // World-space XY half-extent
    float m_probeRangeZ  = 4096.0f;  // World-space Z half-extent

    // Trilinear sampler for probe grid lookup
    ID3D11SamplerState*        m_linearSampler  = nullptr;

    // ── Pass 1: Probe Query CS ────────────────────────────────────────
    ID3D11ComputeShader*       m_skylightCS     = nullptr;
    ID3D11Texture2D*           m_skyRaw         = nullptr;  // Full-res R16_FLOAT
    ID3D11ShaderResourceView*  m_skyRawSRV      = nullptr;
    ID3D11UnorderedAccessView* m_skyRawUAV      = nullptr;
    ID3D11Buffer*              m_skylightCB     = nullptr;

    // ── Pass 2: Spatial denoise ──────────────────────────────────────
    ID3D11ComputeShader*       m_spatialCS      = nullptr;
    ID3D11Texture2D*           m_skySpatial     = nullptr;  // Full-res R16_FLOAT
    ID3D11ShaderResourceView*  m_skySpatialSRV  = nullptr;
    ID3D11UnorderedAccessView* m_skySpatialUAV  = nullptr;
    ID3D11Buffer*              m_spatialCB      = nullptr;

    // ── Pass 3: Temporal accumulation ────────────────────────────────
    ID3D11ComputeShader*       m_temporalCS     = nullptr;
    ID3D11Texture2D*           m_skyHistory[2]  = {};
    ID3D11ShaderResourceView*  m_skyHistorySRV[2] = {};
    ID3D11UnorderedAccessView* m_skyHistoryUAV[2] = {};
    int                        m_pingPongIdx    = 0;
    ID3D11Buffer*              m_temporalCB     = nullptr;

    // Depth SRV (reacquired per frame)
    ID3D11ShaderResourceView*  m_depthSRV       = nullptr;

    // Pipeline handle
    PassHandle                 m_pipelineHandle = 0;

    uint32_t                   m_frameIndex     = 0;
};

} // namespace SB
