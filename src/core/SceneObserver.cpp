//=============================================================================
//  SceneObserver.cpp — Render pipeline observation
//
//  Tier 1: Reads BSShaderManager::State singleton (ambient specular,
//          character light, water intersection, shader timers)
//  Tier 2: Material type counting via BSLightingShader::SetupMaterial vtable hook
//          (no inline detours — all hooks are safe vtable replacements)
//  Tier 3a: Hooks BSLightingShader::SetupMaterial via vtable to read per-draw
//           material properties (specular, roughness, subsurface, flags)
//=============================================================================

#include "SceneObserver.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <REL/Relocation.h>
#include <cstring>
#include <Windows.h>

namespace SB
{
    // ── Hook internals ──────────────────────────────────────────────────

    namespace
    {
        using SetupMaterial_t = void(__fastcall*)(RE::BSShader*, const RE::BSShaderMaterial*);
        SetupMaterial_t s_originalSetupMaterial = nullptr;

        using SetupGeometry_t = void(__fastcall*)(RE::BSShader*, RE::BSRenderPass*, uint32_t);
        SetupGeometry_t s_originalSetupGeometry = nullptr;

        SetupMaterial_t s_originalWaterSetupMaterial = nullptr;
        SetupMaterial_t s_originalEffectSetupMaterial = nullptr;

        // Pointer back to the singleton so hooks can update counters
        SceneObserver* s_observer = nullptr;

        void __fastcall HookedSetupMaterial(
            RE::BSShader*               a_shader,
            const RE::BSShaderMaterial* a_material)
        {
            // SEH: catches access violations from corrupted material pointers
            __try {
                if (s_observer && a_shader && a_material &&
                    a_shader->shaderType == 6 &&
                    a_material->GetType() == RE::BSShaderMaterial::Type::kLighting)
                {
                    auto* lightMat = static_cast<const RE::BSLightingShaderMaterialBase*>(a_material);

                    float specPower    = lightMat->specularPower;
                    float specScale    = lightMat->specularColorScale;
                    float subSurface   = lightMat->subSurfaceLightRolloff;
                    float rimLight     = lightMat->rimLightPower;
                    float matAlpha     = lightMat->materialAlpha;

                    float envMapScale = 0.0f;
                    auto feature = lightMat->GetFeature();
                    if (feature == RE::BSShaderMaterial::Feature::kEnvironmentMap) {
                        auto* envMat = static_cast<const RE::BSLightingShaderMaterialEnvmap*>(lightMat);
                        envMapScale = envMat->envMapScale;
                    } else if (feature == RE::BSShaderMaterial::Feature::kEye) {
                        auto* eyeMat = static_cast<const RE::BSLightingShaderMaterialEye*>(lightMat);
                        envMapScale = eyeMat->envMapScale;
                    }

                    uint32_t featureFlags = static_cast<uint32_t>(feature);
                    s_observer->OnSetupMaterial(specPower, specScale, subSurface, rimLight, matAlpha, envMapScale, featureFlags);
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}

            __try {
                s_originalSetupMaterial(a_shader, a_material);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                s_observer = nullptr;
            }
        }

        void __fastcall HookedSetupGeometry(
            RE::BSShader*     a_shader,
            RE::BSRenderPass* a_pass,
            uint32_t          a_flags)
        {
            __try {
                if (s_observer && a_shader && a_pass) {
                    s_observer->OnSetupGeometry(a_pass->numLights, a_pass->passEnum, a_pass->LODMode.index);
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}

            __try {
                s_originalSetupGeometry(a_shader, a_pass, a_flags);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                s_observer = nullptr;
            }
        }

        void __fastcall HookedWaterSetupMaterial(
            RE::BSShader*               a_shader,
            const RE::BSShaderMaterial* a_material)
        {
            __try {
                if (s_observer && a_material) {
                    auto* wm = static_cast<const RE::BSWaterShaderMaterial*>(a_material);
                    s_observer->OnWaterSetupMaterial(
                        wm->shallowWaterColor.red, wm->shallowWaterColor.green, wm->shallowWaterColor.blue,
                        wm->alpha,
                        wm->sunSpecularPower, wm->reflectionAmount,
                        wm->refractionMagnitude, wm->fresnelAmount,
                        wm->displacementDampener, wm->flowmapScale,
                        wm->aboveWaterFogDistFar, wm->underwaterFogDistFar,
                        wm->plane.normal.x, wm->plane.normal.y, wm->plane.normal.z, wm->plane.constant);
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}

            __try {
                s_originalWaterSetupMaterial(a_shader, a_material);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                s_observer = nullptr;
            }
        }

        void __fastcall HookedEffectSetupMaterial(
            RE::BSShader*               a_shader,
            const RE::BSShaderMaterial* a_material)
        {
            __try {
                if (s_observer && a_material) {
                    auto* em = static_cast<const RE::BSEffectShaderMaterial*>(a_material);
                    s_observer->OnEffectSetupMaterial(
                        em->baseColor.red, em->baseColor.green, em->baseColor.blue, em->baseColor.alpha,
                        em->baseColorScale, em->softFalloffDepth,
                        em->falloffStartOpacity, em->falloffStopOpacity);
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}

            __try {
                s_originalEffectSetupMaterial(a_shader, a_material);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                s_observer = nullptr;
            }
        }
    }

    // ── SceneObserver implementation ────────────────────────────────────

    SceneObserver& SceneObserver::Get()
    {
        static SceneObserver instance;
        return instance;
    }

    bool SceneObserver::Install()
    {
        if (m_installed)
            return true;

        s_observer = this;

        bool t3a = InstallSetupMaterialHook();
        bool t3b_geom = InstallSetupGeometryHook();
        bool t3b_water = InstallWaterSetupMaterialHook();
        bool t3b_effect = InstallEffectSetupMaterialHook();

        m_installed = t3a;  // SetupMaterial vtable hook is the minimum for "installed"

        if (t3a)
            SKSE::log::info("SceneObserver: Tier 2+3a active — material counting + property aggregation");
        if (t3b_geom)
            SKSE::log::info("SceneObserver: Tier B active — SetupGeometry per-draw light counts");
        if (t3b_water)
            SKSE::log::info("SceneObserver: Tier B active — BSWaterShader material observation");
        if (t3b_effect)
            SKSE::log::info("SceneObserver: Tier B active — BSEffectShader material observation");

        return m_installed;
    }

    bool SceneObserver::InstallSetupMaterialHook()
    {
        // BSLightingShader vtable — SetupMaterial is virtual index 4
        // BSShader vtable layout:
        //   0: dtor, 1: DeleteThis, 2: SetupTechnique, 3: RestoreTechnique,
        //   4: SetupMaterial, 5: RestoreMaterial, 6: SetupGeometry, ...
        constexpr std::size_t kSetupMaterialIdx = 4;

        REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_BSLightingShader[0] };

        if (!vtable.address()) {
            SKSE::log::warn("SceneObserver: failed to resolve BSLightingShader vtable");
            return false;
        }

        auto original = vtable.write_vfunc(kSetupMaterialIdx,
            reinterpret_cast<std::uintptr_t>(&HookedSetupMaterial));
        s_originalSetupMaterial = reinterpret_cast<SetupMaterial_t>(original);

        m_setupMaterialHooked = true;
        SKSE::log::info("SceneObserver: SetupMaterial vtable hook installed (Tier 3a) at vtable {:#X}",
            vtable.address());
        return true;
    }

    bool SceneObserver::InstallSetupGeometryHook()
    {
        constexpr std::size_t kSetupGeometryIdx = 6;
        REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_BSLightingShader[0] };
        if (!vtable.address()) {
            SKSE::log::warn("SceneObserver: failed to resolve BSLightingShader vtable for SetupGeometry");
            return false;
        }
        auto original = vtable.write_vfunc(kSetupGeometryIdx,
            reinterpret_cast<std::uintptr_t>(&HookedSetupGeometry));
        s_originalSetupGeometry = reinterpret_cast<SetupGeometry_t>(original);
        m_setupGeometryHooked = true;
        SKSE::log::info("SceneObserver: SetupGeometry vtable hook installed (Tier B) at vtable {:#X}",
            vtable.address());
        return true;
    }

    bool SceneObserver::InstallWaterSetupMaterialHook()
    {
        constexpr std::size_t kSetupMaterialIdx = 4;
        REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_BSWaterShader[0] };
        if (!vtable.address()) {
            SKSE::log::warn("SceneObserver: failed to resolve BSWaterShader vtable");
            return false;
        }
        auto original = vtable.write_vfunc(kSetupMaterialIdx,
            reinterpret_cast<std::uintptr_t>(&HookedWaterSetupMaterial));
        s_originalWaterSetupMaterial = reinterpret_cast<SetupMaterial_t>(original);
        m_waterSetupMaterialHooked = true;
        SKSE::log::info("SceneObserver: BSWaterShader::SetupMaterial hook installed (Tier B) at vtable {:#X}",
            vtable.address());
        return true;
    }

    bool SceneObserver::InstallEffectSetupMaterialHook()
    {
        constexpr std::size_t kSetupMaterialIdx = 4;
        REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_BSEffectShader[0] };
        if (!vtable.address()) {
            SKSE::log::warn("SceneObserver: failed to resolve BSEffectShader vtable");
            return false;
        }
        auto original = vtable.write_vfunc(kSetupMaterialIdx,
            reinterpret_cast<std::uintptr_t>(&HookedEffectSetupMaterial));
        s_originalEffectSetupMaterial = reinterpret_cast<SetupMaterial_t>(original);
        m_effectSetupMaterialHooked = true;
        SKSE::log::info("SceneObserver: BSEffectShader::SetupMaterial hook installed (Tier B) at vtable {:#X}",
            vtable.address());
        return true;
    }

    void SceneObserver::OnSetupMaterial(float specPower, float specScale, float subSurfaceRolloff,
                                        float rimLightPower, float materialAlpha, float envMapScale,
                                        uint32_t featureFlags)
    {
        // Tier 2: draw counting + material classification (replaces removed BeginTechnique hook)
        m_lightingDrawCalls++;
        m_totalDrawCalls++;
        m_materialCounts[static_cast<int>(ClassifyFeature(featureFlags))]++;

        m_sumSpecPower += specPower;
        m_sumSpecScale += specScale;
        m_sumSubSurface += subSurfaceRolloff;
        m_sumRimLight += rimLightPower;
        m_sumMaterialAlpha += materialAlpha;
        m_sumEnvMapScale += envMapScale;
        m_matPropCount++;

        // Track skin-specific specular (FaceGen / FaceGenRGBTint)
        auto feature = static_cast<RE::BSShaderMaterial::Feature>(featureFlags);
        if (feature == RE::BSShaderMaterial::Feature::kFaceGen ||
            feature == RE::BSShaderMaterial::Feature::kFaceGenRGBTint) {
            m_sumSkinSpecPower += specPower;
            m_skinDrawCount++;
        }

        // Track shader property flag fractions
        if (feature == RE::BSShaderMaterial::Feature::kEnvironmentMap)
            m_envMapCount++;
        if (feature == RE::BSShaderMaterial::Feature::kGlowMap)
            m_glowMapCount++;
        // BackLighting and SoftLighting are flags on BSShaderProperty, not material Feature.
        // We approximate by tracking material features that typically use these.
    }

    void SceneObserver::OnSetupGeometry(uint8_t numLights, uint32_t passEnum, uint8_t lodMode)
    {
        m_sumLightsPerDraw += numLights;
        if (numLights > m_maxLightsPerDraw)
            m_maxLightsPerDraw = numLights;
        m_sumPassEnum += passEnum;
        m_sumLODMode += lodMode;
        m_geomCallCount++;
    }

    void SceneObserver::OnWaterSetupMaterial(float shallowR, float shallowG, float shallowB,
                                              float alpha, float sunSpecPower, float reflectionAmount,
                                              float refractionMag, float fresnelAmount,
                                              float displacementDamp, float flowmapScale,
                                              float aboveFogFar, float underwaterFogFar,
                                              float planeNx, float planeNy, float planeNz, float planeD)
    {
        m_totalDrawCalls++;
        m_latestWaterPlane  = { planeNx, planeNy, planeNz, planeD };
        m_latestWaterColor  = { shallowR, shallowG, shallowB, alpha };
        m_latestWaterParams = { sunSpecPower, reflectionAmount, refractionMag, fresnelAmount };
        m_latestWaterWave   = { displacementDamp, flowmapScale, aboveFogFar, underwaterFogFar };
        m_waterUpdatedThisFrame = true;
    }

    void SceneObserver::OnEffectSetupMaterial(float baseR, float baseG, float baseB, float baseA,
                                               float baseColorScale, float softFalloffDepth,
                                               float falloffStartOpacity, float falloffStopOpacity)
    {
        m_totalDrawCalls++;
        m_sumEffectBaseR += baseR;
        m_sumEffectBaseG += baseG;
        m_sumEffectBaseB += baseB;
        m_sumEffectBaseA += baseA;
        m_sumEffectColorScale += baseColorScale;
        m_sumEffectSoftFalloff += softFalloffDepth;
        m_sumEffectFalloffOpacity += (falloffStartOpacity + falloffStopOpacity) * 0.5f;
        m_effectDrawCount++;
    }

    SceneMaterial SceneObserver::ClassifyTechnique(uint32_t technique)
    {
        switch (technique) {
        case 4: case 5:             return SceneMaterial::Skin;
        case 6:                     return SceneMaterial::Hair;
        case 16:                    return SceneMaterial::Eye;
        case 1: case 11: case 14:   return SceneMaterial::MetalGlossy;
        case 8: case 9: case 18: case 19: return SceneMaterial::Terrain;
        case 12:                    return SceneMaterial::Vegetation;
        case 2:                     return SceneMaterial::Emissive;
        default:                    return SceneMaterial::General;
        }
    }

    SceneMaterial SceneObserver::ClassifyFeature(uint32_t feature)
    {
        // Maps BSShaderMaterial::Feature to the same categories as ClassifyTechnique
        using F = RE::BSShaderMaterial::Feature;
        switch (static_cast<F>(feature)) {
        case F::kFaceGen:
        case F::kFaceGenRGBTint:    return SceneMaterial::Skin;
        case F::kHairTint:          return SceneMaterial::Hair;
        case F::kEye:               return SceneMaterial::Eye;
        case F::kEnvironmentMap:    return SceneMaterial::MetalGlossy;
        case F::kMultiTexLand:
        case F::kLODLand:           return SceneMaterial::Terrain;
        case F::kTreeAnim:          return SceneMaterial::Vegetation;
        case F::kGlowMap:           return SceneMaterial::Emissive;
        default:                    return SceneMaterial::General;
        }
    }

    SceneData SceneObserver::Update()
    {
        SceneData out{};

        // ── Tier 2: Snapshot material counts from previous frame ─────────
        std::memcpy(m_prevMaterialCounts, m_materialCounts, sizeof(m_materialCounts));
        m_prevTotalDraws = m_totalDrawCalls;
        m_prevLightingDraws = m_lightingDrawCalls;

        // Reset for next frame
        std::memset(m_materialCounts, 0, sizeof(m_materialCounts));
        m_totalDrawCalls = 0;
        m_lightingDrawCalls = 0;

        // Material counts as fractions of total lighting draws
        float invTotal = m_prevLightingDraws > 0 ? 1.0f / static_cast<float>(m_prevLightingDraws) : 0.0f;

        out.MaterialCounts1.x = m_prevMaterialCounts[static_cast<int>(SceneMaterial::General)]     * invTotal;
        out.MaterialCounts1.y = m_prevMaterialCounts[static_cast<int>(SceneMaterial::Skin)]        * invTotal;
        out.MaterialCounts1.z = m_prevMaterialCounts[static_cast<int>(SceneMaterial::Terrain)]     * invTotal;
        out.MaterialCounts1.w = m_prevMaterialCounts[static_cast<int>(SceneMaterial::Vegetation)]  * invTotal;

        out.MaterialCounts2.x = m_prevMaterialCounts[static_cast<int>(SceneMaterial::Hair)]        * invTotal;
        out.MaterialCounts2.y = m_prevMaterialCounts[static_cast<int>(SceneMaterial::Eye)]         * invTotal;
        out.MaterialCounts2.z = m_prevMaterialCounts[static_cast<int>(SceneMaterial::Snow)]        * invTotal;
        out.MaterialCounts2.w = m_prevMaterialCounts[static_cast<int>(SceneMaterial::Emissive)]    * invTotal;

        out.DrawStats.x = static_cast<float>(m_prevTotalDraws);
        out.DrawStats.y = static_cast<float>(m_prevLightingDraws);
        out.DrawStats.z = m_prevMaterialCounts[static_cast<int>(SceneMaterial::MetalGlossy)] * invTotal;
        out.DrawStats.w = 0.0f; // reserved

        // ── Tier 3a: Snapshot material property averages ─────────────────
        if (m_matPropCount > 0) {
            float inv = 1.0f / static_cast<float>(m_matPropCount);
            m_prevAvgSpecPower = m_sumSpecPower * inv;
            m_prevAvgSpecScale = m_sumSpecScale * inv;
            m_prevAvgSubSurface = m_sumSubSurface * inv;
            m_prevAvgRimLight = m_sumRimLight * inv;
            m_prevAvgEnvMapScale = m_sumEnvMapScale * inv;
            m_prevAvgMaterialAlpha = m_sumMaterialAlpha * inv;
        } else {
            m_prevAvgSpecPower = 0.0f;
            m_prevAvgSpecScale = 0.0f;
            m_prevAvgSubSurface = 0.0f;
            m_prevAvgRimLight = 0.0f;
            m_prevAvgEnvMapScale = 0.0f;
            m_prevAvgMaterialAlpha = 1.0f;
        }

        m_prevSkinSpecPower = m_skinDrawCount > 0 ? m_sumSkinSpecPower / static_cast<float>(m_skinDrawCount) : 0.0f;

        float lightInv = m_matPropCount > 0 ? 1.0f / static_cast<float>(m_matPropCount) : 0.0f;
        m_prevEnvMapFrac = m_envMapCount * lightInv;
        m_prevGlowMapFrac = m_glowMapCount * lightInv;
        m_prevBackLitFrac = m_backLitCount * lightInv;
        m_prevSoftLitFrac = m_softLitCount * lightInv;

        // Reset Tier 3a accumulators
        m_sumSpecPower = 0.0f;
        m_sumSpecScale = 0.0f;
        m_sumSubSurface = 0.0f;
        m_sumRimLight = 0.0f;
        m_sumEnvMapScale = 0.0f;
        m_sumMaterialAlpha = 0.0f;
        m_matPropCount = 0;
        m_sumSkinSpecPower = 0.0f;
        m_skinDrawCount = 0;
        m_envMapCount = 0;
        m_glowMapCount = 0;
        m_backLitCount = 0;
        m_softLitCount = 0;

        // Pack into float4s
        out.MaterialProps1.x = m_prevAvgSpecPower;
        out.MaterialProps1.y = m_prevAvgSpecScale;
        out.MaterialProps1.z = m_prevAvgSpecPower > 0.001f ? 1.0f / m_prevAvgSpecPower : 1.0f;  // roughness approximation
        out.MaterialProps1.w = m_prevAvgSubSurface;

        out.MaterialProps2.x = m_prevAvgRimLight;
        out.MaterialProps2.y = m_prevAvgEnvMapScale;
        out.MaterialProps2.z = m_prevAvgMaterialAlpha;
        out.MaterialProps2.w = m_prevSkinSpecPower;

        out.ShaderFlags.x = m_prevEnvMapFrac;
        out.ShaderFlags.y = m_prevGlowMapFrac;
        out.ShaderFlags.z = m_prevBackLitFrac;
        out.ShaderFlags.w = m_prevSoftLitFrac;

        // ── Tier B: SetupGeometry — per-draw light info ──────────────────
        if (m_geomCallCount > 0) {
            float inv = 1.0f / static_cast<float>(m_geomCallCount);
            out.GeometryInfo.x = static_cast<float>(m_sumLightsPerDraw) * inv;
            out.GeometryInfo.y = static_cast<float>(m_maxLightsPerDraw);
            out.GeometryInfo.z = static_cast<float>(m_sumPassEnum) * inv;
            out.GeometryInfo.w = static_cast<float>(m_sumLODMode) * inv;
        }
        m_sumLightsPerDraw = 0;
        m_maxLightsPerDraw = 0;
        m_sumPassEnum = 0;
        m_sumLODMode = 0;
        m_geomCallCount = 0;

        // ── Tier B: Water shader observation ────────────────────────────
        if (m_waterUpdatedThisFrame) {
            out.WaterPlane  = m_latestWaterPlane;
            out.WaterColor  = m_latestWaterColor;
            out.WaterParams = m_latestWaterParams;
            out.WaterWave   = m_latestWaterWave;
        }
        m_waterUpdatedThisFrame = false;

        // ── Tier B: Effect shader observation ───────────────────────────
        if (m_effectDrawCount > 0) {
            float inv = 1.0f / static_cast<float>(m_effectDrawCount);
            out.EffectShader.x = static_cast<float>(m_effectDrawCount);
            out.EffectShader.y = m_sumEffectColorScale * inv;
            out.EffectShader.z = m_sumEffectSoftFalloff * inv;
            out.EffectShader.w = m_sumEffectFalloffOpacity * inv;
            out.EffectColor.x = m_sumEffectBaseR * inv;
            out.EffectColor.y = m_sumEffectBaseG * inv;
            out.EffectColor.z = m_sumEffectBaseB * inv;
            out.EffectColor.w = m_sumEffectBaseA * inv;
        }
        m_sumEffectBaseR = 0.0f; m_sumEffectBaseG = 0.0f;
        m_sumEffectBaseB = 0.0f; m_sumEffectBaseA = 0.0f;
        m_sumEffectColorScale = 0.0f;
        m_sumEffectSoftFalloff = 0.0f;
        m_sumEffectFalloffOpacity = 0.0f;
        m_effectDrawCount = 0;

        // ── Tier 1: Read BSShaderManager::State singleton ───────────────
        auto& shaderState = RE::BSShaderManager::State::GetSingleton();

        out.CharLight.x = shaderState.characterLightEnabled ? 1.0f : 0.0f;
        out.CharLight.y = shaderState.characterLightParams[0];  // Primary
        out.CharLight.z = shaderState.characterLightParams[1];  // Secondary
        out.CharLight.w = shaderState.characterLightParams[2];  // Luminance

        out.AmbientSpec.x = shaderState.ambientSpecular.red;
        out.AmbientSpec.y = shaderState.ambientSpecular.green;
        out.AmbientSpec.z = shaderState.ambientSpecular.blue;
        out.AmbientSpec.w = shaderState.ambientSpecularEnabled ? 1.0f : 0.0f;

        // ── Tier A: Expanded BSShaderManager::State reads ──────────────
        out.EngineState.x = shaderState.interior ? 1.0f : 0.0f;
        out.EngineState.y = static_cast<float>(shaderState.cameraInWaterState);
        out.EngineState.z = shaderState.waterIntersect;
        out.EngineState.w = static_cast<float>(shaderState.currentShaderTechnique);

        out.EngineTimers.x = shaderState.timerValues[RE::BSShaderManager::kDefault];
        out.EngineTimers.y = shaderState.timerValues[RE::BSShaderManager::kDelta];
        out.EngineTimers.z = shaderState.timerValues[RE::BSShaderManager::kSystem];
        out.EngineTimers.w = shaderState.timerValues[RE::BSShaderManager::kRealDelta];

        // ── Sky directional ambient (3D ambient lighting) ──────────────
        auto* sky = RE::Sky::GetSingleton();
        if (sky) {
            // directionalAmbientColors[axis][0=max, 1=min]
            // Pack as: .rgb = positive direction color, .w = negative direction luminance
            auto lum = [](const RE::NiColor& c) { return c.red * 0.2126f + c.green * 0.7152f + c.blue * 0.0722f; };

            out.DirAmbient1.x = sky->directionalAmbientColors[0][0].red;
            out.DirAmbient1.y = sky->directionalAmbientColors[0][0].green;
            out.DirAmbient1.z = sky->directionalAmbientColors[0][0].blue;
            out.DirAmbient1.w = lum(sky->directionalAmbientColors[0][1]);

            out.DirAmbient2.x = sky->directionalAmbientColors[1][0].red;
            out.DirAmbient2.y = sky->directionalAmbientColors[1][0].green;
            out.DirAmbient2.z = sky->directionalAmbientColors[1][0].blue;
            out.DirAmbient2.w = lum(sky->directionalAmbientColors[1][1]);

            out.DirAmbient3.x = sky->directionalAmbientColors[2][0].red;
            out.DirAmbient3.y = sky->directionalAmbientColors[2][0].green;
            out.DirAmbient3.z = sky->directionalAmbientColors[2][0].blue;
            out.DirAmbient3.w = lum(sky->directionalAmbientColors[2][1]);

            // Sun glare + light counts from ShadowSceneNode
            if (sky->sun) {
                out.SunGlare.x = sky->sun->glareScale;
                out.SunGlare.y = sky->sun->doOcclusionTests ? 1.0f : 0.0f;
            }
        }

        // Active light + shadow caster counts from ShadowSceneNode
        auto* ssn = shaderState.shadowSceneNode[0];
        if (ssn) {
            auto& rt = ssn->GetRuntimeData();
            out.SunGlare.z = static_cast<float>(rt.activeLights.size());
            out.SunGlare.w = static_cast<float>(rt.shadowCasterLights.size());
        }

        return out;
    }

} // namespace SB
