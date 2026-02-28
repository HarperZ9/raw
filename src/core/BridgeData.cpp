#include "BridgeData.h"
#include <cstddef>

//=============================================================================
//  BridgeData.cpp — Parameter name table for ENB push
//
//  Each entry maps a Float4 in AllData to the string name the shader uses.
//  The push loop reads: for each entry, memcpy 16 bytes from AllData + offset,
//  call ENBSetParameter with the name string.
//
//  For Float4x4 (matrices), we push 4 consecutive Float4 rows.
//=============================================================================

#define ENTRY(field, name) \
    { name, offsetof(SB::AllData, field) }

namespace SB
{
    const ParamEntry kParamTable[] = {
        // ── Celestial ───────────────────────────────────────────────────
        ENTRY(celestial.SunNDC,             "SB_Sun_NDC"),
        ENTRY(celestial.SunDirection,       "SB_Sun_Direction"),
        ENTRY(celestial.SunColor,           "SB_Sun_Color"),
        ENTRY(celestial.MasserNDC,          "SB_Masser_NDC"),
        ENTRY(celestial.MasserDirection,    "SB_Masser_Direction"),
        ENTRY(celestial.SecundaNDC,         "SB_Secunda_NDC"),
        ENTRY(celestial.SecundaDirection,   "SB_Secunda_Direction"),
        ENTRY(celestial.TimeData,           "SB_Time"),
        // Convenience aliases for shaders using simplified naming
        ENTRY(celestial.SunNDC,             "SB_Celestial_Sun"),
        ENTRY(celestial.MasserNDC,          "SB_Celestial_Moon"),

        // ── Atmosphere ──────────────────────────────────────────────────
        ENTRY(atmosphere.SkyUpper,          "SB_Atmos_SkyUpper"),
        ENTRY(atmosphere.SkyLower,          "SB_Atmos_SkyLower"),
        ENTRY(atmosphere.Horizon,           "SB_Atmos_Horizon"),
        ENTRY(atmosphere.Ambient,           "SB_Atmos_Ambient"),
        ENTRY(atmosphere.SunlightColor,     "SB_Atmos_Sunlight"),
        ENTRY(atmosphere.CloudLODDiffuse,   "SB_Atmos_CloudDiffuse"),
        ENTRY(atmosphere.CloudLODAmbient,   "SB_Atmos_CloudAmbient"),
        ENTRY(atmosphere.EffectLighting,    "SB_Atmos_EffectLight"),

        // ── Fog ─────────────────────────────────────────────────────────
        ENTRY(fog.NearColor,                "SB_Fog_NearColor"),
        ENTRY(fog.FarColor,                 "SB_Fog_FarColor"),
        ENTRY(fog.Density,                  "SB_Fog_Density"),
        ENTRY(fog.HeightFog,                "SB_Fog_Height"),

        // ── Weather ─────────────────────────────────────────────────────
        ENTRY(weather.Wind,                 "SB_Wind"),
        ENTRY(weather.Precipitation,        "SB_Precipitation"),
        ENTRY(weather.Lightning,            "SB_Lightning"),
        ENTRY(weather.Flags,                "SB_Weather_Flags"),
        ENTRY(weather.Transition,           "SB_Weather_Transition"),

        // ── Player ──────────────────────────────────────────────────────
        ENTRY(player.Position,              "SB_Player_Position"),
        ENTRY(player.Vitals,                "SB_Player_Vitals"),
        ENTRY(player.Movement,              "SB_Player_Movement"),
        ENTRY(player.Combat,                "SB_Player_Combat"),
        ENTRY(player.Water,                 "SB_Player_Water"),

        // ── Camera scalar data ──────────────────────────────────────────
        ENTRY(camera.Info,                  "SB_Camera_Info"),
        ENTRY(camera.Angles,                "SB_Camera_Angles"),
        ENTRY(camera.WorldPos,              "SB_Camera_WorldPos"),

        // ── Camera matrices (each is 4 rows) ───────────────────────────
        ENTRY(camera.ViewMatrix.row[0],     "SB_View_Row0"),
        ENTRY(camera.ViewMatrix.row[1],     "SB_View_Row1"),
        ENTRY(camera.ViewMatrix.row[2],     "SB_View_Row2"),
        ENTRY(camera.ViewMatrix.row[3],     "SB_View_Row3"),

        ENTRY(camera.ProjMatrix.row[0],     "SB_Proj_Row0"),
        ENTRY(camera.ProjMatrix.row[1],     "SB_Proj_Row1"),
        ENTRY(camera.ProjMatrix.row[2],     "SB_Proj_Row2"),
        ENTRY(camera.ProjMatrix.row[3],     "SB_Proj_Row3"),

        ENTRY(camera.ViewProjMatrix.row[0], "SB_ViewProj_Row0"),
        ENTRY(camera.ViewProjMatrix.row[1], "SB_ViewProj_Row1"),
        ENTRY(camera.ViewProjMatrix.row[2], "SB_ViewProj_Row2"),
        ENTRY(camera.ViewProjMatrix.row[3], "SB_ViewProj_Row3"),

        ENTRY(camera.PrevViewProj.row[0],   "SB_PrevVP_Row0"),
        ENTRY(camera.PrevViewProj.row[1],   "SB_PrevVP_Row1"),
        ENTRY(camera.PrevViewProj.row[2],   "SB_PrevVP_Row2"),
        ENTRY(camera.PrevViewProj.row[3],   "SB_PrevVP_Row3"),

        ENTRY(camera.InvViewProj.row[0],    "SB_InvVP_Row0"),
        ENTRY(camera.InvViewProj.row[1],    "SB_InvVP_Row1"),
        ENTRY(camera.InvViewProj.row[2],    "SB_InvVP_Row2"),
        ENTRY(camera.InvViewProj.row[3],    "SB_InvVP_Row3"),

        // ── Interior ────────────────────────────────────────────────────
        ENTRY(interior.IsInterior,          "SB_Interior_Flags"),
        ENTRY(interior.AmbientColor,        "SB_Interior_Ambient"),
        ENTRY(interior.DirectionalColor,    "SB_Interior_DirColor"),
        ENTRY(interior.DirectionalDir,      "SB_Interior_DirDir"),
        ENTRY(interior.InteriorFogColor,    "SB_Interior_FogColor"),
        ENTRY(interior.InteriorFogDist,     "SB_Interior_FogDist"),

        // ── Shadow/Directional ──────────────────────────────────────────
        ENTRY(shadow.LightDirection,        "SB_Shadow_Direction"),
        ENTRY(shadow.LightDiffuse,          "SB_Shadow_Diffuse"),
        ENTRY(shadow.LightAmbient,          "SB_Shadow_Ambient"),

        // ── Active Effects ──────────────────────────────────────────────
        ENTRY(effects.VisionEffects,        "SB_FX_Vision"),
        ENTRY(effects.TimeEffects,          "SB_FX_Time"),
        ENTRY(effects.DamageEffects,        "SB_FX_Damage"),
        ENTRY(effects.MiscEffects,          "SB_FX_Misc"),

        // ── Render State ────────────────────────────────────────────────
        ENTRY(render.FrameInfo,             "SB_Render_Frame"),
        ENTRY(render.Jitter,                "SB_Render_Jitter"),
    };

    const std::size_t kParamCount = sizeof(kParamTable) / sizeof(kParamTable[0]);
}

#undef ENTRY
