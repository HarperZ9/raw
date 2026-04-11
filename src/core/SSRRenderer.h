#pragma once
//=============================================================================
//  SSRRenderer — Screen-Space Reflections via Hi-Z ray marching
//
//  Four-pass compute pipeline (Stachowiak 2015):
//    1. RayMarch CS (8×8, half-res): Hi-Z accelerated ray march per pixel.
//       Starts at coarse mip, refines to fine.  Max 64 steps per ray.
//       Reads Hi-Z depth (t19), scene normals from depth gradients.
//       Output: R16G16_FLOAT hit UV + R16_FLOAT confidence.
//    2. Resolve CS (8×8, half-res): Sample scene color at hit UV,
//       apply Fresnel attenuation + distance fade + edge fade.
//       Output: R16G16B16A16_FLOAT (reflected color + alpha).
//    3. Temporal Denoise CS (8×8, half-res): Ping-pong history,
//       depth-aware rejection + velocity-based blend.
//    4. Composite PS (fullscreen): Bilateral upsample from half-res,
//       additive blend onto scene.  (Registered with RenderPipeline,
//       but for now we just register the SRV at t21.)
//
//  Output: Reflection SRV at t21, registered as PreENB pipeline pass.
//
//  VRAM budget: ~24 MB at 1920×1080 (half-res intermediates)
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include "ComputeManager.h"
#include "RenderPipeline.h"

namespace SB
{

class SSRRenderer
{
public:
    static SSRRenderer& Get()
    {
        static SSRRenderer inst;
        return inst;
    }

    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // Settings
    bool  IsEnabled() const { return m_enabled; }
    void  SetEnabled(bool v) { m_enabled = v; }
    float GetMaxDistance() const { return m_maxDistance; }
    void  SetMaxDistance(float d) { m_maxDistance = d; }
    float GetThickness() const { return m_thickness; }
    void  SetThickness(float t) { m_thickness = t; }
    float GetIntensity() const { return m_intensity; }
    void  SetIntensity(float v) { m_intensity = v; }
    int   GetMaxSteps() const { return m_maxSteps; }
    void  SetMaxSteps(int n) { m_maxSteps = (n < 8) ? 8 : (n > 128) ? 128 : n; }

    // Reflection output SRV (t21) — half-res denoised reflections
    ID3D11ShaderResourceView* GetReflectionSRV() const;

    static constexpr uint32_t kSRVSlot = 27;  // t27 — SSR output (was t21, conflicted with ClusteredLighting)

private:
    SSRRenderer() = default;

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
    uint32_t m_halfW   = 0;
    uint32_t m_halfH   = 0;

    // Settings
    float m_maxDistance = 100.0f;  // World-space max ray distance
    float m_thickness  = 0.5f;    // Depth comparison thickness
    float m_intensity  = 1.0f;    // Reflection intensity
    int   m_maxSteps   = 64;      // Max Hi-Z march steps

    // ── Pass 1: RayMarch ──────────────────────────────────────────────
    ID3D11ComputeShader*       m_rayMarchCS    = nullptr;
    ID3D11Texture2D*           m_hitBuffer      = nullptr;  // Half-res R16G16B16A16_FLOAT (UV.xy + viewZ + confidence)
    ID3D11ShaderResourceView*  m_hitBufferSRV   = nullptr;
    ID3D11UnorderedAccessView* m_hitBufferUAV   = nullptr;
    ID3D11Buffer*              m_rayMarchCB     = nullptr;

    // ── Pass 2: Resolve ───────────────────────────────────────────────
    ID3D11ComputeShader*       m_resolveCS     = nullptr;
    ID3D11Texture2D*           m_reflRaw        = nullptr;  // Half-res R16G16B16A16_FLOAT
    ID3D11ShaderResourceView*  m_reflRawSRV     = nullptr;
    ID3D11UnorderedAccessView* m_reflRawUAV     = nullptr;
    ID3D11Buffer*              m_resolveCB      = nullptr;

    // Backbuffer copy for reflection color sampling
    ID3D11Texture2D*           m_backbufferCopy    = nullptr;
    ID3D11ShaderResourceView*  m_backbufferCopySRV = nullptr;

    // ── Pass 3: Temporal denoise ──────────────────────────────────────
    ID3D11ComputeShader*       m_temporalCS    = nullptr;
    ID3D11Texture2D*           m_reflHistory[2] = {};
    ID3D11ShaderResourceView*  m_reflHistorySRV[2] = {};
    ID3D11UnorderedAccessView* m_reflHistoryUAV[2] = {};
    int                        m_pingPongIdx    = 0;
    ID3D11Buffer*              m_temporalCB     = nullptr;

    // Depth SRV (reacquired per frame)
    ID3D11ShaderResourceView*  m_depthSRV       = nullptr;

    // Samplers
    ID3D11SamplerState*        m_linearSampler  = nullptr;
    ID3D11SamplerState*        m_pointSampler   = nullptr;

    // Pipeline handle
    PassHandle                 m_pipelineHandle = 0;

    uint32_t                   m_frameIndex     = 0;
};

} // namespace SB
