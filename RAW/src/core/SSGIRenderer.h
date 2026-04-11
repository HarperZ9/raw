#pragma once
//=============================================================================
//  SSGIRenderer — Screen-space global illumination via voxel cone tracing
//
//  Four-pass pipeline:
//    1. Voxelize CS (8x8x1):   Reads Hi-Z depth (t19), reconstructs normals
//       from depth gradients, copies backbuffer albedo.  Writes averaged
//       albedo+opacity into a 128^3 R16G16B16A16_FLOAT 3D voxel grid.
//    2. TraceGI CS (8x8, half-res):  Per-pixel hemisphere trace (4 rays,
//       max 32 steps) against the voxel grid.  Accumulates indirect
//       radiance from voxel albedo * direct lighting estimate.
//    3. Denoise CS (8x8, half-res):  Bilateral temporal filter with
//       depth-aware edge stopping and velocity-based history rejection.
//       Ping-pong buffers for temporal accumulation.
//    4. Upsample+Composite PS (fullscreen via RenderPassManager):
//       Bilateral upsampling from half-res GI, additive blend onto scene.
//
//  Output: GI SRV at t26, registered as PostGeometry pipeline pass.
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include "ComputeManager.h"
#include "RenderPassManager.h"
#include "RenderPipeline.h"
#include "RendererBase.h"

namespace SB
{

class SSGIRenderer : public RendererBase
{
public:
    static SSGIRenderer& Get()
    {
        static SSGIRenderer inst;
        return inst;
    }

    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc);
    void Shutdown();
    // IsInitialized / IsEnabled / SetEnabled inherited from RendererBase

    // Settings
    float GetGIIntensity() const { return m_giIntensity; }
    void  SetGIIntensity(float v) { m_giIntensity = v; }
    int   GetRayCount() const { return m_rayCount; }
    void  SetRayCount(int n) { m_rayCount = (n < 1) ? 1 : (n > 8) ? 8 : n; }
    int   GetMaxSteps() const { return m_maxSteps; }
    void  SetMaxSteps(int n) { m_maxSteps = (n < 4) ? 4 : (n > 64) ? 64 : n; }
    float GetVoxelRange() const { return m_voxelRange; }
    void  SetVoxelRange(float r) { m_voxelRange = r; }

    // GI output SRV (t26) — half-res denoised indirect lighting
    ID3D11ShaderResourceView* GetGISRV() const;

    static constexpr uint32_t kSRVSlot     = 26;  // t26 — SSGI output
    static constexpr uint32_t kVoxelRes      = 128;  // 128^3 Y-SH2 voxel grid
    static constexpr uint32_t kCoCgVoxelRes  = 64;   // 64^3 CoCg grid (chroma subsampled)
    static constexpr uint32_t kHiZSlot     = 19;   // t19 — Hi-Z depth input

private:
    SSGIRenderer() = default;

    bool CompileShaders();
public:
    /// Hot-reload: recompile shaders from disk without reinitializing resources.
    bool RecompileShaders();
private:
    bool CreateResources();
    bool AcquireDepthSRV();
    void ReleaseResources();

    // Pipeline pass callback
    void ExecutePass(PassContext& ctx);

    // Common state inherited from RendererBase
    bool m_firstFrame  = true;

    // Half-res dimensions (for trace + denoise)
    uint32_t m_halfW = 0;
    uint32_t m_halfH = 0;

    // Settings
    float m_giIntensity = 0.25f;   // voxel trace intensity (was 1.0 — too hot)
    int   m_rayCount    = 8;
    int   m_maxSteps    = 32;
    float m_voxelRange  = 2048.0f;  // World-space range of the voxel grid

    // ── Pass 1: Voxelize ────────────────────────────────────────────────
    ID3D11ComputeShader*       m_voxelizeCS       = nullptr;
    // YCoCg SH2: voxel grid stores luminance SH2 (Y_L0, Y_L1x, Y_L1y, Y_L1z)
    ID3D11Texture3D*           m_voxelGrid        = nullptr;  // 128^3 R16G16B16A16_FLOAT — Y SH2
    ID3D11ShaderResourceView*  m_voxelGridSRV     = nullptr;
    ID3D11UnorderedAccessView* m_voxelGridUAV     = nullptr;
    // YCoCg SH2: chrominance + opacity (Co, Cg, opacity, pad)
    ID3D11Texture3D*           m_voxelGridCoCg    = nullptr;  // 64^3 R16G16B16A16_FLOAT — CoCg (chroma subsampled)
    ID3D11ShaderResourceView*  m_voxelGridCoCgSRV = nullptr;
    ID3D11UnorderedAccessView* m_voxelGridCoCgUAV = nullptr;
    ID3D11Buffer*              m_voxelizeCB       = nullptr;

    // Backbuffer copy for albedo read
    ID3D11Texture2D*           m_backbufferCopy    = nullptr;
    ID3D11ShaderResourceView*  m_backbufferCopySRV = nullptr;

    // ── Pass 2: TraceGI ─────────────────────────────────────────────────
    ID3D11ComputeShader*       m_traceCS          = nullptr;
    ID3D11Texture2D*           m_giRaw             = nullptr;  // Half-res R16G16B16A16_FLOAT
    ID3D11ShaderResourceView*  m_giRawSRV          = nullptr;
    ID3D11UnorderedAccessView* m_giRawUAV          = nullptr;
    ID3D11Buffer*              m_traceCB           = nullptr;

    // ── Pass 3: Denoise ─────────────────────────────────────────────────
    ID3D11ComputeShader*       m_denoiseCS        = nullptr;
    // Ping-pong GI history (half-res R16G16B16A16_FLOAT)
    ID3D11Texture2D*           m_giHistory[2]      = {};
    ID3D11ShaderResourceView*  m_giHistorySRV[2]   = {};
    ID3D11UnorderedAccessView* m_giHistoryUAV[2]   = {};
    int                        m_pingPongIdx       = 0;
    ID3D11Buffer*              m_denoiseCB        = nullptr;

    // ── Pass 4: Upsample+Composite ──────────────────────────────────────
    RenderPassID               m_compositePass     = 0;
    ID3D11Buffer*              m_compositeCB       = nullptr;
    ID3D11BlendState*          m_additiveBlend     = nullptr;
    ID3D11SamplerState*        m_linearSampler     = nullptr;
    ID3D11SamplerState*        m_pointSampler      = nullptr;

    // Depth SRV (reacquired per frame)
    ID3D11ShaderResourceView*  m_depthSRV          = nullptr;

    // Pipeline handle
    PassHandle                 m_pipelineHandle    = 0;
};

} // namespace SB
