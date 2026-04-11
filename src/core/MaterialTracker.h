#pragma once
//=============================================================================
//  MaterialTracker.h — BSShader::BeginTechnique hook for material ID tracking
//
//  Hooks the game's BSShader::BeginTechnique function to extract the
//  BSLightingShader technique ID per draw call. This provides ground-truth
//  material classification (skin, hair, metal, terrain, etc.) that feeds
//  into the material ID texture for ENB shader consumption.
//
//  Phase 2 of ENB-only material-aware rendering pipeline.
//=============================================================================

#include <cstdint>

namespace SB
{
    // Material types derived from BSLightingShader technique IDs.
    // These map directly to the engine's shader technique selection.
    enum class MaterialType : uint8_t
    {
        General     = 0,  // Default / unknown
        Skin        = 1,  // Facegen, FacegenRGBTint (techniques 4, 5)
        Hair        = 2,  // Hair (technique 6)
        Eye         = 3,  // Eye (technique 16)
        MetalGlossy = 4,  // Envmap, MultilayerParallax (techniques 1, 11)
        Terrain     = 5,  // MTLand, LODLand, variants (techniques 8, 9, 18, 19)
        Vegetation  = 6,  // TreeAnim (technique 12)
        Emissive    = 7,  // Glowmap (technique 2)
        Snow        = 8,  // Snow-covered variants
        Count
    };

    // Shared hook state — file-scope in MaterialTracker.cpp sets these,
    // MaterialTracker class reads them. All single-threaded (render thread).
    namespace detail
    {
        inline MaterialType g_currentMaterial  = MaterialType::General;
        inline uint32_t     g_currentTechnique = 0;
        inline uint32_t     g_hookCallCount    = 0;
        inline uint32_t     g_lightingCallCount = 0;
        // True when BeginTechnique was last called for BSLightingShader.
        // Used by DXBCPatcher to know when to bind UAV / swap shaders.
        inline bool         g_inLightingPass   = false;
    }

    class MaterialTracker
    {
    public:
        static MaterialTracker& Get();

        // Install the BeginTechnique hook. Call once during kDataLoaded.
        bool Install();

        // Current material being rendered (set by the hook each draw call)
        MaterialType GetCurrentMaterial() const { return detail::g_currentMaterial; }

        // Raw technique ID from last BSLightingShader call
        uint32_t GetCurrentTechnique() const { return detail::g_currentTechnique; }

        // Statistics
        uint32_t GetHookCallCount() const { return detail::g_hookCallCount; }
        uint32_t GetLightingCallCount() const { return detail::g_lightingCallCount; }
        bool IsInstalled() const { return m_installed; }

        // Classify a BSLightingShader technique ID to a MaterialType
        static MaterialType ClassifyTechnique(uint32_t a_technique);

    private:
        MaterialTracker() = default;
        bool m_installed = false;
    };

} // namespace SB
