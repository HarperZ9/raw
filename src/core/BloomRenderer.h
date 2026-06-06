#pragma once
//=============================================================================
//  BloomRenderer — Compute-First Multi-Pass Bloom Pipeline
//
//  Fully compute-driven bloom pipeline
//  that runs as a PrePresent pass (priority 10).
//
//  8 compute passes + 1 pixel shader composite:
//
//    Pass 1: Bright Extract CS (full-res -> half-res)
//            Karis anti-firefly 2x2 weighted average with soft-knee threshold.
//
//    Pass 2-4: Downsample CS (Jimenez 13-tap, 3 levels: 1/4 -> 1/8 -> 1/16)
//              Progressive downsampling with energy conservation.
//
//    Pass 5-7: Upsample CS (9-tap tent filter, 3 levels: 1/16 -> 1/8 -> 1/4 -> 1/2)
//              Additive blend with corresponding downsample level + spectral tinting.
//
//    Pass 8: Anamorphic Streak CS (optional, half-res horizontal)
//            9-tap horizontal blur with exponential falloff + spectral dispersion.
//
//    Pass 9: Composite PS (fullscreen, writes to bloom output RT)
//            Blends all mip levels with user weights, chromatic dispersion,
//            color temperature tinting, energy-conserving blend.
//
//  Output: Bloom texture accessible via GetBloomSRV() for downstream systems
//          (ColorPipeline, ToneMapManager, etc.)
//
//  VRAM budget: ~20 MB at 1920x1080 (half + quarter + eighth + sixteenth
//               downsample + upsample chains, R16G16B16A16_FLOAT)
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include "ComputeManager.h"
#include "RenderPassManager.h"
#include "RenderPipeline.h"

namespace SB
{

class BloomRenderer
{
public:
    static BloomRenderer& Get()
    {
        static BloomRenderer inst;
        return inst;
    }

    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // Enable/disable
    bool IsEnabled() const { return m_enabled; }
    void SetEnabled(bool v) { m_enabled = v; }

    // Settings
    float GetThreshold() const { return m_threshold; }
    void  SetThreshold(float v) { m_threshold = v; }
    float GetIntensity() const { return m_intensity; }
    void  SetIntensity(float v) { m_intensity = v; }
    float GetKnee() const { return m_knee; }
    void  SetKnee(float v) { m_knee = v; }
    float GetAnamorphicIntensity() const { return m_anamorphicIntensity; }
    void  SetAnamorphicIntensity(float v) { m_anamorphicIntensity = v; }
    float GetColorTemp() const { return m_colorTemp; }
    void  SetColorTemp(float v) { m_colorTemp = v; }

    // Output — bloom texture SRV for downstream consumers
    ID3D11ShaderResourceView* GetBloomSRV() const;

private:
    BloomRenderer() = default;

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
    float m_threshold          = 1.0f;    // Brightness threshold for extraction
    float m_intensity          = 0.5f;    // Overall bloom intensity
    float m_knee               = 0.5f;    // Soft-knee transition width
    float m_anamorphicIntensity = 0.0f;   // Anamorphic streak strength (0 = off)
    float m_colorTemp          = 6500.0f; // Color temperature in Kelvin

    // ── Mip chain textures (downsample) ────────────────────────────────
    // Level 0 = half-res, 1 = quarter, 2 = eighth, 3 = sixteenth
    struct MipLevel
    {
        ID3D11Texture2D*           tex = nullptr;
        ID3D11ShaderResourceView*  srv = nullptr;
        ID3D11UnorderedAccessView* uav = nullptr;
        uint32_t w = 0, h = 0;
    };
    MipLevel m_mips[4];     // downsample chain

    // ── Upsample scratch textures ──────────────────────────────────────
    // Only 3 levels: upsample goes from sixteenth -> eighth -> quarter -> half.
    // The sixteenth-res data comes directly from m_mips[3] (no upsample for smallest).
    MipLevel m_upMips[4];   // indices 0-2 used; index 3 unused (zeroed)

    // ── Final bloom output ─────────────────────────────────────────────
    ID3D11Texture2D*           m_bloomTex = nullptr;
    ID3D11ShaderResourceView*  m_bloomSRV = nullptr;
    ID3D11RenderTargetView*    m_bloomRTV = nullptr;

    // ── Anamorphic scratch ─────────────────────────────────────────────
    MipLevel m_anamorphic;

    // ── Compute shaders ────────────────────────────────────────────────
    ID3D11ComputeShader* m_extractCS    = nullptr;
    ID3D11ComputeShader* m_downsampleCS = nullptr;
    ID3D11ComputeShader* m_upsampleCS   = nullptr;
    ID3D11ComputeShader* m_anamorphicCS = nullptr;

    // ── Pixel shader composite ─────────────────────────────────────────
    RenderPassID m_compositePass = 0;

    // ── Constant buffers ───────────────────────────────────────────────
    ID3D11Buffer* m_extractCB    = nullptr;
    ID3D11Buffer* m_downsampleCB = nullptr;
    ID3D11Buffer* m_upsampleCB   = nullptr;
    ID3D11Buffer* m_compositeCB  = nullptr;

    // ── Sampler ────────────────────────────────────────────────────────
    ID3D11SamplerState* m_linearSampler = nullptr;

    // ── Backbuffer copy (source for bright extract) ────────────────────
    ID3D11Texture2D*           m_backbufferCopy    = nullptr;
    ID3D11ShaderResourceView*  m_backbufferCopySRV = nullptr;

    // ── Pipeline handle ────────────────────────────────────────────────
    PassHandle m_pipelineHandle = 0;

    uint32_t m_frameIndex = 0;
};

} // namespace SB
