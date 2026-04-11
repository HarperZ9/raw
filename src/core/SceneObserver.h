#pragma once
//=============================================================================
//  SceneObserver.h — Render pipeline observation via BSShader hooks
//
//  Tier 1: Reads BSShaderManager::State each frame (no hooks)
//  Tier 2: Material type counting via SetupMaterial vtable hook
//  Tier 3a: Hooks BSLightingShader::SetupMaterial (vtable) to read per-draw
//           material properties and aggregate specular/roughness/flags
//
//  Pure observation — no D3D11 hooks, no UAV/SRV/CB binding, no DXBC patching.
//  All data delivered via ENBSetParameter like everything else.
//=============================================================================

#include "BridgeData.h"
#include <cstdint>

namespace SB
{
    // Material categories (same classification as the removed MaterialTracker)
    enum class SceneMaterial : uint8_t
    {
        General     = 0,
        Skin        = 1,
        Hair        = 2,
        Eye         = 3,
        MetalGlossy = 4,
        Terrain     = 5,
        Vegetation  = 6,
        Emissive    = 7,
        Snow        = 8,
        Count
    };

    class SceneObserver
    {
    public:
        static SceneObserver& Get();

        // Install hooks. Call once at kDataLoaded.
        // All hooks are safe vtable replacements (no inline detours).
        // Tier 2: material counting via SetupMaterial vtable hook
        // Tier 3a: BSLightingShader::SetupMaterial vtable hook
        bool Install();
        bool IsInstalled() const { return m_installed; }

        // Called from OnENBFrame: snapshots previous frame's counts,
        // reads BSShaderManager::State, resets counters, returns data.
        SceneData Update();

        // Per-frame statistics (read after Update)
        uint32_t GetTotalDrawCalls() const { return m_prevTotalDraws; }
        uint32_t GetLightingDrawCalls() const { return m_prevLightingDraws; }
        const uint32_t* GetMaterialCounts() const { return m_prevMaterialCounts; }

        // Called by SetupMaterial vtable hook (same thread, no sync needed)
        void OnSetupMaterial(float specPower, float specScale, float subSurfaceRolloff,
                             float rimLightPower, float materialAlpha, float envMapScale,
                             uint32_t featureFlags);

        // Called by SetupGeometry vtable hook on BSLightingShader
        void OnSetupGeometry(uint8_t numLights, uint32_t passEnum, uint8_t lodMode);

        // Called by SetupMaterial vtable hook on BSWaterShader
        void OnWaterSetupMaterial(float shallowR, float shallowG, float shallowB,
                                  float alpha, float sunSpecPower, float reflectionAmount,
                                  float refractionMag, float fresnelAmount,
                                  float displacementDamp, float flowmapScale,
                                  float aboveFogFar, float underwaterFogFar,
                                  float planeNx, float planeNy, float planeNz, float planeD);

        // Called by SetupMaterial vtable hook on BSEffectShader
        void OnEffectSetupMaterial(float baseR, float baseG, float baseB, float baseA,
                                   float baseColorScale, float softFalloffDepth,
                                   float falloffStartOpacity, float falloffStopOpacity);

        static SceneMaterial ClassifyTechnique(uint32_t technique);
        static SceneMaterial ClassifyFeature(uint32_t feature);

    private:
        SceneObserver() = default;

        bool InstallSetupMaterialHook();
        bool InstallSetupGeometryHook();
        bool InstallWaterSetupMaterialHook();
        bool InstallEffectSetupMaterialHook();

        bool m_installed = false;
        bool m_setupMaterialHooked = false;
        bool m_setupGeometryHooked = false;
        bool m_waterSetupMaterialHooked = false;
        bool m_effectSetupMaterialHooked = false;

        // ── Tier 2: Draw call counters ──────────────────────────────────
        uint32_t m_materialCounts[static_cast<int>(SceneMaterial::Count)] = {};
        uint32_t m_totalDrawCalls = 0;
        uint32_t m_lightingDrawCalls = 0;

        uint32_t m_prevMaterialCounts[static_cast<int>(SceneMaterial::Count)] = {};
        uint32_t m_prevTotalDraws = 0;
        uint32_t m_prevLightingDraws = 0;

        // ── Tier 3a: Material property accumulators ─────────────────────
        // Accumulated per-frame, then averaged in Update()
        float    m_sumSpecPower = 0.0f;
        float    m_sumSpecScale = 0.0f;
        float    m_sumSubSurface = 0.0f;
        float    m_sumRimLight = 0.0f;
        float    m_sumEnvMapScale = 0.0f;
        float    m_sumMaterialAlpha = 0.0f;
        uint32_t m_matPropCount = 0;          // total lighting draws with material read

        // Skin-specific specular (for SSS approximation)
        float    m_sumSkinSpecPower = 0.0f;
        uint32_t m_skinDrawCount = 0;

        // Shader property flag fractions
        uint32_t m_envMapCount = 0;
        uint32_t m_glowMapCount = 0;
        uint32_t m_backLitCount = 0;
        uint32_t m_softLitCount = 0;

        // Previous frame snapshots
        float m_prevAvgSpecPower = 0.0f;
        float m_prevAvgSpecScale = 0.0f;
        float m_prevAvgSubSurface = 0.0f;
        float m_prevAvgRimLight = 0.0f;
        float m_prevAvgEnvMapScale = 0.0f;
        float m_prevAvgMaterialAlpha = 0.0f;
        float m_prevSkinSpecPower = 0.0f;
        float m_prevEnvMapFrac = 0.0f;
        float m_prevGlowMapFrac = 0.0f;
        float m_prevBackLitFrac = 0.0f;
        float m_prevSoftLitFrac = 0.0f;

        // ── Tier B: SetupGeometry accumulators ──────────────────────────
        uint32_t m_sumLightsPerDraw = 0;
        uint32_t m_maxLightsPerDraw = 0;
        uint32_t m_sumPassEnum = 0;
        uint32_t m_sumLODMode = 0;
        uint32_t m_geomCallCount = 0;

        // ── Tier B: Water shader accumulators ───────────────────────────
        // Stores latest water material data (not averaged — one water surface active at a time)
        Float4 m_latestWaterPlane{};
        Float4 m_latestWaterColor{};
        Float4 m_latestWaterParams{};
        Float4 m_latestWaterWave{};
        bool   m_waterUpdatedThisFrame = false;

        // ── Tier B: Effect shader accumulators ──────────────────────────
        float    m_sumEffectBaseR = 0.0f, m_sumEffectBaseG = 0.0f, m_sumEffectBaseB = 0.0f, m_sumEffectBaseA = 0.0f;
        float    m_sumEffectColorScale = 0.0f;
        float    m_sumEffectSoftFalloff = 0.0f;
        float    m_sumEffectFalloffOpacity = 0.0f;
        uint32_t m_effectDrawCount = 0;
    };

} // namespace SB
