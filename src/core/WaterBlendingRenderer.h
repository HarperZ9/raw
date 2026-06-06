#pragma once
//=============================================================================
//  WaterBlendingRenderer — Water surface enhancement
//
//  Replaces Community Shaders' "Water Blending" feature.
//  Skyrim's BSWaterShader has hard edges at water-terrain boundaries and
//  lacks caustic effects.  This renderer applies screen-space corrections:
//
//    1. Read MaterialClassifier output (t25) to identify water pixels
//    2. Read depth buffer for water-terrain edge detection
//    3. Apply soft depth-based edge blending at water boundaries
//    4. Add screen-space caustic patterns using animated noise
//    5. Correct water color using SceneObserver water material data
//
//  Output: Corrected backbuffer (in-place composite via UAV).
//  Registered as PostGeometry pipeline pass.
//
//  VRAM budget: ~4 MB at 1920x1080 (noise texture + CB)
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include "RenderPipeline.h"

namespace SB
{

class WaterBlendingRenderer
{
public:
    static WaterBlendingRenderer& Get()
    {
        static WaterBlendingRenderer inst;
        return inst;
    }

    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    bool  IsEnabled() const { return m_enabled; }
    void  SetEnabled(bool v) { m_enabled = v; }

    float GetEdgeBlendWidth() const { return m_edgeBlendWidth; }
    void  SetEdgeBlendWidth(float v) { m_edgeBlendWidth = v; }
    float GetCausticIntensity() const { return m_causticIntensity; }
    void  SetCausticIntensity(float v) { m_causticIntensity = v; }
    float GetCausticScale() const { return m_causticScale; }
    void  SetCausticScale(float v) { m_causticScale = v; }
    float GetDepthFogStrength() const { return m_depthFogStrength; }
    void  SetDepthFogStrength(float v) { m_depthFogStrength = v; }

private:
    WaterBlendingRenderer() = default;

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

    // Water correction settings
    float m_edgeBlendWidth    = 0.5f;    // World-space units for soft edge blending
    float m_causticIntensity  = 0.25f;   // Underwater caustic pattern brightness
    float m_causticScale      = 0.02f;   // Caustic pattern world-space scale
    float m_depthFogStrength  = 0.8f;    // Underwater depth fog contribution

    // Compute shader
    ID3D11ComputeShader* m_waterBlendCS = nullptr;
    ID3D11Buffer*        m_constantsCB  = nullptr;

    // Noise texture for caustics (128x128 R8_UNORM, tiling)
    ID3D11Texture2D*          m_noiseTex    = nullptr;
    ID3D11ShaderResourceView* m_noiseSRV    = nullptr;

    // UAV for in-place backbuffer write
    ID3D11UnorderedAccessView* m_backbufferUAV = nullptr;

    // Pipeline handle
    PassHandle m_pipelineHandle = 0;
    uint32_t   m_frameIndex     = 0;
};

} // namespace SB
