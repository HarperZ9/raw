#pragma once
// RawDebugViews.h: Wave 82 C++23 Mirror of RawDebugViews.hlsli
// Canonical 9-Mode Live GPU Debug Render Views (Modes 0..8, Lighting.hlsl flags.w = 40..48)
// + Click-Any-Pixel Buffer/Mesh/Pass Inspector + Fail-Closed Debug View Sample Validator.
// Clean-room MIT, zero external dependencies, MSVC /W4 /WX clean.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace raw::bridge::wave82
{

enum class DebugViewMode : uint32_t
{
    FinalLit            = 0u,
    AlbedoPorosity      = 1u,
    NormalsFilm         = 2u,
    DepthHiz            = 3u,
    MotionDisocclusion  = 4u,
    GiRadianceFroxel    = 5u,
    ShadowCloudTrans    = 6u,
    OverdrawLodTiers    = 7u,
    NavmeshActorProf    = 8u,
    Count               = 9u
};

inline constexpr uint32_t kDebugViewFlagBase = 40u;

// Exact 64-byte 16-byte aligned constant buffer matching RawDebugViews.hlsli::RawDebugViewParams.
struct alignas(16) RawDebugViewCB
{
    uint32_t debugMode        = 0u;
    uint32_t hizMip           = 0u;
    float    depthNear        = 15.0f;
    float    depthFar         = 353840.0f;
    float    heatmapScale[4]  = {1.0f, 1.0f, 16.0f, 0.85f};
    uint32_t flags            = 0u;
    uint32_t pad0[3]          = {0u, 0u, 0u};
    float    inspectorRect[4] = {0.0f, 0.0f, 1920.0f, 1080.0f};
};
static_assert(sizeof(RawDebugViewCB) == 64u, "RawDebugViewCB must be 64 bytes (16B aligned)");

struct DebugPixelSample
{
    float    albedo[3]      = {0.5f, 0.5f, 0.5f};
    float    wetness01      = 0.0f;
    float    worldNormal[3] = {0.0f, 0.0f, 1.0f};
    float    microPerturb01 = 0.0f;
    float    linearDepth    = 100.0f;
    float    rawDepth       = 0.5f;
    float    motionPx[2]    = {0.0f, 0.0f};
    float    disocclusion01 = 0.0f;
    float    giIrradiance[3]= {0.2f, 0.2f, 0.2f};
    float    sunShadow      = 1.0f;
    float    cloudTrans     = 1.0f;
    uint32_t materialClass  = 1u;
    uint32_t pixelX         = 0u;
    uint32_t pixelY         = 0u;
    uint32_t screenW        = 1920u;
    uint32_t screenH        = 1080u;
};

struct PixelInspectorReport
{
    uint32_t    pixelX           = 0u;
    uint32_t    pixelY           = 0u;
    uint32_t    materialClass    = 0u;
    uint32_t    lodTier          = 0u;
    uint32_t    shadowCascadeIdx = 0u;
    float       linearDepth      = 0.0f;
    float       normalLength     = 0.0f;
    float       giLuma           = 0.0f;
    float       wetness01        = 0.0f;
    bool        normalValid      = false;
    bool        depthValid       = false;
    bool        corruptAlarm     = false;
    const char* passTag          = "LightingPass";
};

inline uint32_t EncodeLightingViewFlag(DebugViewMode mode) noexcept
{
    const uint32_t m = static_cast<uint32_t>(mode);
    if (m == 0u || m >= static_cast<uint32_t>(DebugViewMode::Count)) {
        return 0u;
    }
    return kDebugViewFlagBase + m;
}

inline const char* DebugViewName(DebugViewMode mode) noexcept
{
    switch (mode) {
        case DebugViewMode::FinalLit:           return "final_lit";
        case DebugViewMode::AlbedoPorosity:     return "albedo_porosity";
        case DebugViewMode::NormalsFilm:        return "normals_film";
        case DebugViewMode::DepthHiz:           return "depth_hiz";
        case DebugViewMode::MotionDisocclusion: return "motion_disocclusion";
        case DebugViewMode::GiRadianceFroxel:   return "gi_radiance_froxel";
        case DebugViewMode::ShadowCloudTrans:   return "shadow_cloud_trans";
        case DebugViewMode::OverdrawLodTiers:   return "overdraw_lod_tiers";
        case DebugViewMode::NavmeshActorProf:   return "navmesh_actor_profiler";
        default:                                return "unknown";
    }
}

// Click-any-pixel buffer/mesh/pass inspector & fail-closed corruption detector (Planted Control 82-D)
inline PixelInspectorReport InspectPixelBuffer(const DebugPixelSample& s) noexcept
{
    PixelInspectorReport r{};
    r.pixelX        = s.pixelX;
    r.pixelY        = s.pixelY;
    r.materialClass = s.materialClass;
    r.wetness01     = std::isfinite(s.wetness01) ? (std::clamp)(s.wetness01, 0.0f, 1.0f) : 0.0f;

    const bool nFin = std::isfinite(s.worldNormal[0]) && std::isfinite(s.worldNormal[1]) && std::isfinite(s.worldNormal[2]);
    const float nL2 = nFin ? (s.worldNormal[0] * s.worldNormal[0] + s.worldNormal[1] * s.worldNormal[1] + s.worldNormal[2] * s.worldNormal[2]) : 0.0f;
    r.normalLength  = nFin ? std::sqrt((std::max)(nL2, 0.0f)) : 0.0f;
    r.normalValid   = nFin && (nL2 >= 0.25f) && (nL2 <= 2.25f);

    r.depthValid    = std::isfinite(s.linearDepth) && std::isfinite(s.rawDepth) && (s.linearDepth >= 0.0f) && (s.rawDepth >= 0.0f) && (s.rawDepth <= 1.0f);
    r.linearDepth   = r.depthValid ? s.linearDepth : -1.0f;

    const float z = r.depthValid ? s.linearDepth : 0.0f;
    r.lodTier = (s.materialClass == 7u || z >= 5600.0f) ? 3u : (z >= 2400.0f ? 2u : (z >= 900.0f ? 1u : 0u));
    r.shadowCascadeIdx = (z < 650.0f) ? 0u : (z < 2000.0f ? 1u : (z < 5500.0f ? 2u : 3u));

    const bool giFin = std::isfinite(s.giIrradiance[0]) && std::isfinite(s.giIrradiance[1]) && std::isfinite(s.giIrradiance[2]);
    r.giLuma = giFin ? (0.2126f * s.giIrradiance[0] + 0.7152f * s.giIrradiance[1] + 0.0722f * s.giIrradiance[2]) : -1.0f;

    r.passTag = (s.materialClass == 7u) ? "DistantLodPass"
              : (s.materialClass == 10u || s.materialClass == 11u || s.materialClass == 12u || s.materialClass == 14u)
                ? "ActorSkinSssPass" : "DeferredLightingPass";

    r.corruptAlarm = (!r.normalValid) || (!r.depthValid) || (!giFin);
    return r;
}

} // namespace raw::bridge::wave82
