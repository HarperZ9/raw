#pragma once
//=============================================================================
//  SRVInjector.h — Bind compute output textures into shader passes
//
//  Registers SRVs at specific t-slots (t17+) and samplers at s-slots.
//  Called once per frame before the render pipeline runs.
//
//  Slot allocation (must match ARCHITECTURE.md):
//    t17  — LuminanceHistogram (R32_FLOAT 256x1)
//    t18  — LUTManager (R8G8B8A8 64^3 Texture3D)
//    t19  — HiZPyramid (R32_FLOAT mipped, reversed-Z)
//    t20  — GTAORenderer (AO output)
//    t21  — ClusteredLighting (cluster grid)
//    t22  — TAAManager (temporal history)
//    t25  — MaterialClassifier (material IDs)
//    t26  — SSGIRenderer (GI output)
//    t27  — SSRRenderer (reflections)
//    t28  — ContactShadowRenderer (shadow mask)
//    t29  — SkylightingRenderer (sky visibility)
//    t30  — SharedGPUResources (blue noise, 128x128)
//    t31  — SharedGPUResources (linearized depth, R32_FLOAT)
//    t32  — IndirectSpecularRenderer (specular GI)
//    t33  — VolumetricLightingRenderer (scatter + transmittance)
//    t34  — DynamicCubemapRenderer (environment cubemap)
//    t35  — ClusteredLighting (light buffer)
//    t36  — ClusteredLighting (light index list)
//    t37  — VolumetricClouds (cloud scatter + transmittance)
//    t38  — AtmosphereRenderer (celestial bodies)
//    s2   — Trilinear clamp sampler (for LUT)
//    s3   — TAA history sampler
//=============================================================================

#include <d3d11.h>

namespace SB
{

class SRVInjector
{
public:
    static SRVInjector& Get();

    // Initialize (just stores the context pointer)
    bool Initialize(ID3D11DeviceContext* ctx);
    bool IsInitialized() const { return m_context != nullptr; }

    // Register/unregister SRVs for PS injection
    void RegisterSRV(UINT slot, ID3D11ShaderResourceView* srv);
    void UnregisterSRV(UINT slot);

    // Register/unregister samplers for PS injection
    void RegisterSampler(UINT slot, ID3D11SamplerState* sampler);
    void UnregisterSampler(UINT slot);

    // Bind all registered SRVs and samplers to the PS stage.
    // Call this per frame, before the render pipeline processes shader passes.
    void InjectAll();

    // Clear all PS-bound SRVs at registered slots (call in HookedPresent cleanup)
    void ClearAll();

    // Render pass state tracking
    void SetPassActive(bool active) { m_passActive = active; }
    bool IsPassActive() const { return m_passActive; }

private:
    SRVInjector() = default;

    ID3D11DeviceContext* m_context = nullptr;
    bool m_passActive = false;

    static constexpr UINT kMaxSlots = 48;  // Covers slots t0-t47 (current max is t38)
    ID3D11ShaderResourceView*  m_srvs[kMaxSlots] = {};
    bool                       m_srvActive[kMaxSlots] = {};
    ID3D11SamplerState*        m_samplers[kMaxSlots] = {};
    bool                       m_samplerActive[kMaxSlots] = {};
};

} // namespace SB
