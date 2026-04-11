#pragma once
//=============================================================================
//  GTAORenderer — VB-SSGI (Visibility Bitmask Screen-Space Global Illumination)
//
//  Upgraded from GTAO (Jimenez 2016) to VB-SSGI (Activision 2019,
//  "Practical Real-Time Strategies for Accurate Indirect Occlusion").
//
//  Three-pass compute pipeline:
//    1. VB-SSGI CS (8x8, full-res): Per-pixel 32-bit visibility bitmask
//       encoding over N directions x M steps. Gathers scene color from
//       visible samples for short-range indirect bounce lighting.
//       Reads Hi-Z depth (t19) + scene color copy (t1).
//       Output: R16G16B16A16_FLOAT (bounce.rgb, ao.a)
//    2. Spatial Denoise CS (8x8, full-res): 5x5 bilateral filter with
//       depth-gradient edge stopping on float4 data.
//    3. Temporal Accumulation CS (8x8, full-res): Ping-pong history,
//       depth-aware history rejection + exponential blend on float4.
//
//  Output: SRV at t20 — .rgb = indirect bounce light, .a = ambient occlusion
//  Registered as PreENB pipeline pass so output is visible to ENB shaders.
//
//  VRAM budget: ~32 MB at 1920x1080 (4 full-res R16G16B16A16_FLOAT)
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include "ComputeManager.h"
#include "RenderPipeline.h"

namespace SB
{

class GTAORenderer
{
public:
    static GTAORenderer& Get()
    {
        static GTAORenderer inst;
        return inst;
    }

    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // Settings
    bool  IsEnabled() const { return m_enabled; }
    void  SetEnabled(bool v) { m_enabled = v; }
    float GetRadius() const { return m_aoRadius; }
    void  SetRadius(float r) { m_aoRadius = r; }
    float GetIntensity() const { return m_aoIntensity; }
    void  SetIntensity(float v) { m_aoIntensity = v; }
    int   GetDirections() const { return m_numDirections; }
    void  SetDirections(int n) { m_numDirections = (n < 2) ? 2 : (n > 8) ? 8 : n; }
    int   GetSteps() const { return m_numSteps; }
    void  SetSteps(int n) { m_numSteps = (n < 2) ? 2 : (n > 16) ? 16 : n; }

    // Bounce lighting controls
    float GetBounceIntensity() const { return m_bounceIntensity; }
    void SetBounceIntensity(float v) { m_bounceIntensity = v; }
    bool IsBounceEnabled() const { return m_bounceEnabled; }
    void SetBounceEnabled(bool e) { m_bounceEnabled = e; }

    // Output now carries both AO and bounce GI
    // .rgb = indirect bounce light, .a = ambient occlusion
    ID3D11ShaderResourceView* GetOutputSRV() const;

    // Deprecated alias — use GetOutputSRV() instead
    ID3D11ShaderResourceView* GetAOSRV() const { return GetOutputSRV(); }

    static constexpr uint32_t kSRVSlot = 20;  // t20 — VB-SSGI output

private:
    GTAORenderer() = default;

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

    // Screen dimensions (full-res)
    uint32_t m_screenW = 0;
    uint32_t m_screenH = 0;

    // Settings
    float m_aoRadius      = 1.5f;   // World-space AO sample radius
    float m_aoIntensity   = 1.0f;   // Output multiplier
    int   m_numDirections = 4;      // Horizon search directions
    int   m_numSteps      = 8;      // Steps per direction

    // Bounce lighting settings
    float m_bounceIntensity = 0.5f;   // Bounce light multiplier
    bool  m_bounceEnabled   = true;   // Enable bounce color gathering

    // ── Pass 1: VB-SSGI main ────────────────────────────────────────────
    ID3D11ComputeShader*       m_gtaoCS       = nullptr;
    ID3D11Texture2D*           m_aoRaw         = nullptr;  // Full-res R16G16B16A16_FLOAT (bounce.rgb, ao.a)
    ID3D11ShaderResourceView*  m_aoRawSRV      = nullptr;
    ID3D11UnorderedAccessView* m_aoRawUAV      = nullptr;
    ID3D11Buffer*              m_gtaoCB        = nullptr;

    // ── Pass 2: Spatial denoise ───────────────────────────────────────
    ID3D11ComputeShader*       m_spatialCS     = nullptr;
    ID3D11Texture2D*           m_aoSpatial      = nullptr;  // Full-res R16G16B16A16_FLOAT
    ID3D11ShaderResourceView*  m_aoSpatialSRV   = nullptr;
    ID3D11UnorderedAccessView* m_aoSpatialUAV   = nullptr;
    ID3D11Buffer*              m_spatialCB      = nullptr;

    // ── Pass 3: Temporal accumulation ─────────────────────────────────
    ID3D11ComputeShader*       m_temporalCS    = nullptr;
    // Ping-pong history (full-res R16G16B16A16_FLOAT)
    ID3D11Texture2D*           m_aoHistory[2]   = {};
    ID3D11ShaderResourceView*  m_aoHistorySRV[2]= {};
    ID3D11UnorderedAccessView* m_aoHistoryUAV[2]= {};
    int                        m_pingPongIdx    = 0;
    ID3D11Buffer*              m_temporalCB     = nullptr;

    // Scene color copy (for bounce light gathering)
    ID3D11Texture2D*           m_sceneColorTex = nullptr;
    ID3D11ShaderResourceView*  m_sceneColorSRV = nullptr;

    // Depth SRV (reacquired per frame)
    ID3D11ShaderResourceView*  m_depthSRV       = nullptr;

    // Sampler
    ID3D11SamplerState*        m_pointSampler   = nullptr;

    // Pipeline handle
    PassHandle                 m_pipelineHandle = 0;

    uint32_t                   m_frameIndex     = 0;
};

} // namespace SB
