#pragma once
//=============================================================================
//  DynamicCubemapRenderer — Real-time environment cubemap generation
//
//  Replaces Community Shaders' "Dynamic Cubemaps" feature.
//  Skyrim uses static/baked cubemaps for environment reflections on metallic
//  and glass surfaces.  This renderer generates a dynamic cubemap each frame
//  by capturing the scene from the camera position into 6 cube faces.
//
//  Architecture:
//    - 6-face cubemap texture (128x128 per face, R11G11B10_FLOAT)
//    - Renders 1 face per frame (6-frame rotation) for minimal GPU cost
//    - Mip-chain generation for roughness-based reflection lookups
//    - Injected as SRV to replace the game's static cubemap binding
//
//  The cubemap captures the actual rendered scene (post-lighting, pre-ENB)
//  so reflections match the current time of day, weather, and lighting.
//
//  Output: Dynamic cubemap SRV at t30.
//  Registered as PostENB pipeline pass.
//
//  VRAM budget: ~1 MB (128^2 * 6 faces * mip chain * 4 bytes)
//=============================================================================

#include <d3d11.h>
#include <DirectXMath.h>
#include <cstdint>
#include "RenderPipeline.h"

namespace SB
{

class DynamicCubemapRenderer
{
public:
    static DynamicCubemapRenderer& Get()
    {
        static DynamicCubemapRenderer inst;
        return inst;
    }

    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    bool  IsEnabled() const { return m_enabled; }
    void  SetEnabled(bool v) { m_enabled = v; }

    uint32_t GetFaceResolution() const { return m_faceResolution; }
    void     SetFaceResolution(uint32_t v);
    float    GetUpdateFrequency() const { return m_updateFrequency; }
    void     SetUpdateFrequency(float v) { m_updateFrequency = (v < 0.1f) ? 0.1f : (v > 6.0f) ? 6.0f : v; }
    float    GetBlendSpeed() const { return m_blendSpeed; }
    void     SetBlendSpeed(float v) { m_blendSpeed = (v < 0.01f) ? 0.01f : (v > 1.0f) ? 1.0f : v; }

    ID3D11ShaderResourceView* GetCubemapSRV() const { return m_cubemapSRV; }

    static constexpr uint32_t kSRVSlot = 30;  // t30 — dynamic cubemap output

private:
    DynamicCubemapRenderer() = default;

    bool CompileShaders();
    bool CreateResources();
    void ReleaseResources();
    void RebuildCubemap();

    void ExecutePass(PassContext& ctx);
    void CaptureFace(uint32_t faceIndex, PassContext& ctx);
    void GenerateMips();

    bool m_initialized = false;
    bool m_enabled     = false;  // opt-in: writes backbuffer via UAV

    ID3D11Device*        m_device  = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    uint32_t m_screenW = 0;
    uint32_t m_screenH = 0;

    // Settings
    uint32_t m_faceResolution   = 128;    // Per-face resolution
    float    m_updateFrequency  = 1.0f;   // 1.0 = 1 face/frame, 0.5 = 1 face/2 frames
    float    m_blendSpeed       = 0.1f;   // Temporal blend factor per face update

    // Cubemap texture + views
    ID3D11Texture2D*          m_cubemapTex       = nullptr;
    ID3D11ShaderResourceView* m_cubemapSRV       = nullptr;
    ID3D11RenderTargetView*   m_faceRTVs[6]      = {};  // One per face
    ID3D11Texture2D*          m_cubemapStaging    = nullptr;  // Previous frame for blending

    // Downsample/copy shader for capturing backbuffer region
    ID3D11ComputeShader* m_captureCS    = nullptr;
    ID3D11ComputeShader* m_mipGenCS     = nullptr;
    ID3D11Buffer*        m_constantsCB  = nullptr;

    // Per-face view matrices (pre-computed at init)
    DirectX::XMFLOAT4X4 m_faceViewMatrices[6];
    DirectX::XMFLOAT4X4 m_faceProjMatrix;

    // Scheduling
    uint32_t m_currentFace  = 0;   // Which face to update this frame
    uint32_t m_frameIndex   = 0;

    // Pipeline handle
    PassHandle m_pipelineHandle = 0;
};

} // namespace SB
