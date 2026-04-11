#pragma once
//=============================================================================
//  SubsurfaceScatteringRenderer — Screen-space subsurface scattering
//
//  Applies a separable Burley diffusion profile approximation to skin and
//  foliage pixels identified by MaterialClassifier.  Two compute passes
//  (horizontal + vertical) blur the backbuffer with depth-aware bilateral
//  weights to prevent bleeding across depth discontinuities.
//
//  Pipeline:
//    1. Copy backbuffer for safe reading
//    2. Horizontal blur CS:  read copy  -> write intermediate (R16G16B16A16F)
//    3. Vertical   blur CS:  read intermediate -> write backbuffer UAV
//
//  Both passes use the same shader (direction selected via constant buffer).
//  Only pixels classified as MAT_SKIN (1) or MAT_FOLIAGE (4) are blurred;
//  all other pixels are copied through unmodified.
//
//  Output: Modifies backbuffer in-place via UAV (no SRV output slot).
//  Registered as PostGeometry pipeline pass, priority 20.
//
//  Dependencies:
//    - D3D11Hook::GetGameDepthSRV()       (depth)
//    - MaterialClassifier::GetMaterialSRV() (material IDs)
//    - D3D11Hook::GetGBufferNormalsSRV()  (G-buffer normals)
//
//  VRAM budget: ~40 MB at 1920x1080 (2x full-res R16G16B16A16_FLOAT)
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include "RenderPipeline.h"

namespace SB
{

class SubsurfaceScatteringRenderer
{
public:
    static SubsurfaceScatteringRenderer& Get()
    {
        static SubsurfaceScatteringRenderer inst;
        return inst;
    }

    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    bool  IsEnabled() const { return m_enabled; }
    void  SetEnabled(bool v) { m_enabled = v; }

    // SSS parameters
    float GetSSSRadius() const { return m_sssRadius; }
    void  SetSSSRadius(float v) { m_sssRadius = v; }
    float GetSSSStrength() const { return m_sssStrength; }
    void  SetSSSStrength(float v) { m_sssStrength = v; }
    float GetTranslucency() const { return m_translucency; }
    void  SetTranslucency(float v) { m_translucency = v; }

    // Per-channel Burley widths for skin
    float GetSkinWidthR() const { return m_skinWidthR; }
    void  SetSkinWidthR(float v) { m_skinWidthR = v; }
    float GetSkinWidthG() const { return m_skinWidthG; }
    void  SetSkinWidthG(float v) { m_skinWidthG = v; }
    float GetSkinWidthB() const { return m_skinWidthB; }
    void  SetSkinWidthB(float v) { m_skinWidthB = v; }

    // Foliage SSS width (broader than skin)
    float GetFoliageWidth() const { return m_foliageWidth; }
    void  SetFoliageWidth(float v) { m_foliageWidth = v; }

private:
    SubsurfaceScatteringRenderer() = default;

    bool CompileShaders();
    bool CreateResources();
    void ReleaseResources();

    void ExecutePass(PassContext& ctx);

    bool m_initialized = false;
    bool m_enabled     = false;  // opt-in: writes backbuffer via UAV

    ID3D11Device*        m_device  = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    uint32_t m_screenW = 0;
    uint32_t m_screenH = 0;

    // SSS settings
    float m_sssRadius    = 0.012f;   // Screen-space blur radius (0.001-0.05)
    float m_sssStrength  = 0.5f;     // Overall SSS intensity (0-2)
    float m_translucency = 0.3f;     // Back-lighting translucency (0-1)
    float m_skinWidthR   = 0.012f;   // Per-channel Burley widths (skin)
    float m_skinWidthG   = 0.008f;
    float m_skinWidthB   = 0.004f;
    float m_foliageWidth = 0.015f;   // Foliage SSS width (broader than skin)

    // Compute shader (single shader for both H and V passes)
    ID3D11ComputeShader* m_sssBlurCS = nullptr;
    ID3D11Buffer*        m_constantsCB = nullptr;

    // Intermediate texture (horizontal blur output, vertical blur input)
    ID3D11Texture2D*            m_intermediateTex = nullptr;
    ID3D11ShaderResourceView*   m_intermediateSRV = nullptr;
    ID3D11UnorderedAccessView*  m_intermediateUAV = nullptr;

    // Backbuffer copy texture (source for reading while writing backbuffer)
    ID3D11Texture2D*            m_bbCopyTex = nullptr;
    ID3D11ShaderResourceView*   m_bbCopySRV = nullptr;

    // Backbuffer UAV (created per-frame)
    ID3D11UnorderedAccessView*  m_backbufferUAV = nullptr;

    // Point sampler
    ID3D11SamplerState* m_pointSampler = nullptr;

    // Pipeline handle
    PassHandle m_pipelineHandle = 0;
    uint32_t   m_frameIndex     = 0;
};

} // namespace SB
