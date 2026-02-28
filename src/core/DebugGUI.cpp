#include "DebugGUI.h"
#include <imgui.h>
#include <cstring>
#include <cmath>

namespace SB::DebugGUI
{
    // ── Internal state ───────────────────────────────────────────────────
    static AllData s_data{};
    static bool s_initialized = false;
    static bool s_windowOpen = true;
    static bool s_dataPushEnabled = true;

    // Tracker enable flags
    static bool s_enableCelestial = true;
    static bool s_enableAtmosphere = true;
    static bool s_enableFog = true;
    static bool s_enableWeather = true;
    static bool s_enablePlayer = true;
    static bool s_enableCamera = true;
    static bool s_enableInterior = true;
    static bool s_enableShadow = true;
    static bool s_enableEffects = true;
    static bool s_enableRender = true;

    // ── Helper functions ─────────────────────────────────────────────────

    static void DisplayFloat4(const char* label, const Float4& v)
    {
        ImGui::Text("%s: (%.3f, %.3f, %.3f, %.3f)", label, v.x, v.y, v.z, v.w);
    }

    static void DisplayFloat4Color(const char* label, const Float4& v)
    {
        ImVec4 color(v.x, v.y, v.z, 1.0f);
        ImGui::ColorButton(label, color, ImGuiColorEditFlags_NoTooltip, ImVec2(20, 20));
        ImGui::SameLine();
        ImGui::Text("%s: (%.2f, %.2f, %.2f)", label, v.x, v.y, v.z);
    }

    static void DisplayMatrix(const char* label, const Float4x4& m)
    {
        if (ImGui::TreeNode(label)) {
            ImGui::Text("| %8.3f %8.3f %8.3f %8.3f |", m.row[0].x, m.row[0].y, m.row[0].z, m.row[0].w);
            ImGui::Text("| %8.3f %8.3f %8.3f %8.3f |", m.row[1].x, m.row[1].y, m.row[1].z, m.row[1].w);
            ImGui::Text("| %8.3f %8.3f %8.3f %8.3f |", m.row[2].x, m.row[2].y, m.row[2].z, m.row[2].w);
            ImGui::Text("| %8.3f %8.3f %8.3f %8.3f |", m.row[3].x, m.row[3].y, m.row[3].z, m.row[3].w);
            ImGui::TreePop();
        }
    }

    // ── Section renderers ────────────────────────────────────────────────

    static void RenderCelestialSection()
    {
        if (ImGui::CollapsingHeader("Celestial", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Enable##Celestial", &s_enableCelestial);
            if (!s_enableCelestial) {
                ImGui::TextDisabled("(Disabled)");
                return;
            }

            const auto& c = s_data.celestial;

            ImGui::Separator();
            ImGui::Text("Sun:");
            ImGui::Indent();
            DisplayFloat4("NDC Position", c.SunNDC);
            DisplayFloat4("Direction", c.SunDirection);
            DisplayFloat4Color("Color", c.SunColor);
            ImGui::Unindent();

            ImGui::Separator();
            ImGui::Text("Masser:");
            ImGui::Indent();
            DisplayFloat4("NDC Position", c.MasserNDC);
            DisplayFloat4("Direction", c.MasserDirection);
            ImGui::Text("Phase Brightness: %.2f", c.MasserNDC.w);
            ImGui::Unindent();

            ImGui::Separator();
            ImGui::Text("Secunda:");
            ImGui::Indent();
            DisplayFloat4("NDC Position", c.SecundaNDC);
            DisplayFloat4("Direction", c.SecundaDirection);
            ImGui::Text("Phase Brightness: %.2f", c.SecundaNDC.w);
            ImGui::Unindent();

            ImGui::Separator();
            ImGui::Text("Time: %.2f hrs (%.1f%% of day)", c.TimeData.x, c.TimeData.w * 100.0f);
            ImGui::Text("Sunrise: %.2f, Sunset: %.2f", c.TimeData.y, c.TimeData.z);
        }
    }

    static void RenderAtmosphereSection()
    {
        if (ImGui::CollapsingHeader("Atmosphere")) {
            ImGui::Checkbox("Enable##Atmosphere", &s_enableAtmosphere);
            if (!s_enableAtmosphere) {
                ImGui::TextDisabled("(Disabled)");
                return;
            }

            const auto& a = s_data.atmosphere;

            DisplayFloat4Color("Sky Upper", a.SkyUpper);
            DisplayFloat4Color("Sky Lower", a.SkyLower);
            DisplayFloat4Color("Horizon", a.Horizon);
            DisplayFloat4Color("Ambient", a.Ambient);
            DisplayFloat4Color("Sunlight", a.SunlightColor);
            DisplayFloat4Color("Cloud LOD Diffuse", a.CloudLODDiffuse);
            DisplayFloat4Color("Cloud LOD Ambient", a.CloudLODAmbient);
            DisplayFloat4Color("Effect Lighting", a.EffectLighting);
        }
    }

    static void RenderFogSection()
    {
        if (ImGui::CollapsingHeader("Fog")) {
            ImGui::Checkbox("Enable##Fog", &s_enableFog);
            if (!s_enableFog) {
                ImGui::TextDisabled("(Disabled)");
                return;
            }

            const auto& f = s_data.fog;

            DisplayFloat4Color("Near Color", f.NearColor);
            ImGui::Text("Near Distance: %.1f", f.NearColor.w);
            DisplayFloat4Color("Far Color", f.FarColor);
            ImGui::Text("Far Distance: %.1f", f.FarColor.w);
            ImGui::Text("Power: %.2f, Max: %.2f", f.Density.x, f.Density.y);
            ImGui::Text("Is Interior: %s", f.Density.z > 0.5f ? "Yes" : "No");

            ImGui::Separator();
            ImGui::Text("Height Fog:");
            ImGui::Text("  Water Z: %.1f", f.HeightFog.x);
            ImGui::Text("  Player Z: %.1f", f.HeightFog.y);
            ImGui::Text("  Density: %.4f, Falloff: %.4f", f.HeightFog.z, f.HeightFog.w);
        }
    }

    static void RenderWeatherSection()
    {
        if (ImGui::CollapsingHeader("Weather")) {
            ImGui::Checkbox("Enable##Weather", &s_enableWeather);
            if (!s_enableWeather) {
                ImGui::TextDisabled("(Disabled)");
                return;
            }

            const auto& w = s_data.weather;

            ImGui::Text("Wind Speed: %.2f", w.Wind.x);
            ImGui::Text("Wind Direction: %.2f rad", w.Wind.y);

            ImGui::Separator();
            const char* precipType = "None";
            if (w.Precipitation.x >= 2.0f) precipType = "Snow";
            else if (w.Precipitation.x >= 1.0f) precipType = "Rain";
            ImGui::Text("Precipitation: %s (%.0f%%)", precipType, w.Precipitation.y * 100.0f);

            ImGui::Separator();
            ImGui::Text("Lightning Frequency: %.2f", w.Lightning.x);
            ImGui::Text("Time Since Flash: %.2f sec", w.Lightning.w);

            ImGui::Separator();
            ImGui::Text("Weather Flags:");
            ImGui::Text("  Pleasant: %s", w.Flags.x > 0.5f ? "Yes" : "No");
            ImGui::Text("  Cloudy: %s", w.Flags.y > 0.5f ? "Yes" : "No");
            ImGui::Text("  Rainy: %s", w.Flags.z > 0.5f ? "Yes" : "No");
            ImGui::Text("  Snowy: %s", w.Flags.w > 0.5f ? "Yes" : "No");

            ImGui::Separator();
            ImGui::Text("Transition: %.0f%%", w.Transition.x * 100.0f);
            ImGui::Text("Last Weather ID: %.0f", w.Transition.y);
            ImGui::Text("Current Weather ID: %.0f", w.Transition.z);
        }
    }

    static void RenderPlayerSection()
    {
        if (ImGui::CollapsingHeader("Player")) {
            ImGui::Checkbox("Enable##Player", &s_enablePlayer);
            if (!s_enablePlayer) {
                ImGui::TextDisabled("(Disabled)");
                return;
            }

            const auto& p = s_data.player;

            ImGui::Text("Position: (%.1f, %.1f, %.1f)", p.Position.x, p.Position.y, p.Position.z);
            ImGui::Text("Altitude above water: %.1f", p.Position.w);

            ImGui::Separator();
            ImGui::Text("Vitals:");
            ImGui::ProgressBar(p.Vitals.x, ImVec2(-1, 0), "Health");
            ImGui::ProgressBar(p.Vitals.y, ImVec2(-1, 0), "Stamina");
            ImGui::ProgressBar(p.Vitals.z, ImVec2(-1, 0), "Magicka");
            ImGui::Text("Level: %.0f", p.Vitals.w);

            ImGui::Separator();
            ImGui::Text("Movement:");
            ImGui::Text("  Speed: %.2f", p.Movement.x);
            ImGui::Text("  Sprinting: %s", p.Movement.y > 0.5f ? "Yes" : "No");
            ImGui::Text("  Swimming: %s", p.Movement.z > 0.5f ? "Yes" : "No");
            ImGui::Text("  Mounted: %s", p.Movement.w > 0.5f ? "Yes" : "No");

            ImGui::Separator();
            ImGui::Text("Combat:");
            ImGui::Text("  In Combat: %s", p.Combat.x > 0.5f ? "Yes" : "No");
            ImGui::Text("  Bleeding Out: %s", p.Combat.y > 0.5f ? "Yes" : "No");
            ImGui::Text("  Kill Cam: %s", p.Combat.z > 0.5f ? "Yes" : "No");
            ImGui::Text("  Weapon Drawn: %s", p.Combat.w > 0.5f ? "Yes" : "No");

            ImGui::Separator();
            ImGui::Text("Water:");
            ImGui::Text("  Underwater: %s", p.Water.x > 0.5f ? "Yes" : "No");
            ImGui::Text("  Water Height: %.1f", p.Water.y);
            ImGui::Text("  Submersion Depth: %.1f", p.Water.z);
            ImGui::Text("  Wading: %s", p.Water.w > 0.5f ? "Yes" : "No");
        }
    }

    static void RenderCameraSection()
    {
        if (ImGui::CollapsingHeader("Camera")) {
            ImGui::Checkbox("Enable##Camera", &s_enableCamera);
            if (!s_enableCamera) {
                ImGui::TextDisabled("(Disabled)");
                return;
            }

            const auto& c = s_data.camera;

            ImGui::Text("World Position: (%.1f, %.1f, %.1f)", c.WorldPos.x, c.WorldPos.y, c.WorldPos.z);
            ImGui::Text("Pitch: %.2f rad, Yaw: %.2f rad", c.Angles.x, c.Angles.y);
            ImGui::Text("Camera State: %.0f", c.Angles.z);

            ImGui::Separator();
            ImGui::Text("FOV: %.1f deg", c.Info.x);
            ImGui::Text("Near: %.2f, Far: %.1f", c.Info.y, c.Info.z);
            ImGui::Text("Aspect Ratio: %.3f", c.Info.w);

            ImGui::Separator();
            DisplayMatrix("View Matrix", c.ViewMatrix);
            DisplayMatrix("Projection Matrix", c.ProjMatrix);
            DisplayMatrix("ViewProj Matrix", c.ViewProjMatrix);
            DisplayMatrix("Inverse ViewProj", c.InvViewProj);
            DisplayMatrix("Previous ViewProj", c.PrevViewProj);
        }
    }

    static void RenderInteriorSection()
    {
        if (ImGui::CollapsingHeader("Interior")) {
            ImGui::Checkbox("Enable##Interior", &s_enableInterior);
            if (!s_enableInterior) {
                ImGui::TextDisabled("(Disabled)");
                return;
            }

            const auto& i = s_data.interior;

            bool isInterior = i.IsInterior.x > 0.5f;
            ImGui::Text("Is Interior: %s", isInterior ? "Yes" : "No");

            if (isInterior) {
                bool hasLighting = i.IsInterior.y > 0.5f;
                ImGui::Text("Has Lighting Data: %s", hasLighting ? "Yes" : "No");

                if (hasLighting) {
                    ImGui::Separator();
                    DisplayFloat4Color("Ambient", i.AmbientColor);
                    DisplayFloat4Color("Directional", i.DirectionalColor);
                    ImGui::Text("Directional Fade: %.2f", i.DirectionalColor.w);
                    DisplayFloat4("Directional Dir", i.DirectionalDir);

                    ImGui::Separator();
                    DisplayFloat4Color("Interior Fog", i.InteriorFogColor);
                    ImGui::Text("Fog Near: %.1f, Far: %.1f", i.InteriorFogDist.x, i.InteriorFogDist.y);
                    ImGui::Text("Fog Power: %.2f, Clip: %.1f", i.InteriorFogDist.z, i.InteriorFogDist.w);
                }
            }
        }
    }

    static void RenderShadowSection()
    {
        if (ImGui::CollapsingHeader("Shadow")) {
            ImGui::Checkbox("Enable##Shadow", &s_enableShadow);
            if (!s_enableShadow) {
                ImGui::TextDisabled("(Disabled)");
                return;
            }

            const auto& s = s_data.shadow;

            DisplayFloat4("Light Direction", s.LightDirection);
            ImGui::Text("Shadow Intensity: %.2f", s.LightDirection.w);
            DisplayFloat4Color("Light Diffuse", s.LightDiffuse);
            DisplayFloat4Color("Light Ambient", s.LightAmbient);
        }
    }

    static void RenderEffectsSection()
    {
        if (ImGui::CollapsingHeader("Effects")) {
            ImGui::Checkbox("Enable##Effects", &s_enableEffects);
            if (!s_enableEffects) {
                ImGui::TextDisabled("(Disabled)");
                return;
            }

            const auto& e = s_data.effects;

            ImGui::Text("Vision Effects:");
            ImGui::Text("  Night Eye: %s", e.VisionEffects.x > 0.5f ? "Active" : "No");
            ImGui::Text("  Detect Life: %s", e.VisionEffects.y > 0.5f ? "Active" : "No");
            ImGui::Text("  Ethereal: %s", e.VisionEffects.w > 0.5f ? "Active" : "No");

            ImGui::Separator();
            ImGui::Text("Time Effects:");
            ImGui::Text("  Slow Time Factor: %.2f", e.TimeEffects.x);

            ImGui::Separator();
            ImGui::Text("Damage Effects:");
            ImGui::Text("  Fire: %s", e.DamageEffects.x > 0.5f ? "Active" : "No");
            ImGui::Text("  Frost: %s", e.DamageEffects.y > 0.5f ? "Active" : "No");
            ImGui::Text("  Shock: %s", e.DamageEffects.z > 0.5f ? "Active" : "No");
            ImGui::Text("  Poison: %s", e.DamageEffects.w > 0.5f ? "Active" : "No");

            ImGui::Separator();
            ImGui::Text("Misc Effects:");
            ImGui::Text("  Invisible: %s", e.MiscEffects.x > 0.5f ? "Active" : "No");
            ImGui::Text("  Paralyzed: %s", e.MiscEffects.y > 0.5f ? "Active" : "No");
        }
    }

    static void RenderRenderSection()
    {
        if (ImGui::CollapsingHeader("Render Info")) {
            ImGui::Checkbox("Enable##Render", &s_enableRender);
            if (!s_enableRender) {
                ImGui::TextDisabled("(Disabled)");
                return;
            }

            const auto& r = s_data.render;

            ImGui::Text("Frame: %.0f", r.FrameInfo.x);
            ImGui::Text("Delta Time: %.4f sec (%.1f FPS)", r.FrameInfo.y, 1.0f / r.FrameInfo.y);
            ImGui::Text("Resolution: %.0f x %.0f", r.FrameInfo.z, r.FrameInfo.w);

            ImGui::Separator();
            ImGui::Text("TAA Jitter Index: %.0f", r.Jitter.z);
            ImGui::Text("Jitter Offset: (%.4f, %.4f)", r.Jitter.x, r.Jitter.y);
        }
    }

    // ── Public interface ─────────────────────────────────────────────────

    void Init()
    {
        s_initialized = true;
        s_windowOpen = true;
        s_dataPushEnabled = true;
    }

    void Shutdown()
    {
        s_initialized = false;
    }

    void Render()
    {
        if (!s_initialized)
            return;

        ImGui::SetNextWindowSize(ImVec2(400, 600), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("SkyrimBridge Debug", &s_windowOpen, ImGuiWindowFlags_None)) {
            // Global controls
            ImGui::Checkbox("Enable Data Push", &s_dataPushEnabled);
            ImGui::SameLine();
            if (ImGui::Button("Collapse All")) {
                ImGui::GetStateStorage()->Clear();
            }

            ImGui::Separator();

            // Data sections
            RenderCelestialSection();
            RenderAtmosphereSection();
            RenderFogSection();
            RenderWeatherSection();
            RenderPlayerSection();
            RenderCameraSection();
            RenderInteriorSection();
            RenderShadowSection();
            RenderEffectsSection();
            RenderRenderSection();
        }
        ImGui::End();
    }

    void SetData(const AllData& a_data)
    {
        s_data = a_data;
    }

    bool IsDataPushEnabled()
    {
        return s_dataPushEnabled;
    }

    bool IsTrackerEnabled(const char* a_name)
    {
        if (std::strcmp(a_name, "celestial") == 0)  return s_enableCelestial;
        if (std::strcmp(a_name, "atmosphere") == 0) return s_enableAtmosphere;
        if (std::strcmp(a_name, "fog") == 0)        return s_enableFog;
        if (std::strcmp(a_name, "weather") == 0)    return s_enableWeather;
        if (std::strcmp(a_name, "player") == 0)     return s_enablePlayer;
        if (std::strcmp(a_name, "camera") == 0)     return s_enableCamera;
        if (std::strcmp(a_name, "interior") == 0)   return s_enableInterior;
        if (std::strcmp(a_name, "shadow") == 0)     return s_enableShadow;
        if (std::strcmp(a_name, "effects") == 0)    return s_enableEffects;
        if (std::strcmp(a_name, "render") == 0)     return s_enableRender;
        return true;
    }

    void SetDataPushEnabled(bool a_enabled)
    {
        s_dataPushEnabled = a_enabled;
    }
}
