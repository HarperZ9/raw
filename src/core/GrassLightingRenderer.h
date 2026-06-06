#pragma once
//=============================================================================
//  GrassLightingRenderer — Corrected grass/vegetation lighting
//
//  Replaces Community Shaders' "Grass Lighting" feature.
//  Skyrim's BSGrassShader uses simplified lighting that ignores most scene
//  lights and has incorrect ambient contribution.  This renderer applies
//  a screen-space correction pass after the main render:
//
//    1. Read MaterialClassifier output (t25) to identify vegetation pixels
//    2. Read G-buffer normals (RT2) for per-pixel lighting
//    3. Sample ClusteredLighting data (t20-t22) for multi-light contribution
//    4. Composite corrected lighting over vegetation pixels
//
//  Output: Corrected backbuffer (in-place composite via UAV).
//  Registered as PostGeometry pipeline pass.
//
//  VRAM budget: ~2 MB at 1920x1080 (CB only, reads existing SRVs)
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include "RenderPipeline.h"

namespace SB
{

class GrassLightingRenderer
{
public:
    static GrassLightingRenderer& Get()
    {
        static GrassLightingRenderer inst;
        return inst;
    }

    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    bool  IsEnabled() const { return m_enabled; }
    void  SetEnabled(bool v) { m_enabled = v; }

    // Lighting correction parameters
    float GetAmbientBoost() const { return m_ambientBoost; }
    void  SetAmbientBoost(float v) { m_ambientBoost = v; }
    float GetSubsurfaceStrength() const { return m_subsurfaceStrength; }
    void  SetSubsurfaceStrength(float v) { m_subsurfaceStrength = v; }
    float GetMultiLightIntensity() const { return m_multiLightIntensity; }
    void  SetMultiLightIntensity(float v) { m_multiLightIntensity = v; }
    float GetWindSway() const { return m_windSway; }
    void  SetWindSway(float v) { m_windSway = v; }

private:
    GrassLightingRenderer() = default;

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

    // Lighting correction settings
    float m_ambientBoost        = 0.15f;   // Extra ambient for grass (vanilla is too dark)
    float m_subsurfaceStrength  = 0.35f;   // Translucency / back-lit scattering
    float m_multiLightIntensity = 1.0f;    // Clustered light contribution scale
    float m_windSway            = 0.5f;    // Wind-driven normal perturbation

    // Compute shader
    ID3D11ComputeShader* m_grassLightCS = nullptr;
    ID3D11Buffer*        m_constantsCB  = nullptr;

    // UAV for in-place backbuffer write
    ID3D11UnorderedAccessView* m_backbufferUAV = nullptr;

    // Pipeline handle
    PassHandle m_pipelineHandle = 0;
    uint32_t   m_frameIndex     = 0;
};

} // namespace SB
