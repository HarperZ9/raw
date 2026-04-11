#pragma once
//=============================================================================
//  TreeLODLightingRenderer — Corrected tree LOD lighting
//
//  Replaces Community Shaders' "Tree LOD Lighting" feature.
//  Skyrim's BSDistantTreeShader uses flat ambient lighting with no directional
//  contribution, causing LOD trees to look washed out or too dark compared
//  to full-detail trees.  This renderer applies a screen-space correction:
//
//    1. Read MaterialClassifier output (t25) to identify tree LOD pixels
//    2. Read G-buffer normals for per-pixel directional lighting
//    3. Apply atmosphere LUT (t23/t24) for correct ambient color
//    4. Add directional sun contribution from ShadowTracker data
//    5. Composite corrected lighting over tree LOD pixels
//
//  Output: Corrected backbuffer (in-place composite via UAV).
//  Registered as PostGeometry pipeline pass.
//
//  VRAM budget: ~2 MB at 1920x1080 (CB only)
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include "RenderPipeline.h"

namespace SB
{

class TreeLODLightingRenderer
{
public:
    static TreeLODLightingRenderer& Get()
    {
        static TreeLODLightingRenderer inst;
        return inst;
    }

    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    bool  IsEnabled() const { return m_enabled; }
    void  SetEnabled(bool v) { m_enabled = v; }

    float GetAmbientMatchStrength() const { return m_ambientMatchStrength; }
    void  SetAmbientMatchStrength(float v) { m_ambientMatchStrength = v; }
    float GetDirectionalStrength() const { return m_directionalStrength; }
    void  SetDirectionalStrength(float v) { m_directionalStrength = v; }
    float GetColorMatchBlend() const { return m_colorMatchBlend; }
    void  SetColorMatchBlend(float v) { m_colorMatchBlend = v; }

private:
    TreeLODLightingRenderer() = default;

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

    // Correction settings
    float m_ambientMatchStrength = 0.8f;   // How strongly to match atmosphere ambient
    float m_directionalStrength  = 0.6f;   // Sun directional light contribution
    float m_colorMatchBlend      = 0.5f;   // Blend toward full-tree color matching

    // Compute shader
    ID3D11ComputeShader* m_treeLodCS   = nullptr;
    ID3D11Buffer*        m_constantsCB = nullptr;

    // UAV for in-place backbuffer write
    ID3D11UnorderedAccessView* m_backbufferUAV = nullptr;

    // Pipeline handle
    PassHandle m_pipelineHandle = 0;
    uint32_t   m_frameIndex     = 0;
};

} // namespace SB
