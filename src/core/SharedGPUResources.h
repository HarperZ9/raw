#pragma once
//=============================================================================
//  SharedGPUResources — Shared GPU textures and buffers for all effects
//
//  Resources:
//    t30 — Blue noise (R8G8B8A8_UNORM, 128x128, 4 decorrelated channels)
//    t31 — Linearized depth (R32_FLOAT, full-res, view-space Z)
//    b7  — Vanilla post-process params CB (ImageSpace data on GPU)
//
//  Blue noise: Generated once at init from R2 quasi-random sequence.
//    Gives spatially-uniform noise for dithering, ray jitter, TAA, etc.
//
//  Linearized depth: Computed once per frame at PostGeometry:2 (after HiZ).
//    Eliminates redundant per-pixel depth linearization in every effect.
//
//  Vanilla params: ImageSpace exposure/saturation/contrast uploaded per frame.
//    Effects can read game tonemapper state for weather-aware compositing.
//=============================================================================

#include <d3d11.h>
#include <cstdint>

namespace SB
{

struct alignas(16) VanillaParamsCBData
{
    // float4 row 0 — HDR
    float eyeAdaptSpeed;
    float bloomScale;
    float bloomThreshold;
    float sunlightScale;

    // float4 row 1 — Cinematic
    float saturation;
    float brightness;
    float contrast;
    float tintAmount;

    // float4 row 2 — Tint color + DOF strength
    float tintR, tintG, tintB;
    float dofStrength;

    // float4 row 3 — DOF params + IMOD
    float dofDistance;
    float dofRange;
    float imodActive;     // 0 or 1
    float imodStrength;
};
static_assert(sizeof(VanillaParamsCBData) == 64, "VanillaParamsCB must be 64 bytes");


class SharedGPUResources
{
public:
    static SharedGPUResources& Get()
    {
        static SharedGPUResources inst;
        return inst;
    }

    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc);
    void Shutdown();

    /// Per-frame: linearize depth + upload vanilla params.
    /// Called as PostGeometry:2 pipeline pass.
    void Update(ID3D11DeviceContext* ctx);

    ID3D11ShaderResourceView* GetBlueNoiseSRV() const { return m_blueNoiseSRV; }
    ID3D11ShaderResourceView* GetLinearDepthSRV() const { return m_linearDepthSRV; }
    ID3D11Buffer*             GetVanillaParamsCB() const { return m_vanillaParamsCB; }

    bool IsInitialized() const { return m_initialized; }

    static constexpr uint32_t kBlueNoiseSlot   = 30;
    static constexpr uint32_t kLinearDepthSlot  = 31;
    static constexpr uint32_t kVanillaParamsCBSlot = 7;   // PS CB b7

private:
    SharedGPUResources() = default;

    bool CreateBlueNoiseTexture(ID3D11Device* dev);
    bool CreateLinearDepthResources(ID3D11Device* dev, uint32_t w, uint32_t h);
    bool CompileLinearizeCS(ID3D11Device* dev);
    bool CreateVanillaParamsCB(ID3D11Device* dev);

    bool m_initialized = false;

    ID3D11Device*        m_device  = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    uint32_t m_width = 0, m_height = 0;

    // Blue noise (static — generated once)
    ID3D11Texture2D*          m_blueNoiseTex = nullptr;
    ID3D11ShaderResourceView* m_blueNoiseSRV = nullptr;

    // Linearized depth (per-frame)
    ID3D11Texture2D*            m_linearDepthTex = nullptr;
    ID3D11ShaderResourceView*   m_linearDepthSRV = nullptr;
    ID3D11UnorderedAccessView*  m_linearDepthUAV = nullptr;
    ID3D11ComputeShader*        m_linearizeCS    = nullptr;
    ID3D11Buffer*               m_linearizeCB    = nullptr;

    // Vanilla params CB
    ID3D11Buffer* m_vanillaParamsCB = nullptr;
};

} // namespace SB
