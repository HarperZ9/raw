#pragma once
//=============================================================================
//  IndirectSpecularRenderer — Indirect Specular via SSR + Cubemap Fallback
//
//  Combines near-field screen-space reflections (SSR at t21) with far-field
//  dynamic cubemap reflections (t30) into a single unified specular GI term.
//
//  Single compute pass (8x8, full-res):
//    1. Read SSR output (t21): .rgb = reflected color, .a = confidence/mask
//    2. Read dynamic cubemap (t30): sample with reflected direction,
//       roughness-based mip selection for glossy vs blurry reflections
//    3. Read G-buffer normals + depth for reflection vector reconstruction
//    4. Read material classification for per-material roughness estimation
//    5. Blend SSR and cubemap based on SSR confidence and roughness
//    6. Apply Schlick Fresnel for energy-conserving specular response
//    7. Output combined indirect specular reflection
//
//  Roughness-based blending: smooth surfaces prefer SSR (screen-accurate),
//  rough surfaces prefer cubemap (stable, noise-free).  Surfaces above
//  the roughness threshold receive no specular contribution.
//
//  Output: Indirect specular SRV at t32 — R16G16B16A16_FLOAT
//          (.rgb = specular GI color, .a = confidence)
//
//  Registered as PostGeometry pipeline pass (priority 21).
//
//  Dependencies: SSRRenderer (t21), DynamicCubemapRenderer (t30),
//                depth buffer, G-buffer normals, material classification.
//
//  VRAM budget: ~32 MB at 1920x1080 (full-res R16G16B16A16_FLOAT + CB)
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include "RenderPipeline.h"

namespace SB
{

class IndirectSpecularRenderer
{
public:
    static IndirectSpecularRenderer& Get()
    {
        static IndirectSpecularRenderer inst;
        return inst;
    }

    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // Settings
    bool  IsEnabled() const { return m_enabled; }
    void  SetEnabled(bool v) { m_enabled = v; }
    float GetIntensity() const { return m_intensity; }
    void  SetIntensity(float v) { m_intensity = v; }
    float GetCubemapFallback() const { return m_cubemapFallback; }
    void  SetCubemapFallback(float v) { m_cubemapFallback = v; }
    float GetFresnelBias() const { return m_fresnelBias; }
    void  SetFresnelBias(float v) { m_fresnelBias = v; }
    float GetRoughnessThreshold() const { return m_roughnessThreshold; }
    void  SetRoughnessThreshold(float v) { m_roughnessThreshold = v; }

    // Indirect specular output SRV (t32) — full-res combined reflection
    ID3D11ShaderResourceView* GetOutputSRV() const;

    static constexpr uint32_t kSRVSlot = 32;  // t32 — indirect specular output

private:
    IndirectSpecularRenderer() = default;

    bool CompileShaders();
    bool CreateResources();
    void ReleaseResources();

    // Pipeline pass callback
    void ExecutePass(PassContext& ctx);

    bool m_initialized = false;
    bool m_enabled     = false;  // opt-in: verify baseline first

    ID3D11Device*        m_device  = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    // Screen dimensions
    uint32_t m_screenW = 0;
    uint32_t m_screenH = 0;

    // Settings
    float m_intensity          = 0.8f;   // Overall specular GI intensity (0-3)
    float m_cubemapFallback    = 0.5f;   // Cubemap contribution when SSR fails (0-1)
    float m_fresnelBias        = 0.04f;  // F0 for dielectrics, metals use albedo (0-0.3)
    float m_roughnessThreshold = 0.7f;   // Above this roughness, skip specular (0-1)

    // Output texture (full-res R16G16B16A16_FLOAT)
    ID3D11Texture2D*           m_outputTex = nullptr;
    ID3D11ShaderResourceView*  m_outputSRV = nullptr;
    ID3D11UnorderedAccessView* m_outputUAV = nullptr;

    // Constant buffer
    ID3D11Buffer* m_constantsCB = nullptr;

    // Compute shader
    ID3D11ComputeShader* m_indirectSpecularCS = nullptr;

    // Sampler
    ID3D11SamplerState* m_linearClampSampler = nullptr;

    // Pipeline handle
    PassHandle m_pipelineHandle = 0;

    uint32_t m_frameIndex = 0;
};

} // namespace SB
